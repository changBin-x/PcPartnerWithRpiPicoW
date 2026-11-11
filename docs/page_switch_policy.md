# 双页面自动切换与防闪烁光栅化技术规范

本文档阐述基于 Raspberry Pi Pico W（RP2040）+ ST7735S 128×160 屏幕的桌面屏固件中，**「电脑监控页（页面 A）」与「综合生活/战术巡检页（页面 B）」双页面自动切换策略**、**超时判定状态机流转**、**局部刷新与防闪烁协同机制**，以及在 FreeRTOS SMP 双核架构下的具体固件落地步骤。

---

## 1. 架构总览与核心设计决策

### 1.1 核心需求与背景
Pico W 作为桌面免驱副屏，其物理输入能力受限（无外置物理按键与触摸屏）。系统承载两类核心业务场景：
1. **电脑监控页（页面 A）**：电脑开机且运行采集代理（`pc_metrics.host`）时，实时呈现 CPU、GPU、内存使用率与网络吞吐率。
2. **综合综述页（页面 B）**：电脑关机、睡眠或未启动采集程序时，退避显示桌面时钟、公历日期、星期、干支农历与实况天气（或战术巡检综述）。

### 1.2 核心决策采纳
- **决策一（页面调度）**：采用 **「主机指标中断 3 秒自动切换」** 策略。指标断流达 3000 ms 时无缝切入页面 B；当主机恢复发送 UDP 报文且首包校验通过时，立即切回页面 A。
- **决策二（语言规范）**：界面内除保留国际通用硬件缩写 `CPU` 与 `GPU` 外，所有状态字、单位与标签均转换为**纯中文显示**。所需增量汉字由 `tools/build_font.py` 增量注入只读字模库 `font_cn.bin`。

---

## 2. 状态机流转与超时判定机制

### 2.1 状态定义
固件通过 `UiMode` 枚举（`system_state.h`）定义两类主工作模式：

```c
typedef enum {
  kUiModeDashboard = 0, /* 页面 A：电脑指标实时监控模式 */
  kUiModeClock     = 1  /* 页面 B：综合时钟/战术巡检模式 */
} UiMode;
```

### 2.2 状态机状态转移图
状态机维护于单一数据源 `system_state.c` 中，其转移规则如下：

```text
               系统初始化 / 复位
                      │
                      ▼
             ┌─────────────────┐
             │ 页面 B: 综合综述 │ ◄─────────────────────────┐
             │ (kUiModeClock)  │                           │
             └────────┬────────┘                           │
                      │                                    │
    收到合法 UDP 报文 │                                    │ 指标中断超时
    system_state_     │                                    │ elapsed > 3000ms
    update_metrics()  │                                    │ system_state_
                      ▼                                    │ check_timeouts()
             ┌─────────────────┐                           │
             │ 页面 A: 电脑监控 │ ──────────────────────────┘
             │ (kUiModeDashboard)
             └─────────────────┘
```

### 2.3 超时判定逻辑实现细节
- **超时阈值**：宏定义 `kPcMetricTimeoutMs` 严格设定为 `3000u`（3000 毫秒）。
- **时间戳推进**：
  - Core 0 在 UDP 接收回调中，将收到合法报文时的当前系统节拍 `current_tick`（毫秒）写入快照字段 `last_pc_metric_tick`，同时将 `pc_online` 置为 `true`，`ui_mode` 置为 `kUiModeDashboard`。
  - Core 1 独占渲染任务 `vTaskUIRender` 每 20 ms 运行一次，在取帧快照前主动执行 `system_state_check_timeouts(current_tick)`：
    ```c
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
    ```
- **消抖与无锁设计**：
  - 超时推进由且仅由渲染任务单向调用，消费方直接获得更新后的副本。
  - 互斥量锁仅在 `memcpy` 快照瞬间持有（微秒级），绘制过程完全在私有栈快照副本上执行，不阻塞任何网络包写入。

---

## 3. 页面调度时序与防闪烁协同机制

ST7735S 采用 SPI 串行推屏，在 128×160 分辨率下全屏为 20,480 像素（40,960 字节）。若每次刷新都全屏清屏（Clear Screen），人眼会捕捉到明显的背光漏光闪烁；若多行并发绘制，杜邦线高频传输易产生横纹撕裂。为此，固件构建了三层协同防线：

### 3.1 第一防线：模式切换全屏重置，同模式局部脏矩形剪裁
在 `ui_layout.c` 的 `ui_layout_calculate_dirty_regions()` 中实现差分判定：

1. **跨模式切换（`prev->ui_mode != curr->ui_mode`）**：
   - 此时页面 A 与页面 B 布局结构完全异构，算法输出唯一定点全屏矩形：`{0, 0, 128, 160}`。
   - 触发全屏 10 个切片的整体光栅化，旧页面图元一次性被新页面完整覆盖，**严禁先刷纯黑底再画内容**，杜绝黑帧闪烁。
2. **同模式运行（`prev->ui_mode == curr->ui_mode`）**：
   - 算法对比指标值（CPU、GPU、内存、网速、秒级时间）。
   - 仅将发生数值跳变的卡片或文本区域压入 `out_rects` 脏矩形数组。例如仅秒数从 `55` 跳到 `56` 时，仅标记包含该文字的 16×14 像素脏区。

### 3.2 第二防线：双缓冲切片轮换（Ping-Pong Slicing）与 DMA 隔离
为消除单缓冲区引起的画面撕裂，固件在 `.bss` 段静态开辟两块 128×16 切片显存（每块 4,096 字节，总计 8 KB）：

```c
static uint16_t s_slice_buffers[2][kDisplaySlicePixels];
```

光栅化调度流水线时序如下：

```text
时刻 T0: CPU 在 Buffer 0 光栅化切片 0
时刻 T1: DMA 启动推屏 Buffer 0 (切片 0) ──► 同时 CPU 在 Buffer 1 光栅化切片 1
时刻 T2: DMA 启动推屏 Buffer 1 (切片 1) ──► 同时 CPU 在 Buffer 0 光栅化切片 2
...
```

- 切片行高定为 16 行（`kDisplaySliceRows = 16`），整屏 160 行刚好被整除为 10 个切片（Slice 0 ~ Slice 9）。
- 仅当某切片与任一脏矩形发生 Y 轴相交时，该切片才被标记为 `dirty` 并参与光栅化与 DMA 传输；未变动的切片直接跳过，SPI 总线开销下降 70%~90%。

### 3.3 第三防线：杜邦线物理防抖与 CS 状态机复位
- 针对面包板或杜邦线手工跳线的物理分布电容与反射干扰，固件 SPI 时钟设定为稳定可靠的 **10 MHz**。
- 在每个切片传输首尾，驱动强制独立翻转片选信号（CS 引脚），显式复位 ST7735S 内部字节解包时钟同步状态机，彻底消除异步时钟偏移导致的彩色杂色与相位横纹。

---

## 4. FreeRTOS SMP 双核协同与具体落地步骤

### 4.1 双核任务职责划分矩阵

| 运行核心 | 任务标识 | 调度优先级 | 栈空间预算 | 核心职责 |
| :--- | :--- | :---: | :---: | :--- |
| **Core 0** | `udp_listener` | 4 (最高) | 4 KB | 监听 UDP 8765 端口，解析 PC 指标并写入快照 |
| **Core 0** | `wifi_manager` | 2 | 4 KB | 维护 Wi-Fi 链路，掉线按 1/3/10/30s 指数退避重连 |
| **Core 0** | `ntp_service` | 2 | 4 KB | 维持本地秒级软时钟，每 6 小时网络校时一次 |
| **Core 0** | `weather_service` | 1 | 16 KB | 高德 HTTPS 天气拉取（吃栈，低优先级异步刷新） |
| **Core 0** | `supervisor` | 3 | 2 KB | 软硬件看门狗监督，每秒检查签到并执行 `watchdog_update()` |
| **Core 1** | `vTaskUIRender` | 3 | 2 KB | **独占执行**：20ms 推进状态机、收集脏区、切片光栅化与 SPI 刷屏 |
| **Core 1** | `heartbeat_c1` | 2 | 2 KB | Core 1 独立心跳签到任务，确保单核死锁可被看门狗发现 |

### 4.2 固件侧关键实现落地点

#### 步骤 1：状态机超时判定接入（`ui_render.c`）
在 Core 1 的渲染主循环中，确保取快照前推进状态机：
```c
static void vTaskUIRender(void* context) {
  /* ... 初始化 display 与 font_engine ... */
  for (;;) {
    FIRMWARE_CHECK_IN(kSupervisorSlotUi);
    vTaskDelay(pdMS_TO_TICKS(kRenderPeriodMs)); /* 20ms 节奏 */

    /* 关键：必须由唯一快照消费者推进超时状态机 */
    system_state_check_timeouts((uint32_t)pdTICKS_TO_MS(xTaskGetTickCount()));

    SystemSnapshot current;
    system_state_get_snapshot(&current);

    UiRect dirty_rects[kMaxDirtyRects];
    const int dirty_count = ui_render_collect_dirty_regions(
        &previous, &current, dirty_rects, kMaxDirtyRects);

    if (dirty_count > 0) {
      ui_render_draw_dirty(backend, &current, dirty_rects, dirty_count);
    }
    previous = current;
  }
}
```

#### 步骤 2：全中文多主题字模扩展（`build_font.py`）
为消除英文标签，在中文字模生成工具 `tools/build_font.py` 中将以下 40 个界面专用汉字加入白名单：
```python
UI_CHINESE_CHARACTERS = (
    "处理器显卡系统内存负载偏高满载较忙待机休眠稳定"
    "下行上行兆通信延迟毫秒机体护盾反应堆巡检就绪锁定"
    "战备战术抬头显示通道监测时序遥测标准生活看板吉字节"
)
```
重新执行 `python3 tools/build_font.py` 生成紧凑的 `pico_freertos_firmware/assets/font_cn.bin`，并由 CMake 以目标文件方式（`objcopy`）直接嵌入只读 Flash，零 RAM 消耗。

#### 步骤 3：看门狗与死锁防御协同（`supervisor.c`）
- 监督任务每 1000 ms 检查 7 个健康签到槽位（Wi-Fi、UDP、NTP、天气、UI、Core 0 心跳、Core 1 心跳）。
- UI 渲染任务超时阈值设为 1000 ms。若 Core 1 因为 SPI 传输挂死、死锁或内存越界导致超过 1 秒未签到，看门狗立即停止喂狗，硬件看门狗将在 8 秒后强行复位整机，并通过 scratch 寄存器记录阶段标记，确保设备具备 7×24 小时无人值守高可用性。

---

## 5. 验收标准与测试规范

固件修改或主题扩展必须通过以下 3 道验收关卡：

1. **单元测试与端到端回归（`ctest`）**：
   在 `pico_freertos_firmware/tests/` 目录下执行构建，必须确保现有的 13 个测试套件 100% 通过（`13/13 Passed`），特别是：
   - `test_system_state`：验证 `tick + 3001` 时状态机精确切至 `kUiModeClock`；
   - `test_ui_layout`：验证跨模式切换时输出全屏脏矩形，同模式输出局部卡片脏矩形；
   - `test_pipeline_e2e`：端到端验证真实报文解析后触发仪表盘，推进 4401 ms 后落成时钟页 PPM 图像。
2. **代码风格与规范闸门**：
   执行 `./check_style.sh`，确保通过 Google 风格 80 列宽度与 clang-format 校验。
3. **真实光栅化像素比对**：
   通过 `tools/ppm_to_png.py` 将生成的 PPM 转换并验证无残影、无文字粘连。
