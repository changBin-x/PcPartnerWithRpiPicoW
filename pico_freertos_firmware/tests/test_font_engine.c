#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "asset_store.h"
#include "font_engine.h"

enum {
  kCanvasWidth = 32,
  kCanvasHeight = 32,
  kClockWidth = 128,
  kClockHeight = 32,
  kGuardValueBefore = 0xdeadbeef,
  kGuardValueAfter = 0xcafebabe,
};

/* 前后各放一个哨兵，任何越界写入都会破坏它们。 */
typedef struct {
  uint32_t guard_before;
  uint16_t pixels[kCanvasWidth * kCanvasHeight];
  uint32_t guard_after;
} GuardedCanvas;

static GuardedCanvas s_canvas;
static uint16_t s_clock_pixels[kClockWidth * kClockHeight];

static void reset_canvas(GuardedCanvas* canvas, uint16_t color) {
  canvas->guard_before = kGuardValueBefore;
  canvas->guard_after = kGuardValueAfter;
  for (size_t index = 0; index < (size_t)kCanvasWidth * (size_t)kCanvasHeight;
       ++index) {
    canvas->pixels[index] = color;
  }
}

static void assert_canvas_guards(const GuardedCanvas* canvas) {
  assert(canvas->guard_before == kGuardValueBefore);
  assert(canvas->guard_after == kGuardValueAfter);
}

static int count_non_background(const uint16_t* pixels, size_t count,
                                uint16_t background) {
  int result = 0;
  for (size_t index = 0; index < count; ++index) {
    if (pixels[index] != background) {
      ++result;
    }
  }
  return result;
}

static int count_non_background_bytes(const uint8_t* data, size_t count,
                                      uint8_t background) {
  int result = 0;
  for (size_t index = 0; index < count; ++index) {
    if (data[index] != background) {
      ++result;
    }
  }
  return result;
}

static int count_color(const uint16_t* pixels, size_t count, uint16_t color) {
  int result = 0;
  for (size_t index = 0; index < count; ++index) {
    if (pixels[index] == color) {
      ++result;
    }
  }
  return result;
}

static FontSurface make_surface(uint16_t* pixels, int width, int height,
                                int origin_y) {
  FontSurface surface;
  surface.pixels = pixels;
  surface.width = width;
  surface.height = height;
  surface.origin_y = origin_y;
  return surface;
}

static void test_metrics(void) {
  const AssetBlob* font_blob = asset_store_font_cn();
  assert(font_blob != NULL);
  assert(font_blob->size ==
         (size_t)kFontCnGlyphCount * (size_t)kFontChineseBytes);

  assert(font_engine_text_width("CPU") == 3 * kFontAsciiWidth);
  assert(font_engine_text_width("晴") == kFontChineseWidth);
  assert(font_engine_text_width("晴A") == kFontChineseWidth + kFontAsciiWidth);
  assert(font_engine_text_width("") == 0);
  assert(font_engine_text_width(NULL) == 0);

  assert(font_engine_clock_text_width("12:34:56") == 104);
  assert(font_engine_clock_text_width("0:0") == kFontClockDigitAdvance +
                                                    kFontClockColonAdvance +
                                                    kFontClockDigitAdvance);
  assert(font_engine_clock_text_width("") == 0);
}

static void test_glyph_lookup(void) {
  const uint8_t* letter_a = font_engine_ascii_glyph('A');
  assert(letter_a != NULL);
  assert(count_non_background_bytes(letter_a, kFontAsciiBytes, 0) > 0);

  /* 字库没有控制字符，应回退到 '?' 的字形。 */
  const uint8_t* missing_ascii = font_engine_ascii_glyph('\x01');
  assert(missing_ascii != NULL);
  assert(memcmp(missing_ascii, font_engine_ascii_glyph('?'), kFontAsciiBytes) ==
         0);

  const uint8_t* chinese = font_engine_chinese_glyph("晴");
  assert(chinese != NULL);
  assert(count_non_background_bytes(chinese, kFontChineseBytes, 0) > 0);
  const uint8_t* other_chinese = font_engine_chinese_glyph("雨");
  assert(other_chinese != NULL);
  assert(memcmp(chinese, other_chinese, kFontChineseBytes) != 0);

  assert(font_engine_chinese_glyph("龘") == NULL);
  assert(font_engine_chinese_glyph(NULL) == NULL);
}

static void test_blend(void) {
  const uint16_t foreground = 0xf800;
  const uint16_t background = 0x001f;
  assert(font_engine_blend_rgb565(foreground, background, 0) == background);
  assert(font_engine_blend_rgb565(foreground, background, 15) == foreground);
  const uint16_t middle = font_engine_blend_rgb565(foreground, background, 8);
  assert(middle != foreground);
  assert(middle != background);
}

static void test_draw_ascii(void) {
  reset_canvas(&s_canvas, 0x0000);
  FontSurface surface =
      make_surface(s_canvas.pixels, kCanvasWidth, kCanvasHeight, 0);
  assert(font_engine_draw_text(&surface, 0, 0, "A", 0xffff, 0x0000) ==
         (int)kFontAsciiWidth);
  assert(count_non_background(s_canvas.pixels, kCanvasWidth * kCanvasHeight,
                              0x0000) > 0);
  assert(count_color(s_canvas.pixels, kCanvasWidth * kCanvasHeight, 0xffff) >
         0);
  assert_canvas_guards(&s_canvas);

  /* 负坐标与右侧越界都只裁剪，不能写到画布外。 */
  reset_canvas(&s_canvas, 0x0000);
  surface = make_surface(s_canvas.pixels, kCanvasWidth, kCanvasHeight, 0);
  font_engine_draw_text(&surface, -4, -7, "ABC", 0xffff, 0x0000);
  assert_canvas_guards(&s_canvas);

  reset_canvas(&s_canvas, 0x0000);
  surface = make_surface(s_canvas.pixels, kCanvasWidth, kCanvasHeight, 0);
  font_engine_draw_text(&surface, 30, 0, "A", 0xffff, 0x0000);
  for (int row = 0; row < (int)kFontAsciiHeight; ++row) {
    for (int column = 0; column < 30; ++column) {
      assert(s_canvas.pixels[row * kCanvasWidth + column] == 0x0000);
    }
  }
  assert_canvas_guards(&s_canvas);
}

static void test_draw_chinese_and_clipping(void) {
  reset_canvas(&s_canvas, 0x0000);
  FontSurface surface =
      make_surface(s_canvas.pixels, kCanvasWidth, kFontChineseHeight, 16);
  assert(font_engine_draw_text(&surface, 0, 16, "晴", 0xffff, 0x0000) ==
         (int)kFontChineseWidth);
  for (int row = 16; row < (int)kCanvasHeight; ++row) {
    for (int column = 0; column < (int)kCanvasWidth; ++column) {
      assert(s_canvas.pixels[row * kCanvasWidth + column] == 0x0000);
    }
  }
  assert(count_non_background(s_canvas.pixels, kCanvasWidth * kCanvasHeight,
                              0x0000) > 0);
  assert_canvas_guards(&s_canvas);
}

static void test_draw_clock(void) {
  for (size_t index = 0; index < (size_t)kClockWidth * (size_t)kClockHeight;
       ++index) {
    s_clock_pixels[index] = 0x0000;
  }
  FontSurface surface =
      make_surface(s_clock_pixels, kClockWidth, kClockHeight, 0);
  assert(font_engine_draw_clock_text(&surface, 0, 0, "12:34:56", 0xffff,
                                     0x0000) == 104);
  assert(count_non_background(s_clock_pixels, kClockWidth * kClockHeight,
                              0x0000) > 300);
}

int main(void) {
  assert(!font_engine_is_ready());
  assert(!font_engine_init(NULL));
  assert(font_engine_ascii_glyph('A') == NULL);

  assert(asset_store_init());
  const AssetBlob* font_blob = asset_store_font_cn();
  assert(font_blob != NULL);
  const AssetBlob truncated = {font_blob->data, font_blob->size - 1u};
  assert(!font_engine_init(&truncated));
  assert(font_engine_init(font_blob));
  assert(font_engine_is_ready());

  test_metrics();
  test_glyph_lookup();
  test_blend();
  test_draw_ascii();
  test_draw_chinese_and_clipping();
  test_draw_clock();
  return 0;
}
