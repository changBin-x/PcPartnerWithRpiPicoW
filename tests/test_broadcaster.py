import socket
import unittest
from types import SimpleNamespace

from pc_metrics.broadcaster import get_broadcast_address, send_packet
from pc_metrics.host import _next_deadline, _select_interface_ipv4
from pc_metrics.protocol import MAX_PACKET_BYTES


class FakeSocket:
    def __init__(self, send_error=None):
        self.options = []
        self.sent = []
        self.bound = False
        self.send_error = send_error

    def setsockopt(self, *args):
        self.options.append(args)

    def sendto(self, packet, address):
        if self.send_error is not None:
            raise self.send_error
        self.sent.append((packet, address))
        return len(packet)

    def bind(self, _address):
        self.bound = True


class BroadcasterTests(unittest.TestCase):
    def test_calculates_directed_broadcast(self):
        self.assertEqual(
            get_broadcast_address("192.168.10.9", "255.255.255.0"),
            "192.168.10.255",
        )

    def test_rejects_invalid_interface_address(self):
        with self.assertRaises(ValueError):
            get_broadcast_address("", "255.255.255.0")

    def test_rejects_noncontiguous_netmask(self):
        with self.assertRaises(ValueError):
            get_broadcast_address("192.168.10.9", "255.0.255.0")

    def test_rejects_subnets_without_directed_broadcast(self):
        with self.assertRaises(ValueError):
            get_broadcast_address("192.168.10.9", "255.255.255.254")
        with self.assertRaises(ValueError):
            get_broadcast_address("192.168.10.9", "0.0.0.0")

    def test_rejects_loopback_interface_address(self):
        with self.assertRaises(ValueError):
            get_broadcast_address("127.0.0.1", "255.0.0.0")

    def test_selects_subnet_containing_selected_adapter_gateway(self):
        entries = [
            SimpleNamespace(
                family=socket.AF_INET,
                address="192.168.2.1",
                netmask="255.255.255.0",
            ),
            SimpleNamespace(
                family=socket.AF_INET,
                address="192.168.10.9",
                netmask="255.255.255.0",
            ),
        ]

        self.assertEqual(
            _select_interface_ipv4(entries, default_gateway="192.168.10.1"),
            ("192.168.10.9", "192.168.10.255"),
        )

    def test_requires_explicit_address_when_multiple_subnets_are_ambiguous(self):
        entries = [
            SimpleNamespace(
                family=socket.AF_INET,
                address="192.168.2.1",
                netmask="255.255.255.0",
            ),
            SimpleNamespace(
                family=socket.AF_INET,
                address="192.168.10.9",
                netmask="255.255.255.0",
            ),
        ]

        with self.assertRaisesRegex(RuntimeError, "ETHERNET_IPV4_ADDRESS"):
            _select_interface_ipv4(entries, default_gateway=None)

    def test_accepts_explicit_address_override(self):
        entries = [
            SimpleNamespace(
                family=socket.AF_INET,
                address="192.168.2.1",
                netmask="255.255.255.0",
            ),
            SimpleNamespace(
                family=socket.AF_INET,
                address="192.168.10.9",
                netmask="255.255.255.0",
            ),
        ]

        self.assertEqual(
            _select_interface_ipv4(
                entries,
                default_gateway=None,
                configured_address="192.168.2.1",
            ),
            ("192.168.2.1", "192.168.2.255"),
        )

    def test_sends_udp_broadcast_without_binding_receive_port(self):
        fake_socket = FakeSocket()

        sent_length = send_packet(
            fake_socket,
            b'{"v":1}',
            "192.168.10.255",
        )

        self.assertEqual(sent_length, 7)
        self.assertEqual(fake_socket.options, [])
        self.assertEqual(
            fake_socket.sent,
            [(b'{"v":1}', ("192.168.10.255", 8765))],
        )
        self.assertFalse(fake_socket.bound)

    def test_does_not_reconfigure_socket_on_each_send(self):
        fake_socket = FakeSocket()
        send_packet(fake_socket, b"{}", "192.168.10.255")
        send_packet(fake_socket, b"{}", "192.168.10.255")

        self.assertEqual(fake_socket.options, [])
        self.assertEqual(len(fake_socket.sent), 2)

    def test_accepts_packet_exactly_at_the_byte_limit(self):
        fake_socket = FakeSocket()
        packet = b"x" * MAX_PACKET_BYTES

        self.assertEqual(send_packet(fake_socket, packet, "192.168.10.255"),
                         MAX_PACKET_BYTES)

    def test_rejects_packet_over_the_byte_limit(self):
        fake_socket = FakeSocket()
        packet = b"x" * (MAX_PACKET_BYTES + 1)

        with self.assertRaisesRegex(ValueError, "512"):
            send_packet(fake_socket, packet, "192.168.10.255")
        self.assertEqual(fake_socket.sent, [])

    def test_rejects_invalid_udp_port(self):
        for port in (0, -1, 65536, True, "8765", 8765.0):
            with self.subTest(port=port), self.assertRaisesRegex(ValueError, "端口"):
                send_packet(FakeSocket(), b"{}", "192.168.10.255", port=port)

    def test_propagates_socket_errors(self):
        fake_socket = FakeSocket(send_error=OSError("网络不可达"))

        with self.assertRaisesRegex(OSError, "网络不可达"):
            send_packet(fake_socket, b"{}", "192.168.10.255")
        self.assertEqual(fake_socket.sent, [])

    def test_scheduler_skips_missed_intervals_without_catch_up(self):
        self.assertEqual(_next_deadline(10.0, 1.0, 13.4), 14.0)


if __name__ == "__main__":
    unittest.main()
