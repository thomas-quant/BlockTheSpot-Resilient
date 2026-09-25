#pragma once
#include "loader.h"

#include <WinTrust.h>
#pragma comment(lib, "Wintrust.lib")

using WinVerifyTrust_t = LONG(WINAPI*)(HWND, GUID*, LPVOID);
LONG WINAPI WinVerifyTrust_hook(HWND hwnd, GUID* pgActionID, LPVOID pWVTData);
// Injectable delegate for native tests; production always uses WinVerifyTrust.
LONG verify_spotify_file(HWND hwnd, GUID* action, LPVOID data, WinVerifyTrust_t verify);
