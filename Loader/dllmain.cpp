#include "pch.h"
#include "loader.h"

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(module);
        loader_module = module;
        if (!load_original_chrome()) return FALSE;
        // The payload is optional: a missing/broken ad blocker must not prevent
        // forwarding the genuine Chromium API. No working-directory search.
        if (!bts::load_beside_module(module, L"blockthespot.dll"))
            OutputDebugStringW(L"BlockTheSpot: payload unavailable; forwarding without ad blocking.\n");
    }
    return TRUE;
}
