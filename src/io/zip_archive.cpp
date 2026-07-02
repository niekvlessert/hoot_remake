#include "io/zip_archive.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <fstream>
#include <iterator>

#include <zlib.h>

namespace hoot {
namespace {

uint16_t read_u16(const std::vector<uint8_t>& data, size_t offset)
{
    return static_cast<uint16_t>(data[offset] | (data[offset + 1] << 8));
}

uint32_t read_u32(const std::vector<uint8_t>& data, size_t offset)
{
    return static_cast<uint32_t>(data[offset]
        | (data[offset + 1] << 8)
        | (data[offset + 2] << 16)
        | (data[offset + 3] << 24));
}

bool has_bytes(const std::vector<uint8_t>& data, size_t offset, size_t count)
{
    return offset <= data.size() && count <= data.size() - offset;
}

bool equal_ascii_case_insensitive(std::string_view a, std::string_view b)
{
    if (a.size() != b.size()) {
        return false;
    }
    for (size_t i = 0; i < a.size(); ++i) {
        const auto ca = static_cast<unsigned char>(a[i]);
        const auto cb = static_cast<unsigned char>(b[i]);
        if (std::tolower(ca) != std::tolower(cb)) {
            return false;
        }
    }
    return true;
}

} // namespace

bool ZipArchive::open(const std::filesystem::path& path, std::string& error)
{
    path_ = path;
    bytes_.clear();
    entries_.clear();

    std::ifstream input(path, std::ios::binary);
    if (!input) {
        error = "unable to open archive: " + path.string();
        return false;
    }
    bytes_ = std::vector<uint8_t>(
        std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>());

    if (bytes_.size() < 22) {
        error = "archive is too small: " + path.string();
        return false;
    }

    size_t eocd = std::string::npos;
    const size_t min_eocd = bytes_.size() >= 22 ? bytes_.size() - 22 : 0;
    const size_t max_comment = std::min<size_t>(bytes_.size(), 0xffff + 22);
    const size_t search_begin = bytes_.size() - max_comment;
    for (size_t pos = min_eocd + 1; pos-- > search_begin;) {
        if (has_bytes(bytes_, pos, 4) && read_u32(bytes_, pos) == 0x06054b50) {
            eocd = pos;
            break;
        }
        if (pos == 0) {
            break;
        }
    }
    if (eocd == std::string::npos || !has_bytes(bytes_, eocd, 22)) {
        error = "end of central directory not found: " + path.string();
        return false;
    }

    const auto entry_count = read_u16(bytes_, eocd + 10);
    const auto central_offset = read_u32(bytes_, eocd + 16);
    size_t offset = central_offset;

    for (uint16_t i = 0; i < entry_count; ++i) {
        if (!has_bytes(bytes_, offset, 46) || read_u32(bytes_, offset) != 0x02014b50) {
            error = "bad central directory in archive: " + path.string();
            return false;
        }

        Entry entry;
        entry.method = read_u16(bytes_, offset + 10);
        entry.compressed_size = read_u32(bytes_, offset + 20);
        entry.uncompressed_size = read_u32(bytes_, offset + 24);
        const auto name_length = read_u16(bytes_, offset + 28);
        const auto extra_length = read_u16(bytes_, offset + 30);
        const auto comment_length = read_u16(bytes_, offset + 32);
        entry.local_header_offset = read_u32(bytes_, offset + 42);

        if (!has_bytes(bytes_, offset + 46, name_length)) {
            error = "bad file name in archive: " + path.string();
            return false;
        }
        entry.name.assign(reinterpret_cast<const char*>(bytes_.data() + offset + 46), name_length);
        entries_.push_back(std::move(entry));

        offset += 46 + name_length + extra_length + comment_length;
    }

    return true;
}

bool ZipArchive::contains(std::string_view name) const
{
    return find(name) != nullptr;
}

std::vector<uint8_t> ZipArchive::read(std::string_view name, std::string& error) const
{
    const Entry* entry = find(name);
    if (entry == nullptr) {
        error = "archive member not found: " + std::string(name);
        return {};
    }
    if (!has_bytes(bytes_, entry->local_header_offset, 30)
        || read_u32(bytes_, entry->local_header_offset) != 0x04034b50) {
        error = "bad local file header for: " + std::string(name);
        return {};
    }

    const size_t header = entry->local_header_offset;
    const auto name_length = read_u16(bytes_, header + 26);
    const auto extra_length = read_u16(bytes_, header + 28);
    const size_t data_offset = header + 30 + name_length + extra_length;
    if (!has_bytes(bytes_, data_offset, entry->compressed_size)) {
        error = "truncated archive member: " + std::string(name);
        return {};
    }

    if (entry->method == 0) {
        return std::vector<uint8_t>(
            bytes_.begin() + static_cast<std::ptrdiff_t>(data_offset),
            bytes_.begin() + static_cast<std::ptrdiff_t>(data_offset + entry->compressed_size));
    }

    if (entry->method != 8) {
        error = "unsupported zip compression method for: " + std::string(name);
        return {};
    }

    std::vector<uint8_t> output(entry->uncompressed_size);
    z_stream stream{};
    stream.next_in = const_cast<Bytef*>(bytes_.data() + data_offset);
    stream.avail_in = entry->compressed_size;
    stream.next_out = output.data();
    stream.avail_out = entry->uncompressed_size;

    if (inflateInit2(&stream, -MAX_WBITS) != Z_OK) {
        error = "unable to initialize zip inflater";
        return {};
    }
    const int result = inflate(&stream, Z_FINISH);
    inflateEnd(&stream);

    if (result != Z_STREAM_END) {
        error = "unable to inflate archive member: " + std::string(name);
        return {};
    }

    return output;
}

const ZipArchive::Entry* ZipArchive::find(std::string_view name) const
{
    for (const auto& entry : entries_) {
        if (entry.name == name) {
            return &entry;
        }
    }
    for (const auto& entry : entries_) {
        if (equal_ascii_case_insensitive(entry.name, name)) {
            return &entry;
        }
    }
    return nullptr;
}

} // namespace hoot
