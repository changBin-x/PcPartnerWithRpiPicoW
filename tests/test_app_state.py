"""Tests for the Pico metrics freshness state machine."""

import unittest

from pc_metrics.protocol import encode_packet
from pico.app import DashboardState


def packet(seq=1, sampled_at=1_798_000_000, **overrides):
    fields = {
        "seq": seq,
        "sampled_at": sampled_at,
        "cpu": 10.0,
        "gpu": None,
        "ram": 20.0,
        "rx": 0.0,
        "tx": 0.0,
    }
    fields.update(overrides)
    return encode_packet(**fields)


class DashboardStateTests(unittest.TestCase):
    def test_starts_in_clock_mode_without_a_valid_packet(self):
        state = DashboardState()

        self.assertEqual(state.mode(100), "clock")

    def test_valid_packet_selects_dashboard_until_2999_ms(self):
        state = DashboardState()

        self.assertTrue(state.accept_datagram(packet(), 100))
        self.assertEqual(state.mode(100), "dashboard")
        self.assertEqual(state.mode(3099), "dashboard")

    def test_switches_to_clock_at_exactly_3000_ms(self):
        state = DashboardState()
        state.accept_datagram(packet(), 100)

        self.assertEqual(state.mode(3100), "clock")

    def test_new_valid_packet_immediately_restores_dashboard(self):
        state = DashboardState()
        state.accept_datagram(packet(seq=1), 100)
        self.assertEqual(state.mode(3100), "clock")

        self.assertTrue(state.accept_datagram(packet(seq=2, sampled_at=1_798_000_001), 3101))
        self.assertEqual(state.mode(3101), "dashboard")

    def test_invalid_packet_does_not_refresh_freshness(self):
        state = DashboardState()
        valid = packet()
        self.assertTrue(state.accept_datagram(valid, 100))

        self.assertFalse(state.accept_datagram(b"{bad json", 3099))
        self.assertEqual(state.mode(3100), "clock")

    def test_out_of_range_packet_does_not_refresh_freshness(self):
        state = DashboardState()
        self.assertTrue(state.accept_datagram(packet(), 100))
        invalid = (
            b'{"v":1,"seq":2,"ts":1798000001,"cpu":101,"gpu":null,'
            b'"ram":20,"rx":0,"tx":0}'
        )

        self.assertFalse(state.accept_datagram(invalid, 3099))
        self.assertEqual(state.mode(3100), "clock")

    def test_oversized_packet_is_rejected(self):
        state = DashboardState()

        self.assertFalse(state.accept_datagram(b"{" + b" " * 512 + b"}", 100))
        self.assertIsNone(state.last_received_ms)

    def test_duplicate_packet_does_not_refresh_freshness(self):
        state = DashboardState()
        valid = packet(seq=4)
        self.assertTrue(state.accept_datagram(valid, 100))

        self.assertFalse(state.accept_datagram(valid, 3099))
        self.assertEqual(state.mode(3100), "clock")

    def test_sender_restart_with_newer_timestamp_is_accepted(self):
        state = DashboardState()
        self.assertTrue(state.accept_datagram(packet(seq=100), 100))

        self.assertTrue(
            state.accept_datagram(
                packet(seq=0, sampled_at=1_798_000_001),
                101,
            )
        )
        self.assertEqual(state.mode(101), "dashboard")

    def test_freshness_wraps_across_ticks_period(self):
        state = DashboardState()
        period = 1 << 30
        last = period - 5
        state.accept_datagram(packet(), last)

        self.assertEqual(state.mode(2994), "dashboard")
        self.assertEqual(state.mode(2995), "clock")


if __name__ == "__main__":
    unittest.main()
