/**
 * @file network_start.h
 * @brief 网络子系统统一入口。
 */

#ifndef PICO_FREERTOS_FIRMWARE_INCLUDE_NETWORK_START_H_
#define PICO_FREERTOS_FIRMWARE_INCLUDE_NETWORK_START_H_

#include <stdbool.h>

/* 启动 Wi-Fi、UDP 指标监听、NTP 与天气任务。
 *
 * 幂等：重复调用返回第一次的结果，不会重复建任务。未配置 Wi-Fi 时
 * Wi-Fi 任务会自行退出，但其余任务仍然创建并保持等待，固件继续运行。 */
bool network_start(void);

#endif /* PICO_FREERTOS_FIRMWARE_INCLUDE_NETWORK_START_H_ */
