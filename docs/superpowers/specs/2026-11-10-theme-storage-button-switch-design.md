# Pico W 多主题本地存储与硬件按键切换设计规格书

| 项目 | 内容 |
| :--- | :--- |
| 规格版本 | **v1.7.0** |
| 创建日期 | 2026-11-10 |
| 最后更新 | 2026-11-11 |
| 目标平台 | Raspberry Pi Pico W（RP2040，Cortex-M0+ 双核，264 KB SRAM，2 MB QSPI Flash） |
| 起草基线 | `feature/pico-freertos-firmware`，`be5aa3e`（本轮核对时 HEAD 为 `855f322`） |
| 关联规范 | `docs/page_switch_policy.md`（双页面自动切换与防闪烁光栅化规范） |
| 视觉参考 | `docs/superpowers/specs/screen_designs_5styles_dual_pages.html`（**仅配色意向参考，非像素验收基准**） |

## 变更记录

| 版本 | 日期 | 摘要 |
| :--- | :--- | :--- |
| v1.0.0 | 2026-11-10 | 初版：五套主题、GP15 按键、Flash 尾部扇区持久化 |
| v1.1.0 | 2026-11-10 | 改用 `flash_safe_execute`，`theme_id` 纳入 `SystemSnapshot` |
| v1.2.0 | 2026-11-11 | 依据首轮三专家复审重写：裁定范围为「仅换配色」、冻结主题 0 基线、修正 244 字节越界读、补构建依赖与组合根、补验收/风险/回滚/影响面/未决项五章 |
| v1.3.0 | 2026-11-11 | 依据第二轮复审修正：§3.3 改为「模板 × 令牌」落点表并据此生成 17 项配对、补 2 项令牌互辨度、登记主题 0 已知缺陷、重算 82 % 折算（7 对跌破）、修正 603 ms 最坏时序与重试预算、改用 `xTaskCreateStaticAffinitySet`、链接期守卫改用 `section_extra_post_end.incl` 覆盖、平台结构体下沉到 `.c`、补 `theme_storage` 宿主测试与可执行验收步骤、修正全部计数与宽度错误 |
| v1.4.0 | 2026-11-11 | 依据第三轮复审修正：删除不可复现的 82 % 抗锯齿折算表、主题 1/3 改名并对调主题 4 的 `primary`/`tertiary`、`track` 判据改用 ΔL\* 与方向、补三个纯逻辑单元（配置编解码 / 提交策略 / 主题推进）与对应测试目标、依赖断言改为仓库根 cwd 的 `${REL_FIRMWARE}/` 前缀并降为 3 条、修正 `flash_safe_execute` 失败路径与堆预检语义、补 `add_test` 要求与两个漏登记的头文件 |
| v1.5.0 | 2026-11-11 | 依据第四轮复审修正：结果码拆为 6 个（enter/exit 超时不再一码两义、`-9` 单独成码）、堆预检收敛到 `theme_storage_commit()` 内、重试策略与状态机分派对齐、403/603/650 三口径定名、XIP 论证改为「回调体在 XIP 开启时执行」、`panic` 移到 `supervisor_start()` 之后、依赖图补边、`track` 判据改用有符号 `ΔL*(surface, track)` |
| v1.5.1 | 2026-11-11 | 依据第四轮定向闭环复核修正：`ErrVerify` 拆分出 `ErrInvalidArg` 并补第二次重试的终止分支（`s_verify_retried`）、`-9` 分支标注为现状不可达并删除「唯一可安全重来」的过度断言、§9 合并同根因的两行矛盾评级 |
| v1.5.2 | 2026-11-11 | 依据第五轮逐码矩阵复核修正：第 2/3 步补状态前提（否则 `Failed` 会被下一个节拍拉回 `Writing`，形成 0.5 s 一轮的冻结循环）、`NoResources` 终止态改为 `Failed`、`InvalidArg` 标注为现实不可达、状态机整体下沉到被测单元 `theme_commit_policy.c`、把「只有握手超时才复位」限定到提交分派口径 |
| v1.5.3 | 2026-11-11 | 依据第六轮复核修正：第 3 步补承接语（与第 2 步同受 `state == Pending` 约束）、`s_verify_retried` 归属改回策略单元、补 `s_committed` 初值与两条更新路径、`Idle` 的离开条件 |
| v1.6.0 | 2026-11-11 | 依据 1.77 in / 116 ppi / 0.219 mm 像素间距的物理尺寸复判，主题 0 改两个常量：`track` `0x31a7`→`0x0020`（槽底由亮于卡片转为暗于卡片，`ΔL*(surface,track)` −10.06→+10.75）、`tertiary` `0xf5f1`→`0xFE00`（与 `error` 的 ΔE 23.1→73.7、绿色盲模拟 21.1→67.8）；两项层级缺陷（6.59 / 4.72）判定为「大色块可辨、阈值不追溯」并保留登记；§3.4.2 五格、§3.4.3 两格、§3.4.4 缺陷表四行减为两行，基线口径由「冻结」改为「已更新」 |
| v1.7.0 | 2026-11-11 | 依据 D-2 决议给页面 A 顶栏加 `HH:MM:SS`（8 × 14 ASCII，64 px，居 88 px 空档，x = 32…95，`text` 色、未同步 `muted`）：§1.2 增第 5 条范围并改写边界句、§1.3 删去对应非目标、§3.3 落点表增一行、§3.4.1 第 3/4 项依据补「顶栏时刻」（不新增配对，17 项不变）、§4.4 增每秒一个 `{32,1,64,14}` 脏矩形（落 1 个切片、4096 B、3.3 ms/s）、§7 影响面、§8.5 增 V-2、§11.2 补宽度实测 |

---

## 1. 需求、范围与非目标

### 1.1 需求背景

Pico W 桌面状态屏已具备双页面自动切换能力（仪表盘页 A ↔ 时钟页 B），界面色彩固定为单套
Material Design 3 深色。本规格在**不改变现有卡片版式**的前提下，引入可切换的配色主题与
一处顶栏时刻，并通过 Pico W 外接物理按键即时切换，掉电后仍停留在上次选择。

### 1.2 范围（v1 交付什么）

1. 新增 5 套配色主题，加上现有 M3 深色共 **6 套可选**；
2. GP15 单按键循环切换，2/2 采样消抖；
3. 主题值写入 Flash 最后一个扇区，掉电记忆；
4. 主题切换触发一次全屏重绘，切片双缓冲保证无黑帧；
5. 页面 A 顶栏在两枚状态图标之间新增 `HH:MM:SS` 时刻（88 px 空档、8 × 14 ASCII）。

**明确边界：v1 只更换 10 个颜色令牌，外加顶栏时刻这一处新增绘制；不更换排版、字号、文案、图标。**
五套主题共用现有 M3 卡片版式。`screen_designs_5styles_dual_pages.html` 中出现的
6–26 px 字号、磁贴、波形、斜切顶栏、字符块，**全部不在 v1 范围内**——固件只有
8 × 14 ASCII、14 × 14 中文、14 × 26 时钟数字三档字号，且无字重、无缩放、无折线绘制能力。

### 1.3 非目标（v1 明确不做）

| 不做的事 | 理由 |
| :--- | :--- |
| 每套主题独立排版 | 需第二套小字号点阵（6–10 px）、折线绘制、斜切扫描；且 6 px 中文在 128 × 160 上不可读 |
| 扩展中文字库与 ASCII 字集 | 现有 341 字白名单 + 71 个 ASCII 已覆盖全部屏幕文案；扩字库牵扯 5 处硬编码与资源重生成 |
| 主题名上屏 | 除 §1.2 第 5 条的顶栏时刻外，屏幕文案不动；主题名只用于串口日志，UTF-8 日志由主机终端渲染，不经过字体引擎 |
| 双扇区 A/B 备份 | 已评估：成本 4 KB + 约 20 行代码，掉电最坏后果是「回到默认主题」，v1 不承担 |
| 长按加速滚动、组合键恢复默认 | 单按键语义保持「按一次切一个」 |

### 1.4 术语

| 术语 | 含义 |
| :--- | :--- |
| 主题（theme） | 一套 10 个 RGB565 颜色令牌，由 `UiThemeId` 索引 |
| 页面模式（ui_mode） | 仪表盘页 A / 时钟页 B，由 PC 指标超时驱动，与主题**正交** |
| 脏矩形 | 需要重绘的屏幕矩形区域，由 `ui_layout` 计算 |
| 切片 | 屏幕按 16 行切分，128 × 160 共 10 片 |
| 提交（commit） | 把当前 `theme_id` 写入 Flash 的动作 |

---

## 2. 硬件电路与引脚分配

### 2.1 GPIO 映射

ST7735S 占用 GP17–GP21；其中只有 SCK/MOSI 走 SPI0 硬件功能引脚，
CS/DC/RST 是普通 GPIO，由驱动逐位控制。按键选用同一侧边缘的 **GP15**。

| 元件 / 信号 | GPIO | 物理引脚 | 模式配置 | 说明 |
| :--- | :--- | :---: | :--- | :--- |
| 按键 Pin 1 | **GP15** | Pin 20 | `gpio_init` + `gpio_pull_up` | 空闲高电平，按下拉低到 GND |
| 按键 Pin 2 | GND | Pin 18 或 Pin 23 | 数字地 | 机械轻触按键直连，无外围阻容 |
| 片选 CS | GP17 | Pin 22 | 普通 GPIO 输出 | 每切片首尾翻转 |
| 时钟 SCK | GP18 | Pin 24 | SPI0 SCK | 10 MHz |
| 数据 MOSI | GP19 | Pin 25 | SPI0 TX | 只写不读 |
| 复位 RST | GP20 | Pin 26 | 普通 GPIO 输出 | 上电拉低复位 |
| 数据/命令 DC | GP21 | Pin 27 | 普通 GPIO 输出 | 高=数据，低=命令 |

GP15 相邻引脚是 GP14（Pin 19）与 GP16（Pin 21），**并非紧邻 GND**；GND 取自 Pin 18 或 Pin 23。

### 2.2 按键电气与时序预算

- 内部上拉约 50 kΩ，按下电流约 66 µA，机械按键无需去抖电容；
- 采样周期 = 渲染任务周期 = 20 ms，连续 2 拍判定 = **40 ms**；
- 该方案的前提假设：触点抖动持续时间 **< 40 ms**（常见轻触按键 0.5–10 ms）。抖动超过
  40 ms 时仍可能被判为两次单击，属已知限制；
- 单按键、无中断、无独立任务——采样复用 Core 1 已有的 20 ms 渲染节拍。

---

## 3. 主题调色板

### 3.1 数据结构

主题**编号**是一个纯类型，不许依赖任何模块；调色板表属于表现层。因此拆成两个头文件：

```c
/* include/ui_theme_id.h —— 零依赖，state / display / app 都可 include */
#ifndef PICO_FREERTOS_FIRMWARE_INCLUDE_UI_THEME_ID_H_
#define PICO_FREERTOS_FIRMWARE_INCLUDE_UI_THEME_ID_H_

typedef enum {
  kThemeM3Dark = 0,      /**< M3 深色；track/tertiary 见 §3.2 注 */
  kThemeMonoRed = 1,     /**< 黑琥珀 */
  kThemePhosphor = 2,    /**< 荧光绿 */
  kThemeCeladon = 3,     /**< 霓虹青 */
  kThemeDeepTeal = 4,    /**< 深海青 */
  kThemeNeonMagenta = 5, /**< 霓虹洋红 */
  kThemeCount = 6
} UiThemeId;

#endif /* PICO_FREERTOS_FIRMWARE_INCLUDE_UI_THEME_ID_H_ */
```

```c
/* include/ui_theme.h */
#ifndef PICO_FREERTOS_FIRMWARE_INCLUDE_UI_THEME_H_
#define PICO_FREERTOS_FIRMWARE_INCLUDE_UI_THEME_H_

#include <stdint.h>

#include "ui_theme_id.h"

/** @brief 一套主题的全部颜色令牌（RP2040 上 sizeof = 24 字节）。 */
typedef struct {
  const char* name;      /**< 日志用名称，不上屏 */
  uint16_t background;   /**< 屏幕背景画布 */
  uint16_t surface;      /**< 卡片底色 */
  uint16_t surface_high; /**< 胶囊/高亮容器底色 */
  uint16_t track;        /**< 进度条槽底 */
  uint16_t text;         /**< 主文字 */
  uint16_t muted;        /**< 次要文字与标签 */
  uint16_t primary;      /**< 主强调：CPU / 联机 / 时钟 */
  uint16_t secondary;    /**< 次强调：GPU / 星期 / 同步 */
  uint16_t tertiary;     /**< 第三强调：内存 / 农历 */
  uint16_t error;        /**< 告警：断网 / 内存爆满 */
} UiThemePalette;

/** @brief 取主题调色板；越界 id 回退到 kThemeM3Dark，永不返回 NULL。 */
const UiThemePalette* ui_theme_get(UiThemeId theme_id);

#endif /* PICO_FREERTOS_FIRMWARE_INCLUDE_UI_THEME_H_ */
```

结构体大小注记：`const char*` 4 字节 + 10 个 `uint16_t` 20 字节 = **24 字节**，
6 套共 144 字节，链接在 Flash 的 `.rodata`，RAM 增量为 0。
（v1.1.0 曾误写为 20 字节。）

**命名口径**：主题名只由**主轴色与底色**两部分构成，次/三级强调色取互补色，不承诺排版、
字重、图标风格。这条声明是为了避免「名字承诺氛围、实际只交付 accent」的误解——实测六套的
`background` 两两 ΔE 只有 2.1–8.1，画布/底色几乎共用，差异集中在 `primary`。

v1.3.0 的三个名字与 `primary` 实测色相不符，v1.4.0 据此改名或改色：

| 主题 | v1.3.0 名字 | `primary` 色相 | 处置 |
| :--- | :--- | :--- | :--- |
| 1 | 黑白赤 | `0xFB40` = #FF6900，**24.7°（橙）** | 改名 **黑琥珀** |
| 3 | 青瓷 | `0x07F3` = #00FF9C，156.7°（霓虹薄荷） | 改名 **霓虹青** |
| 4 | 深海青 | `0xFFE0` (#FFFF00, 60°) 与 `0x07BA` (#00F7D6, 172°) 位置颠倒 | **对调** `primary`/`tertiary`，名字保留 |

对调只改两个字段，主轴色回到「青」（172°），代价是 §3.4.2 的 5 格与 §3.4.3 的 4 格数字要重算，
下表与矩阵均已按对调后的取值给出。枚举标识符 `kThemeMonoRed` / `kThemeCeladon` 保持
不变——它们是代码符号，与展示名解耦。

### 3.2 静态主题表（`src/display/ui_theme.c`）

```c
static const UiThemePalette s_themes[kThemeCount] = {
    [kThemeM3Dark] = {
        .name = "M3 深色",
        .background = 0x1083, .surface = 0x1904,
        .surface_high = 0x2945, .track = 0x0020,
        .text = 0xe71d, .muted = 0x9493,
        .primary = 0xae3f, .secondary = 0xa699,
        .tertiary = 0xFE00, .error = 0xf5d6,
    },
    [kThemeMonoRed] = {
        .name = "黑琥珀",
        .background = 0x0000, .surface = 0x2124,
        .surface_high = 0x39E7, .track = 0x0000,
        .text = 0xFFFF, .muted = 0x9D13,
        .primary = 0xFB40, .secondary = 0x65B6,
        .tertiary = 0xFE60, .error = 0xF94A,
    },
    [kThemePhosphor] = {
        .name = "荧光绿",
        .background = 0x0020, .surface = 0x0982,
        .surface_high = 0x1224, .track = 0x0020,
        .text = 0xD7FB, .muted = 0x8611,
        .primary = 0x07E0, .secondary = 0x1F34,
        .tertiary = 0xFD00, .error = 0xF9A9,
    },
    [kThemeCeladon] = {
        .name = "霓虹青",
        .background = 0x0861, .surface = 0x2165,
        .surface_high = 0x3208, .track = 0x0861,
        .text = 0xF7BF, .muted = 0xA597,
        .primary = 0x07F3, .secondary = 0x5EBF,
        .tertiary = 0xFE00, .error = 0xF927,
    },
    [kThemeDeepTeal] = {
        .name = "深海青",
        .background = 0x0862, .surface = 0x19A7,
        .surface_high = 0x2A2B, .track = 0x0862,
        .text = 0xF7BF, .muted = 0x9DB9,
        .primary = 0x07BA, .secondary = 0x3DFF,
        .tertiary = 0xFFE0, .error = 0xFB90,
    },
    [kThemeNeonMagenta] = {
        .name = "霓虹洋红",
        .background = 0x0022, .surface = 0x2166,
        .surface_high = 0x3209, .track = 0x0022,
        .text = 0xF7DF, .muted = 0xAD3A,
        .primary = 0xFA19, .secondary = 0x07FF,
        .tertiary = 0xFE60, .error = 0xF94A,
    },
};

const UiThemePalette* ui_theme_get(UiThemeId theme_id) {
  if ((unsigned)theme_id >= (unsigned)kThemeCount) {
    return &s_themes[kThemeM3Dark];
  }
  return &s_themes[theme_id];
}
```

**硬约束**：`kThemeM3Dark` 的 10 个取值里，8 个必须与 `src/display/ui_render.c:22-31`
现有 `kColor*` 枚举**逐字节相等**（`0x1083 / 0x1904 / 0x2945 / 0xe71d / 0x9493 /
0xae3f / 0xa699 / 0xf5d6`）；`track` 与 `tertiary` 在 v1.6.0 被显式改值
（`0x31a7→0x0020`、`0xf5f1→0xFE00`），实现落地时须同步修改 `ui_render.c` 的枚举，
并把 `tests/test_ui_render.c:16-25` 里对应的两个 `kTest*` 常量改成同一新值。

`tests/test_ui_render.c:16-25` 把这组值复制成了 10 个 `kTest*` 常量，:237–:375 之间
**共 22 处引用**（其中 10 处常量写在 `assert(` 同一行，另 12 处位于续行）。
断言数的是「该颜色的像素个数」，与色值本身无关：两边同步改值后，**22 处断言的谓词与
计数都不变**。只有 `background` 被 `tests/test_pipeline_e2e.c:27` 硬编码为 `0x1083`，
而本次没有改 `background`。

### 3.3 令牌落点表与硬规则

先用代码事实把「哪个令牌画在哪个底色上」钉死（行号对应 `ui_render.c`）：

| 绘制点 | 前景令牌 | 底色 | 行号 |
| :--- | :--- | :--- | :--- |
| 顶栏 PC 图标 | `primary` \| `error` | `background` | :210-:212 |
| 顶栏 WiFi 图标（仪表盘页） | `primary` \| `error` | `background` | :213-:216 |
| 顶栏 WiFi 图标（时钟页） | `primary` \| `error` | `background` | :296-:299 |
| 卡片 accent 竖条 | `primary` \| `secondary` \| `tertiary` \| `error` | `surface` | :224（RAM 阈值见 :286） |
| 卡片标签 | `muted` | `surface` | :227-:228 |
| 卡片数值 | `text` | `surface` | :239-:241 |
| 进度条槽 | `track`（被绘制对象） | `surface` | :243-:244 |
| 进度条填充 | `primary` \| `secondary` \| `tertiary` \| `error` | `track` | :246-:248 |
| 网速 RX/TX | `muted` \| `text` | `background` | :255-:275 |
| 天气卡文字 | `text` \| `muted` | `surface` | :309-:330 |
| 时钟数字 | `primary`（未同步占位 `--:--:--` 为 `muted`） | `surface` | :344-:345、:347-:348 |
| 日期行 | `text` | `surface` | :358-:359 |
| 星期胶囊 / 同步胶囊 | `secondary` | `surface_high` | :361-:362、:366-:367、:378-:381 |
| 农历行 | `tertiary` | `surface` | :374-:375 |
| 顶栏时刻（v1.7.0 新增） | `text`（未同步时 `muted`） | `background` | 新增于 `render_top_bar()` 末尾（当前 :203-:219） |

由此得到四条硬规则：

1. **落点封闭**：任何令牌只许出现在上表列出的底面上。新增落点必须同时补进 §3.4 的配对表，
   否则该项对比度无人负责。
2. **`track` 只做槽底，且必须比 `surface` 更暗**：判据用 **ΔL\*(`surface`,`track`) ≥ 10**
   （正值即槽底更暗；全文 `ΔL*(A,B)` 一律定义成 A−B），
   不用 CR——CR 衡量的是「文字能否被读」，而整条槽是一个大色块。槽底比卡片亮会把强调色的
   填充挤到 2:1 附近。新主题实测 ΔL\* = 13.80 / 15.73 / 13.92 / 16.64 / 16.09 且全部更暗；
   主题 0 在 v1.6.0 把 `track` 由 `0x31a7` 改为 `0x0020`，实测 **+10.75**，方向转正。
   **六套全部满足，不再有豁免项。**
3. **`surface_high` 只承载 `secondary`**：`ui_render.c` 里 `surface_high` 除 :24 的枚举定义外
   只出现在 :361-:362、:366-:367、:378-:381，全部配 `secondary`。
4. **文字令牌（`text` / `muted`）不得画在 `surface_high` 上**：五套新主题在该底色上的
   `muted` 满覆盖 CR 只有 **4.29–5.36**（主题 1 的 4.29 已低于 4.5），再叠加 4 bpp 抗锯齿
   只会更差。`text` 在该底色上更高，但同样不属落点表，一并不许。

### 3.4 可测的对比度验收指标

对比度按 WCAG 2.1 相对亮度公式计算，RGB565 按位复制扩展到 8 bit
（`r8 = (r5 << 3) | (r5 >> 2)`）。ΔL\* 用于大色块边界，CR 用于文字与图形，ΔE 用于令牌互辨。

#### 3.4.1 阈值（由 §3.3 落点表归并生成）

| # | 配对 | 阈值 | 依据（落点） |
| :---: | :--- | :---: | :--- |
| 1 | `text` / `surface` | ≥ 4.5 | 卡片数值、天气、日期 |
| 2 | `muted` / `surface` | ≥ 4.5 | 卡片标签、天气次要文字 |
| 3 | `text` / `background` | ≥ 4.5 | 网速数值、顶栏时刻 |
| 4 | `muted` / `background` | ≥ 4.5 | 网速 RX/TX 标签、顶栏时刻的未同步占位 |
| 5 | `primary` / `surface` | ≥ 4.5 | accent 竖条、进度填充、时钟数字 |
| 6 | `secondary` / `surface` | ≥ 4.5 | GPU accent、GPU 进度填充 |
| 7 | `secondary` / `surface_high` | ≥ 4.5 | 星期胶囊、同步胶囊 |
| 8 | `tertiary` / `surface` | ≥ 4.5 | 内存 accent、农历行 |
| 9 | `primary` / `track` | ≥ 3.0 | CPU 进度填充（图形） |
| 10 | `secondary` / `track` | ≥ 3.0 | GPU 进度填充（图形） |
| 11 | `tertiary` / `track` | ≥ 3.0 | 内存进度填充（图形） |
| 12 | `error` / `track` | ≥ 3.0 | 内存告警填充（图形） |
| 13 | `error` / `surface` | ≥ 3.0 | 卡片内断网 accent（图形） |
| 14 | `primary` / `background` | ≥ 3.0 | 顶栏联机图标（图形） |
| 15 | `error` / `background` | ≥ 3.0 | 顶栏断网图标（图形） |
| 16 | ΔL\*(`surface`, `background`) | ≥ 10 | 卡片与画布边界 |
| 17 | ΔL\*(`surface_high`, `surface`) | ≥ 8 | 胶囊与卡片边界 |

口径：上表 1–15 项是 **CR 配对**，由 §3.3 那 14 行落点表**归并去重**而来，不是「一行一项」：
一行里出现多个前景令牌的（行 4 的 accent 竖条带 `primary|secondary|tertiary|error`，
行 8 的进度填充同样四种，行 1–3 的顶栏图标带 `primary|error`），每个令牌与底色的组合各出一项；
同一组合在多行重复出现的（如 `primary`/`background` 在行 1/2/3 都出现）只保留一项。
第 16/17 项是 **容器 ΔL\***，用于大色块边界，不承载任何文字。共计 **15 + 2 = 17 项**。

第 9/12 项与第 14/15 项在主题 1–5 上数值重合，因为五套的 `track` 等于 `background`；
主题 0 两者不等，故四项都保留。

ΔL\* 一律保留 2 位小数。CIELAB 的白点常量取法（D65 的 0.95047/1.00000/1.08883 与
0.9505/1.0/1.089）会让末位产生 ±0.01 的差异，不影响 ≥ 10 / ≥ 8 的判定；若复算结果在
末位上和本文档差 0.01，以本文档为准即可，不必改阈值。

#### 3.4.2 实测矩阵

| 配对 | 0 M3 深色 | 1 黑琥珀 | 2 荧光绿 | 3 霓虹青 | 4 深海青 | 5 霓虹洋红 |
| :--- | ---: | ---: | ---: | ---: | ---: | ---: |
| 1 `text`/`surface` | 13.12 | 15.68 | 13.34 | 13.52 | 12.40 | 13.75 |
| 2 `muted`/`surface` | 5.41 | 6.02 | 7.06 | 6.66 | 6.29 | 6.26 |
| 3 `text`/`bg` | 14.99 | 21.00 | 18.89 | 18.48 | 18.41 | 19.71 |
| 4 `muted`/`bg` | 6.18 | 8.06 | 9.99 | 9.10 | 9.34 | 8.98 |
| 5 `primary`/`surface` | 9.78 | 5.43 | 10.62 | 10.83 | 9.61 | **4.70** |
| 6 `secondary`/`surface` | 10.11 | 6.64 | 9.03 | 8.65 | 6.28 | 11.40 |
| 7 `secondary`/`sh` | 8.96 | **4.73** | 6.87 | 6.41 | **4.77** | 8.43 |
| 8 `tertiary`/`surface` | 10.30 | 10.59 | 7.23 | 8.96 | 12.31 | 9.65 |
| 9 `primary`/`track` | 12.18 | 7.27 | 15.04 | 14.80 | 14.27 | 6.73 |
| 10 `secondary`/`track` | 12.59 | 8.89 | 12.79 | 11.81 | 9.33 | 16.34 |
| 11 `tertiary`/`track` | 12.84 | 14.18 | 10.24 | 12.24 | 18.27 | 13.83 |
| 12 `error`/`track` | 12.43 | 5.68 | 5.74 | 5.22 | 7.42 | 5.54 |
| 13 `error`/`surface` | 9.98 | 4.24 | 4.06 | 3.82 | 5.00 | 3.86 |
| 14 `primary`/`bg` | 11.17 | 7.27 | 15.04 | 14.80 | 14.27 | 6.73 |
| 15 `error`/`bg` | 11.40 | 5.68 | 5.74 | 5.22 | 7.42 | 5.54 |
| 16 ΔL\*(`surface`,`bg`) | **6.59** | 13.80 | 15.73 | 13.92 | 16.64 | 16.09 |
| 17 ΔL\*(`sh`,`surface`) | **4.72** | 11.15 | 8.67 | 9.32 | 8.20 | 9.32 |

**结论：6 套 × 17 项 = 102 个组合，100 个通过、2 个不通过**（主题 0 的第 16、17 项，
已登记为已知缺陷）。

主题 5 的 `primary`/`surface`（4.70）、主题 1 与 4 的 `secondary`/`sh`（4.73 / 4.77）
余量只有 4–6 %。4.5 是**纸面阈值**；ST7735S 模块的面板类型与批次并不统一（TN 与 IPS 都有），
视角偏差、个体差异与背光都会吃掉这点余量，
因此 §8.5 把「真机目视复验」列为独立验收项。新主题的内部目标取正文配对 ≥ 5.0、
图形配对 ≥ 3.4，上表三项在准入门槛之上、内部目标之下，属「允许但需复验」。

#### 3.4.3 令牌互辨度

代码里有两处**纯颜色编码的状态切换**，§3.4.1 的 15 项「令牌 vs 底色」覆盖不到：

| 位置 | 状态切换 | 判据 |
| :--- | :--- | :--- |
| 顶栏图标 :210-:216、:296-:299 | 联机 `primary` ↔ 断网 `error` | ΔE(`primary`, `error`) ≥ 25 |
| 内存 accent :286 | 正常 `tertiary` ↔ 爆满 `error` | ΔE(`tertiary`, `error`) ≥ 25 |

| 判据 | 0 M3 深色 | 1 黑琥珀 | 2 荧光绿 | 3 霓虹青 | 4 深海青 | 5 霓虹洋红 |
| :--- | ---: | ---: | ---: | ---: | ---: | ---: |
| ΔE(`primary`,`error`) | 44.9 | 46.4 | 169.6 | 153.4 | 114.8 | 64.7 |
| ΔE(`tertiary`,`error`) | 73.7 | 96.8 | 65.9 | 83.0 | 114.0 | 96.8 |
| ΔE 绿色盲模拟 (`primary`,`error`) | 45.5 | 30.5 | 37.8 | 35.6 | 29.7 | 65.2 |
| ΔE 绿色盲模拟 (`tertiary`,`error`) | 67.8 | 49.1 | 35.7 | 36.7 | 71.4 | 49.1 |

ΔE 用 CIE76（CIELAB 欧氏距离）；绿色盲模拟用 Machado 2009 严重度 1.0 矩阵在线性 RGB 下
变换后再算 ΔE。模拟值只作参考，不设硬门槛；硬门槛只作用于正常视觉的两行。

v1.5.3 及以前，主题 0 的 `tertiary`↔`error` 只有 23.1（模拟 21.1），意味着默认主题下
「内存爆满」在视觉上几乎不报警。v1.6.0 把 `tertiary` 换成 `0xFE00`（琥珀）后升到
**73.7（模拟 67.8）**，六套全部越过 25 的门槛，该项缺陷已消除。

#### 3.4.4 抗锯齿与已知缺陷

4 bpp 抗锯齿下细笔画不是满覆盖，有效对比度一定低于 §3.4.2 的标称值。**本规格不给出折算表。**

折算系数取决于点阵设计、笔画宽度、子像素覆盖率与面板 gamma。任何具体系数（例如常被引用的
「82 % 覆盖率」）都没有可验证的来源，换一个系数就会得到另一组跌破名单——写一张不可复算的
表比不写更糟，只会把「到底有几处跌破」变成伪问题。

因此本规格把两件事分开：

- **设计期门槛**由 §3.4.1 的 17 项标称 CR / ΔL\* 承担，可机器复算（§8.5 R-7 把它做成测试）；
- **细笔画可读性**（14 px 中文、8 px ASCII 小字）整体交给 §8.5 V-1 的实机目视复验，不设纸面数字。

**已知缺陷登记（须知情）**：

| 缺陷 | 实测 | 修法与代价 | 决定 |
| :--- | :--- | :--- | :--- |
| 主题 0 卡片层级不足 | ΔL\*(`surface`,`bg`) 6.59（阈值 10） | 改主题 0 的 `surface` 会直接改变出厂默认观感，并让 `test_ui_render` 的 22 处引用全部失效（须同步更新基线） | 不修 |
| 主题 0 胶囊层级不足 | ΔL\*(`sh`,`surface`) 4.72（阈值 8） | 同上，改 `surface_high` | 不修 |
| ~~主题 0 内存告警几乎不可辨~~ | ~~ΔE(`tertiary`,`error`) 23.1~~ | v1.6.0 改 `tertiary` 为 `0xFE00` | **已修**，现 73.7 |
| ~~主题 0 `track` 亮于 `surface`~~ | ~~ΔL\* = −10.06~~ | v1.6.0 改 `track` 为 `0x0020` | **已修**，现 +10.75 |

口径说明：**「新主题达标」不等于「六套都没问题」**。主题 0 仍是唯一出厂默认外观，
在卡片层级与胶囊层级两项上弱于新主题，合计 **两项视觉缺陷**，登记而不修。

不修的依据是实测物理尺寸（面板有效区 28.03 × 35.04 mm，对角 1.77 in，像素间距 0.219 mm，
116 ppi）：这两处是 26.3 × 8.8 mm 与 13.1 × 3.1 mm 的**大色块**（视角约 180′×60′ 与
90′×21′，按 500 mm 视距算），ΔL\* 6.59 / 4.72 在同时对比下仍然可辨，属「层级偏弱」
而非「读不出」；≥ 10 / ≥ 8 是为新主题定的加严内部目标，不是可读性下界，故不追溯主题 0。

反过来，v1.6.0 之所以修掉另外两项，也是尺寸决定的：`track` 只有 1.31 mm 高（9′），
槽底比卡片亮会被读成实心高亮段；`tertiary`↔`error` 的语义切换落在 0.88 mm 宽（6′）的
accent 竖条与 1.31 mm 高的填充上，薄条上的色度分辨远低于大色块，23.1 的 ΔE 不足以承载
「正常 / 告警」的区分。

---

## 4. 按键输入、状态联动与重绘

### 4.1 2/2 采样消抖（纯逻辑，零依赖）

```text
              连续 2 拍采样为低电平
        ┌──────────────────────────┐
        ▼                          │
   ┌─────────┐                ┌─────────┐
   │  IDLE   │                │ PRESSED │
   └─────────┘                └─────────┘
        │                          ▲
        └──────────────────────────┘
              连续 2 拍采样为高电平
```

- 按下：空闲态下连续 2 拍（40 ms）读到低电平 → 判定一次有效单击，状态转 `PRESSED`；
- 释放：`PRESSED` 态下连续 2 拍（40 ms）读到高电平 → 回到 `IDLE`，才允许下一次触发；
- 任意一拍读数与预期不符即把计数器清零，等价于「必须连续」；
- 该结构是 2/2 采样计数器，**不是双稳态电路**，「对称双稳态」是 v1.1.0 的措辞错误。

`include/button_filter.h` + `src/logic/button_filter.c` 只依赖 `<stdbool.h>` 与
`<stdint.h>`，宿主测试可直接驱动。接口：

```c
typedef struct {
  uint8_t low_samples;   /**< 连续低电平计数 */
  uint8_t high_samples;  /**< 连续高电平计数 */
  bool pressed;          /**< 是否处于已按下状态 */
} ButtonFilter;

void button_filter_reset(ButtonFilter* filter);
/** @brief 喂一拍采样，返回 true 表示本拍产生一次有效单击。 */
bool button_filter_update(ButtonFilter* filter, bool level_low);
```

**上电时按键已被按住**：`button_filter_reset()` 从 `IDLE` 起步，上电即读到低电平会在
40 ms 内判出一次单击，主题 0→1，5 s 后落盘。v1 **有意接受**这一行为：它只发生在用户
主动按住按键上电时，后果是「开机默认主题变成下一个」，不会变砖；若要禁止，必须再加一个
「必须先见一拍高电平」的状态位，为极低概率场景引入新状态不划算。

### 4.2 输入源抽象（表现层不碰 GPIO）

`ui_render.c` 不得 include `hardware/gpio.h`，也不得 include `board_button.h`。
沿用项目已有的 `DisplayBackend` 函数指针模式，注入一个输入源。该类型**单独放进零依赖头**，
否则 `platform/board_button.c` 为了定义实例就得 include `ui_render.h`，反向污染依赖图：

```c
/* include/ui_input.h —— 零依赖，只含 stdbool */
#ifndef PICO_FREERTOS_FIRMWARE_INCLUDE_UI_INPUT_H_
#define PICO_FREERTOS_FIRMWARE_INCLUDE_UI_INPUT_H_

#include <stdbool.h>

typedef struct {
  /** @brief 返回 true 表示本拍检测到一次有效单击（已消抖）。 */
  bool (*poll_click)(void);
} UiInputSource;

#endif /* PICO_FREERTOS_FIRMWARE_INCLUDE_UI_INPUT_H_ */
```

`ui_render.h` 里只加登记接口：

```c
/** @brief 登记输入源；传 NULL 表示不接收按键（宿主测试默认）。 */
void ui_render_set_input_source(const UiInputSource* source);
```

渲染循环内（每 20 ms 一拍）：

```c
if (s_input_source != NULL && s_input_source->poll_click()) {
  const unsigned next =
      ((unsigned)current.theme_id + 1u) % (unsigned)kThemeCount;
  system_state_set_theme((UiThemeId)next);
  system_state_get_snapshot(&current); /* 同拍拿到新值，立即整屏重绘 */
}
```

`main.c` 负责装配：

```c
static const UiInputSource kBoardInput = {
    .poll_click = board_button_poll_click,
};
ui_render_set_input_source(&kBoardInput);
```

### 4.3 状态镜像（单一数据源）

```c
/* include/system_state.h */
#include "ui_theme_id.h"

typedef struct {
  UiMode ui_mode;
  UiThemeId theme_id;  /**< 当前生效主题；与 ui_mode 正交 */
  bool pc_online;
  /* ... 其余字段不变 ... */
} SystemSnapshot;

/** @brief 越界值被夹紧到合法区间，写入受既有互斥锁保护。 */
void system_state_set_theme(UiThemeId theme_id);
```

`system_state.h` 只依赖零依赖的 `ui_theme_id.h`，不依赖 `ui_theme.h`——否则「state → display」
与「display → state」会形成头文件环。

- `system_state` 初始化时 `theme_id = kThemeM3Dark`；
- 写入走既有 `STATE_LOCK()` / `STATE_UNLOCK()` 宏（设备侧 FreeRTOS 互斥量，宿主侧 pthread）；
- 所有宿主测试用 `memset` 初始化快照，追加字段不影响既有用例。

### 4.4 重绘策略

- `ui_layout_calculate_dirty_regions()` 在比较 `ui_mode` 之后追加一条判断：
  `prev->theme_id != curr->theme_id` → 输出 `{0, 0, 128, 160}` 并 **立即返回**；
- `ui_render_collect_dirty_regions()` 补一条早退：若已返回的矩形覆盖全屏
  （`w == kUiScreenWidth && h == kUiScreenHeight`），直接 `return count;`，
  不再追加顶栏、时间卡、日历卡。

**早退的真实收益是「矩形数量可控」，不是「省 SPI 时间」。** `ui_render_draw_dirty()`
（`ui_render.c:412-441`）先把所有矩形映射成 `bool dirty_slices[10]` 的并集，再按切片逐个绘制，
同一切片只画一次；追加一个已包含在全屏内的矩形，**切片数与 SPI 字节数都不变**。
早退的价值在于：`kMaxDirtyRects` 只有 8，矩形被无意义的追加占满后，
`ui_layout.c` 的 `push_rect` 会**静默丢弃**后续真实变化。

- 页面 A 的顶栏时刻每秒变化一次：在 `ui_render_collect_dirty_regions()` 内（与既有顶栏矩形
  同一处）判断 `clock` 的时分秒或 `time_synced` 是否变化，变了才追加
  `{32, 1, 64, 14}` 一个矩形（每秒至多一次）。该矩形落在第 1 个切片
  （`kDisplaySliceRows = 16`，`st7735s_driver.h:14`），每次重绘 128 × 16 × 2 B = 4096 B，
  10 MHz SPI 下约 3.3 ms，即每秒 0.33 % 的占空，`kMaxDirtyRects = 8` 预算内。
- 全屏刷新的时间预算：`128 × 160 × 2 B = 40960 B`，10 MHz SPI 下
  `40960 × 8 / 10e6 = 32.8 ms`，加命令字节与逐切片开销约 35 ms；
- `vTaskUIRender` 的循环结构与 `previous = current`（`ui_render.c:548`）**不改动**。

### 4.5 任务与核心分配

| 任务 | 核心 | 优先级 | 栈 | 周期 | 职责 |
| :--- | :---: | :---: | :--- | :--- | :--- |
| `vTaskUIRender`（现有） | 1 | 现有 | 512 words（2 KB） | 20 ms | 采样按键、推进 `theme_id`、脏区重绘 |
| `vTaskThemeCommit`（新增） | 0 | 2 | 256 words（1 KB，静态） | 100 ms | 5 s 去抖、调用 `theme_storage_commit()` |
| 其余任务 | — | — | — | — | 不变 |

新增任务用 **`xTaskCreateStaticAffinitySet()`**（`FreeRTOS-Kernel/include/task.h:533`，
要求 `configSUPPORT_STATIC_ALLOCATION == 1 && configNUMBER_OF_CORES > 1 &&
configUSE_CORE_AFFINITY == 1`，本工程三条全满足）一次性带亲和性创建。
两步走（先 `xTaskCreateStatic` 再 `vTaskCoreAffinitySet`）**不是未定义行为**——
`vTaskCoreAffinitySet` 是公开 API（`tasks.c:3058-3110`）——但前一步会以 `tskNO_AFFINITY`
把任务放进就绪表，存在「首个 tick 已经跑到另一颗核上、随后才被迁回」的窗口；
一步到位没有这个窗口，也更短。

**「零动态堆」只对本任务成立。** `flash_safe_execute()` 每次调用都会
`xTaskCreateAffinitySet(..., configMINIMAL_STACK_SIZE, ...)` 动态创建避难任务，
`configMINIMAL_STACK_SIZE = 256` words → 每次提交额外申请约 **1.1 KB** 动态堆。
详见 §5.4 的堆预检与 §9 的风险条目。

优先级 2 与 `kWifiTaskPriority`（`src/network/wifi_manager.c:34`，也是 2）**同为一级**，
低于 supervisor（3）与 UDP 监听（4）。需要说明的是：写入窗口内双核中断全关，
**优先级排序在这个窗口里根本不起作用**；Wi-Fi 停顿不是被优先级吸收的，而是被
「5 s 静默 + 3 s 离线判定」吸收的（§5.5）。

---

## 5. Flash 持久化

### 5.1 存储布局与边界守卫

| 项目 | 值 |
| :--- | :--- |
| Flash 总量 | 2 MB，`0x10000000`–`0x10200000` |
| 选用扇区 | 最后 4 KB（Sector 511） |
| 物理偏移 | `kThemeFlashOffset = 0x1FF000` |
| XIP 映射地址 | `0x101FF000` |
| 擦除粒度 | 4 KB 扇区（`FLASH_SECTOR_SIZE = 4096`） |
| 编程粒度 | 256 字节页（`FLASH_PAGE_SIZE = 256`） |

**「1.1 MB 留白」不是保证，必须机器校验。** `flash_range_*` 内部只有
`hard_assert(flash_offs + count <= PICO_FLASH_SIZE_BYTES)`，它保护的是 Flash 顶端
`0x200000`，不保护镜像边界。若将来字库或农历表膨胀到压过 `0x1FF000`，运行时会把自己
的代码擦掉，症状是随机 HardFault 且只在个别设备复现。

**链接期守卫**：不要自己写 `INCLUDE "memmap_default.ld"`——`memmap_default.ld` 所在的
`src/rp2040/pico_platform` 不在 `PICO_LINKER_SCRIPT_INCLUDE_DIRS` 里，会报 cannot open。
SDK 为此留了 `section_extra_post_end.incl` 钩子（它被 `sections_default.incl` 在
`__flash_binary_end` 的 `PROVIDE` 之后 include，注释明写「可以加 custom asserts」）：

```ld
/* pico_freertos_firmware/linker/section_extra_post_end.incl */
ASSERT(__flash_binary_end <= 0x101FF000,
       "firmware image overlaps theme sector 511")
```

```cmake
# pico_freertos_firmware/CMakeLists.txt
pico_add_linker_script_override_path(pico_status_screen
    ${CMAKE_CURRENT_LIST_DIR}/linker
    FILES section_extra_post_end.incl)
```

**启动期守卫**在 `theme_storage_init()` 第一件事执行（`panic()` 由 §5.2 包含的
`pico/stdlib.h` 引入）：

```c
extern char __flash_binary_end; /**< 由 SDK 的 section_flash_end.incl 提供 */

static void theme_storage_assert_no_overlap(void) {
  const uintptr_t image_end = (uintptr_t)&__flash_binary_end;
  if (image_end > (uintptr_t)(XIP_BASE + kThemeFlashOffset)) {
    panic("固件镜像与主题存储扇区重叠");
  }
}
```

`__flash_binary_end` 取的是绝对 XIP 地址（SDK 自己在
`pico_btstack/btstack_flash_bank.c:62-63` 就是这么用的），故 `== 0x101FF000` 视为合法
（最后一个字节是 `0x101FEFFF`），`>` 才判越界。

### 5.2 数据结构（仅存在于 `theme_storage.c`）

结构体与 `FLASH_PAGE_SIZE` 断言都属于**平台实现细节**。放进头文件会把 `hardware/flash.h`
泄漏到 `include/`，违反 §6.2「只有 platform 层碰 flash SDK」。头文件只暴露 API 与错误码。

```c
/* src/platform/theme_storage.c */
#include "hardware/flash.h"
#include "pico/flash.h"
#include "pico/stdlib.h"
#include "theme_storage.h"

#define THEME_FLASH_MAGIC 0x454D4854u /**< 小端落盘为 "THME" */
#define THEME_FLASH_VERSION 1u

typedef struct {
  uint32_t magic;      /**< 魔数 */
  uint8_t theme_id;    /**< 主题索引 0..5 */
  uint8_t version;     /**< 结构体版本，当前 1 */
  uint8_t reserved[2]; /**< 保留，必须为 0 */
  uint32_t checksum;   /**< magic ^ (theme_id << 8) ^ version */
} PersistentThemeConfig;

_Static_assert(sizeof(PersistentThemeConfig) == 12,
               "主题配置应为 12 字节");
_Static_assert(sizeof(PersistentThemeConfig) <= FLASH_PAGE_SIZE,
               "主题配置必须能装进一个 flash 页");
_Static_assert(offsetof(PersistentThemeConfig, theme_id) == 4, "布局变了");
_Static_assert(offsetof(PersistentThemeConfig, version) == 5, "布局变了");
_Static_assert(offsetof(PersistentThemeConfig, checksum) == 8, "布局变了");
```

**魔数修正**：v1.1.0 写的是 `0x54484D45 /* "THME" */`，但小端落盘字节序是
`45 4D 48 54`，即 `EMHT`，与注释不符。`"THME"` 对应的小端值是 `0x454D4854`。

**校验和说明**：`magic` 固定、`theme_id` 1 字节、`version` 1 字节，校验值实际只有
16 位有效，作用是识别擦除态（全 `0xFF`）与空白态（全 `0x00`），**不提供完整性保证**。
真正的保护是 §5.4 的范围校验与回退策略。

**结构体必须零初始化**：`PersistentThemeConfig cfg = {0};`。若逐个字段赋值，
`reserved` 会保留栈上脏值并被 `memcpy` 一起写进 Flash，导致「12 字节逐字节比对」不再确定，
§8.4 的「切到当前已保存主题 → 0 次擦写」也会在 `reserved` 抖动时退化为每次都擦写。

### 5.3 写入流程

分三步，顺序不可调换：

```c
/* 1) 页缓冲必须常驻 SRAM，且绝不能是 const——ROM 会通过 SSI 直接读它。 */
static uint8_t s_flash_page[FLASH_PAGE_SIZE];

/* 2) 填页在 flash_safe_execute 之外完成：回调体内只允许调那两个 SDK 函数，
 *    任何 strlen/memset 式的额外工作都可能落到带锁或落在 Flash 的路径上。 */
static void theme_prepare_page(const PersistentThemeConfig* cfg) {
  memset(s_flash_page, 0xFF, sizeof(s_flash_page));
  memcpy(s_flash_page, cfg, sizeof(*cfg));
}

/* 3) 避难回调：双核中断全关（XIP 在两次调用之间是开的），
 *    只许调这两个 SDK 函数。禁止 printf / assert / 任何带锁调用。 */
static void __not_in_flash_func(theme_flash_commit)(void* unused) {
  (void)unused;
  flash_range_erase(kThemeFlashOffset, FLASH_SECTOR_SIZE);
  flash_range_program(kThemeFlashOffset, s_flash_page, FLASH_PAGE_SIZE);
}
```

调用：

```c
PersistentThemeConfig cfg = {0};
cfg.magic = THEME_FLASH_MAGIC;
cfg.theme_id = (uint8_t)theme_id;
cfg.version = THEME_FLASH_VERSION;
cfg.checksum = cfg.magic ^ ((uint32_t)cfg.theme_id << 8) ^ cfg.version;
theme_prepare_page(&cfg);
const int rc = flash_safe_execute(theme_flash_commit, NULL, 100);
```

`uint8_t s_flash_page[256]` 不需要 `__attribute__((aligned(4)))`：`flash_range_program`
只要求 `flash_offs` 与 `count` 对齐，数据指针无对齐要求，SDK 自己在
`btstack_flash_bank.c:133` 就是裸数组直传。

**`count` 必须是 256。** 如果按直觉写成
`flash_range_program(offset, (const uint8_t*)&cfg, FLASH_PAGE_SIZE)`——只提供 12 字节
结构体却要求 256 字节，`hardware_flash/flash.c` 会把 `data`/`count` 原样交给 ROM，
`invalid_params_if` 默认关闭，运行时**一行检查都不做**：ROM 会从 12 字节对象之后多读
244 字节（栈与 `.bss` 的脏数据），把它永久写进 Flash。极端情况下对象位于 SRAM 顶端会
触发 MemFault，而此时双核中断全关、XIP 关闭，只能等 8 s 硬件看门狗复位。

**超时参数取 100 ms**。`pico/flash.h` 明确 `enter_exit_timeout_ms` 是 **enter 与 exit
各一次**的超时，不是写入总时长上限；避难任务创建通常在 1 ms 内完成，100 ms 有 100 倍余量。
时序先按两种情形记账（漏掉 exit 的 100 ms 是 v1.2.0 的错），再补一个验收余量口径：

```text
正常：enter 握手 ~0 + 扇区擦除 400 + 页编程 3 + exit 握手 ~0  ≈ 403 ms
最坏：enter 握手 100 + 扇区擦除 400 + 页编程 3 + exit 握手 100 = 603 ms
```

403 ms 里的 400 ms 已按数据手册的**扇区擦除上界**取值，所以它是**预算值**而不是典型实测，
实机通常明显更短。全文统一用三个口径，不再混用：

| 口径 | 数值 | 含义 |
| :--- | ---: | :--- |
| 预算 | 403 ms | 擦除按上界、两次握手都成功 |
| 最坏 | 603 ms | 两次握手各耗尽 100 ms 超时 |
| 验收上界 | 650 ms | 603 ms + 47 ms 抖动余量（§8.3 P-3 用它判「不超过」） |

**XIP 关闭是两次、不是一次。** `hardware_flash/flash.c:214-238`（擦除）与 `:245-269`（编程）
各自独立执行一遍 `connect_internal_flash → flash_exit_xip → ROM 调用 → flash_flush_cache →
flash_enable_xip_via_boot2`，两个函数体都是 `__no_inline_not_in_flash_func`。所以真实时序是：

```text
enter → [XIP off 400 ms] → [XIP on] → [XIP off ~3 ms] → [XIP on] → exit
```

这有两个后果：其一，XIP 只在两段窗口内关闭，**回调体本身是在 XIP 开启时执行的**，
所以「回调必须 `__not_in_flash_func`」不是今天的正确性前提，而是一条对未来 SDK 时序变化的
廉价防御（见 §5.6）；其二，回调内**不得**自行调用 `flash_exit_xip()` /
`flash_enable_xip_via_boot2()` / `flash_flush_cache()`——那两个函数已经按需成对调过，
再调一次会破坏嵌套状态。§5.6 给出完整禁止清单。

页缓冲长度也必须在编译期钉死：

```c
_Static_assert(sizeof(s_flash_page) == FLASH_PAGE_SIZE,
               "页缓冲必须是整页，否则 ROM 会越界读");
```

### 5.4 接口契约、状态机与失败处理

#### 5.4.1 头文件契约（`include/theme_storage.h`）

```c
typedef enum {
  kThemeStorageOk = 0,          /**< 提交成功且回读一致 */
  kThemeStorageErrEnterTimeout, /**< enter 握手超时：回调未执行、Flash 未动，
                                     但 Core 1 已被劫持，本次不可能再成功 */
  kThemeStorageErrExitTimeout,  /**< exit 握手超时：回调已执行、Flash 很可能
                                     已写入但无法确认，须立即回读裁决 */
  kThemeStorageErrVerify,       /**< 回读与预期不一致（调用方最多重试 1 次） */
  kThemeStorageErrInvalidArg,   /**< 入参越界（调用方违约）：不触碰 Flash、
                                     不重试；本仓不可达，见 §5.4.1 */
  kThemeStorageErrNoHeap,       /**< 堆预检未过：没调 flash_safe_execute，
                                     以免触发 malloc 失败钩子的死循环 */
  kThemeStorageErrNoResources,  /**< 建避难任务失败（-9）：未劫持任何核、Flash
                                     未动；现状下被 malloc 钩子拦在前而不可达 */
} ThemeStorageResult;

/**
 * @brief 校验镜像边界并读出已保存主题。
 * @return 有效主题 id；扇区空白/损坏/版本不符时返回 kThemeM3Dark。
 */
UiThemeId theme_storage_init(void);

/**
 * @brief 同步提交主题，阻塞直到写完或失败。
 * @param theme_id 目标主题；越界属调用方违约：先 configASSERT。固件构建里
 *                 configASSERT 停在 firmware_assert_failed() 自旋（不返回），
 *                 只有在 assert 可返回的构建里才会返回
 *                 kThemeStorageErrInvalidArg
 *                 ——无论哪种都不写 Flash、也不夹紧落盘。
 * @return ThemeStorageResult。
 */
ThemeStorageResult theme_storage_commit(UiThemeId theme_id);
```

**为什么越界不夹紧、也不复用 `ErrVerify`**：唯一调用端是
`(current.theme_id + 1) % kThemeCount`，`system_state_set_theme()` 自己也夹紧，加载器还校验
`theme_id < kThemeCount`，所以越界在本仓**不可能出现**；`configASSERT` 在固件里
（`FreeRTOSConfig.h:76-77` → `main.c:95-101`）是关中断后自旋，不会返回。保留该码只为在
assert 可返回的构建下仍有一个合法返回值；若把它混进 `Verify`，一次调用方 bug 会借
「幂等重写」通道把用户已存主题改成主题 0。

**为什么把「超时」拆成两个码**：SDK 的 enter 等待超时（`flash.c:170`）与 exit 等待超时
（`flash.c:221`）都返回 `PICO_ERROR_TIMEOUT`，**但语义相反**——前者 `func(param)` 根本没跑
（`flash.c:78-82` 只在 `rc == 0` 时才调用），后者回调已经跑完、12 字节多半已落盘。
若合成一个码，上层会把「已经写成功」误判成「没写」，下次上电主题跳变。
`theme_storage.c` 用一个 `static volatile bool s_callback_ran` 区分：**每次调用
`flash_safe_execute()` 之前先置 `false`**，进入 `theme_flash_commit()` 第一件事置 `true`，
返回 `PICO_ERROR_TIMEOUT` 时据此二选一——不先清零的话，Verify 重试的第二次若在 enter 阶段
超时，会因上一次残留的 `true` 被误判成 exit 超时。
exit 超时后**先回读**：能读出一致配置就按成功处理；读不出则返回
`kThemeStorageErrExitTimeout`，按 §5.4.3 **不重试**、直接转 `Failed`——此时安全区任务已失控，
再次握手不只会失败，还会把失控窗口拖长。

**阻塞语义**：`theme_storage_commit()` 同步执行「堆预检 → 擦写 → 回读」全流程，
预算约 403 ms、最坏 603 ms（§5.3 的三个口径），验收判据取 650 ms。
调用方 `vTaskThemeCommit` 是核心 0 上的独立任务（优先级 2），这期间 Core 0 的其他任务本来
就被关中断冻结，把写入放进独立任务而不是 `vTaskUIRender` 的唯一原因，是**避免把 650 ms
的画面僵直塞进 20 ms 的渲染节拍里**。堆预检与回读比对都在本函数内部完成，
不向调用方泄漏 Flash 细节；因此 `ThemeStorageResult` 的每一个码都有**唯一的生产者**，
而唯一的消费者是 `vTaskThemeCommit`。

#### 5.4.2 状态机

```c
typedef enum {
  kThemeCommitIdle = 0, /**< 无待提交变更 */
  kThemeCommitPending,  /**< 已切换，等 5 s 去抖 */
  kThemeCommitWriting,  /**< 正在 flash_safe_execute */
  kThemeCommitDone,     /**< 已落盘且回读一致 */
  kThemeCommitFailed,   /**< 放弃本次提交，保留内存态 */
} ThemeCommitState;
```

提交任务每 100 ms 执行：

1. 取快照；`theme_id` 与本地 `s_seen` 不同 → 更新 `s_seen`，把截止时刻置为
   `now + 5000 ms`，状态转 `Pending`；
   **只有这一种方式能离开 `Done` / `Failed`**（初始态为 `Idle`，同样由本步离开）；
2. **仅当 `state == Pending`** 且到达截止时刻且 `s_seen != s_committed`：先读 XIP 地址比对——配置有效且
   `theme_id` 相同则直接转 `Done`（**零擦写**，并把 `s_committed` 置为该值）；
3. **（承接第 2 步：仅当 `state == Pending` 且第 2 步没有短路成 `Done`）** 转 `Writing`，
   调用 `theme_storage_commit()`，按返回码分派：`kThemeStorageOk` → `Done`；
   `kThemeStorageErrNoResources` → `Failed`（与 `NoHeap` 一致：分配型失败一律不重试，
   否则「有没有终止条件」会取决于 D-11 落不落地）；
   `kThemeStorageErrVerify` → 若 `s_verify_retried == false`，置 `true`、留在 `Writing`、
   用 `vTaskDelay(pdMS_TO_TICKS(1000))` 等 1 s 再调一次（§5.4.3），**重试用本次尝试的
   同一个 `theme_id`，不重取快照**——否则这 1 s 内用户又按一次键，簿记会错位；**第二次仍是
   `kThemeStorageErrVerify` → `Failed`**——这条终止分支必须写进指令，否则「最多 1 次」
   会退化成每 1.4 s 一轮、永远不进 `Failed` 的无限重试（`vTaskDelay` 让出 CPU，看门狗
   照常被喂，连 8 s 复位都不会发生）；
   其余码（含 `kThemeStorageErrInvalidArg`）→ `Failed`；
4. 日志分两种：真实擦写打印 `printf("theme: commit id=%u rc=%d\n", id, rc)`，
   零擦写短路打印 `printf("theme: skip id=%u\n", id)`。这是 §8.4 中 S-1/S-2/S-3
   「0 次擦写 / 恰好 1 次擦写」唯一的观测手段——RP2040 没有 Flash 擦写计数器。

`s_committed` 的初值就是 `theme_storage_init()` 的返回值（开机读回的那套），此后只在两条
路径上更新：零擦写短路（第 2 步）与 `kThemeStorageOk`（第 3 步）。它是「省掉这一次擦写」的
判据，不更新就永远重写，§8.4 的 S-1/S-3 也就测不出 0 次擦写。

**状态机与 `s_verify_retried` 位都不在任务里，而在纯逻辑单元 `theme_commit_policy.c`**：
任务只做三件 I/O（取快照、调 `theme_storage_commit()`、把结果喂回策略），策略负责
「下一步该做什么 + 是否还有下一次」。这样「`Failed` 之后是否真终止」「第二次 `Verify` 是否
一定转 `Failed`」是能在宿主上**断言**的性质（§6.3 的 `test_theme_commit_policy`），而不是
要上板跑 8 s 才看得见的时序。`s_verify_retried` 在**每次从 `Pending` 进入 `Writing`** 时清零，
保证一次提交只买一次重试。

**堆预检在 `theme_storage_commit()` 内部，不在任务里。** 只有把它放在真正要调用
`flash_safe_execute()` 的那一层，才能保证「预检未过就绝不进入握手」，也才能让
`kThemeStorageErrNoHeap` 只有一个生产者。阈值 1536 B 是经验值，不是保证：
`xPortGetFreeHeapSize()` 返回的是总空闲字节（声明在 `portable.h:194`、定义在
`heap_4.c:413`，`FreeRTOS.h` 已包含，项目在 `src/app/supervisor.c:90` 已在用），
而 `heap_4` 只在 `vPortFree` 时合并相邻块，**它不能证明存在一个 ≥1.2 KB 的连续块**。
取 1.5 KB 是为了在 1.1 KB 需求上留 36 % 余量；Wi-Fi/TLS 长期运行后的碎片化仍可能击穿它。
预检未过时**直接转 `Failed`、不重试**——`pvPortMalloc` 失败会进
`vApplicationMallocFailedHook()` 死循环，「优雅降级」只有在根本不进那条路径时才成立。

#### 5.4.3 失败处理（必须与看门狗语义对齐）

**握手失败会导致整机复位，这一点必须写进设计而不是回避。** 先把 SDK 的真实路径钉清楚：

- `pico_flash/flash.c:77-83`：`rc = helper->enter_safe_zone_timeout_ms(...)`，**只有 `rc == 0`
  才会执行 `func(param)`**。所以 enter 超时时，写入根本没开始——Flash 内容未被触碰；
- 但 `flash_safe_execute()` 在 enter 之前就已经在另一颗核上创建了优先级 7 的避难任务。
  enter 超时后它有两种结局：
  1. **主流路径**：卡在 `:119-123` 的
     `while (lockout_state[other] != FREERTOS_LOCKOUT_LOCKER_WAITING) { __wfe(); }`，
     **中断仍开着**，但优先级 7 自旋且永不退出——那颗核从此不再出帧；
  2. **窄竞态**：`lockout_state` 先被置成 `LOCKER_DONE`，避难任务看到状态不符，
     在 `:124-:126` 执行 `save_and_disable_interrupts()`（`:124`）、把状态覆盖成
     `LOCKEE_READY`（`:125`）、再 `__sev()`（`:126`），然后在 `__wfe()` 永久自旋——
     那颗核**中断全关**。

两条路径都不可恢复，后果链相同：

```text
enter 超时 → Core 1 被优先级 7 的避难任务永久占用（中断可能仍开）
           → 渲染任务停止签到
           → 1000 ms 后 UI 槽位 stale
           → supervisor.c:118-139 跳过 watchdog_update
           → 距最后一次喂狗 8000 ms 后整机复位（实际约 7–8 s）
```

`supervisor_task` 是 `xTaskCreate(..., NULL)`（`supervisor.c:158-160`），没有亲和性，
但在 SMP 下仍会被调度到 Core 0，**看门狗链本身不依赖它初始落在哪颗核**。必复位的原因是
「UI 槽位 stale 触发的不喂狗」，不是「两颗核同归于尽」——v1.3.0 的那句表述是错的。

因此重试策略必须服从 8 s 看门狗预算：

| 失败类型 | 是否重试 | 理由 |
| :--- | :--- | :--- |
| enter 握手超时（`kThemeStorageErrEnterTimeout`） | **不重试** | 回调未执行、Flash 未动，但 Core 1 已被永久占用，本次上电不可能再成功 |
| exit 握手超时（`kThemeStorageErrExitTimeout`） | **不重试** | 回调已执行，无法确认是否落盘；先回读裁决（一致即按成功），**不一致也不再握手** |
| 回读不一致（`kThemeStorageErrVerify`） | 最多 **1 次**，间隔 1 s | 擦除 + 整页重写天然幂等；1 s 后重试仍落在 8 s 预算内。**第二次仍不一致即转 `Failed`**，终止由 `theme_commit_policy.c` 里的 `s_verify_retried` 位保证 |
| 入参越界（`kThemeStorageErrInvalidArg`） | **不重试** | 调用方违约，与 Flash 状态无关；固件里 `configASSERT` 直接自旋（不返回），既不写也不夹紧落盘——若把它混进 `Verify`，一次调用方 bug 会借「幂等重写」通道把用户已存主题改成主题 0 |
| 堆预检不足（`kThemeStorageErrNoHeap`） | **不重试** | 根本不该进入 `flash_safe_execute`，见 §5.4.2 |
| 建避难任务失败（`kThemeStorageErrNoResources`） | **不重试**，转 `Failed` | `flash.c:151` 的 `xTaskCreateAffinitySet` 失败后直接 `return -9`，既没建任务也没关中断、Flash 未动。但**这条分支今天不可达**：`xTaskCreate` 的唯一运行期失败原因是分配失败，而分配失败先撞 `vApplicationMallocFailedHook()` 死循环（§9、D-11）。与 `NoHeap` 同属分配型失败，一律不重试——即便 D-11 落地，重来也只会再撞一次分配 |

**回读不一致的那 1 次重试不需要新状态。** 它发生在 `Writing` 态内部：`theme_storage_commit()`
返回 `kThemeStorageErrVerify` 后，任务用 `vTaskDelay(pdMS_TO_TICKS(1000))` 原地等 1 s 再调一次，
期间照常让出 CPU、看门狗仍由 supervisor 喂。把 `retry_at_ms` 塞进状态机会多一个
「等待重试」态，却换不来任何可观测差异，故不采用。

v1.2.0 写的「重试上限 3 次、间隔 ≥ 5 s」= 15 s，在 8 s 看门狗面前是**永远跑不到第三个
分支的死代码**，已删除。

#### 5.4.4 加载与校验（`theme_storage_init()`）

```text
magic == THEME_FLASH_MAGIC
&& version == THEME_FLASH_VERSION
&& theme_id < kThemeCount
&& reserved[0] == 0 && reserved[1] == 0
&& checksum == (magic ^ (theme_id << 8) ^ version)
```

任一不满足 → 返回 `kThemeM3Dark`。v1.1.0 缺 `theme_id < kThemeCount` 这一条，
损坏的扇区会让 `s_themes[200]` 读到任意 Flash 内容，表现为花屏。
补 `reserved == 0` 是为了让「同版本固件往保留字段塞新语义」在加载阶段就露馅。
**改动结构体布局必须同时升 `THEME_FLASH_VERSION`**，否则旧值会被新固件按新布局解读。

#### 5.4.5 掉电语义

| 掉电时刻 | Flash 状态 | 下次启动 | 可接受 |
| :--- | :--- | :--- | :---: |
| 擦除中 | 部分页已擦、部分保留旧值 | 12 字节可能仍是旧值 → 读到旧主题 | 是 |
| 擦除完成、编程前 | 整扇区 `0xFF` | 魔数不匹配 → 回退主题 0 | 是 |
| 页编程中（≤ 3 ms，数据手册上限） | 位状态不确定 | 校验失败 → 回退主题 0 | 是 |

结论：**任何时刻掉电，最坏结果是「回到默认主题」，不会变砖。** 但 v1 不保证
「旧值一定存活」——需要该保证时用双扇区 A/B 备份（见 §12 D-8）。

### 5.5 写入窗口的副作用（必须知情）

`flash_safe_execute` 的 SMP 路径会在**另一颗核**上创建一个优先级 7 的避难任务，
该任务 `save_and_disable_interrupts()` 后 `__wfe()` 自旋；调用核在 `enter_safe_zone`
成功后同样关中断，直到 `exit`。因此在整个 `func(param)` 执行期间：

| 影响 | 程度 | 说明 |
| :--- | :--- | :--- |
| 双核中断全关 | 预算 403 ms，最坏 603 ms | Core 0 的 CYW43、lwIP 定时器、UART 全部冻结；603 ms 是两次握手各超时的最坏推算，650 ms 是再加 47 ms 抖动余量的验收上界 |
| 画面冻结 | 同上 | Core 1 的渲染任务被优先级 7 的避难任务抢占，无法出帧 |
| UDP 丢包 | 最多 1–2 包 | 端口 8765 每秒一包；3 s 离线判定不会误触发 |
| UI 槽位心跳 | 预算内安全，最坏擦线 | 1000 ms 槽位；按 603 ms 最坏推算仍有 40 % 余量，但这是算出来的、不是实测的 |
| RTOS tick 停摆 | 403–603 ms | SysTick 被关中断，恢复后只补记 1 tick；所有基于 `xTaskGetTickCount()` 的超时（含 5 s 去抖）整体顺延。`get_absolute_time()` 域（显示时钟、mbedtls 时间源）不受影响 |
| 硬件看门狗 | **只有两条路径会复位** | 8000 ms 看门狗。其一 enter/exit 握手超时 → 渲染停摆 → 1 s 后停喂 → 8 s 复位；其二是堆被碎片化击穿、`flash_safe_execute` 内部 `pvPortMalloc` 失败撞上 `vApplicationMallocFailedHook()` 死循环（§9）。其余返回码一律转 `Failed`，不再碰 Flash、照常喂狗 |

v1.2.0 在这里写的「看门狗不受影响」是错的：正常写入确实不影响，但一旦握手超时，
系统进入 §5.4.3 描述的必死循环，8 s 后一定复位。

**画面冻结发生在最后一次按键之后 5 s**，此时用户通常已经离开按键；写入不改 RAM 中的
`theme_id`，屏幕内容不会跳变，观感是一次轻微卡顿。若不可接受，见 §12 D-4。

### 5.6 回调内的禁止事项

`theme_flash_commit` 运行在「双核中断全关」的窗口内，但 **XIP 并不是全程关闭**：
`flash_range_erase` 与 `flash_range_program` 各自在自己的 RAM 函数里短暂关闭 XIP，
返回前就已恢复（`hardware_flash/flash.c:236`、`:266` 都调了
`flash_enable_xip_via_boot2()`）。所以在两次调用之间的回调体里 XIP 是开着的，
从 Flash 取指也能工作——v1.3.0 在这里写的「XIP 关闭窗口内取指失败」是错的。
真正的硬约束是**中断全关**与**不得嵌套 Flash 操作**：

- **禁止** `printf` / 任何 stdio：会走 `mutex_enter_blocking`，另一核也在关中断状态，死锁；
- **禁止** `assert` / `panic`：失败路径同样会调 I/O 或格式化；
- **禁止** 任何 FreeRTOS API、任何堆分配：调度器依赖中断，关中断状态下调用即死锁；
- **禁止** 自行调用 `flash_exit_xip()` / `flash_enable_xip_via_boot2()` / `flash_flush_cache()`：
  `flash_range_erase` 与 `flash_range_program` 各自已按需成对调过，再调一次会破坏嵌套状态；
- **禁止** 在回调内读写配置扇区本身：擦除进行中，该扇区内容不确定；
- **禁止** 在回调内 `memset`/`memcpy`：页填充已在 `theme_prepare_page()` 里、safe zone 之外完成。

回调仍标 `__not_in_flash_func`：今天它**不是**正确性的必要条件（回调体在 XIP 开启时执行），
但把「回调不依赖 Flash 取指」这条性质显式写进代码，可以在 SDK 未来改变 XIP 时序时不至于
无声失效——这是便宜的防御，不是必需的补救。

---

## 6. 模块职责、组合根与构建

### 6.1 目录与职责

下树**只列本次新增或改动的文件**，不是完整目录清单。`src/platform/` 已有
`mbedtls_time_source.c` 作为「唯一接触 SDK 的实现层」先例，`theme_storage.c` 沿用同一约定。

```text
pico_freertos_firmware/
├── include/
│   ├── ui_theme_id.h          # 零依赖：UiThemeId 枚举
│   ├── ui_input.h             # 零依赖：UiInputSource 函数指针
│   ├── button_filter.h        # 纯逻辑：2/2 采样消抖
│   ├── theme_config.h         # 纯逻辑：12 字节配置的打包与解析
│   ├── theme_commit_policy.h  # 纯逻辑：提交状态机与终止条件
│   ├── theme_switch.h         # 纯逻辑：主题推进与绕回
│   ├── ui_theme.h             # 调色板结构体 + ui_theme_get
│   ├── theme_storage.h        # 提交/初始化接口 + 结果码
│   ├── board_button.h         # GP15 采样
│   ├── theme_commit_task.h    # 提交任务启动
│   └── ui_render.h            # 新增输入源登记接口
├── linker/
│   └── section_extra_post_end.incl  # 链接期边界断言
└── src/
    ├── main.c                 # 组合根：唯一知道「谁装谁」的地方
    ├── display/
    │   ├── ui_theme.c         # 6 套常量表 + 越界回退
    │   └── ui_render.c        # 只读快照，经 ui_theme_get 取色
    ├── logic/
    │   ├── button_filter.c        # 仅 stdbool/stdint
    │   ├── theme_config.c         # 打包/解析 + 校验
    │   ├── theme_commit_policy.c  # 去抖状态推进
    │   └── theme_switch.c         # 主题推进与绕回
    ├── platform/
    │   ├── board_button.c     # 唯一 include hardware/gpio.h 的文件
    │   └── theme_storage.c    # 唯一 include flash 相关 SDK 头的文件
    └── app/
        └── theme_commit_task.c    # 周期任务：调 policy + storage
```

**为什么把提交逻辑拆成 `logic/` 三个纯单元**：`theme_storage.c` 依赖 `hardware/flash.h`，
在宿主上编不过；如果校验、去抖、绕回全都写在它和 `theme_commit_task.c` 里，宿主测试就
**没有可编译的被测单元**（这正是第三轮复审指出的「测试目标无处落地」）。
拆开之后，`theme_config.c` 负责 12 字节布局与校验（覆盖 §8.2 N-1..N-8），
`theme_commit_policy.c` 承载完整状态机（5 个状态、5 s 去抖、零擦写短路、`Verify` 只重试一次、
每个状态的终止条件），任务只做 I/O 与回灌（覆盖「`Failed` 之后不再自动重入」），
`theme_switch.c` 负责 `(current + 1) % kThemeCount`（覆盖绕回）——
三者都不 include 任何 SDK 头，`theme_storage.c` 退化为「读 12 字节 → parse；
pack → `flash_safe_execute` → 回读 parse」的薄适配层。

`main.c` 装配顺序。`theme_storage_init()` 必须在 `asset_store_init()` 之前——UI 起来之前
必须已经知道该用哪套配色；但**创建提交任务的那次 `panic` 必须放到 `supervisor_start()`
之后**，因为看门狗是在 `supervisor_start()` 里 `watchdog_enable()` 的，在那之前 `panic`
只会把芯片停在原地、不会复位：

```c
system_state_init();
system_state_set_theme(theme_storage_init()); /* 边界校验 + 读回已存主题 */
board_button_init();                          /* gpio_init + pull_up(GP15) */
asset_store_init();
supervisor_start();     /* 内部 watchdog_enable(8000, false) */
if (!theme_commit_task_start()) {
  panic("主题提交任务创建失败");               /* 看门狗已在跑，8 s 后复位 */
}
/* ... 原有启动顺序不变 ... */
ui_render_set_input_source(&kBoardInput);
ui_render_start_task();
```

`theme_storage_init()` 内部的镜像边界检查失败同样走 `panic`：那属于构建期配置错误
（`__flash_binary_end` 越过 0x101FF000），不是运行时故障，此时看门狗尚未启用，表现为停在
panic 处、需要重新烧录——与 `asset_store_init()` 失败时的行为一致，不额外兜底。

### 6.2 依赖方向与可机器校验的约束

```text
main.c ──> 全部模块（只做装配，不含业务逻辑）

include/system_state.h ──> ui_theme_id.h（零依赖）
display/ui_render.c    ──> ui_theme.h + ui_input.h
                          + st7735s_driver.h + system_state.h
logic/button_filter.c  ──> 无（仅 stdbool/stdint）
logic/theme_config.c   ──> theme_config.h（仅 stdint/stdbool/stddef）
logic/theme_commit_policy.c ──> theme_commit_policy.h（仅 stdint/stdbool）
logic/theme_switch.c   ──> theme_switch.h + ui_theme_id.h（零依赖）
logic/ 不得反向依赖任何模块
platform/board_button.c ──> hardware/gpio.h + ui_input.h + button_filter.h
app/theme_commit_task.c ──> theme_commit_policy.h + theme_storage.h
                            + system_state.h
platform/theme_storage.c ──> hardware/flash.h + pico/flash.h + pico/stdlib.h
                            + FreeRTOS.h（堆查询与 configASSERT）
                            + theme_config.h + ui_theme_id.h
```

架构不变量是「**表现层不 include pico-sdk 头**，硬件能力全部藏在函数指针或零依赖抽象头
后面」。注意 `st7735s_driver.h` 只 include `stdbool/stddef/stdint`，本身就是零 SDK 依赖
的抽象，`ui_render.c:10` 引用它并不违反上述不变量；真正被禁止的是直接 use SDK。

三条断言，挂到 `pico_freertos_firmware/check_style.sh`（仓库当前没有 CI，
不落进脚本就等于没有强制力）。**路径必须带 `${REL_FIRMWARE}/` 前缀**——该脚本在 `:52`
`cd -- "${REPO_ROOT}"`，cwd 是**仓库根**而不是固件目录：

```bash
# 命中即失败。grep 退出码 1 = 无命中 = 通过；2 = 路径/正则出错 = 必须失败。
# 绝对不要写成 `! grep ...`：路径写错时退出码 2 会被 ! 反转成「通过」。
check_absent() {
  local pattern="$1"
  shift
  local rc=0
  grep -rn -- "$pattern" "$@" >/dev/null || rc=$?
  if [ "$rc" -ne 1 ]; then
    echo "断言失败（rc=$rc）：$pattern"
    return 1
  fi
}

# 1) 表现层不得触碰 pico-sdk
check_absent '#include *["<]pico/' \
    "${REL_FIRMWARE}/src/display/ui_render.c" \
    "${REL_FIRMWARE}/src/display/ui_theme.c" \
    "${REL_FIRMWARE}/src/display/ui_layout.c"

# 2) 平台层与应用层不得反向上依赖表现层
check_absent '#include *["<]\(ui_render\|ui_theme\.h\|st7735s_driver\)' \
    "${REL_FIRMWARE}/src/platform/" "${REL_FIRMWARE}/src/app/"

# 3) 纯逻辑层不得引入任何 SDK 或硬件头
check_absent '#include *["<]\(hardware/\|pico/\)' "${REL_FIRMWARE}/src/logic/"
```

断言 1 必须**显式列文件**，不能扫整个 `src/display/`：`src/display/st7735s_driver.c`
本来就 include 了 `hardware/dma.h`、`hardware/gpio.h`、`hardware/spi.h`、
`hardware/regs/dreq.h`、`pico/stdlib.h` 五条，它是驱动实现层，不受该约束。

三条断言与本功能的实现**同一次提交**落地，落地时 `src/logic/`、`src/display/ui_theme.c`、
`src/platform/board_button.c` 必然已存在，`grep` 在仓库根 cwd 下返回 `rc=1`。
断言与实现必须一起提交，不允许「先加断言、再补目录」的中间态：那段时间 `grep` 会因为
路径不存在返回 `rc=2`，被 `check_absent()` 判为失败——这正是想要的效果。

`theme_storage.c` 与 `board_button.c` 会被固件侧 `GLOB_RECURSE src/*.c` 自动编入，
但宿主测试的 `tests/CMakeLists.txt` 是**显式列举**，不会编进去——这个不对称是设计意图，
不是遗漏。

### 6.3 构建改动

`pico_freertos_firmware/CMakeLists.txt` 当前链接表缺少两个库，三个新 API 全部是未定义符号
（`pico_flash` 是 INTERFACE 库，只镜像链接 `pico_time`、`hardware_sync`，
**不会**把 `hardware_flash` 传递进来）：

```cmake
target_link_libraries(pico_status_screen PRIVATE
    cjson_lib
    FreeRTOS-Kernel
    FreeRTOS-Kernel-Heap4
    hardware_spi
    hardware_watchdog
    hardware_flash            # 新增：flash_range_erase / flash_range_program
    pico_flash                # 新增：flash_safe_execute
    pico_cyw43_arch_lwip_sys_freertos
    pico_mbedtls
    pico_stdlib)
```

`tests/CMakeLists.txt` 改两处、加四处：

- `add_executable(test_ui_render`（114–120 行源文件列表）补 `../src/display/ui_theme.c`；
- `add_executable(test_pipeline_e2e`（129–137 行源文件列表）补同一文件；
- 新增 4 个测试目标（合计 13 → 17 套）：
  - `test_ui_theme`：主表 6 × 17 项对比度/色差断言（§8.5 R-7），只编 `ui_theme.c`；
  - `test_button_filter`：2/2 采样消抖；
  - `test_theme_config`：12 字节打包、解析与全部校验分支（覆盖 §8.2 N-1..N-8）；
  - `test_theme_commit_policy`：5 s 去抖、零擦写短路、绕回（含 `theme_switch.c`），
  以及两条终止性断言——「连续两次 `Verify` 后必须停在 `Failed`」「`Failed` 不会因截止时刻
  已过而被下一个节拍拉回 `Writing`」。这两条正是 v1.5.2 补上的状态门。

**每个新目标都必须同时补一条 `add_test()`**。`tests/CMakeLists.txt:50` 有
`enable_testing()`，`:92-94` 的原注释写得很清楚：「后续任务的测试目标请追加到本行之前，
并在下面补 add_test」。只加 `add_executable` 而不加 `add_test`，`ctest` 仍然只跑 13 套，
§8.5 R-4 的「17 套全绿」就会变成假通过——这是第三轮复审专门点出来的坑。

---

## 7. 变更影响面

| 文件 / 模块 | 变更 | 连带影响 |
| :--- | :--- | :--- |
| `include/ui_theme_id.h`（新） | `UiThemeId` 枚举，零依赖 | `system_state.h` 引用 |
| `include/ui_input.h`（新） | `UiInputSource`，零依赖 | `ui_render.h`、`board_button.c` 引用 |
| `include/ui_theme.h`（新） | 调色板、`ui_theme_get` | 新增 `test_ui_theme` |
| `src/display/ui_theme.c`（新） | 6 套常量表 | 同上 |
| `include/system_state.h` | `+ theme_id`、`+ system_state_set_theme` | `test_system_state` 新增用例 |
| `src/state/system_state.c` | 锁内写入、初始化默认值、越界夹紧 | 同上 |
| `src/display/ui_render.c` | 删 10 个 `kColor*` 枚举改取调色板；`collect_dirty_regions` 加全屏早退；新增输入源登记；`render_top_bar()` 增 `HH:MM:SS` | `test_ui_render` 有 2 个常量随主题 0 新值同步更新，22 处断言谓词不变 |
| `src/display/ui_layout.c` | `+ prev->theme_id != curr->theme_id` → 全屏矩形 + 早退 | `test_ui_layout` 新增用例 |
| `include/theme_storage.h` + `src/platform/theme_storage.c`（新） | 擦写、回读、结果码（薄适配层） | 仅固件侧编入；逻辑已下沉 `test_theme_config` |
| `include/button_filter.h` + `src/logic/button_filter.c`（新） | 2/2 采样消抖 | 新增 `test_button_filter` |
| `include/theme_config.h` + `src/logic/theme_config.c`（新） | 12 字节打包/解析/校验 | 新增 `test_theme_config` |
| `include/theme_commit_policy.h` + `src/logic/theme_commit_policy.c`（新） | 完整提交状态机：去抖、零擦写、`Verify` 单次重试、终止条件（注入时钟） | 新增 `test_theme_commit_policy` |
| `include/theme_switch.h` + `src/logic/theme_switch.c`（新） | `(current + 1) % kThemeCount` 绕回 | 并入 `test_theme_commit_policy` |
| `include/board_button.h` + `src/platform/board_button.c`（新） | GP15 采样 | 仅固件侧编入 |
| `include/theme_commit_task.h` + `src/app/theme_commit_task.c`（新） | 任务创建 + 周期驱动 | 仅固件侧编入 |
| `src/main.c` | 组合根装配 | 冷启动顺序 |
| `CMakeLists.txt` | `+ hardware_flash + pico_flash`、链接脚本覆盖 | 冷构建 |
| `linker/section_extra_post_end.incl`（新） | 链接期边界断言 | 冷构建 |
| `check_style.sh` | 新增 3 条依赖方向断言（带 `${REL_FIRMWARE}/` 前缀） | 样式检查 |
| `tests/CMakeLists.txt` | 2 个目标补源文件；新增 4 个目标 | 13 → 17 套 |
| `docs/page_switch_policy.md` | 补一节「主题与页面模式正交」 | 契约一致性 |
| `README.md` | 主题 0 有 2 个颜色变化（`track`/`tertiary`），但 README 未记载具体色值 | 无需更新 |

**注意**：`ui_render.h` 的公开 API 没有 palette 参数。主题通过 `snapshot.theme_id`
注入，测试无法注入任意调色板——这是刻意的接口约束，保证显示路径只有一个真相来源。

---

## 8. 验收标准

### 8.1 功能（实机）

| 编号 | 判据 |
| :--- | :--- |
| F-1 | 连按 6 次，主题依次为 0→1→2→3→4→5→0 |
| F-2 | 停在任一主题并**停留 > 5 s**（等去抖落盘）后复位，重启仍为该主题。若在 5 s 内复位，重启回退到上一次已保存的主题——这是设计行为，不是缺陷 |
| F-3 | 页面 A/B 自动切换行为与改动前完全一致 |
| F-4 | 六套主题下顶栏图标、进度条、胶囊、农历行的颜色归属与 §3.3 落点表一致 |

### 8.2 功能（宿主注入，替代不可复现的实机操作）

擦除单扇区、伪造 `theme_id = 200`、在 400 ms 擦除窗口内精确掉电，这三件事在实机上
**没有可复现的操作手段**（该 picotool 构建甚至没有 `erase` 子命令）。它们改由
`test_theme_config` 直接喂字节数组完成——被测函数是 `theme_config_parse()`，
它不碰任何硬件：

| 编号 | 注入内容 | 期望 |
| :--- | :--- | :--- |
| N-1 | 扇区全 `0xFF`（擦除态） | `theme_config_parse()` 返回 `false`；`theme_storage_init()` 回退 `kThemeM3Dark` |
| N-2 | 扇区全 `0x00`（空白态） | 同上，魔数不匹配 |
| N-3 | 合法配置但 `theme_id = 200` | 同上，且必须**不越界索引** `s_themes`（宿主侧用 ASan 或边界断言兜底） |
| N-4 | `checksum` 错 1 位 | 同上，校验和不匹配 |
| N-5 | `version = 2` | 同上，版本不匹配 |
| N-6 | `reserved[0] = 1` | 同上，保留字段必须为 0 |
| N-7 | 整页全 `0xFF`（「擦除完成、编程未开始」掉电的等价形态） | 同 N-1 |
| N-8 | 前 4 字节已写、其余 `0xFF`（「编程中途」掉电的等价形态） | 同上（`checksum` 不可能匹配） |

### 8.3 性能

| 编号 | 判据 |
| :--- | :--- |
| P-1 | 按键到屏幕开始变化 ≤ 100 ms。推导：2/2 采样占 2 个渲染周期 = 40 ms（首拍 0–20 ms 内完成第一次采样），判定当拍即发起全屏重绘，刷屏 35 ms，合计 **≈ 75 ms**，判据留 25 ms 余量 |
| P-2 | 主题切换为一次全屏重绘，10 个切片，无黑帧 |
| P-3 | 单次提交耗时：预算 ≈ 403 ms，上界 ≤ 650 ms（100 + 400 + 3 + 100，另留 47 ms 抖动余量）；403 ms 里的 400 ms 已取扇区擦除上界，不是实测典型值 |
| P-4 | 构造 `prev.theme_id != curr.theme_id` 的输入，断言 `ui_render_collect_dirty_regions()` 返回 **1** 个矩形（`{0,0,128,160}`），不追加顶栏/时间卡/日历卡 |

### 8.4 持久化

| 编号 | 判据 | 观测手段 |
| :--- | :--- | :--- |
| S-1 | 连续快速切换 20 次（每次停留 < 5 s）→ 0 次擦写 | 串口日志中 `theme: commit` 行数为 0 |
| S-2 | 停留 > 5 s → 恰好 1 次擦写 | 日志 `theme: commit` 出现 1 行，且 `rc=0` |
| S-3 | 当前主题 == 已保存主题时不写 → 0 次擦写 | 快速连按 6 次绕回已保存主题（期间每次停留 < 5 s），再停留 10 s，日志仍为 0 行 |
| S-4 | 提交后回读 12 字节与预期逐字节一致 | `theme_storage_commit()` 内部回读，失败返回 `kThemeStorageErrVerify` |
| S-5 | 掉电语义（§5.4.5 三行） | 由 N-7 / N-8 覆盖，实机不再要求人工掉电 |

### 8.5 视觉（实机）与资源

| 编号 | 判据 |
| :--- | :--- |
| V-1 | 六套主题在实机上目视复验：§3.4.2 标粗的 3 处 CR 擦线（4.70 / 4.73 / 4.77）、主题 0 的两项层级缺陷（6.59 / 4.72，只能判断「卡片能否与画布分开」，无法量测），以及**细笔画可读性**——14 px 中文与 8 px ASCII 小字在六套下的实际观感 |
| V-2 | 实机确认页面 A 顶栏 `HH:MM:SS` 在 88 px 空档（x = 20…107）内不与两枚 16 × 14 图标重叠（左右各余 12 px），六套配色下 `text`/`background` 可读；未同步时 `--:--:--` 与 `muted` 的观感 |
| R-1 | Flash 增量 ≤ 1 KB（主题表 144 B + 代码 + 字符串），不含 4 KB 配置扇区 |
| R-2 | RAM 增量 ≤ 2 KB（页缓冲 256 B + 提交任务静态栈 1 KB + 少量状态）；不含 `flash_safe_execute` 每次约 1.1 KB 的临时动态堆 |
| R-3 | `check_style.sh` 通过（含 §6.2 三条依赖断言），代码块与源文件均 ≤ 80 列 |
| R-4 | 宿主测试 **17** 套全绿；`test_ui_render` 的 22 处 `kTest*` 引用全部通过（分布在 **22 条 `assert` 语句**里，10 条内联、12 条在续行）；其中 `kTestTrack` 与 `kTestTertiary` 必须与 `ui_render.c` 同步改为 `0x0020` / `0xFE00`，其余 8 个逐字节不变 |
| R-5 | 冷构建无警告（`-Wall -Wextra -Werror`） |
| R-6 | 镜像末尾 ≤ `0x101FF000`：链接期 `ASSERT` 与启动期 `panic` 均存在 |
| R-7 | `test_ui_theme` 遍历 6 套 × §3.4.1 的 15 个 CR 项 + 2 个 ΔL 项逐项断言；配对与阈值只在 `ui_theme.h` 的 `kThemePairs[]` 里定义一份，规格 §3.4 与测试共用同一张表。**另加一条独立断言**：§3.3 规则 2 的 ΔL\*(`surface`,`track`) ≥ 10（正值即更暗），**六套全部覆盖、无豁免**——它不在 17 项内，不参与 17 / 102 的计数 |

实机烧录与验证必须由用户手动确认后执行，本规格不授权自动烧录。

### 8.6 执行步骤

```bash
# 0) 全部在仓库根执行。构建目录必须落在 pico_freertos_firmware/tests/build：
#    src/assets/asset_store.c:66-72 按 cwd 的相对路径找 ../../assets，
#    换一个构建目录会有 4 套测试读不到字库而失败。

# 1) 宿主测试（17 套）
cmake -S pico_freertos_firmware/tests -B pico_freertos_firmware/tests/build
cmake --build pico_freertos_firmware/tests/build -j
ctest --test-dir pico_freertos_firmware/tests/build --output-on-failure

# 2) 样式与依赖方向断言（脚本自己在 :52 cd 到仓库根）
( cd pico_freertos_firmware && ./check_style.sh )

# 3) 固件冷构建（镜像末尾由链接期 ASSERT 把关）
( cd pico_freertos_firmware && ./build.sh )

# 4) 实机（需用户手动确认）
#    烧录后按 §8.1 / §8.4 / §8.5 逐项核对，串口日志计数 commit / skip 行数
```

---

## 9. 风险与权衡

| 风险 | 概率 | 后果 | 处置 |
| :--- | :---: | :--- | :--- |
| 最后 5 s 内的切换因掉电丢失 | 中 | 低 | 有意权衡，写入 §8.4 S-1 |
| 擦除期间画面冻结 | 中 | 中 | 预算 403 ms、最坏 603 ms（验收上界 650 ms）；知情接受，备选方案见 §12 D-4 |
| 擦除期间 Wi-Fi/lwIP 停顿导致丢包 | 中 | 低 | 3 s 离线判定不受影响 |
| `flash_safe_execute` 建避难任务时堆不足（`-9`，实际先撞 malloc 钩子） | 低 | 高 | 同一根因只记一次：预检 `xPortGetFreeHeapSize() >= 1536`（经验值，不保证连续块）只能降低概率；分配真失败时 `pvPortMalloc` 先调 `main.c:103-109` 的死循环钩子，`-9` 根本返回不出来，后果是 8 s 复位。`kThemeStorageErrNoResources` 因此永远返回不出来，按不重试转 `Failed` 处理即可 |
| 握手超时（`PICO_ERROR_TIMEOUT`） | 低 | **高** | enter / exit 超时**都不重试**；只有回读不一致重试 1 次、间隔 1 s。**在返回码分派里只有这两种握手超时才必然 8 s 复位**（分派之外还有 malloc 钩子那条路，见上一行）；`-9` 不属握手超时且今天不可达（上一行），按分配型失败转 `Failed`、不重试（§5.4.3） |
| 掉电写坏配置 | 低 | 低 | 校验回退主题 0；不承诺「旧值必存活」 |
| 未来镜像膨胀压过 Sector 511 | 低 | 高 | 链接期 `ASSERT` + 启动期 `panic` |
| 主题 0 两项层级缺陷（§3.4.4） | 确定 | 低 | 登记不修；`track` 与 `tertiary` 已在 v1.6.0 改值，仅余 ΔL\* 6.59 / 4.72 |
| 主题命名与参考 HTML 的流派名不一致 | 确定 | 低 | 文档显式声明「仅配色」，见 §12 D-1 |
| 触点抖动 > 40 ms 被判成两次单击 | 低 | 低 | 2/2 采样的前提假设（§2.2）；常见轻触按键抖动 0.5–10 ms |
| 按住按键上电 → 主题 0→1 并落盘 | 低 | 低 | 有意接受（§4.1）；后果只是开机主题变下一个，不会变砖 |
| RTOS tick 停摆使全部软件超时顺延 | 中 | 低 | 一次停摆 403–603 ms，5 s 去抖同量级顺延；`get_absolute_time()` 域不受影响（§5.5） |

---

## 10. 回滚方案

| 层级 | 动作 |
| :--- | :--- |
| 代码回滚 | `git revert` 本次提交；Sector 511 的内容保留，旧固件不读它，**无需擦除**。但注意：回滚后再升级，新固件会读回同一个 `theme_id`——用户当初选的主题不会因回滚而丢失 |
| 配置回滚 | 不需要动作。若必须强制清空，只能写一次性调试固件调用 `flash_range_erase(kThemeFlashOffset, FLASH_SECTOR_SIZE)`，或用 SWD 擦除；v1 不提供产品命令 |
| 现场回滚 | 保留上一版 `.uf2`，BOOTSEL 重烧；不需要拆按键 |
| 数据迁移 | v1 首次引入，无历史数据需要迁移 |

---

## 11. 文案宽度与字库约束

### 11.1 字号事实

| 类别 | 宽 × 高 | 步进 | 说明 |
| :--- | :--- | :---: | :--- |
| ASCII | 8 × 14 | 8 | 71 个字符白名单 |
| 中文 | 14 × 14 | 14 | 341 字白名单 |
| 时钟数字 | 14 × 26 | 14 | `CLOCK_CHARACTERS = "0123456789"`，仅 10 个数字 |
| 时钟冒号 | 8 × 26 | 10 | 独立字形，与数字不同宽 |

`"12:34:56"` 的宽度 = 6 个数字 × 14 + 2 个冒号 × 10 = 84 + 20 = **104 px**
（若从 `x = 0` 起画，右边界到 104）。

`font_engine_draw_glyph` 在 `x + width > surface->width` 时**直接截断，不换行、不省略号**
（`font_engine.c:892-894`）。因此宽度必须事前算准。

### 11.2 宽度规则

- 卡片内单行：`font_engine_text_width() <= 116` px（卡片 `x = 4`、宽 120，留 4 px 呼吸）；
- 全屏单行：`<= 124` px（左右各留 2 px）；
- v1.1.0 的「单行汉字不超过 8 个 = 112 px，彻底消除爆框」**不成立**——汉字与 ASCII 混排
  时 8 个字早已超宽。实测：

| 文案 | 宽度 | 结论 |
| :--- | ---: | :--- |
| `[CPU] 43.0% 稳定` | 124 px | 超卡片限制（全屏恰好擦线） |
| `监督任务: 正常喂狗` | 128 px | 超屏（`x=4` 起 → 132） |
| `通道一·监测 12:34:56` | 150 / 156 / 196 px | 严重超屏。三种口径：`·` 不在 ASCII 白名单、按回退 `?` 8 px 算 → 150 px；按 `·` 占 14 px 算 → 156 px；若 `12:34:56` 走时钟字体则 196 px（按 `·` 占 14 px 计；若 `·` 也回退成 8 px 则为 190 px）。无论哪种都远超 116 px |
| `巡检状态: 扫描完成 // 无异常` | 202 px | 严重超屏 |
| `2026年11月6日 星期五` | 148 px | 超屏 |

上表来自参考 HTML 的文案，**不得直接搬进固件**。v1 不改任何屏幕文案，故不触发该风险；
但新增测试应遍历现有全部 UI 字符串，断言宽度不超限。

- 新增的顶栏时刻属**新文案**：`font_engine_text_width("12:34:56")` = 8 个 ASCII × `kFontAsciiWidth` 8
  = **64 px**（`font_engine.c:806-815`），落在 88 px 空档内、左右各余 12 px。它与时钟页
  14 × 26 数字字体的 104 px 不是同一套字形，两者不可互相折算。

**农历行是既有零余量边界**：`甲子年`(42) + 空格(8) + `闰冬月三十`(70) = **120 px**，
正好等于卡片宽，从 `x = 4` 起恰好贴边到 124。它不满足上面的 116 px 新规则，属既有文案，
v1 不动；这也说明「116 px」是给**新文案**的门槛，不是对存量文案的回归门。

### 11.3 字库与 ASCII 缺口

- 主题名只进日志（UTF-8，由主机终端渲染），**不经过字体引擎**，因此不产生缺字；
- 参考 HTML 用到的 `[ ] # >`、`·`、`●`、`■` 均不在 ASCII 白名单，会回退为 `?`/`□`；
  v1 不扩字集；
- 341 的硬编码分布在 **5 处**：`include/font_engine.h:19`、`:40`（注释）、
  `include/asset_store.h:22`（注释）、`tests/test_asset_store.c:7`（注释）、`:8`（定义）。
  `tests/test_asset_store.c:17` 走的是 `kFontGlyphCount` 宏，不含字面量。若将来扩字库，
  必须通过 `tools/build_font.py` 重新生成 `assets/font_cn.bin`，并同步上述全部位置。

---

## 12. 未决项（待用户确认）

| 编号 | 问题 | 当前规格采用的默认值 | 备选 |
| :--- | :--- | :--- | :--- |
| D-1 | 主题用颜色名还是流派名 | **颜色名（黑琥珀/荧光绿/霓虹青/深海青/霓虹洋红）**，已定 v1.6.0：流派名描述的是未实现的版式（§1.3 非目标），颜色名与实测色相一致 | 保留流派名 + 免责声明 |
| D-2 | 页面 A 顶栏是否加时刻 | **加 `HH:MM:SS`**，已定 v1.7.0（用户 m01284 选 A）：8 × 14 ASCII，x = 32…95、y = 1，`text` 色，未同步时 `muted` 画 `--:--:--`；每秒一个 `{32,1,64,14}` 脏矩形，落 1 个切片、4096 B、约 3.3 ms（0.33 % 占空）；不扩字库、不新增对比度配对 | 不加；或只到分（`HH:MM`，40 px，x = 44…83，每分钟一次） |
| D-3 | 主题名是否上屏 | 不上屏，只进日志 | 上屏需补 **10 个汉字**：琥/珀/荧/光/绿/霓/虹/洋/红 + `色`（黑、深、海、青已在白名单） |
| D-4 | 擦写期间画面冻结 ≈ 403 ms 是否能接受 | 接受 | 仅在时钟页提交；或延迟到夜间空闲 |
| D-5 | 是否扩字库 / 扩 ASCII | 不扩 | 按参考 HTML 约 +122 字（+11.7 KB Flash、+488 B RAM） |
| D-6 | 主题 0 的四项已知缺陷是否修 | **修两项（`track`/`tertiary`），不修两项（ΔL\* 层级）**；已落 v1.6.0 | 修：槽方向错误与告警同色落在 1.31 mm / 0.88 mm 的薄元素上，尺寸放大了后果；不修：6.59 / 4.72 是大色块，可辨 |
| D-7 | 首次上电的默认主题 | **0（M3 深色）**，已定 v1.6.0：其余五套从未实机验证，不作出厂默认；`theme_storage_init()` 回退与 `theme_storage_commit()` 夹紧两处均指向它 | 1（黑琥珀） |
| D-8 | 是否做双扇区 A/B 备份 | 不做 | 做则 +4 KB 与约 20 行代码，掉电不会丢旧值 |
| D-9 | 长按是否加速循环 | 不加速 | 长按 500 ms 后每 200 ms 切一次 |
| D-10 | 是否需要组合键恢复默认 | 不需要 | 需第二个按键或开机时长按 |
| D-11 | 是否顺手修 `src/main.c:103-109` 的 malloc 失败钩子 | 不修（属规格外改动，单独提交） | 改为「记录 + 返回 `NULL`」。这**不是**让 `kThemeStorageErrNoHeap` 可达的前提（预检那条路本来就走得到），而是兜住「预检被碎片化击穿」时的最后一道缝：现状下只要真的 malloc 失败，钩子就死循环、8 s 后复位。它也是唯一能让 `kThemeStorageErrNoResources` 从「不可达」变为「可达」的改动（但即便如此，该码仍按不重试处理） |

---

## 13. 自检（可机器核对）

每条都给出命令或可复算依据，不写「通过」了事。

| 检查项 | 结论 | 核对方式 |
| :--- | :--- | :--- |
| 无占位符 | 通过 | `grep -n 'TODO\|TBD\|待定' <file> \| grep -v '无占位符'` 输出 0 行（全文唯一命中是本行模式串自身） |
| 范围已裁定 | 通过 | §1.2 写死「仅换配色」，§1.3 列 6 项非目标 |
| 主题 0 的色值口径 | 通过（已更新） | 10 个里 8 个与 `ui_render.c:22-31` 逐字节相等；`track`/`tertiary` 为 v1.6.0 显式改值，见 §3.2 注与 §3.4.4 |
| 无越界读 | 通过 | §5.3 页缓冲 + 预填充 + `_Static_assert(<= FLASH_PAGE_SIZE)` |
| 结构体布局被锁定 | 通过 | §5.2 三条 `offsetof` 断言 + `sizeof == 12` |
| 构建依赖完整 | 通过 | §6.3 补 `hardware_flash` 与 `pico_flash`（后者是 INTERFACE 库，不传递） |
| 心跳不被伪造 | 通过 | 全文无 `FIRMWARE_CHECK_IN`；写窗口 603 ms < UI 槽位 1000 ms |
| 失败路径与看门狗对齐 | 通过 | §5.4.3 写清两条超时路径；复位口径限定为「在提交返回码的分派里，只有握手超时才必复位，另有一条 malloc 钩子路径」，`kThemeStorageErrNoHeap` 在预检处**可达**（不依赖 D-11）；`-9` 的 `kThemeStorageErrNoResources` 已建模但**今天不可达**（malloc 钩子拦在前面，§9），该码按不重试转 `Failed`；15 s 重试预算已删除 |
| 结果码单一生产者 | 通过 | §5.4.1 七个码各有唯一生产点、唯一消费者是 `vTaskThemeCommit`；enter/exit 超时由 `s_callback_ran` 区分、`Verify` 与 `InvalidArg` 分属「可重试的落盘问题」与「不可重试的调用方违约」；`InvalidArg` 与 `NoResources` 两个码在现状下不可达，均在正文标注而非假装可达；状态机与终止条件由 `test_theme_commit_policy` 断言 |
| 动态堆已交底 | 通过 | §4.5 与 §9 承认 `flash_safe_execute` 每次 ≈1.1 KB，预检 1536 B 标为经验值且不保证连续块 |
| 镜像边界有守卫 | 通过 | §5.1 `section_extra_post_end.incl` + `pico_add_linker_script_override_path` + 启动期 `panic` |
| 对比度可测 | 通过 | §3.4.1 的 17 项由 §3.3 落点表**归并**得出（15 CR + 2 ΔL\*），§3.4.2 给 6 × 17 实测，102 个组合 100 通过 / 2 不通过且已登记 |
| 已知缺陷已登记 | 通过 | §3.4.4 剩两行（ΔL\* 6.59 与 4.72）；`track` 方向与 `tertiary`↔`error` 互辨已在 v1.6.0 修复 |
| 文案宽度可测 | 通过 | §11.2 规则 + 5 条实测反例 + 农历零余量说明 |
| 测试数量表述准确 | 通过 | §6.3 与 §8.5 一致写 13 → 17，4 个新目标逐一列名，并明确每个都要补 `add_test()` |
| 依赖方向可机器校验 | 通过 | §6.2 三条断言，路径带 `${REL_FIRMWARE}/` 前缀（`check_style.sh:52` 已 cd 到仓库根），含退出码语义 |
| 验收步骤可执行 | 通过 | §8.6 四步命令；不可复现的三项改用 §8.2 宿主注入 |
| 与 `page_switch_policy.md` 一致 | 部分 | 页面切换时序一致；该文档尚无主题章节，需按 §7 补一节「主题与页面模式正交」 |
