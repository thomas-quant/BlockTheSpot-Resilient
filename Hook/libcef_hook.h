#pragma once
#include "loader.h"
#include "import_hook.h"

struct cef_import_result {
    imports::result url;
    imports::result zip;
};

// Prepare original function pointers before exposing either interception path.
void configure_cef_interception(HMODULE libcef, bool url_ready, bool zip_ready) noexcept;
FARPROC cef_hook_for_proc(HMODULE module, LPCSTR name) noexcept;
cef_import_result hook_libcef_imports(HMODULE module) noexcept;
