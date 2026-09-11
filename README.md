# TrafficMonitor IPv4/IPv6 流量插件

这是一个遵循 [TrafficMonitor 插件开发指南](https://github.com/zhongyang219/TrafficMonitor/wiki/%E6%8F%92%E4%BB%B6%E5%BC%80%E5%8F%91%E6%8C%87%E5%8D%97) 的 Windows 动态库插件。它在任务栏中提供一个自绘项目，将 IPv4 和 IPv6 的 TCP 进出速率分成两行显示：

```
IPv4 ↓ 1.2 MB/s ↑ 64 KB/s
IPv6 ↓ 18 KB/s  ↑ 2 KB/s
```

插件使用 Windows IP Helper 的 TCP extended statistics (`GetPerTcpConnectionEStats` / `GetPerTcp6ConnectionEStats`) 按地址族汇总活动 TCP 连接的字节增量。这样 IPv4 与 IPv6 不会混在同一列；没有活动连接时显示 `0 B/s`。UDP 和无法提供 extended statistics 的连接不会被猜测或错误归入另一种协议。

## 构建

需要 Visual Studio 2022（或带 Windows SDK 的 MinGW 工具链）以及 CMake：

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

在 Linux Codespace 中也可以用 MinGW 交叉验证 Windows DLL：

```bash
x86_64-w64-mingw32-g++ -std=c++17 -shared -Iinclude -Isrc \
  src/Ipv4Ipv6TrafficPlugin.cpp -o TrafficMonitorIpv4Ipv6.dll \
  -liphlpapi -lws2_32 -luser32 -lgdi32
```

也可以使用仓库内的交叉编译工具链文件：

```bash
cmake -S . -B build-mingw \
  -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/x86_64-w64-mingw32.cmake
cmake --build build-mingw
```

生成的 `TrafficMonitorIpv4Ipv6.dll` 放到 TrafficMonitor 安装目录的 `plugins` 文件夹，重启程序后在任务栏窗口右键菜单的“显示设置”中勾选 **IPv4/IPv6 流量**。

插件按 TrafficMonitor 的 ABI 导出 `TMPluginGetInstance`，项目 ID 为 `IPv6Traffic`，接口版本为 8，并通过 `IsCustomDraw` 和 `IsDoubleLineExclusive` 绘制两行紧凑信息。

## 数据范围

Windows 没有一个可直接读取、同时覆盖 TCP/UDP 的按地址族字节计数器。插件选择使用官方 TCP extended statistics，结果是可复现且按 IPv4/IPv6 精确归属的 TCP 流量；README 明确标注这一范围，避免把 IPv4/IPv6 混合接口计数伪装成协议拆分结果。
