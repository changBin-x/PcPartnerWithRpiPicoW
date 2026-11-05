/**
 * @file weather_service.h
 * @brief 高德实况天气：HTTPS 拉取、JSON 解析与状态刷新。
 */

#ifndef PICO_FREERTOS_FIRMWARE_INCLUDE_WEATHER_SERVICE_H_
#define PICO_FREERTOS_FIRMWARE_INCLUDE_WEATHER_SERVICE_H_

#include <stdbool.h>
#include <stddef.h>

#include "system_state.h"

/* 默认城市 adcode。pico/weather.py 本身不硬编码城市，仓库文档里的示例
 * 是宁波（330200），因此这里以宁波作为无覆盖配置时的默认值。 */
#define kWeatherDefaultAdcode "330200"

/* 启动天气任务。未配置高德 Key 时会打印提示并让任务自行退出。 */
bool weather_service_start(void);

/* 解析高德实况天气 JSON（纯函数，便于宿主测试）。
 *
 * 成功时把 city / weather / temperature 写入 out，并置 valid=true、
 * is_stale=false；解析失败返回 false 且不修改 out。 */
bool weather_service_parse_json(const char* json, WeatherData* out);

/* 从完整 HTTP 响应中取出响应体，处理 Content-Length 与 chunked 两种
 * 编码。返回响应体起始指针并写入长度；失败返回 NULL。
 * 该函数会就地解码 chunked（响应体指针总在头部之后，前向解码安全）。 */
const char* weather_service_http_body(char* response, size_t length,
                                      size_t* out_length);

#endif /* PICO_FREERTOS_FIRMWARE_INCLUDE_WEATHER_SERVICE_H_ */
