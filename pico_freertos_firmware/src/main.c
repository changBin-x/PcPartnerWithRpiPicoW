/**
 * @file main.c
 * @brief 固件入口：按依赖顺序拉起全部子系统，然后启动 FreeRTOS SMP 调度器。
 *
 * 启动顺序有讲究，逐条说明：
 *  1. stdio 与复位原因最先打印 —— 若后面任何一步卡住，看门狗复位后至少
 *     能从日志里看出上一次死在什么阶段。
 *  2. 看门狗在业务任务创建之前就启动 —— 创建阶段若卡住同样要能复位。
 *  3. 业务任务（显示、网络）只是被「创建」，真正的初始化发生在各自任务里，
 *     这样初始化耗时不会拖延调度器启动，也不会阻塞另一核的渲染。
 */

#include <stdint.h>
#include <stdio.h>

#include "FreeRTOS.h"
#include "task.h"

#include "pico/stdlib.h"

#include "asset_store.h"
#include "firmware_checkin.h"
#include "network_start.h"
#include "supervisor.h"
#include "supervisor_monitor.h"
#include "system_state.h"
#include "ui_render.h"

/* 心跳任务参数。签到周期 1 秒，但日志每 30 秒才打一行——串口是给人看的，
 * 每秒刷屏会把真正有用的告警冲走。 */
enum {
  kHeartbeatPeriodMs = 1000,
  kHeartbeatLogEvery = 30,
  kHeartbeatPriority = 2,
  kHeartbeatStackWords = 512,
};

/* 心跳任务配置：core_id 同时用于亲和性绑定与日志标签，slot 用于向监督任务
 * 证明本核仍在运转。 */
typedef struct {
  uint32_t core_id;
  const char* task_name;
  SupervisorSlot slot;
} HeartbeatTaskConfig;

static const HeartbeatTaskConfig kCore0Heartbeat = {
    .core_id = 0,
    .task_name = "heartbeat_core0",
    .slot = kSupervisorSlotCore0,
};

static const HeartbeatTaskConfig kCore1Heartbeat = {
    .core_id = 1,
    .task_name = "heartbeat_core1",
    .slot = kSupervisorSlotCore1,
};

/* 心跳任务主体：定期签到，并周期性打印绑定核号与 FreeRTOS 实际调度所在核号。
 * 两个核号应当始终一致；若不一致说明亲和性掩码没生效。 */
static void heartbeat_task(void* context) {
  const HeartbeatTaskConfig* config = (const HeartbeatTaskConfig*)context;
  uint32_t counter = 0;

  for (;;) {
    FIRMWARE_CHECK_IN(config->slot);
    if (counter % kHeartbeatLogEvery == 0) {
      printf("[core%u] 心跳=%u 实际运行核=%u\n", (unsigned)config->core_id,
             (unsigned)counter, (unsigned)portGET_CORE_ID());
    }
    ++counter;
    vTaskDelay(pdMS_TO_TICKS(kHeartbeatPeriodMs));
  }
}

/* 创建心跳任务，并用 SMP 亲和性掩码绑定到指定核心。 */
static void start_heartbeat_task(const HeartbeatTaskConfig* config) {
  TaskHandle_t handle = NULL;
  const BaseType_t created =
      xTaskCreate(heartbeat_task, config->task_name, kHeartbeatStackWords,
                  (void*)config, kHeartbeatPriority, &handle);
  configASSERT(created == pdPASS);

  /* 位掩码语义：bit N 置位表示允许在 Core N 上运行。 */
  vTaskCoreAffinitySet(handle, (UBaseType_t)(1u << config->core_id));
}

/*
 * FreeRTOS 应用侧钩子。
 *
 * FreeRTOSConfig.h 固定了 configSUPPORT_STATIC_ALLOCATION=1、
 * configUSE_MALLOC_FAILED_HOOK=1、configCHECK_FOR_STACK_OVERFLOW=2，
 * 因此下列钩子必须由应用提供，否则链接阶段会缺符号。
 */

void firmware_assert_failed(const char* file, int line) {
  taskDISABLE_INTERRUPTS();
  printf("断言失败: %s:%d\n", file, line);
  for (;;) {
    tight_loop_contents();
  }
}

void vApplicationMallocFailedHook(void) {
  taskDISABLE_INTERRUPTS();
  printf("FreeRTOS 堆耗尽: 请检查 configTOTAL_HEAP_SIZE\n");
  for (;;) {
    tight_loop_contents();
  }
}

void vApplicationStackOverflowHook(TaskHandle_t task, char* task_name) {
  (void)task;
  taskDISABLE_INTERRUPTS();
  printf("任务栈溢出: %s\n", task_name);
  for (;;) {
    tight_loop_contents();
  }
}

#if (configSUPPORT_STATIC_ALLOCATION == 1)
/* SMP 下 Core 0 用主动空闲任务、Core 1 用被动空闲任务，两者不能共用栈。 */
static StaticTask_t s_idle_task_tcb;
static StackType_t s_idle_task_stack[configMINIMAL_STACK_SIZE];

void vApplicationGetIdleTaskMemory(
    StaticTask_t** ppx_idle_task_tcb_buffer,
    StackType_t** ppx_idle_task_stack_buffer,
    configSTACK_DEPTH_TYPE* pux_idle_task_stack_size) {
  *ppx_idle_task_tcb_buffer = &s_idle_task_tcb;
  *ppx_idle_task_stack_buffer = s_idle_task_stack;
  *pux_idle_task_stack_size = configMINIMAL_STACK_SIZE;
}

#if (configNUMBER_OF_CORES > 1)
static StaticTask_t s_passive_idle_task_tcb;
static StackType_t s_passive_idle_task_stack[configMINIMAL_STACK_SIZE];

void vApplicationGetPassiveIdleTaskMemory(
    StaticTask_t** ppx_idle_task_tcb_buffer,
    StackType_t** ppx_idle_task_stack_buffer,
    configSTACK_DEPTH_TYPE* pux_idle_task_stack_size,
    BaseType_t passive_idle_task_index) {
  (void)passive_idle_task_index;
  *ppx_idle_task_tcb_buffer = &s_passive_idle_task_tcb;
  *ppx_idle_task_stack_buffer = s_passive_idle_task_stack;
  *pux_idle_task_stack_size = configMINIMAL_STACK_SIZE;
}
#endif /* configNUMBER_OF_CORES > 1 */

#if (configUSE_TIMERS == 1)
/* 定时器守护任务同样走静态分配，需要应用提供它的 TCB 与栈。 */
static StaticTask_t s_timer_task_tcb;
static StackType_t s_timer_task_stack[configTIMER_TASK_STACK_DEPTH];

void vApplicationGetTimerTaskMemory(
    StaticTask_t** ppx_timer_task_tcb_buffer,
    StackType_t** ppx_timer_task_stack_buffer,
    configSTACK_DEPTH_TYPE* pux_timer_task_stack_size) {
  *ppx_timer_task_tcb_buffer = &s_timer_task_tcb;
  *ppx_timer_task_stack_buffer = s_timer_task_stack;
  *pux_timer_task_stack_size = configTIMER_TASK_STACK_DEPTH;
}
#endif /* configUSE_TIMERS == 1 */
#endif /* configSUPPORT_STATIC_ALLOCATION */

int main(void) {
  stdio_init_all();

  /* 必须最先执行：它读的是看门狗 scratch 寄存器里的阶段标记，任何后续
   * printf 都不会影响这份信息，但先打印能让日志顺序符合阅读直觉。 */
  supervisor_log_reset_reason();

  system_state_init();

  /* 字模、农历表与根证书都以 const 数据的形式链接在 Flash 里，这里只登记
   * 指针；失败说明构建产物缺少资源段，属于不可恢复的构建错误。 */
  if (!asset_store_init()) {
    panic("只读资源初始化失败");
  }

  /* 看门狗从这里开始倒计时。放在创建任务之前：连创建阶段卡住都会复位。 */
  if (!supervisor_start()) {
    panic("监督任务创建失败");
  }

  start_heartbeat_task(&kCore0Heartbeat);
  start_heartbeat_task(&kCore1Heartbeat);

  /* 显示与网络都只创建任务，初始化在各自任务内完成；显示侧不返回状态，
   * 任务内部的失败会自行打印并置为无输出，不影响 Core 0 的网络业务。 */
  ui_render_start_task();
  if (!network_start()) {
    printf("网络任务创建失败，设备将离线运行。\n");
  }

  /* RP2040 SMP 端口会在启动调度器时自行拉起 Core 1。 */
  vTaskStartScheduler();

  /* 正常情况调度器不返回；走到这里说明动态堆不足以创建空闲任务。 */
  panic("FreeRTOS 调度器启动失败");
  return 0;
}
