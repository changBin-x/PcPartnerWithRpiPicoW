"""Persistent MicroPython entry point with recoverable application errors."""


RESTART_DELAY_MS = 2000


def _render_error(error):
    """Show a short, credential-safe error and restart hint when possible."""
    error_name = type(error).__name__
    try:
        from pico.display import AMBER, BG, MUTED
        from pico.st7735 import ST7735

        screen = ST7735()
        screen.fill(BG)
        screen.text(8, 42, "APP ERROR", AMBER, BG)
        screen.text(8, 64, error_name[:14], MUTED, BG)
        screen.text(8, 88, "RESTARTING", MUTED, BG)
    except Exception:
        # Never print exception text: downstream errors can contain secrets.
        print("Pico dashboard error: %s" % error_name)


def main(app_runner=None, machine_module=None, sleep_ms=None, error_renderer=None):
    """Run the application; recover ordinary failures after a visible delay."""
    try:
        if app_runner is None:
            from pico.app import run

            app_runner = run
        app_runner()
        raise RuntimeError("application returned unexpectedly")
    except KeyboardInterrupt:
        raise
    except Exception as error:
        renderer = error_renderer if error_renderer is not None else _render_error
        try:
            renderer(error)
        except Exception:
            pass

        if sleep_ms is None:
            import time

            sleep_ms = time.sleep_ms
        if machine_module is None:
            import machine

            machine_module = machine
        sleep_ms(RESTART_DELAY_MS)
        machine_module.soft_reset()


if __name__ == "__main__":
    main()
