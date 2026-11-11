#ifndef PICO_FREERTOS_FIRMWARE_INCLUDE_MBEDTLS_TIME_SOURCE_H_
#define PICO_FREERTOS_FIRMWARE_INCLUDE_MBEDTLS_TIME_SOURCE_H_

#include <stdbool.h>
#include <stdint.h>

/* 为 mbedTLS 提供时间源。
 *
 * RP2040 没有电池供电的 RTC，上电后「当前时间」并不存在，而 mbedTLS 校验证书
 * 有效期必须知道真实时间。这里的做法是：
 *
 *   1. 维护一份「软件时钟」——由 NTP 校时写入，之后靠 tick 自动走秒；
 *   2. 通过 mbedtls_platform_set_time() 把这份时钟注入 mbedTLS。
 *
 * 在 NTP 首次成功之前软件时钟为 0，此时所有证书都会被判为「尚未生效」。
 * 因此天气模块必须先等 mbedtls_time_source_is_valid() 为真再发起 HTTPS 请求，
 * 否则只会拿到一串证书错误。 */

/* 安装 mbedTLS 时间钩子。必须在任何 TLS 操作之前调用一次。
 * 重复调用是幂等的。 */
void mbedtls_time_source_init(void);

/* 用 NTP 得到的 Unix 秒数校准软件时钟，并以当前 tick 作为计时起点。 */
void mbedtls_time_source_set_unix(uint32_t unix_seconds);

/* 当前 Unix 秒数；从未校准过时返回 0。 */
uint32_t mbedtls_time_source_get_unix(void);

/* 是否已经校准过（即 get_unix 的返回值可信）。 */
bool mbedtls_time_source_is_valid(void);

#endif /* PICO_FREERTOS_FIRMWARE_INCLUDE_MBEDTLS_TIME_SOURCE_H_ */
