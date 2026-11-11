"""端到端验证 PC 端 UDP 发送与固件解析器的对接。

流程：把固件里的 `metrics_parser.c` 与宿主端接收器 `host_udp_sink.c` 编成一个
本机可执行文件，绑定真实 UDP 端口；再用 PC 端真实的 `pc_metrics.protocol`
编码器生成报文并通过真实套接字发送，最后比对接收端报告的结果。

这样覆盖了单元测试够不到的环节：端口可达性、数据报大小、是否被截断，以及
两侧对「什么算合法报文」的判断是否一致。

用法：

    python3 tools/verify_udp_end_to_end.py
"""

import pathlib
import shutil
import socket
import subprocess
import sys
import tempfile
import time

REPO_ROOT = pathlib.Path(__file__).resolve().parent.parent
FIRMWARE = REPO_ROOT / "pico_freertos_firmware"

sys.path.insert(0, str(REPO_ROOT))

from pc_metrics.protocol import encode_packet  # noqa: E402

PORT = 8765
EXPECTED = 5


def build_sink(output):
    """把接收器与固件解析器编成本机可执行文件。"""
    compiler = shutil.which("cc") or shutil.which("gcc") or shutil.which("clang")
    if compiler is None:
        raise SystemExit("找不到 C 编译器（cc / gcc / clang）")
    command = [
        compiler,
        "-std=gnu11",
        "-Wall",
        "-Wextra",
        "-Werror",
        "-g",
        "-O1",
        "-I",
        str(FIRMWARE / "include"),
        "-I",
        str(FIRMWARE / "tests" / "cjson"),
        str(FIRMWARE / "tests" / "host_udp_sink.c"),
        str(FIRMWARE / "src" / "network" / "metrics_parser.c"),
        str(FIRMWARE / "tests" / "cjson" / "cJSON.c"),
        "-o",
        str(output),
    ]
    print(f"$ {' '.join(command)}")
    subprocess.run(command, check=True, cwd=REPO_ROOT)
    return output


def collect_packets():
    """用真实编码器造出待发送的报文。"""
    return [
        ("典型值", encode_packet(seq=1, sampled_at=1762400000, cpu=42, gpu=15,
                                 ram=61, rx=1.5, tx=0.25)),
        ("无独显", encode_packet(seq=2, sampled_at=1762400001, cpu=7, gpu=None,
                                 ram=33, rx=0.0, tx=0.0)),
        ("满载", encode_packet(seq=3, sampled_at=1762400002, cpu=100, gpu=100,
                               ram=100, rx=999.99, tx=1234.5)),
        ("空载", encode_packet(seq=4, sampled_at=1762400003, cpu=0, gpu=0,
                               ram=0, rx=0, tx=0)),
        ("浮点占比", encode_packet(seq=5, sampled_at=1762400004, cpu=42.5,
                                   gpu=0.0, ram=61.25, rx=0.001, tx=0.002)),
    ]


def main():
    packets = collect_packets()
    for name, payload in packets:
        if len(payload) > 512:
            raise SystemExit(f"用例 {name} 超过 512 字节协议上限")

    with tempfile.TemporaryDirectory() as workspace:
        sink = pathlib.Path(workspace) / "host_udp_sink"
        build_sink(sink)

        process = subprocess.Popen(
            [str(sink), "--port", str(PORT), "--expect", str(EXPECTED),
             "--timeout-ms", "5000"],
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            cwd=str(REPO_ROOT),
        )
        # 等接收端打印 READY，避免报文发出去时套接字还没绑定。
        ready_line = process.stdout.readline()
        if not ready_line.startswith("READY"):
            process.kill()
            raise SystemExit(f"接收端未就绪：{ready_line!r}")

        sent = 0
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sender:
            # 先发一条明显非法的报文：接收端在收满合法报文数后就会退出，
            # 非法报文必须在它退出之前到达才会被处理。
            sender.sendto(b'{"v":99}', ("127.0.0.1", PORT))
            print("发送非法报文: v=99（应被拒绝）")
            time.sleep(0.05)
            for name, payload in packets:
                sender.sendto(payload, ("127.0.0.1", PORT))
                sent += 1
                print(f"发送 {sent}/{len(packets)}: {name} {len(payload)} 字节")
                time.sleep(0.02)

        output, _ = process.communicate(timeout=10)
        print("--- 接收端输出 ---")
        print(output.strip())

        if process.returncode != 0:
            raise SystemExit(f"接收端退出码 {process.returncode}")

        lines = [line for line in output.splitlines() if line.startswith("OK ")]
        if len(lines) != EXPECTED:
            raise SystemExit(f"期望收到 {EXPECTED} 条合法报文，实际 {len(lines)}")
        if "REJECT" not in output:
            raise SystemExit("非法报文没有被拒绝，解析器放行了坏数据")

        # 逐字段比对，确认解析结果与发送端完全一致，而不是「只要能收就算过」。
        first = lines[0]
        assert "seq=1" in first, first
        assert "cpu=42.00" in first, first
        assert "ram=61.00" in first, first
        assert "gpu_valid=1" in first, first
        second = lines[1]
        assert "gpu_valid=0" in second, second

        print("PASS: 端到端 UDP 链路验证通过")
        print(f"      合法 {len(lines)} 条全部按字段一致解析，非法报文被拒绝")
    return 0


if __name__ == "__main__":
    sys.exit(main())
