#include "supervisor_monitor.h"

#include <stddef.h>

/* 每个槽位的允许静默时长。
 *
 * 取值原则是「该任务最慢的循环周期 × 3~5」，留足余量又能在真正卡死时
 * 及时暴露：
 *
 * - Wi-Fi 在指数退避的最长一档要等 30s，但任务本身每秒醒一次签到，所以
 *   静默超时按任务循环算，不按重连周期算；
 * - UDP 用带超时的阻塞收包，没报文时也会按时醒来签到；
 * - NTP / 天气都是「主动等下一个周期」，同样按秒级循环签到；
 * - UI 渲染 20ms 一轮，超时可以给得很紧；
 * - 两个核心心跳 1s 一次。 */
static const uint32_t kSlotTimeoutsMs[kSupervisorSlotCount] = {
    5000,  /* wifi */
    15000, /* udp：收包超时 5s，留三倍余量 */
    30000, /* ntp */
    30000, /* weather：含一次完整 HTTPS 握手的时间 */
    1000,  /* ui：20ms 一轮，1s 已经非常宽松 */
    3000,  /* core0 心跳 */
    3000,  /* core1 心跳 */
};

static const char* const kSlotNames[kSupervisorSlotCount] = {
    "wifi", "udp", "ntp", "weather", "ui", "core0", "core1",
};

/* 最近一次签到的时间戳（毫秒）。每个槽位只有对应任务在写、监督任务在读，
 * 32 位读写在 Cortex-M0+ 上是原子的，不需要额外的锁。 */
static volatile uint32_t s_last_check_in_ms[kSupervisorSlotCount];

/* 用无符号差值判断超时，天然容忍 tick 回绕：即使 now_ms 从 0xFFFFFFFF
 * 翻到 0，差值仍然正确。 */
static bool slot_is_stale(SupervisorSlot slot, uint32_t now_ms) {
  uint32_t elapsed = now_ms - s_last_check_in_ms[slot];
  return elapsed > kSlotTimeoutsMs[slot];
}

void supervisor_monitor_reset(uint32_t now_ms) {
  for (int i = 0; i < kSupervisorSlotCount; ++i) {
    s_last_check_in_ms[i] = now_ms;
  }
}

void supervisor_monitor_check_in(SupervisorSlot slot, uint32_t now_ms) {
  /* SupervisorSlot 的底层类型是无符号，写成 slot < 0 会被 -Wtype-limits
   * 判为恒假；先转无符号再比，负值会回绕成大数从而被正确拒绝。 */
  if ((unsigned)slot >= (unsigned)kSupervisorSlotCount) {
    return;
  }
  s_last_check_in_ms[slot] = now_ms;
}

SupervisorSlot supervisor_monitor_first_stale(uint32_t now_ms) {
  for (int i = 0; i < kSupervisorSlotCount; ++i) {
    if (slot_is_stale((SupervisorSlot)i, now_ms)) {
      return (SupervisorSlot)i;
    }
  }
  return kSupervisorSlotCount;
}

uint32_t supervisor_monitor_stale_count(uint32_t now_ms) {
  uint32_t count = 0;
  for (int i = 0; i < kSupervisorSlotCount; ++i) {
    if (slot_is_stale((SupervisorSlot)i, now_ms)) {
      ++count;
    }
  }
  return count;
}

const char* supervisor_slot_name(SupervisorSlot slot) {
  /* SupervisorSlot 的底层类型是无符号，写成 slot < 0 会被 -Wtype-limits
   * 判为恒假；先转无符号再比，负值会回绕成大数从而被正确拒绝。 */
  if ((unsigned)slot >= (unsigned)kSupervisorSlotCount) {
    return "unknown";
  }
  return kSlotNames[slot];
}

uint32_t supervisor_slot_timeout_ms(SupervisorSlot slot) {
  /* SupervisorSlot 的底层类型是无符号，写成 slot < 0 会被 -Wtype-limits
   * 判为恒假；先转无符号再比，负值会回绕成大数从而被正确拒绝。 */
  if ((unsigned)slot >= (unsigned)kSupervisorSlotCount) {
    return 0u;
  }
  return kSlotTimeoutsMs[slot];
}
