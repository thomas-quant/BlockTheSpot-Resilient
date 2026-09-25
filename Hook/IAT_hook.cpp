#include "pch.h"
#include "IAT_hook.h"
#include "WinTrust_hook.h"
#include "libcef_hook.h"

static FARPROC WINAPI GetProcAddress_hook(HMODULE module, LPCSTR name)
{
    if (!name || IS_INTRESOURCE(name)) return GetProcAddress_orig(module, name);
    if (!strcmp(name, "WinVerifyTrust") && module == GetModuleHandleW(L"WinTrust.dll"))
        return reinterpret_cast<FARPROC>(WinVerifyTrust_hook);
    if (const auto hook = cef_hook_for_proc(module, name)) return hook;
    return GetProcAddress_orig(module, name);
}

imports::result process_IAT_hook_GetProcAddress(HMODULE module) noexcept
{
    // Match the symbol, not kernel32.dll: newer Spotify uses API-set imports.
    // Keep our own unhooked import as the original, never a previously patched
    // slot from another module (which could recurse back into this hook).
    return imports::patch(module, nullptr, "GetProcAddress",
        reinterpret_cast<FARPROC>(GetProcAddress_hook));
}
