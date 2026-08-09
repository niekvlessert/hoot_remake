#pragma once

#include <memory>
#include <string>

#include "sound/midi_synth.h"

namespace hoot {

// High-level Roland CM-32P PCM-section synthesizer.  The firmware/control ROM
// is deliberately not emulated: Hoot consumes the documented MIDI interface
// and renders the user-supplied scrambled PCM ROM dumps directly.
class Cm32pMidiSynth final : public MidiSynth {
public:
    explicit Cm32pMidiSynth(std::string card_model = {});
    ~Cm32pMidiSynth() override;

    bool open(int sample_rate, const std::string& rom_path, std::string& error) override;
    void close() override;
    void reset() override;
    bool active() const override;
    const char* backend_name() const override;
    const std::string& soundfont_path() const override;
    void short_message(uint8_t status, uint8_t data1, uint8_t data2, uint8_t size) override;
    void sysex(const std::vector<uint8_t>& data) override;
    int render_s16(int16_t* interleaved_stereo, int frames) override;

    static std::string find_default_rom_path();

    // Test/diagnostic counters; they do not participate in playback.
    uint64_t debug_note_on_count() const;
    uint64_t debug_sysex_write_count() const;
    size_t debug_active_voice_count() const;

    // Runtime diagnostics used to warn when a pack explicitly expects an
    // SN-U110 expansion card that is not installed.
    bool card_requested() const;
    bool card_loaded() const;
    const std::string& card_model() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace hoot
