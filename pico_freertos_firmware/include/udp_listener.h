#ifndef PICO_FREERTOS_FIRMWARE_INCLUDE_UDP_LISTENER_H_
#define PICO_FREERTOS_FIRMWARE_INCLUDE_UDP_LISTENER_H_

#include <stdbool.h>

/* 启动 UDP 指标监听任务（Core 0，端口 8765）。
 * 可重复调用，只有第一次会创建任务。
 * 返回 true 表示任务已创建或此前已经创建；false 表示创建失败。 */
bool udp_listener_start(void);

#endif /* PICO_FREERTOS_FIRMWARE_INCLUDE_UDP_LISTENER_H_ */
