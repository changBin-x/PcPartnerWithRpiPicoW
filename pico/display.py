"""Compact 128x160 layouts for the online dashboard and offline clock."""

import math

try:
    from .font_cn import ASCII_WIDTH, CHINESE_HEIGHT, CHINESE_WIDTH
    from .st7735 import rgb565
except ImportError:
    from font_cn import ASCII_WIDTH, CHINESE_HEIGHT, CHINESE_WIDTH
    from st7735 import rgb565


BG = rgb565(7, 17, 31)
CARD = rgb565(13, 29, 46)
TEXT = rgb565(232, 245, 255)
MUTED = rgb565(155, 178, 201)
CYAN = rgb565(39, 195, 219)
CYAN_LIGHT = rgb565(134, 216, 245)
GREEN = rgb565(110, 231, 168)
AMBER = rgb565(243, 184, 91)
TRACK = rgb565(23, 48, 71)


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


def _centered_text(canvas, y, value, color, background=BG):
    width = _text_width(value)
    if width > canvas.width:
        raise ValueError("文本超出屏幕宽度: %s" % value)
    canvas.text((canvas.width - width) // 2, y, value, color, background)


def _metric_card_state(canvas, y, name, value_text, percent, color):
    card_x = 4
    card_width = 120
    card_height = 20
    canvas.fill_rect(card_x, y, card_width, card_height, CARD)
    canvas.fill_rect(card_x, y, 2, card_height, color)
    canvas.text(10, y + 1, name, CYAN_LIGHT, CARD)
    canvas.text(82, y + 1, value_text, TEXT, CARD)
    bar_x = 10
    bar_y = y + 15
    bar_width = 108
    canvas.fill_rect(bar_x, bar_y, bar_width, 3, TRACK)
    filled = (bar_width * percent + 50) // 100
    if filled:
        canvas.fill_rect(bar_x, bar_y, filled, 3, color)


def _clear_text_row(canvas, y):
    canvas.fill_rect(0, y, canvas.width, CHINESE_HEIGHT, BG)


def _dashboard_state(time_text, weekday_short, date_text, lunar_text, metrics, wifi_ok):
    has_time = bool(time_text)
    date_line = "%s %s" % (weekday_short or "", date_text or "")
    return {
        "mode": "dashboard",
        "wifi_ok": bool(wifi_ok),
        "time_ok": has_time,
        "time_text": str(time_text) if has_time else "TIME SYNC",
        "date_line": date_line.strip(),
        "lunar_text": lunar_text or "",
        "cpu": _percent_text(metrics.get("cpu")),
        "gpu": _percent_text(metrics.get("gpu")),
        "ram": _percent_text(metrics.get("ram")),
        "rx_text": "ETH RX %sMB/s" % _rate_text(metrics.get("rx")),
        "tx_text": "ETH TX %sMB/s" % _rate_text(metrics.get("tx")),
    }


def _draw_dashboard_header(canvas, wifi_ok):
    title = "PC MONITOR" if wifi_ok else "PC"
    canvas.text(4, 1, title, CYAN_LIGHT, BG)
    wifi_text = "WIFI" if wifi_ok else "NO WIFI"
    wifi_color = GREEN if wifi_ok else AMBER
    canvas.text(92 if wifi_ok else 68, 1, wifi_text, wifi_color, BG)


def _draw_dashboard_time(canvas, state):
    color = TEXT if state["time_ok"] else MUTED
    canvas.text(4, 15, state["time_text"], color, BG)


def _draw_dashboard_date(canvas, state):
    if state["date_line"]:
        canvas.text(4, 30, state["date_line"], MUTED, BG)


def _draw_dashboard_lunar(canvas, state):
    if state["lunar_text"]:
        _centered_text(canvas, 45, state["lunar_text"], AMBER, BG)


def _draw_dashboard_full(canvas, state):
    canvas.fill(BG)
    _draw_dashboard_header(canvas, state["wifi_ok"])
    _draw_dashboard_time(canvas, state)
    _draw_dashboard_date(canvas, state)
    _draw_dashboard_lunar(canvas, state)
    canvas.hline(4, 59, 120, CARD)
    cpu_text, cpu_percent = state["cpu"]
    gpu_text, gpu_percent = state["gpu"]
    ram_text, ram_percent = state["ram"]
    _metric_card_state(canvas, 61, "CPU", cpu_text, cpu_percent, CYAN)
    _metric_card_state(canvas, 83, "GPU", gpu_text, gpu_percent, CYAN)
    _metric_card_state(canvas, 105, "RAM", ram_text, ram_percent, AMBER)
    canvas.hline(4, 128, 120, CARD)
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
    if previous["lunar_text"] != current["lunar_text"]:
        _clear_text_row(canvas, 45)
        _draw_dashboard_lunar(canvas, current)
    for key, y, name, color in (
        ("cpu", 61, "CPU", CYAN),
        ("gpu", 83, "GPU", CYAN),
        ("ram", 105, "RAM", AMBER),
    ):
        if previous[key] != current[key]:
            value_text, percent = current[key]
            _metric_card_state(canvas, y, name, value_text, percent, color)
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
    lunar_text,
    metrics,
    wifi_ok,
    previous=None,
):
    """Draw the metrics layout, updating only fields changed from previous."""
    current = _dashboard_state(
        time_text, weekday_short, date_text, lunar_text, metrics, wifi_ok
    )
    if not isinstance(previous, dict) or previous.get("mode") != "dashboard":
        _draw_dashboard_full(canvas, current)
    else:
        _draw_dashboard_changes(canvas, previous, current)
    return current


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
):
    time_valid = _valid_time_text(time_text)
    return {
        "mode": "clock",
        "wifi_ok": bool(wifi_ok),
        "needs_config": bool(needs_config),
        "time_valid": time_valid,
        "time_text": time_text if time_valid else None,
        "date_text": (date_text or "") if time_valid else "",
        "weekday_full": (weekday_full or "") if time_valid else "",
        "lunar_text": (lunar_text or "") if time_valid else "",
        "footer": (
            ("NTP SYNC UTC+8" if time_synced else "NTP NOT SYNCED")
            if time_valid
            else "NTP WAIT UTC+8"
        ),
    }


def _draw_clock_header(canvas, wifi_ok):
    title = "PC OFFLINE" if wifi_ok else "PC OFF"
    canvas.text(4, 1, title, MUTED, BG)
    wifi_text = "WIFI" if wifi_ok else "NO WIFI"
    wifi_color = GREEN if wifi_ok else AMBER
    canvas.text(92 if wifi_ok else 68, 1, wifi_text, wifi_color, BG)


def _draw_clock_config(canvas, needs_config):
    if needs_config:
        canvas.text(31, 23, "WIFI CONFIG", AMBER, BG)


def _draw_clock_body(canvas, state):
    if state["time_valid"]:
        canvas.clock_text(12, 34, state["time_text"], TEXT, BG)
        if state["date_text"]:
            _centered_text(canvas, 77, state["date_text"], MUTED, BG)
        if state["weekday_full"]:
            _centered_text(canvas, 96, state["weekday_full"], CYAN_LIGHT, BG)
        if state["lunar_text"]:
            _centered_text(canvas, 115, state["lunar_text"], AMBER, BG)
    else:
        _centered_text(canvas, 60, "时间同步中", AMBER, BG)
    canvas.hline(8, 136, 112, CARD)
    _centered_text(canvas, 145, state["footer"], MUTED, BG)


def _clear_clock_body(canvas):
    canvas.fill_rect(0, 34, canvas.width, 26, BG)
    canvas.fill_rect(0, 60, canvas.width, 100, BG)


def _draw_clock_full(canvas, state):
    canvas.fill(BG)
    _draw_clock_header(canvas, state["wifi_ok"])
    _draw_clock_body(canvas, state)
    _draw_clock_config(canvas, state["needs_config"])


def _draw_clock_changes(canvas, previous, current):
    if previous["wifi_ok"] != current["wifi_ok"]:
        _clear_text_row(canvas, 0)
        _draw_clock_header(canvas, current["wifi_ok"])

    config_changed = previous["needs_config"] != current["needs_config"]
    if config_changed:
        _clear_text_row(canvas, 23)

    if previous["time_valid"] != current["time_valid"]:
        _clear_clock_body(canvas)
        _draw_clock_body(canvas, current)
        _draw_clock_config(canvas, current["needs_config"])
        return

    if current["time_valid"]:
        if previous["time_text"] != current["time_text"] or config_changed:
            canvas.clock_text(12, 34, current["time_text"], TEXT, BG)
        if previous["date_text"] != current["date_text"]:
            _clear_text_row(canvas, 77)
            if current["date_text"]:
                _centered_text(canvas, 77, current["date_text"], MUTED, BG)
        if previous["weekday_full"] != current["weekday_full"]:
            _clear_text_row(canvas, 96)
            if current["weekday_full"]:
                _centered_text(canvas, 96, current["weekday_full"], CYAN_LIGHT, BG)
        if previous["lunar_text"] != current["lunar_text"]:
            _clear_text_row(canvas, 115)
            if current["lunar_text"]:
                _centered_text(canvas, 115, current["lunar_text"], AMBER, BG)

    if previous["footer"] != current["footer"]:
        _clear_text_row(canvas, 145)
        _centered_text(canvas, 145, current["footer"], MUTED, BG)

    if config_changed:
        _draw_clock_config(canvas, current["needs_config"])
    elif current["needs_config"] and previous["time_text"] != current["time_text"]:
        _draw_clock_config(canvas, True)


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
    )
    if not isinstance(previous, dict) or previous.get("mode") != "clock":
        _draw_clock_full(canvas, current)
    else:
        _draw_clock_changes(canvas, previous, current)
    return current
