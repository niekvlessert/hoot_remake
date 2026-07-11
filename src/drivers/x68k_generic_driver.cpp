#include "drivers/x68k_generic_driver.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iomanip>
#include <sstream>
#include <vector>

#include "io/zip_archive.h"

extern "C" {
#include "m68k.h"
}

namespace hoot {
namespace {

X68kGenericDriver* g_active_driver = nullptr;

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
    static_assert(N > 0, "destination must have room for a terminator");
    const auto count = std::min(source.size(), N - 1);
    std::memcpy(dest, source.data(), count);
    dest[count] = '\0';
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

extern "C" {

signed int my_irqh_callback(signed int level)
{
    return hoot::g_active_driver != nullptr
        ? hoot::g_active_driver->acknowledge_interrupt(level)
        : M68K_INT_ACK_AUTOVECTOR;
}

uint32_t m68k_read_memory_8(uint32_t address)
{
    return hoot::g_active_driver != nullptr ? hoot::g_active_driver->read_memory_8(address) : 0;
}

uint32_t m68k_read_memory_16(uint32_t address)
{
    return (m68k_read_memory_8(address) << 8) | m68k_read_memory_8(address + 1);
}

uint32_t m68k_read_memory_32(uint32_t address)
{
    return (m68k_read_memory_16(address) << 16) | m68k_read_memory_16(address + 2);
}

void m68k_write_memory_8(uint32_t address, uint32_t value)
{
    if (hoot::g_active_driver != nullptr) {
        hoot::g_active_driver->write_memory_8(address, static_cast<uint8_t>(value));
    }
}

void m68k_write_memory_16(uint32_t address, uint32_t value)
{
    m68k_write_memory_8(address, value >> 8);
    m68k_write_memory_8(address + 1, value);
}

void m68k_write_memory_32(uint32_t address, uint32_t value)
{
    m68k_write_memory_16(address, value >> 16);
    m68k_write_memory_16(address + 2, value);
}

void m68k_write_memory_32_pd(uint32_t address, uint32_t value)
{
    m68k_write_memory_16(address + 2, value >> 16);
    m68k_write_memory_16(address, value);
}

uint32_t m68k_read_immediate_16(uint32_t address) { return m68k_read_memory_16(address); }
uint32_t m68k_read_immediate_32(uint32_t address) { return m68k_read_memory_32(address); }
uint32_t m68k_read_pcrelative_8(uint32_t address) { return m68k_read_memory_8(address); }
uint32_t m68k_read_pcrelative_16(uint32_t address) { return m68k_read_memory_16(address); }
uint32_t m68k_read_pcrelative_32(uint32_t address) { return m68k_read_memory_32(address); }
uint32_t m68k_read_disassembler_8(uint32_t address) { return m68k_read_memory_8(address); }
uint32_t m68k_read_disassembler_16(uint32_t address) { return m68k_read_memory_16(address); }
uint32_t m68k_read_disassembler_32(uint32_t address) { return m68k_read_memory_32(address); }

}

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
               << " archive=\"" << entry.archive << "\""
               << " cpu-clock=" << cpu_clock_hz_
               << " ym2151-clock=" << ym2151_clock_hz_
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
    adpcm_gain_ = 0.40;
    if (const char* value = std::getenv("HOOT_X68K_ADPCM_GAIN")) {
        adpcm_gain_ = std::clamp(std::strtod(value, nullptr), 0.0, 4.0);
    }
    if (const char* value = std::getenv("HOOT_X68K_MUTE_PERCUSSION")) {
        mute_percussion_ = std::strcmp(value, "1") == 0
            || std::strcmp(value, "on") == 0
            || std::strcmp(value, "true") == 0;
    }
    opm_mute_mask_ = parse_opm_mute_mask(std::getenv("HOOT_X68K_CHANNELS"));
    ym2151_.set_mute_mask(opm_mute_mask_);
    g_active_driver = this;
    m68k_set_cpu_type(M68K_CPU_TYPE_68000);
    m68k_init();
    m68k_pulse_reset();
    m68k_set_reg(M68K_REG_ISP, reset_sp_);
    m68k_set_reg(M68K_REG_MSP, reset_sp_);
    execute_with_audio_clock(5000.0 / cpu_clock_hz_);
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

    const auto reset_on_track = entry.options.find("reset_on_track");
    const bool restart_machine = reset_on_track != entry.options.end()
        && reset_on_track->second != 0;
    if (restart_machine) {
        reset();
    } else if (has_selected_track_) {
        const auto stop = entry.options.find("stop");
        if (stop != entry.options.end()) {
            mailbox_code_ = static_cast<uint16_t>(stop->second);
            mailbox_flag_ = 0x01;
            g_active_driver = this;
            execute_with_audio_clock(100000.0 / cpu_clock_hz_);
        }
    }
    select_voice_bank(entry, track_index);
    diagnose_opmdrv_voices(entry, track_index);
    selected_track_ = track_index;
    selected_code_ = entry.tracks[track_index].code;
    uint32_t command_code = selected_code_;

    mailbox_code_ = static_cast<uint16_t>(command_code);
    mailbox_flag_ = 0x01;
    g_active_driver = this;
    execute_with_audio_clock(100000.0 / cpu_clock_hz_);
    has_selected_track_ = true;
    return HOOT_OK;
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
    selected_track_ = 0;
    selected_code_ = 0;
    has_selected_track_ = false;
    render_cycle_remainder_ = 0.0;
    cpu_cycle_debt_ = 0;
    mailbox_flag_ = 0;
    mailbox_code_ = 0;
    midi_reg_high_ = 0;
    midi_vector_ = 0;
    midi_int_enable_ = 0;
    midi_int_vect_ = 0x10;
    midi_buffered_ = 0;
    debug_cpu_cycles_ = 0;
    debug_io_reads_ = 0;
    debug_io_writes_ = 0;
    debug_ym2151_writes_ = 0;
    debug_ym2151_keyons_ = 0;
    debug_ym2151_irqs_ = 0;
    reset_ym2151_timers();
    debug_adpcm_writes_ = 0;
    debug_adpcm_starts_ = 0;
    adpcm_address_ = 0;
    adpcm_size_ = 0;
    current_ym2151_reg_ = 0;
    debug_last_ym2151_reg_ = 0;
    debug_last_ym2151_data_ = 0;
    ym2151_.reset();
    ym2151_.set_mute_mask(opm_mute_mask_);
    adpcm_.reset();
    g_active_driver = this;
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
    // Keep register writes close to the samples they affect.  Nuked OPM also
    // owns a cycle-accurate bus queue, so it needs a render clock each sample.
    const int chunk_frames = ym2151_.uses_nuked_core() ? 1 : 8;
    int rendered = 0;
    while (rendered < frames) {
        const int todo = std::min(chunk_frames, frames - rendered);
        const double exact_cycles = static_cast<double>(todo) * cpu_clock_hz_
            / static_cast<double>(sample_rate_) + render_cycle_remainder_;
        const int cycles = static_cast<int>(exact_cycles);
        render_cycle_remainder_ = exact_cycles - static_cast<double>(cycles);
        execute_seconds(static_cast<double>(cycles) / cpu_clock_hz_);
        ym2151_.render_s16(interleaved_stereo + (rendered * 2), todo);
        if (!mute_percussion_) {
            adpcm_.mix_s16(interleaved_stereo + (rendered * 2), todo, adpcm_gain_);
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
    std::fill(interleaved_stereo, interleaved_stereo + (frames * 2), 0.0f);
    return frames;
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
    copy_c_string(out.driver, name());
    copy_c_string(out.warning, track_warning_);

    if (track_index >= 0 && static_cast<size_t>(track_index) < entry.tracks.size()) {
        copy_c_string(out.title, entry.tracks[track_index].title);
    } else {
        copy_c_string(out.title, entry.title);
    }
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
    voice_banks_.clear();
    active_voice_bank_offset_ = kVoiceBankOffset;
    track_warning_.clear();
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
    adpcm_gain_ = 0.40;
    mute_percussion_ = false;
    opm_mute_mask_ = 0;
    current_ym2151_reg_ = 0;
    debug_last_ym2151_reg_ = 0;
    debug_last_ym2151_data_ = 0;
    mailbox_flag_ = 0;
    mailbox_code_ = 0;
    midi_enabled_ = false;
    midi_reg_high_ = 0;
    midi_vector_ = 0;
    midi_int_enable_ = 0;
    midi_int_vect_ = 0x10;
    midi_buffered_ = 0;
    loaded_ = false;
    has_opmdrv_voice_transform_ = false;
    if (trace_.is_open()) {
        trace_.close();
    }
    trace_events_ = 0;
    trace_limit_ = 0;
}

void X68kGenericDriver::execute_seconds(double seconds)
{
    if (seconds <= 0.0) {
        return;
    }
    g_active_driver = this;
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
    if (midi_buffered_ != 0) {
        midi_buffered_ -= std::min<uint32_t>(midi_buffered_,
                                              static_cast<uint32_t>(executed_total / 3200));
    }
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
    m68k_set_irq(active ? 6 : 0);
}

int X68kGenericDriver::acknowledge_interrupt(int level)
{
    if (level == 6) {
        // X68000 IRQH clears its CPU-side request latch on acknowledge. The
        // YM2151 status bit remains set until register 0x14 resets it.
        ym2151_irq_asserted_ = false;
        trace_io("irq6-ack", 0, 0);
        m68k_set_irq(0);
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
           << " addr=0x" << std::setw(6) << address
           << " data=0x" << std::setw(2) << static_cast<unsigned>(data)
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
            data = midi_buffered_ >= 256 ? 0x01 : 0xc0;
        }
        break;
    default:
        break;
    }
    trace_io("midi-read", address, data);
    return data;
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
            midi_buffered_ = 0;
        }
        break;
    case 0x09:
        if (midi_reg_high_ == 0) {
            midi_vector_ = data & 0xe0;
        }
        break;
    case 0x0d:
        if (midi_reg_high_ == 0) {
            midi_int_enable_ = data;
        } else if (midi_reg_high_ == 5) {
            ++midi_buffered_;
        }
        break;
    default:
        break;
    }
    trace_io("midi-write", address, data);
}

uint8_t X68kGenericDriver::read_memory_8(uint32_t address)
{
    address &= 0x00ffffff;
    if (address < rom_.size()) {
        if (address >= 0x1f000 && address < 0x20000) {
            trace_io("opmb-rom-read", address, rom_[address]);
        }
        return rom_[address];
    }
    if (address >= 0xf00000 && address < 0xf00000 + ram_.size()) {
        return ram_[address - 0xf00000];
    }
    ++debug_io_reads_;
    switch (address) {
    case 0xe00000:
        trace_io("mailbox-read", address, mailbox_flag_);
        return mailbox_flag_;
    case 0xe00001:
        trace_io("mailbox-read", address, static_cast<uint8_t>(mailbox_code_ & 0xff));
        return static_cast<uint8_t>(mailbox_code_ & 0xff);
    case 0xe00002:
        trace_io("mailbox-read", address, static_cast<uint8_t>(mailbox_code_ >> 8));
        return static_cast<uint8_t>(mailbox_code_ >> 8);
    case 0xe00800:
        m68k_end_timeslice();
        return 0;
    case 0xe90003:
        return static_cast<uint8_t>((ym2151_.read(1) & 0x80) | ym2151_timer_status_);
    case 0xe9a005:
        return adpcm_.pan_and_rate();
    default:
        if (address >= 0xeafa01 && address < 0xeafa10) {
            return read_midi(address);
        }
        if (address >= 0xe00000 && address < 0xe00000 + scratch_.size()) {
            --debug_io_reads_;
            trace_io("scratch-read", address, scratch_[address - 0xe00000]);
            return scratch_[address - 0xe00000];
        }
        trace_io("io-read", address, 0);
        return 0;
    }
}

void X68kGenericDriver::write_memory_8(uint32_t address, uint8_t data)
{
    address &= 0x00ffffff;
    if (address < rom_.size()) {
        if (address >= 0x0400 && address < 0x0410) {
            trace_io("low-vector-write", address, data);
        }
        rom_[address] = data;
        return;
    }
    if (address >= 0xf00000 && address < 0xf00000 + ram_.size()) {
        ram_[address - 0xf00000] = data;
        return;
    }
    ++debug_io_writes_;
    switch (address) {
    case 0xe00000:
        mailbox_flag_ = data;
        trace_io("mailbox-write", address, data);
        break;
    case 0xe90001:
        current_ym2151_reg_ = data;
        ++debug_ym2151_writes_;
        ym2151_.write(0, data);
        break;
    case 0xe90003:
        debug_last_ym2151_reg_ = current_ym2151_reg_;
        debug_last_ym2151_data_ = data;
        update_ym2151_timer(current_ym2151_reg_, data);
        if (current_ym2151_reg_ == 0x08 && (data & 0x78) != 0) {
            ++debug_ym2151_keyons_;
        }
        ++debug_ym2151_writes_;
        trace_ym2151(current_ym2151_reg_, data);
        ym2151_.write(1, data);
        break;
    case 0xe840c0:
        ++debug_adpcm_writes_;
        if (data == 0xff) {
            adpcm_.stop();
        }
        break;
    case 0xe840c7:
        ++debug_adpcm_writes_;
        if (data == 0x88 && adpcm_address_ < rom_.size() && adpcm_size_ != 0) {
            const auto count = std::min<size_t>(adpcm_size_, rom_.size() - adpcm_address_);
            if (adpcm_.play_memory(rom_.data() + adpcm_address_, count)) {
                ++debug_adpcm_starts_;
            }
        } else {
            adpcm_.stop();
        }
        break;
    case 0xe840ca:
        ++debug_adpcm_writes_;
        adpcm_size_ = static_cast<uint32_t>(m68k_get_reg(nullptr, M68K_REG_D2) & 0xffff);
        break;
    case 0xe840cc:
        ++debug_adpcm_writes_;
        adpcm_address_ = static_cast<uint32_t>(m68k_get_reg(nullptr, M68K_REG_A1) & 0x00ffffff);
        break;
    case 0xe9a005:
        ++debug_adpcm_writes_;
        adpcm_.set_pan_and_rate(data);
        break;
    default:
        if (address >= 0xeafa01 && address < 0xeafa10) {
            write_midi(address, data);
            break;
        }
        if (address >= 0xe00000 && address < 0xe00000 + scratch_.size()) {
            --debug_io_writes_;
            scratch_[address - 0xe00000] = data;
            trace_io("scratch-write", address, data);
            break;
        }
        trace_io("io-write", address, data);
        break;
    }
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
