#pragma once
#include "pch.h"
#include "config.h"
#include "../Shared/module_paths.h"
#define USE_APC

inline HMODULE hook_module = nullptr;
inline wchar_t ORIGINAL_CHROME_ELF_DLL[bts::path_capacity]{};
inline wchar_t CONFIG_FILEW[bts::path_capacity]{};
inline wchar_t LOG_FILEW[bts::path_capacity]{};

inline bool initialize_hook_paths(HMODULE module) noexcept
{
    hook_module = module;
    return bts::path_beside_module(module, L"chrome_elf_required.dll", ORIGINAL_CHROME_ELF_DLL) &&
        bts::path_beside_module(module, L"config.ini", CONFIG_FILEW) &&
        bts::path_beside_module(module, L"blockthespot.log", LOG_FILEW);
}

constexpr size_t SHARED_BUFFER_SIZE = 1024; // increase if need.
inline char shared_buffer[SHARED_BUFFER_SIZE];

constexpr size_t MAX_CEF_BLOCK_LIST = 5;
constexpr size_t MAX_CEF_BUFFER_MODIFY_LIST = 10;
constexpr size_t MAX_URL_LEN = 50;

inline size_t CEF_REQUEST_GET_URL_OFFSET = 0x30;
inline size_t CEF_ZIP_READER_GET_FILE_NAME_OFFSET = 0x48;
inline size_t CEF_ZIP_READER_GET_READ_FILE_OFFSET = 0x70;

VOID CALLBACK bts_main(ULONG_PTR param);
