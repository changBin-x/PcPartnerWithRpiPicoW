/**
 * @file main.c
 * @brief 固件入口：初始化 stdio 与系统状态，创建双核心跳任务后启动
 *        FreeRTOS SMP 调度器。
 *
 * 当前只提供可验证骨架：Core 0 / Core 1 各一个心跳任务，周期打印绑定核号
 * 与递增计数。Wi-Fi、显示与看门狗业务由后续任务在 src/ 下接入。
 */

#include <stdint.h>
#include <stdio.h>

#include "FreeRTOS.h"
#include "task.h"

#include "pico/stdlib.h"

#include "system_state.h"

/* 心跳任务参数。 */
enum {
  kHeartbeatPeriodMs = 1000,
  kHeartbeatPriority = 2,
  kHeartbeatStackWords = 512,
};

/* 心跳任务配置：core_id 同时用于亲和性绑定与日志标签。 */
typedef struct {
  uint32_t core_id;
  const char* task_name;
} HeartbeatTaskConfig;

static const HeartbeatTaskConfig kCore0Heartbeat = {
    .core_id = 0,
    .task_name = "heartbeat_core0",
};

static const HeartbeatTaskConfig kCore1Heartbeat = {
    .core_id = 1,
    .task_name = "heartbeat_core1",
};

/* 心跳任务主体：打印绑定核号与 FreeRTOS 实际调度所在核号。 */
static void heartbeat_task(void* context) {
  const HeartbeatTaskConfig* config = (const HeartbeatTaskConfig*)context;
  uint32_t counter = 0;

  for (;;) {
    printf("[core%u] heartbeat=%u running_on_core=%u\n",
           (unsigned)config->core_id, (unsigned)counter,
           (unsigned)portGET_CORE_ID());
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
  system_state_init();

  start_heartbeat_task(&kCore0Heartbeat);
  start_heartbeat_task(&kCore1Heartbeat);

  /* RP2040 SMP 端口会在启动调度器时自行拉起 Core 1。 */
  vTaskStartScheduler();

  /* 正常情况调度器不返回；走到这里说明动态堆不足以创建空闲任务。 */
  panic("FreeRTOS 调度器启动失败");
  return 0;
}
