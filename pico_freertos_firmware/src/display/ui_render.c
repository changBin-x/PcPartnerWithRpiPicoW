#include "ui_render.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "asset_store.h"
#include "font_engine.h"
#include "st7735s_driver.h"
#include "system_state.h"
#include "ui_layout.h"

#ifdef PICO_ON_DEVICE
#include "FreeRTOS.h"
#include "task.h"
#endif

/* Material Design 3 深色主题令牌，与 pico/display.py 的取值一致。 */
enum {
  kColorBackground = 0x1083,
  kColorSurface = 0x1904,
  kColorSurfaceHigh = 0x2945,
  kColorTrack = 0x31a7,
  kColorText = 0xe71d,
  kColorMuted = 0x9493,
  kColorPrimary = 0xae3f,
  kColorSecondary = 0xa699,
  kColorTertiary = 0xf5f1,
  kColorError = 0xf5d6,
};

/* 与 ui_layout.c 的脏矩形坐标对齐的排版参数。 */
enum {
  kTopBarIconY = 1,
  kClockIconY = 0,
  kPcIconX = 4,
  kWifiIconX = 108,
  kIconWidth = 16,
  kIconHeight = 14,

  kCardX = 4,
  kCardWidth = 120,
  kCardHeight = 32,
  kCardCpuY = 20,
  kCardGpuY = 56,
  kCardRamY = 92,
  kCardLabelX = 10,
  kCardLabelOffsetY = 3,
  kCardValueRight = 120,
  kProgressX = 10,
  kProgressYOffset = 24,
  kProgressWidth = 108,
  kProgressHeight = 4,

  kNetworkRxY = 128,
  kNetworkTxY = 142,
  kNetworkLabelX = 4,
  kNetworkValueX = 28,

  kWeatherCardX = 4,
  kWeatherCardY = 14,
  kWeatherCardWidth = 120,
  kWeatherCardHeight = 15,
  kWeatherTextY = 15,
  kClockCardX = 4,
  kClockCardY = 30,
  kClockCardWidth = 120,
  kClockCardHeight = 40,
  kClockTextY = 37,
  kDateCardX = 4,
  kDateCardY = 70,
  kDateCardWidth = 120,
  kDateCardHeight = 80,
  kDateTextY = 77,
  kWeekdayCardX = 36,
  kWeekdayCardY = 94,
  kWeekdayCardWidth = 56,
  kWeekdayCardHeight = 18,
  kWeekdayTextY = 96,
  kLunarTextY = 115,
  kSyncCardX = 24,
  kSyncCardY = 94,
  kSyncCardWidth = 80,
  kSyncCardHeight = 20,
  kSyncTextY = 97,

  kSliceCount = kDisplayHeight / kDisplaySliceRows,
  kMaxDirtyRects = 8,
  kRenderPeriodMs = 20,
  kRenderPriority = 3,
  kRenderStackWords = 512,
};

static const uint8_t kIconPc[] = {
    0x00, 0x04, 0x44, 0x44, 0x44, 0x44, 0x40, 0x00, 0x01, 0xef, 0xff, 0xff,
    0xff, 0xff, 0xfe, 0x10, 0x07, 0xf4, 0x44, 0x44, 0x44, 0x44, 0x4f, 0x70,
    0x08, 0xf0, 0x00, 0x00, 0x00, 0x00, 0x0f, 0x80, 0x08, 0xf0, 0x00, 0x00,
    0x00, 0x00, 0x0f, 0x80, 0x08, 0xf0, 0x00, 0x00, 0x00, 0x00, 0x0f, 0x80,
    0x08, 0xf0, 0x00, 0x00, 0x00, 0x00, 0x0f, 0x80, 0x08, 0xf0, 0x00, 0x00,
    0x00, 0x00, 0x0f, 0x80, 0x02, 0xef, 0xff, 0xff, 0xff, 0xff, 0xfe, 0x20,
    0x00, 0x28, 0x88, 0x8f, 0xf8, 0x88, 0x82, 0x00, 0x00, 0x00, 0x00, 0x0f,
    0xf0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x8f, 0xff, 0xff, 0xf8, 0x00, 0x00,
    0x00, 0x00, 0x24, 0x44, 0x44, 0x42, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00,
};

static const uint8_t kIconWifi[] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x37, 0x8a,
    0xa8, 0x73, 0x00, 0x00, 0x00, 0x6d, 0xff, 0xff, 0xff, 0xff, 0xd6, 0x00,
    0x2d, 0xfe, 0x95, 0x30, 0x03, 0x59, 0xef, 0xd2, 0x0c, 0x81, 0x00, 0x24,
    0x42, 0x00, 0x18, 0xc0, 0x00, 0x02, 0x9e, 0xff, 0xff, 0xe9, 0x20, 0x00,
    0x00, 0x3f, 0xfd, 0x88, 0x88, 0xdf, 0xf3, 0x00, 0x00, 0x08, 0x40, 0x00,
    0x00, 0x04, 0x80, 0x00, 0x00, 0x00, 0x04, 0xab, 0xba, 0x40, 0x00, 0x00,
    0x00, 0x00, 0x4f, 0xfd, 0xdf, 0xf4, 0x00, 0x00, 0x00, 0x00, 0x06, 0x20,
    0x02, 0x60, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x09, 0x90, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x08,
    0x80, 0x00, 0x00, 0x00,
};

/* 双缓冲切片：128*16*2 = 4096 字节/块，合计 8192 字节，位于 .bss。
 * write_slice 返回后 DMA 仍在读当前块，CPU 用另一块光栅化下一片。 */
static uint16_t s_slice_buffers[2][kDisplaySlicePixels];

static const char* const kWeekdayNames[7] = {
    "周日", "周一", "周二", "周三", "周四", "周五", "周六",
};

static void fill_rect(uint16_t* pixels, int origin_y, int x, int y, int width,
                      int height, uint16_t color) {
  int first_column = x < 0 ? 0 : x;
  int first_row = y < origin_y ? origin_y : y;
  int last_column = x + width;
  int last_row = y + height;
  if (last_column > kUiScreenWidth) {
    last_column = kUiScreenWidth;
  }
  if (last_row > origin_y + kDisplaySliceRows) {
    last_row = origin_y + kDisplaySliceRows;
  }
  if (first_column >= last_column || first_row >= last_row) {
    return;
  }
  for (int row = first_row; row < last_row; ++row) {
    uint16_t* destination =
        pixels + (size_t)(row - origin_y) * (size_t)kUiScreenWidth;
    for (int column = first_column; column < last_column; ++column) {
      destination[column] = color;
    }
  }
}

static void draw_card(uint16_t* pixels, int origin_y, int x, int y, int width,
                      int height, uint16_t color) {
  /* 三段矩形拼出 1 像素倒角，避免为圆角引入额外位图。 */
  fill_rect(pixels, origin_y, x + 2, y, width - 4, height, color);
  fill_rect(pixels, origin_y, x, y + 2, width, height - 4, color);
  fill_rect(pixels, origin_y, x + 1, y + 1, width - 2, height - 2, color);
}

static FontSurface make_surface(uint16_t* pixels, int origin_y) {
  FontSurface surface;
  surface.pixels = pixels;
  surface.width = kUiScreenWidth;
  surface.height = kDisplaySliceRows;
  surface.origin_y = origin_y;
  return surface;
}

static void draw_centered_text(uint16_t* pixels, int origin_y, int y,
                               const char* text, uint16_t color,
                               uint16_t background) {
  FontSurface surface = make_surface(pixels, origin_y);
  int x = (kUiScreenWidth - font_engine_text_width(text)) / 2;
  if (x < 0) {
    x = 0;
  }
  font_engine_draw_text(&surface, x, y, text, color, background);
}

static int percent_from_float(float value) {
  /* 用 !(value >= 0) 同时挡住负数和 NaN。 */
  if (!(value >= 0.0f)) {
    return 0;
  }
  if (value >= 100.0f) {
    return 100;
  }
  return (int)(value + 0.5f);
}

static void format_rate(char* buffer, size_t size, float mbps) {
  /* newlib-nano 的 printf 默认不带浮点支持，这里先转成整数再格式化。 */
  if (!(mbps >= 0.0f)) {
    mbps = 0.0f;
  }
  int tenths = (int)(mbps * 10.0f + 0.5f);
  if (tenths > 99999) {
    tenths = 99999;
  }
  snprintf(buffer, size, "%d.%dMbps", tenths / 10, tenths % 10);
}

static void render_top_bar(uint16_t* pixels, int origin_y,
                           const SystemSnapshot* snapshot) {
  /* 与 pico/display.py 一致：只画两枚状态图标，用颜色区分在线与离线。
   * 字库没有“在线/离线/无网”这些字，避免回退成占位方框。 */
  FontSurface surface = make_surface(pixels, origin_y);
  const uint16_t pc_color = snapshot->pc_online ? kColorPrimary : kColorError;
  font_engine_draw_glyph(&surface, kPcIconX, kTopBarIconY, kIconPc, kIconWidth,
                         kIconHeight, pc_color, kColorBackground);
  const uint16_t wifi_color =
      snapshot->wifi_connected ? kColorPrimary : kColorError;
  font_engine_draw_glyph(&surface, kWifiIconX, kTopBarIconY, kIconWifi,
                         kIconWidth, kIconHeight, wifi_color, kColorBackground);
}

static void render_metric_card(uint16_t* pixels, int origin_y, int card_y,
                               const char* label, float value, bool valid,
                               uint16_t accent) {
  draw_card(pixels, origin_y, kCardX, card_y, kCardWidth, kCardHeight,
            kColorSurface);
  fill_rect(pixels, origin_y, kCardX, card_y + 2, 2, kCardHeight - 4, accent);

  FontSurface surface = make_surface(pixels, origin_y);
  font_engine_draw_text(&surface, kCardLabelX, card_y + kCardLabelOffsetY,
                        label, kColorMuted, kColorSurface);

  char value_text[16];
  int percent = 0;
  if (valid) {
    percent = percent_from_float(value);
    snprintf(value_text, sizeof(value_text), "%d%%", percent);
  } else {
    snprintf(value_text, sizeof(value_text), "--");
  }
  const int value_width = font_engine_text_width(value_text);
  font_engine_draw_text(&surface, kCardValueRight - value_width,
                        card_y + kCardLabelOffsetY, value_text, kColorText,
                        kColorSurface);

  fill_rect(pixels, origin_y, kProgressX, card_y + kProgressYOffset,
            kProgressWidth, kProgressHeight, kColorTrack);
  if (valid) {
    const int fill_width = (kProgressWidth * percent + 50) / 100;
    fill_rect(pixels, origin_y, kProgressX, card_y + kProgressYOffset,
              fill_width, kProgressHeight, accent);
  }
}

static void render_network(uint16_t* pixels, int origin_y,
                           const SystemSnapshot* snapshot) {
  FontSurface surface = make_surface(pixels, origin_y);
  font_engine_draw_text(&surface, kNetworkLabelX, kNetworkRxY, "RX",
                        kColorMuted, kColorBackground);
  font_engine_draw_text(&surface, kNetworkLabelX, kNetworkTxY, "TX",
                        kColorMuted, kColorBackground);

  char value[32];
  if (snapshot->pc_online) {
    format_rate(value, sizeof(value), snapshot->metrics.rx_mbps);
  } else {
    snprintf(value, sizeof(value), "--");
  }
  font_engine_draw_text(&surface, kNetworkValueX, kNetworkRxY, value,
                        kColorText, kColorBackground);

  if (snapshot->pc_online) {
    format_rate(value, sizeof(value), snapshot->metrics.tx_mbps);
  } else {
    snprintf(value, sizeof(value), "--");
  }
  font_engine_draw_text(&surface, kNetworkValueX, kNetworkTxY, value,
                        kColorText, kColorBackground);
}

static void render_dashboard(uint16_t* pixels, int origin_y,
                             const SystemSnapshot* snapshot) {
  render_top_bar(pixels, origin_y, snapshot);
  render_metric_card(pixels, origin_y, kCardCpuY, "CPU", snapshot->metrics.cpu,
                     true, kColorPrimary);
  render_metric_card(pixels, origin_y, kCardGpuY, "GPU", snapshot->metrics.gpu,
                     snapshot->metrics.gpu_valid, kColorSecondary);
  const int ram_percent = percent_from_float(snapshot->metrics.ram);
  const uint16_t ram_accent = ram_percent > 90 ? kColorError : kColorTertiary;
  render_metric_card(pixels, origin_y, kCardRamY, "RAM", snapshot->metrics.ram,
                     true, ram_accent);
  render_network(pixels, origin_y, snapshot);
}

static void render_clock_header(uint16_t* pixels, int origin_y,
                                const SystemSnapshot* snapshot) {
  /* 时钟页只画 WiFi 图标；仪表盘才同时画 PC 与 WiFi。 */
  FontSurface surface = make_surface(pixels, origin_y);
  const uint16_t wifi_color =
      snapshot->wifi_connected ? kColorPrimary : kColorError;
  font_engine_draw_glyph(&surface, kWifiIconX, kClockIconY, kIconWifi,
                         kIconWidth, kIconHeight, wifi_color, kColorBackground);
}

static void render_clock(uint16_t* pixels, int origin_y,
                         const SystemSnapshot* snapshot) {
  FontSurface surface = make_surface(pixels, origin_y);

  render_clock_header(pixels, origin_y, snapshot);

  /* 天气胶囊：与参考实现一致，位于顶栏图标下方。 */
  draw_card(pixels, origin_y, kWeatherCardX, kWeatherCardY, kWeatherCardWidth,
            kWeatherCardHeight, kColorSurface);
  char weather_text[64];
  uint16_t weather_color = kColorText;
  if (snapshot->weather.valid) {
    if (snapshot->weather.weather[0] != '\0') {
      snprintf(weather_text, sizeof(weather_text), "%s %d℃",
               snapshot->weather.weather, snapshot->weather.temperature);
    } else {
      snprintf(weather_text, sizeof(weather_text), "%d℃",
               snapshot->weather.temperature);
    }
  } else {
    snprintf(weather_text, sizeof(weather_text), "天气暂不可用");
    weather_color = kColorMuted;
  }
  int weather_x = (kUiScreenWidth - font_engine_text_width(weather_text)) / 2;
  if (weather_x < 8) {
    weather_x = 8;
  }
  font_engine_draw_text(&surface, weather_x, kWeatherTextY, weather_text,
                        weather_color, kColorSurface);

  /* 大号时间卡片。 */
  draw_card(pixels, origin_y, kClockCardX, kClockCardY, kClockCardWidth,
            kClockCardHeight, kColorSurface);
  if (snapshot->clock.time_synced) {
    char time_text[32];
    snprintf(time_text, sizeof(time_text), "%02d:%02d:%02d",
             snapshot->clock.hour, snapshot->clock.minute,
             snapshot->clock.second);
    int time_x = (kUiScreenWidth - font_engine_clock_text_width(time_text)) / 2;
    if (time_x < 0) {
      time_x = 0;
    }
    font_engine_draw_clock_text(&surface, time_x, kClockTextY, time_text,
                                kColorPrimary, kColorSurface);
  } else {
    draw_centered_text(pixels, origin_y, 40, "--:--:--", kColorMuted,
                       kColorSurface);
  }

  /* 复合日历大卡片：日期、星期胶囊、农历。 */
  draw_card(pixels, origin_y, kDateCardX, kDateCardY, kDateCardWidth,
            kDateCardHeight, kColorSurface);
  if (snapshot->clock.time_synced) {
    char date_text[32];
    snprintf(date_text, sizeof(date_text), "%d月%02d日", snapshot->clock.month,
             snapshot->clock.day);
    draw_centered_text(pixels, origin_y, kDateTextY, date_text, kColorText,
                       kColorSurface);

    draw_card(pixels, origin_y, kWeekdayCardX, kWeekdayCardY, kWeekdayCardWidth,
              kWeekdayCardHeight, kColorSurfaceHigh);
    const int weekday = snapshot->clock.weekday;
    const char* weekday_name =
        (weekday >= 0 && weekday < 7) ? kWeekdayNames[weekday] : "周?";
    draw_centered_text(pixels, origin_y, kWeekdayTextY, weekday_name,
                       kColorSecondary, kColorSurfaceHigh);

    if (snapshot->lunar.valid && snapshot->lunar.gz_year[0] != '\0' &&
        snapshot->lunar.lunar_md[0] != '\0') {
      char lunar_text[40];
      snprintf(lunar_text, sizeof(lunar_text), "%s %s", snapshot->lunar.gz_year,
               snapshot->lunar.lunar_md);
      draw_centered_text(pixels, origin_y, kLunarTextY, lunar_text,
                         kColorTertiary, kColorSurface);
    }
  } else {
    draw_card(pixels, origin_y, kSyncCardX, kSyncCardY, kSyncCardWidth,
              kSyncCardHeight, kColorSurfaceHigh);
    draw_centered_text(pixels, origin_y, kSyncTextY, "时间同步中",
                       kColorSecondary, kColorSurfaceHigh);
  }
}

static void render_slice(uint16_t* pixels, int slice_y,
                         const SystemSnapshot* snapshot) {
  fill_rect(pixels, slice_y, 0, slice_y, kUiScreenWidth, kDisplaySliceRows,
            kColorBackground);
  if (snapshot->ui_mode == kUiModeClock) {
    render_clock(pixels, slice_y, snapshot);
  } else {
    render_dashboard(pixels, slice_y, snapshot);
  }
}

bool ui_render_init(void) { return font_engine_init(asset_store_font_cn()); }

bool ui_render_draw_dirty(DisplayBackend* backend,
                          const SystemSnapshot* snapshot, const UiRect* rects,
                          int rect_count) {
  if (backend == NULL || snapshot == NULL || rects == NULL || rect_count <= 0) {
    return false;
  }
  if (backend->begin_frame == NULL || backend->write_slice == NULL ||
      backend->end_frame == NULL) {
    return false;
  }
  if (!backend->begin_frame(backend)) {
    return false;
  }

  bool dirty_slices[kSliceCount] = {false};
  for (int index = 0; index < rect_count; ++index) {
    int first_row = rects[index].y;
    int last_row = rects[index].y + rects[index].h;
    if (first_row < 0) {
      first_row = 0;
    }
    if (last_row > kDisplayHeight) {
      last_row = kDisplayHeight;
    }
    if (first_row >= last_row) {
      continue;
    }
    const int first_slice = first_row / kDisplaySliceRows;
    const int last_slice = (last_row - 1) / kDisplaySliceRows;
    for (int slice = first_slice; slice <= last_slice; ++slice) {
      dirty_slices[slice] = true;
    }
  }

  bool ok = true;
  int buffer_index = 0;
  for (int slice = 0; slice < kSliceCount; ++slice) {
    if (!dirty_slices[slice]) {
      continue;
    }
    const int slice_y = slice * kDisplaySliceRows;
    uint16_t* pixels = s_slice_buffers[buffer_index];
    buffer_index ^= 1;
    render_slice(pixels, slice_y, snapshot);
    if (!backend->write_slice(backend, slice_y, kDisplaySliceRows, pixels,
                              kDisplaySlicePixels)) {
      ok = false;
      break;
    }
  }
  backend->end_frame(backend);
  return ok;
}

static void append_dirty_rect(UiRect* rects, int* count, int max_rects, int x,
                              int y, int width, int height) {
  if (*count >= max_rects) {
    return;
  }
  rects[*count].x = (int16_t)x;
  rects[*count].y = (int16_t)y;
  rects[*count].w = (int16_t)width;
  rects[*count].h = (int16_t)height;
  ++*count;
}

int ui_render_collect_dirty_regions(const SystemSnapshot* previous,
                                    const SystemSnapshot* current,
                                    UiRect* out_rects, int max_rects) {
  if (previous == NULL || current == NULL || out_rects == NULL ||
      max_rects <= 0) {
    return 0;
  }

  int count = ui_layout_calculate_dirty_regions(previous, current, out_rects,
                                                max_rects);
  if (current->ui_mode == kUiModeClock &&
      previous->wifi_connected != current->wifi_connected) {
    /* 时钟页顶栏图标不在 ui_layout 的契约里，WiFi 变化时自行补一条。 */
    append_dirty_rect(out_rects, &count, max_rects, 0, 0, kUiScreenWidth,
                      kDisplaySliceRows);
  }

  const bool time_state_changed =
      previous->clock.time_synced != current->clock.time_synced;
  if (current->ui_mode == kUiModeClock && time_state_changed) {
    /* 同步状态切换时，大号时间与骨架占位需要整块重绘。 */
    append_dirty_rect(out_rects, &count, max_rects, kClockCardX, kClockCardY,
                      kClockCardWidth, kClockCardHeight);
  }
  bool card_dirty = time_state_changed;
  for (int i = 0; i < count; ++i) {
    if (out_rects[i].y >= kDateCardY) {
      card_dirty = true;
      break;
    }
  }
  if (current->ui_mode == kUiModeClock && card_dirty) {
    /* ui_layout 的日期脏区只到 y=125，而日历卡铺到 y=150，补整卡。 */
    append_dirty_rect(out_rects, &count, max_rects, kDateCardX, kDateCardY,
                      kDateCardWidth, kDateCardHeight);
  }
  return count;
}

bool ui_render_draw_frame(DisplayBackend* backend,
                          const SystemSnapshot* snapshot) {
  const UiRect full_screen = {0, 0, kUiScreenWidth, kUiScreenHeight};
  return ui_render_draw_dirty(backend, snapshot, &full_screen, 1);
}

#ifdef PICO_ON_DEVICE
static void vTaskUIRender(void* context) {
  (void)context;
  DisplayBackend* backend = st7735s_backend();
  if (!st7735s_init() || !ui_render_init()) {
    printf("显示初始化失败\n");
    vTaskDelete(NULL);
    return;
  }

  SystemSnapshot previous;
  system_state_get_snapshot(&previous);
  if (!ui_render_draw_frame(backend, &previous)) {
    printf("首帧渲染失败\n");
  }

  for (;;) {
    vTaskDelay(pdMS_TO_TICKS(kRenderPeriodMs));
    SystemSnapshot current;
    system_state_get_snapshot(&current);
    UiRect dirty_rects[kMaxDirtyRects];
    const int dirty_count = ui_render_collect_dirty_regions(
        &previous, &current, dirty_rects, kMaxDirtyRects);
    if (dirty_count > 0) {
      ui_render_draw_dirty(backend, &current, dirty_rects, dirty_count);
    }
    previous = current;
  }
}

void ui_render_start_task(void) {
  TaskHandle_t handle = NULL;
  const BaseType_t created =
      xTaskCreate(vTaskUIRender, "vTaskUIRender", kRenderStackWords, NULL,
                  kRenderPriority, &handle);
  configASSERT(created == pdPASS);
  if (created == pdPASS) {
    vTaskCoreAffinitySet(handle, (UBaseType_t)(1u << 1));
  }
}

/* main.c 暂未接线；把入口指针放进链接脚本 KEEP 的 .init_array，
 * 防止 --gc-sections 把 vTaskUIRender 与整个显示链路回收。 */
static void (*const s_ui_render_retain)(void)
    __attribute__((used, section(".init_array"))) = ui_render_start_task;
#endif /* PICO_ON_DEVICE */
