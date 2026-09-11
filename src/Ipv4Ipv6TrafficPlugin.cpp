#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <winsock2.h>
#include <windows.h>
#include <iphlpapi.h>
#include <mstcpip.h>
#include <tcpestats.h>
#include <ws2tcpip.h>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

#include "PluginInterface.h"
#include "TrafficSampler.h"

namespace
{
using Clock = std::chrono::steady_clock;
using EstatsData = TCP_ESTATS_DATA_ROD_v0;

std::uint64_t Delta(std::uint64_t current, std::uint64_t previous)
{
    return current >= previous ? current - previous : current;
}

std::uint64_t PerSecond(std::uint64_t bytes, std::chrono::milliseconds elapsed)
{
    const auto millis = std::max<std::int64_t>(1, elapsed.count());
    return static_cast<std::uint64_t>((bytes * 1000ULL) / static_cast<std::uint64_t>(millis));
}

std::wstring V4Address(DWORD address)
{
    IN_ADDR value{};
    value.S_un.S_addr = address;
    wchar_t text[INET_ADDRSTRLEN]{};
    return InetNtopW(AF_INET, &value, text, static_cast<DWORD>(std::size(text))) ? text : L"?";
}

std::wstring V6Address(const IN6_ADDR& address, DWORD scope)
{
    wchar_t text[INET6_ADDRSTRLEN]{};
    std::wstring result = InetNtopW(AF_INET6, &address, text, static_cast<DWORD>(std::size(text))) ? text : L"?";
    if (scope != 0)
        result += L"%" + std::to_wstring(scope);
    return result;
}

std::wstring V4Key(const MIB_TCPROW& row, DWORD pid)
{
    return V4Address(row.dwLocalAddr) + L":" + std::to_wstring(ntohs(static_cast<u_short>(row.dwLocalPort))) + L"-" +
        V4Address(row.dwRemoteAddr) + L":" + std::to_wstring(ntohs(static_cast<u_short>(row.dwRemotePort))) +
        L"#" + std::to_wstring(pid);
}

std::wstring V6Key(const MIB_TCP6ROW& row, DWORD pid)
{
    return V6Address(row.LocalAddr, row.dwLocalScopeId) + L":" +
        std::to_wstring(ntohs(static_cast<u_short>(row.dwLocalPort))) + L"-" +
        V6Address(row.RemoteAddr, row.dwRemoteScopeId) + L":" +
        std::to_wstring(ntohs(static_cast<u_short>(row.dwRemotePort))) + L"#" + std::to_wstring(pid);
}

std::vector<std::byte> GetTable(ULONG family)
{
    ULONG size = 0;
    const auto first = GetExtendedTcpTable(nullptr, &size, FALSE, family, TCP_TABLE_OWNER_PID_ALL, 0);
    if (first != ERROR_INSUFFICIENT_BUFFER || size == 0)
        return {};
    std::vector<std::byte> bytes(size);
    if (GetExtendedTcpTable(bytes.data(), &size, FALSE, family, TCP_TABLE_OWNER_PID_ALL, 0) != NO_ERROR)
        return {};
    return bytes;
}

bool ReadV4(const MIB_TCPROW& row, EstatsData& data)
{
    using SetFn = ULONG(WINAPI*)(PMIB_TCPROW, TCP_ESTATS_TYPE, PUCHAR, ULONG, ULONG, ULONG);
    using GetFn = ULONG(WINAPI*)(PMIB_TCPROW, TCP_ESTATS_TYPE, PUCHAR, ULONG, ULONG, PUCHAR, ULONG, ULONG, PUCHAR, ULONG, ULONG);
    const auto module = GetModuleHandleW(L"iphlpapi.dll");
    const auto set_stats = module ? reinterpret_cast<SetFn>(GetProcAddress(module, "SetPerTcpConnectionEStats")) : nullptr;
    const auto get_stats = module ? reinterpret_cast<GetFn>(GetProcAddress(module, "GetPerTcpConnectionEStats")) : nullptr;
    if (!set_stats || !get_stats)
        return false;
    TCP_ESTATS_DATA_RW_v0 enable{TcpBoolOptEnabled};
    set_stats(const_cast<PMIB_TCPROW>(&row), TcpConnectionEstatsData,
        reinterpret_cast<PUCHAR>(&enable), 0, sizeof(enable), 0);
    return get_stats(const_cast<PMIB_TCPROW>(&row), TcpConnectionEstatsData,
        nullptr, 0, 0, nullptr, 0, 0, reinterpret_cast<PUCHAR>(&data), 0, sizeof(data)) == NO_ERROR;
}

bool ReadV6(const MIB_TCP6ROW& row, EstatsData& data)
{
    using SetFn = ULONG(WINAPI*)(PMIB_TCP6ROW, TCP_ESTATS_TYPE, PUCHAR, ULONG, ULONG, ULONG);
    using GetFn = ULONG(WINAPI*)(PMIB_TCP6ROW, TCP_ESTATS_TYPE, PUCHAR, ULONG, ULONG, PUCHAR, ULONG, ULONG, PUCHAR, ULONG, ULONG);
    const auto module = GetModuleHandleW(L"iphlpapi.dll");
    const auto set_stats = module ? reinterpret_cast<SetFn>(GetProcAddress(module, "SetPerTcp6ConnectionEStats")) : nullptr;
    const auto get_stats = module ? reinterpret_cast<GetFn>(GetProcAddress(module, "GetPerTcp6ConnectionEStats")) : nullptr;
    if (!set_stats || !get_stats)
        return false;
    TCP_ESTATS_DATA_RW_v0 enable{TcpBoolOptEnabled};
    set_stats(const_cast<PMIB_TCP6ROW>(&row), TcpConnectionEstatsData,
        reinterpret_cast<PUCHAR>(&enable), 0, sizeof(enable), 0);
    return get_stats(const_cast<PMIB_TCP6ROW>(&row), TcpConnectionEstatsData,
        nullptr, 0, 0, nullptr, 0, 0, reinterpret_cast<PUCHAR>(&data), 0, sizeof(data)) == NO_ERROR;
}

std::wstring FormatRate(std::uint64_t bytes)
{
    static constexpr const wchar_t* units[] = {L"B/s", L"KB/s", L"MB/s", L"GB/s"};
    double value = static_cast<double>(bytes);
    std::size_t unit = 0;
    while (value >= 1024.0 && unit + 1 < std::size(units))
    {
        value /= 1024.0;
        ++unit;
    }
    std::wostringstream output;
    if (unit == 0)
        output << static_cast<std::uint64_t>(value) << L' ';
    else
        output << std::fixed << std::setprecision(value < 10.0 ? 1 : 0) << value << L' ';
    output << units[unit];
    return output.str();
}

class TrafficItem final : public IPluginItem
{
public:
    explicit TrafficItem(TrafficSampler& sampler) : m_sampler(sampler) {}

    const wchar_t* GetItemName() const override { return L"IPv4/IPv6 traffic"; }
    const wchar_t* GetItemId() const override { return L"IPv6Traffic"; }
    const wchar_t* GetItemLableText() const override { return L""; }
    const wchar_t* GetItemValueText() const override { return m_value.c_str(); }
    const wchar_t* GetItemValueSampleText() const override { return L"IPv4  ↓ 999.9 MB/s ↑ 999.9 MB/s"; }
    bool IsCustomDraw() const override { return true; }
    int GetItemWidth() const override { return 220; }
    int IsDoubleLineExclusive() const override { return 1; }

    void DrawItem(void* hdc, int x, int y, int width, int height, bool dark_mode) override
    {
        HDC dc = static_cast<HDC>(hdc);
        RECT rect{x, y, x + width, y + height};
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, dark_mode ? RGB(235, 235, 235) : RGB(35, 35, 35));
        const int row_height = std::max(1, height / 2);
        RECT first{rect.left, rect.top, rect.right, rect.top + row_height};
        RECT second{rect.left, rect.top + row_height, rect.right, rect.bottom};
        const auto& v4 = m_sampler.IPv4();
        const auto& v6 = m_sampler.IPv6();
        const std::wstring v4_text = L"IPv4  ↓ " + FormatRate(v4.in_bytes_per_second) + L"  ↑ " + FormatRate(v4.out_bytes_per_second);
        const std::wstring v6_text = L"IPv6  ↓ " + FormatRate(v6.in_bytes_per_second) + L"  ↑ " + FormatRate(v6.out_bytes_per_second);
        DrawTextW(dc, v4_text.c_str(), -1, &first, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        DrawTextW(dc, v6_text.c_str(), -1, &second, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    }

    void RefreshText()
    {
        const auto& v4 = m_sampler.IPv4();
        const auto& v6 = m_sampler.IPv6();
        m_value = L"IPv4 " + FormatRate(v4.in_bytes_per_second) + L" / IPv6 " + FormatRate(v6.in_bytes_per_second);
    }

private:
    TrafficSampler& m_sampler;
    std::wstring m_value;
};

class TrafficPlugin final : public ITMPlugin
{
public:
    TrafficPlugin() : m_item(m_sampler) {}

    IPluginItem* GetItem(int index) override { return index == 0 ? &m_item : nullptr; }
    void DataRequired() override
    {
        m_sampler.Sample();
        m_item.RefreshText();
    }
    const wchar_t* GetInfo(PluginInfoIndex index) override
    {
        switch (index)
        {
        case TMI_NAME: return L"IPv4/IPv6 Traffic";
        case TMI_DESCRIPTION: return L"Separates IPv4 and IPv6 TCP traffic in the taskbar.";
        case TMI_AUTHOR: return L"TrafficMonitor IPv4/IPv6 contributors";
        case TMI_COPYRIGHT: return L"Copyright (C) 2026";
        case TMI_VERSION: return L"1.0.0";
        case TMI_URL: return L"https://github.com/zhongyang219/TrafficMonitor";
        default: return L"";
        }
    }
    const wchar_t* GetTooltipInfo() override { return L"IPv4/IPv6 TCP throughput (bytes per second)"; }

private:
    TrafficSampler m_sampler;
    TrafficItem m_item;
};

TrafficPlugin g_plugin;
}

void TrafficSampler::Sample()
{
    const auto now = Clock::now();
    const auto elapsed = m_last_sample.time_since_epoch().count() == 0
        ? std::chrono::milliseconds(1000)
        : std::chrono::duration_cast<std::chrono::milliseconds>(now - m_last_sample);
    m_last_sample = now;

    CounterMap current_v4, current_v6;
    const auto v4_bytes = SampleV4(current_v4);
    const auto v6_bytes = SampleV6(current_v6);
    m_ipv4.in_bytes_per_second = PerSecond(v4_bytes.in, elapsed);
    m_ipv4.out_bytes_per_second = PerSecond(v4_bytes.out, elapsed);
    m_ipv6.in_bytes_per_second = PerSecond(v6_bytes.in, elapsed);
    m_ipv6.out_bytes_per_second = PerSecond(v6_bytes.out, elapsed);
    m_v4_previous.swap(current_v4);
    m_v6_previous.swap(current_v6);
}

TrafficSampler::ByteDelta TrafficSampler::SampleV4(CounterMap& current_connections)
{
    std::uint64_t in_delta = 0, out_delta = 0;
    const auto bytes = GetTable(AF_INET);
    if (bytes.empty()) return {};
    const auto* table = reinterpret_cast<const MIB_TCPTABLE_OWNER_PID*>(bytes.data());
    for (DWORD i = 0; i < table->dwNumEntries; ++i)
    {
        const auto& source = table->table[i];
        MIB_TCPROW row{source.dwState, source.dwLocalAddr, source.dwLocalPort,
            source.dwRemoteAddr, source.dwRemotePort};
        if (row.dwState == 0) continue;
        EstatsData data{};
        if (!ReadV4(row, data)) continue;
        const auto key = V4Key(row, source.dwOwningPid);
        const auto old = m_v4_previous.find(key);
        if (old != m_v4_previous.end())
        {
            in_delta += Delta(data.DataBytesIn, old->second.in);
            out_delta += Delta(data.DataBytesOut, old->second.out);
        }
        current_connections.emplace(key, Previous{data.DataBytesIn, data.DataBytesOut});
    }
    return {in_delta, out_delta};
}

TrafficSampler::ByteDelta TrafficSampler::SampleV6(CounterMap& current_connections)
{
    std::uint64_t in_delta = 0, out_delta = 0;
    const auto bytes = GetTable(AF_INET6);
    if (bytes.empty()) return {};
    const auto* table = reinterpret_cast<const MIB_TCP6TABLE_OWNER_PID*>(bytes.data());
    for (DWORD i = 0; i < table->dwNumEntries; ++i)
    {
        const auto& source = table->table[i];
        MIB_TCP6ROW row{};
        row.State = static_cast<MIB_TCP_STATE>(source.dwState);
        std::memcpy(&row.LocalAddr, source.ucLocalAddr, sizeof(row.LocalAddr));
        row.dwLocalScopeId = source.dwLocalScopeId;
        row.dwLocalPort = source.dwLocalPort;
        std::memcpy(&row.RemoteAddr, source.ucRemoteAddr, sizeof(row.RemoteAddr));
        row.dwRemoteScopeId = source.dwRemoteScopeId;
        row.dwRemotePort = source.dwRemotePort;
        if (row.State == MIB_TCP_STATE_CLOSED) continue;
        EstatsData data{};
        if (!ReadV6(row, data)) continue;
        const auto key = V6Key(row, source.dwOwningPid);
        const auto old = m_v6_previous.find(key);
        if (old != m_v6_previous.end())
        {
            in_delta += Delta(data.DataBytesIn, old->second.in);
            out_delta += Delta(data.DataBytesOut, old->second.out);
        }
        current_connections.emplace(key, Previous{data.DataBytesIn, data.DataBytesOut});
    }
    return {in_delta, out_delta};
}

extern "C" __declspec(dllexport) ITMPlugin* TMPluginGetInstance()
{
    return &g_plugin;
}
