#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "drivers/hoot_driver.h"

namespace hoot {

class MicrocabinPc98DosDriver final : public HootDriver {
public:
    HootResult load(const HootEntry& entry,
                    const std::string& packs_path,
                    int sample_rate,
                    std::string& error) override;
    HootResult select_track(const HootEntry& entry,
                            int track_index,
                            std::string& error) override;
    void reset() override;
    int render_s16(int16_t* interleaved_stereo, int frames) override;
    int render_float(float* interleaved_stereo, int frames) override;
    void fill_track_info(const HootEntry& entry,
                         int track_index,
                         HootTrackInfo& out) const override;
    const char* name() const override;

private:
    struct LoadedFile {
        std::string path;
        std::vector<uint8_t> data;
    };

    void clear();

    std::map<uint32_t, LoadedFile> files_by_slot_;
    std::vector<uint8_t> mmd_sys_;
    std::string shell_command_;
    std::string selected_bgm_path_;
    std::string selected_voice_path_;
    int sample_rate_ = 44100;
    int selected_track_ = 0;
    uint32_t selected_code_ = 0;
    bool loaded_ = false;
};

} // namespace hoot
