#include <cassert>
#include <cstring>
#include <string>

#include "core/utf8_util.h"

int main()
{
    const std::string japanese = u8"ABC日本語";

    // Byte limits that land inside a multi-byte character must roll back to
    // the previous complete UTF-8 codepoint.
    assert(hoot::utf8::safe_prefix_bytes(japanese, 3) == 3);
    assert(hoot::utf8::safe_prefix_bytes(japanese, 4) == 3);
    assert(hoot::utf8::safe_prefix_bytes(japanese, 5) == 3);
    assert(hoot::utf8::safe_prefix_bytes(japanese, 6) == 6);
    assert(hoot::utf8::safe_prefix_bytes(japanese, 7) == 6);

    char exact[7]{}; // 6 payload bytes + NUL: "ABC日"
    hoot::utf8::copy_c_string(exact, japanese);
    assert(std::string(exact) == u8"ABC日");

    char short_buffer[6]{}; // cannot fit any byte of 日 without splitting it
    hoot::utf8::copy_c_string(short_buffer, japanese);
    assert(std::string(short_buffer) == "ABC");

    assert(hoot::utf8::debug_ascii_fallback(u8"A日本B") == "A??B");

    std::string encoded;
    hoot::utf8::append_codepoint(encoded, 0x65e5); // 日
    assert(encoded == u8"日");
    hoot::utf8::append_codepoint(encoded, 0x1f3b5); // 🎵
    assert(encoded == u8"日🎵");

    return 0;
}
