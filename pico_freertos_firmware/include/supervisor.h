#ifndef PICO_FREERTOS_FIRMWARE_INCLUDE_SUPERVISOR_H_
#define PICO_FREERTOS_FIRMWARE_INCLUDE_SUPERVISOR_H_

#include <stdbool.h>
#include <stdint.h>

/* 监督任务：负责喂硬件看门狗，并在任何子系统卡死时停止喂。
 *
 * 它不试图「修好」卡死的子系统——在 264KB 内存、没有 MMU 的 MCU 上，能做的
 * 只有两件事：把现场打到串口，然后让看门狗复位整机。复位后从头再来，远好过
 * 一块永远停在旧画面上的屏幕。 */

/* 硬件看门狗的超时。监督任务每秒喂一次，留 8 秒意味着允许连续 7 次检查
 * 失败才复位，避免偶发的长耗时（比如一次 TLS 握手）被误判为卡死。 */
enum { kSupervisorWatchdogTimeoutMs = 8000 };

/* 监督任务的轮询周期与栈大小。栈用得很少：只做几次比较和 printf。 */
enum {
  kSupervisorPeriodMs = 1000,
  kSupervisorStackWords = 512,
  kSupervisorPriority = 3,
};

/* 启动硬件看门狗并立刻开始喂它。
 *
 * 必须在创建各业务任务之前调用：一旦看门狗开始倒计时，任何任务创建失败导致
 * 的卡死都会在 8 秒内被复位，而不是留下一个半死不活的设备。
 *
 * 返回 false 表示看门狗未能启动（理论上不会发生）。 */
bool supervisor_start(void);

/* 打印本次复位的原因（看门狗 / 上电 / 复位引脚），以及上次运行留下的阶段
 * 标记。仅用于串口诊断，可在 supervisor_start 之前或之后调用。 */
void supervisor_log_reset_reason(void);

#endif /* PICO_FREERTOS_FIRMWARE_INCLUDE_SUPERVISOR_H_ */
