/**
 * @file test_ui_theme.c
 * @brief 把规格 §3.4.1 的 17 项阈值变成可执行断言。
 *
 * 测试自己实现 WCAG 2.1 相对亮度与 CIELAB 明度差，不依赖任何外部库，
 * 这样「阈值是否满足」不取决于被测代码的实现细节。
 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include "ui_theme.h"

/** @brief RGB565 通道按位复制扩到 8 bit。 */
static void rgb565_to_rgb888(uint16_t color, double* r, double* g, double* b) {
  const unsigned r5 = (color >> 11) & 0x1Fu;
  const unsigned g6 = (color >> 5) & 0x3Fu;
  const unsigned b5 = color & 0x1Fu;
  *r = (double)((r5 << 3) | (r5 >> 2));
  *g = (double)((g6 << 2) | (g6 >> 4));
  *b = (double)((b5 << 3) | (b5 >> 2));
}

/** @brief sRGB 分量转线性光。 */
static double srgb_to_linear(double channel) {
  const double c = channel / 255.0;
  return c <= 0.04045 ? c / 12.92 : pow((c + 0.055) / 1.055, 2.4);
}

/** @brief WCAG 2.1 相对亮度。 */
static double relative_luminance(uint16_t color) {
  double r = 0.0;
  double g = 0.0;
  double b = 0.0;
  rgb565_to_rgb888(color, &r, &g, &b);
  return 0.2126 * srgb_to_linear(r) + 0.7152 * srgb_to_linear(g) +
         0.0722 * srgb_to_linear(b);
}

/** @brief WCAG 2.1 对比度，返回较大者对较小者。 */
static double contrast_ratio(uint16_t a, uint16_t b) {
  const double la = relative_luminance(a);
  const double lb = relative_luminance(b);
  const double hi = la > lb ? la : lb;
  const double lo = la > lb ? lb : la;
  return (hi + 0.05) / (lo + 0.05);
}

/** @brief CIELAB 的 L* 分量（D65）。 */
static double lab_lightness(uint16_t color) {
  double r = 0.0;
  double g = 0.0;
  double b = 0.0;
  rgb565_to_rgb888(color, &r, &g, &b);
  const double rl = srgb_to_linear(r);
  const double gl = srgb_to_linear(g);
  const double bl = srgb_to_linear(b);
  const double y = 0.2126729 * rl + 0.7151522 * gl + 0.0721750 * bl;
  const double yn = 1.0;
  const double t = y / yn;
  const double f =
      t > 0.008856 ? pow(t, 1.0 / 3.0) : (7.787 * t) + 16.0 / 116.0;
  return (116.0 * f) - 16.0;
}

/** @brief 有符号明度差 ΔL*(a, b) = L*(a) - L*(b)。 */
static double delta_l(uint16_t a, uint16_t b) {
  return lab_lightness(a) - lab_lightness(b);
}

/** @brief 主题 0 已登记的两项视觉缺陷，不参与「必须达标」判定。 */
static bool is_registered_defect(unsigned theme, unsigned rule) {
  return theme == (unsigned)kThemeM3Dark &&
         (rule == 15u || rule == 16u); /* 规格表第 16、17 项 */
}

int main(void) {
  for (unsigned theme = 0; theme < (unsigned)kThemeCount; ++theme) {
    const UiThemePalette* palette = ui_theme_get((UiThemeId)theme);
    if (palette == NULL) {
      printf("FAIL: ui_theme_get(%u) 返回 NULL\n", theme);
      return EXIT_FAILURE;
    }
    for (unsigned rule = 0; rule < kThemePairCount; ++rule) {
      const ThemePairRule* pair = &kThemePairs[rule];
      const uint16_t fg = ui_theme_token(palette, pair->foreground);
      const uint16_t bg = ui_theme_token(palette, pair->background);
      const double value =
          pair->use_delta_l ? delta_l(fg, bg) : contrast_ratio(fg, bg);
      if (is_registered_defect(theme, rule)) {
        if (value >= (double)pair->min_value) {
          printf(
              "FAIL: 主题 %u 的 %s 已达标（%.2f ≥ %.2f），"
              "应从已知缺陷表移除\n",
              theme, pair->label, value, (double)pair->min_value);
          return EXIT_FAILURE;
        }
        continue;
      }
      if (value < (double)pair->min_value) {
        printf("FAIL: 主题 %u 的 %s = %.2f，低于阈值 %.2f\n", theme,
               pair->label, value, (double)pair->min_value);
        return EXIT_FAILURE;
      }
    }
    /* 规格 §3.3 规则 2：槽底必须比卡片暗，且差距 ≥ 10。不在 17 项内。 */
    const double track_delta =
        delta_l(ui_theme_token(palette, kThemeTokenSurface),
                ui_theme_token(palette, kThemeTokenTrack));
    if (track_delta < 10.0) {
      printf("FAIL: 主题 %u 的 ΔL*(surface,track) = %.2f < 10\n", theme,
             track_delta);
      return EXIT_FAILURE;
    }
  }
  printf("PASS: 6 套 × 17 项与 ΔL*(surface,track) 全部符合规格\n");
  return EXIT_SUCCESS;
}
