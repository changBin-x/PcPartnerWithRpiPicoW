/**
 * @file ui_gfx.h
 * @brief v2 渲染层的矩形图元：填充、描边、虚线、圆角、网格、折线、斜切。
 *
 * 全部函数以屏幕坐标调用，内部把 y 平移到条带的 `origin_y` 并裁剪到条带
 * 边界；越界像素直接丢弃，不写越界内存。
 */

#ifndef PICO_FREERTOS_FIRMWARE_INCLUDE_UI_GFX_H_
#define PICO_FREERTOS_FIRMWARE_INCLUDE_UI_GFX_H_

#include <stdint.h>

#include "ui_surface.h"

/**
 * @brief 用颜色填满整个条带。
 */
void gfx_clear(const UiSurface* surface, uint16_t color);

/**
 * @brief 填充矩形（含边界）。
 */
void gfx_fill(const UiSurface* surface, int x, int y, int w, int h,
              uint16_t color);

/**
 * @brief 1 px 内描边矩形。
 */
void gfx_stroke(const UiSurface* surface, int x, int y, int w, int h,
                uint16_t color);

/**
 * @brief 水平虚线：先画 dash 个像素，再空 gap 个像素。
 */
void gfx_dashed_hline(const UiSurface* surface, int x, int y, int w,
                      uint16_t color, int dash, int gap);

/**
 * @brief 填充圆角矩形，半径 r（0 等价于 gfx_fill）。
 */
void gfx_round_fill(const UiSurface* surface, int x, int y, int w, int h, int r,
                    uint16_t color);

/**
 * @brief 在矩形内画 step 像素间距的网格线（示波器底纹）。
 */
void gfx_grid(const UiSurface* surface, int x, int y, int w, int h,
              uint16_t color, int step);

/**
 * @brief Bresenham 折线，points 为 (x0,y0,x1,y1,...) 屏幕坐标。
 *
 * @param count 点的个数（不是坐标个数）。
 */
void gfx_polyline(const UiSurface* surface, const int16_t* points, int count,
                  uint16_t color);

/**
 * @brief 填充右上角切掉 cut 像素的矩形（HUD 斜切顶栏）。
 */
void gfx_bevel_top(const UiSurface* surface, int x, int y, int w, int h,
                   int cut, uint16_t color);

#endif /* PICO_FREERTOS_FIRMWARE_INCLUDE_UI_GFX_H_ */
