"""Entry point that turns application crashes into a visible error page."""

RESTART_DELAY_MS = 2000
# ports/rp2/machine_wdt.c rejects timeouts above 8388 ms.
WATCHDOG_TIMEOUT_MS = 8000


def _render_error(error, screen=None):
    """Draw a short error page, reusing the display when one is available."""
    error_name = type(error).__name__
    try:
        from pico.display import AMBER, BG, MUTED

        if screen is None:
            from pico.st7735 import ST7735

            screen = ST7735()
        screen.fill(BG)
        screen.text(4, 30, "APP ERROR", AMBER)
        screen.text(4, 58, error_name[:14], MUTED)
        screen.text(4, 86, "RESTARTING", MUTED)
    except Exception:
        # Printing only the type name keeps secrets out of the serial log.
        print("Pico dashboard error: %s" % error_name)


def _make_watchdog(machine_module):
    """Return a hardware watchdog when the port exposes one, else None."""
    wdt_class = getattr(machine_module, "WDT", None)
    if wdt_class is None:
        return None

    def _fail(error):
        print("Pico WDT error: %s" % type(error).__name__)
        return None

    try:
        return wdt_class(timeout=WATCHDOG_TIMEOUT_MS)
    except TypeError:
        # Some ports accept the id positionally instead of as a keyword.
        try:
            return wdt_class(0, WATCHDOG_TIMEOUT_MS)
        except AttributeError:
            return None
        except Exception as error:
            return _fail(error)
    except AttributeError:
        # Ports without a usable WDT simply run unprotected.
        return None
    except Exception as error:
        return _fail(error)


def _report_reset_cause(machine_module):
    """Log the reset cause once so a watchdog reboot loop is visible."""
    try:
        cause = machine_module.reset_cause()
    except Exception:
        return None
    print("Pico reset cause: %s" % cause)
    return cause


def main(
    app_runner=None,
    machine_module=None,
    sleep_ms=None,
    error_renderer=None,
    screen_factory=None,
    watchdog_factory=None,
):
    """Run the dashboard and replace any escaping exception with an error page.

    The display and watchdog are created once here and handed to the app, so a
    crash does not re-initialise the panel a second time before the reboot.
    """
    if machine_module is None:
        import machine as machine_module
    _report_reset_cause(machine_module)

    if screen_factory is None:
        try:
            from pico.st7735 import ST7735 as screen_factory
        except ImportError:
            screen_factory = None

    screen = None
    if screen_factory is not None:
        try:
            screen = screen_factory()
        except Exception:
            screen = None

    watchdog = None
    factory = watchdog_factory
    if factory is None:
        factory = lambda: _make_watchdog(machine_module)
    try:
        watchdog = factory()
    except Exception:
        watchdog = None

    try:
        if app_runner is None:
            # Import inside the guarded block so a partial deploy still shows
            # the error page instead of dying with a bare REPL traceback.
            try:
                from pico.app import run as app_runner
            except ImportError:
                from app import run as app_runner
        app_runner(screen=screen, watchdog=watchdog)
        raise RuntimeError("application returned unexpectedly")
    except KeyboardInterrupt:
        raise
    except Exception as error:
        renderer = error_renderer or _render_error
        try:
            renderer(error, screen)
        except Exception:
            print("Pico dashboard error: %s" % type(error).__name__)
        if sleep_ms is None:
            import time

            sleep_ms = time.sleep_ms
        sleep_ms(RESTART_DELAY_MS)
        machine_module.soft_reset()


if __name__ == "__main__":
    main()
