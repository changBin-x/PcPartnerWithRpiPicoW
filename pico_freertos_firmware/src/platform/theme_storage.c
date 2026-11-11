/**
 * @file theme_storage.c
 * @brief 主题配置写入 Sector 511：堆预检、擦写、回读与边界守卫。
 */

#include "theme_storage.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "FreeRTOS.h"
#include "hardware/flash.h"
#include "pico/flash.h"
#include "pico/stdlib.h"
#include "task.h"

#include "theme_config.h"

/** @brief 配置扇区在 Flash 内的偏移（Sector 511）。 */
#define kThemeFlashOffset 0x1FF000u

/** @brief 进入握手前要求的最小空闲堆字节数。 */
#define kThemeMinFreeHeapBytes 1536u

/** @brief enter 与 exit 各自的握手超时（毫秒）。 */
#define kThemeFlashTimeoutMs 100u

/* 落盘布局：与 theme_config.c 拼出的 12 字节逐字节对应。本结构体不参与
 * 读写，只把布局钉死在编译期——字段顺序一变，回读比对就会永远失败。 */
typedef struct {
  uint32_t magic;      /**< 魔数 */
  uint8_t theme_id;    /**< 主题索引 0..5 */
  uint8_t version;     /**< 结构体版本，当前 1 */
  uint8_t reserved[2]; /**< 保留，必须为 0 */
  uint32_t checksum;   /**< magic ^ (theme_id << 8) ^ version */
} PersistentThemeConfig;

_Static_assert(sizeof(PersistentThemeConfig) == 12, "主题配置应为 12 字节");
_Static_assert(offsetof(PersistentThemeConfig, theme_id) == 4, "布局变了");
_Static_assert(offsetof(PersistentThemeConfig, version) == 5, "布局变了");
_Static_assert(offsetof(PersistentThemeConfig, checksum) == 8, "布局变了");

/* 页缓冲必须在 SRAM 里且不能是 const：ROM 会在 XIP 关闭的窗口内通过 SSI
 * 直接读它，const 会把这块数据放进 Flash。 */
static uint8_t s_flash_page[FLASH_PAGE_SIZE];

_Static_assert(sizeof(s_flash_page) == FLASH_PAGE_SIZE,
               "页缓冲必须是整页，否则 ROM 会越界读");

/* 回调是否真的执行过。enter 超时与 exit 超时都返回 PICO_ERROR_TIMEOUT，
 * 但语义相反；每次调 flash_safe_execute() 之前先清零。 */
static volatile bool s_callback_ran;

/* 由 SDK 链接脚本在 .flash_end 处提供，取的是绝对 XIP 地址。 */
extern char __flash_binary_end;

/* 镜像边界守卫：扇区前最后一个可用字节是 0x101FEFFF，故等于视为合法。 */
static void theme_storage_assert_no_overlap(void) {
  const uintptr_t image_end = (uintptr_t)&__flash_binary_end;
  if (image_end > (uintptr_t)(XIP_BASE + kThemeFlashOffset)) {
    panic("镜像侵占配置扇区");
  }
}

/* 从 XIP 映射地址抄出前 12 字节再解析；解析失败、版本不符或主题越界
 * 都按「不一致」处理。 */
static bool theme_storage_read_back_matches(UiThemeId theme_id) {
  uint8_t page[kThemeConfigSize];
  memcpy(page, (const void*)(uintptr_t)(XIP_BASE + kThemeFlashOffset),
         sizeof(page));
  ThemeConfig config = {0};
  if (!theme_config_parse(page, sizeof(page), &config)) {
    return false;
  }
  return config.theme_id == theme_id;
}

/* 页内容在进入安全区之前备好：回调体内不许 memset / memcpy。
 * theme_config_pack() 自身会先整页填 0xFF，这里的 memset 让「整页已就绪」
 * 这件事在调用点可见。 */
static void theme_prepare_page(UiThemeId theme_id) {
  memset(s_flash_page, 0xFF, sizeof(s_flash_page));
  theme_config_pack(theme_id, s_flash_page, sizeof(s_flash_page));
}

/* 避难回调：双核中断全关，只许调下面两个 SDK 函数（各自成对开关 XIP）。
 * 这里不打印、不断言、不加锁、不分配，也不自行开关 XIP。 */
static void __not_in_flash_func(theme_flash_commit)(void* param) {
  s_callback_ran = true;
  (void)param;
  flash_range_erase(kThemeFlashOffset, FLASH_SECTOR_SIZE);
  flash_range_program(kThemeFlashOffset, s_flash_page, FLASH_PAGE_SIZE);
}

UiThemeId theme_storage_init(void) {
  theme_storage_assert_no_overlap();
  uint8_t page[kThemeConfigSize];
  memcpy(page, (const void*)(uintptr_t)(XIP_BASE + kThemeFlashOffset),
         sizeof(page));
  ThemeConfig config = {0};
  if (!theme_config_parse(page, sizeof(page), &config)) {
    return kThemeM3Dark;
  }
  return config.theme_id;
}

ThemeStorageResult theme_storage_commit(UiThemeId theme_id) {
  if ((unsigned)theme_id >= (unsigned)kThemeCount) {
    /* 越界属调用方违约：不写 Flash，也不夹紧落盘——夹紧会让一次调用方
     * bug 借「幂等重写」把用户已存主题改成主题 0（规格 §5.4.1）。 */
    configASSERT((unsigned)theme_id < (unsigned)kThemeCount);
    return kThemeStorageErrInvalidArg;
  }
  /* 堆预检必须挡在 flash_safe_execute() 之前：分配失败会撞上
   * vApplicationMallocFailedHook() 的复位路径。 */
  if (xPortGetFreeHeapSize() < kThemeMinFreeHeapBytes) {
    return kThemeStorageErrNoHeap;
  }
  theme_prepare_page(theme_id);
  s_callback_ran = false;
  const int rc =
      flash_safe_execute(theme_flash_commit, NULL, kThemeFlashTimeoutMs);
  ThemeStorageResult result;
  if (rc == PICO_OK) {
    result = theme_storage_read_back_matches(theme_id) ? kThemeStorageOk
                                                       : kThemeStorageErrVerify;
  } else if (rc == PICO_ERROR_TIMEOUT) {
    if (!s_callback_ran) {
      result = kThemeStorageErrEnterTimeout;
    } else {
      result = theme_storage_read_back_matches(theme_id)
                   ? kThemeStorageOk
                   : kThemeStorageErrExitTimeout;
    }
  } else if (rc == PICO_ERROR_INSUFFICIENT_RESOURCES) {
    result = kThemeStorageErrNoResources;
  } else {
    result = kThemeStorageErrVerify;
  }
  printf("theme: commit id=%u rc=%d\n", (unsigned)theme_id, (int)result);
  return result;
}
