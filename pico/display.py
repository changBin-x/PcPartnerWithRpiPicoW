"""Compact 128x160 layouts for the online dashboard and offline clock."""

import math

try:
    from .font_cn import ASCII_WIDTH, CHINESE_WIDTH, LINE_BASELINE
    from .st7735 import rgb565
except ImportError:
    from font_cn import ASCII_WIDTH, CHINESE_WIDTH, LINE_BASELINE
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


def _metric_card(canvas, y, name, value, color):
    card_x = 4
    card_width = 120
    card_height = 20
    canvas.fill_rect(card_x, y, card_width, card_height, CARD)
    canvas.fill_rect(card_x, y, 2, card_height, color)
    value_text, percent = _percent_text(value)
    canvas.text(10, y + 1, name, CYAN_LIGHT, CARD)
    canvas.text(82, y + 1, value_text, TEXT, CARD)
    bar_x = 10
    bar_y = y + 15
    bar_width = 108
    canvas.fill_rect(bar_x, bar_y, bar_width, 3, TRACK)
    filled = (bar_width * percent + 50) // 100
    if filled:
        canvas.fill_rect(bar_x, bar_y, filled, 3, color)


def draw_dashboard(canvas, time_text, weekday_short, date_text, lunar_text, metrics, wifi_ok):
    """Draw the metrics-first B layout using a screen-compatible canvas."""
    canvas.fill(BG)
    canvas.text(4, 1, "PC MONITOR", CYAN_LIGHT, BG)
    wifi_text = "WIFI" if wifi_ok else "NO WIFI"
    wifi_color = GREEN if wifi_ok else AMBER
    canvas.text(92 if wifi_ok else 68, 1, wifi_text, wifi_color, BG)
    if time_text:
        canvas.text(4, 15, time_text, TEXT, BG)
    else:
        canvas.text(4, 15, "TIME SYNC", MUTED, BG)
    date_line = "%s %s" % (weekday_short or "", date_text or "")
    if date_line.strip():
        canvas.text(4, 30, date_line.strip(), MUTED, BG)
    if lunar_text:
        _centered_text(canvas, 45, lunar_text, AMBER, BG)
    canvas.hline(4, 59, 120, CARD)
    _metric_card(canvas, 61, "CPU", metrics.get("cpu"), CYAN)
    _metric_card(canvas, 83, "GPU", metrics.get("gpu"), CYAN)
    _metric_card(canvas, 105, "RAM", metrics.get("ram"), AMBER)
    canvas.hline(4, 128, 120, CARD)
    canvas.text(0, 130, "ETH RX %sMB/s" % _rate_text(metrics.get("rx")), TEXT, BG)
    canvas.text(0, 145, "ETH TX %sMB/s" % _rate_text(metrics.get("tx")), TEXT, BG)


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
    """Draw the clock-first B layout with a dedicated large numeric face."""
    canvas.fill(BG)
    canvas.text(4, 1, "PC OFFLINE", MUTED, BG)
    wifi_text = "WIFI" if wifi_ok else "NO WIFI"
    wifi_color = GREEN if wifi_ok else AMBER
    canvas.text(92 if wifi_ok else 68, 1, wifi_text, wifi_color, BG)
    if not _valid_time_text(time_text):
        _centered_text(canvas, 60, "时间同步中", AMBER, BG)
        canvas.hline(8, 136, 112, CARD)
        _centered_text(canvas, 145, "NTP WAIT UTC+8", MUTED, BG)
        return
    canvas.clock_text(12, 34, time_text, TEXT, BG)
    if date_text:
        _centered_text(canvas, 77, date_text, MUTED, BG)
    if weekday_full:
        _centered_text(canvas, 96, weekday_full, CYAN_LIGHT, BG)
    if lunar_text:
        _centered_text(canvas, 115, lunar_text, AMBER, BG)
    canvas.hline(8, 136, 112, CARD)
    _centered_text(canvas, 145, "NTP SYNC UTC+8" if time_synced else "NTP NOT SYNCED", MUTED, BG)
