// 五套主题调色板的回归测试：数量、越界夹紧、颜色互异与关键取值。

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
  test_known_values();
  if (g_failures != 0) {
    fprintf(stderr, "%d 项失败\n", g_failures);
    return 1;
  }
  printf("test_ui_theme: 全部通过\n");
  return 0;
}
