#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace hoot {

// Small framework-independent contract for memory-mapped arcade PCM chips.
// Board drivers own clocks, CPU scheduling, ROM banking, and command latches;
// devices only implement their registers and audio voices.
class ArcadePcmDevice {
public:
    virtual ~ArcadePcmDevice() = default;

    virtual bool initialize(uint32_t clock, uint32_t output_sample_rate) = 0;
    virtual void reset() = 0;
    virtual void set_rom(std::vector<uint8_t> rom) = 0;
    virtual uint16_t read16(uint32_t word_offset) = 0;
    virtual void write16(uint32_t word_offset, uint16_t data) = 0;
    virtual void render_s16(int16_t* interleaved_stereo, int frames) = 0;

    virtual size_t channel_count() const = 0;
    virtual bool channel_active(size_t channel) const = 0;
    virtual void set_channel_muted(size_t channel, bool muted) = 0;
};

} // namespace hoot
