
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

先在本地复制模板并填写路由器 Wi‑Fi 信息：

```powershell
Copy-Item pico/wifi_secrets.example.py pico/wifi_secrets.py
notepad pico/wifi_secrets.py
```

`pico/wifi_secrets.py` 已加入 `.gitignore`。请勿提交该文件，也不要把密码发到聊天或日志中。部署脚本只检查文件是否存在，不显示其内容。

确保 COM4 没有被 Thonny 等串口程序占用，然后运行：

```powershell
powershell -ExecutionPolicy Bypass -File tools/deploy_pico.ps1
```

脚本先检查本地文件、COM4 和 MicroPython 运行环境，再把应用模块放入设备 `/pico/`，将启动入口复制为设备根目录 `/main.py`，最后重启 Pico。它不会删除设备上的其他文件。

部署后检查：

1. Pico 上电后自动运行 `/main.py`，连接本地 Wi‑Fi；屏幕显示当前时间、日期、星期和中文干支农历。
2. 在电脑启动 `tools/start_metrics.ps1`，屏幕切换到 CPU/GPU/RAM 和 ETH Rx/Tx 仪表盘。
3. 停止电脑端程序，约 3 秒后屏幕切回时钟；重新启动程序后回到仪表盘。
4. 断开 Pico 电源再接回，不打开 Thonny，确认程序自动启动。

若 PC 网卡有多个地址，确认有线网卡网关与目标广播子网一致；若 Pico 收不到 UDP，检查访客 Wi‑Fi/客户端隔离及有线、无线是否处于同一广播域。

## 时钟和农历来源

MicroPython 启动后按 `ntp.aliyun.com`、`ntp1.aliyun.com`、`ntp2.aliyun.com` 顺序尝试校时，并按 UTC+8 显示。农历优先使用随固件安装的香港天文台（HKO）1901–2100 年本地逐日表；表内日期无需访问在线 API。表外日期才可能使用 HKO 公历/农历 API 补充。

**在线 API 尚未通过 Pico 实际 Wi‑Fi 路由验收。** 当前 HKO 服务证书链观察到的中间 CA（Hongkong Post e‑Cert SSL CA 3‑17）被香港邮政标记为已撤销；MicroPython 1.29.0 可校验证书链和主机名，但没有 CRL/OCSP 撤销检查接口。程序不跳过 TLS 证书校验；API 失败时保留有效本地农历表结果。Windows 代理请求成功不能代表 Pico 直连或现场路由可达。

## 本机开发检查

```powershell
python -m unittest discover -s tests -v
python -m compileall pc_metrics tests tools pico
```
