"""Small-area anti-aliased ST7735S driver for the 128x160 Pico W display."""

try:
    from .font_cn import (
        ASCII_FONT,
        ASCII_WIDTH,
        CHINESE_HEIGHT,
        CHINESE_WIDTH,
        CLOCK_DIGIT_HEIGHT,
        CLOCK_DIGIT_WIDTH,
        CLOCK_FONT,
        FONT_CN,
        WEATHER_DIGIT_FONT,
        WEATHER_DIGIT_WIDTH,
    )
except ImportError:
    from font_cn import (
        ASCII_FONT,
        ASCII_WIDTH,
        CHINESE_HEIGHT,
        CHINESE_WIDTH,
        CLOCK_DIGIT_HEIGHT,
        CLOCK_DIGIT_WIDTH,
        CLOCK_FONT,
        FONT_CN,
        WEATHER_DIGIT_FONT,
        WEATHER_DIGIT_WIDTH,
    )


WIDTH = 128
HEIGHT = 160
_BLACK = 0x0000
_SWRESET = 0x01
_SLPOUT = 0x11
_DISPON = 0x29
_CASET = 0x2A
_RASET = 0x2B
_RAMWR = 0x2C
_MADCTL = 0x36
_COLMOD = 0x3A


def rgb565(red, green, blue):
    """Convert 8-bit RGB channels into the ST7735 16-bit color format."""
    return ((red & 0xF8) << 8) | ((green & 0xFC) << 3) | (blue >> 3)


def blend_rgb565(foreground, background, coverage):
    """Blend RGB565 colors using a 4-bit coverage value from zero to 15."""
    if coverage <= 0:
        return background
    if coverage >= 15:
        return foreground
    inverse = 15 - coverage
    red = (((foreground >> 11) & 31) * coverage + ((background >> 11) & 31) * inverse + 7) // 15
    green = (((foreground >> 5) & 63) * coverage + ((background >> 5) & 63) * inverse + 7) // 15
    blue = ((foreground & 31) * coverage + (background & 31) * inverse + 7) // 15
    return (red << 11) | (green << 5) | blue


def pack_coverage(values):
    """Pack 4-bit pixel coverage, most-significant nibble first."""
    values = list(values)
    packed = bytearray((len(values) + 1) // 2)
    for index, value in enumerate(values):
        if not isinstance(value, int) or value < 0 or value > 15:
            raise ValueError("coverage must be an integer from 0 to 15")
        offset = index >> 1
        if index & 1:
            packed[offset] |= value
        else:
            packed[offset] = value << 4
    return bytes(packed)


def unpack_coverage(packed, count):
    """Unpack exactly count coverage values from a nibble array."""
    if count < 0 or count > len(packed) * 2:
        raise ValueError("coverage count exceeds packed data")
    values = []
    for index in range(count):
        value = packed[index >> 1]
        values.append((value & 15) if index & 1 else (value >> 4))
    return values


def clock_colon_coverage(width=8, height=26):
    """Return 4x4 supersampled coverage for two symmetric round dots."""
    scale = 4
    centers_y = ((height - 1) / 2.0 - 5.5, (height - 1) / 2.0 + 5.5)
    center_x = (width - 1) / 2.0
    radius_squared = 1.75 * 1.75
    result = []
    for y in range(height):
        for x in range(width):
            samples_inside = 0
            for sy in range(scale):
                sample_y = y + (sy + 0.5) / scale
                for sx in range(scale):
                    sample_x = x + (sx + 0.5) / scale
                    for center_y in centers_y:
                        dx = sample_x - (center_x + 0.5)
                        dy = sample_y - (center_y + 0.5)
                        if dx * dx + dy * dy <= radius_squared:
                            samples_inside += 1
                            break
            result.append((samples_inside * 15 + 8) // 16)
    return tuple(result)


CLOCK_COLON_GLYPH = pack_coverage(clock_colon_coverage())

CHINESE_PLACEHOLDER_GLYPH = (
    b"\x00\x00\x00\x00\x00\x00\x00"
    b"\x0f\xff\xff\xff\xff\xff\xf0"
    b"\x0f\x00\x00\x00\x00\x00\xf0"
    b"\x0f\x00\x00\x00\x00\x00\xf0"
    b"\x0f\x00\x00\x00\x00\x00\xf0"
    b"\x0f\x00\x00\x00\x00\x00\xf0"
    b"\x0f\x00\x00\x00\x00\x00\xf0"
    b"\x0f\x00\x00\x00\x00\x00\xf0"
    b"\x0f\x00\x00\x00\x00\x00\xf0"
    b"\x0f\x00\x00\x00\x00\x00\xf0"
    b"\x0f\x00\x00\x00\x00\x00\xf0"
    b"\x0f\x00\x00\x00\x00\x00\xf0"
    b"\x0f\xff\xff\xff\xff\xff\xf0"
    b"\x00\x00\x00\x00\x00\x00\x00"
)

def _color_bytes(color):
    return (color >> 8) & 0xFF, color & 0xFF


class ST7735:
    """A portrait display that writes bounded regions without a framebuffer."""

    def __init__(self, spi=None, cs=None, dc=None, rst=None):
        from machine import Pin, SPI

        self.width = WIDTH
        self.height = HEIGHT
        self.spi = spi or SPI(
            0,
            baudrate=20_000_000,
            polarity=0,
            phase=0,
            bits=8,
            firstbit=SPI.MSB,
            sck=Pin(18),
            mosi=Pin(19),
        )
        self.cs = cs or Pin(17, Pin.OUT, value=1)
        self.dc = dc or Pin(21, Pin.OUT, value=1)
        self.rst = rst or Pin(20, Pin.OUT, value=1)
        self._init_panel()
        self.fill(_BLACK)

    def _command(self, command, data=None):
        self.cs.value(0)
        self.dc.value(0)
        self.spi.write(bytes((command,)))
        if data:
            self.dc.value(1)
            self.spi.write(data)
        self.cs.value(1)

    def _init_panel(self):
        import time

        self.rst.value(1)
        time.sleep_ms(5)
        self.rst.value(0)
        time.sleep_ms(20)
        self.rst.value(1)
        time.sleep_ms(120)

        self._command(_SWRESET)
        time.sleep_ms(150)
        self._command(_SLPOUT)
        time.sleep_ms(120)
        self._command(0xB1, b"\x01\x2c\x2d")
        self._command(0xB2, b"\x01\x2c\x2d")
        self._command(0xB3, b"\x01\x2c\x2d\x01\x2c\x2d")
        self._command(0xB4, b"\x07")
        self._command(0xC0, b"\xa2\x02\x84")
        self._command(0xC1, b"\xc5")
        self._command(0xC2, b"\x0a\x00")
        self._command(0xC3, b"\x8a\x2a")
        self._command(0xC4, b"\x8a\xee")
        self._command(0xC5, b"\x0e")
        self._command(0x20)
        self._command(_MADCTL, b"\xc8")
        self._command(_COLMOD, b"\x05")
        self._command(0xE0, b"\x02\x1c\x07\x12\x37\x32\x29\x2d\x29\x25\x2b\x39\x00\x01\x03\x10")
        self._command(0xE1, b"\x03\x1d\x07\x06\x2e\x2c\x29\x2d\x2e\x2e\x37\x3f\x00\x00\x02\x10")
        self._command(_DISPON)
        time.sleep_ms(100)

    def _set_window(self, x, y, width, height):
        x_end = x + width - 1
        y_end = y + height - 1
        self._command(_CASET, bytes((0, x, 0, x_end)))
        self._command(_RASET, bytes((0, y, 0, y_end)))
        self._command(_RAMWR)

    def _write_rect(self, x, y, width, height, pixels):
        if width <= 0 or height <= 0:
            return
        self._set_window(x, y, width, height)
        self.cs.value(0)
        self.dc.value(1)
        try:
            self.spi.write(pixels)
        finally:
            self.cs.value(1)

    def fill(self, color):
        self.fill_rect(0, 0, self.width, self.height, color)

    def fill_rect(self, x, y, width, height, color):
        """Fill a clipped rectangle using one RGB565 scanline buffer."""
        x_end = min(self.width, x + width)
        y_end = min(self.height, y + height)
        x = max(0, x)
        y = max(0, y)
        width = x_end - x
        height = y_end - y
        if width <= 0 or height <= 0:
            return
        high, low = _color_bytes(color)
        row = bytearray(width * 2)
        for offset in range(0, len(row), 2):
            row[offset] = high
            row[offset + 1] = low
        self._set_window(x, y, width, height)
        self.cs.value(0)
        self.dc.value(1)
        try:
            for _ in range(height):
                self.spi.write(row)
        finally:
            self.cs.value(1)

    def hline(self, x, y, width, color):
        self.fill_rect(x, y, width, 1, color)

    def _draw_coverage(self, x, y, width, height, packed, color, background):
        if x < 0 or y < 0 or x + width > self.width or y + height > self.height:
            return
        self._draw_glyph_entries(
            x, y, width, height, ((0, width, packed),), color, background
        )

    def _draw_glyph_entries(self, x, y, total_width, height, entries, color, background):
        """Blend glyphs in vertical bands capped at 3 KB, with one SPI window each."""
        if total_width <= 0 or x < 0 or y < 0:
            return
        if x + total_width > self.width or y + height > self.height:
            return
        rows_per_block = max(1, 3072 // (total_width * 2))
        for source_y in range(0, height, rows_per_block):
            block_height = min(rows_per_block, height - source_y)
            self._draw_glyph_band(
                x, y + source_y, total_width, block_height, source_y,
                entries, color, background
            )

    def _draw_glyph_band(
        self, x, y, total_width, height, source_y, entries, color, background
    ):
        pixels = bytearray(total_width * height * 2)
        background_high, background_low = _color_bytes(background)
        for offset in range(0, len(pixels), 2):
            pixels[offset] = background_high
            pixels[offset + 1] = background_low
        blend_table = tuple(
            _color_bytes(blend_rgb565(color, background, coverage))
            for coverage in range(16)
        )
        for glyph_x, glyph_width, packed in entries:
            for row in range(height):
                source_row = (source_y + row) * glyph_width
                destination_row = row * total_width + glyph_x
                for column in range(glyph_width):
                    pixel_index = source_row + column
                    byte = packed[pixel_index >> 1]
                    coverage = (byte & 15) if pixel_index & 1 else (byte >> 4)
                    high, low = blend_table[coverage]
                    offset = (destination_row + column) * 2
                    pixels[offset] = high
                    pixels[offset + 1] = low
        self._write_rect(x, y, total_width, height, pixels)

    def _draw_ascii(self, x, y, character, color, background):
        glyph = ASCII_FONT.get(character)
        if glyph is None:
            glyph = ASCII_FONT["?"]
        self._draw_coverage(x, y, ASCII_WIDTH, ASCII_HEIGHT, glyph, color, background)
        return ASCII_WIDTH

    def _draw_chinese(self, x, y, character, color, background):
        glyph = FONT_CN.get(character)
        if glyph is None:
            glyph = CHINESE_PLACEHOLDER_GLYPH
        self._draw_coverage(x, y, CHINESE_WIDTH, CHINESE_HEIGHT, glyph, color, background)
        return CHINESE_WIDTH

    def text(self, x, y, value, color, background=0x0000):
        """Draw anti-aliased mixed text in one shared-baseline SPI block."""
        if not isinstance(value, str):
            value = str(value)
        entries = []
        line_width = 0
        for character in value:
            if ord(character) < 128:
                width = ASCII_WIDTH
                glyph = ASCII_FONT.get(character, ASCII_FONT["?"])
            else:
                width = CHINESE_WIDTH
                glyph = FONT_CN.get(character)
                if glyph is None:
                    glyph = CHINESE_PLACEHOLDER_GLYPH
            if x + line_width + width > self.width:
                break
            entries.append((line_width, width, glyph))
            line_width += width
        self._draw_glyph_entries(
            x, y, line_width, CHINESE_HEIGHT, entries, color, background
        )

    def weather_temperature(self, x, y, value, color, background=0x0000):
        """Draw a medium-bold temperature number followed by its Celsius unit."""
        entries = []
        line_width = 0
        for character in value:
            if character in WEATHER_DIGIT_FONT:
                width = WEATHER_DIGIT_WIDTH
                glyph = WEATHER_DIGIT_FONT[character]
            elif ord(character) >= 128:
                width = CHINESE_WIDTH
                glyph = FONT_CN.get(character, CHINESE_PLACEHOLDER_GLYPH)
            else:
                raise ValueError("温度字形仅支持数字、负号和摄氏度")
            if x + line_width + width > self.width:
                break
            entries.append((line_width, width, glyph))
            line_width += width
        self._draw_glyph_entries(
            x, y, line_width, CHINESE_HEIGHT, entries, color, background
        )
        return line_width

    def clock_text(self, x, y, value, color, background=0x0000):
        """Draw a large numeric clock with two smooth, symmetric colon dots."""
        entries = []
        advance = 0
        for character in value:
            if character == ":":
                entries.append((advance, 8, CLOCK_COLON_GLYPH))
                advance += 10
            else:
                glyph = CLOCK_FONT.get(character)
                if glyph is None:
                    raise ValueError("时钟字形仅支持数字和冒号")
                entries.append((advance, CLOCK_DIGIT_WIDTH, glyph))
                advance += CLOCK_DIGIT_WIDTH
        self._draw_glyph_entries(
            x, y, advance, CLOCK_DIGIT_HEIGHT, entries, color, background
        )
        return advance
