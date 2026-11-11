/**
 * @file test_theme_config.c
 * @brief 12 字节配置的打包、解析与全部拒绝分支（规格 §8.2 N-1..N-8）。
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "theme_config.h"

/** @brief 一页擦除后的内容。 */
static void fill_erased(uint8_t* page, size_t size) {
  memset(page, 0xFF, size);
}

/** @brief 断言解析失败（必须回退，不给越界值）。 */
static bool expect_reject(const uint8_t* page, size_t size) {
  ThemeConfig out;
  return !theme_config_parse(page, size, &out);
}

/** @brief 读回页内的小端 32 位值。 */
static uint32_t read_u32(const uint8_t* page, size_t offset) {
  return (uint32_t)page[offset + 0] | ((uint32_t)page[offset + 1] << 8) |
         ((uint32_t)page[offset + 2] << 16) |
         ((uint32_t)page[offset + 3] << 24);
}

/** @brief 写页内的小端 32 位值（构造只坏一个字段的样本）。 */
static void write_u32(uint8_t* page, size_t offset, uint32_t value) {
  page[offset + 0] = (uint8_t)(value & 0xFFu);
  page[offset + 1] = (uint8_t)((value >> 8) & 0xFFu);
  page[offset + 2] = (uint8_t)((value >> 16) & 0xFFu);
  page[offset + 3] = (uint8_t)((value >> 24) & 0xFFu);
}

/** @brief 按 §5.2 公式重算校验和，让样本只在目标字段上违规。 */
static void refresh_checksum(uint8_t* page) {
  const uint32_t magic = 0x454D4854u;
  write_u32(page, kThemeConfigChecksumOffset,
            magic ^ ((uint32_t)page[kThemeConfigThemeIdOffset] << 8) ^
                (uint32_t)page[kThemeConfigVersionOffset]);
}

/** @brief 校验 §5.2 的字节布局：小端 "THME"、字段与校验和公式。 */
static bool layout_matches_spec(const uint8_t* page, UiThemeId theme_id) {
  const uint32_t magic = 0x454D4854u;
  const uint32_t checksum =
      magic ^ ((uint32_t)theme_id << 8) ^ (uint32_t)kThemeConfigVersion;
  return page[0] == 'T' && page[1] == 'H' && page[2] == 'M' && page[3] == 'E' &&
         page[kThemeConfigThemeIdOffset] == (uint8_t)theme_id &&
         page[kThemeConfigVersionOffset] == (uint8_t)kThemeConfigVersion &&
         page[kThemeConfigReservedOffset] == 0 &&
         page[kThemeConfigReservedOffset + 1] == 0 &&
         read_u32(page, kThemeConfigChecksumOffset) == checksum;
}

int main(void) {
  uint8_t page[kThemeConfigPageSize];
  ThemeConfig parsed;

  /* N-1 擦除态。 */
  fill_erased(page, sizeof(page));
  if (!expect_reject(page, sizeof(page))) {
    printf("FAIL: N-1 擦除态未被拒绝\n");
    return EXIT_FAILURE;
  }

  /* N-2 全 0x00。 */
  memset(page, 0x00, sizeof(page));
  if (!expect_reject(page, sizeof(page))) {
    printf("FAIL: N-2 全零未被拒绝\n");
    return EXIT_FAILURE;
  }

  /* 正常路径：打包后必须能原样解析回来。 */
  fill_erased(page, sizeof(page));
  theme_config_pack(kThemeScope, page, sizeof(page));
  if (!theme_config_parse(page, sizeof(page), &parsed)) {
    printf("FAIL: 合法配置未被接受\n");
    return EXIT_FAILURE;
  }
  if (parsed.theme_id != kThemeScope) {
    printf("FAIL: theme_id 解析错位（%u）\n", (unsigned)parsed.theme_id);
    return EXIT_FAILURE;
  }

  /* §5.2：12 字节布局、小端 "THME" 与校验和公式。 */
  if (!layout_matches_spec(page, kThemeScope)) {
    printf("FAIL: 字节布局或校验和不符合 §5.2\n");
    return EXIT_FAILURE;
  }

  /* N-3 theme_id 越界。 */
  theme_config_pack(kThemeScope, page, sizeof(page));
  page[kThemeConfigThemeIdOffset] = 200;
  if (!expect_reject(page, sizeof(page))) {
    printf("FAIL: N-3 越界 theme_id 未被拒绝\n");
    return EXIT_FAILURE;
  }

  /* N-3 隔离形态：越界 id 的校验和自洽，仍必须按范围拒绝。 */
  theme_config_pack(kThemeScope, page, sizeof(page));
  page[kThemeConfigThemeIdOffset] = 200;
  refresh_checksum(page);
  if (!expect_reject(page, sizeof(page))) {
    printf("FAIL: 自洽校验和的越界 theme_id 未被拒绝\n");
    return EXIT_FAILURE;
  }

  /* N-4 checksum 错 1 位。 */
  theme_config_pack(kThemeBento, page, sizeof(page));
  page[kThemeConfigChecksumOffset] ^= 0x01u;
  if (!expect_reject(page, sizeof(page))) {
    printf("FAIL: N-4 错误校验和未被拒绝\n");
    return EXIT_FAILURE;
  }

  /* N-5 版本不符。 */
  theme_config_pack(kThemeHud, page, sizeof(page));
  page[kThemeConfigVersionOffset] = 2;
  if (!expect_reject(page, sizeof(page))) {
    printf("FAIL: N-5 版本不符未被拒绝\n");
    return EXIT_FAILURE;
  }

  /* N-5 隔离形态：版本与校验和自洽，仍必须按版本拒绝。 */
  theme_config_pack(kThemeHud, page, sizeof(page));
  page[kThemeConfigVersionOffset] = 2;
  refresh_checksum(page);
  if (!expect_reject(page, sizeof(page))) {
    printf("FAIL: 自洽校验和的错误版本未被拒绝\n");
    return EXIT_FAILURE;
  }

  /* N-6 reserved 非零。 */
  theme_config_pack(kThemeHud, page, sizeof(page));
  page[kThemeConfigReservedOffset] = 1;
  if (!expect_reject(page, sizeof(page))) {
    printf("FAIL: N-6 保留字段非零未被拒绝\n");
    return EXIT_FAILURE;
  }

  /* N-7 整页 0xFF（擦除完成、编程未开始）。 */
  fill_erased(page, sizeof(page));
  if (!expect_reject(page, sizeof(page))) {
    printf("FAIL: N-7 整页 0xFF 未被拒绝\n");
    return EXIT_FAILURE;
  }

  /* N-8 前 4 字节已写（编程中途）。 */
  fill_erased(page, sizeof(page));
  memcpy(page, &(const uint32_t){kThemeConfigMagic}, sizeof(uint32_t));
  if (!expect_reject(page, sizeof(page))) {
    printf("FAIL: N-8 半写页未被拒绝\n");
    return EXIT_FAILURE;
  }

  /* 缓冲区不足时必须拒绝，而不是越界读。 */
  theme_config_pack(kThemeTui, page, sizeof(page));
  if (!expect_reject(page, kThemeConfigSize - 1u)) {
    printf("FAIL: 短缓冲区未被拒绝\n");
    return EXIT_FAILURE;
  }

  /* 输出缓冲区不足一页时打包必须什么都不写。 */
  memset(page, 0x5A, sizeof(page));
  theme_config_pack(kThemeScope, page, kThemeConfigPageSize - 1u);
  if (page[0] != 0x5Au || page[kThemeConfigPageSize - 1u] != 0x5Au) {
    printf("FAIL: 短输出缓冲区仍被写入\n");
    return EXIT_FAILURE;
  }

  /* 越界入参按 kThemeSwiss 落盘。 */
  theme_config_pack((UiThemeId)200, page, sizeof(page));
  if (!theme_config_parse(page, sizeof(page), &parsed) ||
      parsed.theme_id != kThemeSwiss) {
    printf("FAIL: 越界入参未回退 kThemeSwiss\n");
    return EXIT_FAILURE;
  }

  /* 打包必须把 12 字节之后的区域填 0xFF。 */
  theme_config_pack(kThemeSwiss, page, sizeof(page));
  for (size_t i = kThemeConfigSize; i < sizeof(page); ++i) {
    if (page[i] != 0xFFu) {
      printf("FAIL: 页填充不是 0xFF（偏移 %zu）\n", i);
      return EXIT_FAILURE;
    }
  }

  printf("PASS: N-1..N-8 与页填充全部符合规格\n");
  return EXIT_SUCCESS;
}
