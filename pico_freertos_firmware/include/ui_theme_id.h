#ifndef PICO_FREERTOS_FIRMWARE_INCLUDE_UI_THEME_ID_H_
#define PICO_FREERTOS_FIRMWARE_INCLUDE_UI_THEME_ID_H_

/** @brief 主题编号；零依赖，state / display / app 都可 include。 */
typedef enum {
  kThemeM3Dark = 0,      /**< M3 深色 */
  kThemeMonoRed = 1,     /**< 黑琥珀 */
  kThemePhosphor = 2,    /**< 荧光绿 */
  kThemeCeladon = 3,     /**< 霓虹青 */
  kThemeDeepTeal = 4,    /**< 深海青 */
  kThemeNeonMagenta = 5, /**< 霓虹洋红 */
  kThemeCount = 6
} UiThemeId;

#endif /* PICO_FREERTOS_FIRMWARE_INCLUDE_UI_THEME_ID_H_ */
