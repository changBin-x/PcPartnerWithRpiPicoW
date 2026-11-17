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
    {"典型值", 0, "{\"v\":2,\"seq\":1,\"ts\":1762400000,\"cpu\":42,\"gpu\":15,\"ram\":61,\"rx\":1.5,\"tx\":0.25,\"cpu_label\":\"CPU\",\"gpu_label\":\"GPU\",\"ram_label\":\"16G\"}", 131},
    {"无独显 gpu 为 null", 0, "{\"v\":2,\"seq\":2,\"ts\":1762400001,\"cpu\":7,\"gpu\":null,\"ram\":33,\"rx\":0.0,\"tx\":0.0,\"cpu_label\":\"CPU\",\"gpu_label\":\"GPU\",\"ram_label\":\"16G\"}", 131},
    {"全部取值为零的边界", 0, "{\"v\":2,\"seq\":3,\"ts\":1762400002,\"cpu\":0,\"gpu\":0,\"ram\":0,\"rx\":0,\"tx\":0,\"cpu_label\":\"CPU\",\"gpu_label\":\"GPU\",\"ram_label\":\"16G\"}", 123},
    {"百分比上界 100", 0, "{\"v\":2,\"seq\":4,\"ts\":1762400003,\"cpu\":100,\"gpu\":100,\"ram\":100,\"rx\":999.99,\"tx\":1234.5,\"cpu_label\":\"CPU\",\"gpu_label\":\"GPU\",\"ram_label\":\"16G\"}", 139},
    {"浮点百分比", 0, "{\"v\":2,\"seq\":5,\"ts\":1762400004,\"cpu\":42.5,\"gpu\":0.0,\"ram\":61.25,\"rx\":0.001,\"tx\":0.002,\"cpu_label\":\"CPU\",\"gpu_label\":\"GPU\",\"ram_label\":\"16G\"}", 140},
    {"自定义身份标签", 0, "{\"v\":2,\"seq\":6,\"ts\":1762400005,\"cpu\":10,\"gpu\":null,\"ram\":20,\"rx\":0,\"tx\":0,\"cpu_label\":\"Orin NX\",\"gpu_label\":\"Orin nvgpu\",\"ram_label\":\"16G\"}", 139},
    {"空报文", 1, "", 0},
    {"非 JSON 文本", 1, "hello", 5},
    {"根节点是数组", 1, "[1,2,3]", 7},
    {"旧版 v1 八字段", 2, "{\"v\": 1, \"seq\": 1, \"ts\": 1762400000, \"cpu\": 1, \"gpu\": 1, \"ram\": 1, \"rx\": 1, \"tx\": 1}", 84},
    {"版本号为已废弃的 1", 3, "{\"v\": 1, \"seq\": 1, \"ts\": 1762400000, \"cpu\": 1, \"gpu\": 1, \"ram\": 1, \"rx\": 1, \"tx\": 1, \"cpu_label\": \"CPU\", \"gpu_label\": \"GPU\", \"ram_label\": \"16G\"}", 144},
    {"版本号不支持", 3, "{\"v\": 3, \"seq\": 1, \"ts\": 1762400000, \"cpu\": 1, \"gpu\": 1, \"ram\": 1, \"rx\": 1, \"tx\": 1, \"cpu_label\": \"CPU\", \"gpu_label\": \"GPU\", \"ram_label\": \"16G\"}", 144},
    {"缺少 ram 字段", 2, "{\"v\": 2, \"seq\": 1, \"ts\": 1762400000, \"cpu\": 1, \"gpu\": 1, \"rx\": 1, \"tx\": 1, \"cpu_label\": \"CPU\", \"gpu_label\": \"GPU\", \"ram_label\": \"16G\"}", 134},
    {"缺少 cpu_label 字段", 2, "{\"v\": 2, \"seq\": 1, \"ts\": 1762400000, \"cpu\": 1, \"gpu\": 1, \"ram\": 1, \"rx\": 1, \"tx\": 1, \"gpu_label\": \"GPU\", \"ram_label\": \"16G\"}", 124},
    {"cpu 超出 100", 4, "{\"v\": 2, \"seq\": 1, \"ts\": 1762400000, \"cpu\": 101, \"gpu\": 1, \"ram\": 1, \"rx\": 1, \"tx\": 1, \"cpu_label\": \"CPU\", \"gpu_label\": \"GPU\", \"ram_label\": \"16G\"}", 146},
    {"cpu 为负数", 4, "{\"v\": 2, \"seq\": 1, \"ts\": 1762400000, \"cpu\": -1, \"gpu\": 1, \"ram\": 1, \"rx\": 1, \"tx\": 1, \"cpu_label\": \"CPU\", \"gpu_label\": \"GPU\", \"ram_label\": \"16G\"}", 145},
    {"ts 为零", 4, "{\"v\": 2, \"seq\": 1, \"ts\": 0, \"cpu\": 1, \"gpu\": 1, \"ram\": 1, \"rx\": 1, \"tx\": 1, \"cpu_label\": \"CPU\", \"gpu_label\": \"GPU\", \"ram_label\": \"16G\"}", 135},
    {"gpu 类型错误", 4, "{\"v\": 2, \"seq\": 1, \"ts\": 1762400000, \"cpu\": 1, \"gpu\": \"abc\", \"ram\": 1, \"rx\": 1, \"tx\": 1, \"cpu_label\": \"CPU\", \"gpu_label\": \"GPU\", \"ram_label\": \"16G\"}", 148},
    {"cpu_label 为空", 4, "{\"v\": 2, \"seq\": 1, \"ts\": 1762400000, \"cpu\": 1, \"gpu\": 1, \"ram\": 1, \"rx\": 1, \"tx\": 1, \"cpu_label\": \"\", \"gpu_label\": \"GPU\", \"ram_label\": \"16G\"}", 141},
    {"cpu_label 含非 ASCII", 4, "{\"v\": 2, \"seq\": 1, \"ts\": 1762400000, \"cpu\": 1, \"gpu\": 1, \"ram\": 1, \"rx\": 1, \"tx\": 1, \"cpu_label\": \"\xe5\xa4\x84\xe7\x90\x86\xe5\x99\xa8\", \"gpu_label\": \"GPU\", \"ram_label\": \"16G\"}", 150},
    {"尾部有非空白垃圾", 1, "{\"v\":2,\"seq\":9,\"ts\":1762400000,\"cpu\":1,\"gpu\":1,\"ram\":1,\"rx\":1,\"tx\":1,\"cpu_label\":\"CPU\",\"gpu_label\":\"GPU\",\"ram_label\":\"16G\"}XYZ", 126},
    {"seq 为负数", 4, "{\"v\": 2, \"seq\": -3, \"ts\": 1762400000, \"cpu\": 1, \"gpu\": 1, \"ram\": 1, \"rx\": 1, \"tx\": 1, \"cpu_label\": \"CPU\", \"gpu_label\": \"GPU\", \"ram_label\": \"16G\"}", 145},
};

static const unsigned int kUdpPacketCaseCount =
    sizeof(kUdpPacketCases) / sizeof(kUdpPacketCases[0]);

#endif /* PICO_FREERTOS_FIRMWARE_TESTS_GENERATED_UDP_PACKETS_DATA_H_ */
