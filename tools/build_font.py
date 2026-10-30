"""Build the small Chinese and ASCII bitmap fonts used by the Pico display."""

import argparse
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont


CHINESE_CHARACTERS = "".join(
    sorted(
        set(
            "星期周年月日"
            "时间同步中未断连历暂可用"
            "初十廿三"
            "正二三四五六七八九冬腊月闰"
            "甲乙丙丁戊己庚辛壬癸"
            "子丑寅卯辰巳午未申酉戌亥"
        )
    )
)
ASCII_CHARACTERS = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789 %:./-+_?"
CHINESE_WIDTH = 12
CHINESE_HEIGHT = 12
ASCII_WIDTH = 5
ASCII_HEIGHT = 7


def _font(path, size):
    try:
        return ImageFont.truetype(str(path), size=size)
    except OSError as exc:
        raise ValueError("无法读取字体文件: %s" % path) from exc


def _render_character(font, character, width, height):
    bounds = font.getbbox(character)
    left, top, right, bottom = bounds
    source_width = max(1, right - left)
    source_height = max(1, bottom - top)
    source = Image.new("L", (source_width, source_height), 0)
    ImageDraw.Draw(source).text((-left, -top), character, fill=255, font=font)
    return source.resize((width, height), Image.Resampling.LANCZOS)


def _pack_chinese(font, character, missing_image):
    image = _render_character(font, character, CHINESE_WIDTH, CHINESE_HEIGHT)
    if image.tobytes() == missing_image.tobytes():
        return None

    bitmap = bytearray(18)
    for y in range(CHINESE_HEIGHT):
        for x in range(CHINESE_WIDTH):
            if image.getpixel((x, y)) >= 96:
                bit_index = y * CHINESE_WIDTH + x
                bitmap[bit_index >> 3] |= 1 << (7 - (bit_index & 7))
    if len(bitmap) != 18:
        raise ValueError("中文点阵长度不是 18 字节: %s" % character)
    return bytes(bitmap)


def _pack_ascii(font, character):
    image = _render_character(font, character, ASCII_WIDTH, ASCII_HEIGHT)
    rows = []
    for y in range(ASCII_HEIGHT):
        bits = 0
        for x in range(ASCII_WIDTH):
            if image.getpixel((x, y)) >= 96:
                bits |= 1 << (ASCII_WIDTH - 1 - x)
        rows.append(bits)
    return tuple(rows)


def build_font(font_path, size, output_path):
    if size != CHINESE_WIDTH:
        raise ValueError("中文字体大小必须为 12，以生成 12×12 点阵")
    font_path = Path(font_path)
    output_path = Path(output_path)
    chinese_font = _font(font_path, CHINESE_HEIGHT)
    ascii_font = _font(font_path, 10)
    missing_image = _render_character(chinese_font, "\u10ffff", CHINESE_WIDTH, CHINESE_HEIGHT)

    chinese_bitmaps = {}
    missing_characters = []
    for character in CHINESE_CHARACTERS:
        bitmap = _pack_chinese(chinese_font, character, missing_image)
        if bitmap is None:
            missing_characters.append(character)
        else:
            chinese_bitmaps[character] = bitmap
    if missing_characters:
        raise ValueError("字体缺少字形: " + "、".join(missing_characters))
    ascii_bitmaps = {
        character: _pack_ascii(ascii_font, character)
        for character in ASCII_CHARACTERS
    }

    output_path.parent.mkdir(parents=True, exist_ok=True)
    with output_path.open("w", encoding="utf-8", newline="\n") as output:
        output.write('"""Generated bitmap glyphs. Regenerate with tools/build_font.py."""\n')
        output.write("CHINESE_WIDTH = 12\nCHINESE_HEIGHT = 12\n")
        output.write("ASCII_WIDTH = 5\nASCII_HEIGHT = 7\n")
        output.write("FONT_CN = {\n")
        for character, bitmap in sorted(chinese_bitmaps.items()):
            output.write("    %r: %r,\n" % (character, bitmap))
        output.write("}\nASCII_FONT = {\n")
        for character, rows in sorted(ascii_bitmaps.items()):
            output.write("    %r: %r,\n" % (character, rows))
        output.write("}\n")
    return len(chinese_bitmaps), len(ascii_bitmaps)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--font", required=True, help="TrueType/OpenType font file")
    parser.add_argument("--size", type=int, default=12, help="Chinese glyph cell size (must be 12)")
    parser.add_argument("--output", default="pico/font_cn.py", help="output MicroPython module")
    args = parser.parse_args(argv)
    try:
        chinese_count, ascii_count = build_font(args.font, args.size, args.output)
    except (OSError, ValueError) as exc:
        parser.error(str(exc))
    print(
        "Generated %d Chinese (12x12, 18 bytes each) and %d ASCII (5x7) glyphs: %s"
        % (chinese_count, ascii_count, args.output)
    )


if __name__ == "__main__":
    main()
