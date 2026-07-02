#include "io/d88_image.h"

#include <algorithm>
#include <fstream>
#include <iterator>

namespace hoot {
namespace {

constexpr size_t kD88HeaderSize = 0x20 + (4 * 164);
constexpr size_t kD88TrackTable = 0x20;
constexpr size_t kD88SizeOffset = 0x1c;

bool has_bytes(const std::vector<uint8_t>& data, size_t offset, size_t count)
{
    return offset <= data.size() && count <= data.size() - offset;
}

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

} // namespace

bool D88Image::open(const std::filesystem::path& path, std::string& error)
{
    path_ = path;
    bytes_.clear();
    disks_.clear();

    std::ifstream input(path, std::ios::binary);
    if (!input) {
        error = "unable to open D88 image: " + path.string();
        return false;
    }
    bytes_ = std::vector<uint8_t>(
        std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>());

    size_t offset = 0;
    while (has_bytes(bytes_, offset, kD88HeaderSize)) {
        const auto disk_size = static_cast<size_t>(read_u32(bytes_, offset + kD88SizeOffset));
        if (disk_size < kD88HeaderSize || !has_bytes(bytes_, offset, disk_size)) {
            break;
        }

        Disk disk;
        disk.offset = offset;
        disk.size = disk_size;
        disk.track_offsets.resize(164);
        for (size_t i = 0; i < disk.track_offsets.size(); ++i) {
            disk.track_offsets[i] = read_u32(bytes_, offset + kD88TrackTable + (i * 4));
        }
        disks_.push_back(std::move(disk));
        offset += disk_size;
    }

    if (disks_.empty()) {
        error = "no disks found in D88 image: " + path.string();
        return false;
    }
    return true;
}

std::vector<uint8_t> D88Image::read_data(int disk_index,
                                         int track_index,
                                         int c,
                                         int h,
                                         int r,
                                         int n,
                                         int min_record,
                                         int max_record,
                                         size_t size,
                                         std::string& error) const
{
    if (disk_index < 0 || static_cast<size_t>(disk_index) >= disks_.size()) {
        error = "D88 disk index is outside the image";
        return {};
    }
    if (track_index < 0 || track_index >= 164 || min_record > max_record || n < 0) {
        error = "invalid D88 read request";
        return {};
    }

    std::vector<uint8_t> output(size);
    size_t written = 0;
    int current_track = track_index;
    int current_c = c;
    int current_h = h;
    int current_r = r;
    const int sector_size = 0x80 << n;

    while (written < size) {
        const auto todo = std::min<size_t>(static_cast<size_t>(sector_size), size - written);
        const int read = read_sector(disks_[disk_index],
                                     current_track,
                                     current_c,
                                     current_h,
                                     current_r,
                                     n,
                                     output.data() + written,
                                     todo);
        if (read != static_cast<int>(todo)) {
            error = "unable to read requested D88 sector data";
            return {};
        }
        written += todo;

        if (current_r == max_record) {
            ++current_track;
            if (current_h == 0) {
                current_h = 1;
            } else {
                current_h = 0;
                ++current_c;
            }
            current_r = min_record;
        } else {
            ++current_r;
        }
    }

    return output;
}

int D88Image::read_sector(const Disk& disk,
                          int track_index,
                          int c,
                          int h,
                          int r,
                          int n,
                          uint8_t* output,
                          size_t size) const
{
    if (track_index < 0 || static_cast<size_t>(track_index) >= disk.track_offsets.size()) {
        return 0;
    }
    const auto track_offset = static_cast<size_t>(disk.track_offsets[track_index]);
    if (track_offset == 0 || track_offset >= disk.size) {
        return 0;
    }

    size_t pos = disk.offset + track_offset;
    while (has_bytes(bytes_, pos, 16)) {
        const int sector_c = bytes_[pos + 0];
        const int sector_h = bytes_[pos + 1];
        const int sector_r = bytes_[pos + 2];
        const int sector_n = bytes_[pos + 3];
        const int sector_count = read_u16(bytes_, pos + 4);
        const size_t sector_size = read_u16(bytes_, pos + 0x0e);
        const size_t data_pos = pos + 16;

        if (sector_count == 0 || !has_bytes(bytes_, data_pos, sector_size)) {
            return 0;
        }
        if (sector_c == c && sector_h == h && sector_r == r && sector_n == n) {
            const auto count = std::min(size, sector_size);
            std::copy_n(bytes_.begin() + static_cast<std::ptrdiff_t>(data_pos),
                        count,
                        output);
            return static_cast<int>(count);
        }

        pos = data_pos + sector_size;
    }
    return 0;
}

} // namespace hoot
