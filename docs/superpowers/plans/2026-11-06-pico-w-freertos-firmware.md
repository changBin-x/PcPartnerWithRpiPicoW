# Pico W FreeRTOS 双核多任务固件实现计划

> **面向 AI 代理的工作者：** 必需子技能：使用 superpowers:subagent-driven-development（推荐）或 superpowers:executing-plans 逐任务实现此计划。步骤使用复选框（`- [ ]`）语法来跟踪进度。

**目标：** 在 Raspberry Pi Pico W 上使用 pico-sdk + FreeRTOS (SMP) 重构 PC 状态屏固件，实现双核异步解耦运行、UDP 指标实时接收、3 秒无指标自动切换农历时钟与天气、8000ms 硬件看门狗守护，并通过分层测试与自动化构建验证。

**架构：** 双核分工对称多任务架构（SMP）。Core 0 运行 FreeRTOS 调度器负责网络通信（Wi-Fi、lwIP、UDP 8765 监听、NTP 校时、HTTPS 天气与看门狗监控）；Core 1 独占 UI 渲染引擎与 SPI-DMA 控制器；双核间通过原子状态镜像快照（State Snapshot）进行轻量互斥拷贝，彻底消除渲染闪烁与网络握手引发的界面卡顿。

**技术栈：** C99 / C++17、FreeRTOS-Kernel (RP2040 SMP Port)、pico-sdk (v2.x)、lwIP、mbedTLS、cJSON、ST7735S SPI-DMA。

---

## 文件结构与模块规划

```
pico_freertos_firmware/
├── CMakeLists.txt                         # 固件主构建脚本（配置 RP2040、pico-sdk、FreeRTOS SMP）
├── pico_sdk_import.cmake                  # pico-sdk 导入脚本
├── FreeRTOS_Kernel_import.cmake           # FreeRTOS 内核导入脚本
├── include/
│   ├── FreeRTOSConfig.h                   # FreeRTOS SMP 配置（双核、抢占、互斥、堆栈尺寸）
│   ├── system_state.h                     # 全局系统状态镜像快照与线程安全接口
│   ├── metrics_parser.h                   # UDP 8765 JSON 数据包严格校验与解析接口
│   ├── lunar_service.h                    # HKO 农历二进制表查表与干支日期格式化接口
│   ├── font_engine.h                      # ASCII 5x7/16x26 与 Flash 中文点阵字模接口
│   ├── ui_layout.h                        # Material 3 仪表盘与时钟页面布局及脏矩形计算
│   ├── st7735s_driver.h                   # ST7735S 屏幕控制器与 SPI-DMA 传输接口
│   ├── ui_render.h                        # Core 1 UI 渲染主循环接口
│   ├── wifi_manager.h                     # CYW43439 驱动初始化、连接与重连退避接口
│   ├── udp_listener.h                     # UDP 8765 广播包接收任务接口
│   ├── ntp_service.h                      # SNTP 校时与软 RTC 时钟维护接口
│   ├── weather_service.h                  # 高德 HTTPS 实况天气请求与解析接口
│   └── supervisor.h                       # 双核心跳监测与硬件看门狗维护接口
├── src/
│   ├── app/
│   │   ├── main.c                         # 入口函数、外设与双核任务启动
│   │   └── supervisor.c                   # 看门狗与心跳状态机实现
│   ├── state/
│   │   └── system_state.c                 # 状态快照更新与私有副本读取实现
│   ├── network/
│   │   ├── metrics_parser.c               # UDP 指标 JSON 协议校验实现
│   │   ├── wifi_manager.c                 # CYW43 Wi-Fi 状态管理实现
│   │   └── udp_listener.c                 # lwIP UDP 套接字监听实现
│   ├── services/
│   │   ├── lunar_service.c                # 1901-2100 年农历二进制查表实现
│   │   ├── ntp_service.c                  # SNTP 校准实现
│   │   └── weather_service.c              # 高德 Web API HTTPS GET 实现
│   ├── display/
│   │   ├── font_engine.c                  # 字模光栅化渲染实现
│   │   ├── ui_layout.c                    # 界面卡片、进度条与时钟布局实现
│   │   ├── st7735s_driver.c               # ST7735S 寄存器初始化与 DMA 双缓冲发送
│   │   └── ui_render.c                    # Core 1 渲染主循环实现
│   └── assets/
│       ├── font_cn.bin                    # 33KB 中文点阵字库
│       ├── lunar_table.bin                # 219KB 农历逐日数据表
│       └── certs/                         # 高德根证书
└── tests/
    ├── CMakeLists.txt                     # Host 单元测试构建脚本（GCC/Clang 原生编译）
    ├── test_system_state.c                # 状态快照并发读写单元测试
    ├── test_metrics_parser.c              # UDP JSON 数据包严格校验测试
    ├── test_lunar_service.c               # 农历解码与原 Python 结果一致性校验测试
    └── test_ui_layout.c                   # 布局与脏矩形计算测试
```

---

### 任务 1：系统状态快照（System Snapshot）设计与测试

**文件：**
- 创建：`pico_freertos_firmware/include/system_state.h`
- 创建：`pico_freertos_firmware/src/state/system_state.c`
- 测试：`pico_freertos_firmware/tests/test_system_state.c`
- 构建：`pico_freertos_firmware/tests/CMakeLists.txt`

- [ ] **步骤 1：创建 Host 单元测试工程骨架与状态快照测试**

创建 `pico_freertos_firmware/tests/CMakeLists.txt`：
```cmake
cmake_minimum_required(VERSION 3.16)
project(pico_firmware_host_tests C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -Werror -g")

include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
)

# cJSON 源码嵌入
add_library(cjson_lib STATIC ${CMAKE_CURRENT_SOURCE_DIR}/cjson/cJSON.c)
target_include_directories(cjson_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/cjson)

# test_system_state
add_executable(test_system_state
    test_system_state.c
    ../src/state/system_state.c
)
target_link_libraries(test_system_state pthread)

# test_metrics_parser
add_executable(test_metrics_parser
    test_metrics_parser.c
    ../src/network/metrics_parser.c
)
target_link_libraries(test_metrics_parser cjson_lib)

# test_lunar_service
add_executable(test_lunar_service
    test_lunar_service.c
    ../src/services/lunar_service.c
)

# test_ui_layout
add_executable(test_ui_layout
    test_ui_layout.c
    ../src/display/ui_layout.c
)
```

编写 `pico_freertos_firmware/tests/test_system_state.c`：
```c
#include "system_state.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    system_state_init();

    system_snapshot_t snap;
    system_state_get_snapshot(&snap);
    assert(snap.ui_mode == UI_MODE_CLOCK);
    assert(snap.pc_online == false);
    assert(snap.wifi_connected == false);

    // 模拟更新电脑指标
    pc_metrics_data_t metrics = {
        .seq = 101,
        .ts = 1700000000,
        .cpu = 45.5f,
        .gpu = 60.0f,
        .gpu_valid = true,
        .ram = 72.3f,
        .rx_mbps = 1.25f,
        .tx_mbps = 0.45f
    };
    system_state_update_metrics(&metrics, 1000);

    system_state_get_snapshot(&snap);
    assert(snap.pc_online == true);
    assert(snap.ui_mode == UI_MODE_DASHBOARD);
    assert(snap.metrics.cpu == 45.5f);
    assert(snap.metrics.gpu_valid == true);

    // 测试 3000ms 超时自动切换
    system_state_check_timeouts(4001); // 4001 - 1000 = 3001ms > 3000ms
    system_state_get_snapshot(&snap);
    assert(snap.pc_online == false);
    assert(snap.ui_mode == UI_MODE_CLOCK);

    printf("PASS: test_system_state\n");
    return 0;
}
```

- [ ] **步骤 2：运行测试验证失败**

运行：
```bash
mkdir -p pico_freertos_firmware/tests/build && cd pico_freertos_firmware/tests/build && cmake .. && make test_system_state
```
预期：FAIL，报错找不到 `system_state.h` 和相关函数。

- [ ] **步骤 3：实现 `system_state.h` 与 `system_state.c`**

创建 `pico_freertos_firmware/include/system_state.h`：
```c
#ifndef SYSTEM_STATE_H
#define SYSTEM_STATE_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    UI_MODE_DASHBOARD = 0,
    UI_MODE_CLOCK = 1
} ui_mode_t;

typedef struct {
    uint32_t seq;
    uint32_t ts;
    float cpu;
    float gpu;
    bool gpu_valid;
    float ram;
    float rx_mbps;
    float tx_mbps;
} pc_metrics_data_t;

typedef struct {
    int year;
    int month;
    int day;
    int hour;
    int minute;
    int second;
    int weekday; // 0=Sunday..6=Saturday
    bool time_synced;
} clock_data_t;

typedef struct {
    char gz_year[16];   // 干支年，如 "甲辰年"
    char lunar_md[16];  // 农历月日，如 "九月十五"
    bool valid;
} lunar_data_t;

typedef struct {
    char city[32];
    char weather[32];
    int temperature;
    bool is_stale;
    bool valid;
} weather_data_t;

typedef struct {
    ui_mode_t ui_mode;
    bool pc_online;
    bool wifi_connected;
    pc_metrics_data_t metrics;
    clock_data_t clock;
    lunar_data_t lunar;
    weather_data_t weather;
    uint32_t last_pc_metric_tick;
} system_snapshot_t;

void system_state_init(void);
void system_state_update_metrics(const pc_metrics_data_t *metrics, uint32_t current_tick);
void system_state_update_clock(const clock_data_t *clock);
void system_state_update_lunar(const lunar_data_t *lunar);
void system_state_update_weather(const weather_data_t *weather);
void system_state_set_wifi_status(bool connected);
void system_state_check_timeouts(uint32_t current_tick);
void system_state_get_snapshot(system_snapshot_t *out_snapshot);

#endif // SYSTEM_STATE_H
```

创建 `pico_freertos_firmware/src/state/system_state.c`：
```c
#include "system_state.h"
#include <string.h>

#if defined(PICO_ON_DEVICE) && PICO_ON_DEVICE
#include "FreeRTOS.h"
#include "semphr.h"
static SemaphoreHandle_t s_state_mutex = NULL;
#define STATE_LOCK()   xSemaphoreTake(s_state_mutex, portMAX_DELAY)
#define STATE_UNLOCK() xSemaphoreGive(s_state_mutex)
#else
#include <pthread.h>
static pthread_mutex_t s_state_mutex = PTHREAD_MUTEX_INITIALIZER;
#define STATE_LOCK()   pthread_mutex_lock(&s_state_mutex)
#define STATE_UNLOCK() pthread_mutex_unlock(&s_state_mutex)
#endif

static system_snapshot_t s_global_snapshot;

void system_state_init(void) {
#if defined(PICO_ON_DEVICE) && PICO_ON_DEVICE
    if (s_state_mutex == NULL) {
        s_state_mutex = xSemaphoreCreateMutex();
    }
#endif
    STATE_LOCK();
    memset(&s_global_snapshot, 0, sizeof(s_global_snapshot));
    s_global_snapshot.ui_mode = UI_MODE_CLOCK;
    s_global_snapshot.pc_online = false;
    s_global_snapshot.wifi_connected = false;
    STATE_UNLOCK();
}

void system_state_update_metrics(const pc_metrics_data_t *metrics, uint32_t current_tick) {
    if (!metrics) return;
    STATE_LOCK();
    s_global_snapshot.metrics = *metrics;
    s_global_snapshot.last_pc_metric_tick = current_tick;
    s_global_snapshot.pc_online = true;
    s_global_snapshot.ui_mode = UI_MODE_DASHBOARD;
    STATE_UNLOCK();
}

void system_state_update_clock(const clock_data_t *clock) {
    if (!clock) return;
    STATE_LOCK();
    s_global_snapshot.clock = *clock;
    STATE_UNLOCK();
}

void system_state_update_lunar(const lunar_data_t *lunar) {
    if (!lunar) return;
    STATE_LOCK();
    s_global_snapshot.lunar = *lunar;
    STATE_UNLOCK();
}

void system_state_update_weather(const weather_data_t *weather) {
    if (!weather) return;
    STATE_LOCK();
    s_global_snapshot.weather = *weather;
    STATE_UNLOCK();
}

void system_state_set_wifi_status(bool connected) {
    STATE_LOCK();
    s_global_snapshot.wifi_connected = connected;
    STATE_UNLOCK();
}

void system_state_check_timeouts(uint32_t current_tick) {
    STATE_LOCK();
    if (s_global_snapshot.pc_online) {
        if ((current_tick - s_global_snapshot.last_pc_metric_tick) > 3000) {
            s_global_snapshot.pc_online = false;
            s_global_snapshot.ui_mode = UI_MODE_CLOCK;
        }
    }
    STATE_UNLOCK();
}

void system_state_get_snapshot(system_snapshot_t *out_snapshot) {
    if (!out_snapshot) return;
    STATE_LOCK();
    memcpy(out_snapshot, &s_global_snapshot, sizeof(system_snapshot_t));
    STATE_UNLOCK();
}
```

- [ ] **步骤 4：运行测试验证通过**

运行：
```bash
cd pico_freertos_firmware/tests/build && cmake .. && make test_system_state && ./test_system_state
```
预期：PASS: test_system_state

- [ ] **步骤 5：Commit**

```bash
git add pico_freertos_firmware/include/system_state.h pico_freertos_firmware/src/state/system_state.c pico_freertos_firmware/tests/test_system_state.c
git commit -m "feat(state): 实现系统状态镜像快照与线程安全读写"
```

---

### 任务 2：UDP 指标数据包严格校验与 cJSON 解析器

**文件：**
- 创建：`pico_freertos_firmware/include/metrics_parser.h`
- 创建：`pico_freertos_firmware/src/network/metrics_parser.c`
- 创建：`pico_freertos_firmware/tests/cjson/cJSON.h` 与 `cJSON.c`
- 测试：`pico_freertos_firmware/tests/test_metrics_parser.c`

- [ ] **步骤 1：准备 cJSON 库并编写测试用例**

下载/引入精简 cJSON 库到 `pico_freertos_firmware/tests/cjson/`，编写 `pico_freertos_firmware/tests/test_metrics_parser.c`：
```c
#include "metrics_parser.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    metrics_parser_t parser;
    metrics_parser_init(&parser);

    pc_metrics_data_t out;

    // 1. 正常数据包
    const char *valid_json = "{\"v\":1,\"seq\":1,\"ts\":1700000000,\"cpu\":25.4,\"gpu\":48.0,\"ram\":65.2,\"rx\":10.5,\"tx\":2.1}";
    assert(metrics_parser_parse(&parser, valid_json, strlen(valid_json), &out) == METRICS_PARSE_OK);
    assert(out.seq == 1);
    assert(out.ts == 1700000000);
    assert(out.cpu == 25.4f);
    assert(out.gpu_valid == true);
    assert(out.gpu == 48.0f);

    // 2. GPU 为 null 的合法包
    const char *gpu_null_json = "{\"v\":1,\"seq\":2,\"ts\":1700000001,\"cpu\":25.4,\"gpu\":null,\"ram\":65.2,\"rx\":10.5,\"tx\":2.1}";
    assert(metrics_parser_parse(&parser, gpu_null_json, strlen(gpu_null_json), &out) == METRICS_PARSE_OK);
    assert(out.seq == 2);
    assert(out.gpu_valid == false);

    // 3. 字段缺失错误
    const char *missing_field = "{\"v\":1,\"seq\":3,\"ts\":1700000002,\"cpu\":25.4}";
    assert(metrics_parser_parse(&parser, missing_field, strlen(missing_field), &out) == METRICS_ERR_MISSING_FIELD);

    // 4. 协议版本不匹配
    const char *invalid_version = "{\"v\":2,\"seq\":4,\"ts\":1700000003,\"cpu\":25.4,\"gpu\":null,\"ram\":65.2,\"rx\":10.5,\"tx\":2.1}";
    assert(metrics_parser_parse(&parser, invalid_version, strlen(invalid_version), &out) == METRICS_ERR_INVALID_VERSION);

    // 5. 序列号倒退/重复
    const char *stale_seq = "{\"v\":1,\"seq\":1,\"ts\":1700000004,\"cpu\":25.4,\"gpu\":null,\"ram\":65.2,\"rx\":10.5,\"tx\":2.1}";
    assert(metrics_parser_parse(&parser, stale_seq, strlen(stale_seq), &out) == METRICS_ERR_STALE_SEQ);

    printf("PASS: test_metrics_parser\n");
    return 0;
}
```

- [ ] **步骤 2：运行测试验证失败**

运行：
```bash
cd pico_freertos_firmware/tests/build && make test_metrics_parser
```
预期：FAIL，报错找不到 `metrics_parser.h`。

- [ ] **步骤 3：编写 `metrics_parser.h` 与 `metrics_parser.c`**

创建 `pico_freertos_firmware/include/metrics_parser.h`：
```c
#ifndef METRICS_PARSER_H
#define METRICS_PARSER_H

#include "system_state.h"
#include <stddef.h>
#include <stdint.h>

typedef enum {
    METRICS_PARSE_OK = 0,
    METRICS_ERR_INVALID_JSON,
    METRICS_ERR_MISSING_FIELD,
    METRICS_ERR_INVALID_VERSION,
    METRICS_ERR_INVALID_VALUE,
    METRICS_ERR_STALE_SEQ
} metrics_parse_result_t;

typedef struct {
    uint32_t last_seq;
    bool has_last_seq;
} metrics_parser_t;

void metrics_parser_init(metrics_parser_t *parser);
metrics_parse_result_t metrics_parser_parse(
    metrics_parser_t *parser,
    const char *buffer,
    size_t length,
    pc_metrics_data_t *out_data
);

#endif // METRICS_PARSER_H
```

创建 `pico_freertos_firmware/src/network/metrics_parser.c`：
```c
#include "metrics_parser.h"
#include "cJSON.h"
#include <math.h>
#include <string.h>

void metrics_parser_init(metrics_parser_t *parser) {
    if (!parser) return;
    parser->last_seq = 0;
    parser->has_last_seq = false;
}

metrics_parse_result_t metrics_parser_parse(
    metrics_parser_t *parser,
    const char *buffer,
    size_t length,
    pc_metrics_data_t *out_data
) {
    if (!parser || !buffer || length == 0 || !out_data) {
        return METRICS_ERR_INVALID_JSON;
    }

    cJSON *root = cJSON_ParseWithLength(buffer, length);
    if (!root || !cJSON_IsObject(root)) {
        if (root) cJSON_Delete(root);
        return METRICS_ERR_INVALID_JSON;
    }

    cJSON *v = cJSON_GetObjectItemCaseSensitive(root, "v");
    cJSON *seq = cJSON_GetObjectItemCaseSensitive(root, "seq");
    cJSON *ts = cJSON_GetObjectItemCaseSensitive(root, "ts");
    cJSON *cpu = cJSON_GetObjectItemCaseSensitive(root, "cpu");
    cJSON *gpu = cJSON_GetObjectItemCaseSensitive(root, "gpu");
    cJSON *ram = cJSON_GetObjectItemCaseSensitive(root, "ram");
    cJSON *rx = cJSON_GetObjectItemCaseSensitive(root, "rx");
    cJSON *tx = cJSON_GetObjectItemCaseSensitive(root, "tx");

    if (!v || !seq || !ts || !cpu || !gpu || !ram || !rx || !tx) {
        cJSON_Delete(root);
        return METRICS_ERR_MISSING_FIELD;
    }

    if (!cJSON_IsNumber(v) || v->valueint != 1) {
        cJSON_Delete(root);
        return METRICS_ERR_INVALID_VERSION;
    }

    if (!cJSON_IsNumber(seq) || seq->valuedouble < 0) {
        cJSON_Delete(root);
        return METRICS_ERR_INVALID_VALUE;
    }

    uint32_t current_seq = (uint32_t)seq->valuedouble;
    if (parser->has_last_seq && current_seq <= parser->last_seq) {
        cJSON_Delete(root);
        return METRICS_ERR_STALE_SEQ;
    }

    if (!cJSON_IsNumber(ts) || ts->valuedouble <= 0) {
        cJSON_Delete(root);
        return METRICS_ERR_INVALID_VALUE;
    }

    if (!cJSON_IsNumber(cpu) || cpu->valuedouble < 0.0 || cpu->valuedouble > 100.0) {
        cJSON_Delete(root);
        return METRICS_ERR_INVALID_VALUE;
    }

    if (!cJSON_IsNull(gpu)) {
        if (!cJSON_IsNumber(gpu) || gpu->valuedouble < 0.0 || gpu->valuedouble > 100.0) {
            cJSON_Delete(root);
            return METRICS_ERR_INVALID_VALUE;
        }
        out_data->gpu = (float)gpu->valuedouble;
        out_data->gpu_valid = true;
    } else {
        out_data->gpu = 0.0f;
        out_data->gpu_valid = false;
    }

    if (!cJSON_IsNumber(ram) || ram->valuedouble < 0.0 || ram->valuedouble > 100.0) {
        cJSON_Delete(root);
        return METRICS_ERR_INVALID_VALUE;
    }

    if (!cJSON_IsNumber(rx) || rx->valuedouble < 0.0 || !cJSON_IsNumber(tx) || tx->valuedouble < 0.0) {
        cJSON_Delete(root);
        return METRICS_ERR_INVALID_VALUE;
    }

    out_data->seq = current_seq;
    out_data->ts = (uint32_t)ts->valuedouble;
    out_data->cpu = (float)cpu->valuedouble;
    out_data->ram = (float)ram->valuedouble;
    out_data->rx_mbps = (float)rx->valuedouble;
    out_data->tx_mbps = (float)tx->valuedouble;

    parser->last_seq = current_seq;
    parser->has_last_seq = true;

    cJSON_Delete(root);
    return METRICS_PARSE_OK;
}
```

- [ ] **步骤 4：运行测试验证通过**

运行：
```bash
cd pico_freertos_firmware/tests/build && make test_metrics_parser && ./test_metrics_parser
```
预期：PASS: test_metrics_parser

- [ ] **步骤 5：Commit**

```bash
git add pico_freertos_firmware/include/metrics_parser.h pico_freertos_firmware/src/network/metrics_parser.c pico_freertos_firmware/tests/test_metrics_parser.c
git commit -m "feat(network): 实现 UDP 指标 JSON 协议校验解析器与单测"
```

---

### 任务 3：香港天文台（HKO）农历逐日表解析与干支格式化服务

**文件：**
- 创建：`pico_freertos_firmware/include/lunar_service.h`
- 创建：`pico_freertos_firmware/src/services/lunar_service.c`
- 测试：`pico_freertos_firmware/tests/test_lunar_service.c`

- [ ] **步骤 1：编写农历解码算法单测（校验 1901-2100 年关键节点）**

编写 `pico_freertos_firmware/tests/test_lunar_service.c`：
```c
#include "lunar_service.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    // 使用原有的 pico/lunar_table.bin 文件进行测试
    int ret = lunar_service_init_from_file("../../../pico/lunar_table.bin");
    assert(ret == 0);

    lunar_date_t lunar;

    // 测试已知公历对照：2026年11月6日 -> 农历丙午年九月廿八
    bool ok = lunar_service_lookup(2026, 11, 6, &lunar);
    assert(ok);
    assert(strcmp(lunar.ganzhi_year, "丙午年") == 0);
    assert(strcmp(lunar.lunar_month_day, "九月廿八") == 0);

    // 测试 2024年2月10日 (甲辰龙年春节) -> 正月初一
    ok = lunar_service_lookup(2024, 2, 10, &lunar);
    assert(ok);
    assert(strcmp(lunar.ganzhi_year, "甲辰年") == 0);
    assert(strcmp(lunar.lunar_month_day, "正月初一") == 0);

    // 测试闰月场景：2023年3月23日 -> 闰二月初二
    ok = lunar_service_lookup(2023, 3, 23, &lunar);
    assert(ok);
    assert(lunar.is_leap == true);
    assert(strcmp(lunar.lunar_month_day, "闰二月初二") == 0);

    printf("PASS: test_lunar_service\n");
    return 0;
}
```

- [ ] **步骤 2：运行测试验证失败**

运行：
```bash
cd pico_freertos_firmware/tests/build && make test_lunar_service
```
预期：FAIL，报错找不到 `lunar_service.h`。

- [ ] **步骤 3：实现 `lunar_service.h` 与 `lunar_service.c`**

创建 `pico_freertos_firmware/include/lunar_service.h`：
```c
#ifndef LUNAR_SERVICE_H
#define LUNAR_SERVICE_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    int ganzhi_index;      // 0..59
    char ganzhi_year[16];  // 例如 "丙午年"
    int lunar_month;       // 1..12
    bool is_leap;          // 是否闰月
    int lunar_day;         // 1..30
    char lunar_month_day[32]; // 例如 "九月廿八" 或 "闰二月初二"
} lunar_date_t;

int lunar_service_init_from_file(const char *filepath);
void lunar_service_init_from_memory(const uint8_t *buffer, size_t size);
bool lunar_service_lookup(int year, int month, int day, lunar_date_t *out_lunar);

#endif // LUNAR_SERVICE_H
```

创建 `pico_freertos_firmware/src/services/lunar_service.c`：
```c
#include "lunar_service.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define LUNAR_MAGIC 0x314C4B48 // "HKL1" in little-endian
#define LUNAR_HEADER_SIZE 10
#define LUNAR_RECORD_SIZE 3

static const char *s_stems[] = {"甲", "乙", "丙", "丁", "戊", "己", "庚", "辛", "壬", "癸"};
static const char *s_branches[] = {"子", "丑", "寅", "卯", "辰", "巳", "午", "未", "申", "酉", "戌", "亥"};
static const char *s_month_names[] = {
    "", "正月", "二月", "三月", "四月", "五月", "六月",
    "七月", "八月", "九月", "十月", "冬月", "腊月"
};
static const char *s_digits[] = {"零", "一", "二", "三", "四", "五", "六", "七", "八", "九"};
static const int s_days_per_month[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};

static const uint8_t *s_table_memory = NULL;
static size_t s_table_size = 0;
static uint8_t *s_file_buffer = NULL;
static uint16_t s_start_year = 0;
static uint32_t s_record_count = 0;

static bool is_leap_year(int y) {
    return (y % 4 == 0 && (y % 100 != 0 || y % 400 == 0));
}

static int days_in_month(int y, int m) {
    if (m == 2 && is_leap_year(y)) return 29;
    return s_days_per_month[m - 1];
}

static int calculate_day_index(int y, int m, int d, int start_year) {
    if (y < start_year || m < 1 || m > 12) return -1;
    if (d < 1 || d > days_in_month(y, m)) return -1;

    int prev_y = y - 1;
    int start_prev_y = start_year - 1;
    int days = 365 * (y - start_year);
    days += prev_y / 4 - start_prev_y / 4;
    days -= prev_y / 100 - start_prev_y / 100;
    days += prev_y / 400 - start_prev_y / 400;

    for (int month_idx = 1; month_idx < m; month_idx++) {
        days += days_in_month(y, month_idx);
    }
    return days + d - 1;
}

static void format_lunar_day(int d, char *out) {
    if (d < 10) {
        sprintf(out, "初%s", s_digits[d]);
    } else if (d == 10) {
        strcpy(out, "初十");
    } else if (d < 20) {
        sprintf(out, "十%s", s_digits[d - 10]);
    } else if (d == 20) {
        strcpy(out, "二十");
    } else if (d < 30) {
        sprintf(out, "廿%s", s_digits[d - 20]);
    } else if (d == 30) {
        strcpy(out, "三十");
    }
}

void lunar_service_init_from_memory(const uint8_t *buffer, size_t size) {
    s_table_memory = buffer;
    s_table_size = size;
    if (size >= LUNAR_HEADER_SIZE) {
        s_start_year = buffer[4] | (buffer[5] << 8);
        s_record_count = buffer[6] | (buffer[7] << 8) | (buffer[8] << 16) | (buffer[9] << 24);
    }
}

int lunar_service_init_from_file(const char *filepath) {
    FILE *fp = fopen(filepath, "rb");
    if (!fp) return -1;
    fseek(fp, 0, SEEK_END);
    long sz = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    if (s_file_buffer) free(s_file_buffer);
    s_file_buffer = (uint8_t *)malloc(sz);
    if (!s_file_buffer) {
        fclose(fp);
        return -2;
    }
    if (fread(s_file_buffer, 1, sz, fp) != (size_t)sz) {
        free(s_file_buffer);
        s_file_buffer = NULL;
        fclose(fp);
        return -3;
    }
    fclose(fp);
    lunar_service_init_from_memory(s_file_buffer, sz);
    return 0;
}

bool lunar_service_lookup(int year, int month, int day, lunar_date_t *out_lunar) {
    if (!s_table_memory || s_start_year == 0 || !out_lunar) return false;

    int idx = calculate_day_index(year, month, day, s_start_year);
    if (idx < 0 || (uint32_t)idx >= s_record_count) return false;

    size_t offset = LUNAR_HEADER_SIZE + (size_t)idx * LUNAR_RECORD_SIZE;
    if (offset + LUNAR_RECORD_SIZE > s_table_size) return false;

    const uint8_t *rec = &s_table_memory[offset];
    uint8_t gz = rec[0];
    uint8_t mf = rec[1];
    uint8_t ld = rec[2];

    if (gz >= 60) return false;
    uint8_t lm = mf & 0x0F;
    bool leap = (mf & 0x80) != 0;
    if (lm < 1 || lm > 12 || ld < 1 || ld > 30) return false;

    out_lunar->ganzhi_index = gz;
    sprintf(out_lunar->ganzhi_year, "%s%s年", s_stems[gz % 10], s_branches[gz % 12]);
    out_lunar->lunar_month = lm;
    out_lunar->is_leap = leap;
    out_lunar->lunar_day = ld;

    char day_str[16];
    format_lunar_day(ld, day_str);
    if (leap) {
        sprintf(out_lunar->lunar_month_day, "闰%s%s", s_month_names[lm], day_str);
    } else {
        sprintf(out_lunar->lunar_month_day, "%s%s", s_month_names[lm], day_str);
    }

    return true;
}
```

- [ ] **步骤 4：运行测试验证通过**

运行：
```bash
cd pico_freertos_firmware/tests/build && make test_lunar_service && ./test_lunar_service
```
预期：PASS: test_lunar_service

- [ ] **步骤 5：Commit**

```bash
git add pico_freertos_firmware/include/lunar_service.h pico_freertos_firmware/src/services/lunar_service.c pico_freertos_firmware/tests/test_lunar_service.c
git commit -m "feat(services): 实现香港天文台农历查表与干支格式化算法"
```

---

### 任务 4：UI 页面布局、卡片切角与脏矩形计算引擎

**文件：**
- 创建：`pico_freertos_firmware/include/ui_layout.h`
- 创建：`pico_freertos_firmware/src/display/ui_layout.c`
- 测试：`pico_freertos_firmware/tests/test_ui_layout.c`

- [ ] **步骤 1：编写布局几何算法与脏矩形测试**

编写 `pico_freertos_firmware/tests/test_ui_layout.c`：
```c
#include "ui_layout.h"
#include <assert.h>
#include <stdio.h>

int main(void) {
    ui_rect_t rects[8];
    int count = 0;

    system_snapshot_t prev_snap;
    system_snapshot_t curr_snap;
    memset(&prev_snap, 0, sizeof(prev_snap));
    memset(&curr_snap, 0, sizeof(curr_snap));

    // 1. 模式发生切换 (全屏刷新)
    prev_snap.ui_mode = UI_MODE_CLOCK;
    curr_snap.ui_mode = UI_MODE_DASHBOARD;
    count = ui_layout_calculate_dirty_regions(&prev_snap, &curr_snap, rects, 8);
    assert(count == 1);
    assert(rects[0].x == 0 && rects[0].y == 0 && rects[0].w == 128 && rects[0].h == 160);

    // 2. 模式相同，仅 CPU 指标发生变化 (局部脏矩形刷新)
    prev_snap = curr_snap;
    curr_snap.metrics.cpu = 88.0f;
    count = ui_layout_calculate_dirty_regions(&prev_snap, &curr_snap, rects, 8);
    assert(count >= 1);
    // 确认计算出的脏矩形完全位于 128x160 屏幕边界之内
    for (int i = 0; i < count; i++) {
        assert(rects[i].x >= 0 && rects[i].x + rects[i].w <= 128);
        assert(rects[i].y >= 0 && rects[i].y + rects[i].h <= 160);
    }

    printf("PASS: test_ui_layout\n");
    return 0;
}
```

- [ ] **步骤 2：运行测试验证失败**

运行：
```bash
cd pico_freertos_firmware/tests/build && make test_ui_layout
```
预期：FAIL，报错找不到 `ui_layout.h`。

- [ ] **步骤 3：实现 `ui_layout.h` 与 `ui_layout.c`**

创建 `pico_freertos_firmware/include/ui_layout.h`：
```c
#ifndef UI_LAYOUT_H
#define UI_LAYOUT_H

#include "system_state.h"

typedef struct {
    int16_t x;
    int16_t y;
    int16_t w;
    int16_t h;
} ui_rect_t;

int ui_layout_calculate_dirty_regions(
    const system_snapshot_t *prev,
    const system_snapshot_t *curr,
    ui_rect_t *out_rects,
    int max_rects
);

#endif // UI_LAYOUT_H
```

创建 `pico_freertos_firmware/src/display/ui_layout.c`：
```c
#include "ui_layout.h"
#include <string.h>

int ui_layout_calculate_dirty_regions(
    const system_snapshot_t *prev,
    const system_snapshot_t *curr,
    ui_rect_t *out_rects,
    int max_rects
) {
    if (!prev || !curr || !out_rects || max_rects < 1) return 0;

    // 模式变更全屏刷新
    if (prev->ui_mode != curr->ui_mode) {
        out_rects[0] = (ui_rect_t){.x = 0, .y = 0, .w = 128, .h = 160};
        return 1;
    }

    int count = 0;

    if (curr->ui_mode == UI_MODE_DASHBOARD) {
        // 顶栏图标/状态变动
        if (prev->wifi_connected != curr->wifi_connected || prev->pc_online != curr->pc_online) {
            if (count < max_rects) out_rects[count++] = (ui_rect_t){0, 0, 128, 16};
        }
        // CPU 卡片区域 (Y: 20-55)
        if (prev->metrics.cpu != curr->metrics.cpu) {
            if (count < max_rects) out_rects[count++] = (ui_rect_t){4, 20, 120, 32};
        }
        // GPU 卡片区域 (Y: 56-91)
        if (prev->metrics.gpu != curr->metrics.gpu || prev->metrics.gpu_valid != curr->metrics.gpu_valid) {
            if (count < max_rects) out_rects[count++] = (ui_rect_t){4, 56, 120, 32};
        }
        // RAM 卡片区域 (Y: 92-127)
        if (prev->metrics.ram != curr->metrics.ram) {
            if (count < max_rects) out_rects[count++] = (ui_rect_t){4, 92, 120, 32};
        }
        // 网速行区域 (Y: 128-158)
        if (prev->metrics.rx_mbps != curr->metrics.rx_mbps || prev->metrics.tx_mbps != curr->metrics.tx_mbps) {
            if (count < max_rects) out_rects[count++] = (ui_rect_t){4, 128, 120, 28};
        }
    } else {
        // 时钟模式
        // 秒/分变化引起时间重绘
        if (prev->clock.second != curr->clock.second || prev->clock.minute != curr->clock.minute) {
            if (count < max_rects) out_rects[count++] = (ui_rect_t){0, 30, 128, 40};
        }
        // 日期或农历变化
        if (prev->clock.day != curr->clock.day || strcmp(prev->lunar.lunar_md, curr->lunar.lunar_md) != 0) {
            if (count < max_rects) out_rects[count++] = (ui_rect_t){0, 75, 128, 50};
        }
        // 天气变化
        if (strcmp(prev->weather.weather, curr->weather.weather) != 0 || prev->weather.temperature != curr->weather.temperature) {
            if (count < max_rects) out_rects[count++] = (ui_rect_t){0, 0, 128, 25};
        }
    }

    return count;
}
```

- [ ] **步骤 4：运行测试验证通过**

运行：
```bash
cd pico_freertos_firmware/tests/build && make test_ui_layout && ./test_ui_layout
```
预期：PASS: test_ui_layout

- [ ] **步骤 5：Commit**

```bash
git add pico_freertos_firmware/include/ui_layout.h pico_freertos_firmware/src/display/ui_layout.c pico_freertos_firmware/tests/test_ui_layout.c
git commit -m "feat(display): 实现界面布局与局部脏矩形变动检测引擎"
```

---

### 任务 5：ARM 交叉工具链配置、pico-sdk 与 FreeRTOS-Kernel SMP 工程搭建

**文件：**
- 创建：`pico_freertos_firmware/CMakeLists.txt`
- 创建：`pico_freertos_firmware/include/FreeRTOSConfig.h`
- 创建：`pico_freertos_firmware/src/app/main.c` (最小双核 SMP 启动骨架)

- [ ] **步骤 1：本地安装 ARM GCC 工具链并初始化 pico-sdk**

运行：
```bash
sudo apt-get update && sudo apt-get install -y gcc-arm-none-eabi libnewlib-arm-none-eabi build-essential
```
验证工具链：
```bash
arm-none-eabi-gcc --version
```
预期：输出 `arm-none-eabi-gcc (15:...)` 版本信息。

下载/克隆官方 `pico-sdk` (含 submodules) 与 `FreeRTOS-Kernel`。

- [ ] **步骤 2：编写 `FreeRTOSConfig.h`（配置双核 SMP）**

创建 `pico_freertos_firmware/include/FreeRTOSConfig.h`：
```c
#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H

#define configUSE_PREEMPTION                    1
#define configUSE_TIME_SLICING                  1
#define configUSE_PORT_OPTIMISED_TASK_SELECTION 0
#define configUSE_TICKLESS_IDLE                 0
#define configCPU_CLOCK_HZ                      125000000
#define configTICK_RATE_HZ                      1000
#define configMAX_PRIORITIES                    8
#define configMINIMAL_STACK_SIZE                256
#define configMAX_TASK_NAME_LEN                 16
#define configUSE_16_BIT_TICKS                  0
#define configIDLE_SHOULD_YIELD                 1
#define configUSE_TASK_NOTIFICATIONS            1
#define configUSE_MUTEXES                       1
#define configUSE_RECURSIVE_MUTEXES             1
#define configUSE_COUNTING_SEMAPHORES           1
#define configQUEUE_REGISTRY_SIZE               8
#define configUSE_QUEUE_SETS                    1
#define configUSE_MALLOC_FAILED_HOOK            1
#define configCHECK_FOR_STACK_OVERFLOW          2

/* SMP / 多核关键配置 */
#define configNUMBER_OF_CORES                   2
#define configTICK_CORE                         0
#define configRUN_MULTIPLE_PRIORITIES           1
#define configUSE_CORE_AFFINITY                 1

/* 内存分配配置 */
#define configSUPPORT_STATIC_ALLOCATION         1
#define configSUPPORT_DYNAMIC_ALLOCATION        1
#define configTOTAL_HEAP_SIZE                   (80 * 1024)

#define INCLUDE_vTaskPrioritySet                1
#define INCLUDE_uxTaskPriorityGet               1
#define INCLUDE_vTaskDelete                     1
#define INCLUDE_vTaskSuspend                    1
#define INCLUDE_vTaskDelayUntil                 1
#define INCLUDE_vTaskDelay                      1
#define INCLUDE_xTaskGetSchedulerState          1
#define INCLUDE_xTaskGetCurrentTaskHandle       1
#define INCLUDE_uxTaskGetStackHighWaterMark     1

#endif /* FREERTOS_CONFIG_H */
```

- [ ] **步骤 3：编写固件根 `CMakeLists.txt`**

配置 `pico_freertos_firmware/CMakeLists.txt`：
```cmake
cmake_minimum_required(VERSION 3.16)
include(pico_sdk_import.cmake)
include(FreeRTOS_Kernel_import.cmake)

project(pico_status_screen C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

add_executable(pico_status_screen
    src/app/main.c
    src/app/supervisor.c
    src/state/system_state.c
    src/network/metrics_parser.c
    src/network/wifi_manager.c
    src/network/udp_listener.c
    src/services/lunar_service.c
    src/services/ntp_service.c
    src/services/weather_service.c
    src/display/font_engine.c
    src/display/ui_layout.c
    src/display/st7735s_driver.c
    src/display/ui_render.c
    tests/cjson/cJSON.c
)

target_include_directories(pico_status_screen PRIVATE
    include
    src
    tests/cjson
)

# 静态资产直接内嵌 Flash
pico_add_embed_data(pico_status_screen
    ../pico/font_cn.bin
    ../pico/lunar_table.bin
    ../pico/amap_root_ca.pem
)

target_link_libraries(pico_status_screen
    pico_stdlib
    pico_multicore
    hardware_spi
    hardware_dma
    hardware_watchdog
    pico_cyw43_arch_lwip_sys_freertos
    FreeRTOS-Kernel-Heap4
)

pico_enable_stdio_usb(pico_status_screen 1)
pico_enable_stdio_uart(pico_status_screen 0)
pico_add_extra_outputs(pico_status_screen)
```

- [ ] **步骤 4：编译骨架固件生成 `.uf2`**

运行：
```bash
mkdir -p pico_freertos_firmware/build && cd pico_freertos_firmware/build && cmake .. && make -j4
```
预期：成功生成 `pico_status_screen.uf2` 且零编译 Warning。

- [ ] **步骤 5：Commit**

```bash
git add pico_freertos_firmware/CMakeLists.txt pico_freertos_firmware/include/FreeRTOSConfig.h pico_freertos_firmware/src/app/main.c
git commit -m "feat(build): 建立 pico-sdk 与 FreeRTOS SMP 双核交叉编译工程"
```

---

### 任务 6：ST7735S 高速 SPI-DMA 双缓冲显示驱动与 Core 1 渲染任务

**文件：**
- 创建：`pico_freertos_firmware/include/st7735s_driver.h`
- 创建：`pico_freertos_firmware/include/font_engine.h`
- 创建：`pico_freertos_firmware/src/display/st7735s_driver.c`
- 创建：`pico_freertos_firmware/src/display/font_engine.c`
- 创建：`pico_freertos_firmware/src/display/ui_render.c`

- [ ] **步骤 1：实现 ST7735S 硬件初始化与 DMA Ping-Pong 传输**

配置引脚与时钟（GP18-SCK, GP19-MOSI, GP16-CS, GP17-DC, GP20-RST），初始化 SPI0 为 62.5MHz。
配置 2 个 DMA 通道配合 Ping-Pong 双缓冲区（每次发送 32 行切片，128×32×2 = 8192 字节），保证 CPU 光栅化与 DMA 传输并行不卡顿。

- [ ] **步骤 2：实现中英文点阵字模提取引擎**

实现 `font_engine.c`：
- ASCII 5x7 / 16x26 大字时钟光栅化；
- 中文字模直接定位已嵌入 Flash 的 `font_cn.bin` 地址，通过偏移快速取模绘制到当前行切片中。

- [ ] **步骤 3：实现 Core 1 绑定的渲染循环任务**

在 `ui_render.c` 中创建任务 `vTaskUIRender`，通过 `vTaskCoreAffinitySet(xTask, (1 << 1))` 绑定到 Core 1。
每 20ms（50fps）或脏矩形触发时：
1. `system_state_get_snapshot(&local_snap)` 提取无锁只读私有副本；
2. 比较状态生成脏矩形；
3. 执行行条带光栅化并启动 SPI-DMA 发送。

- [ ] **步骤 4：编译并验证渲染模块**

运行：
```bash
cd pico_freertos_firmware/build && make -j4
```
预期：无警告，产物包含 UI 与 DMA 符号。

- [ ] **步骤 5：Commit**

```bash
git add pico_freertos_firmware/include/st7735s_driver.h pico_freertos_firmware/src/display/st7735s_driver.c pico_freertos_firmware/src/display/font_engine.c pico_freertos_firmware/src/display/ui_render.c
git commit -m "feat(display): 完成 ST7735S SPI-DMA 双缓冲驱动与 Core 1 独立渲染任务"
```

---

### 任务 7：网络通信集成（CYW43 Wi-Fi、UDP 监听、NTP 与高德 HTTPS 天气）

**文件：**
- 创建：`pico_freertos_firmware/src/network/wifi_manager.c`
- 创建：`pico_freertos_firmware/src/network/udp_listener.c`
- 创建：`pico_freertos_firmware/src/services/ntp_service.c`
- 创建：`pico_freertos_firmware/src/services/weather_service.c`

- [ ] **步骤 1：实现 CYW43439 Wi-Fi 管理器**

在 `wifi_manager.c` 中通过 `cyw43_arch_init()` 与 FreeRTOS 后台线程启动无线网卡，支持 AP 掉线 1s, 3s, 10s, 30s 指数退避重连，并在连上后调用 `system_state_set_wifi_status(true)`。

- [ ] **步骤 2：实现 UDP 8765 监听任务**

在 `udp_listener.c` 中创建 `vTaskUDPRecv`，绑定 `0.0.0.0:8765` 阻塞监听：
收到数据包调用 `metrics_parser_parse()` 严格校验，若合法则调用 `system_state_update_metrics(&metrics, xTaskGetTickCount())` 触发界面更新。

- [ ] **步骤 3：实现 NTP 校时与软 RTC 增量维护**

在 `ntp_service.c` 中每 6 小时同步一次阿里云 NTP (`ntp.aliyun.com`)，收到时间戳校准本地硬件/软 RTC，每秒将时钟信息更新至 `system_state_update_clock()`。

- [ ] **步骤 4：实现高德实况天气 HTTPS 客户端**

在 `weather_service.c` 中通过 lwIP + mbedTLS 校验嵌入的 `amap_root_ca.pem`，GET 请求高德 API，解析温度与现象，每 30 分钟刷新一次，结果更新至 `system_state_update_weather()`。网络失败时标记为 `is_stale`。

- [ ] **步骤 5：编译与集成检查**

运行：
```bash
cd pico_freertos_firmware/build && make -j4
```
预期：成功链接，生成完整二进制。

- [ ] **步骤 6：Commit**

```bash
git add pico_freertos_firmware/src/network/wifi_manager.c pico_freertos_firmware/src/network/udp_listener.c pico_freertos_firmware/src/services/ntp_service.c pico_freertos_firmware/src/services/weather_service.c
git commit -m "feat(network): 集成 Wi-Fi 管理、UDP 指标监听、NTP 校时与 HTTPS 天气服务"
```

---

### 任务 8：双核心跳监控、8000ms 硬件看门狗与端到端集成验证

**文件：**
- 创建：`pico_freertos_firmware/src/app/supervisor.c`
- 修改：`pico_freertos_firmware/src/app/main.c`

- [ ] **步骤 1：实现 Supervisor 任务与双核心跳喂狗**

实现 `supervisor.c`：
1. Core 0 网络任务与 Core 1 UI 任务各维护一个原子心跳计数器 `core0_heartbeat` 与 `core1_heartbeat`；
2. Supervisor 任务以 1000ms 周期执行：
   - 检查 `system_state_check_timeouts(now)` 维护 3 秒时钟模式切换；
   - 检查双核心跳是否有递增；若均健康，调用 RP2040 硬件接口 `watchdog_update()`；
   - 若任一核心超过 8000ms 无心跳，硬件触发看门狗复位，串口打印复位源。

- [ ] **步骤 2：全量构建与固件 `.uf2` 检查**

运行：
```bash
cd pico_freertos_firmware/build && cmake .. && make -j4
ls -lh pico_status_screen.uf2
```
预期：生成 `pico_status_screen.uf2` 且大小在合理范围（约 300KB~700KB），SRAM 占用符合预期。

- [ ] **步骤 3：端到端与原有 Host 采集端联调测试**

1. 运行 `tests/build/` 下全量单元测试：
```bash
cd pico_freertos_firmware/tests/build && ctest --output-on-failure
```
预期：100% 测试通过。
2. 运行现有主机端 `pc_metrics` 脚本，抓包确认 UDP 8765 正常广播：
```bash
python3 pc_metrics/main.py --once
```
预期：正常输出采集到的 JSON 数据包并完成广播发送。

- [ ] **步骤 4：编写使用与烧录文档并 Commit**

创建 `pico_freertos_firmware/README.md`，记录编译、烧录方法与配置说明。
```bash
git add pico_freertos_firmware/
git commit -m "feat: 完成 Pico W FreeRTOS 双核多任务固件系统实现与全面验证"
```
