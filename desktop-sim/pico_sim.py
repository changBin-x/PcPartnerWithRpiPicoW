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
      1-5 = 直选主题，ESC = 退出
      Alt/Ctrl 组合的全局热键（Ctrl+Alt+1-5/T/F）在窗口失焦时也可用
鼠标：点击底部主题徽章切主题，点击右侧按钮暂停/恢复（远程客户端最稳）
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
            # 带超时的等待：暂停时仍会周期性醒来，F 键状态可被观察，
            # 也能避免旧版「暂停后永久阻塞」的隐性问题。
            while not pause_event.wait(timeout=0.5):
                pass
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


def draw_hud(dll, screen, font, small, paused: bool,
             focused: bool) -> dict[str, pygame.Rect]:
    """底部提示栏：主题徽章 + 暂停按钮 + 操作说明。

    返回可点击区域（逻辑坐标），供鼠标点击交互使用——网页/远程云电脑
    客户端下键盘焦点常被浏览器抢走，鼠标点击是更可靠的操作入口。
    """
    bar = pygame.Rect(0, PANEL_SIZE[1], PYGAME_SIZE[0],
                      PYGAME_SIZE[1] - PANEL_SIZE[1])
    pygame.draw.rect(screen, (13, 17, 26), bar)
    pygame.draw.line(screen, (38, 48, 62), (0, bar.top),
                     (PYGAME_SIZE[0], bar.top), 2)
    current = dll.sim_current_theme()
    hits: dict[str, pygame.Rect] = {}
    x = 16
    for i, name in enumerate(THEME_NAMES):
        active = i == current
        # 当前主题始终高亮；键盘接管时更亮，便于分辨“选中”与“可输入”
        badge_bg = ((45, 212, 191) if focused else (20, 150, 136)) \
            if active else (30, 38, 50)
        badge_fg = (10, 14, 20) if active else (125, 138, 154)
        tag = f"{i + 1} {name}"
        chip_w = font.size(tag)[0] + 18
        rect = pygame.Rect(x, bar.top + 10, chip_w, 30)
        chip = pygame.Surface((rect.width, rect.height))
        chip.fill(badge_bg)
        screen.blit(chip, rect.topleft)
        screen.blit(font.render(tag, True, badge_fg), (x + 9, bar.top + 17))
        hits[f"theme:{i}"] = rect
        x += chip_w + 8

    btn_w = 128
    # 暂停按钮紧跟徽章之后，保证所有可点击控件都在窗口左半侧（远程桌面
    # 里窗口右缘常被浏览器等遮挡，点击会落到别的窗口上）。
    btn = pygame.Rect(x + 12, bar.top + 10, btn_w, 30)
    btn_bg = (245, 166, 35) if paused else (38, 48, 62)
    btn_fg = (10, 14, 20) if paused else (185, 196, 210)
    pygame.draw.rect(screen, btn_bg, btn)
    label = "恢复广播" if paused else "暂停广播"
    screen.blit(font.render(label, True, btn_fg),
                (btn.centerx - font.size(label)[0] // 2, bar.top + 17))
    hits["pause"] = btn

    if paused:
        warn = "已暂停：右面板可看到 3 秒断流后自动切页"
        screen.blit(small.render(warn, True, (245, 166, 35)),
                    (btn.right + 16, bar.top + 17))

    hint = ("点击徽章切主题 / 点击按钮暂停 · 键盘 1-5 T F ESC · "
            "全局热键 Ctrl+Alt+1-5 T F")
    hint_color = (125, 138, 154) if focused else (245, 166, 35)
    if not focused:
        hint = "键盘未接管（焦点在浏览器）：请点击徽章或按钮操作；" \
               "也可用全局热键 Ctrl+Alt+1-5 / T / F"
    screen.blit(small.render(hint, True, hint_color), (16, bar.top + 52))
    return hits


def start_global_hotkeys(cmd_queue: "queue.Queue[str]") -> bool:
    """注册系统级热键 Ctrl+Alt+1-5 / T / F（不依赖窗口焦点）。

    远程/网页云电脑客户端经常把键盘焦点留在浏览器侧，窗口内按键会
    被其他程序消费；WM_HOTKEY 是系统级投递，只要云电脑系统收到键盘
    输入就生效。

    注意：WM_HOTKEY 只会投递到「调用 RegisterHotKey 的那个线程」的
    消息队列，因此注册与 GetMessageW 必须发生在同一个线程里；跨线程
    注册会导致消息被丢弃（已实测验证的坑）。
    """
    import ctypes
    from ctypes import wintypes

    user32 = ctypes.WinDLL("user32", use_last_error=True)
    MOD_ALT, MOD_CONTROL, MOD_NOREPEAT = 0x0001, 0x0002, 0x4000
    WM_HOTKEY = 0x0312

    bindings = {}
    for i in range(1, 6):
        bindings[100 + i] = f"theme:{i - 1}"
    bindings[200] = "theme:next"   # T
    bindings[201] = "pause"        # F
    vk_map = {100 + i: 0x30 + i for i in range(1, 6)}
    vk_map[200] = 0x54
    vk_map[201] = 0x46

    ready = threading.Event()
    registered: list[int] = []

    def work() -> None:
        mods = MOD_ALT | MOD_CONTROL | MOD_NOREPEAT
        for hk_id, cmd in bindings.items():
            if user32.RegisterHotKey(None, hk_id, mods, vk_map[hk_id]):
                registered.append(hk_id)
        ready.set()
        msg = wintypes.MSG()
        while user32.GetMessageW(ctypes.byref(msg), None, 0, 0) > 0:
            if msg.message == WM_HOTKEY:
                cmd = bindings.get(int(msg.wParam))
                if cmd:
                    cmd_queue.put(cmd)

    threading.Thread(target=work, name="hotkeys", daemon=True).start()
    ready.wait(2.0)
    if not registered:
        log("全局热键注册失败：Ctrl+Alt+数字/T/F 不可用（可改用窗口内按键）")
        return False
    log(f"全局热键已注册 {len(registered)} 个（Ctrl+Alt+数字/T/F，"
        f"窗口失焦也可用）")
    return True


def enable_dpi_awareness() -> None:
    """声明 DPI 感知，配合 pygame.SCALED 让画面按真实像素渲染。

    Windows 默认把 DPI 不感知的进程窗口按 150%/200% 拉伸（画面发虚），
    且 SDL 拿到的鼠标坐标会与绘制坐标错位，导致“点了徽章却没反应”。
    """
    for call in (
        lambda: ctypes.windll.shcore.SetProcessDpiAwareness(2),
        lambda: ctypes.windll.user32.SetProcessDPIAware(),
    ):
        try:
            call()
            return
        except Exception:
            continue


def main() -> int:
    args = sys.argv[1:]
    dll = load_dll()
    if "--selftest" in args:
        return selftest(dll)

    global pygame
    import pygame  # 延迟导入：--selftest 路径不需要窗口栈

    import queue

    enable_dpi_awareness()
    pygame.init()
    scaled = getattr(pygame, "SCALED", 0)
    resizable = getattr(pygame, "RESIZABLE", 0)
    try:
        # SCALED：逻辑分辨率固定 1048x730，SDL 按窗口实际像素渲染并自动把
        # 鼠标事件换算回逻辑坐标；RESIZABLE 允许拖动放大而画面/点击仍准确。
        # 配合 DPI 感知，画面不再被系统 150% 拉伸（发虚）且点击不再错位。
        screen = pygame.display.set_mode(
            PYGAME_SIZE, scaled | resizable)
    except pygame.error:
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
    start_global_hotkeys(hotkey_queue := queue.Queue())
    log("双面板窗口已启动：左侧仪表盘 / 右侧时钟页")

    clock = pygame.time.Clock()
    running = True
    title_at = 0.0
    focused = False  # 键盘焦点跟踪：失焦时高亮提示用户先点击窗口
    ev_focus_gained = getattr(pygame, "WINDOWFOCUSGAINED", None)
    ev_focus_lost = getattr(pygame, "WINDOWFOCUSLOST", None)
    last_focus_log = None
    hud_hits: dict[str, pygame.Rect] = {}
    hud_logged = False

    def toggle_pause(source: str = "按键") -> None:
        if pause_event.is_set():
            pause_event.clear()
            log(f"[{source}] 广播已暂停（真实 3 秒超时切页）")
        else:
            pause_event.set()
            log(f"[{source}] 广播已恢复")

    def apply_theme_cmd(cmd: str, source: str) -> None:
        arg = cmd.split(":", 1)[1]
        if arg == "next":
            theme = dll.sim_advance_theme()
        else:
            theme = int(arg)
            dll.sim_set_theme(theme)
        log(f"[{source}] 主题 -> {THEME_NAMES[theme]}")

    while running:
        paused = not pause_event.is_set()
        for event in pygame.event.get():
            if event.type == pygame.QUIT:
                running = False
            elif ev_focus_gained is not None and event.type == ev_focus_gained:
                focused = True
            elif ev_focus_lost is not None and event.type == ev_focus_lost:
                focused = False
            elif event.type == pygame.KEYDOWN:
                # 逐键日志：远程排查“按键到底有没有进来”
                log(f"KEYDOWN key={event.key} unicode={event.unicode!r}")
                if event.key == pygame.K_ESCAPE:
                    running = False
                elif event.key == pygame.K_t:
                    theme = dll.sim_advance_theme()
                    log(f"主题 -> {THEME_NAMES[theme]}")
                elif event.key in key_to_theme:
                    dll.sim_set_theme(key_to_theme[event.key])
                    log(f"主题 -> {THEME_NAMES[key_to_theme[event.key]]}")
                elif event.key == pygame.K_f:
                    toggle_pause()
            elif event.type == pygame.MOUSEBUTTONDOWN and \
                    event.button == 1:
                # 鼠标点击是远程/网页云电脑下最可靠的操作入口
                pos = event.pos
                hit_key = None
                for key, rect in hud_hits.items():
                    if rect.collidepoint(pos):
                        hit_key = key
                        break
                log(f"CLICK pos={pos} hit={hit_key or 'none'}")
                if hit_key == "pause":
                    toggle_pause("点击")
                elif hit_key:
                    apply_theme_cmd(hit_key, "点击")

        # 全局热键队列（Ctrl+Alt+1-5/T/F，不依赖窗口焦点）
        while True:
            try:
                cmd = hotkey_queue.get_nowait()
            except queue.Empty:
                break
            if cmd.startswith("theme:"):
                apply_theme_cmd(cmd, "热键")
            elif cmd == "pause":
                toggle_pause("热键")

        while True:
            try:
                data, _addr = rx.recvfrom(1024)
            except BlockingIOError:
                break
            dll.sim_feed_packet(data, len(data))

        dll.sim_tick()
        blit_panel(dll, screen, 0, 0, 0)           # 左：仪表盘（固定页面 A）
        blit_panel(dll, screen, 512 + 24, 0, 1)    # 右：时钟页（固定页面 B）
        hud_hits = draw_hud(dll, screen, font, small, paused, focused)
        if not hud_logged:
            hud_logged = True
            log("可点击区域（窗口内逻辑坐标）：" + " | ".join(
                f"{k}={tuple(v)}" for k, v in hud_hits.items()))
        pygame.display.flip()

        # 焦点变化写一次日志，方便远程排查（只记变更）
        if focused != last_focus_log:
            last_focus_log = focused
            log(f"键盘焦点：{'已接管' if focused else '已失去（按键将无效，需点击窗口）'}")

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
