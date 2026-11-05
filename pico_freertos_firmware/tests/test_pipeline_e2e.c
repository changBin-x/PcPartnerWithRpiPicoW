/**
 * @file test_pipeline_e2e.c
 * @brief 端到端串联测试：真实 UDP 报文字节 → 解析 → 状态机 → 渲染成图。
 *
 * 其他测试各自验证一个模块，本测试只做一件事：把 PC 端 encode_packet 产出的
 * 真实字节喂进固件同一条链路，最后落成一张 PPM 画面。它回答的问题是
 * 「这几个模块接在一起还对不对」，而单个模块测试回答不了这个问题。
 */

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "asset_store.h"
#include "metrics_parser.h"
#include "st7735s_driver.h"
#include "system_state.h"
#include "ui_render.h"

#include "generated/udp_packets_data.h"

enum {
  kFramebufferPixels = kDisplayWidth * kDisplayHeight,
  kPipelineBackground = 0x1083,
  kPpmMaxValue = 255,
  kColourChannelBits = 8,
};

/* 宿主端显示后端：把每个切片写进内存帧缓冲，不涉及 SPI。 */
typedef struct {
  uint16_t framebuffer[kFramebufferPixels];
  DisplayBackend backend;
  int slice_calls;
  int rows_written;
} MemoryBackend;

static MemoryBackend* backend_memory(DisplayBackend* backend) {
  return (MemoryBackend*)backend->context;
}

static bool memory_begin_frame(DisplayBackend* backend) {
  (void)backend;
  return true;
}

static bool memory_write_slice(DisplayBackend* backend, int y, int height,
                               const uint16_t* pixels, size_t pixel_count) {
  MemoryBackend* memory = backend_memory(backend);
  if (y < 0 || height <= 0 || y + height > kDisplayHeight) {
    return false;
  }
  if (pixel_count != (size_t)height * kDisplayWidth) {
    return false;
  }
  ++memory->slice_calls;
  memory->rows_written += height;
  memcpy(&memory->framebuffer[y * kDisplayWidth], pixels,
         pixel_count * sizeof(uint16_t));
  return true;
}

static void memory_end_frame(DisplayBackend* backend) {
  (void)backend_memory(backend);
}

static void memory_init(MemoryBackend* memory) {
  memset(memory, 0, sizeof(*memory));
  memory->backend.context = memory;
  memory->backend.begin_frame = memory_begin_frame;
  memory->backend.write_slice = memory_write_slice;
  memory->backend.end_frame = memory_end_frame;
}

/* 统计某矩形内不是背景色的像素数，用来断言「这块区域真的画了东西」。 */
static int count_region_non_background(const uint16_t* framebuffer, int x,
                                       int y, int width, int height,
                                       uint16_t background) {
  int count = 0;
  for (int row = y; row < y + height; ++row) {
    for (int column = x; column < x + width; ++column) {
      if (framebuffer[row * kDisplayWidth + column] != background) {
        ++count;
      }
    }
  }
  return count;
}

static bool write_ppm(const char* path, const uint16_t* framebuffer) {
  FILE* file = fopen(path, "wb");
  if (file == NULL) {
    return false;
  }
  fprintf(file, "P6\n%d %d\n%d\n", kDisplayWidth, kDisplayHeight, kPpmMaxValue);
  for (int i = 0; i < kFramebufferPixels; ++i) {
    const uint16_t pixel = framebuffer[i];
    const unsigned int red =
        (unsigned int)(((pixel >> 11) & 0x1f) * kPpmMaxValue / 0x1f);
    const unsigned int green =
        (unsigned int)(((pixel >> 5) & 0x3f) * kPpmMaxValue / 0x3f);
    const unsigned int blue =
        (unsigned int)((pixel & 0x1f) * kPpmMaxValue / 0x1f);
    const unsigned char rgb[3] = {(unsigned char)red, (unsigned char)green,
                                  (unsigned char)blue};
    if (fwrite(rgb, sizeof(rgb), 1, file) != 1) {
      fclose(file);
      return false;
    }
  }
  fclose(file);
  return true;
}

/* 把生成的报文样本逐条走完整链路：解析成功才写状态机。
 * 返回值是成功写入状态机的报文条数。 */
static int feed_packets_through_pipeline(void) {
  static MetricsParser parser;
  metrics_parser_init(&parser);

  int accepted = 0;
  uint32_t tick = 1000;
  for (unsigned int i = 0; i < kUdpPacketCaseCount; ++i) {
    const UdpPacketCase* item = &kUdpPacketCases[i];
    PcMetricsData metrics;
    memset(&metrics, 0, sizeof(metrics));
    const MetricsParseResult result =
        metrics_parser_parse(&parser, item->packet, item->length, &metrics);
    /* 这里只关心「接受/拒绝」这条二分边界，具体错误码由
     * test_protocol_conformance 逐条负责。解析器是有状态的：seq 一旦推进，
     * 后续序号更小的非法包会先被判为过期，而不是按字段错误码返回。 */
    if ((result == kMetricsParseOk) != (item->expected == 0)) {
      printf("FAIL: 报文「%s」返回码 %d，期望%s\n", item->name, (int)result,
             item->expected == 0 ? "接受" : "拒绝");
      return -1;
    }
    if (result == kMetricsParseOk) {
      system_state_update_metrics(&metrics, tick);
      ++accepted;
    }
    tick += 100;
  }
  /* 状态机在 tick=1000 收到第一条指标，最后一条在 tick=1400。 */
  system_state_check_timeouts(tick);
  return accepted;
}

static void test_pipeline_dashboard(void) {
  assert(asset_store_init());
  system_state_init();
  assert(ui_render_init());

  const int accepted = feed_packets_through_pipeline();
  assert(accepted == 5);

  SystemSnapshot snapshot;
  system_state_get_snapshot(&snapshot);
  assert(snapshot.ui_mode == kUiModeDashboard);
  assert(snapshot.pc_online);
  /* 最后一条合法报文是浮点百分比那条：cpu 42.5 / gpu 0.0 / ram 61.25。 */
  assert(snapshot.metrics.cpu > 42.4f && snapshot.metrics.cpu < 42.6f);
  assert(snapshot.metrics.ram > 61.2f && snapshot.metrics.ram < 61.3f);
  assert(snapshot.metrics.seq == 5u);

  MemoryBackend memory;
  memory_init(&memory);
  assert(ui_render_draw_frame(&memory.backend, &snapshot));
  assert(memory.slice_calls == 10);
  assert(memory.rows_written == kDisplayHeight);

  /* 真正端到端的断言：整屏被画上了东西，且三条进度条区域都非空。 */
  assert(count_region_non_background(memory.framebuffer, 0, 0, kDisplayWidth,
                                     kDisplayHeight,
                                     kPipelineBackground) > 4000);
  assert(count_region_non_background(memory.framebuffer, 8, 34, 112, 12,
                                     kPipelineBackground) > 30);
  assert(count_region_non_background(memory.framebuffer, 8, 76, 112, 12,
                                     kPipelineBackground) > 30);
  assert(count_region_non_background(memory.framebuffer, 8, 118, 112, 12,
                                     kPipelineBackground) > 30);
  assert(write_ppm("/tmp/pipeline_dashboard.ppm", memory.framebuffer));
  printf("PASS: 端到端仪表盘（5 条真实报文 → 状态机 → 渲染）\n");
}

static void test_pipeline_switch_to_clock(void) {
  /* 沿用上一条测试留下的状态机内容，只把时钟与天气补齐，然后让指标超时。 */
  ClockData clock;
  memset(&clock, 0, sizeof(clock));
  clock.year = 2026;
  clock.month = 11;
  clock.day = 6;
  clock.hour = 12;
  clock.minute = 34;
  clock.second = 56;
  clock.weekday = 5; /* 0 = 周日，故周五为 5。 */
  clock.time_synced = true;
  system_state_update_clock(&clock);

  LunarData lunar;
  memset(&lunar, 0, sizeof(lunar));
  snprintf(lunar.gz_year, sizeof(lunar.gz_year), "丙午年");
  snprintf(lunar.lunar_md, sizeof(lunar.lunar_md), "十月初七");
  lunar.valid = true;
  system_state_update_lunar(&lunar);

  WeatherData weather;
  memset(&weather, 0, sizeof(weather));
  snprintf(weather.city, sizeof(weather.city), "宁波");
  snprintf(weather.weather, sizeof(weather.weather), "晴");
  weather.temperature = 23;
  weather.valid = true;
  system_state_update_weather(&weather);

  /* 指标最后更新在 tick=1400，超时阈值 3000，故 4401 才真正超时。 */
  system_state_check_timeouts(4401);

  SystemSnapshot snapshot;
  system_state_get_snapshot(&snapshot);
  assert(snapshot.ui_mode == kUiModeClock);
  assert(!snapshot.pc_online);

  MemoryBackend memory;
  memory_init(&memory);
  assert(ui_render_draw_frame(&memory.backend, &snapshot));
  assert(memory.slice_calls == 10);

  /* 大号时间、日期、星期胶囊、农历四块都必须有内容。 */
  assert(count_region_non_background(memory.framebuffer, 4, 30, 120, 40,
                                     kPipelineBackground) > 300);
  assert(count_region_non_background(memory.framebuffer, 6, 77, 116, 14,
                                     kPipelineBackground) > 10);
  assert(count_region_non_background(memory.framebuffer, 36, 96, 56, 14,
                                     kPipelineBackground) > 10);
  assert(count_region_non_background(memory.framebuffer, 6, 115, 116, 14,
                                     kPipelineBackground) > 10);
  assert(write_ppm("/tmp/pipeline_clock.ppm", memory.framebuffer));
  printf("PASS: 端到端时钟页（指标超时自动切页 + 日历渲染）\n");
}

int main(void) {
  test_pipeline_dashboard();
  test_pipeline_switch_to_clock();
  printf("PASS: test_pipeline_e2e\n");
  return 0;
}
