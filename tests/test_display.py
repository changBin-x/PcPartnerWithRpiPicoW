import math
import unittest

from pico.display import TRACK, _text_width, draw_clock, draw_dashboard
from pico.font_cn import FONT_CN
from pico.st7735 import ST7735


class RecordingCanvas:
    width = 128
    height = 160

    def __init__(self):
        self.calls = []

    def fill(self, color):
        self.calls.append(("fill", color))

    def fill_rect(self, x, y, width, height, color):
        self.calls.append(("rect", x, y, width, height, color))

    def hline(self, x, y, width, color):
        self.calls.append(("line", x, y, width, color))

    def text(self, x, y, value, color, scale=1, background=None):
        self.calls.append(("text", x, y, value, color, scale, background))


class RecordingPin:
    def value(self, value=None):
        return value


class RecordingSPI:
    def __init__(self):
        self.writes = []

    def write(self, data):
        self.writes.append(bytes(data))


class DisplayTests(unittest.TestCase):
    def test_required_weekday_and_lunar_day_characters_have_glyphs(self):
        for text in ("星期一", "初一"):
            missing = [character for character in text if character not in FONT_CN]
            self.assertEqual(missing, [], "缺少字形: %s" % "".join(missing))

    def test_driver_does_not_silently_replace_missing_chinese_with_question_mark(self):
        screen = ST7735.__new__(ST7735)
        with self.assertRaisesRegex(ValueError, "缺少中文字形"):
            screen._draw_chinese(0, 0, "龘", 0xFFFF, 0x0000)

    def test_driver_stops_before_emitting_glyphs_past_right_edge(self):
        screen = ST7735.__new__(ST7735)
        screen.width = 128
        screen.height = 160
        screen.spi = RecordingSPI()
        screen.cs = RecordingPin()
        screen.dc = RecordingPin()

        screen.text(64, 120, "ABCDEFGHIJKLM", 0xFFFF, background=0)

        # Ten 6-pixel cells fit from x=64 through x=123; the last three do not.
        self.assertEqual(len(screen.spi.writes), 10 * 6)

    def test_dashboard_shows_missing_and_invalid_percentages_as_dashes(self):
        canvas = RecordingCanvas()

        draw_dashboard(
            canvas,
            "15:28:41",
            "周六",
            "10/31",
            "丙午年九月廿二",
            {"cpu": 38.0, "gpu": None, "ram": math.nan, "rx": 2.3, "tx": 0.4},
            True,
        )

        visible_text = [call[3] for call in canvas.calls if call[0] == "text"]
        self.assertIn("--", visible_text)
        self.assertIn("38%", visible_text)
        self.assertIn("RX 2.3MB/s", visible_text)
        self.assertIn("TX 0.4MB/s", visible_text)
        self.assertIn("丙午年九月廿二", visible_text)

    def test_dashboard_clamps_only_the_bar_for_valid_percentages(self):
        canvas = RecordingCanvas()

        draw_dashboard(
            canvas,
            "00:00:00",
            "周日",
            "01/01",
            "丙午年冬月廿三",
            {"cpu": 50, "gpu": 100, "ram": -1, "rx": None, "tx": float("inf")},
            False,
        )

        rectangles = [call for call in canvas.calls if call[0] == "rect"]
        self.assertTrue(all(call[1] >= 0 and call[2] >= 0 for call in rectangles))
        self.assertTrue(all(call[1] + call[3] <= canvas.width for call in rectangles))
        self.assertTrue(all(call[2] + call[4] <= canvas.height for call in rectangles))
        self.assertTrue(any(call[1:5] == (10, 78, 54, 4) for call in rectangles))
        self.assertTrue(any(call[1:5] == (10, 104, 108, 4) for call in rectangles))
        self.assertFalse(
            any(call[1] == 10 and call[2] == 130 and call[5] != TRACK for call in rectangles)
        )
        visible_text = [call[3] for call in canvas.calls if call[0] == "text"]
        self.assertIn("--", visible_text)
        self.assertIn("50%", visible_text)
        self.assertIn("100%", visible_text)
        self.assertIn("RX --MB/s", visible_text)
        self.assertIn("TX --MB/s", visible_text)
        for call in canvas.calls:
            if call[0] == "text":
                self.assertLessEqual(call[1] + _text_width(call[3]) * call[5], canvas.width)

    def test_clock_without_valid_rtc_omits_date_and_shows_sync_message(self):
        canvas = RecordingCanvas()

        draw_clock(canvas, None, None, None, None, False, False)

        visible_text = [call[3] for call in canvas.calls if call[0] == "text"]
        self.assertIn("时间同步中", visible_text)
        self.assertNotIn("1970", "".join(value for value in visible_text if isinstance(value, str)))

    def test_clock_shows_chinese_date_and_status(self):
        canvas = RecordingCanvas()

        draw_clock(
            canvas,
            "15:28:41",
            "2026年10月31日",
            "星期六",
            "丙午年九月廿二",
            True,
            True,
        )

        visible_text = [call[3] for call in canvas.calls if call[0] == "text"]
        self.assertIn("2026年10月31日", visible_text)
        self.assertIn("星期六", visible_text)
        self.assertIn("丙午年九月廿二", visible_text)
        self.assertTrue(any(value.startswith("NTP") for value in visible_text if isinstance(value, str)))
        for call in canvas.calls:
            if call[0] == "text":
                self.assertLessEqual(call[1] + _text_width(call[3]) * call[5], canvas.width)


if __name__ == "__main__":
    unittest.main()
