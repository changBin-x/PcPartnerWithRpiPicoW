#ifndef PICO_FREERTOS_FIRMWARE_INCLUDE_NTP_SERVICE_H_
#define PICO_FREERTOS_FIRMWARE_INCLUDE_NTP_SERVICE_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "system_state.h"

/* NTP 纪元（1900-01-01）到 Unix 纪元（1970-01-01）的秒数。 */
#define kNtpUnixEpochOffsetSeconds 2208988800u

/* 把 32 位 NTP 秒数换算成 Unix 秒数。
 *
 * NTP 的 32 位秒数在 2036-02-07 06:28:16 回绕。这里用 uint32 无符号减法，
 * 回绕会自然衔接：NTP 0 对应 Unix 2085978496（2036-02-07），而不是 1900。
 * 因此 NTP 值 >= 2208988800 被解释为 1970..2036 区间，< 2208988800 被
 * 解释为 2036..2106 区间（NTP era 1），适合本固件的可信时间窗口。 */
uint32_t ntp_service_unix_from_ntp(uint32_t ntp_seconds);

/* 把 UTC Unix 秒数换算成年月日时分秒。weekday 0 = 周日。
 * 不依赖 gmtime，避免把 newlib 的时区/格式化代码拖进 ROM。 */
bool ntp_service_civil_from_unix(uint32_t unix_seconds, ClockData* out_clock);

/* 把 UTC Unix 秒数换算成东八区显示时间（秒）。
 * 软件 RTC 与 TLS 时间校验始终使用 UTC，只有 ClockData 使用本地时间；
 * 参考 pico/time_sync.py 的 UTC_OFFSET_SECONDS。 */
uint32_t ntp_service_local_unix(uint32_t unix_seconds);

/* 0 = 周日，1 = 周一，……，6 = 周六。 */
int ntp_service_weekday(uint32_t unix_seconds);

/* 校验并解析 48 字节 NTP 响应，取出「传输时间戳」的秒数。
 * 纯函数，便于宿主测试。 */
bool ntp_service_parse_packet(const uint8_t* packet, size_t length,
                              uint32_t* out_ntp_seconds);

/* 启动 NTP 任务（Core 0）：每 6 小时同步一次，每秒推进系统时钟与农历。 */
bool ntp_service_start(void);

#endif /* PICO_FREERTOS_FIRMWARE_INCLUDE_NTP_SERVICE_H_ */
