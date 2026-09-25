#include "pch.h"
#include "loader.h"
#include "IAT_hook.h"
#include "kill_crashpad.h"
#include "log_thread.h"
#include "cef_url_hook.h"
#include "cef_zip_reader_hook.h"
#include "libcef_hook.h"
#include "cef_offsets.h"
#pragma	comment(lib, "version.lib")

static inline bool is_chrome_elf_required_exist() noexcept
{
	const auto required = CreateFileW(
		ORIGINAL_CHROME_ELF_DLL,
		GENERIC_READ,
		FILE_SHARE_READ,
		nullptr,
		OPEN_EXISTING,
		FILE_ATTRIBUTE_NORMAL,
		nullptr
	);
	if (INVALID_HANDLE_VALUE == required) {
		return false;
	}
	CloseHandle(required);
	return true;
}

VOID CALLBACK bts_main(ULONG_PTR param)
{
	const auto exe_lookup = process_IAT_hook_GetProcAddress(GetModuleHandleW(NULL));
	const wchar_t* cmd =
		reinterpret_cast<const wchar_t*>(param);
	//  Spotify's main process
	if (cmd && NULL == wcsstr(cmd, L"--type=") &&
		NULL == wcsstr(cmd, L"--url=")) {
		init_log_thread();
		if (false == is_chrome_elf_required_exist()) {
			log_info("chrome_elf_required.dll file not found, Did you skip something?");
			return;
		}
		HMODULE spotify_dll_handle =
			bts::load_beside_module(hook_module, L"spotify.dll");
		HMODULE libcef_dll_handle =
			bts::load_beside_module(hook_module, L"libcef.dll");

		if (!spotify_dll_handle) {
			log_debug("Failed to load spotify.dll for IAT hooking.");
			return;
		}
		if (!libcef_dll_handle) {
			log_debug("Failed to load libcef.dll.");
			return;
		}

		resolve_cef_offsets(libcef_dll_handle);
		const bool url_ready = hook_cef_url(libcef_dll_handle);
		const bool zip_ready = hook_cef_reader(libcef_dll_handle);
		configure_cef_interception(libcef_dll_handle, url_ready, zip_ready);
		const auto dll_lookup = process_IAT_hook_GetProcAddress(spotify_dll_handle);
		const auto exe = hook_libcef_imports(GetModuleHandleW(NULL));
		const auto dll = hook_libcef_imports(spotify_dll_handle);

		char status[256];
		_snprintf_s(status, sizeof(status), _TRUNCATE,
			"GetProcAddress imports: exe=%zu/%zu spotify.dll=%zu/%zu errors=%zu",
			exe_lookup.patched, exe_lookup.matched, dll_lookup.patched, dll_lookup.matched,
			exe_lookup.errors + dll_lookup.errors);
		log_info(status);
		_snprintf_s(status, sizeof(status), _TRUNCATE,
			"CEF imports Spotify.exe: url=%zu/%zu zip=%zu/%zu errors=%zu",
			exe.url.patched, exe.url.matched, exe.zip.patched, exe.zip.matched, exe.url.errors + exe.zip.errors);
		log_info(status);
		_snprintf_s(status, sizeof(status), _TRUNCATE,
			"CEF imports spotify.dll: url=%zu/%zu zip=%zu/%zu errors=%zu",
			dll.url.patched, dll.url.matched, dll.zip.patched, dll.zip.matched, dll.url.errors + dll.zip.errors);
		log_info(status);

		const bool ok = url_ready && zip_ready &&
			(dll.url.complete() || dll_lookup.complete()) &&
			(dll.zip.complete() || dll_lookup.complete()) &&
			!exe_lookup.errors && !dll_lookup.errors &&
			!exe.url.errors && !exe.zip.errors && !dll.url.errors && !dll.zip.errors;
		log_info(ok ? "CEF hook installation: OK (playback not verified)." :
			"CEF hook installation: FAILED/PARTIAL; ad blocking may be inactive.");
		if (!ok) OutputDebugStringA("BlockTheSpot: CEF hook installation failed; see blockthespot.log.\n");
	}
}
