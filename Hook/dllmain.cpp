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
	// At process termination Windows has already stopped other threads. Waiting
	// for the logger under the loader lock can deadlock; let the OS close handles.
	if (DLL_PROCESS_DETACH == ul_reason_for_call && lpReserved == nullptr) {
		LPWSTR cmd = GetCommandLineW();
		if (NULL == wcsstr(cmd, L"--type=") &&
			NULL == wcsstr(cmd, L"--url=")) {
			stop_log();
			//remove_debug_log();
		}
	}
	return TRUE;
}

