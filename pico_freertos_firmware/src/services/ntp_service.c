/**
 * @file ntp_service.c
 * @brief SNTP 校时、软件 RTC 推进与农历查询。
 */

#include "ntp_service.h"

#include <stdio.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* 纯函数                                                             */
/* ------------------------------------------------------------------ */

/* 东八区显示偏移；RTC 与 mbedTLS 校时仍保持 UTC。 */
static const uint32_t kNtpLocalOffsetSeconds = 8u * 60u * 60u;

uint32_t ntp_service_unix_from_ntp(uint32_t ntp_seconds) {
  /* NTP 时间戳是 32 位秒数，2036-02-07 06:28:16 UTC 从 0 重新计数。
   * 没有 era 提示时，按“小于 2208988800 视为已回绕一次”解释，
   * 即把 0..2208988799 映射到 2036..2106；2024 年后的设备适用。
   * 无符号减法正好完成这个映射，不会产生负数。 */
  return ntp_seconds - kNtpUnixEpochOffsetSeconds;
}

uint32_t ntp_service_local_unix(uint32_t unix_seconds) {
  return unix_seconds + kNtpLocalOffsetSeconds;
}

int ntp_service_weekday(uint32_t unix_seconds) {
  uint32_t days = unix_seconds / 86400u;
  return (int)((days + 4u) % 7u);
}

/* Howard Hinnant 的 civil_from_days 算法：把 1970-01-01 起的天数转成
 * 公历年月日。全部用整数运算，不依赖系统时区。 */
static void civil_from_days(int64_t days, int* year, int* month, int* day) {
  int64_t z = days + 719468;
  int64_t era = (z >= 0 ? z : z - 146096) / 146097;
  uint32_t doe = (uint32_t)(z - era * 146097);
  uint32_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  int64_t y = (int64_t)yoe + era * 400;
  uint32_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  uint32_t mp = (5 * doy + 2) / 153;
  uint32_t d = doy - (153 * mp + 2) / 5 + 1;
  uint32_t m = mp < 10 ? mp + 3 : mp - 9;

  *year = (int)(y + (m <= 2));
  *month = (int)m;
  *day = (int)d;
}

bool ntp_service_civil_from_unix(uint32_t unix_seconds, ClockData* out_clock) {
  if (out_clock == NULL) {
    return false;
  }

  uint32_t days = unix_seconds / 86400u;
  uint32_t seconds = unix_seconds % 86400u;
  int year = 0;
  int month = 0;
  int day = 0;
  civil_from_days((int64_t)days, &year, &month, &day);

  out_clock->year = year;
  out_clock->month = month;
  out_clock->day = day;
  out_clock->hour = (int)(seconds / 3600u);
  out_clock->minute = (int)((seconds % 3600u) / 60u);
  out_clock->second = (int)(seconds % 60u);
  out_clock->weekday = ntp_service_weekday(unix_seconds);
  out_clock->time_synced = true;
  return true;
}

bool ntp_service_parse_packet(const uint8_t* packet, size_t length,
                              uint32_t* out_ntp_seconds) {
  if (packet == NULL || out_ntp_seconds == NULL || length < 48) {
    return false;
  }

  uint8_t leap_indicator = (uint8_t)(packet[0] >> 6);
  uint8_t mode = (uint8_t)(packet[0] & 0x07u);
  uint8_t stratum = packet[1];
  if (leap_indicator == 3 || mode != 4 || stratum == 0 || stratum > 15) {
    return false;
  }

  uint32_t seconds = ((uint32_t)packet[40] << 24) |
                     ((uint32_t)packet[41] << 16) |
                     ((uint32_t)packet[42] << 8) | (uint32_t)packet[43];
  /* 传输时间戳为 0 表示服务器未给出有效时间，不能当成 2036 年回绕。 */
  if (seconds == 0) {
    return false;
  }
  *out_ntp_seconds = seconds;
  return true;
}

/* ------------------------------------------------------------------ */
/* 设备端                                                             */
/* ------------------------------------------------------------------ */

#ifdef PICO_ON_DEVICE

#include "FreeRTOS.h"
#include "asset_store.h"
#include "lunar_service.h"
#include "lwip/dns.h"
#include "lwip/ip_addr.h"
#include "lwip/pbuf.h"
#include "lwip/tcpip.h"
#include "lwip/udp.h"
#include "mbedtls_time_source.h"
#include "semphr.h"
#include "system_state.h"
#include "task.h"
#include "wifi_manager.h"

#define kNtpHost "ntp.aliyun.com"
#define kNtpPort 123u
#define kNtpSyncIntervalMs (6u * 60u * 60u * 1000u)
#define kNtpTimeoutMs 5000u
#define kNtpTaskStackWords 1024u

typedef struct {
  SemaphoreHandle_t sem;
  uint8_t response[48];
  size_t length;
  bool received;
} NtpUdpContext;

static SemaphoreHandle_t s_dns_sem = NULL;
static SemaphoreHandle_t s_udp_sem = NULL;
static volatile bool s_dns_done = false;
static ip_addr_t s_dns_address;
static NtpUdpContext s_udp_context;
static bool s_started = false;
static uint32_t s_last_unix = 0;

static void ntp_dns_found(const char* name, const ip_addr_t* address,
                          void* argument) {
  (void)name;
  (void)argument;
  if (address != NULL) {
    s_dns_address = *address;
    s_dns_done = true;
  }
  xSemaphoreGive(s_dns_sem);
}

static bool ntp_resolve(void) {
  s_dns_done = false;
  xSemaphoreTake(s_dns_sem, 0);

  err_t error = ERR_OK;
  LOCK_TCPIP_CORE();
  error = dns_gethostbyname(kNtpHost, &s_dns_address, ntp_dns_found, NULL);
  UNLOCK_TCPIP_CORE();

  if (error == ERR_OK) {
    return true;
  }
  if (error != ERR_INPROGRESS) {
    printf("NTP 域名解析失败，错误码 %d。\n", (int)error);
    return false;
  }
  if (xSemaphoreTake(s_dns_sem, pdMS_TO_TICKS(kNtpTimeoutMs)) != pdTRUE) {
    printf("NTP 域名解析超时。\n");
    return false;
  }
  if (!s_dns_done) {
    printf("NTP 域名解析失败。\n");
    return false;
  }
  return true;
}

static void ntp_udp_recv(void* argument, struct udp_pcb* pcb,
                         struct pbuf* packet, const ip_addr_t* address,
                         u16_t port) {
  (void)pcb;
  (void)address;
  (void)port;
  NtpUdpContext* context = (NtpUdpContext*)argument;
  if (packet != NULL) {
    if (packet->tot_len >= sizeof(context->response) &&
        pbuf_copy_partial(packet, context->response, sizeof(context->response),
                          0) == sizeof(context->response)) {
      context->length = sizeof(context->response);
      context->received = true;
    }
    pbuf_free(packet);
  }
  xSemaphoreGive(context->sem);
}

static bool ntp_sync_once(uint32_t* out_unix_seconds) {
  if (!ntp_resolve()) {
    return false;
  }

  /* 先复位接收上下文，再安装回调；否则响应可能在复位之前到达，
   * 被下面的 semaphore 清空逻辑误丢掉。 */
  s_udp_context.received = false;
  s_udp_context.length = 0;
  xSemaphoreTake(s_udp_context.sem, 0);

  struct udp_pcb* pcb = NULL;
  LOCK_TCPIP_CORE();
  pcb = udp_new();
  if (pcb != NULL && udp_bind(pcb, IP_ADDR_ANY, 0) != ERR_OK) {
    udp_remove(pcb);
    pcb = NULL;
  }
  if (pcb != NULL) {
    udp_recv(pcb, ntp_udp_recv, &s_udp_context);
  }
  UNLOCK_TCPIP_CORE();

  if (pcb == NULL) {
    printf("NTP 创建 UDP 套接字失败。\n");
    return false;
  }

  uint8_t request[48];
  memset(request, 0, sizeof(request));
  request[0] = 0x23; /* LI = 0，版本 4，模式 3（客户端）。 */

  struct pbuf* packet = NULL;
  LOCK_TCPIP_CORE();
  packet = pbuf_alloc(PBUF_TRANSPORT, sizeof(request), PBUF_RAM);
  if (packet != NULL) {
    pbuf_take(packet, request, sizeof(request));
  }
  err_t error = ERR_MEM;
  if (packet != NULL) {
    error = udp_sendto(pcb, packet, &s_dns_address, kNtpPort);
    pbuf_free(packet);
  }
  UNLOCK_TCPIP_CORE();

  if (error != ERR_OK) {
    printf("NTP 发送请求失败，错误码 %d。\n", (int)error);
    LOCK_TCPIP_CORE();
    udp_remove(pcb);
    UNLOCK_TCPIP_CORE();
    return false;
  }

  BaseType_t received =
      xSemaphoreTake(s_udp_context.sem, pdMS_TO_TICKS(kNtpTimeoutMs));
  LOCK_TCPIP_CORE();
  udp_remove(pcb);
  UNLOCK_TCPIP_CORE();

  if (received != pdTRUE || !s_udp_context.received) {
    printf("NTP 等待响应超时。\n");
    return false;
  }

  uint32_t ntp_seconds = 0;
  if (!ntp_service_parse_packet(s_udp_context.response, s_udp_context.length,
                                &ntp_seconds)) {
    printf("NTP 响应格式非法。\n");
    return false;
  }

  uint32_t unix_seconds = ntp_service_unix_from_ntp(ntp_seconds);
  if (unix_seconds < 1704067200u) { /* 早于 2024-01-01，明显不可信。 */
    printf("NTP 返回的时间早于 2024 年，已丢弃。\n");
    return false;
  }

  mbedtls_time_source_set_unix(unix_seconds);
  *out_unix_seconds = unix_seconds;
  return true;
}

static bool s_lunar_ready = false;

static void ntp_copy_text(char* destination, size_t capacity,
                          const char* source) {
  if (capacity == 0) {
    return;
  }
  size_t length = 0;
  while (length + 1 < capacity && source[length] != '\0') {
    ++length;
  }
  memcpy(destination, source, length);
  destination[length] = '\0';
}

static void ntp_update_clock(uint32_t unix_seconds) {
  /* ClockData 直接供显示层使用，因此换算成东八区本地时间；
   * 内部 Unix 时间、NTP 请求和 mbedTLS 证书校验仍保持 UTC。 */
  ClockData clock;
  if (!ntp_service_civil_from_unix(ntp_service_local_unix(unix_seconds),
                                   &clock)) {
    return;
  }
  system_state_update_clock(&clock);

  if (!s_lunar_ready) {
    const AssetBlob* table = asset_store_lunar_table();
    if (table != NULL && table->data != NULL && table->size > 0) {
      lunar_service_init_from_memory(table->data, table->size);
      s_lunar_ready = true;
    }
  }

  LunarData lunar;
  memset(&lunar, 0, sizeof(lunar));
  LunarDate date;
  if (s_lunar_ready &&
      lunar_service_lookup(clock.year, clock.month, clock.day, &date)) {
    ntp_copy_text(lunar.gz_year, sizeof(lunar.gz_year), date.ganzhi_year);
    ntp_copy_text(lunar.lunar_md, sizeof(lunar.lunar_md), date.lunar_month_day);
    lunar.valid = true;
  }
  system_state_update_lunar(&lunar);
}

static void ntp_task(void* argument) {
  (void)argument;
  mbedtls_time_source_init();

  static const uint32_t kRetryDelaysMs[] = {30000u, 60000u, 300000u, 900000u};
  TickType_t next_sync = 0;
  uint32_t retry_index = 0;

  for (;;) {
    if (mbedtls_time_source_is_valid()) {
      uint32_t unix_seconds = mbedtls_time_source_get_unix();
      if (unix_seconds != s_last_unix) {
        ntp_update_clock(unix_seconds);
        s_last_unix = unix_seconds;
      }
    }

    if (!wifi_manager_is_network_ready()) {
      vTaskDelay(pdMS_TO_TICKS(1000));
      continue;
    }

    TickType_t now = xTaskGetTickCount();
    if ((int32_t)(now - next_sync) < 0) {
      vTaskDelay(pdMS_TO_TICKS(1000));
      continue;
    }

    uint32_t synced_unix = 0;
    if (ntp_sync_once(&synced_unix)) {
      ClockData clock;
      if (ntp_service_civil_from_unix(ntp_service_local_unix(synced_unix),
                                      &clock)) {
        printf("NTP 同步成功：%04d-%02d-%02d %02d:%02d:%02d（东八区）。\n",
               clock.year, clock.month, clock.day, clock.hour, clock.minute,
               clock.second);
      }
      ntp_update_clock(synced_unix);
      s_last_unix = synced_unix;
      next_sync = now + pdMS_TO_TICKS(kNtpSyncIntervalMs);
      retry_index = 0;
    } else {
      if (retry_index >= sizeof(kRetryDelaysMs) / sizeof(kRetryDelaysMs[0])) {
        retry_index = sizeof(kRetryDelaysMs) / sizeof(kRetryDelaysMs[0]) - 1;
      }
      next_sync = now + pdMS_TO_TICKS(kRetryDelaysMs[retry_index]);
      if (retry_index + 1 <
          sizeof(kRetryDelaysMs) / sizeof(kRetryDelaysMs[0])) {
        ++retry_index;
      }
    }
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}

bool ntp_service_start(void) {
  if (s_started) {
    return true;
  }

  s_dns_sem = xSemaphoreCreateBinary();
  s_udp_sem = xSemaphoreCreateBinary();
  s_udp_context.sem = s_udp_sem;
  if (s_dns_sem == NULL || s_udp_sem == NULL) {
    if (s_dns_sem != NULL) {
      vSemaphoreDelete(s_dns_sem);
    }
    if (s_udp_sem != NULL) {
      vSemaphoreDelete(s_udp_sem);
    }
    s_dns_sem = NULL;
    s_udp_sem = NULL;
    s_udp_context.sem = NULL;
    printf("NTP 信号量创建失败。\n");
    return false;
  }

  TaskHandle_t task = NULL;
  BaseType_t created =
      xTaskCreate(ntp_task, "ntp_service", kNtpTaskStackWords, NULL, 2, &task);
  if (created != pdPASS || task == NULL) {
    vSemaphoreDelete(s_dns_sem);
    vSemaphoreDelete(s_udp_sem);
    s_dns_sem = NULL;
    s_udp_sem = NULL;
    s_udp_context.sem = NULL;
    printf("NTP 任务创建失败。\n");
    return false;
  }
#if (configNUMBER_OF_CORES > 1)
  vTaskCoreAffinitySet(task, 1u << 0);
#endif
  s_started = true;
  return true;
}

#else /* PICO_ON_DEVICE */

bool ntp_service_start(void) { return false; }

#endif /* PICO_ON_DEVICE */
