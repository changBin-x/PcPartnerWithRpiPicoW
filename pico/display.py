"""Compact 128x160 Material Design 3 layouts for the online dashboard and offline clock."""

import math

try:
    from .font_cn import (
        ASCII_WIDTH,
        CHINESE_HEIGHT,
        CHINESE_WIDTH,
        WEATHER_DIGIT_WIDTH,
    )
    from .st7735 import rgb565
except ImportError:
    from font_cn import (
        ASCII_WIDTH,
        CHINESE_HEIGHT,
        CHINESE_WIDTH,
        WEATHER_DIGIT_WIDTH,
    )
    from st7735 import rgb565


# Material Design 3 Dark Theme Surface & Tonal Tokens (RGB565)
BG = rgb565(17, 19, 24)                  # 0x1083 - Surface (#111318)
SURFACE_CONTAINER = rgb565(30, 32, 37)   # 0x1904 - Surface Container (#1E2025)
SURFACE_CONTAINER_HIGH = rgb565(40, 42, 47)  # 0x2945 - Surface Container High (#282A2F)
TRACK = rgb565(51, 53, 58)               # 0x31A7 - Track / Outline (#33353A)
CARD = SURFACE_CONTAINER                 # Backward-compatible alias

# M3 Typography & Content Colors
TEXT = rgb565(226, 226, 233)             # 0xE71C - On Surface (#E2E2E9)
MUTED = rgb565(144, 144, 154)            # 0x8C93 - On Surface Variant (#90909A)
OUTLINE = rgb565(72, 74, 80)             # Outline border (#484A50)

# M3 Accent Roles
PRIMARY = rgb565(168, 199, 250)          # 0xAE3F - Primary 冰湖蓝 (#A8C7FA)
SECONDARY = rgb565(160, 211, 206)        # 0xA699 - Secondary 水鸭绿 (#A0D3CE)
TERTIARY = rgb565(242, 190, 141)         # 0xF5F1 - Tertiary 柔暖杏 (#F2BE8D)
ERROR = rgb565(242, 184, 181)            # 0xF5D6 - Error 珊瑚粉红 (#F2B8B5)

# Color compatibility aliases
CYAN = PRIMARY
CYAN_LIGHT = SECONDARY
GREEN = SECONDARY
AMBER = TERTIARY

ICON_WIDTH = 16
ICON_HEIGHT = 14
ICON_ACTIVE = PRIMARY
ICON_INACTIVE = ERROR

# 16x14 四级灰度掩码，高半字节在前，每像素 4 bit，共 112 字节，直接放在 flash 中。
ICON_PC = (
    b"\x00\x04\x44\x44\x44\x44\x40\x00\x01\xef\xff\xff\xff\xff\xfe\x10"
    b"\x07\xf4\x44\x44\x44\x44\x4f\x70\x08\xf0\x00\x00\x00\x00\x0f\x80"
    b"\x08\xf0\x00\x00\x00\x00\x0f\x80\x08\xf0\x00\x00\x00\x00\x0f\x80"
    b"\x08\xf0\x00\x00\x00\x00\x0f\x80\x08\xf0\x00\x00\x00\x00\x0f\x80"
    b"\x02\xef\xff\xff\xff\xff\xfe\x20\x00\x28\x88\x8f\xf8\x88\x82\x00"
    b"\x00\x00\x00\x0f\xf0\x00\x00\x00\x00\x00\x8f\xff\xff\xf8\x00\x00"
    b"\x00\x00\x24\x44\x44\x42\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
)
ICON_WIFI = (
    b"\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x37\x8a\xa8\x73\x00\x00"
    b"\x00\x6d\xff\xff\xff\xff\xd6\x00\x2d\xfe\x95\x30\x03\x59\xef\xd2"
    b"\x0c\x81\x00\x24\x42\x00\x18\xc0\x00\x02\x9e\xff\xff\xe9\x20\x00"
    b"\x00\x3f\xfd\x88\x88\xdf\xf3\x00\x00\x08\x40\x00\x00\x04\x80\x00"
    b"\x00\x00\x04\xab\xba\x40\x00\x00\x00\x00\x4f\xfd\xdf\xf4\x00\x00"
    b"\x00\x00\x06\x20\x02\x60\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    b"\x00\x00\x00\x09\x90\x00\x00\x00\x00\x00\x00\x08\x80\x00\x00\x00"
)


def _safe_percent(value):
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        return None
    if isinstance(value, float) and not math.isfinite(value):
        return None
    if value < 0 or value > 100:
        return None
    return value


def _safe_rate(value):
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        return None
    if isinstance(value, float) and not math.isfinite(value):
        return None
    if value < 0:
        return None
    return value


def _percent_text(value):
    valid = _safe_percent(value)
    if valid is None:
        return "--", 0
    return "%d%%" % int(valid + 0.5), int(valid + 0.5)


def _rate_text(value):
    valid = _safe_rate(value)
    if valid is None:
        return "--"
    if valid >= 100000000:
        return ">1e8"
    if valid >= 1000:
        mantissa, exponent = ("%.1e" % valid).split("e")
        return "%se%d" % (mantissa, int(exponent))
    return "%.1f" % valid


def _text_width(value):
    width = 0
    for character in value:
        width += ASCII_WIDTH if ord(character) < 128 else CHINESE_WIDTH
    return width


def _clean_weather_text(value):
    if not isinstance(value, str):
        return ""
    return "".join(
        character
        for character in value.strip()
        if ord(character) >= 32 and ord(character) != 127
    )


def _clip_text(value, max_width):
    result = ""
    for character in value:
        if _text_width(result + character) > max_width:
            break
        result += character
    if result != value and max_width >= 16:
        while result and _text_width(result + "..") > max_width:
            result = result[:-1]
        result += ".."
    return result


def _fit_text(value, max_width):
    """Keep the longest leading run that fits; never append an ellipsis."""
    result = ""
    for character in value:
        if _text_width(result + character) > max_width:
            break
        result += character
    return result


def _draw_card_container(canvas, x, y, width, height, color=SURFACE_CONTAINER):
    """Draw an M3 container card with chamfered corners for a soft look."""
    canvas.fill_rect(x + 1, y, width - 2, 1, color)
    canvas.fill_rect(x, y + 1, width, height - 2, color)
    canvas.fill_rect(x + 1, y + height - 1, width - 2, 1, color)


def _weather_state(weather):
    if isinstance(weather, str):
        clean = _clean_weather_text(weather)
        return {
            "has_data": False,
            "city": "",
            "condition": "",
            "temperature": None,
            "is_stale": False,
            "status": clean if clean else "天气暂不可用",
        }
    if not isinstance(weather, dict):
        weather = {}
    has_data = bool(weather.get("has_data"))
    if has_data:
        city = _clean_weather_text(weather.get("city", ""))
        condition = _clean_weather_text(weather.get("weather", ""))
        temperature = weather.get("temperature")
        if (
            not isinstance(temperature, (int, float))
            or isinstance(temperature, bool)
            or (isinstance(temperature, float) and not math.isfinite(temperature))
            or temperature < -80
            or temperature > 70
        ):
            has_data = False
        elif not condition:
            has_data = False
        else:
            return {
                "has_data": True,
                "city": city,
                "condition": condition,
                "temperature": int(temperature + 0.5) if temperature >= 0 else int(temperature - 0.5),
                "is_stale": bool(weather.get("is_stale")),
                "status": "",
            }
    status = weather.get("status", "天气暂不可用")
    if status not in ("天气获取中", "天气暂不可用", "天气未配置", "WIFI CONFIG"):
        status = "天气暂不可用"
    return {
        "has_data": False,
        "city": "",
        "condition": "",
        "temperature": None,
        "is_stale": False,
        "status": status,
    }


def _draw_weather_row(canvas, y, state, panel=False):
    """Draw M3 weather capsule container."""
    container_x = 4
    container_w = 120
    container_h = 15
    bg_color = SURFACE_CONTAINER if panel else BG
    if panel:
        _draw_card_container(canvas, container_x, y - 1, container_w, container_h, bg_color)
    else:
        canvas.fill_rect(0, y, canvas.width, CHINESE_HEIGHT, bg_color)

    if not state["has_data"]:
        text = _clip_text(state["status"], container_w - 8)
        canvas.text(container_x + 4, y, text, MUTED, bg_color)
        return

    city_x = container_x + 4
    if state["is_stale"]:
        canvas.text(city_x, y, "旧", TERTIARY, bg_color)
        city_x += CHINESE_WIDTH

    city = state["city"]
    for suffix in ("市", "地区", "自治州", "盟"):
        if city.endswith(suffix) and len(city) > len(suffix):
            city = city[:-len(suffix)]
            break
    temperature_text = "%d℃" % state["temperature"]
    temperature_width = (
        len(str(state["temperature"])) * WEATHER_DIGIT_WIDTH + CHINESE_WIDTH
    )
    temperature_x = container_x + container_w - 4 - temperature_width
    left_end = temperature_x - 6
    condition = state["condition"]
    # Reserve the 4 px gap plus at least one Chinese glyph for the weather
    # condition before handing out space to the city, so a long city name or a
    # sub-zero temperature can never clip the condition away entirely.
    if condition:
        city_limit = min(42, left_end - CHINESE_WIDTH - 4 - city_x)
    else:
        city_limit = min(42, left_end - city_x)
    city_text = _fit_text(city, city_limit) if city_limit >= CHINESE_WIDTH else ""
    condition_x = city_x + _text_width(city_text) + (4 if city_text else 0)
    condition_width = max(0, left_end - condition_x)
    condition_text = _fit_text(condition, condition_width) if condition else ""

    if city_text:
        canvas.text(city_x, y, city_text, MUTED, bg_color)
    if condition_text:
        canvas.text(condition_x, y, condition_text, SECONDARY, bg_color)

    canvas.weather_temperature(temperature_x, y, temperature_text, PRIMARY, bg_color)


def _centered_text(canvas, y, value, color, background=BG):
    text = _clip_text(value, canvas.width)
    width = _text_width(text)
    canvas.text((canvas.width - width) // 2, y, text, color, background)


def _metric_card_state(canvas, y, name, value_text, percent, color):
    """Draw M3 resource metric card."""
    card_x = 4
    card_width = 120
    card_height = 20
    _draw_card_container(canvas, card_x, y, card_width, card_height, SURFACE_CONTAINER)
    # Accent color indicator bar
    canvas.fill_rect(card_x + 1, y + 2, 2, card_height - 4, color)
    canvas.text(10, y + 1, name, color, SURFACE_CONTAINER)
    canvas.text(82, y + 1, value_text, TEXT, SURFACE_CONTAINER)
    bar_x = 10
    bar_y = y + 15
    bar_width = 108
    canvas.fill_rect(bar_x, bar_y, bar_width, 3, TRACK)
    filled = (bar_width * percent + 50) // 100
    if filled:
        canvas.fill_rect(bar_x, bar_y, filled, 3, color)


def _clear_text_row(canvas, y):
    canvas.fill_rect(0, y, canvas.width, CHINESE_HEIGHT, BG)


def _draw_status_icons(canvas, pc_active, wifi_ok, y=0):
    """Draw PC and Wi-Fi indicator icons at the top."""
    canvas.bitmap(
        4, y, ICON_WIDTH, ICON_HEIGHT, ICON_PC,
        ICON_ACTIVE if pc_active else ICON_INACTIVE, BG,
    )
    canvas.bitmap(
        canvas.width - 4 - ICON_WIDTH, y, ICON_WIDTH, ICON_HEIGHT, ICON_WIFI,
        ICON_ACTIVE if wifi_ok else ICON_INACTIVE, BG,
    )


# ---------------------------------------------------------------------------
# Dashboard Mode
# ---------------------------------------------------------------------------

def _dashboard_state(time_text, weekday_short, date_text, weather, metrics, wifi_ok):
    has_time = bool(time_text)
    date_line = "%s %s" % (weekday_short or "", date_text or "")
    return {
        "mode": "dashboard",
        "wifi_ok": bool(wifi_ok),
        "time_ok": has_time,
        "time_text": str(time_text) if has_time else "TIME SYNC",
        "date_line": date_line.strip(),
        "weather": _weather_state(weather),
        "cpu": _percent_text(metrics.get("cpu")),
        "gpu": _percent_text(metrics.get("gpu")),
        "ram": _percent_text(metrics.get("ram")),
        "rx_text": "ETH RX %sMB/s" % _rate_text(metrics.get("rx")),
        "tx_text": "ETH TX %sMB/s" % _rate_text(metrics.get("tx")),
    }


def _draw_dashboard_header(canvas, wifi_ok):
    _draw_status_icons(canvas, True, wifi_ok, y=0)


def _draw_dashboard_time(canvas, state):
    color = TEXT if state["time_ok"] else MUTED
    canvas.text(4, 15, state["time_text"], color, BG)


def _draw_dashboard_date(canvas, state):
    if state["date_line"]:
        canvas.text(4, 30, state["date_line"], MUTED, BG)


def _draw_dashboard_full(canvas, state):
    canvas.fill(BG)
    _draw_dashboard_header(canvas, state["wifi_ok"])
    _draw_dashboard_time(canvas, state)
    _draw_dashboard_date(canvas, state)
    _draw_weather_row(canvas, 45, state["weather"], panel=True)
    canvas.hline(4, 59, 120, TRACK)
    cpu_text, cpu_percent = state["cpu"]
    gpu_text, gpu_percent = state["gpu"]
    ram_text, ram_percent = state["ram"]
    _metric_card_state(canvas, 61, "CPU", cpu_text, cpu_percent, PRIMARY)
    _metric_card_state(canvas, 83, "GPU", gpu_text, gpu_percent, SECONDARY)
    ram_color = ERROR if ram_percent > 90 else TERTIARY
    _metric_card_state(canvas, 105, "RAM", ram_text, ram_percent, ram_color)
    canvas.hline(4, 128, 120, TRACK)
    canvas.text(0, 130, state["rx_text"], TEXT, BG)
    canvas.text(0, 145, state["tx_text"], TEXT, BG)


def _draw_dashboard_changes(canvas, previous, current):
    if previous["wifi_ok"] != current["wifi_ok"]:
        _clear_text_row(canvas, 0)
        _draw_dashboard_header(canvas, current["wifi_ok"])
    if previous["time_ok"] != current["time_ok"] or previous["time_text"] != current["time_text"]:
        _clear_text_row(canvas, 15)
        _draw_dashboard_time(canvas, current)
    if previous["date_line"] != current["date_line"]:
        _clear_text_row(canvas, 30)
        _draw_dashboard_date(canvas, current)
    if previous["weather"] != current["weather"]:
        _draw_weather_row(canvas, 45, current["weather"], panel=True)
    if previous["cpu"] != current["cpu"]:
        val, pct = current["cpu"]
        _metric_card_state(canvas, 61, "CPU", val, pct, PRIMARY)
    if previous["gpu"] != current["gpu"]:
        val, pct = current["gpu"]
        _metric_card_state(canvas, 83, "GPU", val, pct, SECONDARY)
    if previous["ram"] != current["ram"]:
        val, pct = current["ram"]
        ram_color = ERROR if pct > 90 else TERTIARY
        _metric_card_state(canvas, 105, "RAM", val, pct, ram_color)
    if previous["rx_text"] != current["rx_text"]:
        _clear_text_row(canvas, 130)
        canvas.text(0, 130, current["rx_text"], TEXT, BG)
    if previous["tx_text"] != current["tx_text"]:
        _clear_text_row(canvas, 145)
        canvas.text(0, 145, current["tx_text"], TEXT, BG)


def draw_dashboard(
    canvas,
    time_text,
    weekday_short,
    date_text,
    weather,
    metrics,
    wifi_ok,
    previous=None,
):
    """Draw the metrics layout, updating only fields changed from previous."""
    current = _dashboard_state(
        time_text, weekday_short, date_text, weather, metrics, wifi_ok
    )
    if not isinstance(previous, dict) or previous.get("mode") != "dashboard":
        _draw_dashboard_full(canvas, current)
    else:
        _draw_dashboard_changes(canvas, previous, current)
    return current


# ---------------------------------------------------------------------------
# Clock Mode
# ---------------------------------------------------------------------------

def _valid_time_text(value):
    return (
        isinstance(value, str)
        and len(value) == 8
        and value[2] == ":"
        and value[5] == ":"
        and value[:2].isdigit()
        and value[3:5].isdigit()
        and value[6:].isdigit()
    )


def _clock_state(
    time_text,
    date_text,
    weekday_full,
    lunar_text,
    wifi_ok,
    time_synced,
    needs_config,
    weather,
):
    time_valid = _valid_time_text(time_text)
    weather_state = _weather_state(weather)
    if needs_config:
        weather_state = {
            "has_data": False,
            "city": "",
            "condition": "",
            "temperature": None,
            "is_stale": False,
            "status": "WIFI CONFIG",
        }
    return {
        "mode": "clock",
        "wifi_ok": bool(wifi_ok),
        "needs_config": bool(needs_config),
        "time_valid": time_valid,
        "time_text": time_text if time_valid else None,
        "date_text": (date_text or "") if time_valid else "",
        "weekday_full": (weekday_full or "") if time_valid else "",
        "lunar_text": (lunar_text or "") if time_valid else "",
        "weather": weather_state,
        "time_synced": bool(time_synced),
    }


def _draw_clock_header(canvas, wifi_ok):
    _draw_status_icons(canvas, False, wifi_ok, y=0)


def _draw_clock_body(canvas, state):
    if state["time_valid"]:
        canvas.clock_text(12, 34, state["time_text"], TEXT, BG)
        # M3 复合日历大卡片 (4, 70, 120, 80)
        _draw_card_container(canvas, 4, 70, 120, 80, SURFACE_CONTAINER)
        if state["date_text"]:
            _centered_text(canvas, 77, state["date_text"], TEXT, SURFACE_CONTAINER)
        if state["weekday_full"]:
            _draw_card_container(canvas, 36, 94, 56, 18, SURFACE_CONTAINER_HIGH)
            _centered_text(canvas, 96, state["weekday_full"], SECONDARY, SURFACE_CONTAINER_HIGH)
        if state["lunar_text"]:
            _centered_text(canvas, 115, state["lunar_text"], TERTIARY, SURFACE_CONTAINER)
    else:
        # 优雅骨架与占位
        canvas.text(32, 40, "--:--:--", MUTED, BG)
        _draw_card_container(canvas, 4, 70, 120, 80, SURFACE_CONTAINER)
        _draw_card_container(canvas, 24, 94, 80, 20, SURFACE_CONTAINER_HIGH)
        _centered_text(canvas, 97, "时间同步中", SECONDARY, SURFACE_CONTAINER_HIGH)


def _draw_clock_full(canvas, state):
    canvas.fill(BG)
    _draw_clock_header(canvas, state["wifi_ok"])
    _draw_weather_row(canvas, 15, state["weather"], panel=True)
    _draw_clock_body(canvas, state)


def _draw_clock_changes(canvas, previous, current):
    if previous["wifi_ok"] != current["wifi_ok"]:
        _clear_text_row(canvas, 0)
        _draw_clock_header(canvas, current["wifi_ok"])

    if previous["weather"] != current["weather"]:
        _draw_weather_row(canvas, 15, current["weather"], panel=True)

    if previous["time_valid"] != current["time_valid"]:
        canvas.fill_rect(0, 34, canvas.width, 126, BG)
        _draw_clock_body(canvas, current)
        return

    if current["time_valid"]:
        if previous["time_text"] != current["time_text"]:
            canvas.clock_text(12, 34, current["time_text"], TEXT, BG)
        if previous["date_text"] != current["date_text"]:
            canvas.fill_rect(6, 75, 116, CHINESE_HEIGHT, SURFACE_CONTAINER)
            if current["date_text"]:
                _centered_text(canvas, 77, current["date_text"], TEXT, SURFACE_CONTAINER)
        if previous["weekday_full"] != current["weekday_full"]:
            _draw_card_container(canvas, 36, 94, 56, 18, SURFACE_CONTAINER_HIGH)
            if current["weekday_full"]:
                _centered_text(canvas, 96, current["weekday_full"], SECONDARY, SURFACE_CONTAINER_HIGH)
        if previous["lunar_text"] != current["lunar_text"]:
            canvas.fill_rect(6, 115, 116, CHINESE_HEIGHT, SURFACE_CONTAINER)
            if current["lunar_text"]:
                _centered_text(canvas, 115, current["lunar_text"], TERTIARY, SURFACE_CONTAINER)


def draw_clock(
    canvas,
    time_text,
    date_text,
    weekday_full,
    lunar_text,
    wifi_ok,
    time_synced,
    previous=None,
    needs_config=False,
    weather=None,
):
    """Draw the clock layout, updating only fields changed from previous."""
    current = _clock_state(
        time_text,
        date_text,
        weekday_full,
        lunar_text,
        wifi_ok,
        time_synced,
        needs_config,
        weather,
    )
    if not isinstance(previous, dict) or previous.get("mode") != "clock":
        _draw_clock_full(canvas, current)
    else:
        _draw_clock_changes(canvas, previous, current)
    return current
