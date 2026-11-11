#ifndef PICO_FREERTOS_FIRMWARE_INCLUDE_THEME_CONFIG_H_
#define PICO_FREERTOS_FIRMWARE_INCLUDE_THEME_CONFIG_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ui_theme_id.h"

/** @brief 配置在页内的字节数。 */
enum { kThemeConfigSize = 12 };

/** @brief 一页的字节数；缓冲区由调用方提供，本模块不引用 SDK 宏。 */
enum { kThemeConfigPageSize = 256 };

/** @brief 小端落盘为 "THME"。 */
enum { kThemeConfigMagic = 0x454D4854 };

/** @brief 布局版本；改动字段顺序或含义必须加 1。 */
enum { kThemeConfigVersion = 1 };

/** @brief 字段偏移，与 §5.2 的 _Static_assert 对应。 */
enum {
  kThemeConfigMagicOffset = 0,
  kThemeConfigThemeIdOffset = 4,
  kThemeConfigVersionOffset = 5,
  kThemeConfigReservedOffset = 6,
  kThemeConfigChecksumOffset = 8,
};

/** @brief 解析结果（不含填充与偏移）。 */
typedef struct {
  UiThemeId theme_id; /**< 已校验的合法主题 id */
} ThemeConfig;

/**
 * @brief 把主题写成一页：前 12 字节是配置，其余全省 0xFF。
 *
 * @param theme_id 目标主题；越界时按 kThemeM3Dark 落盘。
 * @param page     输出缓冲区，长度至少 size。
 * @param size     缓冲区长度；小于 kThemeConfigPageSize 时直接返回。
 */
void theme_config_pack(UiThemeId theme_id, uint8_t* page, size_t size);

/**
 * @brief 从一页里解析配置。
 *
 * @param page 输入缓冲区，长度至少 size。
 * @param size 缓冲区长度；小于 kThemeConfigSize 时直接判失败。
 * @param out  解析结果；仅在返回 true 时写入。
 * @return true 表示魔数、版本、保留字段、主题范围、校验和全部通过。
 */
bool theme_config_parse(const uint8_t* page, size_t size, ThemeConfig* out);

#endif /* PICO_FREERTOS_FIRMWARE_INCLUDE_THEME_CONFIG_H_ */
