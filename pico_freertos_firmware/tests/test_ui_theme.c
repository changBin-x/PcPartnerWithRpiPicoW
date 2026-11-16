// 五套主题调色板的回归测试：数量、越界夹紧、颜色互异与关键取值。

#include <math.h>
#include <stdio.h>

#include "ui_theme.h"
#include "ui_theme_id.h"

static int g_failures = 0;

#define CHECK(condition)                                                   \
  do {                                                                     \
    if (!(condition)) {                                                    \
      fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
      ++g_failures;                                                        \
    }                                                                      \
  } while (0)

static void test_count(void) {
  CHECK(kThemeCount == 5);
  CHECK(kThemeSwiss == 0);
  CHECK(kThemeHud == 4);
}

static void test_non_null(void) {
  for (int i = 0; i < kThemeCount; ++i) {
    const UiThemePalette* palette = ui_theme_get((UiThemeId)i);
    CHECK(palette != NULL);
    CHECK(palette->bg != palette->fg);
    CHECK(ui_theme_name((UiThemeId)i)[0] != '\0');
  }
}

static void test_clamp(void) {
  CHECK(ui_theme_get((UiThemeId)kThemeCount) == ui_theme_get(kThemeSwiss));
  CHECK(ui_theme_get((UiThemeId)99) == ui_theme_get(kThemeSwiss));
}

static void test_distinct_backgrounds(void) {
  for (int i = 0; i < kThemeCount; ++i) {
    for (int j = i + 1; j < kThemeCount; ++j) {
      CHECK(ui_theme_get((UiThemeId)i)->bg != ui_theme_get((UiThemeId)j)->bg);
    }
  }
}

/** @brief RGB565 展开到 8 位分量。 */
static void rgb565_to_888(uint16_t color, double* r, double* g, double* b) {
  *r = (double)((color >> 11) & 0x1f) * 255.0 / 31.0;
  *g = (double)((color >> 5) & 0x3f) * 255.0 / 63.0;
  *b = (double)(color & 0x1f) * 255.0 / 31.0;
}

/** @brief sRGB 分量转线性光（WCAG 2.x 使用 2.4 指数）。 */
static double linearize(double channel) {
  double c = channel / 255.0;
  return (c <= 0.04045) ? (c / 12.92) : pow((c + 0.055) / 1.055, 2.4);
}

/** @brief WCAG 相对亮度。 */
static double relative_luminance(uint16_t color) {
  double r, g, b;
  rgb565_to_888(color, &r, &g, &b);
  return 0.2126 * linearize(r) + 0.7152 * linearize(g) + 0.0722 * linearize(b);
}

/** @brief WCAG 对比度。 */
static double contrast_ratio(uint16_t a, uint16_t b) {
  double la = relative_luminance(a);
  double lb = relative_luminance(b);
  double hi = (la > lb) ? la : lb;
  double lo = (la > lb) ? lb : la;
  return (hi + 0.05) / (lo + 0.05);
}

/** @brief 次要文字对比度门槛：可读性验收要求 muted/dim 对底色不低于 4.5:1。 */
static void test_contrast(void) {
  for (int i = 0; i < kThemeCount; ++i) {
    const UiThemePalette* p = ui_theme_get((UiThemeId)i);
    double muted = contrast_ratio(p->muted, p->bg);
    double dim = contrast_ratio(p->dim, p->bg);
    printf("theme %s: muted %.2f dim %.2f\n", ui_theme_name((UiThemeId)i), muted,
           dim);
    CHECK(muted >= 4.5);
    CHECK(dim >= 4.5);
    /* 磁贴/面板里的次要文字画在 surface 上，这里的对比度才是真实最差值。 */
    CHECK(contrast_ratio(p->muted, p->surface) >= 4.5);
    CHECK(contrast_ratio(p->dim, p->surface) >= 4.5);
  }
}

static void test_known_values(void) {
  const UiThemePalette* swiss = ui_theme_get(kThemeSwiss);
  CHECK(swiss->bg == 0x0000);
  CHECK(swiss->fg == 0xe73c);
  CHECK(swiss->accent == 0x07f9);
  const UiThemePalette* tui = ui_theme_get(kThemeTui);
  CHECK(tui->bg == 0x0060);
  CHECK(tui->fg == 0x07ec);
  const UiThemePalette* hud = ui_theme_get(kThemeHud);
  CHECK(hud->bg == 0x0021);
  CHECK(hud->accent_2 == 0xf80a);
}

int main(void) {
  test_count();
  test_non_null();
  test_clamp();
  test_distinct_backgrounds();
  test_contrast();
  test_known_values();
  if (g_failures != 0) {
    fprintf(stderr, "%d 项失败\n", g_failures);
    return 1;
  }
  printf("test_ui_theme: 全部通过\n");
  return 0;
}
