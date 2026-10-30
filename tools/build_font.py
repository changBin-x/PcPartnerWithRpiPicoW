"""Build compact anti-aliased glyph tables for the Pico ST7735S display."""

import argparse
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont


CHINESE_CHARACTERS = "".join(
    sorted(
        set(
            "星期周年月日"
            "时间同步中未断连历暂不可用"
            "初一十廿三"
            "正二三四五六七八九冬腊月闰"
            "甲乙丙丁戊己庚辛壬癸"
            "子丑寅卯辰巳午未申酉戌亥"
        )
    )
)
ASCII_CHARACTERS = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789 %:./-+_?"
CLOCK_CHARACTERS = "0123456789"
COVERAGE_LEVELS = 16
RENDER_SCALE = 4
CHINESE_WIDTH = 14
CHINESE_HEIGHT = 14
ASCII_WIDTH = 8
ASCII_HEIGHT = 14
LINE_BASELINE = 11
CLOCK_DIGIT_WIDTH = 14
CLOCK_DIGIT_HEIGHT = 26
CLOCK_BASELINE = 21


def pack_coverage(values):
    """Pack 4-bit pixel coverage into two pixels per byte, high nibble first."""
    values = list(values)
    packed = bytearray((len(values) + 1) // 2)
    for index, value in enumerate(values):
        if not isinstance(value, int) or value < 0 or value >= COVERAGE_LEVELS:
            raise ValueError("coverage must be an integer from 0 to 15")
        offset = index >> 1
        if index & 1:
            packed[offset] |= value
        else:
            packed[offset] = value << 4
    return bytes(packed)


def unpack_coverage(packed, count):
    """Unpack exactly count 4-bit coverage values."""
    if count < 0 or count > len(packed) * 2:
        raise ValueError("coverage count exceeds packed data")
    values = []
    for index in range(count):
        byte = packed[index >> 1]
        values.append((byte & 15) if index & 1 else (byte >> 4))
    return values


def _font(path, size):
    try:
        return ImageFont.truetype(str(path), size=size)
    except OSError as exc:
        raise ValueError("无法读取字体文件: %s" % path) from exc


def _font_signature(font, character):
    return bytes(font.getmask(character))


def _render_cell(font, character, width, height, baseline):
    high_width = width * RENDER_SCALE
    high_height = height * RENDER_SCALE
    left, top, right, bottom = font.getbbox(character, anchor="ls")
    glyph_width = max(1, right - left)
    x = (high_width - glyph_width) // 2 - left
    baseline_y = baseline * RENDER_SCALE
    source = Image.new("L", (high_width, high_height), 0)
    ImageDraw.Draw(source).text(
        (x, baseline_y), character, fill=255, font=font, anchor="ls"
    )
    return source.resize((width, height), Image.Resampling.LANCZOS)


def _pack_glyph(font, character, width, height, baseline):
    image = _render_cell(font, character, width, height, baseline)
    coverage = ((pixel * 15 + 127) // 255 for pixel in image.tobytes())
    packed = pack_coverage(coverage)
    expected_length = (width * height + 1) // 2
    if len(packed) != expected_length:
        raise ValueError("字形覆盖数据长度错误: %s" % character)
    return packed


def _verify_glyphs(glyphs, required, width, height, label):
    missing = sorted(set(required) - set(glyphs))
    if missing:
        raise ValueError("%s缺少字符: %s" % (label, "、".join(missing)))
    expected_length = (width * height + 1) // 2
    invalid = [character for character in required if len(glyphs[character]) != expected_length]
    if invalid:
        raise ValueError("%s字形长度错误: %s" % (label, "、".join(invalid)))
    for character in required:
        values = unpack_coverage(glyphs[character], width * height)
        if not any(values) and not character.isspace():
            raise ValueError("%s字形为空: %s" % (label, character))


def verify_font_set(chinese_glyphs, ascii_glyphs, clock_glyphs):
    """Check required character coverage and packed glyph dimensions."""
    _verify_glyphs(
        chinese_glyphs, CHINESE_CHARACTERS, CHINESE_WIDTH, CHINESE_HEIGHT, "中文"
    )
    _verify_glyphs(ascii_glyphs, ASCII_CHARACTERS, ASCII_WIDTH, ASCII_HEIGHT, "ASCII")
    _verify_glyphs(clock_glyphs, CLOCK_CHARACTERS, CLOCK_DIGIT_WIDTH, CLOCK_DIGIT_HEIGHT, "时钟数字")


def _build_group(font, characters, width, height, baseline, label):
    missing_signature = _font_signature(font, "\U0010ffff")
    result = {}
    missing = []
    for character in characters:
        if _font_signature(font, character) == missing_signature:
            missing.append(character)
            continue
        result[character] = _pack_glyph(font, character, width, height, baseline)
    if missing:
        raise ValueError("%s字体缺少字形: %s" % (label, "、".join(missing)))
    return result


def _write_mapping(output, name, glyphs):
    output.write("%s = {\n" % name)
    for character, data in sorted(glyphs.items()):
        output.write("    %r: %r,\n" % (character, data))
    output.write("}\n")


def build_font(chinese_font_path, ascii_font_path, size, output_path):
    if size != CHINESE_HEIGHT:
        raise ValueError("正文中文字框必须为 14 像素高")
    chinese_font = _font(chinese_font_path, 46)
    ascii_font = _font(ascii_font_path, 46)
    clock_font = _font(ascii_font_path, 104)
    chinese_glyphs = _build_group(
        chinese_font, CHINESE_CHARACTERS, CHINESE_WIDTH, CHINESE_HEIGHT,
        LINE_BASELINE, "中文"
    )
    ascii_glyphs = _build_group(
        ascii_font, ASCII_CHARACTERS, ASCII_WIDTH, ASCII_HEIGHT,
        LINE_BASELINE, "ASCII"
    )
    clock_glyphs = _build_group(
        clock_font, CLOCK_CHARACTERS, CLOCK_DIGIT_WIDTH, CLOCK_DIGIT_HEIGHT,
        CLOCK_BASELINE, "时钟数字"
    )
    verify_font_set(chinese_glyphs, ascii_glyphs, clock_glyphs)

    output_path = Path(output_path)
    output_path.parent.mkdir(parents=True, exist_ok=True)
    with output_path.open("w", encoding="utf-8", newline="\n") as output:
        output.write(
            '"""Generated 4-bit anti-aliased glyphs; '
            'regenerate with tools/build_font.py."""\n'
        )
        output.write("COVERAGE_BITS = 4\n")
        output.write("CHINESE_WIDTH = 14\nCHINESE_HEIGHT = 14\n")
        output.write("ASCII_WIDTH = 8\nASCII_HEIGHT = 14\n")
        output.write("LINE_BASELINE = 11\n")
        output.write("CLOCK_DIGIT_WIDTH = 14\nCLOCK_DIGIT_HEIGHT = 26\n")
        output.write("CLOCK_BASELINE = 21\n")
        _write_mapping(output, "FONT_CN", chinese_glyphs)
        _write_mapping(output, "ASCII_FONT", ascii_glyphs)
        _write_mapping(output, "CLOCK_FONT", clock_glyphs)
    return len(chinese_glyphs), len(ascii_glyphs), len(clock_glyphs)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--font", required=True, help="CJK TrueType/OpenType font file")
    parser.add_argument("--ascii-font", required=True, help="Latin TrueType/OpenType font file")
    parser.add_argument("--size", type=int, default=14, help="CJK glyph cell height (must be 14)")
    parser.add_argument("--output", default="pico/font_cn.py", help="output MicroPython module")
    args = parser.parse_args(argv)
    try:
        chinese_count, ascii_count, clock_count = build_font(
            args.font, args.ascii_font, args.size, args.output
        )
    except (OSError, ValueError) as exc:
        parser.error(str(exc))
    print(
        "Generated %d CJK, %d ASCII and %d clock glyphs (4-bit coverage): %s"
        % (chinese_count, ascii_count, clock_count, args.output)
    )


if __name__ == "__main__":
    main()
