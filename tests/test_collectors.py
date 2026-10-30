import math
import subprocess
import unittest
from types import SimpleNamespace
from unittest.mock import patch

from pc_metrics.collectors import (
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
                "--query-gpu=utilization.gpu",
                "--format=csv,noheader,nounits",
            ],
            capture_output=True,
            text=True,
            timeout=0.7,
            check=True,
        )
        net_io_counters.assert_called_with(pernic=True)

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
        clock.value = 3.0
        sample = collector.sample()

        self.assertEqual(sample["rx"], 1.0)
        self.assertEqual(sample["tx"], 2.0)
        self.assertIsNone(sample["gpu"])


if __name__ == "__main__":
    unittest.main()
