"""Compact 128x160 layouts for the online dashboard and offline clock."""

import math

try:
    from .st7735 import rgb565
except ImportError:
    # Also allow direct imports when mpremote mounts the pico directory at /remote.
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
    return "%.1f" % valid


def _text_width(value):
    width = 0
    for character in value:
        width += 6 if ord(character) < 128 else 12
    return width


def _centered_text(canvas, y, value, color, scale=1, background=BG):
    width = _text_width(value) * scale
    canvas.text(max(0, (canvas.width - width) // 2), y, value, color, scale, background)


def _metric_card(canvas, y, name, value, color):
    card_x = 4
    card_width = 120
    card_height = 24
    canvas.fill_rect(card_x, y, card_width, card_height, CARD)
    canvas.fill_rect(card_x, y, 2, card_height, color)

    value_text, percent = _percent_text(value)
    canvas.text(10, y + 3, name, CYAN_LIGHT, background=CARD)
    canvas.text(76, y + 1, value_text, TEXT, scale=2, background=CARD)

    bar_x = 10
    bar_y = y + 18
    bar_width = 108
    canvas.fill_rect(bar_x, bar_y, bar_width, 4, TRACK)
    filled = (bar_width * percent + 50) // 100
    if filled:
        canvas.fill_rect(bar_x, bar_y, filled, 4, color)


def draw_dashboard(canvas, time_text, weekday_short, date_text, lunar_text, metrics, wifi_ok):
    """Draw the metrics-first B layout using a screen-compatible canvas."""
    canvas.fill(BG)
    canvas.text(4, 3, "PC MONITOR", CYAN_LIGHT, background=BG)
    canvas.text(100 if wifi_ok else 82, 3, "WIFI" if wifi_ok else "NO WIFI", GREEN if wifi_ok else AMBER, background=BG)

    if time_text:
        canvas.text(4, 14, time_text, TEXT, scale=2, background=BG)
    else:
        canvas.text(4, 17, "TIME SYNC", MUTED, scale=2, background=BG)

    date_line = "%s %s" % (weekday_short or "", date_text or "")
    if date_line.strip():
        canvas.text(4, 33, date_line.strip(), MUTED, background=BG)
    if lunar_text:
        _centered_text(canvas, 45, lunar_text, AMBER, background=BG)
    canvas.hline(4, 58, 120, CARD)

    _metric_card(canvas, 60, "CPU", metrics.get("cpu"), CYAN)
    _metric_card(canvas, 86, "GPU", metrics.get("gpu"), CYAN)
    _metric_card(canvas, 112, "RAM", metrics.get("ram"), AMBER)

    canvas.hline(4, 140, 120, CARD)
    canvas.text(4, 143, "ETH", MUTED, background=BG)
    rx = _rate_text(metrics.get("rx"))
    tx = _rate_text(metrics.get("tx"))
    canvas.text(4, 151, "RX %sMB/s" % rx, TEXT, background=BG)
    canvas.text(64, 151, "TX %sMB/s" % tx, TEXT, background=BG)


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


def draw_clock(canvas, time_text, date_text, weekday_full, lunar_text, wifi_ok, time_synced):
    """Draw the digital-clock B layout without showing an invalid RTC date."""
    canvas.fill(BG)
    canvas.text(4, 5, "PC OFFLINE", MUTED, background=BG)
    canvas.text(100 if wifi_ok else 82, 5, "WIFI" if wifi_ok else "NO WIFI", GREEN if wifi_ok else AMBER, background=BG)

    if not _valid_time_text(time_text):
        _centered_text(canvas, 56, "时间同步中", AMBER, background=BG)
        canvas.hline(8, 137, 112, CARD)
        _centered_text(canvas, 146, "NTP WAIT UTC+8", MUTED, background=BG)
        return

    time_width = _text_width(time_text) * 2
    canvas.text(max(0, (canvas.width - time_width) // 2), 42, time_text, TEXT, scale=2, background=BG)
    if date_text:
        _centered_text(canvas, 78, date_text, MUTED, background=BG)
    if weekday_full:
        _centered_text(canvas, 97, weekday_full, CYAN_LIGHT, background=BG)
    if lunar_text:
        _centered_text(canvas, 118, lunar_text, AMBER, background=BG)

    canvas.hline(8, 137, 112, CARD)
    _centered_text(canvas, 146, "NTP SYNC UTC+8" if time_synced else "NTP NOT SYNCED", MUTED, background=BG)
