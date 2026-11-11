#include "asset_store.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* 设备端把资源文件用 objcopy 转成 .rodata 段链接进 Flash，符号名由
 * 输入文件名派生（见 CMakeLists.txt 的 embed_asset 函数）。 */
#ifdef PICO_ON_DEVICE

extern const uint8_t _binary_font_cn_bin_start[];
extern const uint8_t _binary_font_cn_bin_end[];
extern const uint8_t _binary_lunar_table_bin_start[];
extern const uint8_t _binary_lunar_table_bin_end[];
extern const uint8_t _binary_amap_root_ca_pem_start[];
extern const uint8_t _binary_amap_root_ca_pem_end[];
extern const uint8_t _binary_hko_root_ca_pem_start[];
extern const uint8_t _binary_hko_root_ca_pem_end[];

static struct {
  AssetBlob font_cn;
  AssetBlob lunar_table;
  AssetBlob amap_root_ca;
  AssetBlob hko_root_ca;
} g_assets;

bool asset_store_init(void) {
  g_assets.font_cn.data = _binary_font_cn_bin_start;
  g_assets.font_cn.size =
      (size_t)(_binary_font_cn_bin_end - _binary_font_cn_bin_start);
  g_assets.lunar_table.data = _binary_lunar_table_bin_start;
  g_assets.lunar_table.size =
      (size_t)(_binary_lunar_table_bin_end - _binary_lunar_table_bin_start);
  g_assets.amap_root_ca.data = _binary_amap_root_ca_pem_start;
  g_assets.amap_root_ca.size =
      (size_t)(_binary_amap_root_ca_pem_end - _binary_amap_root_ca_pem_start);
  g_assets.hko_root_ca.data = _binary_hko_root_ca_pem_start;
  g_assets.hko_root_ca.size =
      (size_t)(_binary_hko_root_ca_pem_end - _binary_hko_root_ca_pem_start);

  /* 嵌入的资源比源文件多一个结尾 NUL（见 tools/pad_asset_nul.py），因此
   * 段长度先减一才是真实内容长度。这里顺带校验那个 NUL 确实存在，避免构建
   * 流程被改动后静默出错——调用方依赖 data[size] 恒为 '\0'。 */
  AssetBlob* blobs[] = {
      &g_assets.font_cn,
      &g_assets.lunar_table,
      &g_assets.amap_root_ca,
      &g_assets.hko_root_ca,
  };
  for (size_t i = 0; i < sizeof(blobs) / sizeof(blobs[0]); ++i) {
    if (blobs[i]->size == 0u) {
      return false;
    }
    blobs[i]->size -= 1u;
    if (blobs[i]->data[blobs[i]->size] != '\0') {
      return false;
    }
  }
  return true;
}

#else /* 宿主端：从磁盘读取，便于单元测试与无硬件预览。 */

/* 候选目录覆盖三种常见工作目录：pico_freertos_firmware/、build/ 与
 * tests/build/；最后一项用于从仓库根直接运行宿主测试。 */
static const char* const kAssetDirCandidates[] = {
    "assets",
    "../assets",
    "../../assets",
    "pico_freertos_firmware/assets",
};

static struct {
  AssetBlob font_cn;
  AssetBlob lunar_table;
  AssetBlob amap_root_ca;
  AssetBlob hko_root_ca;
} g_assets;

static bool load_file(const char* dir, const char* name, AssetBlob* out) {
  char path[512];
  int written =
      snprintf(path, sizeof(path), "%s/%s", dir == NULL ? "." : dir, name);
  if (written < 0 || (size_t)written >= sizeof(path)) {
    return false;
  }
  FILE* file = fopen(path, "rb");
  if (file == NULL) {
    return false;
  }
  if (fseek(file, 0, SEEK_END) != 0) {
    fclose(file);
    return false;
  }
  long length = ftell(file);
  if (length < 0) {
    fclose(file);
    return false;
  }
  rewind(file);
  uint8_t* buffer = (uint8_t*)malloc((size_t)length + 1u);
  if (buffer == NULL) {
    fclose(file);
    return false;
  }
  size_t read_bytes = fread(buffer, 1, (size_t)length, file);
  fclose(file);
  if (read_bytes != (size_t)length) {
    free(buffer);
    return false;
  }
  buffer[length] = '\0';
  out->data = buffer;
  out->size = (size_t)length;
  return true;
}

/* 依次尝试候选目录，返回第一个能读到 name 的目录；全部失败返回 NULL。 */
static const char* find_asset_dir(const char* name) {
  const char* override = getenv("PICO_ASSETS_DIR");
  if (override != NULL) {
    AssetBlob probe;
    if (load_file(override, name, &probe)) {
      free((void*)probe.data);
      return override;
    }
  }
  size_t count = sizeof(kAssetDirCandidates) / sizeof(kAssetDirCandidates[0]);
  for (size_t i = 0; i < count; ++i) {
    AssetBlob probe;
    if (load_file(kAssetDirCandidates[i], name, &probe)) {
      free((void*)probe.data);
      return kAssetDirCandidates[i];
    }
  }
  return NULL;
}

bool asset_store_init(void) {
  /* 四个资源可能分布在同一目录，因此只解析一次目录前缀。 */
  const char* dir = find_asset_dir("font_cn.bin");
  if (dir == NULL) {
    return false;
  }
  return load_file(dir, "font_cn.bin", &g_assets.font_cn) &&
         load_file(dir, "lunar_table.bin", &g_assets.lunar_table) &&
         load_file(dir, "amap_root_ca.pem", &g_assets.amap_root_ca) &&
         load_file(dir, "hko_root_ca.pem", &g_assets.hko_root_ca);
}

#endif /* PICO_ON_DEVICE */

const AssetBlob* asset_store_font_cn(void) {
  return g_assets.font_cn.data == NULL ? NULL : &g_assets.font_cn;
}

const AssetBlob* asset_store_lunar_table(void) {
  return g_assets.lunar_table.data == NULL ? NULL : &g_assets.lunar_table;
}

const AssetBlob* asset_store_amap_root_ca(void) {
  return g_assets.amap_root_ca.data == NULL ? NULL : &g_assets.amap_root_ca;
}

const AssetBlob* asset_store_hko_root_ca(void) {
  return g_assets.hko_root_ca.data == NULL ? NULL : &g_assets.hko_root_ca;
}
