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

int main()
{
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
        TrafficItem item(sampler);
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
            assert(drawn == std::vector<std::wstring>{L"4 50M / 6 50M"});
            assert(GetCurrentObject(dc, OBJ_FONT) == scaled_font);
            assert(GetTextColor(dc) == RGB(12, 34, 56));
            drawn.clear();
            item.DrawItem(dc, 0, 0, width, metrics.tmHeight * 2, false);
            assert((drawn == std::vector<std::wstring>{L"4 50M", L"6 50M"}));
            drawn.clear();
            item.DrawItem(dc, 0, 0, 80, metrics.tmHeight, true);
            assert(drawn.size() == 1); // Narrow rectangles still use one line.
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
    assert(RemoveDirectoryW(directory));
    std::cout << "Adaptive drawing and persistence regressions passed\n";
}
