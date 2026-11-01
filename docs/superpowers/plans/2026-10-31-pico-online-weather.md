# Pico 在线天气功能实现计划

> **面向 AI 代理的工作者：** 按下列任务顺序在当前专用 worktree 内执行；保存任务复选状态。设备验证需要用户提供有效本地 Key 并在 Pico 上验收。

**目标：** 在 Pico W 的电脑指标页和离线时钟页安全显示高德城市级实况天气，同时保持主循环和局部刷新可用。

**架构：** 新增 AMap 天气服务模块负责配置、TLS、解析、缓存、退避和单个后台 worker；主循环通过短锁轮询结果并把显示状态传给两页。天气行由 display 模块局部绘制，未知汉字由 ST7735 文本层安全降级。

**技术栈：** MicroPython 1.29.0、RP2040、_thread 单 worker、socket、ssl.SSLContext、JSON、高德 Web 服务、ST7735 字库生成器。

---

## 文件结构

- 创建 pico/weather.py：高德请求、TLS、校验、单 worker、天气状态/缓存/重试。
- 创建 pico/weather_secrets.example.py：Key 与可选城市 adcode 模板。
- 创建 pico/amap_root_ca.pem：由 GlobalSign 官方发布、经当前 AMap 域名链核对的公共根信任证书；运行时保持证书验证。
- 修改 .gitignore：忽略本地 pico/weather_secrets.py。
- 修改 tools/build_font.py 与 pico/font_cn.py：天气词表、城市字符、摄氏度符号、截断省略号和中号温度数字。
- 修改 pico/st7735.py：缺失汉字显示可见占位符。
- 修改 pico/display.py：两页面天气行及天气行局部更新。
- 修改 pico/app.py：调度服务、取结果、传递显示状态，不在主循环执行网络请求。
- 修改 tools/deploy_pico.ps1：部署天气模块、CA 文件和本地 Key 文件；仅做路径存在性检查，不输出 Key。
- 修改 README.md：配置、部署、Key 保护、城市覆盖与现场验收说明。
- 创建本设计说明及本计划。

## 任务 1：天气配置与 TLS API

**文件：**
- 创建：pico/weather.py
- 创建：pico/weather_secrets.example.py
- 创建：pico/amap_root_ca.pem
- 修改：.gitignore

- [x] 新建本地模板，仅包含空 AMAP_WEB_KEY 和空 CITY_OVERRIDE_ADCODE；忽略对应 pico/weather_secrets.py。没有读取/改写 Wi-Fi 配置。
- [x] Key 限制为非空、无控制字符的有限长度字符串并按 URL 参数编码；覆盖码为空或正好六位数字。
- [x] 以 /v3/ip?key=...&output=JSON 发 IP 定位，省略 ip；检验响应状态、adcode、城市/省份。
- [x] 以 /v3/weather/weatherInfo?key=...&city=...&extensions=base&output=JSON 取实况天气；只接受成功响应中的天气及 [-80, 70] 摄氏温度。
- [x] 限制头部/响应体长度，兼容 Content-Length 与 chunked 响应，错误中不带 URL、Key、原始 JSON。
- [x] TLS 通过 SSLContext(PROTOCOL_TLS_CLIENT)、CERT_REQUIRED、GlobalSign CA 文件与 server_hostname=restapi.amap.com 校验证书和主机名；任何验证错误直接失败，不降级。

## 任务 2：单 worker、缓存和重试

**文件：**
- 修改：pico/weather.py

- [x] 在 AmapWeatherService 初始化时安全加载本地配置；配置缺失或无效时关闭网络任务并显示「天气未配置」。
- [x] 用 _thread.allocate_lock() 保护单槽结果与运行标志；worker 内串行完成 IP 定位（若需要）与天气查询；线程不可用时不退回同步网络调用。
- [x] worker 只写结果元组；不访问 TFT、不打印异常、不共享可变解析对象。
- [x] 主线程轮询时快速取走结果；成功缓存天气并安排 30 分钟下次刷新；失败保留缓存、标陈旧并按 1/5/15/30 分钟退避。
- [x] Wi-Fi 从断开到连接时安排立即尝试；只有 NTP 校时有效后才发起 TLS；同一时刻不启动第二个 worker。

## 任务 3：字体与天气行局部绘制

**文件：**
- 修改：tools/build_font.py
- 修改：pico/font_cn.py
- 修改：pico/st7735.py
- 修改：pico/display.py

- [x] 扩充生成器必需字符集合：高德天气现象词表、常见中国城市字符、「旧」、配置/状态词和「℃」。
- [x] 用项目已有抗锯齿流程生成字库；保留现有字库宽高与 ASCII/clock 字形契约，增加 11px 天气温度数字。
- [x] 对未知 CJK 字形使用可见方框占位符，不抛异常。
- [x] 在仪表盘 y=45 显示天气，并将原农历字段移出仪表盘；保持卡片/rates 坐标不变。
- [x] 在时钟 y=15 增加天气行；保留 y=34 时钟、日期/星期/农历和底栏。
- [x] 城市按可用像素宽度裁剪并加「..」；温度使用较宽抗锯齿数字、固定 ℃ 且青色突出；旧数据增加琥珀色标记。
- [x] 将天气显示状态纳入各页面渲染状态比较，只清天气行，不做全屏重绘。

## 任务 4：主循环与部署接入

**文件：**
- 修改：pico/app.py
- 修改：tools/deploy_pico.ps1
- 修改：README.md

- [x] app 启动一次 AmapWeatherService，每轮传递 Wi-Fi 状态、连接变化和 NTP 有效状态；每次绘制时传递 weather snapshot。
- [x] 天气 worker 的结果不改变现有 UDP 收包、PC 断连 3 秒切页、NTP、农历和上电入口路径。
- [x] 部署包增加 weather.py、amap_root_ca.pem、weather_secrets.py，并在天气配置缺失时清晰失败；检查输出绝不显示配置内容。
- [x] README 加入本地复制配置步骤、Web Service Key 申请与 adcode 覆盖设置、Key 烧录可被提取/配额建议和实际中国大陆 Pico 验收步骤。

## 静态审阅和设备交接

- [x] 静态检查差异；确认 TLS 校验未降级，部署范围仅含明确列出的 Pico 模块和配置。
- [x] 2026-11-01 在 COM4 对 Pico W 执行实机检查：Wi‑Fi 已连接、RTC 年份有效、本地 Key 配置存在、线程可用、AMap CA 文件可读。
- [x] Pico 上的正式 `AmapWeatherService` 成功取得天气数据；独立分阶段请求确认 IP 定位与实况天气均返回 `status=1`、`infocode=10000`，城市、天气现象和温度字段有效（18℃）。
- [x] 将 Pico 重启回正式 `main.py` 应用；串口无法读取 TFT 像素，正常启动后的屏幕视觉结果仍待现场目视确认。
- [x] 运行 `python -m unittest tests.test_display`：26 项中 23 项通过、2 项失败、1 项错误。失败项涉及旧测试夹具未传天气参数、ST7735 测试对象未初始化尺寸，以及旧坐标断言；需更新测试后再作为回归门禁。
- [ ] HKO 表外农历在线 API 和其他中国大陆 Wi‑Fi 路由尚未通过 Pico 实测。
- [ ] 天气初次诊断中出现过一次未复现的 `TypeError`；后续正式服务请求成功，当前没有证据确认该瞬时错误的根因。
- [ ] `socket.getaddrinfo()` 没有代码级超时；设计并验证 DNS 长时间无响应时的 worker 恢复办法。
