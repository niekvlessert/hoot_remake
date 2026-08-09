#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>

namespace hoot::utf8 {

inline bool is_continuation(unsigned char ch)
{
    return (ch & 0xc0u) == 0x80u;
}

inline std::size_t sequence_length(unsigned char lead)
{
    if (lead < 0x80u) return 1;
    if ((lead & 0xe0u) == 0xc0u) return 2;
    if ((lead & 0xf0u) == 0xe0u) return 3;
    if ((lead & 0xf8u) == 0xf0u) return 4;
    return 0;
}

// Return the largest prefix <= max_bytes that ends at a UTF-8 codepoint
// boundary. The catalog contract is UTF-8; this protects fixed-size C ABI
// buffers from turning a valid string into invalid UTF-8 when truncating.
inline std::size_t safe_prefix_bytes(std::string_view text, std::size_t max_bytes)
{
    if (text.size() <= max_bytes) return text.size();
    if (max_bytes == 0) return 0;

    std::size_t end = max_bytes;
    while (end > 0 && is_continuation(static_cast<unsigned char>(text[end]))) {
        --end;
    }

    if (end == max_bytes) return end;
    if (end == 0) return 0;

    const auto needed = sequence_length(static_cast<unsigned char>(text[end]));
    if (needed == 0 || end + needed > max_bytes) return end;
    return max_bytes;
}

inline void append_codepoint(std::string& out, std::uint32_t codepoint)
{
    if (codepoint > 0x10ffffu || (codepoint >= 0xd800u && codepoint <= 0xdfffu)) {
        codepoint = 0xfffdu;
    }
    if (codepoint <= 0x7fu) {
        out.push_back(static_cast<char>(codepoint));
    } else if (codepoint <= 0x7ffu) {
        out.push_back(static_cast<char>(0xc0u | (codepoint >> 6)));
        out.push_back(static_cast<char>(0x80u | (codepoint & 0x3fu)));
    } else if (codepoint <= 0xffffu) {
        out.push_back(static_cast<char>(0xe0u | (codepoint >> 12)));
        out.push_back(static_cast<char>(0x80u | ((codepoint >> 6) & 0x3fu)));
        out.push_back(static_cast<char>(0x80u | (codepoint & 0x3fu)));
    } else {
        out.push_back(static_cast<char>(0xf0u | (codepoint >> 18)));
        out.push_back(static_cast<char>(0x80u | ((codepoint >> 12) & 0x3fu)));
        out.push_back(static_cast<char>(0x80u | ((codepoint >> 6) & 0x3fu)));
        out.push_back(static_cast<char>(0x80u | (codepoint & 0x3fu)));
    }
}

inline bool decode_next(std::string_view text, std::size_t& offset, std::uint32_t& codepoint)
{
    if (offset >= text.size()) return false;
    const auto lead = static_cast<unsigned char>(text[offset]);
    const auto length = sequence_length(lead);
    if (length == 0 || offset + length > text.size()) {
        codepoint = 0xfffdu;
        ++offset;
        return true;
    }
    if (length == 1) {
        codepoint = lead;
        ++offset;
        return true;
    }

    std::uint32_t value = lead & ((1u << (7u - static_cast<unsigned>(length))) - 1u);
    for (std::size_t i = 1; i < length; ++i) {
        const auto ch = static_cast<unsigned char>(text[offset + i]);
        if (!is_continuation(ch)) {
            codepoint = 0xfffdu;
            ++offset;
            return true;
        }
        value = (value << 6) | (ch & 0x3fu);
    }

    const std::uint32_t minimum = length == 2 ? 0x80u : (length == 3 ? 0x800u : 0x10000u);
    if (value < minimum || value > 0x10ffffu || (value >= 0xd800u && value <= 0xdfffu)) {
        codepoint = 0xfffdu;
        ++offset;
        return true;
    }

    codepoint = value;
    offset += length;
    return true;
}

inline std::string debug_ascii_fallback(std::string_view text)
{
    std::string out;
    out.reserve(text.size());
    std::size_t offset = 0;
    while (offset < text.size()) {
        std::uint32_t cp = 0;
        decode_next(text, offset, cp);
        if (cp == '\t') out.push_back(' ');
        else if (cp >= 32 && cp <= 126) out.push_back(static_cast<char>(cp));
        else out.push_back('?');
    }
    return out;
}

template <std::size_t N>
inline void copy_c_string(char (&dest)[N], std::string_view source)
{
    static_assert(N > 0, "destination must have room for a terminator");
    const auto count = safe_prefix_bytes(source, N - 1);
    if (count != 0) std::memcpy(dest, source.data(), count);
    dest[count] = '\0';
}

} // namespace hoot::utf8
