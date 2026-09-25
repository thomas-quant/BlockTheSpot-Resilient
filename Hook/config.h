#pragma once
#include "framework.h"

// INI section/key/value text is UTF-8; the file path stays UTF-16 throughout.
// The A-family Win32 calls cannot open every Windows user's profile directory.
inline bool config_name(const char* text, wchar_t (&wide)[128]) noexcept
{
    return text && MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, -1, wide, 128) != 0;
}
inline UINT config_int(const char* section, const char* key, INT fallback, const wchar_t* file) noexcept
{
    wchar_t wide_section[128], wide_key[128];
    if (!file || !*file || !config_name(section, wide_section) || !config_name(key, wide_key)) return fallback;
    return GetPrivateProfileIntW(wide_section, wide_key, fallback, file);
}
inline DWORD config_string(const char* section, const char* key, const char*, char* output,
    DWORD capacity, const wchar_t* file) noexcept
{
    SetLastError(ERROR_INVALID_DATA);
    if (!output || !capacity) return 0;
    output[0] = '\0';
    wchar_t wide_section[128], wide_key[128], value[1024];
    if (!file || !*file || !config_name(section, wide_section) || !config_name(key, wide_key)) return 0;
    const auto length = GetPrivateProfileStringW(wide_section, wide_key, L"", value, 1024, file);
    if (!length) { SetLastError(ERROR_SUCCESS); return 0; }
    if (length == 1023) { SetLastError(ERROR_INSUFFICIENT_BUFFER); return 0; }
    const int bytes = WideCharToMultiByte(CP_UTF8, 0, value, -1, nullptr, 0, nullptr, nullptr);
    if (!bytes || static_cast<DWORD>(bytes) > capacity) { SetLastError(ERROR_INSUFFICIENT_BUFFER); return 0; }
    if (!WideCharToMultiByte(CP_UTF8, 0, value, -1, output, static_cast<int>(capacity), nullptr, nullptr)) return 0;
    SetLastError(ERROR_SUCCESS);
    return bytes - 1;
}
