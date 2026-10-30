"""NTP client and UTC+8 clock formatting for MicroPython on RP2."""

import struct
import time


NTP_HOSTS = ("ntp.aliyun.com", "ntp1.aliyun.com", "ntp2.aliyun.com")
NTP_PORT = 123
NTP_TIMEOUT_SECONDS = 1.5
NTP_UNIX_OFFSET = 2_208_988_800
NTP_ERA_SECONDS = 1 << 32
UTC_OFFSET_SECONDS = 8 * 60 * 60
RESYNC_INTERVAL_MS = 6 * 60 * 60 * 1000
_SYNC_RETRY_MS = (30_000, 60_000, 300_000, 900_000)
_TICKS_PERIOD = 1 << 30
_WEEKDAYS_SHORT = ("周一", "周二", "周三", "周四", "周五", "周六", "周日")
_WEEKDAYS_FULL = ("星期一", "星期二", "星期三", "星期四", "星期五", "星期六", "星期日")


def ticks_diff(ticks1, ticks2):
    half_period = _TICKS_PERIOD // 2
    return ((ticks1 - ticks2 + half_period) % _TICKS_PERIOD) - half_period


def ntp_seconds_to_unix(ntp_seconds, approximate_unix=None):
    """Convert the 32-bit NTP transmit seconds to the nearest known era."""
    if not isinstance(ntp_seconds, int) or ntp_seconds < 0 or ntp_seconds >= NTP_ERA_SECONDS:
        raise ValueError("NTP 秒字段超出范围")
    if approximate_unix is None or approximate_unix < 0:
        return ntp_seconds - NTP_UNIX_OFFSET
    approximate_ntp = approximate_unix + NTP_UNIX_OFFSET
    center_era = approximate_ntp // NTP_ERA_SECONDS
    candidates = []
    for era in (center_era - 1, center_era, center_era + 1):
        if era >= 0:
            candidates.append(ntp_seconds + era * NTP_ERA_SECONDS - NTP_UNIX_OFFSET)
    return min(candidates, key=lambda candidate: abs(candidate - approximate_unix))


def _parse_ntp_response(response, approximate_unix=None):
    if not isinstance(response, (bytes, bytearray)) or len(response) < 48:
        raise ValueError("NTP 响应长度无效")
    first_byte = response[0]
    leap_indicator = first_byte >> 6
    mode = first_byte & 0x07
    stratum = response[1]
    if leap_indicator == 3 or mode != 4 or stratum < 1 or stratum > 15:
        raise ValueError("NTP 响应未同步或模式无效")
    ntp_seconds = struct.unpack("!I", response[40:44])[0]
    if ntp_seconds == 0:
        raise ValueError("NTP 时间戳为空")
    return ntp_seconds_to_unix(ntp_seconds, approximate_unix)


def _query_ntp_host(host, approximate_unix=None):
    """Query one configured NTP hostname; never uses a fixed server IP."""
    import socket

    addresses = socket.getaddrinfo(host, NTP_PORT, socket.AF_INET, socket.SOCK_DGRAM)
    if not addresses:
        raise OSError("NTP DNS 未返回 IPv4 地址")
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        sock.settimeout(NTP_TIMEOUT_SECONDS)
        sock.connect(addresses[0][-1])
        request = bytearray(48)
        request[0] = 0x23  # LI=0, VN=4, client mode=3 (RFC 5905, section 7.3).
        sock.send(request)
        response = sock.recv(512)
        return _parse_ntp_response(response, approximate_unix)
    finally:
        sock.close()


def _time_tuple(unix_seconds):
    return time.gmtime(unix_seconds)


def _rtc_tuple_from_unix(unix_seconds):
    parts = _time_tuple(unix_seconds)
    # MicroPython machine.RTC.datetime order is year, month, day, weekday,
    # hour, minute, second, subseconds (weekday is Monday=0 on RP2).
    return (parts[0], parts[1], parts[2], parts[6], parts[3], parts[4], parts[5], 0)


def _has_plausible_rtc():
    try:
        return 2020 <= time.gmtime()[0] <= 2199
    except (OSError, OverflowError, ValueError):
        return False


class TimeSynchronizer:
    """Synchronize the RTC from NTP and keep it in UTC."""

    def __init__(self, rtc=None, query_host=None, ticks_diff_fn=None):
        self._rtc = rtc
        self._query_host = query_host or _query_ntp_host
        self._ticks_diff = ticks_diff_fn or ticks_diff
        self.synced = False
        self._next_attempt_ms = None
        self._retry_count = 0

    def sync_if_due(self, now_ms, wifi_connected, force=False):
        """Attempt NTP after Wi-Fi connect, retry failures, and refresh at 6h."""
        if not wifi_connected:
            return self.synced
        if force:
            self._next_attempt_ms = now_ms
        if (
            self._next_attempt_ms is not None
            and self._ticks_diff(now_ms, self._next_attempt_ms) < 0
        ):
            return self.synced

        approximate_unix = None
        if _has_plausible_rtc():
            approximate_unix = time.time()
        for host in NTP_HOSTS:
            try:
                unix_seconds = self._query_host(host, approximate_unix)
                if unix_seconds <= 0:
                    raise ValueError("NTP 返回了非正 Unix 时间")
                candidate_year = time.gmtime(unix_seconds)[0]
                if candidate_year < 2020 or candidate_year > 2199:
                    raise ValueError("NTP 返回时间超出有效年范围")
                if self._rtc is None:
                    import machine

                    self._rtc = machine.RTC()
                self._rtc.datetime(_rtc_tuple_from_unix(unix_seconds))
                self.synced = True
                self._retry_count = 0
                self._next_attempt_ms = (now_ms + RESYNC_INTERVAL_MS) % _TICKS_PERIOD
                return True
            except Exception:
                continue

        self.synced = False
        delay_index = min(self._retry_count, len(_SYNC_RETRY_MS) - 1)
        self._retry_count += 1
        self._next_attempt_ms = (now_ms + _SYNC_RETRY_MS[delay_index]) % _TICKS_PERIOD
        return False

    def snapshot(self):
        """Return a display-ready UTC+8 snapshot, or empty fields for bad RTC."""
        if not _has_plausible_rtc():
            return {
                "year": None,
                "date": None,
                "time_text": None,
                "short_date": None,
                "full_date": None,
                "weekday_short": None,
                "weekday_full": None,
            }
        parts = time.gmtime(time.time() + UTC_OFFSET_SECONDS)
        year, month, day, hour, minute, second, weekday = parts[:7]
        return {
            "year": year,
            "date": (year, month, day),
            "time_text": "%02d:%02d:%02d" % (hour, minute, second),
            "short_date": "%02d/%02d" % (month, day),
            "full_date": "%d年%02d月%02d日" % (year, month, day),
            "weekday_short": _WEEKDAYS_SHORT[weekday],
            "weekday_full": _WEEKDAYS_FULL[weekday],
        }
