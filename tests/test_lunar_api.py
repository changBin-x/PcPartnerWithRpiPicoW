"""Tests for parsing the HKO API's traditional-Chinese date fields."""

import unittest

from pico.lunar import (
    HkoLunarService,
    fetch_hko_lunar_date,
    is_hko_api_year_supported,
    parse_hko_api_payload,
)


class HkoApiParsingTests(unittest.TestCase):
    def test_parses_ganzhi_year_and_lunar_date_without_zodiac(self):
        payload = {"LunarYear": "丙午年，馬", "LunarDate": "九月廿二"}

        self.assertEqual(parse_hko_api_payload(payload), "丙午年九月廿二")

    def test_converts_traditional_leap_month_marker(self):
        payload = {"LunarYear": "乙巳年，蛇", "LunarDate": "閏六月初一"}

        self.assertEqual(parse_hko_api_payload(payload), "乙巳年闰六月初一")

    def test_accepts_lunar_day_ten(self):
        payload = {"LunarYear": "丙午年，馬", "LunarDate": "十月初十"}

        self.assertEqual(parse_hko_api_payload(payload), "丙午年十月初十")

    def test_normalizes_hko_november_and_december_month_names(self):
        november = {"LunarYear": "丙午年，馬", "LunarDate": "十一月三十"}
        december = {"LunarYear": "丙午年，馬", "LunarDate": "十二月初二"}

        self.assertEqual(parse_hko_api_payload(november), "丙午年冬月三十")
        self.assertEqual(parse_hko_api_payload(december), "丙午年腊月初二")

    def test_rejects_malformed_lunar_api_payload(self):
        for payload in (
            {},
            {"LunarYear": "丙午年，馬", "LunarDate": "not a lunar date"},
            {"LunarYear": "丙午年，馬", "LunarDate": "九月初三", "extra": "x"},
        ):
            with self.subTest(payload=payload):
                with self.assertRaises(ValueError):
                    parse_hko_api_payload(payload)

    def test_hko_api_supports_current_year_and_two_following_years(self):
        self.assertTrue(is_hko_api_year_supported(2026, 2026))
        self.assertTrue(is_hko_api_year_supported(2028, 2026))
        self.assertFalse(is_hko_api_year_supported(2025, 2026))
        self.assertFalse(is_hko_api_year_supported(2029, 2026))

    def test_fetch_rejects_invalid_gregorian_date_before_network_setup(self):
        with self.assertRaises(ValueError):
            fetch_hko_lunar_date((2026, 2, 30), 2026, ca_path="missing-ca.pem")

    def test_online_validation_never_overrides_valid_local_table(self):
        service = HkoLunarService(fetcher=lambda _date, _year: "乙巳年闰六月初一")
        service.set_date((2026, 10, 31), now_ms=100)
        local_text = service.display_text

        self.assertEqual(local_text, "丙午年九月廿二")
        self.assertTrue(
            service.refresh_api_if_due(
                100,
                current_year=2026,
                wifi_connected=True,
                time_synced=True,
            )
        )
        self.assertTrue(service.api_mismatch)
        self.assertEqual(service.display_text, local_text)

    def test_api_failure_keeps_local_result(self):
        def fail(_date, _year):
            raise OSError("network unavailable")

        service = HkoLunarService(fetcher=fail)
        service.set_date((2026, 10, 31), now_ms=100)

        self.assertFalse(
            service.refresh_api_if_due(
                100,
                current_year=2026,
                wifi_connected=True,
                time_synced=True,
            )
        )
        self.assertEqual(service.display_text, "丙午年九月廿二")

    def test_unsupported_year_skips_api_fallback(self):
        calls = []
        service = HkoLunarService(fetcher=lambda *args: calls.append(args))
        service.set_date((1901, 1, 1), now_ms=0)

        self.assertFalse(
            service.refresh_api_if_due(
                0,
                current_year=2026,
                wifi_connected=True,
                time_synced=True,
            )
        )
        self.assertEqual(calls, [])


if __name__ == "__main__":
    unittest.main()
