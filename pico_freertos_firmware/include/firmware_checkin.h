#ifndef PICO_FREERTOS_FIRMWARE_INCLUDE_FIRMWARE_CHECKIN_H_
#define PICO_FREERTOS_FIRMWARE_INCLUDE_FIRMWARE_CHECKIN_H_

/* 各任务向存活监视器「签到」的统一入口。
 *
 * 每个长期运行的任务在自己的循环里调用一次即可，成本是读一次 tick、写一个
 * 32 位变量。之所以做成宏而不是函数，是为了让宿主单测编译这些源文件时
 * 完全不需要 FreeRTOS —— 非设备环境下它展开成一条空语句。 */

#ifdef PICO_ON_DEVICE

#include "FreeRTOS.h"
#include "supervisor_monitor.h"
#include "task.h"

#define FIRMWARE_CHECK_IN(slot)       \
  supervisor_monitor_check_in((slot), \
                              (uint32_t)pdTICKS_TO_MS(xTaskGetTickCount()))

#else /* PICO_ON_DEVICE */

#define FIRMWARE_CHECK_IN(slot) ((void)0)

#endif /* PICO_ON_DEVICE */

#endif /* PICO_FREERTOS_FIRMWARE_INCLUDE_FIRMWARE_CHECKIN_H_ */
