#pragma once
#include "framework.h"
#include <cstddef>
#include <cstdint>
#include <cstring>

// Shared by production hooks and the native regression tests. Only mapped x64
// PE images are supported. Never execute a delay-load thunk to discover its name.
namespace imports {
struct result {
    size_t matched = 0;
    size_t patched = 0;
    size_t errors = 0;
    bool complete() const noexcept { return matched != 0 && matched == patched && errors == 0; }
};

inline bool readable(const void* ptr, size_t bytes) noexcept
{
    auto address = reinterpret_cast<uintptr_t>(ptr);
    if (!address || bytes > UINTPTR_MAX - address) return false;
    const auto end = address + bytes;
    while (address < end) {
        MEMORY_BASIC_INFORMATION info{};
        if (!VirtualQuery(reinterpret_cast<void*>(address), &info, sizeof(info)) ||
            info.State != MEM_COMMIT || (info.Protect & (PAGE_GUARD | PAGE_NOACCESS))) return false;
        const DWORD protection = info.Protect & 0xff;
        if (protection != PAGE_READONLY && protection != PAGE_READWRITE &&
            protection != PAGE_WRITECOPY && protection != PAGE_EXECUTE_READ &&
            protection != PAGE_EXECUTE_READWRITE && protection != PAGE_EXECUTE_WRITECOPY) return false;
        const auto next = reinterpret_cast<uintptr_t>(info.BaseAddress) + info.RegionSize;
        if (next <= address) return false;
        address = next < end ? next : end;
    }
    return true;
}

inline bool replace(void** slot, void* replacement) noexcept
{
    if (reinterpret_cast<uintptr_t>(slot) % alignof(void*) || !readable(slot, sizeof(*slot))) return false;
    if (*slot == replacement) return true; // repeated initialization is harmless
    DWORD old = 0;
    if (!VirtualProtect(slot, sizeof(*slot), PAGE_READWRITE, &old)) return false;
    InterlockedExchangePointer(slot, replacement);
    DWORD unused = 0;
    return VirtualProtect(slot, sizeof(*slot), old, &unused) != FALSE;
}

class image {
    BYTE* base_ = nullptr;
    size_t size_ = 0;
    const IMAGE_NT_HEADERS64* nt_ = nullptr;
public:
    explicit image(HMODULE module) noexcept
    {
        auto base = reinterpret_cast<BYTE*>(module);
        if (!readable(base, sizeof(IMAGE_DOS_HEADER))) return;
        const auto dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
        if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew < sizeof(IMAGE_DOS_HEADER) ||
            dos->e_lfanew > 1024 * 1024) return;
        const auto nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
        if (!readable(nt, sizeof(*nt)) || nt->Signature != IMAGE_NT_SIGNATURE ||
            nt->FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64 ||
            nt->FileHeader.SizeOfOptionalHeader < sizeof(IMAGE_OPTIONAL_HEADER64) ||
            nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC ||
            nt->OptionalHeader.SizeOfImage < size_t(dos->e_lfanew) + sizeof(*nt)) return;
        base_ = base;
        size_ = nt->OptionalHeader.SizeOfImage;
        nt_ = nt;
    }
    bool valid() const noexcept { return nt_ != nullptr; }
    template<typename T> T* at(size_t rva) const noexcept
    {
        if (!base_ || !rva || rva > size_ || sizeof(T) > size_ - rva) return nullptr;
        auto p = reinterpret_cast<T*>(base_ + rva);
        return readable(p, sizeof(T)) ? p : nullptr;
    }
    const char* name(size_t rva) const noexcept
    {
        if (!base_ || !rva || rva >= size_) return nullptr;
        // Import names are short; reject corrupt/unterminated names rather than
        // scanning arbitrary sections or trusting a PE's claimed image size.
        for (size_t i = 0; i < 512 && i < size_ - rva; ++i) {
            const char* p = at<char>(rva + i);
            if (!p) return nullptr;
            if (!*p) return reinterpret_cast<const char*>(base_ + rva);
        }
        return nullptr;
    }
    IMAGE_DATA_DIRECTORY directory(size_t index) const noexcept
    {
        if (!nt_ || index >= IMAGE_NUMBEROF_DIRECTORY_ENTRIES ||
            index >= nt_->OptionalHeader.NumberOfRvaAndSizes) return {};
        return nt_->OptionalHeader.DataDirectory[index];
    }
    bool contains(size_t rva, size_t bytes) const noexcept
    {
        return rva && rva <= size_ && bytes <= size_ - rva;
    }
};

inline void patch_table(const image& pe, DWORD names, DWORD addresses,
    const char* function, FARPROC replacement, result& out) noexcept
{
    if (!names || !addresses) { ++out.errors; return; }
    for (size_t i = 0;; ++i) {
        const auto name = pe.at<IMAGE_THUNK_DATA64>(size_t(names) + i * sizeof(IMAGE_THUNK_DATA64));
        auto address = pe.at<IMAGE_THUNK_DATA64>(size_t(addresses) + i * sizeof(IMAGE_THUNK_DATA64));
        if (!name || !address) { ++out.errors; return; }
        if (!name->u1.AddressOfData) return;
        if (IMAGE_SNAP_BY_ORDINAL64(name->u1.Ordinal)) continue;
        if (name->u1.AddressOfData > MAXDWORD - offsetof(IMAGE_IMPORT_BY_NAME, Name)) {
            ++out.errors; return;
        }
        const auto imported = pe.name(size_t(name->u1.AddressOfData) + offsetof(IMAGE_IMPORT_BY_NAME, Name));
        if (!imported) { ++out.errors; return; }
        if (strcmp(imported, function)) continue;
        ++out.matched;
        if (replace(reinterpret_cast<void**>(&address->u1.Function), reinterpret_cast<void*>(replacement)))
            ++out.patched;
        else ++out.errors;
    }
}

inline result patch(HMODULE module, const char* dll, const char* function, FARPROC replacement) noexcept
{
    result out;
    const image pe(module);
    if (!pe.valid() || !function || !replacement) { ++out.errors; return out; }
    // A null DLL filter matches by symbol, including Windows API-set providers.
    const auto normal = pe.directory(IMAGE_DIRECTORY_ENTRY_IMPORT);
    if (normal.VirtualAddress || normal.Size) {
        bool terminated = false;
        if (pe.contains(normal.VirtualAddress, normal.Size)) {
            for (size_t offset = 0; offset + sizeof(IMAGE_IMPORT_DESCRIPTOR) <= normal.Size;
                offset += sizeof(IMAGE_IMPORT_DESCRIPTOR)) {
                const auto entry = pe.at<IMAGE_IMPORT_DESCRIPTOR>(size_t(normal.VirtualAddress) + offset);
                if (!entry) break;
                if (!entry->Name) { terminated = true; break; }
                const char* imported = pe.name(entry->Name);
                if (!imported) break;
                if (!dll || !lstrcmpiA(imported, dll))
                    patch_table(pe, entry->OriginalFirstThunk, entry->FirstThunk, function, replacement, out);
            }
        }
        if (!terminated) ++out.errors;
    }
    const auto delay = pe.directory(IMAGE_DIRECTORY_ENTRY_DELAY_IMPORT);
    if (delay.VirtualAddress || delay.Size) {
        bool terminated = false;
        if (pe.contains(delay.VirtualAddress, delay.Size)) {
            for (size_t offset = 0; offset + sizeof(IMAGE_DELAYLOAD_DESCRIPTOR) <= delay.Size;
                offset += sizeof(IMAGE_DELAYLOAD_DESCRIPTOR)) {
                const auto entry = pe.at<IMAGE_DELAYLOAD_DESCRIPTOR>(size_t(delay.VirtualAddress) + offset);
                if (!entry) break;
                if (!entry->DllNameRVA) { terminated = true; break; }
                if (entry->Attributes.AllAttributes != 1) { ++out.errors; continue; }
                const char* imported = pe.name(entry->DllNameRVA);
                if (!imported) break;
                if (!dll || !lstrcmpiA(imported, dll))
                    patch_table(pe, entry->ImportNameTableRVA, entry->ImportAddressTableRVA, function, replacement, out);
            }
        }
        if (!terminated) ++out.errors;
    }
    return out;
}
} // namespace imports
