# Pico ST7735S 局部刷新实现计划

> **面向 AI 代理的工作者：** 使用 `executing-plans` 按任务执行本计划。逐项勾选步骤，并在每个任务结束后核对验收条件。

**目标：** 保留现有画面和 1 秒更新频率，将 Pico 屏幕从周期性整屏重绘改为按可见内容变化局部更新。

**架构：** `pico.display` 将绘制函数改为接收上一帧的显示状态、返回当前显示状态，并按字段变化写入 ST7735S 局部区域。`pico.app` 持有上一帧状态；绘制函数检测页面模式变化并在首次显示或切页时完整重绘。

**技术栈：** MicroPython、ST7735S SPI 区域写入、Python `unittest` 录制画布。

---

## 文件清单

| 路径 | 职责 |
|---|---|
| `tests/test_display.py` | 用 `RecordingCanvas` 验证首次整屏绘制、稳定帧无写入、时钟区域更新、单卡片更新和页面切换。 |
| `pico/display.py` | 生成显示态快照，按变化更新时钟、日期、状态、指标卡和收发速率的局部区域。 |
| `pico/app.py` | 在循环中保留并传递上一帧快照，将 Wi‑Fi 配置提示纳入时钟渲染状态。 |
| `docs/superpowers/specs/2026-10-31-pico-st7735s-partial-refresh-design.md` | 已批准的行为规格。 |

## 任务 1：添加局部刷新回归测试

**文件：** 修改 `tests/test_display.py`。

- [x] **步骤 1：增加显示帧辅助函数和局部刷新回归测试**

在文件中现有 `RecordingCanvas` 定义之后加入辅助函数和测试类。它们使用现有 `RecordingCanvas`，并调用后续实现的 `previous` 参数：

```python
def _draw_dashboard_frame(canvas, metrics, previous=None, **changes):
    values = {
        "time_text": "15:28:41",
        "weekday_short": "周六",
        "date_text": "10/31",
        "lunar_text": "丙午年九月廿二",
        "metrics": metrics,
        "wifi_ok": True,
    }
    values.update(changes)
    return draw_dashboard(
        canvas,
        values["time_text"],
        values["weekday_short"],
        values["date_text"],
        values["lunar_text"],
        values["metrics"],
        values["wifi_ok"],
        previous=previous,
    )


def _draw_clock_frame(canvas, time_text="15:28:41", previous=None, **changes):
    values = {
        "date_text": "2026年10月31日",
        "weekday_full": "星期六",
        "lunar_text": "丙午年九月廿二",
        "wifi_ok": True,
        "time_synced": True,
        "needs_config": False,
    }
    values.update(changes)
    return draw_clock(
        canvas,
        time_text,
        values["date_text"],
        values["weekday_full"],
        values["lunar_text"],
        values["wifi_ok"],
        values["time_synced"],
        previous=previous,
        needs_config=values["needs_config"],
    )


class PartialRefreshTests(unittest.TestCase):
    def test_unchanged_dashboard_frame_performs_no_writes(self):
        canvas = RecordingCanvas()
        metrics = {"cpu": 20, "gpu": 30, "ram": 40, "rx": 1.0, "tx": 0.5}
        previous = _draw_dashboard_frame(canvas, metrics)
        self.assertIn("fill", [call[0] for call in canvas.calls])

        canvas.calls.clear()
        current = _draw_dashboard_frame(canvas, dict(metrics, cpu=20.4), previous)

        self.assertEqual(current, previous)
        self.assertEqual(canvas.calls, [])

    def test_dashboard_cpu_change_repaints_only_cpu_card(self):
        canvas = RecordingCanvas()
        metrics = {"cpu": 20, "gpu": 30, "ram": 40, "rx": 1.0, "tx": 0.5}
        previous = _draw_dashboard_frame(canvas, metrics)
        canvas.calls.clear()

        changed = dict(metrics, cpu=21)
        _draw_dashboard_frame(canvas, changed, previous)

        self.assertNotIn("fill", [call[0] for call in canvas.calls])
        self.assertTrue(canvas.calls)
        for call in canvas.calls:
            if call[0] == "rect":
                self.assertGreaterEqual(call[2], 61)
                self.assertLess(call[2] + call[4], 83)
            elif call[0] == "text":
                self.assertIn(call[3], ("CPU", "21%"))

    def test_clock_second_change_repaints_only_time_band(self):
        canvas = RecordingCanvas()
        previous = _draw_clock_frame(canvas)
        canvas.calls.clear()

        _draw_clock_frame(canvas, "15:28:42", previous)

        self.assertNotIn("fill", [call[0] for call in canvas.calls])
        self.assertTrue(canvas.calls)
        for call in canvas.calls:
            self.assertIn(call[0], ("rect", "clock"))
            if call[0] == "rect":
                self.assertGreaterEqual(call[2], 34)
                self.assertLess(call[2] + call[4], 61)

    def test_switching_layout_forces_full_redraw(self):
        canvas = RecordingCanvas()
        previous = _draw_clock_frame(canvas)
        canvas.calls.clear()

        _draw_dashboard_frame(
            canvas,
            {"cpu": 20, "gpu": 30, "ram": 40, "rx": 1.0, "tx": 0.5},
            previous,
        )

        self.assertIn("fill", [call[0] for call in canvas.calls])

    def test_dashboard_date_change_updates_only_date_row(self):
        canvas = RecordingCanvas()
        metrics = {"cpu": 20, "gpu": 30, "ram": 40, "rx": 1.0, "tx": 0.5}
        previous = _draw_dashboard_frame(canvas, metrics)
        canvas.calls.clear()

        _draw_dashboard_frame(
            canvas, metrics, previous, weekday_short="周日", date_text="11/01"
        )

        self.assertNotIn("fill", [call[0] for call in canvas.calls])
        self.assertTrue(canvas.calls)
        for call in canvas.calls:
            if call[0] == "rect":
                self.assertGreaterEqual(call[2], 30)
                self.assertLess(call[2] + call[4], 45)
            elif call[0] == "text":
                self.assertEqual(call[2], 30)

    def test_dashboard_wifi_change_updates_only_header(self):
        canvas = RecordingCanvas()
        metrics = {"cpu": 20, "gpu": 30, "ram": 40, "rx": 1.0, "tx": 0.5}
        previous = _draw_dashboard_frame(canvas, metrics)
        canvas.calls.clear()

        _draw_dashboard_frame(canvas, metrics, previous, wifi_ok=False)

        self.assertNotIn("fill", [call[0] for call in canvas.calls])
        self.assertTrue(canvas.calls)
        for call in canvas.calls:
            if call[0] == "rect":
                self.assertLess(call[2] + call[4], 15)
            elif call[0] == "text":
                self.assertEqual(call[2], 1)

    def test_dashboard_rate_change_updates_only_its_row(self):
        for key, row_y in (("rx", 130), ("tx", 145)):
            with self.subTest(key=key):
                canvas = RecordingCanvas()
                metrics = {"cpu": 20, "gpu": 30, "ram": 40, "rx": 1.0, "tx": 0.5}
                previous = _draw_dashboard_frame(canvas, metrics)
                canvas.calls.clear()

                _draw_dashboard_frame(canvas, dict(metrics, **{key: 2.0}), previous)

                self.assertNotIn("fill", [call[0] for call in canvas.calls])
                self.assertTrue(canvas.calls)
                for call in canvas.calls:
                    if call[0] == "rect":
                        self.assertGreaterEqual(call[2], row_y)
                        self.assertLess(call[2] + call[4], row_y + 15)
                    elif call[0] == "text":
                        self.assertEqual(call[2], row_y)

    def test_clock_sync_change_updates_only_footer(self):
        canvas = RecordingCanvas()
        previous = _draw_clock_frame(canvas)
        canvas.calls.clear()

        _draw_clock_frame(canvas, previous=previous, time_synced=False)

        self.assertNotIn("fill", [call[0] for call in canvas.calls])
        self.assertTrue(canvas.calls)
        for call in canvas.calls:
            if call[0] == "rect":
                self.assertGreaterEqual(call[2], 145)
            elif call[0] == "text":
                self.assertEqual(call[2], 145)

    def test_invalid_to_valid_clock_repaints_content_without_header(self):
        canvas = RecordingCanvas()
        previous = _draw_clock_frame(canvas, time_text=None)
        canvas.calls.clear()

        _draw_clock_frame(canvas, "15:28:42", previous)

        self.assertNotIn("fill", [call[0] for call in canvas.calls])
        self.assertTrue(canvas.calls)
        self.assertFalse(any(call[0] == "text" and call[2] == 1 for call in canvas.calls))
```

- [x] **步骤 2：运行目标测试确认失败**

运行：`./.venv/Scripts/python.exe -m unittest tests.test_display.PartialRefreshTests -v`

预期：FAIL，现有 `draw_clock` / `draw_dashboard` 尚不接受 `previous` 参数，证明回归测试会捕获当前整屏重绘实现。

## 任务 2：实现显示态缓存和区域更新

**文件：** 修改 `tests/test_display.py`、`pico/display.py`。

- [x] **步骤 1：为现有全量绘制函数增加快照接口**

保留原有调用参数顺序，在 `draw_dashboard` 末尾增加 `previous=None`，在 `draw_clock` 末尾增加 `previous=None, needs_config=False`。两者都返回带有 `mode` 键的显示态字典。若 `previous` 为空或页面模式不同，调用现有全量绘制路径并返回快照；相同模式则逐字段比较快照并只绘制变化区域。

仪表盘快照存储实际显示值：时间字符串、`weekday_short + date_text` 合成的日期行、农历文本、Wi‑Fi 状态、三个指标各自的 `_percent_text` 结果，以及 `_rate_text` 生成的 Rx/Tx 行。时钟快照存储有效/无效时间状态、时钟文本、日期、星期、农历、Wi‑Fi、同步状态和配置提示状态。以显示态而非原始数据比较，确保两个原始值舍入到相同文本和进度条时不产生 SPI 写入。

接口契约：`draw_dashboard(canvas, time_text, weekday_short, date_text, lunar_text, metrics, wifi_ok, previous=None)` 接收上一帧字典并返回当前字典；`draw_clock(canvas, time_text, date_text, weekday_full, lunar_text, wifi_ok, time_synced, previous=None, needs_config=False)` 也返回当前字典。两个字典都包含 `mode`，值分别为 `dashboard` 和 `clock`，并只保存屏幕实际显示的值。

- [x] **步骤 2：实现各字段的局部绘制 helper**

保留 `canvas.fill(BG)` 只用于首次整屏绘制和两个模式之间的转换。增加局部 helper：

```python
def _clear_text_row(canvas, y):
    canvas.fill_rect(0, y, canvas.width, CHINESE_HEIGHT, BG)


def _metric_card_state(canvas, y, name, value_text, percent, color):
    card_x = 4
    card_width = 120
    card_height = 20
    canvas.fill_rect(card_x, y, card_width, card_height, CARD)
    canvas.fill_rect(card_x, y, 2, card_height, color)
    canvas.text(10, y + 1, name, CYAN_LIGHT, CARD)
    canvas.text(82, y + 1, value_text, TEXT, CARD)
    bar_x = 10
    bar_y = y + 15
    bar_width = 108
    canvas.fill_rect(bar_x, bar_y, bar_width, 3, TRACK)
    filled = (bar_width * percent + 50) // 100
    if filled:
        canvas.fill_rect(bar_x, bar_y, filled, 3, color)
```

从 `pico.font_cn` 导入 `CHINESE_HEIGHT` 供文字行清理使用。将 `_metric_card` 中的格式化部分移至 `_metric_card_state(canvas, y, name, value_text, percent, color)`，全量和局部绘制共同调用后者。`_clear_text_row` 后再画日期、农历、星期、NTP 状态或 ETH 收发文字，避免新文字比旧文字短时留下残像。指标卡坐标固定为 CPU `y=61`、GPU `y=83`、RAM `y=105`；收发文字行固定为 `y=130` 和 `y=145`。时钟秒变化时只更新 `clock_text` 时间区域；日期、星期、农历、头部、配置提示和状态行分别更新自己的 14 像素文字行。

- [x] **步骤 3：运行显示测试确认通过**

运行：`./.venv/Scripts/python.exe -m unittest tests.test_display -v`

预期：全部通过；既有排版、边界、字体和格式化断言保持通过，新断言确认稳定帧零写入、局部字段更新和跨模式整屏重绘。

- [ ] **步骤 4：提交显示渲染层**

```powershell
git add pico/display.py tests/test_display.py
git commit -m "fix: add partial Pico display rendering"
```

## 任务 3：将上一帧状态接入 Pico 应用

**文件：** 修改 `pico/app.py`。

- [ ] **步骤 1：让应用循环持有并传递渲染状态**

在 `run()` 初始化 `render_state = None`。在 `draw_due` 分支中将上一帧状态传给页面绘制函数，并保存返回值：

```python
if mode == "dashboard":
    render_state = display.draw_dashboard(
        screen,
        clock["time_text"],
        clock["weekday_short"],
        clock["short_date"],
        lunar_text,
        state.metrics or {},
        wifi_connected,
        previous=render_state,
    )
else:
    render_state = display.draw_clock(
        screen,
        clock["time_text"],
        clock["full_date"],
        clock["weekday_full"],
        lunar_text,
        wifi_connected,
        synchronizer.synced,
        previous=render_state,
        needs_config=wifi.needs_config,
    )
```

删除 `draw_clock` 后额外绘制 `WIFI CONFIG` 的 `screen.text` 调用；配置提示由时钟快照和局部行绘制统一管理。保留 `draw_due` 的 1 秒周期、收到新指标包时立即更新，以及 `DashboardState` 的 3 秒离线逻辑。

- [ ] **步骤 2：验证页面切换和本地离线时钟**

运行：`./.venv/Scripts/python.exe -m unittest tests.test_display tests.test_app_state -v`

预期：全部通过；在仪表盘与时钟之间转换时上一个 `mode` 不匹配，绘制函数执行完整重绘；时钟日期和同步状态仍正常显示。

- [ ] **步骤 3：运行完整本机检查**

运行：

```powershell
./.venv/Scripts/python.exe -m unittest discover -s tests -v
./.venv/Scripts/python.exe -m compileall -q pc_metrics pico tests tools
git diff --check -- pico/app.py pico/display.py tests/test_display.py
```

预期：单元测试全部通过、编译命令退出码为 0、目标代码文件的 `git diff --check` 无输出。只暂存 `pico/app.py`、`pico/display.py`、`tests/test_display.py`，不暂存 `pico/wifi_secrets.example.py`。

- [ ] **步骤 4：提交应用接线与测试结果**

```powershell
git add pico/app.py
git commit -m "fix: avoid full-screen refresh on Pico updates"
```

## 任务 4：合并、部署并做屏幕验收

**文件：** 无额外代码文件；将已测试程序合并到 `main` 并部署至 COM4。

- [ ] **步骤 1：将已验证提交快进合并到 main**

在主工作区运行：

```powershell
git merge --ff-only codex/pico-wifi-metrics-display
git status --short
```

预期：`main` 快进到已验证实现；保留根工作区现有 `tools/start_metrics.ps1` 修改，并保留 worktree 中未提交的 Wi‑Fi 模板改动，不清理用户正在使用的 worktree。

- [ ] **步骤 2：确认依赖路径并部署 Pico**

从 `D:\Projects\pc_partner` 运行：

```powershell
$env:PATH = 'D:\Projects\pc_partner\.worktrees\pico-wifi-metrics-display\.venv\Scripts;' + $env:PATH
.\tools\deploy_pico.ps1 -Port COM4
```

预期：部署脚本上传根目录 `main.py` 与 `/pico` 模块，最后复位 COM4；输出不包含 Wi‑Fi 密码。

- [ ] **步骤 3：观察稳定时钟和指标更新**

在前台终端运行电脑端 `tools/start_metrics.ps1`，目视确认屏幕每秒更新时间或指标时背景和静态标签保持稳定。在该终端按 `Ctrl+C` 停止发送端，确认约 3 秒后时钟出现且每秒只更新时间区域；重新启动发送端，确认仪表盘恢复且没有整屏闪烁。

## 完成标准

- 当前布局、字体、颜色、1 秒更新节奏、UDP 协议和 3 秒断线回退行为不变。
- 首帧和时钟/仪表盘切换各完整绘制一次；同一页面的稳定字段不产生 SPI 写入。
- 时钟秒、仪表数值、网速、日期/农历和连接状态各自只更新所属区域。
- COM4 部署后，用户确认两种页面切换和一秒更新均没有整屏闪烁。
