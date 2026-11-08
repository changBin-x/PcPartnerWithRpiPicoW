#ifndef PICO_FREERTOS_FIRMWARE_INCLUDE_SUPERVISOR_MONITOR_H_
#define PICO_FREERTOS_FIRMWARE_INCLUDE_SUPERVISOR_MONITOR_H_

#include <stdbool.h>
#include <stdint.h>

/* 子系统存活监视器。
 *
 * 这里只做纯逻辑：谁在什么时候签过到、谁已经超时。不含任何 FreeRTOS 调用，
 * 因此可以在宿主上直接单测（尤其是 tick 回绕与「刚好等于超时」这两个边界）。
 *
 * 每个被监视的子系统在各自的任务循环里定期调用一次
 * supervisor_monitor_check_in()，监视器记录时间戳但不阻塞任何人。
 * 监督任务随后用它判断整体是否健康，健康才喂硬件看门狗。
 *
 * 有的子系统会因为配置缺失（没填 SSID、没配高德 Key）而主动退出。这类
 * 退出是设计内行为，不能算故障，否则整机会无限复位。这类子系统在退出前
 * 调用 supervisor_monitor_disable() 把自己从监视里摘掉。 */

/* 被监视的槽位。
 *
 * 顺序即打印顺序，末尾的 kSupervisorSlotCount 兼作「没有不健康槽位」的
 * 哨兵返回值，所以不要把它当成真实槽位使用。 */
typedef enum {
  kSupervisorSlotWifi = 0,
  kSupervisorSlotUdp,
  kSupervisorSlotNtp,
  kSupervisorSlotWeather,
  kSupervisorSlotUi,
  kSupervisorSlotCore0,
  kSupervisorSlotCore1,
  kSupervisorSlotCount
} SupervisorSlot;

/* 把全部槽位置为「刚签过到」并重新启用，以 now_ms 为基准。
 *
 * 启动时必须先调用一次，否则所有槽位都停留在初始的 0，第一次检查就会
 * 集体判超时。 */
void supervisor_monitor_reset(uint32_t now_ms);

/* 记录某个槽位在 now_ms 时刻仍然存活。对已停用的槽位无副作用。 */
void supervisor_monitor_check_in(SupervisorSlot slot, uint32_t now_ms);

/* 停用 / 重新启用某个槽位。停用后的槽位不参与超时判断，supervisor 也就
 * 不会因为它停止喂狗。用于「该子系统已按设计主动关闭」的场景。 */
void supervisor_monitor_disable(SupervisorSlot slot);
void supervisor_monitor_enable(SupervisorSlot slot);

/* 槽位当前是否参与监视。 */
bool supervisor_monitor_is_enabled(SupervisorSlot slot);

/* 返回第一个已经超时的槽位；全部健康时返回 kSupervisorSlotCount。 */
SupervisorSlot supervisor_monitor_first_stale(uint32_t now_ms);

/* 统计已经超时的槽位个数（已停用的槽位不计入）。 */
uint32_t supervisor_monitor_stale_count(uint32_t now_ms);

/* 统计当前处于停用状态的槽位个数。 */
uint32_t supervisor_monitor_disabled_count(void);

/* 槽位的展示名（用于串口日志）与允许的静默时长。 */
const char* supervisor_slot_name(SupervisorSlot slot);
uint32_t supervisor_slot_timeout_ms(SupervisorSlot slot);

#endif /* PICO_FREERTOS_FIRMWARE_INCLUDE_SUPERVISOR_MONITOR_H_ */
