/**
 * @file wifi_manager.c
 * @brief CYW43439 联网管理：初始化、连接、掉线检测与指数退避重试。
 *
 * 所有工作在 Core 0 的独立任务中完成。SSID 未配置时只打印一条中文
 * 提示并结束任务，不阻塞其他模块，也不做无意义的重连。
 */

#include "wifi_manager.h"

#include <stdint.h>

#ifdef PICO_ON_DEVICE

#include <stdio.h>

#include "FreeRTOS.h"
#include "cyw43.h"
#include "firmware_secrets.h"
#include "pico/cyw43_arch.h"
#include "firmware_checkin.h"
#include "system_state.h"
#include "task.h"

#endif /* PICO_ON_DEVICE */

/* 单次连接尝试的超时时间。 */
#define kWifiConnectTimeoutMs 15000u
/* 已连接时的掉线轮询周期。 */
#define kWifiStatusPollMs 1000u
/* Wi-Fi 管理任务栈（字），4KB。 */
#define kWifiTaskStackWords 1024u
/* Wi-Fi 管理任务优先级，低于 UDP 接收任务。 */
#define kWifiTaskPriority 2u
/* 退避表长度，也是“之后固定 30 秒”的起点。 */
#define kWifiBackoffCount 4

static const uint32_t kBackoffDelaysMs[kWifiBackoffCount] = {
    1000u,
    3000u,
    10000u,
    30000u,
};

/* 任务之间只通过这个标志交流连接状态；bool 在 Cortex-M0+ 上是原子访问。 */
static volatile bool s_connected = false;
static volatile bool s_network_ready = false;
#ifdef PICO_ON_DEVICE
/* 通过 volatile 指针读取 SSID，避免编译器在 SSID 为空字符串时把
 * 后面的 CYW43 初始化与重连代码整段做常量折叠掉；当前未配置 Wi-Fi
 * 时也要让连接路径留在镜像里，换入凭据重新构建后即可使用。 */
static const char* volatile s_wifi_ssid = kFirmwareWifiSsid;
static bool s_started = false;
#endif

uint32_t wifi_manager_backoff_delay_ms(int attempt) {
  if (attempt < 0) {
    return kBackoffDelaysMs[0];
  }
  if (attempt >= kWifiBackoffCount) {
    return kBackoffDelaysMs[kWifiBackoffCount - 1];
  }
  return kBackoffDelaysMs[attempt];
}

bool wifi_manager_is_connected(void) { return s_connected; }

bool wifi_manager_is_network_ready(void) { return s_network_ready; }

#ifdef PICO_ON_DEVICE

/* 已连接时每秒检查一次链路；掉线后立即重新连接。 */
static void wifi_manager_wait_while_connected(void) {
  for (;;) {
    /* 已连接期间主循环停在这里，签到必须在这一层也有。 */
    FIRMWARE_CHECK_IN(kSupervisorSlotWifi);
    vTaskDelay(pdMS_TO_TICKS(kWifiStatusPollMs));
    if (cyw43_wifi_link_status(&cyw43_state, CYW43_ITF_STA) == CYW43_LINK_UP) {
      continue;
    }
    s_connected = false;
    system_state_set_wifi_status(false);
    printf("Wi-Fi 连接已断开，立即重连。\n");
    return;
  }
}

static void wifi_manager_task(void* argument) {
  (void)argument;

  const char* ssid = s_wifi_ssid;
  if (ssid[0] == '\0') {
    printf("Wi-Fi 未配置 SSID，跳过联网；时钟与本地界面继续运行。\n");
    s_connected = false;
    system_state_set_wifi_status(false);
    vTaskDelete(NULL);
    return;
  }

  int result = cyw43_arch_init();
  if (result != 0) {
    printf("Wi-Fi 芯片初始化失败，错误码 %d；联网功能不可用。\n", result);
    s_connected = false;
    system_state_set_wifi_status(false);
    vTaskDelete(NULL);
    return;
  }
  cyw43_arch_enable_sta_mode();
  s_network_ready = true;
  printf("Wi-Fi 已初始化，准备连接 SSID：%s\n", ssid);

  int attempt = 0;
  for (;;) {
    /* 连接成功后会一直在 wait_while_connected 里轮询，所以签到放在循环开头，
     * 保证「已连上」和「在重连」两种状态都有心跳。 */
    FIRMWARE_CHECK_IN(kSupervisorSlotWifi);
    result = cyw43_arch_wifi_connect_timeout_ms(ssid, kFirmwareWifiPassword,
                                                CYW43_AUTH_WPA2_AES_PSK,
                                                kWifiConnectTimeoutMs);
    if (result == 0) {
      s_connected = true;
      system_state_set_wifi_status(true);
      printf("Wi-Fi 已连接：%s\n", ssid);
      attempt = 0;
      wifi_manager_wait_while_connected();
      continue;
    }

    uint32_t delay_ms = wifi_manager_backoff_delay_ms(attempt);
    if (attempt < kWifiBackoffCount) {
      ++attempt;
    }
    s_connected = false;
    system_state_set_wifi_status(false);
    printf("Wi-Fi 连接失败（错误码 %d），%u 毫秒后重试。\n", result,
           (unsigned)delay_ms);
    vTaskDelay(pdMS_TO_TICKS(delay_ms));
  }
}

bool wifi_manager_start(void) {
  if (s_started) {
    return true;
  }
  s_connected = false;
  s_network_ready = false;
  system_state_set_wifi_status(false);
  TaskHandle_t task = NULL;
  BaseType_t created =
      xTaskCreate(wifi_manager_task, "wifi_manager", kWifiTaskStackWords, NULL,
                  kWifiTaskPriority, &task);
  if (created != pdPASS || task == NULL) {
    printf("Wi-Fi 管理任务创建失败。\n");
    return false;
  }
#if (configNUMBER_OF_CORES > 1)
  vTaskCoreAffinitySet(task, 1u << 0);
#endif
  s_started = true;
  return true;
}

#else /* PICO_ON_DEVICE */

bool wifi_manager_start(void) { return false; }

#endif /* PICO_ON_DEVICE */
