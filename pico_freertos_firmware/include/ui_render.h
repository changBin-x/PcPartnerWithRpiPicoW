#ifndef PICO_FREERTOS_FIRMWARE_INCLUDE_UI_RENDER_H_
#define PICO_FREERTOS_FIRMWARE_INCLUDE_UI_RENDER_H_

#include <stdbool.h>

#include "st7735s_driver.h"
#include "system_state.h"
#include "ui_input.h"
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

/* 在 ui_layout 的脏矩形基础上，补齐时钟页顶栏 WiFi 图标与整张日历卡。
 * 纯函数，宿主测试可直接调用。 */
int ui_render_collect_dirty_regions(const SystemSnapshot* previous,
                                    const SystemSnapshot* current,
                                    UiRect* out_rects, int max_rects);

/* 创建并绑定到 Core 1 的渲染任务 vTaskUIRender。
 * main.c 目前尚未调用本入口，但函数本身已编译进镜像。 */
/**
 * @brief 登记输入源；传 NULL 表示不处理按键。
 *
 * 渲染任务在同一个 20 ms 节拍里采样按键（消抖需要稳定周期），所以输入源
 * 只是一个函数指针表，不引入第二个任务。
 *
 * @param source 输入源；生命周期必须覆盖整个渲染任务。
 */
void ui_render_set_input_source(const UiInputSource* source);

/* 创建渲染任务；成功返回 true。失败时调用方需要摘掉 UI 监视槽位。 */
bool ui_render_start_task(void);

#endif /* PICO_FREERTOS_FIRMWARE_INCLUDE_UI_RENDER_H_ */
