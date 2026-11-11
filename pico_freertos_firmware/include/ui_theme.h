#ifndef PICO_FREERTOS_FIRMWARE_INCLUDE_UI_THEME_H_
#define PICO_FREERTOS_FIRMWARE_INCLUDE_UI_THEME_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ui_theme_id.h"

/** @brief 一套主题的全部颜色令牌（RP2040 上 sizeof = 24 字节）。 */
typedef struct {
  const char* name;      /**< 日志用名称，不上屏 */
  uint16_t background;   /**< 屏幕背景画布 */
  uint16_t surface;      /**< 卡片底色 */
  uint16_t surface_high; /**< 胶囊/高亮容器底色 */
  uint16_t track;        /**< 进度条槽底 */
  uint16_t text;         /**< 主文字 */
  uint16_t muted;        /**< 次要文字与标签 */
  uint16_t primary;      /**< 主强调：CPU / 联机 / 时钟 */
  uint16_t secondary;    /**< 次强调：GPU / 星期 / 同步 */
  uint16_t tertiary;     /**< 第三强调：内存 / 农历 */
  uint16_t error;        /**< 告警：断网 / 内存爆满 */
} UiThemePalette;

/** @brief 令牌枚举，供对比度规则表按名字取值。 */
typedef enum {
  kThemeTokenBackground = 0,
  kThemeTokenSurface,
  kThemeTokenSurfaceHigh,
  kThemeTokenTrack,
  kThemeTokenText,
  kThemeTokenMuted,
  kThemeTokenPrimary,
  kThemeTokenSecondary,
  kThemeTokenTertiary,
  kThemeTokenError,
  kThemeTokenCount
} UiThemeToken;

/** @brief 一条对比度门禁：前景令牌 vs 背景令牌。 */
typedef struct {
  const char* label;       /**< 断言失败时的可读名称 */
  UiThemeToken foreground; /**< 前景令牌 */
  UiThemeToken background; /**< 背景令牌 */
  bool use_delta_l;        /**< false = WCAG CR，true = ΔL* */
  float min_value;         /**< 阈值 */
} ThemePairRule;

/** @brief 规格 §3.4.1 的 17 项，定义在 ui_theme.c。 */
extern const ThemePairRule kThemePairs[];
extern const size_t kThemePairCount;

/** @brief 按令牌取色值。 */
uint16_t ui_theme_token(const UiThemePalette* palette, UiThemeToken token);

/** @brief 取主题调色板；越界 id 回退 kThemeM3Dark，永不返回 NULL。 */
const UiThemePalette* ui_theme_get(UiThemeId theme_id);

#endif /* PICO_FREERTOS_FIRMWARE_INCLUDE_UI_THEME_H_ */
