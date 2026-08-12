// SPDX-License-Identifier: BSD-3-Clause
// Based on MAME src/devices/sound/rf5c400.cpp.
// Copyright Ville Linde; improvements by the Hoot development team.

#include "sound/rf5c400.h"

#include <algorithm>
#include <cmath>

namespace hoot {
namespace {
constexpr uint16_t kTypeMask = 0x00c0;
constexpr uint16_t kType16 = 0x0000;
constexpr uint16_t kType8Low = 0x0040;
constexpr uint16_t kType8High = 0x0080;
}

bool Rf5c400::initialize(uint32_t clock, uint32_t output_sample_rate)
{
    if (clock == 0 || output_sample_rate == 0) return false;
    clock_ = clock;
    output_sample_rate_ = output_sample_rate;
    const double native_rate = static_cast<double>(clock_) / 384.0;
    native_to_output_ratio_ = native_rate / static_cast<double>(output_sample_rate_);

    double volume = 255.0;
    for (size_t i = 0; i < volume_table_.size(); ++i) {
        volume_table_[i] = static_cast<int>(volume);
        volume /= std::pow(10.0, (4.5 / (256.0 / 16.0)) / 20.0);
    }
    pan_table_.fill(0.0);
    for (size_t i = 0; i < 0x48; ++i) {
        pan_table_[i] = std::sqrt(static_cast<double>(0x47 - i)) / std::sqrt(71.0);
    }

    constexpr double attack_speed = 0.1;
    constexpr int min_attack = 0x02;
    constexpr int max_attack = 0x80;
    constexpr double decay_speed = 2.0;
    constexpr int min_decay = 0x20;
    constexpr int max_decay = 0x73;
    constexpr double release_speed = 0.7;
    constexpr int min_release = 0x20;
    constexpr int max_release = 0x54;
    double rate = 1.0 / (attack_speed * native_rate);
    for (int i = 0; i < min_attack; ++i) attack_table_[i] = 1.0;
    for (int i = min_attack; i < max_attack; ++i)
        attack_table_[i] = rate * (max_attack - i) / (max_attack - min_attack);
    for (int i = max_attack; i < 0x9f; ++i) attack_table_[i] = 0.0;
    rate = -5.0 / (decay_speed * native_rate);
    for (int i = 0; i < min_decay; ++i) decay_table_[i] = rate;
    for (int i = min_decay; i < max_decay; ++i)
        decay_table_[i] = rate * (max_decay - i) / (max_decay - min_decay);
    for (int i = max_decay; i < 0x9f; ++i) decay_table_[i] = 0.0;
    rate = -5.0 / (release_speed * native_rate);
    for (int i = 0; i < min_release; ++i) release_table_[i] = rate;
    for (int i = min_release; i < max_release; ++i)
        release_table_[i] = rate * (max_release - i) / (max_release - min_release);
    for (int i = max_release; i < 0x9f; ++i) release_table_[i] = 0.0;
    reset();
    return true;
}

void Rf5c400::reset()
{
    for (auto& channel : channels_) {
        const bool muted = channel.muted;
        channel = Channel{};
        channel.muted = muted;
    }
    status_ = 0;
    external_memory_address_ = 0;
    external_memory_data_ = 0;
    requested_channel_ = 0;
}

void Rf5c400::set_rom(std::vector<uint8_t> rom) { rom_ = std::move(rom); }

uint8_t Rf5c400::decode_envelope(uint8_t value)
{
    return (value & 0x80) ? static_cast<uint8_t>((value & 0x7f) + 0x1f) : value;
}

double Rf5c400::attack_step(const Channel& channel) const
{
    return attack_table_[decode_envelope(static_cast<uint8_t>(channel.attack >> 8))]
        * native_to_output_ratio_;
}
double Rf5c400::decay_step(const Channel& channel) const
{
    return decay_table_[decode_envelope(static_cast<uint8_t>(channel.decay >> 8))]
        * native_to_output_ratio_;
}
double Rf5c400::release_step(const Channel& channel) const
{
    return release_table_[decode_envelope(static_cast<uint8_t>(channel.release >> 8))]
        * native_to_output_ratio_;
}

uint16_t Rf5c400::read_rom_word(uint32_t byte_address) const
{
    if (byte_address >= rom_.size() || rom_.size() - byte_address < 2) return 0;
    return static_cast<uint16_t>(rom_[byte_address] | (rom_[byte_address + 1] << 8));
}

void Rf5c400::write_rom_word(uint32_t byte_address, uint16_t data)
{
    if (byte_address >= rom_.size() || rom_.size() - byte_address < 2) return;
    rom_[byte_address] = static_cast<uint8_t>(data);
    rom_[byte_address + 1] = static_cast<uint8_t>(data >> 8);
}

uint16_t Rf5c400::read16(uint32_t offset)
{
    if (offset < 0x400) {
        if (offset == 0x00) return status_;
        if (offset == 0x04) return 0;
        if (offset == 0x09) {
            const auto& channel = channels_[requested_channel_ & 0x1f];
            if (channel.envelope_phase == EnvelopePhase::None) return 0;
            const uint32_t start = ((channel.start_high & 0xff00u) << 8) | channel.start_low;
            return static_cast<uint16_t>(((static_cast<uint64_t>(channel.position) >> 16) - start) >> 6);
        }
        if (offset == 0x13) return read_rom_word(external_memory_address_ << 1);
        return 0;
    }
    return 0;
}

void Rf5c400::write16(uint32_t offset, uint16_t data)
{
    if (offset < 0x400) {
        switch (offset) {
        case 0x00: status_ = data; break;
        case 0x01: {
            auto& channel = channels_[data & 0x1f];
            switch (data & 0x60) {
            case 0x60:
                channel.position = static_cast<double>(
                    ((channel.start_high & 0xff00u) << 8) | channel.start_low) * 65536.0;
                channel.envelope_phase = EnvelopePhase::Attack;
                channel.envelope_level = 0.0;
                channel.envelope_step = attack_step(channel);
                break;
            case 0x40:
                if (channel.envelope_phase != EnvelopePhase::None) {
                    channel.envelope_phase = EnvelopePhase::Release;
                    channel.envelope_step = (channel.release & 0x0080)
                        ? 0.0 : release_step(channel);
                }
                break;
            default:
                channel.envelope_phase = EnvelopePhase::None;
                channel.envelope_level = 0.0;
                channel.envelope_step = 0.0;
                break;
            }
            break;
        }
        case 0x08: requested_channel_ = data & 0x1f; break;
        case 0x09: // Observed alias of the low external-memory address register.
        case 0x11:
            external_memory_address_ = (external_memory_address_ & ~0xffffu) | data;
            break;
        case 0x12:
            external_memory_address_ = (external_memory_address_ & 0xffffu)
                | (static_cast<uint32_t>(data) << 16);
            break;
        case 0x13: external_memory_data_ = data; break;
        case 0x14:
            if ((data & 3) == 3) write_rom_word(external_memory_address_ << 1, external_memory_data_);
            break;
        default: break;
        }
        return;
    }

    auto& channel = channels_[(offset >> 5) & 0x1f];
    switch (offset & 0x1f) {
    case 0x00: channel.start_high = data; break;
    case 0x01: channel.start_low = data; break;
    case 0x02:
        channel.frequency = data;
        channel.step = static_cast<uint64_t>((data & 0x1fff) << (data >> 13)) * 4;
        break;
    case 0x03: channel.end_low = data; break;
    case 0x04: channel.end_high_loop_high = data; break;
    case 0x05: channel.loop_low = data; break;
    case 0x06: channel.pan = data; break;
    case 0x07: channel.effect = data; break;
    case 0x08: channel.volume = data; break;
    case 0x09: channel.attack = data; break;
    case 0x0c: channel.decay = data; break;
    case 0x0e: channel.release = data; break;
    case 0x10: channel.cutoff = data; break;
    default: break;
    }
}

void Rf5c400::render_s16(int16_t* output, int frames)
{
    if (output == nullptr || frames <= 0) return;
    std::fill(output, output + frames * 2, int16_t{0});
    std::vector<int32_t> left(static_cast<size_t>(frames), 0);
    std::vector<int32_t> right(static_cast<size_t>(frames), 0);

    for (auto& channel : channels_) {
        const uint32_t start = ((channel.start_high & 0xff00u) << 8) | channel.start_low;
        const uint32_t end = ((channel.end_high_loop_high & 0xffu) << 16) | channel.end_low;
        const uint32_t loop = ((channel.end_high_loop_high & 0xff00u) << 8) | channel.loop_low;
        if (start == end || channel.envelope_phase == EnvelopePhase::None) continue;
        const uint8_t volume = static_cast<uint8_t>(channel.volume);
        const uint8_t left_pan = static_cast<uint8_t>(channel.pan);
        const uint8_t right_pan = static_cast<uint8_t>(channel.pan >> 8);
        const uint16_t type = (channel.volume >> 8) & kTypeMask;

        for (int frame = 0; frame < frames; ++frame) {
            if (channel.envelope_phase == EnvelopePhase::None) break;
            const auto word = read_rom_word(static_cast<uint32_t>(channel.position / 65536.0) << 1);
            int32_t sample = 0;
            if (type == kType16) sample = static_cast<int16_t>(word);
            else if (type == kType8Low) sample = static_cast<int16_t>(word << 8);
            else if (type == kType8High) sample = static_cast<int16_t>(word & 0xff00);
            if (sample & 0x8000) sample ^= 0x7fff;

            channel.envelope_level += channel.envelope_step;
            if (channel.envelope_phase == EnvelopePhase::Attack && channel.envelope_level >= 1.0) {
                channel.envelope_phase = EnvelopePhase::Decay;
                channel.envelope_level = 1.0;
                channel.envelope_step = ((channel.decay & 0x0080) || channel.decay == 0x100)
                    ? 0.0 : decay_step(channel);
            } else if ((channel.envelope_phase == EnvelopePhase::Decay
                        || channel.envelope_phase == EnvelopePhase::Release)
                       && channel.envelope_level <= 0.0) {
                channel.envelope_phase = EnvelopePhase::None;
                channel.envelope_level = 0.0;
                channel.envelope_step = 0.0;
            }

            sample = static_cast<int32_t>((sample * volume_table_[volume] >> 9)
                                          * channel.envelope_level);
            if (!channel.muted) {
                left[frame] += static_cast<int32_t>(sample * pan_table_[left_pan]);
                right[frame] += static_cast<int32_t>(sample * pan_table_[right_pan]);
            }
            channel.position += static_cast<double>(channel.step) * native_to_output_ratio_;
            if (static_cast<uint64_t>(channel.position) >> 16 > end) {
                channel.position -= static_cast<double>(loop) * 65536.0;
                channel.position = static_cast<double>(
                    static_cast<uint64_t>(channel.position) & 0xffffff0000ULL);
                if (channel.position < static_cast<double>(start) * 65536.0)
                    channel.position = static_cast<double>(start) * 65536.0;
            }
        }
    }

    for (int frame = 0; frame < frames; ++frame) {
        output[frame * 2] = static_cast<int16_t>(std::clamp(left[frame], -32768, 32767));
        output[frame * 2 + 1] = static_cast<int16_t>(std::clamp(right[frame], -32768, 32767));
    }
}

bool Rf5c400::channel_active(size_t channel) const
{
    return channel < channels_.size()
        && channels_[channel].envelope_phase != EnvelopePhase::None;
}

void Rf5c400::set_channel_muted(size_t channel, bool muted)
{
    if (channel < channels_.size()) channels_[channel].muted = muted;
}

} // namespace hoot
