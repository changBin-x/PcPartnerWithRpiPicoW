"""Windows host metric collection for the Pico display broadcaster."""

from __future__ import annotations

import math
import subprocess
import time
from collections.abc import Callable
from typing import Any

import psutil


def bytes_delta_to_mb_s(delta_bytes: int, elapsed_seconds: float) -> float | None:
    """Convert an Ethernet byte-counter delta to decimal MB/s.

    ``None`` indicates a counter reset or invalid elapsed time, so callers can
    skip the sample instead of publishing a misleading speed.
    """
    if isinstance(delta_bytes, bool) or not isinstance(delta_bytes, int) or delta_bytes < 0:
        return None
    if isinstance(elapsed_seconds, bool) or not isinstance(elapsed_seconds, (int, float)):
        return None
    try:
        elapsed = float(elapsed_seconds)
    except (OverflowError, ValueError):
        return None
    if not math.isfinite(elapsed) or elapsed <= 0:
        return None
    try:
        rate = delta_bytes / elapsed / 1_000_000
    except OverflowError:
        return None
    if not math.isfinite(rate):
        return None
    return round(rate, 1)


def validate_percent(value: Any) -> float | None:
    """Return a finite 0–100 percentage rounded to one decimal place."""
    if isinstance(value, bool):
        return None
    try:
        number = float(value)
    except (TypeError, ValueError, OverflowError):
        return None
    if not math.isfinite(number) or not 0 <= number <= 100:
        return None
    return round(number, 1)


class MetricsCollector:
    """Collect one computer sample for a named Ethernet interface.

    The first call establishes a network-counter baseline and returns ``None``.
    Later calls return values directly compatible with ``encode_packet``;
    ``None`` is returned when a rate is not trustworthy or required host
    percentages are invalid. GPU utilization alone may be unavailable.
    """

    def __init__(
        self,
        ethernet_interface: str,
        *,
        monotonic: Callable[[], float] | None = None,
    ) -> None:
        if not isinstance(ethernet_interface, str) or not ethernet_interface.strip():
            raise ValueError("必须指定有线网卡名称")
        self._ethernet_interface = ethernet_interface
        self._monotonic = time.monotonic if monotonic is None else monotonic
        self._previous: tuple[float, int, int] | None = None

    def sample(self) -> dict[str, float | None] | None:
        """Collect CPU, GPU, RAM and Ethernet rates for the selected adapter."""
        cpu = validate_percent(psutil.cpu_percent(interval=None))
        ram = validate_percent(psutil.virtual_memory().percent)
        gpu = self._gpu_percent()

        counters = psutil.net_io_counters(pernic=True)
        interface_counters = counters.get(self._ethernet_interface)
        if interface_counters is None:
            raise ValueError(f"未找到有线网卡: {self._ethernet_interface}")

        now = self._monotonic()
        rx_total = interface_counters.bytes_recv
        tx_total = interface_counters.bytes_sent
        previous = self._previous
        self._previous = (now, rx_total, tx_total)
        if previous is None:
            return None

        previous_time, previous_rx, previous_tx = previous
        elapsed = now - previous_time
        rx = bytes_delta_to_mb_s(rx_total - previous_rx, elapsed)
        tx = bytes_delta_to_mb_s(tx_total - previous_tx, elapsed)
        if cpu is None or ram is None or rx is None or tx is None:
            return None
        return {"cpu": cpu, "gpu": gpu, "ram": ram, "rx": rx, "tx": tx}

    @staticmethod
    def _gpu_percent() -> float | None:
        """Query GPU 0 through the NVIDIA driver without delaying indefinitely."""
        try:
            result = subprocess.run(
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
        except (OSError, subprocess.SubprocessError):
            return None
        lines = result.stdout.splitlines()
        if not lines:
            return None
        return validate_percent(lines[0].strip())
