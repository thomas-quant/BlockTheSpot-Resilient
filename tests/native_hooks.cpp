// Windows-only regression tests for the SAME import walker used by the DLL.
#include "import_hook.h"
#include "IAT_hook.h"
#include "libcef_hook.h"
#include "cef_url_hook.h"
#include "cef_zip_reader_hook.h"
#include "WinTrust_hook.h"
#include "funct_pointer.h"
#include "pattern.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <climits>
#include <initializer_list>

static int checks = 0;
#define CHECK(expr) do { ++checks; if (!(expr)) { std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #expr); std::exit(1); } } while (0)
void* cef_urlrequest_create_stub(void*, void*, void*) { return reinterpret_cast<void*>(42); }
void* cef_zip_reader_create_stub(void*) { return reinterpret_cast<void*>(43); }
static INT_PTR WINAPI replacement() { return 77; }

struct fixture {
    BYTE* bytes = static_cast<BYTE*>(VirtualAlloc(nullptr, 0x4000, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    template<class T> T* at(size_t rva) { return reinterpret_cast<T*>(bytes + rva); }
    HMODULE module() { return reinterpret_cast<HMODULE>(bytes); }
    fixture(const char* provider = "api-ms-win-core-libraryloader-l1-2-0.dll") {
        CHECK(bytes);
        auto dos = at<IMAGE_DOS_HEADER>(0); dos->e_magic = IMAGE_DOS_SIGNATURE; dos->e_lfanew = 0x80;
        auto nt = at<IMAGE_NT_HEADERS64>(0x80); nt->Signature = IMAGE_NT_SIGNATURE;
        nt->FileHeader.Machine = IMAGE_FILE_MACHINE_AMD64;
        nt->FileHeader.SizeOfOptionalHeader = sizeof(IMAGE_OPTIONAL_HEADER64);
        nt->OptionalHeader.Magic = IMAGE_NT_OPTIONAL_HDR64_MAGIC;
        nt->OptionalHeader.SizeOfImage = 0x4000;
        nt->OptionalHeader.NumberOfRvaAndSizes = IMAGE_NUMBEROF_DIRECTORY_ENTRIES;
        nt->OptionalHeader.DataDirectory[1] = {0x400, 2 * sizeof(IMAGE_IMPORT_DESCRIPTOR)};
        nt->OptionalHeader.DataDirectory[13] = {0x500, 2 * sizeof(IMAGE_DELAYLOAD_DESCRIPTOR)};
        auto normal = at<IMAGE_IMPORT_DESCRIPTOR>(0x400);
        normal->Name = 0x600; normal->OriginalFirstThunk = 0x900; normal->FirstThunk = 0xa00;
        strcpy_s(at<char>(0x600), 100, provider); strcpy_s(at<char>(0x680), 100, "libcef.dll");
        auto delay = at<IMAGE_DELAYLOAD_DESCRIPTOR>(0x500);
        delay->Attributes.AllAttributes = 1; delay->DllNameRVA = 0x680;
        delay->ImportNameTableRVA = 0xb00; delay->ImportAddressTableRVA = 0xc00;
        at<IMAGE_THUNK_DATA64>(0x900)[0].u1.Ordinal = IMAGE_ORDINAL_FLAG64 | 7;
        at<IMAGE_THUNK_DATA64>(0x900)[1].u1.AddressOfData = 0xd00;
        at<IMAGE_THUNK_DATA64>(0xa00)[0].u1.Function = 0x1111;
        at<IMAGE_THUNK_DATA64>(0xa00)[1].u1.Function = 0x2222;
        at<IMAGE_THUNK_DATA64>(0xb00)[0].u1.AddressOfData = 0xd80;
        at<IMAGE_THUNK_DATA64>(0xb00)[1].u1.AddressOfData = 0xe00;
        at<IMAGE_THUNK_DATA64>(0xc00)[0].u1.Function = 0x3333; // unresolved thunk, MUST NOT execute
        at<IMAGE_THUNK_DATA64>(0xc00)[1].u1.Function = 0x4444;
        strcpy_s(at<char>(0xd02), 100, "GetProcAddress");
        strcpy_s(at<char>(0xd82), 100, "cef_urlrequest_create");
        strcpy_s(at<char>(0xe02), 100, "cef_zip_reader_create");
    }
    ~fixture() { VirtualFree(bytes, 0, MEM_RELEASE); }
};

static void import_tests()
{
    for (const char* provider : {"KERNEL32.dll", "api-ms-win-core-libraryloader-l1-2-0.dll", "KERNELBASE.dll"}) {
        fixture f(provider);
        auto result = process_IAT_hook_GetProcAddress(f.module());
        CHECK(result.complete()); CHECK(result.matched == 1);
        CHECK(f.at<IMAGE_THUNK_DATA64>(0xa00)[0].u1.Function == 0x1111); // skip ordinals
        auto lookup = reinterpret_cast<GetProcAddress_t>(f.at<IMAGE_THUNK_DATA64>(0xa00)[1].u1.Function);
        auto cef = reinterpret_cast<HMODULE>(0x10000);
        configure_cef_interception(cef, true, true);
        CHECK(lookup(cef, "cef_urlrequest_create") == reinterpret_cast<FARPROC>(cef_urlrequest_create_stub));
        CHECK(lookup(cef, "cef_zip_reader_create") == reinterpret_cast<FARPROC>(cef_zip_reader_create_stub));
        auto kernel = GetModuleHandleW(L"kernel32.dll");
        CHECK(lookup(kernel, "Sleep") == GetProcAddress(kernel, "Sleep"));
        CHECK(lookup(kernel, MAKEINTRESOURCEA(0)) == GetProcAddress(kernel, MAKEINTRESOURCEA(0)));
        CHECK(process_IAT_hook_GetProcAddress(f.module()).complete()); // no recursive original
        CHECK(lookup(kernel, "Sleep") == GetProcAddress(kernel, "Sleep"));
        auto direct = hook_libcef_imports(f.module());
        CHECK(direct.url.complete()); CHECK(direct.zip.complete());
        auto url = reinterpret_cast<void* (*)(void*, void*, void*)>(f.at<IMAGE_THUNK_DATA64>(0xc00)[0].u1.Function);
        auto zip = reinterpret_cast<void* (*)(void*)>(f.at<IMAGE_THUNK_DATA64>(0xc00)[1].u1.Function);
        CHECK(url(nullptr, nullptr, nullptr) == reinterpret_cast<void*>(42));
        CHECK(zip(nullptr) == reinterpret_cast<void*>(43));
        CHECK(hook_libcef_imports(f.module()).url.complete());
        configure_cef_interception(cef, false, false);
        CHECK(cef_hook_for_proc(cef, "cef_urlrequest_create") == nullptr);
        CHECK(hook_libcef_imports(f.module()).url.matched == 0);
    }
    fixture normal_cef;
    strcpy_s(normal_cef.at<char>(0x600), 100, "libcef.dll");
    strcpy_s(normal_cef.at<char>(0xd02), 100, "cef_urlrequest_create");
    auto both = imports::patch(normal_cef.module(), "libcef.dll", "cef_urlrequest_create", replacement);
    CHECK(both.complete() && both.matched == 2); // normal AND delay slots, not first-match only
    auto missing = imports::patch(normal_cef.module(), "libcef.dll", "missing", replacement);
    CHECK(!missing.complete() && missing.errors == 0 && missing.matched == 0);

    fixture protection;
    DWORD old;
    CHECK(VirtualProtect(protection.bytes, 0x4000, PAGE_READONLY, &old));
    CHECK(process_IAT_hook_GetProcAddress(protection.module()).complete());
    MEMORY_BASIC_INFORMATION info{};
    CHECK(VirtualQuery(protection.bytes + 0xa00, &info, sizeof(info)) != 0);
    CHECK(info.Protect == PAGE_READONLY);

    fixture corrupt;
    corrupt.at<IMAGE_THUNK_DATA64>(0x900)[1].u1.AddressOfData = 0x7ffffffffffffffeULL;
    CHECK(process_IAT_hook_GetProcAddress(corrupt.module()).errors != 0);
    CHECK(corrupt.at<IMAGE_THUNK_DATA64>(0xa00)[1].u1.Function == 0x2222);
    corrupt.at<IMAGE_THUNK_DATA64>(0x900)[1].u1.AddressOfData = 0x4001;
    CHECK(process_IAT_hook_GetProcAddress(corrupt.module()).errors != 0);
    corrupt.at<IMAGE_IMPORT_DESCRIPTOR>(0x400)->OriginalFirstThunk = 0;
    CHECK(process_IAT_hook_GetProcAddress(corrupt.module()).errors != 0);
    corrupt.at<IMAGE_DELAYLOAD_DESCRIPTOR>(0x500)->Attributes.AllAttributes = 0; // legacy VA layout
    CHECK(imports::patch(corrupt.module(), "libcef.dll", "cef_urlrequest_create", replacement).errors != 0);
    corrupt.at<IMAGE_NT_HEADERS64>(0x80)->OptionalHeader.DataDirectory[1].Size = 0x5000;
    CHECK(process_IAT_hook_GetProcAddress(corrupt.module()).errors != 0);
    corrupt.at<IMAGE_DOS_HEADER>(0)->e_magic = 0;
    CHECK(process_IAT_hook_GetProcAddress(corrupt.module()).errors != 0);
    CHECK(process_IAT_hook_GetProcAddress(nullptr).errors != 0);
    CHECK(process_IAT_hook_GetProcAddress(reinterpret_cast<HMODULE>(1)).errors != 0);
}

static Modify modification(const char* signature, const char* value, UINT offset = 0)
{
    Modify p{};
    const auto n = parse_signaure(signature, strlen(signature), p.signature, p.mask, sizeof(p.mask) - 1);
    CHECK(n != SIZE_MAX); p.mask[n] = '\0';
    p.patch_size = parse_hex(value, strlen(value), p.value, sizeof(p.value));
    CHECK(p.patch_size != SIZE_MAX); p.offset = offset;
    return p;
}

static void pattern_tests()
{
    BYTE data[]{0x41, 0x42, 0x43, 0x44};
    Modify pair[]{modification("41 42", "FF"), modification("43 44", "00")};
    CHECK(apply_modifications(data, sizeof(data), pair, 2));
    CHECK(data[0] == 0xff && data[2] == 0);
    memcpy(data, "ABCD", 4);
    pair[1] = modification("43 45", "00");
    CHECK(!apply_modifications(data, sizeof(data), pair, 2));
    CHECK(!memcmp(data, "ABCD", 4)); // pair is all-or-nothing
    pair[0] = modification("41 ??", "FF", UINT_MAX);
    CHECK(!apply_modifications(data, sizeof(data), pair, 1));
    CHECK(!memcmp(data, "ABCD", 4));
    pair[0] = modification("41 42", "FF", 2); pair[1] = modification("43 44", "00");
    CHECK(!apply_modifications(data, sizeof(data), pair, 2)); // overlapping writes
    memcpy(data, "ABAB", 4); pair[0] = modification("41 42", "00");
    CHECK(!apply_modifications(data, sizeof(data), pair, 1)); // ambiguous signature
    fixture boundary;
    DWORD old;
    CHECK(VirtualProtect(boundary.bytes + 0x1000, 0x1000, PAGE_NOACCESS, &old));
    auto tail = boundary.bytes + 0x1000 - 2; memcpy(tail, "AB", 2);
    pair[0] = modification("41 42 43", "00");
    CHECK(!FindPattern(tail, 2, pair[0].signature, pair[0].mask)); // no over-read into guard page
    pair[0] = modification("41 42", "00", 1);
    CHECK(apply_modifications(tail, 2, pair, 1)); CHECK(tail[1] == 0);
    BYTE output[2]; char mask[2];
    CHECK(parse_hex("FF", 2, output, 2) == 1 && output[0] == 0xff);
    CHECK(parse_hex("FG", 2, output, 2) == SIZE_MAX);
    CHECK(parse_signaure("??", 2, output, mask, 2) == 1 && mask[0] == '?');
}

static void guard_tests()
{
    fixture code;
    struct object { size_t size; FARPROC method; } object{sizeof(object), reinterpret_cast<FARPROC>(code.bytes)};
    g_libcef_range = {reinterpret_cast<uintptr_t>(code.bytes), 0x4000, true};
    CHECK(!get_funct_guarded<FARPROC>(&object, sizeof(size_t))); // non-executable memory
    DWORD old;
    CHECK(VirtualProtect(code.bytes, 0x1000, PAGE_EXECUTE_READ, &old));
    CHECK(get_funct_guarded<FARPROC>(&object, sizeof(size_t)) == object.method);
    CHECK(!get_funct_guarded<FARPROC>(nullptr, sizeof(size_t)));
    CHECK(!get_funct_guarded<FARPROC>(reinterpret_cast<void*>(1), sizeof(size_t)));
    CHECK(!get_funct_guarded<FARPROC>(&object, 1));
    CHECK(!get_funct_guarded<FARPROC>(&object, sizeof(object)));
    object.size = sizeof(size_t);
    CHECK(!get_funct_guarded<FARPROC>(&object, sizeof(size_t)));
    object.size = sizeof(object); g_libcef_range.valid = false;
    CHECK(!get_funct_guarded<FARPROC>(&object, sizeof(size_t))); // unknown module fails closed
}

static WINTRUST_DATA* expected_trust_data = nullptr;
static bool expect_redirect = false;
static LONG WINAPI capture_trust(HWND, GUID*, LPVOID opaque)
{
    auto data = static_cast<WINTRUST_DATA*>(opaque);
    CHECK((data != expected_trust_data) == expect_redirect);
    if (expect_redirect) {
        CHECK(!wcscmp(data->pFile->pcwszFilePath, ORIGINAL_CHROME_ELF_DLL));
        CHECK(!data->pFile->hFile);
        data->hWVTStateData = reinterpret_cast<HANDLE>(42);
    }
    return 123;
}

static void path_and_trust_tests()
{
    CHECK(initialize_hook_paths(GetModuleHandleW(nullptr)));
    wchar_t proxy[bts::path_capacity], too_small[2], old_directory[bts::path_capacity], temp[bts::path_capacity];
    CHECK(bts::path_beside_module(hook_module, L"chrome_elf.dll", proxy));
    CHECK(!bts::path_beside_module(hook_module, L"config.ini", too_small));
    CHECK(!too_small[0]);
    CHECK(!bts::path_beside_module(hook_module, L"../outside", temp));
    CHECK(GetCurrentDirectoryW(bts::path_capacity, old_directory));
    CHECK(GetTempPathW(bts::path_capacity, temp)); CHECK(SetCurrentDirectoryW(temp));
    CHECK(WritePrivateProfileStringW(L"URL_block", L"Enable", L"1", CONFIG_FILEW));
    CHECK(WritePrivateProfileStringW(L"patch", L"Signature_1", L"41 42", CONFIG_FILEW));
    CHECK(config_int("URL_block", "Enable", 0, CONFIG_FILEW) == 1);
    char text[16];
    CHECK(config_string("patch", "Signature_1", "", text, sizeof(text), CONFIG_FILEW) == 5);
    CHECK(!strcmp(text, "41 42"));
    char tiny[2];
    CHECK(!config_string("patch", "Signature_1", "", tiny, sizeof(tiny), CONFIG_FILEW));
    CHECK(GetLastError() == ERROR_INSUFFICIENT_BUFFER && !tiny[0]);
    CHECK(!config_string("patch", "Signature_2", "", text, sizeof(text), CONFIG_FILEW));
    CHECK(GetLastError() == ERROR_SUCCESS);
    CHECK(config_int("URL_block", "Enable", 7, L"") == 7);

    HANDLE backup = CreateFileW(ORIGINAL_CHROME_ELF_DLL, GENERIC_WRITE, 0, nullptr, CREATE_NEW, 0, nullptr);
    CHECK(backup != INVALID_HANDLE_VALUE); CloseHandle(backup);
    HANDLE file = CreateFileW(proxy, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_NEW, 0, nullptr);
    CHECK(file != INVALID_HANDLE_VALUE);
    WINTRUST_FILE_INFO info{}; info.cbStruct = sizeof(info); info.pcwszFilePath = proxy;
    WINTRUST_DATA data{}; data.cbStruct = sizeof(data); data.dwUnionChoice = WTD_CHOICE_FILE;
    data.dwStateAction = WTD_STATEACTION_VERIFY; data.pFile = &info;
    expected_trust_data = &data; expect_redirect = true;
    CHECK(verify_spotify_file(nullptr, nullptr, &data, capture_trust) == 123);
    CHECK(data.pFile == &info && info.pcwszFilePath == proxy && !info.hFile);
    CHECK(data.hWVTStateData == reinterpret_cast<HANDLE>(42));
    info.hFile = file; info.pcwszFilePath = nullptr; // handle-only verification
    CHECK(verify_spotify_file(nullptr, nullptr, &data, capture_trust) == 123);
    expect_redirect = false;
    data.cbStruct = sizeof(data) + 8;
    CHECK(verify_spotify_file(nullptr, nullptr, &data, capture_trust) == 123);
    data.cbStruct = sizeof(data); info.cbStruct = sizeof(info) + 8;
    CHECK(verify_spotify_file(nullptr, nullptr, &data, capture_trust) == 123);
    info.cbStruct = 0;
    CHECK(verify_spotify_file(nullptr, nullptr, &data, capture_trust) == 123);
    info.cbStruct = sizeof(info); data.dwStateAction = WTD_STATEACTION_CLOSE;
    CHECK(verify_spotify_file(nullptr, nullptr, &data, capture_trust) == 123);
    data.dwStateAction = WTD_STATEACTION_VERIFY;
    info.hFile = nullptr; info.pcwszFilePath = L"C:\\unrelated\\chrome_elf.dll";
    CHECK(verify_spotify_file(nullptr, nullptr, &data, capture_trust) == 123);
    data.dwUnionChoice = WTD_CHOICE_CATALOG; data.pCatalog = nullptr;
    CHECK(verify_spotify_file(nullptr, nullptr, &data, capture_trust) == 123);
    expected_trust_data = nullptr;
    CHECK(verify_spotify_file(nullptr, nullptr, nullptr, capture_trust) == 123);
    CloseHandle(file);
    CHECK(DeleteFileW(proxy)); CHECK(DeleteFileW(ORIGINAL_CHROME_ELF_DLL)); CHECK(DeleteFileW(CONFIG_FILEW));
    CHECK(SetCurrentDirectoryW(old_directory));
}

int main()
{
    import_tests(); pattern_tests(); guard_tests(); path_and_trust_tests();
    std::printf("PASS: %d native checks (imports, guards, patches, Unicode paths, trust redirection).\n", checks);
}
