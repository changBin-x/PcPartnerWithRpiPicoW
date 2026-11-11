/**
 * @file render_window.h
 * @brief 渲染窗口：标记刷屏进行中，供写 Flash 前判断能否安全进入。
 */

#ifndef PICO_FREERTOS_FIRMWARE_INCLUDE_RENDER_WINDOW_H_
#define PICO_FREERTOS_FIRMWARE_INCLUDE_RENDER_WINDOW_H_

#include <stdbool.h>

/** @brief 标记渲染开始；设备侧由全屏障保证跨核可见性。 */
void render_window_enter(void);

/** @brief 标记渲染结束；设备侧由全屏障保证跨核可见性。 */
void render_window_leave(void);

/**
 * @brief 渲染是否空闲（可以安全进入 Flash 写入窗口）。
 *
 * @return true 表示当前没有渲染正在刷屏。
 */
bool render_window_is_idle(void);

#endif /* PICO_FREERTOS_FIRMWARE_INCLUDE_RENDER_WINDOW_H_ */
