"""把 PPM(P6) 图片转换成 PNG，便于在编辑器或对话里直接查看。

渲染相关的单元测试用 PPM 输出画面，因为它不需要任何第三方库、代码只有几行；
但多数查看器与对话界面只认 PNG，于是用本脚本做一次无损转换。

用法：

    python3 tools/ppm_to_png.py 输入.ppm 输出.png
"""

import struct
import sys
import zlib


def read_ppm(path):
    """读取 P6 格式的 PPM，返回 (宽, 高, RGB 字节串)。"""
    with open(path, "rb") as handle:
        data = handle.read()
    if not data.startswith(b"P6"):
        raise SystemExit(f"{path} 不是 P6 格式的 PPM")
    fields = []
    index = 2
    while len(fields) < 3:
        while index < len(data) and data[index : index + 1].isspace():
            index += 1
        if index < len(data) and data[index : index + 1] == b"#":
            while index < len(data) and data[index : index + 1] != b"\n":
                index += 1
            continue
        start = index
        while index < len(data) and not data[index : index + 1].isspace():
            index += 1
        fields.append(int(data[start:index]))
    index += 1  # 跳过 maxval 后的单个空白字符
    width, height, maxval = fields
    if maxval != 255:
        raise SystemExit(f"只支持 maxval=255，实际 {maxval}")
    pixels = data[index : index + width * height * 3]
    if len(pixels) != width * height * 3:
        raise SystemExit("PPM 像素数据长度不足")
    return width, height, pixels


def write_png(path, width, height, rgb):
    """写 8 位 RGB 的 PNG。"""
    raw = bytearray()
    stride = width * 3
    for row in range(height):
        raw.append(0)  # 每行的过滤器类型：None
        start = row * stride
        raw.extend(rgb[start : start + stride])

    def chunk(tag, payload):
        head = struct.pack(">I", len(payload))
        body = tag + payload
        return head + body + struct.pack(">I", zlib.crc32(body) & 0xFFFFFFFF)

    header = struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)
    with open(path, "wb") as handle:
        handle.write(b"\x89PNG\r\n\x1a\n")
        handle.write(chunk(b"IHDR", header))
        handle.write(chunk(b"IDAT", zlib.compress(bytes(raw), 9)))
        handle.write(chunk(b"IEND", b""))


def main(argv):
    if len(argv) != 3:
        print("用法: ppm_to_png.py <输入.ppm> <输出.png>", file=sys.stderr)
        return 2
    width, height, rgb = read_ppm(argv[1])
    write_png(argv[2], width, height, rgb)
    print(f"OK: {argv[2]} {width}x{height}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
