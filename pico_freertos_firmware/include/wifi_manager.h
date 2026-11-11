#ifndef PICO_FREERTOS_FIRMWARE_INCLUDE_WIFI_MANAGER_H_
#define PICO_FREERTOS_FIRMWARE_INCLUDE_WIFI_MANAGER_H_

#include <stdbool.h>
#include <stdint.h>

/* 第 attempt 次连接失败后的等待时间，单位毫秒。
 * attempt 从 0 开始：1000、3000、10000、30000，之后固定 30000。
 * 纯函数，不依赖硬件，便于宿主测试。 */
uint32_t wifi_manager_backoff_delay_ms(int attempt);

/* 启动 Wi-Fi 管理任务（Core 0）。可重复调用，只有第一次会创建任务。
 * 返回 true 表示任务已创建或此前已经创建；false 表示创建失败。 */
bool wifi_manager_start(void);

/* 最近一次连接状态。未配置 SSID、初始化失败或掉线时为 false。 */
bool wifi_manager_is_connected(void);

/* lwIP/CYW43 是否已经初始化完成。UDP、NTP、天气任务必须等到它为 true
 * 才能调用 lwIP API；未配置 SSID 时会一直为 false。 */
bool wifi_manager_is_network_ready(void);

#endif /* PICO_FREERTOS_FIRMWARE_INCLUDE_WIFI_MANAGER_H_ */
