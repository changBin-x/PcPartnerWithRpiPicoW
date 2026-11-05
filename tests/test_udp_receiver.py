"""Socket hygiene for the UDP listener used by the Pico loop."""

import socket as socket_module
import unittest
from unittest import mock

from pico.udp_receiver import UdpReceiver

AF_INET = 2
SOCK_DGRAM = 2


class FakeSocket:
    def __init__(self, bind_error=None):
        self.bind_error = bind_error
        self.closed = 0
        self.bound = None
        self.blocking = None

    def setblocking(self, flag):
        self.blocking = flag

    def bind(self, address):
        if self.bind_error is not None:
            raise self.bind_error
        self.bound = address

    def close(self):
        self.closed += 1
        return None

    def recvfrom(self, size):
        raise OSError(11)


class UdpReceiverInitTests(unittest.TestCase):
    def test_injected_socket_is_used_as_is(self):
        sock = FakeSocket()

        receiver = UdpReceiver(sock=sock)
        receiver.close()

        self.assertEqual(sock.closed, 1)
        self.assertIsNone(sock.bound)
        self.assertIsNone(sock.blocking)

    def test_created_socket_is_bound_and_non_blocking(self):
        sock = FakeSocket()
        calls = []

        def factory(family, kind):
            calls.append((family, kind))
            return sock

        with mock.patch.object(socket_module, "socket", factory):
            UdpReceiver(port=8765)

        self.assertEqual(calls, [(socket_module.AF_INET, socket_module.SOCK_DGRAM)])
        self.assertEqual(sock.bound, ("0.0.0.0", 8765))
        self.assertFalse(sock.blocking)
        self.assertEqual(sock.closed, 0)

    def test_bind_failure_closes_the_socket_it_created(self):
        sock = FakeSocket(bind_error=OSError("address in use"))

        with mock.patch.object(socket_module, "socket", lambda family, kind: sock):
            with self.assertRaises(OSError):
                UdpReceiver(port=8765)

        self.assertEqual(sock.closed, 1)
        self.assertIsNone(sock.bound)


if __name__ == "__main__":
    unittest.main()
