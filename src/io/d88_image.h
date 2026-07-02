#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace hoot {

class D88Image {
public:
    bool open(const std::filesystem::path& path, std::string& error);
    std::vector<uint8_t> read_data(int disk_index,
                                   int track_index,
                                   int c,
                                   int h,
                                   int r,
                                   int n,
                                   int min_record,
                                   int max_record,
                                   size_t size,
                                   std::string& error) const;

private:
    struct Disk {
        size_t offset = 0;
        size_t size = 0;
        std::vector<uint32_t> track_offsets;
    };

    int read_sector(const Disk& disk,
                    int track_index,
                    int c,
                    int h,
                    int r,
                    int n,
                    uint8_t* output,
                    size_t size) const;

    std::filesystem::path path_;
    std::vector<uint8_t> bytes_;
    std::vector<Disk> disks_;
};

} // namespace hoot
