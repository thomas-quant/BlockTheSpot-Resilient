#pragma once
#include "pch.h"

// Runtime resolution of CEF C-API struct offsets, plus libcef.dll's mapped
// image range used to validate CEF function pointers before we call through
// them (see get_funct_guarded in funct_pointer.h).
//
// The CEF C-API exposes objects as structs of function pointers whose layout
// is fixed for a given CEF version; an offset only shifts when CEF changes a
// struct (adds/reorders a method, or grows the ref-counted base). We keep the
// compiled-in defaults (which match the libcef in current Spotify builds) and
// apply a per-version override only if a future CEF build moves them. Combined
// with the module-range guard, an unknown/wrong offset degrades to
// "ad-block temporarily off" instead of an access-violation crash.

struct libcef_range_t {
	uintptr_t base = 0;
	size_t size = 0;
	bool valid = false;
};

// libcef.dll [base, base+SizeOfImage). Populated by resolve_cef_offsets().
inline libcef_range_t g_libcef_range;

// Reads libcef.dll's file version into `version` (packed MS<<32 | LS dwords).
bool get_libcef_version(HMODULE libcef, uint64_t& version) noexcept;

// Computes a module's [base, base+SizeOfImage) range from its PE header
// (no psapi dependency).
libcef_range_t compute_module_range(HMODULE mod) noexcept;

// Resolves the CEF struct offsets for the running libcef.dll version and caches
// its module range in g_libcef_range. Must run before hook_cef_url /
// hook_cef_reader so the resolved offsets are the defaults those hooks use.
void resolve_cef_offsets(HMODULE libcef) noexcept;
