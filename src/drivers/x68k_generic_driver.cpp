#include "drivers/x68k_generic_driver.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iomanip>
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
void write_rom_be32(std::array<uint8_t, N>& rom, size_t offset, uint32_t value)
{
    rom[offset] = static_cast<uint8_t>(value >> 24);
    rom[offset + 1] = static_cast<uint8_t>(value >> 16);
    rom[offset + 2] = static_cast<uint8_t>(value >> 8);
    rom[offset + 3] = static_cast<uint8_t>(value);
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
    uint32_t patch_offset = 0;
    bool have_patch_offset = false;
    while (reloc_cursor < reloc_offset + reloc_size) {
        uint32_t delta = read_be16(data, reloc_cursor);
        reloc_cursor += 2;
        if (delta == 0) {
            break;
        }
        if (delta == 1) {
            if (reloc_cursor + 4 > reloc_offset + reloc_size) {
                error = "truncated Human68k .X relocation table: " + path;
                return false;
            }
            delta = read_be32(data, reloc_cursor);
            reloc_cursor += 4;
        }
        patch_offset = have_patch_offset ? patch_offset + delta : delta;
        have_patch_offset = true;
        if (patch_offset + 4 > image_size) {
            error = "Human68k .X relocation outside image: " + path;
            return false;
        }
        const size_t patch_address = offset + patch_offset;
        write_rom_be32(rom, patch_address, read_rom_be32(rom, patch_address) + static_cast<uint32_t>(offset));
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

} // namespace

} // namespace hoot

extern "C" {

signed int my_irqh_callback(signed int)
{
    return M68K_INT_ACK_AUTOVECTOR;
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

        if (entry.archive == "ad68snd" && asset.path == "kmdrv.bin" && !archive.contains(asset.path)) {
            if (!load_ad68snd_legacy_pack(packs_path, error)) {
                return HOOT_ERROR_IO;
            }
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
        if (asset.offset >= rom_.size()) {
            error = "x68k code asset offset is outside ROM: " + asset.path;
            return HOOT_ERROR_PARSE;
        }

        const auto count = std::min<size_t>(data.size(), rom_.size() - asset.offset);
        std::copy_n(data.begin(), count, rom_.begin() + static_cast<std::ptrdiff_t>(asset.offset));
        loaded_code_bytes_ += count;
    }

    if (loaded_code_bytes_ == 0) {
        error = "x68k/generic entry did not load any code asset";
        return HOOT_ERROR_NOT_FOUND;
    }

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
    if (!ym2151_.initialize(ym2151_clock_hz_, static_cast<uint32_t>(sample_rate_))) {
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
    ym2151_.set_irq_handler(&X68kGenericDriver::ym2151_irq_handler, this);
    g_active_driver = this;
    m68k_set_cpu_type(M68K_CPU_TYPE_68000);
    m68k_init();
    m68k_pulse_reset();
    m68k_set_reg(M68K_REG_ISP, reset_sp_);
    m68k_set_reg(M68K_REG_MSP, reset_sp_);
    execute_seconds(5000.0 / cpu_clock_hz_);
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

    selected_track_ = track_index;
    selected_code_ = entry.tracks[track_index].code;
    uint32_t command_code = selected_code_;
    const bool ad68snd_modern_code = entry.archive == "ad68snd" && command_code >= 0xb0 && command_code <= 0xc6;
    if (std::getenv("HOOT_AD68_TRANSLATE_CODES") != nullptr
        && ad68snd_modern_code) {
        command_code -= 0xaf;
    }
    if (ad68snd_legacy_layout_ && command_code != 0x00 && command_code != 0x11) {
        mailbox_code_ = 0x11;
        mailbox_flag_ = 0x01;
        pending_ym2151_irq_pulses_ = std::max<uint32_t>(pending_ym2151_irq_pulses_, 1);
        g_active_driver = this;
        execute_seconds(500000.0 / cpu_clock_hz_);
    }
    mailbox_code_ = static_cast<uint16_t>(command_code);
    mailbox_flag_ = 0x01;
    if (ad68snd_legacy_layout_) {
        pending_ym2151_irq_pulses_ = std::max<uint32_t>(pending_ym2151_irq_pulses_, 1);
    }
    g_active_driver = this;
    execute_seconds(100000.0 / cpu_clock_hz_);

    return HOOT_OK;
}

void X68kGenericDriver::reset()
{
    selected_track_ = 0;
    selected_code_ = 0;
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
    pending_ym2151_irq_pulses_ = 0;
    debug_adpcm_writes_ = 0;
    debug_adpcm_starts_ = 0;
    adpcm_address_ = 0;
    adpcm_size_ = 0;
    current_ym2151_reg_ = 0;
    debug_last_ym2151_reg_ = 0;
    debug_last_ym2151_data_ = 0;
    ym2151_.reset();
    adpcm_.reset();
    g_active_driver = this;
    m68k_pulse_reset();
    m68k_set_reg(M68K_REG_ISP, reset_sp_);
    m68k_set_reg(M68K_REG_MSP, reset_sp_);
}

int X68kGenericDriver::render_s16(int16_t* interleaved_stereo, int frames)
{
    if (interleaved_stereo == nullptr || frames < 0) {
        return 0;
    }
    constexpr int kChunkFrames = 64;
    int rendered = 0;
    while (rendered < frames) {
        const int todo = std::min(kChunkFrames, frames - rendered);
        execute_seconds(static_cast<double>(todo) / static_cast<double>(sample_rate_));
        ym2151_.render_s16(interleaved_stereo + (rendered * 2), todo);
        adpcm_.mix_s16(interleaved_stereo + (rendered * 2), todo, adpcm_gain_);
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
    ram_.fill(0);
    scratch_.fill(0);
    sample_rate_ = 44100;
    cpu_clock_hz_ = 10000000.0;
    ym2151_clock_hz_ = 4000000;
    selected_track_ = 0;
    selected_code_ = 0;
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
    pending_ym2151_irq_pulses_ = 0;
    debug_adpcm_writes_ = 0;
    debug_adpcm_starts_ = 0;
    adpcm_address_ = 0;
    adpcm_size_ = 0;
    adpcm_gain_ = 0.40;
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
    ad68snd_legacy_layout_ = false;
    if (trace_.is_open()) {
        trace_.close();
    }
    trace_events_ = 0;
    trace_limit_ = 0;
}

bool X68kGenericDriver::load_ad68snd_legacy_pack(const std::string& packs_path, std::string& error)
{
    const auto archive_path = std::filesystem::path(packs_path) / "ad68snd.zip";
    ZipArchive archive;
    if (!archive.open(archive_path, error)) {
        return false;
    }

    struct Member {
        const char* path;
        size_t offset;
    };
    constexpr Member kAd68sndBgLayout[] = {
        {"ad68snd.bin", 0x00000},
        {"ADPCM_BG.DAT", 0x20000},
        {"ADPCM_SE.DAT", 0x40000},
        {"SOUND_BG.DAT", 0x60000},
        {"SOUND_SE.DAT", 0x70000},
        {"VOICE_BG.DAT", 0x80000},
        {"VOICE_SE.DAT", 0x90000},
        {"TABLE_BG.DAT", 0xa0000},
        {"YUSEN_TB.DAT", 0xa8000},
    };

    for (const auto& member : kAd68sndBgLayout) {
        if (!load_archive_member_at(archive, rom_, member.path, member.offset, error)) {
            return false;
        }
    }
    if (!load_human68k_x_at(archive, rom_, "KMDRV.X", 0x08000, error)) {
        return false;
    }

    loaded_code_bytes_ += 1;
    ad68snd_legacy_layout_ = true;
    return true;
}

void X68kGenericDriver::execute_seconds(double seconds)
{
    if (seconds <= 0.0) {
        return;
    }
    g_active_driver = this;
    const auto cycles = static_cast<int>(cpu_clock_hz_ * seconds);
    debug_cpu_cycles_ += static_cast<uint64_t>(cycles);
    if (midi_buffered_ != 0) {
        midi_buffered_ -= std::min<uint32_t>(midi_buffered_, static_cast<uint32_t>(cycles / 3200));
    }
    int remaining = cycles;
    if (pending_ym2151_irq_pulses_ != 0 && remaining > 0) {
        int irq_budget = 200;
        if (const char* value = std::getenv("HOOT_X68K_IRQ_CYCLES")) {
            irq_budget = std::max(1, std::atoi(value));
        }
        const int irq_cycles = std::min(remaining, irq_budget);
        --pending_ym2151_irq_pulses_;
        m68k_set_irq(6);
        m68k_execute(irq_cycles);
        m68k_set_irq(0);
        remaining -= irq_cycles;
    }
    if (remaining > 0) {
        m68k_execute(remaining);
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
           << " addr=0x" << std::setw(6) << address
           << " data=0x" << std::setw(2) << static_cast<unsigned>(data)
           << std::dec << std::setfill(' ') << "\n";
}

void X68kGenericDriver::ym2151_irq_handler(void*, uint8_t irq)
{
    if (g_active_driver == nullptr) {
        return;
    }
    if (irq != 0) {
        ++g_active_driver->debug_ym2151_irqs_;
        if (g_active_driver->pending_ym2151_irq_pulses_ < 8) {
            ++g_active_driver->pending_ym2151_irq_pulses_;
        }
    }
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
        return ym2151_.read(1);
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
