"""Pico W 电脑状态屏 · PC 实时桌面仿真。

- 渲染/解析/状态机/农历/时间换算：desktop-sim/build/sim_bridge.dll
  （由 pico_freertos_firmware 的原始 C 源码直接编译，零改动）。
- 指标采集：仓库 pc_metrics 的真实采集器（psutil + nvidia-smi 路径 +
  身份标签探测），按 UDP v2 协议真实编码后发往 127.0.0.1:8765，
  再由 DLL 里的固件真实 metrics_parser.c 解析。
- 时钟：PC 墙钟经固件 ntp_service 换算函数（UTC+8）。
- 天气：注入的模拟数据（标题栏标注）。

键盘：T = 切换主题（真实 theme_switch_advance）
      F = 暂停/恢复指标广播（演示真实 3 秒断流自动切页）
      ESC = 退出
"""

from __future__ import annotations

import ctypes
import os
import socket
import sys
import threading
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parent
sys.path.insert(0, str(REPO))  # import pc_metrics

PYGAME_SIZE = (1048, 730)  # 双面板 2x(512x640) + 中缝 24 + 提示栏 78
DISPLAY_W, DISPLAY_H = 128, 160
PANEL_SCALE = 4
PANEL_SIZE = (DISPLAY_W * PANEL_SCALE, DISPLAY_H * PANEL_SCALE)  # 512x640
FRAME_BYTES = DISPLAY_W * DISPLAY_H * 3
UDP_TARGET = ("127.0.0.1", 8765)

THEME_NAMES = ["瑞士大字阶", "复古终端", "便当网格", "工业示波", "战术机甲"]

_state_lock = threading.Lock()
_latest = {"cpu": None, "ram": None, "gpu": None, "rx": None, "tx": None,
           "weather": None}


def log(msg: str) -> None:
    print(f"[{time.strftime('%H:%M:%S')}] {msg}", flush=True)


def load_dll() -> ctypes.CDLL:
    dll_path = HERE / "build" / "sim_bridge.dll"
    if not dll_path.exists():
        raise SystemExit(f"缺少 {dll_path}，请先运行 build_sim.ps1")
    os.environ.setdefault("PICO_ASSETS_DIR",
                          str(REPO / "pico_freertos_firmware" / "assets"))
    dll = ctypes.CDLL(str(dll_path))
    dll.sim_init.restype = ctypes.c_int
    dll.sim_feed_packet.argtypes = [ctypes.c_char_p, ctypes.c_uint]
    dll.sim_feed_packet.restype = ctypes.c_int
    dll.sim_tick.restype = None
    dll.sim_advance_theme.restype = ctypes.c_int
    dll.sim_current_theme.restype = ctypes.c_int
    dll.sim_ui_mode.restype = ctypes.c_int
    dll.sim_render_frame.restype = ctypes.POINTER(
        ctypes.c_ubyte * FRAME_BYTES)
    dll.sim_render_frame_mode.argtypes = [ctypes.c_int]
    dll.sim_render_frame_mode.restype = ctypes.POINTER(
        ctypes.c_ubyte * FRAME_BYTES)
    dll.sim_stat_packets.restype = ctypes.c_longlong
    dll.sim_stat_invalid.restype = ctypes.c_longlong
    dll.sim_ip_json_to_adcode.argtypes = [ctypes.c_char_p, ctypes.c_char_p,
                                          ctypes.c_uint]
    dll.sim_ip_json_to_adcode.restype = ctypes.c_int
    dll.sim_feed_weather_json.argtypes = [ctypes.c_char_p, ctypes.c_uint]
    dll.sim_feed_weather_json.restype = ctypes.c_int
    dll.sim_mark_weather_stale.restype = None
    dll.sim_set_theme.argtypes = [ctypes.c_int]
    dll.sim_set_theme.restype = None
    rc = dll.sim_init()
    if rc != 0:
        raise SystemExit(f"sim_init 失败，返回码 {rc}")
    return dll


def load_weather_key() -> tuple[str, str]:
    """从 pico/weather_secrets.py（.gitignore 忽略）读取高德 Key 与可选 adcode。"""
    import ast
    path = REPO / "pico" / "weather_secrets.py"
    if not path.exists():
        return "", ""
    key, adcode = "", ""
    try:
        tree = ast.parse(path.read_text(encoding="utf-8-sig"))
    except (OSError, SyntaxError) as exc:
        log(f"weather_secrets.py 解析失败：{exc}")
        return "", ""
    for node in tree.body:
        if isinstance(node, ast.Assign) and len(node.targets) == 1 and \
                isinstance(node.targets[0], ast.Name):
            try:
                value = ast.literal_eval(node.value)
            except ValueError:
                continue
            if not isinstance(value, str):
                continue
            if node.targets[0].id == "AMAP_WEB_KEY":
                key = value.strip()
            elif node.targets[0].id == "CITY_OVERRIDE_ADCODE":
                adcode = value.strip()
    return key, adcode


def http_json(url: str) -> dict:
    import json
    import urllib.request
    with urllib.request.urlopen(url, timeout=10) as resp:
        return json.loads(resp.read().decode("utf-8"))


def start_weather(dll: ctypes.CDLL) -> None:
    """真实天气链路：高德 IP 定位 → adcode → 实况天气 JSON →
    固件 weather_service_parse_json（经 DLL）写入状态机。"""
    key, override = load_weather_key()
    if not key:
        log("未配置高德 Key（pico/weather_secrets.py），天气功能跳过")
        return

    def safe_msg(exc: Exception) -> str:
        text = f"{type(exc).__name__}: {exc}"
        return text.replace(key, "***")

    def work() -> None:
        adcode = override
        last_ok = 0.0
        failures = 0
        backoff = (60, 300, 900, 1800)
        while True:
            delay = 30 * 60.0  # 与固件 kWeatherRefreshMs 一致
            try:
                if not adcode:
                    ip = http_json(f"https://restapi.amap.com/v3/ip?key={key}")
                    raw = __import__("json").dumps(ip).encode("utf-8")
                    buf = ctypes.create_string_buffer(8)
                    if dll.sim_ip_json_to_adcode(raw, buf, 8) != 1:
                        raise RuntimeError("IP 定位无 adcode（高德返回空）")
                    adcode = buf.value.decode("ascii")
                    log(f"高德 IP 定位成功：adcode={adcode}")
                weather = http_json(
                    "https://restapi.amap.com/v3/weather/weatherInfo"
                    f"?key={key}&city={adcode}&extensions=base")
                raw = __import__("json").dumps(weather).encode("utf-8")
                rc = dll.sim_feed_weather_json(raw, len(raw))
                if rc != 0:
                    raise RuntimeError(f"固件解析器拒绝天气 JSON（rc={rc}）")
                lives = weather.get("lives") or [{}]
                first = lives[0] if isinstance(lives, list) else {}
                desc = (f"{first.get('city', '?')} "
                        f"{first.get('weather', '?')} "
                        f"{first.get('temperature', '?')}度")
                with _state_lock:
                    _latest["weather"] = desc
                log(f"实况天气已更新：{desc}"
                    f"（报告时间 {first.get('reporttime', '?')}）")
                last_ok = time.time()
                failures = 0
            except Exception as exc:  # 网络/解析失败：指数退避重试
                failures += 1
                delay = backoff[min(failures, len(backoff)) - 1]
                log(f"天气拉取失败（第 {failures} 次）：{safe_msg(exc)}；"
                    f"{delay} 秒后重试")
                if last_ok and time.time() - last_ok > 3600:
                    dll.sim_mark_weather_stale()
            time.sleep(delay)

    t = threading.Thread(target=work, name="weather", daemon=True)
    t.start()
    return t


def detect_interface() -> str:
    """取第一个 Up 且有 IPv4 的物理网卡名（供真实采集器使用）。"""
    import psutil
    import socket as s
    addrs = psutil.net_if_addrs()
    stats = psutil.net_if_stats()
    virtual_hints = ("loopback", "loop", "isatap", "tun", "tap", "veth",
                     "bluetooth", "wan miniport", "teredo")

    def usable(name: str) -> bool:
        if not any(a.family == s.AF_INET and a.address for a in addrs[name]):
            return False
        st = stats.get(name)
        return st is not None and st.isup

    def physical(name: str) -> bool:
        return not any(h in name.lower() for h in virtual_hints)

    physicals = [n for n in sorted(addrs) if usable(n) and physical(n)]
    if physicals:
        return physicals[0]
    fallback = [n for n in sorted(addrs) if usable(n)]
    return fallback[0] if fallback else "以太网"


def start_sender(pause_event: threading.Event) -> threading.Thread:
    """真实采集链路：pc_metrics 采集 -> v2 编码 -> UDP 环回。"""
    from pc_metrics.collectors import MetricsCollector
    from pc_metrics.labels import resolve_identity_labels
    from pc_metrics.protocol import encode_packet

    os.environ.setdefault("PICO_ETHERNET_INTERFACE", detect_interface())
    interface = os.environ["PICO_ETHERNET_INTERFACE"]

    def work() -> None:
        identity = resolve_identity_labels()
        collector = MetricsCollector(interface)
        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        seq = 0
        log(f"采集线程启动：网卡={interface} 身份="
            f"{identity['cpu_label']}/{identity['gpu_label']}/"
            f"{identity['ram_label']}")
        while True:
            pause_event.wait()
            try:
                sample = collector.sample()
            except Exception as exc:  # 网卡暂时消失等场景：等一拍再试
                log(f"采样异常：{exc}")
                time.sleep(1.0)
                continue
            if sample is None:  # 首拍建基线（真实行为：不发包）
                time.sleep(1.0)
                continue
            with _state_lock:
                for key in ("cpu", "gpu", "ram", "rx", "tx"):
                    _latest[key] = sample.get(key)
            try:
                packet = encode_packet(
                    seq=seq,
                    sampled_at=int(time.time()),
                    cpu=sample["cpu"],
                    gpu=sample["gpu"],
                    ram=sample["ram"],
                    rx=sample["rx"],
                    tx=sample["tx"],
                    cpu_label=identity["cpu_label"],
                    gpu_label=identity["gpu_label"],
                    ram_label=identity["ram_label"],
                )
                sock.sendto(packet, UDP_TARGET)
                seq += 1
            except Exception as exc:
                log(f"编码或发送异常：{exc}")
            time.sleep(1.0)
        sock.close()

    t = threading.Thread(target=work, name="metrics-sender", daemon=True)
    t.start()
    return t


def make_receiver() -> socket.socket:
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind(UDP_TARGET)
    sock.setblocking(False)
    return sock


def selftest(dll: ctypes.CDLL) -> int:
    """不开窗口：喂包 -> 渲染一帧存 PNG，验证 DLL 链路。"""
    from pc_metrics.protocol import encode_packet
    packet = encode_packet(
        seq=7, sampled_at=int(time.time()), cpu=37.0, gpu=None, ram=61.0,
        rx=1.2, tx=0.4, cpu_label="Sim", gpu_label="GPU", ram_label="16G",
    )
    rc = dll.sim_feed_packet(packet, len(packet))
    log(f"selftest feed rc={rc}")
    dll.sim_tick()
    buf = dll.sim_render_frame()
    out = HERE / "build" / "selftest_frame.raw"
    out.write_bytes(bytes(buf.contents))
    log(f"selftest frame -> {out.name} ({out.stat().st_size} bytes)")
    return 0 if rc == 0 else 1


def blit_panel(dll, screen, x: int, y: int, mode: int) -> None:
    """把固件渲染的 128x160 帧放大贴到双面板窗口指定位置。"""
    buf = dll.sim_render_frame_mode(mode)
    if buf is None:
        return
    surface = pygame.image.frombuffer(
        bytes(buf.contents), (DISPLAY_W, DISPLAY_H), "RGB")
    screen.blit(pygame.transform.scale(surface, PANEL_SIZE), (x, y))


def draw_hud(dll, screen, font, small, paused: bool) -> None:
    """底部提示栏：主题序号徽章 + 按键说明，当前主题高亮."""
    bar = pygame.Rect(0, PANEL_SIZE[1], PYGAME_SIZE[0],
                      PYGAME_SIZE[1] - PANEL_SIZE[1])
    pygame.draw.rect(screen, (13, 17, 26), bar)
    pygame.draw.line(screen, (38, 48, 62), (0, bar.top),
                     (PYGAME_SIZE[0], bar.top), 2)
    current = dll.sim_current_theme()
    x = 16
    for i, name in enumerate(THEME_NAMES):
        active = i == current
        badge_bg = (45, 212, 191) if active else (30, 38, 50)
        badge_fg = (10, 14, 20) if active else (125, 138, 154)
        tag = f"{i + 1} {name}"
        chip = pygame.Surface((font.size(tag)[0] + 18, 30))
        chip.fill(badge_bg)
        screen.blit(chip, (x, bar.top + 10))
        screen.blit(font.render(tag, True, badge_fg), (x + 9, bar.top + 17))
        x += font.size(tag)[0] + 18 + 8
    hint = f"1-5 直选主题 · T 轮换 · F 暂停/恢复指标（3 秒断流演示） · ESC 退出"
    screen.blit(small.render(hint, True, (125, 138, 154)),
                (16, bar.top + 52))
    if paused:
        warn = "⏸ 指标广播已暂停：右面板看真实超时切页效果"
        screen.blit(small.render(warn, True, (245, 166, 35)),
                    (PYGAME_SIZE[0] - small.size(warn)[0] - 16, bar.top + 17))


def main() -> int:
    args = sys.argv[1:]
    dll = load_dll()
    if "--selftest" in args:
        return selftest(dll)

    global pygame
    import pygame  # 延迟导入：--selftest 路径不需要窗口栈

    pygame.init()
    screen = pygame.display.set_mode(PYGAME_SIZE)
    pygame.display.set_caption("Pico W 状态屏仿真 · 双面板")
    font = pygame.font.SysFont("microsoftyahei", 15, bold=True)
    small = pygame.font.SysFont("microsoftyahei", 13)
    key_to_theme = {
        getattr(pygame, f"K_{i}"): i - 1 for i in range(1, len(THEME_NAMES) + 1)
    }

    pause_event = threading.Event()
    pause_event.set()
    start_sender(pause_event)
    start_weather(dll)
    rx = make_receiver()
    log("双面板窗口已启动：左侧仪表盘 / 右侧时钟页")

    clock = pygame.time.Clock()
    running = True
    title_at = 0.0
    while running:
        paused = not pause_event.is_set()
        for event in pygame.event.get():
            if event.type == pygame.QUIT:
                running = False
            elif event.type == pygame.KEYDOWN:
                if event.key == pygame.K_ESCAPE:
                    running = False
                elif event.key == pygame.K_t:
                    theme = dll.sim_advance_theme()
                    log(f"主题 -> {THEME_NAMES[theme]}")
                elif event.key in key_to_theme:
                    dll.sim_set_theme(key_to_theme[event.key])
                    log(f"主题 -> {THEME_NAMES[key_to_theme[event.key]]}")
                elif event.key == pygame.K_f:
                    if pause_event.is_set():
                        pause_event.clear()
                        log("广播已暂停（真实 3 秒超时切页）")
                    else:
                        pause_event.set()
                        log("广播已恢复")

        while True:
            try:
                data, _addr = rx.recvfrom(1024)
            except BlockingIOError:
                break
            dll.sim_feed_packet(data, len(data))

        dll.sim_tick()
        blit_panel(dll, screen, 0, 0, 0)           # 左：仪表盘（固定页面 A）
        blit_panel(dll, screen, 512 + 24, 0, 1)    # 右：时钟页（固定页面 B）
        draw_hud(dll, screen, font, small, paused)
        pygame.display.flip()

        now = time.time()
        if now - title_at >= 1.0:
            title_at = now
            theme = THEME_NAMES[dll.sim_current_theme()]
            with _state_lock:
                cpu, ram, gpu, weather = (_latest["cpu"], _latest["ram"],
                                          _latest["gpu"], _latest.get("weather"))
            cap = (f"PicoW状态屏双面板 · {theme} · CPU "
                   f"{cpu if cpu is not None else '--'}% · GPU "
                   f"{gpu if gpu is not None else '--'}% · 内存 "
                   f"{ram if ram is not None else '--'}%")
            if weather:
                cap += f" · {weather}"
            pygame.display.set_caption(cap)

        clock.tick(50)

    log("仿真退出。")
    pygame.quit()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
