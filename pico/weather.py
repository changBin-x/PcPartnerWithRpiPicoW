"""Non-blocking AMap weather client for Pico W / MicroPython."""

try:
    import ujson as json
except ImportError:
    import json


AMAP_API_HOST = "restapi.amap.com"
AMAP_API_PORT = 443
AMAP_IP_PATH = "/v3/ip"
AMAP_WEATHER_PATH = "/v3/weather/weatherInfo"
AMAP_API_TIMEOUT_SECONDS = 5
AMAP_API_MAX_BODY_BYTES = 4096
AMAP_MAX_HEADER_BYTES = 2048
AMAP_MAX_RESPONSE_BYTES = (
    AMAP_API_MAX_BODY_BYTES + AMAP_MAX_HEADER_BYTES + 1024
)

REFRESH_INTERVAL_MS = 30 * 60 * 1000
RETRY_BACKOFF_MS = (60_000, 300_000, 900_000, 1_800_000)
_TICKS_PERIOD = 1 << 30
_WEATHER_STATUS_FETCHING = "天气获取中"
_WEATHER_STATUS_UNAVAILABLE = "天气暂不可用"
_WEATHER_STATUS_UNCONFIGURED = "天气未配置"

try:
    import _thread
except ImportError:
    _thread = None

import time


def _ticks_diff(ticks1, ticks2):
    half_period = _TICKS_PERIOD // 2
    return ((ticks1 - ticks2 + half_period) % _TICKS_PERIOD) - half_period


def is_valid_key(key):
    """Accept a bounded, non-empty key without assuming its character alphabet."""
    if not isinstance(key, str) or not key or len(key) > 128:
        return False
    for character in key:
        codepoint = ord(character)
        if codepoint < 33 or codepoint == 127:
            return False
    return True


def is_valid_adcode(adcode):
    if not isinstance(adcode, str) or len(adcode) != 6:
        return False
    for character in adcode:
        if character < "0" or character > "9":
            return False
    return True


def _parse_temperature(value):
    if isinstance(value, bool):
        return None
    try:
        temperature = float(value)
    except (TypeError, ValueError):
        return None
    if temperature != temperature or temperature < -80.0 or temperature > 70.0:
        return None
    return temperature


def _url_encode(value):
    """Percent-encode UTF-8 while keeping RFC 3986 unreserved bytes."""
    safe = b"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-._~"
    encoded = []
    for byte in value.encode("utf-8"):
        if byte in safe:
            encoded.append(chr(byte))
        else:
            encoded.append("%%%02X" % byte)
    return "".join(encoded)


def _load_weather_config():
    try:
        try:
            from . import weather_secrets
        except ImportError:
            import weather_secrets
    except Exception:
        return "", ""

    key = getattr(weather_secrets, "AMAP_WEB_KEY", "")
    adcode = getattr(weather_secrets, "CITY_OVERRIDE_ADCODE", "")
    if not isinstance(key, str):
        key = ""
    if not isinstance(adcode, str):
        adcode = ""
    return key.strip(), adcode.strip()


def _amap_ca_path_candidates():
    module_file = globals().get("__file__", "")
    candidates = []
    if module_file:
        separator = "/" if "/" in module_file else "\\"
        if separator in module_file:
            candidates.append(
                module_file.rsplit(separator, 1)[0]
                + separator
                + "amap_root_ca.pem"
            )
    candidates.extend(
        ("/pico/amap_root_ca.pem", "pico/amap_root_ca.pem", "amap_root_ca.pem")
    )
    return candidates


def _read_amap_ca(ca_path=None):
    paths = (ca_path,) if ca_path else _amap_ca_path_candidates()
    for path in paths:
        try:
            with open(path, "rb") as source:
                return source.read()
        except OSError:
            pass
    raise OSError("缺少高德 TLS 信任根证书")


def _decode_chunked(body):
    chunks = []
    offset = 0
    total = 0
    while True:
        line_end = body.find(b"\r\n", offset)
        if line_end < 0:
            raise ValueError("HTTP chunk 长度无效")
        size_text = body[offset:line_end].split(b";", 1)[0].strip()
        try:
            size = int(size_text, 16)
        except ValueError:
            raise ValueError("HTTP chunk 长度无效")
        offset = line_end + 2
        if size == 0:
            if body[offset:offset + 2] == b"\r\n":
                return b"".join(chunks)
            trailer_end = body.find(b"\r\n\r\n", offset)
            if trailer_end < 0:
                raise ValueError("HTTP chunk 结束标记无效")
            return b"".join(chunks)
        if size < 0 or total + size > AMAP_API_MAX_BODY_BYTES:
            raise ValueError("HTTP 响应体过长")
        end = offset + size
        if end + 2 > len(body) or body[end:end + 2] != b"\r\n":
            raise ValueError("HTTP chunk 被截断")
        chunks.append(body[offset:end])
        total += size
        offset = end + 2


def _parse_http_response(response):
    header_end = response.find(b"\r\n\r\n")
    if header_end < 0 or header_end > AMAP_MAX_HEADER_BYTES:
        raise ValueError("HTTP 响应头无效")
    lines = response[:header_end].split(b"\r\n")
    status_parts = lines[0].split()
    if len(status_parts) < 2 or status_parts[1] != b"200":
        status = status_parts[1].decode("ascii") if len(status_parts) > 1 else "unknown"
        raise OSError("高德 HTTP 请求失败: %s" % status)

    headers = {}
    for line in lines[1:]:
        if b":" in line:
            name, value = line.split(b":", 1)
            headers[name.strip().lower()] = value.strip().lower()

    body = response[header_end + 4:]
    transfer_encoding = headers.get(b"transfer-encoding", b"")
    if b"chunked" in transfer_encoding.split(b","):
        body = _decode_chunked(body)
    elif b"content-length" in headers:
        try:
            content_length = int(headers[b"content-length"])
        except ValueError:
            raise ValueError("HTTP Content-Length 无效")
        if content_length < 0 or content_length > AMAP_API_MAX_BODY_BYTES:
            raise ValueError("HTTP 响应体过长")
        if len(body) < content_length:
            raise ValueError("HTTP 响应体被截断")
        body = body[:content_length]

    if not body or len(body) > AMAP_API_MAX_BODY_BYTES:
        raise ValueError("HTTP 响应体长度无效")
    return body


def _now_ms():
    """MicroPython ticks when available, CPython monotonic fallback for tests."""
    ticks_ms = getattr(time, "ticks_ms", None)
    if ticks_ms is not None:
        return ticks_ms()
    return int(time.monotonic() * 1000) % _TICKS_PERIOD


def _read_all_response(sock, timeout_seconds=None):
    if timeout_seconds is None:
        timeout_seconds = AMAP_API_TIMEOUT_SECONDS
    # Slow-drip responses could otherwise hold the worker thread for minutes.
    deadline = (_now_ms() + int(timeout_seconds * 1000)) % _TICKS_PERIOD
    response = bytearray()
    while len(response) < AMAP_MAX_RESPONSE_BYTES:
        if _ticks_diff(_now_ms(), deadline) >= 0:
            raise OSError("HTTP 响应读取超时")
        remaining = AMAP_MAX_RESPONSE_BYTES - len(response)
        chunk = sock.read(min(256, remaining))
        if not chunk:
            break
        response.extend(chunk)

        header_end = response.find(b"\r\n\r\n")
        if header_end >= 0:
            headers = bytes(response[:header_end]).lower()
            for line in headers.split(b"\r\n")[1:]:
                if line.startswith(b"content-length:"):
                    try:
                        expected = int(line.split(b":", 1)[1].strip())
                    except ValueError:
                        break
                    if expected >= 0 and len(response) - header_end - 4 >= expected:
                        return bytes(response)

    if len(response) >= AMAP_MAX_RESPONSE_BYTES:
        raise ValueError("HTTP 响应超过大小限制")
    return bytes(response)


def _https_get(path_with_query, ca_path=None):
    import socket
    import ssl

    ca_data = _read_amap_ca(ca_path)
    context = ssl.SSLContext(ssl.PROTOCOL_TLS_CLIENT)
    context.verify_mode = ssl.CERT_REQUIRED
    context.load_verify_locations(cadata=ca_data)

    addresses = socket.getaddrinfo(
        AMAP_API_HOST,
        AMAP_API_PORT,
        socket.AF_INET,
        socket.SOCK_STREAM,
    )
    if not addresses:
        raise OSError("高德 DNS 未返回 IPv4 地址")

    raw_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    tls_socket = None
    try:
        raw_socket.settimeout(AMAP_API_TIMEOUT_SECONDS)
        raw_socket.connect(addresses[0][-1])
        # MicroPython uses server_hostname for SNI and certificate hostname checks.
        tls_socket = context.wrap_socket(
            raw_socket,
            server_hostname=AMAP_API_HOST,
        )
        request = (
            "GET %s HTTP/1.1\r\n"
            "Host: %s\r\n"
            "User-Agent: Pico-W-Weather/1.0\r\n"
            "Accept: application/json\r\n"
            "Accept-Encoding: identity\r\n"
            "Connection: close\r\n\r\n"
        ) % (path_with_query, AMAP_API_HOST)
        request_bytes = request.encode("ascii")
        sent = 0
        while sent < len(request_bytes):
            count = tls_socket.write(request_bytes[sent:])
            if not count:
                raise OSError("HTTPS 请求发送失败")
            sent += count

        body = _parse_http_response(_read_all_response(tls_socket))
        return json.loads(body.decode("utf-8"))
    finally:
        if tls_socket is not None:
            tls_socket.close()
        else:
            raw_socket.close()


def fetch_ip_location(key, ca_path=None):
    if not is_valid_key(key):
        raise ValueError("高德 API Key 无效")
    payload = _https_get(
        "%s?key=%s&output=JSON" % (AMAP_IP_PATH, _url_encode(key)),
        ca_path=ca_path,
    )
    if not isinstance(payload, dict):
        raise ValueError("高德定位响应结构无效")
    if payload.get("status") != "1" or payload.get("info") != "OK":
        raise OSError("高德 IP 定位失败")

    adcode = payload.get("adcode")
    if not is_valid_adcode(adcode):
        raise ValueError("高德定位未返回有效城市编码")

    city = payload.get("city")
    if not isinstance(city, str) or not city.strip() or city == "[]":
        city = payload.get("province", "")
    if not isinstance(city, str):
        city = ""
    return {"adcode": adcode, "city": city.strip()}


def fetch_weather(key, adcode, ca_path=None):
    if not is_valid_key(key):
        raise ValueError("高德 API Key 无效")
    if not is_valid_adcode(adcode):
        raise ValueError("城市编码无效")
    query = "%s?key=%s&city=%s&extensions=base" % (
        AMAP_WEATHER_PATH,
        _url_encode(key),
        adcode,
    )
    query += "&output=JSON"
    payload = _https_get(query, ca_path=ca_path)
    if not isinstance(payload, dict) or payload.get("status") != "1":
        raise OSError("高德天气查询失败")

    lives = payload.get("lives")
    if not isinstance(lives, list) or not lives or not isinstance(lives[0], dict):
        raise ValueError("高德响应缺少实况数据")
    live = lives[0]

    weather = live.get("weather")
    if not isinstance(weather, str) or not weather.strip():
        raise ValueError("高德响应缺少天气现象")
    temperature = _parse_temperature(live.get("temperature"))
    if temperature is None:
        raise ValueError("高德响应温度无效")

    city = live.get("city")
    if not isinstance(city, str) or not city.strip():
        city = live.get("province", "")
    if not isinstance(city, str):
        city = ""
    return {
        "city": city.strip(),
        "weather": weather.strip(),
        "temperature": temperature,
    }


class AmapWeatherService:
    """Cache weather and fetch it off the display loop in one worker thread."""

    def __init__(
        self,
        config=None,
        fetch_ip_fn=None,
        fetch_weather_fn=None,
        ticks_diff_fn=None,
    ):
        if config is None:
            config = _load_weather_config()
        self.key, self.city_override = config
        self._fetch_ip = fetch_ip_fn or fetch_ip_location
        self._fetch_weather = fetch_weather_fn or fetch_weather
        self._ticks_diff = ticks_diff_fn or _ticks_diff

        self._effective_adcode = (
            self.city_override if is_valid_adcode(self.city_override) else None
        )
        self._location_city = ""
        self._last_weather = None
        self._status_text = (
            _WEATHER_STATUS_FETCHING if self.configured
            else _WEATHER_STATUS_UNCONFIGURED
        )
        self._retry_count = 0
        self._next_request_ms = None
        self._was_connected = False
        self._generation = 0
        self._worker_result = None
        self._task_running = False
        self._thread = _thread
        self._lock = None
        if self._thread is not None:
            try:
                self._lock = self._thread.allocate_lock()
            except Exception:
                self._thread = None
        # Reused so the 100 ms loop does not allocate a dict every tick.
        self._snapshot = {
            "configured": self.configured,
            "has_data": False,
            "city": "",
            "weather": "",
            "temperature": None,
            "is_stale": False,
            "status": self._status_text,
        }

    @property
    def configured(self):
        return is_valid_key(self.key)

    @property
    def busy(self):
        """True while the worker thread owns the shared network slot."""
        return self._task_running

    def snapshot(self):
        snap = self._snapshot
        snap["configured"] = self.configured
        value = self._last_weather
        if value is None:
            snap["has_data"] = False
            snap["city"] = ""
            snap["weather"] = ""
            snap["temperature"] = None
            snap["is_stale"] = False
            snap["status"] = self._status_text
            return snap
        snap["has_data"] = True
        snap["city"] = value["city"]
        snap["weather"] = value["weather"]
        snap["temperature"] = value["temperature"]
        snap["is_stale"] = value["is_stale"]
        snap["status"] = ""
        return snap

    def _schedule_failure(self, now_ms):
        delay_index = min(self._retry_count, len(RETRY_BACKOFF_MS) - 1)
        self._retry_count += 1
        self._next_request_ms = (
            now_ms + RETRY_BACKOFF_MS[delay_index]
        ) % _TICKS_PERIOD
        if self._last_weather is not None:
            self._last_weather["is_stale"] = True
        else:
            self._status_text = _WEATHER_STATUS_UNAVAILABLE

    def _worker(self, generation, key, adcode, location_city):
        result_adcode = adcode
        result_city = location_city
        try:
            if not is_valid_adcode(result_adcode):
                location = self._fetch_ip(key)
                result_adcode = location.get("adcode")
                result_city = location.get("city", "")
            if not is_valid_adcode(result_adcode):
                raise ValueError("定位未返回有效城市编码")

            weather_data = self._fetch_weather(key, result_adcode)
            if not isinstance(weather_data, dict):
                raise ValueError("天气结果结构无效")
            condition = weather_data.get("weather")
            temperature = _parse_temperature(weather_data.get("temperature"))
            city = weather_data.get("city") or result_city
            if not isinstance(city, str):
                city = ""
            if not isinstance(condition, str) or not condition.strip():
                raise ValueError("天气现象无效")
            if temperature is None:
                raise ValueError("温度无效")

            weather_data = {
                "city": city.strip() or "未知",
                "weather": condition.strip(),
                "temperature": temperature,
            }
            print("WX ok %s %s" % (temperature, weather_data["weather"]))
            success = True
        except Exception as error:
            # Only the type name and numeric errno are printed: messages can
            # carry the URL, key or response payload.
            reason = type(error).__name__
            try:
                code = error.args[0]
                if isinstance(code, int):
                    reason = "%s %d" % (reason, code)
            except (AttributeError, IndexError, TypeError):
                pass
            print("WX err %s" % reason)
            weather_data = None
            success = False

        self._lock.acquire()
        self._worker_result = (
            generation,
            success,
            weather_data,
            result_adcode,
            result_city,
        )
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

    def _start_worker(self, now_ms):
        if self._thread is None or self._lock is None:
            self._schedule_failure(now_ms)
            return

        self._lock.acquire()
        if self._task_running:
            self._lock.release()
            return
        self._task_running = True
        generation = self._generation
        adcode = self._effective_adcode
        city = self._location_city
        self._lock.release()

        try:
            self._thread.start_new_thread(
                self._worker,
                (generation, self.key, adcode, city),
            )
        except Exception:
            self._lock.acquire()
            self._task_running = False
            self._worker_result = (generation, False, None, adcode, city)
            self._lock.release()

    def _apply_worker_result(self, now_ms):
        result = self._take_worker_result()
        if result is None:
            return
        generation, success, weather_data, adcode, city = result
        if generation != self._generation:
            return

        if is_valid_adcode(adcode):
            self._effective_adcode = adcode
        if isinstance(city, str) and city:
            self._location_city = city

        if success:
            self._last_weather = {
                "city": weather_data["city"],
                "weather": weather_data["weather"],
                "temperature": weather_data["temperature"],
                "is_stale": False,
            }
            self._status_text = ""
            self._retry_count = 0
            self._next_request_ms = (
                now_ms + REFRESH_INTERVAL_MS
            ) % _TICKS_PERIOD
        else:
            self._schedule_failure(now_ms)

    def poll(self, now_ms, wifi_connected, wifi_changed=False, clock_ready=True):
        """Advance state and start at most one background request; never block on I/O.

        clock_ready only guards TLS: certificate validation needs a plausible
        wall clock, so requests wait for NTP (or a still-valid RTC) first.
        """
        if not self.configured:
            self._status_text = _WEATHER_STATUS_UNCONFIGURED
            return self.snapshot()

        wifi_connected = bool(wifi_connected)
        reconnected = wifi_connected and (
            not self._was_connected or bool(wifi_changed)
        )
        disconnected = not wifi_connected and self._was_connected

        if reconnected:
            self._generation += 1
            if not is_valid_adcode(self.city_override):
                self._effective_adcode = None
                self._location_city = ""
            self._next_request_ms = now_ms
        elif disconnected:
            self._generation += 1

        self._was_connected = wifi_connected
        if disconnected and self._last_weather is not None:
            self._last_weather["is_stale"] = True

        self._apply_worker_result(now_ms)

        if not wifi_connected:
            if self._last_weather is None:
                self._status_text = _WEATHER_STATUS_UNAVAILABLE
            return self.snapshot()

        if not clock_ready:
            if self._last_weather is None:
                self._status_text = _WEATHER_STATUS_FETCHING
            return self.snapshot()

        if (
            self._next_request_ms is None
            or self._ticks_diff(now_ms, self._next_request_ms) >= 0
        ):
            self._start_worker(now_ms)

        return self.snapshot()
