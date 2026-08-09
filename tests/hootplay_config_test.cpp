#include <cassert>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

#include "config/hootplay_config.h"

int main()
{
    namespace fs = std::filesystem;
    const fs::path root = fs::temp_directory_path() / "hootplay_config_test";
    fs::remove_all(root);
    fs::create_directories(root / "cfg");

    const fs::path path = root / "cfg" / "hootplay.ini";
    {
        std::ofstream out(path);
        out << "[player]\n"
               "catalog = data/hoot.sqlite.zst\n"
               "packs = ../packs\n"
               "entry = asuka68snd-generic-2\n"
               "sample_rate = 48000\n"
               "track = 3\n"
               "list = no\n"
               "mute_percussion = false\n"
               "channels = 2-5\n"
               "wav = captures/test.wav\n"
               "seconds = 2\n"
               "\n"
               "[gui]\n"
               "font = fonts/NotoSansCJK-Regular.ttc\n"
               "\n"
               "[midi]\n"
               "backend = nuked-sc55\n"
               "soundfont = sounds/GeneralUser.sf2\n"
               "m1_soundfont = sounds/M1.sf2\n"
               "nuked_sc55_clap = plugins/Nuked-SC55.clap\n"
               "soundcanvas_rom_path = roms/sc55\n"
               "mt32emu_library = plugins/libmt32emu.so\n"
               "vermouth_library = plugins/vermouth.dll\n"
               "vermouth_abi = legacy\n"
               "vermouth_soundfont = sounds/Vermouth.sf2\n"
               "munt_rom_path = roms/munt\n"
               "mt32_rom_path = roms/mt32\n"
               "cm32l_rom_path = roms/cm32l\n"
               "cm32p_rom_path = roms/cm32p\n"
               "cm32p_card_rom = roms/cm32p/card.bin\n"
               "cm32p_card_rom_07 = roms/cm32p/card07.bin\n"
               "cm32p_card_rom_10 = roms/cm32p/card10.bin\n"
               "mt32_machine = mt32_1_07\n"
               "cm32l_machine = cm32l_1_02\n"
               "sc55_model = mk2\n"
               "gain = 0.65\n"
               "enabled = true\n"
               "clap_path = /opt/clap:/usr/local/lib/clap\n"
               "legacy_soundfont = sounds/legacy.sf2\n"
               "\n"
               "[general]\n"
               "override_xml = overrides/hoot.xml\n"
               "\n"
               "[x68k]\n"
               "mdxmini_library = plugins/libmdxmini.so\n"
               "startup = auto\n"
               "pcm8 = off\n"
               "mfp_core = hoot\n"
               "mfp_bootstrap = true\n"
               "mfp_ignore_overrides = false\n"
               "trace = traces/x68k.log\n"
               "trace_limit = 1000\n"
               "\n"
               "[psg]\n"
               "disabled = false\n"
               "solo = true\n"
               "channels = AC\n"
               "only = AB\n"
               "gain = 0.8\n"
               "invert = true\n"
               "tone = false\n"
               "noise = true\n"
               "raw = true\n"
               "\n"
               "[pc98]\n"
               "trace = traces/pc98.log\n"
               "trace_limit = 20000\n"
               "trace_opn = true\n"
               "trace_opn_limit = 512\n"
               "trace_dos = false\n"
               "disable_opn_tl_compat = true\n"
               "mmd_timer_hz = 120.0\n"
               "beep_gain = 0.75\n"
               "trace_x86_unsupported = false\n"
               "\n"
               "[pc88]\n"
               "irq_bus = 0x08\n"
               "trace = traces/xak2.log\n"
               "trace_limit = 5000\n"
               "\n"
               "[environment]\n"
               "CUSTOM_TEST_SETTING = hello\n";
    }

    hoot::HootplayFileConfig config;
    std::string error;
    assert(hoot::load_hootplay_config(path.string(), config, error));
    assert(error.empty());

    const fs::path cfg_dir = fs::absolute(root / "cfg").lexically_normal();
    assert(config.has_catalog);
    assert(config.catalog == (cfg_dir / "data/hoot.sqlite.zst").lexically_normal().string());
    assert(config.has_packs);
    assert(config.packs == (cfg_dir / "../packs").lexically_normal().string());
    assert(config.entry == "asuka68snd-generic-2");
    assert(config.rate == 48000);
    assert(config.track == 3);
    assert(config.has_list && !config.list);
    assert(config.has_mute_percussion && !config.mute_percussion);
    assert(config.channels == "2-5");
    assert(config.wav_path == (cfg_dir / "captures/test.wav").lexically_normal().string());
    assert(config.wav_seconds == 2);
    assert(config.has_ui_font);
    assert(config.ui_font == (cfg_dir / "fonts/NotoSansCJK-Regular.ttc").lexically_normal().string());

    assert(config.environment.at("HOOT_X68K_MIDI_BACKEND") == "nuked-sc55");
    assert(config.environment.at("HOOT_X68K_SOUNDFONT") ==
           (cfg_dir / "sounds/GeneralUser.sf2").lexically_normal().string());
    assert(config.environment.at("HOOT_X68K_M1_SOUNDFONT") ==
           (cfg_dir / "sounds/M1.sf2").lexically_normal().string());
    assert(config.environment.at("HOOT_X68K_NUKED_SC55_CLAP") ==
           (cfg_dir / "plugins/Nuked-SC55.clap").lexically_normal().string());
    assert(config.environment.at("SOUNDCANVAS_ROM_PATH") ==
           (cfg_dir / "roms/sc55").lexically_normal().string());
    assert(config.environment.at("HOOT_MT32EMU_LIBRARY") ==
           (cfg_dir / "plugins/libmt32emu.so").lexically_normal().string());
    assert(config.environment.at("HOOT_VERMOUTH_LIBRARY") ==
           (cfg_dir / "plugins/vermouth.dll").lexically_normal().string());
    assert(config.environment.at("HOOT_VERMOUTH_ABI") == "legacy");
    assert(config.environment.at("HOOT_VERMOUTH_SOUNDFONT") ==
           (cfg_dir / "sounds/Vermouth.sf2").lexically_normal().string());
    assert(config.environment.at("HOOT_MUNT_ROM_PATH") ==
           (cfg_dir / "roms/munt").lexically_normal().string());
    assert(config.environment.at("HOOT_MT32_ROM_PATH") ==
           (cfg_dir / "roms/mt32").lexically_normal().string());
    assert(config.environment.at("HOOT_CM32L_ROM_PATH") ==
           (cfg_dir / "roms/cm32l").lexically_normal().string());
    assert(config.environment.at("HOOT_CM32P_ROM_PATH") ==
           (cfg_dir / "roms/cm32p").lexically_normal().string());
    assert(config.environment.at("HOOT_CM32P_CARD_ROM") ==
           (cfg_dir / "roms/cm32p/card.bin").lexically_normal().string());
    assert(config.environment.at("HOOT_CM32P_CARD_ROM_07") ==
           (cfg_dir / "roms/cm32p/card07.bin").lexically_normal().string());
    assert(config.environment.at("HOOT_CM32P_CARD_ROM_10") ==
           (cfg_dir / "roms/cm32p/card10.bin").lexically_normal().string());
    assert(config.environment.at("HOOT_MT32_MACHINE") == "mt32_1_07");
    assert(config.environment.at("HOOT_CM32L_MACHINE") == "cm32l_1_02");
    assert(config.environment.at("HOOT_X68K_SC55_MODEL") == "mk2");
    assert(config.environment.at("HOOT_X68K_MIDI_GAIN") == "0.65");
    assert(config.environment.at("HOOT_X68K_MIDI") == "1");
    assert(config.environment.at("CLAP_PATH") == "/opt/clap:/usr/local/lib/clap");
    assert(config.environment.at("HOOT_MDXMINI_LIBRARY") ==
           (cfg_dir / "plugins/libmdxmini.so").lexically_normal().string());
    assert(config.environment.at("HOOT_X68K_STARTUP") == "auto");
    assert(config.environment.at("HOOT_X68K_PCM8") == "0");
    assert(config.environment.at("HOOT_X68K_MFP_BOOTSTRAP") == "1");
    assert(config.environment.at("HOOT_X68K_MFP_IGNORE_OVERRIDES") == "0");
    assert(config.environment.at("HOOT_X68K_TRACE") ==
           (cfg_dir / "traces/x68k.log").lexically_normal().string());
    assert(config.environment.at("HOOT_MIDI_SOUNDFONT") ==
           (cfg_dir / "sounds/legacy.sf2").lexically_normal().string());
    assert(config.environment.at("HOOT_OVERRIDE_XML") ==
           (cfg_dir / "overrides/hoot.xml").lexically_normal().string());
    assert(config.unset_environment.count("HOOT_DISABLE_PSG") == 1);
    assert(config.environment.at("HOOT_SOLO_PSG") == "1");
    assert(config.environment.at("HOOT_PSG_CHANNELS") == "AC");
    assert(config.environment.at("HOOT_PSG_ONLY") == "AB");
    assert(config.environment.at("HOOT_PSG_GAIN") == "0.8");
    assert(config.environment.at("HOOT_PSG_INVERT") == "1");
    assert(config.environment.at("HOOT_PSG_TONE") == "0");
    assert(config.environment.at("HOOT_PSG_NOISE") == "1");
    assert(config.environment.at("HOOT_PSG_RAW") == "1");
    assert(config.environment.at("HOOT_PC98_TRACE") ==
           (cfg_dir / "traces/pc98.log").lexically_normal().string());
    assert(config.environment.at("HOOT_PC98_TRACE_LIMIT") == "20000");
    assert(config.environment.at("HOOT_TRACE_PC98_OPN") == "1");
    assert(config.environment.at("HOOT_TRACE_PC98_OPN_LIMIT") == "512");
    assert(config.unset_environment.count("HOOT_TRACE_PC98_DOS") == 1);
    assert(config.environment.at("HOOT_DISABLE_OPN_TL_COMPAT") == "1");
    assert(config.environment.at("HOOT_MMD_TIMER_HZ") == "120.0");
    assert(config.environment.at("HOOT_PC98_BEEP_GAIN") == "0.75");
    assert(config.unset_environment.count("HOOT_TRACE_X86_UNSUPPORTED") == 1);
    assert(config.environment.at("HOOT_PC88_IRQ_BUS") == "0x08");
    assert(config.environment.at("HOOT_PC88_TRACE") ==
           (cfg_dir / "traces/xak2.log").lexically_normal().string());
    assert(config.environment.at("HOOT_PC88_TRACE_LIMIT") == "5000");
    assert(config.environment.at("CUSTOM_TEST_SETTING") == "hello");

    hoot::apply_hootplay_environment(config);
    assert(std::string(std::getenv("CUSTOM_TEST_SETTING")) == "hello");
    assert(std::string(std::getenv("HOOT_X68K_CHANNELS")) == "2-5");
    assert(std::getenv("HOOT_X68K_MUTE_PERCUSSION") == nullptr);
    assert(std::getenv("HOOT_DISABLE_PSG") == nullptr);
    assert(std::string(std::getenv("HOOT_MT32_ROM_PATH")) ==
           (cfg_dir / "roms/mt32").lexically_normal().string());

    // Unknown names are rejected so misspelled runtime settings cannot silently
    // produce a different playback configuration.
    const fs::path bad = root / "cfg" / "bad.ini";
    {
        std::ofstream out(bad);
        out << "[midi]\nbackned = auto\n";
    }
    hoot::HootplayFileConfig bad_config;
    assert(!hoot::load_hootplay_config(bad.string(), bad_config, error));
    assert(error.find("unknown midi setting") != std::string::npos);

    fs::remove_all(root);
    return 0;
}
