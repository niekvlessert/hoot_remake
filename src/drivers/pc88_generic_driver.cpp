#include "drivers/pc88_generic_driver.h"
#include "core/utf8_util.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iomanip>
#include <string_view>

#include "io/d88_image.h"
#include "core/visual_state_util.h"
#include "io/zip_archive.h"

namespace hoot {
namespace {

template <size_t N>
void copy_c_string(char (&dest)[N], const std::string& source)
{
    hoot::utf8::copy_c_string(dest, source);
}

HootResult load_member(ZipArchive& archive,
                       const HootAssetRef& asset,
                       const std::string& archive_name,
                       const std::filesystem::path& packs_path,
                       std::vector<uint8_t>& out,
                       std::string& error)
{
    if (archive_name != "xak2_98" && (asset.path == "MMD.COM" || asset.path == "MMD2.COM")) {
        D88Image d88;
        std::string d88_error;
        const auto d88_path = packs_path / "Xak - The Tower of Gazzel (Disk 3).d88";
        if (d88.open(d88_path, d88_error)) {
            if (asset.path == "MMD.COM") {
                out = d88.read_data(0, 0x04, 0x02, 0, 0x06, 0x02, 0x01, 0x09, 0x200, d88_error);
                if (out.size() < 0x20 || out[0x10] != 0xf3 || out[0x11] != 0xe5) {
                    out.clear();
                }
            } else {
                out = d88.read_data(0, 0x0b, 0x05, 1, 0x09, 0x02, 0x01, 0x09, 0xc00, d88_error);
                if (out.size() < 4 || out[0] != 0xe5 || out[1] != 0xd5 || out[2] != 0xc5) {
                    out.clear();
                }
            }
            if (!out.empty() && d88_error.empty()) {
                error.clear();
                return HOOT_OK;
            }
        }
    }

    out = archive.read(asset.path, error);
    if (error.empty()) {
        return HOOT_OK;
    }

    if (asset.path.find('_') != std::string::npos) {
        auto alternate = asset.path;
        *std::find(alternate.begin(), alternate.end(), '_') = '/';
        error.clear();
        out = archive.read(alternate, error);
        if (error.empty()) {
            return HOOT_OK;
        }
    }

    struct Fallback {
        std::string_view archive;
        std::string_view member;
    };
    std::vector<Fallback> fallbacks;
    if (asset.path == "PATCH") {
        if (archive_name == "xak2_98") {
            fallbacks.push_back({"cabin98", "PATCH_XAK2_88/PATCH"});
        }
        if (archive_name == "gazzel_98" || archive_name == "fray_98") {
            fallbacks.push_back({"cabin98", "PATCH_GAZZEL_88/PATCH"});
        }
    } else if (asset.path == "MMD.COM") {
        fallbacks.push_back({"xak2_98", "MMD.COM"});
    } else if (asset.path == "MMD2.COM") {
        fallbacks.push_back({"xak2_98", "MMD2.COM"});
    }

    for (const auto& fallback : fallbacks) {
        ZipArchive fallback_archive;
        std::string fallback_error;
        if (!fallback_archive.open(packs_path / (std::string(fallback.archive) + ".zip"), fallback_error)) {
            continue;
        }
        out = fallback_archive.read(fallback.member, fallback_error);
        if (fallback_error.empty()) {
            error.clear();
            return HOOT_OK;
        }
    }

    if (!error.empty()) {
        return HOOT_ERROR_IO;
    }
    return HOOT_OK;
}

} // namespace

HootResult Pc88GenericDriver::load(const HootEntry& entry,
                                    const std::string& packs_path,
                                    int sample_rate,
                                    std::string& error)
{
    clear();
    sample_rate_ = sample_rate;
    options_ = entry.options;
    use_opna_ = entry.driver_name == "pc88/opna";
    init_pc_ = static_cast<uint16_t>(option("init_pc", 0));
    const int baseclock_mhz = option("baseclock", 4);
    cpu_clock_hz_ = (baseclock_mhz >= 1 && baseclock_mhz <= 64)
        ? static_cast<double>(baseclock_mhz) * 1000000.0
        : kDefaultCpuClock;
    io_.fill(0xff);
    open_trace_from_environment();
    trace_event("load-begin", use_opna_ ? 1u : 0u, init_pc_);

    const bool chip_ok = use_opna_
        ? ym2608_.initialize(kOpnaClock, static_cast<uint32_t>(sample_rate_))
        : ym2203_.initialize(kOpmClock, static_cast<uint32_t>(sample_rate_));
    if (!chip_ok) {
        error = use_opna_ ? "unable to initialize libvgm YM2608 core"
                          : "unable to initialize libvgm YM2203 core";
        return HOOT_ERROR_UNSUPPORTED;
    }

    cpu_.set_memory_callbacks(
        [this](uint16_t address) { return read_memory(address); },
        [this](uint16_t address, uint8_t data) { write_memory(address, data); });
    cpu_.set_io_callbacks(
        [this](uint16_t port) { return read_io(port); },
        [this](uint16_t port, uint8_t data) { write_io(port, data); });
    fm_irq_bus_ = 0x08;
    const char* irq_bus_env = std::getenv("HOOT_PC88_IRQ_BUS");
    if (irq_bus_env == nullptr || *irq_bus_env == '\0') irq_bus_env = std::getenv("HOOT_XAK2_IRQ_BUS");
    if (irq_bus_env != nullptr && *irq_bus_env != '\0') {
        fm_irq_bus_ = static_cast<uint8_t>(std::strtoul(irq_bus_env, nullptr, 0));
    }
    cpu_.set_interrupt_bus(fm_irq_bus_);
    cpu_.set_auto_irq_clear(true);

    const auto archive_path = std::filesystem::path(packs_path) / (entry.archive + ".zip");
    ZipArchive archive;
    if (!archive.open(archive_path, error)) {
        return HOOT_ERROR_IO;
    }

    for (const auto& asset : entry.assets) {
        std::vector<uint8_t> data;
        const auto result = load_member(archive, asset, entry.archive, packs_path, data, error);
        if (result != HOOT_OK) {
            return result;
        }

        if (asset.type == "code") {
            if (asset.offset >= ram_.size()) {
                error = "code asset offset is outside PC-88 RAM: " + asset.path;
                return HOOT_ERROR_PARSE;
            }
            const auto count = std::min<size_t>(data.size(), ram_.size() - asset.offset);
            std::copy_n(data.begin(), count, ram_.begin() + asset.offset);
        } else if (asset.type == "bgm") {
            bgm_[asset.offset] = std::move(data);
        } else if (asset.type == "voice") {
            voices_[asset.offset] = std::move(data);
        } else if (asset.type == "adpcm") {
            adpcm_assets_[asset.offset] = std::move(data);
        }
    }

    if (use_opna_) {
        load_opna_adpcm_assets();
    }

    use_periodic_irq_ = option_enabled("use_vrtc");
    if (use_periodic_irq_) {
        // Original Hoot PC-88 hosts use the periodic RTC/VRTC callback as the
        // Z80 RST 02h source. 60 Hz is the common cadence for these catalog
        // patches; FM Timer A/B remain separate RST 08h sources.
        periodic_irq_interval_frames_ = std::max(1, static_cast<int>(std::lround(sample_rate_ / 60.0)));
        periodic_irq_frames_until_next_ = periodic_irq_interval_frames_;
    }

    // Keep an immutable post-load image of the patch/program RAM. Several
    // PC-88 music drivers self-modify their resident code and work buffers;
    // restoring this image for every track makes random access deterministic
    // and prevents one subsong from contaminating the next one.
    initial_ram_ = ram_;
    loaded_ = true;
    restart_guest_for_track();
    trace_event("load-end", cpu_.pc());
    return HOOT_OK;
}

HootResult Pc88GenericDriver::select_track(const HootEntry& entry,
                                            int track_index,
                                            std::string& error)
{
    if (!loaded_) {
        error = "PC-88 driver is not loaded";
        return HOOT_ERROR_NOT_LOADED;
    }
    if (track_index < 0 || static_cast<size_t>(track_index) >= entry.tracks.size()) {
        error = "track index is outside the catalog track list";
        return HOOT_ERROR_INVALID_ARGUMENT;
    }

    selected_track_ = track_index;
    selected_code_ = entry.tracks[track_index].code;
    restart_guest_for_track();
    trace_event("select-track", static_cast<uint32_t>(track_index), selected_code_);

    // Hoot PC-88 track codes are 32-bit commands.  The low byte selects the
    // BGM/voice asset; upper bytes are driver-specific subsong/effect selectors
    // that the patch reads from host ports 02h/03h.  Do not treat those upper
    // command bytes as part of the archive slot number.
    const uint32_t asset_slot = selected_code_ & 0xffu;
    const auto mdata_addr = static_cast<uint16_t>(option("mdata_addr", 0xf800));
    const auto mdata_size = static_cast<size_t>(std::max(0, option("mdata_size", 0)));
    copy_bgm_to_ram(asset_slot, mdata_addr, mdata_size);
    copy_voice_to_ram(asset_slot);

    play_pending_ = true;
    execute_seconds(100000.0 / cpu_clock_hz_);
    trace_event("select-track-done", cpu_.pc(), play_pending_ ? 1 : 0);
    return HOOT_OK;
}

void Pc88GenericDriver::reset()
{
    selected_track_ = 0;
    selected_code_ = 0;
    restart_guest_for_track();
    trace_event("reset", cpu_.pc());
}

int Pc88GenericDriver::render_s16(int16_t* interleaved_stereo, int frames)
{
    if (interleaved_stereo == nullptr || frames < 0) {
        return 0;
    }
    constexpr int kChunkFrames = 256;
    int rendered = 0;
    while (rendered < frames) {
        int todo = std::min(kChunkFrames, frames - rendered);
        schedule_irq_sources(todo);
        execute_seconds(static_cast<double>(todo) / static_cast<double>(sample_rate_));
        cpu_.lower_irq();
        if (use_opna_) {
            ym2608_.render_s16(interleaved_stereo + (rendered * 2), todo);
        } else {
            ym2203_.render_s16(interleaved_stereo + (rendered * 2), todo);
        }
        advance_irq_sources(todo);
        rendered += todo;
    }
    return frames;
}

int Pc88GenericDriver::render_float(float* interleaved_stereo, int frames)
{
    if (interleaved_stereo == nullptr || frames < 0) {
        return 0;
    }
    std::vector<int16_t> temp(static_cast<size_t>(frames) * 2);
    render_s16(temp.data(), frames);
    for (int i = 0; i < frames * 2; ++i) {
        interleaved_stereo[i] = static_cast<float>(temp[i]) / 32768.0f;
    }
    return frames;
}

void Pc88GenericDriver::fill_track_info(const HootEntry& entry,
                                         int track_index,
                                         HootTrackInfo& out) const
{
    std::memset(&out, 0, sizeof(out));
    out.track_index = track_index;
    out.sample_rate = sample_rate_;
    out.debug_cpu_cycles = debug_cpu_cycles_;
    out.debug_io_reads = debug_io_reads_;
    out.debug_io_writes = debug_io_writes_;
    out.debug_opn_writes = debug_opn_writes_;
    out.debug_opn_keyons = debug_opn_keyons_;
    out.debug_pc = cpu_.pc();
    out.debug_last_opn_reg = debug_last_opn_reg_;
    out.debug_last_opn_data = debug_last_opn_data_;
    out.debug_port_writes_00 = debug_port_writes_[0x00];
    out.debug_port_writes_01 = debug_port_writes_[0x01];
    out.debug_port_writes_02 = debug_port_writes_[0x02];
    out.debug_port_writes_03 = debug_port_writes_[0x03];
    out.debug_port_writes_32 = debug_port_writes_[0x32];
    out.debug_port_writes_44 = debug_port_writes_[0x44];
    out.debug_port_writes_45 = debug_port_writes_[0x45];
    copy_c_string(out.driver, name());

    if (track_index >= 0 && static_cast<size_t>(track_index) < entry.tracks.size()) {
        copy_c_string(out.title, entry.tracks[track_index].title);
    } else {
        copy_c_string(out.title, entry.title);
    }
}


void Pc88GenericDriver::fill_visual_state(const HootEntry&, int, HootVisualState& out) const
{
    out.abi_version = HOOT_VISUAL_ABI_VERSION;
    out.struct_size = sizeof(out);
    visual::copy(out.architecture, "PC-88");
    visual::copy(out.cpu, "Z80");
    visual::copy(out.device, use_opna_ ? "YM2608" : "YM2203");
    visual::copy(out.driver, name());

    visual::add_register(out, "AF", cpu_.af());
    visual::add_register(out, "BC", cpu_.bc());
    visual::add_register(out, "DE", cpu_.de());
    visual::add_register(out, "HL", cpu_.hl());
    visual::add_register(out, "IX", cpu_.ix());
    visual::add_register(out, "IY", cpu_.iy());
    visual::add_register(out, "PC", cpu_.pc());
    visual::add_register(out, "SP", cpu_.sp());
    visual::add_register(out, "I", cpu_.i(), 2);
    visual::add_register(out, "R", cpu_.r(), 2);
    visual::add_register(out, "IM", cpu_.interrupt_mode(), 2);

    const int fm_channels = use_opna_ ? 6 : 3;
    for (int ch = 0; ch < fm_channels; ++ch) {
        const int bank = ch >= 3 ? 1 : 0;
        const int local = ch % 3;
        auto* v = visual::add_channel(out, HOOT_VISUAL_CHANNEL_FM, ch,
            std::string(use_opna_ ? "YM2608 FM#" : "YM2203 FM#") + std::to_string(ch));
        if (!v) break;
        const uint8_t lo = opn_registers_[bank][0xa0 + local];
        const uint8_t hi = opn_registers_[bank][0xa4 + local];
        const uint16_t fnum = static_cast<uint16_t>(lo | ((hi & 0x07) << 8));
        const uint8_t block = static_cast<uint8_t>((hi >> 3) & 0x07);
        v->active = opn_key_on_[ch] ? 1 : 0;
        v->midi_note = visual::opn_fnum_to_midi(fnum, block, use_opna_ ? 7987200.0 : 3993600.0);
        v->volume = visual::inverse_tl_volume(opn_registers_[bank][0x4c + local]);
        v->pan = use_opna_ ? visual::opn_pan(opn_registers_[bank][0xb4 + local]) : 0;
        v->level = v->active ? static_cast<float>(v->volume) / 127.0f : 0.0f;
    }

    const uint8_t mixer = opn_registers_[0][7];
    for (int ch = 0; ch < 3; ++ch) {
        auto* v = visual::add_channel(out, HOOT_VISUAL_CHANNEL_SSG, ch,
            std::string(use_opna_ ? "YM2608 SSG#" : "YM2203 SSG#") + std::to_string(ch));
        if (!v) break;
        const uint16_t period = static_cast<uint16_t>(opn_registers_[0][ch * 2]
            | ((opn_registers_[0][ch * 2 + 1] & 0x0f) << 8));
        const int vol4 = opn_registers_[0][8 + ch] & 0x0f;
        const bool tone_enabled = (mixer & (1u << ch)) == 0;
        const bool noise_enabled = (mixer & (1u << (ch + 3))) == 0;
        const double psg_clock = (use_opna_ ? 7987200.0 : 3993600.0) / 4.0;
        v->active = (vol4 != 0 && (tone_enabled || noise_enabled)) ? 1 : 0;
        v->midi_note = (period != 0 && tone_enabled)
            ? visual::frequency_to_midi(psg_clock / (16.0 * period)) : -1;
        v->volume = std::clamp(vol4 * 8 + (vol4 ? 7 : 0), 0, 127);
        v->level = v->active ? static_cast<float>(v->volume) / 127.0f : 0.0f;
    }

    if (use_opna_) {
        auto* adpcm = visual::add_channel(out, HOOT_VISUAL_CHANNEL_ADPCM, 0, "YM2608 ADPCM#0");
        if (adpcm) {
            adpcm->active = (opn_registers_[1][0x00] & 0x80) != 0;
            adpcm->volume = opn_registers_[1][0x0b] & 0x7f;
            adpcm->pan = visual::opn_pan(opn_registers_[1][0x01]);
            adpcm->level = adpcm->active ? static_cast<float>(adpcm->volume) / 127.0f : 0.0f;
        }
        const uint8_t rhythm_total = static_cast<uint8_t>(0x3f - (opn_registers_[0][0x11] & 0x3f));
        for (int ch = 0; ch < 6; ++ch) {
            auto* r = visual::add_channel(out, HOOT_VISUAL_CHANNEL_RHYTHM, ch,
                "YM2608 RHYTHM#" + std::to_string(ch));
            if (!r) break;
            const uint8_t reg = opn_registers_[0][0x18 + ch];
            r->volume = std::clamp(static_cast<int>(rhythm_total) * 2, 0, 127);
            r->pan = visual::opn_pan(reg);
            // Rhythm key-ons are momentary commands; keep them visually active
            // when the channel has non-zero level rather than fabricating pitch.
            r->active = r->volume != 0;
            r->level = r->active ? static_cast<float>(r->volume) / 127.0f : 0.0f;
        }
    }

    // The generic PC-88 host does not know a driver-defined workspace pointer.
    // Do not invent a fixed F000h window merely to fill the classic hex panel;
    // the GUI now reports that no live workspace is published instead.
    out.driver_work_base = 0;
    out.driver_work_size = 0;
}

bool Pc88GenericDriver::channel_mute_supported(int kind, int index) const
{
    if (kind == HOOT_VISUAL_CHANNEL_FM) return index >= 0 && index < (use_opna_ ? 6 : 3);
    if (kind == HOOT_VISUAL_CHANNEL_SSG) return index >= 0 && index < 3;
    if (use_opna_ && kind == HOOT_VISUAL_CHANNEL_ADPCM) return index == 0;
    if (use_opna_ && kind == HOOT_VISUAL_CHANNEL_RHYTHM) return index >= 0 && index < 6;
    return false;
}

bool Pc88GenericDriver::set_channel_muted(int kind, int index, bool muted)
{
    if (!channel_mute_supported(kind, index)) return false;
    auto set_bit = [muted](uint32_t& mask, int bit) {
        if (muted) mask |= (1u << bit); else mask &= ~(1u << bit);
    };
    if (kind == HOOT_VISUAL_CHANNEL_FM) set_bit(ui_opn_mute_mask_, index);
    else if (kind == HOOT_VISUAL_CHANNEL_SSG) set_bit(ui_ssg_mute_mask_, index);
    else if (kind == HOOT_VISUAL_CHANNEL_RHYTHM) set_bit(ui_opn_mute_mask_, 6 + index);
    else if (kind == HOOT_VISUAL_CHANNEL_ADPCM) set_bit(ui_opn_mute_mask_, 12);
    if (use_opna_) {
        ym2608_.set_mute_mask(ui_opn_mute_mask_);
        ym2608_.set_ssg_mute_mask(ui_ssg_mute_mask_);
    } else {
        ym2203_.set_mute_mask(ui_opn_mute_mask_);
        ym2203_.set_ssg_mute_mask(ui_ssg_mute_mask_);
    }
    return true;
}

void Pc88GenericDriver::clear_channel_mutes()
{
    ui_opn_mute_mask_ = 0;
    ui_ssg_mute_mask_ = 0;
    if (use_opna_) { ym2608_.set_mute_mask(0); ym2608_.set_ssg_mute_mask(0); }
    else { ym2203_.set_mute_mask(0); ym2203_.set_ssg_mute_mask(0); }
}

const char* Pc88GenericDriver::name() const
{
    return use_opna_ ? "pc88-generic-opna" : "pc88-generic-opn";
}

void Pc88GenericDriver::clear()
{
    ram_.fill(0);
    initial_ram_.fill(0);
    io_.fill(0xff);
    bgm_.clear();
    voices_.clear();
    adpcm_assets_.clear();
    options_.clear();
    selected_track_ = 0;
    selected_code_ = 0;
    init_pc_ = 0;
    play_pending_ = false;
    sample_rate_ = 44100;
    cpu_clock_hz_ = kDefaultCpuClock;
    loaded_ = false;
    use_opna_ = false;
    ui_opn_mute_mask_ = 0;
    ui_ssg_mute_mask_ = 0;
    use_periodic_irq_ = false;
    periodic_irq_interval_frames_ = 0;
    periodic_irq_frames_until_next_ = 0;
    debug_cpu_cycles_ = 0;
    debug_io_reads_ = 0;
    debug_io_writes_ = 0;
    debug_opn_writes_ = 0;
    debug_opn_keyons_ = 0;
    debug_last_opn_reg_ = 0;
    debug_last_opn_data_ = 0;
    current_fm_reg_.fill(0);
    for (auto& bank : opn_registers_) bank.fill(0);
    opn_key_on_.fill(false);
    fm_timer_a_ = 0;
    fm_timer_b_ = 0;
    fm_mode_ = 0;
    fm_prescaler_sel_ = 2;
    fm_irq_bus_ = 0x08;
    fm_irq_interval_frames_ = 0;
    fm_irq_frames_until_next_ = 0;
    debug_port_writes_.fill(0);
    if (trace_.is_open()) {
        trace_.close();
    }
    trace_limit_ = 0;
    trace_events_ = 0;
    trace_limit_reported_ = false;
}

void Pc88GenericDriver::restart_guest_for_track()
{
    if (!loaded_) return;

    ram_ = initial_ram_;
    io_.fill(0xff);
    play_pending_ = false;
    current_fm_reg_.fill(0);
    fm_timer_a_ = 0;
    fm_timer_b_ = 0;
    fm_mode_ = 0;
    fm_prescaler_sel_ = 2;
    fm_irq_interval_frames_ = 0;
    fm_irq_frames_until_next_ = 0;
    periodic_irq_frames_until_next_ = periodic_irq_interval_frames_;

    if (use_opna_) {
        ym2608_.reset();
        load_opna_adpcm_assets();
    } else {
        ym2203_.reset();
    }

    cpu_.reset(init_pc_);
    trace_event("cpu-reset", cpu_.pc());
    execute_seconds(500.0 / cpu_clock_hz_);
}

uint8_t Pc88GenericDriver::read_memory(uint16_t address) const
{
    return ram_[address];
}

void Pc88GenericDriver::write_memory(uint16_t address, uint8_t data)
{
    ram_[address] = data;
    if (should_trace_memory(address)) {
        trace_event("mem-write", address, data);
    }
}

uint8_t Pc88GenericDriver::read_io(uint16_t port)
{
    ++debug_io_reads_;
    const auto low = static_cast<uint8_t>(port & 0xff);
    switch (low) {
    case 0x00:
        if (play_pending_) {
            play_pending_ = false;
            io_[low] = 1;
        } else {
            io_[low] = 0;
        }
        break;
    case 0x01: io_[low] = static_cast<uint8_t>(selected_code_ & 0xff); break;
    case 0x02: io_[low] = static_cast<uint8_t>((selected_code_ >> 8) & 0xff); break;
    case 0x03: io_[low] = static_cast<uint8_t>((selected_code_ >> 16) & 0xff); break;
    // Legacy Hoot PC-88 patches (notably Falcom/Ys) read the fourth
    // extended track-code byte from port 80h.  Returning the reset value
    // FFh here selects an invalid subsong even for ordinary 00xxxxxx codes.
    case 0x80: io_[low] = static_cast<uint8_t>((selected_code_ >> 24) & 0xff); break;
    case 0x44:
    case 0x45:
        io_[low] = chip_read(low - 0x44);
        break;
    case 0x46:
    case 0x47:
        io_[low] = use_opna_ ? chip_read(low - 0x44) : 0xff;
        break;
    case 0xa8: io_[low] = chip_read(0); break;
    case 0xa9: io_[low] = chip_read(1); break;
    case 0xaa: io_[low] = io_[0x32]; break;
    case 0xac: io_[low] = use_opna_ ? chip_read(2) : 0xff; break;
    case 0xad: io_[low] = use_opna_ ? chip_read(3) : 0xff; break;
    default:
        break;
    }
    // Port 00h is polled continuously while the patch is idle. Recording
    // every zero-valued poll drowns the useful driver/chip events in traces,
    // so only log that port when it actually returns a command byte.
    if ((low == 0x00 && io_[low] != 0)
        || (low >= 0x01 && low <= 0x03) || low == 0x80
        || low == 0x32 || (low >= 0x44 && low <= 0x47)
        || (low >= 0xa8 && low <= 0xad)) {
        trace_event("io-read", low, io_[low]);
    }
    return io_[low];
}

void Pc88GenericDriver::write_io(uint16_t port, uint8_t data)
{
    ++debug_io_writes_;
    const auto low = static_cast<uint8_t>(port & 0xff);
    ++debug_port_writes_[low];
    switch (low) {
    case 0x00:
    case 0x01:
    case 0x11:
        // Some original Hoot patches request a different resident BGM slot
        // after startup. Preserve that ABI, but use the catalog destination
        // instead of the old Microcabin-only hard-coded C000h address.
        trace_event("bgm-slot-write", low, data);
        copy_bgm_to_ram(data, static_cast<uint16_t>(option("mdata_addr", 0xc000)),
                        static_cast<size_t>(std::max(0, option("mdata_size", 0))));
        break;
    case 0x44: chip_write(0, data); break;
    case 0x45: chip_write(1, data); break;
    case 0x46: if (use_opna_) chip_write(2, data); break;
    case 0x47: if (use_opna_) chip_write(3, data); break;
    case 0xa8: chip_write(0, data); break;
    case 0xa9: chip_write(1, data); break;
    case 0xaa: io_[0x32] = data; break;
    case 0xac: if (use_opna_) chip_write(2, data); break;
    case 0xad: if (use_opna_) chip_write(3, data); break;
    default:
        if (low == 0x32 || low == 0xe4 || low <= 0x03) {
            trace_event("io-write", low, data);
        }
        break;
    }
    io_[low] = data;
}

void Pc88GenericDriver::execute_seconds(double seconds)
{
    if (seconds <= 0.0) {
        return;
    }
    const auto cycles = static_cast<uint32_t>(cpu_clock_hz_ * seconds);
    debug_cpu_cycles_ += cycles;
    cpu_.execute(cycles);
}

void Pc88GenericDriver::update_fm_timer(uint8_t reg, uint8_t data)
{
    switch (reg) {
    case 0x24:
        fm_timer_a_ = static_cast<uint16_t>((fm_timer_a_ & 0x0003u) | (static_cast<uint16_t>(data) << 2));
        refresh_fm_irq_interval();
        break;
    case 0x25:
        fm_timer_a_ = static_cast<uint16_t>((fm_timer_a_ & 0x03fcu) | (data & 0x03));
        refresh_fm_irq_interval();
        break;
    case 0x26:
        fm_timer_b_ = data;
        refresh_fm_irq_interval();
        break;
    case 0x27:
        fm_mode_ = data;
        refresh_fm_irq_interval();
        break;
    case 0x2d:
        fm_prescaler_sel_ |= 0x02;
        refresh_fm_irq_interval();
        break;
    case 0x2e:
        fm_prescaler_sel_ |= 0x01;
        refresh_fm_irq_interval();
        break;
    case 0x2f:
        fm_prescaler_sel_ = 0;
        refresh_fm_irq_interval();
        break;
    default:
        break;
    }
}

void Pc88GenericDriver::refresh_fm_irq_interval()
{
    static constexpr int kTimerPrescalerBySel[4] = {24, 24, 72, 36};
    const int prescaler = kTimerPrescalerBySel[fm_prescaler_sel_ & 0x03];
    const double clock = use_opna_ ? static_cast<double>(kOpnaClock) : static_cast<double>(kOpmClock);
    double best = 0.0;

    // Bits 2/0 control Timer A IRQ enable/start; bits 3/1 do the same for B.
    if ((fm_mode_ & 0x05) == 0x05) {
        const uint16_t timer_a = static_cast<uint16_t>(fm_timer_a_ & 0x03ffu);
        const double seconds = static_cast<double>(1024 - timer_a) * static_cast<double>(prescaler) / clock;
        if (seconds > 0.0) best = seconds;
    }
    if ((fm_mode_ & 0x0a) == 0x0a && fm_timer_b_ != 0xff) {
        const double seconds = static_cast<double>((256 - fm_timer_b_) << 4)
            * static_cast<double>(prescaler) / clock;
        if (seconds > 0.0 && (best == 0.0 || seconds < best)) best = seconds;
    }

    if (best == 0.0) {
        fm_irq_interval_frames_ = 0;
        fm_irq_frames_until_next_ = 0;
        return;
    }
    fm_irq_interval_frames_ = std::max(1, static_cast<int>(std::lround(best * sample_rate_)));
    if (fm_irq_frames_until_next_ <= 0) {
        fm_irq_frames_until_next_ = fm_irq_interval_frames_;
    }
}

void Pc88GenericDriver::schedule_irq_sources(int& todo)
{
    const bool fm_due = fm_irq_interval_frames_ > 0 && fm_irq_frames_until_next_ <= 0;
    const bool periodic_due = use_periodic_irq_ && periodic_irq_frames_until_next_ <= 0;
    if (fm_due && (io_[0x32] & 0x80) == 0) {
        raise_irq(fm_irq_bus_, "fm");
        fm_irq_frames_until_next_ += fm_irq_interval_frames_;
    } else if (periodic_due) {
        raise_irq(0x02, "rtc-vrtc");
        periodic_irq_frames_until_next_ += periodic_irq_interval_frames_;
    }

    if (fm_irq_interval_frames_ > 0) {
        todo = std::min(todo, std::max(1, fm_irq_frames_until_next_));
    }
    if (use_periodic_irq_) {
        todo = std::min(todo, std::max(1, periodic_irq_frames_until_next_));
    }
}

void Pc88GenericDriver::advance_irq_sources(int frames)
{
    if (fm_irq_interval_frames_ > 0) {
        fm_irq_frames_until_next_ -= frames;
    }
    if (use_periodic_irq_) {
        periodic_irq_frames_until_next_ -= frames;
    }
}

void Pc88GenericDriver::raise_irq(uint8_t bus, const char* source)
{
    cpu_.set_interrupt_bus(bus);
    const uint32_t irq_state = static_cast<uint32_t>(io_[0x32])
        | (static_cast<uint32_t>(cpu_.interrupt_enable()) << 8)
        | (static_cast<uint32_t>(cpu_.interrupt_mode()) << 16)
        | (static_cast<uint32_t>(cpu_.interrupt_page()) << 24);
    trace_event("irq-raise", bus, irq_state);
    cpu_.raise_irq();
    (void)source;
}

void Pc88GenericDriver::copy_bgm_to_ram(uint32_t slot, uint16_t destination, size_t limit)
{
    const auto it = bgm_.find(slot);
    if (it == bgm_.end() || destination >= ram_.size()) {
        return;
    }
    size_t count = it->second.size();
    if (limit != 0) count = std::min(count, limit);
    count = std::min(count, ram_.size() - destination);
    std::copy_n(it->second.begin(), count, ram_.begin() + destination);
    trace_event("bgm-copy", slot, static_cast<uint32_t>(count));
}

void Pc88GenericDriver::copy_voice_to_ram(uint32_t slot)
{
    auto it = voices_.find(slot);
    if (it == voices_.end()) {
        // A large fraction of Hoot PC-88 packs use one shared voice bank at
        // slot 0. Use it when no track-specific bank exists.
        it = voices_.find(0);
    }
    if (it == voices_.end()) return;
    const auto destination = static_cast<uint16_t>(option("vdata_addr", 0xf400));
    if (destination >= ram_.size()) return;
    size_t count = it->second.size();
    const int configured = option("vdata_size", 0);
    if (configured > 0) count = std::min(count, static_cast<size_t>(configured));
    count = std::min(count, ram_.size() - destination);
    std::copy_n(it->second.begin(), count, ram_.begin() + destination);
    trace_event("voice-copy", slot, static_cast<uint32_t>(count));
}

uint8_t Pc88GenericDriver::chip_read(uint8_t port)
{
    return use_opna_ ? ym2608_.read(port) : ym2203_.read(port & 1u);
}

void Pc88GenericDriver::chip_write(uint8_t port, uint8_t data)
{
    ++debug_opn_writes_;
    const uint8_t bank = static_cast<uint8_t>((port >> 1) & 1u);
    if ((port & 1u) == 0) {
        current_fm_reg_[bank] = data;
        trace_event("opn-addr", port, data);
    } else {
        const uint8_t reg = current_fm_reg_[bank];
        opn_registers_[bank][reg] = data;
        if (bank == 0 && reg == 0x28) {
            uint8_t channel = static_cast<uint8_t>(data & 0x03);
            if (channel != 3) {
                if ((data & 0x04) != 0) channel = static_cast<uint8_t>(channel + 3);
                if (channel < opn_key_on_.size()) opn_key_on_[channel] = (data & 0xf0) != 0;
            }
        }
        debug_last_opn_reg_ = reg;
        debug_last_opn_data_ = data;
        if (bank == 0 && reg == 0x28 && (data & 0xf0) != 0) {
            ++debug_opn_keyons_;
        }
        trace_event("opn-data", (static_cast<uint32_t>(bank) << 8) | reg, data);
        if (bank == 0) update_fm_timer(reg, data);
    }
    if (use_opna_) ym2608_.write(port, data);
    else ym2203_.write(port & 1u, data);
}

void Pc88GenericDriver::load_opna_adpcm_assets()
{
    if (!use_opna_ || adpcm_assets_.empty()) return;
    uint64_t required = 0;
    for (const auto& [offset, data] : adpcm_assets_) {
        required = std::max<uint64_t>(required, static_cast<uint64_t>(offset) + data.size());
    }
    if (required == 0) return;
    required = std::min<uint64_t>(required, 16u * 1024u * 1024u);
    ym2608_.allocate_adpcm_memory(static_cast<uint32_t>(required));
    for (const auto& [offset, data] : adpcm_assets_) {
        if (offset >= required) continue;
        const auto count = static_cast<uint32_t>(std::min<uint64_t>(data.size(), required - offset));
        ym2608_.write_adpcm_memory(offset, data.data(), count);
    }
    trace_event("adpcm-load", static_cast<uint32_t>(required), static_cast<uint32_t>(adpcm_assets_.size()));
}

void Pc88GenericDriver::open_trace_from_environment()
{
    const char* trace_path = std::getenv("HOOT_PC88_TRACE");
    if (trace_path == nullptr || trace_path[0] == '\0') {
        trace_path = std::getenv("HOOT_XAK2_TRACE"); // backward compatibility
    }
    if (trace_path == nullptr || trace_path[0] == '\0') return;
    trace_.open(trace_path, std::ios::out | std::ios::trunc);
    const char* limit = std::getenv("HOOT_PC88_TRACE_LIMIT");
    if (limit == nullptr || limit[0] == '\0') limit = std::getenv("HOOT_XAK2_TRACE_LIMIT");
    if (limit != nullptr && limit[0] != '\0') trace_limit_ = std::strtoull(limit, nullptr, 10);
    if (trace_.is_open()) {
        trace_ << "# hoot generic PC-88 trace\n";
        trace_ << "# columns: event cycles pc a b\n";
    }
}

void Pc88GenericDriver::trace_event(const char* kind, uint32_t a, uint32_t b)
{
    if (!trace_.is_open()) return;
    if (trace_limit_ != 0 && trace_events_ >= trace_limit_) {
        if (!trace_limit_reported_) {
            trace_ << "# trace limit reached: " << trace_limit_ << "\n";
            trace_limit_reported_ = true;
        }
        return;
    }
    ++trace_events_;
    trace_ << kind
           << " cycles=" << debug_cpu_cycles_
           << " pc=0x" << std::hex << std::setw(4) << std::setfill('0') << cpu_.pc()
           << " a=0x" << std::setw(4) << a
           << " b=0x" << std::setw(4) << b
           << std::dec << std::setfill(' ') << "\n";
}

bool Pc88GenericDriver::should_trace_memory(uint16_t address) const
{
    const auto mdata = static_cast<uint16_t>(option("mdata_addr", 0xf800));
    const auto vdata = static_cast<uint16_t>(option("vdata_addr", 0xf400));
    const uint32_t a = address;
    return address == 0x00ff
        || (a >= mdata && a < std::min<uint32_t>(0x10000u, static_cast<uint32_t>(mdata) + 0x800u))
        || (a >= vdata && a < std::min<uint32_t>(0x10000u, static_cast<uint32_t>(vdata) + 0x800u));
}

int Pc88GenericDriver::option(const char* name, int fallback) const
{
    const auto it = options_.find(name);
    return it == options_.end() ? fallback : it->second;
}

bool Pc88GenericDriver::option_enabled(const char* name) const
{
    return option(name, 0) != 0;
}

} // namespace hoot
