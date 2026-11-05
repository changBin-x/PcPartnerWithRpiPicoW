#include "metrics_parser.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "generated/udp_packets_data.h"

/* 数据集由 tools/gen_protocol_fixtures.py 生成：合法报文全部来自 PC 端真实的
 * pc_metrics.protocol.encode_packet，非法报文为手写样本。本测试逐条断言固件
 * 解析器给出与 PC 端一致的判断，把「协议对齐」从口头约定变成可执行证据。 */

int main(void) {
  assert(kUdpPacketCaseCount > 0u);

  unsigned int accepted = 0;
  unsigned int rejected = 0;
  for (unsigned int i = 0; i < kUdpPacketCaseCount; ++i) {
    const UdpPacketCase* item = &kUdpPacketCases[i];

    /* 长度字段必须与字符串真实长度一致，否则说明生成器写错了，会掩盖
     * 「按长度解析」路径上的问题。 */
    if (strlen(item->packet) != (size_t)item->length) {
      printf("FAIL: 用例「%s」的长度字段 %u 与字符串长度 %zu 不一致\n",
             item->name, item->length, strlen(item->packet));
      return 1;
    }
    /* 每条用例都用全新解析器，避免上一条的 last_seq 影响判定。 */
    MetricsParser parser;
    metrics_parser_init(&parser);
    PcMetricsData metrics;
    memset(&metrics, 0x5a, sizeof(metrics));

    MetricsParseResult result = metrics_parser_parse(
        &parser, item->packet, (size_t)item->length, &metrics);
    if ((int)result != item->expected) {
      printf("FAIL: 用例「%s」期望 %d，实际 %d\n", item->name, item->expected,
             (int)result);
      return 1;
    }

    if (result == kMetricsParseOk) {
      ++accepted;
      /* 合法报文必须真的被解出内容，不能只是返回码正确。 */
      if (metrics.seq == 0u && item->length > 0u) {
        /* seq 允许为 0，这里只确保字段被写过：ts 必须落在合理区间。 */
        if (metrics.ts == 0u) {
          printf("FAIL: 用例「%s」判定合法但 ts 未被填充\n", item->name);
          return 1;
        }
      }
      if (metrics.cpu < 0.0f || metrics.cpu > 100.0f) {
        printf("FAIL: 用例「%s」cpu 越界 %f\n", item->name,
               (double)metrics.cpu);
        return 1;
      }
    } else {
      ++rejected;
      /* 拒绝时报文内容不得污染输出结构体。 */
      const unsigned char* raw = (const unsigned char*)&metrics;
      for (size_t b = 0; b < sizeof(metrics); ++b) {
        if (raw[b] != 0x5a) {
          printf("FAIL: 用例「%s」被拒绝却改写了输出结构体\n", item->name);
          return 1;
        }
      }
    }
  }

  /* 数据集必须同时覆盖两侧，否则「全部接受」或「全部拒绝」也能让测试通过。 */
  assert(accepted >= 5u);
  assert(rejected >= 10u);
  printf("PASS: 协议一致性 %u 条合法 / %u 条拒绝，共 %u 条\n", accepted,
         rejected, kUdpPacketCaseCount);

  /* 补充：同一 seq 重复到达必须判为过期，跨包状态机与单包校验是两件事。 */
  MetricsParser parser;
  metrics_parser_init(&parser);
  PcMetricsData metrics;
  static const char kPacket[] =
      "{\"v\":1,\"seq\":7,\"ts\":1762400000,\"cpu\":50,\"gpu\":null,"
      "\"ram\":40,\"rx\":1,\"tx\":2}";
  assert(metrics_parser_parse(&parser, kPacket, sizeof(kPacket) - 1,
                              &metrics) == kMetricsParseOk);
  assert(metrics_parser_parse(&parser, kPacket, sizeof(kPacket) - 1,
                              &metrics) == kMetricsErrStaleSeq);
  printf("PASS: 重复 seq 判为过期\n");

  printf("PASS: test_protocol_conformance\n");
  return 0;
}
