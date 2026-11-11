/**
 * @file test_theme_commit_policy.c
 * @brief 覆盖绕回、5 s 去抖、零擦写短路、Verify 只重试一次、
 *        Failed 之后不自动重入、渲染窗口的有界等待。
 */

#include <stdio.h>
#include <stdlib.h>

#include "render_window.h"
#include "theme_commit_policy.h"
#include "theme_switch.h"

/** @brief 断言宏：失败时打印行号并退出。 */
#define CHECK(cond)                                      \
  do {                                                   \
    if (!(cond)) {                                       \
      printf("FAIL: %s（第 %d 行）\n", #cond, __LINE__); \
      return EXIT_FAILURE;                               \
    }                                                    \
  } while (0)

int main(void) {
  /* 绕回：5 → 0。 */
  CHECK(theme_switch_advance(kThemeHud, true) == kThemeSwiss);
  CHECK(theme_switch_advance(kThemeSwiss, true) == kThemeTui);
  CHECK(theme_switch_advance(kThemeScope, false) == kThemeScope);

  /* 渲染窗口：进入后非空闲，离开后空闲。 */
  render_window_leave();
  CHECK(render_window_is_idle());
  render_window_enter();
  CHECK(!render_window_is_idle());
  render_window_leave();
  CHECK(render_window_is_idle());

  /* 状态机：切换后要等满 5 s 才动。 */
  ThemeCommitPolicy policy;
  theme_commit_policy_init(&policy, kThemeSwiss);
  CHECK(theme_commit_policy_tick(&policy, kThemeTui, 1000, false) ==
        kThemeCommitActionNone);
  CHECK(theme_commit_policy_state(&policy) == kThemeCommitPending);
  CHECK(theme_commit_policy_tick(&policy, kThemeTui, 5999, false) ==
        kThemeCommitActionNone);
  CHECK(theme_commit_policy_tick(&policy, kThemeTui, 6000, false) ==
        kThemeCommitActionWrite);

  /* 零擦写短路：XIP 里的配置已经是目标值。 */
  theme_commit_policy_init(&policy, kThemeSwiss);
  CHECK(theme_commit_policy_tick(&policy, kThemeBento, 0, false) ==
        kThemeCommitActionNone);
  CHECK(theme_commit_policy_tick(&policy, kThemeBento, 5000, true) ==
        kThemeCommitActionShortCircuit);
  CHECK(theme_commit_policy_state(&policy) == kThemeCommitDone);
  CHECK(theme_commit_policy_tick(&policy, kThemeBento, 9000, true) ==
        kThemeCommitActionNone);

  /* 成功路径：写入后转 Done，且不再重复写。 */
  theme_commit_policy_init(&policy, kThemeSwiss);
  (void)theme_commit_policy_tick(&policy, kThemeScope, 0, false);
  CHECK(theme_commit_policy_tick(&policy, kThemeScope, 5000, false) ==
        kThemeCommitActionWrite);
  theme_commit_policy_result(&policy, kThemeCommitOutcomeOk);
  CHECK(theme_commit_policy_state(&policy) == kThemeCommitDone);
  CHECK(theme_commit_policy_committed(&policy) == kThemeScope);
  CHECK(theme_commit_policy_tick(&policy, kThemeScope, 20000, false) ==
        kThemeCommitActionNone);

  /* Verify 只重试一次；第二次仍 Verify 必须停在 Failed。 */
  theme_commit_policy_init(&policy, kThemeSwiss);
  (void)theme_commit_policy_tick(&policy, kThemeHud, 0, false);
  CHECK(theme_commit_policy_tick(&policy, kThemeHud, 5000, false) ==
        kThemeCommitActionWrite);
  theme_commit_policy_result(&policy, kThemeCommitOutcomeVerify);
  CHECK(theme_commit_policy_state(&policy) == kThemeCommitWriting);
  theme_commit_policy_result(&policy, kThemeCommitOutcomeVerify);
  CHECK(theme_commit_policy_state(&policy) == kThemeCommitFailed);

  /* 终止性：Failed 之后即使截止时刻已过也不得被拉回 Writing。 */
  for (unsigned i = 0; i < 10; ++i) {
    CHECK(theme_commit_policy_tick(&policy, kThemeHud, 20000u + i * 100u,
                                   false) == kThemeCommitActionNone);
    CHECK(theme_commit_policy_state(&policy) == kThemeCommitFailed);
  }

  /* 同一主题不得离开 Failed；换成别的主题才允许回到 Pending。 */
  CHECK(theme_commit_policy_tick(&policy, kThemeHud, 30000, false) ==
        kThemeCommitActionNone);
  CHECK(theme_commit_policy_state(&policy) == kThemeCommitFailed);
  CHECK(theme_commit_policy_tick(&policy, kThemeBento, 30100, false) ==
        kThemeCommitActionNone);
  CHECK(theme_commit_policy_state(&policy) == kThemeCommitPending);

  /* 分配型失败一律不重试。 */
  theme_commit_policy_init(&policy, kThemeSwiss);
  (void)theme_commit_policy_tick(&policy, kThemeTui, 0, false);
  (void)theme_commit_policy_tick(&policy, kThemeTui, 5000, false);
  theme_commit_policy_result(&policy, kThemeCommitOutcomeNoHeap);
  CHECK(theme_commit_policy_state(&policy) == kThemeCommitFailed);

  printf("PASS: 状态机、绕回与渲染窗口符合规格 §5.4.2\n");
  return EXIT_SUCCESS;
}
