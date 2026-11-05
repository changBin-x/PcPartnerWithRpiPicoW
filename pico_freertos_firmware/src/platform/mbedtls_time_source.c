#include "mbedtls_time_source.h"

#include <stddef.h>

#include "pico/stdlib.h"

#include "mbedtls/platform_time.h"

/* 软件时钟的状态。
 *
 * base_unix 是校准那一刻的 Unix 秒数，base_ms 是同时记录的「开机以来毫秒数」。
 * 读取时用两者之差现算，不需要每秒触发一次任务，也就不会因为调度延迟累积
 * 漂移。基准值只在 NTP 校时时改写，因此用 32 位原子读写即可，不必加锁；
 * 唯一需要注意的是「先写 base_ms 再写 base_unix」的顺序——反过来的话，
 * 另一个核可能读到新的 base_unix 配旧的 base_ms，算出巨大的时间差。 */
static volatile uint32_t g_base_unix = 0;
static volatile uint32_t g_base_ms = 0;
static volatile bool g_is_valid = false;

uint32_t mbedtls_time_source_get_unix(void) {
  uint32_t base_unix = g_base_unix;
  uint32_t base_ms = g_base_ms;
  if (!g_is_valid) {
    return 0u;
  }
  uint32_t elapsed_ms =
      (uint32_t)to_ms_since_boot(get_absolute_time()) - base_ms;
  return base_unix + elapsed_ms / 1000u;
}

bool mbedtls_time_source_is_valid(void) { return g_is_valid; }

void mbedtls_time_source_set_unix(uint32_t unix_seconds) {
  /* 顺序很重要：先记时间基准，最后才置有效的 base_unix 与标志位。 */
  g_base_ms = (uint32_t)to_ms_since_boot(get_absolute_time());
  g_base_unix = unix_seconds;
  g_is_valid = true;
}

/* mbedTLS 通过这个回调取「墙钟时间」。 */
static mbedtls_time_t platform_time_callback(mbedtls_time_t* timer) {
  mbedtls_time_t now = (mbedtls_time_t)mbedtls_time_source_get_unix();
  if (timer != NULL) {
    *timer = now;
  }
  return now;
}

/* MBEDTLS_PLATFORM_MS_TIME_ALT 要求由应用提供毫秒级时间源。mbedTLS 内部只用
 * 它给 PSA 的密钥槽打时间戳，单调递增即可，不要求是真实墙钟。 */
mbedtls_ms_time_t mbedtls_ms_time(void) {
  return (mbedtls_ms_time_t)to_ms_since_boot(get_absolute_time());
}

void mbedtls_time_source_init(void) {
  mbedtls_platform_set_time(platform_time_callback);
}
