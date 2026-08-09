#include "config/hoot_settings.h"

#include "config/hootplay_config.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace hoot {
namespace {

std::string trim(std::string value)
{
    const auto nonspace = [](unsigned char ch) { return !std::isspace(ch); };
    value.erase(value.begin(), std::find_if(value.begin(), value.end(), nonspace));
    value.erase(std::find_if(value.rbegin(), value.rend(), nonspace).base(), value.end());
    return value;
}

std::string lower(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

std::string unquote(std::string value)
{
    value = trim(std::move(value));
    if (value.size() >= 2 && ((value.front() == '"' && value.back() == '"') ||
                              (value.front() == '\'' && value.back() == '\'')))
        value = value.substr(1, value.size() - 2);
    return value;
}

const HootSettingSpec* find_spec(const std::string& section, const std::string& key)
{
    const auto& specs = hoot_setting_specs();
    const auto it = std::find_if(specs.begin(), specs.end(), [&](const auto& spec) {
        return section == spec.section && key == spec.key;
    });
    return it == specs.end() ? nullptr : &*it;
}

const char* kind_name(HootSettingKind kind)
{
    switch (kind) {
    case HootSettingKind::Boolean: return "boolean";
    case HootSettingKind::Integer: return "integer";
    case HootSettingKind::Number: return "number";
    case HootSettingKind::Path: return "file";
    case HootSettingKind::Directory: return "directory";
    case HootSettingKind::Choice: return "choice";
    default: return "text";
    }
}

} // namespace

const std::vector<HootSettingSpec>& hoot_setting_specs()
{
    static const std::vector<HootSettingSpec> specs = {
        {"general","override_xml","Override XML",HootSettingKind::Path,"hoot-overrides.xml","","Optional XML overrides layered over the catalogue."},

        {"player","catalog","Catalogue",HootSettingKind::Path,"catalog/hoot.sqlite.zst","","Hoot SQLite, JSON or XML catalogue."},
        {"player","packs","Pack directory",HootSettingKind::Directory,"packs","","Directory containing Hoot pack ZIP files."},
        {"player","entry","Startup entry",HootSettingKind::Text,"","","Catalogue entry/archive loaded at startup."},
        {"player","sample_rate","Sample rate",HootSettingKind::Integer,"44100","","Output sample rate in Hz."},
        {"player","track","Startup track",HootSettingKind::Integer,"1","","One-based track selected at startup."},
        {"player","list","List only",HootSettingKind::Boolean,"false","","CLI lists supported entries instead of playing."},
        {"player","mute_percussion","Mute percussion",HootSettingKind::Boolean,"false","","Mute percussion where the driver supports it."},
        {"player","channels","Channels",HootSettingKind::Text,"2-5","","Driver-specific channel selection/mute expression."},
        {"player","wav","WAV output",HootSettingKind::Path,"captures/track.wav","","CLI renders to this WAV instead of live output."},
        {"player","seconds","WAV seconds",HootSettingKind::Integer,"30","","Duration used for WAV rendering."},

        {"gui","font","UI font",HootSettingKind::Path,"fonts/NotoSansCJK-Regular.ttc","","Japanese-capable TTF/OTF/TTC font; automatic lookup when disabled."},

        {"midi","enabled","MIDI enabled",HootSettingKind::Boolean,"true","","Force MIDI transport on/off instead of catalogue metadata."},
        {"midi","backend","MIDI backend",HootSettingKind::Choice,"auto","auto|cm64|cm32p|munt|vermouth|nuked-sc55|fluidsynth|none","Synth backend selection."},
        {"midi","mt32emu_library","mt32emu library",HootSettingKind::Path,"lib/libmt32emu.so","","Optional explicit Munt/mt32emu shared library."},
        {"midi","vermouth_library","Vermouth library",HootSettingKind::Path,"lib/vermouth.so","","Native Vermouth compatible shared library."},
        {"midi","vermouth_abi","Vermouth ABI",HootSettingKind::Choice,"legacy","legacy|fluidsynth","Vermouth module-creation ABI."},
        {"midi","vermouth_soundfont","Vermouth SoundFont",HootSettingKind::Path,"soundfonts/GeneralUser_GS.sf2","","SF2 used by FluidSynth-based Vermouth."},
        {"midi","munt_rom_path","Munt ROM directory",HootSettingKind::Directory,"roms/munt","","Fallback directory for Munt ROM images."},
        {"midi","mt32_rom_path","MT-32 ROM directory",HootSettingKind::Directory,"roms/mt32","","Preferred MT-32 control/PCM ROM directory."},
        {"midi","cm32l_rom_path","CM-32L ROM directory",HootSettingKind::Directory,"roms/cm32l","","CM-32L ROM directory for CM-64 playback."},
        {"midi","cm32p_rom_path","CM-32P ROM directory",HootSettingKind::Directory,"roms/cm32p","","CM-32P PCM ROM directory."},
        {"midi","cm32p_card_rom","CM-32P card ROM",HootSettingKind::Path,"roms/cm32p/SN-U110.bin","","Generic SN-U110 expansion card ROM."},
        {"midi","cm32p_card_rom_07","SN-U110-07 ROM",HootSettingKind::Path,"roms/cm32p/SN-U110-07.bin","","Card-specific SN-U110-07 ROM."},
        {"midi","cm32p_card_rom_10","SN-U110-10 ROM",HootSettingKind::Path,"roms/cm32p/SN-U110-10.bin","","Card-specific SN-U110-10 ROM."},
        {"midi","mt32_machine","MT-32 machine",HootSettingKind::Text,"mt32_1_07","","Optional exact Munt MT-32 machine id."},
        {"midi","cm32l_machine","CM-32L machine",HootSettingKind::Text,"cm32l_1_02","","Optional exact Munt CM-32L machine id."},
        {"midi","soundfont","GM/GS SoundFont",HootSettingKind::Path,"soundfonts/GeneralUser_GS.sf2","","Primary FluidSynth GM/GS SoundFont."},
        {"midi","m1_soundfont","M1 SoundFont",HootSettingKind::Path,"soundfonts/Korg_M1_compatible.sf2","","X68000 Korg M1 compatibility SoundFont."},
        {"midi","legacy_soundfont","Legacy SoundFont",HootSettingKind::Path,"soundfonts/GeneralUser_GS.sf2","","Generic legacy MIDI SoundFont fallback."},
        {"midi","nuked_sc55_clap","Nuked-SC55 CLAP",HootSettingKind::Path,"plugins/Nuked-SC55.clap","","Explicit Nuked-SC55 CLAP plugin path."},
        {"midi","soundcanvas_rom_path","Sound Canvas ROM directory",HootSettingKind::Directory,"roms/sc55","","Directory containing Sound Canvas ROM files."},
        {"midi","sc55_model","SC-55 model",HootSettingKind::Choice,"v1.21","v1.00|v1.10|v1.20|v1.21|v2.00|mk2","Nuked-SC55 hardware revision."},
        {"midi","gain","MIDI gain",HootSettingKind::Number,"0.70","","MIDI contribution to the final mix."},
        {"midi","clap_path","CLAP search path",HootSettingKind::Directory,"/usr/local/lib/clap","","Additional CLAP plugin search path."},

        {"x68k","mdxmini_library","mdxmini library",HootSettingKind::Path,"lib/libmdxmini.so","","Optional mdxmini shared library."},
        {"x68k","x_load_mode","X loader mode",HootSettingKind::Choice,"auto","auto|raw-file|raw-body","Human68k .X loader mode."},
        {"x68k","cpu_clock","68000 clock",HootSettingKind::Integer,"10000000","","Emulated 68000 clock in Hz."},
        {"x68k","ym2151_clock","YM2151 clock",HootSettingKind::Integer,"4000000","","YM2151 clock in Hz."},
        {"x68k","ym2151_core","YM2151 core",HootSettingKind::Choice,"nuked","nuked|mame","YM2151 emulation core."},
        {"x68k","pcm8","PCM8",HootSettingKind::Boolean,"true","","Force PCM8 support on/off."},
        {"x68k","mfp_core","MFP core",HootSettingKind::Choice,"hoot","hoot|legacy|mame|mc68901","MFP implementation override."},
        {"x68k","mfp_bootstrap","MFP bootstrap",HootSettingKind::Boolean,"true","","Use Hoot-compatible MFP bootstrap."},
        {"x68k","mfp_ignore_overrides","Ignore MFP overrides",HootSettingKind::Boolean,"false","","Ignore catalogue MFP overrides."},
        {"x68k","startup","Startup policy",HootSettingKind::Choice,"auto","auto|native|human68k|hoot|legacy|direct","X68000 startup policy."},
        {"x68k","opm_gain","OPM gain",HootSettingKind::Number,"0.75","","YM2151 contribution to the mix."},
        {"x68k","adpcm_gain","ADPCM gain",HootSettingKind::Number,"0.40","","Standard ADPCM contribution."},
        {"x68k","pcm8_gain","PCM8 gain",HootSettingKind::Number,"0.40","","PCM8 contribution."},
        {"x68k","total_gain","Total gain",HootSettingKind::Number,"1.0","","Final X68000 output gain."},
        {"x68k","trace","Trace file",HootSettingKind::Path,"logs/x68k.trace","","Detailed X68000 trace output."},
        {"x68k","trace_limit","Trace limit",HootSettingKind::Integer,"10000","","Maximum X68000 trace events."},

        {"psg","disabled","Disable PSG",HootSettingKind::Boolean,"false","","Disable PSG/SSG output completely."},
        {"psg","solo","Solo PSG",HootSettingKind::Boolean,"false","","Suppress FM and solo PSG/SSG where supported."},
        {"psg","channels","PSG channels",HootSettingKind::Text,"ABC","","PSG channels to keep enabled."},
        {"psg","only","PSG channels (legacy)",HootSettingKind::Text,"ABC","","Legacy alias for PSG channel selection."},
        {"psg","gain","PSG gain",HootSettingKind::Number,"0.90","","PSG/SSG gain."},
        {"psg","invert","Invert PSG",HootSettingKind::Boolean,"false","","Invert PSG/SSG polarity."},
        {"psg","tone","PSG tone",HootSettingKind::Boolean,"true","","Enable tone generation."},
        {"psg","noise","PSG noise",HootSettingKind::Boolean,"true","","Enable noise generation."},
        {"psg","raw","Raw PSG",HootSettingKind::Boolean,"false","","Disable PSG DC-removal for diagnostics."},

        {"pc98","trace","Trace file",HootSettingKind::Path,"logs/pc98.trace","","Full PC-98 driver trace output."},
        {"pc98","trace_limit","Trace limit",HootSettingKind::Integer,"20000","","Maximum PC-98 trace events."},
        {"pc98","trace_opn","Trace OPNA",HootSettingKind::Boolean,"false","","Enable OPNA register tracing."},
        {"pc98","trace_opn_limit","OPNA trace limit",HootSettingKind::Integer,"512","","Maximum OPNA trace events."},
        {"pc98","trace_dos","Trace DOS",HootSettingKind::Boolean,"false","","Enable DOS-call tracing."},
        {"pc98","disable_opn_tl_compat","Disable OPN TL compat",HootSettingKind::Boolean,"false","","Disable OPN total-level compatibility adjustment."},
        {"pc98","mmd_timer_hz","MMD timer",HootSettingKind::Number,"120.0","","Micro Cabin MMD timer rate in Hz."},
        {"pc98","beep_gain","Beep gain",HootSettingKind::Number,"1.0","","PC-98 PIT speaker/buzzer gain."},
        {"pc98","trace_x86_unsupported","Trace unsupported x86",HootSettingKind::Boolean,"false","","Print unsupported x86 opcodes."},

        {"pc88","irq_bus","IRQ bus value",HootSettingKind::Text,"0x08","","Generic PC-88 FM-timer interrupt bus value."},
        {"pc88","trace","Trace file",HootSettingKind::Path,"logs/pc88.trace","","Generic PC-88 trace output."},
        {"pc88","trace_limit","Trace limit",HootSettingKind::Integer,"10000","","Maximum PC-88 trace events."},
    };
    return specs;
}

void reset_hoot_settings(HootSettingsDocument& document)
{
    document.values.clear();
    document.environment.clear();
    document.values.reserve(hoot_setting_specs().size());
    for (const auto& spec : hoot_setting_specs())
        document.values.push_back(HootSettingValue{&spec, false, spec.default_value});
}

bool load_hoot_settings_document(const std::string& path,
                                 HootSettingsDocument& document,
                                 std::string& error)
{
    reset_hoot_settings(document);
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        error = "unable to open settings: " + path;
        return false;
    }

    std::string section = "player";
    std::string line;
    size_t line_number = 0;
    while (std::getline(file, line)) {
        ++line_number;
        if (line_number == 1 && line.size() >= 3 &&
            static_cast<unsigned char>(line[0]) == 0xef &&
            static_cast<unsigned char>(line[1]) == 0xbb &&
            static_cast<unsigned char>(line[2]) == 0xbf) line.erase(0, 3);
        line = trim(std::move(line));
        if (line.empty() || line[0] == '#' || line[0] == ';') continue;
        if (line.front() == '[' && line.back() == ']') {
            section = lower(trim(line.substr(1, line.size() - 2)));
            continue;
        }
        const auto equals = line.find('=');
        if (equals == std::string::npos) continue;
        const auto key = lower(trim(line.substr(0, equals)));
        const auto value = unquote(line.substr(equals + 1));
        if (section == "environment") {
            document.environment[trim(line.substr(0, equals))] = value;
            continue;
        }
        const HootSettingSpec* spec = find_spec(section, key == "archive" ? "entry" : key);
        if (!spec) continue;
        for (auto& item : document.values) {
            if (item.spec == spec) {
                item.enabled = true;
                item.value = value;
                break;
            }
        }
    }
    return true;
}

bool save_hoot_settings_document(const std::string& path,
                                 const HootSettingsDocument& document,
                                 std::string& error)
{
    const auto target = std::filesystem::absolute(path).lexically_normal();
    std::error_code ec;
    std::filesystem::create_directories(target.parent_path(), ec);
    if (ec) {
        error = "unable to create settings directory: " + ec.message();
        return false;
    }

    std::ostringstream out;
    out << "# Hoot configuration. Managed by hootui Settings; manual edits are supported.\n"
           "# Commented settings are reference values only: they do not override catalogue, autodetection, or built-in behavior.\n"
           "# Relative paths use this directory.\n\n";
    const std::vector<std::string> sections = {"general","player","gui","midi","x68k","psg","pc98","pc88"};
    for (const auto& section : sections) {
        out << '[' << section << "]\n";
        for (const auto& item : document.values) {
            if (!item.spec || section != item.spec->section) continue;
            out << "# " << item.spec->description << " (" << kind_name(item.spec->kind) << ")\n";
            if (!item.enabled) out << "# ";
            out << item.spec->key << " = " << (item.value.empty() ? item.spec->default_value : item.value) << "\n";
        }
        out << '\n';
    }
    out << "[environment]\n"
           "# Advanced environment pass-through.\n";
    for (const auto& pair : document.environment) out << pair.first << " = " << pair.second << '\n';

    const auto tmp = target.string() + ".tmp";
    {
        std::ofstream file(tmp, std::ios::binary | std::ios::trunc);
        if (!file) {
            error = "unable to write temporary settings: " + tmp;
            return false;
        }
        const auto text = out.str();
        file.write(text.data(), static_cast<std::streamsize>(text.size()));
        if (!file) {
            error = "unable to write settings: " + tmp;
            return false;
        }
    }

    HootplayFileConfig validation;
    std::string validation_error;
    if (!load_hootplay_config(tmp, validation, validation_error)) {
        std::filesystem::remove(tmp, ec);
        error = validation_error;
        return false;
    }

    std::filesystem::rename(tmp, target, ec);
    if (ec) {
        std::filesystem::remove(target, ec);
        ec.clear();
        std::filesystem::rename(tmp, target, ec);
    }
    if (ec) {
        std::filesystem::remove(tmp, ec);
        error = "unable to replace settings file: " + ec.message();
        return false;
    }
    return true;
}

} // namespace hoot
