// dllmain.cpp : Defines the entry point for the DLL application.
#include "pch.h"
#include "kill_crashpad.h"
#include "log_thread.h"

BOOL APIENTRY DllMain( HMODULE hModule,
                       DWORD  ul_reason_for_call,
                       LPVOID lpReserved
                     )
{
	if (DLL_PROCESS_ATTACH == ul_reason_for_call) {
		DisableThreadLibraryCalls(hModule);
		if (!initialize_hook_paths(hModule)) return FALSE;
		// Import slots and the logger can outlive arbitrary FreeLibrary calls.
		// Keep the payload mapped until process exit; never wait for a worker
		// thread from DLL_PROCESS_DETACH while holding Windows' loader lock.
		HMODULE pinned = nullptr;
		if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
			reinterpret_cast<LPCWSTR>(hModule), &pinned)) return FALSE;
		LPWSTR cmd = GetCommandLineW();
#ifdef USE_APC
		QueueUserAPC(
			bts_main,
			GetCurrentThread(),
			reinterpret_cast<ULONG_PTR>(cmd)
		);
#else
		bts_main(reinterpret_cast<ULONG_PTR>(cmd));
#endif
		// Crashpad process
		if (cmd != NULL) {
			if (NULL != wcsstr(cmd, L"--url=")) {
				kill_crashpad();
			}
		}
	}
	// Process exit owns final thread/handle cleanup for this pinned DLL.
	return TRUE;
}

