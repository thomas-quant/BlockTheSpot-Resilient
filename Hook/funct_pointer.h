#pragma once
#include <cstdint>
#include "cef_offsets.h"
#include "import_hook.h"

struct cef_utf16_string {
    wchar_t* str;
    size_t length;
    void (*dtor)(wchar_t*);
};
using cef_string_free_t = void (*)(cef_utf16_string*);

// CEF ref-counted structs start with their byte size. These checks reject null,
// truncated and inaccessible objects before reading a slot. A pointer inside
// executable libcef memory is necessary, but NOT proof of method identity if a
// future ABI reorders methods. Unknown layouts still need compatibility testing.
template<typename T>
inline T get_funct_guarded(void* base, size_t offset)
{
    if (!g_libcef_range.valid || offset % alignof(void*) || offset < sizeof(size_t) ||
        !imports::readable(base, sizeof(size_t))) return nullptr;
    const auto size = *static_cast<const size_t*>(base);
    if (offset > size || sizeof(T) > size - offset ||
        offset > UINTPTR_MAX - reinterpret_cast<uintptr_t>(base)) return nullptr;
    auto slot = reinterpret_cast<T*>(reinterpret_cast<uintptr_t>(base) + offset);
    if (!imports::readable(slot, sizeof(*slot))) return nullptr;
    T fp = *slot;
    const auto address = reinterpret_cast<uintptr_t>(fp);
    if (address < g_libcef_range.base || address - g_libcef_range.base >= g_libcef_range.size) return nullptr;
    MEMORY_BASIC_INFORMATION info{};
    if (!VirtualQuery(reinterpret_cast<void*>(address), &info, sizeof(info)) ||
        info.State != MEM_COMMIT || (info.Protect & (PAGE_GUARD | PAGE_NOACCESS))) return nullptr;
    const DWORD protection = info.Protect & 0xff;
    return protection == PAGE_EXECUTE || protection == PAGE_EXECUTE_READ ||
        protection == PAGE_EXECUTE_READWRITE || protection == PAGE_EXECUTE_WRITECOPY ? fp : nullptr;
}

template<typename T>
inline bool overwrite_funct_t(void* base, size_t offset, T replacement)
{
    if (!get_funct_guarded<T>(base, offset)) return false;
    return imports::replace(reinterpret_cast<void**>(reinterpret_cast<uintptr_t>(base) + offset),
        reinterpret_cast<void*>(replacement));
}
