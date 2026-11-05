/*
 * Host unit tests for the strict UDP metrics JSON parser (protocol v1).
 *
 * The parser is stateful (it remembers the last accepted seq), therefore the
 * test cases below run in the documented order and every failing case also
 * asserts that the parser state did not advance.
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

  /* 1. Normal datagram: every field parsed and validated. */
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

  /* 2. GPU unavailable: JSON null is accepted as gpu_valid == false. */
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

  /* 3. Required field missing: three of the eight fields are absent. */
  {
    const char* json = "{\"v\":1,\"seq\":3,\"ts\":1700000002,\"cpu\":25.4}";

    result = parse_cstr(&parser, json, &data);
    assert(result == kMetricsErrMissingField);
    assert(parser.last_seq == 2U);

    /* "gpu" being absent is a missing field, not "GPU unavailable". */
    json =
        "{\"v\":1,\"seq\":3,\"ts\":1700000002,\"cpu\":25.4,"
        "\"ram\":65.2,\"rx\":10.5,\"tx\":2.1}";
    result = parse_cstr(&parser, json, &data);
    assert(result == kMetricsErrMissingField);
  }

  /* 4. Unsupported protocol version. */
  {
    const char* json =
        "{\"v\":2,\"seq\":4,\"ts\":1700000003,\"cpu\":25.4,"
        "\"gpu\":40.0,\"ram\":65.2,\"rx\":10.5,\"tx\":2.1}";

    result = parse_cstr(&parser, json, &data);
    assert(result == kMetricsErrInvalidVersion);
    assert(parser.last_seq == 2U);
  }

  /* 5. Sequence regression: seq 1 is older than the last accepted seq 2. */
  {
    const char* json =
        "{\"v\":1,\"seq\":1,\"ts\":1700000004,\"cpu\":25.4,"
        "\"gpu\":40.0,\"ram\":65.2,\"rx\":10.5,\"tx\":2.1}";

    result = parse_cstr(&parser, json, &data);
    assert(result == kMetricsErrStaleSeq);
    assert(parser.last_seq == 2U);

    /* Duplicate sequence number is stale as well. */
    json =
        "{\"v\":1,\"seq\":2,\"ts\":1700000005,\"cpu\":25.4,"
        "\"gpu\":40.0,\"ram\":65.2,\"rx\":10.5,\"tx\":2.1}";
    result = parse_cstr(&parser, json, &data);
    assert(result == kMetricsErrStaleSeq);
    assert(parser.last_seq == 2U);
  }

  /* 6. Extra: malformed JSON and non-object roots. */
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

  /* 7. Extra: out-of-range and wrong-typed values. */
  {
    const char* json =
        "{\"v\":1,\"seq\":3,\"ts\":1700000006,\"cpu\":150,"
        "\"gpu\":40.0,\"ram\":65.2,\"rx\":10.5,\"tx\":2.1}";

    result = parse_cstr(&parser, json, &data);
    assert(result == kMetricsErrInvalidValue);

    /* gpu must be a number or null, never a string. */
    json =
        "{\"v\":1,\"seq\":3,\"ts\":1700000006,\"cpu\":25.4,"
        "\"gpu\":\"hot\",\"ram\":65.2,\"rx\":10.5,\"tx\":2.1}";
    result = parse_cstr(&parser, json, &data);
    assert(result == kMetricsErrInvalidValue);

    /* A negative transfer rate is not a valid number for rx. */
    json =
        "{\"v\":1,\"seq\":3,\"ts\":1700000006,\"cpu\":25.4,"
        "\"gpu\":40.0,\"ram\":65.2,\"rx\":-1.0,\"tx\":2.1}";
    result = parse_cstr(&parser, json, &data);
    assert(result == kMetricsErrInvalidValue);

    /* Timestamp 0 is not a valid wall clock value. */
    json =
        "{\"v\":1,\"seq\":3,\"ts\":0,\"cpu\":25.4,"
        "\"gpu\":40.0,\"ram\":65.2,\"rx\":10.5,\"tx\":2.1}";
    result = parse_cstr(&parser, json, &data);
    assert(result == kMetricsErrInvalidValue);

    /* v must be a number, not the string "1". */
    json =
        "{\"v\":\"1\",\"seq\":3,\"ts\":1700000006,\"cpu\":25.4,"
        "\"gpu\":40.0,\"ram\":65.2,\"rx\":10.5,\"tx\":2.1}";
    result = parse_cstr(&parser, json, &data);
    assert(result == kMetricsErrInvalidVersion);

    /* Fractional seq must not be silently truncated. */
    json =
        "{\"v\":1,\"seq\":3.5,\"ts\":1700000006,\"cpu\":25.4,"
        "\"gpu\":40.0,\"ram\":65.2,\"rx\":10.5,\"tx\":2.1}";
    result = parse_cstr(&parser, json, &data);
    assert(result == kMetricsErrInvalidValue);

    assert(parser.last_seq == 2U);
  }

  /* 8. Extra: NULL/invalid arguments must be rejected without crashing. */
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

    /* metrics_parser_init(NULL) is a safe no-op. */
    metrics_parser_init(NULL);
    assert(parser.last_seq == 2U);
  }

  printf("PASS: test_metrics_parser\n");
  return 0;
}
