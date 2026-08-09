#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "config/hoot_catalog.h"
#include "drivers/driver_registry.h"

#if defined(_WIN32)
static void set_env(const char* key, const std::string& value) { _putenv_s(key, value.c_str()); }
static void unset_env(const char* key) { _putenv_s(key, ""); }
#else
static void set_env(const char* key, const std::string& value) { setenv(key, value.c_str(), 1); }
static void unset_env(const char* key) { unsetenv(key); }
#endif

static void touch(const std::filesystem::path& path)
{
    std::ofstream file(path, std::ios::binary);
    file.put('\0');
}

static hoot::HootEntry make_entry(int midiout_type, const std::string& title = {})
{
    hoot::HootEntry entry;
    entry.id = midiout_type == 2 ? "synthetic-pc98-munt" : "synthetic-pc98-midi";
    entry.title = title.empty() ? entry.id : title;
    entry.driver_name = "pc98dos/beep";
    entry.archive = "pc98_midi_synthetic";
    entry.options["midiout"] = 1;
    entry.options["midiout_type"] = midiout_type;
    entry.assets.push_back({"file", "miditest.com", {}, UINT32_MAX, 0, false});
    entry.assets.push_back({"file", "track.dat", {}, 1, 0, false});
    entry.assets.push_back({"shell", "miditest", {}, 0, 0, false});
    entry.tracks.push_back({1, "synthetic PC-98 MPU-401 MIDI", {}});
    return entry;
}

static bool run_case(const hoot::HootEntry& entry, const char* packs_dir, bool require_synth,
                     uint32_t expected_backend_kind, std::string& failure,
                     const char* expected_warning = nullptr)
{
    auto driver = hoot::DriverRegistry::instance().create(entry);
    if (!driver) {
        failure = "driver creation failed";
        return false;
    }
    std::string error;
    if (driver->load(entry, packs_dir, 44100, error) != HOOT_OK) {
        failure = "load failed: " + error;
        return false;
    }
    if (driver->select_track(entry, 0, error) != HOOT_OK) {
        failure = "select failed: " + error;
        return false;
    }
    std::vector<int16_t> audio(4410 * 2, 0);
    driver->render_s16(audio.data(), 4410);

    int peak = 0;
    size_t nonzero = 0;
    for (const auto v : audio) {
        peak = std::max(peak, std::abs(static_cast<int>(v)));
        if (v != 0) ++nonzero;
    }
    HootTrackInfo info{};
    driver->fill_track_info(entry, 0, info);
    const bool base_ok = info.debug_midiout_type == static_cast<uint32_t>(entry.options.at("midiout_type"))
        && info.debug_midi_bytes_enqueued == 5
        && info.debug_midi_bytes_transmitted == 5
        && info.debug_midi_program_changes == 1
        && info.debug_midi_note_ons == 1
        && info.debug_midi_malformed_bytes == 0
        && info.debug_unsupported_opcodes == 0;
    const bool synth_ok = !require_synth || (info.debug_midi_backend_active != 0
        && info.debug_midi_backend_kind == expected_backend_kind
        && info.debug_midi_synth_frames != 0 && peak >= 10 && nonzero >= 100);
    const bool warning_ok = expected_warning == nullptr
        || (expected_warning[0] == '\0' ? info.warning[0] == '\0'
                                           : std::strstr(info.warning, expected_warning) != nullptr);
    if (!base_ok || !synth_ok || !warning_ok) {
        failure = "bad integrated MPU result: type=" + std::to_string(info.debug_midiout_type)
            + " enqueued=" + std::to_string(info.debug_midi_bytes_enqueued)
            + " tx=" + std::to_string(info.debug_midi_bytes_transmitted)
            + " programs=" + std::to_string(info.debug_midi_program_changes)
            + " notes=" + std::to_string(info.debug_midi_note_ons)
            + " malformed=" + std::to_string(info.debug_midi_malformed_bytes)
            + " backend=" + std::to_string(info.debug_midi_backend_active)
            + " kind=" + std::to_string(info.debug_midi_backend_kind)
            + " synth_frames=" + std::to_string(info.debug_midi_synth_frames)
            + " peak=" + std::to_string(peak) + " nonzero=" + std::to_string(nonzero)
            + " unsupported=" + std::to_string(info.debug_unsupported_opcodes)
            + " warning=\"" + info.warning + "\"";
        return false;
    }
    return true;
}

int main(int argc, char** argv)
{
    if (argc != 3) {
        std::cerr << "usage: pc98_midi_driver_test PACKS_DIR MOCK_MT32EMU\n";
        return 2;
    }

    // Preserve the pre-existing GS/SC-55 integration fixture. If a real
    // SoundFont is supplied by the test environment this still exercises
    // FluidSynth audio as well as transport.
    const char* test_soundfont = std::getenv("HOOT_TEST_SOUNDFONT");
    const bool test_synth = test_soundfont != nullptr && test_soundfont[0] != '\0';
    set_env("HOOT_X68K_MIDI_BACKEND", test_synth ? "fluidsynth" : "none");
    if (test_synth) set_env("HOOT_X68K_SOUNDFONT", test_soundfont);

    std::string failure;
    if (!run_case(make_entry(4), argv[1], test_synth, 1, failure)) {
        std::cerr << failure << "\n";
        return 3;
    }

    if (test_synth) {
        failure.clear();
        if (!run_case(make_entry(7), argv[1], true, 1, failure,
                      "Roland SC-88 target: FluidSynth compatibility rendering is active, not hardware-exact SC-88")) {
            std::cerr << failure << "\n";
            return 7;
        }
    }

    // Run the exact same PC-98 DOS guest/physical MPU fixture through the
    // MT-32 backend. The mock is ABI-compatible with libmt32emu, so this
    // verifies the host integration without redistributing Roland ROMs.
    const auto root = std::filesystem::temp_directory_path() / "hoot_pc98_munt_test";
    std::filesystem::remove_all(root);
    const auto mt32 = root / "mt32";
    const auto cm32l = root / "cm32l";
    std::filesystem::create_directories(mt32);
    std::filesystem::create_directories(cm32l);
    touch(mt32 / "control_mt32.rom");
    touch(mt32 / "pcm_mt32.rom");
    touch(cm32l / "control_cm32l.rom");
    touch(cm32l / "pcm_cm32l.rom");
    set_env("HOOT_MT32EMU_LIBRARY", argv[2]);
    set_env("HOOT_MT32_ROM_PATH", mt32.string());
    set_env("HOOT_CM32L_ROM_PATH", cm32l.string());
    set_env("HOOT_X68K_MIDI_BACKEND", "munt");

    failure.clear();
    const bool munt_ok = run_case(make_entry(2), argv[1], true, 3, failure, "");
    if (!munt_ok) {
        std::filesystem::remove_all(root);
        std::cerr << failure << "\n";
        return 4;
    }

    // In auto mode a CM-64 pack may fall back to the working CM-32L section
    // when the CM-32P PCM ROMs are absent. A pack that names an SN-U110 card
    // must explain both missing requirements and their audible consequence.
    unset_env("HOOT_CM32P_ROM_PATH");
    unset_env("HOOT_CM32P_CARD_ROM");
    unset_env("HOOT_CM32P_CARD_ROM_07");
    unset_env("HOOT_CM32P_CARD_ROM_10");
    set_env("HOOT_X68K_MIDI_BACKEND", "auto");
    failure.clear();
    const bool cm64_warning_ok = run_case(
        make_entry(3, "synthetic PC-98 CM-64"), argv[1], true, 4, failure,
        "~/.hoot/roms/cm32p (the default)");
    if (!cm64_warning_ok) {
        std::filesystem::remove_all(root);
        std::cerr << failure << "\n";
        return 5;
    }

    failure.clear();
    const bool cm64_card_warning_ok = run_case(
        make_entry(3, "synthetic PC-98 CM-64 + SN-U110-10"), argv[1], true, 4, failure,
        "cm32p_card_rom_10=roms/cm32p/SN-U110-10.bin");
    std::filesystem::remove_all(root);
    if (!cm64_card_warning_ok) {
        std::cerr << failure << "\n";
        return 6;
    }
    return 0;
}
