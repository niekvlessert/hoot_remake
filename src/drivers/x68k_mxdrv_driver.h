#pragma once

#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <string>

#include "drivers/hoot_driver.h"

namespace hoot {

class X68kMxdrvDriver final : public HootDriver {
public:
    X68kMxdrvDriver();
    ~X68kMxdrvDriver() override;

    HootResult load(const HootEntry& entry, const std::string& packs_path,
                    int sample_rate, std::string& error) override;
    HootResult select_track(const HootEntry& entry, int track_index,
                            std::string& error) override;
    void reset() override;
    int render_s16(int16_t* interleaved_stereo, int frames) override;
    int render_float(float* interleaved_stereo, int frames) override;
    void fill_track_info(const HootEntry& entry, int track_index,
                         HootTrackInfo& out) const override;
    void fill_visual_state(const HootEntry& entry, int track_index,
                           HootVisualState& out) const override;
    const char* name() const override { return "x68k-mxdrv-mdxmini"; }

private:
    struct Impl;
    void close_song();
    void cleanup_temp();
    std::unique_ptr<Impl> impl_;
    int sample_rate_ = 44100;
    int selected_track_ = -1;
    uint64_t rendered_frames_ = 0;
    std::filesystem::path temp_dir_;
    std::map<uint32_t, std::filesystem::path> track_files_;
    std::string warning_;
};

} // namespace hoot
