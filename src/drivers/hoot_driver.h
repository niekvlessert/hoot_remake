#pragma once

#include <cstdint>
#include <string>

#include "config/hoot_catalog.h"
#include "core/hoot_errors.h"
#include "core/hoot_track_info.h"
#include "core/hoot_visual_state.h"

namespace hoot {

class HootDriver {
public:
    virtual ~HootDriver() = default;

    virtual HootResult load(const HootEntry& entry,
                            const std::string& packs_path,
                            int sample_rate,
                            std::string& error) = 0;
    virtual HootResult select_track(const HootEntry& entry,
                                    int track_index,
                                    std::string& error) = 0;
    virtual void reset() = 0;
    virtual int render_s16(int16_t* interleaved_stereo, int frames) = 0;
    virtual int render_float(float* interleaved_stereo, int frames) = 0;
    virtual void fill_track_info(const HootEntry& entry,
                                 int track_index,
                                 HootTrackInfo& out) const = 0;
    virtual void fill_visual_state(const HootEntry&, int, HootVisualState& out) const
    {
        out.abi_version = HOOT_VISUAL_ABI_VERSION;
        out.struct_size = sizeof(out);
    }
    // Host/UI channel muting uses the same kind/index pairs published in
    // HootVisualState. Drivers that can safely silence an individual hardware
    // voice implement these methods; unsupported adapters remain explicit.
    virtual bool channel_mute_supported(int, int) const { return false; }
    virtual bool set_channel_muted(int, int, bool) { return false; }
    virtual void clear_channel_mutes() {}
    virtual const char* name() const = 0;
};

} // namespace hoot
