#ifndef PICO_FREERTOS_FIRMWARE_LWIPOPTS_H_
#define PICO_FREERTOS_FIRMWARE_LWIPOPTS_H_

/*
 * lwIP 配置：跑在 FreeRTOS 之上（NO_SYS=0），只用 raw API。
 *
 * pico-sdk 2.3.1 不再自带 lwipopts.h（lib/lwip 是纯上游代码），固件必须
 * 自备一份，否则 opt.h 里 #include "lwipopts.h" 直接失败。网络任务后续若
 * 需要 BSD socket、TCP 服务端或 TLS，在本文件里扩展即可。
 */

/* 系统模式：由 FreeRTOS 端口提供 sys_arch（tcpip 线程 + 邮箱）。 */
#define NO_SYS 0

/* 只用 raw API，省掉 netconn 与 socket 层。 */
#define LWIP_NETCONN 0
#define LWIP_SOCKET 0

/* FreeRTOS 端口的 sys_mbox_new 断言长度必须大于 0，故邮箱不能留默认值 0。 */
#define TCPIP_MBOX_SIZE 16
#define DEFAULT_RAW_RECVMBOX_SIZE 0
#define DEFAULT_UDP_RECVMBOX_SIZE 0
#define DEFAULT_TCP_RECVMBOX_SIZE 0
#define DEFAULT_ACCEPTMBOX_SIZE 0

/* tcpip 线程：栈单位是字节（端口按 lwIP 语义解释，非 FreeRTOS 字长），
 * 取值对齐设计规格中的 8 KB 预算。 */
#define TCPIP_THREAD_STACKSIZE 8192
#define TCPIP_THREAD_PRIO 3

/* 内存：sys 模式必须用 lwIP 自己的堆，不能改成 libc malloc。 */
#define MEM_LIBC_MALLOC 0
#define MEM_ALIGNMENT 4
#define MEM_SIZE (8 * 1024)

/* 收包缓冲池：每个 pbuf 容纳一个完整以太网帧（1500 字节载荷 + 链路头），
 * 21 x 1524 约 32 KB，与设计规格的 PBUF 预算一致。 */
#define PBUF_POOL_SIZE 21
#define PBUF_POOL_BUFSIZE 1524

/* 协议开关：IPv4 + UDP 是主链路，DHCP/DNS 供联网使用。 */
#define LWIP_IPV4 1
#define LWIP_IPV6 0
#define LWIP_ARP 1
#define LWIP_ETHERNET 1
#define LWIP_ICMP 1
#define LWIP_RAW 1
#define LWIP_UDP 1
#define LWIP_TCP 1
#define LWIP_DHCP 1
#define LWIP_DNS 1

/* TCP 参数：HTTPS 走 Wi-Fi，MSS 按以太网 MTU 取 1460；窗口先给 4 个段，
 * 由网络任务按实测吞吐调整。 */
#define TCP_MSS 1460
#define TCP_WND (4 * TCP_MSS)
#define TCP_SND_BUF (4 * TCP_MSS)
#define TCP_SND_QUEUELEN ((4 * TCP_SND_BUF + (TCP_MSS - 1)) / TCP_MSS)
#define MEMP_NUM_TCP_SEG 16

/* 网卡事件回调：联网状态要反馈给业务层。 */
#define LWIP_NETIF_HOSTNAME 1
#define LWIP_NETIF_STATUS_CALLBACK 1
#define LWIP_NETIF_LINK_CALLBACK 1
#define LWIP_NETIF_TX_SINGLE_PBUF 1

/* Release 固件关闭统计，省 flash 与 RAM。 */
#define MEM_STATS 0
#define SYS_STATS 0
#define MEMP_STATS 0
#define LINK_STATS 0

#endif /* PICO_FREERTOS_FIRMWARE_LWIPOPTS_H_ */
