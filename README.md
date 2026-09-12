# TrafficMonitor IPv4/IPv6 流量插件

这是一个遵循 [TrafficMonitor 插件开发指南](https://github.com/zhongyang219/TrafficMonitor/wiki/%E6%8F%92%E4%BB%B6%E5%BC%80%E5%8F%91%E6%8C%87%E5%8D%97) 的 Windows 动态库插件。它在任务栏中提供一个自绘项目，将当天 IPv4 和 IPv6 的 TCP 累计总流量分成两行显示：

```
IPv4 1.20G
IPv6 18M
```

宿主只分配一行高度时（例如同时开启总流量显示），默认显示 `50M / 50M`，左侧 IPv4、右侧 IPv6。两行时默认保留完整协议名称。G 单位默认固定两位小数，例如 `1.00G`、`12.50G`；其他单位的精度保持不变。悬停提示始终显示 IPv4、IPv6 名称和当天累计值。

是否使用两行由实际可用高度和当前字体高度决定，宽度按自定义格式和字体测量；较窄区域会缩小字体以避免覆盖相邻项目。

插件使用 Windows IP Helper 的 TCP extended statistics (`GetPerTcpConnectionEStats` / `GetPerTcp6ConnectionEStats`) 按地址族汇总活动 TCP 连接的字节增量。任务栏只显示每个地址族的当天累计合计，不再拆分上传和下载；日期变化时自动清零，并在 TrafficMonitor 配置目录保存当天的累计值。尚未累计流量时显示 `0B`，空闲时保留累计值。UDP 和无法提供 extended statistics 的连接不会被猜测或错误归入另一种协议。

采样只更新内存，累计值或日期有变化时每 60 秒保存一次；正常退出/卸载时补存。写入失败也按该间隔重试。保存先写临时文件，完整写入后替换原文件，保留原有 `.dat` 格式。异常终止或断电可能丢失最近约一分钟未保存的数据；目录不可写时无法保存。

## 构建

需要 Visual Studio 2022（或带 Windows SDK 的 MinGW 工具链）以及 CMake：

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

在 Linux Codespace 中使用 MinGW 和仓库内的工具链文件（CMake 同时编译选项窗口资源）：

```bash
cmake -S . -B build-mingw \
  -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/x86_64-w64-mingw32.cmake
cmake --build build-mingw
```

生成的 `TrafficMonitorIpv4Ipv6.dll` 放到 TrafficMonitor 安装目录的 `plugins` 文件夹，重启程序后在任务栏窗口右键菜单的“显示设置”中勾选 **IPv4/IPv6 流量**。

插件按 TrafficMonitor 的 ABI 导出 `TMPluginGetInstance`，项目 ID 为 `IPv6Traffic`，接口版本为 8。支持独占双行的布局仍使用两行；单行区域自动合并显示。支持字体测量的宿主根据单行示例分配宽度，旧宿主回退到 105px（96 DPI）。

## 自定义显示

打开插件管理，选中插件后点击“选项”，或从插件项目右键菜单打开“选项”。三个格式可分别编辑：

| 设置 | 默认格式 |
| --- | --- |
| Single line（单行） | `{v4} / {v6}` |
| First row（双行上排） | `IPv4 {v4}` |
| Second row（双行下排） | `IPv6 {v6}` |

`{v4}` 和 `{v6}` 会替换成带单位的累计值，其余文字原样显示；可重排或重复占位符。例如 `{v6} | {v4}` 交换顺序，`v4={v4}` 自定义标签。每个格式最多 120 个字符，不支持换行或其他花括号占位符。

G decimal places 控制 G 单位的小数位数（0-4，默认 2）。Defaults 恢复默认格式与精度，OK 保存并应用，Cancel 放弃本次修改。显示设置仅在确认修改时写入配置目录的 `TrafficMonitorIpv4Ipv6.ini`，独立于累计流量记录。悬停提示不受显示格式影响。

## 回归测试

Windows 构建时传入 `-DV4V6_BUILD_TESTS=ON`，构建后运行 `ctest --test-dir build -C Release --output-on-failure`。
Codespace 中安装 Wine 后，可直接运行交叉编译出的 `wine build/windows_regression.exe`。
完整选项窗口测试可用 `xvfb-run -a wine build/windows_regression.exe --dialog` 运行（Windows 上直接运行 `windows_regression.exe --dialog`）。
测试执行实际 GDI 绘制及配置文件读写，覆盖不同字体高度、单/双行切换、两位小数、自定义格式及 Unicode 设置读写、保存限频、正常退出补存、跨日清零及损坏记录。选项窗口测试加载生成的 DLL，自动检查保存、取消和恢复默认。

## 数据范围

Windows 没有一个可直接读取、同时覆盖 TCP/UDP 的按地址族字节计数器。插件选择使用官方 TCP extended statistics，结果是可复现且按 IPv4/IPv6 精确归属的 TCP 流量；README 明确标注这一范围，避免把 IPv4/IPv6 混合接口计数伪装成协议拆分结果。
