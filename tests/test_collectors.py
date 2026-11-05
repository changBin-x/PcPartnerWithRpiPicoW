import math
import subprocess
import unittest
from types import SimpleNamespace
from unittest.mock import patch

from pc_metrics.collectors import (
    DROP_REASON_BASELINE,
    DROP_REASON_COUNTER_RESET,
    DROP_REASON_INTERFACE_MISSING,
    DROP_REASON_OUT_OF_RANGE,
    InterfaceNotFoundError,
    MetricsCollector,
    bytes_delta_to_mb_s,
    validate_percent,
)


class FakeClock:
    def __init__(self, value=0.0):
        self.value = value

    def __call__(self):
        return self.value


class CollectorTests(unittest.TestCase):
    def test_network_rate_uses_decimal_megabytes(self):
        self.assertEqual(bytes_delta_to_mb_s(2_500_000, 1.0), 2.5)

    def test_network_rate_rejects_counter_reset_and_invalid_interval(self):
        self.assertIsNone(bytes_delta_to_mb_s(-1, 1.0))
        self.assertIsNone(bytes_delta_to_mb_s(1, 0))
        self.assertIsNone(bytes_delta_to_mb_s(1, math.inf))

    def test_percent_rejects_invalid_values(self):
        self.assertIsNone(validate_percent(float("nan")))
        self.assertIsNone(validate_percent(101))
        self.assertIsNone(validate_percent(None))
        self.assertIsNone(validate_percent(True))
        self.assertEqual(validate_percent("45.26"), 45.3)

    @patch("pc_metrics.collectors.subprocess.run")
    @patch("pc_metrics.collectors.psutil.net_io_counters")
    @patch("pc_metrics.collectors.psutil.virtual_memory")
    @patch("pc_metrics.collectors.psutil.cpu_percent")
    def test_first_sample_establishes_baseline_then_returns_metrics(
        self, cpu_percent, virtual_memory, net_io_counters, run
    ):
        clock = FakeClock(10.0)
        collector = MetricsCollector("Ethernet", monotonic=clock)
        cpu_percent.side_effect = [0.0, 43.26]
        virtual_memory.return_value = SimpleNamespace(percent=61.24)
        net_io_counters.side_effect = [
            {"Ethernet": SimpleNamespace(bytes_recv=2_000_000, bytes_sent=500_000)},
            {"Ethernet": SimpleNamespace(bytes_recv=3_500_000, bytes_sent=800_000)},
        ]
        run.return_value = SimpleNamespace(stdout="73\n")

        self.assertIsNone(collector.sample())
        clock.value = 11.5
        sample = collector.sample()

        self.assertEqual(
            sample,
            {"cpu": 43.3, "gpu": 73.0, "ram": 61.2, "rx": 1.0, "tx": 0.2},
        )
        run.assert_called_with(
            [
                "nvidia-smi",
                "-i",
                "0",
                "--query-gpu=utilization.gpu",
                "--format=csv,noheader,nounits",
            ],
            capture_output=True,
            encoding="utf-8",
            errors="replace",
            timeout=0.7,
            check=True,
        )
        net_io_counters.assert_called_with(pernic=True)

    @patch("pc_metrics.collectors.subprocess.run")
    @patch("pc_metrics.collectors.psutil.net_io_counters")
    @patch("pc_metrics.collectors.psutil.virtual_memory")
    @patch("pc_metrics.collectors.psutil.cpu_percent")
    def test_first_sample_is_cpu_baseline_even_when_percent_is_nonzero(
        self, cpu_percent, virtual_memory, net_io_counters, run
    ):
        clock = FakeClock(5.0)
        collector = MetricsCollector("Ethernet", monotonic=clock)
        cpu_percent.return_value = 43.0
        virtual_memory.return_value = SimpleNamespace(percent=20.0)
        net_io_counters.side_effect = [
            {"Ethernet": SimpleNamespace(bytes_recv=0, bytes_sent=0)},
            {"Ethernet": SimpleNamespace(bytes_recv=1_000_000, bytes_sent=0)},
        ]
        run.return_value = SimpleNamespace(stdout="50\n")

        self.assertIsNone(collector.sample())
        self.assertEqual(collector.last_drop_reason, DROP_REASON_BASELINE)

        clock.value = 6.0
        self.assertEqual(collector.sample()["cpu"], 43.0)
        self.assertIsNone(collector.last_drop_reason)

    @patch("pc_metrics.collectors.subprocess.run")
    @patch("pc_metrics.collectors.psutil.net_io_counters")
    @patch("pc_metrics.collectors.psutil.virtual_memory")
    @patch("pc_metrics.collectors.psutil.cpu_percent")
    def test_gpu_timeout_does_not_block_other_metrics(
        self, cpu_percent, virtual_memory, net_io_counters, run
    ):
        clock = FakeClock(1.0)
        collector = MetricsCollector("Ethernet", monotonic=clock)
        cpu_percent.return_value = 10
        virtual_memory.return_value = SimpleNamespace(percent=20)
        net_io_counters.side_effect = [
            {"Ethernet": SimpleNamespace(bytes_recv=100, bytes_sent=200)},
            {"Ethernet": SimpleNamespace(bytes_recv=1_000_100, bytes_sent=2_000_200)},
        ]
        run.side_effect = subprocess.TimeoutExpired("nvidia-smi", 0.7)

        self.assertIsNone(collector.sample())
        clock.value = 2.0
        sample = collector.sample()

        self.assertEqual(sample["gpu"], None)
        self.assertEqual(sample["rx"], 1.0)
        self.assertEqual(sample["tx"], 2.0)

    @patch("pc_metrics.collectors.subprocess.run")
    def test_gpu_value_error_is_treated_as_unavailable(self, run):
        run.side_effect = ValueError("embedded null byte")
        collector = MetricsCollector("Ethernet", monotonic=FakeClock(0.0))

        self.assertIsNone(collector._gpu_percent())
        self.assertFalse(collector.gpu_unavailable)

    @patch("pc_metrics.collectors.subprocess.run")
    def test_gpu_queries_are_throttled_after_three_failures(self, run):
        clock = FakeClock(0.0)
        collector = MetricsCollector("Ethernet", monotonic=clock)
        run.side_effect = OSError("nvidia-smi missing")

        for _ in range(3):
            self.assertIsNone(collector._gpu_percent())
            clock.value += 1.0

        self.assertTrue(collector.gpu_unavailable)
        self.assertEqual(run.call_count, 3)

        clock.value = 5.0
        self.assertIsNone(collector._gpu_percent())
        self.assertEqual(run.call_count, 3)

        clock.value = 12.0
        self.assertIsNone(collector._gpu_percent())
        self.assertEqual(run.call_count, 4)

    @patch("pc_metrics.collectors.subprocess.run")
    def test_gpu_failure_right_after_cooldown_reenters_cooldown(self, run):
        clock = FakeClock(0.0)
        collector = MetricsCollector("Ethernet", monotonic=clock)
        run.side_effect = OSError("nvidia-smi missing")

        for _ in range(3):
            self.assertIsNone(collector._gpu_percent())
            clock.value += 1.0
        self.assertTrue(collector.gpu_unavailable)
        self.assertEqual(run.call_count, 3)

        # Cooldown elapsed at 12 s: one probe is allowed, and failing it puts
        # the collector straight back into the next cooldown window.
        clock.value = 12.0
        self.assertIsNone(collector._gpu_percent())
        self.assertEqual(run.call_count, 4)

        clock.value = 15.0
        self.assertIsNone(collector._gpu_percent())
        self.assertEqual(run.call_count, 4)

        clock.value = 22.0
        self.assertIsNone(collector._gpu_percent())
        self.assertEqual(run.call_count, 5)

    @patch("pc_metrics.collectors.subprocess.run")
    def test_gpu_success_resets_unavailable_state(self, run):
        clock = FakeClock(0.0)
        collector = MetricsCollector("Ethernet", monotonic=clock)
        run.side_effect = [
            OSError("failure 1"),
            OSError("failure 2"),
            OSError("failure 3"),
            SimpleNamespace(stdout="42\n"),
        ]

        for _ in range(3):
            collector._gpu_percent()
            clock.value += 1.0
        self.assertTrue(collector.gpu_unavailable)

        clock.value = 100.0
        self.assertEqual(collector._gpu_percent(), 42.0)
        self.assertFalse(collector.gpu_unavailable)

    @patch("pc_metrics.collectors.subprocess.run")
    @patch("pc_metrics.collectors.psutil.net_io_counters")
    @patch("pc_metrics.collectors.psutil.virtual_memory")
    @patch("pc_metrics.collectors.psutil.cpu_percent")
    def test_counter_reset_discards_sample_and_refreshes_baseline(
        self, cpu_percent, virtual_memory, net_io_counters, run
    ):
        clock = FakeClock(1.0)
        collector = MetricsCollector("Ethernet", monotonic=clock)
        cpu_percent.return_value = 10
        virtual_memory.return_value = SimpleNamespace(percent=20)
        net_io_counters.side_effect = [
            {"Ethernet": SimpleNamespace(bytes_recv=1_000, bytes_sent=2_000)},
            {"Ethernet": SimpleNamespace(bytes_recv=10, bytes_sent=1_000)},
            {"Ethernet": SimpleNamespace(bytes_recv=1_000_010, bytes_sent=2_001_000)},
        ]
        run.return_value = SimpleNamespace(stdout="N/A\n")

        self.assertIsNone(collector.sample())
        clock.value = 2.0
        self.assertIsNone(collector.sample())
        self.assertEqual(collector.last_drop_reason, DROP_REASON_COUNTER_RESET)
        clock.value = 3.0
        sample = collector.sample()

        self.assertEqual(sample["rx"], 1.0)
        self.assertEqual(sample["tx"], 2.0)
        self.assertIsNone(sample["gpu"])
        self.assertIsNone(collector.last_drop_reason)

    @patch("pc_metrics.collectors.subprocess.run")
    @patch("pc_metrics.collectors.psutil.net_io_counters")
    @patch("pc_metrics.collectors.psutil.virtual_memory")
    @patch("pc_metrics.collectors.psutil.cpu_percent")
    def test_out_of_range_percent_records_drop_reason(
        self, cpu_percent, virtual_memory, net_io_counters, run
    ):
        clock = FakeClock(1.0)
        collector = MetricsCollector("Ethernet", monotonic=clock)
        cpu_percent.return_value = 150
        virtual_memory.return_value = SimpleNamespace(percent=20)
        net_io_counters.side_effect = [
            {"Ethernet": SimpleNamespace(bytes_recv=1_000, bytes_sent=2_000)},
            {"Ethernet": SimpleNamespace(bytes_recv=2_000_000, bytes_sent=2_000)},
        ]
        run.return_value = SimpleNamespace(stdout="10\n")

        self.assertIsNone(collector.sample())
        clock.value = 2.0
        self.assertIsNone(collector.sample())
        self.assertEqual(collector.last_drop_reason, DROP_REASON_OUT_OF_RANGE)

    @patch("pc_metrics.collectors.subprocess.run")
    @patch("pc_metrics.collectors.psutil.net_io_counters")
    @patch("pc_metrics.collectors.psutil.virtual_memory")
    @patch("pc_metrics.collectors.psutil.cpu_percent")
    def test_missing_interface_raises_and_records_reason(
        self, cpu_percent, virtual_memory, net_io_counters, run
    ):
        collector = MetricsCollector("Ethernet", monotonic=FakeClock(1.0))
        cpu_percent.return_value = 10
        virtual_memory.return_value = SimpleNamespace(percent=20)
        net_io_counters.return_value = {}
        run.return_value = SimpleNamespace(stdout="10\n")

        with self.assertRaisesRegex(InterfaceNotFoundError, "未找到有线网卡"):
            collector.sample()
        self.assertEqual(collector.last_drop_reason, DROP_REASON_INTERFACE_MISSING)


if __name__ == "__main__":
    unittest.main()
