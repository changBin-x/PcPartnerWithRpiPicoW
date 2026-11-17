import json
import unittest

from pc_metrics.protocol import (
    LABEL_MAX_BYTES,
    MAX_PACKET_BYTES,
    PACKET_VERSION,
    decode_packet,
    encode_packet,
    sanitize_label,
)

LABELS = {"cpu_label": "CPU", "gpu_label": "GPU", "ram_label": "16G"}


def _encode(*args, **overrides):
    labels = dict(LABELS)
    labels.update(overrides)
    return encode_packet(*args, **labels)


def _payload(**overrides):
    base = {
        "v": PACKET_VERSION,
        "seq": 0,
        "ts": 1,
        "cpu": 0,
        "gpu": None,
        "ram": 0,
        "rx": 0,
        "tx": 0,
        **LABELS,
    }
    base.update(overrides)
    return base


class ProtocolTests(unittest.TestCase):
    def test_round_trip(self):
        packet = _encode(17, 1_700_000_000, 12.5, 43, 80, 1.25, 2)
        self.assertIsInstance(packet, bytes)
        self.assertLessEqual(len(packet), MAX_PACKET_BYTES)
        self.assertEqual(
            decode_packet(packet),
            {
                "v": 2,
                "seq": 17,
                "ts": 1_700_000_000,
                "cpu": 12.5,
                "gpu": 43.0,
                "ram": 80.0,
                "rx": 1.25,
                "tx": 2.0,
                **LABELS,
            },
        )

    def test_gpu_may_be_null(self):
        packet = _encode(0, 1, 0, None, 100, 0, 0)
        self.assertIsNone(decode_packet(packet)["gpu"])

    def test_identity_labels_round_trip(self):
        packet = _encode(
            0,
            1,
            0,
            None,
            0,
            0,
            0,
            cpu_label="Orin NX",
            gpu_label="Orin nvgpu",
            ram_label="16G",
        )
        decoded = decode_packet(packet)
        self.assertEqual(decoded["cpu_label"], "Orin NX")
        self.assertEqual(decoded["gpu_label"], "Orin nvgpu")
        self.assertEqual(decoded["ram_label"], "16G")

    def test_sanitize_label_truncates_and_strips_non_ascii(self):
        self.assertEqual(sanitize_label("  RTX 3060  "), "RTX 3060")
        self.assertEqual(sanitize_label("αCPU"), "CPU")
        self.assertEqual(
            sanitize_label("A" * (LABEL_MAX_BYTES + 4)),
            "A" * LABEL_MAX_BYTES,
        )
        self.assertIsNone(sanitize_label(""))
        self.assertIsNone(sanitize_label("   "))
        self.assertIsNone(sanitize_label(None))

    def test_encode_rejects_invalid_labels(self):
        for kwargs in (
            {"cpu_label": ""},
            {"cpu_label": "A" * (LABEL_MAX_BYTES + 1)},
            {"gpu_label": "显卡"},
            {"ram_label": "16G\n"},
            {"cpu_label": 1},
        ):
            with self.subTest(kwargs=kwargs), self.assertRaises(ValueError):
                _encode(0, 1, 0, None, 0, 0, 0, **kwargs)

    def test_encode_rejects_percentages_out_of_range(self):
        for args in ((0, 1, -0.1, None, 0, 0, 0), (0, 1, 0, 101, 0, 0, 0)):
            with self.subTest(args=args), self.assertRaises(ValueError):
                _encode(*args)

    def test_decode_rejects_percentage_out_of_range(self):
        payload = _payload(ram=100.1)
        with self.assertRaises(ValueError):
            decode_packet(json.dumps(payload).encode())

    def test_rejects_negative_sequence(self):
        with self.assertRaises(ValueError):
            _encode(-1, 1, 0, None, 0, 0, 0)
        payload = _payload(seq=-1)
        with self.assertRaises(ValueError):
            decode_packet(json.dumps(payload).encode())

    def test_rejects_zero_timestamp(self):
        with self.assertRaises(ValueError):
            _encode(0, 0, 0, None, 0, 0, 0)
        payload = _payload(ts=0)
        with self.assertRaises(ValueError):
            decode_packet(json.dumps(payload).encode())

    def test_rejects_negative_rx_rate(self):
        with self.assertRaises(ValueError):
            _encode(0, 1, 0, None, 0, -0.1, 0)
        payload = _payload(rx=-0.1)
        with self.assertRaises(ValueError):
            decode_packet(json.dumps(payload).encode())

    def test_rejects_negative_tx_rate(self):
        with self.assertRaises(ValueError):
            _encode(0, 1, 0, None, 0, 0, -0.1)
        payload = _payload(tx=-0.1)
        with self.assertRaises(ValueError):
            decode_packet(json.dumps(payload).encode())

    def test_rejects_oversized_packet_on_encode_and_decode(self):
        with self.assertRaises(ValueError):
            _encode(10**600, 1, 0, None, 0, 0, 0)
        with self.assertRaises(ValueError):
            decode_packet(b" " * (MAX_PACKET_BYTES + 1))

    def test_accepts_packet_exactly_at_byte_limit(self):
        compact = _encode(0, 1, 0, None, 0, 0, 0)
        packet = compact[:-1] + b" " * (MAX_PACKET_BYTES - len(compact)) + b"}"
        self.assertEqual(len(packet), MAX_PACKET_BYTES)
        self.assertEqual(decode_packet(packet)["seq"], 0)

    def test_rejects_duplicate_json_keys(self):
        packet = (
            b'{"v":2,"seq":0,"ts":1,"cpu":0,"gpu":null,'
            b'"ram":0,"rx":0,"tx":0,"cpu_label":"CPU","gpu_label":"GPU",'
            b'"ram_label":"16G","seq":0}'
        )
        with self.assertRaisesRegex(ValueError, "重复字段.*seq"):
            decode_packet(packet)

    def test_rejects_invalid_utf8(self):
        packet = (
            b'{"v":2,"seq":0,"ts":1,"cpu":0,"gpu":null,'
            b'"ram":0,"rx":0,"tx":0,"cpu_label":"CPU","gpu_label":"GPU",'
            b'"ram_label":"16G"}\xff'
        )
        with self.assertRaises(ValueError):
            decode_packet(packet)

    def test_rejects_top_level_non_object(self):
        packet = b'["v","seq","ts","cpu","gpu","ram","rx","tx"]'
        with self.assertRaises(ValueError):
            decode_packet(packet)

    def test_rejects_exponent_overflow(self):
        packet = (
            b'{"v":2,"seq":0,"ts":1,"cpu":0,"gpu":null,'
            b'"ram":0,"rx":1e999,"tx":0,"cpu_label":"CPU",'
            b'"gpu_label":"GPU","ram_label":"16G"}'
        )
        with self.assertRaises(ValueError):
            decode_packet(packet)

    def test_large_integer_throughput_round_trips_exactly(self):
        # v2 labels consume extra bytes; keep a still-huge int within 512.
        throughput = 10**350
        packet = _encode(0, 1, 0, None, 0, throughput, 0)
        self.assertLessEqual(len(packet), MAX_PACKET_BYTES)
        decoded = decode_packet(packet)
        self.assertEqual(decoded["rx"], throughput)
        self.assertIsInstance(decoded["rx"], int)
        self.assertEqual(decoded["tx"], 0)

    def test_invalid_throughput_values_raise_value_error(self):
        for value in (-1, True, "1", float("nan"), float("inf"), float("-inf")):
            with self.subTest(value=value), self.assertRaises(ValueError):
                _encode(0, 1, 0, None, 0, value, 0)
        for value in (-1, True, "1", float("nan"), float("inf"), float("-inf")):
            payload = _payload(rx=value)
            encoded = json.dumps(payload, allow_nan=True).encode()
            with self.subTest(decoded_value=value), self.assertRaises(ValueError):
                decode_packet(encoded)

    def test_encode_packet_uses_compact_json(self):
        packet = _encode(0, 1, 0, None, 0, 0, 0)
        self.assertEqual(
            packet,
            b'{"v":2,"seq":0,"ts":1,"cpu":0,"gpu":null,'
            b'"ram":0,"rx":0,"tx":0,"cpu_label":"CPU",'
            b'"gpu_label":"GPU","ram_label":"16G"}',
        )
        self.assertNotIn(b" ", packet)

    def test_rejects_invalid_json(self):
        with self.assertRaises(ValueError):
            decode_packet(b"{")

    def test_rejects_version_and_field_mismatch(self):
        base = _payload()
        for payload in (
            {**base, "v": 1},
            {**base, "v": 3},
            {key: value for key, value in base.items() if key != "tx"},
            {key: value for key, value in base.items() if key != "cpu_label"},
            {**base, "extra": 1},
        ):
            with self.subTest(payload=payload), self.assertRaises(ValueError):
                decode_packet(json.dumps(payload).encode())

    def test_rejects_bool_as_integer_or_number_and_nonfinite_values(self):
        for args in (
            (True, 1, 0, None, 0, 0, 0),
            (0, True, 0, None, 0, 0, 0),
            (0, 1, True, None, 0, 0, 0),
            (0, 1, 0, None, 0, float("nan"), 0),
        ):
            with self.subTest(args=args), self.assertRaises(ValueError):
                _encode(*args)
        for value in (True, float("nan"), float("inf")):
            payload = _payload(rx=value)
            with self.subTest(value=value), self.assertRaises(ValueError):
                decode_packet(json.dumps(payload, allow_nan=True).encode())

    def test_rejects_empty_and_non_bytes_payloads(self):
        for data in (b"", "{}", bytearray(b"{}"), None):
            with self.subTest(data=data), self.assertRaises(ValueError):
                decode_packet(data)


if __name__ == "__main__":
    unittest.main()
