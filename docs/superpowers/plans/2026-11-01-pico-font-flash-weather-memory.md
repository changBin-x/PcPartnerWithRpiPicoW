# 字库移入 Flash 与天气内存修复实现计划

> **面向 AI 代理的工作者：** 按任务顺序执行；每个任务完成后勾选并记录证据。设备验证使用 COM4，执行前确认 Thonny 未占用串口。

**目标：** 把 341 个中文字模从 RAM 移到 `/pico/font_cn.bin` 按需读取，把应用稳态空闲堆从 81.7 KB 提升到约 131 KB，使天气后台线程的 HTTPS 请求稳定成功。

**架构：** 生成器 `tools/build_font.py` 同时产出 `pico/font_cn.py`（常量 + 热路径小字表 + 顺序串）和 `pico/font_cn.bin`（定长字模）；新增 `pico/glyph_font.py` 提供带小缓存的 dict 风格读取器；`st7735.py` / `display.py` 保持调用方式不变。

**技术栈：** MicroPython 1.29.0、RP2040、ST7735S、Python 3.11（生成器与测试）、mpremote 1.29.0、pyserial 3.5。

---

## 任务 1：字模数据与生成器

- [ ] `tools/build_font.py`：抽出 `write_glyph_module(output_path, blob_path, ...)`，写 .py 的同时写 .bin（按字符升序、定长）。
- [ ] `tools/build_font.py`：`_verify_glyphs` 支持 dict 之外的 `__contains__` / `__getitem__` 字表。
- [ ] 由现有 `pico/font_cn.py` 数据无损转换出 `pico/font_cn.bin` 与新版 `pico/font_cn.py`（仓库无源字体文件，不重跑渲染）。
- [ ] 校验：.bin 长度 = 341 × 98 = 33,418 B，且逐字符与旧表字节一致。

## 任务 2：读取器模块

- [ ] 新增 `pico/glyph_font.py`：`FlashGlyphs`，含 `get` / `__getitem__` / `__contains__` / `__len__`、64 字缓存、惰性打开、`OSError` 降级。
- [ ] `pico/lunar.py` 的路径推导方式在 `default_path()` 中复用。

## 任务 3：天气诊断输出

- [ ] `pico/weather.py` 的 `_worker`：成功打印 `WX ok <温度> <现象>`，失败打印 `WX err <异常类型名>`，不打印异常消息。

## 任务 4：部署与测试

- [ ] `tools/deploy_pico.ps1`：清单加入 `font_cn.bin`、`glyph_font.py`。
- [ ] `tests/test_display.py`：字表覆盖断言适配 `FlashGlyphs`；新增 .bin 长度与顺序串一致性测试。
- [ ] `python -m unittest discover -s tests -v`、`python -m compileall pc_metrics tests tools pico`。

## 任务 5：设备验证

- [ ] 探针 1：按 `run()` 顺序装载并构造对象后测量空闲堆（目标 ≥ 120 KB）。
- [ ] 探针 2：同一状态下驱动 `AmapWeatherService.poll`，10 s 内取得真实温度。
- [ ] 探针 3：抽样比对设备读出的字模与源数据字节一致。
- [ ] `tools/deploy_pico.ps1 -Port COM4` 部署并重启。
- [ ] 被动串口采集 ≥ 3 分钟：出现 `WX ok` 行，无 `WX err` 连续刷屏、无重启循环。
- [ ] 用户目视确认时钟页天气行正常。

## 任务 6：文档

- [ ] `docs/PROJECT_HANDOFF.md`：更新内存约束、字库数据文件与本次验证结论。
- [ ] `README.md`：部署清单与故障排查补充字库文件说明。
- [ ] 记录根因、修改与验证证据。
