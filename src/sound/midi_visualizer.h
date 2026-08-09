#pragma once

#include <array>
#include <algorithm>
#include <cstdint>

#include "sound/x68k_midi_transport.h"

namespace hoot {

class MidiVisualizer {
public:
    struct Channel {
        uint64_t keys_lo = 0;
        uint64_t keys_hi = 0;
        uint8_t last_note = 0;
        uint8_t velocity = 0;
        uint8_t volume = 100;
        uint8_t expression = 127;
        uint8_t pan = 64;
        uint8_t program = 0;
        int16_t pitch_bend = 0;
    };

    void reset() { channels_ = {}; }

    void message(const X68kMidiMessage& msg)
    {
        if (msg.kind != X68kMidiMessage::Kind::Channel || msg.status < 0x80 || msg.status >= 0xf0) return;
        const uint8_t chn = msg.status & 0x0f;
        auto& ch = channels_[chn];
        const uint8_t op = msg.status & 0xf0;
        if (op == 0x80 || (op == 0x90 && msg.data2 == 0)) {
            set_key(ch, msg.data1 & 0x7f, false);
            if (ch.last_note == (msg.data1 & 0x7f)) ch.velocity = 0;
        } else if (op == 0x90) {
            const uint8_t note = msg.data1 & 0x7f;
            set_key(ch, note, true);
            ch.last_note = note;
            ch.velocity = msg.data2 & 0x7f;
        } else if (op == 0xb0) {
            const uint8_t controller = msg.data1 & 0x7f;
            const uint8_t value = msg.data2 & 0x7f;
            if (controller == 7) ch.volume = value;
            else if (controller == 10) ch.pan = value;
            else if (controller == 11) ch.expression = value;
            else if (controller == 120 || controller == 123) { ch.keys_lo = 0; ch.keys_hi = 0; ch.velocity = 0; }
        } else if (op == 0xc0) {
            ch.program = msg.data1 & 0x7f;
        } else if (op == 0xe0) {
            ch.pitch_bend = static_cast<int16_t>(((msg.data2 & 0x7f) << 7) | (msg.data1 & 0x7f)) - 8192;
        }
    }

    const Channel& channel(size_t index) const { return channels_[index & 15u]; }

private:
    static void set_key(Channel& ch, uint8_t note, bool on)
    {
        uint64_t& mask = note < 64 ? ch.keys_lo : ch.keys_hi;
        const uint64_t bit = 1ull << (note & 63);
        if (on) mask |= bit; else mask &= ~bit;
    }

    std::array<Channel, 16> channels_{};
};

} // namespace hoot
