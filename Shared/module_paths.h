#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <cwchar>

namespace bts {
inline constexpr size_t path_capacity = 32768;

// Resolve relative to the DLL itself, never the host process's working directory.
// Fail on truncation; do not silently fall back to a DLL search or relative path.
template<size_t N>
bool path_beside_module(HMODULE module, const wchar_t* leaf, wchar_t (&path)[N]) noexcept
{
    path[0] = L'\0';
    if (!module || !leaf || !*leaf || wcspbrk(leaf, L"\\/:")) return false;
    const DWORD length = GetModuleFileNameW(module, path, static_cast<DWORD>(N));
    if (!length || length >= N) { path[0] = L'\0'; return false; }
    auto slash = wcsrchr(path, L'\\');
    if (!slash || size_t(slash + 1 - path) + wcslen(leaf) >= N) { path[0] = L'\0'; return false; }
    wcscpy_s(slash + 1, N - size_t(slash + 1 - path), leaf);
    return true;
}

inline HMODULE load_beside_module(HMODULE module, const wchar_t* leaf) noexcept
{
    wchar_t path[path_capacity];
    if (!path_beside_module(module, leaf, path)) { SetLastError(ERROR_BAD_PATHNAME); return nullptr; }
    return LoadLibraryExW(path, nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
}
} // namespace bts
