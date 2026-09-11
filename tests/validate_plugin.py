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

assert "today_bytes" in source
assert "SetConfigDir" in source and "LoadTotals" in source and "SaveTotals" in source
assert "↓" not in source and "↑" not in source
assert "B/s" not in source

for token in ("class ITMPlugin", "class IPluginItem", "GetAPIVersion", "DrawItem"):
    assert token in interface, f"interface header is incomplete: {token}"

assert "add_library(TrafficMonitorIpv4Ipv6 SHARED" in cmake
assert all(name in cmake for name in ("iphlpapi", "ws2_32", "user32", "gdi32"))
assert (ROOT / "cmake" / "toolchains" / "x86_64-w64-mingw32.cmake").exists()
assert "GetItem(int index) override" in source
assert "return index == 0 ? &m_item : nullptr;" in source

print("plugin contract checks passed")
