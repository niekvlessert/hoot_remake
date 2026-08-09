#include <cassert>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "sound/mt32emu_midi_synth.h"

#if defined(_WIN32)
#include <cstdlib>
static void set_env(const char* k, const std::string& v) { _putenv_s(k, v.c_str()); }
#else
#include <cstdlib>
static void set_env(const char* k, const std::string& v) { setenv(k, v.c_str(), 1); }
#endif

static void touch(const std::filesystem::path& p)
{
    std::ofstream f(p, std::ios::binary); f.put('\0');
}

int main(int argc, char** argv)
{
    assert(argc == 2);
    set_env("HOOT_MT32EMU_LIBRARY", argv[1]);

    const auto root = std::filesystem::temp_directory_path() / "hoot_mt32emu_test";
    std::filesystem::remove_all(root);
    const auto mt = root / "mt32";
    const auto cm = root / "cm32l";
    std::filesystem::create_directories(mt);
    std::filesystem::create_directories(cm);
    touch(mt / "control_mt32.rom"); touch(mt / "pcm_mt32.rom");
    touch(cm / "control_cm32l.rom"); touch(cm / "pcm_cm32l.rom");

    std::string error;
    {
        hoot::Mt32EmuMidiSynth synth(hoot::Mt32EmuModel::MT32);
        assert(synth.open(44100, mt.string(), error));
        assert(synth.active());
        assert(std::string(synth.backend_name()) == "munt-mt32");
        synth.short_message(0x90, 60, 100, 3);
        synth.sysex({0x41, 0x10, 0x16, 0x12, 0x7f, 0x00, 0x00, 0x01});
        std::vector<int16_t> audio(32 * 2);
        assert(synth.render_s16(audio.data(), 32) == 32);
        assert(audio[0] == 101);       // one short message reached the mock
        assert(audio[1] == 211);       // one SysEx, framed with F0/F7
        synth.reset();
        synth.short_message(0xc0, 7, 0, 2);
        synth.short_message(0x90, 64, 80, 3);
        assert(synth.render_s16(audio.data(), 1) == 1);
        assert(audio[0] == 102);       // reset reopened synth and cleared mock count
    }
    {
        hoot::Mt32EmuMidiSynth synth(hoot::Mt32EmuModel::CM32L);
        error.clear();
        assert(synth.open(48000, cm.string(), error));
        assert(std::string(synth.backend_name()) == "munt-cm32l");
    }
    {
        hoot::Mt32EmuMidiSynth synth(hoot::Mt32EmuModel::CM32L);
        error.clear();
        assert(!synth.open(44100, mt.string(), error));
        assert(error.find("CM-32L") != std::string::npos);
    }

    std::filesystem::remove_all(root);
    return 0;
}
