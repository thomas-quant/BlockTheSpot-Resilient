#include "pch.h"
#include "cef_offsets.h"
#include "loader.h"
#include "log_thread.h"
#include <winver.h>
#pragma comment(lib, "version.lib")

// ---------------------------------------------------------------------------
// Default offset derivation (x64):
//   cef_base_ref_counted_t = size_t + 4 fn-ptrs                 = 40 bytes
//   cef_request_t.get_url  = base + 1 fn-ptr (is_read_only)     = 0x30
//   cef_zip_reader_t.get_file_name / read_file follow the same struct-of-
//   function-pointers rule (0x48 / 0x70 in current CEF).
// These have been stable across recent CEF versions, which is why a single
// hardcoded value worked for a long time. If a future CEF build shifts them,
// add a keyed override in offsets_for_version() rather than editing config.ini.
// ---------------------------------------------------------------------------

struct cef_offset_set {
	size_t get_url;
	size_t zip_read_file;
	size_t zip_file_name;
};

// Known-good defaults (match the libcef used by current Spotify builds).
static constexpr cef_offset_set kDefaultOffsets{ 0x30, 0x70, 0x48 };

// Per-major-version overrides. Empty today because the layout has been stable;
// add an entry (keyed by libcef major version) if a future CEF shifts a struct.
static cef_offset_set offsets_for_version(uint64_t version) noexcept
{
	const uint16_t major = static_cast<uint16_t>((version >> 48) & 0xFFFF);
	switch (major) {
	// case <major>: return cef_offset_set{ ... };
	default:
		return kDefaultOffsets;
	}
}

bool get_libcef_version(HMODULE libcef, uint64_t& version) noexcept
{
	version = 0;

	wchar_t path[MAX_PATH];
	const DWORD path_len = GetModuleFileNameW(libcef, path, MAX_PATH);
	if (0 == path_len || path_len >= MAX_PATH) {
		return false;
	}

	DWORD dummy = 0;
	const DWORD info_size = GetFileVersionInfoSizeW(path, &dummy);
	if (0 == info_size) {
		return false;
	}

	// Version info blocks are small; a fixed buffer avoids heap/STL in a DLL
	// built with exceptions disabled.
	BYTE buffer[4096];
	if (info_size > sizeof(buffer)) {
		return false;
	}
	if (!GetFileVersionInfoW(path, 0, info_size, buffer)) {
		return false;
	}

	VS_FIXEDFILEINFO* ffi = nullptr;
	UINT ffi_len = 0;
	if (!VerQueryValueW(buffer, L"\\", reinterpret_cast<LPVOID*>(&ffi), &ffi_len)
		|| nullptr == ffi || ffi_len < sizeof(VS_FIXEDFILEINFO)) {
		return false;
	}

	version = (static_cast<uint64_t>(ffi->dwFileVersionMS) << 32) | ffi->dwFileVersionLS;
	return true;
}

libcef_range_t compute_module_range(HMODULE mod) noexcept
{
	libcef_range_t range;
	if (!mod) {
		return range;
	}

	const auto base = reinterpret_cast<uintptr_t>(mod);
	const auto dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
	if (IMAGE_DOS_SIGNATURE != dos->e_magic) {
		return range;
	}

	const auto nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
	if (IMAGE_NT_SIGNATURE != nt->Signature) {
		return range;
	}

	range.base = base;
	range.size = nt->OptionalHeader.SizeOfImage;
	range.valid = 0 != range.size;
	return range;
}

void resolve_cef_offsets(HMODULE libcef) noexcept
{
	g_libcef_range = compute_module_range(libcef);

	uint64_t version = 0;
	const bool have_version = get_libcef_version(libcef, version);
	const cef_offset_set off = have_version ? offsets_for_version(version) : kDefaultOffsets;

	CEF_REQUEST_GET_URL_OFFSET = off.get_url;
	CEF_ZIP_READER_GET_READ_FILE_OFFSET = off.zip_read_file;
	CEF_ZIP_READER_GET_FILE_NAME_OFFSET = off.zip_file_name;

	char buf[256];
	_snprintf_s(
		buf, sizeof(buf), _TRUNCATE,
		"resolve_cef_offsets: libcef v%u.%u.%u.%u range=%s get_url=0x%zx read_file=0x%zx file_name=0x%zx",
		static_cast<unsigned>((version >> 48) & 0xFFFF),
		static_cast<unsigned>((version >> 32) & 0xFFFF),
		static_cast<unsigned>((version >> 16) & 0xFFFF),
		static_cast<unsigned>(version & 0xFFFF),
		g_libcef_range.valid ? "ok" : "FAIL",
		CEF_REQUEST_GET_URL_OFFSET,
		CEF_ZIP_READER_GET_READ_FILE_OFFSET,
		CEF_ZIP_READER_GET_FILE_NAME_OFFSET
	);
	log_info(buf);
}
