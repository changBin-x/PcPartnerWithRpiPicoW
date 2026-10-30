"""MicroPython station-mode Wi-Fi connection with bounded retries."""


_TICKS_PERIOD = 1 << 30
_CONNECT_TIMEOUT_MS = 15_000
_RETRY_DELAYS_MS = (1_000, 3_000, 10_000, 30_000)


def _ticks_diff(ticks1, ticks2):
    half_period = _TICKS_PERIOD // 2
    return ((ticks1 - ticks2 + half_period) % _TICKS_PERIOD) - half_period


def _load_credentials():
    try:
        try:
            from . import wifi_secrets
        except ImportError:
            import wifi_secrets
    except Exception:
        return "", ""

    ssid = getattr(wifi_secrets, "SSID", "")
    password = getattr(wifi_secrets, "PASSWORD", "")
    if not isinstance(ssid, str) or not isinstance(password, str):
        return "", ""
    return ssid, password


class WifiManager:
    """Maintain a Wi-Fi station connection without exposing its credentials."""

    def __init__(self, credentials=None, station=None, ticks_diff=None):
        if credentials is None:
            credentials = _load_credentials()
        self.ssid, self.password = credentials
        self._station = station
        self._ticks_diff = ticks_diff or _ticks_diff
        self._station_ready = station is not None
        self._connecting = False
        self._connect_started_ms = None
        self._next_retry_ms = None
        self._retry_count = 0
        self._was_connected = False

        if self.configured and self._station is None:
            import network

            self._station = network.WLAN(network.STA_IF)
        if self.configured:
            try:
                self._station.active(True)
            except OSError:
                self._station_ready = False
            else:
                self._station_ready = True

    @property
    def configured(self):
        return isinstance(self.ssid, str) and bool(self.ssid.strip())

    @property
    def needs_config(self):
        return not self.configured

    def _schedule_retry(self, now_ms):
        delay_index = min(self._retry_count, len(_RETRY_DELAYS_MS) - 1)
        self._retry_count += 1
        self._next_retry_ms = (now_ms + _RETRY_DELAYS_MS[delay_index]) % _TICKS_PERIOD
        self._connecting = False
        self._connect_started_ms = None

    def poll(self, now_ms):
        """Return (connected, just_became_connected), starting retries as needed."""
        if not self.configured or self._station is None:
            return False, False

        if not self._station_ready:
            if (
                self._next_retry_ms is not None
                and self._ticks_diff(now_ms, self._next_retry_ms) < 0
            ):
                return False, False
            try:
                self._station.active(True)
            except OSError:
                self._schedule_retry(now_ms)
                self._station_ready = False
                return False, False
            self._station_ready = True
            self._next_retry_ms = now_ms

        try:
            connected = self._station.isconnected()
        except OSError:
            connected = False

        if connected:
            just_connected = not self._was_connected
            self._was_connected = True
            self._connecting = False
            self._connect_started_ms = None
            self._retry_count = 0
            return True, just_connected

        if self._was_connected:
            self._was_connected = False
            self._connecting = False
            self._connect_started_ms = None
            self._retry_count = 0
            self._next_retry_ms = now_ms

        if self._connecting:
            elapsed = self._ticks_diff(now_ms, self._connect_started_ms)
            if elapsed < _CONNECT_TIMEOUT_MS:
                return False, False
            try:
                self._station.disconnect()
            except OSError:
                pass
            self._schedule_retry(now_ms)

        if self._next_retry_ms is None or self._ticks_diff(now_ms, self._next_retry_ms) >= 0:
            try:
                self._station.connect(self.ssid, self.password)
            except OSError:
                self._schedule_retry(now_ms)
            else:
                self._connecting = True
                self._connect_started_ms = now_ms
        return False, False
