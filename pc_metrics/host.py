"""Run the Windows metrics collector and UDP broadcast loop."""

from __future__ import annotations

import ipaddress
import math
import os
import socket
import subprocess
import sys
import time
from collections.abc import Callable

import psutil

from pc_metrics import config
from pc_metrics.broadcaster import get_broadcast_address, send_packet
from pc_metrics.collectors import InterfaceNotFoundError, MetricsCollector
from pc_metrics.protocol import encode_packet

# Retry delays for consecutive sample/broadcast failures, capped at 30 seconds.
BACKOFF_SECONDS = (1.0, 2.0, 5.0, 10.0, 20.0, 30.0)
DROP_WARNING_THRESHOLD = 5
FAILURE_REPORT_EVERY = 10


def _backoff_delay(failure_count: int) -> float:
    """Return the capped retry delay for a run of consecutive failures."""
    index = max(0, min(failure_count - 1, len(BACKOFF_SECONDS) - 1))
    return BACKOFF_SECONDS[index]


class _FailureReporter:
    """Print a failure once, then only every tenth repeat of the same reason."""

    def __init__(self, every: int = FAILURE_REPORT_EVERY) -> None:
        self._every = every
        self._reason: str | None = None
        self._count = 0

    def should_report(self, reason: str) -> bool:
        """Count one failure and decide whether its reason should be printed."""
        if reason != self._reason:
            self._reason = reason
            self._count = 1
            return True
        self._count += 1
        return self._count % self._every == 0

    def reset(self) -> None:
        """Forget the current reason after the loop recovered."""
        self._reason = None
        self._count = 0


def _interface_candidates(addresses, stats) -> str:
    """Format every interface, including adapters that have no IPv4 address."""
    lines = []
    for name in sorted(set(addresses) | set(stats)):
        ipv4 = [
            entry.address
            for entry in addresses.get(name, [])
            if entry.family == socket.AF_INET and entry.address
        ]
        status = stats.get(name)
        state = "Up" if status is not None and status.isup else "Down"
        detail = ", ".join(ipv4) if ipv4 else "无 IPv4 地址"
        lines.append(f"  - {name} ({state}): {detail}")
    return "\n".join(lines) if lines else "  (没有发现任何网卡)"


def _get_ethernet_details(interface_name: str) -> tuple[str, str]:
    """Get the IPv4 address and subnet broadcast for one explicitly named NIC."""
    try:
        stats = psutil.net_if_stats()
        addresses = psutil.net_if_addrs()
    except psutil.Error as exc:
        detail = str(exc) or exc.__class__.__name__
        raise RuntimeError(f"无法读取网卡信息：{detail}") from exc

    candidates_text = _interface_candidates(addresses, stats)
    if not isinstance(interface_name, str) or not interface_name.strip():
        raise RuntimeError(
            "未配置 ETHERNET_INTERFACE；请从以下候选网卡中填写有线网卡名称：\n"
            f"{candidates_text}"
        )

    status = stats.get(interface_name)
    if status is None or not status.isup:
        raise RuntimeError(
            f"有线网卡“{interface_name}”不存在或未连接。当前候选网卡：\n"
            f"{candidates_text}"
        )

    candidates = addresses.get(interface_name, [])
    default_gateway = None
    configured_address = config.ETHERNET_IPV4_ADDRESS.strip()
    if not configured_address:
        try:
            default_gateway = _get_interface_default_gateway(interface_name)
        except RuntimeError as exc:
            raise RuntimeError(
                f"{exc}\n如无法读取该网卡的默认网关，请在 pc_metrics/config.py "
                "设置 ETHERNET_IPV4_ADDRESS 为该接口的准确 IPv4 地址。"
            ) from exc
    try:
        return _select_interface_ipv4(
            candidates,
            default_gateway=default_gateway,
            configured_address=configured_address,
        )
    except RuntimeError as exc:
        raise RuntimeError(f"{exc}\n当前候选网卡：\n{candidates_text}") from exc


def _get_interface_default_gateway(interface_name: str) -> str | None:
    """Read the selected adapter's IPv4 default gateway without using VPN routes."""
    command = [
        "powershell.exe",
        "-NoProfile",
        "-NonInteractive",
        "-Command",
        (
            "$adapter = Get-NetAdapter -Name $env:PICO_ETHERNET_INTERFACE "
            "-ErrorAction Stop; "
            "$routes = @(Get-NetRoute -DestinationPrefix '0.0.0.0/0' "
            "-InterfaceIndex $adapter.ifIndex | "
            "Where-Object { $_.NextHop -ne '0.0.0.0' } | "
            "Sort-Object RouteMetric); "
            "if ($routes.Length -gt 0) { "
            "$metric = $routes[0].RouteMetric; "
            "$routes | Where-Object { $_.RouteMetric -eq $metric } | "
            "Select-Object -ExpandProperty NextHop -Unique }"
        ),
    ]
    environment = os.environ.copy()
    environment["PICO_ETHERNET_INTERFACE"] = interface_name
    try:
        result = subprocess.run(
            command,
            capture_output=True,
            text=True,
            timeout=3.0,
            check=True,
            env=environment,
        )
    except (OSError, subprocess.SubprocessError) as exc:
        raise RuntimeError(
            f"无法读取有线网卡“{interface_name}”自己的 IPv4 默认网关"
        ) from exc

    gateways = list(
        dict.fromkeys(
            line.strip() for line in result.stdout.splitlines() if line.strip()
        )
    )
    if not gateways:
        return None
    parsed_gateways = []
    for gateway in gateways:
        try:
            parsed_gateways.append(str(ipaddress.IPv4Address(gateway)))
        except ipaddress.AddressValueError as exc:
            raise RuntimeError(
                f"有线网卡“{interface_name}”返回了无效 IPv4 网关：{gateway}"
            ) from exc
    if len(parsed_gateways) > 1:
        raise RuntimeError(
            f"有线网卡“{interface_name}”存在多个相同优先级的 IPv4 默认网关"
        )
    return parsed_gateways[0]


def _unique_by_broadcast(candidates):
    """Keep the first candidate per broadcast address to remove subnet duplicates."""
    unique = {}
    for candidate in candidates:
        unique.setdefault(candidate[1], candidate)
    return list(unique.values())


def _available_summary(candidates) -> str:
    """List the usable IPv4 addresses for an error message."""
    addresses = list(dict.fromkeys(item[0] for item in candidates))
    return "、".join(addresses) if addresses else "（无）"


def _skipped_summary(skipped: list[tuple[str, str]]) -> str:
    """Describe rejected addresses together with the reason each was rejected."""
    if not skipped:
        return ""
    details = "；".join(f"{address}（{reason}）" for address, reason in skipped)
    return f"已跳过不可用地址：{details}。"


def _select_interface_ipv4(
    entries,
    *,
    default_gateway: str | None,
    configured_address: str = "",
) -> tuple[str, str]:
    """Choose one address by explicit override or selected-NIC gateway subnet."""
    candidates = []
    skipped: list[tuple[str, str]] = []
    for entry in entries:
        if entry.family != socket.AF_INET:
            continue
        address = getattr(entry, "address", None)
        netmask = getattr(entry, "netmask", None)
        if not address or not netmask:
            skipped.append((str(address or "未知地址"), "缺少 IPv4 地址或子网掩码"))
            continue
        try:
            broadcast = get_broadcast_address(address, netmask)
            network = ipaddress.IPv4Network(f"{address}/{netmask}", strict=False)
        except ValueError as exc:
            skipped.append((address, str(exc)))
            continue
        candidates.append((address, broadcast, network))

    if configured_address:
        matches = [item for item in candidates if item[0] == configured_address]
        if len(matches) == 1:
            return matches[0][0], matches[0][1]
        raise RuntimeError(
            f"配置的 ETHERNET_IPV4_ADDRESS“{configured_address}”不是所选有线网卡上的"
            f"唯一有效 IPv4 地址；可用地址：{_available_summary(candidates)}。"
            f"{_skipped_summary(skipped)}"
        )

    if default_gateway:
        try:
            gateway = ipaddress.IPv4Address(default_gateway)
        except ipaddress.AddressValueError as exc:
            raise RuntimeError(
                f"默认网关不是有效 IPv4 地址：{default_gateway}。"
                f"{_skipped_summary(skipped)}"
            ) from exc
        matches = _unique_by_broadcast(
            [item for item in candidates if gateway in item[2]]
        )
        if len(matches) == 1:
            return matches[0][0], matches[0][1]
        if len(matches) > 1:
            detail = "、".join(f"{item[0]}（{item[2]}）" for item in matches)
            raise RuntimeError(
                f"默认网关 {default_gateway} 同时匹配多个有线网卡 IPv4 子网"
                f"（{detail}）；请设置 ETHERNET_IPV4_ADDRESS 明确指定地址。"
                f"{_skipped_summary(skipped)}"
            )
        unique = _unique_by_broadcast(candidates)
        if len(unique) == 1:
            return unique[0][0], unique[0][1]
        raise RuntimeError(
            f"网卡默认网关 {default_gateway} 不属于该网卡的任何 IPv4 子网；"
            f"可用地址：{_available_summary(unique)}。{_skipped_summary(skipped)}"
            "请设置 ETHERNET_IPV4_ADDRESS 明确指定地址"
        )

    unique = _unique_by_broadcast(candidates)
    if len(unique) == 1:
        return unique[0][0], unique[0][1]
    if not unique:
        raise RuntimeError(
            f"所选有线网卡没有可用 IPv4 地址和子网掩码。{_skipped_summary(skipped)}"
        )
    raise RuntimeError(
        "所选有线网卡有多个 IPv4 子网且没有可用默认网关；"
        "请在 pc_metrics/config.py 设置 ETHERNET_IPV4_ADDRESS。"
        f"可用地址：{_available_summary(unique)}。"
        f"{_skipped_summary(skipped)}"
    )


def _next_deadline(deadline: float, interval: float, now: float) -> float:
    """Advance one period and skip elapsed periods instead of catching up."""
    if not math.isfinite(interval) or interval <= 0:
        raise ValueError("采样间隔必须是正数")
    deadline += interval
    if deadline <= now:
        missed = int((now - deadline) // interval) + 1
        deadline += missed * interval
    return deadline


def _validate_settings() -> None:
    """Validate runtime settings before opening any host resource."""
    interface = config.ETHERNET_INTERFACE
    if not isinstance(interface, str) or not interface.strip():
        raise RuntimeError("ETHERNET_INTERFACE 必须是非空字符串")
    address = config.ETHERNET_IPV4_ADDRESS
    if not isinstance(address, str):
        raise RuntimeError("ETHERNET_IPV4_ADDRESS 必须是字符串（留空表示自动选择）")
    port = config.UDP_PORT
    if isinstance(port, bool) or not isinstance(port, int) or not 1 <= port <= 65535:
        raise RuntimeError("UDP_PORT 必须是 1–65535 之间的整数")
    interval = config.SAMPLE_INTERVAL_SECONDS
    if (
        isinstance(interval, bool)
        or not isinstance(interval, (int, float))
        or not math.isfinite(interval)
        or interval <= 0
    ):
        raise RuntimeError("SAMPLE_INTERVAL_SECONDS 必须是正数")


def _open_socket(local_address: str) -> socket.socket:
    """Create the broadcast socket once, bind it to the chosen interface, and own it."""
    udp_socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        udp_socket.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
        udp_socket.bind((local_address, 0))
    except OSError:
        udp_socket.close()
        raise
    return udp_socket


def _rebuild_endpoint(
    resolve: Callable[[str], tuple[str, str]],
    make_socket: Callable[[str], socket.socket],
) -> tuple[str, str, socket.socket]:
    """Re-resolve the adapter endpoint and create a replacement bound socket."""
    local_address, broadcast_address = resolve(config.ETHERNET_INTERFACE)
    return local_address, broadcast_address, make_socket(local_address)


def run(
    *,
    resolve_endpoint: Callable[[str], tuple[str, str]] | None = None,
    collector_factory: Callable[[str], MetricsCollector] | None = None,
    socket_factory: Callable[[str], socket.socket] | None = None,
    sleep: Callable[[float], None] | None = None,
    monotonic: Callable[[], float] | None = None,
    wall_clock: Callable[[], float] | None = None,
) -> int:
    """Collect and broadcast metrics until Ctrl+C or a fatal host error.

    The optional callables are seams for tests and default to the real host
    implementations.
    """
    resolve = _get_ethernet_details if resolve_endpoint is None else resolve_endpoint
    make_collector = (
        MetricsCollector if collector_factory is None else collector_factory
    )
    make_socket = _open_socket if socket_factory is None else socket_factory
    wait = time.sleep if sleep is None else sleep
    now = time.monotonic if monotonic is None else monotonic
    timestamp = time.time if wall_clock is None else wall_clock

    try:
        _validate_settings()
        local_address, broadcast_address = resolve(config.ETHERNET_INTERFACE)
    except (RuntimeError, OSError, psutil.Error) as exc:
        print(f"启动失败：{exc}", file=sys.stderr)
        return 2

    interval = float(config.SAMPLE_INTERVAL_SECONDS)
    print(
        f"有线网卡：{config.ETHERNET_INTERFACE} ({local_address})；"
        f"UDP 广播：{broadcast_address}:{config.UDP_PORT}；"
        f"采样间隔：{interval:g} 秒。按 Ctrl+C 停止。"
    )

    try:
        collector = make_collector(config.ETHERNET_INTERFACE)
        udp_socket = make_socket(local_address)
    except (ValueError, OSError, psutil.Error) as exc:
        print(f"启动失败：{exc}", file=sys.stderr)
        return 2

    sequence = 0
    deadline = now()
    failures = 0
    drops = 0
    gpu_warning_printed = False
    drop_warning_printed = False
    reporter = _FailureReporter()

    try:
        while True:
            remaining = deadline - now()
            if remaining > 0:
                wait(remaining)

            try:
                sample = collector.sample()
                if sample is not None:
                    packet = encode_packet(
                        seq=sequence,
                        sampled_at=int(timestamp()),
                        cpu=sample["cpu"],
                        gpu=sample["gpu"],
                        ram=sample["ram"],
                        rx=sample["rx"],
                        tx=sample["tx"],
                    )
                    send_packet(
                        udp_socket,
                        packet,
                        broadcast_address,
                        config.UDP_PORT,
                    )
                    sequence += 1
                deadline = _next_deadline(deadline, interval, now())
            except (ValueError, OSError, psutil.Error) as exc:
                failures += 1
                reason = str(exc) or exc.__class__.__name__
                if isinstance(exc, (OSError, psutil.Error, InterfaceNotFoundError)):
                    try:
                        (
                            local_address,
                            broadcast_address,
                            replacement,
                        ) = _rebuild_endpoint(resolve, make_socket)
                    except (RuntimeError, OSError, psutil.Error) as recovery_exc:
                        reason = f"{reason}；重新解析网卡失败：{recovery_exc}"
                    else:
                        udp_socket.close()
                        udp_socket = replacement
                delay = _backoff_delay(failures)
                if reporter.should_report(reason):
                    print(
                        f"采样或广播失败：{reason}；{delay:g} 秒后重试。",
                        file=sys.stderr,
                    )
                wait(delay)
                deadline = now()
                continue

            failures = 0
            reporter.reset()
            if collector.gpu_unavailable and not gpu_warning_printed:
                print("GPU 指标不可用，已降低 nvidia-smi 查询频率。")
                gpu_warning_printed = True
            if sample is None:
                drops += 1
                if drops == DROP_WARNING_THRESHOLD and not drop_warning_printed:
                    print(
                        f"已连续 {DROP_WARNING_THRESHOLD} 次丢弃采样："
                        f"{collector.last_drop_reason or '原因未知'}",
                        file=sys.stderr,
                    )
                    drop_warning_printed = True
            else:
                drops = 0
                drop_warning_printed = False
    finally:
        udp_socket.close()


def main() -> int:
    try:
        return run()
    except KeyboardInterrupt:
        print("\n指标广播已停止。")
        return 0


if __name__ == "__main__":
    raise SystemExit(main())
