/**
 * @file ui_font.h
 * @brief v2 多字号字库：12 档像素高度（6/7/8/9/10/11/14/15/17/18/24/26）。
 *
 * 字库由 `tools/build_font_multi.py` 生成，格式见该脚本头部注释：
 * 24 字节头 + 12 条 12 字节档位记录 + UTF-8 中文顺序表 + 各档 ASCII/中文
 * 4bpp 覆盖度位图。中文只在前 6 档（6–11 px）生成，更高档位只有 ASCII。
 */

#ifndef PICO_FREERTOS_FIRMWARE_INCLUDE_UI_FONT_H_
#define PICO_FREERTOS_FIRMWARE_INCLUDE_UI_FONT_H_

#include <stdbool.h>
#include <stdint.h>

#include "asset_store.h"
#include "ui_surface.h"

/** @brief 字库支持的字号档数，与生成脚本的 TIERS 顺序一致。 */
enum { kUiFontTierCount = 12 };

/**
 * @brief 登记字库 blob 并校验头部。
 *
 * @param blob asset_store 取出的字库资源；为空或魔数不符时返回 false。
 * @return 校验结果。
 */
bool ui_font_init(const AssetBlob* blob);

/** @brief 字库是否已成功登记。 */
bool ui_font_ready(void);

/**
 * @brief 取中文顺序表（UTF-8，非以 0 结尾）。
 *
 * @param count 输出字符数，可为 NULL。
 * @return 顺序表首字节；未初始化时返回 NULL。
 */
const char* ui_font_charset(int* count);

/**
 * @brief 文本在给定字号下的像素宽度。
 *
 * 未知字符按 '?' 的宽度计；字号不在档位表内时返回 0。
 *
 * @param px 字号（像素高度）。
 * @param text UTF-8 文本。
 * @return 像素宽度。
 */
int ui_font_width(int px, const char* text);

/**
 * @brief 取某档字号的字高（等于该档的像素高度）。
 *
 * @param px 字号。
 * @return 字高；不在档位表内时返回 0。
 */
int ui_font_height(int px);

/**
 * @brief 把一行文本画到条带上，(x, y) 是文本盒左上角。
 *
 * 文本盒高度为 px；超出条带或屏幕边界的像素被裁剪。未知字符画 '?'。
 *
 * @param surface 目标条带。
 * @param x 屏幕坐标 x。
 * @param y 屏幕坐标 y（文本盒顶部）。
 * @param px 字号。
 * @param text UTF-8 文本。
 * @param fg 前景色 RGB565。
 * @param bg 背景色 RGB565。
 * @return 实际推进的像素宽度。
 */
int ui_font_draw(const UiSurface* surface, int x, int y, int px,
                 const char* text, uint16_t fg, uint16_t bg);

#endif /* PICO_FREERTOS_FIRMWARE_INCLUDE_UI_FONT_H_ */
