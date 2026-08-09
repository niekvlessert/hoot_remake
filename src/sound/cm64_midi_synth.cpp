#include "sound/cm64_midi_synth.h"

#include <algorithm>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "sound/cm32p_midi_synth.h"
#include "sound/mt32emu_midi_synth.h"

namespace hoot {

struct Cm64MidiSynth::Impl {
    std::unique_ptr<Mt32EmuMidiSynth> la;
    std::unique_ptr<Cm32pMidiSynth> pcm;
    std::string paths;
    std::vector<int16_t> la_mix;
    std::vector<int16_t> pcm_mix;
    std::string card_model;
};

Cm64MidiSynth::Cm64MidiSynth(std::string card_model) : impl_(std::make_unique<Impl>())
{
    impl_->card_model = std::move(card_model);
}
Cm64MidiSynth::~Cm64MidiSynth() { close(); }

bool Cm64MidiSynth::open(int sample_rate, const std::string& /*ignored*/, std::string& error)
{
    close();
    std::string la_error;
    std::string pcm_error;
    auto la = std::make_unique<Mt32EmuMidiSynth>(Mt32EmuModel::CM32L);
    if (!la->open(sample_rate, {}, la_error)) {
        error = "CM-64 LA section unavailable: " + la_error;
        return false;
    }
    auto pcm = std::make_unique<Cm32pMidiSynth>(impl_->card_model);
    if (!pcm->open(sample_rate, {}, pcm_error)) {
        la->close();
        error = "CM-64 PCM section unavailable: " + pcm_error;
        return false;
    }
    impl_->paths = la->soundfont_path() + ";" + pcm->soundfont_path();
    impl_->la = std::move(la);
    impl_->pcm = std::move(pcm);
    return true;
}

void Cm64MidiSynth::close()
{
    if (!impl_) return;
    if (impl_->la) impl_->la->close();
    if (impl_->pcm) impl_->pcm->close();
    impl_->la.reset();
    impl_->pcm.reset();
    impl_->paths.clear();
    impl_->la_mix.clear();
    impl_->pcm_mix.clear();
}

void Cm64MidiSynth::reset()
{
    if (impl_->la) impl_->la->reset();
    if (impl_->pcm) impl_->pcm->reset();
}

bool Cm64MidiSynth::active() const
{
    return impl_->la && impl_->pcm && impl_->la->active() && impl_->pcm->active();
}

const char* Cm64MidiSynth::backend_name() const { return "munt-cm64"; }
const std::string& Cm64MidiSynth::soundfont_path() const { return impl_->paths; }

void Cm64MidiSynth::short_message(uint8_t status, uint8_t data1, uint8_t data2, uint8_t size)
{
    if (impl_->la) impl_->la->short_message(status, data1, data2, size);
    if (impl_->pcm) impl_->pcm->short_message(status, data1, data2, size);
}

void Cm64MidiSynth::sysex(const std::vector<uint8_t>& data)
{
    if (impl_->la) impl_->la->sysex(data);
    if (impl_->pcm) impl_->pcm->sysex(data);
}

int Cm64MidiSynth::render_s16(int16_t* interleaved_stereo, int frames)
{
    if (!active() || interleaved_stereo == nullptr || frames <= 0) return 0;
    impl_->la_mix.assign(static_cast<size_t>(frames) * 2u, 0);
    impl_->pcm_mix.assign(static_cast<size_t>(frames) * 2u, 0);
    if (impl_->la->render_s16(impl_->la_mix.data(), frames) != frames) return 0;
    if (impl_->pcm->render_s16(impl_->pcm_mix.data(), frames) != frames) return 0;
    for (int i = 0; i < frames * 2; ++i) {
        const int mixed = static_cast<int>(impl_->la_mix[static_cast<size_t>(i)])
                        + static_cast<int>(impl_->pcm_mix[static_cast<size_t>(i)]);
        interleaved_stereo[i] = static_cast<int16_t>(std::clamp(mixed, -32768, 32767));
    }
    return frames;
}

bool Cm64MidiSynth::pcm_card_requested() const
{
    return impl_ && impl_->pcm && impl_->pcm->card_requested();
}

bool Cm64MidiSynth::pcm_card_loaded() const
{
    return impl_ && impl_->pcm && impl_->pcm->card_loaded();
}

const std::string& Cm64MidiSynth::pcm_card_model() const
{
    static const std::string empty;
    return impl_ && impl_->pcm ? impl_->pcm->card_model() : empty;
}

} // namespace hoot
