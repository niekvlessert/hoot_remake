#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "sound/cm32p_midi_synth.h"
#include "sound/cm64_midi_synth.h"

#if defined(_WIN32)
static void set_env(const char* k, const std::string& v) { _putenv_s(k, v.c_str()); }
static void unset_env(const char* k) { _putenv_s(k, ""); }
#else
static void set_env(const char* k, const std::string& v) { setenv(k, v.c_str(), 1); }
static void unset_env(const char* k) { unsetenv(k); }
#endif

static uint32_t bitswap19(uint32_t value, const std::array<int,19>& src)
{
    uint32_t out = 0;
    for (size_t i = 0; i < src.size(); ++i) out |= ((value >> src[i]) & 1u) << (18 - i);
    return out;
}

static uint32_t scramble_addr(uint32_t offset)
{
    static constexpr std::array<int,19> bits = {
        18,17,14,16,15,9,13,12,8,10,7,11,3,1,2,6,5,4,0
    };
    return bitswap19(offset, bits);
}

static uint8_t unscramble_data(uint8_t value)
{
    static constexpr std::array<int,8> bits = {1,2,7,3,5,0,4,6};
    uint8_t out = 0;
    for (size_t i = 0; i < bits.size(); ++i) out = static_cast<uint8_t>(out | (((value >> bits[i]) & 1u) << (7 - i)));
    return out;
}

static uint8_t scramble_data(uint8_t logical)
{
    for (int raw = 0; raw < 256; ++raw) {
        if (unscramble_data(static_cast<uint8_t>(raw)) == logical) return static_cast<uint8_t>(raw);
    }
    assert(false);
    return 0;
}

static void write_scrambled(const std::filesystem::path& path, const std::vector<uint8_t>& logical)
{
    assert(logical.size() == 0x80000);
    std::vector<uint8_t> raw(logical.size(), 0xff);
    for (uint32_t proper = 0; proper < logical.size(); ++proper) {
        raw[scramble_addr(proper)] = scramble_data(logical[proper]);
    }
    std::ofstream f(path, std::ios::binary);
    f.write(reinterpret_cast<const char*>(raw.data()), static_cast<std::streamsize>(raw.size()));
    assert(f.good());
}

static void make_cm32p_roms(const std::filesystem::path& root)
{
    std::filesystem::create_directories(root);
    std::vector<uint8_t> ic18(0x80000, 0xff);
    std::vector<uint8_t> ic19(0x80000, 0xff);
    std::vector<uint8_t> ic20(0x80000, 0xff);

    constexpr uint32_t sample_start = 0x10000;
    for (size_t i = 0; i < 256; ++i) {
        uint8_t* p = &ic18[0x100 + i * 0x0a];
        p[0] = static_cast<uint8_t>(sample_start);
        p[1] = static_cast<uint8_t>(sample_start >> 8);
        p[2] = static_cast<uint8_t>((sample_start >> 16) & 0x07); // bank 0, normal loop
        p[3] = 0xff; p[4] = 0x00; // 256 samples
        p[5] = 0x00; p[6] = 0x01; // loop length 256
        p[7] = 0;
        p[8] = 60;
        p[9] = 0;
    }
    for (size_t i = 0; i < 128; ++i) {
        uint8_t* t = &ic18[0x1000 + i * 0x50];
        std::fill(t, t + 0x50, 0xff);
        const char name[10] = {'T','E','S','T','T','O','N','E',' ',' '};
        std::copy(name, name + 10, t);
        t[0x0a] = 0; // single
        std::fill(t + 0x10, t + 0x1b, 0xff);
        std::fill(t + 0x1b, t + 0x27, 0xff);
        t[0x1b] = 0;
    }
    // A continuous triangle in the differential source format. It loops back
    // near zero, which also catches wrong signed-delta decoding.
    for (int i = 0; i < 256; ++i) {
        const int8_t delta = i < 128 ? int8_t{8} : int8_t{-8};
        ic18[sample_start + static_cast<uint32_t>(i)] = static_cast<uint8_t>(delta);
    }

    write_scrambled(root / "test.ic18", ic18);
    write_scrambled(root / "test.ic19", ic19);
    write_scrambled(root / "test.ic20", ic20);
    write_scrambled(root / "test.card", ic18);
}

static std::vector<uint8_t> dt1(uint8_t a0, uint8_t a1, uint8_t a2, std::initializer_list<uint8_t> payload)
{
    std::vector<uint8_t> out{0x41,0x10,0x16,0x12,a0,a1,a2};
    out.insert(out.end(), payload.begin(), payload.end());
    unsigned sum = a0 + a1 + a2;
    for (const auto b : payload) sum += b;
    out.push_back(static_cast<uint8_t>((128 - (sum & 0x7f)) & 0x7f));
    return out;
}

static long audio_energy(const std::vector<int16_t>& audio)
{
    long energy = 0;
    for (const auto s : audio) energy += std::abs(static_cast<int>(s));
    return energy;
}

static void touch(const std::filesystem::path& p)
{
    std::ofstream f(p, std::ios::binary);
    f.put('\0');
}

int main(int argc, char** argv)
{
    assert(argc == 2);
    namespace fs = std::filesystem;
    const auto root = fs::temp_directory_path() / "hoot_cm32p_test";
    fs::remove_all(root);
    const auto pcm = root / "cm32p";
    const auto la = root / "cm32l";
    make_cm32p_roms(pcm);
    fs::create_directories(la);
    touch(la / "control_cm32l.rom");
    touch(la / "pcm_cm32l.rom");

    std::string error;
    {
        hoot::Cm32pMidiSynth synth;
        assert(synth.open(44100, pcm.string(), error));
        assert(synth.active());
        assert(std::string(synth.backend_name()) == "cm32p");

        // Default receive channels are 11..16. Channel 1 must be ignored.
        synth.short_message(0x90, 60, 100, 3);
        assert(synth.debug_note_on_count() == 0);
        synth.short_message(0x9a, 60, 100, 3);
        assert(synth.debug_note_on_count() == 1);
        assert(synth.debug_active_voice_count() == 1);
        std::vector<int16_t> audio(4096 * 2);
        assert(synth.render_s16(audio.data(), 4096) == 4096);
        assert(audio_energy(audio) > 10000);

        // CM-32P pan is reversed compared with GM: 127 is hard left.
        synth.short_message(0xba, 10, 127, 3);
        synth.short_message(0x9a, 64, 120, 3);
        std::fill(audio.begin(), audio.end(), 0);
        synth.render_s16(audio.data(), 2048);
        long left = 0, right = 0;
        for (size_t i = 0; i < audio.size(); i += 2) {
            left += std::abs(static_cast<int>(audio[i]));
            right += std::abs(static_cast<int>(audio[i + 1]));
        }
        assert(left > right * 4);

        // Documented system-area DT1 can remap part 1 from channel 11 to 1.
        const auto writes_before = synth.debug_sysex_write_count();
        synth.sysex(dt1(0x52, 0x00, 0x0a, {0x00}));
        assert(synth.debug_sysex_write_count() == writes_before + 1);
        const auto notes_before = synth.debug_note_on_count();
        synth.short_message(0x90, 67, 100, 3);
        assert(synth.debug_note_on_count() == notes_before + 1);

        // A bad Roland checksum must not alter state.
        auto bad = dt1(0x52, 0x00, 0x10, {0x00});
        bad.back() ^= 1;
        const auto bad_before = synth.debug_sysex_write_count();
        synth.sysex(bad);
        assert(synth.debug_sysex_write_count() == bad_before);
    }

    // A named catalog card is optional for opening the base CM-32P, but its
    // absence remains observable so the driver can warn only packs that
    // explicitly require that expansion card.
    unset_env("HOOT_CM32P_CARD_ROM");
    unset_env("HOOT_CM32P_CARD_ROM_07");
    unset_env("HOOT_CM32P_CARD_ROM_10");
    {
        hoot::Cm32pMidiSynth synth("10");
        error.clear();
        assert(synth.open(44100, pcm.string(), error));
        assert(synth.card_requested());
        assert(!synth.card_loaded());
        assert(synth.card_model() == "10");
    }

    // A named catalog card variant selects its dedicated ROM without requiring
    // users to swap the generic card path between tracks. The synthetic IC18
    // image also contains a valid card-format sample/tone table.
    set_env("HOOT_CM32P_CARD_ROM_10", (pcm / "test.card").string());
    {
        hoot::Cm32pMidiSynth synth("10");
        error.clear();
        assert(synth.open(44100, pcm.string(), error));
        assert(synth.card_requested());
        assert(synth.card_loaded());
        synth.sysex(dt1(0x50, 0x00, 0x00, {0x01, 0x00})); // part 1: card tone 0
        synth.short_message(0x9a, 60, 110, 3);
        std::vector<int16_t> audio(2048 * 2);
        assert(synth.render_s16(audio.data(), 2048) == 2048);
        assert(audio_energy(audio) > 10000);
    }

    // Full CM-64 composition: ABI-compatible Munt supplies CM-32L and the
    // same synthetic PCM ROMs supply CM-32P. This exercises open/reset/MIDI
    // fan-out and mixed rendering without redistributing Roland ROMs.
    set_env("HOOT_MT32EMU_LIBRARY", argv[1]);
    set_env("HOOT_CM32L_ROM_PATH", la.string());
    set_env("HOOT_CM32P_ROM_PATH", pcm.string());
    {
        hoot::Cm64MidiSynth synth;
        error.clear();
        assert(synth.open(48000, {}, error));
        assert(synth.active());
        assert(std::string(synth.backend_name()) == "munt-cm64");
        synth.short_message(0x9a, 60, 100, 3);
        std::vector<int16_t> audio(2048 * 2);
        assert(synth.render_s16(audio.data(), 2048) == 2048);
        assert(audio_energy(audio) > 10000);
        synth.reset();
    }

    fs::remove_all(root);
    return 0;
}
