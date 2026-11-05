/**
 * @file test_metrics_parser.c
 * @brief UDP 指标 JSON 严格解析器（协议 v1）的主机端单元测试。
 *
 * 解析器是有状态的（会记住最近一次被接受的 seq），因此下面的用例按文档
 * 顺序执行，且每个失败用例还会断言解析器状态没有前进。
 */

#include "metrics_parser.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

#define APPROX_EQ(a, b) ((((a) - (b)) < 0.0001f) && (((b) - (a)) < 0.0001f))

static MetricsParseResult parse_cstr(MetricsParser* parser, const char* json,
                                     PcMetricsData* out_data) {
  return metrics_parser_parse(parser, json, strlen(json), out_data);
}

int main(void) {
  MetricsParser parser;
  PcMetricsData data;
  MetricsParseResult result;

  metrics_parser_init(&parser);
  assert(parser.has_last_seq == false);
  assert(parser.last_seq == 0U);

  /* 1. 正常报文：8 个字段全部解析并通过校验。 */
  {
    const char* json =
        "{\"v\":1,\"seq\":1,\"ts\":1700000000,\"cpu\":25.4,"
        "\"gpu\":48.0,\"ram\":65.2,\"rx\":10.5,\"tx\":2.1}";

    result = parse_cstr(&parser, json, &data);
    assert(result == kMetricsParseOk);
    assert(data.seq == 1U);
    assert(data.ts == 1700000000U);
    assert(APPROX_EQ(data.cpu, 25.4f));
    assert(APPROX_EQ(data.gpu, 48.0f));
    assert(data.gpu_valid == true);
    assert(APPROX_EQ(data.ram, 65.2f));
    assert(APPROX_EQ(data.rx_mbps, 10.5f));
    assert(APPROX_EQ(data.tx_mbps, 2.1f));
    assert(parser.has_last_seq == true);
    assert(parser.last_seq == 1U);
  }

  /* 2. GPU 不可用：JSON null 被接受，gpu_valid 置为 false。 */
  {
    const char* json =
        "{\"v\":1,\"seq\":2,\"ts\":1700000001,\"cpu\":30.0,"
        "\"gpu\":null,\"ram\":70.0,\"rx\":12.0,\"tx\":3.0}";

    result = parse_cstr(&parser, json, &data);
    assert(result == kMetricsParseOk);
    assert(data.seq == 2U);
    assert(data.gpu_valid == false);
    assert(data.gpu == 0.0f);
    assert(APPROX_EQ(data.cpu, 30.0f));
    assert(parser.last_seq == 2U);
  }

  /* 3. 必填字段缺失：8 个字段里缺了 4 个。 */
  {
    const char* json = "{\"v\":1,\"seq\":3,\"ts\":1700000002,\"cpu\":25.4}";

    result = parse_cstr(&parser, json, &data);
    assert(result == kMetricsErrMissingField);
    assert(parser.last_seq == 2U);

    /* "gpu" 整个缺失属于缺字段，不能当成 "GPU 不可用"。 */
    json =
        "{\"v\":1,\"seq\":3,\"ts\":1700000002,\"cpu\":25.4,"
        "\"ram\":65.2,\"rx\":10.5,\"tx\":2.1}";
    result = parse_cstr(&parser, json, &data);
    assert(result == kMetricsErrMissingField);
  }

  /* 4. 不支持的协议版本。 */
  {
    const char* json =
        "{\"v\":2,\"seq\":4,\"ts\":1700000003,\"cpu\":25.4,"
        "\"gpu\":40.0,\"ram\":65.2,\"rx\":10.5,\"tx\":2.1}";

    result = parse_cstr(&parser, json, &data);
    assert(result == kMetricsErrInvalidVersion);
    assert(parser.last_seq == 2U);
  }

  /* 5. 序号回退：seq 1 早于最近接受过的 seq 2。 */
  {
    const char* json =
        "{\"v\":1,\"seq\":1,\"ts\":1700000004,\"cpu\":25.4,"
        "\"gpu\":40.0,\"ram\":65.2,\"rx\":10.5,\"tx\":2.1}";

    result = parse_cstr(&parser, json, &data);
    assert(result == kMetricsErrStaleSeq);
    assert(parser.last_seq == 2U);

    /* 重复的序号同样视为过期。 */
    json =
        "{\"v\":1,\"seq\":2,\"ts\":1700000005,\"cpu\":25.4,"
        "\"gpu\":40.0,\"ram\":65.2,\"rx\":10.5,\"tx\":2.1}";
    result = parse_cstr(&parser, json, &data);
    assert(result == kMetricsErrStaleSeq);
    assert(parser.last_seq == 2U);
  }

  /* 6. 补充用例：畸形 JSON 与非对象根节点。 */
  {
    result = parse_cstr(&parser, "not json", &data);
    assert(result == kMetricsErrInvalidJson);

    result = parse_cstr(&parser, "{\"v\":1,", &data);
    assert(result == kMetricsErrInvalidJson);

    result = parse_cstr(&parser, "[1,2,3]", &data);
    assert(result == kMetricsErrInvalidJson);

    result = parse_cstr(&parser, "", &data);
    assert(result == kMetricsErrInvalidJson);
  }

  /* 7. 补充用例：越界取值与类型错误。 */
  {
    const char* json =
        "{\"v\":1,\"seq\":3,\"ts\":1700000006,\"cpu\":150,"
        "\"gpu\":40.0,\"ram\":65.2,\"rx\":10.5,\"tx\":2.1}";

    result = parse_cstr(&parser, json, &data);
    assert(result == kMetricsErrInvalidValue);

    /* gpu 只能是数字或 null，不能是字符串。 */
    json =
        "{\"v\":1,\"seq\":3,\"ts\":1700000006,\"cpu\":25.4,"
        "\"gpu\":\"hot\",\"ram\":65.2,\"rx\":10.5,\"tx\":2.1}";
    result = parse_cstr(&parser, json, &data);
    assert(result == kMetricsErrInvalidValue);

    /* rx 不接受负数形式的传输速率。 */
    json =
        "{\"v\":1,\"seq\":3,\"ts\":1700000006,\"cpu\":25.4,"
        "\"gpu\":40.0,\"ram\":65.2,\"rx\":-1.0,\"tx\":2.1}";
    result = parse_cstr(&parser, json, &data);
    assert(result == kMetricsErrInvalidValue);

    /* 时间戳 0 不是合法的实际时间取值。 */
    json =
        "{\"v\":1,\"seq\":3,\"ts\":0,\"cpu\":25.4,"
        "\"gpu\":40.0,\"ram\":65.2,\"rx\":10.5,\"tx\":2.1}";
    result = parse_cstr(&parser, json, &data);
    assert(result == kMetricsErrInvalidValue);

    /* v 必须是数字，不能是字符串 "1"。 */
    json =
        "{\"v\":\"1\",\"seq\":3,\"ts\":1700000006,\"cpu\":25.4,"
        "\"gpu\":40.0,\"ram\":65.2,\"rx\":10.5,\"tx\":2.1}";
    result = parse_cstr(&parser, json, &data);
    assert(result == kMetricsErrInvalidVersion);

    /* 小数形式的 seq 不允许被静默截断。 */
    json =
        "{\"v\":1,\"seq\":3.5,\"ts\":1700000006,\"cpu\":25.4,"
        "\"gpu\":40.0,\"ram\":65.2,\"rx\":10.5,\"tx\":2.1}";
    result = parse_cstr(&parser, json, &data);
    assert(result == kMetricsErrInvalidValue);

    assert(parser.last_seq == 2U);
  }

  /* 8. 补充用例：NULL 或非法入参必须被拒绝且不崩溃。 */
  {
    const char* json =
        "{\"v\":1,\"seq\":3,\"ts\":1700000007,\"cpu\":25.4,"
        "\"gpu\":40.0,\"ram\":65.2,\"rx\":10.5,\"tx\":2.1}";
    size_t len = strlen(json);

    assert(metrics_parser_parse(NULL, json, len, &data) ==
           kMetricsErrInvalidJson);
    assert(metrics_parser_parse(&parser, NULL, len, &data) ==
           kMetricsErrInvalidJson);
    assert(metrics_parser_parse(&parser, json, 0U, &data) ==
           kMetricsErrInvalidJson);
    assert(metrics_parser_parse(&parser, json, len, NULL) ==
           kMetricsErrInvalidJson);

    /* metrics_parser_init(NULL) 是安全的空操作。 */
    metrics_parser_init(NULL);
    assert(parser.last_seq == 2U);
  }

  /* 9. 报文尾部严格校验：只允许空白字符，任何垃圾字节整包拒绝。 */
  {
    const char* json_with_garbage =
        "{\"v\":1,\"seq\":3,\"ts\":1700000008,\"cpu\":25.4,"
        "\"gpu\":40.0,\"ram\":65.2,\"rx\":10.5,\"tx\":2.1}XYZ";
    const char* json_with_whitespace =
        "{\"v\":1,\"seq\":3,\"ts\":1700000008,\"cpu\":25.4,"
        "\"gpu\":40.0,\"ram\":65.2,\"rx\":10.5,\"tx\":2.1}\r\n \t";
    const char* json_with_binary =
        "{\"v\":1,\"seq\":4,\"ts\":1700000009,\"cpu\":25.4,"
        "\"gpu\":40.0,\"ram\":65.2,\"rx\":10.5,\"tx\":2.1}\x01";

    /* 合法 JSON 后跟非空白内容：整包拒绝，且不得推进 last_seq。 */
    result = parse_cstr(&parser, json_with_garbage, &data);
    assert(result == kMetricsErrInvalidJson);
    assert(parser.last_seq == 2U);

    /* 同一报文（seq 3）仅尾部换成空白字符后必须被接受。 */
    result = parse_cstr(&parser, json_with_whitespace, &data);
    assert(result == kMetricsParseOk);
    assert(data.seq == 3U);
    assert(data.ts == 1700000008U);
    assert(APPROX_EQ(data.cpu, 25.4f));
    assert(parser.last_seq == 3U);

    /* 尾部是 NUL 之外的二进制字节时同样拒绝。 */
    result = parse_cstr(&parser, json_with_binary, &data);
    assert(result == kMetricsErrInvalidJson);
    assert(parser.last_seq == 3U);
  }

  printf("PASS: test_metrics_parser\n");
  return 0;
}
