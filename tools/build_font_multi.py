"""Build the multi-tier 4bpp font asset used by the v2 screen renderer.

The design baseline (`screen_designs_5styles_dual_pages.html`) uses twelve
pixel sizes between 6 px and 26 px.  This tool renders every tier once and
packs the glyphs into a single flash blob:

    header (24 B) + tier records (12 B each) + CJK charset (UTF-8)
    + per-tier ASCII block + per-tier CJK block

All glyph bitmaps are 4bpp coverage, high nibble first, packed row-major.
"""

import argparse
import struct
import sys
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

TIERS = (6, 7, 8, 9, 10, 11, 14, 15, 17, 18, 24, 26)
# 中文只在 6–11 px 出现；14 px 以上全是数字与拉丁字母。
CJK_TIERS = frozenset((6, 7, 8, 9, 10, 11))
ASCII_FIRST = 32
ASCII_LAST = 126
RENDER_SCALE = 4
MAGIC = b"FNT2"
HEADER_FORMAT = "<4sBBHIIII"
HEADER_SIZE = struct.calcsize(HEADER_FORMAT)
TIER_FORMAT = "<BBBBII"
TIER_RECORD_SIZE = struct.calcsize(TIER_FORMAT)

# 设计稿屏内出现的全部汉字（去重 147 字）。
DESIGN_CJK = (
    "一七上下丙东主二云五今令件任休传位体俗信值充兆八公内农况准初制功务动包区十午占卡历双反发口号吉同喂器在城堆境处备天头字存宁完定实对就峰巡市常干年广序应度延异待微心忙态成战扫护抬指接控描摄播支无日时星显晴月朗期术机标栈校核检正步毫氏民气法波测温源满点状狗率环理用监盾看眠督硬秒稳端系线终络统绪网联自节荷行触设象负转较运连迟通道遥里锁门间防阿马"
)


def _load_base_cjk():
    """Reuse the existing weather/city whitelist so live text keeps working."""
    sys.path.insert(0, str(Path(__file__).resolve().parent))
    import build_font  # noqa: PLC0415 - 延迟导入避免与旧脚本耦合

    return build_font.CHINESE_CHARACTERS


def cjk_charset():
    """Return the sorted union of the weather whitelist and the design text."""
    chars = set(_load_base_cjk()) | set(DESIGN_CJK)
    chars.discard(" ")
    return "".join(sorted(chars))


def ascii_advance(px):
    """Return the monospace advance for one ASCII cell at `px` pixels tall."""
    return max(3, (px * 6 + 5) // 10)


def _pack(values):
    values = list(values)
    out = bytearray((len(values) + 1) // 2)
    for index, value in enumerate(values):
        if index & 1:
            out[index >> 1] |= value
        else:
            out[index >> 1] = value << 4
    return bytes(out)


def _render(font, char, width, height, baseline_px):
    """Render one glyph at 4x and downsample it to the target cell."""
    high_w = width * RENDER_SCALE
    high_h = height * RENDER_SCALE
    left, _, right, _ = font.getbbox(char, anchor="ls")
    glyph_w = max(1, right - left)
    x = (high_w - glyph_w) // 2 - left
    image = Image.new("L", (high_w, high_h), 0)
    ImageDraw.Draw(image).text(
        (x, baseline_px * RENDER_SCALE), char, fill=255, font=font, anchor="ls"
    )
    return image.resize((width, height), Image.Resampling.LANCZOS)


def _glyph(font, char, width, height, baseline_px):
    image = _render(font, char, width, height, baseline_px)
    return _pack((pixel * 15 + 127) // 255 for pixel in image.tobytes())


def build(cjk_font_path, ascii_font_path, blob_path):
    """Write the packed asset and return (byte size, CJK glyph count)."""
    charset = cjk_charset()
    charset_bytes = charset.encode("utf-8")
    cjk_fonts = {
        px: ImageFont.truetype(str(cjk_font_path), size=px * RENDER_SCALE)
        for px in sorted(CJK_TIERS)
    }
    latin_fonts = {
        px: ImageFont.truetype(str(ascii_font_path), size=px * RENDER_SCALE)
        for px in TIERS
    }

    ascii_blocks = {}
    for px in TIERS:
        adv = ascii_advance(px)
        baseline = max(1, px - max(1, px // 5))
        block = bytearray()
        for code in range(ASCII_FIRST, ASCII_LAST + 1):
            block += _glyph(latin_fonts[px], chr(code), adv, px, baseline)
        ascii_blocks[px] = (adv, bytes(block))

    cjk_blocks = {}
    for px in sorted(CJK_TIERS):
        baseline = max(1, px - 1)
        block = bytearray()
        for char in charset:
            block += _glyph(cjk_fonts[px], char, px, px, baseline)
        cjk_blocks[px] = bytes(block)

    tiers_off = HEADER_SIZE
    charset_off = tiers_off + TIER_RECORD_SIZE * len(TIERS)
    data_off = (charset_off + len(charset_bytes) + 3) & ~3
    cursor = data_off
    records = []
    for px in TIERS:
        adv, ascii_data = ascii_blocks[px]
        cjk_data = cjk_blocks.get(px, b"")
        records.append((px, px, adv, cursor, cursor + len(ascii_data)))
        cursor += len(ascii_data) + len(cjk_data)

    blob = bytearray()
    blob += struct.pack(
        HEADER_FORMAT, MAGIC, 1, len(TIERS), len(charset),
        tiers_off, charset_off, data_off, cursor,
    )
    for px, cjk_adv, ascii_adv, ascii_off, cjk_off in records:
        blob += struct.pack(TIER_FORMAT, px, cjk_adv, ascii_adv, 0, cjk_off, ascii_off)
    assert len(blob) == charset_off, (len(blob), charset_off)
    blob += charset_bytes
    blob += b"\x00" * (data_off - len(blob))
    for px in TIERS:
        blob += ascii_blocks[px][1]
        blob += cjk_blocks.get(px, b"")
    assert len(blob) == cursor, (len(blob), cursor)

    Path(blob_path).parent.mkdir(parents=True, exist_ok=True)
    Path(blob_path).write_bytes(blob)
    return len(blob), len(charset)


def dump(blob_path, char, px):
    """Print one glyph as ASCII art so a human can judge legibility."""
    data = Path(blob_path).read_bytes()
    magic, version, tiers, cjk_count, tiers_off, charset_off, _, _ = struct.unpack(
        HEADER_FORMAT, data[:HEADER_SIZE]
    )
    if magic != MAGIC or version != 1:
        raise SystemExit("bad asset")
    charset = data[charset_off:].split(b"\x00", 1)[0]
    charset = charset.decode("utf-8")[:cjk_count]
    index = charset.index(char)
    for offset in range(tiers):
        base = tiers_off + offset * TIER_RECORD_SIZE
        tier_px, cjk_adv, _, _, cjk_off, _ = struct.unpack(
            TIER_FORMAT, data[base:base + TIER_RECORD_SIZE]
        )
        if tier_px != px:
            continue
        cell = (cjk_adv * tier_px + 1) // 2
        glyph = data[cjk_off + index * cell:cjk_off + (index + 1) * cell]
        for row in range(tier_px):
            line = ""
            for col in range(cjk_adv):
                bit = row * cjk_adv + col
                byte = glyph[bit >> 1]
                value = (byte & 15) if bit & 1 else (byte >> 4)
                line += " .:-=+*#%@"[min(9, value * 10 // 16)]
            print(line)
        return
    raise SystemExit("tier not found: %d" % px)


def ascii_dump(blob_path, char, px):
    """Print one ASCII glyph as ASCII art."""
    data = Path(blob_path).read_bytes()
    magic, version, tiers, cjk_count, tiers_off, charset_off, _, _ = struct.unpack(
        HEADER_FORMAT, data[:HEADER_SIZE]
    )
    if magic != MAGIC or version != 1:
        raise SystemExit("bad asset")
    for offset in range(tiers):
        base = tiers_off + offset * TIER_RECORD_SIZE
        tier_px, _, adv, _, _, ascii_off = struct.unpack(
            TIER_FORMAT, data[base:base + TIER_RECORD_SIZE]
        )
        if tier_px != px:
            continue
        cell = (adv * tier_px + 1) // 2
        index = ord(char) - ASCII_FIRST
        glyph = data[ascii_off + index * cell:ascii_off + (index + 1) * cell]
        for row in range(tier_px):
            line = ""
            for col in range(adv):
                bit = row * adv + col
                byte = glyph[bit >> 1]
                value = (byte & 15) if bit & 1 else (byte >> 4)
                line += " .:-=+*#%@"[min(9, value * 10 // 16)]
            print(line)
        return
    raise SystemExit("tier not found: %d" % px)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cjk-font", required=True)
    parser.add_argument("--ascii-font", required=True)
    parser.add_argument("--output", required=True)
    parser.add_argument("--dump", help="CJK character to dump")
    parser.add_argument("--dump-ascii", help="ASCII character to dump")
    parser.add_argument("--dump-px", type=int, default=8)
    args = parser.parse_args(argv)
    if args.dump:
        dump(args.output, args.dump, args.dump_px)
        return
    if args.dump_ascii:
        ascii_dump(args.output, args.dump_ascii, args.dump_px)
        return
    size, count = build(args.cjk_font, args.ascii_font, args.output)
    print("wrote %d bytes, %d CJK glyphs, %d tiers" % (size, count, len(TIERS)))


if __name__ == "__main__":
    main()
