import unittest

from pico.main import RESTART_DELAY_MS, main


class FakeMachine:
    def __init__(self):
        self.reset_count = 0

    def soft_reset(self):
        self.reset_count += 1


class MainTests(unittest.TestCase):
    def test_application_error_is_shown_then_soft_reset(self):
        machine = FakeMachine()
        sleeps = []
        rendered = []

        def fail():
            raise ValueError("wifi password must never be echoed")

        main(
            app_runner=fail,
            machine_module=machine,
            sleep_ms=sleeps.append,
            error_renderer=rendered.append,
        )

        self.assertEqual(machine.reset_count, 1)
        self.assertEqual(sleeps, [RESTART_DELAY_MS])
        self.assertEqual(len(rendered), 1)
        self.assertIsInstance(rendered[0], ValueError)

    def test_keyboard_interrupt_stops_without_reset(self):
        machine = FakeMachine()
        sleeps = []

        def interrupt():
            raise KeyboardInterrupt()

        with self.assertRaises(KeyboardInterrupt):
            main(
                app_runner=interrupt,
                machine_module=machine,
                sleep_ms=sleeps.append,
                error_renderer=lambda error: None,
            )

        self.assertEqual(machine.reset_count, 0)
        self.assertEqual(sleeps, [])

    def test_unexpected_return_is_treated_as_failure(self):
        machine = FakeMachine()
        rendered = []

        main(
            app_runner=lambda: None,
            machine_module=machine,
            sleep_ms=lambda delay: None,
            error_renderer=rendered.append,
        )

        self.assertEqual(machine.reset_count, 1)
        self.assertIsInstance(rendered[0], RuntimeError)


if __name__ == "__main__":
    unittest.main()
