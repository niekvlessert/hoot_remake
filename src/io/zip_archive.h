#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace hoot {

class ZipArchive {
public:
    bool open(const std::filesystem::path& path, std::string& error);
    bool contains(std::string_view name) const;
    std::vector<uint8_t> read(std::string_view name, std::string& error) const;

private:
    struct Entry {
        std::string name;
        uint16_t method = 0;
        uint32_t compressed_size = 0;
        uint32_t uncompressed_size = 0;
        uint32_t local_header_offset = 0;
    };

    const Entry* find(std::string_view name) const;

    std::filesystem::path path_;
    std::vector<uint8_t> bytes_;
    std::vector<Entry> entries_;
};

} // namespace hoot
