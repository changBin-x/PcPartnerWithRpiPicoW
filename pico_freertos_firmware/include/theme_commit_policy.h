/**
 * @file theme_commit_policy.h
 * @brief 主题落盘策略：5 s 去抖、零擦写短路、Verify 只重试一次。
 */

#ifndef PICO_FREERTOS_FIRMWARE_INCLUDE_THEME_COMMIT_POLICY_H_
#define PICO_FREERTOS_FIRMWARE_INCLUDE_THEME_COMMIT_POLICY_H_

#include <stdbool.h>
#include <stdint.h>

#include "ui_theme_id.h"

/** @brief 提交状态机（规格 §5.4.2）。 */
typedef enum {
  kThemeCommitIdle = 0, /**< 无待提交变更 */
  kThemeCommitPending,  /**< 已切换，等 5 s 去抖 */
  kThemeCommitWriting,  /**< 正在 flash_safe_execute */
  kThemeCommitDone,     /**< 已落盘且回读一致 */
  kThemeCommitFailed,   /**< 放弃本次提交，保留内存态 */
} ThemeCommitState;

/** @brief 策略层认识的结果；由任务层从 ThemeStorageResult 映射而来。 */
typedef enum {
  kThemeCommitOutcomeOk = 0,      /**< 成功 */
  kThemeCommitOutcomeVerify,      /**< 回读不一致，可再试一次 */
  kThemeCommitOutcomeNoHeap,      /**< 堆预检未过，不重试 */
  kThemeCommitOutcomeNoResources, /**< 建避难任务失败，不重试 */
  kThemeCommitOutcomeTimeout,     /**< 握手超时，不重试 */
  kThemeCommitOutcomeFatal,       /**< 其余错误 */
} ThemeCommitOutcome;

/** @brief 策略要做的一件事。 */
typedef enum {
  kThemeCommitActionNone = 0,     /**< 什么也不做 */
  kThemeCommitActionShortCircuit, /**< 当前快照已等于已保存值 */
  kThemeCommitActionWrite,        /**< 调 theme_storage_commit(seen) */
} ThemeCommitAction;

/** @brief 提交策略的全部状态。 */
typedef struct {
  ThemeCommitState state;
  UiThemeId seen;       /**< 最近一次看到的主题 */
  UiThemeId committed;  /**< 已确认落盘的主题 */
  UiThemeId writing;    /**< 正在写的主题，重试复用 */
  uint32_t deadline_ms; /**< 去抖截止时刻 */
  bool verify_retried;  /**< 本次提交是否已用掉那 1 次重试 */
} ThemeCommitPolicy;

/**
 * @brief 初始化；committed 取开机读回值。
 *
 * @param policy    待初始化的策略对象。
 * @param committed 开机从 XIP 读回并校验通过的主题。
 */
void theme_commit_policy_init(ThemeCommitPolicy* policy, UiThemeId committed);

/**
 * @brief 推进一拍：去抖计时、短路判定与写入门控都在这里。
 *
 * @param policy        策略对象。
 * @param current       当前快照里的主题。
 * @param now_ms        单调毫秒。
 * @param saved_matches XIP 里的配置是否已等于 current。
 * @return 本拍需要执行的动作。
 */
ThemeCommitAction theme_commit_policy_tick(ThemeCommitPolicy* policy,
                                           UiThemeId current, uint32_t now_ms,
                                           bool saved_matches);

/**
 * @brief 喂回一次写入的结果；非 Writing 状态下的迟到结果被丢弃。
 *
 * @param policy  策略对象。
 * @param outcome 任务层映射后的结果。
 */
void theme_commit_policy_result(ThemeCommitPolicy* policy,
                                ThemeCommitOutcome outcome);

/**
 * @brief 读当前状态（测试与日志用）。
 *
 * @param policy 策略对象。
 * @return 当前状态。
 */
ThemeCommitState theme_commit_policy_state(const ThemeCommitPolicy* policy);

/**
 * @brief 读已落盘主题（测试与日志用）。
 *
 * @param policy 策略对象。
 * @return 已确认落盘的主题。
 */
UiThemeId theme_commit_policy_committed(const ThemeCommitPolicy* policy);

#endif /* PICO_FREERTOS_FIRMWARE_INCLUDE_THEME_COMMIT_POLICY_H_ */
