import io
import unittest
from contextlib import redirect_stdout

from pico.main import RESTART_DELAY_MS, WATCHDOG_TIMEOUT_MS, main


class FakeMachine:
    def __init__(self, wdt_class=None):
        self.reset_count = 0
        self.wdt_class = wdt_class

    def soft_reset(self):
        self.reset_count += 1

    def WDT(self, timeout=None):  # noqa: N802 - mirrors MicroPython's name
        if self.wdt_class is None:
            raise AttributeError("WDT")
        return self.wdt_class(timeout=timeout)


class FakeWatchdog:
    def __init__(self, timeout=None):
        self.timeout = timeout
        self.feed_count = 0

    def feed(self):
        self.feed_count += 1


class FakeScreen:
    def __init__(self):
        self.fills = []
        self.texts = []

    def fill(self, color):
        self.fills.append(color)

    def text(self, text, x, y, color):
        self.texts.append((text, x, y, color))


class MainTests(unittest.TestCase):
    def _run(self, runner, machine=None, renderer=None, screen_factory=None, sleeps=None):
        machine = machine or FakeMachine()
        sleeps = [] if sleeps is None else sleeps
        rendered = []

        def render(error, screen):
            rendered.append((error, screen))

        main(
            app_runner=runner,
            machine_module=machine,
            sleep_ms=sleeps.append,
            error_renderer=renderer or render,
            screen_factory=screen_factory,
        )
        return machine, sleeps, rendered

    def test_application_error_is_shown_then_soft_reset(self):
        screen = FakeScreen()

        def fail(screen=None, watchdog=None):
            raise ValueError("wifi password must never be echoed")

        machine, sleeps, rendered = self._run(fail, screen_factory=lambda: screen)

        self.assertEqual(machine.reset_count, 1)
        self.assertEqual(sleeps, [RESTART_DELAY_MS])
        self.assertEqual(len(rendered), 1)
        self.assertIsInstance(rendered[0][0], ValueError)
        # The error page reuses the app's panel instead of re-initialising it.
        self.assertIs(rendered[0][1], screen)

    def test_default_error_renderer_reuses_supplied_screen(self):
        screen = FakeScreen()

        def fail(screen=None, watchdog=None):
            raise RuntimeError("boom")

        # No error_renderer: the real _render_error draws on the same panel.
        main(
            app_runner=fail,
            machine_module=FakeMachine(),
            sleep_ms=lambda delay: None,
            screen_factory=lambda: screen,
        )

        self.assertEqual(len(screen.fills), 1)
        self.assertEqual(
            [entry[0] for entry in screen.texts],
            ["APP ERROR", "RuntimeError", "RESTARTING"],
        )

    def test_screen_failure_does_not_prevent_the_error_page(self):
        def fail(screen=None, watchdog=None):
            raise RuntimeError("boom")

        def broken_screen():
            raise OSError("no display")

        machine, sleeps, rendered = self._run(fail, screen_factory=broken_screen)

        self.assertEqual(machine.reset_count, 1)
        self.assertEqual(len(rendered), 1)
        self.assertIsNone(rendered[0][1])

    def test_watchdog_is_created_and_handed_to_the_app(self):
        seen = {}

        def runner(screen=None, watchdog=None):
            seen["watchdog"] = watchdog
            raise ValueError("stop after recording")

        machine = FakeMachine(wdt_class=FakeWatchdog)
        sleeps = []
        main(
            app_runner=runner,
            machine_module=machine,
            sleep_ms=sleeps.append,
            error_renderer=lambda error, screen: None,
            screen_factory=lambda: None,
        )

        self.assertIsInstance(seen["watchdog"], FakeWatchdog)
        self.assertEqual(seen["watchdog"].timeout, WATCHDOG_TIMEOUT_MS)
        self.assertEqual(machine.reset_count, 1)

    def test_watchdog_timeout_stays_within_the_rp2_limit(self):
        # ports/rp2/machine_wdt.c raises above 8388 ms, which would silently
        # disable the watchdog instead of protecting the loop.
        self.assertLessEqual(WATCHDOG_TIMEOUT_MS, 8388)

    def test_wdt_value_error_is_reported_and_tolerated(self):
        class LimitedMachine(FakeMachine):
            def WDT(self, timeout=None):  # noqa: N802 - mirrors MicroPython
                raise ValueError("timeout exceeds 8388")

        machine = LimitedMachine()
        seen = []
        stdout = io.StringIO()

        with redirect_stdout(stdout):
            main(
                app_runner=lambda screen=None, watchdog=None: seen.append(watchdog),
                machine_module=machine,
                sleep_ms=lambda delay: None,
                error_renderer=lambda error, screen: None,
                screen_factory=lambda: FakeScreen(),
            )

        self.assertEqual(seen, [None])
        self.assertIn("Pico WDT error: ValueError", stdout.getvalue())
        self.assertEqual(machine.reset_count, 1)

    def test_missing_watchdog_class_degrades_to_none(self):
        seen = {}

        def runner(screen=None, watchdog=None):
            seen["watchdog"] = watchdog
            raise ValueError("stop after recording")

        machine = FakeMachine()
        main(
            app_runner=runner,
            machine_module=machine,
            sleep_ms=lambda delay: None,
            error_renderer=lambda error, screen: None,
            screen_factory=lambda: None,
        )

        self.assertIsNone(seen["watchdog"])

    def test_reset_cause_is_reported_once(self):
        machine = FakeMachine()
        machine.reset_cause = lambda: 3
        output = io.StringIO()

        with redirect_stdout(output):
            main(
                app_runner=lambda screen=None, watchdog=None: (_ for _ in ()).throw(
                    ValueError("boom")
                ),
                machine_module=machine,
                sleep_ms=lambda delay: None,
                error_renderer=lambda error, screen: None,
                screen_factory=lambda: None,
            )

        self.assertIn("Pico reset cause: 3", output.getvalue())

    def test_keyboard_interrupt_stops_without_reset(self):
        machine = FakeMachine()
        sleeps = []

        def interrupt(screen=None, watchdog=None):
            raise KeyboardInterrupt()

        with self.assertRaises(KeyboardInterrupt):
            main(
                app_runner=interrupt,
                machine_module=machine,
                sleep_ms=sleeps.append,
                error_renderer=lambda error, screen: None,
                screen_factory=lambda: None,
            )

        self.assertEqual(machine.reset_count, 0)
        self.assertEqual(sleeps, [])

    def test_unexpected_return_is_treated_as_failure(self):
        machine = FakeMachine()
        rendered = []

        main(
            app_runner=lambda screen=None, watchdog=None: None,
            machine_module=machine,
            sleep_ms=lambda delay: None,
            error_renderer=lambda error, screen: rendered.append((error, screen)),
            screen_factory=lambda: None,
        )

        self.assertEqual(machine.reset_count, 1)
        self.assertIsInstance(rendered[0][0], RuntimeError)


if __name__ == "__main__":
    unittest.main()
