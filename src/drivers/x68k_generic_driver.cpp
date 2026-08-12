#include "drivers/x68k_generic_driver.h"
#include "core/utf8_util.h"
#include "core/visual_state_util.h"

#include <algorithm>
#include <cmath>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iomanip>
#include <sstream>
#include <string_view>
#include <vector>

#include "io/zip_archive.h"
#include "sound/fluidsynth_midi_synth.h"
#include "sound/vermouth_midi_synth.h"
#include "sound/cm32p_midi_synth.h"
#include "sound/cm64_midi_synth.h"
#include "sound/mt32emu_midi_synth.h"
#include "sound/nuked_sc55_clap_midi_synth.h"

extern "C" {
#include "m68k.h"
}

namespace hoot {
namespace {

std::string cm32p_card_hint(const std::string& title)
{
    const auto lower = [&]() {
        std::string value = title;
        std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
        return value;
    }();
    if (lower.find("sn-u110-07") != std::string::npos) return "07";
    if (lower.find("sn-u110-10") != std::string::npos) return "10";
    return {};
}

std::string cm32p_card_name(const std::string& hint)
{
    if (hint == "07") return "SN-U110-07";
    if (hint == "10") return "SN-U110-10";
    return hint.empty() ? std::string{} : ("SN-U110-" + hint);
}

std::string cm32p_missing_card_warning(const std::string& hint)
{
    const auto card = cm32p_card_name(hint);
    if (card.empty()) return {};
    return "This pack expects " + card + ". Put " + card +
        ".bin in ~/.hoot/roms/cm32p (the default) or set [midi] cm32p_card_rom_" + hint +
        "=roms/cm32p/" + card +
        ".bin in hootplay.ini. Playback continues without expansion-card sounds.";
}

std::string cm64_la_only_warning(const std::string& hint)
{
    const auto card = cm32p_card_name(hint);
    if (!card.empty()) {
        return "CM-64 + " + card + " needs IC18/IC19/IC20 and " + card +
            ".bin. Put them in ~/.hoot/roms/cm32p (the default); alternatively set [midi] cm32p_rom_path and "
            "cm32p_card_rom_" + hint + "=roms/cm32p/" + card +
            ".bin in hootplay.ini. LA-only fallback: PCM/card sounds missing.";
    }
    return "CM-64 needs CM-32P PCM ROMs IC18/IC19/IC20. Put the three 512 KiB dumps in "
        "~/.hoot/roms/cm32p (the default), or set [midi] cm32p_rom_path in hootplay.ini. "
        "LA-only fallback: CM-32P instruments missing; less authentic playback.";
}


uint16_t read_be16(const std::vector<uint8_t>& data, size_t offset)
{
    return static_cast<uint16_t>((data[offset] << 8) | data[offset + 1]);
}

uint32_t read_be32(const std::vector<uint8_t>& data, size_t offset)
{
    return (static_cast<uint32_t>(data[offset]) << 24)
        | (static_cast<uint32_t>(data[offset + 1]) << 16)
        | (static_cast<uint32_t>(data[offset + 2]) << 8)
        | static_cast<uint32_t>(data[offset + 3]);
}

template <size_t N>
uint32_t read_rom_be32(const std::array<uint8_t, N>& rom, size_t offset)
{
    return (static_cast<uint32_t>(rom[offset]) << 24)
        | (static_cast<uint32_t>(rom[offset + 1]) << 16)
        | (static_cast<uint32_t>(rom[offset + 2]) << 8)
        | static_cast<uint32_t>(rom[offset + 3]);
}

template <size_t N>
uint16_t read_rom_be16(const std::array<uint8_t, N>& rom, size_t offset)
{
    return static_cast<uint16_t>((rom[offset] << 8) | rom[offset + 1]);
}

template <size_t N>
void write_rom_be32(std::array<uint8_t, N>& rom, size_t offset, uint32_t value)
{
    rom[offset] = static_cast<uint8_t>(value >> 24);
    rom[offset + 1] = static_cast<uint8_t>(value >> 16);
    rom[offset + 2] = static_cast<uint8_t>(value >> 8);
    rom[offset + 3] = static_cast<uint8_t>(value);
}

template <size_t N>
void write_rom_be16(std::array<uint8_t, N>& rom, size_t offset, uint16_t value)
{
    rom[offset] = static_cast<uint8_t>(value >> 8);
    rom[offset + 1] = static_cast<uint8_t>(value);
}

std::string alternate_asset_path(const HootEntry& entry, const HootAssetRef& asset)
{
    (void)entry;
    (void)asset;
    return {};
}

template <size_t N>
bool load_archive_member_at(ZipArchive& archive,
                            std::array<uint8_t, N>& rom,
                            const std::string& path,
                            size_t offset,
                            std::string& error)
{
    auto data = archive.read(path, error);
    if (!error.empty()) {
        return false;
    }
    if (offset >= rom.size()) {
        error = "x68k code asset offset is outside ROM: " + path;
        return false;
    }
    const auto count = std::min<size_t>(data.size(), rom.size() - offset);
    std::copy_n(data.begin(), count, rom.begin() + static_cast<std::ptrdiff_t>(offset));
    return true;
}

template <size_t N>
bool load_human68k_x_at(ZipArchive& archive,
                        std::array<uint8_t, N>& rom,
                        const std::string& path,
                        size_t offset,
                        std::string& error)
{
    auto data = archive.read(path, error);
    if (!error.empty()) {
        return false;
    }
    if (data.size() < 0x40 || data[0] != 'H' || data[1] != 'U') {
        error = "unsupported Human68k .X executable: " + path;
        return false;
    }

    if (const char* mode = std::getenv("HOOT_X68K_X_LOAD_MODE")) {
        if (std::strcmp(mode, "raw-file") == 0 || std::strcmp(mode, "raw-body") == 0) {
            const size_t source_offset = std::strcmp(mode, "raw-body") == 0 ? 0x40 : 0;
            if (offset >= rom.size()) {
                error = "Human68k .X image is outside ROM: " + path;
                return false;
            }
            const auto count = std::min<size_t>(data.size() - source_offset, rom.size() - offset);
            std::copy_n(data.begin() + static_cast<std::ptrdiff_t>(source_offset),
                        count,
                        rom.begin() + static_cast<std::ptrdiff_t>(offset));
            return true;
        }
    }

    const uint32_t base_address = read_be32(data, 0x04);
    const size_t text_size = read_be32(data, 0x0c);
    const size_t data_size = read_be32(data, 0x10);
    const size_t bss_size = read_be32(data, 0x14);
    const size_t reloc_size = read_be32(data, 0x18);
    const size_t image_size = text_size + data_size + bss_size;
    const size_t file_image_size = text_size + data_size;
    const size_t reloc_offset = 0x40 + file_image_size;
    if (0x40 + file_image_size > data.size() || reloc_offset + reloc_size > data.size()) {
        error = "truncated Human68k .X executable: " + path;
        return false;
    }
    if (offset >= rom.size() || image_size > rom.size() - offset) {
        error = "Human68k .X image is outside ROM: " + path;
        return false;
    }

    std::copy_n(data.begin() + 0x40,
                file_image_size,
                rom.begin() + static_cast<std::ptrdiff_t>(offset));
    std::fill(rom.begin() + static_cast<std::ptrdiff_t>(offset + file_image_size),
              rom.begin() + static_cast<std::ptrdiff_t>(offset + image_size),
              uint8_t{0});

    size_t reloc_cursor = reloc_offset;
    size_t patch_offset = 0;
    const uint32_t relocation_delta = static_cast<uint32_t>(offset) - base_address;
    while (reloc_cursor < reloc_offset + reloc_size) {
        uint32_t delta = read_be16(data, reloc_cursor);
        reloc_cursor += 2;
        if (delta == 1) {
            if (reloc_cursor + 4 > reloc_offset + reloc_size) {
                error = "truncated Human68k .X relocation table: " + path;
                return false;
            }
            delta = read_be32(data, reloc_cursor);
            reloc_cursor += 4;
        }
        const bool word_relocation = (delta & 1) != 0;
        patch_offset += delta & ~uint32_t{1};
        const size_t patch_size = word_relocation ? 2 : 4;
        if (patch_offset + patch_size > image_size) {
            error = "Human68k .X relocation outside image: " + path;
            return false;
        }
        const size_t patch_address = offset + patch_offset;
        if (word_relocation) {
            write_rom_be16(rom, patch_address,
                           static_cast<uint16_t>(read_rom_be16(rom, patch_address) + relocation_delta));
        } else {
            write_rom_be32(rom, patch_address, read_rom_be32(rom, patch_address) + relocation_delta);
        }
    }

    return true;
}

template <size_t N>
bool load_archive_member_slice_at(ZipArchive& archive,
                                  std::array<uint8_t, N>& rom,
                                  const std::string& path,
                                  size_t source_offset,
                                  size_t offset,
                                  std::string& error)
{
    auto data = archive.read(path, error);
    if (!error.empty()) {
        return false;
    }
    if (source_offset > data.size()) {
        error = "x68k code asset source offset is outside member: " + path;
        return false;
    }
    if (offset >= rom.size()) {
        error = "x68k code asset offset is outside ROM: " + path;
        return false;
    }
    const auto count = std::min<size_t>(data.size() - source_offset, rom.size() - offset);
    std::copy_n(data.begin() + static_cast<std::ptrdiff_t>(source_offset),
                count,
                rom.begin() + static_cast<std::ptrdiff_t>(offset));
    return true;
}

template <size_t N>
void copy_c_string(char (&dest)[N], const std::string& source)
{
    hoot::utf8::copy_c_string(dest, source);
}

uint32_t parse_opm_mute_mask(const char* value)
{
    if (value == nullptr || value[0] == '\0') {
        return 0;
    }

    uint32_t solo_mask = 0;
    std::string spec(value);
    size_t cursor = 0;
    while (cursor < spec.size()) {
        const size_t comma = spec.find(',', cursor);
        const std::string token = spec.substr(cursor, comma == std::string::npos
                                                        ? std::string::npos
                                                        : comma - cursor);
        const size_t dash = token.find('-');
        const auto parse_number = [](const std::string& text) -> int {
            char* end = nullptr;
            const long number = std::strtol(text.c_str(), &end, 10);
            return end != text.c_str() && *end == '\0' ? static_cast<int>(number) : 0;
        };
        const int first = parse_number(token.substr(0, dash));
        const int last = dash == std::string::npos
            ? first
            : parse_number(token.substr(dash + 1));
        if (first < 1 || first > 8 || last < first || last > 8) {
            return 0;
        }
        for (int channel = first; channel <= last; ++channel) {
            solo_mask |= 1u << (channel - 1);
        }
        cursor = comma == std::string::npos ? spec.size() : comma + 1;
    }
    return (~solo_mask) & 0xffu;
}

std::vector<uint8_t> expand_opmdrv_compact_voice(const std::vector<uint8_t>& data)
{
    if (data.size() < 2 || data[0] != '(' || data[1] != 0xb6) {
        return data;
    }

    std::vector<uint8_t> expanded;
    expanded.reserve(data.size() * 2);
    const auto append_number = [&](uint8_t value) {
        const auto text = std::to_string(value);
        expanded.insert(expanded.end(), text.begin(), text.end());
    };

    for (size_t cursor = 0; cursor < data.size();) {
        const uint8_t token = data[cursor++];
        if (token == 0x1a) {
            expanded.push_back(token);
            break;
        }
        if ((token == 0xb6 || token == 0xc0) && cursor < data.size()) {
            if (token == 0xb6) {
                expanded.push_back('V');
            }
            append_number(data[cursor++]);
            if (cursor < data.size() && data[cursor] != ')') {
                expanded.push_back(',');
            }
            continue;
        }
        expanded.push_back(token);
    }
    expanded.push_back(0);
    return expanded;
}

} // namespace

} // namespace hoot

namespace hoot {

namespace {

constexpr size_t kVoiceBankOffset = 0x20000;
constexpr size_t kVoiceBankCapacity = 0x14000;

std::string track_filename(const HootEntry& entry, int track_index)
{
    if (track_index < 0 || static_cast<size_t>(track_index) >= entry.tracks.size()) {
        return {};
    }
    const auto& title = entry.tracks[track_index].title;
    const auto end = title.find_first_of(" :\t");
    return title.substr(0, end);
}

bool title_contains_ci(const std::string& title, const char* needle)
{
    std::string lower = title;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    std::string wanted = needle;
    std::transform(wanted.begin(), wanted.end(), wanted.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return lower.find(wanted) != std::string::npos;
}

} // namespace

HootResult X68kGenericDriver::load(const HootEntry& entry,
                                   const std::string& packs_path,
                                   int sample_rate,
                                   std::string& error)
{
    clear();
    sample_rate_ = sample_rate;
    cpu_clock_hz_ = 10000000.0;
    if (const char* value = std::getenv("HOOT_X68K_CPU_CLOCK")) {
        cpu_clock_hz_ = std::clamp(std::strtod(value, nullptr), 1000000.0, 50000000.0);
    }
    ym2151_clock_hz_ = 4000000;
    if (const char* value = std::getenv("HOOT_X68K_YM2151_CLOCK")) {
        ym2151_clock_hz_ = static_cast<uint32_t>(
            std::clamp(std::strtoul(value, nullptr, 0), 1000000ul, 8000000ul));
    }
    const auto midiout = entry.options.find("midiout");
    midi_enabled_ = midiout != entry.options.end() && midiout->second != 0;
    const auto midiout_type = entry.options.find("midiout_type");
    midiout_type_ = midiout_type != entry.options.end() ? midiout_type->second : -1;
    const auto pcm8 = entry.options.find("pcm8");
    pcm8_enabled_ = pcm8 != entry.options.end() && pcm8->second != 0;
    if (const char* value = std::getenv("HOOT_X68K_PCM8")) {
        if (std::strcmp(value, "1") == 0 || std::strcmp(value, "on") == 0) {
            pcm8_enabled_ = true;
        } else if (std::strcmp(value, "0") == 0 || std::strcmp(value, "off") == 0) {
            pcm8_enabled_ = false;
        }
    }
    pcm8_.reset();
    pcm8_.set_mute_mask(ui_pcm8_mute_mask_);
    const auto dmaint = entry.options.find("dmaint");
    dmaint_enabled_ = dmaint != entry.options.end() && dmaint->second != 0;
    const auto mfp = entry.options.find("mfp");
    mfp_enabled_ = mfp != entry.options.end() && mfp->second != 0;
    mame_mfp_ = false;
    if (const auto option = entry.options.find("mfp_core");
        option != entry.options.end()) {
        mame_mfp_ = option->second != 0;
    }
    if (const char* value = std::getenv("HOOT_X68K_MFP_CORE")) {
        if (std::strcmp(value, "mame") == 0 || std::strcmp(value, "mc68901") == 0) {
            mame_mfp_ = true;
        } else if (std::strcmp(value, "hoot") == 0 || std::strcmp(value, "legacy") == 0) {
            mame_mfp_ = false;
        }
    }
    mfp_bootstrap_ = true;
    if (const auto option = entry.options.find("mfp_bootstrap");
        option != entry.options.end()) {
        mfp_bootstrap_ = option->second != 0;
    }
    if (const char* value = std::getenv("HOOT_X68K_MFP_BOOTSTRAP")) {
        if (std::strcmp(value, "reset") == 0 || std::strcmp(value, "strict") == 0
            || std::strcmp(value, "0") == 0 || std::strcmp(value, "off") == 0) {
            mfp_bootstrap_ = false;
        } else if (std::strcmp(value, "hoot") == 0 || std::strcmp(value, "1") == 0
                   || std::strcmp(value, "on") == 0) {
            mfp_bootstrap_ = true;
        }
    }
    mfp_ignore_overrides_ = false;
    if (const auto option = entry.options.find("mfp_ignore_overrides");
        option != entry.options.end()) {
        mfp_ignore_overrides_ = option->second != 0;
    }
    if (const char* value = std::getenv("HOOT_X68K_MFP_IGNORE_OVERRIDES")) {
        mfp_ignore_overrides_ = std::strcmp(value, "1") == 0
            || std::strcmp(value, "on") == 0
            || std::strcmp(value, "true") == 0;
    }
    startup_policy_ = StartupPolicy::Auto;
    // Backward compatibility for older local catalogues. Production catalogues
    // no longer need a per-pack hoot_startup override: auto mode retries the
    // legacy direct-IRQ bootstrap only when the first mailbox command is not
    // consumed by the native Human68k/MFP path.
    if (const auto option = entry.options.find("hoot_startup");
        option != entry.options.end() && option->second != 0) {
        startup_policy_ = StartupPolicy::LegacyHoot;
    }
    if (const char* value = std::getenv("HOOT_X68K_STARTUP")) {
        if (std::strcmp(value, "auto") == 0) {
            startup_policy_ = StartupPolicy::Auto;
        } else if (std::strcmp(value, "native") == 0
                   || std::strcmp(value, "human68k") == 0
                   || std::strcmp(value, "0") == 0
                   || std::strcmp(value, "off") == 0) {
            startup_policy_ = StartupPolicy::Native;
        } else if (std::strcmp(value, "hoot") == 0
                   || std::strcmp(value, "legacy") == 0
                   || std::strcmp(value, "direct") == 0
                   || std::strcmp(value, "1") == 0
                   || std::strcmp(value, "on") == 0) {
            startup_policy_ = StartupPolicy::LegacyHoot;
        }
    }
    legacy_startup_active_ = startup_policy_ == StartupPolicy::LegacyHoot;
    startup_fallbacks_ = 0;
    if (legacy_startup_active_) {
        activate_legacy_startup();
    }
    mfp_timer_divider_ = 1;
    if (!mfp_ignore_overrides_) {
        if (const auto option = entry.options.find("mfp_timer_divider");
            option != entry.options.end()) {
            mfp_timer_divider_ = std::clamp(option->second, 1, 1000);
        }
    }
    mfp_sound_timer_ = -1;
    if (!mfp_ignore_overrides_) {
        if (const auto option = entry.options.find("mfp_sound_timer");
            option != entry.options.end()) {
            mfp_sound_timer_ = option->second;
        }
    }
    mfp_initial_ierb_ = 0x3e;
    if (!mfp_ignore_overrides_) {
        if (const auto option = entry.options.find("mfp_initial_ierb");
            option != entry.options.end()) {
            mfp_initial_ierb_ = static_cast<uint8_t>(option->second);
        }
    }
    mfp_initial_imrb_ = 0x3e;
    if (!mfp_ignore_overrides_) {
        if (const auto option = entry.options.find("mfp_initial_imrb");
            option != entry.options.end()) {
            mfp_initial_imrb_ = static_cast<uint8_t>(option->second);
        }
    }
    if (const char* value = std::getenv("HOOT_X68K_MIDI")) {
        if (std::strcmp(value, "1") == 0 || std::strcmp(value, "on") == 0) {
            midi_enabled_ = true;
        } else if (std::strcmp(value, "0") == 0 || std::strcmp(value, "off") == 0) {
            midi_enabled_ = false;
        }
    }

    const auto archive_path = std::filesystem::path(packs_path) / (entry.archive + ".zip");
    ZipArchive archive;
    if (!archive.open(archive_path, error)) {
        return HOOT_ERROR_IO;
    }

    for (const auto& asset : entry.assets) {
        if (asset.type != "code" && asset.type != "x") {
            continue;
        }

        if (asset.type == "x") {
            if (!load_human68k_x_at(archive, rom_, asset.path, asset.offset, error)) {
                return HOOT_ERROR_IO;
            }
            loaded_code_bytes_ += 1;
            continue;
        }

        auto data = archive.read(asset.path, error);
        if (!error.empty()) {
            const auto alternate = alternate_asset_path(entry, asset);
            if (!alternate.empty()) {
                error.clear();
                data = archive.read(alternate, error);
            }
        }
        if (!error.empty()) {
            return HOOT_ERROR_IO;
        }
        if (!asset.transform.empty()) {
            if (asset.transform != "opmdrv-compact-voice") {
                error = "unsupported x68k asset transform: " + asset.transform;
                return HOOT_ERROR_UNSUPPORTED;
            }
            data = expand_opmdrv_compact_voice(data);
            has_opmdrv_voice_transform_ = true;
        }
        if (asset.offset >= rom_.size()) {
            error = "x68k code asset offset is outside ROM: " + asset.path;
            return HOOT_ERROR_PARSE;
        }

        const auto count = std::min<size_t>(data.size(), rom_.size() - asset.offset);
        std::copy_n(data.begin(), count, rom_.begin() + static_cast<std::ptrdiff_t>(asset.offset));
        loaded_code_bytes_ += count;
    }

    for (const auto& asset : entry.assets) {
        constexpr std::string_view prefix = "voicebank:";
        if (asset.type.compare(0, prefix.size(), prefix) != 0) {
            continue;
        }
        auto data = archive.read(asset.path, error);
        if (!error.empty()) {
            return HOOT_ERROR_IO;
        }
        if (!asset.transform.empty()) {
            if (asset.transform != "opmdrv-compact-voice") {
                error = "unsupported x68k voice-bank transform: " + asset.transform;
                return HOOT_ERROR_UNSUPPORTED;
            }
            data = expand_opmdrv_compact_voice(data);
            has_opmdrv_voice_transform_ = true;
        }
        voice_banks_[asset.type.substr(prefix.size())] = {asset.offset, std::move(data)};
    }

    if (loaded_code_bytes_ == 0) {
        error = "x68k/generic entry did not load any code asset";
        return HOOT_ERROR_NOT_FOUND;
    }

    // Both the bootstrap and resident Human68k drivers use writable data in
    // the mapped image.  Keep the pack image so selecting a new cue can
    // restart from power-on state instead of reusing self-modified state.
    rom_image_ = rom_;
    reset_sp_ = read_be32(0);
    reset_pc_ = read_be32(4);
    memdump_address_ = read_be32(0x800);
    open_trace_from_environment();
    if (trace_.is_open()) {
        trace_ << "midi-config cycles=0 pc=0x000000 title=\"" << entry.title
               << "\" midiout=" << (midiout != entry.options.end() ? midiout->second : -1)
               << " midiout-type=" << midiout_type_
               << " archive=\"" << entry.archive << "\""
               << " cpu-clock=" << cpu_clock_hz_
               << " ym2151-clock=" << ym2151_clock_hz_
               << " mfp=" << (mfp_enabled_ ? 1 : 0)
               << " mfp-core=" << (mame_mfp_ ? "mame" : "hoot")
               << " mfp-bootstrap=" << (mfp_bootstrap_ ? 1 : 0)
               << " mfp-ignore-overrides=" << (mfp_ignore_overrides_ ? 1 : 0)
               << " startup-policy="
               << (startup_policy_ == StartupPolicy::Auto ? "auto"
                   : startup_policy_ == StartupPolicy::Native ? "native" : "hoot")
               << " startup=" << (legacy_startup_active_ ? "hoot" : "native")
               << " pcm8=" << (pcm8_enabled_ ? 1 : 0)
               << " dmaint=" << (dmaint_enabled_ ? 1 : 0)
               << "\n";
    }
    trace_io(midi_enabled_ ? "midi-board-present" : "midi-board-absent", 0xeafa00, 0);
    const char* ym2151_core = std::getenv("HOOT_X68K_YM2151_CORE");
    const bool use_nuked_ym2151 = ym2151_core != nullptr && std::strcmp(ym2151_core, "nuked") == 0;
    if (!ym2151_.initialize(ym2151_clock_hz_,
                            static_cast<uint32_t>(sample_rate_),
                            use_nuked_ym2151)) {
        error = "unable to initialize libvgm YM2151 core";
        return HOOT_ERROR_UNSUPPORTED;
    }
    if (!adpcm_.initialize(static_cast<uint32_t>(sample_rate_))) {
        error = "unable to initialize libvgm OKIM6258 core";
        return HOOT_ERROR_UNSUPPORTED;
    }
    midi_synth_.reset();
    midi_transport_.reset();
    midi_transport_.set_sink([this](const X68kMidiMessage& message) {
        handle_midi_message(message);
    });
    // Type 0 means Hoot's untyped/"NORMAL" MIDI route. The two current
    // X68000 catalogue entries explicitly identify themselves as MT-32, so
    // route those through Munt rather than leaving known-good module metadata
    // unused. Type 6 is Hoot's Vermouth software-synth class and type 5 is
    // Korg M1; both get a FluidSynth compatibility renderer so transport and
    // arrangement can be heard even though FluidSynth is not an M1 emulator.
    const bool normal_mt32 = midiout_type_ == 0
        && (title_contains_ci(entry.title, "mt-32") || title_contains_ci(entry.title, "mt32"));
    const bool munt_compatible = normal_mt32 || midiout_type_ == 1 || midiout_type_ == 2 || midiout_type_ == 3;
    const bool fluidsynth_compatible = midiout_type_ == 4 || midiout_type_ == 5
        || midiout_type_ == 6 || midiout_type_ == 7 || midiout_type_ == 8;
    if (midi_enabled_) {
        std::string preference = "auto";
        if (const char* value = std::getenv("HOOT_X68K_MIDI_BACKEND"); value != nullptr && value[0] != '\0') {
            preference = value;
            std::transform(preference.begin(), preference.end(), preference.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        }
        const bool backend_disabled = preference == "none" || preference == "off";
        const bool force_nuked = preference == "nuked" || preference == "nuked-sc55" ||
                                 preference == "nuked-sc55-clap";
        const bool force_fluid = preference == "fluidsynth" || preference == "fluid";
        const bool force_vermouth = preference == "vermouth";
        const bool force_munt = preference == "munt" || preference == "mt32emu" || preference == "mt-32";
        const bool force_cm64 = preference == "cm64" || preference == "cm-64";
        const bool force_cm32p = preference == "cm32p" || preference == "cm-32p";
        std::string nuked_error;
        std::string fluid_error;
        std::string vermouth_error;
        std::string munt_error;
        std::string cm64_error;
        std::string cm32p_error;
        const std::string cm32p_card = cm32p_card_hint(entry.title);

        // Hoot midiout_type 6 is the historical Vermouth software synth.
        // Prefer the actual Vermouth DLL/shared-library ABI; retain the old
        // FluidSynth path only as an audible compatibility fallback.
        if (!backend_disabled && midiout_type_ == 6 && !force_nuked && !force_fluid &&
            !force_munt && !force_cm64 && !force_cm32p) {
            auto synth = std::make_unique<VermouthMidiSynth>();
            if (synth->open(sample_rate_, {}, vermouth_error)) {
                midi_synth_ = std::move(synth);
                trace_io("midi-synth-ready-vermouth", 0xeafa00, 7);
            } else {
                trace_io("midi-synth-vermouth-unavailable", 0xeafa00, 0);
            }
        }

        // Hoot midiout_type 3 denotes CM-64.  In auto mode prefer a complete
        // module: Munt renders CM-32L/LA while the built-in CM-32P renderer
        // handles the six PCM parts.  Retain the older LA-only fallback when
        // CM-32P ROMs are not installed so existing setups keep working.
        if (!backend_disabled && midiout_type_ == 3 && !force_nuked && !force_fluid && !force_munt && !force_cm32p && !force_vermouth) {
            auto synth = std::make_unique<Cm64MidiSynth>(cm32p_card);
            if (synth->open(sample_rate_, {}, cm64_error)) {
                if (!cm32p_card.empty() && !synth->pcm_card_loaded()) {
                    driver_warning_ = cm32p_missing_card_warning(cm32p_card);
                }
                midi_synth_ = std::move(synth);
                trace_io("midi-synth-ready-cm64", 0xeafa00, 5);
            } else {
                trace_io("midi-synth-cm64-unavailable", 0xeafa00, 0);
            }
        }

        if (!backend_disabled && midiout_type_ == 3 && force_cm32p) {
            auto synth = std::make_unique<Cm32pMidiSynth>(cm32p_card);
            if (synth->open(sample_rate_, {}, cm32p_error)) {
                if (!cm32p_card.empty() && !synth->card_loaded()) {
                    driver_warning_ = cm32p_missing_card_warning(cm32p_card);
                }
                midi_synth_ = std::move(synth);
                trace_io("midi-synth-ready-cm32p", 0xeafa00, 6);
            }
        }

        if (!backend_disabled && !midi_synth_ && munt_compatible && !force_nuked && !force_fluid && !force_cm64 && !force_cm32p && !force_vermouth) {
            const auto model = midiout_type_ == 3 ? Mt32EmuModel::CM32L : Mt32EmuModel::MT32;
            auto synth = std::make_unique<Mt32EmuMidiSynth>(model);
            if (synth->open(sample_rate_, {}, munt_error)) {
                midi_synth_ = std::move(synth);
                trace_io("midi-synth-ready-munt", 0xeafa00, midiout_type_ == 3 ? 4 : 3);
                if (midiout_type_ == 3 && preference == "auto" && !cm64_error.empty()) {
                    // Munt opened successfully, so the failed full-CM-64 attempt
                    // was specifically the PCM side. Explain the audible impact
                    // instead of exposing only a low-level ROM-path error.
                    driver_warning_ = cm64_la_only_warning(cm32p_card);
                }
            } else {
                trace_io("midi-synth-munt-unavailable", 0xeafa00, 0);
            }
        }

        if (!backend_disabled && midiout_type_ == 4 && !force_fluid && !force_munt && !force_cm64 && !force_cm32p && !force_vermouth) {
            auto synth = std::make_unique<NukedSc55ClapMidiSynth>();
            if (synth->open(sample_rate_, {}, nuked_error)) {
                midi_synth_ = std::move(synth);
                trace_io("midi-synth-ready-nuked-sc55", 0xeafa00, 2);
            } else {
                trace_io("midi-synth-nuked-unavailable", 0xeafa00, 0);
            }
        }

        if (!backend_disabled && !midi_synth_ && force_vermouth && midiout_type_ == 6) {
            auto synth = std::make_unique<VermouthMidiSynth>();
            if (synth->open(sample_rate_, {}, vermouth_error)) {
                midi_synth_ = std::move(synth);
                trace_io("midi-synth-ready-vermouth", 0xeafa00, 7);
            }
        }

        if (!backend_disabled && !midi_synth_ && fluidsynth_compatible && !force_nuked && !force_munt && !force_cm64 && !force_cm32p && !force_vermouth) {
            auto synth = std::make_unique<FluidSynthMidiSynth>();
            std::string soundfont_override;
            if (midiout_type_ == 5) {
                if (const char* value = std::getenv("HOOT_X68K_M1_SOUNDFONT"); value != nullptr && value[0] != '\0') {
                    soundfont_override = value;
                }
            }
            if (synth->open(sample_rate_, soundfont_override, fluid_error)) {
                midi_synth_ = std::move(synth);
                trace_io("midi-synth-ready-fluidsynth", 0xeafa00, 1);
            } else {
                trace_io("midi-synth-fluid-unavailable", 0xeafa00, 0);
            }
        }

        if (midi_synth_) {
            reset_midi_synth_mode();
            if (midiout_type_ == 5 && driver_warning_.empty()) {
                driver_warning_ = "Korg M1 target: FluidSynth compatibility is active, not hardware-exact M1. "
                    "For closer timbres set [midi] m1_soundfont in hootplay.ini; authentic playback requires a licensed M1-compatible synth with original PCM/program data.";
            } else if (midiout_type_ == 7 && driver_warning_.empty() &&
                       std::string(midi_synth_->backend_name()) == "fluidsynth") {
                driver_warning_ = "Roland SC-88 target: FluidSynth compatibility rendering is active, not hardware-exact SC-88. "
                    "Set [midi] soundfont in hootplay.ini for the closest available compatible bank; authentic playback requires a real SC-88 or a dedicated SC-88-compatible renderer.";
            } else if (midiout_type_ == 6 && driver_warning_.empty() &&
                       std::string(midi_synth_->backend_name()) != "vermouth") {
                driver_warning_ = "Vermouth target: native Vermouth is unavailable; FluidSynth compatibility is active. "
                    "For original GUS-patch timbres set [midi] vermouth_library and vermouth_abi=legacy; "
                    "place timidity.cfg/GUS patches where Vermouth can find them.";
            }
        } else if (backend_disabled) {
            trace_io("midi-synth-disabled", 0xeafa00, 0);
        } else if (force_cm64 && midiout_type_ != 3) {
            driver_warning_ = "CM-64 backend is only selected for Hoot midiout_type 3";
        } else if (force_cm64) {
            driver_warning_ = cm64_error.empty() ? "full CM-64 backend unavailable" : cm64_error;
        } else if (force_cm32p && midiout_type_ != 3) {
            driver_warning_ = "CM-32P backend is only selected for Hoot CM-64 midiout_type 3";
        } else if (force_cm32p) {
            driver_warning_ = cm32p_error.empty() ? "CM-32P backend unavailable" : cm32p_error;
        } else if (force_munt && !munt_compatible) {
            driver_warning_ = "Munt/mt32emu is only selected for Hoot MT-32/CM-64 midiout_type 1, 2 or 3";
        } else if (force_munt) {
            driver_warning_ = munt_error.empty() ? "Munt/mt32emu backend unavailable" : munt_error;
        } else if (force_nuked && midiout_type_ != 4) {
            driver_warning_ = "Nuked-SC55 is only selected for Hoot SC-55/GS midiout_type 4";
        } else if (force_nuked) {
            driver_warning_ = nuked_error.empty() ? "Nuked-SC55 backend unavailable" : nuked_error;
        } else if (force_vermouth && midiout_type_ != 6) {
            driver_warning_ = "Vermouth backend is only selected for Hoot midiout_type 6";
        } else if (force_vermouth) {
            driver_warning_ = vermouth_error.empty() ? "Vermouth backend unavailable" : vermouth_error;
        } else if (force_fluid) {
            driver_warning_ = fluid_error.empty() ? "FluidSynth backend unavailable" : fluid_error;
        } else if (fluidsynth_compatible) {
            if (!nuked_error.empty() && midiout_type_ == 4) {
                driver_warning_ = nuked_error + "; " +
                    (fluid_error.empty() ? "FluidSynth fallback unavailable" : fluid_error);
            } else if (midiout_type_ == 6 && !vermouth_error.empty()) {
                driver_warning_ = vermouth_error + "; " +
                    (fluid_error.empty() ? "FluidSynth fallback unavailable" : fluid_error);
            } else {
                driver_warning_ = fluid_error.empty() ? "MIDI software synth unavailable" : fluid_error;
            }
        } else if (midiout_type_ == 3) {
            if (!cm64_error.empty() && !munt_error.empty()) driver_warning_ = cm64_error + "; " + munt_error;
            else driver_warning_ = !cm64_error.empty() ? cm64_error : (munt_error.empty() ? "CM-64 software synth unavailable" : munt_error);
        } else if (munt_compatible) {
            driver_warning_ = munt_error.empty() ? "Munt/mt32emu backend unavailable" : munt_error;
        } else {
            driver_warning_ = "MIDI transport active; this midiout_type needs a dedicated synth backend";
            trace_io("midi-synth-unsupported-type", 0xeafa00, static_cast<uint8_t>(midiout_type_));
        }
    }
    // Hoot's mixer treats 0x100 as unity and the generic X68000 driver uses
    // 0xc0 by default.  Keep that exact YM2151 scale here: using unity for the
    // 0xc0 default made loud OPMDRV material (notably Xak) hard-clip even
    // before ADPCM was mixed in.
    opm_gain_ = 192.0 / 256.0;
    if (const auto option = entry.options.find("opm_mix"); option != entry.options.end()) {
        opm_gain_ = std::clamp(static_cast<double>(option->second) / 256.0, 0.0, 4.0);
    }
    adpcm_gain_ = 0.40;
    if (const auto option = entry.options.find("pcm_mix"); option != entry.options.end()) {
        adpcm_gain_ *= std::clamp(static_cast<double>(option->second) / 240.0, 0.0, 4.0);
    }
    // PCM8 historically shares Hoot's PCM mixer control, but keeps a
    // separate host gain so direct-block output can be tuned independently.
    pcm8_gain_ = adpcm_gain_;
    total_gain_ = 1.0;
    if (const auto option = entry.options.find("total_mix"); option != entry.options.end()) {
        total_gain_ = std::clamp(static_cast<double>(option->second) / 256.0, 0.0, 4.0);
    }
    if (const char* value = std::getenv("HOOT_X68K_OPM_GAIN")) {
        opm_gain_ = std::clamp(std::strtod(value, nullptr), 0.0, 4.0);
    }
    if (const char* value = std::getenv("HOOT_X68K_ADPCM_GAIN")) {
        adpcm_gain_ = std::clamp(std::strtod(value, nullptr), 0.0, 4.0);
        pcm8_gain_ = adpcm_gain_;
    }
    if (const char* value = std::getenv("HOOT_X68K_PCM8_GAIN")) {
        pcm8_gain_ = std::clamp(std::strtod(value, nullptr), 0.0, 4.0);
    }
    midi_gain_ = 0.70;
    if (const char* value = std::getenv("HOOT_X68K_MIDI_GAIN")) {
        midi_gain_ = std::clamp(std::strtod(value, nullptr), 0.0, 4.0);
    }
    if (const char* value = std::getenv("HOOT_X68K_TOTAL_GAIN")) {
        total_gain_ = std::clamp(std::strtod(value, nullptr), 0.0, 4.0);
    }
    if (const char* value = std::getenv("HOOT_X68K_MUTE_PERCUSSION")) {
        mute_percussion_ = std::strcmp(value, "1") == 0
            || std::strcmp(value, "on") == 0
            || std::strcmp(value, "true") == 0;
    }
    opm_mute_mask_ = parse_opm_mute_mask(std::getenv("HOOT_X68K_CHANNELS"));
    ym2151_.set_mute_mask(opm_mute_mask_ | ui_opm_mute_mask_);
    musashi_set_active_bus(this);
    m68k_set_cpu_type(M68K_CPU_TYPE_68000);
    m68k_init();
    m68k_set_instr_hook_callback(trace_.is_open() ? musashi_instruction_hook_callback : nullptr);
    m68k_pulse_reset();
    m68k_set_reg(M68K_REG_ISP, reset_sp_);
    m68k_set_reg(M68K_REG_MSP, reset_sp_);
    initialize_mfp();
    mfp_suspended_ = true;
    // The legacy Hoot X68000 host reset the 68000 but deliberately did not
    // execute a startup slice before the first Play() mailbox command. Some
    // resident ZMSC images sample that mailbox during their one-time startup;
    // pre-running them leaves the direct-IRQ bootstrap idle before a cue has
    // been posted. Native Human68k/MFP images still need the short setup run.
    if (!legacy_startup_active_) {
        execute_with_audio_clock(5000.0 / cpu_clock_hz_);
    }
    mfp_trap_bridge_ = false;
    mfp_trap_magic_ = 0;
    iocs_opm_interrupt_handler_ = 0;
    iocs_vectors_.clear();
    if (mfp_enabled_) {
        const uint32_t handler = read_memory_32(47u * 4u) & 0x00ffffffu;
        if (handler + 22u < rom_.size()
            && read_memory_32(0x43u * 4u) == read_memory_32(0x40u * 4u)
            && read_memory_8(handler + 0) == 0x0c
            && read_memory_8(handler + 1) == 0x00
            && read_memory_8(handler + 2) == 0x00
            && read_memory_8(handler + 4) == 0x67
            && read_memory_8(handler + 5) == 0x0a
            && read_memory_8(handler + 6) == 0x13
            && read_memory_8(handler + 7) == 0xfc
            && read_memory_8(handler + 8) == 0x00
            && read_memory_8(handler + 9) == 0x2f
            && read_memory_8(handler + 10) == 0x00
            && read_memory_8(handler + 11) == 0xe0
            && read_memory_8(handler + 12) == 0x00
            && read_memory_8(handler + 13) == 0x10
            && read_memory_8(handler + 14) == 0x4e
            && read_memory_8(handler + 15) == 0x73
            && read_memory_8(handler + 16) == 0x4e
            && read_memory_8(handler + 17) == 0xb9
            && read_memory_8(handler + 22) == 0x4e
            && read_memory_8(handler + 23) == 0x73) {
            mfp_trap_bridge_ = true;
            mfp_trap_magic_ = read_memory_8(handler + 3);
            // This bootstrap forwards the YM2151 timer IRQ through trap
            // #15 using D0=$f0. It is not an independent MFP Timer-D clock.
            // Keep all synthetic MFP timers off and expose only the OPM IRQ
            // input (legacy MFP pending bit 0x08 / vector 0x43).
            mfp_regs_[3] = 0;
            mfp_regs_[4] = 0x08;
            mfp_regs_[5] = 0;
            mfp_regs_[6] = 0;
            mfp_regs_[7] = 0;
            mfp_regs_[8] = 0;
            mfp_regs_[9] = 0;
            mfp_regs_[10] = 0x08;
            mfp_regs_[11] = 0x40;
            mfp_regs_[12] = 0;
            mfp_regs_[13] = 0;
            mfp_regs_[14] = 0;
            mfp_regs_[15] = 0;
            mfp_regs_[16] = 0;
            mfp_regs_[17] = 0;
            mfp_regs_[18] = 0;
            mfp_timer_values_[0] = 0;
            mfp_timer_values_[1] = 0;
            mfp_timer_values_[2] = 0;
            mfp_timer_values_[3] = 0;
            std::fill(std::begin(mfp_timer_accumulators_),
                      std::end(mfp_timer_accumulators_), 0.0);

        }
    }
    mfp_suspended_ = false;
    loaded_ = true;
    return HOOT_OK;
}

HootResult X68kGenericDriver::select_track(const HootEntry& entry,
                                           int track_index,
                                           std::string& error)
{
    if (!loaded_) {
        error = "x68k/generic driver is not loaded";
        return HOOT_ERROR_NOT_LOADED;
    }
    if (track_index < 0 || static_cast<size_t>(track_index) >= entry.tracks.size()) {
        error = "track index is outside the catalog track list";
        return HOOT_ERROR_INVALID_ARGUMENT;
    }

    startup_preroll_.clear();
    startup_preroll_offset_ = 0;
    const bool first_selection = !has_selected_track_;
    const auto reset_on_track = entry.options.find("reset_on_track");
    const bool restart_machine = reset_on_track != entry.options.end()
        && reset_on_track->second != 0;
    if (has_selected_track_ && pcm8_enabled_) {
        pcm8_.stop_playback();
    }
    if (restart_machine) {
        reset();
    } else if (has_selected_track_) {
        const auto stop = entry.options.find("stop");
        if (stop != entry.options.end()) {
            post_mailbox_command_fixed(static_cast<uint16_t>(stop->second));
        }
    }
    select_voice_bank(entry, track_index);
    diagnose_opmdrv_voices(entry, track_index);
    selected_track_ = track_index;
    selected_code_ = entry.tracks[track_index].code;

    constexpr uint32_t kStartupHandshakeCycles = 3000000;
    constexpr double kStartupProbeSeconds = 0.050;
    const bool startup_candidate = first_selection && pcm8_enabled_ && !midi_enabled_;
    const uint64_t keyons_before = debug_ym2151_keyons_;
    const uint64_t pcm8_starts_before = pcm8_.stats().starts;
    bool consumed = false;
    if (startup_candidate) {
        consumed = dispatch_mailbox_command(static_cast<uint16_t>(selected_code_),
                                             kStartupHandshakeCycles);
    } else if (first_selection && midi_enabled_) {
        // MIDI-capable ZMSC images perform a serialized module/channel reset
        // before they return to the host mailbox. At the CZ-6BM1 wire rate,
        // that startup stream is much longer than Hoot's historic fixed
        // 100k-cycle post window. Poll only the first MIDI command to its real
        // guest acknowledgement; subsequent cue changes retain the established
        // fixed timing.
        constexpr uint32_t kMidiStartupHandshakeCycles = 15000000;
        consumed = dispatch_mailbox_command(static_cast<uint16_t>(selected_code_),
                                             kMidiStartupHandshakeCycles);
    } else {
        // Preserve the original generic-host timing for ordinary OPM and
        // subsequent track changes. Poll-to-ack is limited to first-start
        // compatibility probes so deterministic OPM regression positions stay
        // unchanged.
        post_mailbox_command_fixed(static_cast<uint16_t>(selected_code_));
        consumed = mailbox_flag_ == 0;
    }

    // Auto mode observes a short piece of real output after the guest accepts
    // its first cue. The samples are retained and returned by render_s16(), so
    // startup detection never cuts the opening attack from successful native
    // playback. A zero-length IOCS ADPCM kick is ignored here because several
    // old bootstraps emit one even when their music IRQ route is unusable.
    auto probe_startup = [&](bool accept_iocs_adpcm) {
        if (!consumed) {
            return false;
        }
        const uint64_t probe_keyons_before = debug_ym2151_keyons_;
        const uint64_t probe_adpcm_before = debug_adpcm_starts_;
        const uint64_t probe_pcm8_before = pcm8_.stats().starts;
        const int probe_frames = std::max(1, static_cast<int>(
            std::ceil(static_cast<double>(sample_rate_) * kStartupProbeSeconds)));
        std::vector<int16_t> probe(static_cast<size_t>(probe_frames) * 2u);
        render_s16(probe.data(), probe_frames);
        const bool active = debug_ym2151_keyons_ != probe_keyons_before
            || pcm8_.stats().starts != probe_pcm8_before
            || (accept_iocs_adpcm && debug_adpcm_starts_ != probe_adpcm_before);
        if (active) {
            startup_preroll_ = std::move(probe);
            startup_preroll_offset_ = 0;
        }
        return active;
    };

    bool startup_activity = debug_ym2151_keyons_ != keyons_before
        || pcm8_.stats().starts != pcm8_starts_before;
    if (first_selection
        && startup_policy_ == StartupPolicy::Auto
        && !legacy_startup_active_
        && pcm8_enabled_
        && !midi_enabled_
        && consumed
        && !startup_activity) {
        startup_activity = probe_startup(false);
    }

    // Some older standalone Hoot bootstraps consume the cue in a native
    // Human68k/MFP environment but never receive their music IRQ. Retry once
    // from a clean machine using the original Hoot direct-IRQ environment.
    // MIDI configurations are excluded because silence there means an external
    // synthesizer backend is absent, not that X68000 startup failed.
    if (first_selection
        && startup_policy_ == StartupPolicy::Auto
        && !legacy_startup_active_
        && pcm8_enabled_
        && !midi_enabled_
        && (!consumed || !startup_activity)) {
        trace_io("startup-auto-fallback", mailbox_code_, mailbox_flag_);
        ++startup_fallbacks_;
        startup_preroll_.clear();
        startup_preroll_offset_ = 0;
        activate_legacy_startup();
        reset();
        select_voice_bank(entry, track_index);
        diagnose_opmdrv_voices(entry, track_index);
        selected_track_ = track_index;
        selected_code_ = entry.tracks[track_index].code;
        const uint64_t fallback_keyons_before = debug_ym2151_keyons_;
        const uint64_t fallback_adpcm_before = debug_adpcm_starts_;
        const uint64_t fallback_pcm8_before = pcm8_.stats().starts;
        consumed = dispatch_mailbox_command(static_cast<uint16_t>(selected_code_),
                                            kStartupHandshakeCycles);
        startup_activity = debug_ym2151_keyons_ != fallback_keyons_before
            || debug_adpcm_starts_ != fallback_adpcm_before
            || pcm8_.stats().starts != fallback_pcm8_before;
        if (consumed && !startup_activity) {
            startup_activity = probe_startup(true);
        }
        trace_io(consumed ? "startup-auto-fallback-ok" : "startup-auto-fallback-pending",
                 mailbox_code_, mailbox_flag_);
        if (!consumed) {
            driver_warning_ = "x68k startup auto-detection could not consume the first mailbox command";
        } else if (!startup_activity) {
            driver_warning_ = "x68k startup auto-detection consumed the cue but observed no key-on or sample-start activity";
        }
    }
    has_selected_track_ = true;
    return HOOT_OK;
}

void X68kGenericDriver::activate_legacy_startup()
{
    legacy_startup_active_ = true;
    // The original Hoot generic X68000 host did not expose an MFP. YM2151
    // timer events were delivered directly as IRQ6 and acknowledged through
    // the IOCS OPM vector ($43).
    mfp_enabled_ = false;
    mame_mfp_ = false;
}

void X68kGenericDriver::post_mailbox_command_fixed(uint16_t command, uint32_t cycles)
{
    mailbox_code_ = command;
    mailbox_flag_ = 0x01;
    musashi_set_active_bus(this);
    mfp_suspended_ = true;
    m68k_set_irq(0);
    execute_with_audio_clock(static_cast<double>(cycles) / cpu_clock_hz_);
    mfp_suspended_ = false;
    mfp_irq_asserted_ = false;
    update_mfp_irq();
}

bool X68kGenericDriver::dispatch_mailbox_command(uint16_t command, uint32_t max_cycles)
{
    mailbox_code_ = command;
    mailbox_flag_ = 0x01;
    musashi_set_active_bus(this);
    mfp_suspended_ = true;
    m68k_set_irq(0);
    // Poll the guest's own mailbox acknowledgement in short slices. A native
    // ZMSC image may need several timer periods before it reaches the mailbox,
    // while an incompatible bootstrap never clears it. Stopping on the exact
    // acknowledgement avoids throwing away the beginning of a valid cue.
    constexpr uint32_t kSliceCycles = 5000;
    uint32_t executed = 0;
    while (mailbox_flag_ != 0 && executed < max_cycles) {
        const uint32_t slice = std::min(kSliceCycles, max_cycles - executed);
        execute_with_audio_clock(static_cast<double>(slice) / cpu_clock_hz_);
        executed += slice;
    }
    mfp_suspended_ = false;
    mfp_irq_asserted_ = false;
    update_mfp_irq();
    return mailbox_flag_ == 0;
}

void X68kGenericDriver::diagnose_opmdrv_voices(const HootEntry& entry, int track_index)
{
    track_warning_.clear();
    if (!has_opmdrv_voice_transform_) {
        return;
    }

    const auto filename = track_filename(entry, track_index);
    const auto asset = std::find_if(entry.assets.begin(), entry.assets.end(), [&](const auto& item) {
        return std::filesystem::path(item.path).filename().string() == filename;
    });
    if (asset == entry.assets.end() || asset->offset >= rom_.size()) {
        return;
    }

    std::array<bool, 256> available{};
    const size_t bank_end = std::min(rom_.size(), active_voice_bank_offset_ + kVoiceBankCapacity);
    for (size_t pos = active_voice_bank_offset_; pos + 2 < bank_end; ++pos) {
        if (rom_[pos] == '(' && rom_[pos + 1] == 0xb6) {
            available[rom_[pos + 2]] = true;
        } else if (rom_[pos] == '(' && (rom_[pos + 1] == 'v' || rom_[pos + 1] == 'V')
                   && rom_[pos + 2] >= '0' && rom_[pos + 2] <= '9') {
            unsigned voice = 0;
            size_t digit = pos + 2;
            while (digit < bank_end && rom_[digit] >= '0' && rom_[digit] <= '9') {
                voice = voice * 10 + static_cast<unsigned>(rom_[digit] - '0');
                ++digit;
            }
            if (voice < available.size() && digit < bank_end && rom_[digit] == ',') {
                available[voice] = true;
            }
        }
    }

    size_t track_end = rom_.size();
    for (const auto& item : entry.assets) {
        if (item.offset > asset->offset) {
            track_end = std::min(track_end, static_cast<size_t>(item.offset));
        }
    }
    std::array<bool, 256> missing{};
    for (size_t pos = asset->offset; pos < track_end; ++pos) {
        if (rom_[pos] == 0) {
            break;
        }
        if (rom_[pos] != '@' || pos + 1 >= track_end
            || rom_[pos + 1] < '0' || rom_[pos + 1] > '9') {
            continue;
        }
        unsigned voice = 0;
        size_t digit = pos + 1;
        while (digit < track_end && rom_[digit] >= '0' && rom_[digit] <= '9') {
            voice = voice * 10 + static_cast<unsigned>(rom_[digit] - '0');
            ++digit;
        }
        if (voice < missing.size() && !available[voice]) {
            missing[voice] = true;
        }
        pos = digit - 1;
    }

    std::ostringstream message;
    bool any = false;
    for (size_t voice = 0; voice < missing.size(); ++voice) {
        if (!missing[voice]) {
            continue;
        }
        if (!any) {
            message << "requested YM2151 voice ";
        } else {
            message << ", ";
        }
        message << voice;
        any = true;
    }
    if (any) {
        message << (std::count(missing.begin(), missing.end(), true) == 1 ? " is" : " are")
                << " not present in the loaded OPMDRV voice bank; the driver will use its fallback voice";
        track_warning_ = message.str();
    }
}

void X68kGenericDriver::select_voice_bank(const HootEntry& entry, int track_index)
{
    if (track_index < 0 || static_cast<size_t>(track_index) >= entry.tracks.size()) {
        return;
    }
    const auto bank = voice_banks_.find(entry.tracks[track_index].voice_bank);
    if (bank == voice_banks_.end() || bank->second.offset >= rom_.size()) {
        return;
    }

    active_voice_bank_offset_ = bank->second.offset;
    const size_t capacity = std::min(kVoiceBankCapacity, rom_.size() - bank->second.offset);
    std::fill_n(rom_.begin() + static_cast<std::ptrdiff_t>(bank->second.offset), capacity, 0);
    std::copy_n(bank->second.data.begin(), std::min(bank->second.data.size(), capacity),
                rom_.begin() + static_cast<std::ptrdiff_t>(bank->second.offset));
}

void X68kGenericDriver::reset()
{
    rom_ = rom_image_;
    active_voice_bank_offset_ = kVoiceBankOffset;
    ram_.fill(0);
    scratch_.fill(0);
    high_memory_.fill(0);
    selected_track_ = 0;
    selected_code_ = 0;
    has_selected_track_ = false;
    render_cycle_remainder_ = 0.0;
    cpu_cycle_debt_ = 0;
    mailbox_flag_ = 0;
    mailbox_code_ = 0;
    mfp_suspended_ = false;
    mfp_irq_asserted_ = false;
    mfp_trap_bridge_ = false;
    mfp_trap_magic_ = 0;
    iocs_opm_interrupt_handler_ = 0;
    iocs_vectors_.clear();
    midi_reg_high_ = 0;
    midi_vector_ = 0;
    midi_int_enable_ = 0;
    midi_int_vect_ = 0x10;
    midi_int_flag_ = 0;
    midi_r05_ = 0;
    midi_g_timer_max_ = 0;
    midi_m_timer_max_ = 0;
    midi_g_timer_value_ = 0;
    midi_m_timer_value_ = 0;
    midi_irq_asserted_ = false;
    debug_midi_irq_count_ = 0;
    debug_midi_synth_frames_ = 0;
    debug_midi_sysex_handled_ = 0;
    midi_transport_.reset();
    midi_transport_.set_sink([this](const X68kMidiMessage& message) {
        handle_midi_message(message);
    });
    reset_midi_synth_mode();
    debug_cpu_cycles_ = 0;
    debug_io_reads_ = 0;
    debug_io_writes_ = 0;
    debug_ym2151_writes_ = 0;
    debug_ym2151_keyons_ = 0;
    debug_ym2151_irqs_ = 0;
    debug_unhandled_exceptions_ = 0;
    reset_ym2151_timers();
    initialize_mfp();
    debug_adpcm_writes_ = 0;
    debug_adpcm_starts_ = 0;
    adpcm_address_ = 0;
    adpcm_size_ = 0;
    current_ym2151_reg_ = 0;
    ym2151_registers_.fill(0);
    ym2151_key_on_.fill(false);
    debug_last_ym2151_reg_ = 0;
    debug_last_ym2151_data_ = 0;
    ym2151_.reset();
    ym2151_.set_mute_mask(opm_mute_mask_ | ui_opm_mute_mask_);
    adpcm_.reset();
    pcm8_.reset();
    pcm8_.set_mute_mask(ui_pcm8_mute_mask_);
    pcm8_mix_buffer_.clear();
    midi_mix_buffer_.clear();
    startup_preroll_.clear();
    startup_preroll_offset_ = 0;
    musashi_set_active_bus(this);
    m68k_pulse_reset();
    m68k_set_irq(0);
    m68k_set_reg(M68K_REG_ISP, reset_sp_);
    m68k_set_reg(M68K_REG_MSP, reset_sp_);
}

int X68kGenericDriver::render_s16(int16_t* interleaved_stereo, int frames)
{
    if (interleaved_stereo == nullptr || frames < 0) {
        return 0;
    }
    int rendered = 0;
    if (startup_preroll_offset_ < startup_preroll_.size()) {
        const size_t available_frames =
            (startup_preroll_.size() - startup_preroll_offset_) / 2u;
        const int copy_frames = std::min<int>(frames, static_cast<int>(available_frames));
        std::copy_n(startup_preroll_.data() + startup_preroll_offset_,
                    static_cast<size_t>(copy_frames) * 2u,
                    interleaved_stereo);
        startup_preroll_offset_ += static_cast<size_t>(copy_frames) * 2u;
        rendered += copy_frames;
        if (startup_preroll_offset_ >= startup_preroll_.size()) {
            startup_preroll_.clear();
            startup_preroll_offset_ = 0;
        }
    }

    // Keep register writes close to the samples they affect. Nuked OPM also
    // owns a cycle-accurate bus queue, so it needs a render clock each sample.
    const int chunk_frames = ym2151_.uses_nuked_core() ? 1 : 8;
    while (rendered < frames) {
        const int todo = std::min(chunk_frames, frames - rendered);
        const double exact_cycles = static_cast<double>(todo) * cpu_clock_hz_
            / static_cast<double>(sample_rate_) + render_cycle_remainder_;
        const int cycles = static_cast<int>(exact_cycles);
        render_cycle_remainder_ = exact_cycles - static_cast<double>(cycles);
        execute_seconds(static_cast<double>(cycles) / cpu_clock_hz_);

        mix_buffer_.resize(static_cast<size_t>(todo) * 2);
        ym2151_.render_s16(mix_buffer_.data(), todo);
        auto* destination = interleaved_stereo + (rendered * 2);
        for (int sample = 0; sample < todo * 2; ++sample) {
            const long value = std::lround(static_cast<double>(mix_buffer_[sample]) * opm_gain_);
            destination[sample] = static_cast<int16_t>(std::clamp(value, -32768l, 32767l));
        }
        const bool adpcm_was_playing = adpcm_.is_playing();
        if (!mute_percussion_ && !ui_adpcm_muted_) {
            adpcm_.mix_s16(destination, todo, adpcm_gain_);
        } else if (adpcm_was_playing) {
            // A host mute must silence the contribution without freezing the
            // guest DMA/sample timeline. Advance into a throwaway buffer.
            mix_buffer_.assign(static_cast<size_t>(todo) * 2u, 0);
            adpcm_.mix_s16(mix_buffer_.data(), todo, 0.0);
        }
        if (dmaint_enabled_ && adpcm_was_playing && !adpcm_.is_playing()) {
            dma_irq_pending_ = true;
            trace_io("irq3-assert-dma", static_cast<uint32_t>(dma_niv_), 3);
            update_cpu_irq();
        }
        if (pcm8_enabled_ && pcm8_.active_voice_count() != 0) {
            pcm8_mix_buffer_.assign(static_cast<size_t>(todo) * 2u, 0);
            pcm8_.mix_s32(pcm8_mix_buffer_.data(), todo,
                static_cast<uint32_t>(sample_rate_),
                [this](uint32_t address, uint8_t& value) {
                    return read_pcm_memory_8(address, value);
                },
                mute_percussion_ ? 0.0 : pcm8_gain_);
            for (int sample = 0; sample < todo * 2; ++sample) {
                const int64_t mixed = static_cast<int64_t>(destination[sample])
                    + pcm8_mix_buffer_[static_cast<size_t>(sample)];
                destination[sample] = static_cast<int16_t>(
                    std::clamp<int64_t>(mixed, -32768, 32767));
            }
        }
        if (midi_enabled_ && midi_synth_ && midi_synth_->active()) {
            midi_mix_buffer_.assign(static_cast<size_t>(todo) * 2u, 0);
            if (midi_synth_->render_s16(midi_mix_buffer_.data(), todo) == todo) {
                debug_midi_synth_frames_ += static_cast<uint64_t>(todo);
                for (int sample = 0; sample < todo * 2; ++sample) {
                    const int64_t midi = static_cast<int64_t>(std::lround(
                        static_cast<double>(midi_mix_buffer_[static_cast<size_t>(sample)])
                        * midi_gain_));
                    const int64_t mixed = static_cast<int64_t>(destination[sample]) + midi;
                    destination[sample] = static_cast<int16_t>(
                        std::clamp<int64_t>(mixed, -32768, 32767));
                }
            }
        }
        if (total_gain_ != 1.0) {
            for (int sample = 0; sample < todo * 2; ++sample) {
                const long value = std::lround(static_cast<double>(destination[sample]) * total_gain_);
                destination[sample] = static_cast<int16_t>(std::clamp(value, -32768l, 32767l));
            }
        }
        rendered += todo;
    }
    return frames;
}

int X68kGenericDriver::render_float(float* interleaved_stereo, int frames)
{
    if (interleaved_stereo == nullptr || frames < 0) {
        return 0;
    }

    // Do not reuse mix_buffer_ here: render_s16() uses that member as its
    // YM2151 staging buffer and may resize it while rendering. A separate
    // conversion buffer prevents the output pointer from aliasing that state.
    std::vector<int16_t> pcm(static_cast<size_t>(frames) * 2);
    const int rendered = render_s16(pcm.data(), frames);
    for (int sample = 0; sample < rendered * 2; ++sample) {
        interleaved_stereo[sample] = static_cast<float>(pcm[sample]) / 32768.0f;
    }
    return rendered;
}

void X68kGenericDriver::fill_track_info(const HootEntry& entry,
                                        int track_index,
                                        HootTrackInfo& out) const
{
    std::memset(&out, 0, sizeof(out));
    out.track_index = track_index;
    out.sample_rate = sample_rate_;
    out.debug_cpu_cycles = debug_cpu_cycles_;
    out.debug_io_reads = debug_io_reads_;
    out.debug_io_writes = debug_io_writes_;
    out.debug_opn_writes = debug_ym2151_writes_;
    out.debug_opn_keyons = debug_ym2151_keyons_;
    out.debug_pc = m68k_get_reg(nullptr, M68K_REG_PC);
    out.debug_last_opn_reg = debug_last_ym2151_reg_;
    out.debug_last_opn_data = debug_last_ym2151_data_;
    out.debug_port_writes_00 = mailbox_flag_;
    out.debug_port_writes_01 = selected_code_;
    out.debug_port_writes_02 = debug_adpcm_writes_;
    out.debug_port_writes_03 = debug_adpcm_starts_;
    out.debug_port_writes_32 = debug_ym2151_irqs_;
    out.debug_port_writes_44 = adpcm_address_;
    out.debug_port_writes_45 = adpcm_size_;
    const auto& pcm8_stats = pcm8_.stats();
    out.debug_pcm8_commands = pcm8_stats.commands;
    out.debug_pcm8_starts = pcm8_stats.starts;
    out.debug_pcm8_stops = pcm8_stats.stops;
    out.debug_pcm8_mode_changes = pcm8_stats.mode_changes;
    out.debug_pcm8_queries = pcm8_stats.queries;
    out.debug_pcm8_unimplemented = pcm8_stats.unimplemented;
    out.debug_pcm8_unknown = pcm8_stats.unknown;
    out.debug_pcm8_unsupported_channels = pcm8_stats.unsupported_channels;
    out.debug_pcm8_rendered_voice_frames = pcm8_stats.rendered_voice_frames;
    out.debug_pcm8_rendered_source_bytes = pcm8_stats.rendered_source_bytes;
    out.debug_pcm8_completed_voices = pcm8_stats.completed_voices;
    out.debug_pcm8_memory_faults = pcm8_stats.memory_faults;
    out.debug_pcm8_active_voices = static_cast<uint32_t>(pcm8_.active_voice_count());
    out.debug_pcm8_last_d0 = pcm8_stats.last_d0;
    out.debug_pcm8_last_d1 = pcm8_stats.last_d1;
    out.debug_pcm8_last_d2 = pcm8_stats.last_d2;
    out.debug_pcm8_last_a1 = pcm8_stats.last_a1;
    out.debug_pcm8_last_kind = static_cast<uint32_t>(pcm8_stats.last_kind);
    out.debug_pcm8_last_channel = pcm8_stats.last_channel;
    const auto& midi_stats = midi_transport_.stats();
    out.debug_midi_bytes_enqueued = midi_stats.bytes_enqueued;
    out.debug_midi_bytes_transmitted = midi_stats.bytes_transmitted;
    out.debug_midi_channel_messages = midi_stats.channel_messages;
    out.debug_midi_system_common_messages = midi_stats.system_common_messages;
    out.debug_midi_sysex_messages = midi_stats.sysex_messages;
    out.debug_midi_sysex_bytes = midi_stats.sysex_bytes;
    out.debug_midi_realtime_bytes = midi_stats.realtime_bytes;
    out.debug_midi_running_status_messages = midi_stats.running_status_messages;
    out.debug_midi_malformed_bytes = midi_stats.malformed_bytes;
    out.debug_midi_note_ons = midi_stats.note_on_messages;
    out.debug_midi_note_offs = midi_stats.note_off_messages;
    out.debug_midi_control_changes = midi_stats.control_changes;
    out.debug_midi_program_changes = midi_stats.program_changes;
    out.debug_midi_pitch_bends = midi_stats.pitch_bends;
    out.debug_midi_fifo_full_transitions = midi_stats.fifo_full_transitions;
    out.debug_midi_irq_count = debug_midi_irq_count_;
    out.debug_midi_synth_frames = debug_midi_synth_frames_;
    out.debug_midi_sysex_handled = debug_midi_sysex_handled_;
    out.debug_midi_fifo_bytes = static_cast<uint32_t>(midi_transport_.buffered_bytes());
    out.debug_midi_peak_fifo_bytes = midi_stats.peak_fifo_bytes;
    out.debug_midi_last_status = midi_stats.last_status;
    out.debug_midi_backend_active = midi_synth_ && midi_synth_->active() ? 1u : 0u;
    out.debug_midi_backend_kind = 0;
    if (midi_synth_ && midi_synth_->active()) {
        const std::string_view backend(midi_synth_->backend_name());
        if (backend == "fluidsynth") out.debug_midi_backend_kind = 1;
        else if (backend == "nuked-sc55-clap") out.debug_midi_backend_kind = 2;
        else if (backend == "munt-mt32") out.debug_midi_backend_kind = 3;
        else if (backend == "munt-cm32l") out.debug_midi_backend_kind = 4;
        else if (backend == "munt-cm64") out.debug_midi_backend_kind = 5;
        else if (backend == "cm32p") out.debug_midi_backend_kind = 6;
        else if (backend == "vermouth") out.debug_midi_backend_kind = 7;
    }
    out.debug_midiout_type = midiout_type_;
    out.debug_x68k_startup_policy = static_cast<uint32_t>(startup_policy_);
    out.debug_x68k_startup_mode = legacy_startup_active_ ? 1u : 0u;
    out.debug_x68k_startup_fallbacks = startup_fallbacks_;
    out.debug_x68k_mailbox_pending = mailbox_flag_ != 0 ? 1u : 0u;
    copy_c_string(out.driver, name());
    if (driver_warning_.empty()) {
        copy_c_string(out.warning, track_warning_);
    } else if (track_warning_.empty()) {
        copy_c_string(out.warning, driver_warning_);
    } else {
        copy_c_string(out.warning, driver_warning_ + "; " + track_warning_);
    }

    if (track_index >= 0 && static_cast<size_t>(track_index) < entry.tracks.size()) {
        copy_c_string(out.title, entry.tracks[track_index].title);
    } else {
        copy_c_string(out.title, entry.title);
    }
}


void X68kGenericDriver::fill_visual_state(const HootEntry&, int, HootVisualState& out) const
{
    out.abi_version = HOOT_VISUAL_ABI_VERSION;
    out.struct_size = sizeof(out);
    visual::copy(out.architecture, "X68000");
    visual::copy(out.cpu, "MC68000");
    std::string devices = "YM2151";
    if (adpcm_.is_playing() || debug_adpcm_writes_ != 0) devices += " + MSM6258";
    if (pcm8_enabled_) devices += " + PCM8";
    if (midi_enabled_) devices += " + MIDI";
    visual::copy(out.device, devices);
    visual::copy(out.driver, name());

    for (int i = 0; i < 8; ++i) {
        const std::string d = "D" + std::to_string(i);
        visual::add_register(out, d.c_str(), m68k_get_reg(nullptr, static_cast<m68k_register_t>(M68K_REG_D0 + i)), 8);
    }
    for (int i = 0; i < 8; ++i) {
        const std::string a = "A" + std::to_string(i);
        visual::add_register(out, a.c_str(), m68k_get_reg(nullptr, static_cast<m68k_register_t>(M68K_REG_A0 + i)), 8);
    }
    visual::add_register(out, "PC", m68k_get_reg(nullptr, M68K_REG_PC), 8);
    visual::add_register(out, "SR", m68k_get_reg(nullptr, M68K_REG_SR), 4);

    for (int ch = 0; ch < 8; ++ch) {
        auto* v = visual::add_channel(out, HOOT_VISUAL_CHANNEL_FM, ch, "YM2151 FM#" + std::to_string(ch));
        if (!v) break;
        v->active = ym2151_key_on_[ch] ? 1 : 0;
        v->midi_note = visual::ym2151_kc_to_midi(ym2151_registers_[0x28 + ch]);
        if (v->midi_note >= 0) {
            if (v->midi_note < 64) v->key_mask_lo = 1ull << v->midi_note;
            else v->key_mask_hi = 1ull << (v->midi_note - 64);
        }
        const uint8_t tl0 = ym2151_registers_[0x60 + ch];
        const uint8_t tl1 = ym2151_registers_[0x68 + ch];
        const uint8_t tl2 = ym2151_registers_[0x70 + ch];
        const uint8_t tl3 = ym2151_registers_[0x78 + ch];
        const uint8_t tl = std::min(std::min(tl0, tl1), std::min(tl2, tl3));
        v->volume = visual::inverse_tl_volume(tl);
        v->pan = visual::ym2151_pan(ym2151_registers_[0x20 + ch]);
        v->instrument = ym2151_registers_[0x20 + ch] & 0x07;
        v->level = v->active ? static_cast<float>(v->volume) / 127.0f : 0.0f;
    }

    if (adpcm_.is_playing() || debug_adpcm_writes_ != 0) {
        auto* a = visual::add_channel(out, HOOT_VISUAL_CHANNEL_ADPCM, 0, "MSM6258 ADPCM#0");
        if (a) {
            a->active = adpcm_.is_playing() ? 1 : 0;
            const uint8_t pan = adpcm_.pan_and_rate() & 0x03;
            a->pan = pan == 1 ? 63 : (pan == 2 ? -64 : 0);
            a->volume = a->active ? 96 : 0;
            a->level = a->active ? 0.75f : 0.0f;
        }
    }
    if (pcm8_enabled_) {
        for (int ch = 0; ch < X68kPcm8Mixer::kVoiceCount && out.channel_count < HOOT_VISUAL_CHANNELS_MAX; ++ch) {
            const auto& voice = pcm8_.voice(ch);
            auto* p = visual::add_channel(out, HOOT_VISUAL_CHANNEL_PCM, ch, "PCM8 #" + std::to_string(ch));
            if (!p) break;
            p->active = voice.active && !voice.channel_paused ? 1 : 0;
            p->volume = std::clamp(static_cast<int>(voice.mode.volume) * 8, 0, 127);
            p->pan = voice.mode.pan == 1 ? -64 : (voice.mode.pan == 2 ? 63 : 0);
            p->instrument = static_cast<int>(voice.mode.frequency);
            p->level = p->active ? static_cast<float>(p->volume) / 127.0f : 0.0f;
        }
    }
    if (midi_enabled_) {
        for (int ch = 0; ch < 16 && out.channel_count < HOOT_VISUAL_CHANNELS_MAX; ++ch) {
            const auto& m = midi_visualizer_.channel(static_cast<size_t>(ch));
            auto* v = visual::add_channel(out, HOOT_VISUAL_CHANNEL_MIDI, ch, "MIDI CH#" + std::to_string(ch + 1));
            if (!v) break;
            v->key_mask_lo = m.keys_lo;
            v->key_mask_hi = m.keys_hi;
            v->midi_note = (m.keys_lo || m.keys_hi) ? m.last_note : -1;
            v->active = (m.keys_lo || m.keys_hi) ? 1 : 0;
            v->volume = std::clamp((static_cast<int>(m.volume) * static_cast<int>(m.expression)) / 127, 0, 127);
            v->pan = std::clamp(static_cast<int>(m.pan) - 64, -64, 63);
            v->instrument = m.program;
            v->level = v->active ? (static_cast<float>(m.velocity) / 127.0f) * (static_cast<float>(v->volume) / 127.0f) : 0.0f;
        }
    }

    out.driver_work_base = 0xff0000;
    out.driver_work_size = HOOT_VISUAL_DRIVER_WORK_MAX;
    std::copy_n(high_memory_.data(), out.driver_work_size, out.driver_work);
}

bool X68kGenericDriver::channel_mute_supported(int kind, int index) const
{
    if (kind == HOOT_VISUAL_CHANNEL_FM) return index >= 0 && index < 8;
    if (kind == HOOT_VISUAL_CHANNEL_ADPCM) return index == 0;
    if (kind == HOOT_VISUAL_CHANNEL_PCM) return pcm8_enabled_ && index >= 0 && index < X68kPcm8Mixer::kVoiceCount;
    if (kind == HOOT_VISUAL_CHANNEL_MIDI) return midi_enabled_ && index >= 0 && index < 16;
    return false;
}

bool X68kGenericDriver::set_channel_muted(int kind, int index, bool muted)
{
    if (!channel_mute_supported(kind, index)) return false;
    if (kind == HOOT_VISUAL_CHANNEL_FM) {
        if (muted) ui_opm_mute_mask_ |= (1u << index); else ui_opm_mute_mask_ &= ~(1u << index);
        ym2151_.set_mute_mask(opm_mute_mask_ | ui_opm_mute_mask_);
    } else if (kind == HOOT_VISUAL_CHANNEL_ADPCM) {
        ui_adpcm_muted_ = muted;
    } else if (kind == HOOT_VISUAL_CHANNEL_PCM) {
        if (muted) ui_pcm8_mute_mask_ |= (1u << index); else ui_pcm8_mute_mask_ &= ~(1u << index);
        pcm8_.set_mute_mask(ui_pcm8_mute_mask_);
    } else if (kind == HOOT_VISUAL_CHANNEL_MIDI) {
        if (muted) ui_midi_mute_mask_ |= static_cast<uint16_t>(1u << index);
        else ui_midi_mute_mask_ &= static_cast<uint16_t>(~(1u << index));
        if (muted && midi_synth_ && midi_synth_->active()) {
            midi_synth_->short_message(static_cast<uint8_t>(0xb0 | index), 120, 0, 3);
            midi_synth_->short_message(static_cast<uint8_t>(0xb0 | index), 123, 0, 3);
        }
    }
    return true;
}

void X68kGenericDriver::clear_channel_mutes()
{
    ui_opm_mute_mask_ = 0;
    ui_pcm8_mute_mask_ = 0;
    ui_midi_mute_mask_ = 0;
    ui_adpcm_muted_ = false;
    ym2151_.set_mute_mask(opm_mute_mask_);
    pcm8_.set_mute_mask(0);
}

const char* X68kGenericDriver::name() const
{
    return "x68k-generic";
}

void X68kGenericDriver::clear()
{
    rom_.fill(0);
    rom_image_.fill(0);
    ram_.fill(0);
    scratch_.fill(0);
    high_memory_.fill(0);
    voice_banks_.clear();
    active_voice_bank_offset_ = kVoiceBankOffset;
    track_warning_.clear();
    driver_warning_.clear();
    sample_rate_ = 44100;
    cpu_clock_hz_ = 10000000.0;
    render_cycle_remainder_ = 0.0;
    cpu_cycle_debt_ = 0;
    ym2151_clock_hz_ = 4000000;
    selected_track_ = 0;
    selected_code_ = 0;
    has_selected_track_ = false;
    reset_sp_ = 0;
    reset_pc_ = 0;
    memdump_address_ = 0;
    loaded_code_bytes_ = 0;
    debug_cpu_cycles_ = 0;
    debug_io_reads_ = 0;
    debug_io_writes_ = 0;
    debug_ym2151_writes_ = 0;
    debug_ym2151_keyons_ = 0;
    debug_ym2151_irqs_ = 0;
    debug_unhandled_exceptions_ = 0;
    ym2151_timer_a_high_ = 0;
    ym2151_timer_a_low_ = 0;
    ym2151_timer_b_ = 0;
    ym2151_timer_control_ = 0;
    ym2151_timer_a_remaining_ = 0.0;
    ym2151_timer_b_remaining_ = 0.0;
    debug_adpcm_writes_ = 0;
    debug_adpcm_starts_ = 0;
    adpcm_address_ = 0;
    adpcm_size_ = 0;
    opm_gain_ = 192.0 / 256.0;
    adpcm_gain_ = 0.40;
    pcm8_gain_ = 0.40;
    total_gain_ = 1.0;
    mix_buffer_.clear();
    pcm8_mix_buffer_.clear();
    midi_mix_buffer_.clear();
    startup_preroll_.clear();
    startup_preroll_offset_ = 0;
    mute_percussion_ = false;
    opm_mute_mask_ = 0;
    ui_opm_mute_mask_ = 0;
    ui_pcm8_mute_mask_ = 0;
    ui_midi_mute_mask_ = 0;
    ui_adpcm_muted_ = false;
    current_ym2151_reg_ = 0;
    debug_last_ym2151_reg_ = 0;
    debug_last_ym2151_data_ = 0;
    mailbox_flag_ = 0;
    mailbox_code_ = 0;
    midi_enabled_ = false;
    pcm8_enabled_ = false;
    pcm8_.reset();
    pcm8_.set_mute_mask(ui_pcm8_mute_mask_);
    dmaint_enabled_ = false;
    dma_irq_pending_ = false;
    dma_niv_ = 0x6a;
    dma_eiv_ = 0x6e;
    debug_dma_irqs_ = 0;
    mfp_enabled_ = false;
    mame_mfp_ = false;
    mfp_bootstrap_ = true;
    mfp_ignore_overrides_ = false;
    startup_policy_ = StartupPolicy::Auto;
    legacy_startup_active_ = false;
    startup_fallbacks_ = 0;
    mfp_irq_asserted_ = false;
    mfp_suspended_ = false;
    mfp_trap_bridge_ = false;
    mfp_trap_magic_ = 0;
    iocs_opm_interrupt_handler_ = 0;
    iocs_vectors_.clear();
    mfp_timer_divider_ = 1;
    mfp_sound_timer_ = -1;
    mfp_initial_ierb_ = 0x3e;
    mfp_initial_imrb_ = 0x3e;
    std::fill(std::begin(mfp_regs_), std::end(mfp_regs_), uint8_t{0});
    mfp_gpio_input_ = 0;
    std::fill(std::begin(mfp_timer_values_), std::end(mfp_timer_values_), uint16_t{0});
    std::fill(std::begin(mfp_timer_accumulators_), std::end(mfp_timer_accumulators_), 0.0);
    midi_reg_high_ = 0;
    midi_vector_ = 0;
    midi_int_enable_ = 0;
    midi_int_vect_ = 0x10;
    midi_int_flag_ = 0;
    midi_r05_ = 0;
    midi_g_timer_max_ = 0;
    midi_m_timer_max_ = 0;
    midi_g_timer_value_ = 0;
    midi_m_timer_value_ = 0;
    midi_irq_asserted_ = false;
    debug_midi_irq_count_ = 0;
    debug_midi_synth_frames_ = 0;
    debug_midi_sysex_handled_ = 0;
    midi_transport_.reset();
    midi_transport_.set_sink({});
    midi_synth_.reset();
    midi_visualizer_.reset();
    midiout_type_ = -1;
    midi_gain_ = 0.70;
    loaded_ = false;
    has_opmdrv_voice_transform_ = false;
    if (trace_.is_open()) {
        trace_.close();
    }
    trace_events_ = 0;
    recent_pcs_.fill(0);
    recent_pc_cursor_ = 0;
    default_exception_traced_ = false;
    trace_limit_ = 0;
}

void X68kGenericDriver::execute_seconds(double seconds)
{
    if (seconds <= 0.0) {
        return;
    }
    musashi_set_active_bus(this);
    const auto cycles = static_cast<int>(cpu_clock_hz_ * seconds);
    int remaining = cycles - cpu_cycle_debt_;
    cpu_cycle_debt_ = 0;
    uint64_t executed_total = 0;
    while (remaining > 0) {
        const bool timer_a_running = (ym2151_timer_control_ & 0x01) != 0;
        const bool timer_b_running = (ym2151_timer_control_ & 0x02) != 0;
        double until_overflow = static_cast<double>(remaining);
        if (timer_a_running && ym2151_timer_a_remaining_ > 0.0) {
            until_overflow = std::min(until_overflow, ym2151_timer_a_remaining_);
        }
        if (timer_b_running && ym2151_timer_b_remaining_ > 0.0) {
            until_overflow = std::min(until_overflow, ym2151_timer_b_remaining_);
        }

        constexpr int kMaxCpuQuantum = 256;
        const int run_cycles = std::min({remaining,
                                         kMaxCpuQuantum,
                                         std::max(1, static_cast<int>(std::ceil(until_overflow)))});
        const int executed = m68k_execute(run_cycles);
        debug_cpu_cycles_ += static_cast<uint64_t>(executed);
        executed_total += static_cast<uint64_t>(executed);
        remaining -= executed;
        if (timer_a_running) {
            ym2151_timer_a_remaining_ -= static_cast<double>(executed);
        }
        if (timer_b_running) {
            ym2151_timer_b_remaining_ -= static_cast<double>(executed);
        }
        update_mfp(executed);
        update_midi(executed);

        const bool timer_a_overflow = timer_a_running && ym2151_timer_a_remaining_ <= 0.0;
        const bool timer_b_overflow = timer_b_running && ym2151_timer_b_remaining_ <= 0.0;
        if (!timer_a_overflow && !timer_b_overflow) {
            continue;
        }
        const uint8_t previous_status = ym2151_timer_status_;
        if (timer_a_overflow) {
            ym2151_timer_a_remaining_ += ym2151_timer_a_cycles();
            ym2151_timer_status_ |= 0x01;
        }
        if (timer_b_overflow) {
            ym2151_timer_b_remaining_ += ym2151_timer_b_cycles();
            ym2151_timer_status_ |= 0x02;
        }

        const bool was_asserted = ym2151_irq_asserted_;
        if (ym2151_timer_status_ != previous_status) {
            update_ym2151_irq();
        }
        if (!was_asserted && ym2151_irq_asserted_) {
            ++debug_ym2151_irqs_;
        }
    }
    cpu_cycle_debt_ = std::max(0, -remaining);
    (void)executed_total;
}

void X68kGenericDriver::execute_with_audio_clock(double seconds)
{
    if (!ym2151_.uses_nuked_core()) {
        execute_seconds(seconds);
        return;
    }

    const int frames = std::max(1, static_cast<int>(std::ceil(seconds * sample_rate_)));
    int remaining_cycles = static_cast<int>(std::llround(seconds * cpu_clock_hz_));
    int16_t silent_frame[2]{};
    for (int frame = 0; frame < frames; ++frame) {
        const int frames_left = frames - frame;
        const int cycles = remaining_cycles / frames_left;
        remaining_cycles -= cycles;
        execute_seconds(static_cast<double>(cycles) / cpu_clock_hz_);
        ym2151_.render_s16(silent_frame, 1);
        if (!mute_percussion_) {
            adpcm_.mix_s16(silent_frame, 1, adpcm_gain_);
        }
    }
}

void X68kGenericDriver::reset_ym2151_timers()
{
    ym2151_timer_a_high_ = 0;
    ym2151_timer_a_low_ = 0;
    ym2151_timer_b_ = 0;
    ym2151_timer_control_ = 0;
    ym2151_timer_status_ = 0;
    ym2151_irq_asserted_ = false;
    ym2151_timer_a_remaining_ = 0.0;
    ym2151_timer_b_remaining_ = 0.0;
}

void X68kGenericDriver::initialize_mfp()
{
    const auto trace_initial_state = [this]() {
        for (size_t reg = 0; reg <= 18; ++reg) {
            trace_io("mfp-init-reg",
                     0xe88001u + static_cast<uint32_t>(reg * 2),
                     mfp_regs_[reg]);
        }
    };

    if (!mfp_enabled_) {
        std::fill(std::begin(mfp_regs_), std::end(mfp_regs_), uint8_t{0});
        std::fill(std::begin(mfp_timer_values_), std::end(mfp_timer_values_), uint16_t{0});
        std::fill(std::begin(mfp_timer_accumulators_), std::end(mfp_timer_accumulators_), 0.0);
        mfp_irq_asserted_ = false;
        mfp_gpio_input_ = 0;
        trace_initial_state();
        return;
    }

    if (mame_mfp_ && !mfp_bootstrap_) {
        // Behavior adapted from MAME's BSD-3-Clause mc68901_device. MAME
        // resets the device registers to zero; the Hoot bootstrap defaults
        // below are bypassed for a strict power-on comparison.
        std::fill(std::begin(mfp_regs_), std::end(mfp_regs_), uint8_t{0});
        // GPIP inputs are inactive-high after reset.
        mfp_regs_[0] = 0xff;
        std::fill(std::begin(mfp_timer_values_), std::end(mfp_timer_values_), uint16_t{0});
        std::fill(std::begin(mfp_timer_accumulators_), std::end(mfp_timer_accumulators_), 0.0);
        mfp_irq_asserted_ = false;
        mfp_gpio_input_ = 0xff;
        trace_initial_state();
        return;
    }

    // Power-on values used by the X68000 MFP model. The sound drivers that
    // request MFP support rely on the resident timer setup being present
    // before they receive their first mailbox command.
    uint8_t defaults[24] = {
        0x7b, 0x06, 0x00, 0x18, mfp_initial_ierb_, 0x00, 0x00, 0x00,
        0x00, 0x18, mfp_initial_imrb_, 0x40, 0x08, 0x01, 0x77, 0x01,
        0x0d, 0xc8, 0x14, 0x00, 0x88, 0x01, 0x81, 0x00,
    };
    std::copy(std::begin(defaults), std::end(defaults), std::begin(mfp_regs_));
    for (int timer = 0; timer < 4; ++timer) {
        const uint8_t reload = mfp_regs_[15 + timer];
        mfp_timer_values_[timer] = reload != 0 ? reload : 0x100;
    }
    std::fill(std::begin(mfp_timer_accumulators_), std::end(mfp_timer_accumulators_), 0.0);
    mfp_irq_asserted_ = false;
    mfp_gpio_input_ = mame_mfp_ ? 0xff : 0;
    trace_initial_state();
}

void X68kGenericDriver::update_mfp(int executed_cycles)
{
    if (!mfp_enabled_ || mfp_suspended_ || executed_cycles <= 0) {
        return;
    }

    // The legacy path follows PX68K's post-boot model. The selectable MAME
    // path follows mc68901's native prescaler table and 4 MHz device clock.
    constexpr int hoot_prescalers[8] = {1, 10, 25, 40, 125, 160, 250, 500};
    constexpr int mame_prescalers[8] = {0, 4, 10, 16, 50, 64, 100, 200};
    constexpr uint8_t timer_sources[4] = {0x20, 0x01, 0x20, 0x10};
    constexpr uint8_t pending_registers[4] = {5, 5, 6, 6};
    // Timer A is in IERA; Timer B/C/D are in IERB.
    constexpr uint8_t enable_registers[4] = {3, 4, 4, 4};
    for (int timer = 0; timer < 4; ++timer) {
        uint8_t mode = 0;
        if (timer == 0) {
            mode = mfp_regs_[12] & 0x0f;
        } else if (timer == 1) {
            mode = mfp_regs_[13] & 0x0f;
        } else if (timer == 2) {
            mode = (mfp_regs_[14] >> 4) & 0x0f;
        } else {
            mode = mfp_regs_[14] & 0x0f;
        }
        if (mode == 0 || mode >= 8) {
            continue;
        }

        const int prescaler = mame_mfp_ ? mame_prescalers[mode] : hoot_prescalers[mode];
        if (prescaler <= 0) {
            continue;
        }
        const double mfp_ticks = mame_mfp_
            ? static_cast<double>(executed_cycles) * 0.4
            : static_cast<double>(executed_cycles) / static_cast<double>(mfp_timer_divider_);
        mfp_timer_accumulators_[timer] += mfp_ticks / static_cast<double>(prescaler);
        while (mfp_timer_accumulators_[timer] >= 1.0) {
            mfp_timer_accumulators_[timer] -= 1.0;
            const bool expired = mame_mfp_
                ? mfp_timer_values_[timer] <= 1
                : mfp_timer_values_[timer] == 1;
            if (expired) {
                const uint8_t reload = mfp_regs_[15 + timer];
                mfp_timer_values_[timer] = reload != 0 ? reload : 0x100;
                if (!mame_mfp_ || (mfp_regs_[enable_registers[timer]] & timer_sources[timer])) {
                    mfp_regs_[pending_registers[timer]] |= timer_sources[timer];
                    trace_io("mfp-expire", static_cast<uint32_t>(timer),
                             mfp_timer_values_[timer]);
                    update_mfp_irq();
                }
            } else if (mfp_timer_values_[timer] != 0) {
                --mfp_timer_values_[timer];
            }
        }
    }
}

void X68kGenericDriver::update_cpu_irq()
{
    int level = 0;
    if (mfp_irq_asserted_ || (!mfp_enabled_ && ym2151_irq_asserted_)) {
        level = 6;
    } else if (midi_irq_asserted_) {
        level = 4;
    } else if (dma_irq_pending_) {
        level = 3;
    }
    m68k_set_irq(level);
}

void X68kGenericDriver::update_mfp_irq()
{
    if (!mfp_enabled_ || mfp_suspended_) {
        return;
    }
    const bool pending = mame_mfp_
        ? ((mfp_regs_[5] & mfp_regs_[9] & static_cast<uint8_t>(~mfp_regs_[7])) != 0)
            || ((mfp_regs_[6] & mfp_regs_[10] & static_cast<uint8_t>(~mfp_regs_[8])) != 0)
        : ((mfp_regs_[5] & mfp_regs_[3] & mfp_regs_[9]
            & static_cast<uint8_t>(~mfp_regs_[7])) != 0)
            || ((mfp_regs_[6] & mfp_regs_[4] & mfp_regs_[10]
                & static_cast<uint8_t>(~mfp_regs_[8])) != 0);
    if (pending != mfp_irq_asserted_) {
        mfp_irq_asserted_ = pending;
        trace_io(pending ? "irq6-assert-mfp" : "irq6-clear-mfp",
                 (static_cast<uint32_t>(mfp_regs_[5]) << 8) | mfp_regs_[6],
                 pending ? 6 : 0);
        update_cpu_irq();
    }
}

uint8_t X68kGenericDriver::read_mfp(uint32_t address)
{
    if ((address & 1) == 0) {
        return 0xff;
    }
    const size_t reg = static_cast<size_t>((address & 0x3f) >> 1);
    uint8_t data = reg < std::size(mfp_regs_) ? mfp_regs_[reg] : 0;
    if (mame_mfp_ && reg == 0) {
        data = static_cast<uint8_t>((mfp_gpio_input_ & ~mfp_regs_[2])
                                    | (mfp_regs_[0] & mfp_regs_[2]));
    }
    trace_io("mfp-read", address, data);
    return data;
}

void X68kGenericDriver::write_mfp(uint32_t address, uint8_t data)
{
    if ((address & 1) == 0) {
        return;
    }
    const size_t reg = static_cast<size_t>((address & 0x3f) >> 1);
    if (reg >= std::size(mfp_regs_)) {
        return;
    }
    trace_io("mfp-write", address, data);
    switch (reg) {
    case 3:
    case 4:
        mfp_regs_[reg] = data;
        if (!mame_mfp_) {
            mfp_regs_[reg + 2] &= data;
        }
        break;
    case 5:
    case 6:
    case 7:
    case 8:
        if (mame_mfp_) {
            // Hoot's MC68901 implementation stores IPR/ISR writes directly.
            mfp_regs_[reg] = data;
        } else {
            mfp_regs_[reg] &= data;
        }
        break;
    case 9:
    case 10:
        mfp_regs_[reg] = data;
        break;
    case 15:
    case 16:
    case 17:
    case 18:
        mfp_regs_[reg] = data;
        mfp_timer_values_[reg - 15] = data != 0 ? data : 0x100;
        break;
    default:
        if (mame_mfp_ && reg == 11) {
            // Hoot stores VR verbatim; bit 3 selects automatic EOI.
            mfp_regs_[reg] = data;
        } else if (mame_mfp_ && (reg == 12 || reg == 13)) {
            mfp_regs_[reg] = data & 0x0f;
        } else if (mame_mfp_ && reg == 14) {
            // Both nibbles retain bit 3 for event-count mode.
            mfp_regs_[reg] = data;
        } else {
            mfp_regs_[reg] = data;
        }
        break;
    }
    update_mfp_irq();
}

void X68kGenericDriver::update_ym2151_irq()
{
    const bool active = ((ym2151_timer_status_ & 0x01) != 0
                         && (ym2151_timer_control_ & 0x04) != 0)
        || ((ym2151_timer_status_ & 0x02) != 0
            && (ym2151_timer_control_ & 0x08) != 0);
    if (active == ym2151_irq_asserted_) {
        return;
    }
    ym2151_irq_asserted_ = active;
    if (mfp_enabled_) {
        if (mame_mfp_) {
            // MAME wires the YM2151 IRQ to MFP GPIO3 as an active-low input.
            write_mfp_gpio3(active ? 0 : 1);
        } else {
            // The legacy bootstrap path represents the source directly as a
            // pending MFP bit. With the standard vector base this is 0x43.
            if (active) {
                mfp_regs_[6] |= 0x08;
            }
            update_mfp_irq();
        }
    } else {
        trace_io(active ? "irq6-assert-ym" : "irq6-clear-ym",
                 0xe90003, ym2151_timer_status_);
        update_cpu_irq();
    }
}

void X68kGenericDriver::write_mfp_gpio3(int state)
{
    if (!mame_mfp_) {
        return;
    }
    state = state ? 1 : 0;
    const int previous = (mfp_gpio_input_ >> 3) & 1;
    if (state == previous) {
        return;
    }
    const int active_edge = (mfp_regs_[1] >> 3) & 1;
    if (state == active_edge && (mfp_regs_[4] & 0x08) != 0) {
        mfp_regs_[6] |= 0x08;
        trace_io("mfp-gpio3-edge", 3, static_cast<uint8_t>(state));
        update_mfp_irq();
    }
    if (state != 0) {
        mfp_gpio_input_ |= 0x08;
    } else {
        mfp_gpio_input_ &= static_cast<uint8_t>(~0x08u);
    }
}

int X68kGenericDriver::acknowledge_interrupt(int level)
{
    if (level == 3 && dmaint_enabled_ && dma_irq_pending_) {
        dma_irq_pending_ = false;
        ++debug_dma_irqs_;
        trace_io("irq3-ack-dma", static_cast<uint32_t>(dma_niv_), 0);
        update_cpu_irq();
        return dma_niv_;
    }
    if (level == 4 && midi_enabled_ && midi_irq_asserted_) {
        const int delivered_vector = static_cast<int>(midi_vector_ | midi_int_vect_);
        midi_irq_asserted_ = false;
        trace_io("irq4-ack-midi", static_cast<uint32_t>(delivered_vector), midi_int_flag_);
        update_cpu_irq();
        return delivered_vector;
    }
    if (level == 6) {
        trace_io("irq6-ack-request", 6, static_cast<uint8_t>(mfp_irq_asserted_ ? 1 : 0));
        if (mfp_enabled_ && mfp_irq_asserted_) {
            // The MFP callback acknowledges the highest-priority request and
            // clears its pending bit before returning the source vector.
            // Leaving the bit set lets an unrelated timer re-trigger the same
            // sound interrupt as soon as the CPU line is lowered.
            uint8_t flag = 0;
            int vector = -1;
            uint8_t* pending = nullptr;
            uint8_t* in_service = nullptr;
            const uint8_t vector_base = mfp_regs_[11] & 0xf0;

            for (uint8_t candidate = 0x80; candidate != 0; candidate >>= 1) {
                if ((mfp_regs_[5] & candidate) != 0
                    && (mame_mfp_ || (mfp_regs_[3] & candidate) != 0)
                    && (mfp_regs_[9] & candidate) != 0
                    && (mfp_regs_[7] & candidate) == 0) {
                    flag = candidate;
                    vector = 15;
                    for (uint8_t bit = 0x80; bit != candidate; bit >>= 1) {
                        --vector;
                    }
                    pending = &mfp_regs_[5];
                    in_service = &mfp_regs_[7];
                    break;
                }
            }
            if (flag == 0) {
                for (uint8_t candidate = 0x80; candidate != 0; candidate >>= 1) {
                    if ((mfp_regs_[6] & candidate) != 0
                        && (mame_mfp_ || (mfp_regs_[4] & candidate) != 0)
                        && (mfp_regs_[10] & candidate) != 0
                        && (mfp_regs_[8] & candidate) == 0) {
                        flag = candidate;
                        vector = 7;
                        for (uint8_t bit = 0x80; bit != candidate; bit >>= 1) {
                            --vector;
                        }
                        pending = &mfp_regs_[6];
                        in_service = &mfp_regs_[8];
                        break;
                    }
                }
            }

            if (flag != 0 && pending != nullptr && in_service != nullptr) {
                *pending &= static_cast<uint8_t>(~flag);
                if ((mfp_regs_[11] & 0x08) != 0) {
                    *in_service |= flag;
                }
                mfp_irq_asserted_ = false;
                uint32_t delivered_vector = static_cast<uint32_t>(vector_base | vector);
                if ((!mame_mfp_ || mfp_bootstrap_)
                           && mfp_sound_timer_ == 2
                           && pending == &mfp_regs_[6]
                           && flag == 0x20) {
                    delivered_vector = 0x43u;
                }
                const uint32_t handler = read_memory_32(delivered_vector * 4u) & 0x00ffffffu;
                const bool unhandled_sink = handler == 0
                    || (read_memory_8(handler) == 0x4e
                        && read_memory_8(handler + 1u) == 0x71
                        && read_memory_8(handler + 2u) == 0x60
                        && read_memory_8(handler + 3u) == 0xfc);
                if (unhandled_sink) {
                    // The Hoot bootstrap fills unused vectors with a NOP /
                    // BRA.S -4 sink. Real X68000 firmware would normally keep
                    // unrelated MFP sources masked until a handler is
                    // installed. Delivering a synthetic timer to that sink
                    // permanently strands the headless guest (A-Train II
                    // exposes this with the default Timer-D source). Disable
                    // only the unhandled source and let installed MFP/OPM
                    // vectors continue normally.
                    if (pending == &mfp_regs_[5]) {
                        mfp_regs_[3] &= static_cast<uint8_t>(~flag);
                        mfp_regs_[9] &= static_cast<uint8_t>(~flag);
                    } else {
                        mfp_regs_[4] &= static_cast<uint8_t>(~flag);
                        mfp_regs_[10] &= static_cast<uint8_t>(~flag);
                    }
                    // Musashi has already accepted the CPU interrupt at
                    // this point. Returning the spurious vector would enter
                    // another bootstrap sink. Point this source at a tiny
                    // host-provided RTE trampoline in the original Hoot-style
                    // writable dummy page for the current delivery, then leave
                    // the source masked for subsequent ticks.
                    constexpr uint32_t kUnhandledMfpRte = 0x00ffff00u;
                    write_memory_8(kUnhandledMfpRte + 0u, 0x4e);
                    write_memory_8(kUnhandledMfpRte + 1u, 0x73);
                    write_memory_32(delivered_vector * 4u, kUnhandledMfpRte);
                    trace_io("mfp-drop-unhandled", delivered_vector, flag);
                    update_mfp_irq();
                    // acknowledge_interrupt() cleared the host-side latch before
                    // update_mfp_irq() recomputed pending state. If no source
                    // remains, update_mfp_irq() sees no edge and therefore has
                    // nothing to trace; explicitly refresh the CPU line so
                    // Musashi does not immediately acknowledge a phantom IRQ6.
                    update_cpu_irq();
                    return delivered_vector;
                }

                trace_io("mfp-ack", delivered_vector, flag);
                update_mfp_irq();
                update_cpu_irq();
                return delivered_vector;
            }
        }

        // X68000 IRQH clears its CPU-side request latch on acknowledge. The
        // YM2151 status bit remains set until register 0x14 resets it.
        ym2151_irq_asserted_ = false;
        mfp_irq_asserted_ = false;
        const int delivered_vector = (!mfp_enabled_ && legacy_startup_active_
                                      && iocs_opm_interrupt_handler_ != 0)
            ? 0x43
            : M68K_INT_ACK_AUTOVECTOR;
        // The original Hoot M68000 host delivered its synthesized YM2151
        // timer IRQ6 through the X68000 OPM vector ($43), not the 68000
        // level-6 autovector. Resident ZMSC/OPMDRV images install their RTE
        // callback there via IOCS _OPMINTST.
        trace_io(delivered_vector == 0x43 ? "irq6-ack-opm-vector" : "irq6-ack",
                 static_cast<uint32_t>(delivered_vector), 0);
        update_cpu_irq();
        return delivered_vector;
    }
    return M68K_INT_ACK_AUTOVECTOR;
}

double X68kGenericDriver::ym2151_timer_a_cycles() const
{
    const uint16_t value = static_cast<uint16_t>((ym2151_timer_a_high_ << 2)
                                                  | (ym2151_timer_a_low_ & 0x03));
    return std::max(1.0, static_cast<double>(1024 - value) * 64.0
                             * cpu_clock_hz_ / static_cast<double>(ym2151_clock_hz_));
}

double X68kGenericDriver::ym2151_timer_b_cycles() const
{
    return std::max(1.0, static_cast<double>(256 - ym2151_timer_b_) * 1024.0
                             * cpu_clock_hz_ / static_cast<double>(ym2151_clock_hz_));
}

void X68kGenericDriver::update_ym2151_timer(uint8_t reg, uint8_t data)
{
    switch (reg) {
    case 0x10:
        ym2151_timer_a_high_ = data;
        ym2151_timer_a_remaining_ = ym2151_timer_a_cycles();
        break;
    case 0x11:
        ym2151_timer_a_low_ = data & 0x03;
        ym2151_timer_a_remaining_ = ym2151_timer_a_cycles();
        break;
    case 0x12:
        ym2151_timer_b_ = data;
        ym2151_timer_b_remaining_ = ym2151_timer_b_cycles();
        break;
    case 0x14: {
        const uint8_t previous = ym2151_timer_control_;
        if ((data & 0x10) != 0) {
            ym2151_timer_status_ &= ~uint8_t{0x01};
        }
        if ((data & 0x20) != 0) {
            ym2151_timer_status_ &= ~uint8_t{0x02};
        }
        ym2151_timer_control_ = data;
        if ((data & 0x01) != 0 && (previous & 0x01) == 0) {
            ym2151_timer_a_remaining_ = ym2151_timer_a_cycles();
        }
        if ((data & 0x02) != 0 && (previous & 0x02) == 0) {
            ym2151_timer_b_remaining_ = ym2151_timer_b_cycles();
        }
        update_ym2151_irq();
        break;
    }
    default:
        break;
    }
}

void X68kGenericDriver::open_trace_from_environment()
{
    const char* trace_path = std::getenv("HOOT_X68K_TRACE");
    if (trace_path == nullptr || trace_path[0] == '\0') {
        return;
    }

    trace_.open(trace_path, std::ios::out | std::ios::trunc);
    if (const char* limit = std::getenv("HOOT_X68K_TRACE_LIMIT")) {
        trace_limit_ = std::strtoull(limit, nullptr, 10);
    }
    if (trace_.is_open()) {
        trace_ << "# hoot x68k trace\n";
        trace_ << "# columns: event cycles pc details\n";
    }
}


void X68kGenericDriver::instruction_hook(uint32_t pc)
{
    pc &= 0x00ffffffu;
    recent_pcs_[recent_pc_cursor_++ % recent_pcs_.size()] = pc;
    if (!trace_.is_open() || default_exception_traced_ || pc != 0x000850u) {
        return;
    }
    default_exception_traced_ = true;
    trace_ << "default-exception cycles=" << debug_cpu_cycles_
           << " pc=0x" << std::hex << std::setw(6) << std::setfill('0') << pc
           << " a7=0x" << std::setw(8) << m68k_get_reg(nullptr, M68K_REG_A7)
           << " sr=0x" << std::setw(4) << m68k_get_reg(nullptr, M68K_REG_SR)
           << " stack=";
    const uint32_t sp = m68k_get_reg(nullptr, M68K_REG_A7) & 0x00ffffffu;
    for (uint32_t offset = 0; offset < 24; ++offset) {
        trace_ << std::setw(2) << static_cast<unsigned>(read_memory_8(sp + offset));
    }
    trace_ << " recent=";
    const size_t cursor = recent_pc_cursor_;
    for (size_t index = 0; index < recent_pcs_.size(); ++index) {
        trace_ << std::setw(6)
               << recent_pcs_[(cursor + index) % recent_pcs_.size()];
        if (index + 1 != recent_pcs_.size()) {
            trace_ << ',';
        }
    }
    trace_ << std::dec << std::setfill(' ') << "\n";
}

void X68kGenericDriver::trace_ym2151(uint8_t reg, uint8_t data)
{
    if (!trace_.is_open()) {
        return;
    }
    if (trace_limit_ != 0 && trace_events_ >= trace_limit_) {
        return;
    }
    ++trace_events_;
    trace_ << "ym2151"
           << " cycles=" << debug_cpu_cycles_
           << " pc=0x" << std::hex << std::setw(6) << std::setfill('0') << m68k_get_reg(nullptr, M68K_REG_PC)
           << " a0=0x" << std::setw(6) << m68k_get_reg(nullptr, M68K_REG_A0)
           << " a1=0x" << std::setw(6) << m68k_get_reg(nullptr, M68K_REG_A1)
           << " a2=0x" << std::setw(6) << m68k_get_reg(nullptr, M68K_REG_A2)
           << " reg=0x" << std::setw(2) << static_cast<unsigned>(reg)
           << " data=0x" << std::setw(2) << static_cast<unsigned>(data)
           << std::dec << std::setfill(' ') << "\n";
}

void X68kGenericDriver::trace_io(const char* operation, uint32_t address, uint8_t data)
{
    if (!trace_.is_open()) {
        return;
    }
    if (trace_limit_ != 0 && trace_events_ >= trace_limit_) {
        return;
    }
    ++trace_events_;
    trace_ << operation
           << " cycles=" << debug_cpu_cycles_
           << " pc=0x" << std::hex << std::setw(6) << std::setfill('0') << m68k_get_reg(nullptr, M68K_REG_PC)
           << " d0=0x" << std::setw(8) << m68k_get_reg(nullptr, M68K_REG_D0)
           << " d1=0x" << std::setw(8) << m68k_get_reg(nullptr, M68K_REG_D1)
           << " d2=0x" << std::setw(8) << m68k_get_reg(nullptr, M68K_REG_D2)
           << " a0=0x" << std::setw(8) << m68k_get_reg(nullptr, M68K_REG_A0)
           << " a1=0x" << std::setw(8) << m68k_get_reg(nullptr, M68K_REG_A1)
           << " a2=0x" << std::setw(8) << m68k_get_reg(nullptr, M68K_REG_A2)
           << " a5=0x" << std::setw(8) << m68k_get_reg(nullptr, M68K_REG_A5)
           << " a6=0x" << std::setw(8) << m68k_get_reg(nullptr, M68K_REG_A6)
           << " a7=0x" << std::setw(8) << m68k_get_reg(nullptr, M68K_REG_A7)
           << " sr=0x" << std::setw(4) << m68k_get_reg(nullptr, M68K_REG_SR)
           << " addr=0x" << std::setw(6) << address
           << " data=0x" << std::setw(2) << static_cast<unsigned>(data)
           << std::dec << std::setfill(' ') << "\n";
}

void X68kGenericDriver::trace_pcm8(const X68kPcm8Mixer::CommandResult& result,
                                   uint32_t d0,
                                   uint32_t d1,
                                   uint32_t d2,
                                   uint32_t a1)
{
    if (!trace_.is_open()) {
        return;
    }
    if (trace_limit_ != 0 && trace_events_ >= trace_limit_) {
        return;
    }
    ++trace_events_;
    trace_ << "pcm8"
           << " cycles=" << debug_cpu_cycles_
           << " pc=0x" << std::hex << std::setw(6) << std::setfill('0')
           << m68k_get_reg(nullptr, M68K_REG_PC)
           << " " << X68kPcm8Mixer::describe(result, d0, d1, d2, a1)
           << std::dec << std::setfill(' ') << "\n";
}

uint8_t X68kGenericDriver::read_midi(uint32_t address)
{
    if (!midi_enabled_) {
        const uint8_t data = (address & 0x0f) == 0x01 ? 0x01 : 0x00;
        trace_io("midi-disabled-read", address, data);
        return data;
    }

    uint8_t data = 0;
    switch (address & 0x0f) {
    case 0x01:
        data = static_cast<uint8_t>(midi_vector_ | midi_int_vect_);
        midi_int_vect_ = 0x10;
        break;
    case 0x09:
        if (midi_reg_high_ == 5) {
            data = midi_transport_.tx_full() ? 0x01 : 0xc0;
        }
        break;
    default:
        break;
    }
    trace_io("midi-read", address, data);
    return data;
}

void X68kGenericDriver::reset_midi_synth_mode()
{
    if (!midi_synth_ || !midi_synth_->active()) {
        return;
    }
    midi_synth_->reset();
    // Hoot's midiout_type 4 is GS/SC-55, 7 is later GS/SC-88 and 8 is GM.
    // FluidSynth implements GM mode-on and GS reset, and can therefore provide
    // a useful software-rendered fallback for those catalog classes. It is not
    // presented as an exact SC-55/SC-88 hardware emulation.
    if (midiout_type_ == 4 || midiout_type_ == 7) {
        midi_synth_->sysex({0x41, 0x10, 0x42, 0x12, 0x40, 0x00, 0x7f, 0x00, 0x41});
    } else if (midiout_type_ == 5 || midiout_type_ == 6 || midiout_type_ == 8) {
        // M1/Vermouth compatibility rendering uses FluidSynth's GM bank.
        midi_synth_->sysex({0x7e, 0x7f, 0x09, 0x01});
    }
}

void X68kGenericDriver::handle_midi_message(const X68kMidiMessage& message)
{
    midi_visualizer_.message(message);
    if (trace_.is_open() && (trace_limit_ == 0 || trace_events_ < trace_limit_)) {
        ++trace_events_;
        trace_ << "midi-message cycles=" << debug_cpu_cycles_
               << " pc=0x" << std::hex << std::setw(6) << std::setfill('0')
               << m68k_get_reg(nullptr, M68K_REG_PC)
               << " status=0x" << std::setw(2) << static_cast<unsigned>(message.status)
               << std::dec << std::setfill(' ');
        if (message.kind == X68kMidiMessage::Kind::SysEx) {
            trace_ << " kind=sysex bytes=" << message.sysex.size() << " data=";
            for (const uint8_t byte : message.sysex) {
                trace_ << std::hex << std::setw(2) << std::setfill('0')
                       << static_cast<unsigned>(byte);
            }
            trace_ << std::dec << std::setfill(' ');
        } else {
            trace_ << " kind=short size=" << static_cast<unsigned>(message.size)
                   << " d1=" << static_cast<unsigned>(message.data1)
                   << " d2=" << static_cast<unsigned>(message.data2);
        }
        trace_ << "\n";
    }

    if (!midi_synth_ || !midi_synth_->active()) {
        return;
    }
    if (message.kind == X68kMidiMessage::Kind::SysEx) {
        midi_synth_->sysex(message.sysex);
        ++debug_midi_sysex_handled_;
    } else {
        const uint8_t op = static_cast<uint8_t>(message.status & 0xf0);
        const int channel = message.status < 0xf0 ? static_cast<int>(message.status & 0x0f) : -1;
        const bool muted_note_on = channel >= 0 && (ui_midi_mute_mask_ & (1u << channel)) != 0
            && op == 0x90 && message.data2 != 0;
        if (!muted_note_on)
            midi_synth_->short_message(message.status, message.data1, message.data2, message.size);
    }
}

void X68kGenericDriver::raise_midi_irq(uint8_t vector, uint8_t flag)
{
    midi_int_flag_ |= flag;
    midi_int_vect_ = vector;
    if (!midi_irq_asserted_) {
        midi_irq_asserted_ = true;
        ++debug_midi_irq_count_;
        trace_io("irq4-assert-midi", static_cast<uint32_t>(midi_vector_ | vector), flag);
        update_cpu_irq();
    }
}

void X68kGenericDriver::update_midi(int executed_cycles)
{
    if (!midi_enabled_ || executed_cycles <= 0) {
        return;
    }

    const bool fifo_was_full = midi_transport_.tx_full();
    midi_transport_.advance_cycles(static_cast<uint32_t>(executed_cycles));
    if (fifo_was_full && midi_transport_.tx_ready() && (midi_int_enable_ & 0x40) != 0) {
        raise_midi_irq(0x0c, 0x40);
    }

    if (midi_m_timer_max_ != 0) {
        midi_m_timer_value_ -= executed_cycles;
        const int64_t period = static_cast<int64_t>(midi_m_timer_max_) * 80;
        while (midi_m_timer_value_ < 0 && period > 0) {
            midi_m_timer_value_ += period;
            if ((midi_r05_ & 0x80) == 0 && (midi_int_enable_ & 0x02) != 0) {
                raise_midi_irq(0x02, 0x02);
            }
        }
    }
    if (midi_g_timer_max_ != 0) {
        midi_g_timer_value_ -= executed_cycles;
        const int64_t period = static_cast<int64_t>(midi_g_timer_max_) * 80;
        while (midi_g_timer_value_ < 0 && period > 0) {
            midi_g_timer_value_ += period;
            if ((midi_int_enable_ & 0x80) != 0) {
                raise_midi_irq(0x0e, 0x80);
            }
        }
    }
}

void X68kGenericDriver::write_midi(uint32_t address, uint8_t data)
{
    if (!midi_enabled_) {
        trace_io("midi-disabled-write", address, data);
        return;
    }

    switch (address & 0x0f) {
    case 0x03:
        midi_reg_high_ = data & 0x0f;
        if ((data & 0x80) != 0) {
            midi_vector_ = 0;
            midi_int_enable_ = 0;
            midi_int_vect_ = 0x10;
            midi_int_flag_ = 0;
            midi_r05_ = 0;
            midi_g_timer_max_ = 0;
            midi_m_timer_max_ = 0;
            midi_g_timer_value_ = 0;
            midi_m_timer_value_ = 0;
            midi_irq_asserted_ = false;
            midi_transport_.reset();
            midi_transport_.set_sink([this](const X68kMidiMessage& message) {
                handle_midi_message(message);
            });
            reset_midi_synth_mode();
            update_cpu_irq();
        }
        break;
    case 0x09:
        if (midi_reg_high_ == 0) {
            midi_vector_ = data & 0xe0;
        } else if (midi_reg_high_ == 8) {
            midi_g_timer_max_ = (midi_g_timer_max_ & 0xff00u) | data;
        }
        break;
    case 0x0b:
        if (midi_reg_high_ == 0) {
            midi_r05_ = data;
        } else if (midi_reg_high_ == 8) {
            midi_g_timer_max_ = (midi_g_timer_max_ & 0x00ffu)
                | (static_cast<uint32_t>(data & 0x3f) << 8);
            if ((data & 0x80) != 0) {
                midi_g_timer_value_ = static_cast<int64_t>(midi_g_timer_max_) * 80;
            }
        }
        break;
    case 0x0d:
        if (midi_reg_high_ == 0) {
            midi_int_enable_ = data;
        } else if (midi_reg_high_ == 5) {
            midi_transport_.enqueue(data);
        } else if (midi_reg_high_ == 8) {
            midi_m_timer_max_ = (midi_m_timer_max_ & 0xff00u) | data;
        }
        break;
    case 0x0f:
        if (midi_reg_high_ == 8) {
            midi_m_timer_max_ = (midi_m_timer_max_ & 0x00ffu)
                | (static_cast<uint32_t>(data & 0x3f) << 8);
            if ((data & 0x80) != 0) {
                midi_m_timer_value_ = static_cast<int64_t>(midi_m_timer_max_) * 80;
            }
        }
        break;
    default:
        break;
    }
    trace_io("midi-write", address, data);
}

uint8_t X68kGenericDriver::iocs_adpcm_mode_to_ppi(uint32_t mode) const
{
    // IOCS _ADPCMOUT uses the low byte for its logical sample-rate selector
    // and the next byte for routing. The packed drivers seen in the Hoot
    // catalogue use 0x0403 for centred 15.6 kHz playback. Convert that common
    // form to the X68000 PPI layout used by the MSM6258 implementation.
    const uint8_t rate = static_cast<uint8_t>(mode & 0xff);
    const uint8_t route = static_cast<uint8_t>((mode >> 8) & 0xff);

    uint8_t ppi_rate = 0x08; // 15.6 kHz
    if (rate <= 1) {
        ppi_rate = 0x00; // Closest hardware rate: 7.8 kHz.
    } else if (rate == 2) {
        ppi_rate = 0x04; // 10.4 kHz.
    }

    // Hardware pan bits are active-low. IOCS route 1/2 select a side; route 4
    // is the normal centred mode. Unknown values remain centred rather than
    // muting a channel accidentally.
    uint8_t ppi_pan = 0x00;
    if (route == 1) {
        ppi_pan = 0x01;
    } else if (route == 2) {
        ppi_pan = 0x02;
    }
    return static_cast<uint8_t>(ppi_rate | ppi_pan);
}

void X68kGenericDriver::write_memory_32(uint32_t address, uint32_t value)
{
    write_memory_8(address, static_cast<uint8_t>(value >> 24));
    write_memory_8(address + 1, static_cast<uint8_t>(value >> 16));
    write_memory_8(address + 2, static_cast<uint8_t>(value >> 8));
    write_memory_8(address + 3, static_cast<uint8_t>(value));
}

void X68kGenericDriver::handle_iocs_call()
{
    const uint8_t call = static_cast<uint8_t>(m68k_get_reg(nullptr, M68K_REG_D0));
    const uint32_t d1 = m68k_get_reg(nullptr, M68K_REG_D1);
    const uint32_t d2 = m68k_get_reg(nullptr, M68K_REG_D2);
    const uint32_t a1 = m68k_get_reg(nullptr, M68K_REG_A1) & 0x00ffffffu;

    switch (call) {
    case 0x60: { // _ADPCMOUT
        const size_t address = static_cast<size_t>(a1);
        const size_t requested = static_cast<size_t>(d2);
        adpcm_.set_pan_and_rate(iocs_adpcm_mode_to_ppi(d1));
        ++debug_adpcm_writes_;
        if (address < rom_.size() && requested != 0) {
            const size_t count = std::min(requested, rom_.size() - address);
            if (adpcm_.play_memory(rom_.data() + address, count)) {
                ++debug_adpcm_starts_;
                m68k_set_reg(M68K_REG_D0, 0);
                return;
            }
        }
        adpcm_.stop();
        m68k_set_reg(M68K_REG_D0, 0xffffffffu);
        return;
    }
    case 0x61: // _ADPCMSTOP
        ++debug_adpcm_writes_;
        adpcm_.stop();
        m68k_set_reg(M68K_REG_D0, 0);
        return;
    case 0x62: // _ADPCMSNS
        m68k_set_reg(M68K_REG_D0, adpcm_.is_playing() ? 1u : 0u);
        return;
    case 0x68: { // _OPMSET
        const uint8_t reg = static_cast<uint8_t>(d1);
        const uint8_t value = static_cast<uint8_t>(d2);
        write_memory_8(0xe90001, reg);
        write_memory_8(0xe90003, value);
        m68k_set_reg(M68K_REG_D0, 0);
        return;
    }
    case 0x69: // _OPMSNS
        m68k_set_reg(M68K_REG_D0, read_memory_8(0xe90003));
        return;
    case 0x6a: // _OPMINTST
        iocs_opm_interrupt_handler_ = a1;
        // The IOCS callback is the actual OPM interrupt routine and returns
        // with RTE. Install it directly as X68000 vector $43; invoking it via
        // JSR or through the trap-$f0 command dispatcher corrupts the frame
        // and/or leaves the CPU at IPL6.
        if (a1 != 0) {
            write_memory_32(0x43u * 4u, a1);
        }
        // Human68k returns zero when the interrupt callback was accepted.
        // OPMDRV explicitly treats any non-zero result as initialization
        // failure, so leaving the call number in D0 disables the driver.
        m68k_set_reg(M68K_REG_D0, 0);
        return;
    case 0x80: { // _B_INTVCS: atomically exchange an interrupt or IOCS vector
        const uint16_t vector = static_cast<uint16_t>(d1);
        uint32_t vector_address = 0;
        if (vector <= 0x00ffu) {
            vector_address = static_cast<uint32_t>(vector) * 4u;
        } else if (vector <= 0x01ffu) {
            // IOCS software vectors live in the dispatch table used by the
            // trap-#15 bootstrap: entry $100+n maps to $400+n*4.  Keeping
            // these only in a host-side map meant resident drivers could
            // install an IOCS extension but the guest dispatcher would never
            // call it (A-Train II installs _OPMDRV at vector $1f0).
            vector_address = 0x400u + static_cast<uint32_t>(vector & 0x00ffu) * 4u;
        } else {
            m68k_set_reg(M68K_REG_D0, 0xffffffffu);
            return;
        }

        const uint32_t previous = read_memory_32(vector_address);
        write_memory_32(vector_address, a1);
        iocs_vectors_[vector] = a1;
        m68k_set_reg(M68K_REG_D0, previous);
        return;
    }
    case 0x84: // _B_LPEEK
        m68k_set_reg(M68K_REG_D0, read_memory_32(a1));
        return;
    default:
        trace_io("iocs-unsupported", call, static_cast<uint8_t>(d1));
        // Most IOCS procedures use zero for success. Returning an explicit
        // failure is safer than accidentally echoing the call number as a
        // plausible pointer or status value.
        m68k_set_reg(M68K_REG_D0, 0xffffffffu);
        return;
    }
}

bool X68kGenericDriver::bus_address_readable(uint32_t address, uint32_t size) const
{
    address &= 0x00ffffffu;
    if (size != 1u && size != 2u && size != 4u) {
        return false;
    }
    if (size != 1u && (address & 1u) != 0u) {
        return false;
    }
    if (address > 0x01000000u - size) {
        return false;
    }

    const auto byte_readable = [&](uint32_t current) {
        current &= 0x00ffffffu;
        if (current < kRomSize) {
            return true;
        }
        if (current >= 0xf00000u && current < 0xf00000u + kRamSize) {
            return true;
        }
        if (current >= 0xff0000u) {
            return true;
        }
        if (mfp_enabled_ && current >= 0xe88000u && current <= 0xe8802fu) {
            return true;
        }
        if (dmaint_enabled_ && (current == 0xe840e5u || current == 0xe840e7u)) {
            return true;
        }
        if (current == 0xe90001u || current == 0xe90003u || current == 0xe9a005u) {
            return true;
        }
        if (midi_enabled_ && current >= 0xeafa01u && current < 0xeafa10u) {
            return true;
        }
        return false;
    };

    for (uint32_t offset = 0; offset < size; ++offset) {
        if (!byte_readable(address + offset)) {
            return false;
        }
    }
    return true;
}

bool X68kGenericDriver::bus_address_writable(uint32_t address, uint32_t size) const
{
    address &= 0x00ffffffu;
    if (size != 1u && size != 2u && size != 4u) {
        return false;
    }
    if (size != 1u && (address & 1u) != 0u) {
        return false;
    }
    if (address > 0x01000000u - size) {
        return false;
    }

    const auto byte_writable = [&](uint32_t current) {
        current &= 0x00ffffffu;
        if (current < kRomSize) {
            return true;
        }
        if (current >= 0xf00000u && current < 0xf00000u + kRamSize) {
            return true;
        }
        if (current >= 0xff0000u) {
            return true;
        }
        if (mfp_enabled_ && current >= 0xe88000u && current <= 0xe8802fu) {
            return true;
        }
        if ((dmaint_enabled_ && (current == 0xe840e5u || current == 0xe840e7u))
            || current == 0xe840c0u || current == 0xe840c7u
            || current == 0xe840cau || current == 0xe840ccu
            || current == 0xe90001u || current == 0xe90003u
            || current == 0xe9a005u) {
            return true;
        }
        if (midi_enabled_ && current >= 0xeafa01u && current < 0xeafa10u) {
            return true;
        }
        return false;
    };

    for (uint32_t offset = 0; offset < size; ++offset) {
        if (!byte_writable(address + offset)) {
            return false;
        }
    }
    return true;
}

uint32_t X68kGenericDriver::read_memory_sized(uint32_t address, uint32_t size)
{
    uint32_t value = 0;
    for (uint32_t offset = 0; offset < size; ++offset) {
        value = (value << 8) | read_memory_8(address + offset);
    }
    return value;
}

void X68kGenericDriver::write_memory_sized(uint32_t address, uint32_t size, uint32_t value)
{
    for (uint32_t offset = 0; offset < size; ++offset) {
        const uint32_t shift = (size - offset - 1u) * 8u;
        write_memory_8(address + offset, static_cast<uint8_t>(value >> shift));
    }
}

void X68kGenericDriver::handle_host_callback(uint8_t vector)
{
    trace_io("host-callback", 0xe00010, vector);
    // Real Hoot X68000 bootstraps expose trap #2 through host callback
    // marker $22. Keep $02 as compatibility for early synthetic fixtures.
    if ((vector == 0x22 || vector == 0x02) && pcm8_enabled_) {
        const uint32_t d0 = m68k_get_reg(nullptr, M68K_REG_D0);
        const uint32_t d1 = m68k_get_reg(nullptr, M68K_REG_D1);
        const uint32_t d2 = m68k_get_reg(nullptr, M68K_REG_D2);
        const uint32_t a1 = m68k_get_reg(nullptr, M68K_REG_A1);
        const auto result = pcm8_.command(d0, d1, d2, a1,
            [this](uint32_t address, uint8_t& value) {
                return read_pcm_memory_8(address, value);
            });
        trace_pcm8(result, d0, d1, d2, a1);
        m68k_set_reg(M68K_REG_D0, static_cast<uint32_t>(result.return_value));
    } else if (vector == 0x2f) {
        handle_iocs_call();
    } else if (vector == 0xff) {
        // The bootstrap restores A5 before entering the host callback. The
        // actual 68000 format-0 exception frame remains at A7: SR followed by
        // the faulting PC.
        const uint32_t exception_sp = m68k_get_reg(nullptr, M68K_REG_A7) & 0x00ffffffu;
        const uint32_t fault_pc = read_memory_32(exception_sp + 2u) & 0x00ffffffu;
        const uint16_t opcode = static_cast<uint16_t>(
            (static_cast<uint16_t>(read_memory_8(fault_pc)) << 8)
            | read_memory_8(fault_pc + 1u));

        if ((opcode & 0xff00u) == 0xff00u) {
            const uint8_t dos_call = static_cast<uint8_t>(opcode);
            trace_io("human68k-dos", fault_pc, dos_call);
            switch (dos_call) {
            case 0x09: // _PRINT: console output is intentionally discarded.
            case 0x1e: // _FPUTS: diagnostic/file text is discarded headlessly.
                m68k_set_reg(M68K_REG_D0, 0);
                write_memory_32(exception_sp + 2u, (fault_pc + 2u) & 0x00ffffffu);
                return;
            case 0x30: // _VERNUM: report a stable Human68k 3.02 environment.
                // Human68k encodes the signature "68" in the high word and
                // the major/minor version bytes in the low word. ZMSC uses
                // this during bootstrap to select its version-3 DOS path.
                m68k_set_reg(M68K_REG_D0, 0x36380302u);
                write_memory_32(exception_sp + 2u, (fault_pc + 2u) & 0x00ffffffu);
                return;
            case 0xf7: { // _BUS_ERR: copy P1 to P2 while probing bus validity.
                // A format-0 exception frame occupies six bytes. Human68k DOS
                // arguments remain immediately above it: P1.l, P2.l, SIZE.w.
                const uint32_t p1 = read_memory_32(exception_sp + 6u) & 0x00ffffffu;
                const uint32_t p2 = read_memory_32(exception_sp + 10u) & 0x00ffffffu;
                const uint32_t size = (static_cast<uint32_t>(read_memory_8(exception_sp + 14u)) << 8)
                    | read_memory_8(exception_sp + 15u);
                int32_t result = -1;
                if (size == 1u || size == 2u || size == 4u) {
                    if (!bus_address_readable(p1, size)) {
                        result = 2;
                    } else if (!bus_address_writable(p2, size)) {
                        result = 1;
                    } else {
                        write_memory_sized(p2, size, read_memory_sized(p1, size));
                        result = 0;
                    }
                }
                if (trace_.is_open() && (trace_limit_ == 0 || trace_events_ < trace_limit_)) {
                    ++trace_events_;
                    trace_ << "human68k-bus-err"
                           << " cycles=" << debug_cpu_cycles_
                           << " p1=0x" << std::hex << std::setw(6) << std::setfill('0') << p1
                           << " p2=0x" << std::setw(6) << p2
                           << " size=" << std::dec << size
                           << " result=" << result << std::setfill(' ') << "\n";
                }
                m68k_set_reg(M68K_REG_D0, static_cast<uint32_t>(result));
                write_memory_32(exception_sp + 2u, (fault_pc + 2u) & 0x00ffffffu);
                return;
            }
            default:
                break;
            }
        }

        const uint8_t fault_byte = static_cast<uint8_t>(opcode >> 8);
        trace_io("exception-fault-byte", fault_pc, fault_byte);
        ++debug_unhandled_exceptions_;
        if (driver_warning_.empty()) {
            std::ostringstream message;
            message << "unhandled X68000 line-F call at 0x" << std::hex
                    << std::setw(6) << std::setfill('0') << fault_pc
                    << " (opcode 0x" << std::setw(4)
                    << static_cast<unsigned>(opcode) << ")";
            driver_warning_ = message.str();
        }
    }
}

uint8_t X68kGenericDriver::read_memory_8(uint32_t address)
{
    address &= 0x00ffffff;
    if (address >= 0xf00000 && address < 0xf00000 + ram_.size()) {
        return ram_[address - 0xf00000];
    }

    // Decode the few Hoot/X68000 devices before main memory. Modern packs use
    // ordinary writable memory all the way up to 0xe7ffff, including most of
    // the old broad 0xe00000 scratch window.
    switch (address) {
    case 0xe00000:
        ++debug_io_reads_;
        trace_io("mailbox-read", address, mailbox_flag_);
        return mailbox_flag_;
    case 0xe00001:
        ++debug_io_reads_;
        trace_io("mailbox-read", address, static_cast<uint8_t>(mailbox_code_ & 0xff));
        return static_cast<uint8_t>(mailbox_code_ & 0xff);
    case 0xe00002:
        ++debug_io_reads_;
        trace_io("mailbox-read", address, static_cast<uint8_t>(mailbox_code_ >> 8));
        return static_cast<uint8_t>(mailbox_code_ >> 8);
    case 0xe00800:
        ++debug_io_reads_;
        m68k_end_timeslice();
        return 0;
    case 0xe90003:
        ++debug_io_reads_;
        return static_cast<uint8_t>((ym2151_.read(1) & 0x80) | ym2151_timer_status_);
    case 0xe840e5:
        if (dmaint_enabled_) { ++debug_io_reads_; return dma_niv_; }
        break;
    case 0xe840e7:
        if (dmaint_enabled_) { ++debug_io_reads_; return dma_eiv_; }
        break;
    case 0xe9a005:
        ++debug_io_reads_;
        return adpcm_.pan_and_rate();
    default:
        break;
    }

    if (mfp_enabled_ && address >= 0xe88000 && address <= 0xe89fff) {
        ++debug_io_reads_;
        if (address <= 0xe8802f) {
            return read_mfp(address);
        }
        trace_io("mfp-unmapped-read", address, 0);
        return 0;
    }
    if (address >= 0xeafa01 && address < 0xeafa10) {
        ++debug_io_reads_;
        return read_midi(address);
    }

    if (address < rom_.size()) {
        if (address >= 0x1f000 && address < 0x20000) {
            trace_io("opmb-rom-read", address, rom_[address]);
        }
        return rom_[address];
    }
    if (address >= 0xe00000 && address < 0xe00000 + scratch_.size()) {
        trace_io("scratch-read", address, scratch_[address - 0xe00000]);
        return scratch_[address - 0xe00000];
    }
    if (address >= 0xff0000u) {
        return high_memory_[address - 0xff0000u];
    }

    ++debug_io_reads_;
    trace_io("io-read", address, 0);
    return 0;
}

void X68kGenericDriver::write_memory_8(uint32_t address, uint8_t data)
{
    address &= 0x00ffffff;
    if (address >= 0xf00000 && address < 0xf00000 + ram_.size()) {
        ram_[address - 0xf00000] = data;
        return;
    }

    switch (address) {
    case 0xe00000:
        ++debug_io_writes_;
        mailbox_flag_ = data;
        trace_io("mailbox-write", address, data);
        return;
    case 0xe00010:
        ++debug_io_writes_;
        handle_host_callback(data);
        return;
    case 0xe90001:
        ++debug_io_writes_;
        current_ym2151_reg_ = data;
        ++debug_ym2151_writes_;
        ym2151_.write(0, data);
        return;
    case 0xe90003:
        ++debug_io_writes_;
        debug_last_ym2151_reg_ = current_ym2151_reg_;
        debug_last_ym2151_data_ = data;
        ym2151_registers_[current_ym2151_reg_] = data;
        if (current_ym2151_reg_ == 0x08) {
            const uint8_t channel = data & 0x07;
            ym2151_key_on_[channel] = (data & 0x78) != 0;
        }
        update_ym2151_timer(current_ym2151_reg_, data);
        if (current_ym2151_reg_ == 0x08 && (data & 0x78) != 0) {
            ++debug_ym2151_keyons_;
        }
        ++debug_ym2151_writes_;
        trace_ym2151(current_ym2151_reg_, data);
        ym2151_.write(1, data);
        return;
    case 0xe840c0:
        ++debug_io_writes_;
        ++debug_adpcm_writes_;
        if (data == 0xff) {
            adpcm_.stop();
        }
        return;
    case 0xe840c7:
        ++debug_io_writes_;
        ++debug_adpcm_writes_;
        if (data == 0x88 && adpcm_address_ < rom_.size() && adpcm_size_ != 0) {
            const auto count = std::min<size_t>(adpcm_size_, rom_.size() - adpcm_address_);
            if (adpcm_.play_memory(rom_.data() + adpcm_address_, count)) {
                ++debug_adpcm_starts_;
            }
        } else {
            adpcm_.stop();
        }
        return;
    case 0xe840ca:
        ++debug_io_writes_;
        ++debug_adpcm_writes_;
        adpcm_size_ = static_cast<uint32_t>(m68k_get_reg(nullptr, M68K_REG_D2) & 0xffff);
        return;
    case 0xe840cc:
        ++debug_io_writes_;
        ++debug_adpcm_writes_;
        adpcm_address_ = static_cast<uint32_t>(m68k_get_reg(nullptr, M68K_REG_A1) & 0x00ffffff);
        return;
    case 0xe840e5:
        if (dmaint_enabled_) {
            ++debug_io_writes_;
            dma_niv_ = data;
            trace_io("dma-niv-write", address, data);
            return;
        }
        break;
    case 0xe840e7:
        if (dmaint_enabled_) {
            ++debug_io_writes_;
            dma_eiv_ = data;
            trace_io("dma-eiv-write", address, data);
            return;
        }
        break;
    case 0xe9a005:
        ++debug_io_writes_;
        ++debug_adpcm_writes_;
        adpcm_.set_pan_and_rate(data);
        return;
    default:
        break;
    }

    if (mfp_enabled_ && address >= 0xe88000 && address <= 0xe89fff) {
        ++debug_io_writes_;
        if (address <= 0xe8802f) {
            write_mfp(address, data);
        } else {
            trace_io("mfp-unmapped-write", address, data);
        }
        return;
    }
    if (address >= 0xeafa01 && address < 0xeafa10) {
        ++debug_io_writes_;
        write_midi(address, data);
        return;
    }

    if (address < rom_.size()) {
        if ((address >= 0x0100 && address < 0x0200)
            || (address >= 0x0400 && address < 0x0410)) {
            trace_io("low-vector-write", address, data);
        }
        rom_[address] = data;
        return;
    }
    if (address >= 0xe00000 && address < 0xe00000 + scratch_.size()) {
        scratch_[address - 0xe00000] = data;
        trace_io("scratch-write", address, data);
        return;
    }
    if (address >= 0xff0000u) {
        high_memory_[address - 0xff0000u] = data;
        return;
    }

    ++debug_io_writes_;
    trace_io("io-write", address, data);
}

bool X68kGenericDriver::read_pcm_memory_8(uint32_t address, uint8_t& value) const
{
    address &= 0x00ffffffu;
    if (address < rom_.size()) {
        value = rom_[address];
        return true;
    }
    if (address >= 0xf00000u && address < 0xf00000u + ram_.size()) {
        value = ram_[address - 0xf00000u];
        return true;
    }
    if (address >= 0xff0000u) {
        value = high_memory_[address - 0xff0000u];
        return true;
    }
    return false;
}

uint32_t X68kGenericDriver::read_memory_32(uint32_t address)
{
    return (static_cast<uint32_t>(read_memory_8(address)) << 24)
        | (static_cast<uint32_t>(read_memory_8(address + 1)) << 16)
        | (static_cast<uint32_t>(read_memory_8(address + 2)) << 8)
        | static_cast<uint32_t>(read_memory_8(address + 3));
}

uint32_t X68kGenericDriver::read_be32(size_t offset) const
{
    if (offset + 4 > rom_.size()) {
        return 0;
    }
    return (static_cast<uint32_t>(rom_[offset]) << 24)
        | (static_cast<uint32_t>(rom_[offset + 1]) << 16)
        | (static_cast<uint32_t>(rom_[offset + 2]) << 8)
        | static_cast<uint32_t>(rom_[offset + 3]);
}

} // namespace hoot
