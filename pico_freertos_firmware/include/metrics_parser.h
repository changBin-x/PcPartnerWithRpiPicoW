#ifndef PICO_FREERTOS_FIRMWARE_INCLUDE_METRICS_PARSER_H_
#define PICO_FREERTOS_FIRMWARE_INCLUDE_METRICS_PARSER_H_

#include "system_state.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* metrics_parser_parse 的返回码。 */
typedef enum {
  kMetricsParseOk = 0,       /* 解析成功。 */
  kMetricsErrInvalidJson,    /* JSON 非法或根节点不是对象。 */
  kMetricsErrMissingField,   /* 协议 v1 的必填字段缺失。 */
  kMetricsErrInvalidVersion, /* 协议版本不受支持。 */
  kMetricsErrInvalidValue,   /* 字段类型或取值不合法。 */
  kMetricsErrStaleSeq        /* 序号重复或回退。 */
} MetricsParseResult;

/* 解析器状态：记住最近一次被接受的 seq，用于丢弃过期报文。 */
typedef struct {
  uint32_t last_seq; /* 最近一次被接受的 seq。 */
  bool has_last_seq; /* 是否已接受过报文。 */
} MetricsParser;

/* 重置解析器状态；parser 为 NULL 时是安全的空操作。 */
void metrics_parser_init(MetricsParser* parser);
/* 解析一整条 UDP 报文：成功时把结果写入 out_data 并推进 last_seq；
 * 失败时 out_data 与解析器状态均保持不变。 */
MetricsParseResult metrics_parser_parse(MetricsParser* parser,
                                        const char* buffer, size_t length,
                                        PcMetricsData* out_data);

#endif /* PICO_FREERTOS_FIRMWARE_INCLUDE_METRICS_PARSER_H_ */
