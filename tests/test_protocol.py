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
