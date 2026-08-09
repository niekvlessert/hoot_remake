#pragma once

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

#include "core/hoot_visual_state.h"

namespace hoot::visual {

template <size_t N>
inline void copy(char (&dst)[N], const std::string& value)
{
    const size_t n = std::min(value.size(), N - 1);
    std::memcpy(dst, value.data(), n);
    dst[n] = '\0';
}

template <size_t N>
inline void copy(char (&dst)[N], const char* value)
{
    copy(dst, value ? std::string(value) : std::string{});
}

inline void add_register(HootVisualState& out, const char* label, uint32_t value, int digits = 4)
{
    if (out.register_count >= HOOT_VISUAL_REGISTERS_MAX) return;
    auto& reg = out.registers[out.register_count++];
    copy(reg.label, label);
    std::snprintf(reg.value, sizeof(reg.value), "%0*X", digits, value);
}

inline HootVisualChannel* add_channel(HootVisualState& out,
                                      HootVisualChannelKind kind,
                                      int index,
                                      const std::string& label)
{
    if (out.channel_count >= HOOT_VISUAL_CHANNELS_MAX) return nullptr;
    auto& ch = out.channels[out.channel_count++];
    std::memset(&ch, 0, sizeof(ch));
    ch.kind = static_cast<int>(kind);
    ch.index = index;
    ch.midi_note = -1;
    ch.instrument = -1;
    ch.pan = 0;
    copy(ch.label, label);
    return &ch;
}

inline int frequency_to_midi(double hz)
{
    if (!(hz > 0.0) || !std::isfinite(hz)) return -1;
    const long note = std::lround(69.0 + 12.0 * std::log2(hz / 440.0));
    return static_cast<int>(std::clamp(note, 0l, 127l));
}

// YM2203/YM2608 FM F-number conversion. This is intended for visualisation,
// not synthesis: it converts the currently programmed block/F-number to the
// closest equal-tempered MIDI key for the keyboard display.
inline int opn_fnum_to_midi(uint16_t fnum, uint8_t block, double clock_hz)
{
    if (fnum == 0 || clock_hz <= 0.0) return -1;
    const double hz = (static_cast<double>(fnum) * clock_hz)
        / (144.0 * std::ldexp(1.0, 20 - static_cast<int>(block)));
    return frequency_to_midi(hz);
}

// YM2151 KC is laid out as octave in bits 6..4 and note code in 3..0. The
// chip's note code skips several values. Translate it to a chromatic key.
inline int ym2151_kc_to_midi(uint8_t kc)
{
    static constexpr int semitone[16] = {
        0, 1, 2, 3, 3, 4, 5, 6, 7, 8, 8, 9, 10, 10, 11, 11
    };
    const int octave = (kc >> 4) & 0x07;
    const int note = semitone[kc & 0x0f];
    return std::clamp(12 + octave * 12 + note, 0, 127);
}

inline int opn_pan(uint8_t b4)
{
    const bool left = (b4 & 0x80) != 0;
    const bool right = (b4 & 0x40) != 0;
    if (left && !right) return -64;
    if (!left && right) return 63;
    return 0;
}

inline int ym2151_pan(uint8_t reg20)
{
    const bool left = (reg20 & 0x80) != 0;
    const bool right = (reg20 & 0x40) != 0;
    if (left && !right) return -64;
    if (!left && right) return 63;
    return 0;
}

inline int inverse_tl_volume(uint8_t tl)
{
    return std::clamp(127 - static_cast<int>(tl & 0x7f), 0, 127);
}

} // namespace hoot::visual
