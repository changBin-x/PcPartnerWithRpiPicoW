/**
 * @file ui_theme.c
 * @brief 六套主题的静态调色板表、令牌取值器与对比度门禁配对。
 *
 * 色值逐字取自规格 §3.2；§3.4.1 的 17 项阈值由 kThemePairs 描述，
 * 宿主端测试 tests/test_ui_theme.c 逐主题复算 CR 与 ΔL*。
 */

#include "ui_theme.h"

static const UiThemePalette s_themes[kThemeCount] = {
    [kThemeM3Dark] =
        {
            .name = "M3 深色",
            .background = 0x1083,
            .surface = 0x1904,
            .surface_high = 0x2945,
            .track = 0x0020,
            .text = 0xe71d,
            .muted = 0x9493,
            .primary = 0xae3f,
            .secondary = 0xa699,
            .tertiary = 0xFE00,
            .error = 0xf5d6,
        },
    [kThemeMonoRed] =
        {
            .name = "黑琥珀",
            .background = 0x0000,
            .surface = 0x2124,
            .surface_high = 0x39E7,
            .track = 0x0000,
            .text = 0xFFFF,
            .muted = 0x9D13,
            .primary = 0xFB40,
            .secondary = 0x65B6,
            .tertiary = 0xFE60,
            .error = 0xF94A,
        },
    [kThemePhosphor] =
        {
            .name = "荧光绿",
            .background = 0x0020,
            .surface = 0x0982,
            .surface_high = 0x1224,
            .track = 0x0020,
            .text = 0xD7FB,
            .muted = 0x8611,
            .primary = 0x07E0,
            .secondary = 0x1F34,
            .tertiary = 0xFD00,
            .error = 0xF9A9,
        },
    [kThemeCeladon] =
        {
            .name = "霓虹青",
            .background = 0x0861,
            .surface = 0x2165,
            .surface_high = 0x3208,
            .track = 0x0861,
            .text = 0xF7BF,
            .muted = 0xA597,
            .primary = 0x07F3,
            .secondary = 0x5EBF,
            .tertiary = 0xFE00,
            .error = 0xF927,
        },
    [kThemeDeepTeal] =
        {
            .name = "深海青",
            .background = 0x0862,
            .surface = 0x19A7,
            .surface_high = 0x2A2B,
            .track = 0x0862,
            .text = 0xF7BF,
            .muted = 0x9DB9,
            .primary = 0x07BA,
            .secondary = 0x3DFF,
            .tertiary = 0xFFE0,
            .error = 0xFB90,
        },
    [kThemeNeonMagenta] =
        {
            .name = "霓虹洋红",
            .background = 0x0022,
            .surface = 0x2166,
            .surface_high = 0x3209,
            .track = 0x0022,
            .text = 0xF7DF,
            .muted = 0xAD3A,
            .primary = 0xFA19,
            .secondary = 0x07FF,
            .tertiary = 0xFE60,
            .error = 0xF94A,
        },
};

const ThemePairRule kThemePairs[] = {
    {"text/surface", kThemeTokenText, kThemeTokenSurface, false, 4.5f},
    {"muted/surface", kThemeTokenMuted, kThemeTokenSurface, false, 4.5f},
    {"text/background", kThemeTokenText, kThemeTokenBackground, false, 4.5f},
    {"muted/background", kThemeTokenMuted, kThemeTokenBackground, false, 4.5f},
    {"primary/surface", kThemeTokenPrimary, kThemeTokenSurface, false, 4.5f},
    {"secondary/surface", kThemeTokenSecondary, kThemeTokenSurface, false,
     4.5f},
    {"secondary/surface_high", kThemeTokenSecondary, kThemeTokenSurfaceHigh,
     false, 4.5f},
    {"tertiary/surface", kThemeTokenTertiary, kThemeTokenSurface, false, 4.5f},
    {"primary/track", kThemeTokenPrimary, kThemeTokenTrack, false, 3.0f},
    {"secondary/track", kThemeTokenSecondary, kThemeTokenTrack, false, 3.0f},
    {"tertiary/track", kThemeTokenTertiary, kThemeTokenTrack, false, 3.0f},
    {"error/track", kThemeTokenError, kThemeTokenTrack, false, 3.0f},
    {"error/surface", kThemeTokenError, kThemeTokenSurface, false, 3.0f},
    {"primary/background", kThemeTokenPrimary, kThemeTokenBackground, false,
     3.0f},
    {"error/background", kThemeTokenError, kThemeTokenBackground, false, 3.0f},
    {"dL(surface,background)", kThemeTokenSurface, kThemeTokenBackground, true,
     10.0f},
    {"dL(surface_high,surface)", kThemeTokenSurfaceHigh, kThemeTokenSurface,
     true, 8.0f},
};

const size_t kThemePairCount = sizeof(kThemePairs) / sizeof(kThemePairs[0]);

uint16_t ui_theme_token(const UiThemePalette* palette, UiThemeToken token) {
  switch (token) {
    case kThemeTokenBackground:
      return palette->background;
    case kThemeTokenSurface:
      return palette->surface;
    case kThemeTokenSurfaceHigh:
      return palette->surface_high;
    case kThemeTokenTrack:
      return palette->track;
    case kThemeTokenText:
      return palette->text;
    case kThemeTokenMuted:
      return palette->muted;
    case kThemeTokenPrimary:
      return palette->primary;
    case kThemeTokenSecondary:
      return palette->secondary;
    case kThemeTokenTertiary:
      return palette->tertiary;
    case kThemeTokenError:
      return palette->error;
    default:
      return palette->background;
  }
}

const UiThemePalette* ui_theme_get(UiThemeId theme_id) {
  if ((unsigned)theme_id >= (unsigned)kThemeCount) {
    return &s_themes[kThemeM3Dark];
  }
  return &s_themes[theme_id];
}
