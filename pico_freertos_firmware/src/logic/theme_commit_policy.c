/**
 * @file theme_commit_policy.c
 * @brief 主题落盘策略：5 s 去抖、零擦写短路、Verify 只重试一次。
 */

#include "theme_commit_policy.h"

/** @brief 5 s 去抖窗口。 */
enum { kDebounceMs = 5000 };

void theme_commit_policy_init(ThemeCommitPolicy* policy, UiThemeId committed) {
  policy->state = kThemeCommitIdle;
  policy->seen = committed;
  policy->committed = committed;
  policy->writing = committed;
  policy->deadline_ms = 0;
  policy->verify_retried = false;
}

ThemeCommitAction theme_commit_policy_tick(ThemeCommitPolicy* policy,
                                           UiThemeId current, uint32_t now_ms,
                                           bool saved_matches) {
  if (policy->state != kThemeCommitWriting && policy->seen != current) {
    policy->seen = current;
    policy->deadline_ms = now_ms + (uint32_t)kDebounceMs;
    policy->state = kThemeCommitPending;
    policy->verify_retried = false;
    return kThemeCommitActionNone;
  }
  if (policy->state != kThemeCommitPending) {
    return kThemeCommitActionNone;
  }
  if ((int32_t)(now_ms - policy->deadline_ms) < 0) {
    return kThemeCommitActionNone;
  }
  if (policy->seen == policy->committed) {
    policy->state = kThemeCommitDone;
    return kThemeCommitActionShortCircuit;
  }
  if (saved_matches) {
    policy->committed = policy->seen;
    policy->state = kThemeCommitDone;
    return kThemeCommitActionShortCircuit;
  }
  policy->writing = policy->seen;
  policy->state = kThemeCommitWriting;
  return kThemeCommitActionWrite;
}

void theme_commit_policy_result(ThemeCommitPolicy* policy,
                                ThemeCommitOutcome outcome) {
  if (policy->state != kThemeCommitWriting) {
    return;
  }
  if (outcome == kThemeCommitOutcomeOk) {
    policy->committed = policy->writing;
    policy->state = kThemeCommitDone;
    return;
  }
  if (outcome == kThemeCommitOutcomeVerify && !policy->verify_retried) {
    policy->verify_retried = true;
    return; /* 留在 Writing，由任务层等 1 s 再调一次 */
  }
  policy->state = kThemeCommitFailed;
}

ThemeCommitState theme_commit_policy_state(const ThemeCommitPolicy* policy) {
  return policy->state;
}

UiThemeId theme_commit_policy_committed(const ThemeCommitPolicy* policy) {
  return policy->committed;
}
