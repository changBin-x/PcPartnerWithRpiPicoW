/**
 * @file ui_theme.h
 * @brief 五套主题的调色板，来源是设计稿
 * `screen_designs_5styles_dual_pages.html` 里每个流派内联写死的十六进制颜色。
 *
 * 调色板只描述颜色，不含字号与版式；版式差异由 `ui_render.c` 按主题分别
 * 绘制。颜色已换算成 RGB565。
 */

#ifndef PICO_FREERTOS_FIRMWARE_INCLUDE_UI_THEME_H_
#define PICO_FREERTOS_FIRMWARE_INCLUDE_UI_THEME_H_

#include <stdint.h>

#include "ui_theme_id.h"

/** @brief 一套主题用到的全部颜色（RGB565）。 */
typedef struct {
  uint16_t bg;         /**< 页面底色 */
  uint16_t fg;         /**< 主文字 */
  uint16_t muted;      /**< 次要文字 */
  uint16_t dim;        /**< 最弱文字/注释 */
  uint16_t line;       /**< 分隔线与描边 */
  uint16_t surface;    /**< 卡片、磁贴、输入框底色 */
  uint16_t surface_hi; /**< 高一级的表面（反色标题、强调底） */
  uint16_t accent;     /**< 主轴强调色 */
  uint16_t accent_2;   /**< 次强调色 */
  uint16_t warn;       /**< 警示色 */
  uint16_t error;      /**< 错误/告警色 */
  uint16_t grid;       /**< 网格与底纹 */
} UiThemePalette;

/**
 * @brief 取主题调色板。
 *
 * @param theme 主题标识；越界时夹回 `kThemeSwiss`。
 * @return 指向 Flash 常量的调色板，永不为 NULL。
 */
const UiThemePalette* ui_theme_get(UiThemeId theme);

/**
 * @brief 取主题中文名（仅用于串口日志与调试，不经过字体引擎）。
 *
 * @param theme 主题标识；越界时返回第一个主题的名字。
 * @return UTF-8 常量字符串。
 */
const char* ui_theme_name(UiThemeId theme);

#endif /* PICO_FREERTOS_FIRMWARE_INCLUDE_UI_THEME_H_ */
