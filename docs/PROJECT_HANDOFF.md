# 项目交接：Pico W 电脑状态屏

更新日期：2026-11-01

## 项目目标

Windows 电脑通过有线网采集 CPU、GPU、系统内存和以太网收发速率，并以 UDP 广播发送给 Raspberry Pi Pico W。Pico 通过 Wi‑Fi 接收指标，显示在 ST7735S 128×160 屏幕上；电脑指标中断后约 3 秒切换到时钟、星期、农历和在线天气。

## 当前实现

| 部分 | 实现 |
|---|---|
| Windows 主机 | Python 指标采集与 UDP 广播，端口 8765；GPU 无可用 NVIDIA 数据时显示 `--` |
| Pico 网络 | Wi‑Fi STA；接收同一广播子网中的 PC 指标，超时后切换时钟页 |
| 显示 | ST7735S，128×160；时钟、日期、星期、农历、天气和指标卡；每秒刷新时尽量只重绘变化区域 |
| 时钟 | NTP 服务校时，按 UTC+8 显示 |
| 农历 | 优先使用固件内 HKO 1901–2100 年逐日表；表外日期可能尝试在线 HKO API |
| 天气 | 高德 Web 服务 IP 定位和实况天气；支持本地 adcode 覆盖；摄氏温度；成功后 30 分钟刷新，失败按 1/5/15/30 分钟退避 |
| 启动 | Pico 根目录 `/main.py` 自动启动固件；部署通过 `tools/deploy_pico.ps1` |

硬件接线：SCL/SCK→GP18，SDA/MOSI→GP19，RST→GP20，DC→GP21，CS→GP17。当前开发板串口为 COM4；其他电脑应使用部署脚本的 `-Port` 参数指定实际端口。

## 本机配置与安全

- 本机 `pico/wifi_secrets.py` 和 `pico/weather_secrets.py` 存在于主工作区并由 `.gitignore` 忽略。配置内容未写入本文件，也不得通过终端、日志、截图或 Git 输出。
- `*.example.py` 应保持为空值模板。不要将真实 SSID、密码或高德 Key 写进示例文件。
- 天气 Key 通过 TLS 请求发送，并被部署到 Pico 文件系统；持有设备的人可能从闪存提取。建议使用单独 Key 和配额。
- 高德公网 IPv4 定位可能定位到运营商出口城市；在展示场所定位不准时，在本地配置六位 adcode 覆盖城市。
- 电脑与 Pico 必须位于可广播的同一子网；不要启用访客网络或无线客户端隔离。PC 端只发送 UDP 广播，不开放 TCP/HTTP 入站端口。

## 已验证状态

2026-11-01 在当前开发机和 Pico W（COM4，MicroPython 1.29.0）完成了以下检查：

- Pico 的 Wi‑Fi 已连接；RTC 年份有效；本地天气配置有效标志为真；`_thread` 可用；AMap 根证书文件可读。
- Pico 上未修改的正式 `AmapWeatherService` 成功返回有效城市、天气现象和温度字段，温度为 18℃。
- 分阶段实测中，高德 IP 定位和实况天气均返回 `status=1`、`infocode=10000`。
- 测试后通过 COM4 重启了正式应用，并留出 15 秒完成启动。
- 针对占位状态转成有效天气的本地显示检查通过：时钟页和仪表盘都更新天气区域；时钟页状态未变化时只重绘天气行。
- 运行 `python -m unittest tests.test_display` 得到 26 项中 23 项通过、2 项失败、1 项错误。失败/错误涉及测试夹具没有跟上天气参数、ST7735 测试对象未初始化尺寸，以及旧坐标断言。需先更新这些测试再作为可靠的显示回归门禁。
- 没有读取或输出实际 Key、Wi‑Fi 密码，也没有把配置文件加入 Git。

串口不能读取 TFT 像素，因此正式应用重启后的屏幕画面没有通过自动化设备截图确认。第一次天气探针还曾遇到一次未复现的 `TypeError`；随后正式服务请求成功，目前没有证据确定该瞬时错误的根因。当前网络实测只证明开发机所连路由可访问高德，不代表其他展会网络。

## 已知限制

1. 天气缓存只在 RAM 中。冷启动后需要重新连接 Wi‑Fi、校时并获取天气；网络或 API 失败时无历史值可恢复。
2. 高德 IP 定位只支持大陆 IPv4；VPN、企业出口或运营商 NAT 可能导致城市偏差。可使用 adcode 覆盖。
3. HKO 表外农历在线 API 尚未在 Pico 的当前 Wi‑Fi 上验收。已观察到的 HKO 证书链中间 CA 被标记为撤销，而固件没有 CRL/OCSP 撤销检查接口；不得为通过请求关闭 TLS 校验。本地年表范围内仍可离线换算。
4. `_thread` 是 MicroPython RP2 的实验接口；TLS 和 DNS 等网络请求必须留在后台工作线程，不能移动到绘屏主循环。
5. RP2 的 `socket.getaddrinfo()` 调用没有代码级超时。若 DNS 在目标网络上挂起，后台 worker 可能一直保持运行标志，显示主循环仍可响应，但天气不会进入下一次退避重试。
6. 当前显示测试有 3 个未解决的测试问题；不能把该测试文件的整体状态描述为通过。

## 下一步计划

1. 在屏幕上目视确认正常上电后天气行显示城市、现象和温度，并核对城市定位；在目标展会 Wi‑Fi 上复测 DNS、TLS 和天气接口。
2. 更新 `tests/test_display.py` 的旧测试夹具和坐标断言，补上天气状态从“暂不可用”转为有效值的时钟页/仪表盘局部刷新覆盖；之后运行完整 `unittest` 和 `compileall`。
3. 设计并验证 DNS 长时间不返回时的恢复策略；`getaddrinfo()` 当前没有代码级超时，不能把后台线程仍存活误判为正常重试。
4. 在目标 Pico 上验证首次连接、Wi‑Fi 断开/恢复、API 失败退避和恢复、连续运行时 CPU 指标与时钟仍持续更新。
5. 单独处理 HKO 表外农历服务的证书撤销约束，保持证书验证开启；若在线 API 不可用，明确保留本地年表能力边界。
6. 在实际 Windows 有线网卡上验证指标广播，确认网卡名配置、路由器广播域和客户端隔离设置。

## 常用命令

在项目根目录使用 PowerShell：

    python -m pip install -r requirements-host.txt
    powershell -ExecutionPolicy Bypass -File tools/start_metrics.ps1
    powershell -ExecutionPolicy Bypass -File tools/deploy_pico.ps1 -Port COM4
    python -m unittest discover -s tests -v
    python -m compileall pc_metrics tests tools pico

部署或串口诊断会重启/中断 Pico 上运行的应用。完成后确认固件已重新启动。
