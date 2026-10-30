import json
import unittest

from pc_metrics.protocol import MAX_PACKET_BYTES, decode_packet, encode_packet


class ProtocolTests(unittest.TestCase):
    def test_round_trip(self):
        packet = encode_packet(17, 1_700_000_000, 12.5, 43, 80, 1.25, 2)
        self.assertIsInstance(packet, bytes)
        self.assertLessEqual(len(packet), MAX_PACKET_BYTES)
        self.assertEqual(
            decode_packet(packet),
            {"v": 1, "seq": 17, "ts": 1_700_000_000, "cpu": 12.5,
             "gpu": 43.0, "ram": 80.0, "rx": 1.25, "tx": 2.0},
        )

    def test_gpu_may_be_null(self):
        packet = encode_packet(0, 1, 0, None, 100, 0, 0)
        self.assertIsNone(decode_packet(packet)["gpu"])

    def test_encode_rejects_percentages_out_of_range(self):
        for args in ((0, 1, -0.1, None, 0, 0, 0), (0, 1, 0, 101, 0, 0, 0)):
            with self.subTest(args=args), self.assertRaises(ValueError):
                encode_packet(*args)

    def test_decode_rejects_percentage_out_of_range(self):
        payload = {"v": 1, "seq": 0, "ts": 1, "cpu": 0, "gpu": None,
                   "ram": 100.1, "rx": 0, "tx": 0}
        with self.assertRaises(ValueError):
            decode_packet(json.dumps(payload).encode())

    def test_rejects_negative_sequence(self):
        with self.assertRaises(ValueError):
            encode_packet(-1, 1, 0, None, 0, 0, 0)
        payload = {"v": 1, "seq": -1, "ts": 1, "cpu": 0, "gpu": None,
                   "ram": 0, "rx": 0, "tx": 0}
        with self.assertRaises(ValueError):
            decode_packet(json.dumps(payload).encode())

    def test_rejects_zero_timestamp(self):
        with self.assertRaises(ValueError):
            encode_packet(0, 0, 0, None, 0, 0, 0)
        payload = {"v": 1, "seq": 0, "ts": 0, "cpu": 0, "gpu": None,
                   "ram": 0, "rx": 0, "tx": 0}
        with self.assertRaises(ValueError):
            decode_packet(json.dumps(payload).encode())

    def test_rejects_negative_rx_rate(self):
        with self.assertRaises(ValueError):
            encode_packet(0, 1, 0, None, 0, -0.1, 0)
        payload = {"v": 1, "seq": 0, "ts": 1, "cpu": 0, "gpu": None,
                   "ram": 0, "rx": -0.1, "tx": 0}
        with self.assertRaises(ValueError):
            decode_packet(json.dumps(payload).encode())

    def test_rejects_negative_tx_rate(self):
        with self.assertRaises(ValueError):
            encode_packet(0, 1, 0, None, 0, 0, -0.1)
        payload = {"v": 1, "seq": 0, "ts": 1, "cpu": 0, "gpu": None,
                   "ram": 0, "rx": 0, "tx": -0.1}
        with self.assertRaises(ValueError):
            decode_packet(json.dumps(payload).encode())

    def test_rejects_oversized_packet_on_encode_and_decode(self):
        with self.assertRaises(ValueError):
            encode_packet(10**600, 1, 0, None, 0, 0, 0)
        with self.assertRaises(ValueError):
            decode_packet(b" " * (MAX_PACKET_BYTES + 1))

    def test_accepts_packet_exactly_at_byte_limit(self):
        compact = encode_packet(0, 1, 0, None, 0, 0, 0)
        packet = compact[:-1] + b" " * (MAX_PACKET_BYTES - len(compact)) + b"}"
        self.assertEqual(len(packet), MAX_PACKET_BYTES)
        self.assertEqual(decode_packet(packet)["seq"], 0)

    def test_rejects_duplicate_json_keys(self):
        packet = (b'{"v":1,"seq":0,"ts":1,"cpu":0,"gpu":null,'
                  b'"ram":0,"rx":0,"tx":0,"seq":0}')
        with self.assertRaises(ValueError):
            decode_packet(packet)

    def test_rejects_invalid_utf8(self):
        packet = b'{"v":1,"seq":0,"ts":1,"cpu":0,"gpu":null,'
        packet += b'"ram":0,"rx":0,"tx":0}\xff'
        with self.assertRaises(ValueError):
            decode_packet(packet)

    def test_rejects_top_level_non_object(self):
        packet = b'["v","seq","ts","cpu","gpu","ram","rx","tx"]'
        with self.assertRaises(ValueError):
            decode_packet(packet)

    def test_rejects_exponent_overflow(self):
        packet = (b'{"v":1,"seq":0,"ts":1,"cpu":0,"gpu":null,'
                  b'"ram":0,"rx":1e999,"tx":0}')
        with self.assertRaises(ValueError):
            decode_packet(packet)

    def test_large_integer_throughput_round_trips_exactly(self):
        throughput = 10**400
        packet = encode_packet(0, 1, 0, None, 0, throughput, 0)
        self.assertLessEqual(len(packet), MAX_PACKET_BYTES)
        decoded = decode_packet(packet)
        self.assertEqual(decoded["rx"], throughput)
        self.assertIsInstance(decoded["rx"], int)
        self.assertEqual(decoded["tx"], 0)

    def test_invalid_throughput_values_raise_value_error(self):
        for value in (-1, True, "1", float("nan"), float("inf"), float("-inf")):
            with self.subTest(value=value), self.assertRaises(ValueError):
                encode_packet(0, 1, 0, None, 0, value, 0)
        for value in (-1, True, "1", float("nan"), float("inf"), float("-inf")):
            payload = {"v": 1, "seq": 0, "ts": 1, "cpu": 0, "gpu": None,
                       "ram": 0, "rx": value, "tx": 0}
            encoded = json.dumps(payload, allow_nan=True).encode()
            with self.subTest(decoded_value=value), self.assertRaises(ValueError):
                decode_packet(encoded)

    def test_encode_packet_uses_compact_json(self):
        packet = encode_packet(0, 1, 0, None, 0, 0, 0)
        self.assertEqual(
            packet,
            b'{"v":1,"seq":0,"ts":1,"cpu":0,"gpu":null,'
            b'"ram":0,"rx":0,"tx":0}',
        )
        self.assertNotIn(b" ", packet)

    def test_rejects_invalid_json(self):
        with self.assertRaises(ValueError):
            decode_packet(b"{")

    def test_rejects_version_and_field_mismatch(self):
        base = {"v": 1, "seq": 0, "ts": 1, "cpu": 0, "gpu": None,
                "ram": 0, "rx": 0, "tx": 0}
        for payload in (
            {**base, "v": 2},
            {key: value for key, value in base.items() if key != "tx"},
            {**base, "extra": 1},
        ):
            with self.subTest(payload=payload), self.assertRaises(ValueError):
                decode_packet(json.dumps(payload).encode())

    def test_rejects_bool_as_integer_or_number_and_nonfinite_values(self):
        for args in ((True, 1, 0, None, 0, 0, 0), (0, True, 0, None, 0, 0, 0),
                     (0, 1, True, None, 0, 0, 0), (0, 1, 0, None, 0, float("nan"), 0)):
            with self.subTest(args=args), self.assertRaises(ValueError):
                encode_packet(*args)
        for value in (True, float("nan"), float("inf")):
            payload = {"v": 1, "seq": 0, "ts": 1, "cpu": 0, "gpu": None,
                       "ram": 0, "rx": value, "tx": 0}
            with self.subTest(value=value), self.assertRaises(ValueError):
                decode_packet(json.dumps(payload, allow_nan=True).encode())

    def test_rejects_empty_and_non_bytes_payloads(self):
        for data in (b"", "{}", bytearray(b"{}"), None):
            with self.subTest(data=data), self.assertRaises(ValueError):
                decode_packet(data)


if __name__ == "__main__":
    unittest.main()
