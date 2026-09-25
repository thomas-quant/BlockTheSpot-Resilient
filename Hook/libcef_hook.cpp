#include "pch.h"
#include "libcef_hook.h"
#include "cef_url_hook.h"
#include "cef_zip_reader_hook.h"

static HMODULE cef_module = nullptr;
static bool url_available = false;
static bool zip_available = false;

void configure_cef_interception(HMODULE libcef, bool url_ready, bool zip_ready) noexcept
{
    cef_module = libcef;
    url_available = url_ready;
    zip_available = zip_ready;
}

FARPROC cef_hook_for_proc(HMODULE module, LPCSTR name) noexcept
{
    if (!cef_module || module != cef_module || !name || IS_INTRESOURCE(name)) return nullptr;
    if (url_available && !strcmp(name, "cef_urlrequest_create"))
        return reinterpret_cast<FARPROC>(cef_urlrequest_create_stub);
    if (zip_available && !strcmp(name, "cef_zip_reader_create"))
        return reinterpret_cast<FARPROC>(cef_zip_reader_create_stub);
    return nullptr;
}

cef_import_result hook_libcef_imports(HMODULE module) noexcept
{
    // Adapted from MichaelMiksa's by-name delay-IAT fix (August 2026):
    // https://github.com/MichaelMiksa/BlockTheSpot---continued/commit/7725515
    // Intercept unresolved AND resolved slots, without invoking their thunks.
    // Also cover ordinary imports, validate bounds/protections, and report both
    // functions separately so a partial installation cannot look successful.
    cef_import_result out;
    if (url_available)
        out.url = imports::patch(module, "libcef.dll", "cef_urlrequest_create",
            reinterpret_cast<FARPROC>(cef_urlrequest_create_stub));
    if (zip_available)
        out.zip = imports::patch(module, "libcef.dll", "cef_zip_reader_create",
            reinterpret_cast<FARPROC>(cef_zip_reader_create_stub));
    return out;
}
