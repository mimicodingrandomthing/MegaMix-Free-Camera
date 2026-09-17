#include "sigscan.h"
#include <windows.h>
#include <vector>
#include <sstream>
#include <string>
#include <cstring>

static std::vector<int> parse_pattern(const char* pattern) {
    std::vector<int> out;
    std::istringstream ss(pattern);
    std::string token;

    while (ss >> token) {
        if (token == "?" || token == "??") {
            out.push_back(-1);
        } else {
            out.push_back(std::strtoul(token.c_str(), nullptr, 16) & 0xFF);
        }
    }
    return out;
}

uintptr_t FindPattern(uintptr_t base, size_t size, const char* pattern) {
    const auto bytes = parse_pattern(pattern);
    if (bytes.empty() || bytes.size() > size)
        return 0;

    const auto* p = reinterpret_cast<const unsigned char*>(base);

    for (size_t i = 0; i <= size - bytes.size(); ++i) {
        bool ok = true;
        for (size_t j = 0; j < bytes.size(); ++j) {
            if (bytes[j] != -1 && p[i + j] != bytes[j]) {
                ok = false;
                break;
            }
        }
        if (ok)
            return base + i;
    }
    return 0;
}

uintptr_t FindPatternInModule(const wchar_t* moduleName, const char* pattern) {
    HMODULE mod = GetModuleHandleW(moduleName);
    if (!mod)
        return 0;

    auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(mod);
    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS64*>(
        reinterpret_cast<unsigned char*>(mod) + dos->e_lfanew);

    return FindPattern(
        reinterpret_cast<uintptr_t>(mod),
        nt->OptionalHeader.SizeOfImage,
        pattern);
}
