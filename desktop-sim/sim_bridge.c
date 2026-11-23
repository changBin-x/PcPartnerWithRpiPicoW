/**
 * sim_bridge.c — PC 桌面仿真桥接 DLL。
 *
 * 链接的渲染/解析/状态机/农历/时间换算全部是 pico_freertos_firmware 的
 * 原始源码（零改动），仅由本文件做三件事：
 *   1. 提供 C ABI 供 Python ctypes 调用；
 *   2. 用内存 DisplayBackend 收固件渲染出的 RGB565 切片，转 RGB888 缓冲；
 *   3. 用本机墙钟代替 NTP / 用注入数据代替高德 HTTPS 天气（界面口径不变）。
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <windows.h>

#include "asset_store.h"
#include "lunar_service.h"
#include "metrics_parser.h"
#include "ntp_service.h"
#include "st7735s_driver.h"
#include "system_state.h"
#include "theme_switch.h"
#include "ui_render.h"
#include "weather_service.h"

#ifdef _WIN32
#define SIM_EXPORT __declspec(dllexport)
#else
#define SIM_EXPORT
#endif

static uint16_t s_frame[kDisplayWidth * kDisplayHeight];
static unsigned char s_rgb[kDisplayWidth * kDisplayHeight * 3];

static long long s_fed_packets = 0;
static long long s_invalid_packets = 0;
static int s_last_second = -1;

/* ---------------------------------------------------------------- 内存后端 */

static bool mem_begin(DisplayBackend* self) {
  (void)self;
  return true;
}

static bool mem_write(DisplayBackend* self, int y, int height,
                      const uint16_t* pixels, size_t count) {
  (void)self;
  if (y < 0 || y + height > kDisplayHeight) {
    return false;
  }
  memcpy(&s_frame[y * kDisplayWidth], pixels, count * sizeof(uint16_t));
  return true;
}

static void mem_end(DisplayBackend* self) { (void)self; }

/* ---------------------------------------------------------------- 导出接口 */

SIM_EXPORT int sim_init(void) {
  if (!asset_store_init()) {
    fprintf(stderr, "[sim] 资源初始化失败（需设置 PICO_ASSETS_DIR）\n");
    return -1;
  }
  if (!ui_render_init()) {
    fprintf(stderr, "[sim] 字库初始化失败\n");
    return -2;
  }
  const AssetBlob* lunar_blob = asset_store_lunar_table();
  if (lunar_blob == NULL) {
    fprintf(stderr, "[sim] 农历表缺失\n");
    return -3;
  }
  lunar_service_init_from_memory(lunar_blob->data, lunar_blob->size);

  system_state_init();
  system_state_set_theme(kThemeSwiss);

  /* WiFi 真实路径是 wifi_manager 异步回调；仿真视为已连接。 */
  system_state_set_wifi_status(true);

  /* 天气不再注入：与真机开机一致，valid=false 显示「天气暂不可用」，
   * 由 Python 侧高德 HTTP 线程经 sim_feed_weather_json() 填入真实数据。 */
  return 0;
}

/**
 * 高德 IP 定位 JSON → 6 位 adcode（复用固件 weather_service_parse_ip_json）。
 * 成功返回 1。
 */
SIM_EXPORT int sim_ip_json_to_adcode(const char* json, char* out,
                                     unsigned int size) {
  return weather_service_parse_ip_json(json, out, (size_t)size) ? 1 : 0;
}

/**
 * 高德实况天气 JSON → 状态机（复用固件 weather_service_parse_json，
 * 内含 city/weather/temperature 提取与 valid/is_stale 规则）。
 * 成功返回 0。
 */
SIM_EXPORT int sim_feed_weather_json(const char* json, unsigned int length) {
  (void)length;
  WeatherData weather;
  if (!weather_service_parse_json(json, &weather)) {
    return -1;
  }
  system_state_update_weather(&weather);
  return 0;
}

/** 长时间拉取失败后把天气标记为过期（与固件 weather_mark_stale 同义）。 */
SIM_EXPORT void sim_mark_weather_stale(void) {
  SystemSnapshot snap;
  system_state_get_snapshot(&snap);
  WeatherData stale = snap.weather;
  if (!stale.valid) {
    return;
  }
  stale.is_stale = true;
  system_state_update_weather(&stale);
}

/** 喂一个「UDP 报文」：走固件真实 metrics_parser（含全部校验与丢包规则）。 */
SIM_EXPORT int sim_feed_packet(const unsigned char* data, unsigned int length) {
  static MetricsParser parser;
  static int parser_ready = 0;
  if (!parser_ready) {
    metrics_parser_init(&parser);
    parser_ready = 1;
  }
  PcMetricsData metrics;
  const MetricsParseResult result =
      metrics_parser_parse(&parser, (const char*)data, (size_t)length, &metrics);
  if (result != kMetricsParseOk) {
    ++s_invalid_packets;
    return (int)result;
  }
  /* 与固件 udp_listener_task 相同：合法包带当前毫秒 tick 写入状态机。 */
  system_state_update_metrics(&metrics, (uint32_t)GetTickCount64());
  ++s_fed_packets;
  return 0;
}

/**
 * 每一帧调用（约 20 ms 一次）：
 *  - 秒变化时用真实 ntp_service 换算函数推进墙钟（UTC+8）；
 *  - 日期变化时用真实农历表换算（实际上随秒一起查，与固件节奏一致）；
 *  - 推进 3 秒断流状态机（真实超时逻辑）。
 */
SIM_EXPORT void sim_tick(void) {
  const long long now_ms = GetTickCount64();
  system_state_check_timeouts((uint32_t)now_ms);

  const time_t unix_utc = time(NULL);
  const uint32_t local =
      ntp_service_local_unix((uint32_t)unix_utc) % 86400u;
  (void)local; /* 仅证明换算路径可用；真正使用在下面 civil 换算 */

  ClockData clock;
  if (!ntp_service_civil_from_unix(
          ntp_service_local_unix((uint32_t)unix_utc), &clock)) {
    return;
  }
  clock.time_synced = true;
  if (clock.second != s_last_second) {
    s_last_second = clock.second;
    system_state_update_clock(&clock);

    LunarDate lunar;
    if (lunar_service_lookup(clock.year, clock.month, clock.day, &lunar)) {
      LunarData data;
      memset(&data, 0, sizeof(data));
      data.valid = true;
      strcpy(data.gz_year, lunar.ganzhi_year);
      strcpy(data.lunar_md, lunar.lunar_month_day);
      system_state_update_lunar(&data);
    }
  }
}

/** 直选主题（供键盘 1-5）；越界值由 system_state_set_theme 夹紧。 */
SIM_EXPORT void sim_set_theme(int theme) {
  system_state_set_theme((UiThemeId)theme);
}

/** 真实 theme_switch_advance：按键循环五套主题，返回新主题。 */
SIM_EXPORT int sim_advance_theme(void) {
  SystemSnapshot snap;
  system_state_get_snapshot(&snap);
  const UiThemeId next = theme_switch_advance(snap.theme_id, true);
  system_state_set_theme(next);
  return (int)next;
}

SIM_EXPORT int sim_current_theme(void) {
  SystemSnapshot snap;
  system_state_get_snapshot(&snap);
  return (int)snap.theme_id;
}

SIM_EXPORT int sim_ui_mode(void) {
  SystemSnapshot snap;
  system_state_get_snapshot(&snap);
  return (int)snap.ui_mode;
}

/* RGB565 帧缓冲 → RGB888 静态缓冲。 */
static const unsigned char* frame_to_rgb(void) {
  for (int i = 0; i < kDisplayWidth * kDisplayHeight; ++i) {
    const uint16_t px = s_frame[i];
    s_rgb[i * 3 + 0] = (unsigned char)(((px >> 11) & 0x1Fu) * 255u / 31u);
    s_rgb[i * 3 + 1] = (unsigned char)(((px >> 5) & 0x3Fu) * 255u / 63u);
    s_rgb[i * 3 + 2] = (unsigned char)((px & 0x1Fu) * 255u / 31u);
  }
  return s_rgb;
}

/** 渲染当前快照全帧，返回 128*160*3 RGB888 静态缓冲指针。 */
SIM_EXPORT const unsigned char* sim_render_frame(void) {
  static DisplayBackend backend = {"sim", mem_begin, mem_write, mem_end, NULL};
  SystemSnapshot snap;
  system_state_get_snapshot(&snap);
  if (!ui_render_draw_frame(&backend, &snap)) {
    return NULL;
  }
  return frame_to_rgb();
}

/**
 * 按指定页面渲染当前快照（0=仪表盘 1=时钟），不改动全局状态机。
 * 供双面板同屏展示：左面板永远画仪表盘、右面板永远画时钟，
 * 页面选择只覆盖局部快照字段，真实 ui_mode/超时逻辑照常运转。
 */
SIM_EXPORT const unsigned char* sim_render_frame_mode(int mode) {
  static DisplayBackend backend = {"sim", mem_begin, mem_write, mem_end, NULL};
  SystemSnapshot snap;
  system_state_get_snapshot(&snap);
  snap.ui_mode = (mode == 1) ? kUiModeClock : kUiModeDashboard;
  if (!ui_render_draw_frame(&backend, &snap)) {
    return NULL;
  }
  return frame_to_rgb();
}

SIM_EXPORT long long sim_stat_packets(void) { return s_fed_packets; }
SIM_EXPORT long long sim_stat_invalid(void) { return s_invalid_packets; }
