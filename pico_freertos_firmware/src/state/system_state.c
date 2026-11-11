/**
 * @file system_state.c
 * @brief 系统状态镜像快照：单一数据源 + 线程安全读写。
 *
 * 所有 UI 线程 / 网络线程 / 服务线程只通过本模块读写全局状态，
 * 内部用互斥锁保护，读路径 memcpy 出私有副本，避免读者持有锁渲染。
 *
 * 双后端：
 *  - 设备端（PICO_ON_DEVICE）：FreeRTOS 互斥量
 *  - 主机端（单元测试）：pthread 互斥量
 */

#include "system_state.h"

#include <string.h>

/* PC 指标超时阈值：超过该时长未收到新指标即判定 PC 离线。 */
#define kPcMetricTimeoutMs 3000u

#if defined(PICO_ON_DEVICE) && PICO_ON_DEVICE

#include "FreeRTOS.h"
#include "semphr.h"

static SemaphoreHandle_t s_state_mutex = NULL;

/* 惰性创建：即使调用方漏掉 system_state_init() 也不会解引用空句柄。 */
static void state_lock_init(void) {
  if (s_state_mutex == NULL) {
    s_state_mutex = xSemaphoreCreateMutex();
  }
}

#define STATE_LOCK()                                    \
  do {                                                  \
    state_lock_init();                                  \
    (void)xSemaphoreTake(s_state_mutex, portMAX_DELAY); \
  } while (0)

#define STATE_UNLOCK()                   \
  do {                                   \
    (void)xSemaphoreGive(s_state_mutex); \
  } while (0)

#else /* !(defined(PICO_ON_DEVICE) && PICO_ON_DEVICE) */

#include <pthread.h>

static pthread_mutex_t s_state_mutex = PTHREAD_MUTEX_INITIALIZER;

/* pthread 互斥量由 PTHREAD_MUTEX_INITIALIZER 静态初始化，此处仅保持接口对称。
 */
static void state_lock_init(void) {}

#define STATE_LOCK()                          \
  do {                                        \
    (void)pthread_mutex_lock(&s_state_mutex); \
  } while (0)

#define STATE_UNLOCK()                          \
  do {                                          \
    (void)pthread_mutex_unlock(&s_state_mutex); \
  } while (0)

#endif

static SystemSnapshot s_global_snapshot;

void system_state_init(void) {
  /* 双核 SMP 下先在主核启动阶段建好互斥量，避免两核并发惰性创建竞态。 */
  state_lock_init();

  STATE_LOCK();
  memset(&s_global_snapshot, 0, sizeof(s_global_snapshot));
  s_global_snapshot.ui_mode = kUiModeClock;
  s_global_snapshot.theme_id = kThemeSwiss;
  s_global_snapshot.pc_online = false;
  s_global_snapshot.wifi_connected = false;
  STATE_UNLOCK();
}

void system_state_update_metrics(const PcMetricsData* metrics,
                                 uint32_t current_tick) {
  if (metrics == NULL) {
    return;
  }

  STATE_LOCK();
  s_global_snapshot.metrics = *metrics;
  s_global_snapshot.last_pc_metric_tick = current_tick;
  s_global_snapshot.pc_online = true;
  s_global_snapshot.ui_mode = kUiModeDashboard;
  STATE_UNLOCK();
}

void system_state_update_clock(const ClockData* clock) {
  if (clock == NULL) {
    return;
  }

  STATE_LOCK();
  s_global_snapshot.clock = *clock;
  STATE_UNLOCK();
}

void system_state_update_lunar(const LunarData* lunar) {
  if (lunar == NULL) {
    return;
  }

  STATE_LOCK();
  s_global_snapshot.lunar = *lunar;
  STATE_UNLOCK();
}

void system_state_update_weather(const WeatherData* weather) {
  if (weather == NULL) {
    return;
  }

  STATE_LOCK();
  s_global_snapshot.weather = *weather;
  STATE_UNLOCK();
}

void system_state_set_wifi_status(bool connected) {
  STATE_LOCK();
  s_global_snapshot.wifi_connected = connected;
  STATE_UNLOCK();
}

void system_state_set_theme(UiThemeId theme_id) {
  const UiThemeId clamped =
      ((unsigned)theme_id < (unsigned)kThemeCount) ? theme_id : kThemeSwiss;
  STATE_LOCK();
  s_global_snapshot.theme_id = clamped;
  STATE_UNLOCK();
}

void system_state_check_timeouts(uint32_t current_tick) {
  STATE_LOCK();
  if (s_global_snapshot.pc_online) {
    uint32_t elapsed = current_tick - s_global_snapshot.last_pc_metric_tick;
    if (elapsed > kPcMetricTimeoutMs) {
      s_global_snapshot.pc_online = false;
      s_global_snapshot.ui_mode = kUiModeClock;
    }
  }
  STATE_UNLOCK();
}

void system_state_get_snapshot(SystemSnapshot* out_snapshot) {
  if (out_snapshot == NULL) {
    return;
  }

  STATE_LOCK();
  memcpy(out_snapshot, &s_global_snapshot, sizeof(*out_snapshot));
  STATE_UNLOCK();
}
