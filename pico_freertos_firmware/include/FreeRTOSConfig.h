#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H

/* ------------------------------------------------------------------ 内核基础 */
#define configUSE_PREEMPTION                     1
#define configUSE_TIME_SLICING                   1
#define configUSE_PORT_OPTIMISED_TASK_SELECTION  0
#define configUSE_TICKLESS_IDLE                  0
#define configUSE_IDLE_HOOK                      0
#define configUSE_TICK_HOOK                      0
#define configCPU_CLOCK_HZ                       125000000
#define configTICK_RATE_HZ                       1000
#define configMAX_PRIORITIES                     8
#define configMINIMAL_STACK_SIZE                 256
#define configMAX_TASK_NAME_LEN                  16
#define configUSE_16_BIT_TICKS                   0
#define configIDLE_SHOULD_YIELD                  1
#define configUSE_TASK_NOTIFICATIONS             1
#define configUSE_MUTEXES                        1
#define configUSE_RECURSIVE_MUTEXES              1
#define configUSE_COUNTING_SEMAPHORES            1
#define configQUEUE_REGISTRY_SIZE                8
/* 打开后 uxTaskGetSystemState 可用，监督任务得以每 10 秒打印一次整机任务表
 * （状态、优先级、栈最低水位）。设备没有调试器可用，这张表是判断「某个
 * 任务栈给少了」的唯一依据。 */
#define configUSE_TRACE_FACILITY                 1
#define configUSE_QUEUE_SETS                     1
#define configUSE_MALLOC_FAILED_HOOK             1
#define configCHECK_FOR_STACK_OVERFLOW           2

/* --------------------------------------------------------------- SMP 双核配置 */
#define configNUMBER_OF_CORES                    2
#define configTICK_CORE                          0
#define configRUN_MULTIPLE_PRIORITIES            1
#define configUSE_CORE_AFFINITY                  1
/* SMP 端口要求显式定义；被动空闲钩子仅随 configUSE_PASSIVE_IDLE_HOOK 生效。 */
#define configUSE_PASSIVE_IDLE_HOOK              0

/* ------------------------------------------------------------------ 内存分配 */
#define configSUPPORT_STATIC_ALLOCATION          1
#define configSUPPORT_DYNAMIC_ALLOCATION         1
/* 96KB 而不是规格最初的 80KB：实测各任务栈合计约 46KB，TLS 握手还要在堆上
 * 额外申请 4~8KB，80KB 的余量只剩个位数 KB。RP2040 的 264KB SRAM 里还有
 * 约 101KB 从未被用过，把堆提到 96KB 只是占用这部分闲置内存。 */
#define configTOTAL_HEAP_SIZE                    (96 * 1024)

/* -------------------------------------------------------------- 可选 API 开关 */
#define INCLUDE_vTaskPrioritySet                 1
#define INCLUDE_uxTaskPriorityGet                1
#define INCLUDE_vTaskDelete                      1
#define INCLUDE_vTaskSuspend                     1
#define INCLUDE_vTaskDelayUntil                  1
#define INCLUDE_vTaskDelay                       1
#define INCLUDE_xSemaphoreGetMutexHolder         1
#define INCLUDE_xTaskGetSchedulerState           1
#define INCLUDE_xTaskGetCurrentTaskHandle        1
#define INCLUDE_uxTaskGetStackHighWaterMark      1
#define INCLUDE_uxTaskGetSystemState             1
#define INCLUDE_pcTaskGetTaskName                1
#define INCLUDE_xTimerGetTimerDaemonTaskHandle   1
#define INCLUDE_xTimerPendFunctionCall           1

/* --------------------------------------------------------------- 内核定时器 */
/* RP2040 SMP 端口在 configSUPPORT_PICO_SYNC_INTEROP 打开时，会用
 * xEventGroupSetBitsFromISR 在中断里唤醒等待自旋锁的另一核；该 API 由定时器
 * 守护任务实现，故必须启用 configUSE_TIMERS，否则 port.c 出现隐式声明并在
 * 链接期缺符号。 */
#define configUSE_TIMERS                         1
#define configTIMER_TASK_PRIORITY                (configMAX_PRIORITIES - 1)
#define configTIMER_QUEUE_LENGTH                 16
#define configTIMER_TASK_STACK_DEPTH             256

/* --------------------------------------------------------------------- 断言 */
/* 主机编译单元不参与固件链接；固件侧在 main.c 中提供 configASSERT 实现。 */
extern void firmware_assert_failed(const char *file, int line);
#define configASSERT(x) \
    if ((x) == 0) { firmware_assert_failed(__FILE__, __LINE__); }

#endif /* FREERTOS_CONFIG_H */
