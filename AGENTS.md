---
alwaysApply: true
---

# pc_partner 项目协作规范

## 项目速览

- 项目：Pico W 电脑状态屏。Windows 主机采集 CPU、GPU、系统内存和有线网卡收发速率，通过 UDP 广播发送给 Pico W；电脑指标离线后，Pico 显示时钟、农历和在线天气。
- 技术栈：Windows、Python、PowerShell、Raspberry Pi Pico W（RP2040）、MicroPython 1.29.0、ST7735S 128×160 TFT。
- 主要目录：
  - `pc_metrics/`：Windows 主机采集和 UDP 广播。
  - `pico/`：Pico MicroPython 固件、显示驱动、时钟、农历、天气和 Wi‑Fi 状态。
  - `tools/`：主机启动、固件部署及字库/数据生成工具。
  - `tests/`：Python `unittest` 测试。
  - `docs/`：设计规格、实施计划和项目交接资料。

## 协作原则

- 默认用中文交流；代码、命令、协议名和标准术语保留原文。
- 先检查当前目录、Git 分支和工作区状态；事实、推测与未验证事项分开说明。
- 提供简要决策依据和验证证据，不输出隐藏的逐步思维链。
- 保留用户已有的未提交改动。不得用 `git reset --hard`、`git clean -fdx` 或覆盖文件来清理工作区。
- 只有用户明确要求时才提交、推送、合并、删除 worktree 或操作实体硬件；用户已授权的当前任务范围内可直接执行。
- 不推送远端，除非用户明确要求。
- 不创建子代理，除非用户要求或项目指令明确要求。

## 系统结构与网络

- Windows 端只向 UDP 广播端口 8765 发指标，不开放 TCP/HTTP 入站服务。
- 主机应接入有线网，Pico 连接同一路由器 Wi‑Fi；二者需位于可广播子网。访客 Wi‑Fi、客户端隔离或跨 VLAN 会阻断广播。
- 有线网卡名通过 `pc_metrics/config.py` 配置；当前开发机使用的名称和 COM 端口不代表其他设备的配置。
- Pico 在线天气使用高德 Web 服务 IP 定位和实况天气。定位依赖大陆 IPv4 出口；可用本地 adcode 覆盖城市。天气每 30 分钟刷新，失败按 1、5、15、30 分钟退避。
- Pico 时钟经 NTP 校时后按 UTC+8 显示。农历优先查本地 HKO 年表；表外在线 API 的可用性与天气 API 分开验证。

## 硬件与固件约束

| TFT 信号 | Pico W GPIO |
|---|---|
| SCL / SPI SCK | GP18 |
| SDA / SPI MOSI | GP19 |
| RST | GP20 |
| DC | GP21 |
| CS | GP17 |

- 目标硬件是 Pico W（RP2040），不是 Pico 2 W（RP2350）。在目标固件上验证 API 支持，不能仅凭 CPython 行为推断。
- `pico/main.py` 是设备启动入口。天气、NTP 和其他网络 I/O 不得阻塞绘屏/指标主循环；RP2 的 `_thread` 属实验 API，需考虑线程启动失败和有限堆内存。目标固件的 `socket.getaddrinfo()` 没有代码级超时；必须留在后台 worker，并为线程卡住后的恢复设计单独验证。
- TLS 必须校验证书链和主机名；严禁使用 `CERT_NONE`、明文回退或忽略证书错误。
- 屏幕每秒更新，但应保留局部刷新；天气状态变化只重绘天气区域。不要在主循环每秒整屏重绘。
- `pico/font_cn.py` 是生成字库；调整字符集合时同步维护 `tools/build_font.py` 和字体验证。
- 使用 `mpremote` 或其他串口 REPL 诊断可能中断正在运行的 `main.py`。诊断结束后重启设备，并确认应用已重新启动。

## 配置与密钥

- 本地配置文件 `pico/wifi_secrets.py` 和 `pico/weather_secrets.py` 必须被 `.gitignore` 忽略。
- 不读取、回显、截图、日志记录或提交本地配置内容。检查配置时只报告文件是否存在、字段是否有效以及 ignore 状态。
- `*.example.py` 只能包含空值或明确的占位值，禁止写入真实 SSID、密码或 Key。
- 天气 Key 会通过 TLS 请求发送，也会随部署写入 Pico 闪存；设备持有者可能提取它。建议使用独立 Key 并在高德控制台限制配额。
- 不把密钥文件纳入代码图、索引或诊断包。若工具会扫描整个工作树且可能读取忽略文件，应跳过该工具并采用安全的定向检查。

## 常用 Windows 命令

在项目根目录使用 PowerShell：

    python -m pip install -r requirements-host.txt
    powershell -ExecutionPolicy Bypass -File tools/start_metrics.ps1
    powershell -ExecutionPolicy Bypass -File tools/deploy_pico.ps1
    python -m unittest discover -s tests -v
    python -m compileall pc_metrics tests tools pico

- 部署脚本参数 `-Port` 可指定 Pico 串口；当前开发板使用 COM4。执行前确认 Thonny 等串口程序没有占用设备。
- 部署只上传明确列出的固件文件并重启设备，不要通过手工 `mpremote rm` 清空设备文件系统。
- 测试完成后报告实际运行的命令和结果。纯文档变更不需要运行代码测试。

## Git 与 worktree

- 操作前检查主工作区与目标 worktree 的 `git status`、分支、提交差异和未跟踪文件。
- 只暂存任务相关的明确路径，不使用 `git add -A`，以免把本机配置或用户文件纳入提交。
- `.worktrees/` 中可能有被忽略的本机配置。移除 worktree 前先确认并安全保留用户需要的配置；不要用强制清理替代盘点。
- 合并后检查主分支状态及被保留的用户改动；未获授权不删除其他 worktree 或分支。
- Git 提交中不得加入 AI 协作者署名或生成的个人信息。

## 文档与工具

- `README.md` 面向使用者，写安装、配置、部署和现场验收步骤。
- 设计规格和计划保存在 `docs/superpowers/specs/` 与 `docs/superpowers/plans/`；实施完成后更新验证记录，不保留过时的“尚未部署/未测试”状态。
- `docs/PROJECT_HANDOFF.md` 汇总实现状态、验证边界、已知问题和下一步任务，方便后续开发者或 AI 接手。
- 若代码图或其他辅助工具无法安全排除密钥文件，跳过索引并说明原因；不要为满足工具流程而暴露凭据。
