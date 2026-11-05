#ifndef UI_LAYOUT_H
#define UI_LAYOUT_H

#include "system_state.h"
#include <stdint.h>

#define UI_SCREEN_WIDTH  128
#define UI_SCREEN_HEIGHT 160

typedef struct {
    int16_t x;
    int16_t y;
    int16_t w;
    int16_t h;
} ui_rect_t;

/* 计算 prev -> curr 之间需要重绘的脏矩形。
 * 返回写入 out_rects 的矩形个数（0..max_rects）。 */
int ui_layout_calculate_dirty_regions(
    const system_snapshot_t *prev,
    const system_snapshot_t *curr,
    ui_rect_t *out_rects,
    int max_rects);

#endif /* UI_LAYOUT_H */
