#include "log.h"
#include <windows.h>
#include <cstdio>
#include <cstdarg>

namespace {
    FILE* g_file = nullptr;
}

void Log::init() {
    if (g_file) return;
    fopen_s(&g_file, "freecam.log", "a");
}

void Log::write(const char* fmt, ...) {
    if (!g_file) init();
    if (!g_file) return;

    SYSTEMTIME t{};
    GetLocalTime(&t);
    std::fprintf(g_file, "[%02u:%02u:%02u.%03u] ",
        t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);

    va_list ap;
    va_start(ap, fmt);
    std::vfprintf(g_file, fmt, ap);
    va_end(ap);

    std::fputc('\\n', g_file);
    std::fflush(g_file);
}
