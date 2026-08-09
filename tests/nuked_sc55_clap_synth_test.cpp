#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <vector>

#include "sound/nuked_sc55_clap_midi_synth.h"

namespace {
void set_env(const char* name, const char* value)
{
#if defined(_WIN32)
    _putenv_s(name, value);
#else
    setenv(name, value, 1);
#endif
}
}

int main(int argc, char** argv)
{
    assert(argc == 2);
    set_env("HOOT_X68K_NUKED_SC55_CLAP", argv[1]);
    set_env("HOOT_X68K_SC55_MODEL", "v1.21");

    hoot::NukedSc55ClapMidiSynth synth;
    std::string error;
    assert(synth.open(44100, {}, error));
    assert(synth.active());
    assert(std::string(synth.backend_name()) == "nuked-sc55-clap");
    assert(!synth.soundfont_path().empty());

    // Nuked's CLAP event ABI expects complete F0...F7 SysEx packets. The
    // MidiSynth abstraction intentionally carries only the body, so the host
    // adapter must add both delimiters.
    synth.sysex({0x41, 0x10, 0x42, 0x12, 0x40, 0x00, 0x7f, 0x00, 0x41});
    synth.short_message(0x90, 60, 100, 3);
    std::vector<int16_t> audio(256 * 2);
    assert(synth.render_s16(audio.data(), 256) == 256);
    bool nonzero = false;
    for (auto sample : audio) nonzero |= sample != 0;
    assert(nonzero);

    synth.short_message(0x80, 60, 0, 3);
    std::fill(audio.begin(), audio.end(), static_cast<int16_t>(123));
    assert(synth.render_s16(audio.data(), 256) == 256);
    for (auto sample : audio) assert(sample == 0);

    synth.reset();
    synth.close();
    assert(!synth.active());
    return 0;
}
