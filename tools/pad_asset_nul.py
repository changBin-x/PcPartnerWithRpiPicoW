"""为固件内嵌的只读资源复制一份并在末尾追加 NUL 字节，并生成对应汇编包装。

objcopy 会生成 EABI 0 的目标文件，在 ARM GCC 12 下与 EABI 5 目标链接时产生
版本冲突。使用带 .incbin 的汇编包装由 arm-none-eabi-gcc 编译为与工程完全一致的
EABI 5 目标，符号名格式与 objcopy 完全一致（_binary_<name>_start / end）。
"""

import os
import re
import sys


def main(argv):
    if len(argv) < 3:
        print("用法: pad_asset_nul.py <源文件> <目标文件> [汇编输出文件]", file=sys.stderr)
        return 2
    source, destination = argv[1], argv[2]
    with open(source, "rb") as handle:
        content = handle.read()
    with open(destination, "wb") as handle:
        handle.write(content)
        handle.write(b"\x00")
    print(f"OK: {destination} {len(content)} 字节 + NUL")

    if len(argv) >= 4:
        asm_output = argv[3]
        base = os.path.basename(destination)
        sym = re.sub(r"[^a-zA-Z0-9]", "_", base)
        inc_path = destination.replace("\\", "/")
        asm_content = (
            "    .syntax unified\n"
            "    .cpu cortex-m0plus\n"
            "    .thumb\n"
            '    .section .rodata, "a"\n'
            f"    .global _binary_{sym}_start\n"
            f"    .global _binary_{sym}_end\n"
            "    .align 4\n"
            f"_binary_{sym}_start:\n"
            f'    .incbin "{inc_path}"\n'
            f"_binary_{sym}_end:\n"
        )
        with open(asm_output, "w", encoding="utf-8") as f:
            f.write(asm_content)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
