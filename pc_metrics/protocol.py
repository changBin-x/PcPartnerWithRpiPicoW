"""Version 2 UDP packet codec for PC metrics."""

import json
import math
from typing import Any, NoReturn

MAX_PACKET_BYTES = 512
PACKET_VERSION = 2
LABEL_MAX_BYTES = 16
_FIELDS = frozenset(
    (
        "v",
        "seq",
        "ts",
        "cpu",
        "gpu",
        "ram",
        "rx",
        "tx",
        "cpu_label",
        "gpu_label",
        "ram_label",
    )
)


def sanitize_label(value: Any) -> str | None:
    """Return a printable ASCII label of at most ``LABEL_MAX_BYTES``.

    Args:
        value: Candidate label. Non-strings are rejected.

    Returns:
        The cleaned label, or ``None`` when empty/invalid after cleaning.
        Non-ASCII and control bytes are dropped; length is truncated to
        ``LABEL_MAX_BYTES``.
    """
    if not isinstance(value, str):
        return None
    cleaned_chars: list[str] = []
    for char in value.strip():
        code = ord(char)
        if 0x20 <= code <= 0x7E:
            cleaned_chars.append(char)
    cleaned = "".join(cleaned_chars)
    if not cleaned:
        return None
    return cleaned[:LABEL_MAX_BYTES]


def _integer(value: Any, *, minimum: int, field: str) -> int:
    if isinstance(value, bool) or not isinstance(value, int) or value < minimum:
        raise ValueError(f"{field} 必须是大于等于 {minimum} 的整数")
    return value


def _number(
    value: Any,
    *,
    minimum: float,
    maximum: int | float | None = None,
    allow_none: bool = False,
    field: str,
) -> int | float | None:
    if value is None and allow_none:
        return None
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        raise ValueError(f"{field} 必须是数字")
    if isinstance(value, int):
        number = value
    else:
        if not math.isfinite(value):
            raise ValueError(f"{field} 超出有效范围")
        number = value
    if number < minimum or (maximum is not None and number > maximum):
        raise ValueError(f"{field} 超出有效范围")
    return number


def _label(value: Any, *, field: str) -> str:
    """Require a non-empty printable ASCII string within the length cap."""
    if not isinstance(value, str) or not value:
        raise ValueError(f"{field} 必须是可打印 ASCII 字符串")
    if len(value) > LABEL_MAX_BYTES:
        raise ValueError(f"{field} 超过 {LABEL_MAX_BYTES} 字节")
    if any(ord(char) < 0x20 or ord(char) > 0x7E for char in value):
        raise ValueError(f"{field} 必须是可打印 ASCII 字符串")
    return value


def encode_packet(
    seq: int,
    sampled_at: int,
    cpu: int | float,
    gpu: int | float | None,
    ram: int | float,
    rx: int | float,
    tx: int | float,
    *,
    cpu_label: str,
    gpu_label: str,
    ram_label: str,
) -> bytes:
    """Validate and encode one v2 metrics packet.

    Args:
        seq: Non-negative packet sequence number.
        sampled_at: Positive Unix timestamp in seconds.
        cpu: CPU utilization percentage from 0 through 100.
        gpu: GPU utilization percentage from 0 through 100, or None if unavailable.
        ram: RAM utilization percentage from 0 through 100.
        rx: Receive throughput in decimal MB/s.
        tx: Transmit throughput in decimal MB/s.
        cpu_label: Host CPU identity, printable ASCII, 1–16 bytes.
        gpu_label: Host GPU identity, printable ASCII, 1–16 bytes.
        ram_label: Host RAM tier identity, printable ASCII, 1–16 bytes.

    Returns:
        The compact UTF-8 JSON packet as bytes.

    Raises:
        ValueError: If a field is invalid or the encoded packet exceeds 512 bytes.
    """
    payload = {
        "v": PACKET_VERSION,
        "seq": _integer(seq, minimum=0, field="seq"),
        "ts": _integer(sampled_at, minimum=1, field="ts"),
        "cpu": _number(cpu, minimum=0, maximum=100, field="cpu"),
        "gpu": _number(gpu, minimum=0, maximum=100, allow_none=True, field="gpu"),
        "ram": _number(ram, minimum=0, maximum=100, field="ram"),
        "rx": _number(rx, minimum=0, field="rx"),
        "tx": _number(tx, minimum=0, field="tx"),
        "cpu_label": _label(cpu_label, field="cpu_label"),
        "gpu_label": _label(gpu_label, field="gpu_label"),
        "ram_label": _label(ram_label, field="ram_label"),
    }
    try:
        data = json.dumps(payload, separators=(",", ":"), allow_nan=False).encode("utf-8")
    except (OverflowError, TypeError, ValueError) as exc:
        raise ValueError("无法编码协议数据") from exc
    if len(data) > MAX_PACKET_BYTES:
        raise ValueError("数据包超过 512 字节")
    return data


class _DuplicateFieldError(ValueError):
    """A JSON object repeated one of its field names."""

    def __init__(self, field: str) -> None:
        super().__init__(field)
        self.field = field


def _unique_object(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
    result: dict[str, Any] = {}
    for key, value in pairs:
        if key in result:
            raise _DuplicateFieldError(key)
        result[key] = value
    return result


def _reject_constant(value: str) -> NoReturn:
    """Reject non-standard JSON constants such as NaN and Infinity."""
    raise ValueError(f"不允许 JSON 常量: {value}")


def decode_packet(data: bytes) -> dict[str, int | float | str | None]:
    """Decode and validate a v2 UTF-8 JSON packet.

    Args:
        data: Non-empty bytes containing a packet no larger than 512 bytes.

    Returns:
        A mapping with exactly the eleven v2 fields. ``ts`` is a Unix
        timestamp in seconds; ``rx`` and ``tx`` are throughput values in
        decimal MB/s. GPU may be None. Labels are printable ASCII strings
        of length 1–16.

        Only lower bounds are checked for ``seq``, ``ts``, ``rx``, and ``tx``;
        no upper bound is imposed on them. The percentage fields ``cpu``,
        ``gpu``, and ``ram`` additionally have a 0–100 upper bound.

    Raises:
        ValueError: If the bytes, JSON, protocol fields, version, or values are invalid.
    """
    if not isinstance(data, bytes) or not data:
        raise ValueError("数据包必须是非空 bytes")
    if len(data) > MAX_PACKET_BYTES:
        raise ValueError("数据包超过 512 字节")
    try:
        payload = json.loads(
            data.decode("utf-8"),
            object_pairs_hook=_unique_object,
            parse_constant=_reject_constant,
        )
    except _DuplicateFieldError as exc:
        raise ValueError(f"数据包包含重复字段：{exc.field}") from exc
    except (UnicodeDecodeError, json.JSONDecodeError, ValueError, TypeError) as exc:
        raise ValueError("数据包不是有效 JSON") from exc
    if not isinstance(payload, dict) or frozenset(payload) != _FIELDS:
        raise ValueError("协议字段不匹配")
    if (
        isinstance(payload["v"], bool)
        or not isinstance(payload["v"], int)
        or payload["v"] != PACKET_VERSION
    ):
        raise ValueError("不支持的协议版本")
    result: dict[str, int | float | str | None] = {
        "v": PACKET_VERSION,
        "seq": _integer(payload["seq"], minimum=0, field="seq"),
        "ts": _integer(payload["ts"], minimum=1, field="ts"),
        "cpu": _number(payload["cpu"], minimum=0, maximum=100, field="cpu"),
        "gpu": _number(
            payload["gpu"], minimum=0, maximum=100, allow_none=True, field="gpu"
        ),
        "ram": _number(payload["ram"], minimum=0, maximum=100, field="ram"),
        "rx": _number(payload["rx"], minimum=0, field="rx"),
        "tx": _number(payload["tx"], minimum=0, field="tx"),
        "cpu_label": _label(payload["cpu_label"], field="cpu_label"),
        "gpu_label": _label(payload["gpu_label"], field="gpu_label"),
        "ram_label": _label(payload["ram_label"], field="ram_label"),
    }
    return result
