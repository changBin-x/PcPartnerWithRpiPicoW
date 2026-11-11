/* 本文件由 tools/gen_protocol_fixtures.py 生成，请勿手工编辑。
 * 合法报文来自 PC 端真实的 pc_metrics.protocol.encode_packet，
 * 非法报文为手写样本；两侧判断必须完全一致。 */
#ifndef PICO_FREERTOS_FIRMWARE_TESTS_GENERATED_UDP_PACKETS_DATA_H_
#define PICO_FREERTOS_FIRMWARE_TESTS_GENERATED_UDP_PACKETS_DATA_H_

typedef struct {
  const char* name;   /* 用例名称。 */
  int expected;       /* 期望的 MetricsParseResult 取值。 */
  const char* packet; /* 报文字节内容（不含结尾 NUL）。 */
  unsigned int length;
} UdpPacketCase;

static const UdpPacketCase kUdpPacketCases[] = {
    {"典型值", 0, "{\"v\":1,\"seq\":1,\"ts\":1762400000,\"cpu\":42,\"gpu\":15,\"ram\":61,\"rx\":1.5,\"tx\":0.25}", 77},
    {"无独显 gpu 为 null", 0, "{\"v\":1,\"seq\":2,\"ts\":1762400001,\"cpu\":7,\"gpu\":null,\"ram\":33,\"rx\":0.0,\"tx\":0.0}", 77},
    {"全部取值为零的边界", 0, "{\"v\":1,\"seq\":3,\"ts\":1762400002,\"cpu\":0,\"gpu\":0,\"ram\":0,\"rx\":0,\"tx\":0}", 69},
    {"百分比上界 100", 0, "{\"v\":1,\"seq\":4,\"ts\":1762400003,\"cpu\":100,\"gpu\":100,\"ram\":100,\"rx\":999.99,\"tx\":1234.5}", 85},
    {"浮点百分比", 0, "{\"v\":1,\"seq\":5,\"ts\":1762400004,\"cpu\":42.5,\"gpu\":0.0,\"ram\":61.25,\"rx\":0.001,\"tx\":0.002}", 86},
    {"空报文", 1, "", 0},
    {"非 JSON 文本", 1, "hello", 5},
    {"根节点是数组", 1, "[1,2,3]", 7},
    {"版本号不支持", 3, "{\"v\": 2, \"seq\": 1, \"ts\": 1762400000, \"cpu\": 1, \"gpu\": 1, \"ram\": 1, \"rx\": 1, \"tx\": 1}", 84},
    {"缺少 ram 字段", 2, "{\"v\": 1, \"seq\": 1, \"ts\": 1762400000, \"cpu\": 1, \"gpu\": 1, \"rx\": 1, \"tx\": 1}", 74},
    {"cpu 超出 100", 4, "{\"v\": 1, \"seq\": 1, \"ts\": 1762400000, \"cpu\": 101, \"gpu\": 1, \"ram\": 1, \"rx\": 1, \"tx\": 1}", 86},
    {"cpu 为负数", 4, "{\"v\": 1, \"seq\": 1, \"ts\": 1762400000, \"cpu\": -1, \"gpu\": 1, \"ram\": 1, \"rx\": 1, \"tx\": 1}", 85},
    {"ts 为零", 4, "{\"v\": 1, \"seq\": 1, \"ts\": 0, \"cpu\": 1, \"gpu\": 1, \"ram\": 1, \"rx\": 1, \"tx\": 1}", 75},
    {"gpu 类型错误", 4, "{\"v\": 1, \"seq\": 1, \"ts\": 1762400000, \"cpu\": 1, \"gpu\": \"abc\", \"ram\": 1, \"rx\": 1, \"tx\": 1}", 88},
    {"尾部有非空白垃圾", 1, "{\"v\":1,\"seq\":9,\"ts\":1762400000,\"cpu\":1,\"gpu\":1,\"ram\":1,\"rx\":1,\"tx\":1}XYZ", 72},
    {"seq 为负数", 4, "{\"v\": 1, \"seq\": -3, \"ts\": 1762400000, \"cpu\": 1, \"gpu\": 1, \"ram\": 1, \"rx\": 1, \"tx\": 1}", 85},
};

static const unsigned int kUdpPacketCaseCount =
    sizeof(kUdpPacketCases) / sizeof(kUdpPacketCases[0]);

#endif /* PICO_FREERTOS_FIRMWARE_TESTS_GENERATED_UDP_PACKETS_DATA_H_ */
