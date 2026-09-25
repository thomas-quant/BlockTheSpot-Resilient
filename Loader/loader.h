#pragma once
#include "../Shared/module_paths.h"

inline HMODULE loader_module = nullptr;
bool load_original_chrome() noexcept;
