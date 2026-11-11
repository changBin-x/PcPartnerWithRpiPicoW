/**
 * @file theme_storage.h
 * @brief 主题配置的 Flash 持久化：同步提交、回读校验与镜像边界守卫。
 */

#ifndef PICO_FREERTOS_FIRMWARE_INCLUDE_THEME_STORAGE_H_
#define PICO_FREERTOS_FIRMWARE_INCLUDE_THEME_STORAGE_H_

#include "ui_theme_id.h"

/** @brief 提交结果；每个码只有一个生产者（规格 §5.4.1）。 */
typedef enum {
  kThemeStorageOk = 0,          /**< 提交成功且回读一致 */
  kThemeStorageErrEnterTimeout, /**< enter 握手超时：回调未执行 */
  kThemeStorageErrExitTimeout,  /**< exit 握手超时：回调已执行 */
  kThemeStorageErrVerify,       /**< 回读与预期不一致 */
  kThemeStorageErrInvalidArg,   /**< 入参越界（调用方违约）：不碰 Flash */
  kThemeStorageErrNoHeap,       /**< 堆预检未过：未进入握手 */
  kThemeStorageErrNoResources,  /**< 建避难任务失败：未动 Flash */
} ThemeStorageResult;

/**
 * @brief 校验镜像边界并读出已保存主题。
 *
 * @return 有效主题 id；扇区空白、损坏或版本不符时返回 kThemeM3Dark。
 */
UiThemeId theme_storage_init(void);

/**
 * @brief 同步提交主题，阻塞直到写完或失败（预算 403 ms，上界 650 ms）。
 *
 * @param theme_id 目标主题；越界属调用方违约：先 configASSERT（固件里
 *                 停在 firmware_assert_failed() 自旋、不返回），在断言可
 *                 返回的构建里返回 kThemeStorageErrInvalidArg；两种情形都
 *                 不写 Flash，也不夹紧落盘。
 * @return ThemeStorageResult。
 */
ThemeStorageResult theme_storage_commit(UiThemeId theme_id);

#endif /* PICO_FREERTOS_FIRMWARE_INCLUDE_THEME_STORAGE_H_ */
