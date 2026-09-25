#include "pch.h"
#include "loader.h"

static INIT_ONCE original_once = INIT_ONCE_STATIC_INIT;
static HMODULE original_chrome = nullptr;

static BOOL CALLBACK initialize_original(PINIT_ONCE, PVOID, PVOID*) noexcept
{
    original_chrome = bts::load_beside_module(loader_module, L"chrome_elf_required.dll");
    return original_chrome != nullptr;
}

bool load_original_chrome() noexcept
{
    return InitOnceExecuteOnce(&original_once, initialize_original, nullptr, nullptr) != FALSE;
}

extern "C" LPVOID WINAPI LoadAPI(const char* name)
{
    // GetProcAddress is already thread-safe; a mutable unordered_map here added
    // races and heap allocation to every forwarded call without needing either.
    if (load_original_chrome()) {
        if (const auto function = GetProcAddress(original_chrome, name))
            return reinterpret_cast<LPVOID>(function);
    }
    // A missing mandatory export has no ABI-safe generic return value. Report a
    // loader mismatch explicitly instead of jumping through a null pointer.
    OutputDebugStringA("BlockTheSpot: required chrome_elf export missing; repair/reinstall Spotify.\n");
    RaiseFailFastException(nullptr, nullptr, 0);
    TerminateProcess(GetCurrentProcess(), ERROR_PROC_NOT_FOUND);
    return nullptr; // unreachable unless Windows' termination APIs fail
}
