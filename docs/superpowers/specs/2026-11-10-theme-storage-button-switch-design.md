# Pico W 多主题本地存储与硬件按键切换设计规格书

- **文档名称**：Pico W 多主题本地存储与硬件按键切换设计规格
- **创建日期**：2026-11-10
- **规格版本**：v1.0.0
- **目标平台**：Raspberry Pi Pico W（RP2040 双核 Cortex-M0+, 264KB SRAM, 2MB QSPI Flash）
- **关联规范**：`docs/page_switch_policy.md`（双页面自动切换与防闪烁光栅化规范）

---

## 1. 需求与设计目标

### 1.1 需求背景
当前 Pico W 桌面屏已具备双页面（电脑监控页 A ↔ 综合生活/战术巡检页 B）自动切换能力，但界面色彩固定为单套 Material Design 3 深色模式。用户希望固件内置五套风格迥异的主题（瑞士大字阶、复古字符终端、便当网格磁贴、工业示波波形、赛博战术机甲 HUD），并通过 Pico W 外接物理按键实时循环切换，同时支持断电记忆，重启后自动停留在上次选定的主题。

### 1.2 核心设计原则
1. **接线极致简洁**：仅需单个普通轻触按键，零外围阻容元器件；
2. **极速无缝响应**：按下瞬间屏幕 3~5ms 内整体完成调色板切换，无黑帧闪烁，无弹窗遮挡监控数据；
3. **零死锁与防磨损持久化**：采用 5 秒延迟提交与“先读比对跳过”策略，杜绝频繁擦写 Flash；结合 RP2040 硬件级双核安全锁定（`multicore_lockout`），彻底消除 XIP 总线死锁（Hard Fault）风险；
4. **零 RAM 膨胀**：五套主题以结构体常量形式常驻 Flash（每套 20 字节，总计 100 字节），Core 1 寄存器基址直接寻址，零动态内存分配。

---

## 2. 硬件电路与引脚分配

### 2.1 GPIO 物理映射
ST7735S 屏幕已占用 GP17~GP21。选用位于开发板边缘、紧邻 GND 引脚的 **GP15** 作为主题切换按键引脚：

| 元件 / 信号 | Pico W 引脚 | 物理引脚编号 | 模式配置 | 作用说明 |
| :--- | :--- | :---: | :--- | :--- |
| **按键 Pin 1** | **GP15** | Pin 20 | `gpio_init` + `gpio_pull_up` | 空闲悬空为 3.3V 高电平，按下为 GND 低电平 |
| **按键 Pin 2** | **GND** | Pin 18 或 23 | 芯片数字地 | 机械轻触按键直连，外围无需任何电阻电容 |
| **屏幕总线** | GP17~GP21 | Pin 22,24,25,26,27 | 硬件 SPI0 | 驱动屏幕刷新，引脚电气隔离，杜绝串扰 |

---

## 3. 五套主题色彩空间与数据结构

### 3.1 调色板结构体定义（`ui_theme.h`）
每个主题由 10 个 16-bit RGB565 颜色令牌构成（20 字节）：

```c
#ifndef PICO_FREERTOS_FIRMWARE_INCLUDE_UI_THEME_H_
#define PICO_FREERTOS_FIRMWARE_INCLUDE_UI_THEME_H_

#include <stdint.h>

typedef struct {
  const char* name;       /**< 纯中文主题名称，如 "赛博战术" */
  uint16_t background;    /**< 屏幕背景画布 */
  uint16_t surface;       /**< 基础卡片容器 */
  uint16_t surface_high;  /**< 嵌套胶囊/高亮容器 */
  uint16_t track;         /**< 进度/走势槽底色 */
  uint16_t text;          /**< 主要文字（高对比） */
  uint16_t muted;         /**< 次要/标签文字 */
  uint16_t primary;       /**< 主强调色（CPU/联机/时间） */
  uint16_t secondary;     /**< 次强调色（GPU/星期） */
  uint16_t tertiary;      /**< 第三强调色（内存/农历） */
  uint16_t error;         /**< 告警色（爆满/断网） */
} UiThemePalette;

typedef enum {
  kThemeSwissTypographic = 0, /**< 瑞士大字阶流 */
  kThemeTuiConsole       = 1, /**< 复古字符终端流 */
  kThemeBentoGrid        = 2, /**< 便当网格磁贴流 */
  kThemeOscilloscope     = 3, /**< 工业示波波形流 */
  kThemeTacticalMecha    = 4, /**< 赛博战术机甲 HUD 流 */
  kThemeCount            = 5
} UiThemeId;

const UiThemePalette* ui_theme_get(UiThemeId theme_id);

#endif /* PICO_FREERTOS_FIRMWARE_INCLUDE_UI_THEME_H_ */
```

### 3.2 静态只读主题表（`ui_theme.c`）
数据常驻 Flash 只读段（`.rodata`），零 RAM 开销：
```c
static const UiThemePalette s_themes[kThemeCount] = {
    [kThemeSwissTypographic] = {
        .name = "瑞士大字", .background = 0x0000, .surface = 0x1082, .surface_high = 0x2104,
        .track = 0x3186, .text = 0xF7BE, .muted = 0x7BCF, .primary = 0x07BA,
        .secondary = 0x4410, .tertiary = 0xFE60, .error = 0xF94A
    },
    [kThemeTuiConsole] = {
        .name = "字符终端", .background = 0x0020, .surface = 0x08C1, .surface_high = 0x1163,
        .track = 0x1A04, .text = 0xD7FB, .muted = 0x5CEC, .primary = 0x07EC,
        .secondary = 0x1F34, .tertiary = 0xFE00, .error = 0xF9A9
    },
    [kThemeBentoGrid] = {
        .name = "便当磁贴", .background = 0x0861, .surface = 0x18E4, .surface_high = 0x2987,
        .track = 0x3A09, .text = 0xF7BF, .muted = 0x8CB4, .primary = 0x07BA,
        .secondary = 0xFE00, .tertiary = 0xFE00, .error = 0xF927
    },
    [kThemeOscilloscope] = {
        .name = "工业示波", .background = 0x0862, .surface = 0x10E5, .surface_high = 0x2188,
        .track = 0x2A0B, .text = 0xF7BF, .muted = 0x7CB5, .primary = 0x07EC,
        .secondary = 0x3DFF, .tertiary = 0xFE00, .error = 0xFB90
    },
    [kThemeTacticalMecha] = {
        .name = "赛博机甲", .background = 0x0022, .surface = 0x18A5, .surface_high = 0x2929,
        .track = 0x396B, .text = 0xF7DF, .muted = 0x9477, .primary = 0x07BA,
        .secondary = 0xFA74, .tertiary = 0xFE60, .error = 0xF94A
    }
};
```

---

## 4. 按键输入、消抖与即时渲染时序

### 4.1 20ms 就地轮询采样消抖状态机
- 不注册 GPIO 硬件中断，避免机械按键 5~15ms 抖动毛刺造成高频中断雪崩与双核互锁死锁。
- 由 Core 1 上已有的 20ms（50Hz）周期渲染任务 `vTaskUIRender` 就地采样：
  1. 每 20ms 读取一次 `gpio_get(kThemeButtonPin)`；
  2. 连续 2 次（40ms）采样为低电平（按压）且此前处于释放态，判定为有效“单击”；
  3. 按键保持不放不重复触发，释放回弹后状态机自动复位。

### 4.2 即时渲染与脏矩形调度
- 判定单击后：
  1. 推进主题索引：`s_active_theme = (s_active_theme + 1) % kThemeCount;`
  2. 重置 5 秒持久化延迟计数器：`s_flash_save_countdown_ms = 5000;`
  3. 标记全屏脏矩形：追加 `{0, 0, 128, 160}`，告知下一阶段渲染管线执行整体调色板重铺；
  4. 画面在 3~5ms 内以全新主题色调推屏完成，无卡顿、无残影、无遮挡。

---

## 5. Flash 尾部扇区断电安全持久化（防死锁与抗磨损）

### 5.1 扇区划分
- RP2040 板载 2MB Flash（`0x10000000` ~ `0x10200000`）；
- 选取 Flash **最后一个 4KB 扇区（Sector 511）**：
  - 物理偏移：`FLASH_TARGET_OFFSET = (2048 - 4) * 1024 = 0x1FF000`（2,093,056）；
  - 内存映射只读地址：`XIP_BASE + 0x1FF000 = 0x101FF000`；
  - 距离当前固件镜像末尾有超过 1.1MB 物理余量，绝无覆盖代码风险。

### 5.2 数据结构定义
```c
#define THEME_FLASH_MAGIC 0x54484D45u /* ASCII "THME" */

typedef struct __attribute__((aligned(4))) {
  uint32_t magic;      /**< 校验魔数 */
  uint8_t  theme_id;   /**< 选中的主题索引 (0..4) */
  uint8_t  reserved[3];/**< 4字节对齐填充 */
  uint32_t checksum;   /**< 校验和: magic ^ theme_id */
} PersistentThemeConfig;
```

### 5.3 四重双核安全写入防线（彻底消灭 Hard Fault）
1. **5 秒延迟提交（Anti-Thrashing）**：
   用户连续按键切换时，每次仅在 RAM 中重置 5000ms 倒计时。当且仅当停留在某主题连续 5 秒无任何按键后，才触发实际 Flash 写入，消除 99% 的无效擦写。
2. **先读后比对跳过（Skip-if-equal）**：
   写入前直接读取 XIP 内存 `0x101FF000`，若 Flash 中已有配置有效且 `theme_id` 与目标值相同，**直接跳过擦写（0ms 耗时，0 写入磨损）**。
3. **双核安全互锁隔离（Multicore Lockout）**：
   - 在主核启动阶段预先初始化：`multicore_lockout_victim_init()`；
   - 触发 Flash 写入前，调用 `multicore_lockout_start_blocking()`。此函数通过芯片内部硬件 FIFO 发送特定握手包，迫使 Core 0 暂停 XIP 访问并在 SRAM 中自旋阻塞等待；
   - 调用 `save_and_disable_interrupts()` 关闭本核中断；
   - 安全调用 SDK 原生驱动：
     `flash_range_erase(FLASH_TARGET_OFFSET, FLASH_SECTOR_SIZE);`
     `flash_range_program(FLASH_TARGET_OFFSET, (const uint8_t*)&cfg, FLASH_PAGE_SIZE);`
   - 调用 `restore_interrupts()` 恢复中断；
   - 调用 `multicore_lockout_end_blocking()` 释放 Core 0，恢复多核并发。
4. **出厂全空自动回退**：
   开机读取时若 `magic != THEME_FLASH_MAGIC`（如全新出厂全 `0xFF`）或 `checksum` 校验失败，安全回退加载默认主题 0，绝无未定义行为。

---

## 6. 模块职责与对外契约接口

```text
pico_freertos_firmware/
├── include/
│   ├── ui_theme.h          # 主题调色板结构体与枚举定义
│   └── theme_storage.h     # Flash 延迟持久化对外接口
└── src/
    ├── display/
    │   ├── ui_theme.c      # 5套主题常量表与读取函数
    │   └── ui_render.c     # 20ms GP15 按键轮询采样与全屏换色
    └── platform/
        └── theme_storage.c # 双核互锁 Flash 扇区读取与延迟保存
```

### 关键接口契约
- `void theme_storage_init(void)`：开机阶段读取 Flash 恢复 `g_active_theme_id`；
- `void theme_storage_request_save(UiThemeId theme_id)`：按键切换时调用，标记脏状态并重置 5 秒倒计时；
- `void theme_storage_process_countdown(uint32_t elapsed_ms)`：由周期任务推进倒计时并在归零时执行安全 Flash 写入。

---

## 7. 规格自检清单

- [x] **占位符扫描**：全文无“待定”、“TODO”或未定义的模糊引脚或数据结构。
- [x] **内部一致性**：GP15 接线、20ms 消抖采样、10MHz SPI、双缓冲 Ping-Pong、多核锁定流程与 `docs/page_switch_policy.md` 完全对齐。
- [x] **范围检查**：严格聚焦在 5 套主题存储、GP15 单键循环切换与 Flash 安全持久化，不引入任何无关外设或协议改动。
- [x] **模糊性消除**：明确了 QSPI Flash 扇区偏移 `0x1FF000`、双核互锁调用时序与 5 秒延迟提交去抖机制。
