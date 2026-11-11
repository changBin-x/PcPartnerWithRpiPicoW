"""为固件内嵌的只读资源复制一份并在末尾追加 NUL 字节。

objcopy 把资源文件原样转成 .rodata 段，文件里没有结尾 NUL，于是固件拿到的
缓冲区无法直接当 C 字符串使用（PEM 根证书尤其需要）。本脚本生成一份内容
相同、末尾多一个 0x00 的副本，交由 objcopy 嵌入；asset_store 报告的 size
不包含这个字节，因此调用方始终可以安全访问 data[size]。
"""

import sys


def main(argv):
    if len(argv) != 3:
        print("用法: pad_asset_nul.py <源文件> <目标文件>", file=sys.stderr)
        return 2
    source, destination = argv[1], argv[2]
    with open(source, "rb") as handle:
        content = handle.read()
    with open(destination, "wb") as handle:
        handle.write(content)
        handle.write(b"\x00")
    print(f"OK: {destination} {len(content)} 字节 + NUL")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
