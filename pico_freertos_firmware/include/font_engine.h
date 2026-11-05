#ifndef PICO_FREERTOS_FIRMWARE_INCLUDE_FONT_ENGINE_H_
#define PICO_FREERTOS_FIRMWARE_INCLUDE_FONT_ENGINE_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "asset_store.h"

/* 字形规格：与 pico/font_cn.py 的生成参数保持一致。 */
enum {
  kFontCoverageBits = 4,
  kFontAsciiWidth = 8,
  kFontAsciiHeight = 14,
  kFontAsciiBytes = 56,
  kFontChineseWidth = 14,
  kFontChineseHeight = 14,
  kFontChineseBytes = 98,
  kFontCnGlyphCount = 341,
  kFontClockDigitWidth = 14,
  kFontClockDigitHeight = 26,
  kFontClockDigitBytes = 182,
  kFontClockColonWidth = 8,
  kFontClockColonHeight = 26,
  kFontClockColonBytes = 104,
  kFontClockColonAdvance = 10,
  kFontClockDigitAdvance = 14,
};

/* 渲染目标：一块从左到右、从上到下的 RGB565 像素缓冲。
 * 行 0 对应屏幕的 origin_y 行，因此可以直接指向某个切片的缓冲区。 */
typedef struct {
  uint16_t* pixels;
  int width;
  int height;
  int origin_y;
} FontSurface;

/* 登记中文字模 blob 并解码字库顺序表。
 * blob 必须恰好是 341 个 14x14 字形；成功返回 true。 */
bool font_engine_init(const AssetBlob* blob);

bool font_engine_is_ready(void);

/* 取 ASCII 字形（8x14，4bpp，高半字节在前）。不在字库内的字符回退到
 * '?'；未初始化时返回 NULL。 */
const uint8_t* font_engine_ascii_glyph(char symbol);

/* 取 UTF-8 中文字形（14x14，4bpp）。字库内没有该字符时返回 NULL。 */
const uint8_t* font_engine_chinese_glyph(const char* utf8_symbol);

/* 文本像素宽度：ASCII 8 列、中文 14 列。不涉及屏幕裁剪。 */
int font_engine_text_width(const char* text);

/* 大号时钟文本宽度：数字 14 列，冒号 8 列、步进 10 列。 */
int font_engine_clock_text_width(const char* text);

/* 4bpp 覆盖度混合到 RGB565：0 返回背景色，15 返回前景色，其余逐通道
 * 线性插值。 */
uint16_t font_engine_blend_rgb565(uint16_t foreground, uint16_t background,
                                  uint8_t coverage);

/* 把一个已打包的 4bpp 字形画到 surface 上，自动按 surface 范围裁剪。
 * glyph 的宽高由调用方给出，用于图标等非字库位图。 */
void font_engine_draw_glyph(const FontSurface* surface, int x, int y,
                            const uint8_t* glyph, int glyph_width,
                            int glyph_height, uint16_t color,
                            uint16_t background_color);

/* 画混合中英文文本（ASCII 8x14、中文 14x14），返回文本总宽度。
 * 超出 surface 的像素被裁剪，不会越界写入。 */
int font_engine_draw_text(const FontSurface* surface, int x, int y,
                          const char* text, uint16_t color,
                          uint16_t background_color);

/* 画大号时钟文本（14x26 数字 + 8x26 冒号），返回总宽度。 */
int font_engine_draw_clock_text(const FontSurface* surface, int x, int y,
                                const char* text, uint16_t color,
                                uint16_t background_color);

#endif /* PICO_FREERTOS_FIRMWARE_INCLUDE_FONT_ENGINE_H_ */
