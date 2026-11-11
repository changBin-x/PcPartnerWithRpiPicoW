/**
 * @file ui_font.c
 * @brief v2 多字号字库渲染器：解析 `font_ui.bin`，按 4bpp 覆盖度把字画到条带。
 *
 * 字库格式（由 `tools/build_font_multi.py` 生成）：
 * 24 字节头 + 12 条 12 字节档位记录 + UTF-8 中文顺序表 + 各档 ASCII 块
 * + 各档中文块。所有字形都是 4bpp、高半字节在前、按行优先打包。
 */

#include "ui_font.h"

#include <stddef.h>

/** @brief 头部格式：magic、版本、档数、汉字数、三个偏移与总长。 */
enum {
  kHeaderSize = 24,
  kTierRecordSize = 12,
  kMagicOffset = 0,
  kVersionOffset = 4,
  kTierCountOffset = 5,
  kCjkCountOffset = 6,
  kTiersOffsetOffset = 8,
  kCharsetOffsetField = 12,
};

/** @brief 一档字号的解析结果。 */
typedef struct {
  int px;             /**< 像素高度 */
  int cjk_adv;        /**< 中文步进 */
  int ascii_adv;      /**< ASCII 步进 */
  uint32_t cjk_off;   /**< 中文块在 blob 内的绝对偏移 */
  uint32_t ascii_off; /**< ASCII 块在 blob 内的绝对偏移 */
} FontTier;

static const uint8_t* s_blob;              /**< 资源首字节 */
static uint32_t s_blob_size;               /**< 资源长度 */
static const char* s_charset;              /**< UTF-8 汉字顺序表 */
static int s_charset_count;                /**< 顺序表字数 */
static FontTier s_tiers[kUiFontTierCount]; /**< 档位表 */
static int s_tier_count;                   /**< 有效档数 */
static bool s_ready;                       /**< 是否已成功登记 */

/** @brief 读小端 16 位。 */
static uint16_t read_u16(const uint8_t* p) {
  return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

/** @brief 读小端 32 位。 */
static uint32_t read_u32(const uint8_t* p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
         ((uint32_t)p[3] << 24);
}

/** @brief 解码一个 UTF-8 字符，返回消耗字节数；非法序列按 1 字节处理。 */
static int utf8_decode(const char* text, uint32_t* out) {
  const uint8_t* p = (const uint8_t*)text;
  if (p[0] < 0x80u) {
    *out = p[0];
    return 1;
  }
  if ((p[0] & 0xE0u) == 0xC0u && p[1] != 0u) {
    *out = ((uint32_t)(p[0] & 0x1Fu) << 6) | (uint32_t)(p[1] & 0x3Fu);
    return 2;
  }
  if ((p[0] & 0xF0u) == 0xE0u && p[1] != 0u && p[2] != 0u) {
    *out = ((uint32_t)(p[0] & 0x0Fu) << 12) | ((uint32_t)(p[1] & 0x3Fu) << 6) |
           (uint32_t)(p[2] & 0x3Fu);
    return 3;
  }
  if ((p[0] & 0xF8u) == 0xF0u && p[1] != 0u && p[2] != 0u && p[3] != 0u) {
    *out = ((uint32_t)(p[0] & 0x07u) << 18) | ((uint32_t)(p[1] & 0x3Fu) << 12) |
           ((uint32_t)(p[2] & 0x3Fu) << 6) | (uint32_t)(p[3] & 0x3Fu);
    return 4;
  }
  *out = '?';
  return 1;
}

/** @brief 按字号找档位；找不到返回 NULL。 */
static const FontTier* find_tier(int px) {
  for (int i = 0; i < s_tier_count; ++i) {
    if (s_tiers[i].px == px) {
      return &s_tiers[i];
    }
  }
  return NULL;
}

/** @brief 在汉字顺序表里查序号；找不到返回 -1。 */
static int charset_index(uint32_t cp) {
  int byte_index = 0;
  for (int i = 0; i < s_charset_count; ++i) {
    uint32_t value = 0;
    byte_index += utf8_decode(&s_charset[byte_index], &value);
    if (value == cp) {
      return i;
    }
  }
  return -1;
}

/** @brief 把 4bpp 覆盖度混到底色上。 */
static uint16_t blend(uint16_t bg, uint16_t fg, uint32_t coverage) {
  if (coverage == 0u) {
    return bg;
  }
  if (coverage >= 15u) {
    return fg;
  }
  uint32_t inv = 15u - coverage;
  uint32_t r = (((uint32_t)(fg >> 11) & 0x1Fu) * coverage +
                (((uint32_t)(bg >> 11) & 0x1Fu) * inv)) /
               15u;
  uint32_t g = (((uint32_t)(fg >> 5) & 0x3Fu) * coverage +
                (((uint32_t)(bg >> 5) & 0x3Fu) * inv)) /
               15u;
  uint32_t b =
      (((uint32_t)fg & 0x1Fu) * coverage + ((uint32_t)bg & 0x1Fu) * inv) / 15u;
  return (uint16_t)((r << 11) | (g << 5) | b);
}

bool ui_font_init(const AssetBlob* blob) {
  s_ready = false;
  if (blob == NULL || blob->data == NULL || blob->size < kHeaderSize) {
    return false;
  }
  const uint8_t* data = (const uint8_t*)blob->data;
  if (data[0] != 'F' || data[1] != 'N' || data[2] != 'T' || data[3] != '2' ||
      data[kVersionOffset] != 1u) {
    return false;
  }
  int count = (int)read_u16(&data[kCjkCountOffset]);
  uint32_t tiers_off = read_u32(&data[kTiersOffsetOffset]);
  uint32_t charset_off = read_u32(&data[kCharsetOffsetField]);
  uint32_t total = read_u32(&data[20]);
  if (count <= 0 || total > blob->size ||
      tiers_off + (uint32_t)kUiFontTierCount * kTierRecordSize > total ||
      charset_off >= total) {
    return false;
  }
  int tiers = (int)data[kTierCountOffset];
  if (tiers <= 0 || tiers > kUiFontTierCount) {
    return false;
  }
  for (int i = 0; i < tiers; ++i) {
    const uint8_t* rec = &data[tiers_off + (uint32_t)i * kTierRecordSize];
    s_tiers[i].px = rec[0];
    s_tiers[i].cjk_adv = rec[1];
    s_tiers[i].ascii_adv = rec[2];
    s_tiers[i].cjk_off = read_u32(&rec[4]);
    s_tiers[i].ascii_off = read_u32(&rec[8]);
    if (s_tiers[i].cjk_off > total || s_tiers[i].ascii_off > total) {
      return false;
    }
  }
  s_tier_count = tiers;
  s_blob = data;
  s_blob_size = total;
  s_charset = (const char*)&data[charset_off];
  s_charset_count = count;
  s_ready = true;
  return true;
}

bool ui_font_ready(void) { return s_ready; }

const char* ui_font_charset(int* count) {
  if (count != NULL) {
    *count = s_ready ? s_charset_count : 0;
  }
  return s_ready ? s_charset : NULL;
}

int ui_font_height(int px) {
  const FontTier* tier = s_ready ? find_tier(px) : NULL;
  return (tier != NULL) ? tier->px : 0;
}

int ui_font_width(int px, const char* text) {
  const FontTier* tier = s_ready ? find_tier(px) : NULL;
  if (tier == NULL || text == NULL) {
    return 0;
  }
  int width = 0;
  const char* cursor = text;
  while (*cursor != '\0') {
    uint32_t cp = 0;
    int used = utf8_decode(cursor, &cp);
    if (cp >= 32u && cp <= 126u) {
      width += tier->ascii_adv;
    } else {
      width += tier->cjk_adv;
    }
    cursor += used;
  }
  return width;
}

int ui_font_draw(const UiSurface* surface, int x, int y, int px,
                 const char* text, uint16_t fg, uint16_t bg) {
  const FontTier* tier = s_ready ? find_tier(px) : NULL;
  if (surface == NULL || surface->pixels == NULL || tier == NULL ||
      text == NULL) {
    return 0;
  }
  int pen = x;
  const char* cursor = text;
  while (*cursor != '\0') {
    uint32_t cp = 0;
    int used = utf8_decode(cursor, &cp);
    cursor += used;
    bool ascii = (cp >= 32u && cp <= 126u);
    int index = ascii ? (int)cp - 32 : charset_index(cp);
    if (!ascii && index < 0) {
      ascii = true;
      index = (int)'?' - 32;
    }
    int adv = ascii ? tier->ascii_adv : tier->cjk_adv;
    uint32_t base = ascii ? tier->ascii_off : tier->cjk_off;
    uint32_t cell = ((uint32_t)adv * (uint32_t)tier->px + 1u) / 2u;
    const uint8_t* glyph =
        (index >= 0 && base + (uint32_t)index * cell + cell <= s_blob_size)
            ? &s_blob[base + (uint32_t)index * cell]
            : NULL;
    if (glyph != NULL) {
      for (int row = 0; row < tier->px; ++row) {
        int screen_y = y + row;
        int strip_y = screen_y - surface->origin_y;
        if (strip_y < 0 || strip_y >= surface->height) {
          continue;
        }
        for (int col = 0; col < adv; ++col) {
          int screen_x = pen + col;
          if (screen_x < 0 || screen_x >= surface->width) {
            continue;
          }
          uint32_t bit = (uint32_t)row * (uint32_t)adv + (uint32_t)col;
          uint8_t byte = glyph[bit >> 1];
          uint32_t value =
              (bit & 1u) ? (uint32_t)(byte & 0x0Fu) : (uint32_t)(byte >> 4);
          if (value == 0u) {
            continue;
          }
          uint16_t* pixel =
              &surface->pixels[(size_t)strip_y * (size_t)surface->width +
                               (size_t)screen_x];
          *pixel = blend(bg, fg, value);
        }
      }
    }
    pen += adv;
  }
  return pen - x;
}
