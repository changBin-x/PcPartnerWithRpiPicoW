#ifndef PICO_FREERTOS_FIRMWARE_INCLUDE_ASSET_STORE_H_
#define PICO_FREERTOS_FIRMWARE_INCLUDE_ASSET_STORE_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* 一段只读二进制资源的视图。
 * data 指向的缓冲区生命周期与程序相同，调用方不得写入或释放。
 * 对 CA 证书，size 包含文件末尾的 NUL 字节，可以直接交给 mbedTLS。 */
typedef struct {
  const uint8_t* data;
  size_t size;
} AssetBlob;

/* 初始化资源访问。
 * 设备端：资源以 const 数据的形式链接进 Flash，本函数只做指针登记，恒成功。
 * 宿主端（单元测试）：按 PICO_ASSETS_DIR、pico/、../pico/、../../pico/、
 * ../../../pico/ 的顺序查找 pico/ 目录，全部找不到时返回 false。 */
bool asset_store_init(void);

/* 中文字模表（14x14、4bpp、98 字节/字形，共 341 个字形）。
 * 调用 asset_store_init 成功前返回 NULL。 */
const AssetBlob* asset_store_font_cn(void);

/* 农历逐日数据表（magic "HKL1"）。
 * 调用 asset_store_init 成功前返回 NULL。 */
const AssetBlob* asset_store_lunar_table(void);

/* 高德地图 Web 服务根证书（PEM）。
 * 调用 asset_store_init 成功前返回 NULL。 */
const AssetBlob* asset_store_amap_root_ca(void);

/* 香港天文台根证书（PEM）。
 * 调用 asset_store_init 成功前返回 NULL。 */
const AssetBlob* asset_store_hko_root_ca(void);

#endif /* PICO_FREERTOS_FIRMWARE_INCLUDE_ASSET_STORE_H_ */
