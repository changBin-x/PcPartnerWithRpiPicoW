/**
 * @file test_ui_layout.c
 * @brief 脏矩形（Dirty Region）变更检测引擎的主机端单元测试。
 *
 * 覆盖：模式切换整屏重绘、同模式单字段局部重绘、无变化返回 0、
 * max_rects 截断不越界、时钟页农历/天气/时间命中、NULL 入参兜底，
 * 并对所有产出矩形断言其落在 128x160 屏幕内。
 */

#include "ui_layout.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

/* 矩形逐字段比较，便于断言期望值。 */
static int rect_eq(const ui_rect_t *r, int16_t x, int16_t y, int16_t w, int16_t h)
{
    return r->x == x && r->y == y && r->w == w && r->h == h;
}

/* 所有脏矩形必须完全落在屏幕内，且宽高非负。 */
static void assert_rects_inside_screen(const ui_rect_t *rects, int count)
{
    for (int i = 0; i < count; i++) {
        assert(rects[i].x >= 0);
        assert(rects[i].y >= 0);
        assert(rects[i].w >= 0);
        assert(rects[i].h >= 0);
        assert(rects[i].x + rects[i].w <= UI_SCREEN_WIDTH);
        assert(rects[i].y + rects[i].h <= UI_SCREEN_HEIGHT);
    }
}

/* 构造一个干净的基线快照。 */
static void snapshot_init(system_snapshot_t *snap, ui_mode_t mode)
{
    memset(snap, 0, sizeof(*snap));
    snap->ui_mode = mode;
}

/* ------------------------------------------------------------ NULL / 非法入参 */
static void test_null_and_invalid_args(void)
{
    system_snapshot_t snap;
    ui_rect_t rects[4];

    snapshot_init(&snap, UI_MODE_DASHBOARD);
    memset(rects, 0, sizeof(rects));

    /* 任一指针为 NULL 都返回 0 且不崩溃。 */
    assert(ui_layout_calculate_dirty_regions(NULL, &snap, rects, 4) == 0);
    assert(ui_layout_calculate_dirty_regions(&snap, NULL, rects, 4) == 0);
    assert(ui_layout_calculate_dirty_regions(&snap, &snap, NULL, 4) == 0);
    assert(ui_layout_calculate_dirty_regions(NULL, NULL, NULL, 4) == 0);

    /* max_rects < 1 同样返回 0。 */
    assert(ui_layout_calculate_dirty_regions(&snap, &snap, rects, 0) == 0);
    assert(ui_layout_calculate_dirty_regions(&snap, &snap, rects, -3) == 0);
}

/* ------------------------------------------------------------ 模式切换 */
static void test_mode_switch_full_screen(void)
{
    system_snapshot_t prev;
    system_snapshot_t curr;
    ui_rect_t rects[8];

    /* CLOCK -> DASHBOARD：1 个整屏矩形。 */
    snapshot_init(&prev, UI_MODE_CLOCK);
    snapshot_init(&curr, UI_MODE_DASHBOARD);

    int n = ui_layout_calculate_dirty_regions(&prev, &curr, rects, 8);
    assert(n == 1);
    assert(rect_eq(&rects[0], 0, 0, 128, 160));
    assert_rects_inside_screen(rects, n);

    /* DASHBOARD -> CLOCK：同样整屏。 */
    snapshot_init(&prev, UI_MODE_DASHBOARD);
    snapshot_init(&curr, UI_MODE_CLOCK);

    n = ui_layout_calculate_dirty_regions(&prev, &curr, rects, 8);
    assert(n == 1);
    assert(rect_eq(&rects[0], 0, 0, 128, 160));
    assert_rects_inside_screen(rects, n);
}

/* ------------------------------------------------------------ 仪表盘单字段 */
static void test_dashboard_single_field_changes(void)
{
    system_snapshot_t prev;
    system_snapshot_t curr;
    ui_rect_t rects[8];
    int n;

    /* 仅 CPU 变化 -> 只有 CPU 卡片。 */
    snapshot_init(&prev, UI_MODE_DASHBOARD);
    snapshot_init(&curr, UI_MODE_DASHBOARD);
    prev.metrics.cpu = 10.0f;
    curr.metrics.cpu = 42.5f;

    n = ui_layout_calculate_dirty_regions(&prev, &curr, rects, 8);
    assert(n == 1);
    assert(rect_eq(&rects[0], 4, 20, 120, 32));
    assert_rects_inside_screen(rects, n);

    /* 仅 wifi_connected 变化 -> 顶栏。 */
    snapshot_init(&prev, UI_MODE_DASHBOARD);
    snapshot_init(&curr, UI_MODE_DASHBOARD);
    prev.wifi_connected = false;
    curr.wifi_connected = true;

    n = ui_layout_calculate_dirty_regions(&prev, &curr, rects, 8);
    assert(n == 1);
    assert(rect_eq(&rects[0], 0, 0, 128, 16));
    assert_rects_inside_screen(rects, n);

    /* 仅 pc_online 变化 -> 顶栏。 */
    snapshot_init(&prev, UI_MODE_DASHBOARD);
    snapshot_init(&curr, UI_MODE_DASHBOARD);
    prev.pc_online = true;
    curr.pc_online = false;

    n = ui_layout_calculate_dirty_regions(&prev, &curr, rects, 8);
    assert(n == 1);
    assert(rect_eq(&rects[0], 0, 0, 128, 16));
    assert_rects_inside_screen(rects, n);

    /* 仅 GPU 数值变化 -> GPU 卡片。 */
    snapshot_init(&prev, UI_MODE_DASHBOARD);
    snapshot_init(&curr, UI_MODE_DASHBOARD);
    prev.metrics.gpu_valid = true;
    curr.metrics.gpu_valid = true;
    prev.metrics.gpu = 30.0f;
    curr.metrics.gpu = 0.0f;

    n = ui_layout_calculate_dirty_regions(&prev, &curr, rects, 8);
    assert(n == 1);
    assert(rect_eq(&rects[0], 4, 56, 120, 32));
    assert_rects_inside_screen(rects, n);

    /* 仅 gpu_valid 翻转（数值不变）-> 仍命中 GPU 卡片。 */
    snapshot_init(&prev, UI_MODE_DASHBOARD);
    snapshot_init(&curr, UI_MODE_DASHBOARD);
    prev.metrics.gpu = 5.0f;
    curr.metrics.gpu = 5.0f;
    prev.metrics.gpu_valid = false;
    curr.metrics.gpu_valid = true;

    n = ui_layout_calculate_dirty_regions(&prev, &curr, rects, 8);
    assert(n == 1);
    assert(rect_eq(&rects[0], 4, 56, 120, 32));
    assert_rects_inside_screen(rects, n);

    /* 仅 RAM 变化 -> RAM 卡片。 */
    snapshot_init(&prev, UI_MODE_DASHBOARD);
    snapshot_init(&curr, UI_MODE_DASHBOARD);
    prev.metrics.ram = 50.0f;
    curr.metrics.ram = 51.0f;

    n = ui_layout_calculate_dirty_regions(&prev, &curr, rects, 8);
    assert(n == 1);
    assert(rect_eq(&rects[0], 4, 92, 120, 32));
    assert_rects_inside_screen(rects, n);

    /* 仅 rx_mbps 变化 -> 网速行。 */
    snapshot_init(&prev, UI_MODE_DASHBOARD);
    snapshot_init(&curr, UI_MODE_DASHBOARD);
    prev.metrics.rx_mbps = 1.0f;
    curr.metrics.rx_mbps = 9.5f;

    n = ui_layout_calculate_dirty_regions(&prev, &curr, rects, 8);
    assert(n == 1);
    assert(rect_eq(&rects[0], 4, 128, 120, 28));
    assert_rects_inside_screen(rects, n);

    /* 仅 tx_mbps 变化 -> 网速行。 */
    snapshot_init(&prev, UI_MODE_DASHBOARD);
    snapshot_init(&curr, UI_MODE_DASHBOARD);
    prev.metrics.tx_mbps = 1.0f;
    curr.metrics.tx_mbps = 2.0f;

    n = ui_layout_calculate_dirty_regions(&prev, &curr, rects, 8);
    assert(n == 1);
    assert(rect_eq(&rects[0], 4, 128, 120, 28));
    assert_rects_inside_screen(rects, n);
}

/* 仪表盘全字段同时变化：顺序固定为 顶栏/CPU/GPU/RAM/网速。 */
static void test_dashboard_all_field_changes(void)
{
    system_snapshot_t prev;
    system_snapshot_t curr;
    ui_rect_t rects[8];

    snapshot_init(&prev, UI_MODE_DASHBOARD);
    snapshot_init(&curr, UI_MODE_DASHBOARD);

    prev.wifi_connected = false;
    curr.wifi_connected = true;
    prev.pc_online = false;
    curr.pc_online = true;
    prev.metrics.cpu = 1.0f;
    curr.metrics.cpu = 2.0f;
    prev.metrics.gpu = 3.0f;
    curr.metrics.gpu = 4.0f;
    prev.metrics.gpu_valid = false;
    curr.metrics.gpu_valid = true;
    prev.metrics.ram = 5.0f;
    curr.metrics.ram = 6.0f;
    prev.metrics.rx_mbps = 7.0f;
    curr.metrics.rx_mbps = 8.0f;
    prev.metrics.tx_mbps = 9.0f;
    curr.metrics.tx_mbps = 10.0f;

    int n = ui_layout_calculate_dirty_regions(&prev, &curr, rects, 8);
    assert(n == 5);
    assert(rect_eq(&rects[0], 0, 0, 128, 16));
    assert(rect_eq(&rects[1], 4, 20, 120, 32));
    assert(rect_eq(&rects[2], 4, 56, 120, 32));
    assert(rect_eq(&rects[3], 4, 92, 120, 32));
    assert(rect_eq(&rects[4], 4, 128, 120, 28));
    assert_rects_inside_screen(rects, n);
}

/* ------------------------------------------------------------ 无变化 */
static void test_no_change(void)
{
    system_snapshot_t prev;
    system_snapshot_t curr;
    ui_rect_t rects[8];

    /* 仪表盘完全一致 -> 0。 */
    snapshot_init(&prev, UI_MODE_DASHBOARD);
    snapshot_init(&curr, UI_MODE_DASHBOARD);
    prev.wifi_connected = true;
    curr.wifi_connected = true;
    prev.pc_online = true;
    curr.pc_online = true;
    prev.metrics.cpu = 33.0f;
    curr.metrics.cpu = 33.0f;
    prev.metrics.ram = 44.0f;
    curr.metrics.ram = 44.0f;

    int n = ui_layout_calculate_dirty_regions(&prev, &curr, rects, 8);
    assert(n == 0);

    /* 时钟页完全一致 -> 0。 */
    snapshot_init(&prev, UI_MODE_CLOCK);
    snapshot_init(&curr, UI_MODE_CLOCK);
    prev.clock.hour = 9;
    curr.clock.hour = 9;
    prev.clock.minute = 30;
    curr.clock.minute = 30;
    prev.clock.second = 15;
    curr.clock.second = 15;
    prev.clock.day = 6;
    curr.clock.day = 6;
    strcpy(prev.lunar.lunar_md, "九月十五");
    strcpy(curr.lunar.lunar_md, "九月十五");
    strcpy(prev.weather.weather, "晴");
    strcpy(curr.weather.weather, "晴");
    prev.weather.temperature = 21;
    curr.weather.temperature = 21;

    n = ui_layout_calculate_dirty_regions(&prev, &curr, rects, 8);
    assert(n == 0);
}

/* ------------------------------------------------------------ max_rects 截断 */
static void test_max_rects_truncation(void)
{
    system_snapshot_t prev;
    system_snapshot_t curr;
    ui_rect_t rects[4];

    snapshot_init(&prev, UI_MODE_DASHBOARD);
    snapshot_init(&curr, UI_MODE_DASHBOARD);

    /* 5 项同时变化，但只允许写 1 个矩形。 */
    prev.wifi_connected = false;
    curr.wifi_connected = true;
    prev.metrics.cpu = 1.0f;
    curr.metrics.cpu = 2.0f;
    prev.metrics.gpu = 3.0f;
    curr.metrics.gpu = 4.0f;
    prev.metrics.ram = 5.0f;
    curr.metrics.ram = 6.0f;
    prev.metrics.rx_mbps = 7.0f;
    curr.metrics.rx_mbps = 8.0f;

    /* 哨兵值：截断后必须原样保留，证明没有越界写。 */
    rects[0] = (ui_rect_t){-1, -1, -1, -1};
    rects[1] = (ui_rect_t){-7, -8, -9, -10};

    int n = ui_layout_calculate_dirty_regions(&prev, &curr, rects, 1);
    assert(n == 1);
    assert(rect_eq(&rects[0], 0, 0, 128, 16));
    assert(rect_eq(&rects[1], -7, -8, -9, -10));
    assert_rects_inside_screen(rects, n);

    /* max_rects = 2 且 5 项变化：写满即停，第 3 个槽位必须不被触碰。 */
    rects[2] = (ui_rect_t){-11, -12, -13, -14};
    n = ui_layout_calculate_dirty_regions(&prev, &curr, rects, 2);
    assert(n == 2);
    assert(rect_eq(&rects[0], 0, 0, 128, 16));
    assert(rect_eq(&rects[1], 4, 20, 120, 32));
    assert(rect_eq(&rects[2], -11, -12, -13, -14));
    assert_rects_inside_screen(rects, n);
}

/* ------------------------------------------------------------ 时钟页 */
static void test_clock_time_and_date_changes(void)
{
    system_snapshot_t prev;
    system_snapshot_t curr;
    ui_rect_t rects[8];
    int n;

    /* 秒变化 -> 大号时间区。 */
    snapshot_init(&prev, UI_MODE_CLOCK);
    snapshot_init(&curr, UI_MODE_CLOCK);
    prev.clock.minute = 30;
    curr.clock.minute = 30;
    prev.clock.second = 1;
    curr.clock.second = 2;

    n = ui_layout_calculate_dirty_regions(&prev, &curr, rects, 8);
    assert(n == 1);
    assert(rect_eq(&rects[0], 0, 30, 128, 40));
    assert_rects_inside_screen(rects, n);

    /* 分变化 -> 大号时间区。 */
    snapshot_init(&prev, UI_MODE_CLOCK);
    snapshot_init(&curr, UI_MODE_CLOCK);
    prev.clock.minute = 30;
    curr.clock.minute = 31;
    prev.clock.second = 5;
    curr.clock.second = 5;

    n = ui_layout_calculate_dirty_regions(&prev, &curr, rects, 8);
    assert(n == 1);
    assert(rect_eq(&rects[0], 0, 30, 128, 40));
    assert_rects_inside_screen(rects, n);

    /* 日期变化 -> 日期+农历卡。 */
    snapshot_init(&prev, UI_MODE_CLOCK);
    snapshot_init(&curr, UI_MODE_CLOCK);
    prev.clock.day = 6;
    curr.clock.day = 7;

    n = ui_layout_calculate_dirty_regions(&prev, &curr, rects, 8);
    assert(n == 1);
    assert(rect_eq(&rects[0], 0, 75, 128, 50));
    assert_rects_inside_screen(rects, n);
}

static void test_clock_lunar_and_weather_changes(void)
{
    system_snapshot_t prev;
    system_snapshot_t curr;
    ui_rect_t rects[8];
    int n;

    /* lunar_md 字符串变化 -> 命中日期+农历卡 {0,75,128,50}。 */
    snapshot_init(&prev, UI_MODE_CLOCK);
    snapshot_init(&curr, UI_MODE_CLOCK);
    prev.clock.day = 6;
    curr.clock.day = 6;
    strcpy(prev.lunar.lunar_md, "九月十五");
    strcpy(curr.lunar.lunar_md, "九月十六");

    n = ui_layout_calculate_dirty_regions(&prev, &curr, rects, 8);
    assert(n == 1);
    assert(rect_eq(&rects[0], 0, 75, 128, 50));
    assert_rects_inside_screen(rects, n);

    /* weather.temperature 变化 -> 命中天气行 {0,0,128,25}。 */
    snapshot_init(&prev, UI_MODE_CLOCK);
    snapshot_init(&curr, UI_MODE_CLOCK);
    strcpy(prev.weather.weather, "晴");
    strcpy(curr.weather.weather, "晴");
    prev.weather.temperature = 20;
    curr.weather.temperature = 21;

    n = ui_layout_calculate_dirty_regions(&prev, &curr, rects, 8);
    assert(n == 1);
    assert(rect_eq(&rects[0], 0, 0, 128, 25));
    assert_rects_inside_screen(rects, n);

    /* weather.weather 字符串变化 -> 命中天气行 {0,0,128,25}。 */
    snapshot_init(&prev, UI_MODE_CLOCK);
    snapshot_init(&curr, UI_MODE_CLOCK);
    strcpy(prev.weather.weather, "晴");
    strcpy(curr.weather.weather, "小雨");
    prev.weather.temperature = 20;
    curr.weather.temperature = 20;

    n = ui_layout_calculate_dirty_regions(&prev, &curr, rects, 8);
    assert(n == 1);
    assert(rect_eq(&rects[0], 0, 0, 128, 25));
    assert_rects_inside_screen(rects, n);
}

int main(void)
{
    test_null_and_invalid_args();
    test_mode_switch_full_screen();
    test_dashboard_single_field_changes();
    test_dashboard_all_field_changes();
    test_no_change();
    test_max_rects_truncation();
    test_clock_time_and_date_changes();
    test_clock_lunar_and_weather_changes();

    printf("PASS: test_ui_layout\n");
    return 0;
}
