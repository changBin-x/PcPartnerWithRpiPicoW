#!/usr/bin/env python3
"""把 RP2040 的可执行 ELF 转换成 UF2，用于 BOOTSEL 模式拖拽烧录。

本机没有安装 picotool（Ubuntu 仓库里也没有这个包），pico-sdk 2.x 又删掉了
自带的 elf2uf2，导致 `pico_add_extra_outputs()` 只能产出 .bin/.hex/.elf。
UF2 的容器格式是公开且固定的，这里用标准库实现一个最小可用转换器。

只处理位于 Flash 地址空间的 PT_LOAD 段（RP2040 Flash 起始 0x10000000，
容量 2 MiB）。段按物理地址（p_paddr，即加载地址）排序后合并相邻区间，
再切成 256 字节的 UF2 数据块。段内 `.data` 的初始值本来就存放在 Flash 里，
因此整份镜像是一个连续的 Flash 布局。
"""

from __future__ import annotations

import argparse
import struct
import sys
from pathlib import Path

UF2_MAGIC_START0 = 0x0A324655
UF2_MAGIC_START1 = 0x9E5D5157
UF2_MAGIC_END = 0x0AB16F30
UF2_FLAG_FAMILY_ID_PRESENT = 0x00002000
RP2040_FAMILY_ID = 0xE48BFF56
UF2_PAYLOAD_SIZE = 256
UF2_BLOCK_SIZE = 512
UF2_DATA_OFFSET = 32
UF2_DATA_CAPACITY = 476

FLASH_BASE = 0x10000000
FLASH_SIZE = 2 * 1024 * 1024

PT_LOAD = 1
ELF_PROGRAM_HEADER_FORMAT = "<IIIIIIII"


def _read_segments(elf_bytes: bytes) -> list[tuple[int, bytes]]:
    """读出 ELF 中所有位于 Flash 的 PT_LOAD 段。

    Args:
        elf_bytes: 整个 ELF 文件的字节内容。

    Returns:
        (加载地址, 段数据) 的列表，已按地址升序排列。

    Raises:
        ValueError: 文件不是 ELF32 小端可执行文件，或段地址越界。
    """
    if len(elf_bytes) < 52 or elf_bytes[:4] != b"\x7fELF":
        raise ValueError("不是 ELF 文件")
    if elf_bytes[4] != 1 or elf_bytes[5] != 1:
        raise ValueError("只支持 ELF32 小端序")

    phoff = struct.unpack_from("<I", elf_bytes, 28)[0]
    phentsize = struct.unpack_from("<H", elf_bytes, 42)[0]
    phnum = struct.unpack_from("<H", elf_bytes, 44)[0]

    segments: list[tuple[int, bytes]] = []
    for index in range(phnum):
        offset = phoff + index * phentsize
        fields = struct.unpack_from(
            ELF_PROGRAM_HEADER_FORMAT, elf_bytes, offset
        )
        p_type, p_offset, _p_vaddr, p_paddr, p_filesz = fields[:5]
        if p_type != PT_LOAD or p_filesz == 0:
            continue
        if not FLASH_BASE <= p_paddr < FLASH_BASE + FLASH_SIZE:
            raise ValueError(
                f"段 {index} 的加载地址 0x{p_paddr:08x} 不在 Flash 空间内，"
                "本工具只生成 Flash 下载镜像"
            )
        if p_paddr + p_filesz > FLASH_BASE + FLASH_SIZE:
            raise ValueError(f"段 {index} 超出 Flash 容量")
        segments.append((p_paddr, elf_bytes[p_offset : p_offset + p_filesz]))

    if not segments:
        raise ValueError("ELF 中没有位于 Flash 的 PT_LOAD 段")

    segments.sort(key=lambda item: item[0])
    return segments


def _merge_contiguous(
    segments: list[tuple[int, bytes]],
) -> list[tuple[int, bytes]]:
    """把地址首尾相接的段合并成一整块，避免产生碎片化的 UF2 目标地址。"""
    merged: list[tuple[int, bytes]] = []
    for address, data in segments:
        if merged and merged[-1][0] + len(merged[-1][1]) == address:
            previous_address, previous_data = merged[-1]
            merged[-1] = (previous_address, previous_data + data)
        else:
            merged.append((address, data))
    return merged


def _build_blocks(segments: list[tuple[int, bytes]]) -> list[tuple[int, bytes]]:
    """把各段切成 256 字节的 UF2 数据块，末块用零填充。"""
    blocks: list[tuple[int, bytes]] = []
    for address, data in segments:
        for start in range(0, len(data), UF2_PAYLOAD_SIZE):
            chunk = data[start : start + UF2_PAYLOAD_SIZE]
            blocks.append((address + start, chunk))
    return blocks


def encode_uf2(elf_bytes: bytes) -> bytes:
    """把 ELF 字节编码为 UF2 字节。

    Args:
        elf_bytes: 整个 ELF 文件的字节内容。

    Returns:
        UF2 文件内容。

    Raises:
        ValueError: ELF 无法解析或没有可下载的 Flash 段。
    """
    blocks = _build_blocks(_merge_contiguous(_read_segments(elf_bytes)))
    total = len(blocks)
    output = bytearray()
    for index, (address, chunk) in enumerate(blocks):
        payload = chunk + bytes(UF2_DATA_CAPACITY - len(chunk))
        header = struct.pack(
            "<8I",
            UF2_MAGIC_START0,
            UF2_MAGIC_START1,
            UF2_FLAG_FAMILY_ID_PRESENT,
            address,
            UF2_PAYLOAD_SIZE,
            index,
            total,
            RP2040_FAMILY_ID,
        )
        output += header + payload + struct.pack("<I", UF2_MAGIC_END)
    if output and len(output) % UF2_BLOCK_SIZE != 0:
        raise AssertionError("UF2 块长度异常")
    return bytes(output)


def decode_uf2(uf2_bytes: bytes) -> list[tuple[int, bytes]]:
    """反向解析 UF2，用于自校验。

    Args:
        uf2_bytes: UF2 文件内容。

    Returns:
        (目标地址, 有效载荷) 的列表。

    Raises:
        ValueError: 魔数、块序号或块大小不符合 UF2 规范。
    """
    if not uf2_bytes or len(uf2_bytes) % UF2_BLOCK_SIZE != 0:
        raise ValueError("UF2 长度不是 512 的整数倍")
    total_blocks = len(uf2_bytes) // UF2_BLOCK_SIZE
    blocks: list[tuple[int, bytes]] = []
    for index in range(total_blocks):
        block = uf2_bytes[index * UF2_BLOCK_SIZE : (index + 1) * UF2_BLOCK_SIZE]
        (
            magic0,
            magic1,
            flags,
            address,
            payload_size,
            block_no,
            num_blocks,
            family,
        ) = struct.unpack_from("<8I", block, 0)
        magic_end = struct.unpack_from("<I", block, 508)[0]
        if magic0 != UF2_MAGIC_START0 or magic1 != UF2_MAGIC_START1:
            raise ValueError(f"第 {index} 块起始魔数错误")
        if magic_end != UF2_MAGIC_END:
            raise ValueError(f"第 {index} 块结束魔数错误")
        if flags != UF2_FLAG_FAMILY_ID_PRESENT:
            raise ValueError(f"第 {index} 块 flags 异常")
        if family != RP2040_FAMILY_ID:
            raise ValueError(f"第 {index} 块 family id 不是 RP2040")
        if payload_size != UF2_PAYLOAD_SIZE:
            raise ValueError(f"第 {index} 块载荷长度异常")
        if block_no != index or num_blocks != total_blocks:
            raise ValueError(f"第 {index} 块序号或总块数不一致")
        payload = block[UF2_DATA_OFFSET : UF2_DATA_OFFSET + payload_size]
        blocks.append((address, payload))
    return blocks


def main(argv: list[str] | None = None) -> int:
    """命令行入口：把 ELF 转成 UF2 并做一次往返自校验。"""
    parser = argparse.ArgumentParser(description="RP2040 ELF 转 UF2")
    parser.add_argument("elf", type=Path, help="输入 ELF 文件")
    parser.add_argument("uf2", type=Path, help="输出 UF2 文件")
    args = parser.parse_args(argv)

    elf_bytes = args.elf.read_bytes()
    uf2_bytes = encode_uf2(elf_bytes)
    args.uf2.write_bytes(uf2_bytes)

    # 往返自校验：重新解析并与 ELF 段内容逐字节比对。
    expected = _merge_contiguous(_read_segments(elf_bytes))
    chunks = dict(decode_uf2(uf2_bytes))
    for address, data in expected:
        rebuilt = bytearray()
        for start in range(0, len(data), UF2_PAYLOAD_SIZE):
            chunk = chunks.get(address + start)
            if chunk is None:
                print(
                    f"FAIL: 缺少地址 0x{address + start:08x} 的块",
                    file=sys.stderr,
                )
                return 1
            rebuilt += chunk
        if bytes(rebuilt[: len(data)]) != data:
            print(f"FAIL: 地址 0x{address:08x} 数据往返不一致", file=sys.stderr)
            return 1

    print(
        f"OK: {args.uf2.name} {len(uf2_bytes)} 字节，"
        f"{len(uf2_bytes) // UF2_BLOCK_SIZE} 块，"
        f"起始地址 0x{expected[0][0]:08x}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
