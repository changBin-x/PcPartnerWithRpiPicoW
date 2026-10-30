# Pico Wi-Fi 电脑状态屏实现计划

> **面向 AI 代理的工作者：** 必需子技能：使用 `executing-plans` 按任务执行本计划。每个任务结束后回看验收项；步骤用复选框记录。

**目标：** 在 ST7735S 128×160 屏上实现电脑指标仪表盘，并在电脑指标断开时显示可独立运行的全屏时钟。

**架构：** Windows Python 采集器每秒读取系统指标并向本地子网 UDP 广播；Pico 的 MicroPython 程序连接路由器 Wi-Fi、接收数据并绘制屏幕。Pico 使用板载香港天文台 1901–2100 年历表保证常规农历显示，在线 HKO API 作校验或越界补充；`main.py` 上电自动启动。

**技术栈：** Windows Python 3、`psutil`、NVIDIA `nvidia-smi`、UDP/JSON；Pico MicroPython、SPI、ST7735S、NTP、HTTPS/JSON；标准库 `unittest`。

---

## 执行前已知情况

- 已确认屏幕型号、分辨率、连线、界面 B/B、指标、单位、1 秒刷新、UDP 广播、3 秒断线切换、NTP/UTC+8、农历格式和 `main.py` 持久启动要求。完整决策见 [设计说明](../specs/2026-10-31-pico-wifi-metrics-display-design.md)。
- 当前 `D:\Projects\pc_partner` 文件系统仅见 `README.md` 与会话原型文件；IDE 标签里的 `src/main.cpp`、`test_st7735s.py` 当前不在磁盘树中。任务 1 先重查实际路径，若源文件重新可见则先读后改；若仍不可见，按本计划在本目录新建独立目录，不覆盖 IDE 外部文件。
- 用户已明确批准初始化本地 Git 并创建 worktree。基线提交只包含 `.gitignore`、`README.md` 与 `docs/` 设计/计划文件；`.superpowers/` 浏览器会话、`.vscode/` 本机设置、`.worktrees/`、本地 Wi-Fi 密码和缓存均忽略。功能代码在 `.worktrees/pico-wifi-metrics-display` 的 `codex/pico-wifi-metrics-display` 分支开发；完成后按分支收尾流程整合。
- COM4 实测为 MicroPython 1.29.0，`sys.implementation` 标识 `Raspberry Pi Pico W with RP2040`；用户核对实体丝印为 Pico W。电脑采集器只发包，不建立 TCP/HTTP 入站服务。

## 目标文件清单

| 路径 | 职责 |
|---|---|
| `README.md` | Wi-Fi 配置、电脑启动、Pico 部署、排障和演示步骤 |
| `.gitignore` | 排除 `pico/wifi_secrets.py`、缓存和本机生成日志 |
| `requirements-host.txt` | Windows 采集器的 `psutil` 依赖 |
| `requirements-build.txt` | 本地农历/中文点阵资源生成工具依赖 |
| `pc_metrics/__init__.py` | 主机端 Python 包标识 |
| `pc_metrics/config.py` | UDP 端口、网卡覆盖配置和广播参数 |
| `pc_metrics/protocol.py` | v1 JSON 编码、长度限制和数据范围校验 |
| `pc_metrics/collectors.py` | CPU、GPU、RAM、Ethernet Rx/Tx 采集 |
| `pc_metrics/broadcaster.py` | 绑定有线网卡并发送 UDP 子网广播 |
| `pc_metrics/host.py` | 主机端启动循环、采样节奏、日志和退出处理 |
| `tools/start_metrics.ps1` | Windows 手动启动入口 |
| `tools/build_lunar_table.py` | 从 HKO 官方逐日对照文本生成紧凑板载年表 |
| `tools/build_font.py` | 将指定 CJK 字体生成 Pico 可用的字形子集 |
| `pico/main.py` | MicroPython 自动启动入口和顶层异常恢复 |
| `pico/__init__.py` | 让源目录可作为 MicroPython/Python 包导入 |
| `pico/app.py` | 在线仪表盘/断连时钟状态机和一秒刷新 |
| `pico/st7735.py` | ST7735S SPI 初始化与区域绘制原语 |
| `pico/display.py` | B/B 两种布局、色彩和指标绘制 |
| `pico/font_cn.py` | 中文星期、月日、干支和状态提示点阵字形 |
| `pico/wifi.py` | Wi-Fi 连接、重连和状态读取 |
| `pico/udp_receiver.py` | UDP 广播监听、JSON 校验和最新包维护 |
| `pico/time_sync.py` | 多个 NTP 主机重试、UTC RTC 和 UTC+8 格式化 |
| `pico/lunar.py` | 板载年表查询、HKO API 解析、繁转简和日期校验 |
| `pico/hko_root_ca.pem` | 香港邮政 Root CA 3 的 PEM 信任锚，仅用于 HKO HTTPS 证书链校验 |
| `pico/lunar_table.bin` | 由官方历表构建的 1901–2100 紧凑数据文件 |
| `pico/wifi_secrets.example.py` | 不含真实密码的本地配置模板 |
| `tests/test_protocol.py` | 主机协议和异常输入测试 |
| `tests/__init__.py` | 允许按模块路径运行 unittest |
| `tests/test_collectors.py` | 主机速率换算与采集器测试 |
| `tests/test_broadcaster.py` | IPv4 广播目标计算与非法网卡配置测试 |
| `tests/test_lunar.py` | 农历边界、干支、繁简和已知日期测试 |
| `tests/test_lunar_api.py` | HKO API 响应转换、本地优先和支持年份边界测试 |
| `tests/test_time_sync.py` | NTP 时间戳、服务器响应、RTC 调度测试 |
| `tests/test_wifi.py` | Wi-Fi 无凭据、重连和非阻塞超时测试 |
| `tests/test_app_state.py` | 新包、断线阈值和恢复状态测试 |

## 任务 1：确认工作区、硬件和部署工具

**文件：** 无；只读检查。

- [x] **步骤 1：重新定位当前源文件和 Git 状态**

运行：

```powershell
Get-ChildItem -Force
Test-Path src/main.cpp
Test-Path test_st7735s.py
git rev-parse --is-inside-work-tree
```

结果：`src/main.cpp` 与 `test_st7735s.py` 在工作树中均不存在；Git worktree 位于 `codex/pico-wifi-metrics-display`。未删除或覆盖 IDE 外部文件。

- [x] **步骤 2：读取 COM4 的板型和 MicroPython 版本**

在 worktree 的隔离 `.venv` 中安装 mpremote 后运行：

```powershell
python -m venv .venv
.\.venv\Scripts\python.exe -m pip install mpremote
.\.venv\Scripts\python.exe -m mpremote connect COM4 exec "import sys; print(sys.implementation); print(sys.version)"
```

结果：输出 RP2 端口、`Raspberry Pi Pico W with RP2040`、MicroPython 1.29.0；未访问 Wi-Fi 配置。

- [x] **步骤 3：创建独立目录骨架**

仅在确认源文件缺失后创建：

```powershell
New-Item -ItemType Directory -Force pc_metrics,pico,tools,tests | Out-Null
```

结果：`pc_metrics/`、`pico/`、`tools/`、`tests/` 均已创建，已有 `README.md` 保留。

## 任务 2：固定 UDP v1 契约并测试数据校验

**文件：** 创建 `pc_metrics/protocol.py`、`tests/test_protocol.py`。

- [x] **步骤 1：先写协议测试**

创建空文件 `tests/__init__.py` 与 `pc_metrics/__init__.py`，再在 `tests/test_protocol.py` 加入：

```python
import unittest

from pc_metrics.protocol import decode_packet, encode_packet


class ProtocolTests(unittest.TestCase):
    def test_round_trip_accepts_gpu_unavailable(self):
        packet = encode_packet(
            seq=8,
            sampled_at=1793404800,
            cpu=12.5,
            gpu=None,
            ram=61.0,
            rx=2.3,
            tx=0.4,
        )
        self.assertEqual(decode_packet(packet)["gpu"], None)
        self.assertEqual(decode_packet(packet)["seq"], 8)

    def test_rejects_out_of_range_usage(self):
        with self.assertRaises(ValueError):
            encode_packet(1, 1793404800, 101, 0, 50, 0, 0)

    def test_rejects_oversized_packet(self):
        with self.assertRaises(ValueError):
            decode_packet(b"{" + b" " * 512 + b"}")


if __name__ == "__main__":
    unittest.main()
```

- [x] **步骤 2：运行测试确认初始失败**

运行：`python -m unittest tests.test_protocol -v`  
预期：因 `pc_metrics.protocol` 尚不存在而失败。

- [x] **步骤 3：实现协议函数**

创建 `pc_metrics/protocol.py`，按以下核心逻辑实现：

```python
import json
import math

MAX_PACKET_BYTES = 512
PACKET_VERSION = 1


def _number(value, minimum, maximum=None, allow_none=False):
    if value is None and allow_none:
        return None
    if isinstance(value, bool):
        raise ValueError("布尔值不是有效数字")
    if not isinstance(value, (int, float)):
        raise ValueError("字段必须是数字")
    if isinstance(value, float) and not math.isfinite(value):
        raise ValueError("字段超出有效范围")
    if value < minimum:
        raise ValueError("字段超出有效范围")
    if maximum is not None and value > maximum:
        raise ValueError("字段超出有效范围")
    return value


def encode_packet(seq, sampled_at, cpu, gpu, ram, rx, tx):
    if isinstance(seq, bool) or not isinstance(seq, int) or seq < 0:
        raise ValueError("seq 必须是非负整数")
    if isinstance(sampled_at, bool) or not isinstance(sampled_at, int) or sampled_at <= 0:
        raise ValueError("ts 必须是正整数 Unix 时间")
    payload = {
        "v": PACKET_VERSION,
        "seq": seq,
        "ts": sampled_at,
        "cpu": _number(cpu, 0, 100),
        "gpu": _number(gpu, 0, 100, allow_none=True),
        "ram": _number(ram, 0, 100),
        "rx": _number(rx, 0),
        "tx": _number(tx, 0),
    }
    data = json.dumps(payload, separators=(",", ":")).encode("utf-8")
    if len(data) > MAX_PACKET_BYTES:
        raise ValueError("UDP 数据包过长")
    return data


def decode_packet(data):
    if not isinstance(data, bytes) or not data or len(data) > MAX_PACKET_BYTES:
        raise ValueError("UDP 数据包长度或类型无效")
    try:
        payload = json.loads(data.decode("utf-8"))
    except (UnicodeDecodeError, ValueError) as exc:
        raise ValueError("UDP 数据包不是有效 JSON") from exc
    expected = {"v", "seq", "ts", "cpu", "gpu", "ram", "rx", "tx"}
    if not isinstance(payload, dict) or set(payload) != expected:
        raise ValueError("UDP 数据包字段不匹配")
    if payload["v"] != PACKET_VERSION:
        raise ValueError("UDP 协议版本不支持")
    if isinstance(payload["seq"], bool) or not isinstance(payload["seq"], int) or payload["seq"] < 0:
        raise ValueError("seq 必须是非负整数")
    if isinstance(payload["ts"], bool) or not isinstance(payload["ts"], int) or payload["ts"] <= 0:
        raise ValueError("ts 必须是正整数 Unix 时间")
    _number(payload["cpu"], 0, 100)
    _number(payload["gpu"], 0, 100, allow_none=True)
    _number(payload["ram"], 0, 100)
    _number(payload["rx"], 0)
    _number(payload["tx"], 0)
    return payload
```

协议字段固定为 `v=1`、`seq`、`ts`、`cpu`、`gpu`、`ram`、`rx`、`tx`；单包上限 512 字节。CPU/GPU/RAM 必须是 0–100 的有限数字，GPU 允许 `None`，Rx/Tx 必须是非负有限数字；未知版本、缺字段、JSON 错误、越界和超长一律抛 `ValueError`。

- [x] **步骤 4：运行协议测试确认通过**

运行：`python -m unittest tests.test_protocol -v`  
结果：21 项协议测试通过；覆盖字段缺失/多余、非法 UTF-8、重复键、特殊常量、边界包长、GPU 缺失和大整数精确往返。Spec 与代码质量复核均通过。

## 任务 3：实现 Windows 指标采集

**文件：** 创建 `requirements-host.txt`、`pc_metrics/collectors.py`、`tests/test_collectors.py`。

- [x] **步骤 1：声明并安装主机依赖**

`requirements-host.txt` 内容：

```text
psutil>=5.9,<8
```

运行：`python -m pip install -r requirements-host.txt`  
结果：隔离 `.venv` 安装 `psutil 7.2.2` 成功；GPU 命令仍由已安装的 NVIDIA 驱动提供。

- [x] **步骤 2：先写纯函数测试**

在 `tests/test_collectors.py` 加入：

```python
import unittest

from pc_metrics.collectors import bytes_delta_to_mb_s, validate_percent


class CollectorTests(unittest.TestCase):
    def test_network_rate_uses_decimal_megabytes(self):
        self.assertEqual(bytes_delta_to_mb_s(2_500_000, 1.0), 2.5)

    def test_network_rate_rejects_counter_reset(self):
        self.assertIsNone(bytes_delta_to_mb_s(-1, 1.0))

    def test_percent_rejects_invalid_values(self):
        self.assertIsNone(validate_percent(float("nan")))
        self.assertIsNone(validate_percent(101))


if __name__ == "__main__":
    unittest.main()
```

- [x] **步骤 3：实现采集函数与采样器**

创建 `pc_metrics/collectors.py`。按下面实现纯函数，再实现 `MetricsCollector.sample()`：

```python
import math


def bytes_delta_to_mb_s(delta_bytes, elapsed_seconds):
    if delta_bytes < 0 or elapsed_seconds <= 0:
        return None
    return round(delta_bytes / elapsed_seconds / 1_000_000, 1)


def validate_percent(value):
    try:
        number = float(value)
    except (TypeError, ValueError):
        return None
    if not math.isfinite(number) or not 0 <= number <= 100:
        return None
    return round(number, 1)
```

`sample()` 用 `psutil.cpu_percent(interval=None)`、`psutil.virtual_memory().percent`、指定 Ethernet 的 `psutil.net_io_counters(pernic=True)`；速率使用单调时钟实测间隔。GPU 用 `subprocess.run(["nvidia-smi", "--query-gpu=utilization.gpu", "--format=csv,noheader,nounits"], capture_output=True, text=True, timeout=0.7, check=True)` 查询 GPU 0，失败或输出不是 0–100 数值时设为 `None`。第一次调用只建立网络计数基线并返回尚未就绪状态，不能把首个瞬时采样伪装为 0 MB/s。

- [x] **步骤 4：运行采集器测试**

运行：`python -m unittest tests.test_collectors -v`  
结果：6 项采集器测试通过；首次采样仅建立计数基线，GPU 不可用时保留其他指标。

- [x] **步骤 5：列出本机网卡并确定有线接口选择**

运行：

```powershell
Get-NetRoute -DestinationPrefix 0.0.0.0/0 | Sort-Object RouteMetric | Select-Object -First 5 InterfaceIndex,RouteMetric,NextHop
Get-NetAdapter | Select-Object ifIndex,Name,InterfaceDescription,Status,PhysicalMediaType
```

结果：默认路由 metric 最低的是 FlClash 虚拟隧道（ifIndex 60）；真实有线口为 `以太网`（ifIndex 24）。接口有 `192.168.10.9/24` 与 `192.168.2.1/24` 两个地址，网关 `192.168.10.1` 匹配前者。配置明确指定 `以太网`，广播目标选择 `192.168.10.255`。

## 任务 4：实现广播发送与手动启动器

**文件：** 创建 `pc_metrics/config.py`、`pc_metrics/broadcaster.py`、`pc_metrics/host.py`、`tools/start_metrics.ps1`。

- [x] **步骤 1：实现配置和广播地址单元测试**

在 `tests/test_broadcaster.py` 加入：

```python
import unittest

from pc_metrics.broadcaster import get_broadcast_address


class BroadcasterTests(unittest.TestCase):
    def test_calculates_directed_broadcast(self):
        self.assertEqual(
            get_broadcast_address("192.168.10.9", "255.255.255.0"),
            "192.168.10.255",
        )

    def test_rejects_invalid_interface_address(self):
        with self.assertRaises(ValueError):
            get_broadcast_address("", "255.255.255.0")


if __name__ == "__main__":
    unittest.main()
```

运行：`python -m unittest tests.test_broadcaster -v`。预期：因函数尚不存在而失败。

- [x] **步骤 2：实现单包发送器**

创建 `pc_metrics/broadcaster.py`：

```python
import ipaddress
import socket


def get_broadcast_address(address, netmask):
    try:
        network = ipaddress.IPv4Network(
            f"{address}/{netmask}", strict=False
        )
    except (TypeError, ValueError) as exc:
        raise ValueError("有线网卡必须提供有效 IPv4 地址和子网掩码") from exc
    return str(network.broadcast_address)


def send_packet(sock, packet, broadcast_address, port=8765):
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
    return sock.sendto(packet, (broadcast_address, port))
```

运行：`python -m unittest tests.test_broadcaster -v`。预期：两项测试通过。发送器不调用 `bind()` 建立接收端口。

- [x] **步骤 3：实现主机循环和退出行为**

`pc_metrics/host.py` 每秒按单调时钟安排一次采样，调用协议编码和广播发送；落后时跳过积压采样而不突发补发。`Ctrl+C` 时关闭套接字并输出停止信息。GPU 为空时继续发送其余指标。

- [x] **步骤 4：创建 PowerShell 手动启动入口**

`tools/start_metrics.ps1` 先将工作目录切到项目根目录，再启动 `python -m pc_metrics.host`；Python 不存在或依赖缺失时显示安装命令。执行：`powershell -ExecutionPolicy Bypass -File tools/start_metrics.ps1`。预期：终端显示已选 Ethernet 名称、IPv4/广播目标和 1 秒启动状态。

- [x] **步骤 5：用本机 UDP 接收器验证格式与方向**

运行临时测试接收器监听 UDP 8765，确认收包字段、序号和周期；停止测试接收器后，主机发送程序仍可运行。确认没有新增 TCP 监听端口。若同一路由器下 Pico 不收包，先检查广播域、访客 Wi-Fi 和客户端隔离，不添加 TCP 入站服务。

结果：临时接收器收到有效 v1 数据包，序号递增、周期约 0.98 秒；停止接收器后发送端仍运行。发送端没有建立 TCP/HTTP 或 UDP 接收服务。广播与采集/协议合计 37 项定向测试通过。

## 任务 5：构建本地 HKO 农历年表

**文件：** 创建 `requirements-build.txt`、`tools/build_lunar_table.py`、`pico/lunar_table.bin`、`tests/test_lunar.py`。

- [x] **步骤 1：锁定官方数据输入与构建工具**

`requirements-build.txt` 仅使用 `requests` 作为构建期依赖。生成器从 HKO 官方公历/农历对照页面下载 1901–2100 年逐年纯文本表（索引：`https://www.hko.gov.hk/tc/gts/time/conversion1_text.htm`；年度文件：`https://www.hko.gov.hk/tc/gts/time/calendar/text/files/T{year}c.txt`）。这是与年度 PDF 同源的官方逐日数据，字段更便于确定性解析和日期连续性校验；不把请求库放进 Pico 运行时。

- [x] **步骤 2：写入固定日期回归测试**

在 `tests/test_lunar.py` 加入：

```python
import unittest
from datetime import date

from pico.lunar import format_lunar_date


class LunarTests(unittest.TestCase):
    def test_hko_known_date_is_simplified_and_has_no_prefix(self):
        self.assertEqual(format_lunar_date(date(2026, 10, 31)), "丙午年九月廿二")


if __name__ == "__main__":
    unittest.main()
```

- [x] **步骤 3：实现年表生成器**

运行接口固定为：`python tools/build_lunar_table.py --start 1901 --end 2100 --output pico/lunar_table.bin`。生成器下载并解析 HKO 逐日文本表，将每个公历日映射压缩为干支年索引、月序/闰月标记和日序；验证 1901–2100 每个公历日均只有一条记录、没有日期缺口或重复，失败时以非零码退出。

- [x] **步骤 4：实现 MicroPython 查询和显示格式化**

`pico/lunar.py` 从 `lunar_table.bin` 查找日期，返回简体农历月份/日期及干支年，例如 `丙午年九月廿二`。闰月以“闰”字标记，不加“农历”前缀；年表范围外返回 `None`，由 API 层处理。对 2026-10-31、春节前一天、春节当天、闰月首尾和每种月长增加断言。

- [x] **步骤 5：生成数据并运行日历测试**

运行：

```powershell
python -m pip install -r requirements-build.txt
python tools/build_lunar_table.py --start 1901 --end 2100 --output pico/lunar_table.bin
python -m unittest tests.test_lunar -v
```

结果：HKO 1901–2100 年逐日文件全部下载并校验，生成 73,049 条记录、219,157 字节年表；7 项 `tests.test_lunar` 测试通过。2025-07-25、2026-02-16、2026-02-17、2026-10-31 四个在线 API 支持日期与本地格式结果一致。HKO 注明 2057-09-28、2089-09-04、2097-08-07 新月交界日可能相差一天；本地数据遵循其公布版本。

## 任务 6：实现 ST7735S 驱动、中文字形与 B/B 界面

**文件：** `tools/build_font.py`、`pico/font_cn.py`、`pico/st7735.py`、`pico/display.py`、`tests/test_display.py`。

- [x] **步骤 1：生成共享基线的抗锯齿字形**

  `tools/build_font.py --font C:\Windows\Fonts\msyh.ttc --ascii-font C:\Windows\Fonts\segoeui.ttf --size 14 --output pico/font_cn.py` 使用 4 倍分辨率渲染，再以 LANCZOS 缩小并量化为 4-bit coverage。中文格为 14×14、ASCII 为 8×14，共享 11px 基线；时钟数字单独生成为 14×26。构建自检校验指定字库覆盖字符和压缩长度，缺字立即失败。

- [x] **步骤 2：实现 ST7735S RGB565 混色与小区域 SPI 写入**

  保留 GP18/GP19 SPI0、GP17 CS、GP20 RST、GP21 DC 的既有硬件映射。coverage 在驱动中与 RGB565 前景/背景混色；文字按垂直条带绘制且每个 SPI 临时块不超过 3,072 字节；矩形填充使用单行缓存，不分配整屏 framebuffer。正文英文/中文不再缩放 5×7 点阵。

- [x] **步骤 3：实现在线仪表盘 B 布局**

  保留时间、短日期/周几、干支农历、CPU/GPU/RAM 卡片和 ETH Rx/Tx 的顺序与配色；RAM 使用琥珀色。非法指标显示 `--`，收发速率保持 MB/s 格式，过长数值用紧凑科学记数显示以避免裁切。

- [x] **步骤 4：实现断连时钟 B 布局**

  HH:MM:SS 使用专用平滑数字字库；冒号以两个抗锯齿圆点绘制，围绕中线对称。公历日期、完整中文星期、全中文干支农历、Wi-Fi 与 NTP 状态继续使用共享基线正文格。

- [x] **步骤 5：在 COM4 运行固定样例并保留验证边界**

  通过 `mpremote connect COM4 mount pico exec ...` 临时挂载驱动，分别绘制仪表盘和时钟固定样例；不复制或覆盖设备根目录文件。运行调用返回成功后，检查方向、实际字边缘、对比度和人眼观感仍需现场观察者完成。

- [ ] **用户目视验收：确认新字体、冒号与混排基线**

  代码路径、SPI 写入、帧耗时及堆占用已实测，但开发工具不能读取 LCD 像素。请用户检查改版后的实体画面，并确认字缘是否更平滑、日期/农历基线是否齐、冒号是否自然、两种布局有无截断；收到反馈后再决定是否需要微调。

### Task 6 视觉迭代记录（2026-10-31）

- 使用微软雅黑和 Segoe UI 重新生成 56 个中文、71 个 ASCII、10 个大时钟数字字形。字库源文件为 42,385 字节；覆盖字形数据合计 11,284 字节。
- `python -m unittest tests.test_display -v`：14 项通过，覆盖 coverage 编解码、RGB565 中间色、星期一/初一和状态/日期文案字符覆盖、混排字宽、时钟点状冒号及 128×160 边界。
- 合并逐字 SPI 窗口前单帧 dashboard 为 949 ms；按文本合并窗口、限制块大小后，COM4 连续轮显两轮得到 dashboard 539/540 ms、clock 455/445 ms，最后停在 dashboard。无 MicroPython 或 SPI 写入异常。
- 显示模块导入后空闲堆 154,016 B；dashboard、clock 绘制并 `gc.collect()` 后分别为 153,888 B、153,904 B。临时 SPI 块最大 3,072 B；显示模块未分配整屏缓冲。联网/TLS 与完整应用堆峰值属于后续集成验收。
- `mpremote fs ls :` 在测试前后均返回空根目录，临时挂载没有留下板载文件。
- 冒号圆点覆盖率按 4×4 子采样的命中比例计算，测试检查存在中间 coverage；两种布局在 Wi-Fi 在线/离线状态下都验证标题与状态不重叠且位于 128×160 边界内。断网时标题缩为“PC” / “PC OFF”，保留“NO WIFI”状态。
- 本环境不能读取屏幕像素或观察面板；字形是否显著更平滑、基线是否舒服、颜色和留白是否合眼仍待用户目视确认。本记录不代表视觉验收通过。

## 任务 7：实现 Wi-Fi、NTP、HKO HTTPS 和 UDP 状态机

**文件：** 创建 `pico/wifi.py`、`pico/udp_receiver.py`、`pico/time_sync.py`、更新 `pico/lunar.py`、创建 `pico/app.py`。

- [x] **步骤 1：增加本地 Wi-Fi 模板和秘密忽略规则**

创建 `pico/wifi_secrets.example.py`，内容只有空值：

```python
SSID = ""
PASSWORD = ""
```

在 `.gitignore` 加入 `pico/wifi_secrets.py`、`__pycache__/`、`*.pyc` 和 `logs/`。`pico/wifi.py` 检查配置为空时在屏幕显示配置提示，不把 SSID/密码写入日志。

- [x] **步骤 2：实现 NTP 多源与本地时钟行为**

`pico/time_sync.py` 按 `ntp.aliyun.com`、`ntp1.aliyun.com`、`ntp2.aliyun.com` 顺序尝试 UDP NTP；开机及 Wi-Fi 重连时同步，成功后每 6 小时复核。失败时保留有效 RTC 并标为未同步；首次时间未知时不格式化伪造日期。所有用户可见时间按 UTC+8。

- [x] **步骤 3：实现 HKO API 安全请求和边界处理**

`pico/lunar.py` 对 `https://data.weather.gov.hk/weatherAPI/opendata/lunardate.php?date=YYYY-MM-DD` 发 HTTPS GET，设置 2 秒套接字超时和 SNI，先校时再用 `ssl.CERT_REQUIRED` 与 CA 信任链验证证书。DNS `getaddrinfo()` 不受套接字超时约束。先显示本地年表结果，再在开机/日期变化时低频请求；只解析 `LunarYear`/`LunarDate`，繁体转简体、移除生肖和标点。API 失败时继续用有效本地表；不按秒请求，重试间隔逐步增加。

- [x] **步骤 4：实现 UDP 接收和三秒超时状态**

`pico/udp_receiver.py` 在 UDP 8765 上接收广播，使用 `ujson` 解析，检查版本、包长、字段、数值范围与序号。`pico/app.py` 用 `time.ticks_ms()`/`time.ticks_diff()` 计算收包新鲜度：有效包到达进入在线仪表盘；超过 3000 ms 切换时钟；新包到达立即恢复。UDP 等待必须非阻塞或短超时，确保时间和绘图持续刷新。

- [x] **步骤 5：用主机单元测试覆盖状态机和数据异常**

在 `tests/test_app_state.py` 验证：刚收到数据为 `dashboard`、2999 ms 为 `dashboard`、3000 ms 及以上为 `clock`、恢复有效包后为 `dashboard`、无效包不刷新超时。运行：`python -m unittest discover -s tests -v`。预期：协议、采集器、日历和状态机测试全部通过。

### Task 7 实施证据与限制（2026-10-31）

- HKO Open Data API 文档 1.13（2025-09）规定公历/农历转换接口只支持当前年份及之后两年；实现会在请求前拒绝不支持的年份和无效公历日期。接口为 `https://data.weather.gov.hk/weatherAPI/opendata/lunardate.php?date=YYYY-MM-DD`，返回 `LunarYear` 与 `LunarDate`。
- COM4 上 MicroPython 1.29.0 只读探测确认 `SSLContext`、`CERT_REQUIRED`、`load_verify_locations(cadata=...)` 和 `server_hostname` 接口可用；Root CA 3 PEM 已在该固件上成功加载。实现只信任香港邮政官方 Root CA 3，不把中间 CA 设为信任锚。
- Windows `requests` 当前请求 HKO 返回 HTTP 200，且用 Root CA 3 单独校验证书链时通过；观察到的服务端证书 issuer 为 `Hongkong Post e-Cert SSL CA 3 - 17`。香港邮政官方 [CA 状态页](https://www.hongkongpost.gov.hk/product/download/root/index_text.html) 明确称该 Sub CA 于 2026-06-11 已撤销。MicroPython 1.29.0 公布的 SSL API 支持 CA 链、`CERT_REQUIRED` 和 SNI，但未提供 CRL/OCSP 校验设置；因此代码只声称链和主机名校验，不声称完成证书撤销检查。HKO 本地表优先，在线结果不覆盖有效本地日期；API/TLS 失败时继续显示本地结果。
- HKO/NTP 套接字连接与 I/O 分别设置 2 秒/1.5 秒套接字超时；`socket.getaddrinfo()` 是阻塞 DNS 调用，不受套接字超时约束，因此无法保证总请求耗时上限，DNS 阶段可能暂时暂停主循环。HKO 请求安排在首帧绘制后，失败只保留本地农历；目标路由上的实际延迟仍需现场测量。
- Windows `curl.exe` 与 `requests` 使用的网络代理路径不同：`curl.exe` 解析到 FlClash 的 `198.18.0.72` 后 5 秒超时，不能当作 Pico 路由器网络的可达性结论。COM4 仅验证了 SSL API 和 CA 文件加载，未进行 Pico Wi-Fi、NTP 或 HKO HTTPS 实际连接；因为本地 `pico/wifi_secrets.py` 不存在，本次只检查文件是否存在，没有读取内容。任务 8 必须由用户填写本地 Wi-Fi 配置后，在目标路由器上验证，并分别记录 API 与撤销状态结果；不得把主机 HTTP 200 或 SSL 接口存在写成 Pico 现场访问已通过。

## 任务 8：接入自动启动、部署并进行现场网络验收

**文件：** 创建 `pico/main.py`、`tools/deploy_pico.ps1`，更新 `README.md`。

- [x] **步骤 1：建立启动入口与崩溃可恢复行为**

`pico/main.py` 使用 `from pico.app import run`；顶层捕获普通异常，在屏幕显示简短错误状态并定时软重启，避免 Wi-Fi/API 单次错误退出到 REPL。创建空的 `pico/__init__.py` 作为包入口。Ctrl+C 仍允许维护时中断。不得把永久阻塞初始化放进 `boot.py`。

- [x] **步骤 2：实现明确文件清单的 COM4 部署脚本**

`tools/deploy_pico.ps1` 在缺少 `pico/wifi_secrets.py` 时停止并提示用户从模板复制后本地填写；不输出该文件内容。先在 Pico 内部创建 `/pico`，将 `pico/__init__.py`、应用模块、年表、字形和本机配置分别复制到 `/pico/<文件名>`，再把 `pico/main.py` 复制为设备根目录 `/main.py`。上传后执行一次硬件 reset。

- [x] **步骤 3：写入操作文档**

README 写明：PC Ethernet/Pico Wi-Fi 同属可广播局域网；填写 SSID/密码；安装 `requirements-host.txt`；运行 PowerShell 启动器；部署到 COM4；观察两个屏幕状态；停止主机程序；现场网络检查和常见故障。文档不含任何真实凭据。

- [x] **步骤 4：运行完整主机侧自动化检查**

运行：`python -m unittest discover -s tests -v`  
结果：91 项主机侧测试通过；`python -m compileall pc_metrics tests tools`、Pico 模块导入检查、PowerShell parser 与 `git diff --check` 均通过。部署脚本在缺少本地 Wi-Fi 配置时于任何 COM/设备操作前安全退出。

- [ ] **步骤 5：验证用户指定的大陆现场网络**

在用户实际展示路由器上，先由 Windows Python 采集器发 UDP，再观察 Pico 收包；确认 DNS 解析、多个 NTP 主机中至少一个校时成功、Pico HTTPS 到 HKO API 返回 200 且证书校验通过。若 API 出站被拦，确认本地表仍正确显示并记录为“该网络在线 API 未通过”，不得宣称已验证可用。

- [ ] **步骤 6：验证断连、恢复和断电持久性**

停止主机采集器，确认约 3 秒后切到时钟；重新启动，确认回仪表盘；拔掉 Pico 电源后重新上电，不启动 Thonny，确认 `main.py` 自动运行、Wi-Fi 恢复且时间/日期有效。

## 全局验收命令

```powershell
python -m unittest discover -s tests -v
python -m compileall pc_metrics tests tools
```

硬件验收还需现场观察屏幕、UDP 跨有线/Wi-Fi 广播、Pico HTTPS 证书校验和真实断电启动；主机单元测试不能替代这些检查。

## 资料依据

- [MicroPython RP2 Wi-Fi 与 socket 文档](https://docs.micropython.org/en/latest/rp2/quickref.html)
- [MicroPython main.py 启动文档](https://docs.micropython.org/en/v1.25.0/reference/reset_boot.html)
- [MicroPython SSL 证书验证与 SNI 文档](https://docs.micropython.org/en/latest/library/ssl.html)
- [HKO API 文档](https://www.hko.gov.hk/en/weatherAPI/doc/files/HKO_Open_Data_API_Documentation.pdf)
- [HKO 1901–2100 年度历表索引](https://www.hko.gov.hk/tc/gts/time/conversion.htm)
- [阿里云公网 NTP 服务器文档](https://help.aliyun.com/zh/ecs/user-guide/alibaba-cloud-ntp-server)

## 实施前检查清单

- [ ] 复核 `src/main.cpp`、`test_st7735s.py` 是否位于当前工作区；有则先检查并保留既有实现。
- [ ] 复核 COM4 板型、MicroPython 版本和屏幕静态绘制。
- [ ] 确认电脑有线和 Pico Wi-Fi 在允许局域网广播的同一广播域。
- [ ] 由用户本地填写 Wi-Fi 配置，不在聊天或日志中读取、回显密码。
- [ ] 在计划展示的中国大陆 Wi-Fi 上通过 NTP 与 HKO API 实机验收。
