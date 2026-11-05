"""Compact Hong Kong Observatory lunar-date lookup for MicroPython."""

try:
    import ujson as json
except ImportError:
    import json

try:
    import _thread
except ImportError:
    _thread = None

_MAGIC = b"HKL1"
_HEADER_SIZE = 10
_RECORD_SIZE = 3
_START_YEAR_FALLBACK = 1901

_STEMS = ("甲", "乙", "丙", "丁", "戊", "己", "庚", "辛", "壬", "癸")
_BRANCHES = ("子", "丑", "寅", "卯", "辰", "巳", "午", "未", "申", "酉", "戌", "亥")
_MONTH_NAMES = (
    "",
    "正月",
    "二月",
    "三月",
    "四月",
    "五月",
    "六月",
    "七月",
    "八月",
    "九月",
    "十月",
    "冬月",
    "腊月",
)
_DIGITS = ("零", "一", "二", "三", "四", "五", "六", "七", "八", "九")
_DAYS_PER_MONTH = (31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31)


def _default_table_path():
    module_file = globals().get("__file__", "")
    separator = "/" if "/" in module_file else "\\" if "\\" in module_file else ""
    if separator:
        module_dir = module_file.rsplit(separator, 1)[0]
        if module_dir:
            return module_dir + separator + "lunar_table.bin"
    return "lunar_table.bin"


def _date_parts(value):
    try:
        year = value.year
        month = value.month
        day = value.day
    except AttributeError:
        try:
            year, month, day = value[0], value[1], value[2]
        except (TypeError, IndexError, KeyError):
            return None
    if any(isinstance(part, bool) or not isinstance(part, int) for part in (year, month, day)):
        return None
    return year, month, day


def _is_leap_year(year):
    return year % 4 == 0 and (year % 100 != 0 or year % 400 == 0)


def _days_in_month(year, month):
    if month == 2 and _is_leap_year(year):
        return 29
    return _DAYS_PER_MONTH[month - 1]


def _day_index(year, month, day, start_year):
    if year < start_year or month < 1 or month > 12:
        return None
    if day < 1 or day > _days_in_month(year, month):
        return None

    previous_year = year - 1
    start_previous_year = start_year - 1
    days = 365 * (year - start_year)
    days += previous_year // 4 - start_previous_year // 4
    days -= previous_year // 100 - start_previous_year // 100
    days += previous_year // 400 - start_previous_year // 400
    for month_number in range(1, month):
        days += _days_in_month(year, month_number)
    return days + day - 1


def lookup_lunar_date(date_value, table_path=None):
    """Return (sexagenary index, month, leap flag, day), or None outside table."""
    parts = _date_parts(date_value)
    if parts is None:
        return None
    year, month, day = parts

    path = _default_table_path() if table_path is None else table_path
    source = open(path, "rb")
    try:
        header = source.read(_HEADER_SIZE)
        if len(header) != _HEADER_SIZE or header[:4] != _MAGIC:
            raise ValueError("农历年表文件头无效")
        start_year = header[4] | (header[5] << 8)
        record_count = (
            header[6]
            | (header[7] << 8)
            | (header[8] << 16)
            | (header[9] << 24)
        )
        if not start_year or not record_count:
            raise ValueError("农历年表元数据无效")

        index = _day_index(year, month, day, start_year)
        if index is None or index >= record_count:
            return None
        source.seek(_HEADER_SIZE + index * _RECORD_SIZE)
        record = source.read(_RECORD_SIZE)
    finally:
        source.close()

    if len(record) != _RECORD_SIZE:
        raise ValueError("农历年表记录不完整")
    sexagenary_index = record[0]
    month_flags = record[1]
    lunar_month = month_flags & 0x0F
    is_leap_month = bool(month_flags & 0x80)
    lunar_day = record[2]
    if (
        sexagenary_index >= 60
        or month_flags & 0x70
        or lunar_month < 1
        or lunar_month > 12
        or lunar_day < 1
        or lunar_day > 30
    ):
        raise ValueError("农历年表记录超出有效范围")
    return sexagenary_index, lunar_month, is_leap_month, lunar_day


def _day_name(day):
    if day < 10:
        return "初" + _DIGITS[day]
    if day == 10:
        return "初十"
    if day < 20:
        return "十" + _DIGITS[day - 10]
    if day == 20:
        return "二十"
    if day < 30:
        return "廿" + _DIGITS[day - 20]
    return "三十"


def format_lunar_date(date_value, table_path=None):
    """Format a datetime.date or MicroPython localtime tuple in Simplified Chinese."""
    lunar_date = lookup_lunar_date(date_value, table_path)
    if lunar_date is None:
        return None
    sexagenary_index, month, is_leap_month, day = lunar_date
    year_name = _STEMS[sexagenary_index % 10] + _BRANCHES[sexagenary_index % 12]
    month_name = _MONTH_NAMES[month]
    if is_leap_month:
        month_name = "闰" + month_name
    return year_name + "年" + month_name + _day_name(day)


HKO_API_HOST = "data.weather.gov.hk"
HKO_API_PATH = "/weatherAPI/opendata/lunardate.php?date="
HKO_API_MAX_BODY_BYTES = 2048
HKO_API_TIMEOUT_SECONDS = 2
_HKO_API_RETRY_MS = (60_000, 300_000, 1_800_000, 21_600_000)
_TICKS_PERIOD = 1 << 30
_DAY_NAMES = tuple(_day_name(day) for day in range(1, 31))
_HKO_MONTH_ALIASES = (("十一月", "冬月"), ("十二月", "腊月"))


def _ticks_diff(ticks1, ticks2):
    half_period = _TICKS_PERIOD // 2
    return ((ticks1 - ticks2 + half_period) % _TICKS_PERIOD) - half_period


def is_hko_api_year_supported(year, current_year):
    """Return whether HKO's API documentation permits this Gregorian year.

    HKO Open Data API Documentation v1.13 (Sep 2025) specifies the current
    year and the following two years for the Gregorian-Lunar conversion API.
    """
    if any(
        isinstance(value, bool) or not isinstance(value, int)
        for value in (year, current_year)
    ):
        return False
    return current_year <= year <= current_year + 2


def _is_valid_gregorian_date(parts):
    return parts is not None and _day_index(parts[0], parts[1], parts[2], 1) is not None


def parse_hko_api_payload(payload):
    """Convert HKO's traditional-Chinese LunarYear/LunarDate JSON to Simplified."""
    if not isinstance(payload, dict) or set(payload) != {"LunarYear", "LunarDate"}:
        raise ValueError("HKO 农历 API 字段不匹配")
    lunar_year = payload.get("LunarYear")
    lunar_date = payload.get("LunarDate")
    if not isinstance(lunar_year, str) or not isinstance(lunar_date, str):
        raise ValueError("HKO 农历 API 字段类型无效")

    year_separator = lunar_year.find("年")
    if year_separator != 2:
        raise ValueError("HKO 干支年格式无效")
    stem, branch = lunar_year[0], lunar_year[1]
    if stem not in _STEMS or branch not in _BRANCHES:
        raise ValueError("HKO 干支年不在六十甲子范围内")

    simplified_date = lunar_date.replace("閏", "闰").replace("臘", "腊")
    for hko_month, display_month in _HKO_MONTH_ALIASES:
        leap_prefix = "闰" + hko_month
        if simplified_date.startswith(leap_prefix):
            simplified_date = "闰" + display_month + simplified_date[len(leap_prefix) :]
            break
        if simplified_date.startswith(hko_month):
            simplified_date = display_month + simplified_date[len(hko_month) :]
            break

    normalized_date = None
    month_names = tuple(sorted(_MONTH_NAMES[1:], key=len, reverse=True))
    for month_name in month_names:
        if simplified_date.startswith("闰" + month_name):
            normalized_date = simplified_date[len("闰") + len(month_name) :]
            break
        if simplified_date.startswith(month_name):
            normalized_date = simplified_date[len(month_name) :]
            break
    if normalized_date not in _DAY_NAMES:
        raise ValueError("HKO 农历日格式无效")
    if not any(
        simplified_date.startswith(month_name)
        or simplified_date.startswith("闰" + month_name)
        for month_name in _MONTH_NAMES[1:]
    ):
        raise ValueError("HKO 农历月格式无效")
    return stem + branch + "年" + simplified_date


def _hko_ca_path_candidates():
    module_file = globals().get("__file__", "")
    candidates = []
    if module_file:
        separator = "/" if "/" in module_file else "\\"
        if separator in module_file:
            candidates.append(module_file.rsplit(separator, 1)[0] + separator + "hko_root_ca.pem")
    candidates.extend(("/pico/hko_root_ca.pem", "pico/hko_root_ca.pem", "hko_root_ca.pem"))
    return candidates


def _read_hko_ca(ca_path=None):
    paths = (ca_path,) if ca_path else _hko_ca_path_candidates()
    for path in paths:
        try:
            with open(path, "rb") as source:
                return source.read()
        except OSError:
            pass
    raise OSError("缺少香港邮政 Root CA 3 信任证书")


def _decode_chunked(body):
    chunks = []
    offset = 0
    total = 0
    while True:
        line_end = body.find(b"\r\n", offset)
        if line_end < 0:
            raise ValueError("HKO HTTP chunk 长度无效")
        size_text = body[offset:line_end].split(b";", 1)[0].strip()
        try:
            size = int(size_text, 16)
        except ValueError as exc:
            raise ValueError("HKO HTTP chunk 长度无效") from exc
        offset = line_end + 2
        if size == 0:
            return b"".join(chunks)
        if size < 0 or total + size > HKO_API_MAX_BODY_BYTES:
            raise ValueError("HKO HTTP 响应体过长")
        end = offset + size
        if end + 2 > len(body) or body[end : end + 2] != b"\r\n":
            raise ValueError("HKO HTTP chunk 被截断")
        chunks.append(body[offset:end])
        total += size
        offset = end + 2


def _parse_hko_http_response(response):
    header_end = response.find(b"\r\n\r\n")
    if header_end < 0:
        raise ValueError("HKO HTTP 响应缺少头部结束符")
    headers = response[:header_end].split(b"\r\n")
    status_parts = headers[0].split()
    if len(status_parts) < 2 or status_parts[1] != b"200":
        raise OSError("HKO HTTP 请求未返回 200")
    header_values = {}
    for line in headers[1:]:
        if b":" in line:
            key, value = line.split(b":", 1)
            header_values[key.strip().lower()] = value.strip().lower()
    body = response[header_end + 4 :]
    if header_values.get(b"transfer-encoding") == b"chunked":
        body = _decode_chunked(body)
    elif b"content-length" in header_values:
        try:
            content_length = int(header_values[b"content-length"])
        except ValueError as exc:
            raise ValueError("HKO HTTP Content-Length 无效") from exc
        if content_length < 0 or content_length > HKO_API_MAX_BODY_BYTES:
            raise ValueError("HKO HTTP 响应体过长")
        if len(body) < content_length:
            raise ValueError("HKO HTTP 响应体被截断")
        body = body[:content_length]
    if not body or len(body) > HKO_API_MAX_BODY_BYTES:
        raise ValueError("HKO HTTP 响应体长度无效")
    return body


def _read_hko_response(sock):
    response = bytearray()
    while len(response) <= HKO_API_MAX_BODY_BYTES + 1024:
        try:
            chunk = sock.read(256)
        except OSError:
            if response:
                break
            raise
        if not chunk:
            break
        response.extend(chunk)
        if b"\r\n\r\n" in response:
            header_end = response.find(b"\r\n\r\n")
            headers = bytes(response[:header_end]).lower()
            marker = b"content-length:"
            marker_at = headers.find(marker)
            if marker_at >= 0:
                line_end = headers.find(b"\r\n", marker_at)
                if line_end < 0:
                    line_end = len(headers)
                try:
                    content_length = int(headers[marker_at + len(marker) : line_end].strip())
                except ValueError:
                    break
                if len(response) - header_end - 4 >= content_length:
                    break
    if len(response) > HKO_API_MAX_BODY_BYTES + 1024:
        raise ValueError("HKO HTTP 响应超过缓冲上限")
    return bytes(response)


def fetch_hko_lunar_date(date_value, current_year, ca_path=None):
    """Fetch one HKO conversion with SNI and CA-chain verification.

    The TLS context trusts only the official Hongkong Post Root CA 3. The
    MicroPython 1.29 SSL API documents CERT_REQUIRED and server_hostname, but
    does not expose a CRL/OCSP validation setting. The current HKO issuer's
    revocation status therefore remains an explicit runtime verification limit.

    Sources:
      HKO API v1.13: https://data.weather.gov.hk/weatherAPI/doc/HKO_Open_Data_API_Documentation.pdf
      MicroPython 1.29 SSL: https://docs.micropython.org/en/v1.29.0/library/ssl.html
      Hongkong Post CA: https://www1.ecert.gov.hk/root/root_ca_3_pem.crt
    """
    parts = _date_parts(date_value)
    if not _is_valid_gregorian_date(parts):
        raise ValueError("HKO API 日期无效")
    year, month, day = parts
    if not is_hko_api_year_supported(year, current_year):
        raise ValueError("HKO API 仅支持当前公历年及之后两年")
    date_text = "%04d-%02d-%02d" % (year, month, day)

    import socket
    import ssl

    ca_data = _read_hko_ca(ca_path)
    context = ssl.SSLContext(ssl.PROTOCOL_TLS_CLIENT)
    context.verify_mode = ssl.CERT_REQUIRED
    context.load_verify_locations(cadata=ca_data)

    addresses = socket.getaddrinfo(
        HKO_API_HOST,
        443,
        socket.AF_INET,
        socket.SOCK_STREAM,
    )
    if not addresses:
        raise OSError("HKO API DNS 未返回 IPv4 地址")
    raw_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    tls_socket = None
    try:
        raw_socket.settimeout(HKO_API_TIMEOUT_SECONDS)
        raw_socket.connect(addresses[0][-1])
        tls_socket = context.wrap_socket(
            raw_socket,
            server_hostname=HKO_API_HOST,
        )
        request = (
            "GET %s%s HTTP/1.1\r\n"
            "Host: %s\r\n"
            "User-Agent: Pico-W-Lunar-Display/1\r\n"
            "Accept: application/json\r\n"
            "Accept-Encoding: identity\r\n"
            "Connection: close\r\n\r\n"
        ) % (HKO_API_PATH, date_text, HKO_API_HOST)
        tls_socket.write(request.encode("ascii"))
        response = _read_hko_response(tls_socket)
        body = _parse_hko_http_response(response)
        try:
            payload = json.loads(body.decode("utf-8"))
        except Exception as exc:
            raise ValueError("HKO API JSON 无效") from exc
        return parse_hko_api_payload(payload)
    finally:
        if tls_socket is not None:
            tls_socket.close()
        else:
            raw_socket.close()


class HkoLunarService:
    """Cache local lunar output and rate-limit the optional HKO API check.

    The network check always runs in a worker thread, so the display loop never
    blocks on DNS or TLS. Results are applied only while the generation still
    matches; every failure keeps the local table result on screen.
    """

    def __init__(self, fetcher=None, thread_module=None):
        self._fetcher = fetcher or fetch_hko_lunar_date
        self._thread = thread_module if thread_module is not None else _thread
        self._lock = None
        if self._thread is not None:
            try:
                self._lock = self._thread.allocate_lock()
            except Exception:
                self._thread = None
        self._task_running = False
        self._worker_result = None
        self._generation = 0
        self._date_value = None
        self._local_text = None
        self._api_text = None
        self._api_attempted_successfully = False
        self._api_attempts = 0
        self._next_attempt_ms = None
        self.api_mismatch = False
        self.api_error = None

    @property
    def busy(self):
        """True while the worker thread owns the shared network slot."""
        return self._task_running

    @property
    def display_text(self):
        return self._local_text or self._api_text or "历日暂不可用"

    @property
    def api_verified(self):
        return self._api_attempted_successfully

    def set_date(self, date_value, now_ms):
        """Load the local table once when the displayed Gregorian date changes."""
        parts = _date_parts(date_value) if date_value is not None else None
        if not _is_valid_gregorian_date(parts):
            parts = None
        if parts == self._date_value:
            return False
        self._generation += 1
        self._date_value = parts
        self._local_text = None
        self._api_text = None
        self._api_attempted_successfully = False
        self._api_attempts = 0
        self._next_attempt_ms = now_ms
        self.api_mismatch = False
        self.api_error = None
        if parts is not None:
            try:
                self._local_text = format_lunar_date(parts)
            except (OSError, ValueError):
                self._local_text = None
        return True

    def poll(self, now_ms, current_year, wifi_connected, clock_ready, peer_busy=False):
        """Apply a finished check and start at most one bounded worker; never block."""
        self._apply_worker_result(now_ms)

        if (
            self._date_value is None
            or not wifi_connected
            or not clock_ready
            or self._api_attempted_successfully
        ):
            return False
        if not is_hko_api_year_supported(self._date_value[0], current_year):
            return False
        if self._next_attempt_ms is not None and _ticks_diff(now_ms, self._next_attempt_ms) < 0:
            return False
        # Share the single TLS worker slot with the weather client: two
        # concurrent handshakes add avoidable heap pressure on RP2040.
        if self._task_running or peer_busy:
            return False
        started = self._start_worker(now_ms, current_year)
        if started:
            # Apply at once when an injected executor runs the worker inline.
            self._apply_worker_result(now_ms)
        return started

    def _start_worker(self, now_ms, current_year):
        if self._thread is None or self._lock is None:
            self.api_error = "NoThread"
            self._schedule_failure(now_ms)
            return False

        self._lock.acquire()
        generation = self._generation
        date_value = self._date_value
        self._task_running = True
        self._lock.release()

        try:
            self._thread.start_new_thread(
                self._worker,
                (generation, date_value, current_year),
            )
        except Exception as error:
            self.api_error = type(error).__name__
            self._lock.acquire()
            self._task_running = False
            self._worker_result = None
            self._lock.release()
            self._schedule_failure(now_ms)
            return False
        return True

    def _worker(self, generation, date_value, current_year):
        try:
            api_text = self._fetcher(date_value, current_year)
            success = isinstance(api_text, str) and bool(api_text)
        except Exception as error:
            # Only the type name is printed: messages can carry URLs/payloads.
            self.api_error = type(error).__name__
            print("HKO err %s" % self.api_error)
            api_text = None
            success = False

        self._lock.acquire()
        self._worker_result = (generation, success, api_text)
        self._task_running = False
        self._lock.release()

    def _take_worker_result(self):
        if self._lock is None:
            return None
        self._lock.acquire()
        result = self._worker_result
        self._worker_result = None
        self._lock.release()
        return result

    def _apply_worker_result(self, now_ms):
        result = self._take_worker_result()
        if result is None:
            return
        generation, success, api_text = result
        if generation != self._generation:
            return
        if not success:
            self._schedule_failure(now_ms)
            return
        self._api_attempted_successfully = True
        self._api_text = api_text
        if self._local_text is not None:
            self.api_mismatch = self._local_text != api_text

    def _schedule_failure(self, now_ms):
        delay_index = min(self._api_attempts, len(_HKO_API_RETRY_MS) - 1)
        self._api_attempts += 1
        self._next_attempt_ms = (
            now_ms + _HKO_API_RETRY_MS[delay_index]
        ) % _TICKS_PERIOD
