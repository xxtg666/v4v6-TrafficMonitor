from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
source = (ROOT / "src" / "Ipv4Ipv6TrafficPlugin.cpp").read_text(encoding="utf-8")
interface = (ROOT / "include" / "PluginInterface.h").read_text(encoding="utf-8-sig")
cmake = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")

required_source_tokens = (
    'TMPluginGetInstance',
    'GetPerTcpConnectionEStats',
    'GetPerTcp6ConnectionEStats',
    'AF_INET',
    'AF_INET6',
    'IsDoubleLineExclusive',
    'IPv4',
    'IPv6',
)
for token in required_source_tokens:
    assert token in source, f"missing implementation token: {token}"

for token in ("class ITMPlugin", "class IPluginItem", "GetAPIVersion", "DrawItem"):
    assert token in interface, f"interface header is incomplete: {token}"

assert "add_library(TrafficMonitorIpv4Ipv6 SHARED" in cmake
assert "iphlpapi" in cmake and "ws2_32" in cmake
assert "GetItem(int index) override" in source
assert "return index == 0 ? &m_item : nullptr;" in source

print("plugin contract checks passed")
