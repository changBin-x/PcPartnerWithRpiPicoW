// 端到端：真实报文 → 解析 → 状态机 → 渲染成图，覆盖五套主题的仪表盘页。

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "asset_store.h"
#include "metrics_parser.h"
#include "st7735s_driver.h"
#include "system_state.h"
#include "ui_render.h"
#include "ui_theme.h"

static int g_failures;
static uint16_t g_frame[kDisplayWidth * kDisplayHeight];

#define CHECK(condition)                                                   \
  do {                                                                     \
    if (!(condition)) {                                                    \
      fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
      ++g_failures;                                                        \
    }                                                                      \
  } while (0)

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
  memcpy(&g_frame[y * kDisplayWidth], pixels, count * sizeof(uint16_t));
  return true;
}

static void mem_end(DisplayBackend* self) { (void)self; }

int main(void) {
  if (!asset_store_init() || !ui_render_init()) {
    fprintf(stderr, "资源初始化失败\n");
    return 1;
  }
  const char* packet =
      "{\"v\":1,\"seq\":42,\"ts\":1700000000,\"cpu\":43.0,\"gpu\":0.0,"
      "\"ram\":61.0,\"rx\":1.2,\"tx\":0.4}";
  MetricsParser parser;
  metrics_parser_init(&parser);
  PcMetricsData data;
  CHECK(metrics_parser_parse(&parser, packet, strlen(packet), &data) ==
        kMetricsParseOk);
  CHECK(data.seq == 42);
  CHECK(data.cpu > 42.9f && data.cpu < 43.1f);

  system_state_init();
  system_state_update_metrics(&data, 1000);
  SystemSnapshot snap;
  system_state_get_snapshot(&snap);
  CHECK(snap.pc_online);
  CHECK(snap.ui_mode == kUiModeDashboard);

  DisplayBackend backend = {"mem", mem_begin, mem_write, mem_end, NULL};
  uint16_t first_bg = 0;
  for (int theme = 0; theme < kThemeCount; ++theme) {
    system_state_set_theme((UiThemeId)theme);
    system_state_get_snapshot(&snap);
    CHECK(snap.theme_id == (UiThemeId)theme);
    memset(g_frame, 0, sizeof(g_frame));
    CHECK(ui_render_draw_frame(&backend, &snap));
    const UiThemePalette* palette = ui_theme_get((UiThemeId)theme);
    int bg_count = 0;
    for (int i = 0; i < kDisplayWidth * kDisplayHeight; ++i) {
      if (g_frame[i] == palette->bg) {
        ++bg_count;
      }
    }
    CHECK(bg_count >= 200);
    if (theme == 0) {
      first_bg = palette->bg;
    } else {
      CHECK(palette->bg != first_bg);
    }
  }

  /* 指标超时后必须回到时钟页。 */
  system_state_check_timeouts(1000 + 3001);
  system_state_get_snapshot(&snap);
  CHECK(!snap.pc_online);
  CHECK(snap.ui_mode == kUiModeClock);
  CHECK(ui_render_draw_frame(&backend, &snap));

  if (g_failures != 0) {
    fprintf(stderr, "%d 项失败\n", g_failures);
    return 1;
  }
  printf("test_pipeline_e2e: 全部通过\n");
  return 0;
}
