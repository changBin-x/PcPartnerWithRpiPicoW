import math
import unittest
from pathlib import Path

from pico.display import (
    ICON_ACTIVE,
    ICON_HEIGHT,
    ICON_INACTIVE,
    ICON_PC,
    ICON_WIFI,
    ICON_WIDTH,
    LINE_BASELINE,
    TRACK,
    _text_width,
    draw_clock,
    draw_dashboard,
)
from pico.font_cn import (
    ASCII_FONT,
    ASCII_HEIGHT,
    ASCII_WIDTH,
    CHINESE_HEIGHT,
    CHINESE_WIDTH,
    CLOCK_DIGIT_HEIGHT,
    CLOCK_DIGIT_WIDTH,
    CLOCK_FONT,
    FONT_CN,
    FONT_CN_BLOB_BYTES,
    FONT_CN_BYTES_PER_GLYPH,
    FONT_CN_ORDER,
)
from tools.build_font import ASCII_CHARACTERS, CHINESE_CHARACTERS, verify_font_set
from pico.st7735 import (
    ST7735,
    blend_rgb565,
    clock_colon_coverage,
    pack_coverage,
    unpack_coverage,
)


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

    def text(self, x, y, value, color, background=None):
        self.calls.append(("text", x, y, value, color, background))

    def clock_text(self, x, y, value, color, background=None):
        self.calls.append(("clock", x, y, value, color, background))

    def bitmap(self, x, y, width, height, packed, color, background=None):
        self.calls.append(("bitmap", x, y, width, height, packed, color, background))


class RecordingPin:
    def value(self, value=None):
        return value


class RecordingSPI:
    def __init__(self):
        self.writes = []

    def write(self, data):
        self.writes.append(bytes(data))


class DisplayTests(unittest.TestCase):
    def test_coverage_nibbles_round_trip_and_preserve_two_pixels_per_byte(self):
        coverage = [0, 1, 7, 8, 14, 15]
        packed = pack_coverage(coverage)
        self.assertEqual(len(packed), 3)
        self.assertEqual(unpack_coverage(packed, len(coverage)), coverage)

    def test_coverage_blend_produces_intermediate_rgb565_color(self):
        black = 0x0000
        white = 0xFFFF
        half = blend_rgb565(white, black, 8)
        self.assertNotEqual(half, black)
        self.assertNotEqual(half, white)
        self.assertEqual(blend_rgb565(white, black, 0), black)
        self.assertEqual(blend_rgb565(white, black, 15), white)

    def test_text_glyph_cells_share_baseline_and_use_native_dimensions(self):
        self.assertEqual((CHINESE_WIDTH, CHINESE_HEIGHT), (14, 14))
        self.assertEqual((ASCII_WIDTH, ASCII_HEIGHT), (8, 14))
        self.assertEqual(LINE_BASELINE, 11)
        self.assertEqual(len(FONT_CN["一"]), 98)
        self.assertTrue(any(0 < value < 15 for value in unpack_coverage(FONT_CN["中"], 14 * 14)))
        chinese_phrases = (
            "星期一", "初一", "丙午年九月廿二", "2026年10月31日",
            "历日暂不可用", "时间同步中",
        )
        for text in chinese_phrases:
            for character in text:
                if ord(character) >= 128:
                    self.assertIn(character, CHINESE_CHARACTERS)
                    self.assertIn(character, FONT_CN)
                else:
                    self.assertIn(character, ASCII_CHARACTERS)
                    self.assertIn(character, ASCII_FONT)
        ascii_phrases = ("ETH RX 125.0MB/s", "NTP SYNC UTC+8", "PC", "MEM")
        for text in ascii_phrases:
            for character in text:
                self.assertIn(character, ASCII_CHARACTERS)
                self.assertIn(character, ASCII_FONT)
        verify_font_set(FONT_CN, ASCII_FONT, CLOCK_FONT)
        self.assertEqual(len(ASCII_FONT["1"]), 56)
        self.assertEqual(_text_width("星期一 10/31"), 90)

    def test_chinese_glyphs_are_flash_backed_to_save_heap(self):
        blob_path = Path(__file__).resolve().parent.parent / "pico" / "font_cn.bin"
        self.assertEqual(FONT_CN_BYTES_PER_GLYPH, 98)
        self.assertEqual(len(FONT_CN_ORDER), len(FONT_CN))
        self.assertEqual(blob_path.stat().st_size, FONT_CN_BLOB_BYTES)
        self.assertEqual(FONT_CN_BLOB_BYTES, len(FONT_CN_ORDER) * FONT_CN_BYTES_PER_GLYPH)
        with blob_path.open("rb") as blob:
            index = FONT_CN_ORDER.find("宁")
            self.assertGreaterEqual(index, 0)
            blob.seek(index * FONT_CN_BYTES_PER_GLYPH)
            self.assertEqual(FONT_CN["宁"], blob.read(FONT_CN_BYTES_PER_GLYPH))
        self.assertIsNone(FONT_CN.get("\U00020000"))
        self.assertNotIn("\U00020000", FONT_CN)

    def test_large_clock_digits_have_separate_smooth_glyph_set(self):
        self.assertEqual((CLOCK_DIGIT_WIDTH, CLOCK_DIGIT_HEIGHT), (14, 26))
        self.assertTrue(all(len(CLOCK_FONT[digit]) == 182 for digit in "0123456789"))
        self.assertTrue(any(0 < value < 15 for value in unpack_coverage(CLOCK_FONT["8"], 14 * 26)))

    def test_clock_colon_is_two_round_dots_symmetric_around_center(self):
        width, height = 8, 26
        coverage = clock_colon_coverage(width, height)
        self.assertEqual(len(coverage), width * height)
        self.assertTrue(any(0 < value < 15 for value in coverage))
        active = {(i % width, i // width) for i, value in enumerate(coverage) if value >= 8}
        components = []
        pending = set(active)
        while pending:
            component = {pending.pop()}
            stack = list(component)
            while stack:
                x, y = stack.pop()
                for neighbor in ((x - 1, y), (x + 1, y), (x, y - 1), (x, y + 1)):
                    if neighbor in pending:
                        pending.remove(neighbor)
                        component.add(neighbor)
                        stack.append(neighbor)
            components.append(component)
        self.assertEqual(len(components), 2)
        centers = [sum(y for _, y in part) / len(part) for part in components]
        self.assertAlmostEqual(centers[0] + centers[1], height - 1, delta=1)
        self.assertTrue(all(len(part) >= 4 for part in components))

    def test_driver_rejects_unlisted_cjk_instead_of_substituting_question_mark(self):
        screen = ST7735.__new__(ST7735)
        with self.assertRaisesRegex(ValueError, "缺少中文字形"):
            screen._draw_chinese(0, 0, "龘", 0xFFFF, 0x0000)

    def test_driver_clips_ascii_at_right_edge_without_scaling_5x7_font(self):
        screen = ST7735.__new__(ST7735)
        screen.width = 128
        screen.height = 160
        screen.spi = RecordingSPI()
        screen.cs = RecordingPin()
        screen.dc = RecordingPin()
        screen._set_window = lambda *args: None
        screen.text(64, 120, "ABCDEFGHIJKLM", 0xFFFF, background=0)
        self.assertEqual(len(screen.spi.writes), 1)
        self.assertEqual(len(screen.spi.writes[0]), 64 * 14 * 2)

    def test_glyph_spi_blocks_never_exceed_three_kilobytes(self):
        screen = ST7735.__new__(ST7735)
        screen.width = 128
        screen.height = 160
        screen.spi = RecordingSPI()
        screen.cs = RecordingPin()
        screen.dc = RecordingPin()
        screen._set_window = lambda *args: None
        screen.clock_text(12, 34, "15:28:41", 0xFFFF, 0x0000)
        self.assertEqual(len(screen.spi.writes), 2)
        self.assertLessEqual(max(len(write) for write in screen.spi.writes), 3072)

    def test_status_icons_replace_header_text_in_both_layouts(self):
        for wifi_ok in (True, False):
            with self.subTest(layout="dashboard", wifi_ok=wifi_ok):
                canvas = RecordingCanvas()
                draw_dashboard(
                    canvas, "15:28:41", "周六", "10/31", "丙午年九月廿二",
                    {"cpu": 20, "gpu": 30, "ram": 40, "rx": 1.0, "tx": 0.5},
                    wifi_ok,
                )
                self._assert_status_icons(canvas, pc_active=True, wifi_ok=wifi_ok)
            with self.subTest(layout="clock", wifi_ok=wifi_ok):
                canvas = RecordingCanvas()
                draw_clock(
                    canvas, "15:28:41", "2026年10月31日", "星期六",
                    "丙午年九月廿二", wifi_ok, True,
                )
                self._assert_status_icons(canvas, pc_active=False, wifi_ok=wifi_ok)

    def _assert_status_icons(self, canvas, pc_active, wifi_ok):
        header_text = [call for call in canvas.calls if call[0] == "text" and call[2] < 4]
        self.assertEqual(header_text, [])
        icons = [call for call in canvas.calls if call[0] == "bitmap"]
        self.assertEqual(len(icons), 2)
        pc_icon, wifi_icon = icons
        self.assertEqual(pc_icon[1:6], (4, 0, ICON_WIDTH, ICON_HEIGHT, ICON_PC))
        self.assertEqual(
            wifi_icon[1:6],
            (canvas.width - 4 - ICON_WIDTH, 0, ICON_WIDTH, ICON_HEIGHT, ICON_WIFI),
        )
        self.assertEqual(pc_icon[6], ICON_ACTIVE if pc_active else ICON_INACTIVE)
        self.assertEqual(wifi_icon[6], ICON_ACTIVE if wifi_ok else ICON_INACTIVE)
        for icon in icons:
            self.assertGreaterEqual(icon[1], 0)
            self.assertGreaterEqual(icon[2], 0)
            self.assertLessEqual(icon[1] + icon[3], canvas.width)
            self.assertLessEqual(icon[2] + icon[4], canvas.height)

    def test_status_icons_are_antialiased_112_byte_masks(self):
        for packed in (ICON_PC, ICON_WIFI):
            self.assertEqual(len(packed), ICON_WIDTH * ICON_HEIGHT // 2)
            coverage = unpack_coverage(packed, ICON_WIDTH * ICON_HEIGHT)
            self.assertEqual(coverage[0], 0)
            self.assertEqual(coverage[-1], 0)
            self.assertIn(15, coverage)
            self.assertTrue(any(0 < value < 15 for value in coverage))

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
        self.assertIn("ETH RX 2.3MB/s", visible_text)
        self.assertIn("ETH TX 0.4MB/s", visible_text)
        self.assertIn("丙午年九月廿二", visible_text)

    def test_dashboard_keeps_large_ethernet_rates_inside_the_display(self):
        canvas = RecordingCanvas()
        draw_dashboard(
            canvas, "23:59:59", "周六", "10/31", "丙午年九月廿二",
            {"cpu": 0, "gpu": 0, "ram": 0, "rx": 125.0, "tx": 1260.0}, True,
        )
        rate_calls = [
            call for call in canvas.calls
            if call[0] == "text" and call[3].startswith("ETH")
        ]
        self.assertEqual([call[3] for call in rate_calls], ["ETH RX 125.0MB/s", "ETH TX 1.3e3MB/s"])
        self.assertTrue(all(call[1] + _text_width(call[3]) <= canvas.width for call in rate_calls))

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
        self.assertTrue(any(call[1:5] == (10, 76, 54, 3) for call in rectangles))
        self.assertTrue(any(call[1:5] == (10, 98, 108, 3) for call in rectangles))
        self.assertFalse(any(
            call[1] == 10 and call[2] == 130 and call[5] != TRACK
            for call in rectangles
        ))
        visible_text = [call[3] for call in canvas.calls if call[0] == "text"]
        self.assertIn("--", visible_text)
        self.assertIn("50%", visible_text)
        self.assertIn("100%", visible_text)
        self.assertIn("ETH RX --MB/s", visible_text)
        self.assertIn("ETH TX --MB/s", visible_text)
        for call in canvas.calls:
            if call[0] == "text":
                self.assertLessEqual(call[1] + _text_width(call[3]), canvas.width)

    def test_clock_without_valid_rtc_omits_date_and_shows_sync_message(self):
        canvas = RecordingCanvas()
        draw_clock(canvas, None, None, None, None, False, False)
        visible_text = [call[3] for call in canvas.calls if call[0] == "text"]
        self.assertIn("时间同步中", visible_text)
        self.assertNotIn("1970", "".join(value for value in visible_text if isinstance(value, str)))

    def test_clock_uses_dedicated_large_time_renderer_and_chinese_date(self):
        canvas = RecordingCanvas()
        draw_clock(canvas, "15:28:41", "2026年10月31日", "星期六", "丙午年九月廿二", True, True)
        clock_calls = [call for call in canvas.calls if call[0] == "clock"]
        self.assertEqual(len(clock_calls), 1)
        self.assertEqual(clock_calls[0][3], "15:28:41")
        visible_text = [call[3] for call in canvas.calls if call[0] == "text"]
        self.assertIn("2026年10月31日", visible_text)
        self.assertIn("星期六", visible_text)
        self.assertIn("丙午年九月廿二", visible_text)
        self.assertNotIn("NTP", "".join(str(value) for value in visible_text))
        for call in canvas.calls:
            if call[0] == "text":
                self.assertLessEqual(call[1] + _text_width(call[3]), canvas.width)
        self.assertTrue(all(
            call[2] + (CLOCK_DIGIT_HEIGHT if call[0] == "clock" else 14) <= canvas.height
            for call in canvas.calls if call[0] in ("text", "clock")
        ))


def _draw_dashboard_frame(canvas, metrics, previous=None, **changes):
    values = {
        "time_text": "15:28:41",
        "weekday_short": "周六",
        "date_text": "10/31",
        "lunar_text": "丙午年九月廿二",
        "metrics": metrics,
        "wifi_ok": True,
    }
    values.update(changes)
    return draw_dashboard(
        canvas,
        values["time_text"],
        values["weekday_short"],
        values["date_text"],
        values["lunar_text"],
        values["metrics"],
        values["wifi_ok"],
        previous=previous,
    )


def _draw_clock_frame(canvas, time_text="15:28:41", previous=None, **changes):
    values = {
        "date_text": "2026年10月31日",
        "weekday_full": "星期六",
        "lunar_text": "丙午年九月廿二",
        "wifi_ok": True,
        "time_synced": True,
        "needs_config": False,
    }
    values.update(changes)
    return draw_clock(
        canvas,
        time_text,
        values["date_text"],
        values["weekday_full"],
        values["lunar_text"],
        values["wifi_ok"],
        values["time_synced"],
        previous=previous,
        needs_config=values["needs_config"],
    )


class PartialRefreshTests(unittest.TestCase):
    def test_unchanged_dashboard_frame_performs_no_writes(self):
        canvas = RecordingCanvas()
        metrics = {"cpu": 20, "gpu": 30, "ram": 40, "rx": 1.0, "tx": 0.5}
        previous = _draw_dashboard_frame(canvas, metrics)
        self.assertIn("fill", [call[0] for call in canvas.calls])

        canvas.calls.clear()
        current = _draw_dashboard_frame(canvas, dict(metrics, cpu=20.4), previous)

        self.assertEqual(current, previous)
        self.assertEqual(canvas.calls, [])

    def test_dashboard_cpu_change_repaints_only_cpu_card(self):
        canvas = RecordingCanvas()
        metrics = {"cpu": 20, "gpu": 30, "ram": 40, "rx": 1.0, "tx": 0.5}
        previous = _draw_dashboard_frame(canvas, metrics)
        canvas.calls.clear()

        changed = dict(metrics, cpu=21)
        _draw_dashboard_frame(canvas, changed, previous)

        self.assertNotIn("fill", [call[0] for call in canvas.calls])
        self.assertTrue(canvas.calls)
        for call in canvas.calls:
            if call[0] == "rect":
                self.assertGreaterEqual(call[2], 61)
                self.assertLess(call[2] + call[4], 83)
            elif call[0] == "text":
                self.assertIn(call[3], ("CPU", "21%"))

    def test_clock_second_change_repaints_only_time_band(self):
        canvas = RecordingCanvas()
        previous = _draw_clock_frame(canvas)
        canvas.calls.clear()

        _draw_clock_frame(canvas, "15:28:42", previous)

        self.assertNotIn("fill", [call[0] for call in canvas.calls])
        self.assertTrue(canvas.calls)
        for call in canvas.calls:
            self.assertIn(call[0], ("rect", "clock"))
            if call[0] == "rect":
                self.assertGreaterEqual(call[2], 34)
                self.assertLess(call[2] + call[4], 61)

    def test_switching_layout_forces_full_redraw(self):
        canvas = RecordingCanvas()
        previous = _draw_clock_frame(canvas)
        canvas.calls.clear()

        _draw_dashboard_frame(
            canvas,
            {"cpu": 20, "gpu": 30, "ram": 40, "rx": 1.0, "tx": 0.5},
            previous,
        )

        self.assertIn("fill", [call[0] for call in canvas.calls])

    def test_dashboard_date_change_updates_only_date_row(self):
        canvas = RecordingCanvas()
        metrics = {"cpu": 20, "gpu": 30, "ram": 40, "rx": 1.0, "tx": 0.5}
        previous = _draw_dashboard_frame(canvas, metrics)
        canvas.calls.clear()

        _draw_dashboard_frame(
            canvas, metrics, previous, weekday_short="周日", date_text="11/01"
        )

        self.assertNotIn("fill", [call[0] for call in canvas.calls])
        self.assertTrue(canvas.calls)
        for call in canvas.calls:
            if call[0] == "rect":
                self.assertGreaterEqual(call[2], 30)
                self.assertLess(call[2] + call[4], 45)
            elif call[0] == "text":
                self.assertEqual(call[2], 30)

    def test_dashboard_wifi_change_updates_only_header(self):
        canvas = RecordingCanvas()
        metrics = {"cpu": 20, "gpu": 30, "ram": 40, "rx": 1.0, "tx": 0.5}
        previous = _draw_dashboard_frame(canvas, metrics)
        canvas.calls.clear()

        _draw_dashboard_frame(canvas, metrics, previous, wifi_ok=False)

        self.assertNotIn("fill", [call[0] for call in canvas.calls])
        self.assertTrue(canvas.calls)
        for call in canvas.calls:
            if call[0] == "rect":
                self.assertLess(call[2] + call[4], 15)
        icons = [call for call in canvas.calls if call[0] == "bitmap"]
        self.assertEqual(len(icons), 2)
        self.assertTrue(all(call[2] == 0 for call in icons))
        self.assertEqual(icons[1][6], ICON_INACTIVE)

    def test_dashboard_rate_change_updates_only_its_row(self):
        for key, row_y in (("rx", 130), ("tx", 145)):
            with self.subTest(key=key):
                canvas = RecordingCanvas()
                metrics = {"cpu": 20, "gpu": 30, "ram": 40, "rx": 1.0, "tx": 0.5}
                previous = _draw_dashboard_frame(canvas, metrics)
                canvas.calls.clear()

                _draw_dashboard_frame(canvas, dict(metrics, **{key: 2.0}), previous)

                self.assertNotIn("fill", [call[0] for call in canvas.calls])
                self.assertTrue(canvas.calls)
                for call in canvas.calls:
                    if call[0] == "rect":
                        self.assertGreaterEqual(call[2], row_y)
                        self.assertLess(call[2] + call[4], row_y + 15)
                    elif call[0] == "text":
                        self.assertEqual(call[2], row_y)

    def test_clock_sync_change_does_not_draw_removed_footer(self):
        canvas = RecordingCanvas()
        previous = _draw_clock_frame(canvas)
        canvas.calls.clear()

        _draw_clock_frame(canvas, previous=previous, time_synced=False)

        self.assertNotIn("fill", [call[0] for call in canvas.calls])
        self.assertEqual(canvas.calls, [])

    def test_invalid_to_valid_clock_repaints_content_without_header(self):
        canvas = RecordingCanvas()
        previous = _draw_clock_frame(canvas, time_text=None)
        canvas.calls.clear()

        _draw_clock_frame(canvas, "15:28:42", previous)

        self.assertNotIn("fill", [call[0] for call in canvas.calls])
        self.assertTrue(canvas.calls)
        self.assertFalse(any(call[0] == "bitmap" for call in canvas.calls))

    def test_unchanged_clock_frame_performs_no_writes(self):
        canvas = RecordingCanvas()
        previous = _draw_clock_frame(canvas)
        canvas.calls.clear()

        current = _draw_clock_frame(canvas, previous=previous)

        self.assertEqual(current, previous)
        self.assertEqual(canvas.calls, [])

    def test_clock_lunar_change_updates_only_lunar_row(self):
        canvas = RecordingCanvas()
        previous = _draw_clock_frame(canvas)
        canvas.calls.clear()

        _draw_clock_frame(canvas, previous=previous, lunar_text="丙午年九月廿三")

        self.assertNotIn("fill", [call[0] for call in canvas.calls])
        self.assertTrue(canvas.calls)
        for call in canvas.calls:
            if call[0] == "rect":
                self.assertGreaterEqual(call[2], 115)
                self.assertLess(call[2] + call[4], 130)
            elif call[0] == "text":
                self.assertEqual(call[2], 115)

    def test_clock_config_change_updates_only_config_and_overlapping_time(self):
        canvas = RecordingCanvas()
        previous = _draw_clock_frame(canvas)
        canvas.calls.clear()

        _draw_clock_frame(canvas, previous=previous, needs_config=True)

        self.assertNotIn("fill", [call[0] for call in canvas.calls])
        self.assertIn("WIFI CONFIG", [call[3] for call in canvas.calls if call[0] == "text"])
        for call in canvas.calls:
            if call[0] == "rect":
                self.assertTrue(14 <= call[2] < 32)
            elif call[0] == "text":
                self.assertEqual(call[2], 15)


if __name__ == "__main__":
    unittest.main()
