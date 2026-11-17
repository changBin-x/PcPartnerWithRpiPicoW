"""Host identity labels for UDP v2 dashboard text.

Detect short ASCII labels for CPU / GPU / RAM from the running machine,
with optional environment overrides. Detection failures never raise; they
fall back to the neutral placeholders ``CPU`` / ``GPU`` / ``RAM``.
"""

from __future__ import annotations

import os
import platform
import re
import subprocess
import sys
from collections.abc import Mapping
from typing import Callable

import psutil

from pc_metrics.protocol import LABEL_MAX_BYTES, sanitize_label

DEFAULT_CPU_LABEL = "CPU"
DEFAULT_GPU_LABEL = "GPU"
DEFAULT_RAM_LABEL = "RAM"

ENV_CPU_LABEL = "PICO_CPU_LABEL"
ENV_GPU_LABEL = "PICO_GPU_LABEL"
ENV_RAM_LABEL = "PICO_RAM_LABEL"

# Physical RAM is reported as the smallest common tier that can hold it.
_RAM_TIERS_GIB = (8, 16, 32, 64, 128, 256, 512)


def format_ram_label(total_bytes: int) -> str:
    """Map physical RAM size to an upward-rounded common tier label.

    Args:
        total_bytes: Total physical memory in bytes (must be positive).

    Returns:
        A label such as ``16G``. Values above the last tier become
        ``{tier}G+``. Invalid sizes fall back to ``RAM``.
    """
    if isinstance(total_bytes, bool) or not isinstance(total_bytes, int):
        return DEFAULT_RAM_LABEL
    if total_bytes <= 0:
        return DEFAULT_RAM_LABEL
    gib = total_bytes / (1024**3)
    for tier in _RAM_TIERS_GIB:
        if gib <= tier:
            return f"{tier}G"
    return f"{_RAM_TIERS_GIB[-1]}G+"


def shorten_cpu_name(raw: str) -> str:
    """Compress a verbose CPU model string into a short ASCII label.

    Args:
        raw: Vendor model string from device-tree, cpuinfo, or WMI.

    Returns:
        A sanitized label of at most ``LABEL_MAX_BYTES`` characters.
    """
    text = " ".join(str(raw).split())
    if not text:
        return DEFAULT_CPU_LABEL
    lower = text.lower()
    if "orin" in lower:
        if "nx" in lower:
            return sanitize_label("Orin NX") or DEFAULT_CPU_LABEL
        if "agx" in lower:
            return sanitize_label("Orin AGX") or DEFAULT_CPU_LABEL
        if "nano" in lower:
            return sanitize_label("Orin Nano") or DEFAULT_CPU_LABEL
        return sanitize_label("Orin") or DEFAULT_CPU_LABEL

    ryzen = re.search(
        r"ryzen\s*([3579])\s*([0-9]{3,5}\w*)", text, flags=re.IGNORECASE
    )
    if ryzen:
        return (
            sanitize_label(f"R{ryzen.group(1)} {ryzen.group(2)}")
            or DEFAULT_CPU_LABEL
        )

    core = re.search(
        r"core\(tm\)\s*(i[3579]-?[0-9]{3,5}\w*)", text, flags=re.IGNORECASE
    )
    if core:
        return sanitize_label(core.group(1)) or DEFAULT_CPU_LABEL

    intel = re.search(r"\b(i[3579]-?[0-9]{3,5}\w*)\b", text, flags=re.IGNORECASE)
    if intel:
        return sanitize_label(intel.group(1)) or DEFAULT_CPU_LABEL

    return sanitize_label(text) or DEFAULT_CPU_LABEL


def shorten_gpu_name(raw: str) -> str:
    """Compress a GPU marketing name into a short ASCII label.

    Args:
        raw: Name from ``nvidia-smi`` or similar.

    Returns:
        A sanitized label of at most ``LABEL_MAX_BYTES`` characters.
    """
    text = " ".join(str(raw).split())
    if not text:
        return DEFAULT_GPU_LABEL
    lower = text.lower()
    if "orin" in lower:
        return sanitize_label("Orin nvgpu") or DEFAULT_GPU_LABEL
    text = re.sub(
        r"^(nvidia\s+)?(geforce\s+)?", "", text, flags=re.IGNORECASE
    ).strip()
    return sanitize_label(text) or DEFAULT_GPU_LABEL


def _read_text_file(path: str) -> str:
    """Read a small text file and strip NULs / surrounding whitespace."""
    with open(path, "rb") as handle:
        data = handle.read(512)
    return data.replace(b"\x00", b"").decode("utf-8", errors="replace").strip()


def _linux_cpu_raw() -> str:
    """Best-effort Linux CPU model from device-tree or /proc/cpuinfo."""
    try:
        model = _read_text_file("/proc/device-tree/model")
        if model:
            return model
    except OSError:
        pass
    try:
        with open("/proc/cpuinfo", encoding="utf-8", errors="replace") as handle:
            for line in handle:
                if line.startswith("model name") or line.startswith("Hardware"):
                    _, _, value = line.partition(":")
                    value = value.strip()
                    if value:
                        return value
    except OSError:
        pass
    return ""


def _windows_cpu_raw() -> str:
    """Best-effort Windows CPU name via CIM; fall back to platform APIs."""
    command = [
        "powershell.exe",
        "-NoProfile",
        "-NonInteractive",
        "-Command",
        "(Get-CimInstance Win32_Processor | Select-Object -First 1 -ExpandProperty Name)",
    ]
    try:
        result = subprocess.run(
            command,
            capture_output=True,
            encoding="utf-8",
            errors="replace",
            timeout=2.0,
            check=True,
        )
        name = result.stdout.strip()
        if name:
            return name
    except (OSError, ValueError, subprocess.SubprocessError):
        pass
    processor = platform.processor() or ""
    if processor and "Family" not in processor:
        return processor
    return platform.machine() or ""


def detect_cpu_label(
    *,
    reader: Callable[[], str] | None = None,
) -> str:
    """Detect a short CPU label for the current host.

    Args:
        reader: Optional injectable raw-name source for tests.

    Returns:
        Sanitized label, or ``CPU`` when detection fails.
    """
    if reader is None:
        if sys.platform.startswith("linux"):
            reader = _linux_cpu_raw
        elif sys.platform.startswith("win"):
            reader = _windows_cpu_raw
        else:
            reader = lambda: platform.processor() or platform.machine() or ""
    try:
        raw = reader()
    except (OSError, ValueError, RuntimeError):
        return DEFAULT_CPU_LABEL
    return shorten_cpu_name(raw)


def _gpu_label_from_board(board: str) -> str | None:
    """Derive a GPU short name from board/CPU clues when smi is unavailable."""
    text = " ".join(str(board).split())
    if not text:
        return None
    lower = text.lower()
    if "orin" in lower or "jetson" in lower:
        return sanitize_label("Orin nvgpu")
    return None


def detect_gpu_label(
    *,
    query: Callable[[], str] | None = None,
    board_reader: Callable[[], str] | None = None,
) -> str:
    """Detect a short GPU label via ``nvidia-smi``, then board clues.

    Args:
        query: Optional injectable name source for tests.
        board_reader: Optional board/CPU raw-name source used when smi fails
            (Jetson/Orin → ``Orin nvgpu``).

    Returns:
        Sanitized label, or ``GPU`` when every source fails.
    """
    if query is None:

        def query() -> str:
            result = subprocess.run(
                [
                    "nvidia-smi",
                    "-i",
                    "0",
                    "--query-gpu=name",
                    "--format=csv,noheader",
                ],
                capture_output=True,
                encoding="utf-8",
                errors="replace",
                timeout=0.7,
                check=True,
            )
            lines = result.stdout.splitlines()
            return lines[0].strip() if lines else ""

    raw = ""
    try:
        raw = query() or ""
    except (OSError, ValueError, subprocess.SubprocessError):
        raw = ""

    if raw.strip():
        lower = raw.lower()
        if "couldn't communicate" not in lower and "has failed" not in lower:
            return shorten_gpu_name(raw)

    if board_reader is None:
        if sys.platform.startswith("linux"):
            board_reader = _linux_cpu_raw
        else:
            board_reader = lambda: ""
    try:
        board = board_reader() or ""
    except (OSError, ValueError, RuntimeError):
        board = ""
    derived = _gpu_label_from_board(board)
    if derived:
        return derived
    return DEFAULT_GPU_LABEL


def detect_ram_label(
    *,
    total_bytes: int | None = None,
) -> str:
    """Detect a tiered RAM capacity label.

    Args:
        total_bytes: Optional injectable total for tests; defaults to
            ``psutil.virtual_memory().total``.

    Returns:
        Tier label such as ``32G``, or ``RAM`` on failure.
    """
    if total_bytes is None:
        try:
            total_bytes = int(psutil.virtual_memory().total)
        except (OSError, ValueError, AttributeError, TypeError):
            return DEFAULT_RAM_LABEL
    return format_ram_label(total_bytes)


def resolve_identity_labels(
    environ: Mapping[str, str] | None = None,
    *,
    cpu_detector: Callable[[], str] | None = None,
    gpu_detector: Callable[[], str] | None = None,
    ram_detector: Callable[[], str] | None = None,
) -> dict[str, str]:
    """Resolve the three identity labels with env overrides first.

    Args:
        environ: Environment mapping; defaults to ``os.environ``.
        cpu_detector: Optional CPU detector override for tests.
        gpu_detector: Optional GPU detector override for tests.
        ram_detector: Optional RAM detector override for tests.

    Returns:
        Mapping with keys ``cpu_label``, ``gpu_label``, and ``ram_label``.
    """
    env = os.environ if environ is None else environ

    def _from_env(key: str, fallback: Callable[[], str], default: str) -> str:
        raw = env.get(key, "")
        if isinstance(raw, str) and raw.strip():
            cleaned = sanitize_label(raw)
            if cleaned:
                return cleaned
        try:
            detected = fallback()
        except (OSError, ValueError, RuntimeError):
            return default
        return detected if detected else default

    return {
        "cpu_label": _from_env(
            ENV_CPU_LABEL,
            cpu_detector or detect_cpu_label,
            DEFAULT_CPU_LABEL,
        ),
        "gpu_label": _from_env(
            ENV_GPU_LABEL,
            gpu_detector or detect_gpu_label,
            DEFAULT_GPU_LABEL,
        ),
        "ram_label": _from_env(
            ENV_RAM_LABEL,
            ram_detector or detect_ram_label,
            DEFAULT_RAM_LABEL,
        ),
    }
