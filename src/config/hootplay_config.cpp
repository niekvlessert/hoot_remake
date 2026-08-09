#include "config/hootplay_config.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <unordered_map>

namespace hoot {
namespace {

std::string trim(std::string value)
{
    const auto not_space = [](unsigned char ch) { return !std::isspace(ch); };
    value.erase(value.begin(), std::find_if(value.begin(), value.end(), not_space));
    value.erase(std::find_if(value.rbegin(), value.rend(), not_space).base(), value.end());
    return value;
}

std::string lower(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

bool parse_bool(const std::string& raw, bool& value)
{
    const auto text = lower(trim(raw));
    if (text == "1" || text == "true" || text == "yes" || text == "on") {
        value = true;
        return true;
    }
    if (text == "0" || text == "false" || text == "no" || text == "off") {
        value = false;
        return true;
    }
    return false;
}

bool parse_int(const std::string& raw, int& value)
{
    const auto text = trim(raw);
    if (text.empty()) return false;
    try {
        size_t used = 0;
        const long parsed = std::stol(text, &used, 0);
        if (used != text.size()) return false;
        value = static_cast<int>(parsed);
        return true;
    } catch (...) {
        return false;
    }
}

std::string unquote(std::string value)
{
    value = trim(std::move(value));
    if (value.size() >= 2 && ((value.front() == '"' && value.back() == '"') ||
                              (value.front() == '\'' && value.back() == '\''))) {
        value = value.substr(1, value.size() - 2);
    }
    return value;
}

std::string resolve_path(const std::filesystem::path& config_dir, const std::string& raw)
{
    const auto value = unquote(raw);
    if (value.empty()) return value;
    std::filesystem::path path(value);
    if (path.is_absolute()) return path.lexically_normal().string();
    return (config_dir / path).lexically_normal().string();
}

bool set_environment_alias(HootplayFileConfig& config,
                           const std::string& section,
                           const std::string& key,
                           const std::string& value)
{
    static const std::unordered_map<std::string, std::string> aliases = {
        {"general.override_xml", "HOOT_OVERRIDE_XML"},

        {"midi.backend", "HOOT_X68K_MIDI_BACKEND"},
        {"midi.soundfont", "HOOT_X68K_SOUNDFONT"},
        {"midi.m1_soundfont", "HOOT_X68K_M1_SOUNDFONT"},
        {"midi.legacy_soundfont", "HOOT_MIDI_SOUNDFONT"},
        {"midi.nuked_sc55_clap", "HOOT_X68K_NUKED_SC55_CLAP"},
        {"midi.sc55_model", "HOOT_X68K_SC55_MODEL"},
        {"midi.gain", "HOOT_X68K_MIDI_GAIN"},
        {"midi.enabled", "HOOT_X68K_MIDI"},
        {"midi.clap_path", "CLAP_PATH"},
        {"midi.soundcanvas_rom_path", "SOUNDCANVAS_ROM_PATH"},
        {"midi.mt32emu_library", "HOOT_MT32EMU_LIBRARY"},
        {"midi.vermouth_library", "HOOT_VERMOUTH_LIBRARY"},
        {"midi.vermouth_abi", "HOOT_VERMOUTH_ABI"},
        {"midi.vermouth_soundfont", "HOOT_VERMOUTH_SOUNDFONT"},
        {"midi.munt_rom_path", "HOOT_MUNT_ROM_PATH"},
        {"midi.mt32_rom_path", "HOOT_MT32_ROM_PATH"},
        {"midi.cm32l_rom_path", "HOOT_CM32L_ROM_PATH"},
        {"midi.cm32p_rom_path", "HOOT_CM32P_ROM_PATH"},
        {"midi.cm32p_card_rom", "HOOT_CM32P_CARD_ROM"},
        {"midi.cm32p_card_rom_07", "HOOT_CM32P_CARD_ROM_07"},
        {"midi.cm32p_card_rom_10", "HOOT_CM32P_CARD_ROM_10"},
        {"midi.mt32_machine", "HOOT_MT32_MACHINE"},
        {"midi.cm32l_machine", "HOOT_CM32L_MACHINE"},

        {"x68k.x_load_mode", "HOOT_X68K_X_LOAD_MODE"},
        {"x68k.mdxmini_library", "HOOT_MDXMINI_LIBRARY"},
        {"x68k.cpu_clock", "HOOT_X68K_CPU_CLOCK"},
        {"x68k.ym2151_clock", "HOOT_X68K_YM2151_CLOCK"},
        {"x68k.ym2151_core", "HOOT_X68K_YM2151_CORE"},
        {"x68k.pcm8", "HOOT_X68K_PCM8"},
        {"x68k.mfp_core", "HOOT_X68K_MFP_CORE"},
        {"x68k.mfp_bootstrap", "HOOT_X68K_MFP_BOOTSTRAP"},
        {"x68k.mfp_ignore_overrides", "HOOT_X68K_MFP_IGNORE_OVERRIDES"},
        {"x68k.startup", "HOOT_X68K_STARTUP"},
        {"x68k.opm_gain", "HOOT_X68K_OPM_GAIN"},
        {"x68k.adpcm_gain", "HOOT_X68K_ADPCM_GAIN"},
        {"x68k.pcm8_gain", "HOOT_X68K_PCM8_GAIN"},
        {"x68k.total_gain", "HOOT_X68K_TOTAL_GAIN"},
        {"x68k.trace", "HOOT_X68K_TRACE"},
        {"x68k.trace_limit", "HOOT_X68K_TRACE_LIMIT"},

        {"psg.channels", "HOOT_PSG_CHANNELS"},
        {"psg.only", "HOOT_PSG_ONLY"},
        {"psg.gain", "HOOT_PSG_GAIN"},
        {"psg.invert", "HOOT_PSG_INVERT"},
        {"psg.tone", "HOOT_PSG_TONE"},
        {"psg.noise", "HOOT_PSG_NOISE"},

        {"pc98.trace", "HOOT_PC98_TRACE"},
        {"pc98.trace_limit", "HOOT_PC98_TRACE_LIMIT"},
        {"pc98.trace_opn_limit", "HOOT_TRACE_PC98_OPN_LIMIT"},
        {"pc98.mmd_timer_hz", "HOOT_MMD_TIMER_HZ"},
        {"pc98.beep_gain", "HOOT_PC98_BEEP_GAIN"},

        {"pc88.irq_bus", "HOOT_PC88_IRQ_BUS"},
        {"pc88.trace", "HOOT_PC88_TRACE"},
        {"pc88.trace_limit", "HOOT_PC88_TRACE_LIMIT"},
    };
    const auto it = aliases.find(section + "." + key);
    if (it == aliases.end()) return false;
    const auto clean = unquote(value);
    if (!clean.empty()) config.environment[it->second] = clean;
    return true;
}

bool set_presence_boolean(HootplayFileConfig& config,
                          const std::string& section,
                          const std::string& key,
                          const std::string& value,
                          bool& recognized)
{
    static const std::unordered_map<std::string, std::string> aliases = {
        {"psg.disabled", "HOOT_DISABLE_PSG"},
        {"psg.raw", "HOOT_PSG_RAW"},
        {"psg.solo", "HOOT_SOLO_PSG"},
        {"pc98.trace_opn", "HOOT_TRACE_PC98_OPN"},
        {"pc98.trace_dos", "HOOT_TRACE_PC98_DOS"},
        {"pc98.disable_opn_tl_compat", "HOOT_DISABLE_OPN_TL_COMPAT"},
        {"pc98.trace_x86_unsupported", "HOOT_TRACE_X86_UNSUPPORTED"},
    };
    const auto it = aliases.find(section + "." + key);
    if (it == aliases.end()) {
        recognized = false;
        return true;
    }
    recognized = true;
    bool flag = false;
    if (!parse_bool(value, flag)) return false;
    if (flag) {
        config.environment[it->second] = "1";
        config.unset_environment.erase(it->second);
    } else {
        config.environment.erase(it->second);
        config.unset_environment.insert(it->second);
    }
    return true;
}

bool is_path_alias(const std::string& section, const std::string& key)
{
    return (section == "general" && key == "override_xml") ||
           (section == "midi" &&
            (key == "soundfont" || key == "m1_soundfont" || key == "legacy_soundfont" ||
             key == "nuked_sc55_clap" || key == "soundcanvas_rom_path" ||
             key == "mt32emu_library" || key == "vermouth_library" ||
             key == "vermouth_soundfont" || key == "munt_rom_path" ||
             key == "mt32_rom_path" || key == "cm32l_rom_path" ||
             key == "cm32p_rom_path" || key == "cm32p_card_rom" ||
             key == "cm32p_card_rom_07" || key == "cm32p_card_rom_10")) ||
           (section == "x68k" && (key == "trace" || key == "mdxmini_library")) ||
           (section == "pc98" && key == "trace") ||
           (section == "pc88" && key == "trace");
}

} // namespace

bool load_hootplay_config(const std::string& path,
                          HootplayFileConfig& config,
                          std::string& error)
{
    std::ifstream file(path);
    if (!file) {
        error = "unable to open hootplay config: " + path;
        return false;
    }

    const auto absolute_config = std::filesystem::absolute(path).lexically_normal();
    const auto config_dir = absolute_config.parent_path();
    std::string section = "player";
    std::string line;
    size_t line_number = 0;

    while (std::getline(file, line)) {
        ++line_number;
        if (line_number == 1 && line.size() >= 3 &&
            static_cast<unsigned char>(line[0]) == 0xef &&
            static_cast<unsigned char>(line[1]) == 0xbb &&
            static_cast<unsigned char>(line[2]) == 0xbf) {
            line.erase(0, 3);
        }
        line = trim(std::move(line));
        if (line.empty() || line[0] == '#' || line[0] == ';') continue;
        if (line.front() == '[' && line.back() == ']') {
            section = lower(trim(line.substr(1, line.size() - 2)));
            continue;
        }

        const auto equals = line.find('=');
        if (equals == std::string::npos) {
            error = path + ":" + std::to_string(line_number) + ": expected key = value";
            return false;
        }
        const auto key_original = trim(line.substr(0, equals));
        const auto key = lower(key_original);
        const auto raw_value = trim(line.substr(equals + 1));
        const auto value = unquote(raw_value);

        auto bad_value = [&](const char* expected) {
            error = path + ":" + std::to_string(line_number) + ": invalid " + expected +
                    " for " + section + "." + key_original + ": " + raw_value;
            return false;
        };

        if (section == "player") {
            if (key == "catalog") {
                config.has_catalog = true;
                config.catalog = resolve_path(config_dir, raw_value);
            } else if (key == "packs") {
                config.has_packs = true;
                config.packs = resolve_path(config_dir, raw_value);
            } else if (key == "entry" || key == "archive") {
                config.has_entry = true;
                config.entry = value;
            } else if (key == "sample_rate" || key == "rate") {
                if (!parse_int(value, config.rate)) return bad_value("integer");
                config.has_rate = true;
            } else if (key == "list") {
                if (!parse_bool(value, config.list)) return bad_value("boolean");
                config.has_list = true;
            } else if (key == "mute_percussion") {
                if (!parse_bool(value, config.mute_percussion)) return bad_value("boolean");
                config.has_mute_percussion = true;
            } else if (key == "channels") {
                config.has_channels = true;
                config.channels = value;
            } else if (key == "wav" || key == "wav_path") {
                config.has_wav_path = true;
                config.wav_path = resolve_path(config_dir, raw_value);
            } else if (key == "seconds" || key == "wav_seconds") {
                if (!parse_int(value, config.wav_seconds)) return bad_value("integer");
                config.has_wav_seconds = true;
            } else if (key == "track") {
                if (!parse_int(value, config.track)) return bad_value("integer");
                config.has_track = true;
            } else {
                error = path + ":" + std::to_string(line_number) + ": unknown player setting: " + key_original;
                return false;
            }
            continue;
        }

        if (section == "gui") {
            if (key == "font") {
                config.has_ui_font = true;
                config.ui_font = resolve_path(config_dir, raw_value);
            } else {
                error = path + ":" + std::to_string(line_number) + ": unknown gui setting: " + key_original;
                return false;
            }
            continue;
        }

        if (section == "environment") {
            if (key_original.empty()) return bad_value("environment variable name");
            if (!value.empty()) config.environment[key_original] = value;
            continue;
        }

        if (section == "general" || section == "midi" || section == "x68k" ||
            section == "psg" || section == "pc98" || section == "pc88") {
            bool presence_recognized = false;
            if (!set_presence_boolean(config, section, key, value, presence_recognized)) {
                return bad_value("boolean");
            }
            if (presence_recognized) continue;

            std::string alias_value = raw_value;
            if ((section == "midi" && key == "enabled") ||
                (section == "x68k" && (key == "pcm8" || key == "mfp_bootstrap" ||
                                         key == "mfp_ignore_overrides")) ||
                (section == "psg" && (key == "invert" || key == "tone" || key == "noise"))) {
                bool flag = false;
                if (!parse_bool(value, flag)) return bad_value("boolean");
                alias_value = flag ? "1" : "0";
            }
            if (is_path_alias(section, key) && !value.empty()) {
                alias_value = resolve_path(config_dir, raw_value);
            }
            if (!set_environment_alias(config, section, key, alias_value)) {
                error = path + ":" + std::to_string(line_number) + ": unknown " + section + " setting: " + key_original;
                return false;
            }
            continue;
        }

        error = path + ":" + std::to_string(line_number) + ": unknown section: [" + section + "]";
        return false;
    }
    return true;
}

void apply_hootplay_environment(const HootplayFileConfig& config)
{
    auto clear_value = [](const std::string& name) {
#if defined(_WIN32)
        _putenv_s(name.c_str(), "");
#else
        unsetenv(name.c_str());
#endif
    };
    auto set_value = [](const std::string& name, const std::string& value) {
#if defined(_WIN32)
        _putenv_s(name.c_str(), value.c_str());
#else
        setenv(name.c_str(), value.c_str(), 1);
#endif
    };

    for (const auto& name : config.unset_environment) clear_value(name);
    for (const auto& item : config.environment) set_value(item.first, item.second);

    // These two player-level options historically map to X68000 replay
    // environment knobs.  Apply them here as well so every frontend that
    // consumes hootplay.ini gets the same behavior as hootplay.
    if (config.has_mute_percussion) {
        if (config.mute_percussion) set_value("HOOT_X68K_MUTE_PERCUSSION", "1");
        else clear_value("HOOT_X68K_MUTE_PERCUSSION");
    }
    if (config.has_channels) {
        if (config.channels.empty()) clear_value("HOOT_X68K_CHANNELS");
        else set_value("HOOT_X68K_CHANNELS", config.channels);
    }
}

} // namespace hoot
