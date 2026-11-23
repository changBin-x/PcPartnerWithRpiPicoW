#ifndef PICO_FREERTOS_FIRMWARE_INCLUDE_SYSTEM_STATE_H_
#define PICO_FREERTOS_FIRMWARE_INCLUDE_SYSTEM_STATE_H_

#include <stdbool.h>
#include <stdint.h>

#include "ui_theme_id.h"

typedef enum { kUiModeDashboard = 0, kUiModeClock = 1 } UiMode;

/** UDP v2 身份标签最大长度（不含结尾 NUL）。 */
#define kPcMetricsLabelMax 16

typedef struct {
  uint32_t seq;
  uint32_t ts;
  float cpu;
  float gpu;
  bool gpu_valid;
  float ram;
  float rx_mbps;
  float tx_mbps;
  char cpu_label[kPcMetricsLabelMax + 1];
  char gpu_label[kPcMetricsLabelMax + 1];
  char ram_label[kPcMetricsLabelMax + 1];
} PcMetricsData;

typedef struct {
  int year;
  int month;
  int day;
  int hour;
  int minute;
  int second;
  int weekday; /* 0 = Sunday .. 6 = Saturday */
  bool time_synced;
} ClockData;

typedef struct {
  char gz_year[16];  /* 干支年, e.g. "甲辰年" */
  char lunar_md[16]; /* 农历月日, e.g. "九月十五" */
  bool valid;
} LunarData;

/** 每秒指标历史槽数（与 1Hz UDP 指标包节奏对齐，约 24 秒趋势）。 */
#define kMetricHistorySlots 24
/** 历史槽未写入时的占位值（区别于合法的 0%）。 */
#define kMetricHistoryInvalid 0xFFu

typedef struct {
  char city[32];
  char weather[32];
  int temperature;
  bool is_stale;
  bool valid;
} WeatherData;

typedef struct {
  UiMode ui_mode;
  UiThemeId theme_id; /**< 当前主题，渲染层据此选择调色板。 */
  bool pc_online;
  bool wifi_connected;
  PcMetricsData metrics;
  ClockData clock;
  LunarData lunar;
  WeatherData weather;
  uint32_t last_pc_metric_tick;
  /** 每秒一次的 CPU 历史环形缓冲（0..100，或 kMetricHistoryInvalid）。
   *  由 system_state 按秒推进，供示波流波形等需要趋势的界面消费。 */
  uint8_t cpu_history[kMetricHistorySlots];
  uint8_t ram_history[kMetricHistorySlots];
  /** 年 0..4095，月 1..12，日 1..31（公历）。 */
  uint16_t history_year;
  uint8_t history_month;
  uint8_t history_day;
  /** ring_next：下一个写入槽位（0..kMetricHistorySlots-1）。 */
  uint8_t history_next;
  bool history_valid;
} SystemSnapshot;

void system_state_init(void);
void system_state_update_metrics(const PcMetricsData* metrics,
                                 uint32_t current_tick);
void system_state_update_clock(const ClockData* clock);
void system_state_update_lunar(const LunarData* lunar);
void system_state_update_weather(const WeatherData* weather);
void system_state_set_wifi_status(bool connected);
/** @brief 写入当前主题；越界值夹紧到 kThemeSwiss，写入受锁保护。 */
void system_state_set_theme(UiThemeId theme_id);
void system_state_check_timeouts(uint32_t current_tick);
void system_state_get_snapshot(SystemSnapshot* out_snapshot);

#endif /* PICO_FREERTOS_FIRMWARE_INCLUDE_SYSTEM_STATE_H_ */
