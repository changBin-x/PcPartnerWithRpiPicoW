/**
 * @file ui_surface.h
 * @brief v2 渲染层的唯一绘图目标：一块 128 宽的水平条带。
 *
 * 屏幕按 16 行切成 10 条带依次绘制，条带缓冲放在 RAM 里，行 0 对应屏幕的
 * `origin_y` 行。所有 v2 绘图函数（`ui_font_*` 与 `gfx_*`）都只接受这个
 * 结构，并自行把屏幕坐标平移到条带坐标后裁剪。
 */

#ifndef PICO_FREERTOS_FIRMWARE_INCLUDE_UI_SURFACE_H_
#define PICO_FREERTOS_FIRMWARE_INCLUDE_UI_SURFACE_H_

#include <stdint.h>

/** @brief 一条水平条带。pixels 为 RGB565 行优先缓冲。 */
typedef struct {
  uint16_t* pixels; /**< 条带首像素，行 0 对应屏幕 origin_y 行 */
  int width;        /**< 条带宽度，固定 kUiScreenWidth */
  int height;       /**< 条带高度（行数） */
  int origin_y;     /**< 条带第 0 行对应的屏幕 y 坐标 */
} UiSurface;

#endif /* PICO_FREERTOS_FIRMWARE_INCLUDE_UI_SURFACE_H_ */
