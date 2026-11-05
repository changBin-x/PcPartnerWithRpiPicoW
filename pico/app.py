"""Pico dashboard freshness state and MicroPython application loop."""

try:
    from . import display
    from .udp_receiver import validate_metrics_payload
except ImportError:
    import display
    from udp_receiver import validate_metrics_payload


OFFLINE_AFTER_MS = 3000
_TICKS_PERIOD = 1 << 30


def _ticks_diff(ticks1, ticks2):
    """Host-test fallback matching MicroPython's RP2 ticks_diff period."""
    half_period = _TICKS_PERIOD // 2
    return ((ticks1 - ticks2 + half_period) % _TICKS_PERIOD) - half_period


class DashboardState:
    """Keep only the newest valid packet and decide dashboard/clock mode."""

    def __init__(self, ticks_diff=None, offline_after_ms=OFFLINE_AFTER_MS):
        self._ticks_diff = ticks_diff or _ticks_diff
        self.offline_after_ms = offline_after_ms
        self.last_received_ms = None
        self.last_seq = None
        self.last_timestamp = None
        self.metrics = None

    def accept_datagram(self, data, now_ms):
        """Validate a raw UDP datagram and accept it only when fresh/new."""
        try:
            from .udp_receiver import decode_metrics_packet
        except ImportError:
            from udp_receiver import decode_metrics_packet
        try:
            payload = decode_metrics_packet(data)
        except ValueError:
            return False
        return self.accept_payload(payload, now_ms)

    def accept_payload(self, payload, now_ms):
        """Accept a validated metrics object, rejecting duplicate/stale seqs."""
        try:
            validate_metrics_payload(payload)
        except ValueError:
            return False

        sequence = payload["seq"]
        timestamp = payload["ts"]
        if self.last_seq is not None:
            age = self._ticks_diff(now_ms, self.last_received_ms)
            if sequence == self.last_seq:
                return False
            if sequence < self.last_seq and timestamp <= self.last_timestamp and age < self.offline_after_ms:
                return False

        self.last_seq = sequence
        self.last_timestamp = timestamp
        self.last_received_ms = now_ms
        self.metrics = {
            "cpu": payload["cpu"],
            "gpu": payload["gpu"],
            "ram": payload["ram"],
            "rx": payload["rx"],
            "tx": payload["tx"],
        }
        return True

    def mode(self, now_ms):
        """Return dashboard for age < 3000 ms, and clock otherwise."""
        if self.last_received_ms is None:
            return "clock"
        age = self._ticks_diff(now_ms, self.last_received_ms)
        if age < 0 or age >= self.offline_after_ms:
            return "clock"
        return "dashboard"


def run(
    screen=None,
    watchdog=None,
    time_module=None,
    receiver_factory=None,
    wifi_factory=None,
    synchronizer_factory=None,
    state_factory=None,
    lunar_factory=None,
    weather_factory=None,
):
    """Connect to Wi-Fi, accept PC broadcasts, and redraw on second boundaries.

    Every collaborator is injectable so the loop itself can run under the host
    test suite with a fake clock, fake screen and fake network services.
    """
    import time

    ticks = time_module or time

    try:
        from .st7735 import ST7735
        from .udp_receiver import UdpReceiver
        from .wifi import WifiManager
        from .time_sync import TimeSynchronizer
        from .lunar import HkoLunarService
        from .weather import AmapWeatherService
    except ImportError:
        from st7735 import ST7735
        from udp_receiver import UdpReceiver
        from wifi import WifiManager
        from time_sync import TimeSynchronizer
        from lunar import HkoLunarService
        from weather import AmapWeatherService

    make_receiver = receiver_factory or UdpReceiver
    make_wifi = wifi_factory or (lambda: WifiManager(ticks_diff=ticks.ticks_diff))
    make_synchronizer = synchronizer_factory or TimeSynchronizer
    make_state = state_factory or (lambda: DashboardState(ticks_diff=ticks.ticks_diff))
    make_lunar = lunar_factory or HkoLunarService
    make_weather = weather_factory or AmapWeatherService

    if screen is None:
        screen = ST7735()
    wifi = make_wifi()
    synchronizer = make_synchronizer()
    state = make_state()
    lunar = make_lunar()
    weather = make_weather()

    receiver = None
    try:
        receiver = make_receiver()
    except OSError:
        receiver = None

    last_draw_ms = None
    last_mode = None
    last_metrics_ms = None
    last_date = None
    last_time_text = None
    render_state = None

    try:
        while True:
            if watchdog is not None:
                watchdog.feed()
            now_ms = ticks.ticks_ms()
            wifi_connected, wifi_changed = wifi.poll(now_ms)

            if wifi_changed and wifi_connected:
                # A Wi-Fi reconnect can invalidate lwIP sockets; rebind the
                # listener so metrics keep arriving after the link returns.
                if receiver is not None:
                    try:
                        receiver.close()
                    except OSError:
                        pass
                    receiver = None
            if receiver is None and wifi_connected:
                try:
                    receiver = make_receiver()
                except OSError:
                    receiver = None

            synchronizer.sync_if_due(
                now_ms,
                wifi_connected,
                force=wifi_changed and wifi_connected,
            )
            clock = synchronizer.snapshot()
            clock_ready = clock["time_text"] is not None

            weather_snapshot = weather.poll(
                now_ms,
                wifi_connected,
                wifi_changed=wifi_changed,
                clock_ready=clock_ready,
                peer_busy=getattr(lunar, "busy", False),
            )

            if receiver is not None:
                payload = receiver.poll()
                if payload is not None and state.accept_payload(payload, now_ms):
                    last_metrics_ms = state.last_received_ms
            else:
                last_metrics_ms = None

            date_value = clock["date"]
            if date_value != last_date:
                last_date = date_value
                lunar.set_date(date_value, now_ms)

            mode = state.mode(now_ms)
            # Redraw as soon as the wall-clock second changes so the displayed
            # second never skips; a 1 s fallback still covers static screens.
            seconds_changed = clock["time_text"] != last_time_text
            draw_due = (
                last_draw_ms is None
                or mode != last_mode
                or last_metrics_ms == now_ms
                or seconds_changed
                or ticks.ticks_diff(now_ms, last_draw_ms) >= 1000
            )
            if draw_due:
                lunar_text = lunar.display_text
                if mode == "dashboard":
                    render_state = display.draw_dashboard(
                        screen,
                        clock["time_text"],
                        clock["weekday_short"],
                        clock["short_date"],
                        weather_snapshot,
                        state.metrics or {},
                        wifi_connected,
                        previous=render_state,
                    )
                else:
                    render_state = display.draw_clock(
                        screen,
                        clock["time_text"],
                        clock["full_date"],
                        clock["weekday_full"],
                        lunar_text,
                        wifi_connected,
                        clock_ready,
                        previous=render_state,
                        needs_config=wifi.needs_config,
                        weather=weather_snapshot,
                    )
                last_draw_ms = now_ms
                last_mode = mode
                last_time_text = clock["time_text"]

            lunar.poll(
                now_ms,
                current_year=clock["year"],
                wifi_connected=wifi_connected,
                clock_ready=clock_ready,
                peer_busy=getattr(weather, "busy", False),
            )

            ticks.sleep_ms(100)
    finally:
        if receiver is not None:
            try:
                receiver.close()
            except OSError:
                pass
