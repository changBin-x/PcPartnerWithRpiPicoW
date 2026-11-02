
# Pico W 电脑状态屏

基于 Raspberry Pi Pico W（RP2040）和 ST7735S 128×160 TFT。Pico 连接路由器 Wi‑Fi；Windows 电脑保持有线连接，通过 UDP 广播发送 CPU、GPU、系统内存及有线网卡收发速率。Pico 断开电脑指标约 3 秒后显示全屏时钟。

## 硬件连接

| 屏幕信号 | Pico W 引脚 |
|---|---|
| SCL / SPI SCK | GP18 |
| SDA / SPI MOSI | GP19 |
| RST | GP20 |
| DC | GP21 |
| CS | GP17 |

屏幕供电和背光按模块标识接线。项目已在 MicroPython 1.29.0 的 Pico W 上运行 SPI 画面样例。

## Windows 电脑

电脑需用有线网卡接入与 Pico Wi‑Fi 相同的可广播子网；路由器不能启用访客网络或无线客户端隔离。指标服务只发送 UDP 广播到端口 8765，不创建 TCP/HTTP 入站服务。

当前工程使用的有线接口名为 `以太网`。若在其他电脑上运行，请检查 `pc_metrics/config.py` 的 `ETHERNET_INTERFACE`；程序按指定网卡自己的默认网关选择匹配子网的 IPv4 地址，并在启动时打印接口和广播目标。

在项目根目录运行：

```powershell
python -m pip install -r requirements-host.txt
powershell -ExecutionPolicy Bypass -File tools/start_metrics.ps1
```

如果没有 NVIDIA 驱动或 `nvidia-smi` 不可用，GPU 指标显示 `--`，其他指标仍会发送。以太网速率单位为十进制 MB/s。

## Pico Wi‑Fi 配置与部署

先在本地复制模板并填写路由器 Wi‑Fi 和高德天气配置：

```powershell
Copy-Item pico/wifi_secrets.example.py pico/wifi_secrets.py
Copy-Item pico/weather_secrets.example.py pico/weather_secrets.py
notepad pico/wifi_secrets.py
notepad pico/weather_secrets.py
```

在高德开放平台创建 **Web 服务** Key，并为应用启用 IP 定位和天气查询。把 Key 填入 `pico/weather_secrets.py` 的 `AMAP_WEB_KEY`。`CITY_OVERRIDE_ADCODE` 可留空以按公网 IPv4 自动定位，也可填写六位高德 adcode 固定城市。公网 IP 定位可能落到运营商出口城市；高德 IP 定位仅支持中国大陆 IPv4。需要固定展示城市时使用覆盖值。

两个 `*_secrets.py` 文件均已加入 `.gitignore`，请勿提交，也不要把密码或 Key 发到聊天、日志或截图中。天气 Key 会随固件写入 Pico 闪存，拿到设备的人可以提取；建议为设备单独创建 Key 并在高德控制台设置调用配额。部署脚本只检查配置文件是否存在，不读取或显示其内容。

天气请求在 Wi‑Fi 连接且 NTP 校时成功后由后台线程发起，TLS 会校验证书链和主机名，不会在绘屏主循环中同步访问网络。成功后每 30 分钟刷新；失败按 1、5、15、30 分钟退避重试，Wi‑Fi 重连时立即重试。温度固定显示摄氏度。最近一次成功天气只缓存在 RAM；断网或接口失败时保留并标为旧数据，断电重启后重新获取。仪表盘用天气行替换农历行，离线时钟仍显示天气和完整农历。

Pico W 的可用堆很紧张：完整应用启动后约 125 KB 空闲堆，而高德 HTTPS 请求峰值会占去其中大部分。中文点阵字库因此改为按需读取设备上的 `font_cn.bin`，不再整份驻留 RAM；部署清单必须包含该文件与 `pico/glyph_font.py`，漏传时汉字会显示为空白。

接口说明：[高德 IP 定位](https://lbs.amap.com/api/webservice/guide/api/ipconfig)、[高德天气查询](https://lbs.amap.com/api/webservice/guide/api/weatherinfo)。2026-11-01 已在开发机当前 Wi‑Fi 上由 Pico 实测完成 IP 定位和天气查询：修复堆内存不足后，冷启动 2–10 秒内即取到城市、现象和温度，160 秒观测窗口内无失败。这只证明当前网络可达，展会或其他路由仍需现场确认。

确保 COM4 没有被 Thonny 等串口程序占用，然后运行：

```powershell
powershell -ExecutionPolicy Bypass -File tools/deploy_pico.ps1
```

脚本先检查本地文件、COM4 和 MicroPython 运行环境，再把应用、天气 TLS 根证书、中文点阵字库 `font_cn.bin` 和本地配置放入设备 `/pico/`，将启动入口复制为设备根目录 `/main.py`，最后重启 Pico。它不会删除设备上的其他文件。

部署后检查：

1. Pico 上电后自动运行 `/main.py`，连接本地 Wi‑Fi 并完成 NTP 校时；天气行显示城市、现象和摄氏温度，时钟页还显示日期、星期和中文干支农历。
2. 在电脑启动 `tools/start_metrics.ps1`，屏幕切换到 CPU/GPU/RAM 和 ETH Rx/Tx 仪表盘。
3. 停止电脑端程序，约 3 秒后屏幕切回时钟；重新启动程序后回到仪表盘。
4. 断开 Pico 电源再接回，不打开 Thonny，确认程序自动启动。

若 PC 网卡有多个地址，确认有线网卡网关与目标广播子网一致；若 Pico 收不到 UDP，检查访客 Wi‑Fi/客户端隔离及有线、无线是否处于同一广播域。

天气取数结果会打印到串口（115200 波特率）：成功是 `WX ok <温度> <现象>`，失败是 `WX err <异常类型> <errno>`（只打印类型与错误码，不含 URL、Key 或响应体）。用只读方式连上串口观察即可，不要发送 Ctrl-C 中断正在运行的程序。

## 时钟和农历来源

MicroPython 启动后按 `ntp.aliyun.com`、`ntp1.aliyun.com`、`ntp2.aliyun.com` 顺序尝试校时，并按 UTC+8 显示。农历优先使用随固件安装的香港天文台（HKO）1901–2100 年本地逐日表；表内日期无需访问在线 API。表外日期才可能使用 HKO 公历/农历 API 补充。

**HKO 表外农历 API 尚未通过 Pico 实际 Wi‑Fi 路由验收。** 当前 HKO 服务证书链观察到的中间 CA（Hongkong Post e‑Cert SSL CA 3‑17）被香港邮政标记为已撤销；MicroPython 1.29.0 可校验证书链和主机名，但没有 CRL/OCSP 撤销检查接口。程序不跳过 TLS 证书校验；API 失败时保留有效本地农历表结果。Windows 代理请求成功不能代表 Pico 直连或现场路由可达。

## 本机开发检查

```powershell
python -m unittest discover -s tests -v
python -m compileall pc_metrics tests tools pico
```
