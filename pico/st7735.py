"""Small-area ST7735S driver for a 128x160 Pico W display."""

try:
    from .font_cn import ASCII_FONT, FONT_CN
except ImportError:
    # Also allow direct imports when mpremote mounts the pico directory at /remote.
    from font_cn import ASCII_FONT, FONT_CN


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
        # MX|MY|BGR: portrait orientation and the BGR panel color order.
        self._command(_MADCTL, b"\xc8")
        self._command(_COLMOD, b"\x05")
        self._command(0xE0, b"\x02\x1c\x07\x12\x37\x32\x29\x2d\x29\x25\x2b\x39\x00\x01\x03\x10")
        self._command(0xE1, b"\x03\x1d\x07\x06\x2e\x2c\x29\x2d\x2e\x2e\x37\x3f\x00\x00\x02\x10")
        self._command(_DISPON)
        time.sleep_ms(100)

    def _set_window(self, x, y, width, height):
        x_end = x + width - 1
        y_end = y + height - 1
        self._command(
            _CASET,
            bytes((0, x, 0, x_end)),
        )
        self._command(
            _RASET,
            bytes((0, y, 0, y_end)),
        )
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
        """Fill a clipped rectangle using a single scanline buffer."""
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

    def _draw_ascii(self, x, y, character, color, background, scale):
        rows = ASCII_FONT.get(character, ASCII_FONT["?"])
        cell_width = 6
        cell_height = 8
        pixel_width = cell_width * scale
        pixel_height = cell_height * scale
        pixels = bytearray(pixel_width * pixel_height * 2)
        bg_high, bg_low = _color_bytes(background)
        fg_high, fg_low = _color_bytes(color)
        for py in range(cell_height):
            bits = rows[py] if py < len(rows) else 0
            for px in range(cell_width):
                foreground = px < 5 and bool(bits & (1 << (4 - px)))
                value_high, value_low = (fg_high, fg_low) if foreground else (bg_high, bg_low)
                for dy in range(scale):
                    for dx in range(scale):
                        index = ((py * scale + dy) * pixel_width + px * scale + dx) * 2
                        pixels[index] = value_high
                        pixels[index + 1] = value_low
        self._write_rect(x, y, pixel_width, pixel_height, pixels)
        return pixel_width

    def _draw_chinese(self, x, y, character, color, background):
        bitmap = FONT_CN.get(character)
        if bitmap is None:
            raise ValueError("缺少中文字形: %s" % character)
        width = 12
        height = 12
        bg_high, bg_low = _color_bytes(background)
        fg_high, fg_low = _color_bytes(color)
        pixels = bytearray(width * height * 2)
        for py in range(height):
            for px in range(width):
                bit_index = py * width + px
                foreground = bool(bitmap[bit_index >> 3] & (1 << (7 - (bit_index & 7))))
                index = (py * width + px) * 2
                value_high, value_low = (fg_high, fg_low) if foreground else (bg_high, bg_low)
                pixels[index] = value_high
                pixels[index + 1] = value_low
        self._write_rect(x, y, width, height, pixels)
        return width

    def text(self, x, y, value, color, scale=1, background=0x0000):
        """Draw mixed ASCII/CJK text; each glyph uses at most 384 bytes."""
        if not isinstance(value, str):
            value = str(value)
        for character in value:
            if ord(character) < 128:
                width = 6 * scale
                height = 8 * scale
                if x + width > self.width:
                    break
                if x >= 0 and y >= 0 and y + height <= self.height:
                    self._draw_ascii(x, y, character, color, background, scale)
            else:
                width = 12
                height = 12
                if x + width > self.width:
                    break
                if x >= 0 and y >= 0 and y + height <= self.height:
                    self._draw_chinese(x, y, character, color, background)
            x += width
