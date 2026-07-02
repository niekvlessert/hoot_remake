#include "drivers/microcabin_pc98dos_driver.h"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <sstream>

#include "io/zip_archive.h"

namespace hoot {
namespace {

template <size_t N>
void copy_c_string(char (&dest)[N], const std::string& source)
{
    static_assert(N > 0, "destination must have room for a terminator");
    const auto count = std::min(source.size(), N - 1);
    std::memcpy(dest, source.data(), count);
    dest[count] = '\0';
}

std::string first_token(const std::string& text)
{
    const auto pos = text.find_first_of(" \t\r\n");
    if (pos == std::string::npos) {
        return text;
    }
    return text.substr(0, pos);
}

bool has_negative_offset(uint32_t offset)
{
    return offset == UINT32_MAX;
}

std::string hex_slot(uint32_t slot)
{
    std::ostringstream out;
    out << "0x" << std::hex << slot;
    return out.str();
}

} // namespace

HootResult MicrocabinPc98DosDriver::load(const HootEntry& entry,
                                         const std::string& packs_path,
                                         int sample_rate,
                                         std::string& error)
{
    clear();
    sample_rate_ = sample_rate;

    const auto archive_path = std::filesystem::path(packs_path) / (entry.archive + ".zip");
    ZipArchive archive;
    if (!archive.open(archive_path, error)) {
        return HOOT_ERROR_IO;
    }

    for (const auto& asset : entry.assets) {
        if (asset.type == "device") {
            auto driver_name = first_token(asset.path);
            auto data = archive.read(driver_name, error);
            if (!error.empty()) {
                return HOOT_ERROR_IO;
            }
            mmd_sys_ = std::move(data);
            continue;
        }
        if (asset.type == "shell") {
            shell_command_ = asset.path;
            continue;
        }
        if (asset.type != "file") {
            continue;
        }

        auto data = archive.read(asset.path, error);
        if (!error.empty()) {
            return HOOT_ERROR_IO;
        }
        if (asset.path == "MMD.SYS" || asset.path == "MMD2.SYS" || asset.path == "mmd.sys") {
            mmd_sys_ = data;
        }
        if (!has_negative_offset(asset.offset)) {
            files_by_slot_[asset.offset] = LoadedFile{asset.path, std::move(data)};
        }
    }

    if (mmd_sys_.empty()) {
        error = "pc98dos Microcabin entry did not provide MMD.SYS/MMD2.SYS";
        return HOOT_ERROR_NOT_FOUND;
    }
    if (shell_command_.empty()) {
        error = "pc98dos Microcabin entry did not provide a shell command";
        return HOOT_ERROR_NOT_FOUND;
    }

    loaded_ = true;
    return HOOT_OK;
}

HootResult MicrocabinPc98DosDriver::select_track(const HootEntry& entry,
                                                 int track_index,
                                                 std::string& error)
{
    if (!loaded_) {
        error = "pc98dos Microcabin driver is not loaded";
        return HOOT_ERROR_NOT_LOADED;
    }
    if (track_index < 0 || static_cast<size_t>(track_index) >= entry.tracks.size()) {
        error = "track index is outside the catalog track list";
        return HOOT_ERROR_INVALID_ARGUMENT;
    }

    selected_track_ = track_index;
    selected_code_ = entry.tracks[track_index].code;

    const uint32_t voice_slot = (selected_code_ >> 16) & 0xff;
    const uint32_t bgm_slot = selected_code_ & 0xffff;
    selected_voice_path_.clear();
    selected_bgm_path_.clear();

    const auto bgm = files_by_slot_.find(bgm_slot);
    if (bgm == files_by_slot_.end()) {
        error = "pc98dos Microcabin track references missing BGM slot "
            + hex_slot(bgm_slot);
        return HOOT_ERROR_NOT_FOUND;
    }
    selected_bgm_path_ = bgm->second.path;

    const auto voice = files_by_slot_.find(voice_slot);
    if (voice != files_by_slot_.end()) {
        selected_voice_path_ = voice->second.path;
    }

    error = "pc98dos Microcabin assets loaded for " + selected_bgm_path_;
    if (!selected_voice_path_.empty()) {
        error += " with voice " + selected_voice_path_;
    }
    error += ", but playback needs a PC-98 DOS/V30 host for "
        + shell_command_ + " and MMD.SYS INT D2 calls; that runtime is not implemented yet";
    return HOOT_ERROR_UNSUPPORTED;
}

void MicrocabinPc98DosDriver::reset()
{
    selected_track_ = 0;
    selected_code_ = 0;
    selected_bgm_path_.clear();
    selected_voice_path_.clear();
}

int MicrocabinPc98DosDriver::render_s16(int16_t* interleaved_stereo, int frames)
{
    if (interleaved_stereo == nullptr || frames < 0) {
        return 0;
    }
    std::fill(interleaved_stereo, interleaved_stereo + (frames * 2), int16_t{0});
    return frames;
}

int MicrocabinPc98DosDriver::render_float(float* interleaved_stereo, int frames)
{
    if (interleaved_stereo == nullptr || frames < 0) {
        return 0;
    }
    std::fill(interleaved_stereo, interleaved_stereo + (frames * 2), 0.0f);
    return frames;
}

void MicrocabinPc98DosDriver::fill_track_info(const HootEntry& entry,
                                              int track_index,
                                              HootTrackInfo& out) const
{
    std::memset(&out, 0, sizeof(out));
    out.track_index = track_index;
    out.sample_rate = sample_rate_;
    out.debug_cpu_cycles = mmd_sys_.size();
    out.debug_io_reads = files_by_slot_.size();
    out.debug_io_writes = selected_code_;
    copy_c_string(out.driver, name());
    if (track_index >= 0 && static_cast<size_t>(track_index) < entry.tracks.size()) {
        copy_c_string(out.title, entry.tracks[track_index].title);
    } else {
        copy_c_string(out.title, entry.title);
    }
}

const char* MicrocabinPc98DosDriver::name() const
{
    return "microcabin-pc98dos-opn";
}

void MicrocabinPc98DosDriver::clear()
{
    files_by_slot_.clear();
    mmd_sys_.clear();
    shell_command_.clear();
    selected_bgm_path_.clear();
    selected_voice_path_.clear();
    sample_rate_ = 44100;
    selected_track_ = 0;
    selected_code_ = 0;
    loaded_ = false;
}

} // namespace hoot
