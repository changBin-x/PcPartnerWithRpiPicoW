"""Loop-level tests: cadence, reconnect handling, watchdog feeding and HKO."""

import unittest

import pico.app
from pico.app import run

TICKS_PERIOD = 1 << 30


class FakeTime:
    def __init__(self, limit):
        self.now = 0
        self.limit = limit
        self.sleeps = []
        self._sleeps = 0

    def ticks_ms(self):
        return self.now

    def ticks_diff(self, a, b):
        diff = (a - b) % TICKS_PERIOD
        if diff >= TICKS_PERIOD // 2:
            diff -= TICKS_PERIOD
        return diff

    def sleep_ms(self, delay):
        self.sleeps.append(delay)
        self._sleeps += 1
        self.now = (self.now + delay) % TICKS_PERIOD
        if self._sleeps >= self.limit:
            # Ends the infinite loop without being an application error.
            raise KeyboardInterrupt()


class FakeDisplay:
    def __init__(self):
        self.calls = []

    def draw_dashboard(self, screen, time_text, weekday, date, weather, metrics, wifi_ok, previous=None):
        self.calls.append(("dashboard", time_text, wifi_ok, metrics, previous))
        return {"screen": "dashboard"}

    def draw_clock(self, screen, time_text, full_date, weekday, lunar, wifi_ok, ready, previous=None, needs_config=False, weather=None):
        self.calls.append(("clock", time_text, ready, lunar, previous))
        return {"screen": "clock"}


class FakeSynchronizer:
    def __init__(self, ready_at_ms=0):
        self.ready_at_ms = ready_at_ms
        self.sync_calls = []
        self.synced = False
        self._snapshot = {
            "year": None,
            "date": None,
            "time_text": None,
            "short_date": None,
            "full_date": None,
            "weekday_short": None,
            "weekday_full": None,
        }

    def sync_if_due(self, now_ms, wifi_connected, force=False):
        self.sync_calls.append((now_ms, wifi_connected, force))
        self.synced = True
        return True

    def snapshot(self):
        return self._snapshot


class TickingSynchronizer(FakeSynchronizer):
    """Derives HH:MM:SS from the fake clock so seconds change predictably."""

    def __init__(self, time_module, ready_at_ms=0):
        super().__init__(ready_at_ms)
        self.time = time_module

    def snapshot(self):
        snap = self._snapshot
        if self.time.now < self.ready_at_ms:
            for key in snap:
                snap[key] = None
            return snap
        total = self.time.now // 1000
        snap["year"] = 2026
        snap["date"] = (2026, 11, 5)
        snap["time_text"] = "%02d:%02d:%02d" % (total // 3600, (total // 60) % 60, total % 60)
        snap["short_date"] = "11/05"
        snap["full_date"] = "2026年11月05日"
        snap["weekday_short"] = "周四"
        snap["weekday_full"] = "星期四"
        return snap


class FakeReceiver:
    instances = []

    def __init__(self):
        self.payloads = []
        self.closed = 0
        FakeReceiver.instances.append(self)

    def poll(self):
        return self.payloads.pop(0) if self.payloads else None

    def close(self):
        self.closed += 1


class FakeWifi:
    def __init__(self, script=None):
        self.script = list(script or [])
        self.poll_count = 0
        self.needs_config = False

    def poll(self, now_ms):
        index = min(self.poll_count, len(self.script) - 1) if self.script else -1
        self.poll_count += 1
        if index < 0:
            return False, False
        return self.script[index]


class FakeState:
    def __init__(self):
        self.last_received_ms = None
        self.accepted = []
        self.metrics = {"cpu": 12.5}
        self.mode_fn = lambda now_ms: "dashboard"

    def accept_payload(self, payload, now_ms):
        self.accepted.append((payload, now_ms))
        self.last_received_ms = now_ms
        return True

    def mode(self, now_ms):
        return self.mode_fn(now_ms)


class FakeLunar:
    def __init__(self):
        self.dates = []
        self.polls = []

    @property
    def display_text(self):
        return "丙午年九月廿二"

    def set_date(self, date_value, now_ms):
        self.dates.append((date_value, now_ms))
        return True

    def poll(self, now_ms, current_year, wifi_connected, clock_ready, peer_busy=False):
        self.polls.append((now_ms, current_year, wifi_connected, clock_ready, peer_busy))
        return False


class FakeWeather:
    def __init__(self):
        self.polls = []
        self.busy = False
        self._snapshot = {"has_data": False, "status": "天气获取中"}

    def poll(self, now_ms, wifi_connected, wifi_changed=False, clock_ready=True):
        self.polls.append((now_ms, wifi_connected, wifi_changed, clock_ready))
        return self._snapshot

    def snapshot(self):
        return self._snapshot


class FakeWatchdog:
    def __init__(self):
        self.feed_count = 0

    def feed(self):
        self.feed_count += 1


class LoopTests(unittest.TestCase):
    def setUp(self):
        self.display = FakeDisplay()
        self._original_display = pico.app.display
        pico.app.display = self.display
        FakeReceiver.instances = []

    def tearDown(self):
        pico.app.display = self._original_display

    def _run(self, limit, wifi=None, state=None, ready_at_ms=0, lunar=None, weather=None, watchdog=None):
        time_module = FakeTime(limit)
        synchronizer = TickingSynchronizer(time_module, ready_at_ms=ready_at_ms)
        state = state or FakeState()
        lunar = lunar or FakeLunar()
        weather = weather or FakeWeather()
        wifi = wifi or FakeWifi()
        with self.assertRaises(KeyboardInterrupt):
            run(
                screen=object(),
                watchdog=watchdog,
                time_module=time_module,
                receiver_factory=FakeReceiver,
                wifi_factory=lambda: wifi,
                synchronizer_factory=lambda: synchronizer,
                state_factory=lambda: state,
                lunar_factory=lambda: lunar,
                weather_factory=lambda: weather,
            )
        return time_module, state, lunar, weather, wifi, synchronizer

    def test_redraws_on_each_wall_clock_second(self):
        time_module, _, _, _, _, _ = self._run(25)

        self.assertEqual([call[1] for call in self.display.calls], ["00:00:00", "00:00:01", "00:00:02"])
        self.assertTrue(all(call[0] == "dashboard" for call in self.display.calls))
        self.assertEqual(time_module.sleeps, [100] * 25)

    def test_mode_change_redraws_immediately(self):
        state = FakeState()
        state.mode_fn = lambda now_ms: "dashboard" if now_ms < 500 else "clock"

        self._run(15, state=state)

        kinds = [call[0] for call in self.display.calls]
        self.assertEqual(kinds[0], "dashboard")
        self.assertEqual(kinds[1], "clock")
        self.assertEqual(kinds[1:], ["clock"] * (len(kinds) - 1))

    def test_watchdog_is_fed_every_tick(self):
        watchdog = FakeWatchdog()

        time_module, _, _, _, _, _ = self._run(12, watchdog=watchdog)

        self.assertEqual(watchdog.feed_count, len(time_module.sleeps))

    def test_reconnect_rebinds_the_listener(self):
        wifi = FakeWifi([(True, False), (True, False), (True, True), (True, False)])

        _, _, _, _, _, _ = self._run(8, wifi=wifi)

        self.assertEqual(len(FakeReceiver.instances), 2)
        self.assertEqual(FakeReceiver.instances[0].closed, 1)
        self.assertEqual(FakeReceiver.instances[1].closed, 1)  # closed by finally

    def test_listener_is_recreated_after_a_failed_bind(self):
        created = []
        time_module = FakeTime(6)
        synchronizer = TickingSynchronizer(time_module)

        def factory():
            created.append(1)
            if len(created) == 1:
                raise OSError("bind failed")
            return FakeReceiver()

        with self.assertRaises(KeyboardInterrupt):
            run(
                screen=object(),
                time_module=time_module,
                receiver_factory=factory,
                wifi_factory=lambda: FakeWifi([(True, False)]),
                synchronizer_factory=lambda: synchronizer,
                state_factory=FakeState,
                lunar_factory=FakeLunar,
                weather_factory=FakeWeather,
            )

        self.assertEqual(len(created), 2)
        self.assertEqual(len(FakeReceiver.instances), 1)

    def test_hko_poll_receives_clock_ready_and_peer_busy(self):
        lunar = FakeLunar()
        weather = FakeWeather()
        weather.busy = True

        time_module, _, _, _, _, _ = self._run(
            15,
            ready_at_ms=1000,
            wifi=FakeWifi([(True, False)]),
            lunar=lunar,
            weather=weather,
        )

        self.assertEqual(len(lunar.polls), len(time_module.sleeps))
        for now_ms, year, wifi_connected, clock_ready, peer_busy in lunar.polls:
            self.assertTrue(wifi_connected)
            self.assertTrue(peer_busy)
            if clock_ready:
                self.assertEqual(year, 2026)
            else:
                self.assertIsNone(year)
        self.assertFalse(lunar.polls[0][3])
        self.assertTrue(lunar.polls[-1][3])

    def test_weather_waits_for_a_plausible_clock(self):
        weather = FakeWeather()

        time_module, _, _, _, _, _ = self._run(8, ready_at_ms=500, weather=weather)

        ready_flags = [entry[3] for entry in weather.polls]
        self.assertEqual(ready_flags, [False, False, False, False, False, True, True, True])
        self.assertEqual(len(weather.polls), len(time_module.sleeps))

    def test_metrics_packet_triggers_an_immediate_draw(self):
        state = FakeState()
        receiver_holder = {}

        def factory():
            receiver = FakeReceiver()
            receiver_holder["receiver"] = receiver
            return receiver

        time_module = FakeTime(4)
        synchronizer = TickingSynchronizer(time_module)

        # Feed a payload only into the third poll of the receiver.
        original_poll = FakeReceiver.poll
        results = [None, None, '{"v":1}']
        state_holder = state

        def scripted_poll(receiver_self):
            if receiver_self is receiver_holder.get("receiver") and results:
                return results.pop(0)
            return None

        FakeReceiver.poll = scripted_poll
        try:
            with self.assertRaises(KeyboardInterrupt):
                run(
                    screen=object(),
                    time_module=time_module,
                    receiver_factory=factory,
                    wifi_factory=lambda: FakeWifi([(True, False)]),
                    synchronizer_factory=lambda: synchronizer,
                    state_factory=lambda: state_holder,
                    lunar_factory=FakeLunar,
                    weather_factory=FakeWeather,
                )
        finally:
            FakeReceiver.poll = original_poll

        self.assertEqual(len(state.accepted), 1)
        self.assertEqual([call[1] for call in self.display.calls][:2], ["00:00:00", "00:00:00"])

    def test_receiver_is_closed_when_the_loop_exits(self):
        self._run(3)

        self.assertEqual(FakeReceiver.instances[0].closed, 1)


if __name__ == "__main__":
    unittest.main()
