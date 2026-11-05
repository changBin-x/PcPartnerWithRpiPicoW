"""Windows host metric collection for the Pico display broadcaster."""

from __future__ import annotations

import math
import subprocess
import time
from collections.abc import Callable
from typing import Any

import psutil

GPU_FAILURE_THRESHOLD = 3
GPU_RETRY_COOLDOWN_SECONDS = 10.0

DROP_REASON_BASELINE = "首次采样基线"
DROP_REASON_COUNTER_RESET = "网络计数器重置或采样间隔无效"
DROP_REASON_OUT_OF_RANGE = "指标超出有效范围"
DROP_REASON_INTERFACE_MISSING = "网卡未找到"


class InterfaceNotFoundError(ValueError):
    """The configured Ethernet interface is absent from psutil counters."""


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

    The first call establishes CPU and network-counter baselines and returns
    ``None``. Later calls return values directly compatible with
    ``encode_packet``; ``None`` is returned when a rate is not trustworthy or
    required host percentages are invalid. GPU utilization alone may be
    unavailable, and repeated GPU failures are throttled.
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
        self._cpu_baseline_ready = False
        self._last_drop_reason: str | None = None
        self._gpu_failures = 0
        self._gpu_retry_after = 0.0
        self._gpu_unavailable = False

    @property
    def last_drop_reason(self) -> str | None:
        """Return why the most recent sample was discarded, if it was."""
        return self._last_drop_reason

    @property
    def gpu_unavailable(self) -> bool:
        """Return whether GPU queries are throttled after repeated failures."""
        return self._gpu_unavailable

    def sample(self) -> dict[str, float | None] | None:
        """Collect CPU, GPU, RAM and Ethernet rates for the selected adapter."""
        cpu_reading = psutil.cpu_percent(interval=None)
        ram = validate_percent(psutil.virtual_memory().percent)
        gpu = self._gpu_percent()

        counters = psutil.net_io_counters(pernic=True)
        interface_counters = counters.get(self._ethernet_interface)
        if interface_counters is None:
            self._last_drop_reason = DROP_REASON_INTERFACE_MISSING
            raise InterfaceNotFoundError(f"未找到有线网卡: {self._ethernet_interface}")

        now = self._monotonic()
        previous = self._previous
        self._previous = (
            now,
            interface_counters.bytes_recv,
            interface_counters.bytes_sent,
        )
        if not self._cpu_baseline_ready or previous is None:
            self._cpu_baseline_ready = True
            self._last_drop_reason = DROP_REASON_BASELINE
            return None

        cpu = validate_percent(cpu_reading)
        previous_time, previous_rx, previous_tx = previous
        elapsed = now - previous_time
        rx = bytes_delta_to_mb_s(interface_counters.bytes_recv - previous_rx, elapsed)
        tx = bytes_delta_to_mb_s(interface_counters.bytes_sent - previous_tx, elapsed)
        if rx is None or tx is None:
            self._last_drop_reason = DROP_REASON_COUNTER_RESET
            return None
        if cpu is None or ram is None:
            self._last_drop_reason = DROP_REASON_OUT_OF_RANGE
            return None
        self._last_drop_reason = None
        return {"cpu": cpu, "gpu": gpu, "ram": ram, "rx": rx, "tx": tx}

    def _gpu_percent(self) -> float | None:
        """Query only GPU 0 through nvidia-smi, throttling after repeated failures."""
        now = self._monotonic()
        if now < self._gpu_retry_after:
            return None
        if self._gpu_failures >= GPU_FAILURE_THRESHOLD:
            # Cooldown just elapsed: sit one failure below the threshold so a
            # single new failure re-enters cooldown instead of accumulating.
            self._gpu_failures = GPU_FAILURE_THRESHOLD - 1
        try:
            result = subprocess.run(
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
        except (OSError, ValueError, subprocess.SubprocessError):
            return self._record_gpu_failure(now)
        lines = result.stdout.splitlines()
        value = validate_percent(lines[0].strip()) if lines else None
        if value is None:
            return self._record_gpu_failure(now)
        self._gpu_failures = 0
        self._gpu_unavailable = False
        return value

    def _record_gpu_failure(self, now: float) -> None:
        """Count a GPU query failure and start a cooldown after too many."""
        self._gpu_failures += 1
        if self._gpu_failures >= GPU_FAILURE_THRESHOLD:
            self._gpu_retry_after = now + GPU_RETRY_COOLDOWN_SECONDS
            self._gpu_unavailable = True
