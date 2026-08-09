#pragma once

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace hoot {

inline void enable_utf8_console()
{
#if defined(_WIN32)
    // Hoot's public text contract is UTF-8. Windows consoles otherwise inherit
    // an OEM/ANSI code page, which corrupts Japanese metadata on output.
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif
}

} // namespace hoot
