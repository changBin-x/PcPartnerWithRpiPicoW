#ifndef PICO_FREERTOS_FIRMWARE_INCLUDE_UI_LAYOUT_H_
#define PICO_FREERTOS_FIRMWARE_INCLUDE_UI_LAYOUT_H_

#include "system_state.h"
#include <stdint.h>

#define kUiScreenWidth 128
#define kUiScreenHeight 160

typedef struct {
  int16_t x;
  int16_t y;
  int16_t w;
  int16_t h;
} UiRect;

/* 计算 prev -> curr 之间需要重绘的脏矩形。
 * 返回写入 out_rects 的矩形个数（0..max_rects）。 */
int ui_layout_calculate_dirty_regions(const SystemSnapshot* prev,
                                      const SystemSnapshot* curr,
                                      UiRect* out_rects, int max_rects);

#endif /* PICO_FREERTOS_FIRMWARE_INCLUDE_UI_LAYOUT_H_ */
