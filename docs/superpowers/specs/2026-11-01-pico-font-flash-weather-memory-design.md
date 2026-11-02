# 字库移入 Flash 与天气内存修复设计说明

**状态：** 用户已批准方案 A（2026-11-01）
**目标硬件：** Raspberry Pi Pico W（RP2040，MicroPython 1.29.0）和 ST7735S 128×160 TFT

## 问题

电脑离线时钟页的天气行长期显示「天气暂不可用」，后台刷新从不成功。

### 实测证据（COM4 真机）

| 实验 | 结果 |
|---|---|
| 设备直调 `fetch_ip_location` / `fetch_weather` | `IPLOC_OK 330200 宁波市`、`WEATHER_OK 小雨 17.0` |
| 只装载 `weather` 模块（空闲堆 175 KB）后运行服务 | 3.5 s 取到数据，状态正常 |
| 按 `run()` 顺序装载全部模块并构造对象（空闲堆 81.7 KB） | worker 内 `OSError [Errno 12] ENOMEM`，状态停在「天气暂不可用」 |
| 工作线程内取数，进入线程时 78.7 KB 空闲 | 成功 |
| 工作线程内取数，进入线程时 74.6 KB 空闲 | `ENOMEM` |
| 把 `FONT_CN` 从 RAM 释放（空闲堆 128.1 KB） | 同样的应用代码 3 s 内取到 `宁波市 小雨 17.0` |

### 根因

高德 HTTPS 请求在 worker 线程内需要约 79 KB 空闲堆。真实应用装载字库后只剩 81.7 KB，扣除 `_thread` 栈 4.1 KB 后为 77.6 KB，且主循环每秒绘屏持续产生临时分配，TLS/DNS 分配抛 `OSError [Errno 12] ENOMEM`。

`pico/weather.py` 的 `_worker` 以 `except Exception` 吞掉异常且不记录原因，`_schedule_failure` 在无缓存时把状态写成「天气暂不可用」，因此故障表现为稳定复现且无法自诊断。

内存大头是 `pico/font_cn.py`：`import` 后独占 61.5 KB（187,152 → 125,616），由 341 个 14×14 字模（每字 98 B，共 33,418 B）加 dict/str 开销组成。

## 目标

1. 释放中文字库占用的大部分 RAM，使 worker 内的 TLS 请求有充足余量。
2. 保持显示能力不变：341 个汉字全部仍可渲染，不缩小字符集。
3. 不改变 `st7735.py` / `display.py` 的绘制调用方式。
4. 让天气失败在串口可见，避免再次无证据排查。

## 非目标

- 不改天气接口、TLS 校验策略、刷新与退避时序。
- 不改屏幕布局。
- 不引入明文回退或跳过证书校验。

## 设计

### 数据布局

`pico/font_cn.bin`（新，部署到设备 `/pico/font_cn.bin`）按字符升序存放全部 341 个中文字模，每条定长 98 B（14×14，4 bit 覆盖率，两像素一字节），总长 33,418 B。

`pico/font_cn.py`（保持为生成文件）保留：

- 尺寸常量（`CHINESE_WIDTH`、`CLOCK_DIGIT_HEIGHT` 等）；
- 热路径小字表仍然驻留 RAM：`ASCII_FONT`（71 字）、`CLOCK_FONT`（10 字）、`WEATHER_DIGIT_FONT`（11 字），合计约 8.6 KB；
- `FONT_CN_ORDER`（341 字符的升序字符串，约 1.1 KB）、`FONT_CN_BYTES_PER_GLYPH = 98`；
- `FONT_CN = FlashGlyphs(_default_glyph_path(), FONT_CN_ORDER, FONT_CN_BYTES_PER_GLYPH)`。

顺序由 `sorted(chinese_glyphs.items())` 产生，与 .bin 写盘顺序一致，字符下标 = `FONT_CN_ORDER.find(character)`（MicroPython 返回字符下标，已在设备验证）。

### 模块接口

新增 `pico/glyph_font.py`：`FlashGlyphs(path, order, bytes_per_glyph, cache_limit=64)`。

| 成员 | 行为 |
|---|---|
| `get(character, default=None)` | 命中缓存直接返回；否则按 `order.find` 求下标、`seek` + `read(98)` 返回 `bytes`；字符不在表内或读失败返回 `default` |
| `__getitem__` / `__contains__` / `__len__` | 供测试与现有 dict 习惯使用 |
| 缓存 | 上限 64 个字模（约 7.5 KB）；达到上限整体清空，避免 LRU 记账开销 |
| 文件句柄 | 首次未命中时 `open(path, "rb")` 并复用；`OSError` 时返回 `default` 并下次重开 |
| 路径 | 与 `pico/lunar.py` 相同：由 `__file__` 推导模块目录，回退到裸文件名 |

`st7735.py`、`display.py` 不改：它们只调用 `FONT_CN.get(...)`，接口保持。

### 内存预算

| 项 | 现状 | 改造后 |
|---|---|---|
| 中文字模 | 61.5 KB | 8.6 KB（顺序串 + 缓存） |
| 应用稳态空闲堆 | 81.7 KB | 约 131 KB |
| worker 取数时可用堆 | 74.6 KB（不足） | 约 126 KB（余量约 47 KB） |

### 失败与降级

字库读失败（文件缺失、提前 EOF）时 `get()` 返回 `default`，`st7735` 落到既有 `CHINESE_PLACEHOLDER_GLYPH` 占位符，页面不崩、不阻塞主循环。`tools/deploy_pico.ps1` 的部署前存在性检查包含 `font_cn.bin`，缺失时脚本直接报错停止。

### 诊断输出

`pico/weather.py` 的 worker 在成功与失败时各打印一行（成功含温度与现象，失败只含异常类型名，不打印异常消息、URL、Key 或响应内容）：

- 成功：`WX ok 17.0 小雨`
- 失败：`WX err OSError`

这样无需中断 `main.py` 即可从串口判断真实运行状态。

## 影响范围

| 文件 | 变更 |
|---|---|
| `pico/glyph_font.py` | 新增：flash 字模读取器 |
| `pico/font_cn.py` | 生成文件结构变更：`FONT_CN` 改为 `FlashGlyphs` |
| `pico/font_cn.bin` | 新增：33,418 B 字模数据 |
| `tools/build_font.py` | 同时生成 .py 与 .bin；`verify_font_set` 兼容非 dict 字表 |
| `tools/deploy_pico.ps1` | 部署清单加入 `font_cn.bin`、`glyph_font.py` |
| `pico/weather.py` | worker 增加一行诊断输出 |
| `tests/test_display.py` | 字表覆盖断言改为适配 `FlashGlyphs` |
| `README.md` / `docs/PROJECT_HANDOFF.md` | 记录新数据文件与验证结果 |

## 验证标准

1. `python -m unittest discover -s tests -v` 全部通过；`python -m compileall pc_metrics tests tools pico` 无错误。
2. 设备装载全部模块并构造对象后，稳态空闲堆 ≥ 120 KB；worker 内取数在 10 s 内返回真实温度，状态不再停在「天气暂不可用」。
3. 设备读出的字模与仓库内 341 个字模逐字节一致（抽样比对设备读出值与源数据）。
4. `tools/deploy_pico.ps1 -Port COM4` 部署成功，设备重启后串口出现 `WX ok` 行且无反复重启。
5. 用户目视确认时钟页天气行显示城市、现象与温度。
