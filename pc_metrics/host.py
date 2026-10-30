"""Run the Windows metrics collector and UDP broadcast loop."""

from __future__ import annotations

import ipaddress
import os
import socket
import subprocess
import sys
import time

import psutil

from pc_metrics import config
from pc_metrics.broadcaster import get_broadcast_address, send_packet
from pc_metrics.collectors import MetricsCollector
from pc_metrics.protocol import encode_packet


def _interface_candidates() -> str:
    """Format current IPv4 interfaces to make adapter-selection failures actionable."""
    addresses = psutil.net_if_addrs()
    stats = psutil.net_if_stats()
    lines = []
    for name, entries in addresses.items():
        ipv4 = [entry.address for entry in entries if entry.family == socket.AF_INET]
        if ipv4:
            state = "Up" if stats.get(name) and stats[name].isup else "Down"
            lines.append(f"  - {name} ({state}): {', '.join(ipv4)}")
    return "\n".join(lines) if lines else "  (没有发现带 IPv4 地址的网卡)"


def _get_ethernet_details(interface_name: str) -> tuple[str, str]:
    """Get the IPv4 address and subnet broadcast for one explicitly named NIC."""
    if not isinstance(interface_name, str) or not interface_name.strip():
        raise RuntimeError(
            "未配置 ETHERNET_INTERFACE；请从以下候选网卡中填写有线网卡名称：\n"
            f"{_interface_candidates()}"
        )

    stats = psutil.net_if_stats()
    addresses = psutil.net_if_addrs()
    status = stats.get(interface_name)
    if status is None or not status.isup:
        raise RuntimeError(
            f"有线网卡“{interface_name}”不存在或未连接。当前候选网卡：\n"
            f"{_interface_candidates()}"
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
        raise RuntimeError(f"{exc}\n当前候选网卡：\n{_interface_candidates()}") from exc


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


def _select_interface_ipv4(
    entries,
    *,
    default_gateway: str | None,
    configured_address: str = "",
) -> tuple[str, str]:
    """Choose one address by explicit override or selected-NIC gateway subnet."""
    candidates = []
    for entry in entries:
        if entry.family != socket.AF_INET or not entry.address or not entry.netmask:
            continue
        try:
            broadcast = get_broadcast_address(entry.address, entry.netmask)
            network = ipaddress.IPv4Network(
                f"{entry.address}/{entry.netmask}", strict=False
            )
        except ValueError:
            continue
        candidates.append((entry.address, broadcast, network))

    if configured_address:
        matches = [item for item in candidates if item[0] == configured_address]
        if len(matches) == 1:
            return matches[0][0], matches[0][1]
        raise RuntimeError(
            f"配置的 ETHERNET_IPV4_ADDRESS“{configured_address}”不是所选有线网卡上的"
            "唯一有效 IPv4 地址"
        )

    if default_gateway:
        try:
            gateway = ipaddress.IPv4Address(default_gateway)
        except ipaddress.AddressValueError as exc:
            raise RuntimeError(f"默认网关不是有效 IPv4 地址：{default_gateway}") from exc
        matches = [item for item in candidates if gateway in item[2]]
        if len(matches) == 1:
            return matches[0][0], matches[0][1]
        if len(matches) > 1:
            raise RuntimeError(
                f"默认网关 {default_gateway} 同时匹配多个有线网卡 IPv4 子网；"
                "请设置 ETHERNET_IPV4_ADDRESS 明确指定地址"
            )
        raise RuntimeError(
            f"网卡默认网关 {default_gateway} 不属于该网卡的任何 IPv4 子网；"
            "请设置 ETHERNET_IPV4_ADDRESS 明确指定地址"
        )

    if len(candidates) == 1:
        return candidates[0][0], candidates[0][1]
    if not candidates:
        raise RuntimeError("所选有线网卡没有有效 IPv4 地址和子网掩码")
    raise RuntimeError(
        "所选有线网卡有多个 IPv4 子网且没有可用默认网关；"
        "请在 pc_metrics/config.py 设置 ETHERNET_IPV4_ADDRESS"
    )


def _next_deadline(deadline: float, interval: float, now: float) -> float:
    """Advance one period and skip elapsed periods instead of catching up."""
    deadline += interval
    if deadline <= now:
        missed = int((now - deadline) // interval) + 1
        deadline += missed * interval
    return deadline


def run() -> int:
    """Collect and broadcast metrics until Ctrl+C or a fatal host error."""
    try:
        local_address, broadcast_address = _get_ethernet_details(
            config.ETHERNET_INTERFACE
        )
    except RuntimeError as exc:
        print(f"启动失败：{exc}", file=sys.stderr)
        return 2

    interval = config.SAMPLE_INTERVAL_SECONDS
    print(
        f"有线网卡：{config.ETHERNET_INTERFACE} ({local_address})；"
        f"UDP 广播：{broadcast_address}:{config.UDP_PORT}；"
        f"采样间隔：{interval:g} 秒。按 Ctrl+C 停止。"
    )

    collector = MetricsCollector(config.ETHERNET_INTERFACE)
    udp_socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sequence = 0
    deadline = time.monotonic()

    try:
        while True:
            delay = deadline - time.monotonic()
            if delay > 0:
                time.sleep(delay)

            sample = collector.sample()
            if sample is not None:
                packet = encode_packet(
                    seq=sequence,
                    sampled_at=int(time.time()),
                    cpu=sample["cpu"],
                    gpu=sample["gpu"],
                    ram=sample["ram"],
                    rx=sample["rx"],
                    tx=sample["tx"],
                )
                try:
                    send_packet(
                        udp_socket,
                        packet,
                        broadcast_address,
                        config.UDP_PORT,
                    )
                except OSError as exc:
                    print(f"UDP 广播失败：{exc}", file=sys.stderr)
                else:
                    sequence += 1

            deadline = _next_deadline(
                deadline,
                interval,
                time.monotonic(),
            )
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
