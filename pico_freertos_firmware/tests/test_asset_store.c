#include "asset_store.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

/* 新的多档字库（FNT2）头：magic/version/档位数/汉字数/四个偏移。 */
#define kFontTierCount 12
#define kFontHeaderBytes 24

static void test_font_cn(void) {
  const AssetBlob* blob = asset_store_font_cn();
  assert(blob != NULL);
  assert(blob->data != NULL);
  assert(blob->size > kFontHeaderBytes);
  /* magic 必须是 "FNT2"，且头部记录的 total 与实际长度一致，说明引用的是
   * 预期的那份多档字库，而不是旧的单档表。 */
  assert(memcmp(blob->data, "FNT2", 4) == 0);
  assert(blob->data[4] == 1u);
  assert(blob->data[5] == (unsigned char)kFontTierCount);
  uint32_t total = 0;
  memcpy(&total, blob->data + 20, sizeof(total));
  assert(total == blob->size);
  printf("PASS: 多档字库 %zu 字节，%d 档\n", blob->size, kFontTierCount);
}

static void test_lunar_table(void) {
  const AssetBlob* blob = asset_store_lunar_table();
  assert(blob != NULL);
  assert(blob->data != NULL);
  /* 表头 magic 为 "HKL1"，与 lunar_service 的解码器一致。 */
  assert(blob->size > 10u);
  assert(memcmp(blob->data, "HKL1", 4) == 0);
  printf("PASS: 农历表 %zu 字节，magic=%.4s\n", blob->size, blob->data);
}

static void test_certificates(void) {
  const AssetBlob* amap = asset_store_amap_root_ca();
  const AssetBlob* hko = asset_store_hko_root_ca();
  assert(amap != NULL && amap->data != NULL && amap->size > 0u);
  assert(hko != NULL && hko->data != NULL && hko->size > 0u);
  /* PEM 必须以文件头开始，且 data[size] 恒为 NUL，否则 mbedTLS 解析会失败。 */
  static const char kBegin[] = "-----BEGIN CERTIFICATE-----";
  assert(amap->size > sizeof(kBegin));
  assert(memcmp(amap->data, kBegin, sizeof(kBegin) - 1) == 0);
  assert(amap->data[amap->size] == '\0');
  assert(hko->size > sizeof(kBegin));
  assert(memcmp(hko->data, kBegin, sizeof(kBegin) - 1) == 0);
  assert(hko->data[hko->size] == '\0');
  printf("PASS: 根证书 amap=%zu 字节 hko=%zu 字节\n", amap->size, hko->size);
}

int main(void) {
  /* 初始化之前所有访问器都必须返回 NULL，避免调用方拿到半初始化的指针。 */
  assert(asset_store_font_cn() == NULL);
  assert(asset_store_lunar_table() == NULL);
  assert(asset_store_amap_root_ca() == NULL);
  assert(asset_store_hko_root_ca() == NULL);

  assert(asset_store_init());
  test_font_cn();
  test_lunar_table();
  test_certificates();
  printf("PASS: test_asset_store\n");
  return 0;
}
