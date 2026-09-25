#pragma once
#include "loader.h"
#include "import_hook.h"

using GetProcAddress_t = FARPROC(WINAPI*)(HMODULE, LPCSTR);
inline const GetProcAddress_t GetProcAddress_orig = ::GetProcAddress;

imports::result process_IAT_hook_GetProcAddress(HMODULE module) noexcept;
