#include "lunar_service.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define TABLE_PATH "../../../pico/lunar_table.bin"

static void expect_lunar(int year, int month, int day, int ganzhi_index,
                         const char* ganzhi_year, int lunar_month, bool is_leap,
                         int lunar_day, const char* lunar_month_day) {
  LunarDate lunar;

  memset(&lunar, 0, sizeof(lunar));
  assert(lunar_service_lookup(year, month, day, &lunar));
  assert(lunar.ganzhi_index == ganzhi_index);
  assert(strcmp(lunar.ganzhi_year, ganzhi_year) == 0);
  assert(lunar.lunar_month == lunar_month);
  assert(lunar.is_leap == is_leap);
  assert(lunar.lunar_day == lunar_day);
  assert(strcmp(lunar.lunar_month_day, lunar_month_day) == 0);
  printf("  %04d-%02d-%02d -> %s %s (gz=%d, month=%d, leap=%d, day=%d)\n", year,
         month, day, lunar.ganzhi_year, lunar.lunar_month_day,
         lunar.ganzhi_index, lunar.lunar_month, lunar.is_leap ? 1 : 0,
         lunar.lunar_day);
}

int main(void) {
  LunarDate lunar;
  int rc;

  printf("== test_lunar_service ==\n");

  rc = lunar_service_init_from_file(TABLE_PATH);
  printf("init_from_file(%s) = %d\n", TABLE_PATH, rc);
  assert(rc == 0);

  /* 与 pico/lunar.py format_lunar_date 交叉验证过的用例。 */
  expect_lunar(2026, 11, 6, 42, "丙午年", 9, false, 28, "九月廿八");
  expect_lunar(2024, 2, 10, 40, "甲辰年", 1, false, 1, "正月初一");
  expect_lunar(2023, 3, 23, 39, "癸卯年", 2, true, 2, "闰二月初二");

  /* 表外与非法日期必须返回 false。 */
  memset(&lunar, 0, sizeof(lunar));
  assert(!lunar_service_lookup(1900, 1, 1, &lunar));
  printf("  1900-01-01 -> false (早于表起始年)\n");
  assert(!lunar_service_lookup(2101, 1, 1, &lunar));
  printf("  2101-01-01 -> false (晚于表最后一天)\n");
  assert(!lunar_service_lookup(2026, 13, 1, &lunar));
  assert(!lunar_service_lookup(2026, 0, 1, &lunar));
  assert(!lunar_service_lookup(2026, 2, 30, &lunar));
  assert(!lunar_service_lookup(2026, 1, 0, &lunar));
  printf("  非法月/日 -> false\n");
  assert(!lunar_service_lookup(2026, 11, 6, NULL));
  printf("  NULL out -> false\n");

  /* 表覆盖到 2100，2100-12-31 是最后一条记录。 */
  assert(lunar_service_lookup(2030, 1, 1, &lunar));
  printf("  2030-01-01 -> %s %s\n", lunar.ganzhi_year, lunar.lunar_month_day);
  assert(lunar_service_lookup(2100, 12, 31, &lunar));
  printf("  2100-12-31 -> %s %s\n", lunar.ganzhi_year, lunar.lunar_month_day);

  /* 设备端路径：直接从内存加载（此处用构造的最小表）。 */
  static const uint8_t tiny_table[13] = {
      'H',  'K',  'L',  '1',  0x6D, 0x07, /* start_year = 1901 */
      0x01, 0x00, 0x00, 0x00,             /* record_count = 1 */
      0x00,                               /* 干支序号 0 -> 甲子 */
      0x01,                               /* 正月，非闰 */
      0x01,                               /* 初一 */
  };
  lunar_service_init_from_memory(tiny_table, sizeof(tiny_table));
  memset(&lunar, 0, sizeof(lunar));
  assert(lunar_service_lookup(1901, 1, 1, &lunar));
  assert(lunar.ganzhi_index == 0);
  assert(strcmp(lunar.ganzhi_year, "甲子年") == 0);
  assert(lunar.lunar_month == 1);
  assert(!lunar.is_leap);
  assert(lunar.lunar_day == 1);
  assert(strcmp(lunar.lunar_month_day, "正月初一") == 0);
  printf("  init_from_memory(有效最小表) -> %s %s\n", lunar.ganzhi_year,
         lunar.lunar_month_day);
  assert(!lunar_service_lookup(1901, 1, 2, &lunar));

  /* 无效表（错 magic / 截断 / 空指针）必须让查询返回 false。 */
  static const uint8_t bad_magic_table[13] = {
      'X', 'K', 'L', '1', 0x6D, 0x07, 0x01, 0x00, 0x00, 0x00, 0x00, 0x01, 0x01,
  };
  lunar_service_init_from_memory(bad_magic_table, sizeof(bad_magic_table));
  assert(!lunar_service_lookup(1901, 1, 1, &lunar));
  lunar_service_init_from_memory(tiny_table, sizeof(tiny_table) - 1);
  assert(!lunar_service_lookup(1901, 1, 1, &lunar));
  lunar_service_init_from_memory(NULL, 0);
  assert(!lunar_service_lookup(1901, 1, 1, &lunar));
  printf("  无效表 -> 查询返回 false\n");

  assert(lunar_service_init_from_file(TABLE_PATH) == 0);
  assert(lunar_service_init_from_file("../../../pico/missing.bin") < 0);
  /* 打开失败不破坏已加载的表。 */
  assert(lunar_service_lookup(2024, 2, 10, &lunar));
  printf("  重载有效表 + 缺失文件返回负值 -> ok\n");

  printf("PASS: test_lunar_service\n");
  return 0;
}
