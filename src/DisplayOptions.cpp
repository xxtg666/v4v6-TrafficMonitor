#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include "DisplayOptions.h"
#include "resource.h"

bool ValidDisplayFormat(const std::wstring& format)
{
    if (format.empty() || format.size() > 120)
        return false;
    for (std::size_t i = 0; i < format.size(); ++i)
    {
        if (format[i] < L' ' || format[i] == L'}')
            return false;
        if (format[i] == L'{')
        {
            if (format.compare(i, 4, L"{v4}") != 0 && format.compare(i, 4, L"{v6}") != 0)
                return false;
            i += 3;
        }
    }
    return format.find_first_not_of(L' ') != std::wstring::npos;
}

std::wstring ExpandDisplayFormat(const std::wstring& format, const std::wstring& v4, const std::wstring& v6)
{
    std::wstring result;
    for (std::size_t i = 0; i < format.size();)
    {
        if (format.compare(i, 4, L"{v4}") == 0)
        {
            result += v4;
            i += 4;
        }
        else if (format.compare(i, 4, L"{v6}") == 0)
        {
            result += v6;
            i += 4;
        }
        else
            result += format[i++];
    }
    return result;
}

DisplayOptions LoadDisplayOptions(const std::wstring& directory)
{
    DisplayOptions options;
    if (directory.empty())
        return options;
    const auto path = directory + L"\\TrafficMonitorIpv4Ipv6.ini";
    auto read = [&](const wchar_t* key, std::wstring& value)
    {
        wchar_t buffer[256]{};
        GetPrivateProfileStringW(L"Display", key, value.c_str(), buffer, 256, path.c_str());
        if (ValidDisplayFormat(buffer))
            value = buffer;
    };
    read(L"SingleLine", options.single);
    read(L"FirstRow", options.first);
    read(L"SecondRow", options.second);
    const auto decimals = GetPrivateProfileIntW(L"Display", L"GBDecimals", 2, path.c_str());
    if (decimals <= 4)
        options.gb_decimals = static_cast<int>(decimals);
    return options;
}

bool SaveDisplayOptions(const std::wstring& directory, const DisplayOptions& options)
{
    if (directory.empty() || !ValidDisplayFormat(options.single) || !ValidDisplayFormat(options.first) ||
        !ValidDisplayFormat(options.second) || options.gb_decimals < 0 || options.gb_decimals > 4)
        return false;
    const auto path = directory + L"\\TrafficMonitorIpv4Ipv6.ini";
    const auto temporary = path + L".tmp";
    HANDLE file = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return false;
    // The profile API preserves arbitrary Unicode formats when the INI has a BOM.
    const wchar_t bom = 0xfeff;
    DWORD written{};
    bool ok = WriteFile(file, &bom, sizeof(bom), &written, nullptr) && written == sizeof(bom);
    CloseHandle(file);
    // Quote the whole value so profile reads preserve surrounding spaces/quotes.
    ok = ok && WritePrivateProfileStringW(L"Display", L"SingleLine", (L"\"" + options.single + L"\"").c_str(), temporary.c_str()) &&
        WritePrivateProfileStringW(L"Display", L"FirstRow", (L"\"" + options.first + L"\"").c_str(), temporary.c_str()) &&
        WritePrivateProfileStringW(L"Display", L"SecondRow", (L"\"" + options.second + L"\"").c_str(), temporary.c_str()) &&
        WritePrivateProfileStringW(L"Display", L"GBDecimals", std::to_wstring(options.gb_decimals).c_str(), temporary.c_str());
    WritePrivateProfileStringW(nullptr, nullptr, nullptr, temporary.c_str());
    if (ok && MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        return true;
    DeleteFileW(temporary.c_str());
    return false;
}

namespace
{
struct DialogState
{
    DisplayOptions options;
    std::wstring directory;
};

std::wstring ReadControl(HWND window, int id)
{
    wchar_t value[256]{};
    GetDlgItemTextW(window, id, value, 256);
    return value;
}

void SetControls(HWND window, const DisplayOptions& options)
{
    SetDlgItemTextW(window, IDC_SINGLE, options.single.c_str());
    SetDlgItemTextW(window, IDC_FIRST, options.first.c_str());
    SetDlgItemTextW(window, IDC_SECOND, options.second.c_str());
    SetDlgItemInt(window, IDC_DECIMALS, options.gb_decimals, FALSE);
}

INT_PTR CALLBACK OptionsProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
    auto* state = reinterpret_cast<DialogState*>(GetWindowLongPtrW(window, DWLP_USER));
    if (message == WM_INITDIALOG)
    {
        state = reinterpret_cast<DialogState*>(lparam);
        SetWindowLongPtrW(window, DWLP_USER, lparam);
        for (int id : {IDC_SINGLE, IDC_FIRST, IDC_SECOND})
            SendDlgItemMessageW(window, id, EM_SETLIMITTEXT, 120, 0);
        SendDlgItemMessageW(window, IDC_DECIMALS, EM_SETLIMITTEXT, 1, 0);
        SetControls(window, state->options);
        return TRUE;
    }
    if (message == WM_COMMAND && state)
    {
        if (LOWORD(wparam) == IDC_DEFAULTS)
        {
            SetControls(window, DisplayOptions{});
            return TRUE;
        }
        if (LOWORD(wparam) == IDOK)
        {
            DisplayOptions candidate;
            candidate.single = ReadControl(window, IDC_SINGLE);
            candidate.first = ReadControl(window, IDC_FIRST);
            candidate.second = ReadControl(window, IDC_SECOND);
            BOOL valid_number{};
            candidate.gb_decimals = GetDlgItemInt(window, IDC_DECIMALS, &valid_number, FALSE);
            if (!ValidDisplayFormat(candidate.single) || !ValidDisplayFormat(candidate.first) ||
                !ValidDisplayFormat(candidate.second) || !valid_number || candidate.gb_decimals > 4)
            {
                MessageBoxW(window, L"Use {v4} and {v6} for traffic values, with text around them.\nFormats must contain 1-120 characters. G decimal places must be 0-4.", L"Invalid display format", MB_OK | MB_ICONWARNING);
                return TRUE;
            }
            if (candidate.single == state->options.single && candidate.first == state->options.first &&
                candidate.second == state->options.second && candidate.gb_decimals == state->options.gb_decimals)
                EndDialog(window, IDCANCEL);
            else if (SaveDisplayOptions(state->directory, candidate))
            {
                state->options = candidate;
                EndDialog(window, IDOK);
            }
            else
                MessageBoxW(window, L"Could not save display settings to the plugin configuration directory.", L"IPv4/IPv6 Traffic", MB_OK | MB_ICONERROR);
            return TRUE;
        }
        if (LOWORD(wparam) == IDCANCEL)
        {
            EndDialog(window, IDCANCEL);
            return TRUE;
        }
    }
    return FALSE;
}
}

bool EditDisplayOptions(void* parent, const std::wstring& directory, DisplayOptions& options)
{
    static const int module_anchor = 0;
    HMODULE module{};
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(&module_anchor), &module))
        return false;
    DialogState state{options, directory};
    const auto result = DialogBoxParamW(module, MAKEINTRESOURCEW(IDD_DISPLAY), static_cast<HWND>(parent), OptionsProc, reinterpret_cast<LPARAM>(&state));
    if (result == -1)
        MessageBoxW(static_cast<HWND>(parent), L"Could not open display settings.", L"IPv4/IPv6 Traffic", MB_OK | MB_ICONERROR);
    if (result != IDOK)
        return false;
    options = state.options;
    return true;
}
