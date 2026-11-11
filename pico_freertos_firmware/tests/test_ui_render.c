// v2 渲染层回归测试：用内存后端把 10 屏各画一遍，检查底色、文字像素与脏区。

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "asset_store.h"
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
  if (count != (size_t)(kDisplayWidth * height)) {
    return false;
  }
  memcpy(&g_frame[y * kDisplayWidth], pixels, count * sizeof(uint16_t));
  return true;
}

static void mem_end(DisplayBackend* self) { (void)self; }

static DisplayBackend make_backend(void) {
  DisplayBackend backend = {"mem", mem_begin, mem_write, mem_end, NULL};
  return backend;
}

/** @brief 统计某个矩形内与底色不同的像素数。 */
static int count_ink(int x, int y, int w, int h, uint16_t bg) {
  int total = 0;
  for (int row = y; row < y + h; ++row) {
    for (int col = x; col < x + w; ++col) {
      if (g_frame[row * kDisplayWidth + col] != bg) {
        ++total;
      }
    }
  }
  return total;
}

static void fill_snapshot(SystemSnapshot* snap) {
  system_state_init();
  system_state_update_metrics(&(PcMetricsData){.seq = 7,
                                               .ts = 1,
                                               .cpu = 43.0f,
                                               .gpu = 0.0f,
                                               .gpu_valid = false,
                                               .ram = 61.0f,
                                               .rx_mbps = 1.2f,
                                               .tx_mbps = 0.4f},
                              1000);
  system_state_update_clock(&(ClockData){.year = 2026,
                                         .month = 11,
                                         .day = 6,
                                         .hour = 12,
                                         .minute = 34,
                                         .second = 56,
                                         .weekday = 5,
                                         .time_synced = true});
  system_state_update_lunar(
      &(LunarData){.gz_year = "丙午年", .lunar_md = "十月初七", .valid = true});
  system_state_update_weather(&(WeatherData){.city = "宁波",
                                             .weather = "晴",
                                             .temperature = 23,
                                             .is_stale = false,
                                             .valid = true});
  system_state_set_wifi_status(true);
  system_state_get_snapshot(snap);
}

static void test_all_screens_render(void) {
  SystemSnapshot snap;
  fill_snapshot(&snap);
  for (int theme = 0; theme < kThemeCount; ++theme) {
    for (int mode = 0; mode < 2; ++mode) {
      snap.theme_id = (UiThemeId)theme;
      snap.ui_mode = (mode == 0) ? kUiModeDashboard : kUiModeClock;
      memset(g_frame, 0, sizeof(g_frame));
      CHECK(ui_render_draw_frame(NULL, &snap) == false);
      DisplayBackend backend = make_backend();
      CHECK(ui_render_draw_frame(&backend, &snap));
      const UiThemePalette* palette = ui_theme_get((UiThemeId)theme);
      /* 底色必须占多数（顶栏等元素会覆盖左上角，不能只看第 0 个像素）。 */
      int bg_count = 0;
      int colors = 0;
      for (int i = 0; i < kDisplayWidth * kDisplayHeight; ++i) {
        if (g_frame[i] == palette->bg) {
          ++bg_count;
        }
        int seen = 0;
        for (int j = 0; j < i; ++j) {
          if (g_frame[j] == g_frame[i]) {
            seen = 1;
            break;
          }
        }
        if (!seen) {
          ++colors;
        }
      }
      CHECK(bg_count >= 200);
      CHECK(colors >= 4);
      /* 每屏都必须真的画出东西：整屏不能只剩底色。 */
      int ink = count_ink(0, 0, kDisplayWidth, kDisplayHeight, palette->bg);
      CHECK(ink > 200);
      CHECK(ink < kDisplayWidth * kDisplayHeight);
    }
  }
}

static void test_themes_differ(void) {
  SystemSnapshot snap;
  fill_snapshot(&snap);
  snap.ui_mode = kUiModeDashboard;
  uint32_t sums[kThemeCount];
  for (int theme = 0; theme < kThemeCount; ++theme) {
    snap.theme_id = (UiThemeId)theme;
    DisplayBackend backend = make_backend();
    CHECK(ui_render_draw_frame(&backend, &snap));
    uint32_t sum = 2166136261u;
    for (int i = 0; i < kDisplayWidth * kDisplayHeight; ++i) {
      sum = (sum ^ g_frame[i]) * 16777619u;
    }
    sums[theme] = sum;
  }
  for (int i = 0; i < kThemeCount; ++i) {
    for (int j = i + 1; j < kThemeCount; ++j) {
      CHECK(sums[i] != sums[j]);
    }
  }
}

static void test_dirty_regions(void) {
  SystemSnapshot a;
  SystemSnapshot b;
  fill_snapshot(&a);
  b = a;
  UiRect rects[8];
  CHECK(ui_render_collect_dirty_regions(&a, &b, rects, 8) == 0);
  b.clock.second = (b.clock.second + 1) % 60;
  CHECK(ui_render_collect_dirty_regions(&a, &b, rects, 8) == 1);
  CHECK(rects[0].w == kDisplayWidth);
  CHECK(rects[0].h == kDisplayHeight);
  b = a;
  b.theme_id = kThemeTui;
  CHECK(ui_render_collect_dirty_regions(&a, &b, rects, 8) == 1);
  b = a;
  b.ui_mode = kUiModeClock;
  CHECK(ui_render_collect_dirty_regions(&a, &b, rects, 8) == 1);
  CHECK(ui_render_collect_dirty_regions(NULL, &a, rects, 8) == 1);
  CHECK(ui_render_collect_dirty_regions(&a, &b, NULL, 8) == 0);
  CHECK(ui_render_collect_dirty_regions(&a, &b, rects, 0) == 0);
}

static void test_clock_text_visible(void) {
  SystemSnapshot snap;
  fill_snapshot(&snap);
  snap.theme_id = kThemeSwiss;
  snap.ui_mode = kUiModeClock;
  DisplayBackend backend = make_backend();
  CHECK(ui_render_draw_frame(&backend, &snap));
  const UiThemePalette* palette = ui_theme_get(kThemeSwiss);
  /* 时钟大字占用的中段必须有点亮的像素。 */
  CHECK(count_ink(0, 18, kDisplayWidth, 30, palette->bg) > 80);
}

static void test_argument_validation(void) {
  SystemSnapshot snap;
  fill_snapshot(&snap);
  CHECK(ui_render_draw_frame(NULL, &snap) == false);
  DisplayBackend backend = make_backend();
  CHECK(ui_render_draw_frame(&backend, NULL) == false);
  UiRect rect = {0, 0, 1, 1};
  CHECK(ui_render_draw_dirty(&backend, &snap, NULL, 1) == false);
  CHECK(ui_render_draw_dirty(&backend, &snap, &rect, 0) == false);
}

int main(void) {
  if (!asset_store_init()) {
    fprintf(stderr, "资源初始化失败\n");
    return 1;
  }
  if (!ui_render_init()) {
    fprintf(stderr, "字库初始化失败\n");
    return 1;
  }
  test_all_screens_render();
  test_themes_differ();
  test_dirty_regions();
  test_clock_text_visible();
  test_argument_validation();
  if (g_failures != 0) {
    fprintf(stderr, "%d 项失败\n", g_failures);
    return 1;
  }
  printf("test_ui_render: 全部通过\n");
  return 0;
}
