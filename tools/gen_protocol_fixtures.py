"""生成 UDP 指标报文的协议一致性测试数据集。

固件里的 `metrics_parser.c` 是 C 实现，PC 端的 `pc_metrics/protocol.py`
是 Python 实现；两者必须对「哪些报文合法」达成完全一致的判断，否则现场
会出现收不到数据却查不出原因的故障。本脚本用 **真实的 Python 编码器**
造出合法报文，再手写一批应当被拒绝的报文，输出成 C 头文件供
`test_protocol_conformance.c` 逐条断言。

用法：

    python3 tools/gen_protocol_fixtures.py

输出：pico_freertos_firmware/tests/generated/udp_packets_data.h
"""

import json
import pathlib
import sys

REPO_ROOT = pathlib.Path(__file__).resolve().parent.parent
sys.path.insert(0, str(REPO_ROOT))

from pc_metrics.protocol import PACKET_VERSION, encode_packet  # noqa: E402

OUTPUT = (
    REPO_ROOT
    / "pico_freertos_firmware"
    / "tests"
    / "generated"
    / "udp_packets_data.h"
)

# 与 metrics_parser.h 的 MetricsParseResult 枚举顺序严格一致。
PARSE_OK = 0
PARSE_INVALID_JSON = 1
PARSE_MISSING_FIELD = 2
PARSE_INVALID_VERSION = 3
PARSE_INVALID_VALUE = 4
PARSE_STALE_SEQ = 5

HEADER = """\
/* 本文件由 tools/gen_protocol_fixtures.py 生成，请勿手工编辑。
 * 合法报文来自 PC 端真实的 pc_metrics.protocol.encode_packet，
 * 非法报文为手写样本；两侧判断必须完全一致。 */
#ifndef PICO_FREERTOS_FIRMWARE_TESTS_GENERATED_UDP_PACKETS_DATA_H_
#define PICO_FREERTOS_FIRMWARE_TESTS_GENERATED_UDP_PACKETS_DATA_H_

typedef struct {
  const char* name;   /* 用例名称。 */
  int expected;       /* 期望的 MetricsParseResult 取值。 */
  const char* packet; /* 报文字节内容（不含结尾 NUL）。 */
  unsigned int length;
} UdpPacketCase;

static const UdpPacketCase kUdpPacketCases[] = {
"""

FOOTER = """\
};

static const unsigned int kUdpPacketCaseCount =
    sizeof(kUdpPacketCases) / sizeof(kUdpPacketCases[0]);

#endif /* PICO_FREERTOS_FIRMWARE_TESTS_GENERATED_UDP_PACKETS_DATA_H_ */
"""


def c_literal(payload):
    """把字节串转义成 C 字符串字面量。"""
    out = []
    for byte in payload:
        char = chr(byte)
        if char == '"':
            out.append('\\"')
        elif char == "\\":
            out.append("\\\\")
        elif 0x20 <= byte < 0x7F:
            out.append(char)
        else:
            out.append(f"\\x{byte:02x}")
    return "".join(out)


def valid_cases():
    """用真实 Python 编码器构造的合法报文。"""
    return [
        (
            "典型值",
            encode_packet(
                seq=1, sampled_at=1762400000, cpu=42, gpu=15, ram=61,
                rx=1.5, tx=0.25,
            ),
        ),
        (
            "无独显 gpu 为 null",
            encode_packet(
                seq=2, sampled_at=1762400001, cpu=7, gpu=None, ram=33,
                rx=0.0, tx=0.0,
            ),
        ),
        (
            "全部取值为零的边界",
            encode_packet(
                seq=3, sampled_at=1762400002, cpu=0, gpu=0, ram=0,
                rx=0, tx=0,
            ),
        ),
        (
            "百分比上界 100",
            encode_packet(
                seq=4, sampled_at=1762400003, cpu=100, gpu=100, ram=100,
                rx=999.99, tx=1234.5,
            ),
        ),
        (
            "浮点百分比",
            encode_packet(
                seq=5, sampled_at=1762400004, cpu=42.5, gpu=0.0, ram=61.25,
                rx=0.001, tx=0.002,
            ),
        ),
    ]


def invalid_cases():
    """应当被固件解析器拒绝的报文。"""
    return [
        (
            "空报文",
            b"",
            PARSE_INVALID_JSON,
        ),
        (
            "非 JSON 文本",
            b"hello",
            PARSE_INVALID_JSON,
        ),
        (
            "根节点是数组",
            b"[1,2,3]",
            PARSE_INVALID_JSON,
        ),
        (
            "版本号不支持",
            json.dumps(
                {
                    "v": PACKET_VERSION + 1, "seq": 1, "ts": 1762400000,
                    "cpu": 1, "gpu": 1, "ram": 1, "rx": 1, "tx": 1,
                }
            ).encode(),
            PARSE_INVALID_VERSION,
        ),
        (
            "缺少 ram 字段",
            json.dumps(
                {
                    "v": PACKET_VERSION, "seq": 1, "ts": 1762400000,
                    "cpu": 1, "gpu": 1, "rx": 1, "tx": 1,
                }
            ).encode(),
            PARSE_MISSING_FIELD,
        ),
        (
            "cpu 超出 100",
            json.dumps(
                {
                    "v": PACKET_VERSION, "seq": 1, "ts": 1762400000,
                    "cpu": 101, "gpu": 1, "ram": 1, "rx": 1, "tx": 1,
                }
            ).encode(),
            PARSE_INVALID_VALUE,
        ),
        (
            "cpu 为负数",
            json.dumps(
                {
                    "v": PACKET_VERSION, "seq": 1, "ts": 1762400000,
                    "cpu": -1, "gpu": 1, "ram": 1, "rx": 1, "tx": 1,
                }
            ).encode(),
            PARSE_INVALID_VALUE,
        ),
        (
            "ts 为零",
            json.dumps(
                {
                    "v": PACKET_VERSION, "seq": 1, "ts": 0,
                    "cpu": 1, "gpu": 1, "ram": 1, "rx": 1, "tx": 1,
                }
            ).encode(),
            PARSE_INVALID_VALUE,
        ),
        (
            "gpu 类型错误",
            json.dumps(
                {
                    "v": PACKET_VERSION, "seq": 1, "ts": 1762400000,
                    "cpu": 1, "gpu": "abc", "ram": 1, "rx": 1, "tx": 1,
                }
            ).encode(),
            PARSE_INVALID_VALUE,
        ),
        (
            "尾部有非空白垃圾",
            encode_packet(
                seq=9, sampled_at=1762400000, cpu=1, gpu=1, ram=1,
                rx=1, tx=1,
            )
            + b"XYZ",
            PARSE_INVALID_JSON,
        ),
        (
            "seq 为负数",
            json.dumps(
                {
                    "v": PACKET_VERSION, "seq": -3, "ts": 1762400000,
                    "cpu": 1, "gpu": 1, "ram": 1, "rx": 1, "tx": 1,
                }
            ).encode(),
            PARSE_INVALID_VALUE,
        ),
    ]


def main():
    cases = []
    for name, payload in valid_cases():
        cases.append((name, PARSE_OK, payload))
    cases.extend(
        (name, expected, payload)
        for name, payload, expected in invalid_cases()
    )

    lines = [HEADER]
    for name, expected, payload in cases:
        if len(payload) > 512:
            raise SystemExit(f"用例 {name} 超过 512 字节上限")
        lines.append(
            f'    {{"{name}", {expected}, "{c_literal(payload)}", '
            f"{len(payload)}}},\n"
        )
    lines.append(FOOTER)

    OUTPUT.parent.mkdir(parents=True, exist_ok=True)
    OUTPUT.write_text("".join(lines), encoding="utf-8")
    print(f"OK: 已生成 {OUTPUT.relative_to(REPO_ROOT)}（{len(cases)} 条用例）")
    return 0


if __name__ == "__main__":
    sys.exit(main())
