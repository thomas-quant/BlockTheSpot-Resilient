#include "pch.h"
#include "cef_zip_reader_hook.h"
#include "funct_pointer.h"
#include "log_thread.h"
#include "pattern.h"
#include "IAT_hook.h"
#include <atomic>

static size_t cef_buffer_modify_count = 0;
static char cef_buffer_list[MAX_CEF_BUFFER_MODIFY_LIST][MAX_URL_LEN] = {};
using create_reader_t = void* (*)(void*);
using read_file_t = int(CALLBACK*)(void*, void*, size_t);
static create_reader_t create_orig = nullptr;
static create_reader_t create_impl = nullptr;
static std::atomic<read_file_t> read_orig{nullptr};
static cef_string_free_t free_string = nullptr;

static bool do_patch_buffer(const char* patch_name, void* buffer, size_t length) noexcept
{
    Modify patches[2]{};
    size_t count = 0;
    // All configuration scratch buffers are per-call, not shared with network
    // callbacks. Validate both halves of paired patches BEFORE modifying bytes.
    char key[32];
    char text[SHARED_BUFFER_SIZE];
    for (size_t i = 0; i < 2; ++i) {
        auto& patch = patches[i];
        _snprintf_s(key, sizeof(key), _TRUNCATE, "Signature_%zu", i + 1);
        const auto n = GetPrivateProfileStringA(patch_name, key, "", text, sizeof(text), CONFIG_FILEA);
        if (!n) break;
        if (n == sizeof(text) - 1) return false;
        const auto size = parse_signaure(text, n, patch.signature, patch.mask, sizeof(patch.mask) - 1);
        if (!size || size == SIZE_MAX) return false;
        patch.mask[size] = '\0';
        _snprintf_s(key, sizeof(key), _TRUNCATE, "Offset_%zu", i + 1);
        patch.offset = GetPrivateProfileIntA(patch_name, key, 0, CONFIG_FILEA);
        _snprintf_s(key, sizeof(key), _TRUNCATE, "Value_%zu", i + 1);
        const auto value_len = GetPrivateProfileStringA(patch_name, key, "", text, sizeof(text), CONFIG_FILEA);
        if (!value_len || value_len == sizeof(text) - 1) return false;
        patch.patch_size = parse_hex(text, value_len, patch.value, sizeof(patch.value));
        if (!patch.patch_size || patch.patch_size == SIZE_MAX) return false;
        ++count;
    }
    return apply_modifications(buffer, length, patches, count);
}

static void patch_file(const char* file, void* buffer, size_t length) noexcept
{
    for (size_t i = 0; i < MAX_CEF_BUFFER_MODIFY_LIST; ++i) {
        char key[16];
        char patch[MAX_URL_LEN];
        _snprintf_s(key, sizeof(key), _TRUNCATE, "%zu", i + 1);
        if (!GetPrivateProfileStringA(file, key, "", patch, sizeof(patch), CONFIG_FILEA)) break;
        const bool ok = do_patch_buffer(patch, buffer, length);
        char message[256];
        _snprintf_s(message, sizeof(message), _TRUNCATE, "SPA patch %s: %s / %s",
            ok ? "applied" : "skipped (no safe match)", file, patch);
        log_info(message);
    }
}

static int CALLBACK read_file_hook(void* self, void* buffer, size_t capacity)
{
    const auto original = read_orig.load();
    if (!original) return 0; // never publish a slot before its original is ready
    const int length = original(self, buffer, capacity);
    if (!buffer || length <= 0 || static_cast<size_t>(length) > capacity) return length;
    using get_name_t = cef_utf16_string* (__stdcall*)(void*);
    const auto get_name = get_funct_guarded<get_name_t>(self, CEF_ZIP_READER_GET_FILE_NAME_OFFSET);
    if (!get_name) return length;
    const auto name = get_name(self);
    if (!name) return length;
    char file[MAX_URL_LEN]{};
    int n = 0;
    if (name->str && name->length && name->length < MAX_URL_LEN)
        n = WideCharToMultiByte(CP_UTF8, 0, name->str, static_cast<int>(name->length),
            file, sizeof(file) - 1, nullptr, nullptr);
    free_string(name);
    if (!n) return length;
    file[n] = '\0';
    static LONG observed = 0;
    if (!InterlockedExchange(&observed, 1)) log_info("CEF ZIP read callback observed (filename decoded).");
    for (size_t i = 0; i < cef_buffer_modify_count; ++i) {
        if (!lstrcmpiA(file, cef_buffer_list[i])) {
            // Only scan bytes returned by CEF, not uninitialized buffer capacity.
            patch_file(file, buffer, static_cast<size_t>(length));
            break;
        }
    }
    return length;
}

static void* create_reader_hook(void* stream)
{
    static LONG observed = 0;
    if (!InterlockedExchange(&observed, 1)) log_info("CEF ZIP callback observed.");
    auto reader = create_orig(stream);
    const auto original = get_funct_guarded<read_file_t>(reader, CEF_ZIP_READER_GET_READ_FILE_OFFSET);
    if (!original) return reader;
    read_file_t expected = nullptr;
    if (!read_orig.compare_exchange_strong(expected, original) && expected != original) {
        log_info("CEF ZIP read_file implementation changed; leaving reader unhooked.");
        return reader;
    }
    if (!overwrite_funct_t(reader, CEF_ZIP_READER_GET_READ_FILE_OFFSET, read_file_hook))
        log_info("CEF ZIP read_file slot could not be patched.");
    return reader;
}

void* cef_zip_reader_create_stub(void* stream)
{
    return create_impl ? create_impl(stream) : nullptr;
}

bool hook_cef_reader(HMODULE libcef) noexcept
{
    create_orig = reinterpret_cast<create_reader_t>(GetProcAddress_orig(libcef, "cef_zip_reader_create"));
    create_impl = create_orig;
    free_string = reinterpret_cast<cef_string_free_t>(GetProcAddress_orig(libcef, "cef_string_userfree_utf16_free"));
    if (!create_orig || !free_string) {
        log_info("CEF ZIP exports missing; interception disabled.");
        return false;
    }
    if (!GetPrivateProfileIntA("Buffer_modify", "Enable", 0, CONFIG_FILEA)) {
        log_info("SPA patching disabled by config.");
        return true;
    }
    CEF_ZIP_READER_GET_READ_FILE_OFFSET = GetPrivateProfileIntA("LIBCEF", "CEF_ZIP_READER_GET_READ_FILE_OFFSET",
        static_cast<INT>(CEF_ZIP_READER_GET_READ_FILE_OFFSET), CONFIG_FILEA);
    CEF_ZIP_READER_GET_FILE_NAME_OFFSET = GetPrivateProfileIntA("LIBCEF", "CEF_ZIP_READER_GET_FILE_NAME_OFFSET",
        static_cast<INT>(CEF_ZIP_READER_GET_FILE_NAME_OFFSET), CONFIG_FILEA);
    cef_buffer_modify_count = 0;
    for (size_t i = 0; i < MAX_CEF_BUFFER_MODIFY_LIST; ++i) {
        char key[16];
        _snprintf_s(key, sizeof(key), _TRUNCATE, "%zu", i + 1);
        if (!GetPrivateProfileStringA("Buffer_modify", key, "", cef_buffer_list[i], MAX_URL_LEN, CONFIG_FILEA)) break;
        ++cef_buffer_modify_count;
    }
    create_impl = create_reader_hook;
    log_info("CEF ZIP handler ready (imports not yet patched).");
    return true;
}
