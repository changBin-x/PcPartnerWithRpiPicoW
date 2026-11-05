# Pico W FreeRTOS 双核多任务固件系统架构与设计规格书

- **文档名称**：Pico W FreeRTOS 双核多任务固件系统设计规格
- **创建日期**：2026-11-06
- **架构版本**：v1.0.0
- **目标平台**：Raspberry Pi Pico W (RP2040 双核 Cortex-M0+, 264KB SRAM, 2MB Flash, CYW43439 Wi-Fi/BT)
- **显示外设**：ST7735S 128×160 SPI TFT 彩色液晶屏

---

## 1. 背景与架构目标

### 1.1 背景
当前项目为运行在 Raspberry Pi Pico W 上的桌面状态屏，原有 MicroPython 实现已验证了功能原型（UDP 广播接收电脑指标并渲染、3 秒无数据自动回退时钟与农历、在线高德天气与 NTP 校时）。但 MicroPython 单线程/异步事件循环在进行 HTTPS TLS 握手与 DNS 解析时易受网络抖动影响，偶发引发看门狗报警。

### 1.2 目标
采用 C/C++ 配合 Raspberry Pi 官方 `pico-sdk` 与 `FreeRTOS (SMP 模式)` 重构嵌入式固件：
1. **零界面卡顿**：充分发挥 RP2040 双核特性，Core 1 独占 UI 渲染与 SPI-DMA 传输，完全不受网络请求与 TLS 加密握手影响，保证时钟与动画 100% 准时平滑。
2. **极低资源开销与长效稳定**：静态/受控动态内存分配，避免堆内存碎片；双核心跳守护硬件看门狗（8000ms），故障自动软复位与原因记录。
3. **分层测试与模块解耦**：核心协议、农历算法、状态机在 Host 平台具备 100% 自动化单元测试；通过 CMake 自动化交叉编译生成 `.uf2` 固件。

---

## 2. 操作系统选型分析与权衡

| 评估维度 | FreeRTOS (SMP + pico-sdk) | RT-Thread (标准版 / Nano) | 结论与抉择 |
| :--- | :--- | :--- | :--- |
| **官方支持度** | **官方一等公民支持**：Raspberry Pi 官方 SDK 包含对 FreeRTOS-Kernel 及 RP2040 双核 SMP 的完整移植。 | **实验性/芯片级**：官方仅提供基础版 Pico BSP，对 Pico W 缺乏长期维护的板级驱动。 | 选 FreeRTOS |
| **无线网卡驱动 (CYW43439)** | **开箱即用**：提供 `pico_cyw43_arch_freertos` 库，固件固化、总线通信与事件中断完全适配。 | **高风险阻断**：缺少 CYW43 协议总线对接与 RT-Thread WLAN/netdev 驱动，自研周期无法收敛。 | 选 FreeRTOS |
| **网络与安全栈** | **标准完备**：官方集成 `lwIP` (支持多线程 `NO_SYS=0`) 与 `mbedTLS`，完整支持 HTTPS 证书链校验。 | **需大量二次适配**：SAL 组件需底层驱动支撑。 | 选 FreeRTOS |
| **内存与效率** | 内核精简，内存开销高度透明可控。 | 组件丰富但系统占用相对较重。 | 选 FreeRTOS |

**最终选型结论**：采用 **pico-sdk + FreeRTOS (SMP 模式)**。

---

## 3. 硬件资源与内存划分

### 3.1 双核分工与硬件外设映射
- **Core 0（通信与业务调度核）**：
  - 承载 FreeRTOS 内核调度器；
  - 运行 Wi-Fi 连接管理、lwIP 协议栈线程；
  - 运行 UDP 8765 指标监听与 cJSON 校验任务；
  - 运行 NTP 授时与 HTTPS 高德天气更新任务；
  - 运行 Supervisor 看门狗与心跳监控任务。
- **Core 1（图形与渲染专享核）**：
  - 独占运行 UI 渲染主循环；
  - 独占 SPI0 控制器与 DMA 通道操作，以最高 62.5MHz 时钟驱动 ST7735S 屏幕；
  - 不做任何阻塞性网络操作。
- **硬件看门狗 (Hardware Watchdog)**：
  - 超时配置为 8000ms；
  - 仅在 Core 0 监控任务与 Core 1 UI 任务同时保持心跳推进时才执行 `watchdog_update()`。

### 3.2 内存空间分配 (264KB SRAM)
- **任务栈空间（约 42 KB）**：
  - `tcpip_thread` (lwIP): 8 KB
  - `vTaskWeatherSync` (mbedTLS): 16 KB
  - `vTaskUDPRecv`: 4 KB
  - `vTaskTimeSync`: 4 KB
  - `vTaskUIRender`: 10 KB
- **FreeRTOS 系统动态堆（Heap 4 / Heap 5）**：约 80 KB，供任务队列、动态 JSON 解析及 TLS 瞬时会话分配。
- **图形行切片与双缓冲（DMA Ping-Pong）**：约 8 KB。
  - 128×160 分辨率，16 位 RGB565。采用 32 行条带（128×32×2B = 8KB）双缓冲机制，零大数组内存溢出风险。
- **lwIP PBUF 缓冲区**：约 32 KB。
- **系统 BSS、数据段与安全余量**：约 102 KB。

### 3.3 Flash 静态资源嵌入（零 SRAM 消耗）
利用 CMake `pico_add_embed_data` 将静态资源常驻 Flash（直接指针寻址）：
1. `font_cn.bin` (33KB 中文点阵字库)
2. `lunar_table.bin` (219KB 1901-2100 年农历逐日表)
3. `amap_root_ca.pem` / `hko_root_ca.pem` (X.509 根证书)

---

## 4. 多任务并发模型与双核通信

### 4.1 任务拓扑与优先级
1. `vTaskSupervisor`（优先级 5，最高）：看门狗保活、双核心跳检测、系统健康度统计。
2. `vTaskUDPRecv`（优先级 4）：监听 UDP 8765 端口广播包，对包结构、时间戳、指标合法性进行校验并更新状态。
3. `vTaskUIRender`（优先级 3，Core 1 亲和）：定频 50fps / 脏矩形触发，渲染卡片与时钟。
4. `vTaskTimeSync`（优先级 2）：启动后立即 NTP 同步，后续每 6 小时周期校时，失败退避重试。
5. `vTaskWeatherSync`（优先级 1）：启动后获取实况天气，后续每 30 分钟刷新，失败退避重试。

### 4.2 双核通信机制：只读状态镜像快照（State Snapshot）
为了杜绝死锁与阻塞渲染：
1. 定义全局结构体 `SystemSnapshot_t`，记录当前所有可显示数据（CPU/GPU/RAM/网速、公历时间、农历字符串、天气信息、Wi-Fi 状态、PC 在线状态）。
2. Core 0 各任务更新数据时，通过快速互斥锁拷贝到全局快照（耗时 < 1µs）。
3. Core 1 的 UI 渲染任务在每帧开始前，加锁快速将全局快照拷贝至私有渲染快照 `local_snapshot`。
4. Core 1 的字体查找、卡片排版、切角计算与 DMA 渲染全程仅访问私有快照，不持有任何全局互斥锁。

### 4.3 状态机转换规则
- **电脑指标在线判决**：收到合法 UDP 指标包，记录最新时间戳；
- **自动切换**：若当前时间与上次接收时间差大于 3000ms，系统自动从 `UI_MODE_DASHBOARD` 切换为 `UI_MODE_CLOCK`；
- **即时恢复**：任意时刻收到合法指标包，立刻切回 `UI_MODE_DASHBOARD`。

---

## 5. 软件模块划分

```
pico_freertos_firmware/
├── CMakeLists.txt                # 工程根构建脚本，集成 pico-sdk 与 FreeRTOS-Kernel
├── FreeRTOSConfig.h              # SMP 多核与任务裁剪配置
├── src/
│   ├── app/
│   │   ├── main.c                # 系统初始化、Wi-Fi 驱动装载、多核任务启动
│   │   └── supervisor.c          # 双核心跳与看门狗监控
│   ├── state/
│   │   ├── system_state.h        # SystemSnapshot 结构体定义
│   │   └── system_state.c        # 线程安全的状态更新与快照导出接口
│   ├── display/
│   │   ├── st7735s_driver.c      # ST7735S 屏幕控制器与 SPI-DMA 驱动
│   │   ├── font_engine.c         # ASCII 与 Flash 中文点阵字模光栅化引擎
│   │   ├── ui_render.c           # 界面状态机、卡片与局部重绘控制器
│   │   └── ui_layout.c           # 仪表盘模式与时钟模式坐标及图形生成
│   ├── network/
│   │   ├── wifi_manager.c        # CYW43 固件加载、AP 接入与重连退避
│   │   ├── udp_listener.c        # 8765 端口监听与网络包捕获
│   │   └── metrics_parser.c      # cJSON 语法与业务字段合法性校验
│   ├── services/
│   │   ├── ntp_service.c         # SNTP 时间同步与软 RTC 驱动
│   │   ├── lunar_service.c       # 1901-2100 年 Flash 农历表解析器
│   │   └── weather_service.c     # 高德 Web API HTTPS GET 与 TLS 证书校验
│   └── assets/
│       ├── font_cn.bin           # 静态中文字模
│       ├── lunar_table.bin       # 静态农历数据表
│       └── certs/                # HTTPS 根证书
└── tests/
    ├── host_mock/                # Host 端模拟桩代码
    ├── test_metrics_parser.cpp   # UDP JSON 协议解析与边界攻击测试
    ├── test_lunar_table.cpp      # 农历解码算法一致性校验测试
    └── test_ui_dirty_rect.cpp    # 脏矩形与渲染坐标算法测试
```

---

## 6. 验证与质量保证策略

1. **Host 纯逻辑单元测试（自动化保障）**：
   - 提取 `metrics_parser.c`、`lunar_service.c`、`ui_layout.c` 进行平台无关编译；
   - 编写 C++ GoogleTest/CUnit 测试套件，覆盖正常包、损坏包、版本越界、极端数值边界；
   - 校验农历输出与原 Python `lunar.py` 的逐日计算完全吻合。
2. **交叉编译静态检查**：
   - 开启 `-Wall -Wextra -Werror -Wformat`，严禁隐式转换与未初始化变量；
   - 链接地图分析（Map File Inspection），验证各段 SRAM 与 Flash 实际占用。
3. **真实固件打包与联调**：
   - 最终输出标准的 `firmware.uf2` 镜像文件。
   - 配套运行现有的 `pc_metrics` 采集程序测试联通性。

---

## 7. 规格自检清单

- [x] **占位符扫描**：全文无“待定”、“TODO”或未定义的模糊需求。
- [x] **内部一致性**：双核硬件资源分配、DMA 通道使用、FreeRTOS 任务优先级与通信模型完全吻合。
- [x] **范围检查**：专注 Pico W 固件端从 MicroPython 到 FreeRTOS C 的完备重构，不涉及 Host 端破坏性变更。
- [x] **模糊性消除**：明确了双核通信使用本地私有读快照模式消除锁竞争，明确了 32 行切片 DMA 双缓冲。
