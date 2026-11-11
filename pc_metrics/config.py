"""Runtime settings for the metrics broadcaster.

这份配置同时服务两台发送端主机：Windows 工作站与 Jetson Orin（Ubuntu
24.04）。两边有线/无线网卡的名字完全不同，因此网卡名支持用环境变量
``PICO_ETHERNET_INTERFACE`` 覆盖，避免为了换一台机器就要改源码：

    # Jetson 上走 Wi-Fi 直连的示例
    PICO_ETHERNET_INTERFACE=wlP1p1s0 python -m pc_metrics.host

不设置环境变量时沿用 Windows 工作站上已核对过的适配器名“以太网”。
"""

import os

# This workstation's wired adapter was verified as the active physical
# Ethernet interface. Change this value if the adapter has a different name.
ETHERNET_INTERFACE = os.environ.get("PICO_ETHERNET_INTERFACE", "以太网")
# Leave empty to select the address whose subnet contains this adapter's
# default gateway. Set an exact interface IPv4 address only if that is
# ambiguous or Windows cannot report the adapter's gateway.
ETHERNET_IPV4_ADDRESS = ""
UDP_PORT = 8765
SAMPLE_INTERVAL_SECONDS = 1.0
