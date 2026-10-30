"""Host-side tests for non-blocking Wi-Fi connection retries."""

import unittest

from pico.wifi import WifiManager


class FakeStation:
    def __init__(self):
        self.connected = False
        self.active_calls = 0
        self.connect_calls = []
        self.disconnect_calls = 0

    def active(self, value):
        self.active_calls += 1

    def isconnected(self):
        return self.connected

    def connect(self, ssid, password):
        self.connect_calls.append((ssid, password))

    def disconnect(self):
        self.disconnect_calls += 1
        self.connected = False


class WifiManagerTests(unittest.TestCase):
    def test_missing_credentials_do_not_attempt_a_connection(self):
        manager = WifiManager(credentials=("", ""))

        self.assertTrue(manager.needs_config)
        self.assertEqual(manager.poll(0), (False, False))

    def test_connects_reconnects_and_reports_new_link(self):
        station = FakeStation()
        manager = WifiManager(credentials=("SSID", "secret"), station=station)

        self.assertEqual(manager.poll(0), (False, False))
        self.assertEqual(station.connect_calls, [("SSID", "secret")])
        station.connected = True
        self.assertEqual(manager.poll(100), (True, True))
        self.assertEqual(manager.poll(200), (True, False))

        station.connected = False
        self.assertEqual(manager.poll(300), (False, False))
        self.assertEqual(station.connect_calls, [("SSID", "secret"), ("SSID", "secret")])

    def test_connection_attempt_times_out_without_blocking_poll(self):
        station = FakeStation()
        manager = WifiManager(credentials=("SSID", "secret"), station=station)
        manager.poll(0)

        self.assertEqual(manager.poll(14_999), (False, False))
        self.assertEqual(len(station.connect_calls), 1)
        self.assertEqual(manager.poll(15_000), (False, False))
        self.assertEqual(station.disconnect_calls, 1)
        self.assertEqual(manager.poll(15_999), (False, False))
        self.assertEqual(len(station.connect_calls), 1)
        self.assertEqual(manager.poll(16_000), (False, False))
        self.assertEqual(len(station.connect_calls), 2)


if __name__ == "__main__":
    unittest.main()
