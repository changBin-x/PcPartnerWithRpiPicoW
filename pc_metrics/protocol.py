"""Version 1 UDP packet codec for PC metrics."""

import json
import math
from typing import Any, NoReturn

MAX_PACKET_BYTES = 512
PACKET_VERSION = 1
_FIELDS = frozenset(("v", "seq", "ts", "cpu", "gpu", "ram", "rx", "tx"))


def _integer(value: Any, *, minimum: int, field: str) -> int:
    if isinstance(value, bool) or not isinstance(value, int) or value < minimum:
        raise ValueError(f"{field} 必须是大于等于 {minimum} 的整数")
    return value


def _number(
    value: Any,
    *,
    minimum: float,
    maximum: float | None = None,
    allow_none: bool = False,
    field: str,
) -> float | None:
    if value is None and allow_none:
        return None
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        raise ValueError(f"{field} 必须是数字")
    try:
        number = float(value)
    except (OverflowError, ValueError) as exc:
        raise ValueError(f"{field} 超出有效范围") from exc
    if not math.isfinite(number) or number < minimum or (maximum is not None and number > maximum):
        raise ValueError(f"{field} 超出有效范围")
    return number


def encode_packet(
    seq: int,
    sampled_at: int,
    cpu: int | float,
    gpu: int | float | None,
    ram: int | float,
    rx: int | float,
    tx: int | float,
) -> bytes:
    """Validate and encode one v1 metrics packet.

    Args:
        seq: Non-negative packet sequence number.
        sampled_at: Positive Unix timestamp in seconds.
        cpu: CPU utilization percentage from 0 through 100.
        gpu: GPU utilization percentage from 0 through 100, or None if unavailable.
        ram: RAM utilization percentage from 0 through 100.
        rx: Receive throughput in decimal MB/s.
        tx: Transmit throughput in decimal MB/s.

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
    }
    try:
        data = json.dumps(payload, separators=(",", ":"), allow_nan=False).encode("utf-8")
    except (OverflowError, TypeError, ValueError) as exc:
        raise ValueError("无法编码协议数据") from exc
    if len(data) > MAX_PACKET_BYTES:
        raise ValueError("数据包超过 512 字节")
    return data


def _unique_object(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
    result: dict[str, Any] = {}
    for key, value in pairs:
        if key in result:
            raise ValueError("JSON 包含重复字段")
        result[key] = value
    return result


def _reject_constant(value: str) -> NoReturn:
    """Reject non-standard JSON constants such as NaN and Infinity."""
    raise ValueError(f"不允许 JSON 常量: {value}")


def decode_packet(data: bytes) -> dict[str, int | float | None]:
    """Decode and validate a v1 UTF-8 JSON packet.

    Args:
        data: Non-empty bytes containing a packet no larger than 512 bytes.

    Returns:
        A mapping with exactly ``v``, ``seq``, ``ts``, ``cpu``, ``gpu``,
        ``ram``, ``rx``, and ``tx``. ``ts`` is a Unix timestamp in seconds;
        ``rx`` and ``tx`` are throughput values in decimal MB/s. GPU may be None.

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
    except (UnicodeDecodeError, json.JSONDecodeError, ValueError, TypeError) as exc:
        raise ValueError("数据包不是有效 JSON") from exc
    if not isinstance(payload, dict) or frozenset(payload) != _FIELDS:
        raise ValueError("协议字段不匹配")
    if isinstance(payload["v"], bool) or not isinstance(payload["v"], int) or payload["v"] != PACKET_VERSION:
        raise ValueError("不支持的协议版本")
    result = {
        "v": PACKET_VERSION,
        "seq": _integer(payload["seq"], minimum=0, field="seq"),
        "ts": _integer(payload["ts"], minimum=1, field="ts"),
        "cpu": _number(payload["cpu"], minimum=0, maximum=100, field="cpu"),
        "gpu": _number(payload["gpu"], minimum=0, maximum=100, allow_none=True, field="gpu"),
        "ram": _number(payload["ram"], minimum=0, maximum=100, field="ram"),
        "rx": _number(payload["rx"], minimum=0, field="rx"),
        "tx": _number(payload["tx"], minimum=0, field="tx"),
    }
    return result
