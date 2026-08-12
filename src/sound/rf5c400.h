// SPDX-License-Identifier: BSD-3-Clause
// Standalone adaptation of MAME's RF5C400 device by Ville Linde, including
// improvements credited there to the Hoot development team.
#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include "sound/arcade_pcm_device.h"

namespace hoot {

class Rf5c400 final : public ArcadePcmDevice {
public:
    bool initialize(uint32_t clock, uint32_t output_sample_rate) override;
    void reset() override;
    void set_rom(std::vector<uint8_t> rom) override;
    uint16_t read16(uint32_t word_offset) override;
    void write16(uint32_t word_offset, uint16_t data) override;
    void render_s16(int16_t* interleaved_stereo, int frames) override;
    size_t channel_count() const override { return channels_.size(); }
    bool channel_active(size_t channel) const override;
    void set_channel_muted(size_t channel, bool muted) override;

private:
    enum class EnvelopePhase : uint8_t { None, Attack, Decay, Release };
    struct Channel {
        uint16_t start_high = 0;
        uint16_t start_low = 0;
        uint16_t frequency = 0;
        uint16_t end_low = 0;
        uint16_t end_high_loop_high = 0;
        uint16_t loop_low = 0;
        uint16_t pan = 0;
        uint16_t effect = 0;
        uint16_t volume = 0;
        uint16_t attack = 0;
        uint16_t decay = 0;
        uint16_t release = 0;
        uint16_t cutoff = 0;
        double position = 0.0; // 16.16 sample position
        uint64_t step = 0;
        EnvelopePhase envelope_phase = EnvelopePhase::None;
        double envelope_level = 0.0;
        double envelope_step = 0.0;
        bool muted = false;
    };

    static uint8_t decode_envelope(uint8_t value);
    uint16_t read_rom_word(uint32_t byte_address) const;
    void write_rom_word(uint32_t byte_address, uint16_t data);
    double attack_step(const Channel& channel) const;
    double decay_step(const Channel& channel) const;
    double release_step(const Channel& channel) const;

    uint32_t clock_ = 16934400;
    uint32_t output_sample_rate_ = 44100;
    double native_to_output_ratio_ = 1.0;
    std::array<Channel, 32> channels_{};
    std::array<int, 256> volume_table_{};
    std::array<double, 256> pan_table_{};
    std::array<double, 0x9f> attack_table_{};
    std::array<double, 0x9f> decay_table_{};
    std::array<double, 0x9f> release_table_{};
    std::vector<uint8_t> rom_;
    uint16_t status_ = 0;
    uint32_t external_memory_address_ = 0;
    uint16_t external_memory_data_ = 0;
    uint16_t requested_channel_ = 0;
};

} // namespace hoot
