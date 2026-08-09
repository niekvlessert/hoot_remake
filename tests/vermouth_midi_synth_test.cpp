#include <algorithm>
#include <cassert>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "sound/vermouth_midi_synth.h"

#if defined(_WIN32)
static void set_env(const char* k, const std::string& v) { _putenv_s(k, v.c_str()); }
#else
static void set_env(const char* k, const std::string& v) { setenv(k, v.c_str(), 1); }
#endif

static bool nonzero(const std::vector<int16_t>& audio)
{
    return std::any_of(audio.begin(), audio.end(), [](int16_t v) { return v != 0; });
}

int main(int argc, char** argv)
{
    assert(argc == 3);
    const auto root = std::filesystem::temp_directory_path() / "hoot_vermouth_test";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);
    const auto sf2 = root / "test.sf2";
    { std::ofstream f(sf2, std::ios::binary); f << "mock"; }

    std::string error;
    std::vector<int16_t> audio(256 * 2);
    set_env("HOOT_VERMOUTH_LIBRARY", argv[1]);
    set_env("HOOT_VERMOUTH_ABI", "fluidsynth");
    set_env("HOOT_VERMOUTH_SOUNDFONT", sf2.string());
    {
        hoot::VermouthMidiSynth synth;
        assert(synth.open(44100, {}, error));
        assert(synth.active());
        assert(std::string(synth.backend_name()) == "vermouth");
        assert(synth.soundfont_path() == sf2.string());
        synth.short_message(0x90, 60, 100, 3);
        synth.sysex({0x7e, 0x7f, 0x09, 0x01});
        assert(synth.render_s16(audio.data(), 256) == 256);
        assert(nonzero(audio));
        synth.short_message(0x80, 60, 0, 3);
    }

    set_env("HOOT_VERMOUTH_LIBRARY", argv[2]);
    set_env("HOOT_VERMOUTH_ABI", "legacy");
    {
        hoot::VermouthMidiSynth synth;
        error.clear();
        assert(synth.open(48000, {}, error));
        assert(synth.soundfont_path().find("timidity.cfg") != std::string::npos);
        std::fill(audio.begin(), audio.end(), 0);
        synth.short_message(0x90, 67, 90, 3);
        assert(synth.render_s16(audio.data(), 256) == 256);
        assert(nonzero(audio));
    }

    std::filesystem::remove_all(root);
    return 0;
}
