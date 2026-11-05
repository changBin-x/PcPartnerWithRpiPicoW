#ifndef PICO_FREERTOS_FIRMWARE_INCLUDE_UI_RENDER_H_
#define PICO_FREERTOS_FIRMWARE_INCLUDE_UI_RENDER_H_

#include <stdbool.h>

#include "st7735s_driver.h"
#include "system_state.h"
#include "ui_layout.h"

/* 登记字模资源，之后才能渲染。asset_store_init 必须先成功。 */
bool ui_render_init(void);

/* 渲染整屏一帧（等价于整屏脏区）。纯函数，不依赖 FreeRTOS，宿主测试
 * 可以直接用内存后端调用。 */
bool ui_render_draw_frame(DisplayBackend* backend,
                          const SystemSnapshot* snapshot);

/* 只重绘给定脏矩形覆盖到的 16 行切片。矩形按 ui_layout 的输出传入，
 * 调用方负责给出非空的 rects 数组。 */
bool ui_render_draw_dirty(DisplayBackend* backend,
                          const SystemSnapshot* snapshot, const UiRect* rects,
                          int rect_count);

/* 创建并绑定到 Core 1 的渲染任务 vTaskUIRender。
 * main.c 目前尚未调用本入口，但函数本身已编译进镜像。 */
void ui_render_start_task(void);

#endif /* PICO_FREERTOS_FIRMWARE_INCLUDE_UI_RENDER_H_ */
