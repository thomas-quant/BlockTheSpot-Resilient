#pragma once
#include <cstdint>
#include "cef_offsets.h"

template<typename T>
inline T get_funct_t(void* base, size_t offset)
{
    return *reinterpret_cast<T*>(
        reinterpret_cast<uintptr_t>(base) + offset
        );
}

// Reads the function pointer at base+offset, but only returns it if it points
// inside libcef.dll's mapped image. If the offset is wrong for the running CEF
// version, the value there won't land in libcef and we return nullptr so the
// caller can skip instead of calling through a bad pointer (which crashes).
// If the module range is unknown, we fall back to the raw read.
template<typename T>
inline T get_funct_guarded(void* base, size_t offset)
{
    T fp = *reinterpret_cast<T*>(
        reinterpret_cast<uintptr_t>(base) + offset
        );
    if (g_libcef_range.valid) {
        const auto p = reinterpret_cast<uintptr_t>(fp);
        if (p < g_libcef_range.base || p >= g_libcef_range.base + g_libcef_range.size) {
            return nullptr;
        }
    }
    return fp;
}

template<typename T>
inline void overwrite_funct_t(void* base, size_t offset, T replacement)
{
    auto slot = reinterpret_cast<void**>(
        reinterpret_cast<uintptr_t>(base) + offset
        );
    DWORD old;
    VirtualProtect(slot, sizeof(void*), PAGE_EXECUTE_READWRITE, &old);
    *slot = reinterpret_cast<void*>(replacement);
    VirtualProtect(slot, sizeof(void*), old, &old);
}