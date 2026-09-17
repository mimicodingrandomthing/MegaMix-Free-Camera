#pragma once
#include <cstddef>
#include <cstdint>

uintptr_t FindPattern(uintptr_t moduleBase, size_t moduleSize, const char* pattern);
uintptr_t FindPatternInModule(const wchar_t* moduleName, const char* pattern);
