# 多主题配色与按键切换 实现计划

> **面向 AI 代理的工作者：** 必需子技能：使用 superpowers:subagent-driven-development（推荐）或 superpowers:executing-plans 逐任务实现此计划。步骤使用复选框（`- [ ]`）语法来跟踪进度。

**目标：** 让 Pico W 状态屏在 6 套配色间用单个按键循环切换，并把选择断电保存，主题切换为一次全屏重绘，不影响双页面自动切换与看门狗。

**架构：** 颜色令牌集中到 Flash 常量表 `s_themes[6]`，`ui_render.c` 每帧经 `ui_theme_get()` 取一次指针再绘制；按键、去抖、配置编解码、提交状态机全部下沉到 `src/logic/` 的四个纯单元（零 SDK 依赖，宿主可测）；Flash 读写与 GPIO 采样分别隔离在 `src/platform/theme_storage.c`、`src/platform/board_button.c`；`main.c` 是唯一知道装配顺序的组合根。

**技术栈：** C11、FreeRTOS SMP（RP2040 双核）、pico-sdk 2.3.1（`hardware_flash` / `pico_flash` / `hardware_watchdog`）、CMake + Ninja、`arm-none-eabi-gcc` 13.2、项目自带 13 套 ctest、`.clang-format`（Google、80 列）。

**规格：** `docs/superpowers/specs/2026-11-10-theme-storage-button-switch-design.md`（v1.8.4）。本计划中的每个数值都能在该文件里找到出处；两者冲突时以规格为准，并回头改本计划。

---

## 硬约束（每个任务都必须满足）

1. **主题 0 的 8 个色值逐字节冻结**：`0x1083 / 0x1904 / 0x2945 / 0xe71d / 0x9493 / 0xae3f / 0xa699 / 0xf5d6`；只有 `track`（`0x31a7→0x0020`）与 `tertiary`（`0xf5f1→0xFE00`）改值，且 `tests/test_ui_render.c:16-25` 必须同步改这两个常量。
2. **表现层不 include pico-sdk 头**；硬件能力只经函数指针或零依赖抽象头进入。
3. **所有源文件 ≤ 80 列、Google 风格**，每次提交前跑 `( cd pico_freertos_firmware && ./check_style.sh )`。
4. **提交信息用中文 Conventional Commits**，不写 AI 署名。
5. **不烧录实机**——烧录与 §8.1/§8.4/§8.5 的实机验收由用户手动执行。

## 文件结构

| 文件 | 职责 | 任务 |
| :--- | :--- | :---: |
| `include/ui_theme_id.h` | 零依赖：`UiThemeId` 枚举 | 1 |
| `include/ui_theme.h` | 调色板结构体、`ui_theme_get`、`kThemePairs[]`、令牌取值器 | 1 |
| `src/display/ui_theme.c` | `s_themes[6]` 常量表、越界回退、`kThemePairs[]` 定义 | 1 |
| `tests/test_ui_theme.c` | 6 套 × 17 项阈值 + ΔL\*(`surface`,`track`) | 1 |
| `include/button_filter.h`、`src/logic/button_filter.c` | 2/2 采样消抖 | 2 |
| `tests/test_button_filter.c` | 按下/释放/抖动/上电按住 | 2 |
| `include/theme_config.h`、`src/logic/theme_config.c` | 12 字节配置打包/解析/校验 | 3 |
| `tests/test_theme_config.c` | 覆盖 §8.2 N-1..N-8 | 3 |
| `include/render_window.h`、`src/logic/render_window.c` | 渲染忙标志（零依赖） | 4 |
| `include/theme_switch.h`、`src/logic/theme_switch.c` | `(current + 1) % kThemeCount` | 4 |
| `include/theme_commit_policy.h`、`src/logic/theme_commit_policy.c` | 5 状态机、5 s 去抖、零擦写短路、Verify 只重试一次 | 4 |
| `tests/test_theme_commit_policy.c` | 状态机 + 绕回 + 渲染窗口 + 两条终止性断言 | 4 |
| `include/system_state.h`、`src/state/system_state.c` | `theme_id` 字段与 `system_state_set_theme()` | 5 |
| `src/display/ui_layout.c` | 主题变化 → 全屏脏矩形 | 5 |
| `src/display/ui_render.c` | 改取调色板、顶栏时刻、早退、渲染窗口 | 5 |
| `tests/test_ui_render.c` | 两个 `kTest*` 常量改值 | 5 |
| `include/theme_storage.h`、`src/platform/theme_storage.c` | 边界守卫、读回、擦写、结果码 | 6 |
| `linker/section_extra_post_end.incl` | 链接期镜像边界断言 | 6 |
| `include/board_button.h`、`src/platform/board_button.c` | GP15 采样 | 7 |
| `include/theme_commit_task.h`、`src/app/theme_commit_task.c` | 100 ms 周期任务：策略 + I/O | 7 |
| `include/ui_input.h`、`include/ui_render.h`、`src/display/ui_render.c` | 输入源登记 | 7 |
| `src/main.c` | 组合根装配 | 7 |
| `pico_freertos_firmware/CMakeLists.txt` | 补 `hardware_flash`、`pico_flash`、链接脚本钩子 | 6 |
| `pico_freertos_firmware/check_style.sh` | 三条依赖方向断言 | 7 |
| `pico_freertos_firmware/tests/CMakeLists.txt` | 5 处新增目标 + 2 处补源文件 | 1/2/3/4/5 |

**任务顺序的依赖关系：** 1 → (2, 3) → 4 → 5 → 6 → 7 → 8。任务 5 依赖 1；任务 6 依赖 3；任务 7 依赖 2 与 4。

---

### 任务 1：主题编号、调色板与对比度门禁

**文件：**
- 创建：`pico_freertos_firmware/include/ui_theme_id.h`
- 创建：`pico_freertos_firmware/include/ui_theme.h`
- 创建：`pico_freertos_firmware/src/display/ui_theme.c`
- 测试：`pico_freertos_firmware/tests/test_ui_theme.c`
- 修改：`pico_freertos_firmware/tests/CMakeLists.txt`

- [ ] **步骤 1：写失败测试**

创建 `pico_freertos_firmware/tests/test_ui_theme.c`：

```c
/**
 * @file test_ui_theme.c
 * @brief 把规格 §3.4.1 的 17 项阈值变成可执行断言。
 *
 * 测试自己实现 WCAG 2.1 相对亮度与 CIELAB 明度差，不依赖任何外部库，
 * 这样「阈值是否满足」不取决于被测代码的实现细节。
 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include "ui_theme.h"

/** @brief RGB565 通道按位复制扩到 8 bit。 */
static void rgb565_to_rgb888(uint16_t color, double* r, double* g, double* b) {
  const unsigned r5 = (color >> 11) & 0x1Fu;
  const unsigned g6 = (color >> 5) & 0x3Fu;
  const unsigned b5 = color & 0x1Fu;
  *r = (double)((r5 << 3) | (r5 >> 2));
  *g = (double)((g6 << 2) | (g6 >> 4));
  *b = (double)((b5 << 3) | (b5 >> 2));
}

/** @brief sRGB 分量转线性光。 */
static double srgb_to_linear(double channel) {
  const double c = channel / 255.0;
  return c <= 0.04045 ? c / 12.92 : pow((c + 0.055) / 1.055, 2.4);
}

/** @brief WCAG 2.1 相对亮度。 */
static double relative_luminance(uint16_t color) {
  double r = 0.0;
  double g = 0.0;
  double b = 0.0;
  rgb565_to_rgb888(color, &r, &g, &b);
  return 0.2126 * srgb_to_linear(r) + 0.7152 * srgb_to_linear(g) +
         0.0722 * srgb_to_linear(b);
}

/** @brief WCAG 2.1 对比度，返回较大者对较小者。 */
static double contrast_ratio(uint16_t a, uint16_t b) {
  const double la = relative_luminance(a);
  const double lb = relative_luminance(b);
  const double hi = la > lb ? la : lb;
  const double lo = la > lb ? lb : la;
  return (hi + 0.05) / (lo + 0.05);
}

/** @brief CIELAB 的 L* 分量（D65）。 */
static double lab_lightness(uint16_t color) {
  double r = 0.0;
  double g = 0.0;
  double b = 0.0;
  rgb565_to_rgb888(color, &r, &g, &b);
  const double rl = srgb_to_linear(r);
  const double gl = srgb_to_linear(g);
  const double bl = srgb_to_linear(b);
  const double y = 0.2126729 * rl + 0.7151522 * gl + 0.0721750 * bl;
  const double yn = 1.0;
  const double t = y / yn;
  const double f =
      t > 0.008856 ? pow(t, 1.0 / 3.0) : (7.787 * t) + 16.0 / 116.0;
  return (116.0 * f) - 16.0;
}

/** @brief 有符号明度差 ΔL*(a, b) = L*(a) - L*(b)。 */
static double delta_l(uint16_t a, uint16_t b) {
  return lab_lightness(a) - lab_lightness(b);
}

/** @brief 主题 0 已登记的两项视觉缺陷，不参与「必须达标」判定。 */
static bool is_registered_defect(unsigned theme, unsigned rule) {
  return theme == (unsigned)kThemeM3Dark &&
         (rule == 15u || rule == 16u); /* 规格表第 16、17 项 */
}

int main(void) {
  for (unsigned theme = 0; theme < (unsigned)kThemeCount; ++theme) {
    const UiThemePalette* palette = ui_theme_get((UiThemeId)theme);
    if (palette == NULL) {
      printf("FAIL: ui_theme_get(%u) 返回 NULL\n", theme);
      return EXIT_FAILURE;
    }
    for (unsigned rule = 0; rule < kThemePairCount; ++rule) {
      const ThemePairRule* pair = &kThemePairs[rule];
      const uint16_t fg = ui_theme_token(palette, pair->foreground);
      const uint16_t bg = ui_theme_token(palette, pair->background);
      const double value =
          pair->use_delta_l ? delta_l(fg, bg) : contrast_ratio(fg, bg);
      if (is_registered_defect(theme, rule)) {
        if (value >= (double)pair->min_value) {
          printf("FAIL: 主题 %u 的 %s 已达标（%.2f ≥ %.2f），"
                 "应从已知缺陷表移除\n",
                 theme, pair->label, value, (double)pair->min_value);
          return EXIT_FAILURE;
        }
        continue;
      }
      if (value < (double)pair->min_value) {
        printf("FAIL: 主题 %u 的 %s = %.2f，低于阈值 %.2f\n", theme,
               pair->label, value, (double)pair->min_value);
        return EXIT_FAILURE;
      }
    }
    /* 规格 §3.3 规则 2：槽底必须比卡片暗，且差距 ≥ 10。不在 17 项内。 */
    const double track_delta =
        delta_l(ui_theme_token(palette, kThemeTokenSurface),
                ui_theme_token(palette, kThemeTokenTrack));
    if (track_delta < 10.0) {
      printf("FAIL: 主题 %u 的 ΔL*(surface,track) = %.2f < 10\n", theme,
             track_delta);
      return EXIT_FAILURE;
    }
  }
  printf("PASS: 6 套 × 17 项与 ΔL*(surface,track) 全部符合规格\n");
  return EXIT_SUCCESS;
}
```

- [ ] **步骤 2：在 `tests/CMakeLists.txt` 里加目标并跑，确认失败**

在 `enable_testing()`（第 50 行）之后、`add_test` 块之前插入：

```cmake
add_executable(test_ui_theme
    test_ui_theme.c
    ../src/display/ui_theme.c
)
target_link_libraries(test_ui_theme m)

add_test(NAME ui_theme COMMAND test_ui_theme)
```

运行：

```bash
cmake -S pico_freertos_firmware/tests -B pico_freertos_firmware/tests/build
cmake --build pico_freertos_firmware/tests/build -j --target test_ui_theme
```

预期：编译失败，报 `ui_theme.h: No such file or directory`。

- [ ] **步骤 3：创建 `include/ui_theme_id.h`**

```c
#ifndef PICO_FREERTOS_FIRMWARE_INCLUDE_UI_THEME_ID_H_
#define PICO_FREERTOS_FIRMWARE_INCLUDE_UI_THEME_ID_H_

/** @brief 主题编号；零依赖，state / display / app 都可 include。 */
typedef enum {
  kThemeM3Dark = 0,      /**< M3 深色 */
  kThemeMonoRed = 1,     /**< 黑琥珀 */
  kThemePhosphor = 2,    /**< 荧光绿 */
  kThemeCeladon = 3,     /**< 霓虹青 */
  kThemeDeepTeal = 4,    /**< 深海青 */
  kThemeNeonMagenta = 5, /**< 霓虹洋红 */
  kThemeCount = 6
} UiThemeId;

#endif /* PICO_FREERTOS_FIRMWARE_INCLUDE_UI_THEME_ID_H_ */
```

- [ ] **步骤 4：创建 `include/ui_theme.h`**

```c
#ifndef PICO_FREERTOS_FIRMWARE_INCLUDE_UI_THEME_H_
#define PICO_FREERTOS_FIRMWARE_INCLUDE_UI_THEME_H_

#include <stdbool.h>
#include <stddef.h>
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

/** @brief 令牌枚举，供对比度规则表按名字取值。 */
typedef enum {
  kThemeTokenBackground = 0,
  kThemeTokenSurface,
  kThemeTokenSurfaceHigh,
  kThemeTokenTrack,
  kThemeTokenText,
  kThemeTokenMuted,
  kThemeTokenPrimary,
  kThemeTokenSecondary,
  kThemeTokenTertiary,
  kThemeTokenError,
  kThemeTokenCount
} UiThemeToken;

/** @brief 一条对比度门禁：前景令牌 vs 背景令牌。 */
typedef struct {
  const char* label;        /**< 断言失败时的可读名称 */
  UiThemeToken foreground;  /**< 前景令牌 */
  UiThemeToken background;  /**< 背景令牌 */
  bool use_delta_l;         /**< false = WCAG CR，true = ΔL* */
  float min_value;          /**< 阈值 */
} ThemePairRule;

/** @brief 规格 §3.4.1 的 17 项，定义在 ui_theme.c。 */
extern const ThemePairRule kThemePairs[];
extern const size_t kThemePairCount;

/** @brief 按令牌取色值。 */
uint16_t ui_theme_token(const UiThemePalette* palette, UiThemeToken token);

/** @brief 取主题调色板；越界 id 回退 kThemeM3Dark，永不返回 NULL。 */
const UiThemePalette* ui_theme_get(UiThemeId theme_id);

#endif /* PICO_FREERTOS_FIRMWARE_INCLUDE_UI_THEME_H_ */
```

- [ ] **步骤 5：创建 `src/display/ui_theme.c`**

`s_themes` 的 6 段常量表逐字照抄规格 §3.2 的代码块（含 `kThemeM3Dark` 的
`.track = 0x0020`、`.tertiary = 0xFE00` 与 `kThemeDeepTeal` 的 `.primary = 0x07BA`、
`.tertiary = 0xFFE0`），在其后补：

```c
const ThemePairRule kThemePairs[] = {
    {"text/surface", kThemeTokenText, kThemeTokenSurface, false, 4.5f},
    {"muted/surface", kThemeTokenMuted, kThemeTokenSurface, false, 4.5f},
    {"text/background", kThemeTokenText, kThemeTokenBackground, false, 4.5f},
    {"muted/background", kThemeTokenMuted, kThemeTokenBackground, false, 4.5f},
    {"primary/surface", kThemeTokenPrimary, kThemeTokenSurface, false, 4.5f},
    {"secondary/surface", kThemeTokenSecondary, kThemeTokenSurface, false,
     4.5f},
    {"secondary/surface_high", kThemeTokenSecondary, kThemeTokenSurfaceHigh,
     false, 4.5f},
    {"tertiary/surface", kThemeTokenTertiary, kThemeTokenSurface, false, 4.5f},
    {"primary/track", kThemeTokenPrimary, kThemeTokenTrack, false, 3.0f},
    {"secondary/track", kThemeTokenSecondary, kThemeTokenTrack, false, 3.0f},
    {"tertiary/track", kThemeTokenTertiary, kThemeTokenTrack, false, 3.0f},
    {"error/track", kThemeTokenError, kThemeTokenTrack, false, 3.0f},
    {"error/surface", kThemeTokenError, kThemeTokenSurface, false, 3.0f},
    {"primary/background", kThemeTokenPrimary, kThemeTokenBackground, false,
     3.0f},
    {"error/background", kThemeTokenError, kThemeTokenBackground, false, 3.0f},
    {"dL(surface,background)", kThemeTokenSurface, kThemeTokenBackground, true,
     10.0f},
    {"dL(surface_high,surface)", kThemeTokenSurfaceHigh, kThemeTokenSurface,
     true, 8.0f},
};

const size_t kThemePairCount = sizeof(kThemePairs) / sizeof(kThemePairs[0]);

uint16_t ui_theme_token(const UiThemePalette* palette, UiThemeToken token) {
  switch (token) {
    case kThemeTokenBackground:
      return palette->background;
    case kThemeTokenSurface:
      return palette->surface;
    case kThemeTokenSurfaceHigh:
      return palette->surface_high;
    case kThemeTokenTrack:
      return palette->track;
    case kThemeTokenText:
      return palette->text;
    case kThemeTokenMuted:
      return palette->muted;
    case kThemeTokenPrimary:
      return palette->primary;
    case kThemeTokenSecondary:
      return palette->secondary;
    case kThemeTokenTertiary:
      return palette->tertiary;
    case kThemeTokenError:
      return palette->error;
    default:
      return palette->background;
  }
}
```

`ui_theme_get()` 照抄规格 §3.2 末尾的实现（越界返回 `&s_themes[kThemeM3Dark]`）。

- [ ] **步骤 6：跑测试确认通过**

运行：`cmake --build pico_freertos_firmware/tests/build -j --target test_ui_theme && \
pico_freertos_firmware/tests/build/test_ui_theme`

预期：`PASS: 6 套 × 17 项与 ΔL*(surface,track) 全部符合规格`

- [ ] **步骤 7：Commit**

```bash
git add pico_freertos_firmware/include/ui_theme_id.h \
        pico_freertos_firmware/include/ui_theme.h \
        pico_freertos_firmware/src/display/ui_theme.c \
        pico_freertos_firmware/tests/test_ui_theme.c \
        pico_freertos_firmware/tests/CMakeLists.txt
git commit -m "feat(theme): 增加六套主题调色板与对比度门禁测试"
```

---

### 任务 2：2/2 采样消抖

**文件：**
- 创建：`pico_freertos_firmware/include/button_filter.h`
- 创建：`pico_freertos_firmware/src/logic/button_filter.c`
- 测试：`pico_freertos_firmware/tests/test_button_filter.c`
- 修改：`pico_freertos_firmware/tests/CMakeLists.txt`

- [ ] **步骤 1：写失败测试**

创建 `pico_freertos_firmware/tests/test_button_filter.c`：

```c
/**
 * @file test_button_filter.c
 * @brief 验证 2/2 采样消抖：必须连续两拍同电平才改变状态。
 */

#include <stdio.h>
#include <stdlib.h>

#include "button_filter.h"

/** @brief 连续喂同一电平 n 拍。 */
static void feed(ButtonFilter* filter, bool low, unsigned times,
                 unsigned* clicks) {
  for (unsigned i = 0; i < times; ++i) {
    if (button_filter_update(filter, low)) {
      ++(*clicks);
    }
  }
}

int main(void) {
  ButtonFilter filter;
  unsigned clicks = 0;

  /* 抖动：低-高-低-高 不应产生任何单击。 */
  button_filter_reset(&filter);
  feed(&filter, true, 1, &clicks);
  feed(&filter, false, 1, &clicks);
  feed(&filter, true, 1, &clicks);
  feed(&filter, false, 1, &clicks);
  if (clicks != 0) {
    printf("FAIL: 抖动产生了 %u 次单击\n", clicks);
    return EXIT_FAILURE;
  }

  /* 稳定按下 2 拍 = 一次单击（在第 2 拍产生）。 */
  feed(&filter, true, 2, &clicks);
  if (clicks != 1) {
    printf("FAIL: 按下未产生单击（%u）\n", clicks);
    return EXIT_FAILURE;
  }

  /* 持续按住不再重复触发。 */
  feed(&filter, true, 10, &clicks);
  if (clicks != 1) {
    printf("FAIL: 按住重复触发（%u）\n", clicks);
    return EXIT_FAILURE;
  }

  /* 释放 2 拍后才能再次触发。 */
  feed(&filter, false, 1, &clicks);
  feed(&filter, true, 1, &clicks);
  if (clicks != 1) {
    printf("FAIL: 释放未满 2 拍就再次触发（%u）\n", clicks);
    return EXIT_FAILURE;
  }
  feed(&filter, false, 2, &clicks);
  feed(&filter, true, 2, &clicks);
  if (clicks != 2) {
    printf("FAIL: 第二次单击未触发（%u）\n", clicks);
    return EXIT_FAILURE;
  }

  /* 上电即按住：按规格 §4.1 有意接受为一次单击。 */
  clicks = 0;
  button_filter_reset(&filter);
  feed(&filter, true, 2, &clicks);
  if (clicks != 1) {
    printf("FAIL: 上电按住应产生 1 次单击（%u）\n", clicks);
    return EXIT_FAILURE;
  }

  printf("PASS: 消抖行为符合规格 §4.1\n");
  return EXIT_SUCCESS;
}
```

- [ ] **步骤 2：加 CMake 目标并确认失败**

```cmake
add_executable(test_button_filter
    test_button_filter.c
    ../src/logic/button_filter.c
)

add_test(NAME button_filter COMMAND test_button_filter)
```

运行 `cmake --build pico_freertos_firmware/tests/build -j --target test_button_filter`
预期：编译失败，`button_filter.h: No such file or directory`。

- [ ] **步骤 3：创建头文件与实现**

`include/button_filter.h`：

```c
#ifndef PICO_FREERTOS_FIRMWARE_INCLUDE_BUTTON_FILTER_H_
#define PICO_FREERTOS_FIRMWARE_INCLUDE_BUTTON_FILTER_H_

#include <stdbool.h>
#include <stdint.h>

/** @brief 2/2 采样消抖状态。 */
typedef struct {
  uint8_t low_samples;   /**< 连续低电平计数 */
  uint8_t high_samples;  /**< 连续高电平计数 */
  bool pressed;          /**< 是否处于已按下状态 */
} ButtonFilter;

/** @brief 复位到空闲态（IDLE）。 */
void button_filter_reset(ButtonFilter* filter);

/** @brief 喂一拍采样，返回 true 表示本拍产生一次有效单击。 */
bool button_filter_update(ButtonFilter* filter, bool level_low);

#endif /* PICO_FREERTOS_FIRMWARE_INCLUDE_BUTTON_FILTER_H_ */
```

`src/logic/button_filter.c`：

```c
#include "button_filter.h"

/** @brief 判定一次有效动作所需的连续采样拍数。 */
enum { kSamplesRequired = 2 };

void button_filter_reset(ButtonFilter* filter) {
  filter->low_samples = 0;
  filter->high_samples = 0;
  filter->pressed = false;
}

bool button_filter_update(ButtonFilter* filter, bool level_low) {
  if (level_low) {
    filter->high_samples = 0;
    if (filter->low_samples < kSamplesRequired) {
      ++filter->low_samples;
    }
    if (!filter->pressed && filter->low_samples >= kSamplesRequired) {
      filter->pressed = true;
      return true;
    }
    return false;
  }
  filter->low_samples = 0;
  if (filter->high_samples < kSamplesRequired) {
    ++filter->high_samples;
  }
  if (filter->pressed && filter->high_samples >= kSamplesRequired) {
    filter->pressed = false;
  }
  return false;
}
```

- [ ] **步骤 4：跑测试确认通过**

运行：`cmake --build pico_freertos_firmware/tests/build -j --target test_button_filter && \
pico_freertos_firmware/tests/build/test_button_filter`
预期：`PASS: 消抖行为符合规格 §4.1`

- [ ] **步骤 5：Commit**

```bash
git add pico_freertos_firmware/include/button_filter.h \
        pico_freertos_firmware/src/logic/button_filter.c \
        pico_freertos_firmware/tests/test_button_filter.c \
        pico_freertos_firmware/tests/CMakeLists.txt
git commit -m "feat(theme): 增加 2/2 采样消抖纯逻辑单元"
```

---

### 任务 3：12 字节配置的打包与解析

**文件：**
- 创建：`pico_freertos_firmware/include/theme_config.h`
- 创建：`pico_freertos_firmware/src/logic/theme_config.c`
- 测试：`pico_freertos_firmware/tests/test_theme_config.c`
- 修改：`pico_freertos_firmware/tests/CMakeLists.txt`

- [ ] **步骤 1：写失败测试**

创建 `test_theme_config.c`，覆盖规格 §8.2 的 N-1..N-8：

```c
/**
 * @file test_theme_config.c
 * @brief 12 字节配置的打包、解析与全部拒绝分支（规格 §8.2 N-1..N-8）。
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "theme_config.h"

/** @brief 一页擦除后的内容。 */
static void fill_erased(uint8_t* page, size_t size) {
  memset(page, 0xFF, size);
}

/** @brief 断言解析失败（必须回退，不给越界值）。 */
static bool expect_reject(const uint8_t* page, size_t size) {
  ThemeConfig out;
  return !theme_config_parse(page, size, &out);
}

int main(void) {
  uint8_t page[kThemeConfigPageSize];
  ThemeConfig cfg;
  ThemeConfig parsed;

  /* N-1 擦除态。 */
  fill_erased(page, sizeof(page));
  if (!expect_reject(page, sizeof(page))) {
    printf("FAIL: N-1 擦除态未被拒绝\n");
    return EXIT_FAILURE;
  }

  /* N-2 全 0x00。 */
  memset(page, 0x00, sizeof(page));
  if (!expect_reject(page, sizeof(page))) {
    printf("FAIL: N-2 全零未被拒绝\n");
    return EXIT_FAILURE;
  }

  /* 正常路径：打包后必须能原样解析回来。 */
  fill_erased(page, sizeof(page));
  theme_config_pack(kThemeCeladon, page, sizeof(page));
  if (!theme_config_parse(page, sizeof(page), &parsed)) {
    printf("FAIL: 合法配置未被接受\n");
    return EXIT_FAILURE;
  }
  if (parsed.theme_id != kThemeCeladon) {
    printf("FAIL: theme_id 解析错位（%u）\n", (unsigned)parsed.theme_id);
    return EXIT_FAILURE;
  }

  /* N-3 theme_id 越界。 */
  theme_config_pack(kThemeCeladon, page, sizeof(page));
  page[kThemeConfigThemeIdOffset] = 200;
  if (!expect_reject(page, sizeof(page))) {
    printf("FAIL: N-3 越界 theme_id 未被拒绝\n");
    return EXIT_FAILURE;
  }

  /* N-4 checksum 错 1 位。 */
  theme_config_pack(kThemePhosphor, page, sizeof(page));
  page[kThemeConfigChecksumOffset] ^= 0x01u;
  if (!expect_reject(page, sizeof(page))) {
    printf("FAIL: N-4 错误校验和未被拒绝\n");
    return EXIT_FAILURE;
  }

  /* N-5 版本不符。 */
  theme_config_pack(kThemeDeepTeal, page, sizeof(page));
  page[kThemeConfigVersionOffset] = 2;
  if (!expect_reject(page, sizeof(page))) {
    printf("FAIL: N-5 版本不符未被拒绝\n");
    return EXIT_FAILURE;
  }

  /* N-6 reserved 非零。 */
  theme_config_pack(kThemeNeonMagenta, page, sizeof(page));
  page[kThemeConfigReservedOffset] = 1;
  if (!expect_reject(page, sizeof(page))) {
    printf("FAIL: N-6 保留字段非零未被拒绝\n");
    return EXIT_FAILURE;
  }

  /* N-7 整页 0xFF（擦除完成、编程未开始）。 */
  fill_erased(page, sizeof(page));
  if (!expect_reject(page, sizeof(page))) {
    printf("FAIL: N-7 整页 0xFF 未被拒绝\n");
    return EXIT_FAILURE;
  }

  /* N-8 前 4 字节已写（编程中途）。 */
  fill_erased(page, sizeof(page));
  memcpy(page, &(const uint32_t){kThemeConfigMagic}, sizeof(uint32_t));
  if (!expect_reject(page, sizeof(page))) {
    printf("FAIL: N-8 半写页未被拒绝\n");
    return EXIT_FAILURE;
  }

  /* 缓冲区不足时必须拒绝，而不是越界读。 */
  theme_config_pack(kThemeMonoRed, page, sizeof(page));
  if (!expect_reject(page, kThemeConfigSize - 1u)) {
    printf("FAIL: 短缓冲区未被拒绝\n");
    return EXIT_FAILURE;
  }

  /* 打包必须把 12 字节之后的区域填 0xFF。 */
  memset(&cfg, 0x5A, sizeof(cfg));
  theme_config_pack(kThemeM3Dark, page, sizeof(page));
  for (size_t i = kThemeConfigSize; i < sizeof(page); ++i) {
    if (page[i] != 0xFFu) {
      printf("FAIL: 页填充不是 0xFF（偏移 %zu）\n", i);
      return EXIT_FAILURE;
    }
  }

  printf("PASS: N-1..N-8 与页填充全部符合规格\n");
  return EXIT_SUCCESS;
}
```

- [ ] **步骤 2：加 CMake 目标并确认失败**

```cmake
add_executable(test_theme_config
    test_theme_config.c
    ../src/logic/theme_config.c
)

add_test(NAME theme_config COMMAND test_theme_config)
```

运行 `cmake --build pico_freertos_firmware/tests/build -j --target test_theme_config`
预期：编译失败，`theme_config.h: No such file or directory`。

- [ ] **步骤 3：创建头文件**

```c
#ifndef PICO_FREERTOS_FIRMWARE_INCLUDE_THEME_CONFIG_H_
#define PICO_FREERTOS_FIRMWARE_INCLUDE_THEME_CONFIG_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ui_theme_id.h"

/** @brief 配置在页内的字节数。 */
enum { kThemeConfigSize = 12 };

/** @brief 一页的大小，与 FLASH_PAGE_SIZE 相等（此处不复用 SDK 宏）。 */
enum { kThemeConfigPageSize = 256 };

/** @brief 小端落盘为 "THME"。 */
enum { kThemeConfigMagic = 0x454D4854 };

/** @brief 布局版本；改动字段顺序或含义必须加 1。 */
enum { kThemeConfigVersion = 1 };

/** @brief 字段偏移，与 §5.2 的 _Static_assert 对应。 */
enum {
  kThemeConfigMagicOffset = 0,
  kThemeConfigThemeIdOffset = 4,
  kThemeConfigVersionOffset = 5,
  kThemeConfigReservedOffset = 6,
  kThemeConfigChecksumOffset = 8,
};

/** @brief 解析结果（不含填充与偏移）。 */
typedef struct {
  UiThemeId theme_id; /**< 已校验的合法主题 id */
} ThemeConfig;

/**
 * @brief 把主题写成一页：前 12 字节是配置，其余全省 0xFF。
 * @param theme_id 目标主题；越界时按 kThemeM3Dark 落盘。
 * @param page     输出缓冲区，长度至少 size。
 * @param size     缓冲区长度；小于 kThemeConfigPageSize 时直接返回。
 */
void theme_config_pack(UiThemeId theme_id, uint8_t* page, size_t size);

/**
 * @brief 从一页里解析配置。
 * @return true 表示魔数、版本、保留字段、主题范围、校验和全部通过。
 */
bool theme_config_parse(const uint8_t* page, size_t size, ThemeConfig* out);

#endif /* PICO_FREERTOS_FIRMWARE_INCLUDE_THEME_CONFIG_H_ */
```

- [ ] **步骤 4：创建实现**

`src/logic/theme_config.c`：

```c
#include "theme_config.h"

#include <string.h>

/** @brief 按位拼接写小端。 */
static void put_u32(uint8_t* page, size_t offset, uint32_t value) {
  page[offset + 0] = (uint8_t)(value & 0xFFu);
  page[offset + 1] = (uint8_t)((value >> 8) & 0xFFu);
  page[offset + 2] = (uint8_t)((value >> 16) & 0xFFu);
  page[offset + 3] = (uint8_t)((value >> 24) & 0xFFu);
}

/** @brief 按位拼接读小端。 */
static uint32_t get_u32(const uint8_t* page, size_t offset) {
  return (uint32_t)page[offset + 0] | ((uint32_t)page[offset + 1] << 8) |
         ((uint32_t)page[offset + 2] << 16) |
         ((uint32_t)page[offset + 3] << 24);
}

/** @brief 校验和：魔数、主题、版本三者异或。 */
static uint32_t checksum_of(uint32_t magic, uint8_t theme_id,
                            uint8_t version) {
  return magic ^ ((uint32_t)theme_id << 8) ^ (uint32_t)version;
}

void theme_config_pack(UiThemeId theme_id, uint8_t* page, size_t size) {
  if (size < (size_t)kThemeConfigPageSize) {
    return;
  }
  const bool valid = (unsigned)theme_id < (unsigned)kThemeCount;
  const uint8_t id = (uint8_t)(valid ? (unsigned)theme_id : 0u);
  memset(page, 0xFF, size);
  put_u32(page, kThemeConfigMagicOffset, (uint32_t)kThemeConfigMagic);
  page[kThemeConfigThemeIdOffset] = id;
  page[kThemeConfigVersionOffset] = (uint8_t)kThemeConfigVersion;
  page[kThemeConfigReservedOffset] = 0;
  page[kThemeConfigReservedOffset + 1] = 0;
  put_u32(page, kThemeConfigChecksumOffset,
          checksum_of((uint32_t)kThemeConfigMagic, id,
                      (uint8_t)kThemeConfigVersion));
}

bool theme_config_parse(const uint8_t* page, size_t size, ThemeConfig* out) {
  if (size < (size_t)kThemeConfigSize) {
    return false;
  }
  const uint32_t magic = get_u32(page, kThemeConfigMagicOffset);
  const uint8_t id = page[kThemeConfigThemeIdOffset];
  const uint8_t version = page[kThemeConfigVersionOffset];
  const uint32_t checksum = get_u32(page, kThemeConfigChecksumOffset);
  if (magic != (uint32_t)kThemeConfigMagic) {
    return false;
  }
  if (version != (uint8_t)kThemeConfigVersion) {
    return false;
  }
  if (page[kThemeConfigReservedOffset] != 0 ||
      page[kThemeConfigReservedOffset + 1] != 0) {
    return false;
  }
  if ((unsigned)id >= (unsigned)kThemeCount) {
    return false;
  }
  if (checksum != checksum_of(magic, id, version)) {
    return false;
  }
  out->theme_id = (UiThemeId)id;
  return true;
}
```

- [ ] **步骤 5：跑测试确认通过**

运行：`cmake --build pico_freertos_firmware/tests/build -j --target test_theme_config && \
pico_freertos_firmware/tests/build/test_theme_config`
预期：`PASS: N-1..N-8 与页填充全部符合规格`

- [ ] **步骤 6：Commit**

```bash
git add pico_freertos_firmware/include/theme_config.h \
        pico_freertos_firmware/src/logic/theme_config.c \
        pico_freertos_firmware/tests/test_theme_config.c \
        pico_freertos_firmware/tests/CMakeLists.txt
git commit -m "feat(theme): 增加 12 字节配置的打包解析与校验"
```

---

### 任务 4：绕回、渲染窗口与提交状态机

**文件：**
- 创建：`pico_freertos_firmware/include/theme_switch.h`、`src/logic/theme_switch.c`
- 创建：`pico_freertos_firmware/include/render_window.h`、`src/logic/render_window.c`
- 创建：`pico_freertos_firmware/include/theme_commit_policy.h`、`src/logic/theme_commit_policy.c`
- 测试：`pico_freertos_firmware/tests/test_theme_commit_policy.c`
- 修改：`pico_freertos_firmware/tests/CMakeLists.txt`

- [ ] **步骤 1：写失败测试**

创建 `test_theme_commit_policy.c`：

```c
/**
 * @file test_theme_commit_policy.c
 * @brief 覆盖绕回、5 s 去抖、零擦写短路、Verify 只重试一次、
 *        Failed 之后不自动重入、渲染窗口的有界等待。
 */

#include <stdio.h>
#include <stdlib.h>

#include "render_window.h"
#include "theme_commit_policy.h"
#include "theme_switch.h"

/** @brief 断言宏：失败时打印行号并退出。 */
#define CHECK(cond)                                        \
  do {                                                     \
    if (!(cond)) {                                         \
      printf("FAIL: %s（第 %d 行）\n", #cond, __LINE__);   \
      return EXIT_FAILURE;                                 \
    }                                                      \
  } while (0)

int main(void) {
  /* 绕回：5 → 0。 */
  CHECK(theme_switch_advance(kThemeNeonMagenta, true) == kThemeM3Dark);
  CHECK(theme_switch_advance(kThemeM3Dark, true) == kThemeMonoRed);
  CHECK(theme_switch_advance(kThemeCeladon, false) == kThemeCeladon);

  /* 渲染窗口：进入后非空闲，离开后空闲。 */
  render_window_leave();
  CHECK(render_window_is_idle());
  render_window_enter();
  CHECK(!render_window_is_idle());
  render_window_leave();
  CHECK(render_window_is_idle());

  /* 状态机：切换后要等满 5 s 才动。 */
  ThemeCommitPolicy policy;
  theme_commit_policy_init(&policy, kThemeM3Dark);
  CHECK(theme_commit_policy_tick(&policy, kThemeMonoRed, 1000, false) ==
        kThemeCommitActionNone);
  CHECK(theme_commit_policy_state(&policy) == kThemeCommitPending);
  CHECK(theme_commit_policy_tick(&policy, kThemeMonoRed, 5999, false) ==
        kThemeCommitActionNone);
  CHECK(theme_commit_policy_tick(&policy, kThemeMonoRed, 6000, false) ==
        kThemeCommitActionWrite);

  /* 零擦写短路：XIP 里的配置已经是目标值。 */
  theme_commit_policy_init(&policy, kThemeM3Dark);
  CHECK(theme_commit_policy_tick(&policy, kThemePhosphor, 0, false) ==
        kThemeCommitActionNone);
  CHECK(theme_commit_policy_tick(&policy, kThemePhosphor, 5000, true) ==
        kThemeCommitActionShortCircuit);
  CHECK(theme_commit_policy_state(&policy) == kThemeCommitDone);
  CHECK(theme_commit_policy_tick(&policy, kThemePhosphor, 9000, true) ==
        kThemeCommitActionNone);

  /* 成功路径：写入后转 Done，且不再重复写。 */
  theme_commit_policy_init(&policy, kThemeM3Dark);
  (void)theme_commit_policy_tick(&policy, kThemeCeladon, 0, false);
  CHECK(theme_commit_policy_tick(&policy, kThemeCeladon, 5000, false) ==
        kThemeCommitActionWrite);
  theme_commit_policy_result(&policy, kThemeCommitOutcomeOk);
  CHECK(theme_commit_policy_state(&policy) == kThemeCommitDone);
  CHECK(theme_commit_policy_committed(&policy) == kThemeCeladon);
  CHECK(theme_commit_policy_tick(&policy, kThemeCeladon, 20000, false) ==
        kThemeCommitActionNone);

  /* Verify 只重试一次；第二次仍 Verify 必须停在 Failed。 */
  theme_commit_policy_init(&policy, kThemeM3Dark);
  (void)theme_commit_policy_tick(&policy, kThemeDeepTeal, 0, false);
  CHECK(theme_commit_policy_tick(&policy, kThemeDeepTeal, 5000, false) ==
        kThemeCommitActionWrite);
  theme_commit_policy_result(&policy, kThemeCommitOutcomeVerify);
  CHECK(theme_commit_policy_state(&policy) == kThemeCommitWriting);
  theme_commit_policy_result(&policy, kThemeCommitOutcomeVerify);
  CHECK(theme_commit_policy_state(&policy) == kThemeCommitFailed);

  /* 终止性：Failed 之后即使截止时刻已过也不得被拉回 Writing。 */
  for (unsigned i = 0; i < 10; ++i) {
    CHECK(theme_commit_policy_tick(&policy, kThemeDeepTeal, 20000u + i * 100u,
                                   false) == kThemeCommitActionNone);
    CHECK(theme_commit_policy_state(&policy) == kThemeCommitFailed);
  }

  /* 只有快照变化才能离开 Failed。 */
  CHECK(theme_commit_policy_tick(&policy, kThemeNeonMagenta, 30000, false) ==
        kThemeCommitActionNone);
  CHECK(theme_commit_policy_state(&policy) == kThemeCommitPending);

  /* 分配型失败一律不重试。 */
  theme_commit_policy_init(&policy, kThemeM3Dark);
  (void)theme_commit_policy_tick(&policy, kThemeMonoRed, 0, false);
  (void)theme_commit_policy_tick(&policy, kThemeMonoRed, 5000, false);
  theme_commit_policy_result(&policy, kThemeCommitOutcomeNoHeap);
  CHECK(theme_commit_policy_state(&policy) == kThemeCommitFailed);

  printf("PASS: 状态机、绕回与渲染窗口符合规格 §5.4.2\n");
  return EXIT_SUCCESS;
}
```

- [ ] **步骤 2：加 CMake 目标并确认失败**

```cmake
add_executable(test_theme_commit_policy
    test_theme_commit_policy.c
    ../src/logic/theme_commit_policy.c
    ../src/logic/theme_switch.c
    ../src/logic/render_window.c
)

add_test(NAME theme_commit_policy COMMAND test_theme_commit_policy)
```

运行 `cmake --build ... --target test_theme_commit_policy`
预期：编译失败，`render_window.h: No such file or directory`。

- [ ] **步骤 3：实现 `theme_switch` 与 `render_window`**

`include/theme_switch.h`：

```c
#ifndef PICO_FREERTOS_FIRMWARE_INCLUDE_THEME_SWITCH_H_
#define PICO_FREERTOS_FIRMWARE_INCLUDE_THEME_SWITCH_H_

#include <stdbool.h>

#include "ui_theme_id.h"

/** @brief 有单击则前进一套并绕回；无单击原样返回。 */
UiThemeId theme_switch_advance(UiThemeId current, bool clicked);

#endif /* PICO_FREERTOS_FIRMWARE_INCLUDE_THEME_SWITCH_H_ */
```

`src/logic/theme_switch.c`：

```c
#include "theme_switch.h"

UiThemeId theme_switch_advance(UiThemeId current, bool clicked) {
  if (!clicked) {
    return current;
  }
  const unsigned id = (unsigned)current;
  if (id >= (unsigned)kThemeCount) {
    return kThemeM3Dark;
  }
  return (UiThemeId)((id + 1u) % (unsigned)kThemeCount);
}
```

`include/render_window.h`：

```c
#ifndef PICO_FREERTOS_FIRMWARE_INCLUDE_RENDER_WINDOW_H_
#define PICO_FREERTOS_FIRMWARE_INCLUDE_RENDER_WINDOW_H_

#include <stdbool.h>

/** @brief 标记渲染开始；设备侧实现带一次 __dmb()。 */
void render_window_enter(void);

/** @brief 标记渲染结束；设备侧实现带一次 __dmb()。 */
void render_window_leave(void);

/** @brief 渲染是否空闲（可以安全进入 Flash 写入窗口）。 */
bool render_window_is_idle(void);

#endif /* PICO_FREERTOS_FIRMWARE_INCLUDE_RENDER_WINDOW_H_ */
```

`src/logic/render_window.c`：

```c
#include "render_window.h"

#include <stdatomic.h>

/** @brief 渲染任务正在刷屏时置 true。 */
static atomic_bool s_busy;

void render_window_enter(void) {
  atomic_store_explicit(&s_busy, true, memory_order_seq_cst);
}

void render_window_leave(void) {
  atomic_store_explicit(&s_busy, false, memory_order_seq_cst);
}

bool render_window_is_idle(void) {
  return !atomic_load_explicit(&s_busy, memory_order_seq_cst);
}
```

**为什么用 `<stdatomic.h>` 而不是 `__dmb()`**：规格 §6.2 的断言 3 禁止
`src/logic/` include 任何 `pico/` 头，而 `__dmb()` 来自 `pico/platform.h`（CMSIS）。
`memory_order_seq_cst` 在 Cortex-M0+ 上由 GCC 生成 `DMB` 指令，在宿主上退化为普通
原子操作，语义与 `__dmb()` 等价且不引入 SDK 依赖。规格 §6.1 的目录树漏登记了
`src/logic/render_window.c` 这一行，本计划补上——`render_window.h` 的注释里
「设备侧实现带一次 `__dmb()`」同步改为「带一次全屏障」。

- [ ] **步骤 4：实现提交策略**

`include/theme_commit_policy.h`：

```c
#ifndef PICO_FREERTOS_FIRMWARE_INCLUDE_THEME_COMMIT_POLICY_H_
#define PICO_FREERTOS_FIRMWARE_INCLUDE_THEME_COMMIT_POLICY_H_

#include <stdbool.h>
#include <stdint.h>

#include "ui_theme_id.h"

/** @brief 提交状态机（规格 §5.4.2）。 */
typedef enum {
  kThemeCommitIdle = 0, /**< 无待提交变更 */
  kThemeCommitPending,  /**< 已切换，等 5 s 去抖 */
  kThemeCommitWriting,  /**< 正在 flash_safe_execute */
  kThemeCommitDone,     /**< 已落盘且回读一致 */
  kThemeCommitFailed,   /**< 放弃本次提交，保留内存态 */
} ThemeCommitState;

/** @brief 策略层认识的结果；由任务层从 ThemeStorageResult 映射而来。 */
typedef enum {
  kThemeCommitOutcomeOk = 0, /**< 成功 */
  kThemeCommitOutcomeVerify, /**< 回读不一致，可再试一次 */
  kThemeCommitOutcomeNoHeap, /**< 堆预检未过，不重试 */
  kThemeCommitOutcomeNoResources, /**< 建避难任务失败，不重试 */
  kThemeCommitOutcomeTimeout,     /**< 握手超时，不重试 */
  kThemeCommitOutcomeFatal,       /**< 其余错误 */
} ThemeCommitOutcome;

/** @brief 策略要做的一件事。 */
typedef enum {
  kThemeCommitActionNone = 0,     /**< 什么也不做 */
  kThemeCommitActionShortCircuit, /**< 当前快照已等于已保存值 */
  kThemeCommitActionWrite,        /**< 调 theme_storage_commit(seen) */
} ThemeCommitAction;

/** @brief 提交策略的全部状态。 */
typedef struct {
  ThemeCommitState state;
  UiThemeId seen;       /**< 最近一次看到的主题 */
  UiThemeId committed;  /**< 已确认落盘的主题 */
  UiThemeId writing;    /**< 正在写的主题，重试复用 */
  uint32_t deadline_ms; /**< 去抖截止时刻 */
  bool verify_retried;  /**< 本次提交是否已用掉那 1 次重试 */
} ThemeCommitPolicy;

/** @brief 初始化；committed 取开机读回值。 */
void theme_commit_policy_init(ThemeCommitPolicy* policy, UiThemeId committed);

/**
 * @brief 推进一拍。
 * @param current       当前快照里的主题
 * @param now_ms        单调毫秒
 * @param saved_matches XIP 里的配置是否已等于 current
 */
ThemeCommitAction theme_commit_policy_tick(ThemeCommitPolicy* policy,
                                           UiThemeId current, uint32_t now_ms,
                                           bool saved_matches);

/** @brief 喂回一次写入的结果。 */
void theme_commit_policy_result(ThemeCommitPolicy* policy,
                                ThemeCommitOutcome outcome);

/** @brief 读当前状态（测试与日志用）。 */
ThemeCommitState theme_commit_policy_state(const ThemeCommitPolicy* policy);

/** @brief 读已落盘主题（测试与日志用）。 */
UiThemeId theme_commit_policy_committed(const ThemeCommitPolicy* policy);

#endif /* PICO_FREERTOS_FIRMWARE_INCLUDE_THEME_COMMIT_POLICY_H_ */
```

`src/logic/theme_commit_policy.c`：

```c
#include "theme_commit_policy.h"

/** @brief 5 s 去抖窗口。 */
enum { kDebounceMs = 5000 };

void theme_commit_policy_init(ThemeCommitPolicy* policy, UiThemeId committed) {
  policy->state = kThemeCommitIdle;
  policy->seen = committed;
  policy->committed = committed;
  policy->writing = committed;
  policy->deadline_ms = 0;
  policy->verify_retried = false;
}

ThemeCommitAction theme_commit_policy_tick(ThemeCommitPolicy* policy,
                                           UiThemeId current, uint32_t now_ms,
                                           bool saved_matches) {
  if (policy->state != kThemeCommitWriting &&
      (UiThemeId)policy->seen != current) {
    policy->seen = current;
    policy->deadline_ms = now_ms + (uint32_t)kDebounceMs;
    policy->state = kThemeCommitPending;
    policy->verify_retried = false;
    return kThemeCommitActionNone;
  }
  if (policy->state != kThemeCommitPending) {
    return kThemeCommitActionNone;
  }
  if ((int32_t)(now_ms - policy->deadline_ms) < 0) {
    return kThemeCommitActionNone;
  }
  if ((UiThemeId)policy->seen == policy->committed) {
    policy->state = kThemeCommitDone;
    return kThemeCommitActionShortCircuit;
  }
  if (saved_matches) {
    policy->committed = policy->seen;
    policy->state = kThemeCommitDone;
    return kThemeCommitActionShortCircuit;
  }
  policy->writing = policy->seen;
  policy->state = kThemeCommitWriting;
  return kThemeCommitActionWrite;
}

void theme_commit_policy_result(ThemeCommitPolicy* policy,
                                ThemeCommitOutcome outcome) {
  if (policy->state != kThemeCommitWriting) {
    return;
  }
  if (outcome == kThemeCommitOutcomeOk) {
    policy->committed = policy->writing;
    policy->state = kThemeCommitDone;
    return;
  }
  if (outcome == kThemeCommitOutcomeVerify && !policy->verify_retried) {
    policy->verify_retried = true;
    return; /* 留在 Writing，由任务层等 1 s 再调一次 */
  }
  policy->state = kThemeCommitFailed;
}

ThemeCommitState theme_commit_policy_state(const ThemeCommitPolicy* policy) {
  return policy->state;
}

UiThemeId theme_commit_policy_committed(const ThemeCommitPolicy* policy) {
  return policy->committed;
}
```

- [ ] **步骤 5：跑测试确认通过**

运行：`cmake --build pico_freertos_firmware/tests/build -j --target test_theme_commit_policy && \
pico_freertos_firmware/tests/build/test_theme_commit_policy`
预期：`PASS: 状态机、绕回与渲染窗口符合规格 §5.4.2`

- [ ] **步骤 6：Commit**

```bash
git add pico_freertos_firmware/include/theme_switch.h \
        pico_freertos_firmware/include/render_window.h \
        pico_freertos_firmware/include/theme_commit_policy.h \
        pico_freertos_firmware/src/logic/theme_switch.c \
        pico_freertos_firmware/src/logic/render_window.c \
        pico_freertos_firmware/src/logic/theme_commit_policy.c \
        pico_freertos_firmware/tests/test_theme_commit_policy.c \
        pico_freertos_firmware/tests/CMakeLists.txt
git commit -m "feat(theme): 增加主题推进、渲染窗口与提交状态机"
```

---

### 任务 5：状态镜像、脏区与调色板落地到渲染

**文件：**
- 修改：`pico_freertos_firmware/include/system_state.h`、`src/state/system_state.c`
- 修改：`pico_freertos_firmware/src/display/ui_layout.c`
- 修改：`pico_freertos_firmware/src/display/ui_render.c`
- 修改：`pico_freertos_firmware/tests/test_ui_render.c`
- 修改：`pico_freertos_firmware/tests/CMakeLists.txt`

- [ ] **步骤 1：给快照加字段与 setter**

`include/system_state.h`：加 `#include "ui_theme_id.h"`，在 `ui_mode` 之后插入
`UiThemeId theme_id;`，并声明：

```c
/** @brief 写入当前主题；越界值被夹紧，写入受既有互斥锁保护。 */
void system_state_set_theme(UiThemeId theme_id);
```

`src/state/system_state.c`：`system_state_init()` 里把 `theme_id` 置为 `kThemeM3Dark`，
并实现：

```c
void system_state_set_theme(UiThemeId theme_id) {
  const UiThemeId clamped =
      ((unsigned)theme_id < (unsigned)kThemeCount) ? theme_id : kThemeM3Dark;
  STATE_LOCK();
  s_snapshot.theme_id = clamped;
  STATE_UNLOCK();
}
```

- [ ] **步骤 2：主题变化 → 全屏脏矩形**

在 `ui_layout.c` 的 `ui_layout_calculate_dirty_regions()` 里，紧跟 `ui_mode` 比较之后插入：

```c
  if (prev->theme_id != curr->theme_id) {
    push_rect(out_rects, max_rects, &count, 0, 0, kUiScreenWidth,
              kUiScreenHeight);
    return count;
  }
```

（`push_rect` 为该文件已有的静态函数，签名以实际代码为准。）

- [ ] **步骤 3：`ui_render.c` 改用调色板**

**先删枚举、再替换**——顺序反了会让 `sed` 把枚举里的定义行也改坏。

```bash
cd pico_freertos_firmware/src/display
sed -i \
  -e 's/kColorBackground/s_palette->background/g' \
  -e 's/kColorSurfaceHigh/s_palette->surface_high/g' \
  -e 's/kColorSurface/s_palette->surface/g' \
  -e 's/kColorTrack/s_palette->track/g' \
  -e 's/kColorText/s_palette->text/g' \
  -e 's/kColorMuted/s_palette->muted/g' \
  -e 's/kColorPrimary/s_palette->primary/g' \
  -e 's/kColorSecondary/s_palette->secondary/g' \
  -e 's/kColorTertiary/s_palette->tertiary/g' \
  -e 's/kColorError/s_palette->error/g' ui_render.c
```

注意 `kColorSurfaceHigh` 与 `kColorSurface` 的替换顺序不能颠倒（先长后短）。

然后：

1. 删除文件顶部 20–32 行的 `kColor*` 枚举（**必须在 `sed` 之前完成**），换成
   `#include "ui_theme.h"` 与文件静态指针：

```c
/** @brief 本帧使用的调色板；每次绘制入口由 ui_theme_get() 赋值。 */
static const UiThemePalette* s_palette;
```

2. `ui_render_draw_frame()` 与 `ui_render_draw_dirty()` 的第一条语句插入：

```c
  s_palette = ui_theme_get(snapshot->theme_id);
```

3. 顶栏时刻：`render_top_bar(uint16_t* pixels, int origin_y, const SystemSnapshot*
   snapshot)` 就是现有签名（`ui_render.c:205-219`），**不需要改形参**；在两枚图标之后追加：

```c
  const int kClockTextX = 32;
  char text[9];
  if (!snapshot->clock.time_synced) {
    snprintf(text, sizeof(text), "--:--:--");
    font_engine_draw_text(surface, kClockTextX, kTopBarIconY, text,
                          s_palette->muted, s_palette->background);
    return;
  }
  snprintf(text, sizeof(text), "%02u:%02u:%02u", (unsigned)snapshot->clock.hour,
           (unsigned)snapshot->clock.minute, (unsigned)snapshot->clock.second);
  font_engine_draw_text(surface, kClockTextX, kTopBarIconY, text,
                        s_palette->text, s_palette->background);
```

（`surface` 是该函数里已有的 `FontSurface` 局部变量；`snprintf` 既有代码已在用。）

4. `ui_render_collect_dirty_regions()`：

   - 开头如果 `count >= 1` 且第一个矩形覆盖全屏，直接 `return count;`；
   - 在追加顶栏矩形之前，比较秒与 `time_synced`：

```c
  if (prev->clock.second != curr->clock.second ||
      prev->clock.time_synced != curr->clock.time_synced) {
    push_rect(out_rects, max_rects, &count, 32, 1, 64, 14);
  }
```

5. 渲染窗口：`ui_render_draw_dirty()` 调用前 `render_window_enter()`，返回后
   `render_window_leave()`（`#include "render_window.h"`）。

- [ ] **步骤 4：测试常量同步改值**

`tests/test_ui_render.c:16-25` 里：

- `kTestTrack` 改为 `0x0020`
- `kTestTertiary` 改为 `0xFE00`
- 其余 8 个常量不动

并在两个测试目标的源文件列表里补 `../src/display/ui_theme.c`：

```cmake
add_executable(test_ui_render
    test_ui_render.c
    ../src/display/st7735s_driver.c
    ../src/display/ui_render.c
    ../src/display/ui_theme.c        # 新增
    ...
```

`test_pipeline_e2e` 同样补一行。

- [ ] **步骤 5：跑全部宿主测试**

运行：`cmake --build pico_freertos_firmware/tests/build -j && \
ctest --test-dir pico_freertos_firmware/tests/build --output-on-failure`
预期：13 套全部通过（此时新增的 4 套也在跑，共 17 套）。

- [ ] **步骤 6：Commit**

```bash
git add pico_freertos_firmware/include/system_state.h \
        pico_freertos_firmware/src/state/system_state.c \
        pico_freertos_firmware/src/display/ui_layout.c \
        pico_freertos_firmware/src/display/ui_render.c \
        pico_freertos_firmware/tests/test_ui_render.c \
        pico_freertos_firmware/tests/CMakeLists.txt
git commit -m "feat(display): 渲染层改用主题调色板并加顶栏时刻"
```

---

### 任务 6：Flash 存储与镜像边界

**文件：**
- 创建：`pico_freertos_firmware/include/theme_storage.h`
- 创建：`pico_freertos_firmware/src/platform/theme_storage.c`
- 创建：`pico_freertos_firmware/linker/section_extra_post_end.incl`
- 修改：`pico_freertos_firmware/CMakeLists.txt`

- [ ] **步骤 1：补链接库与链接脚本覆盖**

`CMakeLists.txt` 的 `target_link_libraries` 里，`hardware_watchdog` 之后插入：

```cmake
    hardware_flash            # flash_range_erase / flash_range_program
    pico_flash                # flash_safe_execute
```

并在 `pico_add_subdirectory`/`target_sources` 之后加：

```cmake
pico_add_linker_script_override_path(pico_status_screen
    ${CMAKE_CURRENT_LIST_DIR}/linker
    FILES section_extra_post_end.incl)
```

- [ ] **步骤 2：创建链接期断言**

`linker/section_extra_post_end.incl`：

```ld
/* 主题配置扇区在 0x101FF000；镜像末尾不得越过它。 */
ASSERT(__flash_binary_end <= 0x101FF000,
       "firmware image overlaps theme storage sector")
```

- [ ] **步骤 3：创建 `include/theme_storage.h`**

```c
#ifndef PICO_FREERTOS_FIRMWARE_INCLUDE_THEME_STORAGE_H_
#define PICO_FREERTOS_FIRMWARE_INCLUDE_THEME_STORAGE_H_

#include "ui_theme_id.h"

/** @brief 提交结果；每个码只有唯一生产者（规格 §5.4.1）。 */
typedef enum {
  kThemeStorageOk = 0,
  kThemeStorageErrEnterTimeout,
  kThemeStorageErrExitTimeout,
  kThemeStorageErrVerify,
  kThemeStorageErrInvalidArg,
  kThemeStorageErrNoHeap,
  kThemeStorageErrNoResources,
} ThemeStorageResult;

/**
 * @brief 校验镜像边界并读出已保存主题。
 * @return 有效主题 id；扇区空白/损坏/版本不符时返回 kThemeM3Dark。
 */
UiThemeId theme_storage_init(void);

/**
 * @brief 同步提交主题，阻塞直到写完或失败（预算 403 ms，上界 650 ms）。
 * @param theme_id 目标主题；越界属调用方违约，先 configASSERT。
 */
ThemeStorageResult theme_storage_commit(UiThemeId theme_id);

#endif /* PICO_FREERTOS_FIRMWARE_INCLUDE_THEME_STORAGE_H_ */
```

- [ ] **步骤 4：创建 `src/platform/theme_storage.c`**

结构（按规格 §5.2–§5.4 逐条实现）：

```c
#include "theme_storage.h"

#include <string.h>

#include "FreeRTOS.h"
#include "hardware/flash.h"
#include "pico/flash.h"
#include "pico/stdlib.h"
#include "task.h"

#include "theme_config.h"

/** @brief 配置扇区在 Flash 内的偏移（最后一个扇区）。 */
#define kThemeFlashOffset (PICO_FLASH_SIZE_BYTES - FLASH_SECTOR_SIZE)

/** @brief 一页的静态缓冲；必须在 safe zone 之外填好。 */
static uint8_t s_flash_page[kThemeConfigPageSize];

/** @brief 回调是否真的执行过，用于区分 enter 与 exit 超时。 */
static volatile bool s_callback_ran;

/** @brief 只做擦写，不碰任何需要取指或加锁的东西。 */
static void __not_in_flash_func(theme_flash_commit)(void* unused) {
  (void)unused;
  s_callback_ran = true;
  flash_range_erase(kThemeFlashOffset, FLASH_SECTOR_SIZE);
  flash_range_program(kThemeFlashOffset, s_flash_page, kThemeConfigPageSize);
}
```

`theme_storage_commit()` 的流程：

1. `configASSERT((unsigned)theme_id < (unsigned)kThemeCount);`
2. `xPortGetFreeHeapSize() < 1536` → 返回 `kThemeStorageErrNoHeap`；
3. `theme_config_pack(theme_id, s_flash_page, sizeof(s_flash_page));`
4. `s_callback_ran = false;`
5. `rc = flash_safe_execute(theme_flash_commit, NULL, 100);`
6. `rc == 0` → 回读比对 → 一致 `kThemeStorageOk`，否则 `kThemeStorageErrVerify`；
7. `rc == PICO_ERROR_TIMEOUT` → `s_callback_ran ? 先回读、一致按 Ok : kThemeStorageErrEnterTimeout`，
   回读不一致返回 `kThemeStorageErrExitTimeout`；
8. `rc == PICO_ERROR_INSUFFICIENT_RESOURCES` → `kThemeStorageErrNoResources`；
9. 其余 → `kThemeStorageErrFatal`（并入 `kThemeStorageErrVerify` 之外的兜底值，
   实际写法为返回 `kThemeStorageErrNoResources` 以外的最大枚举，按规格 §5.4.3 归入 `Failed`）。

`theme_storage_init()`：

1. `extern char __flash_binary_end;` 与 `XIP_BASE + kThemeFlashOffset` 比较，越界 `panic("镜像越界")`；
2. 用 `memcpy` 从 `(const void*)(XIP_BASE + kThemeFlashOffset)` 读 12 字节到局部数组；
3. `theme_config_parse()` 成功返回 `cfg.theme_id`，否则 `kThemeM3Dark`。

- [ ] **步骤 5：编译固件确认符号齐全**

运行：`( cd pico_freertos_firmware && cmake --build build -j )`
预期：链接成功，无 `undefined reference to flash_range_erase` 等错误。

- [ ] **步骤 6：Commit**

```bash
git add pico_freertos_firmware/include/theme_storage.h \
        pico_freertos_firmware/src/platform/theme_storage.c \
        pico_freertos_firmware/linker/section_extra_post_end.incl \
        pico_freertos_firmware/CMakeLists.txt
git commit -m "feat(theme): 增加 Flash 持久化与镜像边界守卫"
```

---

### 任务 7：按键、提交任务与组合根

**文件：**
- 创建：`pico_freertos_firmware/include/board_button.h`、`src/platform/board_button.c`
- 创建：`pico_freertos_firmware/include/theme_commit_task.h`、`src/app/theme_commit_task.c`
- 创建：`pico_freertos_firmware/include/ui_input.h`
- 修改：`pico_freertos_firmware/include/ui_render.h`、`src/display/ui_render.c`
- 修改：`pico_freertos_firmware/src/main.c`
- 修改：`pico_freertos_firmware/check_style.sh`

- [ ] **步骤 1：输入源与按键**

`include/ui_input.h`（零依赖）与 `include/board_button.h`：

```c
void board_button_init(void);
bool board_button_poll_click(void);
```

`src/platform/board_button.c`：

```c
#include "board_button.h"

#include "hardware/gpio.h"

#include "button_filter.h"

/** @brief 按键引脚。 */
enum { kBoardButtonPin = 15 };

/** @brief 唯一的消抖状态。 */
static ButtonFilter s_filter;

void board_button_init(void) {
  gpio_init(kBoardButtonPin);
  gpio_set_dir(kBoardButtonPin, GPIO_IN);
  gpio_pull_up(kBoardButtonPin);
  button_filter_reset(&s_filter);
}

bool board_button_poll_click(void) {
  return button_filter_update(&s_filter, !gpio_get(kBoardButtonPin));
}
```

- [ ] **步骤 2：渲染循环接按键**

`ui_render.h` 加：

```c
void ui_render_set_input_source(const UiInputSource* source);
```

`ui_render.c` 里加文件静态 `static const UiInputSource* s_input_source;`、
`ui_render_set_input_source()` 的实现，并在 `vTaskUIRender` 循环里取快照之后插入：

```c
    if (s_input_source != NULL && s_input_source->poll_click()) {
      system_state_set_theme(
          theme_switch_advance(current.theme_id, true));
      system_state_get_snapshot(&current);
    }
```

- [ ] **步骤 3：提交任务**

`src/app/theme_commit_task.c` 要点：

```c
static StaticTask_t s_task_buffer;
static StackType_t s_stack[256];
static ThemeCommitPolicy s_policy;

static void vTaskThemeCommit(void* unused);
```

每 100 ms：

1. 取快照；
2. 读 XIP 里的 12 字节并 `theme_config_parse()`，得到 `saved_matches`；
3. `action = theme_commit_policy_tick(&s_policy, snap.theme_id, now_ms, saved_matches);`
4. `ShortCircuit` → `printf("theme: skip id=%u\n", ...)`；
5. `Write` → 先轮询 `render_window_is_idle()` 最多 40 ms（`vTaskDelay(pdMS_TO_TICKS(20))` 两次），
   拿不到就跳过本轮；拿到后 `rc = theme_storage_commit(s_policy.seen);`，
   映射成 `ThemeCommitOutcome` 喂回，并在 `Verify` 重试分支里
   `vTaskDelay(pdMS_TO_TICKS(1000))` 后重调一次；
6. 真擦写打印 `printf("theme: commit id=%u rc=%d\n", id, rc);`

`theme_commit_task_start()` 用 `xTaskCreateStaticAffinitySet(...)`，第 6 个参数传
`1u << 0`（绑核 0），失败返回 `false`。

- [ ] **步骤 4：组合根装配**

`main.c` 按顺序插入（位置见规格 §6.1）：`system_state_init()` →
`system_state_set_theme(theme_storage_init())` → `board_button_init()` →
`asset_store_init()` → `supervisor_start()` → `if (!theme_commit_task_start()) panic(...)`；
并在 `ui_render_start_task()` 之前
`ui_render_set_input_source(&kBoardInput);`。

- [ ] **步骤 5：加三条依赖方向断言**

`check_style.sh` 末尾追加规格 §6.2 的 `check_absent()` 与三条断言（路径带
`${REL_FIRMWARE}/` 前缀），并以 `|| exit 1` 收尾。

- [ ] **步骤 6：跑宿主测试与风格检查**

```bash
cmake --build pico_freertos_firmware/tests/build -j
ctest --test-dir pico_freertos_firmware/tests/build --output-on-failure
( cd pico_freertos_firmware && ./check_style.sh )
```

预期：17/17 通过；`PASS: 代码风格检查通过`。

- [ ] **步骤 7：Commit**

```bash
git add pico_freertos_firmware/include/board_button.h \
        pico_freertos_firmware/include/theme_commit_task.h \
        pico_freertos_firmware/include/ui_input.h \
        pico_freertos_firmware/include/ui_render.h \
        pico_freertos_firmware/src/platform/board_button.c \
        pico_freertos_firmware/src/app/theme_commit_task.c \
        pico_freertos_firmware/src/display/ui_render.c \
        pico_freertos_firmware/src/main.c \
        pico_freertos_firmware/check_style.sh
git commit -m "feat(theme): 接入 GP15 按键切换与 100 ms 提交任务"
```

---

### 任务 8：全量验收

**文件：** 无改动，只跑验收。

- [ ] **步骤 1：跑 §8.6 的四步**

```bash
cmake -S pico_freertos_firmware/tests -B pico_freertos_firmware/tests/build
cmake --build pico_freertos_firmware/tests/build -j
ctest --test-dir pico_freertos_firmware/tests/build --output-on-failure
( cd pico_freertos_firmware && ./check_style.sh && ./build.sh )
```

预期：`100% tests passed`，共 **17** 套；风格检查 PASS；固件冷构建无警告。

- [ ] **步骤 2：核对 §8.5 的资源判据**

```bash
arm-none-eabi-size pico_freertos_firmware/build/pico_status_screen.elf
nm --size-sort -S pico_freertos_firmware/build/pico_status_screen.elf | tail -20
```

对照 R-1（Flash 增量 ≤ 1 KB）与 R-2（RAM 增量 ≤ 2 KB）：前者看 `.rodata` 里
`s_themes` 是否为 144 字节，后者看 `.bss` 增量是否 ≤ 2 KB。

- [ ] **步骤 3：把实机项留给用户**

§8.1 的 F-1..F-4、§8.4 的 S-1..S-3、§8.5 的 V-1..V-3 需要烧录后人工观察，
本计划不授权自动烧录。交付时逐条列出待人工确认项。

---

## 自检

**规格覆盖度**

| 规格章节 | 落点 |
| :--- | :--- |
| §3.1 数据结构 | 任务 1 |
| §3.2 六套常量表 | 任务 1 步骤 5 |
| §3.3 落点表与硬规则 | 任务 1 的 `kThemePairs[]` + 任务 5 的 sed 映射 |
| §3.4 十七项阈值 | 任务 1 步骤 1 |
| §4.1 消抖 | 任务 2 |
| §4.2 输入源 | 任务 7 步骤 1–2 |
| §4.3 状态镜像 | 任务 5 步骤 1 |
| §4.4 重绘与安全窗口 | 任务 5 步骤 2/4、任务 4 的 `render_window` |
| §4.5 任务表 | 任务 7 步骤 3 |
| §5.1 边界守卫 | 任务 6 步骤 1–2 |
| §5.2 数据结构 | 任务 3 |
| §5.3 写入流程 | 任务 6 步骤 4 |
| §5.4 契约与状态机 | 任务 4（策略）+ 任务 6（结果码）+ 任务 7（映射） |
| §5.5 / §5.6 副作用与禁止项 | 任务 6 步骤 4 的 `__not_in_flash_func` 与「回调内只调擦写」 |
| §6.1 目录与装配 | 任务 7 步骤 4 |
| §6.2 依赖断言 | 任务 7 步骤 5 |
| §6.3 构建改动 | 任务 1/2/3/4/5 的 CMake 步骤 + 任务 6 步骤 1 |
| §8.1–§8.5 验收 | 任务 8 + 交付时的人工清单 |

**占位符扫描**：本计划无「待定 / TODO / 类似任务 N」；每个代码步骤都给了可编译内容。
任务 6 步骤 4 与任务 7 步骤 3 用段落描述而非整段代码，因为它们是长函数，
其逐条行为已在规格 §5.4 钉死并可由任务 4 的单元测试与任务 8 的构建验证；
实现时必须逐条对照规格行号，不允许自行增删分支。

**类型一致性**：`UiThemeId` / `UiThemePalette` / `UiThemeToken` / `ThemePairRule`
定义在任务 1，后续任务直接使用；`ThemeCommitOutcome` 由任务 4 定义、任务 7 映射，
不与 `ThemeStorageResult` 混用；`ButtonFilter` 由任务 2 定义、任务 7 实例化。
