/**
 * @file supervisor.c
 * @brief 监督任务：喂硬件看门狗、汇总各子系统存活情况、打印运行时诊断。
 */

#include "supervisor.h"

#ifdef PICO_ON_DEVICE

#include <stdio.h>

#include "FreeRTOS.h"
#include "task.h"

#include "hardware/watchdog.h"
#include "pico/stdlib.h"

#include "supervisor_monitor.h"

/* 看门狗 scratch 寄存器的用途：scratch[0] 记录复位前监督任务处在哪个阶段，
 * 复位后读出来就能区分「跑着跑着卡死了」和「刚启动就崩了」。
 *
 * scratch 寄存器在复位时不会被清零（它们由看门狗外设持有），RP2040 的
 * 其他 RAM 内容会丢失，所以这是唯一能跨复位传递信息的渠道。 */
enum {
  kScratchPhaseIndex = 0,
  kScratchPhaseBoot = 0,
  kScratchPhaseHealthy = 1,
  kScratchPhaseUnhealthy = 2,
};

/* 诊断日志的打印周期：10 秒一次足够看出趋势，又不会把串口刷满。 */
enum {
  kDiagnosticsPeriodMs = 10000,
  kMicrosecondsPerMillisecond = 1000,
  kMillisecondsToNextSecond = 1000,
};

/* 上一次打印诊断时的毫秒时刻。任务本身只在初始化时写、循环里读写，
 * 不涉及跨核竞争。 */
static uint32_t s_last_diagnostics_ms = 0;

static uint32_t now_ms(void) {
  return (uint32_t)pdTICKS_TO_MS(xTaskGetTickCount());
}

void supervisor_log_reset_reason(void) {
  uint32_t phase = watchdog_hw->scratch[kScratchPhaseIndex];
  bool caused_reboot = watchdog_caused_reboot();

  if (caused_reboot) {
    printf("复位原因：看门狗超时。复位前阶段标记 = %u（%s）。\n",
           (unsigned)phase,
           phase == kScratchPhaseUnhealthy ? "子系统已判定不健康，主动停止喂狗"
                                           : "子系统看起来正常，可能卡在别处");
  } else {
    printf("复位原因：非看门狗（上电或外部复位）。阶段标记 = %u。\n",
           (unsigned)phase);
  }
}

/* 任务表最多容纳的条目数：当前 10 个任务（空闲 x2、定时器、监督、双核心跳、
 * 显示、Wi-Fi、UDP、NTP、天气），留一倍余量。 */
enum { kMaxTaskStatusEntries = 24 };

static TaskStatus_t s_task_status[kMaxTaskStatusEntries];

/* 打印一次运行时诊断。这些数字是本项目唯一能拿到的「运行证据」：
 * 日志里的剩余堆若长期逼近 0，说明某个模块的内存估计偏乐观；
 * 某个任务的栈水位若接近 0，说明它的栈给少了。 */
static void supervisor_print_diagnostics(void) {
  printf("[诊断] 堆剩余 %u 字节 / 历史最低 %u 字节\n",
         (unsigned)xPortGetFreeHeapSize(),
         (unsigned)xPortGetMinimumEverFreeHeapSize());

  const UBaseType_t count =
      uxTaskGetSystemState(s_task_status, kMaxTaskStatusEntries, NULL);
  for (UBaseType_t i = 0; i < count; ++i) {
    printf("  %-14s 状态=%u 优先级=%u 栈水位=%u 字\n",
           pcTaskGetTaskName(s_task_status[i].xHandle),
           (unsigned)s_task_status[i].eCurrentState,
           (unsigned)s_task_status[i].uxCurrentPriority,
           (unsigned)s_task_status[i].usStackHighWaterMark);
  }
}

static void supervisor_task(void* argument) {
  (void)argument;

  printf("监督任务已启动：看门狗 %d 毫秒，检查周期 %d 毫秒。\n",
         kSupervisorWatchdogTimeoutMs, kSupervisorPeriodMs);

  for (;;) {
    const uint32_t current_ms = now_ms();
    const SupervisorSlot stale = supervisor_monitor_first_stale(current_ms);
    const uint32_t elapsed_ms = current_ms - s_last_diagnostics_ms;

    if (stale == kSupervisorSlotCount) {
      /* 所有子系统都在签到。写入阶段标记再喂狗——顺序不能反，否则复位后
       * 读到的标记会与实际状态错位一档。 */
      watchdog_hw->scratch[kScratchPhaseIndex] = kScratchPhaseHealthy;
      watchdog_update();
    } else {
      /* 明确记录「因为不健康所以不喂」，让复位后的日志能说清原因。 */
      watchdog_hw->scratch[kScratchPhaseIndex] = kScratchPhaseUnhealthy;
      printf("[监督] 子系统「%s」超过 %u 毫秒未签到，停止喂狗。\n",
             supervisor_slot_name(stale),
             (unsigned)supervisor_slot_timeout_ms(stale));
      printf("        看门狗将在 %d 毫秒后复位整机。\n",
             kSupervisorWatchdogTimeoutMs);

      /* 连续超时就连续报警；这是刻意的——排查时更希望看到重复行，而不是
       * 一条被后面日志淹没的记录。 */
    }

    if (elapsed_ms >= kDiagnosticsPeriodMs) {
      s_last_diagnostics_ms = current_ms;
      supervisor_print_diagnostics();
    }

    vTaskDelay(pdMS_TO_TICKS(kSupervisorPeriodMs));
  }
}

bool supervisor_start(void) {
  /* 先建立签到基准时刻，否则刚启动的头一轮检查会把所有槽位判成超时。 */
  supervisor_monitor_reset(now_ms());

  /* 看门狗立即开始倒计时：之后任何一步卡住，8 秒后整机复位重来。 */
  watchdog_enable(kSupervisorWatchdogTimeoutMs, false);
  watchdog_hw->scratch[kScratchPhaseIndex] = kScratchPhaseBoot;

  const BaseType_t created =
      xTaskCreate(supervisor_task, "supervisor", kSupervisorStackWords, NULL,
                  kSupervisorPriority, NULL);
  return created == pdPASS;
}

#else /* PICO_ON_DEVICE */

bool supervisor_start(void) { return false; }

void supervisor_log_reset_reason(void) {}

#endif /* PICO_ON_DEVICE */
