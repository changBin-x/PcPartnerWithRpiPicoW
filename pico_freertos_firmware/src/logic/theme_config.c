/**
 * @file theme_config.c
 * @brief 12 字节配置的打包与解析：魔数、版本、保留字段与校验和。
 */

#include "theme_config.h"

#include <string.h>

/** @brief 按位拼接写小端。 */
static void put_u32(uint8_t* page, size_t offset, uint32_t value) {
  page[offset + 0] = (uint8_t)(value & 0xFFu);
  page[offset + 1] = (uint8_t)((value >> 8) & 0xFFu);
  page[offset + 2] = (uint8_t)((value >> 16) & 0xFFu);
  page[offset + 3] = (uint8_t)((value >> 24) & 0xFFu);
}

/** @brief 按位拼接读小端。 */
static uint32_t get_u32(const uint8_t* page, size_t offset) {
  return (uint32_t)page[offset + 0] | ((uint32_t)page[offset + 1] << 8) |
         ((uint32_t)page[offset + 2] << 16) |
         ((uint32_t)page[offset + 3] << 24);
}

/** @brief 校验和：魔数、主题、版本三者异或。 */
static uint32_t checksum_of(uint32_t magic, uint8_t theme_id, uint8_t version) {
  return magic ^ ((uint32_t)theme_id << 8) ^ (uint32_t)version;
}

void theme_config_pack(UiThemeId theme_id, uint8_t* page, size_t size) {
  if (size < (size_t)kThemeConfigPageSize) {
    return;
  }
  const bool valid = (unsigned)theme_id < (unsigned)kThemeCount;
  const uint8_t id = (uint8_t)(valid ? (unsigned)theme_id : 0u);
  memset(page, 0xFF, size);
  put_u32(page, kThemeConfigMagicOffset, (uint32_t)kThemeConfigMagic);
  page[kThemeConfigThemeIdOffset] = id;
  page[kThemeConfigVersionOffset] = (uint8_t)kThemeConfigVersion;
  page[kThemeConfigReservedOffset] = 0;
  page[kThemeConfigReservedOffset + 1] = 0;
  put_u32(page, kThemeConfigChecksumOffset,
          checksum_of((uint32_t)kThemeConfigMagic, id,
                      (uint8_t)kThemeConfigVersion));
}

bool theme_config_parse(const uint8_t* page, size_t size, ThemeConfig* out) {
  if (size < (size_t)kThemeConfigSize) {
    return false;
  }
  const uint32_t magic = get_u32(page, kThemeConfigMagicOffset);
  const uint8_t id = page[kThemeConfigThemeIdOffset];
  const uint8_t version = page[kThemeConfigVersionOffset];
  const uint32_t checksum = get_u32(page, kThemeConfigChecksumOffset);
  if (magic != (uint32_t)kThemeConfigMagic) {
    return false;
  }
  if (version != (uint8_t)kThemeConfigVersion) {
    return false;
  }
  if (page[kThemeConfigReservedOffset] != 0 ||
      page[kThemeConfigReservedOffset + 1] != 0) {
    return false;
  }
  if ((unsigned)id >= (unsigned)kThemeCount) {
    return false;
  }
  if (checksum != checksum_of(magic, id, version)) {
    return false;
  }
  out->theme_id = (UiThemeId)id;
  return true;
}
