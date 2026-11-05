/**
 * @file network_start.c
 * @brief 网络子系统统一启动入口。
 */

#include "network_start.h"

#ifdef PICO_ON_DEVICE

#include <stdio.h>

#include "mbedtls_time_source.h"
#include "ntp_service.h"
#include "udp_listener.h"
#include "weather_service.h"
#include "wifi_manager.h"

/* main.c 目前还没有调用 network_start。链接器以入口符号为根做
 * --gc-sections，未引用的函数会被整段回收。这里注册一个空构造函数：
 * 它只在恒为 false 的 volatile 分支里引用 network_start，构造函数本身
 * 不启动任何任务，但能让链接器把整条网络调用链保留在镜像里。
 * 等 main 完成接线后可以删除这段。 */
static volatile bool s_link_keepalive = false;

static void network_start_link_keepalive(void) __attribute__((constructor));

static void network_start_link_keepalive(void) {
  if (s_link_keepalive) {
    (void)network_start();
  }
}

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

  printf("网络子系统启动：Wi-Fi %s，UDP %s，NTP %s，天气 %s。\n",
         wifi ? "已创建" : "失败", udp ? "已创建" : "失败",
         ntp ? "已创建" : "失败", weather ? "已创建" : "失败");
  return result;
}

#else /* PICO_ON_DEVICE */

bool network_start(void) { return false; }

#endif /* PICO_ON_DEVICE */
