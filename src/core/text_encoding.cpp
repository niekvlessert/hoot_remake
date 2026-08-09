#include "core/text_encoding.h"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstring>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#elif defined(HOOT_HAVE_ICONV)
#include <iconv.h>
#endif

namespace hoot {
namespace {

std::string lower_ascii(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

std::string declared_encoding(std::string_view input)
{
    const auto probe_len = std::min<std::size_t>(input.size(), 512);
    std::string probe(input.substr(0, probe_len));
    const auto lowered = lower_ascii(probe);
    const auto xml_end = lowered.find("?>");
    const auto search_end = xml_end == std::string::npos ? lowered.size() : xml_end;
    const auto pos = lowered.find("encoding");
    if (pos == std::string::npos || pos >= search_end) return {};

    auto eq = lowered.find('=', pos + 8);
    if (eq == std::string::npos || eq >= search_end) return {};
    ++eq;
    while (eq < search_end && std::isspace(static_cast<unsigned char>(probe[eq]))) ++eq;
    if (eq >= search_end || (probe[eq] != '\'' && probe[eq] != '"')) return {};
    const char quote = probe[eq++];
    const auto end = probe.find(quote, eq);
    if (end == std::string::npos || end > search_end) return {};
    return lower_ascii(probe.substr(eq, end - eq));
}

bool is_shift_jis_name(const std::string& encoding)
{
    return encoding == "shift_jis" || encoding == "shift-jis" ||
           encoding == "sjis" || encoding == "cp932" ||
           encoding == "windows-31j" || encoding == "ms932";
}

#if defined(_WIN32)
bool cp932_to_utf8(std::string_view input, std::string& output, std::string& error)
{
    if (input.empty()) { output.clear(); return true; }
    const int in_size = static_cast<int>(input.size());
    const int wide_size = MultiByteToWideChar(932, 0, input.data(), in_size, nullptr, 0);
    if (wide_size <= 0) {
        error = "unable to decode Shift_JIS/CP932 XML";
        return false;
    }
    std::wstring wide(static_cast<std::size_t>(wide_size), L'\0');
    if (MultiByteToWideChar(932, 0, input.data(), in_size, wide.data(), wide_size) <= 0) {
        error = "unable to decode Shift_JIS/CP932 XML";
        return false;
    }
    const int utf8_size = WideCharToMultiByte(CP_UTF8, 0, wide.data(), wide_size,
                                               nullptr, 0, nullptr, nullptr);
    if (utf8_size <= 0) {
        error = "unable to encode legacy XML as UTF-8";
        return false;
    }
    output.resize(static_cast<std::size_t>(utf8_size));
    if (WideCharToMultiByte(CP_UTF8, 0, wide.data(), wide_size,
                            output.data(), utf8_size, nullptr, nullptr) <= 0) {
        error = "unable to encode legacy XML as UTF-8";
        return false;
    }
    return true;
}
#elif defined(HOOT_HAVE_ICONV)
bool cp932_to_utf8(std::string_view input, std::string& output, std::string& error)
{
    iconv_t cd = iconv_open("UTF-8", "CP932");
    if (cd == (iconv_t)-1) cd = iconv_open("UTF-8", "SHIFT_JIS");
    if (cd == (iconv_t)-1) {
        error = "iconv does not provide CP932/Shift_JIS conversion";
        return false;
    }

    output.assign(std::max<std::size_t>(32, input.size() * 3 + 16), '\0');
    char* in_ptr = const_cast<char*>(input.data());
    std::size_t in_left = input.size();
    std::size_t used = 0;

    while (true) {
        char* out_ptr = output.data() + used;
        std::size_t out_left = output.size() - used;
        errno = 0;
        const std::size_t result = iconv(cd, &in_ptr, &in_left, &out_ptr, &out_left);
        used = output.size() - out_left;
        if (result != static_cast<std::size_t>(-1)) break;
        if (errno == E2BIG) {
            output.resize(output.size() * 2 + 32);
            continue;
        }
        iconv_close(cd);
        error = "invalid Shift_JIS/CP932 sequence in XML";
        output.clear();
        return false;
    }

    iconv_close(cd);
    output.resize(used);
    return true;
}
#else
bool cp932_to_utf8(std::string_view, std::string& output, std::string& error)
{
    output.clear();
    error = "Shift_JIS/CP932 XML is unsupported in this build (no converter available)";
    return false;
}
#endif

} // namespace

bool normalize_xml_to_utf8(std::string_view input, std::string& output, std::string& error)
{
    error.clear();

    // UTF-8 BOM is legal but unnecessary once the data is in std::string form.
    if (input.size() >= 3 &&
        static_cast<unsigned char>(input[0]) == 0xef &&
        static_cast<unsigned char>(input[1]) == 0xbb &&
        static_cast<unsigned char>(input[2]) == 0xbf) {
        input.remove_prefix(3);
    }

    const auto encoding = declared_encoding(input);
    if (encoding.empty() || encoding == "utf-8" || encoding == "utf8" || encoding == "us-ascii") {
        output.assign(input.data(), input.size());
        return true;
    }
    if (is_shift_jis_name(encoding)) return cp932_to_utf8(input, output, error);

    error = "unsupported XML encoding: " + encoding;
    output.clear();
    return false;
}

} // namespace hoot
