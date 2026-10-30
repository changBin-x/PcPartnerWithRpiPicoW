import unittest
from datetime import date

from pico.lunar import format_lunar_date


class LunarTests(unittest.TestCase):
    def test_hko_known_date_is_simplified_and_has_no_prefix(self):
        self.assertEqual(format_lunar_date(date(2026, 10, 31)), "丙午年九月廿二")

    def test_first_table_date_uses_previous_lunar_year(self):
        self.assertEqual(format_lunar_date(date(1901, 1, 1)), "庚子年冬月十一")
        self.assertEqual(format_lunar_date(date(1901, 2, 19)), "辛丑年正月初一")

    def test_date_before_chinese_new_year_uses_previous_ganzhi_year(self):
        self.assertEqual(format_lunar_date(date(2026, 2, 16)), "乙巳年腊月廿九")

    def test_chinese_new_year_starts_new_ganzhi_year(self):
        self.assertEqual(format_lunar_date(date(2026, 2, 17)), "丙午年正月初一")

    def test_leap_month_start_and_end_are_marked(self):
        self.assertEqual(format_lunar_date(date(2025, 7, 25)), "乙巳年闰六月初一")
        self.assertEqual(format_lunar_date(date(2025, 8, 22)), "乙巳年闰六月廿九")

    def test_thirty_day_month_rolls_into_next_month(self):
        self.assertEqual(format_lunar_date(date(2025, 2, 27)), "乙巳年正月三十")
        self.assertEqual(format_lunar_date(date(2025, 2, 28)), "乙巳年二月初一")

    def test_date_outside_table_range_returns_none(self):
        self.assertIsNone(format_lunar_date(date(1900, 12, 31)))
        self.assertIsNone(format_lunar_date(date(2101, 1, 1)))


if __name__ == "__main__":
    unittest.main()
