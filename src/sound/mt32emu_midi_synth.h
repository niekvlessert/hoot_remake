#pragma once

#include <memory>
#include <string>

#include "sound/midi_synth.h"

namespace hoot {

enum class Mt32EmuModel {
    MT32,
    CM32L,
};

class Mt32EmuMidiSynth final : public MidiSynth {
public:
    explicit Mt32EmuMidiSynth(Mt32EmuModel model);
    ~Mt32EmuMidiSynth() override;

    bool open(int sample_rate, const std::string& rom_path, std::string& error) override;
    void close() override;
    void reset() override;
    bool active() const override;
    const char* backend_name() const override;
    const std::string& soundfont_path() const override;
    void short_message(uint8_t status, uint8_t data1, uint8_t data2, uint8_t size) override;
    void sysex(const std::vector<uint8_t>& data) override;
    int render_s16(int16_t* interleaved_stereo, int frames) override;

    static std::string find_default_rom_path(Mt32EmuModel model);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace hoot
