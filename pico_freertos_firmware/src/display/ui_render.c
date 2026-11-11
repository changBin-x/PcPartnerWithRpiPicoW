/**
 * @file ui_render.c
 * @brief v2 渲染层：五套主题 × 两种页面，共 10 屏，全部按 128×160 像素直接
 *        排版，配色与版式取自设计稿 `screen_designs_5styles_dual_pages.html`。
 *
 * 渲染模型沿用 v1：屏幕切成 10 条 16 行的水平条带，脏矩形只触发其覆盖到的
 * 条带重绘；每一片都重画整幅画面，绘图函数按条带 `origin_y` 自行裁剪。
 */

#include "ui_render.h"

#include <stdio.h>
#include <string.h>

#include "asset_store.h"
#include "firmware_checkin.h"
#include "render_window.h"
#include "ui_font.h"
#include "ui_gfx.h"
#include "ui_surface.h"
#include "ui_theme.h"
#include "theme_switch.h"

#ifdef PICO_ON_DEVICE
#include "FreeRTOS.h"
#include "task.h"
#endif

/** @brief 渲染任务参数与切片常量。 */
enum {
  kRenderPeriodMs = 20,                             /**< 渲染节拍 */
  kSliceCount = kDisplayHeight / kDisplaySliceRows, /**< 切片数 */
  kRenderStackWords = 512,                          /**< 渲染任务栈 */
  kRenderPriority = 3,                              /**< 渲染任务优先级 */
  kMaxDirtyRects = 8,                               /**< 单帧脏矩形上限 */
};

/** @brief 双缓冲条带。 */
static uint16_t s_slice_buffers[2][kDisplaySlicePixels];
/** @brief 按键输入源。 */
static const UiInputSource* s_input_source;
/** @brief 字库是否就绪。 */
static bool s_initialized;

/* ---------------------------------------------------------------- 绘图小工具
 */

/** @brief 左对齐画一行文本。 */
static void text_at(const UiSurface* surface, int x, int y, int px, uint16_t fg,
                    uint16_t bg, const char* str) {
  (void)ui_font_draw(surface, x, y, px, str, fg, bg);
}

/** @brief 右对齐画一行文本，right 为右边界（不含）。 */
static void text_right(const UiSurface* surface, int right, int y, int px,
                       uint16_t fg, uint16_t bg, const char* str) {
  int width = ui_font_width(px, str);
  text_at(surface, right - width, y, px, fg, bg, str);
}

/** @brief 水平居中画一行文本。 */
static void text_center(const UiSurface* surface, int y, int px, uint16_t fg,
                        uint16_t bg, const char* str) {
  int width = ui_font_width(px, str);
  text_at(surface, (kUiScreenWidth - width) / 2, y, px, fg, bg, str);
}

/** @brief UTF-8 首字节对应的字节数。 */
static int utf8_bytes(unsigned char lead) {
  if (lead < 0x80u) {
    return 1;
  }
  if ((lead & 0xE0u) == 0xC0u) {
    return 2;
  }
  if ((lead & 0xF0u) == 0xE0u) {
    return 3;
  }
  return 4;
}

/** @brief 左对齐画一行文本，超出 right 的整字丢弃。 */
static void text_fit(const UiSurface* surface, int x, int y, int px, int right,
                     uint16_t fg, uint16_t bg, const char* str) {
  char clipped[96];
  int width = 0;
  size_t used = 0;
  size_t i = 0;
  while (str[i] != '\0') {
    int bytes = utf8_bytes((unsigned char)str[i]);
    char one[8] = {0};
    if (used + (size_t)bytes >= sizeof clipped) {
      break;
    }
    for (int k = 0; k < bytes; ++k) {
      one[k] = str[i + (size_t)k];
    }
    int advance = ui_font_width(px, one);
    if (x + width + advance > right) {
      break;
    }
    for (int k = 0; k < bytes; ++k) {
      clipped[used++] = one[k];
    }
    width += advance;
    i += (size_t)bytes;
  }
  clipped[used] = '\0';
  text_at(surface, x, y, px, fg, bg, clipped);
}

/** @brief 画一个实心圆点（替代 ●）。 */
static void dot(const UiSurface* surface, int x, int y, int size,
                uint16_t color) {
  gfx_round_fill(surface, x, y, size, size, size / 2, color);
}

/** @brief 画一个空心圆环（替代 ○）。 */
static void ring(const UiSurface* surface, int x, int y, int size,
                 uint16_t color, uint16_t bg) {
  gfx_round_fill(surface, x, y, size, size, size / 2, color);
  gfx_round_fill(surface, x + 1, y + 1, size - 2, size - 2, (size - 2) / 2, bg);
}

/** @brief 把秒数格式化成 HH:MM:SS 或 HH:MM；未同步时画占位符。 */
static void format_clock(const ClockData* clock, char* out, size_t size,
                         bool with_seconds) {
  if (!clock->time_synced) {
    snprintf(out, size, "%s", with_seconds ? "--:--:--" : "--:--");
    return;
  }
  if (with_seconds) {
    snprintf(out, size, "%02d:%02d:%02d", clock->hour, clock->minute,
             clock->second);
  } else {
    snprintf(out, size, "%02d:%02d", clock->hour, clock->minute);
  }
}

/** @brief 把秒格式化成 "SS 秒"。 */
static void format_seconds(const ClockData* clock, char* out, size_t size) {
  if (!clock->time_synced) {
    snprintf(out, size, "-- 秒");
  } else {
    snprintf(out, size, "%02d 秒", clock->second);
  }
}

/** @brief 星期的中文单字。 */
static const char* weekday_char(int weekday) {
  static const char* const kNames[7] = {"日", "一", "二", "三",
                                        "四", "五", "六"};
  if (weekday < 0 || weekday > 6) {
    return "?";
  }
  return kNames[weekday];
}

/** @brief 公历日期，形如 "11月06日 星期五"。 */
static void format_date(const ClockData* clock, char* out, size_t size) {
  snprintf(out, size, "%02d月%02d日 星期%s", clock->month, clock->day,
           weekday_char(clock->weekday));
}

/** @brief 农历，形如 "丙午年 十月初七"。 */
static void format_lunar(const LunarData* lunar, char* out, size_t size) {
  if (!lunar->valid) {
    snprintf(out, size, "农历未配置");
    return;
  }
  snprintf(out, size, "%s %s", lunar->gz_year, lunar->lunar_md);
}

/** @brief 天气，形如 "晴 23度"；无效时给出占位。 */
static void format_weather(const WeatherData* weather, char* out, size_t size) {
  if (!weather->valid || weather->is_stale) {
    snprintf(out, size, "天气暂不可用");
    return;
  }
  snprintf(out, size, "%s %d度", weather->weather, weather->temperature);
}

/** @brief 百分比整数，无效时给 "--"。 */
static void format_percent(float value, bool valid, char* out, size_t size) {
  if (!valid) {
    snprintf(out, size, "--");
    return;
  }
  int rounded = (int)(value + 0.5f);
  if (rounded < 0) {
    rounded = 0;
  }
  if (rounded > 100) {
    rounded = 100;
  }
  snprintf(out, size, "%d", rounded);
}

/** @brief 百分比一位小数，无效时给 "--.-"。 */
static void format_percent1(float value, bool valid, char* out, size_t size) {
  if (!valid) {
    snprintf(out, size, "--.-");
    return;
  }
  snprintf(out, size, "%d.%d", (int)value, (int)((value - (int)value) * 10));
}

/** @brief 网速，形如 "1.2 兆/秒"。 */
static void format_rate(float mbps, bool valid, char* out, size_t size) {
  if (!valid) {
    snprintf(out, size, "-- 兆/秒");
    return;
  }
  snprintf(out, size, "%d.%d 兆/秒", (int)mbps, (int)((mbps - (int)mbps) * 10));
}

/* ------------------------------------------------------- 流派一：瑞士大字阶 */

/** @brief 瑞士流：顶栏 13 px。 */
static void swiss_top(const UiSurface* s, const SystemSnapshot* snap,
                      const UiThemePalette* p) {
  char buf[16];
  if (snap->pc_online) {
    dot(s, 4, 4, 5, p->accent);
    text_at(s, 11, 2, 7, p->muted, p->bg, "主机联机");
  } else {
    ring(s, 4, 4, 5, p->dim, p->bg);
    text_at(s, 11, 2, 7, p->dim, p->bg, "主机离线");
  }
  format_clock(&snap->clock, buf, sizeof buf, true);
  text_right(s, 124, 2, 7, p->muted, p->bg, buf);
  gfx_fill(s, 0, 13, kUiScreenWidth, 1, p->line);
}

/** @brief 瑞士流：一行指标（标签 + 标签胶囊 + 24 px 大数值）。 */
static void swiss_item(const UiSurface* s, int y, const char* label,
                       float value, bool active, const UiThemePalette* p) {
  char buf[8];
  const char* tag = active ? "[正常]" : "[待机]";
  uint16_t tag_bg = active ? p->surface_hi : p->surface;
  uint16_t tag_fg = active ? p->accent : p->dim;
  int tag_w = ui_font_width(6, tag);
  text_at(s, 6, y + 2, 8, p->muted, p->bg, label);
  gfx_fill(s, 122 - tag_w - 6, y + 1, tag_w + 6, 10, tag_bg);
  text_at(s, 122 - tag_w - 3, y + 2, 6, tag_fg, tag_bg, tag);
  format_percent(value, active, buf, sizeof buf);
  text_at(s, 6, y + 9, 24, active ? p->accent : p->dim, p->bg, buf);
  text_at(s, 6 + ui_font_width(24, buf) + 2, y + 25, 9, p->muted, p->bg, "%");
  gfx_fill(s, 0, y + 43, kUiScreenWidth, 1, p->line);
}

/** @brief 瑞士流：页面 A（监控）。 */
static void swiss_page_a(const UiSurface* s, const SystemSnapshot* snap,
                         const UiThemePalette* p) {
  char buf[48];
  char rx[24];
  char tx[24];
  gfx_clear(s, p->bg);
  swiss_top(s, snap, p);
  bool online = snap->pc_online;
  swiss_item(s, 14, "CPU 处理器", snap->metrics.cpu, online, p);
  swiss_item(s, 57, "GPU 显卡", snap->metrics.gpu,
             online && snap->metrics.gpu_valid, p);
  swiss_item(s, 100, "系统内存", snap->metrics.ram, online, p);
  format_rate(snap->metrics.rx_mbps, online, rx, sizeof rx);
  format_rate(snap->metrics.tx_mbps, online, tx, sizeof tx);
  snprintf(buf, sizeof buf, "下行: %s", rx);
  text_at(s, 4, 145, 7, p->muted, p->bg, buf);
  snprintf(buf, sizeof buf, "上行: %s", tx);
  text_right(s, 124, 145, 7, p->muted, p->bg, buf);
}

/** @brief 瑞士流：页面 B（时钟历法）。 */
static void swiss_page_b(const UiSurface* s, const SystemSnapshot* snap,
                         const UiThemePalette* p) {
  char buf[48];
  char date[32];
  char lunar[40];
  char weather[40];
  gfx_clear(s, p->bg);
  if (snap->wifi_connected) {
    dot(s, 4, 4, 5, p->accent);
    text_at(s, 11, 2, 7, p->muted, p->bg, "无线连接");
  } else {
    ring(s, 4, 4, 5, p->dim, p->bg);
    text_at(s, 11, 2, 7, p->dim, p->bg, "无线断开");
  }
  text_right(s, 124, 2, 7, p->muted, p->bg, "标准时间");
  gfx_fill(s, 0, 13, kUiScreenWidth, 1, p->line);

  format_clock(&snap->clock, buf, sizeof buf, false);
  text_center(s, 20, 26, p->accent, p->bg, buf);
  format_seconds(&snap->clock, buf, sizeof buf);
  text_center(s, 48, 8, p->muted, p->bg, buf);
  gfx_fill(s, 0, 61, kUiScreenWidth, 1, p->line);

  format_date(&snap->clock, date, sizeof date);
  text_at(s, 6, 65, 7, p->muted, p->bg, "公历日期");
  text_at(s, 6, 76, 11, p->fg, p->bg, date);
  gfx_fill(s, 0, 96, kUiScreenWidth, 1, p->line);

  format_lunar(&snap->lunar, lunar, sizeof lunar);
  text_at(s, 6, 100, 7, p->muted, p->bg, "传统农历");
  text_at(s, 6, 111, 11, p->warn, p->bg, lunar);
  gfx_fill(s, 0, 131, kUiScreenWidth, 1, p->line);

  format_weather(&snap->weather, weather, sizeof weather);
  text_at(s, 4, 138, 7, p->accent, p->bg, "今日天气");
  text_right(s, 124, 138, 7, p->fg, p->bg, weather);
}

/* --------------------------------------------------------- 流派二：字符终端 */

/** @brief TUI：反色标题行。 */
static void tui_header(const UiSurface* s, const char* title,
                       const char* status, const UiThemePalette* p) {
  gfx_fill(s, 0, 0, kUiScreenWidth, 10, p->surface_hi);
  text_at(s, 4, 1, 7, p->bg, p->surface_hi, title);
  text_right(s, 124, 1, 7, p->bg, p->surface_hi, status);
}

/** @brief TUI：10 格进度条。 */
static void tui_bar(const UiSurface* s, int y, int filled, uint16_t on,
                    uint16_t off) {
  for (int i = 0; i < 10; ++i) {
    int x = 5 + i * 7;
    gfx_fill(s, x, y, 6, 6, (i < filled) ? on : off);
  }
}

/** @brief TUI：页面 A（监控）。 */
static void tui_page_a(const UiSurface* s, const SystemSnapshot* snap,
                       const UiThemePalette* p) {
  char buf[32];
  char value[48];
  gfx_clear(s, p->bg);
  format_clock(&snap->clock, buf, sizeof buf, true);
  snprintf(value, sizeof value, "> 终端 %s", buf);
  tui_header(s, value, snap->pc_online ? "在线" : "离线", p);

  int y = 16;
  const char* labels[3] = {"[CPU] ", "[GPU] ", "[内存] "};
  const char* states[3] = {"稳定", "休眠", "负荷"};
  float values[3] = {snap->metrics.cpu, snap->metrics.gpu, snap->metrics.ram};
  bool valid[3] = {snap->pc_online, snap->pc_online && snap->metrics.gpu_valid,
                   snap->pc_online};
  for (int i = 0; i < 3; ++i) {
    char line[64];
    format_percent1(values[i], valid[i], value, sizeof value);
    snprintf(line, sizeof line, "%s%s%%", labels[i], value);
    uint16_t fg = valid[i] ? (i == 1 ? p->dim : p->fg) : p->dim;
    text_at(s, 5, y, 8, fg, p->bg, line);
    text_right(s, 123, y, 8, fg, p->bg, states[i]);
    int filled = valid[i] ? (int)(values[i] / 10.0f + 0.5f) : 0;
    if (filled > 10) {
      filled = 10;
    }
    tui_bar(s, y + 18, filled, i == 1 ? p->dim : p->fg, p->dim);
    gfx_dashed_hline(s, 0, y + 30, kUiScreenWidth, p->line, 3, 2);
    y += 40;
  }

  char rx[24];
  char tx[24];
  format_rate(snap->metrics.rx_mbps, snap->pc_online, rx, sizeof rx);
  format_rate(snap->metrics.tx_mbps, snap->pc_online, tx, sizeof tx);
  gfx_fill(s, 0, 140, kUiScreenWidth, 20, p->surface);
  snprintf(buf, sizeof buf, "下行 %s", rx);
  text_at(s, 5, 143, 7, p->fg, p->surface, buf);
  snprintf(buf, sizeof buf, "上行 %s", tx);
  text_right(s, 123, 143, 7, p->fg, p->surface, buf);
}

/** @brief TUI：一行日志（左标题、右取值）。 */
static void tui_log_line(const UiSurface* s, int y, const char* key,
                         const char* value, uint16_t value_color,
                         const UiThemePalette* p) {
  text_at(s, 5, y, 7, p->muted, p->bg, key);
  text_fit(s, 7 + ui_font_width(7, key), y, 7, 123, value_color, p->bg, value);
  gfx_dashed_hline(s, 5, y + 9, 118, p->line, 1, 1);
}

/** @brief TUI：页面 B（内核日志）。 */
static void tui_page_b(const UiSurface* s, const SystemSnapshot* snap,
                       const UiThemePalette* p) {
  char buf[32];
  char value[48];
  gfx_clear(s, p->bg);
  format_clock(&snap->clock, buf, sizeof buf, true);
  snprintf(value, sizeof value, "> 内核 %s", buf);
  tui_header(s, value, snap->clock.time_synced ? "就绪" : "对时中", p);

  int y = 16;
  tui_log_line(s, y, "设备: ", "微控制器 双核", p->accent, p);
  y += 20;
  tui_log_line(s, y,
               "时间校准: ", snap->clock.time_synced ? "对时正常" : "等待对时",
               snap->clock.time_synced ? p->fg : p->warn, p);
  y += 20;
  format_weather(&snap->weather, value, sizeof value);
  tui_log_line(s, y, "天气节点: ", value, p->fg, p);
  y += 20;
  format_lunar(&snap->lunar, value, sizeof value);
  tui_log_line(s, y, "干支农历: ", value, p->fg, p);
  y += 20;
  tui_log_line(s, y, "监督任务: ", "正常喂狗", p->fg, p);
  y += 20;
  tui_log_line(s, y, "网络广播: ", "端口8765", p->fg, p);

  gfx_fill(s, 0, 136, kUiScreenWidth, 24, p->surface);
  gfx_fill(s, 0, 136, kUiScreenWidth, 1, p->line);
  text_at(s, 5, 141, 7, p->accent, p->surface, "指令就绪_");
  text_right(s, 123, 141, 7, p->muted, p->surface, "端口8765");
}

/* ------------------------------------------------------------ 流派三：便当 */

/** @brief 便当：磁贴外框。 */
static void bento_tile(const UiSurface* s, int x, int y, int w, int h,
                       const UiThemePalette* p) {
  gfx_round_fill(s, x, y, w, h, 3, p->surface_hi);
  gfx_round_fill(s, x + 1, y + 1, w - 2, h - 2, 2, p->surface);
}

/** @brief 便当：页面 A（监控）。 */
static void bento_page_a(const UiSurface* s, const SystemSnapshot* snap,
                         const UiThemePalette* p) {
  char buf[48];
  char value[16];
  gfx_clear(s, p->bg);
  if (snap->pc_online) {
    dot(s, 3, 4, 4, p->accent_2);
    text_at(s, 9, 2, 7, p->muted, p->bg, "主机: 联机");
  } else {
    ring(s, 3, 4, 4, p->dim, p->bg);
    text_at(s, 9, 2, 7, p->dim, p->bg, "主机: 离线");
  }
  format_clock(&snap->clock, buf, sizeof buf, true);
  text_right(s, 125, 2, 7, p->muted, p->bg, buf);

  int tile_w = 59;
  int tile_h = 44;
  for (int i = 0; i < 2; ++i) {
    int x = 3 + i * (tile_w + 3);
    int y = 13;
    bento_tile(s, x, y, tile_w, tile_h, p);
    bool valid = snap->pc_online && (i == 0 || snap->metrics.gpu_valid);
    const char* name = (i == 0) ? "CPU" : "GPU";
    uint16_t color = valid ? ((i == 0) ? p->accent_2 : p->dim) : p->dim;
    text_at(s, x + 4, y + 4, 7, p->muted, p->surface, name);
    format_percent(i == 0 ? snap->metrics.cpu : snap->metrics.gpu, valid, value,
                   sizeof value);
    snprintf(buf, sizeof buf, "%s%%", value);
    if (i == 0) {
      text_right(s, x + tile_w - 4, y + 4, 7, color, p->surface, buf);
    }
    text_at(s, x + 4, y + 15, 15, color, p->surface, buf);
    text_at(s, x + 4, y + 33, 6, p->dim, p->surface,
            valid ? (i == 0 ? "负荷正常" : "核心在线") : "核心休眠");
    gfx_fill(s, x + 1, y + tile_h - 3, tile_w - 2, 2, color);
  }

  int wide_y = 13 + tile_h + 3;
  gfx_round_fill(s, 3, wide_y, 122, 40, 3, p->surface_hi);
  gfx_round_fill(s, 4, wide_y + 1, 120, 38, 2, p->surface);
  text_at(s, 9, wide_y + 4, 7, p->muted, p->surface, "系统内存占用率");
  format_percent(snap->metrics.ram, snap->pc_online, value, sizeof value);
  snprintf(buf, sizeof buf, "%s%%", value);
  text_right(s, 119, wide_y + 4, 7, p->accent, p->surface, buf);
  text_at(s, 9, wide_y + 15, 15, p->accent, p->surface, buf);
  text_at(s, 9 + ui_font_width(15, buf) + 2, wide_y + 23, 8, p->muted,
          p->surface, "已用");
  gfx_fill(s, 4, wide_y + 37, 120, 2, p->accent);

  char rx[24];
  char tx[24];
  format_rate(snap->metrics.rx_mbps, snap->pc_online, rx, sizeof rx);
  format_rate(snap->metrics.tx_mbps, snap->pc_online, tx, sizeof tx);
  int net_y = wide_y + 43;
  gfx_round_fill(s, 3, net_y, 122, 18, 3, p->surface_hi);
  gfx_round_fill(s, 4, net_y + 1, 120, 16, 2, p->surface);
  snprintf(buf, sizeof buf, "下行 %s", rx);
  text_at(s, 9, net_y + 5, 7, p->muted, p->surface, buf);
  snprintf(buf, sizeof buf, "上行 %s", tx);
  text_right(s, 119, net_y + 5, 7, p->muted, p->surface, buf);
}

/** @brief 便当：页面 B（时钟）。 */
static void bento_page_b(const UiSurface* s, const SystemSnapshot* snap,
                         const UiThemePalette* p) {
  char buf[48];
  char date[32];
  char lunar[40];
  char weather[40];
  gfx_clear(s, p->bg);
  if (snap->wifi_connected) {
    dot(s, 3, 4, 4, p->accent_2);
  } else {
    ring(s, 3, 4, 4, p->dim, p->bg);
  }
  text_at(s, 9, 2, 7, p->muted, p->bg,
          snap->wifi_connected ? "无线连接正常" : "无线已断开");
  text_right(s, 125, 2, 7, p->muted, p->bg, "东八区");

  gfx_round_fill(s, 3, 13, 122, 46, 3, p->surface_hi);
  gfx_round_fill(s, 4, 14, 120, 44, 2, p->surface);
  format_clock(&snap->clock, buf, sizeof buf, true);
  text_center(s, 19, 17, p->accent_2, p->surface, buf);
  format_date(&snap->clock, date, sizeof date);
  text_center(s, 40, 7, p->muted, p->surface, date);

  int tile_y = 62;
  int tile_w = 59;
  int tile_h = 62;
  format_lunar(&snap->lunar, lunar, sizeof lunar);
  gfx_round_fill(s, 3, tile_y, tile_w, tile_h, 3, p->surface_hi);
  gfx_round_fill(s, 4, tile_y + 1, tile_w - 2, tile_h - 2, 2, p->surface);
  text_at(s, 8, tile_y + 4, 7, p->muted, p->surface, "农历民俗");
  if (snap->lunar.valid) {
    text_fit(s, 8, tile_y + 16, 9, 3 + tile_w - 4, p->accent, p->surface,
             snap->lunar.gz_year);
    text_fit(s, 8, tile_y + 28, 9, 3 + tile_w - 4, p->accent, p->surface,
             snap->lunar.lunar_md);
  } else {
    text_fit(s, 8, tile_y + 16, 9, 3 + tile_w - 4, p->dim, p->surface, lunar);
  }
  gfx_fill(s, 4, tile_y + tile_h - 4, tile_w - 2, 2, p->accent);

  format_weather(&snap->weather, weather, sizeof weather);
  gfx_round_fill(s, 3 + tile_w + 3, tile_y, tile_w, tile_h, 3, p->surface_hi);
  gfx_round_fill(s, 4 + tile_w + 3, tile_y + 1, tile_w - 2, tile_h - 2, 2,
                 p->surface);
  text_at(s, 8 + tile_w + 3, tile_y + 4, 7, p->muted, p->surface, "实况气象");
  text_fit(s, 8 + tile_w + 3, tile_y + 16, 10, 3 + tile_w * 2 + 3 - 4,
           p->accent_2, p->surface, weather);
  gfx_fill(s, 4 + tile_w + 3, tile_y + tile_h - 4, tile_w - 2, 2, p->accent_2);

  int net_y = tile_y + tile_h + 3;
  gfx_round_fill(s, 3, net_y, 122, 18, 3, p->surface_hi);
  gfx_round_fill(s, 4, net_y + 1, 120, 16, 2, p->surface);
  snprintf(buf, sizeof buf, "城市: %s",
           snap->weather.valid ? snap->weather.city : "未配置");
  text_at(s, 9, net_y + 5, 7, p->muted, p->surface, buf);
  text_right(s, 119, net_y + 5, 7, p->muted, p->surface, "定位就绪");
}

/* --------------------------------------------------------- 流派四：工业示波 */

/** @brief 示波：一条指标行（左侧数值 + 右侧 72×36 波形框）。 */
static void scope_row(const UiSurface* s, int y, const char* tag, float value,
                      bool valid, uint16_t color, const UiThemePalette* p) {
  char buf[8];
  text_at(s, 4, y + 4, 7, p->muted, p->bg, tag);
  format_percent(value, valid, buf, sizeof buf);
  text_at(s, 4, y + 14, 14, valid ? color : p->dim, p->bg, buf);
  gfx_fill(s, 52, y + 4, 72, 36, p->surface);
  gfx_grid(s, 52, y + 4, 72, 36, p->grid, 12);
  gfx_stroke(s, 52, y + 4, 72, 36, p->line);
  /* 波形：以当前值决定振幅的固定锯齿，避免引入历史缓冲。 */
  int16_t points[16];
  int amplitude = valid ? (int)(value * 0.12f) : 2;
  if (amplitude < 2) {
    amplitude = 2;
  }
  for (int i = 0; i < 8; ++i) {
    points[i * 2] = (int16_t)(53 + i * 10);
    points[i * 2 + 1] =
        (int16_t)(y + 22 + ((i % 2 == 0) ? -amplitude : amplitude));
  }
  gfx_polyline(s, points, 8, valid ? color : p->dim);
  gfx_fill(s, 0, y + 44, kUiScreenWidth, 1, p->line);
}

/** @brief 示波：页面 A（三通道监控）。 */
static void scope_page_a(const UiSurface* s, const SystemSnapshot* snap,
                         const UiThemePalette* p) {
  char buf[48];
  gfx_clear(s, p->bg);
  gfx_fill(s, 0, 0, kUiScreenWidth, 10, p->surface_hi);
  format_clock(&snap->clock, buf, sizeof buf, true);
  snprintf(buf, sizeof buf, "通道一·监测 %02d:%02d:%02d", snap->clock.hour,
           snap->clock.minute, snap->clock.second);
  text_at(s, 4, 1, 6, p->fg, p->surface_hi, buf);
  text_right(s, 124, 1, 6, p->muted, p->surface_hi,
             snap->pc_online ? "自动触发" : "等待信号");

  scope_row(s, 12, "CPU", snap->metrics.cpu, snap->pc_online, p->accent, p);
  scope_row(s, 56, "GPU", snap->metrics.gpu,
            snap->pc_online && snap->metrics.gpu_valid, p->muted, p);
  scope_row(s, 100, "内存", snap->metrics.ram, snap->pc_online, p->warn, p);

  char rx[24];
  char tx[24];
  format_rate(snap->metrics.rx_mbps, snap->pc_online, rx, sizeof rx);
  format_rate(snap->metrics.tx_mbps, snap->pc_online, tx, sizeof tx);
  gfx_fill(s, 0, 146, kUiScreenWidth, 14, p->surface);
  snprintf(buf, sizeof buf, "下行 %s", rx);
  text_at(s, 4, 149, 7, p->fg, p->surface, buf);
  snprintf(buf, sizeof buf, "上行 %s", tx);
  text_right(s, 124, 149, 7, p->fg, p->surface, buf);
}

/** @brief 示波：页面 B（时序遥测）。 */
static void scope_page_b(const UiSurface* s, const SystemSnapshot* snap,
                         const UiThemePalette* p) {
  char buf[48];
  gfx_clear(s, p->bg);
  gfx_fill(s, 0, 0, kUiScreenWidth, 10, p->surface_hi);
  snprintf(buf, sizeof buf, "通道二·遥测 %02d:%02d:%02d", snap->clock.hour,
           snap->clock.minute, snap->clock.second);
  text_at(s, 4, 1, 6, p->fg, p->surface_hi, buf);
  text_right(s, 124, 1, 6, p->muted, p->surface_hi,
             snap->clock.time_synced ? "时序正常" : "对时中");

  gfx_fill(s, 4, 14, 120, 74, p->surface);
  gfx_grid(s, 4, 14, 120, 74, p->grid, 10);
  gfx_stroke(s, 4, 14, 120, 74, p->line);
  int16_t points[40];
  int amplitude = (int)(snap->metrics.ram * 0.3f);
  if (amplitude < 4) {
    amplitude = 4;
  }
  if (amplitude > 28) {
    amplitude = 28;
  }
  for (int i = 0; i < 20; ++i) {
    points[i * 2] = (int16_t)(6 + i * 6);
    points[i * 2 + 1] = (int16_t)(51 + ((i % 4 < 2) ? -amplitude : amplitude));
  }
  gfx_polyline(s, points, 20, p->accent);

  text_at(s, 5, 92, 7, p->muted, p->bg, "下行峰值: ");
  text_at(s, 5 + ui_font_width(7, "下行峰值: "), 92, 7, p->warn, p->bg,
          "-- 兆/秒");
  text_at(s, 5, 104, 7, p->muted, p->bg, "通信延迟: ");
  text_at(s, 5 + ui_font_width(7, "通信延迟: "), 104, 7, p->fg, p->bg,
          "-- 毫秒");
  snprintf(buf, sizeof buf, "网络包序号: #%06u", (unsigned)snap->metrics.seq);
  text_at(s, 5, 116, 7, p->muted, p->bg, buf);
  text_at(s, 5, 128, 7, p->muted, p->bg, "时间同步源: 阿里云节点");

  gfx_fill(s, 0, 142, kUiScreenWidth, 18, p->surface);
  snprintf(buf, sizeof buf, "环境温度: %d度",
           snap->weather.valid ? snap->weather.temperature : 0);
  text_at(s, 4, 146, 7, p->fg, p->surface, buf);
  format_weather(&snap->weather, buf, sizeof buf);
  text_right(s, 124, 146, 7, p->fg, p->surface, buf);
}

/* ------------------------------------------------------- 流派五：战术机甲 */

/** @brief HUD：顶栏（右上角斜切 8 px）。 */
static void hud_top(const UiSurface* s, const char* prefix, const char* status,
                    const UiThemePalette* p) {
  gfx_bevel_top(s, 0, 0, kUiScreenWidth, 14, 8, p->surface_hi);
  text_at(s, 6, 2, 7, p->accent, p->surface_hi, prefix);
  text_right(s, 118, 2, 7, p->muted, p->surface_hi, status);
  gfx_fill(s, 0, 14, kUiScreenWidth, 1, p->line);
}

/** @brief HUD：一行指标（编号 + 名称 + 18 px 数值 + 右侧 5 段电平）。 */
static void hud_item(const UiSurface* s, int y, const char* code,
                     const char* name, float value, bool valid, uint16_t color,
                     const UiThemePalette* p) {
  char buf[16];
  int box_h = 42;
  gfx_fill(s, 4, y, 120, box_h, p->surface);
  gfx_stroke(s, 4, y, 120, box_h, p->line);
  gfx_fill(s, 4, y, 2, box_h, valid ? color : p->dim);
  text_at(s, 10, y + 3, 6, p->muted, p->surface, code);
  text_at(s, 10 + ui_font_width(6, code) + 2, y + 3, 6, p->dim, p->surface,
          name);
  format_percent1(value, valid, buf, sizeof buf);
  text_at(s, 10, y + 13, 18, valid ? color : p->dim, p->surface, buf);
  text_at(s, 10 + ui_font_width(18, buf) + 2, y + 26, 9, p->muted, p->surface,
          "%");
  int level = valid ? (int)(value / 20.0f + 0.5f) : 0;
  for (int i = 0; i < 5; ++i) {
    int width = 24 - i * 4;
    uint16_t seg = (i < level) ? color : p->line;
    gfx_fill(s, 122 - width, y + 5 + i * 7, width, 3, seg);
  }
}

/** @brief HUD：页面 A（战术抬头）。 */
static void hud_page_a(const UiSurface* s, const SystemSnapshot* snap,
                       const UiThemePalette* p) {
  char buf[48];
  gfx_clear(s, p->bg);
  snprintf(buf, sizeof buf, "战术抬头 %02d:%02d:%02d", snap->clock.hour,
           snap->clock.minute, snap->clock.second);
  hud_top(s, buf, snap->pc_online ? "联机" : "离线", p);
  hud_item(s, 16, "[01]", "CPU 处理器", snap->metrics.cpu, snap->pc_online,
           p->accent, p);
  hud_item(s, 60, "[02]", "GPU 显卡", snap->metrics.gpu,
           snap->pc_online && snap->metrics.gpu_valid, p->muted, p);
  hud_item(s, 104, "[03]", "内存堆栈", snap->metrics.ram, snap->pc_online,
           p->accent_2, p);

  char rx[24];
  char tx[24];
  format_rate(snap->metrics.rx_mbps, snap->pc_online, rx, sizeof rx);
  format_rate(snap->metrics.tx_mbps, snap->pc_online, tx, sizeof tx);
  snprintf(buf, sizeof buf, "下行 %s", rx);
  text_at(s, 4, 150, 7, p->accent_2, p->bg, buf);
  snprintf(buf, sizeof buf, "上行 %s", tx);
  text_right(s, 124, 150, 7, p->accent_2, p->bg, buf);
}

/** @brief HUD：面板的一行。 */
static void hud_row(const UiSurface* s, int y, const char* key,
                    const char* value, uint16_t color,
                    const UiThemePalette* p) {
  text_at(s, 10, y, 7, p->muted, p->surface, key);
  text_fit(s, 12 + ui_font_width(7, key), y, 7, 118, color, p->surface, value);
  gfx_fill(s, 10, y + 9, 108, 1, p->line);
}

/** @brief HUD：页面 B（战术巡检）。 */
static void hud_page_b(const UiSurface* s, const SystemSnapshot* snap,
                       const UiThemePalette* p) {
  char buf[48];
  char lunar[40];
  gfx_clear(s, p->bg);
  snprintf(buf, sizeof buf, "战术巡检 %02d:%02d:%02d", snap->clock.hour,
           snap->clock.minute, snap->clock.second);
  hud_top(s, buf, snap->clock.time_synced ? "就绪" : "对时中", p);

  gfx_fill(s, 4, 18, 120, 96, p->surface);
  gfx_stroke(s, 4, 18, 120, 96, p->line);
  gfx_fill(s, 4, 18, 2, 96, p->accent_2);
  hud_row(s, 22, "机体护盾: ", "100% 满充", p->accent, p);
  hud_row(s, 36, "双核反应堆: ", "正常运转", p->fg, p);
  format_weather(&snap->weather, buf, sizeof buf);
  hud_row(s, 50, "气象环境: ", buf, p->fg, p);
  format_lunar(&snap->lunar, lunar, sizeof lunar);
  hud_row(s, 64, "历法时序: ", lunar, p->accent_2, p);
  hud_row(s, 78, "看门狗防护: ", "硬件锁定", p->fg, p);

  text_at(s, 8, 120, 7, p->muted, p->bg, "巡检状态: 扫描完成 // 无异常");
  gfx_fill(s, 0, 134, kUiScreenWidth, 1, p->line);
  text_at(s, 4, 140, 7, p->accent_2, p->bg, "状态: 战备正常");
  text_right(s, 124, 140, 7, p->muted, p->bg,
             snap->clock.time_synced ? "时间源锁定" : "时间源搜索");
}

/* -------------------------------------------------------------- 分发与切片 */

/** @brief 按主题与页面分发到 10 个屏之一。 */
static void render_frame(const UiSurface* surface,
                         const SystemSnapshot* snapshot) {
  const UiThemePalette* palette = ui_theme_get(snapshot->theme_id);
  bool page_a = (snapshot->ui_mode == kUiModeDashboard);
  switch (snapshot->theme_id) {
    case kThemeTui:
      if (page_a) {
        tui_page_a(surface, snapshot, palette);
      } else {
        tui_page_b(surface, snapshot, palette);
      }
      break;
    case kThemeBento:
      if (page_a) {
        bento_page_a(surface, snapshot, palette);
      } else {
        bento_page_b(surface, snapshot, palette);
      }
      break;
    case kThemeScope:
      if (page_a) {
        scope_page_a(surface, snapshot, palette);
      } else {
        scope_page_b(surface, snapshot, palette);
      }
      break;
    case kThemeHud:
      if (page_a) {
        hud_page_a(surface, snapshot, palette);
      } else {
        hud_page_b(surface, snapshot, palette);
      }
      break;
    case kThemeSwiss:
    default:
      if (page_a) {
        swiss_page_a(surface, snapshot, palette);
      } else {
        swiss_page_b(surface, snapshot, palette);
      }
      break;
  }
}

bool ui_render_init(void) {
  const AssetBlob* blob = asset_store_font_cn();
  s_initialized = ui_font_init(blob);
  return s_initialized;
}

void ui_render_set_input_source(const UiInputSource* source) {
  s_input_source = source;
}

bool ui_render_draw_dirty(DisplayBackend* backend,
                          const SystemSnapshot* snapshot, const UiRect* rects,
                          int rect_count) {
  if (backend == NULL || backend->begin_frame == NULL ||
      backend->write_slice == NULL || backend->end_frame == NULL ||
      snapshot == NULL || rects == NULL || rect_count <= 0) {
    return false;
  }
  if (!s_initialized) {
    return false;
  }
  if (!backend->begin_frame(backend)) {
    return false;
  }
  render_window_enter();
  bool slices[kSliceCount] = {false};
  for (int i = 0; i < rect_count; ++i) {
    int first_row = rects[i].y;
    int last_row = rects[i].y + rects[i].h;
    if (first_row < 0) {
      first_row = 0;
    }
    if (last_row > kDisplayHeight) {
      last_row = kDisplayHeight;
    }
    for (int row = first_row; row < last_row; ++row) {
      slices[row / kDisplaySliceRows] = true;
    }
  }
  UiSurface surface = {NULL, kUiScreenWidth, kDisplaySliceRows, 0};
  bool ok = true;
  for (int slice = 0; slice < kSliceCount && ok; ++slice) {
    if (!slices[slice]) {
      continue;
    }
    surface.pixels = s_slice_buffers[slice & 1];
    surface.origin_y = slice * kDisplaySliceRows;
    render_frame(&surface, snapshot);
    ok = backend->write_slice(backend, surface.origin_y, kDisplaySliceRows,
                              surface.pixels, kDisplaySlicePixels);
  }
  render_window_leave();
  backend->end_frame(backend);
  return ok;
}

bool ui_render_draw_frame(DisplayBackend* backend,
                          const SystemSnapshot* snapshot) {
  UiRect full = {0, 0, kUiScreenWidth, kUiScreenHeight};
  return ui_render_draw_dirty(backend, snapshot, &full, 1);
}

/** @brief 判断两份快照是否需要重画整屏。 */
static bool snapshot_changed(const SystemSnapshot* a, const SystemSnapshot* b) {
  return a->theme_id != b->theme_id || a->ui_mode != b->ui_mode ||
         a->pc_online != b->pc_online ||
         a->wifi_connected != b->wifi_connected ||
         a->metrics.seq != b->metrics.seq ||
         a->clock.second != b->clock.second ||
         a->clock.minute != b->clock.minute ||
         a->clock.time_synced != b->clock.time_synced ||
         a->lunar.valid != b->lunar.valid ||
         a->weather.valid != b->weather.valid ||
         a->weather.is_stale != b->weather.is_stale;
}

int ui_render_collect_dirty_regions(const SystemSnapshot* previous,
                                    const SystemSnapshot* current,
                                    UiRect* out_rects, int max_rects) {
  if (current == NULL || out_rects == NULL || max_rects <= 0) {
    return 0;
  }
  if (previous != NULL && !snapshot_changed(previous, current)) {
    return 0;
  }
  out_rects[0].x = 0;
  out_rects[0].y = 0;
  out_rects[0].w = kUiScreenWidth;
  out_rects[0].h = kUiScreenHeight;
  return 1;
}

#ifdef PICO_ON_DEVICE

/** @brief 渲染任务：20 ms 一拍，推进状态机、采样按键、按脏区刷屏。 */
static void vTaskUIRender(void* context) {
  (void)context;
  DisplayBackend* backend = st7735s_backend();
  if (!st7735s_init() || !ui_render_init()) {
    printf("显示初始化失败\n");
    FIRMWARE_CHECK_IN_DISABLE(kSupervisorSlotUi);
    vTaskDelete(NULL);
    return;
  }
  SystemSnapshot previous;
  system_state_get_snapshot(&previous);
  (void)ui_render_draw_frame(backend, &previous);
  for (;;) {
    FIRMWARE_CHECK_IN(kSupervisorSlotUi);
    vTaskDelay(pdMS_TO_TICKS(kRenderPeriodMs));
    /* 指标超时后要把界面切回时钟页；状态机由渲染任务推进。 */
    system_state_check_timeouts((uint32_t)pdTICKS_TO_MS(xTaskGetTickCount()));
    SystemSnapshot current;
    system_state_get_snapshot(&current);
    /* 按键与渲染共用 20 ms 节拍：消抖需要稳定采样周期。 */
    if (s_input_source != NULL && s_input_source->poll_click != NULL &&
        s_input_source->poll_click()) {
      system_state_set_theme(theme_switch_advance(current.theme_id, true));
      system_state_get_snapshot(&current);
    }
    UiRect dirty_rects[kMaxDirtyRects];
    const int dirty_count = ui_render_collect_dirty_regions(
        &previous, &current, dirty_rects, kMaxDirtyRects);
    if (dirty_count > 0) {
      (void)ui_render_draw_dirty(backend, &current, dirty_rects, dirty_count);
    }
    previous = current;
  }
}

bool ui_render_start_task(void) {
  TaskHandle_t handle = NULL;
  const BaseType_t created =
      xTaskCreate(vTaskUIRender, "vTaskUIRender", kRenderStackWords, NULL,
                  kRenderPriority, &handle);
  if (created != pdPASS || handle == NULL) {
    printf("显示任务创建失败\n");
    return false;
  }
  vTaskCoreAffinitySet(handle, (UBaseType_t)(1u << 1));
  return true;
}

#else

bool ui_render_start_task(void) { return true; }

#endif
