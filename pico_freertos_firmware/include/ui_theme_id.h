/**
 * @file ui_theme_id.h
 * @brief 主题标识符：零依赖，供 system_state 与 UI 共用。
 *
 * 五套主题与 `screen_designs_5styles_dual_pages.html` 的五个流派一一对应，
 * 每套主题有自己的两种页面版式（页面 A 监控、页面 B 时钟/综述）。
 */

#ifndef PICO_FREERTOS_FIRMWARE_INCLUDE_UI_THEME_ID_H_
#define PICO_FREERTOS_FIRMWARE_INCLUDE_UI_THEME_ID_H_

/** @brief 主题标识；顺序即按键循环顺序。 */
typedef enum {
  kThemeSwiss = 0, /**< 瑞士大字阶排版流 */
  kThemeTui,       /**< 复古字符终端流 */
  kThemeBento,     /**< 便当网格磁贴流 */
  kThemeScope,     /**< 工业示波时序波形流 */
  kThemeHud,       /**< 赛博战术机甲 HUD 流 */
  kThemeCount      /**< 主题总数，同时用作越界哨兵 */
} UiThemeId;

#endif /* PICO_FREERTOS_FIRMWARE_INCLUDE_UI_THEME_ID_H_ */
