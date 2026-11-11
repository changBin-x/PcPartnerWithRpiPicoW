/* 宿主端 UDP 接收端：把固件里同一份 metrics_parser.c 挂到真实 socket 上。
 *
 * 用途是端到端验证协议链路：PC 端用 pc_metrics.protocol.encode_packet 生成
 * 报文并通过真实 UDP 套接字发送，这里用固件解析器接收并判定，从而覆盖
 * 「函数级单测覆盖不到」的环节——数据报大小、是否被截断、端口可达性。
 *
 * 用法：
 *   host_udp_sink --port 8765 --expect 5 --timeout-ms 5000
 * 退出码：全部按预期收齐为 0，超时或结果不符为 1。
 */

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include "metrics_parser.h"

/* 协议上限是 512 字节；缓冲区留出余量，这样一旦对端发超长报文，recvfrom 也
 * 不会把报文截断成「看起来合法」的数据。 */
#define kRecvBufferBytes 1024

static int parse_int_arg(int argc, char** argv, const char* name,
                         int fallback) {
  for (int i = 1; i + 1 < argc; ++i) {
    if (strcmp(argv[i], name) == 0) {
      return atoi(argv[i + 1]);
    }
  }
  return fallback;
}

int main(int argc, char** argv) {
  int port = parse_int_arg(argc, argv, "--port", 8765);
  int expect = parse_int_arg(argc, argv, "--expect", 1);
  int timeout_ms = parse_int_arg(argc, argv, "--timeout-ms", 5000);
  if (port <= 0 || port > 65535 || expect <= 0 || timeout_ms <= 0) {
    fprintf(stderr, "参数非法\n");
    return 2;
  }

  int sock = socket(AF_INET, SOCK_DGRAM, 0);
  if (sock < 0) {
    perror("socket");
    return 2;
  }
  struct timeval timeout;
  timeout.tv_sec = timeout_ms / 1000;
  timeout.tv_usec = (timeout_ms % 1000) * 1000;
  setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

  struct sockaddr_in address;
  memset(&address, 0, sizeof(address));
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_ANY);
  address.sin_port = htons((uint16_t)port);
  if (bind(sock, (struct sockaddr*)&address, sizeof(address)) < 0) {
    perror("bind");
    close(sock);
    return 2;
  }
  /* 先打印就绪行，驱动脚本据此判断可以开始发送，避免启动竞争。 */
  printf("READY %d\n", port);
  fflush(stdout);

  MetricsParser parser;
  metrics_parser_init(&parser);
  static char buffer[kRecvBufferBytes];
  int accepted = 0;
  int rejected = 0;
  while (accepted < expect) {
    struct sockaddr_in peer;
    socklen_t peer_length = sizeof(peer);
    ssize_t received = recvfrom(sock, buffer, sizeof(buffer), 0,
                                (struct sockaddr*)&peer, &peer_length);
    if (received < 0) {
      if (errno == EAGAIN || errno == EWOULDBLOCK) {
        printf("TIMEOUT 已接受 %d/%d\n", accepted, expect);
        close(sock);
        return 1;
      }
      perror("recvfrom");
      close(sock);
      return 2;
    }
    PcMetricsData metrics;
    MetricsParseResult result =
        metrics_parser_parse(&parser, buffer, (size_t)received, &metrics);
    if (result == kMetricsParseOk) {
      ++accepted;
      printf(
          "OK %d seq=%u ts=%u cpu=%.2f gpu_valid=%d ram=%.2f rx=%.3f "
          "tx=%.3f\n",
          accepted, metrics.seq, metrics.ts, (double)metrics.cpu,
          metrics.gpu_valid ? 1 : 0, (double)metrics.ram,
          (double)metrics.rx_mbps, (double)metrics.tx_mbps);
    } else {
      ++rejected;
      printf("REJECT code=%d\n", (int)result);
    }
    fflush(stdout);
  }
  printf("DONE accepted=%d rejected=%d\n", accepted, rejected);
  close(sock);
  return 0;
}
