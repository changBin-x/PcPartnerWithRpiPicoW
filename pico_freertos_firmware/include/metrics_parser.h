#ifndef PICO_FREERTOS_FIRMWARE_INCLUDE_METRICS_PARSER_H_
#define PICO_FREERTOS_FIRMWARE_INCLUDE_METRICS_PARSER_H_

#include "system_state.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    kMetricsParseOk = 0,
    kMetricsErrInvalidJson,
    kMetricsErrMissingField,
    kMetricsErrInvalidVersion,
    kMetricsErrInvalidValue,
    kMetricsErrStaleSeq
} MetricsParseResult;

typedef struct {
    uint32_t last_seq;
    bool has_last_seq;
} MetricsParser;

void metrics_parser_init(MetricsParser *parser);
MetricsParseResult metrics_parser_parse(
    MetricsParser *parser,
    const char *buffer,
    size_t length,
    PcMetricsData *out_data);

#endif /* PICO_FREERTOS_FIRMWARE_INCLUDE_METRICS_PARSER_H_ */
