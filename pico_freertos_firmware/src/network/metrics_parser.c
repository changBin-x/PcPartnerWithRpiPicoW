/**
 * @file metrics_parser.c
 * @brief UDP 指标 JSON 协议（协议版本 1）的严格校验解析器。
 *
 * 期望载荷（8 个字段全部必填，"gpu" 允许为 null）：
 *   {"v":1,"seq":1,"ts":1700000000,"cpu":25.4,
 *    "gpu":48.0,"ram":65.2,"rx":10.5,"tx":2.1}
 *
 * 解析策略刻意从严：协议版本未知、字段缺失、数值越界、序号非递增，
 * 或报文尾部夹带非空白字节的报文一律拒绝。
 */

#include "metrics_parser.h"

#include <float.h>

#include "cJSON.h"

/* 本解析器支持的协议版本。 */
#define kMetricsProtocolVersion 1

/* 百分比类字段可接受的闭区间浮点范围。 */
#define kMetricsPercentMin 0.0
#define kMetricsPercentMax 100.0

/* uint32_t 的闭区间上界，以 double 表示。 */
#define kMetricsUint32MaxD 4294967295.0

/* value 落在 [min, max] 内返回 true；NaN 也返回 false。 */
static bool value_in_range(double value, double min, double max) {
  return (value >= min) && (value <= max);
}

/*
 * 判断 JSON 值之后的剩余字节是否只包含空白字符。
 * RFC 8259 允许忽略的空白只有空格、制表符、回车和换行，
 * 其余字节（包括 NUL 和任意二进制垃圾）一律视为多余内容。
 */
static bool tail_is_whitespace_only(const char* tail, const char* limit) {
  while (tail < limit) {
    const char c = *tail;
    if ((c != ' ') && (c != '\t') && (c != '\r') && (c != '\n')) {
      return false;
    }
    ++tail;
  }
  return true;
}

/*
 * 把严格非负的整型 JSON 值读入 uint32_t。
 * 拒绝缺失项、非数字、小数值，以及超出 uint32_t 表示范围的值
 * （从而保证 double -> uint32_t 的转换安全）。
 */
static bool read_uint32(const cJSON* item, uint32_t* out_value) {
  double value;

  if (!cJSON_IsNumber(item)) {
    return false;
  }

  value = cJSON_GetNumberValue(item);
  if (!value_in_range(value, 0.0, kMetricsUint32MaxD)) {
    return false;
  }

  /* 整型字段不允许静默截断，例如 seq 1.9 变成 1。 */
  if ((double)(uint32_t)value != value) {
    return false;
  }

  *out_value = (uint32_t)value;
  return true;
}

/* 读取必填数值并写入 float，要求落在 [0, 100] 内。 */
static bool read_percent(const cJSON* item, float* out_value) {
  double value;

  if (!cJSON_IsNumber(item)) {
    return false;
  }

  value = cJSON_GetNumberValue(item);
  if (!value_in_range(value, kMetricsPercentMin, kMetricsPercentMax)) {
    return false;
  }

  *out_value = (float)value;
  return true;
}

/*
 * 读取必填的非负数值（bit rate）并写入 float。
 * 以 FLT_MAX 为上界，避免 float 转换溢出成 +infinity，同时拒绝 NaN。
 */
static bool read_mbps(const cJSON* item, float* out_value) {
  double value;

  if (!cJSON_IsNumber(item)) {
    return false;
  }

  value = cJSON_GetNumberValue(item);
  if (!value_in_range(value, 0.0, (double)FLT_MAX)) {
    return false;
  }

  *out_value = (float)value;
  return true;
}

void metrics_parser_init(MetricsParser* parser) {
  if (parser == NULL) {
    return;
  }

  parser->last_seq = 0U;
  parser->has_last_seq = false;
}

MetricsParseResult metrics_parser_parse(MetricsParser* parser,
                                        const char* buffer, size_t length,
                                        PcMetricsData* out_data) {
  cJSON* root = NULL;
  cJSON* item_v = NULL;
  cJSON* item_seq = NULL;
  cJSON* item_ts = NULL;
  cJSON* item_cpu = NULL;
  cJSON* item_gpu = NULL;
  cJSON* item_ram = NULL;
  cJSON* item_rx = NULL;
  cJSON* item_tx = NULL;
  const char* parse_end = NULL;
  PcMetricsData parsed = {0};
  MetricsParseResult result = kMetricsParseOk;
  uint32_t seq = 0U;
  uint32_t ts = 0U;

  if ((parser == NULL) || (buffer == NULL) || (out_data == NULL) ||
      (length == 0U)) {
    return kMetricsErrInvalidJson;
  }

  root = cJSON_ParseWithLengthOpts(buffer, length, &parse_end, 0);
  if ((root == NULL) || !cJSON_IsObject(root) || (parse_end == NULL) ||
      !tail_is_whitespace_only(parse_end, buffer + length)) {
    result = kMetricsErrInvalidJson;
    goto cleanup;
  }

  item_v = cJSON_GetObjectItemCaseSensitive(root, "v");
  item_seq = cJSON_GetObjectItemCaseSensitive(root, "seq");
  item_ts = cJSON_GetObjectItemCaseSensitive(root, "ts");
  item_cpu = cJSON_GetObjectItemCaseSensitive(root, "cpu");
  item_gpu = cJSON_GetObjectItemCaseSensitive(root, "gpu");
  item_ram = cJSON_GetObjectItemCaseSensitive(root, "ram");
  item_rx = cJSON_GetObjectItemCaseSensitive(root, "rx");
  item_tx = cJSON_GetObjectItemCaseSensitive(root, "tx");

  /* 协议 v1 的 8 个字段必须齐全（gpu 允许为 JSON null）。 */
  if ((item_v == NULL) || (item_seq == NULL) || (item_ts == NULL) ||
      (item_cpu == NULL) || (item_gpu == NULL) || (item_ram == NULL) ||
      (item_rx == NULL) || (item_tx == NULL)) {
    result = kMetricsErrMissingField;
    goto cleanup;
  }

  /* v 必须精确等于受支持的协议版本。 */
  if (!cJSON_IsNumber(item_v) ||
      (cJSON_GetNumberValue(item_v) != (double)kMetricsProtocolVersion)) {
    result = kMetricsErrInvalidVersion;
    goto cleanup;
  }

  if (!read_uint32(item_seq, &seq)) {
    result = kMetricsErrInvalidValue;
    goto cleanup;
  }

  /* 在读取其他任何字段之前，先丢弃重复或乱序到达的报文。 */
  if (parser->has_last_seq && (seq <= parser->last_seq)) {
    result = kMetricsErrStaleSeq;
    goto cleanup;
  }

  if (!read_uint32(item_ts, &ts) || (ts == 0U)) {
    result = kMetricsErrInvalidValue;
    goto cleanup;
  }

  if (!read_percent(item_cpu, &parsed.cpu)) {
    result = kMetricsErrInvalidValue;
    goto cleanup;
  }

  if (cJSON_IsNull(item_gpu)) {
    parsed.gpu = 0.0f;
    parsed.gpu_valid = false;
  } else {
    if (!read_percent(item_gpu, &parsed.gpu)) {
      result = kMetricsErrInvalidValue;
      goto cleanup;
    }
    parsed.gpu_valid = true;
  }

  if (!read_percent(item_ram, &parsed.ram)) {
    result = kMetricsErrInvalidValue;
    goto cleanup;
  }

  if (!read_mbps(item_rx, &parsed.rx_mbps)) {
    result = kMetricsErrInvalidValue;
    goto cleanup;
  }

  if (!read_mbps(item_tx, &parsed.tx_mbps)) {
    result = kMetricsErrInvalidValue;
    goto cleanup;
  }

  parsed.seq = seq;
  parsed.ts = ts;

  /* 整条报文校验通过后才对外发布结果。 */
  *out_data = parsed;
  parser->last_seq = seq;
  parser->has_last_seq = true;
  result = kMetricsParseOk;

cleanup:
  cJSON_Delete(root);
  return result;
}
