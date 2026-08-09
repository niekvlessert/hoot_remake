#pragma once

#include <memory>
#include <string>

#include "sound/midi_synth.h"

namespace hoot {

// Full CM-64 software module: Munt supplies the CM-32L/LA section while the
// built-in Cm32pMidiSynth supplies the six-part PCM section.  Both see the
// same MIDI stream, matching the parallel sections of the physical CM-64.
class Cm64MidiSynth final : public MidiSynth {
public:
    explicit Cm64MidiSynth(std::string card_model = {});
    ~Cm64MidiSynth() override;

    bool open(int sample_rate, const std::string& ignored, std::string& error) override;
    void close() override;
    void reset() override;
    bool active() const override;
    const char* backend_name() const override;
    const std::string& soundfont_path() const override;
    void short_message(uint8_t status, uint8_t data1, uint8_t data2, uint8_t size) override;
    void sysex(const std::vector<uint8_t>& data) override;
    int render_s16(int16_t* interleaved_stereo, int frames) override;

    // Expose only the optional PCM-card state for pack-specific authenticity
    // warnings. The full CM-64 backend is active only when both LA and PCM
    // sections opened successfully.
    bool pcm_card_requested() const;
    bool pcm_card_loaded() const;
    const std::string& pcm_card_model() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace hoot
