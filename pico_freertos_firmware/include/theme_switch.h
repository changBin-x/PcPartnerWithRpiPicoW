/**
 * @file theme_switch.h
 * @brief 单击推进主题并绕回：纯逻辑，不依赖 pico-sdk。
 */

#ifndef PICO_FREERTOS_FIRMWARE_INCLUDE_THEME_SWITCH_H_
#define PICO_FREERTOS_FIRMWARE_INCLUDE_THEME_SWITCH_H_

#include <stdbool.h>

#include "ui_theme_id.h"

/**
 * @brief 有单击则前进一套并绕回；无单击原样返回。
 *
 * @param current 当前主题。
 * @param clicked 本次消抖后是否判定为单击。
 * @return 下一套主题；入参越界时夹紧回 kThemeSwiss。
 */
UiThemeId theme_switch_advance(UiThemeId current, bool clicked);

#endif /* PICO_FREERTOS_FIRMWARE_INCLUDE_THEME_SWITCH_H_ */
