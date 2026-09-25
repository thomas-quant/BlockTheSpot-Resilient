#include "pch.h"
#include "WinTrust_hook.h"

LONG verify_spotify_file(HWND window, GUID* action, LPVOID opaque, WinVerifyTrust_t verify)
{
    auto data = static_cast<WINTRUST_DATA*>(opaque);
    if (!data || data->cbStruct < sizeof(WINTRUST_DATA) || data->dwUnionChoice != WTD_CHOICE_FILE ||
        data->dwStateAction == WTD_STATEACTION_CLOSE || !data->pFile ||
        data->pFile->cbStruct < sizeof(WINTRUST_FILE_INFO)) return verify(window, action, opaque);

    const auto source = data->pFile;
    wchar_t path[bts::path_capacity], proxy[bts::path_capacity];
    DWORD length = 0;
    if (source->hFile && source->hFile != INVALID_HANDLE_VALUE)
        length = GetFinalPathNameByHandleW(source->hFile, path, bts::path_capacity, FILE_NAME_NORMALIZED);
    else if (source->pcwszFilePath)
        length = GetFullPathNameW(source->pcwszFilePath, bts::path_capacity, path, nullptr);
    if (!length || length >= bts::path_capacity ||
        !bts::path_beside_module(hook_module, L"chrome_elf.dll", proxy)) return verify(window, action, opaque);
    const auto normalized = !wcsncmp(path, L"\\\\?\\", 4) ? path + 4 : path;
    const auto normalized_proxy = !wcsncmp(proxy, L"\\\\?\\", 4) ? proxy + 4 : proxy;
    if (lstrcmpiW(normalized, normalized_proxy) ||
        GetFileAttributesW(ORIGINAL_CHROME_ELF_DLL) == INVALID_FILE_ATTRIBUTES)
        return verify(window, action, opaque);

    // Redirect only OUR proxy, not every file with the same basename. Use
    // per-call copies instead of mutating a caller's file-info pointer or sharing
    // a permanently open handle across concurrent signature checks.
    WINTRUST_FILE_INFO file = *source;
    file.pcwszFilePath = ORIGINAL_CHROME_ELF_DLL;
    file.hFile = nullptr;
    WINTRUST_DATA redirected = *data;
    redirected.pFile = &file;
    const auto result = verify(window, action, &redirected);
    data->hWVTStateData = redirected.hWVTStateData; // preserve VERIFY/CLOSE lifecycle
    return result;
}

LONG WINAPI WinVerifyTrust_hook(HWND window, GUID* action, LPVOID data)
{
    return verify_spotify_file(window, action, data, ::WinVerifyTrust);
}
