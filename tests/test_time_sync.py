"""Host-side tests for NTP packet parsing and RTC scheduling."""

import struct
import time
import unittest
from unittest.mock import patch

from pico.time_sync import (
    NTP_ERA_SECONDS,
    NTP_HOSTS,
    NTP_UNIX_OFFSET,
    RESYNC_INTERVAL_MS,
    TimeSynchronizer,
    _parse_ntp_response,
    ntp_seconds_to_unix,
)


class NtpConversionTests(unittest.TestCase):
    def test_converts_ntp_seconds_in_first_era(self):
        unix_seconds = 1_798_000_000
        raw_ntp_seconds = (unix_seconds + NTP_UNIX_OFFSET) % NTP_ERA_SECONDS

        self.assertEqual(ntp_seconds_to_unix(raw_ntp_seconds), unix_seconds)

    def test_uses_valid_rtc_to_resolve_second_ntp_era(self):
        unix_seconds = 2_208_988_800
        raw_ntp_seconds = (unix_seconds + NTP_UNIX_OFFSET) % NTP_ERA_SECONDS

        self.assertEqual(
            ntp_seconds_to_unix(raw_ntp_seconds, unix_seconds - 60),
            unix_seconds,
        )

    def test_rejects_unsynchronized_or_non_server_ntp_packets(self):
        packet = bytearray(48)
        packet[0] = 0xE4  # LI=3 means unsynchronized.
        packet[1] = 2
        struct.pack_into("!I", packet, 40, 4_000_000_000)
        with self.assertRaises(ValueError):
            _parse_ntp_response(packet)

        packet[0] = 0x23  # Client mode instead of server mode.
        with self.assertRaises(ValueError):
            _parse_ntp_response(packet)


class TimeSynchronizerTests(unittest.TestCase):
    def test_factory_default_rtc_is_invalid_but_reasonable_current_rtc_is_valid(self):
        real_gmtime = time.gmtime

        def rtc_gmtime(year):
            return (year, 1, 1, 0, 0, 0, 4, 1, 0)

        def patched_gmtime(*args):
            if not args:
                return rtc_gmtime(current_year[0])
            return real_gmtime(*args)

        current_year = [2021]
        epoch = [1_609_462_492]
        with patch("pico.time_sync.time.gmtime", side_effect=patched_gmtime), patch(
            "pico.time_sync.time.time", side_effect=lambda: epoch[0]
        ):
            default_snapshot = TimeSynchronizer().snapshot()
            self.assertIsNone(default_snapshot["time_text"])
            self.assertIsNone(default_snapshot["date"])

            current_year[0] = 2026
            epoch[0] = 1_798_000_000
            current_snapshot = TimeSynchronizer().snapshot()
            self.assertIsNotNone(current_snapshot["time_text"])
            self.assertEqual(current_snapshot["year"], 2026)

    def test_tries_configured_hosts_in_order_and_schedules_six_hour_refresh(self):
        calls = []
        rtc_values = []

        class FakeRtc:
            def datetime(self, value):
                rtc_values.append(value)

        def query(host, _approximate_unix):
            calls.append(host)
            if host != NTP_HOSTS[-1]:
                raise OSError("no response")
            return 1_798_000_000

        sync = TimeSynchronizer(rtc=FakeRtc(), query_host=query)
        self.assertTrue(sync.sync_if_due(100, wifi_connected=True))
        self.assertEqual(calls, list(NTP_HOSTS))
        self.assertEqual(len(rtc_values), 1)
        self.assertTrue(sync.synced)

        calls.clear()
        self.assertTrue(sync.sync_if_due(101, wifi_connected=True))
        self.assertEqual(calls, [])
        self.assertTrue(
            sync.sync_if_due(100 + RESYNC_INTERVAL_MS, wifi_connected=True)
        )
        self.assertEqual(calls, list(NTP_HOSTS))

    def test_failed_sync_does_not_modify_rtc_and_retries_after_backoff(self):
        rtc_values = []
        calls = []

        class FakeRtc:
            def datetime(self, value):
                rtc_values.append(value)

        def fail(host, _approximate_unix):
            calls.append(host)
            raise OSError("offline")

        sync = TimeSynchronizer(rtc=FakeRtc(), query_host=fail)
        self.assertFalse(sync.sync_if_due(500, wifi_connected=True))
        self.assertFalse(sync.synced)
        self.assertEqual(rtc_values, [])
        first_count = len(calls)

        self.assertFalse(sync.sync_if_due(30_499, wifi_connected=True))
        self.assertEqual(len(calls), first_count)
        self.assertFalse(sync.sync_if_due(30_500, wifi_connected=True))
        self.assertEqual(len(calls), first_count + len(NTP_HOSTS))


if __name__ == "__main__":
    unittest.main()
