"""Runtime settings for the Windows metrics broadcaster."""

# This workstation's wired adapter was verified as the active physical
# Ethernet interface. Change this value if the adapter has a different name.
ETHERNET_INTERFACE = "以太网"
# Leave empty to select the address whose subnet contains this adapter's
# default gateway. Set an exact interface IPv4 address only if that is
# ambiguous or Windows cannot report the adapter's gateway.
ETHERNET_IPV4_ADDRESS = ""
UDP_PORT = 8765
SAMPLE_INTERVAL_SECONDS = 1.0
