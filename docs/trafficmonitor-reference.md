# TrafficMonitor reference notes

The implementation was checked against the upstream source and wiki cloned during development:

```text
https://github.com/zhongyang219/TrafficMonitor
https://github.com/zhongyang219/TrafficMonitor.wiki
```

The source checkout used was `188b8773b959733bfc0e4f506c762d9a255883d4` and the
wiki checkout was `cf35274094fe2ffb040ca1f29d6ce73c713d93a2`.

The relevant upstream files are `include/PluginInterface.h`, `PluginDemo/PluginDemo.cpp`,
`PluginDemo/CustomDrawItem.cpp`, and the wiki pages `插件开发指南.md` and `插件功能.md`.
The guide requires a Windows dynamic library, a single exported `TMPluginGetInstance`
function, an `ITMPlugin` implementation, and one `IPluginItem` for each display item.
The plugin follows those rules and uses API version 8 so it can opt into
`IPluginItem::IsDoubleLineExclusive`.

TrafficMonitor invokes `DataRequired` before asking an item for its text. The sampler
therefore performs all IP Helper calls in `DataRequired`; `GetItemValueText` and
`DrawItem` only format cached values. The upstream taskbar layout gives an exclusive
double-line custom item its own column, which is why the IPv4 and IPv6 rows remain
visually grouped in one taskbar column.

The host's `MI_UP` and `MI_DOWN` counters are interface-wide and cannot identify the
address family. Windows IP Helper's TCP extended statistics expose byte counters on
individual IPv4 and IPv6 TCP rows, so the plugin keeps separate connection maps and
computes deltas for each family. This intentionally documents the measurement scope:
UDP and TCP connections for which Windows does not expose extended statistics are not
silently assigned to either family.
