#pragma once

#include <string>

struct DisplayOptions
{
    std::wstring single{L"{v4} / {v6}"};
    std::wstring first{L"IPv4 {v4}"};
    std::wstring second{L"IPv6 {v6}"};
    int gb_decimals{2};
};

bool ValidDisplayFormat(const std::wstring& format);
std::wstring ExpandDisplayFormat(const std::wstring& format, const std::wstring& v4, const std::wstring& v6);
DisplayOptions LoadDisplayOptions(const std::wstring& directory);
bool SaveDisplayOptions(const std::wstring& directory, const DisplayOptions& options);
bool EditDisplayOptions(void* parent, const std::wstring& directory, DisplayOptions& options);
