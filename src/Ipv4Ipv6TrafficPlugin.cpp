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
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <limits>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

#include "PluginInterface.h"
#include "TrafficSampler.h"
#include "DisplayOptions.h"

namespace
{
using EstatsData = TCP_ESTATS_DATA_ROD_v0;

struct PersistedTotals
{
    wchar_t day[16]{};
    std::uint64_t ipv4{};
    std::uint64_t ipv6{};
};

std::uint64_t Delta(std::uint64_t current, std::uint64_t previous)
{
    return current >= previous ? current - previous : current;
}

std::uint64_t SaturatingAdd(std::uint64_t left, std::uint64_t right)
{
    return right > std::numeric_limits<std::uint64_t>::max() - left
        ? std::numeric_limits<std::uint64_t>::max()
        : left + right;
}

std::wstring CurrentDay()
{
    SYSTEMTIME now{};
    GetLocalTime(&now);
    wchar_t day[16]{};
    swprintf_s(day, L"%04u-%02u-%02u", now.wYear, now.wMonth, now.wDay);
    return day;
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
    // Extended statistics are disabled by default on many Windows versions.
    // Enabling collection is harmless for an already-enabled connection.
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

std::wstring FormatBytes(std::uint64_t bytes, int gb_decimals = 2)
{
    // A one-letter unit keeps the daily totals narrow enough for a taskbar column.
    static constexpr const wchar_t* units[] = {L"B", L"K", L"M", L"G", L"T"};
    double value = static_cast<double>(bytes);
    std::size_t unit = 0;
    while (value >= 1024.0 && unit + 1 < std::size(units))
    {
        value /= 1024.0;
        ++unit;
    }
    std::wostringstream output;
    if (unit == 0)
        output << static_cast<std::uint64_t>(value);
    else
        output << std::fixed << std::setprecision(unit == 3 ? gb_decimals : (value < 10.0 ? 1 : 0)) << value;
    output << units[unit];
    return output.str();
}

class TrafficItem final : public IPluginItem
{
public:
    TrafficItem(TrafficSampler& sampler, const DisplayOptions& options) : m_sampler(sampler), m_options(options) {}

    const wchar_t* GetItemName() const override { return L"IPv4/IPv6 daily traffic"; }
    const wchar_t* GetItemId() const override { return L"IPv6Traffic"; }
    const wchar_t* GetItemLableText() const override { return L""; }
    const wchar_t* GetItemValueText() const override { RefreshText(); return m_value.c_str(); }
    const wchar_t* GetItemValueSampleText() const override
    {
        const auto sample = FormatBytes(1024ULL * 1024 * 1024 * 1024 - 1, m_options.gb_decimals);
        m_sample = ExpandDisplayFormat(m_options.single, sample, sample);
        return m_sample.c_str();
    }
    bool IsCustomDraw() const override { return true; }
    int GetItemWidth() const override { return 105; }
    int IsDoubleLineExclusive() const override { return 1; }

    int GetItemWidthEx(void* hdc) const override
    {
        SIZE size{};
        HDC dc = static_cast<HDC>(hdc);
        const auto* sample_text = GetItemValueSampleText();
        if (!dc || !GetTextExtentPoint32W(dc, sample_text,
                static_cast<int>(wcslen(sample_text)), &size))
            return 0;
        const auto sample = FormatBytes(1024ULL * 1024 * 1024 * 1024 - 1, m_options.gb_decimals);
        for (const auto* format : {&m_options.first, &m_options.second})
        {
            const auto row = ExpandDisplayFormat(*format, sample, sample);
            SIZE row_size{};
            GetTextExtentPoint32W(dc, row.c_str(), static_cast<int>(row.size()), &row_size);
            size.cx = std::max(size.cx, row_size.cx);
        }
        return size.cx + MulDiv(4, GetDeviceCaps(dc, LOGPIXELSX), 96);
    }

    void DrawItem(void* hdc, int x, int y, int width, int height, bool dark_mode) override
    {
        HDC dc = static_cast<HDC>(hdc);
        if (!dc || width <= 0 || height <= 0)
            return;
        const int saved = SaveDC(dc);
        if (!saved)
            return;
        IntersectClipRect(dc, x, y, x + width, y + height);
        RECT rect{x, y, x + width, y + height};
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, dark_mode ? RGB(235, 235, 235) : RGB(35, 35, 35));
        const int row_height = std::max(1, height / 2);
        RECT first{rect.left, rect.top, rect.right, rect.top + row_height};
        RECT second{rect.left, rect.top + row_height, rect.right, rect.bottom};
        const auto& v4 = m_sampler.IPv4();
        const auto& v6 = m_sampler.IPv6();
        const auto v4_value = FormatBytes(v4.today_bytes, m_options.gb_decimals);
        const auto v6_value = FormatBytes(v6.today_bytes, m_options.gb_decimals);
        const auto v4_text = ExpandDisplayFormat(m_options.first, v4_value, v6_value);
        const auto v6_text = ExpandDisplayFormat(m_options.second, v4_value, v6_value);
        TEXTMETRICW metrics{};
        const bool two_rows = GetTextMetricsW(dc, &metrics) && height >= 2 * metrics.tmHeight;
        const auto line = ExpandDisplayFormat(m_options.single, v4_value, v6_value);
        // Older hosts may allocate less width than requested. Fit within that
        // rectangle without drawing into neighbouring taskbar items.
        SIZE extent{};
        GetTextExtentPoint32W(dc, line.c_str(), static_cast<int>(line.size()), &extent);
        if (two_rows)
        {
            SIZE v4_size{}, v6_size{};
            GetTextExtentPoint32W(dc, v4_text.c_str(), static_cast<int>(v4_text.size()), &v4_size);
            GetTextExtentPoint32W(dc, v6_text.c_str(), static_cast<int>(v6_text.size()), &v6_size);
            extent.cx = std::max(v4_size.cx, v6_size.cx);
        }
        HFONT fitted = nullptr;
        if (extent.cx > width || metrics.tmHeight > (two_rows ? height / 2 : height))
        {
            LOGFONTW font{};
            if (GetObjectW(GetCurrentObject(dc, OBJ_FONT), sizeof(font), &font))
            {
                const double scale = std::min({1.0, double(width) / std::max<LONG>(1, extent.cx),
                    double(two_rows ? height / 2 : height) / std::max<LONG>(1, metrics.tmHeight)});
                font.lfHeight = -std::max(1, static_cast<int>(std::abs(font.lfHeight) * scale));
                fitted = CreateFontIndirectW(&font);
                if (fitted)
                    SelectObject(dc, fitted);
            }
        }
        constexpr UINT flags = DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX;
        if (two_rows)
        {
            DrawTextW(dc, v4_text.c_str(), -1, &first, flags);
            DrawTextW(dc, v6_text.c_str(), -1, &second, flags);
        }
        else
            DrawTextW(dc, line.c_str(), -1, &rect, flags);
        RestoreDC(dc, saved);
        if (fitted)
            DeleteObject(fitted);
    }

    void RefreshText() const
    {
        const auto& v4 = m_sampler.IPv4();
        const auto& v6 = m_sampler.IPv6();
        m_value = ExpandDisplayFormat(m_options.single, FormatBytes(v4.today_bytes, m_options.gb_decimals),
            FormatBytes(v6.today_bytes, m_options.gb_decimals));
    }

private:
    TrafficSampler& m_sampler;
    const DisplayOptions& m_options;
    mutable std::wstring m_value;
    mutable std::wstring m_sample;
};

class TrafficPlugin final : public ITMPlugin
{
public:
    TrafficPlugin() : m_item(m_sampler, m_options) {}

    IPluginItem* GetItem(int index) override { return index == 0 ? &m_item : nullptr; }
    void DataRequired() override
    {
        m_sampler.Sample();
    }
    const wchar_t* GetInfo(PluginInfoIndex index) override
    {
        switch (index)
        {
        case TMI_NAME: return L"IPv4/IPv6 Traffic";
        case TMI_DESCRIPTION: return L"Shows today's IPv4 and IPv6 TCP totals in the taskbar.";
        case TMI_AUTHOR: return L"TrafficMonitor IPv4/IPv6 contributors";
        case TMI_COPYRIGHT: return L"Copyright (C) 2026";
        case TMI_VERSION: return L"1.0.0";
        case TMI_URL: return L"https://github.com/zhongyang219/TrafficMonitor";
        default: return L"";
        }
    }
    const wchar_t* GetTooltipInfo() override
    {
        m_tooltip = L"IPv4 " + FormatBytes(m_sampler.IPv4().today_bytes, m_options.gb_decimals) +
            L" | IPv6 " + FormatBytes(m_sampler.IPv6().today_bytes, m_options.gb_decimals) + L" (today, TCP total)";
        return m_tooltip.c_str();
    }
    OptionReturn ShowOptionsDialog(void* parent) override
    {
        return EditDisplayOptions(parent, m_config_dir, m_options) ? OR_OPTION_CHANGED : OR_OPTION_UNCHANGED;
    }
    void OnExtenedInfo(ExtendedInfoIndex index, const wchar_t* data) override
    {
        if (index == EI_CONFIG_DIR && data != nullptr)
        {
            m_sampler.SetConfigDir(data);
            m_config_dir = data;
            m_options = LoadDisplayOptions(m_config_dir);
        }
    }

private:
    TrafficSampler m_sampler;
    DisplayOptions m_options;
    std::wstring m_config_dir;
    TrafficItem m_item;
    std::wstring m_tooltip;
};

TrafficPlugin g_plugin;
}

void TrafficSampler::Sample()
{
    const auto old_day = m_day;
    const auto old_v4 = m_ipv4.today_bytes;
    const auto old_v6 = m_ipv6.today_bytes;
    const auto day = CurrentDay();
    if (m_day.empty())
        m_day = day;
    else if (m_day != day)
    {
        m_day = day;
        m_ipv4 = {};
        m_ipv6 = {};
        m_v4_previous.clear();
        m_v6_previous.clear();
    }

    CounterMap current_v4, current_v6;
    const auto v4_bytes = SampleV4(current_v4);
    const auto v6_bytes = SampleV6(current_v6);
    m_ipv4.today_bytes = SaturatingAdd(m_ipv4.today_bytes, SaturatingAdd(v4_bytes.in, v4_bytes.out));
    m_ipv6.today_bytes = SaturatingAdd(m_ipv6.today_bytes, SaturatingAdd(v6_bytes.in, v6_bytes.out));
    m_v4_previous.swap(current_v4);
    m_v6_previous.swap(current_v6);
    m_dirty = m_dirty || old_day != m_day || old_v4 != m_ipv4.today_bytes || old_v6 != m_ipv6.today_bytes;
    SaveTotals();
}

TrafficSampler::~TrafficSampler()
{
    SaveTotals(true);
}

void TrafficSampler::SetConfigDir(const wchar_t* config_dir)
{
    if (config_dir == nullptr || *config_dir == L'\0' || m_config_dir == config_dir)
        return;
    SaveTotals(true);
    m_config_dir = config_dir;
    m_last_save_attempt = GetTickCount64();
    LoadTotals();
}

void TrafficSampler::LoadTotals()
{
    if (m_config_dir.empty())
        return;
    const auto path = m_config_dir + L"\\TrafficMonitorIpv4Ipv6.dat";
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
        FILE_ATTRIBUTE_HIDDEN, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return;
    PersistedTotals totals{};
    DWORD read = 0;
    const bool ok = ReadFile(file, &totals, sizeof(totals), &read, nullptr) != FALSE && read == sizeof(totals);
    CloseHandle(file);
    if (ok && totals.day[10] == L'\0' && std::wstring(totals.day, 10) == CurrentDay())
    {
        m_day = totals.day;
        m_ipv4.today_bytes = totals.ipv4;
        m_ipv6.today_bytes = totals.ipv6;
        m_dirty = false;
    }
}

void TrafficSampler::SaveTotals(bool force)
{
    if (!m_dirty || m_config_dir.empty() || m_day.empty())
        return;
    const auto now = GetTickCount64();
    if (!force && now - m_last_save_attempt < 60000)
        return;
    // Throttle failed attempts too (for example, a read-only config directory).
    m_last_save_attempt = now;
    const auto path = m_config_dir + L"\\TrafficMonitorIpv4Ipv6.dat";
    const auto temporary = path + L".tmp";
    HANDLE file = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
        FILE_ATTRIBUTE_HIDDEN, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return;
    PersistedTotals totals{};
    wcsncpy_s(totals.day, std::size(totals.day), m_day.c_str(), _TRUNCATE);
    totals.ipv4 = m_ipv4.today_bytes;
    totals.ipv6 = m_ipv6.today_bytes;
    DWORD written = 0;
    const bool ok = WriteFile(file, &totals, sizeof(totals), &written, nullptr) &&
        written == sizeof(totals) && FlushFileBuffers(file);
    CloseHandle(file);
    if (ok && MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        m_dirty = false;
    else
        DeleteFileW(temporary.c_str());
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
