# Pico 在线天气显示设计说明

**状态：** 用户已批准天气屏幕预览并授权实现（2026-10-31）
**目标硬件：** Raspberry Pi Pico W（RP2040，MicroPython 1.29.0）和 ST7735S 128×160 TFT

## 目标

在电脑指标仪表盘和电脑离线时钟两页显示中国大陆城市级实况天气。Pico 保持独立联网：电脑指标发送端停止后，时钟和天气仍由 Pico 的 Wi-Fi 更新。

## 已确认的产品选择

| 项目 | 选择 |
|---|---|
| 定位 | 默认调用高德 Web 服务 IP 定位，以请求来源 IPv4 推断城市；不提交显式 IP |
| 位置精度 | 公网出口 IP 的城市级推断；VPN、企业出口或运营商 NAT 可能偏到其他城市 |
| 手动覆盖 | 本地配置 CITY_OVERRIDE_ADCODE；有值时跳过 IP 定位 |
| 天气来源 | 高德 Web 服务实况天气，按 adcode 查询 |
| 字段 | 中文城市、中文天气现象、摄氏温度；温度作为主要视觉信息 |
| 页面 | 仪表盘天气替换农历行；离线时钟增加天气行且保留农历 |
| 刷新 | 启动/Wi-Fi 重连立即尝试；成功后每 30 分钟刷新 |
| 失败 | 1、5、15、30 分钟递增退避，成功后归零；保留最后成功值并标「旧」 |
| 无缓存 | 显示「获取中」或「暂不可用」；未配置 Key 时显示「天气未配置」 |
| 主循环 | 网络请求由单个后台工作线程执行；主线程不等待 DNS、TCP、TLS 或 HTTP |
| 密钥 | Key 存于新的本地 ignored 文件，不修改现有 Wi-Fi 配置；烧录到设备后可被物理提取 |
| 视觉 | 沿用暗色青色主题；天气状态变化只重画天气行 |

## API 契约

1. IP 定位：https://restapi.amap.com/v3/ip，仅发送 key 和 output=JSON，不提供 ip 参数。返回数据必须含成功状态、六位 adcode 及可显示的城市或省名。
2. 实况天气：https://restapi.amap.com/v3/weather/weatherInfo，参数为 Key、city=<六位 adcode>、extensions=base、output=JSON。仅接受成功状态下非空 lives 中的 weather、temperature；城市名缺失时回退定位服务的城市/省名。
3. Key 限制为非空、无控制字符的有限长度字符串并按 URL 参数编码；错误信息不带 URL、Key、原始 JSON 或服务端消息。
4. 两个端点都使用 HTTPS；TLS 开启证书链和主机名校验并设置 SNI。禁止使用 CERT_NONE、明文回退或忽略验证错误。
5. 高德文档说明 IP 定位仅支持 IPv4 且不解析国外 IP；实况天气输入为 adcode，温度单位为摄氏度。IP 定位失败时只在配置了城市覆盖码时继续天气请求。

## 运行结构

Pico W Wi-Fi → 单个后台天气工作线程 → 高德 HTTPS IP 定位与实况天气 → 线程安全单槽结果 → 主循环的指标、时钟与天气局部刷新。

工作线程只做同步网络 IO 和响应解析，不绘屏、不访问指标状态。主循环只在短临界区取出结果；同时最多有一个 worker。Wi-Fi 未连接或 RTC 尚未校时期间不启动 TLS 请求。Wi-Fi 恢复触发立即尝试；未完成 worker 不重复启动。暂时掉线不清空内存天气缓存。

Key 在 URL 查询串中发送是高德 Web Service 接口的要求，因此仅在已校验证书的 TLS 连接中使用，并禁止记录完整请求路径。Key 文件由本地复制产生，不提交 Git。gitignore 排除该文件。

## 页面布局与局部刷新

使用已批准的 weather-layout-v2.html：

- 仪表盘：天气行位于 y=45，替换原农历行；CPU/GPU/RAM 卡、收发速度和分割线坐标不变。
- 电脑离线时钟：天气行位于 y=15；时钟 y=34、公历日期 y=77、星期 y=96、农历 y=115、分割线 y=136、NTP 底栏 y=145。
- 城市名按剩余像素宽度裁剪并以「..」结尾。温度固定 ℃，使用比城市和现象更宽的抗锯齿数字与青色强调。
- 缓存陈旧时在天气行显示琥珀色「旧」标记；没有可用天气时，显示短中文状态文字。网络失败不得清空成功值。
- 字库补齐高德天气现象词表和常见城市名称字符。任意未覆盖汉字使用可见占位符，不得抛异常导致应用重启。

## 边界与恢复

- 缺少 Key、Key 格式错误、无效 adcode、空响应、超长响应、JSON 格式不符、温度超界或城市/天气为空，均视为可恢复的天气失败。
- 网络异常只留下通用状态，不保留或显示服务端错误正文。
- 当前天气缓存保留在 RAM；冷启动时尚未成功取得天气则显示获取状态，不假装存在历史缓存。
- 不支持国外出口 IP 是高德接口限制；可用本地覆盖 adcode。现场网络 DNS、TLS 出站策略和路由仍需 Pico 实机验收。

## 官方资料

- [高德 Web 服务 IP 定位](https://lbs.amap.com/api/webservice/guide/api/ipconfig)：IPv4 范围、请求参数和定位结果。
- [高德 Web 服务天气查询](https://lbs.amap.com/api/webservice/guide/api/weatherinfo)：实况接口、adcode、Key、返回温度和中文天气字段。
- [高德天气现象表](https://lbs.amap.com/api/webservice/guide/tools/weather-code)：天气现象中文词表。
- [GlobalSign 根证书](https://support.globalsign.com/ca-certificates/globalsign-root-certificates)：公开根证书及其有效期。
- [MicroPython 1.29.0 SSL](https://docs.micropython.org/en/v1.29.0/library/ssl.html)：CERT_REQUIRED、CA 加载和 SNI 要求。
- [MicroPython 1.29.0 _thread](https://docs.micropython.org/en/v1.29.0/library/_thread.html)：线程接口；文档标注该 API 仍属实验性，因此必须在目标 Pico 固件上验收线程启动、网络线程和内存余量。

## 验收

1. 不配置 Key 时设备继续启动，天气行显示「天气未配置」，其他页面正常。
2. 配置有效 Key 后，Pico 在 TLS 校时成功后取得 IP 定位和实况天气；设置城市覆盖码时跳过 IP 定位。
3. 每 30 分钟更新一次；Wi-Fi 恢复立即尝试；连续失败按 1/5/15/30 分钟退避。
4. 网络请求运行期间，时钟秒数和 UDP 指标仍更新；天气值变化只写天气行。
5. 请求失败时保留旧天气并显示「旧」；无缓存时显示暂不可用。
6. 未覆盖城市字形、恶意/超长响应、空字段和温度异常不会导致固件应用重启。
7. 在实际展示用的中国大陆 Wi-Fi、Pico 和屏幕上验收 DNS、证书链、TLS SNI、Key 配额与在线天气。当前电脑 TLS 握手或代理网络可达不代替 Pico 直连验收。
