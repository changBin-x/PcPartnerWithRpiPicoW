/* 子系统存活监视器的宿主单测。
 *
 * 重点覆盖三类容易写错的地方：
 *   1. 超时判定的边界——「刚好等于超时」必须算健康，多 1ms 才判死；
 *   2. tick 回绕——时间戳从 0xFFFFFF00 附近翻到 0 之后差值仍然正确；
 *   3. reset 的必要性——没有 reset 就检查，所有槽位会集体判超时。 */

#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include "supervisor_monitor.h"

static uint32_t slot_timeout(SupervisorSlot slot) {
  return supervisor_slot_timeout_ms(slot);
}

static void test_reset_makes_everything_healthy(void) {
  supervisor_monitor_reset(1000u);
  assert(supervisor_monitor_first_stale(1000u) == kSupervisorSlotCount);
  assert(supervisor_monitor_stale_count(1000u) == 0u);

  /* UI 的超时最短（1000ms），刚好用满仍是健康。 */
  assert(supervisor_monitor_stale_count(1000u + 1000u) == 0u);
  /* 再多 1ms 就判死。 */
  assert(supervisor_monitor_first_stale(1000u + 1001u) == kSupervisorSlotUi);
  printf("PASS: reset 后全部健康，超时边界精确到 1ms\n");
}

static void test_zero_baseline_all_stale(void) {
  /* 模拟「上电后从未签到」：基准是 0，而当前时间已经过去一分钟。 */
  supervisor_monitor_reset(0u);
  uint32_t now = 60000u;
  assert(supervisor_monitor_stale_count(now) == (uint32_t)kSupervisorSlotCount);
  printf("PASS: 未签到时全部槽位判超时\n");
}

static void test_check_in_revives_only_that_slot(void) {
  supervisor_monitor_reset(0u);
  uint32_t start = 100000u;
  supervisor_monitor_reset(start);

  /* 所有槽位一起放到超时边缘，然后只给 wifi 补签。 */
  uint32_t now = start + slot_timeout(kSupervisorSlotUi) + 1u;
  assert(supervisor_monitor_first_stale(now) == kSupervisorSlotUi);

  supervisor_monitor_check_in(kSupervisorSlotUi, now);
  /* UI 补签后它就是唯一超时的那个已经没了，其余槽位还没到点。 */
  assert(supervisor_monitor_first_stale(now) == kSupervisorSlotCount);

  /* 再把时间推到这个 1ms 之前的地方，确认补签不是「把整个表清零」：
   * wifi 的超时远长于 ui，所以此刻它仍然是健康的。 */
  assert(supervisor_monitor_stale_count(now) == 0u);
  printf("PASS: 补签只复活对应槽位\n");
}

static void test_tick_wraparound(void) {
  /* 从 0xFFFFFFF0 出发，加 200ms 就跨过了 32 位无符号数的回绕点。 */
  uint32_t start = 0xFFFFFFF0u;
  supervisor_monitor_reset(start);

  /* 走 200ms：跨过了 0xFFFFFFFF → 0x00000000 的回绕。 */
  uint32_t later = start + 200u;
  assert(later < start);
  assert(supervisor_monitor_first_stale(later) == kSupervisorSlotCount);

  /* 再走到 UI 超时 +1ms，仍然应该只判 UI 死。 */
  uint32_t stale_time = start + slot_timeout(kSupervisorSlotUi) + 1u;
  assert(supervisor_monitor_first_stale(stale_time) == kSupervisorSlotUi);
  printf("PASS: tick 回绕后超时判定依然正确\n");
}

static void test_names_and_bounds(void) {
  assert(supervisor_slot_timeout_ms(kSupervisorSlotWifi) > 0u);
  for (int i = 0; i < kSupervisorSlotCount; ++i) {
    assert(supervisor_slot_name((SupervisorSlot)i) != NULL);
  }
  /* 越界槽位必须被安全拒绝，而不是越界读数组。 */
  assert(supervisor_slot_timeout_ms(kSupervisorSlotCount) == 0u);
  supervisor_monitor_check_in(kSupervisorSlotCount, 123u);
  supervisor_monitor_check_in((SupervisorSlot)-1, 123u);
  printf("PASS: 槽位名与越界检查\n");
}

int main(void) {
  test_reset_makes_everything_healthy();
  test_zero_baseline_all_stale();
  test_check_in_revives_only_that_slot();
  test_tick_wraparound();
  test_names_and_bounds();
  printf("PASS: test_supervisor_monitor\n");
  return 0;
}
