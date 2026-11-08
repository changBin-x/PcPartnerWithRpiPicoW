#include "supervisor_monitor.h"

#include <stddef.h>

/* 每个槽位的允许静默时长。
 *
 * 取值原则是「该任务最慢的一次阻塞 × 3~5」，留足余量又能在真正卡死时
 * 及时暴露：
 *
 * - Wi-Fi 单次连接尝试最长阻塞 15s（cyw43_arch_wifi_connect_timeout_ms），
 *   超时必须大于它，否则「正常连接中」会被误判成卡死；
 * - UDP 用带 5s 超时的阻塞收包，没报文时也会按时醒来签到；
 * - NTP / 天气都是「主动等下一个周期」，两者都在等的时候按秒签到；
 * - UI 渲染 20ms 一轮，超时可以给得很紧；
 * - 两个核心心跳 1s 一次。 */
static const uint32_t kSlotTimeoutsMs[kSupervisorSlotCount] = {
    20000, /* wifi：大于单次连接尝试的 15s */
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

/* 槽位是否参与监视。停用的槽位由子系统自己关闭（例如没配 SSID），
 * 监督任务不应因此停止喂狗。 */
static volatile bool s_enabled[kSupervisorSlotCount];

/* 越界的槽位一律当作非法入参处理，返回 true 表示可以继续往下走。 */
static bool slot_is_valid(SupervisorSlot slot) {
  /* SupervisorSlot 的底层类型是无符号，写成 slot < 0 会被 -Wtype-limits
   * 判为恒假；先转无符号再比，负值会回绕成大数从而被正确拒绝。 */
  return (unsigned)slot < (unsigned)kSupervisorSlotCount;
}

/* 用无符号差值判断超时，天然容忍 tick 回绕：即使 now_ms 从 0xFFFFFFFF
 * 翻到 0，差值仍然正确。 */
static bool slot_is_stale(SupervisorSlot slot, uint32_t now_ms) {
  if (!s_enabled[slot]) {
    return false;
  }
  uint32_t elapsed = now_ms - s_last_check_in_ms[slot];
  return elapsed > kSlotTimeoutsMs[slot];
}

void supervisor_monitor_reset(uint32_t now_ms) {
  for (int i = 0; i < kSupervisorSlotCount; ++i) {
    s_last_check_in_ms[i] = now_ms;
    s_enabled[i] = true;
  }
}

void supervisor_monitor_check_in(SupervisorSlot slot, uint32_t now_ms) {
  if (!slot_is_valid(slot)) {
    return;
  }
  s_last_check_in_ms[slot] = now_ms;
}

void supervisor_monitor_disable(SupervisorSlot slot) {
  if (!slot_is_valid(slot)) {
    return;
  }
  s_enabled[slot] = false;
}

void supervisor_monitor_enable(SupervisorSlot slot) {
  if (!slot_is_valid(slot)) {
    return;
  }
  s_enabled[slot] = true;
}

bool supervisor_monitor_is_enabled(SupervisorSlot slot) {
  if (!slot_is_valid(slot)) {
    return false;
  }
  return s_enabled[slot];
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

uint32_t supervisor_monitor_disabled_count(void) {
  uint32_t count = 0;
  for (int i = 0; i < kSupervisorSlotCount; ++i) {
    if (!s_enabled[i]) {
      ++count;
    }
  }
  return count;
}

const char* supervisor_slot_name(SupervisorSlot slot) {
  if (!slot_is_valid(slot)) {
    return "unknown";
  }
  return kSlotNames[slot];
}

uint32_t supervisor_slot_timeout_ms(SupervisorSlot slot) {
  if (!slot_is_valid(slot)) {
    return 0u;
  }
  return kSlotTimeoutsMs[slot];
}
