"""Small helpers for sending PC metrics as directed UDP broadcasts."""

from __future__ import annotations

import ipaddress
import socket


def get_broadcast_address(address: str, netmask: str) -> str:
    """Return the IPv4 directed broadcast for an interface address and mask."""
    if not isinstance(address, str) or not isinstance(netmask, str):
        raise ValueError("有线网卡必须提供有效 IPv4 地址和子网掩码")
    try:
        interface = ipaddress.IPv4Interface(f"{address}/{netmask}")
    except (ipaddress.AddressValueError, ipaddress.NetmaskValueError, ValueError) as exc:
        raise ValueError("有线网卡必须提供有效 IPv4 地址和子网掩码") from exc

    network = interface.network
    if network.prefixlen == 0 or network.prefixlen >= 31:
        raise ValueError("有线网卡子网必须支持 IPv4 定向广播")
    if (
        interface.ip.is_unspecified
        or interface.ip.is_multicast
        or interface.ip.is_loopback
        or interface.ip.is_link_local
        or interface.ip.is_reserved
    ):
        raise ValueError("有线网卡 IPv4 地址必须是有效单播地址")
    if interface.ip in (network.network_address, network.broadcast_address):
        raise ValueError("网卡 IPv4 地址不能是网络地址或广播地址")
    return str(network.broadcast_address)


def send_packet(
    sock: socket.socket,
    packet: bytes,
    broadcast_address: str,
    port: int = 8765,
) -> int:
    """Send one UDP datagram to a subnet broadcast without opening a listener."""
    if not isinstance(port, int) or isinstance(port, bool) or not 1 <= port <= 65535:
        raise ValueError("UDP 端口必须在 1–65535 之间")
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
    return sock.sendto(packet, (broadcast_address, port))
