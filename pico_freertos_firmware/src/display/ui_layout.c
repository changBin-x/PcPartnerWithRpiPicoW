/**
 * @file ui_layout.c
 * @brief 界面布局与局部脏矩形（Dirty Region）变更检测引擎。
 *
 * 渲染线程每帧拿两份快照（上一帧 / 当前帧），由本模块比较出真正变化的
 * 区域，只把脏矩形推给 LCD，避免整屏重绘。屏幕 128x160，原点左上。
 *
 * 布局约定：
 *  - 仪表盘：顶栏 y=0..15，CPU/GPU/RAM 三张卡片各高 32，底部网速行高 28。
 *  - 时钟页：天气行 y=0..24，大号时间 y=30..69，日期+农历卡 y=75..124。
 */

#include "ui_layout.h"

#include <string.h>

/* 追加一个矩形；写满 max_rects 即静默丢弃，保证不越界。 */
static void push_rect(UiRect* out_rects, int max_rects, int* count, int16_t x,
                      int16_t y, int16_t w, int16_t h) {
  if (*count >= max_rects) {
    return;
  }

  out_rects[*count].x = x;
  out_rects[*count].y = y;
  out_rects[*count].w = w;
  out_rects[*count].h = h;
  *count += 1;
}

int ui_layout_calculate_dirty_regions(const SystemSnapshot* prev,
                                      const SystemSnapshot* curr,
                                      UiRect* out_rects, int max_rects) {
  if (prev == NULL || curr == NULL || out_rects == NULL || max_rects < 1) {
    return 0;
  }

  int count = 0;

  /* 模式切换：两套布局完全不同，直接整屏重绘。 */
  if (prev->ui_mode != curr->ui_mode) {
    push_rect(out_rects, max_rects, &count, 0, 0, (int16_t)kUiScreenWidth,
              (int16_t)kUiScreenHeight);
    return count;
  }

  switch (curr->ui_mode) {
    case kUiModeDashboard:
      if (prev->wifi_connected != curr->wifi_connected ||
          prev->pc_online != curr->pc_online) {
        push_rect(out_rects, max_rects, &count, 0, 0, 128, 16); /* 顶栏 */
      }
      if (prev->metrics.cpu != curr->metrics.cpu) {
        push_rect(out_rects, max_rects, &count, 4, 20, 120, 32); /* CPU 卡片 */
      }
      if (prev->metrics.gpu != curr->metrics.gpu ||
          prev->metrics.gpu_valid != curr->metrics.gpu_valid) {
        push_rect(out_rects, max_rects, &count, 4, 56, 120, 32); /* GPU 卡片 */
      }
      if (prev->metrics.ram != curr->metrics.ram) {
        push_rect(out_rects, max_rects, &count, 4, 92, 120, 32); /* RAM 卡片 */
      }
      if (prev->metrics.rx_mbps != curr->metrics.rx_mbps ||
          prev->metrics.tx_mbps != curr->metrics.tx_mbps) {
        push_rect(out_rects, max_rects, &count, 4, 128, 120, 28); /* 网速行 */
      }
      break;

    case kUiModeClock:
      if (prev->clock.second != curr->clock.second ||
          prev->clock.minute != curr->clock.minute) {
        push_rect(out_rects, max_rects, &count, 0, 30, 128, 40); /* 大号时间 */
      }
      if (prev->clock.day != curr->clock.day ||
          strcmp(prev->lunar.lunar_md, curr->lunar.lunar_md) != 0) {
        push_rect(out_rects, max_rects, &count, 0, 75, 128,
                  50); /* 日期 + 农历卡 */
      }
      if (strcmp(prev->weather.weather, curr->weather.weather) != 0 ||
          prev->weather.temperature != curr->weather.temperature) {
        push_rect(out_rects, max_rects, &count, 0, 0, 128, 25); /* 天气行 */
      }
      break;

    default:
      break;
  }

  return count;
}
