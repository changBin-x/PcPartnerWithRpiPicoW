/**
 * @file test_system_state.c
 * @brief system_state.c 的主机单元测试（pthread 后端）。
 */

#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "system_state.h"

static void test_init_defaults(void)
{
    system_snapshot_t snap;

    system_state_init();
    system_state_get_snapshot(&snap);

    assert(snap.ui_mode == UI_MODE_CLOCK);
    assert(snap.pc_online == false);
    assert(snap.wifi_connected == false);
    assert(snap.metrics.seq == 0);
    assert(snap.last_pc_metric_tick == 0);
    assert(snap.clock.time_synced == false);
    assert(snap.lunar.valid == false);
    assert(snap.weather.valid == false);
}

static void test_update_metrics_switches_to_dashboard(void)
{
    pc_metrics_data_t metrics;
    system_snapshot_t snap;

    memset(&metrics, 0, sizeof(metrics));
    metrics.seq = 101;
    metrics.ts = 1700000000u;
    metrics.cpu = 45.5f;
    metrics.gpu = 60.0f;
    metrics.gpu_valid = true;
    metrics.ram = 72.3f;
    metrics.rx_mbps = 1.25f;
    metrics.tx_mbps = 0.45f;

    system_state_update_metrics(&metrics, 1000);

    system_state_get_snapshot(&snap);
    assert(snap.pc_online == true);
    assert(snap.ui_mode == UI_MODE_DASHBOARD);
    assert(snap.metrics.cpu == 45.5f);
    assert(snap.metrics.gpu == 60.0f);
    assert(snap.metrics.gpu_valid == true);
    assert(snap.metrics.ram == 72.3f);
    assert(snap.metrics.rx_mbps == 1.25f);
    assert(snap.metrics.tx_mbps == 0.45f);
    assert(snap.metrics.seq == 101);
    assert(snap.metrics.ts == 1700000000u);
    assert(snap.last_pc_metric_tick == 1000);
}

static void test_timeout_does_not_fire_before_threshold(void)
{
    system_snapshot_t snap;

    /* 4001 - 1000 = 3001 > 3000 才超时；先验证 3000 - 1000 = 2000 不切换。 */
    system_state_check_timeouts(3000);

    system_state_get_snapshot(&snap);
    assert(snap.pc_online == true);
    assert(snap.ui_mode == UI_MODE_DASHBOARD);
}

static void test_timeout_falls_back_to_clock(void)
{
    system_snapshot_t snap;

    system_state_check_timeouts(4001); /* 4001 - 1000 = 3001 > 3000 */

    system_state_get_snapshot(&snap);
    assert(snap.pc_online == false);
    assert(snap.ui_mode == UI_MODE_CLOCK);
    /* 指标数据本身保留，便于 UI 显示"最后值"灰显。 */
    assert(snap.metrics.cpu == 45.5f);

    /* 再次检查不应产生副作用。 */
    system_state_check_timeouts(9000);
    system_state_get_snapshot(&snap);
    assert(snap.pc_online == false);
    assert(snap.ui_mode == UI_MODE_CLOCK);

    /* 新指标到达后恢复仪表盘。 */
    system_state_update_metrics(&snap.metrics, 5000);
    system_state_get_snapshot(&snap);
    assert(snap.pc_online == true);
    assert(snap.ui_mode == UI_MODE_DASHBOARD);
}

static void test_update_clock_lunar_weather(void)
{
    clock_data_t clock;
    lunar_data_t lunar;
    weather_data_t weather;
    system_snapshot_t snap;

    memset(&clock, 0, sizeof(clock));
    clock.year = 2024;
    clock.month = 10;
    clock.day = 17;
    clock.hour = 15;
    clock.minute = 30;
    clock.second = 45;
    clock.weekday = 4;
    clock.time_synced = true;

    memset(&lunar, 0, sizeof(lunar));
    strcpy(lunar.gz_year, "甲辰年");
    strcpy(lunar.lunar_md, "九月十五");
    lunar.valid = true;

    memset(&weather, 0, sizeof(weather));
    strcpy(weather.city, "上海");
    strcpy(weather.weather, "晴");
    weather.temperature = 25;
    weather.is_stale = false;
    weather.valid = true;

    system_state_update_clock(&clock);
    system_state_update_lunar(&lunar);
    system_state_update_weather(&weather);

    system_state_get_snapshot(&snap);
    assert(snap.clock.year == 2024);
    assert(snap.clock.month == 10);
    assert(snap.clock.day == 17);
    assert(snap.clock.hour == 15);
    assert(snap.clock.minute == 30);
    assert(snap.clock.second == 45);
    assert(snap.clock.weekday == 4);
    assert(snap.clock.time_synced == true);
    assert(strcmp(snap.lunar.gz_year, "甲辰年") == 0);
    assert(strcmp(snap.lunar.lunar_md, "九月十五") == 0);
    assert(snap.lunar.valid == true);
    assert(strcmp(snap.weather.city, "上海") == 0);
    assert(strcmp(snap.weather.weather, "晴") == 0);
    assert(snap.weather.temperature == 25);
    assert(snap.weather.is_stale == false);
    assert(snap.weather.valid == true);
}

static void test_wifi_status(void)
{
    system_snapshot_t snap;

    system_state_set_wifi_status(true);
    system_state_get_snapshot(&snap);
    assert(snap.wifi_connected == true);

    system_state_set_wifi_status(false);
    system_state_get_snapshot(&snap);
    assert(snap.wifi_connected == false);
}

static void test_null_arguments_are_safe(void)
{
    /* 任何 NULL 入参都必须安全返回，不得崩溃。 */
    system_state_update_metrics(NULL, 0);
    system_state_update_clock(NULL);
    system_state_update_lunar(NULL);
    system_state_update_weather(NULL);
    system_state_get_snapshot(NULL);

    printf("PASS: NULL 参数安全性检查\n");
}

int main(void)
{
    test_init_defaults();
    test_update_metrics_switches_to_dashboard();
    test_timeout_does_not_fire_before_threshold();
    test_timeout_falls_back_to_clock();
    test_update_clock_lunar_weather();
    test_wifi_status();
    test_null_arguments_are_safe();

    printf("PASS: test_system_state\n");
    return 0;
}
