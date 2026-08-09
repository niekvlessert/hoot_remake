#include "sound/pc98_beep.h"

#include <algorithm>
#include <cmath>

namespace hoot {

namespace {

inline int16_t saturate16(double value)
{
    const long v = std::lround(value);
    return static_cast<int16_t>(std::clamp<long>(v, -32768, 32767));
}

} // namespace

bool Pc98Beep::initialize(uint32_t output_rate, uint32_t pit_clock)
{
    if (output_rate == 0 || pit_clock == 0) return false;
    output_rate_ = output_rate;
    pit_clock_ = pit_clock;
    reset();
    return true;
}

void Pc98Beep::reset()
{
    // PC-98 BIOS initializes the speaker PIT near 2 kHz and leaves the
    // buzzer inhibited. Drivers normally program the divider before enabling
    // it. 1229 is the closest integer divider at the 2.4576 MHz PIT clock.
    divider_ = static_cast<uint16_t>(std::max<uint32_t>(1u, pit_clock_ / 2000u));
    mode_ = 3;
    access_mode_ = 3;
    write_phase_ = 0;
    pending_lsb_ = 0;
    ppi_port_c_ = 0xf8;
    speaker_enabled_ = false;
    phase_ = 0.0;
    stats_ = {};
}

bool Pc98Beep::handles_port(uint16_t port) const
{
    switch (port) {
    case 0x35: // system 8255 port C
    case 0x37: // system 8255 control / bit set-reset
    case 0x73: // PIT channel 1 (speaker)
    case 0x77: // PIT control
    case 0x3fdb: // PC-98 PIT channel 1 alias
    case 0x3fdf: // PC-98 PIT control alias
        return true;
    default:
        return false;
    }
}

uint8_t Pc98Beep::read_port(uint16_t port) const
{
    if (port == 0x35) return ppi_port_c_;
    // Counter readback is uncommon in Hoot music drivers. Return the low byte
    // as a stable, useful approximation until a latch command is requested.
    if (port == 0x73 || port == 0x3fdb) return static_cast<uint8_t>(divider_ & 0xff);
    return 0xff;
}

void Pc98Beep::set_speaker_enabled(bool enabled)
{
    if (speaker_enabled_ == enabled) return;
    speaker_enabled_ = enabled;
    ++stats_.gate_changes;
    // Restarting the waveform at a deterministic phase avoids track-to-track
    // state leakage and mirrors the audible edge produced by gating hardware.
    if (enabled) phase_ = 0.0;
}

void Pc98Beep::commit_divider(uint16_t value)
{
    // 8253/8254 interpret a programmed count of zero as 65536. Keep zero in
    // state so frequency() can apply that rule without truncating 65536.
    if (divider_ != value) ++stats_.divider_changes;
    divider_ = value;
    const uint32_t effective = value == 0 ? 65536u : value;
    if (stats_.min_divider == 0 || effective < stats_.min_divider) stats_.min_divider = effective;
    stats_.max_divider = std::max(stats_.max_divider, effective);
}

void Pc98Beep::write_counter(uint8_t data)
{
    ++stats_.pit_data_writes;
    switch (access_mode_) {
    case 1:
        commit_divider(static_cast<uint16_t>((divider_ & 0xff00u) | data));
        break;
    case 2:
        commit_divider(static_cast<uint16_t>((divider_ & 0x00ffu) | (static_cast<uint16_t>(data) << 8)));
        break;
    case 3:
    default:
        if (write_phase_ == 0) {
            pending_lsb_ = data;
            write_phase_ = 1;
        } else {
            commit_divider(static_cast<uint16_t>(pending_lsb_ | (static_cast<uint16_t>(data) << 8)));
            write_phase_ = 0;
        }
        break;
    }
}

void Pc98Beep::apply_control(uint8_t data)
{
    ++stats_.pit_control_writes;
    const uint8_t channel = static_cast<uint8_t>((data >> 6) & 0x03);
    if (channel != 1) return; // PC-98 speaker is PIT channel 1
    const uint8_t access = static_cast<uint8_t>((data >> 4) & 0x03);
    if (access == 0) return; // latch command: no state change needed for playback
    access_mode_ = access;
    uint8_t mode = static_cast<uint8_t>((data >> 1) & 0x07);
    if (mode > 5) mode = static_cast<uint8_t>(mode - 4);
    mode_ = mode;
    write_phase_ = 0;
}

void Pc98Beep::write_port(uint16_t port, uint8_t data)
{
    switch (port) {
    case 0x73:
    case 0x3fdb:
        write_counter(data);
        return;
    case 0x77:
    case 0x3fdf:
        apply_control(data);
        return;
    case 0x35:
        ++stats_.ppi_writes;
        ppi_port_c_ = data;
        // PC3=1 inhibits the buzzer; PC3=0 enables it.
        set_speaker_enabled((data & 0x08) == 0);
        return;
    case 0x37:
        ++stats_.ppi_writes;
        if ((data & 0x80) == 0) {
            // 8255 bit set/reset command: bits 3..1 select Port-C bit, bit 0
            // chooses reset/set. PMDB uses 06h (reset PC3 -> sound on) and
            // 07h (set PC3 -> inhibit).
            const uint8_t bit = static_cast<uint8_t>((data >> 1) & 0x07);
            const uint8_t mask = static_cast<uint8_t>(1u << bit);
            if ((data & 1) != 0) ppi_port_c_ = static_cast<uint8_t>(ppi_port_c_ | mask);
            else ppi_port_c_ = static_cast<uint8_t>(ppi_port_c_ & ~mask);
            if (bit == 3) set_speaker_enabled((ppi_port_c_ & 0x08) == 0);
        }
        return;
    default:
        return;
    }
}

double Pc98Beep::frequency() const
{
    const uint32_t effective = divider_ == 0 ? 65536u : divider_;
    return effective == 0 ? 0.0 : static_cast<double>(pit_clock_) / static_cast<double>(effective);
}

void Pc98Beep::mix_s16(int16_t* interleaved_stereo, int frames, double gain)
{
    if (!interleaved_stereo || frames <= 0) return;
    stats_.rendered_frames += static_cast<uint64_t>(frames);
    if (!speaker_enabled_ || gain <= 0.0) return;

    const double freq = frequency();
    if (!(freq > 0.0) || output_rate_ == 0) return;
    const double step = freq / static_cast<double>(output_rate_);
    // The built-in PC-98 speaker is intentionally conservative in the global
    // mix. It is mono hardware, duplicated to both output channels.
    const double amplitude = 5200.0 * std::clamp(gain, 0.0, 4.0);
    for (int i = 0; i < frames; ++i) {
        // Mode 3 is the normal square-wave speaker mode. For other periodic
        // modes, a 50% duty approximation is preferable to silence and keeps
        // the programmed frequency exact.
        const double wave = phase_ < 0.5 ? amplitude : -amplitude;
        for (int ch = 0; ch < 2; ++ch) {
            const int idx = i * 2 + ch;
            interleaved_stereo[idx] = saturate16(static_cast<double>(interleaved_stereo[idx]) + wave);
        }
        phase_ += step;
        phase_ -= std::floor(phase_);
    }
    stats_.audible_frames += static_cast<uint64_t>(frames);
}

} // namespace hoot
