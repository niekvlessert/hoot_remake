#include "drivers/konami_hornet_driver.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <sstream>
#include <utility>

#include "core/utf8_util.h"
#include "core/visual_state_util.h"
#include "io/zip_archive.h"

extern "C" {
#include "m68k.h"
}

namespace hoot {
namespace {

bool copy_asset(const HootAssetRef& asset, ZipArchive& archive,
                std::vector<uint8_t>& destination, std::string& error)
{
    auto data = archive.read(asset.path, error);
    if (!error.empty()) return false;
    if (asset.offset > destination.size() || data.size() > destination.size() - asset.offset) {
        error = "Hornet asset exceeds configured region: " + asset.path;
        return false;
    }
    std::copy(data.begin(), data.end(), destination.begin() + asset.offset);
    return true;
}

} // namespace

HootResult KonamiHornetDriver::load(const HootEntry& entry,
                                    const std::string& packs_path,
                                    int sample_rate,
                                    std::string& error)
{
    loaded_ = false;
    sample_rate_ = sample_rate > 0 ? sample_rate : 44100;
    pcm_gain_ = 1.0;
    if (const auto it = entry.options.find("pcm_mix"); it != entry.options.end())
        pcm_gain_ = std::clamp(static_cast<double>(it->second) / 256.0, 0.0, 4.0);

    ZipArchive archive;
    const auto archive_path = std::filesystem::path(packs_path) / (entry.archive + ".zip");
    if (!archive.open(archive_path, error)) return HOOT_ERROR_IO;

    std::vector<uint8_t> raw_program(kProgramSize, 0xff);
    size_t pcm_size = 0x800000;
    if (const auto it = entry.options.find("pcm_size"); it != entry.options.end() && it->second > 0)
        pcm_size = static_cast<size_t>(it->second);
    for (const auto& asset : entry.assets) {
        if (asset.type == "pcm")
            pcm_size = std::max(pcm_size, static_cast<size_t>(asset.offset) + 1);
    }
    std::vector<uint8_t> pcm(pcm_size, 0);
    bool have_program = false;
    bool have_pcm = false;
    for (const auto& asset : entry.assets) {
        if (asset.type == "code") {
            if (!copy_asset(asset, archive, raw_program, error)) return HOOT_ERROR_IO;
            have_program = true;
        } else if (asset.type == "pcm") {
            auto data = archive.read(asset.path, error);
            if (!error.empty()) return HOOT_ERROR_IO;
            const size_t required = static_cast<size_t>(asset.offset) + data.size();
            if (required > pcm.size()) pcm.resize(required, 0);
            std::copy(data.begin(), data.end(), pcm.begin() + asset.offset);
            have_pcm = true;
        }
    }
    // Convenient fallback for the canonical Gradius IV sound-only archive.
    if (!have_program && archive.contains("837a08.7s")) {
        HootAssetRef asset{"code", "837a08.7s", {}, 0, 0, false};
        if (!copy_asset(asset, archive, raw_program, error)) return HOOT_ERROR_IO;
        have_program = true;
    }
    if (!have_pcm && archive.contains("837a09.16p") && archive.contains("837a10.14p")) {
        for (const auto& item : std::array<std::pair<const char*, size_t>, 2>{{
                 {"837a09.16p", 0}, {"837a10.14p", 0x400000}}}) {
            auto data = archive.read(item.first, error);
            if (!error.empty()) return HOOT_ERROR_IO;
            if (item.second + data.size() > pcm.size()) pcm.resize(item.second + data.size(), 0);
            std::copy(data.begin(), data.end(), pcm.begin() + item.second);
        }
        have_pcm = true;
    }
    if (!have_program || !have_pcm) {
        error = "Hornet requires a 68000 code ROM and RF5C400 PCM ROMs";
        return HOOT_ERROR_NOT_FOUND;
    }

    // MAME's ROM_LOAD16_WORD_SWAP reflects the byte lanes on the 68000 bus.
    for (size_t i = 0; i + 1 < program_.size(); i += 2) {
        program_[i] = raw_program[i + 1];
        program_[i + 1] = raw_program[i];
    }
    if (!rf5c400_.initialize(kSoundClock, static_cast<uint32_t>(sample_rate_))) {
        error = "unable to initialize RF5C400";
        return HOOT_ERROR_UNSUPPORTED;
    }
    rf5c400_.set_rom(std::move(pcm));
    musashi_set_active_bus(this);
    m68k_set_cpu_type(M68K_CPU_TYPE_68000);
    m68k_init();
    m68k_set_instr_hook_callback(nullptr);
    loaded_ = true;
    reset();
    return HOOT_OK;
}

HootResult KonamiHornetDriver::select_track(const HootEntry& entry,
                                            int track_index,
                                            std::string& error)
{
    if (!loaded_) {
        error = "Hornet driver is not loaded";
        return HOOT_ERROR_NOT_LOADED;
    }
    if (track_index < 0 || static_cast<size_t>(track_index) >= entry.tracks.size()) {
        error = "Hornet track index is out of range";
        return HOOT_ERROR_INVALID_ARGUMENT;
    }
    reset();
    if (!run_until_interface_ready()) {
        std::ostringstream message;
        message << "Hornet sound program did not enable the K056800 command interrupt"
                << " (pc=0x" << std::hex << m68k_get_reg(nullptr, M68K_REG_PC)
                << ", d0=0x" << m68k_get_reg(nullptr, M68K_REG_D0)
                << ", d1=0x" << m68k_get_reg(nullptr, M68K_REG_D1)
                << ", a2=0x" << m68k_get_reg(nullptr, M68K_REG_A2)
                << ", sp=0x" << m68k_get_reg(nullptr, M68K_REG_SP)
                << ", sr=0x" << m68k_get_reg(nullptr, M68K_REG_SR)
                << std::dec << ", cycles=" << cpu_cycles_
                << ", io_reads=" << io_reads_ << ", io_writes=" << io_writes_ << ')';
        error = message.str();
        return HOOT_ERROR_UNSUPPORTED;
    }
    selected_track_ = track_index;
    selected_code_ = entry.tracks[track_index].code;
    send_command(selected_code_);
    // Give the sound program enough time to consume the command before the
    // first UI buffer; audio is intentionally not pre-rendered or discarded.
    execute_cycles(kCpuClock / 1000);
    return HOOT_OK;
}

void KonamiHornetDriver::reset()
{
    if (!loaded_) return;
    ram_.fill(0);
    rf_registers_.fill(0);
    interface_.reset();
    rf5c400_.reset();
    cpu_cycle_fraction_ = 0.0;
    periodic_irq_cycles_ = 0.0;
    timer_irq_enabled_ = false;
    timer_irq_asserted_ = false;
    selected_track_ = -1;
    selected_code_ = 0;
    rendered_frames_ = 0;
    cpu_cycles_ = 0;
    io_reads_ = 0;
    io_writes_ = 0;
    rf5c400_writes_ = 0;
    musashi_set_active_bus(this);
    m68k_pulse_reset();
    m68k_set_irq(0);
}

bool KonamiHornetDriver::run_until_interface_ready()
{
    constexpr int slice = 2000;
    // The Gradius IV program performs its large work-RAM initialization from
    // the 344.5 Hz service interrupt.  It needs substantially more than a
    // conventional sub-second CPU bootstrap before enabling K056800 IRQ2.
    constexpr int limit = kCpuClock * 30;
    for (int cycles = 0; cycles < limit && !interface_.interrupt_enabled(); cycles += slice)
        execute_cycles(slice);
    return interface_.interrupt_enabled();
}

void KonamiHornetDriver::send_command(uint32_t code)
{
    interface_.host_write(0, static_cast<uint8_t>(code >> 24));
    interface_.host_write(1, static_cast<uint8_t>(code >> 16));
    interface_.host_write(2, static_cast<uint8_t>(code >> 8));
    interface_.host_write(3, static_cast<uint8_t>(code));
    interface_.host_write(7, 0);
    update_cpu_irq();
}

void KonamiHornetDriver::execute_cycles(int cycles)
{
    if (cycles <= 0) return;
    musashi_set_active_bus(this);
    int remaining = cycles;
    constexpr double irq_period = static_cast<double>(kCpuClock) * 384.0 * 128.0
        / static_cast<double>(kSoundClock);
    while (remaining > 0) {
        const double until_irq = irq_period - periodic_irq_cycles_;
        const int quantum = std::min(remaining,
            std::max(1, static_cast<int>(std::ceil(until_irq))));
        const int executed = m68k_execute(quantum);
        if (executed <= 0) break;
        remaining -= executed;
        cpu_cycles_ += static_cast<uint64_t>(executed);
        periodic_irq_cycles_ += executed;
        if (periodic_irq_cycles_ >= irq_period) {
            periodic_irq_cycles_ = std::fmod(periodic_irq_cycles_, irq_period);
            if (timer_irq_enabled_) timer_irq_asserted_ = true;
            update_cpu_irq();
        }
    }
}

void KonamiHornetDriver::update_cpu_irq()
{
    m68k_set_irq(interface_.interrupt_asserted() ? 2 : timer_irq_asserted_ ? 1 : 0);
}

uint8_t KonamiHornetDriver::read_memory_8(uint32_t address)
{
    address &= 0xffffff;
    if (address < program_.size()) return program_[address];
    if (address >= 0x100000 && address < 0x100000 + ram_.size())
        return ram_[address - 0x100000];
    if (address >= 0x200000 && address <= 0x200fff) {
        ++io_reads_;
        const uint16_t value = rf5c400_.read16((address - 0x200000) >> 1);
        return (address & 1) ? static_cast<uint8_t>(value) : static_cast<uint8_t>(value >> 8);
    }
    if (address >= 0x300000 && address <= 0x30001f) {
        ++io_reads_;
        if ((address & 1) == 0) return 0xff;
        return interface_.sound_read(static_cast<uint8_t>((address - 0x300000) >> 1));
    }
    return 0xff;
}

uint16_t KonamiHornetDriver::read_memory_16(uint32_t address)
{
    if (address >= 0x200000 && address <= 0x200ffe) {
        ++io_reads_;
        return rf5c400_.read16((address - 0x200000) >> 1);
    }
    return MusashiBus::read_memory_16(address);
}

void KonamiHornetDriver::write_memory_8(uint32_t address, uint8_t data)
{
    address &= 0xffffff;
    if (address >= 0x100000 && address < 0x100000 + ram_.size()) {
        ram_[address - 0x100000] = data;
        return;
    }
    if (address >= 0x200000 && address <= 0x200fff) {
        ++io_writes_;
        const size_t offset = (address - 0x200000) >> 1;
        auto value = rf_registers_[offset];
        value = (address & 1) ? static_cast<uint16_t>((value & 0xff00) | data)
                              : static_cast<uint16_t>((value & 0x00ff) | (data << 8));
        rf_registers_[offset] = value;
        rf5c400_.write16(static_cast<uint32_t>(offset), value);
        ++rf5c400_writes_;
        return;
    }
    if (address >= 0x300000 && address <= 0x30001f && (address & 1)) {
        ++io_writes_;
        interface_.sound_write(static_cast<uint8_t>((address - 0x300000) >> 1), data);
        update_cpu_irq();
        return;
    }
    if (address == 0x500000 || address == 0x500001) {
        ++io_writes_;
        timer_irq_enabled_ = (data & 1) == 0;
        return;
    }
    if (address == 0x600000 || address == 0x600001) {
        ++io_writes_;
        timer_irq_asserted_ = false;
        update_cpu_irq();
    }
}

void KonamiHornetDriver::write_memory_16(uint32_t address, uint16_t data)
{
    if (address >= 0x200000 && address <= 0x200ffe) {
        ++io_writes_;
        const size_t offset = (address - 0x200000) >> 1;
        rf_registers_[offset] = data;
        rf5c400_.write16(static_cast<uint32_t>(offset), data);
        ++rf5c400_writes_;
        return;
    }
    if (address == 0x500000) {
        ++io_writes_;
        timer_irq_enabled_ = (data & 1) == 0;
        return;
    }
    if (address == 0x600000) {
        ++io_writes_;
        timer_irq_asserted_ = false;
        update_cpu_irq();
        return;
    }
    MusashiBus::write_memory_16(address, data);
}

int KonamiHornetDriver::acknowledge_interrupt(int) { return M68K_INT_ACK_AUTOVECTOR; }

int KonamiHornetDriver::render_s16(int16_t* output, int frames)
{
    if (output == nullptr || frames < 0 || !loaded_) return 0;
    constexpr int chunk_frames = 32;
    int rendered = 0;
    while (rendered < frames) {
        const int count = std::min(chunk_frames, frames - rendered);
        const double exact_cycles = static_cast<double>(count) * kCpuClock / sample_rate_
            + cpu_cycle_fraction_;
        const int cycles = static_cast<int>(exact_cycles);
        cpu_cycle_fraction_ = exact_cycles - cycles;
        execute_cycles(cycles);
        rf5c400_.render_s16(output + rendered * 2, count);
        if (pcm_gain_ != 1.0) {
            for (int i = 0; i < count * 2; ++i) {
                const long scaled = std::lround(output[rendered * 2 + i] * pcm_gain_);
                output[rendered * 2 + i] = static_cast<int16_t>(
                    std::clamp(scaled, -32768l, 32767l));
            }
        }
        rendered += count;
    }
    rendered_frames_ += static_cast<uint64_t>(rendered);
    return rendered;
}

int KonamiHornetDriver::render_float(float* output, int frames)
{
    if (output == nullptr || frames < 0) return 0;
    std::vector<int16_t> pcm(static_cast<size_t>(frames) * 2);
    const int rendered = render_s16(pcm.data(), frames);
    for (int i = 0; i < rendered * 2; ++i) output[i] = pcm[i] / 32768.0f;
    return rendered;
}

void KonamiHornetDriver::fill_track_info(const HootEntry& entry, int track_index,
                                         HootTrackInfo& out) const
{
    std::memset(&out, 0, sizeof(out));
    out.track_index = track_index;
    out.sample_rate = sample_rate_;
    out.debug_cpu_cycles = cpu_cycles_;
    out.debug_io_reads = io_reads_;
    out.debug_io_writes = io_writes_;
    out.debug_opn_writes = rf5c400_writes_;
    out.debug_pc = m68k_get_reg(nullptr, M68K_REG_PC);
    out.debug_last_opn_data = selected_code_;
    if (track_index >= 0 && static_cast<size_t>(track_index) < entry.tracks.size())
        utf8::copy_c_string(out.title, entry.tracks[track_index].title);
    utf8::copy_c_string(out.driver, name());
}

void KonamiHornetDriver::fill_visual_state(const HootEntry&, int, HootVisualState& out) const
{
    std::memset(&out, 0, sizeof(out));
    out.abi_version = HOOT_VISUAL_ABI_VERSION;
    out.struct_size = sizeof(out);
    out.sample_rate = static_cast<uint32_t>(sample_rate_);
    out.rendered_frames = rendered_frames_;
    visual::copy(out.architecture, "Konami Hornet");
    visual::copy(out.cpu, "MC68EC000 16 MHz");
    visual::copy(out.device, "RF5C400 32-channel PCM");
    visual::copy(out.driver, name());
    for (size_t index = 0; index < rf5c400_.channel_count(); ++index) {
        auto* channel = visual::add_channel(out, HOOT_VISUAL_CHANNEL_PCM,
                                            static_cast<int>(index),
                                            "RF5C400 " + std::to_string(index + 1));
        if (channel != nullptr) channel->active = rf5c400_.channel_active(index) ? 1 : 0;
    }
    visual::add_register(out, "CMD", selected_code_, 8);
    visual::add_register(out, "PC", m68k_get_reg(nullptr, M68K_REG_PC), 6);
}

bool KonamiHornetDriver::channel_mute_supported(int kind, int index) const
{
    return kind == HOOT_VISUAL_CHANNEL_PCM && index >= 0 && index < 32;
}

bool KonamiHornetDriver::set_channel_muted(int kind, int index, bool muted)
{
    if (!channel_mute_supported(kind, index)) return false;
    rf5c400_.set_channel_muted(static_cast<size_t>(index), muted);
    return true;
}

void KonamiHornetDriver::clear_channel_mutes()
{
    for (size_t i = 0; i < rf5c400_.channel_count(); ++i) rf5c400_.set_channel_muted(i, false);
}

} // namespace hoot
