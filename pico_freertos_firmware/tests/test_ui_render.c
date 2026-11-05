#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "asset_store.h"
#include "st7735s_driver.h"
#include "system_state.h"
#include "ui_layout.h"
#include "ui_render.h"

enum {
  kFramebufferPixels = kDisplayWidth * kDisplayHeight,
  kTestBackground = 0x1083,
  kTestSurface = 0x1904,
  kTestSurfaceHigh = 0x2945,
  kTestTrack = 0x31a7,
  kTestText = 0xe71d,
  kTestMuted = 0x9493,
  kTestPrimary = 0xae3f,
  kTestSecondary = 0xa699,
  kTestTertiary = 0xf5f1,
  kTestError = 0xf5d6,
  kGuardBefore = 0x13572468,
  kGuardAfter = 0x24681357,
};

/* 宿主端的显示后端：把切片写进内存帧缓冲，并统计调用顺序。 */
typedef struct {
  uint32_t guard_before;
  uint16_t framebuffer[kFramebufferPixels];
  uint32_t guard_after;
  DisplayBackend backend;
  int begin_calls;
  int end_calls;
  int slice_calls;
  int rows_written;
} MemoryBackend;

static MemoryBackend* backend_memory(DisplayBackend* backend) {
  return (MemoryBackend*)backend->context;
}

static bool memory_begin_frame(DisplayBackend* backend) {
  ++backend_memory(backend)->begin_calls;
  return true;
}

static bool memory_write_slice(DisplayBackend* backend, int y, int height,
                               const uint16_t* pixels, size_t pixel_count) {
  MemoryBackend* memory = backend_memory(backend);
  ++memory->slice_calls;
  if (y < 0 || height <= 0 || y + height > kDisplayHeight) {
    return false;
  }
  if (pixel_count != (size_t)kDisplayWidth * (size_t)height) {
    return false;
  }
  memcpy(&memory->framebuffer[(size_t)y * (size_t)kDisplayWidth], pixels,
         pixel_count * sizeof(uint16_t));
  memory->rows_written += height;
  return true;
}

static void memory_end_frame(DisplayBackend* backend) {
  ++backend_memory(backend)->end_calls;
}

static void memory_init(MemoryBackend* memory) {
  memset(memory, 0, sizeof(*memory));
  memory->guard_before = kGuardBefore;
  memory->guard_after = kGuardAfter;
  memory->backend.name = "memory";
  memory->backend.begin_frame = memory_begin_frame;
  memory->backend.write_slice = memory_write_slice;
  memory->backend.end_frame = memory_end_frame;
  memory->backend.context = memory;
}

static void memory_reset_stats(MemoryBackend* memory) {
  memory->begin_calls = 0;
  memory->end_calls = 0;
  memory->slice_calls = 0;
  memory->rows_written = 0;
}

static void assert_guards(const MemoryBackend* memory) {
  assert(memory->guard_before == kGuardBefore);
  assert(memory->guard_after == kGuardAfter);
}

static SystemSnapshot make_dashboard(float cpu, float gpu, float ram, float rx,
                                     float tx) {
  SystemSnapshot snapshot;
  memset(&snapshot, 0, sizeof(snapshot));
  snapshot.ui_mode = kUiModeDashboard;
  snapshot.pc_online = true;
  snapshot.wifi_connected = true;
  snapshot.metrics.cpu = cpu;
  snapshot.metrics.gpu = gpu;
  snapshot.metrics.gpu_valid = true;
  snapshot.metrics.ram = ram;
  snapshot.metrics.rx_mbps = rx;
  snapshot.metrics.tx_mbps = tx;
  return snapshot;
}

static SystemSnapshot make_clock(void) {
  SystemSnapshot snapshot;
  memset(&snapshot, 0, sizeof(snapshot));
  snapshot.ui_mode = kUiModeClock;
  snapshot.pc_online = true;
  snapshot.wifi_connected = true;
  snapshot.clock.time_synced = true;
  snapshot.clock.year = 2026;
  snapshot.clock.month = 11;
  snapshot.clock.day = 6;
  snapshot.clock.hour = 12;
  snapshot.clock.minute = 34;
  snapshot.clock.second = 56;
  snapshot.clock.weekday = 5;
  snapshot.lunar.valid = true;
  snprintf(snapshot.lunar.gz_year, sizeof(snapshot.lunar.gz_year), "%s",
           "丙午年");
  snprintf(snapshot.lunar.lunar_md, sizeof(snapshot.lunar.lunar_md), "%s",
           "十月初七");
  snapshot.weather.valid = true;
  snprintf(snapshot.weather.weather, sizeof(snapshot.weather.weather), "%s",
           "晴");
  snapshot.weather.temperature = 23;
  return snapshot;
}

static int count_region_color(const uint16_t* framebuffer, int x, int y,
                              int width, int height, uint16_t color) {
  int result = 0;
  for (int row = y; row < y + height; ++row) {
    for (int column = x; column < x + width; ++column) {
      if (framebuffer[row * kDisplayWidth + column] == color) {
        ++result;
      }
    }
  }
  return result;
}

static int count_region_non_background(const uint16_t* framebuffer, int x,
                                       int y, int width, int height,
                                       uint16_t background) {
  int result = 0;
  for (int row = y; row < y + height; ++row) {
    for (int column = x; column < x + width; ++column) {
      if (framebuffer[row * kDisplayWidth + column] != background) {
        ++result;
      }
    }
  }
  return result;
}

static int count_region_difference(const uint16_t* left, const uint16_t* right,
                                   int x, int y, int width, int height) {
  int result = 0;
  for (int row = y; row < y + height; ++row) {
    for (int column = x; column < x + width; ++column) {
      if (left[row * kDisplayWidth + column] !=
          right[row * kDisplayWidth + column]) {
        ++result;
      }
    }
  }
  return result;
}

static int count_total_color(const uint16_t* framebuffer, uint16_t color) {
  return count_region_color(framebuffer, 0, 0, kDisplayWidth, kDisplayHeight,
                            color);
}

static bool write_ppm(const char* path, const uint16_t* framebuffer) {
  FILE* file = fopen(path, "wb");
  if (file == NULL) {
    return false;
  }
  bool ok =
      fprintf(file, "P6\n%d %d\n255\n", kDisplayWidth, kDisplayHeight) > 0;
  for (int row = 0; row < kDisplayHeight && ok; ++row) {
    for (int column = 0; column < kDisplayWidth && ok; ++column) {
      const uint16_t pixel = framebuffer[row * kDisplayWidth + column];
      const uint8_t rgb[3] = {
          (uint8_t)(((pixel >> 11) & 0x1f) * 255 / 31),
          (uint8_t)(((pixel >> 5) & 0x3f) * 255 / 63),
          (uint8_t)((pixel & 0x1f) * 255 / 31),
      };
      ok = fwrite(rgb, sizeof(rgb), 1, file) == 1;
    }
  }
  if (fclose(file) != 0) {
    ok = false;
  }
  return ok;
}

static void test_argument_validation(const SystemSnapshot* snapshot) {
  MemoryBackend memory;
  memory_init(&memory);
  const UiRect rect = {0, 0, 16, 16};
  assert(!ui_render_draw_frame(NULL, snapshot));
  assert(!ui_render_draw_frame(&memory.backend, NULL));
  assert(!ui_render_draw_dirty(&memory.backend, snapshot, &rect, 0));
  assert(!ui_render_draw_dirty(&memory.backend, snapshot, NULL, 1));

  DisplayBackend incomplete = memory.backend;
  incomplete.write_slice = NULL;
  assert(!ui_render_draw_frame(&incomplete, snapshot));
  assert(memory.begin_calls == 0);
  assert(memory.slice_calls == 0);
  assert_guards(&memory);
}

static void test_dashboard(void) {
  MemoryBackend memory;
  memory_init(&memory);
  const SystemSnapshot snapshot =
      make_dashboard(42.0f, 55.5f, 63.4f, 12.34f, 0.25f);

  assert(ui_render_draw_frame(&memory.backend, &snapshot));
  assert(memory.begin_calls == 1);
  assert(memory.end_calls == 1);
  assert(memory.slice_calls == 10);
  assert(memory.rows_written == kDisplayHeight);
  assert_guards(&memory);

  assert(count_region_non_background(memory.framebuffer, 0, 0, kDisplayWidth,
                                     kDisplayHeight, kTestBackground) > 4000);
  assert(count_total_color(memory.framebuffer, kTestSurface) > 1000);
  assert(count_region_non_background(memory.framebuffer, 0, 0, 128, 16,
                                     kTestBackground) > 100);
  assert(count_region_non_background(memory.framebuffer, 10, 23, 40, 14,
                                     kTestSurface) > 20);
  assert(count_region_non_background(memory.framebuffer, 4, 128, 124, 28,
                                     kTestBackground) > 50);
  assert(count_total_color(memory.framebuffer, kTestPrimary) > 40);
  assert(count_total_color(memory.framebuffer, kTestSecondary) > 40);
  assert(count_total_color(memory.framebuffer, kTestTertiary) > 40);
  assert(count_total_color(memory.framebuffer, kTestTrack) > 300);

  assert(write_ppm("/tmp/ui_dashboard.ppm", memory.framebuffer));
}

static void test_dashboard_offline(void) {
  MemoryBackend memory;
  memory_init(&memory);
  SystemSnapshot snapshot = make_dashboard(0.0f, 0.0f, 0.0f, 0.0f, 0.0f);
  snapshot.pc_online = false;
  snapshot.wifi_connected = false;
  snapshot.metrics.gpu_valid = false;

  assert(ui_render_draw_frame(&memory.backend, &snapshot));
  assert(count_total_color(memory.framebuffer, kTestError) > 20);
  assert_guards(&memory);
}

static void test_cpu_dirty_region(void) {
  const SystemSnapshot low = make_dashboard(10.0f, 55.0f, 40.0f, 1.0f, 0.5f);
  SystemSnapshot high = low;
  high.metrics.cpu = 90.0f;

  MemoryBackend low_frame;
  memory_init(&low_frame);
  MemoryBackend high_frame;
  memory_init(&high_frame);
  assert(ui_render_draw_frame(&low_frame.backend, &low));
  assert(ui_render_draw_frame(&high_frame.backend, &high));

  /* 只有 CPU 区域变化，顶栏和网速区必须逐像素一致。 */
  assert(count_region_difference(low_frame.framebuffer, high_frame.framebuffer,
                                 4, 20, 120, 32) > 100);
  assert(count_region_difference(low_frame.framebuffer, high_frame.framebuffer,
                                 0, 0, 128, 16) == 0);
  assert(count_region_difference(low_frame.framebuffer, high_frame.framebuffer,
                                 0, 64, 128, 96) == 0);

  UiRect rects[8];
  const int rect_count =
      ui_layout_calculate_dirty_regions(&low, &high, rects, 8);
  assert(rect_count == 1);
  assert(rects[0].x == 4);
  assert(rects[0].y == 20);
  assert(rects[0].w == 120);
  assert(rects[0].h == 32);

  /* 脏区只覆盖 3 个切片（16..63 行），其余切片保持原样本。 */
  MemoryBackend dirty;
  memory_init(&dirty);
  assert(ui_render_draw_frame(&dirty.backend, &low));
  memory_reset_stats(&dirty);
  assert(ui_render_draw_dirty(&dirty.backend, &high, rects, rect_count));
  assert(dirty.begin_calls == 1);
  assert(dirty.end_calls == 1);
  assert(dirty.slice_calls == 3);
  assert(dirty.rows_written == 48);
  assert(count_region_difference(low_frame.framebuffer, dirty.framebuffer, 4,
                                 20, 120, 32) > 100);
  assert(count_region_difference(low_frame.framebuffer, dirty.framebuffer, 0, 0,
                                 128, 16) == 0);
  assert(count_region_difference(low_frame.framebuffer, dirty.framebuffer, 0,
                                 64, 128, 96) == 0);
  assert_guards(&dirty);
}

static void test_clipped_dirty_rect(void) {
  MemoryBackend memory;
  memory_init(&memory);
  const SystemSnapshot snapshot =
      make_dashboard(30.0f, 40.0f, 50.0f, 2.0f, 1.0f);
  const UiRect clipped = {-10, -5, 20, 10};
  assert(ui_render_draw_dirty(&memory.backend, &snapshot, &clipped, 1));
  assert(memory.slice_calls == 1);
  assert(memory.rows_written == kDisplaySliceRows);
  assert_guards(&memory);
}

static void test_clock(void) {
  MemoryBackend memory;
  memory_init(&memory);
  const SystemSnapshot snapshot = make_clock();

  assert(ui_render_draw_frame(&memory.backend, &snapshot));
  assert(memory.slice_calls == 10);
  /* 大号数字区域必须有足够多的 PRIMARY 像素。 */
  assert(count_region_color(memory.framebuffer, 0, 30, 128, 40, kTestPrimary) >
         150);
  /* 天气胶囊位于顶栏图标下方，文字不应为空。 */
  assert(count_region_non_background(memory.framebuffer, 8, 15, 112, 14,
                                     kTestSurface) > 10);
  /* 日期、星期胶囊、农历三行都要有内容。 */
  assert(count_region_non_background(memory.framebuffer, 6, 77, 116, 14,
                                     kTestSurface) > 10);
  assert(count_region_non_background(memory.framebuffer, 36, 96, 56, 14,
                                     kTestSurfaceHigh) > 10);
  assert(count_region_non_background(memory.framebuffer, 6, 115, 116, 14,
                                     kTestSurface) > 10);
  /* 日历卡必须铺到 y=150，底部不能是空白背景。 */
  assert(count_region_color(memory.framebuffer, 4, 140, 120, 9, kTestSurface) >
         900);
  /* 时钟页只画 WiFi 图标，不画 PC 图标。 */
  assert(count_region_color(memory.framebuffer, 108, 0, 16, 14, kTestPrimary) >
         10);
  assert(count_region_non_background(memory.framebuffer, 4, 0, 16, 14,
                                     kTestBackground) == 0);
  assert(write_ppm("/tmp/ui_clock.ppm", memory.framebuffer));
  assert_guards(&memory);
}

static void test_clock_without_time(void) {
  MemoryBackend memory;
  memory_init(&memory);
  SystemSnapshot snapshot = make_clock();
  snapshot.clock.time_synced = false;
  snapshot.weather.valid = false;
  snapshot.lunar.valid = false;

  assert(ui_render_draw_frame(&memory.backend, &snapshot));
  assert(count_region_color(memory.framebuffer, 0, 30, 128, 40, kTestPrimary) ==
         0);
  /* 未同步时仍然有时间占位、天气占位与骨架胶囊。 */
  assert(count_region_non_background(memory.framebuffer, 0, 30, 128, 40,
                                     kTestSurface) > 10);
  assert(count_region_non_background(memory.framebuffer, 8, 15, 112, 14,
                                     kTestSurface) > 10);
  assert(count_region_non_background(memory.framebuffer, 24, 94, 80, 20,
                                     kTestSurfaceHigh) > 10);
  assert(write_ppm("/tmp/ui_clock_no_time.ppm", memory.framebuffer));
  assert_guards(&memory);
}

static void test_clock_dirty_extras(void) {
  SystemSnapshot previous = make_clock();
  SystemSnapshot current = previous;
  UiRect rects[8];

  assert(ui_render_collect_dirty_regions(&previous, &current, rects, 8) == 0);

  current.wifi_connected = false;
  int count = ui_render_collect_dirty_regions(&previous, &current, rects, 8);
  assert(count == 1);
  assert(rects[0].x == 0 && rects[0].y == 0 && rects[0].w == kUiScreenWidth &&
         rects[0].h == kDisplaySliceRows);

  current = previous;
  current.clock.day = 7;
  snprintf(current.lunar.lunar_md, sizeof(current.lunar.lunar_md), "%s",
           "十月初八");
  count = ui_render_collect_dirty_regions(&previous, &current, rects, 8);
  assert(count == 2);
  bool found_card = false;
  for (int i = 0; i < count; ++i) {
    if (rects[i].x == 4 && rects[i].y == 70 && rects[i].w == 120 &&
        rects[i].h == 80) {
      found_card = true;
    }
  }
  assert(found_card);

  current = previous;
  current.clock.time_synced = false;
  count = ui_render_collect_dirty_regions(&previous, &current, rects, 8);
  assert(count == 2);
  bool found_time_card = false;
  found_card = false;
  for (int i = 0; i < count; ++i) {
    if (rects[i].x == 4 && rects[i].y == 30 && rects[i].w == 120 &&
        rects[i].h == 40) {
      found_time_card = true;
    }
    if (rects[i].x == 4 && rects[i].y == 70 && rects[i].w == 120 &&
        rects[i].h == 80) {
      found_card = true;
    }
  }
  assert(found_time_card);
  assert(found_card);
}

int main(void) {
  assert(asset_store_init());
  assert(ui_render_init());

  const SystemSnapshot snapshot =
      make_dashboard(42.0f, 55.5f, 63.4f, 12.34f, 0.25f);
  test_argument_validation(&snapshot);
  test_dashboard();
  test_dashboard_offline();
  test_cpu_dirty_region();
  test_clipped_dirty_rect();
  test_clock();
  test_clock_without_time();
  test_clock_dirty_extras();
  return 0;
}
