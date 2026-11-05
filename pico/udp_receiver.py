"""Non-blocking MicroPython receiver and validator for UDP metrics v1."""

import math

try:
    import ujson as json
except ImportError:
    import json


MAX_PACKET_BYTES = 512
UDP_PORT = 8765
_FIELDS = ("v", "seq", "ts", "cpu", "gpu", "ram", "rx", "tx")


def _integer(value, minimum):
    return (
        isinstance(value, int)
        and not isinstance(value, bool)
        and value >= minimum
    )


def _number(value, minimum, maximum=None, allow_none=False):
    if value is None and allow_none:
        return True
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        return False
    if isinstance(value, float) and not math.isfinite(value):
        return False
    if value < minimum:
        return False
    if maximum is not None and value > maximum:
        return False
    return True


def validate_metrics_payload(payload):
    """Validate and return a decoded protocol-v1 object or raise ValueError."""
    if not isinstance(payload, dict) or len(payload) != len(_FIELDS):
        raise ValueError("UDP 字段不匹配")
    for key in _FIELDS:
        if key not in payload:
            raise ValueError("UDP 字段缺失")
    if not _integer(payload["v"], 1) or payload["v"] != 1:
        raise ValueError("UDP 协议版本无效")
    if not _integer(payload["seq"], 0):
        raise ValueError("UDP 序号无效")
    if not _integer(payload["ts"], 1):
        raise ValueError("UDP 时间戳无效")
    if not _number(payload["cpu"], 0, 100):
        raise ValueError("CPU 百分比无效")
    if not _number(payload["gpu"], 0, 100, allow_none=True):
        raise ValueError("GPU 百分比无效")
    if not _number(payload["ram"], 0, 100):
        raise ValueError("RAM 百分比无效")
    if not _number(payload["rx"], 0) or not _number(payload["tx"], 0):
        raise ValueError("网速无效")
    return payload


def decode_metrics_packet(data):
    """Decode one bounded UTF-8 v1 datagram, raising ValueError on invalid data."""
    if not isinstance(data, (bytes, bytearray)) or not data or len(data) > MAX_PACKET_BYTES:
        raise ValueError("UDP 包长度或类型无效")
    try:
        payload = json.loads(data.decode("utf-8"))
    except Exception as exc:
        raise ValueError("UDP JSON 无效") from exc
    return validate_metrics_payload(payload)


class UdpReceiver:
    """Listen for PC broadcasts without blocking the app's display loop."""

    def __init__(self, port=UDP_PORT, sock=None):
        if sock is None:
            import socket

            sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            try:
                sock.setblocking(False)
                sock.bind(("0.0.0.0", port))
            except OSError:
                sock.close()
                raise
        self._socket = sock

    def poll(self):
        """Return the next valid payload, or None when no datagram is ready."""
        try:
            data, _address = self._socket.recvfrom(MAX_PACKET_BYTES + 1)
        except OSError:
            return None
        try:
            return decode_metrics_packet(data)
        except ValueError:
            return None

    def close(self):
        self._socket.close()
