/**
 * @file theme_commit_task.c
 * @brief 主题提交任务：5 s 去抖后把主题写进配置扇区。
 *
 * 任务每 100 ms 醒一次，只做三件事：取快照、问策略要不要写、写之前等渲染
 * 窗口空闲。所有「该不该写」的判断都在 theme_commit_policy.c（纯逻辑、宿主
 * 可测），这里只负责 I/O 与那 1 s 的 Verify 重试等待。
 */

#include "theme_commit_task.h"

#include <stdio.h>

#include "FreeRTOS.h"
#include "task.h"

#include "render_window.h"
#include "system_state.h"
#include "theme_commit_policy.h"
#include "theme_storage.h"

/** @brief 任务参数。 */
enum {
  kThemeCommitPeriodMs = 100,   /**< 轮询周期 */
  kThemeCommitStackWords = 256, /**< 静态栈深度，Words 而非字节 */
  kThemeCommitPriority = 2,     /**< 与 Wi-Fi 任务同级 */
  kRenderWindowWaitMs = 40,     /**< 等渲染窗口空闲的上限 */
  kRenderWindowPollMs = 20,     /**< 每 20 ms 看一次 */
  kVerifyRetryDelayMs = 1000,   /**< 回读不一致后等 1 s 再试 */
};

/** @brief 任务控制块与栈必须静态分配：任务常驻，不能占用动态堆。 */
static StaticTask_t s_task_tcb;
static StackType_t s_task_stack[kThemeCommitStackWords];

/** @brief 提交策略状态；只在提交任务里访问。 */
static ThemeCommitPolicy s_policy;

/**
 * @brief 把存储层结果映射成策略层认识的结果。
 *
 * @param result theme_storage_commit() 的返回值。
 * @return 对应策略结果。
 */
static ThemeCommitOutcome map_result(ThemeStorageResult result) {
  switch (result) {
    case kThemeStorageOk:
      return kThemeCommitOutcomeOk;
    case kThemeStorageErrVerify:
      return kThemeCommitOutcomeVerify;
    case kThemeStorageErrNoHeap:
      return kThemeCommitOutcomeNoHeap;
    case kThemeStorageErrNoResources:
      return kThemeCommitOutcomeNoResources;
    case kThemeStorageErrInvalidArg:
      /* 调用方违约：本仓不可达；按规格 §5.4.2 其余码一律转 Failed。 */
      return kThemeCommitOutcomeFatal;
    case kThemeStorageErrEnterTimeout:
    case kThemeStorageErrExitTimeout:
      return kThemeCommitOutcomeTimeout;
    default:
      return kThemeCommitOutcomeFatal;
  }
}

/**
 * @brief 等渲染窗口空闲；超时返回 false，本轮不写。
 *
 * 擦写会夺走 Core 1 约 400 ms，若正好卡在一次 SPI 切片传输中间，屏幕会
 * 留下半张切片。宁可晚 100 ms 再试，也不留下可复现的撕裂。
 *
 * @return 拿到空闲窗口返回 true。
 */
static bool wait_render_window_idle(void) {
  int waited_ms = 0;
  while (!render_window_is_idle()) {
    if (waited_ms >= kRenderWindowWaitMs) {
      return false;
    }
    vTaskDelay(pdMS_TO_TICKS(kRenderWindowPollMs));
    waited_ms += kRenderWindowPollMs;
  }
  return true;
}

/**
 * @brief 写一次；回读不一致时按规格等 1 s 再试一次。
 *
 * 策略层在第一次 Verify 时留在 Writing 并置位 verify_retried，第二次仍
 * Verify 才转 Failed；因此这里只需照状态判断还能不能再试。
 */
static void commit_once(void) {
  for (;;) {
    const ThemeStorageResult result = theme_storage_commit(s_policy.writing);
    theme_commit_policy_result(&s_policy, map_result(result));
    const bool retry =
        result == kThemeStorageErrVerify &&
        theme_commit_policy_state(&s_policy) == kThemeCommitWriting;
    if (!retry) {
      return;
    }
    vTaskDelay(pdMS_TO_TICKS(kVerifyRetryDelayMs));
  }
}

/**
 * @brief 任务主体。
 *
 * @param context 未使用。
 */
static void vTaskThemeCommit(void* context) {
  (void)context;
  /* committed 取开机读回值：内存态与扇区永远从同一个事实出发。 */
  theme_commit_policy_init(&s_policy, theme_storage_init());

  for (;;) {
    vTaskDelay(pdMS_TO_TICKS(kThemeCommitPeriodMs));
    SystemSnapshot snapshot;
    system_state_get_snapshot(&snapshot);
    /* theme_storage_init() 只是读 12 字节并校验，幂等且不碰 Flash，
     * 用它回答「扇区里是不是已经是这套主题」，免得再抄一份扇区偏移。 */
    const bool saved_matches = theme_storage_init() == snapshot.theme_id;
    const uint32_t now_ms = (uint32_t)pdTICKS_TO_MS(xTaskGetTickCount());
    /* 窗口没空就先不推进策略：tick 一旦返回 Write 就把状态置为 Writing，
     * 而 Writing 的唯一出口是写入结果；此处直接跳过会让本次上电再也落不
     * 了盘（没有回滚通道）。等窗口是纯等待，不改变任何状态。 */
    if (!wait_render_window_idle()) {
      continue;
    }
    switch (theme_commit_policy_tick(&s_policy, snapshot.theme_id, now_ms,
                                     saved_matches)) {
      case kThemeCommitActionShortCircuit:
        printf("theme: skip id=%u\n", (unsigned)s_policy.seen);
        break;
      case kThemeCommitActionWrite:
        commit_once();
        break;
      default:
        break;
    }
  }
}

bool theme_commit_task_start(void) {
  TaskHandle_t handle = xTaskCreateStaticAffinitySet(
      vTaskThemeCommit, "theme commit", kThemeCommitStackWords, NULL,
      kThemeCommitPriority, s_task_stack, &s_task_tcb, 1u << 0);
  return handle != NULL;
}
