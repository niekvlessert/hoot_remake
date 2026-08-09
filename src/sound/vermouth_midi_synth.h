#pragma once

#include <memory>
#include <string>

#include "sound/midi_synth.h"

namespace hoot {

// Adapter for Hoot's historical Vermouth software-synth ABI.  Two module-
// creation ABIs exist in the wild:
//   legacy     midimod_create(UINT sample_rate), loads timidity.cfg/GUS patches
//   fluidsynth midimod_create(wchar_t* sf2, UINT sample_rate), modern drop-in
// Select with HOOT_VERMOUTH_ABI=legacy|fluidsynth (default: fluidsynth).
class VermouthMidiSynth final : public MidiSynth {
public:
    VermouthMidiSynth();
    ~VermouthMidiSynth() override;

    bool open(int sample_rate, const std::string& soundfont, std::string& error) override;
    void close() override;
    void reset() override;
    bool active() const override;
    const char* backend_name() const override;
    const std::string& soundfont_path() const override;
    void short_message(uint8_t status, uint8_t data1, uint8_t data2, uint8_t size) override;
    void sysex(const std::vector<uint8_t>& data) override;
    int render_s16(int16_t* interleaved_stereo, int frames) override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace hoot
