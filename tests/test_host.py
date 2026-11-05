"""Tests for the host loop, interface selection, and socket ownership."""

import contextlib
import io
import socket
import unittest
from types import SimpleNamespace
from unittest.mock import MagicMock, patch

import psutil

from pc_metrics import config, host
from pc_metrics.collectors import InterfaceNotFoundError
from pc_metrics.protocol import decode_packet

SAMPLE = {"cpu": 12.5, "gpu": 30.0, "ram": 40.0, "rx": 1.5, "tx": 0.5}

SETTINGS = {
    "ETHERNET_INTERFACE": "以太网",
    "ETHERNET_IPV4_ADDRESS": "",
    "UDP_PORT": 8765,
    "SAMPLE_INTERVAL_SECONDS": 1.0,
}


def settings(**overrides):
    """Patch every host setting, applying the requested overrides."""
    values = dict(SETTINGS)
    values.update(overrides)
    return patch.multiple(config, **values)


class StopRun(Exception):
    """Raised by the fake sleeper to end the endless run loop."""


class FakeSocket:
    def __init__(self, send_failures=0, bind_error=None):
        self.options = []
        self.bound = []
        self.sent = []
        self.closed = False
        self.bind_error = bind_error
        self._send_failures = send_failures

    def setsockopt(self, *args):
        self.options.append(args)

    def bind(self, address):
        if self.bind_error is not None:
            raise self.bind_error
        self.bound.append(address)

    def sendto(self, packet, address):
        if self._send_failures:
            self._send_failures -= 1
            raise OSError("网络不可达")
        self.sent.append((packet, address))
        return len(packet)

    def close(self):
        self.closed = True


class FakeCollector:
    def __init__(self, samples, *, gpu_unavailable=False, drop_reason="网络计数器重置"):
        self._samples = list(samples)
        self.gpu_unavailable = gpu_unavailable
        self.last_drop_reason = None
        self._drop_reason = drop_reason

    def sample(self):
        result = self._samples.pop(0)
        if isinstance(result, BaseException):
            raise result
        self.last_drop_reason = self._drop_reason if result is None else None
        return result


def entry(address, netmask="255.255.255.0", family=socket.AF_INET):
    return SimpleNamespace(family=family, address=address, netmask=netmask)


class HostRunTests(unittest.TestCase):
    def run_loop(
        self,
        *,
        resolve=None,
        collector=None,
        sockets=None,
        stop_after=1,
    ):
        """Run the loop with fakes until the sleeper has been called enough."""
        sleeps = []
        created = []
        if sockets is None:
            sockets = [FakeSocket()]
        socket_iter = iter(sockets)
        if resolve is None:
            endpoints = [("192.168.1.10", "192.168.1.255")]
        else:
            endpoints = list(resolve)
        resolve_mock = MagicMock(side_effect=endpoints)
        if collector is None:
            collector = FakeCollector([SAMPLE])

        def make_socket(address):
            created.append((address, next(socket_iter)))
            return created[-1][1]

        def stop_sleep(delay):
            sleeps.append(delay)
            if len(sleeps) >= stop_after:
                raise StopRun

        stderr = io.StringIO()
        stdout = io.StringIO()
        with settings(), contextlib.redirect_stderr(stderr), contextlib.redirect_stdout(
            stdout
        ):
            with self.assertRaises(StopRun):
                host.run(
                    resolve_endpoint=resolve_mock,
                    collector_factory=lambda _name: collector,
                    socket_factory=make_socket,
                    sleep=stop_sleep,
                    monotonic=lambda: 0.0,
                    wall_clock=lambda: 1_700_000_000.0,
                )
        return SimpleNamespace(
            sleeps=sleeps,
            created=created,
            resolve=resolve_mock,
            stderr=stderr.getvalue(),
            stdout=stdout.getvalue(),
        )

    def test_invalid_settings_exit_with_code_two(self):
        cases = (
            {"ETHERNET_INTERFACE": ""},
            {"ETHERNET_INTERFACE": "   "},
            {"ETHERNET_INTERFACE": None},
            {"ETHERNET_IPV4_ADDRESS": 123},
            {"UDP_PORT": 0},
            {"UDP_PORT": 65536},
            {"UDP_PORT": True},
            {"UDP_PORT": "8765"},
            {"SAMPLE_INTERVAL_SECONDS": 0},
            {"SAMPLE_INTERVAL_SECONDS": -1.0},
            {"SAMPLE_INTERVAL_SECONDS": float("nan")},
            {"SAMPLE_INTERVAL_SECONDS": float("inf")},
        )
        for overrides in cases:
            stderr = io.StringIO()
            with self.subTest(overrides=overrides), settings(**overrides), (
                contextlib.redirect_stderr(stderr)
            ):
                self.assertEqual(host.run(), 2)
            self.assertIn("启动失败", stderr.getvalue())

    def test_interface_resolution_failure_exits_with_code_two(self):
        stderr = io.StringIO()
        resolve = MagicMock(side_effect=RuntimeError("网卡不存在"))
        with settings(), contextlib.redirect_stderr(stderr):
            self.assertEqual(
                host.run(resolve_endpoint=resolve), 2
            )
        self.assertIn("启动失败：网卡不存在", stderr.getvalue())

    def test_socket_creation_failure_exits_with_code_two(self):
        stderr = io.StringIO()
        with settings(), contextlib.redirect_stderr(stderr):
            self.assertEqual(
                host.run(
                    resolve_endpoint=lambda _name: ("192.168.1.10", "192.168.1.255"),
                    collector_factory=lambda _name: FakeCollector([SAMPLE]),
                    socket_factory=MagicMock(side_effect=OSError("无法绑定")),
                ),
                2,
            )
        self.assertIn("启动失败：无法绑定", stderr.getvalue())

    def test_interface_loss_rebuilds_socket_and_keeps_running(self):
        first = FakeSocket()
        second = FakeSocket()
        outcome = self.run_loop(
            resolve=[
                ("192.168.1.10", "192.168.1.255"),
                ("192.168.1.20", "192.168.1.255"),
            ],
            collector=FakeCollector(
                [InterfaceNotFoundError("未找到有线网卡: 以太网"), SAMPLE]
            ),
            sockets=[first, second],
            stop_after=2,
        )

        self.assertTrue(first.closed)
        self.assertEqual(len(outcome.created), 2)
        self.assertEqual(
            [address for address, _sock in outcome.created],
            ["192.168.1.10", "192.168.1.20"],
        )
        self.assertEqual(outcome.resolve.call_count, 2)
        self.assertEqual(len(second.sent), 1)
        packet, destination = second.sent[0]
        self.assertEqual(decode_packet(packet)["seq"], 0)
        self.assertEqual(destination, ("192.168.1.255", 8765))
        self.assertIn("未找到有线网卡", outcome.stderr)
        self.assertEqual(outcome.sleeps[0], 1.0)

    def test_payload_error_retries_on_the_interval_without_rebuilding(self):
        fake_socket = FakeSocket()
        outcome = self.run_loop(
            collector=FakeCollector([dict(SAMPLE, cpu=200), SAMPLE]),
            sockets=[fake_socket],
            stop_after=2,
        )

        # A deterministic encoding error must not grow the backoff or swap the
        # socket: it waits one sampling interval and tries again.
        self.assertEqual(outcome.sleeps[0], 1.0)
        self.assertEqual(len(outcome.created), 1)
        self.assertEqual(len(fake_socket.sent), 1)
        self.assertIn("数据无效", outcome.stderr)
        packet, _destination = fake_socket.sent[0]
        self.assertEqual(decode_packet(packet)["seq"], 0)

    def test_malformed_sample_is_reported_without_killing_the_loop(self):
        fake_socket = FakeSocket()
        outcome = self.run_loop(
            collector=FakeCollector([{"ram": 1.0}, SAMPLE]),
            sockets=[fake_socket],
            stop_after=2,
        )

        self.assertIn("数据无效", outcome.stderr)
        self.assertEqual(len(fake_socket.sent), 1)

    def test_send_failure_does_not_increment_sequence(self):
        first = FakeSocket(send_failures=1)
        second = FakeSocket()
        outcome = self.run_loop(
            resolve=[
                ("192.168.1.10", "192.168.1.255"),
                ("192.168.1.20", "192.168.1.255"),
            ],
            collector=FakeCollector([SAMPLE, SAMPLE]),
            sockets=[first, second],
            stop_after=2,
        )

        self.assertTrue(first.closed)
        self.assertEqual(first.sent, [])
        self.assertEqual(len(second.sent), 1)
        packet, _destination = second.sent[0]
        self.assertEqual(decode_packet(packet)["seq"], 0)
        self.assertIn("网络不可达", outcome.stderr)

    def test_successful_sends_increment_sequence(self):
        fake_socket = FakeSocket()
        self.run_loop(
            collector=FakeCollector([SAMPLE, SAMPLE]),
            sockets=[fake_socket],
            stop_after=2,
        )

        self.assertEqual(len(fake_socket.sent), 2)
        sequences = [decode_packet(packet)["seq"] for packet, _ in fake_socket.sent]
        self.assertEqual(sequences, [0, 1])
        self.assertTrue(fake_socket.closed)

    def test_drop_warning_is_printed_once(self):
        outcome = self.run_loop(
            collector=FakeCollector([None] * 5 + [SAMPLE]),
            stop_after=6,
        )

        self.assertEqual(outcome.stderr.count("连续 5 次"), 1)
        self.assertIn("网络计数器重置", outcome.stderr)

    def test_gpu_warning_is_printed_once(self):
        outcome = self.run_loop(
            collector=FakeCollector([SAMPLE, SAMPLE], gpu_unavailable=True),
            stop_after=2,
        )

        self.assertEqual(outcome.stdout.count("GPU 指标不可用"), 1)

    def test_same_failure_reason_is_throttled(self):
        failing = FakeCollector([OSError("网络不可达")] * 9)
        outcome = self.run_loop(
            resolve=[("192.168.1.10", "192.168.1.255")] * 10,
            collector=failing,
            sockets=[FakeSocket() for _ in range(10)],
            stop_after=9,
        )

        self.assertEqual(outcome.stderr.count("网络不可达"), 1)
        self.assertEqual(len(outcome.sleeps), 9)
        self.assertEqual(outcome.sleeps[:6], [1.0, 2.0, 5.0, 10.0, 20.0, 30.0])
        self.assertEqual(outcome.sleeps[-1], 30.0)

    def test_keyboard_interrupt_returns_zero(self):
        stdout = io.StringIO()
        with patch.object(host, "run", side_effect=KeyboardInterrupt), (
            contextlib.redirect_stdout(stdout)
        ):
            self.assertEqual(host.main(), 0)
        self.assertIn("指标广播已停止", stdout.getvalue())


class SocketSetupTests(unittest.TestCase):
    def test_binds_to_chosen_address_and_sets_broadcast_once(self):
        fake_socket = FakeSocket()
        with patch.object(host.socket, "socket", return_value=fake_socket) as factory:
            opened = host._open_socket("192.168.1.10")

        factory.assert_called_once_with(socket.AF_INET, socket.SOCK_DGRAM)
        self.assertIs(opened, fake_socket)
        self.assertEqual(
            fake_socket.options,
            [(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)],
        )
        self.assertEqual(fake_socket.bound, [("192.168.1.10", 0)])

    def test_closes_socket_when_bind_fails(self):
        fake_socket = FakeSocket(bind_error=OSError("无法绑定"))
        with patch.object(host.socket, "socket", return_value=fake_socket):
            with self.assertRaisesRegex(OSError, "无法绑定"):
                host._open_socket("192.168.1.10")

        self.assertTrue(fake_socket.closed)


class InterfaceCandidateTests(unittest.TestCase):
    def test_lists_every_interface_with_state_and_ipv4_presence(self):
        addresses = {
            "以太网": [
                entry("192.168.1.10"),
                entry("fe80::1", family=socket.AF_INET6),
            ],
            "WLAN": [],
        }
        stats = {
            "以太网": SimpleNamespace(isup=True),
            "WLAN": SimpleNamespace(isup=False),
        }

        text = host._interface_candidates(addresses, stats)

        self.assertIn("以太网 (Up): 192.168.1.10", text)
        self.assertIn("WLAN (Down): 无 IPv4 地址", text)
        self.assertNotIn("fe80", text)

    def test_empty_interface_set_is_explained(self):
        self.assertEqual(host._interface_candidates({}, {}), "  (没有发现任何网卡)")

    def test_missing_interface_queries_psutil_only_once(self):
        stats = {"其他网卡": SimpleNamespace(isup=True)}
        addresses = {"其他网卡": [entry("10.0.0.2")]}
        with patch.object(host.psutil, "net_if_stats", return_value=stats) as stats_mock, (
            patch.object(host.psutil, "net_if_addrs", return_value=addresses)
        ) as addrs_mock, patch.object(config, "ETHERNET_IPV4_ADDRESS", ""):
            with self.assertRaisesRegex(RuntimeError, "不存在或未连接") as raised:
                host._get_ethernet_details("以太网")

        self.assertEqual(stats_mock.call_count, 1)
        self.assertEqual(addrs_mock.call_count, 1)
        self.assertIn("其他网卡 (Up): 10.0.0.2", str(raised.exception))

    def test_psutil_failure_is_wrapped_without_a_second_read(self):
        with patch.object(
            host.psutil, "net_if_stats", side_effect=psutil.Error("拒绝访问")
        ) as stats_mock, patch.object(host.psutil, "net_if_addrs") as addrs_mock:
            with self.assertRaisesRegex(RuntimeError, "无法读取网卡信息：Error"):
                host._get_ethernet_details("以太网")

        self.assertEqual(stats_mock.call_count, 1)
        addrs_mock.assert_not_called()

    def test_missing_powershell_is_reported_as_runtime_error(self):
        with patch.object(
            host.subprocess, "run", side_effect=FileNotFoundError("powershell.exe")
        ), patch.object(config, "ETHERNET_IPV4_ADDRESS", ""):
            with self.assertRaisesRegex(RuntimeError, "无法读取有线网卡"):
                host._get_interface_default_gateway("以太网")


class InterfaceSelectionTests(unittest.TestCase):
    def test_gateway_mismatch_falls_back_to_unique_candidate(self):
        self.assertEqual(
            host._select_interface_ipv4(
                [entry("192.168.10.9")],
                default_gateway="10.20.30.1",
            ),
            ("192.168.10.9", "192.168.10.255"),
        )

    def test_same_subnet_addresses_are_not_ambiguous(self):
        candidates = [entry("192.168.1.10"), entry("192.168.1.11")]

        self.assertEqual(
            host._select_interface_ipv4(candidates, default_gateway="192.168.1.1"),
            ("192.168.1.10", "192.168.1.255"),
        )
        self.assertEqual(
            host._select_interface_ipv4(candidates, default_gateway=None),
            ("192.168.1.10", "192.168.1.255"),
        )

    def test_configured_address_not_found_lists_available_addresses(self):
        with self.assertRaisesRegex(RuntimeError, "可用地址：192\\.168\\.1\\.10") as raised:
            host._select_interface_ipv4(
                [entry("192.168.1.10")],
                default_gateway=None,
                configured_address="192.168.1.99",
            )

        self.assertIn("192.168.1.99", str(raised.exception))

    def test_no_candidate_reports_each_skipped_reason(self):
        with self.assertRaisesRegex(RuntimeError, "没有可用 IPv4 地址和子网掩码"):
            host._select_interface_ipv4(
                [entry("192.168.10.9", "255.255.255.255")],
                default_gateway=None,
            )

        with self.assertRaises(RuntimeError) as raised:
            host._select_interface_ipv4(
                [entry("192.168.10.9", "255.255.255.255")],
                default_gateway=None,
            )
        message = str(raised.exception)
        self.assertIn("192.168.10.9", message)
        self.assertIn("定向广播", message)

    def test_skipped_addresses_keep_their_specific_reasons(self):
        cases = (
            ("192.168.10.9", "255.255.255.254", "定向广播"),
            ("192.168.10.9", "255.255.255.255", "定向广播"),
            ("10.0.0.5", "255.0.255.0", "子网掩码"),
            ("169.254.10.5", "255.255.0.0", "单播"),
        )
        for address, netmask, expected in cases:
            with self.subTest(address=address, netmask=netmask):
                with self.assertRaises(RuntimeError) as raised:
                    host._select_interface_ipv4(
                        [entry(address, netmask)], default_gateway=None
                    )
                message = str(raised.exception)
                self.assertIn(address, message)
                self.assertIn(expected, message)

    def test_gateway_matching_multiple_subnets_requires_explicit_address(self):
        candidates = [entry("10.1.2.3", "255.255.0.0"), entry("10.2.3.4", "255.0.0.0")]

        with self.assertRaisesRegex(RuntimeError, "同时匹配多个") as raised:
            host._select_interface_ipv4(candidates, default_gateway="10.1.0.1")

        self.assertIn("ETHERNET_IPV4_ADDRESS", str(raised.exception))

    def test_invalid_gateway_is_reported(self):
        with self.assertRaisesRegex(RuntimeError, "默认网关不是有效 IPv4 地址"):
            host._select_interface_ipv4(
                [entry("192.168.1.10")], default_gateway="not-an-ip"
            )

    def test_gateway_miss_with_multiple_subnets_requires_explicit_address(self):
        candidates = [entry("192.168.1.10"), entry("10.0.0.5")]

        with self.assertRaisesRegex(RuntimeError, "不属于该网卡的任何 IPv4 子网"):
            host._select_interface_ipv4(candidates, default_gateway="172.16.0.1")

    def test_multiple_subnets_without_gateway_require_explicit_address(self):
        candidates = [entry("192.168.1.10"), entry("10.0.0.5")]

        with self.assertRaisesRegex(RuntimeError, "多个 IPv4 子网") as raised:
            host._select_interface_ipv4(candidates, default_gateway=None)

        self.assertIn("可用地址：192.168.1.10、10.0.0.5", str(raised.exception))

    def test_configured_address_selects_duplicate_subnet_address(self):
        candidates = [entry("192.168.1.10"), entry("192.168.1.11")]

        self.assertEqual(
            host._select_interface_ipv4(
                candidates,
                default_gateway=None,
                configured_address="192.168.1.11",
            ),
            ("192.168.1.11", "192.168.1.255"),
        )


class SchedulingTests(unittest.TestCase):
    def test_backoff_is_capped_at_thirty_seconds(self):
        delays = [host._backoff_delay(count) for count in range(1, 10)]
        self.assertEqual(delays, [1.0, 2.0, 5.0, 10.0, 20.0, 30.0, 30.0, 30.0, 30.0])

    def test_failure_reporter_throttles_repeated_reasons(self):
        reporter = host._FailureReporter(every=3)

        self.assertTrue(reporter.should_report("网卡未找到"))
        self.assertFalse(reporter.should_report("网卡未找到"))
        self.assertTrue(reporter.should_report("网卡未找到"))
        self.assertFalse(reporter.should_report("网卡未找到"))
        self.assertTrue(reporter.should_report("网络不可达"))
        reporter.reset()
        self.assertTrue(reporter.should_report("网络不可达"))

    def test_deadline_advances_one_interval(self):
        self.assertEqual(host._next_deadline(10.0, 1.0, 10.0), 11.0)
        self.assertEqual(host._next_deadline(10.0, 1.0, 10.5), 11.0)

    def test_deadline_skips_elapsed_periods(self):
        self.assertEqual(host._next_deadline(10.0, 1.0, 11.0), 12.0)
        self.assertEqual(host._next_deadline(10.0, 1.0, 100.0), 101.0)
        self.assertEqual(host._next_deadline(10.0, 0.5, 13.4), 13.5)

    def test_deadline_rejects_non_positive_or_non_finite_intervals(self):
        for interval in (0, -1, float("nan"), float("inf")):
            with self.subTest(interval=interval), self.assertRaisesRegex(
                ValueError, "正数"
            ):
                host._next_deadline(0.0, interval, 0.0)


if __name__ == "__main__":
    unittest.main()
