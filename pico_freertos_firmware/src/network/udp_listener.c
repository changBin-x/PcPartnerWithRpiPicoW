/**
 * @file udp_listener.c
 * @brief UDP 指标接收：监听 8765 端口，严格解析后写入系统状态。
 *
 * lwIP 以 LWIP_SOCKET=0 编译，因此这里使用 raw UDP API。接收回调在
 * tcpip 线程里把报文复制进定长队列，解析任务再取出来解析；回调本身
 * 不做 JSON 解析，避免阻塞协议栈。队列满或报文超长时只计数，不刷屏。
 */

#include "udp_listener.h"

#ifdef PICO_ON_DEVICE

#include <stdio.h>

#include "FreeRTOS.h"
#include "lwip/ip_addr.h"
#include "lwip/pbuf.h"
#include "lwip/udp.h"
#include "metrics_parser.h"
#include "pico/cyw43_arch.h"
#include "queue.h"
#include "firmware_checkin.h"
#include "system_state.h"
#include "task.h"
#include "wifi_manager.h"

/* 监听端口，与 PC 端 pc_metrics/protocol.py 保持一致。 */
#define kUdpListenerPort 8765
/* 单包缓冲必须大于协议上限 512 字节。 */
#define kUdpListenerBufferBytes 1024
/* 队列长度：足以吸收 1Hz 指标包的短时抖动。 */
#define kUdpListenerQueueLength 4
/* UDP 任务栈（字），4KB。 */
#define kUdpListenerTaskStackWords 1024u
/* UDP 任务优先级，高于 Wi-Fi、NTP 与天气任务。 */
#define kUdpListenerTaskPriority 4u
/* 等待队列的超时时间；超时只是回到循环顶部，不会卡死。 */
#define kUdpListenerReceiveTimeoutMs 5000u
/* 非法包日志的抽样间隔。 */
#define kUdpListenerInvalidLogEvery 100u

typedef struct {
  uint16_t length;
  uint8_t data[kUdpListenerBufferBytes];
} UdpPacket;

static QueueHandle_t s_queue = NULL;
static MetricsParser s_parser;
static bool s_started = false;
static volatile uint32_t s_dropped_packets = 0;
static uint32_t s_invalid_packets = 0;

/* 回调在 tcpip 线程运行：复制到队列即返回，解析放到任务里做。 */
static void udp_listener_receive(void* argument, struct udp_pcb* pcb,
                                 struct pbuf* packet, const ip_addr_t* address,
                                 uint16_t port) {
  (void)argument;
  (void)pcb;
  (void)address;
  (void)port;

  if (packet == NULL) {
    return;
  }

  /* 回调只在 tcpip 线程串行执行，静态缓冲避免占用 1KB 协议栈。 */
  static UdpPacket received;
  if (packet->tot_len > kUdpListenerBufferBytes) {
    ++s_dropped_packets;
  } else {
    received.length = packet->tot_len;
    if (pbuf_copy_partial(packet, received.data, packet->tot_len, 0) !=
        packet->tot_len) {
      ++s_dropped_packets;
    } else if (xQueueSend(s_queue, &received, 0) != pdPASS) {
      ++s_dropped_packets;
    }
  }
  pbuf_free(packet);
}

static void udp_listener_task(void* argument) {
  (void)argument;

  metrics_parser_init(&s_parser);

  struct udp_pcb* pcb = NULL;
  for (;;) {
    if (wifi_manager_is_network_ready()) {
      cyw43_arch_lwip_begin();
      pcb = udp_new();
      if (pcb != NULL) {
        if (udp_bind(pcb, IP_ADDR_ANY, kUdpListenerPort) == ERR_OK) {
          udp_recv(pcb, udp_listener_receive, NULL);
        } else {
          udp_remove(pcb);
          pcb = NULL;
        }
      }
      cyw43_arch_lwip_end();
      if (pcb != NULL) {
        break;
      }
      printf("UDP 监听初始化失败，5 秒后重试。\n");
    }
    vTaskDelay(pdMS_TO_TICKS(5000));
  }

  printf("UDP 指标监听已启动：0.0.0.0:%d\n", kUdpListenerPort);
  static UdpPacket packet;
  uint32_t reported_drops = 0;
  for (;;) {
    FIRMWARE_CHECK_IN(kSupervisorSlotUdp);
    if (xQueueReceive(s_queue, &packet,
                      pdMS_TO_TICKS(kUdpListenerReceiveTimeoutMs)) != pdTRUE) {
      continue;
    }

    PcMetricsData metrics;
    MetricsParseResult result = metrics_parser_parse(
        &s_parser, (const char*)packet.data, packet.length, &metrics);
    if (result == kMetricsParseOk) {
      system_state_update_metrics(&metrics, xTaskGetTickCount());
    } else {
      ++s_invalid_packets;
      if (s_invalid_packets <= 3 ||
          (s_invalid_packets % kUdpListenerInvalidLogEvery) == 0) {
        printf("收到非法指标包（累计 %u 个），解析错误码 %d。\n",
               (unsigned)s_invalid_packets, (int)result);
      }
    }

    uint32_t drops = s_dropped_packets;
    if (drops != reported_drops) {
      printf("UDP 接收队列丢弃 %u 个报文。\n", (unsigned)drops);
      reported_drops = drops;
    }
  }
}

bool udp_listener_start(void) {
  if (s_started) {
    return true;
  }
  s_queue = xQueueCreate(kUdpListenerQueueLength, sizeof(UdpPacket));
  if (s_queue == NULL) {
    printf("UDP 接收队列创建失败。\n");
    return false;
  }

  TaskHandle_t task = NULL;
  BaseType_t created =
      xTaskCreate(udp_listener_task, "udp_listener", kUdpListenerTaskStackWords,
                  NULL, kUdpListenerTaskPriority, &task);
  if (created != pdPASS || task == NULL) {
    vQueueDelete(s_queue);
    s_queue = NULL;
    printf("UDP 监听任务创建失败。\n");
    return false;
  }
#if (configNUMBER_OF_CORES > 1)
  vTaskCoreAffinitySet(task, 1u << 0);
#endif
  s_started = true;
  return true;
}

#else /* PICO_ON_DEVICE */

bool udp_listener_start(void) { return false; }

#endif /* PICO_ON_DEVICE */
