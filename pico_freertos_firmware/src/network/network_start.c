/**
 * @file network_start.c
 * @brief 网络子系统统一启动入口。
 */

#include "network_start.h"

#ifdef PICO_ON_DEVICE

#include <stdio.h>

#include "firmware_checkin.h"
#include "mbedtls_time_source.h"
#include "ntp_service.h"
#include "udp_listener.h"
#include "weather_service.h"
#include "wifi_manager.h"

/* 入口由 main 调用；--gc-sections 会顺着它保留四个模块的任务函数。
 * noinline 防止编译器把它拆成局部 .part.0 后丢掉公开符号。 */
__attribute__((noinline)) bool network_start(void) {
  static bool started = false;
  static bool result = false;
  if (started) {
    return result;
  }
  started = true;

  mbedtls_time_source_init();

  bool wifi = wifi_manager_start();
  bool udp = udp_listener_start();
  bool ntp = ntp_service_start();
  bool weather = weather_service_start();
  result = wifi && udp && ntp && weather;

  /* 任务没建起来就不会有任何签到，必须把对应槽位摘掉，否则监督任务会
   * 认定系统不健康而停止喂狗，把「少一个功能」放大成「整机复位循环」。 */
  if (!wifi) {
    FIRMWARE_CHECK_IN_DISABLE(kSupervisorSlotWifi);
  }
  if (!udp) {
    FIRMWARE_CHECK_IN_DISABLE(kSupervisorSlotUdp);
  }
  if (!ntp) {
    FIRMWARE_CHECK_IN_DISABLE(kSupervisorSlotNtp);
  }
  if (!weather) {
    FIRMWARE_CHECK_IN_DISABLE(kSupervisorSlotWeather);
  }

  printf("网络子系统启动：Wi-Fi %s，UDP %s，NTP %s，天气 %s。\n",
         wifi ? "已创建" : "失败", udp ? "已创建" : "失败",
         ntp ? "已创建" : "失败", weather ? "已创建" : "失败");
  return result;
}

#else /* PICO_ON_DEVICE */

bool network_start(void) { return false; }

#endif /* PICO_ON_DEVICE */
