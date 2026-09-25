#include "pch.h"
#include "cef_url_hook.h"
#include "loader.h"
#include "funct_pointer.h"
#include "log_thread.h"
#include "IAT_hook.h"

static size_t cef_block_count = 0;
static char cef_block_list[MAX_CEF_BLOCK_LIST][MAX_URL_LEN] = {};
using cef_urlrequest_create_t = void* (*)(void*, void*, void*);
static cef_urlrequest_create_t cef_urlrequest_create_orig = nullptr;
static cef_urlrequest_create_t cef_urlrequest_create_impl = nullptr;
static cef_string_free_t free_string = nullptr;

static bool is_blocked(const char* url) noexcept
{
    for (size_t i = 0; i < cef_block_count; ++i)
        if (strstr(url, cef_block_list[i])) return true;
    return false;
}

void* cef_urlrequest_create_stub(void* request, void* client, void* context)
{
    return cef_urlrequest_create_impl ? cef_urlrequest_create_impl(request, client, context) : nullptr;
}

static void* cef_urlrequest_create_hook(void* request, void* client, void* context)
{
    static LONG observed = 0;
    if (!InterlockedExchange(&observed, 1)) log_info("CEF URL callback observed.");
    using get_url_t = cef_utf16_string* (__stdcall*)(void*);
    const auto get_url = get_funct_guarded<get_url_t>(request, CEF_REQUEST_GET_URL_OFFSET);
    if (!get_url) return cef_urlrequest_create_orig(request, client, context);
    const auto url = get_url(request);
    if (!url) return cef_urlrequest_create_orig(request, client, context);

    // Per-call storage: URL callbacks can run concurrently with SPA patching.
    char buffer[SHARED_BUFFER_SIZE]{};
    int length = 0;
    if (url->str && url->length && url->length < SHARED_BUFFER_SIZE)
        length = WideCharToMultiByte(CP_UTF8, 0, url->str, static_cast<int>(url->length),
            buffer, sizeof(buffer) - 1, nullptr, nullptr);
    free_string(url);
    if (!length) return cef_urlrequest_create_orig(request, client, context);
    buffer[length] = '\0';
    static LONG active = 0;
    if (!InterlockedExchange(&active, 1)) log_info("CEF URL handler active (URL decoded).");
    const bool blocked = is_blocked(buffer);
    // Detailed URLs are opt-in (Level=2), never part of the default diagnostics.
    char message[256];
    _snprintf_s(message, sizeof(message), _TRUNCATE, "%s:%s", blocked ? "block" : "allow", buffer);
    log_debug(message);
    return blocked ? nullptr : cef_urlrequest_create_orig(request, client, context);
}

bool hook_cef_url(HMODULE libcef) noexcept
{
    cef_urlrequest_create_orig = reinterpret_cast<cef_urlrequest_create_t>(
        GetProcAddress_orig(libcef, "cef_urlrequest_create"));
    cef_urlrequest_create_impl = cef_urlrequest_create_orig;
    free_string = reinterpret_cast<cef_string_free_t>(GetProcAddress_orig(libcef, "cef_string_userfree_utf16_free"));
    if (!cef_urlrequest_create_orig || !free_string) {
        log_info("CEF URL exports missing; interception disabled.");
        return false;
    }
    if (!GetPrivateProfileIntA("URL_block", "Enable", 0, CONFIG_FILEA)) {
        log_info("URL blocking disabled by config.");
        return true;
    }
    CEF_REQUEST_GET_URL_OFFSET = GetPrivateProfileIntA("LIBCEF", "CEF_REQUEST_GET_URL_OFFSET",
        static_cast<INT>(CEF_REQUEST_GET_URL_OFFSET), CONFIG_FILEA);
    cef_block_count = 0;
    for (size_t i = 0; i < MAX_CEF_BLOCK_LIST; ++i) {
        char key[16];
        _snprintf_s(key, sizeof(key), _TRUNCATE, "%zu", i + 1);
        if (!GetPrivateProfileStringA("URL_block", key, "", cef_block_list[i], MAX_URL_LEN, CONFIG_FILEA)) break;
        ++cef_block_count;
    }
    cef_urlrequest_create_impl = cef_urlrequest_create_hook;
    log_info("CEF URL handler ready (imports not yet patched).");
    return true;
}
