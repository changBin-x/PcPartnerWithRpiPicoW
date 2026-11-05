/**
 * @file weather_service.c
 * @brief 高德实况天气：lwIP raw TCP + mbedTLS 自定义 BIO。
 *
 * 传输层说明：工程的 lwipopts.h 关闭了 socket/netconn，因此这里用 raw TCP
 * 建立一个连接，再由 mbedTLS 的 BIO 回调读写。TCP 回调运行在 tcpip 线程，
 * 只做「拷进环形缓冲 + tcp_recved + 释放信号量」，绝不在这里跑 TLS；
 * 握手、HTTP 与 JSON 解析都在天气任务里完成。
 */

#include "weather_service.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"

/* 高德响应体上限与 pico/weather.py 保持一致，避免异常服务器把
 * 解析器拖进大内存分配。 */
#define kWeatherMaxBodyBytes 4096u

/* ------------------------------------------------------------------ */
/* 纯函数：HTTP 响应体提取与 JSON 解析                               */
/* ------------------------------------------------------------------ */

static bool weather_region_contains_ci(const char* region, size_t length,
                                       const char* needle) {
  size_t needle_length = strlen(needle);
  if (needle_length == 0 || needle_length > length) {
    return false;
  }
  for (size_t i = 0; i + needle_length <= length; ++i) {
    size_t j = 0;
    while (j < needle_length && tolower((unsigned char)region[i + j]) ==
                                    tolower((unsigned char)needle[j])) {
      ++j;
    }
    if (j == needle_length) {
      return true;
    }
  }
  return false;
}

static int weather_hex_value(char c) {
  if (c >= '0' && c <= '9') {
    return c - '0';
  }
  if (c >= 'a' && c <= 'f') {
    return c - 'a' + 10;
  }
  if (c >= 'A' && c <= 'F') {
    return c - 'A' + 10;
  }
  return -1;
}

/* 解析 Content-Length 的值；找不到时返回 false。 */
static bool weather_content_length(const char* response, size_t header_end,
                                   size_t* out_length) {
  const char kName[] = "content-length:";
  size_t name_length = sizeof(kName) - 1;
  for (size_t i = 0; i + name_length <= header_end; ++i) {
    size_t j = 0;
    while (j < name_length && tolower((unsigned char)response[i + j]) ==
                                  tolower((unsigned char)kName[j])) {
      ++j;
    }
    if (j != name_length) {
      continue;
    }
    size_t pos = i + name_length;
    while (pos < header_end &&
           (response[pos] == ' ' || response[pos] == '\t')) {
      ++pos;
    }
    size_t value = 0;
    bool found = false;
    while (pos < header_end && response[pos] >= '0' && response[pos] <= '9') {
      value = value * 10 + (size_t)(response[pos] - '0');
      /* 超过业务上限后停止精确累加，防止恶意超长数字回绕后
       * 被误判成“响应完整”。后续统一按超限拒绝。 */
      if (value > kWeatherMaxBodyBytes) {
        value = kWeatherMaxBodyBytes + 1u;
      }
      ++pos;
      found = true;
    }
    if (found) {
      *out_length = value;
      return true;
    }
    return false;
  }
  return false;
}

const char* weather_service_http_body(char* response, size_t length,
                                      size_t* out_length) {
  if (response == NULL || out_length == NULL || length < 16) {
    return NULL;
  }

  size_t header_end = 0;
  for (size_t i = 0; i + 3 < length; ++i) {
    if (response[i] == '\r' && response[i + 1] == '\n' &&
        response[i + 2] == '\r' && response[i + 3] == '\n') {
      header_end = i + 4;
      break;
    }
  }
  if (header_end == 0 || header_end > 2048u) {
    return NULL;
  }

  if (strncmp(response, "HTTP/1.", 7) != 0) {
    return NULL;
  }
  const char* space = memchr(response, ' ', header_end);
  if (space == NULL || (size_t)(space - response) + 4 > header_end) {
    return NULL;
  }
  if (space[1] != '2' || space[2] != '0' || space[3] != '0') {
    return NULL;
  }

  size_t available = length - header_end;
  bool chunked =
      weather_region_contains_ci(response, header_end, "transfer-encoding:") &&
      weather_region_contains_ci(response, header_end, "chunked");
  char* body = response + header_end;

  if (!chunked) {
    size_t body_length = available;
    size_t declared = 0;
    if (weather_content_length(response, header_end, &declared)) {
      /* 声明长度大于实际可用字节说明响应被截断，不能当成功解析。 */
      if (declared > body_length) {
        return NULL;
      }
      body_length = declared;
    }
    if (body_length > kWeatherMaxBodyBytes) {
      return NULL;
    }
    *out_length = body_length;
    return body;
  }

  /* chunked：就地解码，输出指针始终不落后于输入指针。 */
  size_t read_pos = 0;
  size_t write_pos = 0;
  for (;;) {
    size_t line_end = read_pos;
    while (line_end + 1 < available &&
           !(body[line_end] == '\r' && body[line_end + 1] == '\n')) {
      ++line_end;
    }
    if (line_end + 1 >= available) {
      return NULL;
    }

    size_t chunk_size = 0;
    bool has_digit = false;
    for (size_t i = read_pos; i < line_end; ++i) {
      if (body[i] == ';' || body[i] == ' ' || body[i] == '\t') {
        break;
      }
      int value = weather_hex_value(body[i]);
      if (value < 0) {
        return NULL;
      }
      /* 先按可用长度限幅，避免超长十六进制数把 size_t 乘溢出。 */
      if (chunk_size > available / 16) {
        return NULL;
      }
      chunk_size = chunk_size * 16 + (size_t)value;
      if (chunk_size > available) {
        return NULL;
      }
      has_digit = true;
    }
    if (!has_digit) {
      return NULL;
    }

    read_pos = line_end + 2;
    if (chunk_size == 0) {
      if (write_pos > kWeatherMaxBodyBytes) {
        return NULL;
      }
      *out_length = write_pos;
      return body;
    }
    if (chunk_size > available - read_pos) {
      return NULL;
    }
    for (size_t i = 0; i < chunk_size; ++i) {
      body[write_pos + i] = body[read_pos + i];
    }
    write_pos += chunk_size;
    read_pos += chunk_size;
    if (read_pos + 1 >= available || body[read_pos] != '\r' ||
        body[read_pos + 1] != '\n') {
      return NULL;
    }
    read_pos += 2;
  }
}

/* 去掉首尾空白后复制；高德返回的 city/weather 可能带空格。 */
static void weather_copy_trimmed(char* destination, size_t capacity,
                                 const char* source) {
  while (*source == ' ' || *source == '\t' || *source == '\r' ||
         *source == '\n') {
    ++source;
  }
  const char* end = source;
  while (*end != '\0') {
    ++end;
  }
  while (end > source && (end[-1] == ' ' || end[-1] == '\t' ||
                          end[-1] == '\r' || end[-1] == '\n')) {
    --end;
  }
  size_t length = (size_t)(end - source);
  if (length >= capacity) {
    length = capacity - 1;
  }
  memcpy(destination, source, length);
  destination[length] = '\0';
}

static bool weather_text_is_empty(const char* text) {
  while (*text != '\0') {
    if (*text != ' ' && *text != '\t' && *text != '\r' && *text != '\n') {
      return false;
    }
    ++text;
  }
  return true;
}

/* 温度字段既可能是字符串（高德当前返回 "16"），也可能是数字。 */
static bool weather_temperature(const cJSON* item, double* out) {
  if (item == NULL) {
    return false;
  }
  if (cJSON_IsNumber(item)) {
    *out = item->valuedouble;
    return true;
  }
  if (!cJSON_IsString(item) || item->valuestring == NULL) {
    return false;
  }
  char* end = NULL;
  double value = strtod(item->valuestring, &end);
  if (end == item->valuestring) {
    return false;
  }
  while (*end == ' ' || *end == '\t') {
    ++end;
  }
  if (*end != '\0') {
    return false;
  }
  *out = value;
  return true;
}

static const char* weather_city_name(const cJSON* live) {
  const cJSON* city = cJSON_GetObjectItemCaseSensitive(live, "city");
  if (cJSON_IsString(city) && city->valuestring != NULL &&
      city->valuestring[0] != '\0' && strcmp(city->valuestring, "[]") != 0) {
    return city->valuestring;
  }
  const cJSON* province = cJSON_GetObjectItemCaseSensitive(live, "province");
  if (cJSON_IsString(province) && province->valuestring != NULL) {
    return province->valuestring;
  }
  return "";
}

bool weather_service_parse_json(const char* json, WeatherData* out) {
  if (json == NULL || out == NULL) {
    return false;
  }

  /* 要求整个字符串都是合法 JSON，拒绝尾部垃圾字节。 */
  cJSON* root = cJSON_ParseWithOpts(json, NULL, 1);
  if (root == NULL) {
    return false;
  }

  bool ok = false;
  WeatherData data;
  memset(&data, 0, sizeof(data));

  const cJSON* status = cJSON_GetObjectItemCaseSensitive(root, "status");
  const cJSON* lives = cJSON_GetObjectItemCaseSensitive(root, "lives");
  const cJSON* live = cJSON_GetArrayItem(lives, 0);

  if (cJSON_IsString(status) && status->valuestring != NULL &&
      strcmp(status->valuestring, "1") == 0 && cJSON_IsArray(lives) &&
      live != NULL) {
    const cJSON* weather = cJSON_GetObjectItemCaseSensitive(live, "weather");
    double temperature = 0.0;
    if (cJSON_IsString(weather) && weather->valuestring != NULL &&
        !weather_text_is_empty(weather->valuestring) &&
        weather_temperature(
            cJSON_GetObjectItemCaseSensitive(live, "temperature"),
            &temperature) &&
        temperature >= -80.0 && temperature <= 70.0) {
      weather_copy_trimmed(data.city, sizeof(data.city),
                           weather_city_name(live));
      weather_copy_trimmed(data.weather, sizeof(data.weather),
                           weather->valuestring);
      data.temperature = (int)temperature;
      data.is_stale = false;
      data.valid = true;
      *out = data;
      ok = true;
    }
  }

  cJSON_Delete(root);
  return ok;
}

/* ------------------------------------------------------------------ */
/* 设备端：raw TCP + mbedTLS                                         */
/* ------------------------------------------------------------------ */

#ifdef PICO_ON_DEVICE

#include "FreeRTOS.h"
#include "asset_store.h"
#include "firmware_secrets.h"
#include "lwip/dns.h"
#include "lwip/ip_addr.h"
#include "lwip/pbuf.h"
#include "lwip/tcp.h"
#include "lwip/tcpip.h"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/entropy.h"
#include "mbedtls/error.h"
#include "mbedtls/ssl.h"
#include "mbedtls/x509_crt.h"
#include "mbedtls_time_source.h"
#include "pico/cyw43_arch.h"
#include "semphr.h"
#include "firmware_checkin.h"
#include "system_state.h"
#include "task.h"
#include "wifi_manager.h"

#define kWeatherHost "restapi.amap.com"
#define kWeatherPort 443
#define kWeatherRefreshMs (30u * 60u * 1000u)
#define kWeatherConnectTimeoutMs 10000u
#define kWeatherTlsWaitMs 5000u
#define kWeatherRequestBytes 512u
#define kWeatherResponseBytes 7168u
#define kWeatherRingBytes 8192u
#define kWeatherTaskStackWords 4096u

typedef struct {
  struct tcp_pcb* pcb;
  SemaphoreHandle_t connected_sem;
  SemaphoreHandle_t rx_sem;
  volatile bool connected;
  volatile bool closed;
  volatile bool error;
  uint32_t rx_head;
  uint32_t rx_tail;
  uint8_t rx_ring[kWeatherRingBytes];
} WeatherTlsConn;

static WeatherTlsConn s_conn;
static SemaphoreHandle_t s_dns_sem = NULL;
static volatile bool s_dns_done = false;
static ip_addr_t s_dns_address;

static mbedtls_ssl_context s_ssl;
static mbedtls_ssl_config s_conf;
static mbedtls_x509_crt s_ca;
static mbedtls_entropy_context s_entropy;
static mbedtls_ctr_drbg_context s_drbg;
static bool s_tls_ready = false;
static bool s_started = false;

/* 通过 volatile 指针读取 Key，避免 Key 为空时编译器把整条 HTTPS 调用链
 * 当成不可达代码删掉。 */
static const char* volatile s_weather_key = kFirmwareAmapWebKey;

static char s_request[kWeatherRequestBytes];
static char s_response[kWeatherResponseBytes];
static WeatherData s_last_weather;
static bool s_has_last_weather = false;

static void weather_log_error(const char* stage, int error) {
  char text[128];
  text[0] = '\0';
  if (error < 0) {
    mbedtls_strerror(error, text, sizeof(text));
  }
  if (text[0] == '\0') {
    snprintf(text, sizeof(text), "错误码 %d", error);
  }
  printf("天气%s失败：%s。\n", stage, text);
}

static bool weather_ring_write(WeatherTlsConn* conn, const uint8_t* data,
                               uint16_t length) {
  uint32_t used = conn->rx_head - conn->rx_tail;
  if (used + length > kWeatherRingBytes) {
    conn->error = true;
    return false;
  }
  for (uint16_t i = 0; i < length; ++i) {
    conn->rx_ring[(conn->rx_head + i) & (kWeatherRingBytes - 1)] = data[i];
  }
  conn->rx_head += length;
  return true;
}

static size_t weather_ring_read(WeatherTlsConn* conn, uint8_t* buffer,
                                size_t length) {
  uint32_t available = conn->rx_head - conn->rx_tail;
  if (available == 0) {
    return 0;
  }
  size_t count = available < length ? available : length;
  for (size_t i = 0; i < count; ++i) {
    buffer[i] = conn->rx_ring[(conn->rx_tail + i) & (kWeatherRingBytes - 1)];
  }
  conn->rx_tail += (uint32_t)count;
  return count;
}

static err_t weather_tcp_recv(void* argument, struct tcp_pcb* pcb,
                              struct pbuf* packet, err_t error) {
  WeatherTlsConn* conn = (WeatherTlsConn*)argument;
  if (error != ERR_OK) {
    if (packet != NULL) {
      pbuf_free(packet);
    }
    conn->error = true;
    xSemaphoreGive(conn->rx_sem);
    return ERR_OK;
  }
  if (packet == NULL) {
    conn->closed = true;
    xSemaphoreGive(conn->rx_sem);
    return ERR_OK;
  }

  bool overflow = false;
  for (const struct pbuf* part = packet; part != NULL; part = part->next) {
    if (!weather_ring_write(conn, (const uint8_t*)part->payload, part->len)) {
      /* 环形缓冲已满：标记错误让等待中的 TLS 读取立刻失败，避免
       * 半截 TLS 记录把握手或响应解析卡到超时。 */
      overflow = true;
      break;
    }
  }
  if (overflow) {
    conn->error = true;
  }
  tcp_recved(pcb, packet->tot_len);
  pbuf_free(packet);
  xSemaphoreGive(conn->rx_sem);
  return ERR_OK;
}

static void weather_tcp_error(void* argument, err_t error) {
  (void)error;
  WeatherTlsConn* conn = (WeatherTlsConn*)argument;
  conn->error = true;
  conn->closed = true;
  conn->pcb = NULL;
  xSemaphoreGive(conn->connected_sem);
  xSemaphoreGive(conn->rx_sem);
}

static err_t weather_tcp_connected(void* argument, struct tcp_pcb* pcb,
                                   err_t error) {
  WeatherTlsConn* conn = (WeatherTlsConn*)argument;
  if (error == ERR_OK) {
    conn->pcb = pcb;
    conn->connected = true;
  } else {
    conn->error = true;
  }
  xSemaphoreGive(conn->connected_sem);
  return ERR_OK;
}

static void weather_dns_found(const char* name, const ip_addr_t* address,
                              void* argument) {
  (void)name;
  (void)argument;
  if (address != NULL) {
    s_dns_address = *address;
    s_dns_done = true;
  }
  xSemaphoreGive(s_dns_sem);
}

static bool weather_resolve(void) {
  s_dns_done = false;
  xSemaphoreTake(s_dns_sem, 0);

  err_t error = ERR_OK;
  LOCK_TCPIP_CORE();
  error =
      dns_gethostbyname(kWeatherHost, &s_dns_address, weather_dns_found, NULL);
  UNLOCK_TCPIP_CORE();

  if (error == ERR_OK) {
    return true;
  }
  if (error != ERR_INPROGRESS) {
    weather_log_error("域名解析", (int)error);
    return false;
  }
  if (xSemaphoreTake(s_dns_sem, pdMS_TO_TICKS(kWeatherTlsWaitMs)) != pdTRUE) {
    weather_log_error("域名解析超时", -1);
    return false;
  }
  if (!s_dns_done) {
    weather_log_error("域名解析", -1);
    return false;
  }
  return true;
}

static void weather_tcp_close(void) {
  /* 读取 pcb、清空字段和关闭必须在同一把核心锁内完成，否则 tcpip
   * 线程的 error 回调可能先把 pcb 释放掉，留下悬空指针。 */
  LOCK_TCPIP_CORE();
  struct tcp_pcb* pcb = s_conn.pcb;
  s_conn.pcb = NULL;
  if (pcb != NULL) {
    if (tcp_close(pcb) != ERR_OK) {
      tcp_abort(pcb);
    }
  }
  UNLOCK_TCPIP_CORE();
}

static bool weather_tcp_open(void) {
  if (!weather_resolve()) {
    return false;
  }

  s_conn.connected = false;
  s_conn.closed = false;
  s_conn.error = false;
  s_conn.rx_head = 0;
  s_conn.rx_tail = 0;
  xSemaphoreTake(s_conn.connected_sem, 0);
  xSemaphoreTake(s_conn.rx_sem, 0);

  struct tcp_pcb* pcb = NULL;
  LOCK_TCPIP_CORE();
  pcb = tcp_new_ip_type(IPADDR_TYPE_V4);
  if (pcb != NULL) {
    tcp_arg(pcb, &s_conn);
    tcp_err(pcb, weather_tcp_error);
    tcp_recv(pcb, weather_tcp_recv);
    s_conn.pcb = pcb;
    if (tcp_connect(pcb, &s_dns_address, kWeatherPort, weather_tcp_connected) !=
        ERR_OK) {
      s_conn.pcb = NULL;
      tcp_abort(pcb);
      pcb = NULL;
    }
  }
  UNLOCK_TCPIP_CORE();

  if (pcb == NULL) {
    weather_log_error("创建 TCP 连接", -1);
    return false;
  }

  if (xSemaphoreTake(s_conn.connected_sem,
                     pdMS_TO_TICKS(kWeatherConnectTimeoutMs)) != pdTRUE ||
      !s_conn.connected || s_conn.error) {
    weather_log_error("TCP 连接超时", -1);
    weather_tcp_close();
    return false;
  }
  return true;
}

static int weather_tls_send(void* context, const unsigned char* buffer,
                            size_t length) {
  WeatherTlsConn* conn = (WeatherTlsConn*)context;
  size_t sent = 0;
  TickType_t start = xTaskGetTickCount();

  while (sent < length) {
    u16_t space = 0;
    err_t error = ERR_OK;
    bool wrote = false;

    /* pcb 的读取与使用都在核心锁内，避免 tcpip 线程的 error 回调
     * 在两者之间释放 pcb，造成 use-after-free。 */
    LOCK_TCPIP_CORE();
    struct tcp_pcb* pcb = conn->pcb;
    if (conn->error || pcb == NULL) {
      UNLOCK_TCPIP_CORE();
      return conn->error ? MBEDTLS_ERR_SSL_INTERNAL_ERROR
                         : MBEDTLS_ERR_SSL_CONN_EOF;
    }
    space = tcp_sndbuf(pcb);
    if (space > 0) {
      size_t chunk = length - sent;
      if (chunk > space) {
        chunk = space;
      }
      error = tcp_write(pcb, buffer + sent, (u16_t)chunk, TCP_WRITE_FLAG_COPY);
      if (error == ERR_OK) {
        /* tcp_write 已把数据放进发送队列，无论 tcp_output 现在能否
         * 立刻发出，都不能重写这一段，否则会重复发送。 */
        sent += chunk;
        wrote = true;
        err_t output_error = tcp_output(pcb);
        if (output_error != ERR_OK && output_error != ERR_MEM) {
          error = output_error;
        }
      }
    }
    UNLOCK_TCPIP_CORE();

    if (error == ERR_MEM) {
      if ((xTaskGetTickCount() - start) > pdMS_TO_TICKS(10000)) {
        return MBEDTLS_ERR_SSL_TIMEOUT;
      }
      vTaskDelay(pdMS_TO_TICKS(10));
      continue;
    }
    if (error != ERR_OK) {
      return MBEDTLS_ERR_SSL_INTERNAL_ERROR;
    }
    if (!wrote) {
      if ((xTaskGetTickCount() - start) > pdMS_TO_TICKS(10000)) {
        return MBEDTLS_ERR_SSL_TIMEOUT;
      }
      vTaskDelay(pdMS_TO_TICKS(10));
    }
  }
  return (int)sent;
}

static int weather_tls_recv(void* context, unsigned char* buffer, size_t length,
                            uint32_t timeout) {
  WeatherTlsConn* conn = (WeatherTlsConn*)context;
  TickType_t wait =
      timeout == 0 ? pdMS_TO_TICKS(kWeatherTlsWaitMs) : pdMS_TO_TICKS(timeout);

  for (;;) {
    size_t count = weather_ring_read(conn, buffer, length);
    if (count > 0) {
      return (int)count;
    }
    if (conn->error) {
      return MBEDTLS_ERR_SSL_INTERNAL_ERROR;
    }
    if (conn->closed) {
      return 0;
    }
    if (xSemaphoreTake(conn->rx_sem, wait) != pdTRUE) {
      return MBEDTLS_ERR_SSL_TIMEOUT;
    }
  }
}

static bool weather_tls_init(void) {
  if (s_tls_ready) {
    return true;
  }

  mbedtls_ssl_init(&s_ssl);
  mbedtls_ssl_config_init(&s_conf);
  mbedtls_x509_crt_init(&s_ca);
  mbedtls_entropy_init(&s_entropy);
  mbedtls_ctr_drbg_init(&s_drbg);

  static const char kPersonalization[] = "pico-weather";
  int error = mbedtls_ctr_drbg_seed(&s_drbg, mbedtls_entropy_func, &s_entropy,
                                    (const unsigned char*)kPersonalization,
                                    sizeof(kPersonalization) - 1);
  if (error != 0) {
    weather_log_error("初始化随机数", error);
    return false;
  }

  const AssetBlob* ca = asset_store_amap_root_ca();
  if (ca == NULL || ca->data == NULL || ca->size == 0) {
    weather_log_error("读取高德根证书", -1);
    return false;
  }
  error = mbedtls_x509_crt_parse(&s_ca, ca->data, ca->size + 1);
  if (error != 0) {
    weather_log_error("解析高德根证书", error);
    return false;
  }

  error = mbedtls_ssl_config_defaults(&s_conf, MBEDTLS_SSL_IS_CLIENT,
                                      MBEDTLS_SSL_TRANSPORT_STREAM,
                                      MBEDTLS_SSL_PRESET_DEFAULT);
  if (error != 0) {
    weather_log_error("初始化 TLS 配置", error);
    return false;
  }
  mbedtls_ssl_conf_authmode(&s_conf, MBEDTLS_SSL_VERIFY_REQUIRED);
  mbedtls_ssl_conf_ca_chain(&s_conf, &s_ca, NULL);
  mbedtls_ssl_conf_rng(&s_conf, mbedtls_ctr_drbg_random, &s_drbg);
  mbedtls_ssl_conf_read_timeout(&s_conf, kWeatherTlsWaitMs);

  error = mbedtls_ssl_setup(&s_ssl, &s_conf);
  if (error != 0) {
    weather_log_error("初始化 TLS 会话", error);
    return false;
  }
  error = mbedtls_ssl_set_hostname(&s_ssl, kWeatherHost);
  if (error != 0) {
    weather_log_error("设置 TLS 主机名", error);
    return false;
  }
  mbedtls_ssl_set_bio(&s_ssl, &s_conn, weather_tls_send, NULL,
                      weather_tls_recv);

  s_tls_ready = true;
  return true;
}

static bool weather_send_request(void) {
  int length =
      snprintf(s_request, sizeof(s_request),
               "GET /v3/weather/weatherInfo?key=%s&city=%s&extensions=base"
               "&output=JSON HTTP/1.1\r\n"
               "Host: " kWeatherHost
               "\r\n"
               "User-Agent: Pico-W-Weather/1.0\r\n"
               "Accept: application/json\r\n"
               "Accept-Encoding: identity\r\n"
               "Connection: close\r\n"
               "\r\n",
               s_weather_key,
               /* CITY_OVERRIDE_ADCODE 配置过就听用户的，否则用内置默认城市。 */
               kFirmwareWeatherAdcode[0] != '\0' ? kFirmwareWeatherAdcode
                                                 : kWeatherDefaultAdcode);
  if (length <= 0 || (size_t)length >= sizeof(s_request)) {
    weather_log_error("构造请求", -1);
    return false;
  }

  size_t sent = 0;
  while (sent < (size_t)length) {
    int count = mbedtls_ssl_write(
        &s_ssl, (const unsigned char*)s_request + sent, (size_t)length - sent);
    if (count <= 0) {
      weather_log_error("发送 HTTPS 请求", count);
      return false;
    }
    sent += (size_t)count;
  }
  return true;
}

static bool weather_read_response(size_t* out_length) {
  size_t total = 0;
  for (;;) {
    if (total + 1 >= sizeof(s_response)) {
      break;
    }
    int count = mbedtls_ssl_read(&s_ssl, (unsigned char*)s_response + total,
                                 sizeof(s_response) - 1 - total);
    if (count > 0) {
      total += (size_t)count;
      continue;
    }
    if (count == 0 || count == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY ||
        count == MBEDTLS_ERR_SSL_CONN_EOF) {
      break;
    }
    if (count == MBEDTLS_ERR_SSL_WANT_READ ||
        count == MBEDTLS_ERR_SSL_WANT_WRITE) {
      continue;
    }
    if (count == MBEDTLS_ERR_SSL_TIMEOUT && total > 0) {
      /* 服务器可能不立即关闭连接；已经收到的数据先交给 HTTP 解析器，
       * 由 Content-Length/chunked 校验完整性，避免白等 5 秒。 */
      break;
    }
    weather_log_error("读取 HTTPS 响应", count);
    return false;
  }
  s_response[total] = '\0';
  *out_length = total;
  return true;
}

static void weather_mark_stale(void) {
  WeatherData stale;
  if (s_has_last_weather) {
    stale = s_last_weather;
    stale.valid = true;
  } else {
    memset(&stale, 0, sizeof(stale));
    stale.valid = false;
  }
  stale.is_stale = true;
  system_state_update_weather(&stale);
}

static int weather_fetch_once(void) {
  int error = mbedtls_ssl_session_reset(&s_ssl);
  if (error != 0) {
    weather_log_error("重置 TLS 会话", error);
    return -1;
  }
  mbedtls_ssl_set_bio(&s_ssl, &s_conn, weather_tls_send, NULL,
                      weather_tls_recv);

  if (!weather_tcp_open()) {
    return -1;
  }

  error = mbedtls_ssl_handshake(&s_ssl);
  if (error != 0) {
    /* 证书链/主机名校验失败与普通握手失败分开打，串口才能定位。 */
    uint32_t verify_result = mbedtls_ssl_get_verify_result(&s_ssl);
    if (verify_result != 0 && verify_result != 0xFFFFFFFFu) {
      weather_log_error("证书校验", (int)verify_result);
    } else {
      weather_log_error("TLS 握手", error);
    }
    weather_tcp_close();
    return -1;
  }

  if (!weather_send_request()) {
    weather_tcp_close();
    return -1;
  }

  size_t response_length = 0;
  if (!weather_read_response(&response_length)) {
    weather_tcp_close();
    return -1;
  }
  if (response_length == 0) {
    weather_log_error("读取 HTTPS 响应", -1);
    weather_tcp_close();
    return -1;
  }

  size_t body_length = 0;
  char* body = (char*)weather_service_http_body(s_response, response_length,
                                                &body_length);
  if (body == NULL || body_length == 0) {
    weather_log_error("解析 HTTP 响应", -1);
    weather_tcp_close();
    return -1;
  }
  body[body_length] = '\0';

  WeatherData weather;
  if (!weather_service_parse_json(body, &weather)) {
    weather_log_error("解析天气 JSON", -1);
    weather_tcp_close();
    return -1;
  }

  s_last_weather = weather;
  s_has_last_weather = true;
  system_state_update_weather(&weather);
  printf("天气更新：%s %s %d℃。\n", weather.city, weather.weather,
         weather.temperature);

  mbedtls_ssl_close_notify(&s_ssl);
  weather_tcp_close();
  return 0;
}

static bool weather_key_is_valid(const char* key) {
  size_t length = strlen(key);
  if (length == 0 || length > 128) {
    return false;
  }
  for (size_t i = 0; i < length; ++i) {
    unsigned char value = (unsigned char)key[i];
    if (value < 33 || value == 127) {
      return false;
    }
  }
  return true;
}

static void weather_task(void* argument) {
  (void)argument;

  const char* key = s_weather_key;
  if (key[0] == '\0') {
    printf("未配置高德 Web 服务 Key，天气模块跳过，其它功能照常运行。\n");
    vTaskDelete(NULL);
    return;
  }
  if (!weather_key_is_valid(key)) {
    printf("高德 Web 服务 Key 格式非法，天气模块跳过。\n");
    vTaskDelete(NULL);
    return;
  }

  mbedtls_time_source_init();
  if (!weather_tls_init()) {
    printf("天气模块初始化失败，本次运行不再重试。\n");
    weather_mark_stale();
    vTaskDelete(NULL);
    return;
  }

  static const uint32_t kRetryDelaysMs[] = {60000u, 300000u, 900000u, 1800000u};
  TickType_t next_attempt = 0;
  uint32_t retry_index = 0;

  for (;;) {
    FIRMWARE_CHECK_IN(kSupervisorSlotWeather);
    if (!wifi_manager_is_network_ready() || !mbedtls_time_source_is_valid()) {
      vTaskDelay(pdMS_TO_TICKS(5000));
      continue;
    }

    TickType_t now = xTaskGetTickCount();
    if ((int32_t)(now - next_attempt) < 0) {
      vTaskDelay(pdMS_TO_TICKS(1000));
      continue;
    }

    int result = weather_fetch_once();
    TickType_t finished = xTaskGetTickCount();
    printf("天气刷新后剩余堆：%u 字节。\n", (unsigned)xPortGetFreeHeapSize());

    if (result == 0) {
      next_attempt = finished + pdMS_TO_TICKS(kWeatherRefreshMs);
      retry_index = 0;
    } else {
      weather_mark_stale();
      if (retry_index >= sizeof(kRetryDelaysMs) / sizeof(kRetryDelaysMs[0])) {
        retry_index = sizeof(kRetryDelaysMs) / sizeof(kRetryDelaysMs[0]) - 1;
      }
      next_attempt = finished + pdMS_TO_TICKS(kRetryDelaysMs[retry_index]);
      if (retry_index + 1 <
          sizeof(kRetryDelaysMs) / sizeof(kRetryDelaysMs[0])) {
        ++retry_index;
      }
    }
  }
}

bool weather_service_start(void) {
  if (s_started) {
    return true;
  }

  s_dns_sem = xSemaphoreCreateBinary();
  s_conn.connected_sem = xSemaphoreCreateBinary();
  s_conn.rx_sem = xSemaphoreCreateBinary();
  if (s_dns_sem == NULL || s_conn.connected_sem == NULL ||
      s_conn.rx_sem == NULL) {
    if (s_dns_sem != NULL) {
      vSemaphoreDelete(s_dns_sem);
    }
    if (s_conn.connected_sem != NULL) {
      vSemaphoreDelete(s_conn.connected_sem);
    }
    if (s_conn.rx_sem != NULL) {
      vSemaphoreDelete(s_conn.rx_sem);
    }
    s_dns_sem = NULL;
    s_conn.connected_sem = NULL;
    s_conn.rx_sem = NULL;
    printf("天气模块信号量创建失败。\n");
    return false;
  }

  TaskHandle_t task = NULL;
  BaseType_t created = xTaskCreate(weather_task, "weather_service",
                                   kWeatherTaskStackWords, NULL, 1, &task);
  if (created != pdPASS || task == NULL) {
    vSemaphoreDelete(s_dns_sem);
    vSemaphoreDelete(s_conn.connected_sem);
    vSemaphoreDelete(s_conn.rx_sem);
    s_dns_sem = NULL;
    s_conn.connected_sem = NULL;
    s_conn.rx_sem = NULL;
    printf("天气任务创建失败。\n");
    return false;
  }
#if (configNUMBER_OF_CORES > 1)
  vTaskCoreAffinitySet(task, 1u << 0);
#endif
  s_started = true;
  return true;
}

#else /* PICO_ON_DEVICE */

bool weather_service_start(void) { return false; }

#endif /* PICO_ON_DEVICE */
