/*
 * Strict parser for the UDP metrics JSON protocol (protocol version 1).
 *
 * Expected payload (all 8 fields mandatory, "gpu" may be null):
 *   {"v":1,"seq":1,"ts":1700000000,"cpu":25.4,"gpu":48.0,"ram":65.2,"rx":10.5,"tx":2.1}
 *
 * The parser is intentionally strict: unknown versions, missing fields,
 * out-of-range values and non-monotonic sequence numbers are rejected.
 */

#include "metrics_parser.h"

#include <float.h>

#include "cJSON.h"

/* Protocol version understood by this parser. */
#define kMetricsProtocolVersion 1

/* Inclusive float range accepted for percentage-like fields. */
#define kMetricsPercentMin 0.0
#define kMetricsPercentMax 100.0

/* Inclusive upper bound of uint32_t, as a double. */
#define kMetricsUint32MaxD 4294967295.0

/* Return true when `value` is inside [min, max]; false also for NaN. */
static bool value_in_range(double value, double min, double max) {
  return (value >= min) && (value <= max);
}

/*
 * Read a strictly non-negative integer JSON value into a uint32_t.
 * Rejects missing items, non-numbers, fractional values and values that
 * do not fit into uint32_t (so the double -> uint32_t conversion is safe).
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

  /* Integer fields must not silently truncate, e.g. seq 1.9 -> 1. */
  if ((double)(uint32_t)value != value) {
    return false;
  }

  *out_value = (uint32_t)value;
  return true;
}

/* Read a required number into a float when it lies inside [0, 100]. */
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
 * Read a required non-negative number (bit rate) into a float.
 * FLT_MAX keeps the float conversion from overflowing to +infinity and
 * also rejects NaN.
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
  PcMetricsData parsed = {0};
  MetricsParseResult result = kMetricsParseOk;
  uint32_t seq = 0U;
  uint32_t ts = 0U;

  if ((parser == NULL) || (buffer == NULL) || (out_data == NULL) ||
      (length == 0U)) {
    return kMetricsErrInvalidJson;
  }

  root = cJSON_ParseWithLength(buffer, length);
  if ((root == NULL) || !cJSON_IsObject(root)) {
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

  /* All eight protocol v1 fields must be present (gpu may be JSON null). */
  if ((item_v == NULL) || (item_seq == NULL) || (item_ts == NULL) ||
      (item_cpu == NULL) || (item_gpu == NULL) || (item_ram == NULL) ||
      (item_rx == NULL) || (item_tx == NULL)) {
    result = kMetricsErrMissingField;
    goto cleanup;
  }

  /* v must be exactly the supported protocol version. */
  if (!cJSON_IsNumber(item_v) ||
      (cJSON_GetNumberValue(item_v) != (double)kMetricsProtocolVersion)) {
    result = kMetricsErrInvalidVersion;
    goto cleanup;
  }

  if (!read_uint32(item_seq, &seq)) {
    result = kMetricsErrInvalidValue;
    goto cleanup;
  }

  /* Drop duplicate/out-of-order datagrams before touching any other field. */
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

  /* Publish only after the whole datagram validated. */
  *out_data = parsed;
  parser->last_seq = seq;
  parser->has_last_seq = true;
  result = kMetricsParseOk;

cleanup:
  cJSON_Delete(root);
  return result;
}
