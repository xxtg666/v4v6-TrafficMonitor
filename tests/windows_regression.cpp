#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include <windows.h>
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <limits>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>
#include <iostream>

// Exercise the actual persistence and drawing code with a controlled clock.
#define private public
#include "../src/TrafficSampler.h"
#undef private
static ULONGLONG ticks = 100000;
static ULONGLONG TestTicks() { return ticks; }
static std::vector<std::wstring> drawn;
static int TestDrawText(HDC dc, LPCWSTR text, int count, LPRECT rect, UINT flags)
{
    drawn.emplace_back(text);
    TEXTMETRICW metrics{};
    GetTextMetricsW(dc, &metrics);
    assert(metrics.tmHeight <= rect->bottom - rect->top);
    return DrawTextW(dc, text, count, rect, flags);
}
#define GetTickCount64 TestTicks
#define DrawTextW TestDrawText
#include "../src/Ipv4Ipv6TrafficPlugin.cpp"
#undef DrawTextW
#undef GetTickCount64
#include "../src/resource.h"

static int dialog_action{};
static int timer_polls{};
static void CALLBACK DriveOptions(HWND, UINT, UINT_PTR timer, DWORD)
{
    assert(++timer_polls < 200);
    HWND dialog = GetActiveWindow();
    if (!dialog || !GetDlgItem(dialog, IDC_SINGLE))
        return;
    KillTimer(nullptr, timer);
    if (dialog_action == 2)
        SendMessageW(dialog, WM_COMMAND, IDC_DEFAULTS, 0);
    else
    {
        SetDlgItemTextW(dialog, IDC_SINGLE, dialog_action == 1 ? L"Discard this edit {v4}" : L"{v6} :: {v4}");
        SetDlgItemTextW(dialog, IDC_FIRST, L"v4={v4}");
        SetDlgItemTextW(dialog, IDC_SECOND, L"v6={v6}");
        SetDlgItemInt(dialog, IDC_DECIMALS, 4, FALSE);
    }
    PostMessageW(dialog, WM_COMMAND, dialog_action == 1 ? IDCANCEL : IDOK, 0);
}

int main(int argc, char** argv)
{
    constexpr std::uint64_t gib = 1024ULL * 1024 * 1024;
    assert(FormatBytes(gib) == L"1.00G");
    assert(FormatBytes(25 * gib / 2) == L"12.50G");
    assert(FormatBytes(999 * gib) == L"999.00G");
    assert(FormatBytes(5 * gib / 4, 3) == L"1.250G");
    assert(FormatBytes(50 * 1024 * 1024) == L"50M");
    assert(ValidDisplayFormat(L"IPv4 {v4} / IPv6 {v6}"));
    assert(!ValidDisplayFormat(L"{unknown}"));
    assert(!ValidDisplayFormat(L"{v4"));
    assert(!ValidDisplayFormat(L"{v4}\n{v6}"));
    assert(!ValidDisplayFormat(L"   "));
    assert(!ValidDisplayFormat(std::wstring(121, L'x')));
    assert(ExpandDisplayFormat(L"{v6} | {v4} | {v6}", L"1.25G", L"50M") == L"50M | 1.25G | 50M");
    wchar_t temp[MAX_PATH]{}, directory[MAX_PATH]{};
    assert(GetTempPathW(MAX_PATH, temp));
    assert(GetTempFileNameW(temp, L"v46", 0, directory));
    assert(DeleteFileW(directory));
    assert(CreateDirectoryW(directory, nullptr));
    const std::wstring path = std::wstring(directory) + L"\\TrafficMonitorIpv4Ipv6.dat";
    {
        TrafficSampler sampler;
        sampler.SetConfigDir(directory);
        sampler.m_day = CurrentDay();
        sampler.m_ipv4.today_bytes = 50 * 1024 * 1024;
        sampler.m_ipv6.today_bytes = 50 * 1024 * 1024;
        sampler.m_dirty = true;
        for (int i = 0; i < 59; ++i)
        {
            ticks += 1000;
            sampler.SaveTotals();
            assert(GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES);
        }
        ticks += 1000;
        sampler.SaveTotals();
        assert(!sampler.m_dirty);
        assert(GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES);
        TrafficSampler restored;
        restored.SetConfigDir(directory);
        assert(restored.IPv4().today_bytes == 50 * 1024 * 1024);
        assert(restored.IPv6().today_bytes == 50 * 1024 * 1024);
        const auto last_save = sampler.m_last_save_attempt;
        ticks += 60000;
        sampler.SaveTotals();
        assert(sampler.m_last_save_attempt == last_save); // No writes when idle.

        HDC dc = CreateCompatibleDC(nullptr);
        HBITMAP bitmap = CreateBitmap(400, 120, 1, 32, nullptr);
        auto old_bitmap = SelectObject(dc, bitmap);
        HFONT font = CreateFontW(-14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, 0, 0, 0, 0, L"Arial");
        auto old_font = SelectObject(dc, font);
        SetTextColor(dc, RGB(12, 34, 56));
        DisplayOptions options;
        TrafficItem item(sampler, options);
        for (int font_height : {14, 21, 28})
        {
            LOGFONTW logfont{};
            GetObjectW(font, sizeof(logfont), &logfont);
            logfont.lfHeight = -font_height;
            auto scaled_font = CreateFontIndirectW(&logfont);
            SelectObject(dc, scaled_font);
            TEXTMETRICW metrics{};
            GetTextMetricsW(dc, &metrics);
            const int width = item.GetItemWidthEx(dc);
            drawn.clear();
            item.DrawItem(dc, 0, 0, width, metrics.tmHeight + 2, true);
            assert(drawn == std::vector<std::wstring>{L"50M / 50M"});
            assert(GetCurrentObject(dc, OBJ_FONT) == scaled_font);
            assert(GetTextColor(dc) == RGB(12, 34, 56));
            drawn.clear();
            item.DrawItem(dc, 0, 0, width, metrics.tmHeight * 2, false);
            assert((drawn == std::vector<std::wstring>{L"IPv4 50M", L"IPv6 50M"}));
            drawn.clear();
            item.DrawItem(dc, 0, 0, 80, metrics.tmHeight, true);
            assert(drawn.size() == 1); // Narrow rectangles still use one line.
            sampler.m_ipv4.today_bytes = gib;
            sampler.m_ipv6.today_bytes = 25 * gib / 2;
            options.single = L"{v6} + {v4}";
            drawn.clear();
            item.DrawItem(dc, 0, 0, item.GetItemWidthEx(dc), metrics.tmHeight, true);
            assert(drawn == std::vector<std::wstring>{L"12.50G + 1.00G"});
            assert(std::wstring(item.GetItemValueText()) == drawn.front());
            options.first = L"Top: {v6}";
            options.second = L"Bottom: {v4}";
            drawn.clear();
            item.DrawItem(dc, 0, 0, item.GetItemWidthEx(dc), metrics.tmHeight * 2, false);
            assert((drawn == std::vector<std::wstring>{L"Top: 12.50G", L"Bottom: 1.00G"}));
            options = DisplayOptions{};
            sampler.m_ipv4.today_bytes = sampler.m_ipv6.today_bytes = 50 * 1024 * 1024;
            SelectObject(dc, font);
            DeleteObject(scaled_font);
        }
        SelectObject(dc, old_font);
        SelectObject(dc, old_bitmap);
        DeleteObject(font);
        DeleteObject(bitmap);
        DeleteDC(dc);

        sampler.m_ipv4.today_bytes = 1234;
        sampler.m_dirty = true;
        sampler.SaveTotals();
        assert(!sampler.m_dirty);
        sampler.m_ipv4.today_bytes = 5678;
        sampler.m_dirty = true;
        ticks += 1000;
        sampler.SaveTotals();
        assert(sampler.m_dirty); // Must wait for next interval.
    } // Normal unload flushes the final dirty sample.
    {
        TrafficSampler restored;
        restored.SetConfigDir(directory);
        assert(restored.IPv4().today_bytes == 5678);
        restored.m_day = L"2000-01-01";
        restored.Sample();
        assert(restored.IPv4().today_bytes == 0);
        assert(restored.IPv6().today_bytes == 0);
    }
    {
        // Corrupt fixed-width dates must never cause an unbounded string read.
        PersistedTotals bad{};
        std::fill(std::begin(bad.day), std::end(bad.day), L'X');
        HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_HIDDEN, nullptr);
        DWORD written{};
        assert(WriteFile(file, &bad, sizeof(bad), &written, nullptr));
        CloseHandle(file);
        TrafficSampler restored;
        restored.SetConfigDir(directory);
        assert(restored.m_day.empty());
        restored.m_day = CurrentDay();
        restored.m_dirty = true;
        HANDLE locked = CreateFileW(path.c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, 0, nullptr);
        assert(locked != INVALID_HANDLE_VALUE);
        ticks += 60000;
        restored.SaveTotals();
        assert(restored.m_dirty);
        const auto attempted = restored.m_last_save_attempt;
        ticks += 1000;
        restored.SaveTotals();
        assert(restored.m_last_save_attempt == attempted);
        CloseHandle(locked);
    }
    assert(DeleteFileW(path.c_str()));
    {
        DisplayOptions options;
        options.single = L"  \"{v6}\" | {v4}  ";
        options.first = L"\u6d41\u91cf {v4}";
        options.second = L"IPv6={v6}";
        options.gb_decimals = 3;
        assert(SaveDisplayOptions(directory, options));
        const auto loaded = LoadDisplayOptions(directory);
        assert(loaded.single == options.single);
        assert(loaded.first == options.first);
        assert(loaded.second == options.second);
        assert(loaded.gb_decimals == 3);
        options.single = L"{invalid}";
        assert(!SaveDisplayOptions(directory, options));
        assert(LoadDisplayOptions(directory).single == loaded.single);
        TrafficPlugin plugin;
        plugin.OnExtenedInfo(ITMPlugin::EI_CONFIG_DIR, directory);
        assert(std::wstring(plugin.GetTooltipInfo()).find(L"IPv4") != std::wstring::npos);
        assert(std::wstring(plugin.GetTooltipInfo()).find(L"IPv6") != std::wstring::npos);
    }
    if (argc > 1 && std::string(argv[1]) == "--dialog")
    {
        HMODULE dll = LoadLibraryW(L"TrafficMonitorIpv4Ipv6.dll");
        assert(dll);
        using Entry = ITMPlugin* (*)();
        auto entry = reinterpret_cast<Entry>(GetProcAddress(dll, "TMPluginGetInstance"));
        assert(entry);
        auto* plugin = entry();
        plugin->OnExtenedInfo(ITMPlugin::EI_CONFIG_DIR, directory);
        for (dialog_action = 0; dialog_action < 3; ++dialog_action)
        {
            timer_polls = 0;
            assert(SetTimer(nullptr, 0, 50, DriveOptions));
            const auto result = plugin->ShowOptionsDialog(nullptr);
            assert(result == (dialog_action == 1 ? ITMPlugin::OR_OPTION_UNCHANGED : ITMPlugin::OR_OPTION_CHANGED));
            const auto saved = LoadDisplayOptions(directory);
            assert(saved.single == (dialog_action == 2 ? DisplayOptions{}.single : L"{v6} :: {v4}"));
            assert(saved.gb_decimals == (dialog_action == 2 ? 2 : 4));
            const auto text = std::wstring(plugin->GetItem(0)->GetItemValueText());
            assert(text.find(dialog_action == 2 ? L" / " : L" :: ") != std::wstring::npos);
        }
        FreeLibrary(dll);
    }
    assert(DeleteFileW((std::wstring(directory) + L"\\TrafficMonitorIpv4Ipv6.ini").c_str()));
    assert(RemoveDirectoryW(directory));
    std::cout << "Adaptive drawing and persistence regressions passed\n";
}
