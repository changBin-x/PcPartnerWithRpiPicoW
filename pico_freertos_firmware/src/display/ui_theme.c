/**
 * @file ui_theme.c
 * @brief 五套主题调色板表；颜色逐项取自设计稿的内联样式。
 */

#include "ui_theme.h"

#include <stddef.h>

/** @brief 主题总数必须与枚举一致。 */
_Static_assert(kThemeCount == 5, "主题表长度与 kThemeCount 不一致");

/** @brief 五套调色板，顺序与 UiThemeId 一致。 */
static const UiThemePalette kThemes[kThemeCount] = {
    /* 瑞士大字阶：纯黑底、青色数值、琥珀警示。 */
    {
        .bg = 0x0000,
        .fg = 0xe73c,
        .muted = 0x73ae,
        .dim = 0x632c,
        .line = 0x1082,
        .surface = 0x0841,
        .surface_hi = 0x18a3,
        .accent = 0x07f9,
        .accent_2 = 0xfd40,
        .warn = 0xfd40,
        .error = 0xf80a,
        .grid = 0x18a3,
    },
    /* 复古字符终端：磷光绿、反色标题行。 */
    {
        .bg = 0x0060,
        .fg = 0x07ec,
        .muted = 0x064a,
        .dim = 0x0223,
        .line = 0x01a2,
        .surface = 0x00a1,
        .surface_hi = 0x07ec,
        .accent = 0x8fe0,
        .accent_2 = 0x07ec,
        .warn = 0x8fe0,
        .error = 0xf80a,
        .grid = 0x0142,
    },
    /* 便当网格：深灰底、琥珀数字、薄荷与天蓝点缀。 */
    {
        .bg = 0x0862,
        .fg = 0xef9e,
        .muted = 0x8c51,
        .dim = 0x632c,
        .line = 0x2125,
        .surface = 0x10a3,
        .surface_hi = 0x2125,
        .accent = 0xfd80,
        .accent_2 = 0x0734,
        .warn = 0xfd80,
        .error = 0xf80a,
        .grid = 0x2125,
    },
    /* 工业示波：深海军底、青色波形、琥珀刻度。 */
    {
        .bg = 0x0842,
        .fg = 0xdf9e,
        .muted = 0x7d38,
        .dim = 0x4b91,
        .line = 0x10e5,
        .surface = 0x0883,
        .surface_hi = 0x08a3,
        .accent = 0x07f9,
        .accent_2 = 0xfe40,
        .warn = 0xfe40,
        .error = 0xf80a,
        .grid = 0x1105,
    },
    /* 战术机甲 HUD：近黑底、青主色、洋红告警。 */
    {
        .bg = 0x0021,
        .fg = 0x077f,
        .muted = 0x5392,
        .dim = 0x322a,
        .line = 0x1106,
        .surface = 0x0842,
        .surface_hi = 0x0862,
        .accent = 0x077f,
        .accent_2 = 0xf80a,
        .warn = 0xf80a,
        .error = 0xf80a,
        .grid = 0x10c5,
    },
};

/** @brief 主题中文名，仅用于日志。 */
static const char* const kThemeNames[kThemeCount] = {
    "瑞士大字阶", "复古终端", "便当网格", "工业示波", "战术机甲",
};

const UiThemePalette* ui_theme_get(UiThemeId theme) {
  if ((unsigned)theme >= (unsigned)kThemeCount) {
    return &kThemes[kThemeSwiss];
  }
  return &kThemes[theme];
}

const char* ui_theme_name(UiThemeId theme) {
  if ((unsigned)theme >= (unsigned)kThemeCount) {
    return kThemeNames[kThemeSwiss];
  }
  return kThemeNames[theme];
}
