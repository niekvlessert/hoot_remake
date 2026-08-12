#include "drivers/sharp_x1_generic_driver.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>

#include "core/utf8_util.h"
#include "core/visual_state_util.h"
#include "io/zip_archive.h"

namespace hoot {
namespace {

template <size_t N>
void copy_c_string(char (&dest)[N], const std::string& source)
{
    utf8::copy_c_string(dest, source);
}

int16_t mixed_sample(double value)
{
    return static_cast<int16_t>(std::clamp<long>(std::lround(value), -32768, 32767));
}

} // namespace

HootResult SharpX1GenericDriver::load(const HootEntry& entry,
                                      const std::string& packs_path,
                                      int sample_rate,
                                      std::string& error)
{
    clear();
    sample_rate_ = sample_rate;
    options_ = entry.options;
    init_pc_ = static_cast<uint16_t>(option("init_pc", 0xc000));
    if (entry.driver_name == "x1/opm") board_ = Board::Opm;
    else if (entry.driver_name == "x1/opmx2") board_ = Board::DualOpm;
    else if (entry.driver_name == "x1/opn") board_ = Board::Opn;
    else if (entry.driver_name == "x1/psg") board_ = Board::Psg;
    else {
        error = "not a Sharp X1 catalogue driver: " + entry.driver_name;
        return HOOT_ERROR_UNSUPPORTED;
    }

    if (!psg_.initialize(kPsgHostClock, static_cast<uint32_t>(sample_rate_))) {
        error = "unable to initialize Sharp X1 AY-3-8910 core";
        return HOOT_ERROR_UNSUPPORTED;
    }
    // LibvgmYm2203 gives us its accurately resampled AY core. Silence the
    // unused FM portion and apply the catalogue's original 8.8 mix value.
    psg_.set_mute_mask(0x07);
    psg_.set_ssg_gain(static_cast<double>(option("psg_mix", 0xef)) / 256.0);

    const bool mame_opm = option("mame_opm", 1) != 0;
    if ((board_ == Board::Opm || board_ == Board::DualOpm)
        && !opm1_.initialize(kOpmClock, static_cast<uint32_t>(sample_rate_), !mame_opm)) {
        error = "unable to initialize Sharp X1 YM2151 core";
        return HOOT_ERROR_UNSUPPORTED;
    }
    if (board_ == Board::DualOpm
        && !opm2_.initialize(kOpmClock, static_cast<uint32_t>(sample_rate_), !mame_opm)) {
        error = "unable to initialize second Sharp X1 YM2151 core";
        return HOOT_ERROR_UNSUPPORTED;
    }
    if (board_ == Board::Opn
        && !opn_.initialize(kOpnClock, static_cast<uint32_t>(sample_rate_))) {
        error = "unable to initialize Sharp X1 YM2203 core";
        return HOOT_ERROR_UNSUPPORTED;
    }

    cpu_.set_memory_callbacks(
        [this](uint16_t address) { return ram_[address]; },
        [this](uint16_t address, uint8_t data) { ram_[address] = data; });
    cpu_.set_io_callbacks(
        [this](uint16_t port) { return read_io(port); },
        [this](uint16_t port, uint8_t data) { write_io(port, data); });
    cpu_.set_interrupt_bus(0xff);
    cpu_.set_auto_irq_clear(true);

    ZipArchive archive;
    if (!archive.open(std::filesystem::path(packs_path) / (entry.archive + ".zip"), error)) {
        return HOOT_ERROR_IO;
    }
    for (const auto& asset : entry.assets) {
        auto data = archive.read(asset.path, error);
        if (!error.empty()) return HOOT_ERROR_IO;
        if (asset.type == "code") {
            if (asset.offset >= ram_.size()) {
                error = "code asset offset is outside Sharp X1 RAM: " + asset.path;
                return HOOT_ERROR_PARSE;
            }
            const size_t count = std::min<size_t>(data.size(), ram_.size() - asset.offset);
            std::copy_n(data.begin(), count, ram_.begin() + asset.offset);
        } else if (asset.type == "data") {
            if (asset.offset >= io_.size()) {
                error = "data asset offset is outside Sharp X1 I/O memory: " + asset.path;
                return HOOT_ERROR_PARSE;
            }
            const size_t count = std::min<size_t>(data.size(), io_.size() - asset.offset);
            std::copy_n(data.begin(), count, io_.begin() + asset.offset);
        } else if (asset.type == "bgm") {
            const int file_limit = option("mfile_size", 0);
            if (file_limit > 0 && data.size() > static_cast<size_t>(file_limit)) data.resize(file_limit);
            bgm_[asset.offset] = std::move(data);
        } else if (asset.type == "voice") {
            const int file_limit = option("vfile_size", 0);
            if (file_limit > 0 && data.size() > static_cast<size_t>(file_limit)) data.resize(file_limit);
            voices_[asset.offset] = std::move(data);
        }
    }

    initial_ram_ = ram_;
    timer_interval_frames_ = std::max(1.0, sample_rate_ * (256.0 * 18.0) / kCpuClock);
    vsync_interval_frames_ = std::max(1.0, sample_rate_ / 60.0);
    const int ctc = option("ctc0", option("ctc3", 0));
    ctc_interval_frames_ = ctc > 0
        ? std::max(1.0, sample_rate_ * (256.0 * static_cast<double>(ctc)) / kCpuClock)
        : 0.0;
    const int psgpcm = option("use_psgpcm", 0);
    psgpcm_slice_frames_ = psgpcm > 0 ? std::clamp(psgpcm, 1, 256) : 0;
    loaded_ = true;
    restart_guest();
    return HOOT_OK;
}

HootResult SharpX1GenericDriver::select_track(const HootEntry& entry,
                                               int track_index,
                                               std::string& error)
{
    if (!loaded_) {
        error = "Sharp X1 driver is not loaded";
        return HOOT_ERROR_NOT_LOADED;
    }
    if (track_index < 0 || static_cast<size_t>(track_index) >= entry.tracks.size()) {
        error = "track index is outside the catalog track list";
        return HOOT_ERROR_INVALID_ARGUMENT;
    }
    selected_track_ = track_index;
    selected_code_ = entry.tracks[track_index].code;
    restart_guest();
    copy_bgm(selected_code_ & 0xffu, static_cast<uint16_t>(option("mdata_addr", 0x5000)));
    copy_voice(selected_code_ & 0xffu, static_cast<uint16_t>(option("vdata_addr", 0x8000)));

    // Original Hoot's X1 ABI publishes the play request at C010h. Newer
    // patches consume all four catalogue code bytes, while old patches only
    // inspect C011h, so populate both forms without changing the old flag.
    ram_[0xc010] = 0x01;
    for (int byte = 0; byte < 4; ++byte) {
        ram_[0xc011 + byte] = static_cast<uint8_t>(selected_code_ >> (byte * 8));
    }
    cpu_.execute(100000);
    debug_cpu_cycles_ += 100000;
    return HOOT_OK;
}

void SharpX1GenericDriver::reset()
{
    selected_track_ = 0;
    selected_code_ = 0;
    restart_guest();
}

int SharpX1GenericDriver::render_s16(int16_t* out, int frames)
{
    if (out == nullptr || frames < 0) return 0;
    constexpr int kChunk = 256;
    int done = 0;
    while (done < frames) {
        int count = std::min(kChunk, frames - done);
        if (psgpcm_slice_frames_ > 0) count = std::min(count, psgpcm_slice_frames_);
        execute_frames(count);

        psg_audio_.resize(static_cast<size_t>(count) * 2);
        psg_.render_s16(psg_audio_.data(), count);
        if (board_ == Board::Opm || board_ == Board::DualOpm) {
            opm1_audio_.resize(static_cast<size_t>(count) * 2);
            opm1_.render_s16(opm1_audio_.data(), count);
        }
        if (board_ == Board::DualOpm) {
            opm2_audio_.resize(static_cast<size_t>(count) * 2);
            opm2_.render_s16(opm2_audio_.data(), count);
        }
        if (board_ == Board::Opn) {
            opn_audio_.resize(static_cast<size_t>(count) * 2);
            opn_.render_s16(opn_audio_.data(), count);
        }

        const double opm_gain = static_cast<double>(option("opm_mix", 0x100)) / 256.0;
        const double opm2_gain = static_cast<double>(option("opm2_mix", option("opm_mix", 0x100))) / 256.0;
        for (int i = 0; i < count * 2; ++i) {
            double sample = psg_audio_[i];
            if (board_ == Board::Opm || board_ == Board::DualOpm) sample += opm1_audio_[i] * opm_gain;
            if (board_ == Board::DualOpm) sample += opm2_audio_[i] * opm2_gain;
            if (board_ == Board::Opn) sample += opn_audio_[i] * opm_gain;
            out[(done * 2) + i] = mixed_sample(sample);
        }
        advance_interrupts(count);
        done += count;
    }
    return frames;
}

int SharpX1GenericDriver::render_float(float* out, int frames)
{
    if (out == nullptr || frames < 0) return 0;
    std::vector<int16_t> temp(static_cast<size_t>(frames) * 2);
    render_s16(temp.data(), frames);
    for (int i = 0; i < frames * 2; ++i) out[i] = static_cast<float>(temp[i]) / 32768.0f;
    return frames;
}

void SharpX1GenericDriver::fill_track_info(const HootEntry& entry, int track_index,
                                            HootTrackInfo& out) const
{
    std::memset(&out, 0, sizeof(out));
    out.track_index = track_index;
    out.sample_rate = sample_rate_;
    out.debug_cpu_cycles = debug_cpu_cycles_;
    out.debug_io_reads = debug_io_reads_;
    out.debug_io_writes = debug_io_writes_;
    out.debug_opn_writes = debug_chip_writes_;
    out.debug_opn_keyons = debug_keyons_;
    out.debug_pc = cpu_.pc();
    out.debug_last_opn_reg = debug_last_reg_;
    out.debug_last_opn_data = debug_last_data_;
    copy_c_string(out.driver, name());
    if (track_index >= 0 && static_cast<size_t>(track_index) < entry.tracks.size())
        copy_c_string(out.title, entry.tracks[track_index].title);
    else copy_c_string(out.title, entry.title);
}

void SharpX1GenericDriver::fill_visual_state(const HootEntry&, int, HootVisualState& out) const
{
    out.abi_version = HOOT_VISUAL_ABI_VERSION;
    out.struct_size = sizeof(out);
    visual::copy(out.architecture, "Sharp X1");
    visual::copy(out.cpu, "Z80 (4 MHz)");
    const char* device = board_ == Board::Psg ? "AY-3-8910"
        : board_ == Board::Opm ? "AY-3-8910 + YM2151"
        : board_ == Board::DualOpm ? "AY-3-8910 + 2x YM2151"
        : "AY-3-8910 + YM2203";
    visual::copy(out.device, device);
    visual::copy(out.driver, name());
    visual::add_register(out, "AF", cpu_.af());
    visual::add_register(out, "BC", cpu_.bc());
    visual::add_register(out, "DE", cpu_.de());
    visual::add_register(out, "HL", cpu_.hl());
    visual::add_register(out, "IX", cpu_.ix());
    visual::add_register(out, "IY", cpu_.iy());
    visual::add_register(out, "PC", cpu_.pc());
    visual::add_register(out, "SP", cpu_.sp());

    const uint8_t mixer = psg_registers_[7];
    for (int ch = 0; ch < 3; ++ch) {
        auto* v = visual::add_channel(out, HOOT_VISUAL_CHANNEL_SSG, ch,
                                      "AY-3-8910 #" + std::to_string(ch));
        if (!v) break;
        const uint16_t period = static_cast<uint16_t>(psg_registers_[ch * 2]
            | ((psg_registers_[ch * 2 + 1] & 0x0f) << 8));
        const int volume = psg_registers_[8 + ch] & 0x0f;
        const bool tone = (mixer & (1u << ch)) == 0;
        const bool noise = (mixer & (1u << (ch + 3))) == 0;
        v->active = volume != 0 && (tone || noise);
        v->midi_note = period && tone ? visual::frequency_to_midi(2000000.0 / (16.0 * period)) : -1;
        v->volume = volume * 8 + (volume ? 7 : 0);
        v->level = v->active ? static_cast<float>(v->volume) / 127.0f : 0.0f;
    }
    const int opm_count = board_ == Board::DualOpm ? 2 : board_ == Board::Opm ? 1 : 0;
    for (int chip = 0; chip < opm_count; ++chip) {
        for (int ch = 0; ch < 8; ++ch) {
            const int index = chip * 8 + ch;
            auto* v = visual::add_channel(out, HOOT_VISUAL_CHANNEL_FM, index,
                "YM2151 #" + std::to_string(index));
            if (!v) break;
            v->active = fm_key_on_[index];
            v->midi_note = visual::ym2151_kc_to_midi(opm_registers_[chip][0x28 + ch]);
            v->pan = visual::ym2151_pan(opm_registers_[chip][0x20 + ch]);
            v->volume = visual::inverse_tl_volume(opm_registers_[chip][0x60 + ch]);
            v->level = v->active ? static_cast<float>(v->volume) / 127.0f : 0.0f;
        }
    }
    if (board_ == Board::Opn) {
        for (int ch = 0; ch < 3; ++ch) {
            auto* v = visual::add_channel(out, HOOT_VISUAL_CHANNEL_FM, ch,
                                          "YM2203 FM#" + std::to_string(ch));
            if (!v) break;
            const uint16_t fnum = static_cast<uint16_t>(opn_registers_[0xa0 + ch]
                | ((opn_registers_[0xa4 + ch] & 7) << 8));
            v->active = fm_key_on_[ch];
            v->midi_note = visual::opn_fnum_to_midi(fnum, (opn_registers_[0xa4 + ch] >> 3) & 7,
                                                     kOpnClock);
            v->volume = visual::inverse_tl_volume(opn_registers_[0x4c + ch]);
            v->level = v->active ? static_cast<float>(v->volume) / 127.0f : 0.0f;
        }
    }
}

bool SharpX1GenericDriver::channel_mute_supported(int kind, int index) const
{
    if (kind == HOOT_VISUAL_CHANNEL_SSG) return index >= 0 && index < 3;
    if (kind != HOOT_VISUAL_CHANNEL_FM) return false;
    if (board_ == Board::Opm) return index >= 0 && index < 8;
    if (board_ == Board::DualOpm) return index >= 0 && index < 16;
    if (board_ == Board::Opn) return index >= 0 && index < 3;
    return false;
}

bool SharpX1GenericDriver::set_channel_muted(int kind, int index, bool muted)
{
    if (!channel_mute_supported(kind, index)) return false;
    auto bit = [muted](uint32_t& mask, int n) {
        if (muted) mask |= 1u << n; else mask &= ~(1u << n);
    };
    if (kind == HOOT_VISUAL_CHANNEL_SSG) {
        bit(psg_mute_mask_, index);
        psg_.set_ssg_mute_mask(psg_mute_mask_);
    } else if (board_ == Board::Opn) {
        bit(opn_mute_mask_, index);
        opn_.set_mute_mask(opn_mute_mask_);
    } else {
        const int chip = index / 8;
        bit(opm_mute_mask_[chip], index % 8);
        (chip == 0 ? opm1_ : opm2_).set_mute_mask(opm_mute_mask_[chip]);
    }
    return true;
}

void SharpX1GenericDriver::clear_channel_mutes()
{
    psg_mute_mask_ = 0;
    opm_mute_mask_.fill(0);
    opn_mute_mask_ = 0;
    psg_.set_ssg_mute_mask(0);
    opm1_.set_mute_mask(0);
    opm2_.set_mute_mask(0);
    opn_.set_mute_mask(0);
}

const char* SharpX1GenericDriver::name() const
{
    switch (board_) {
    case Board::Psg: return "sharp-x1-psg";
    case Board::Opm: return "sharp-x1-opm";
    case Board::DualOpm: return "sharp-x1-dual-opm";
    case Board::Opn: return "sharp-x1-opn";
    }
    return "sharp-x1";
}

void SharpX1GenericDriver::clear()
{
    ram_.fill(0);
    initial_ram_.fill(0);
    io_.fill(0xff);
    psg_registers_.fill(0);
    for (auto& regs : opm_registers_) regs.fill(0);
    opn_registers_.fill(0);
    fm_key_on_.fill(false);
    bgm_.clear();
    voices_.clear();
    options_.clear();
    loaded_ = false;
    board_ = Board::Psg;
    selected_track_ = 0;
    selected_code_ = 0;
    init_pc_ = 0xc000;
    psg_latch_ = 0;
    opm_latch_.fill(0);
    opn_latch_ = 0;
    psg_mute_mask_ = 0;
    opm_mute_mask_.fill(0);
    opn_mute_mask_ = 0;
    debug_cpu_cycles_ = debug_io_reads_ = debug_io_writes_ = 0;
    debug_chip_writes_ = debug_keyons_ = 0;
    debug_last_reg_ = debug_last_data_ = 0;
}

void SharpX1GenericDriver::restart_guest()
{
    if (!loaded_) return;
    ram_ = initial_ram_;
    psg_.reset();
    if (board_ == Board::Opm || board_ == Board::DualOpm) opm1_.reset();
    if (board_ == Board::DualOpm) opm2_.reset();
    if (board_ == Board::Opn) opn_.reset();
    psg_registers_.fill(0);
    for (auto& regs : opm_registers_) regs.fill(0);
    opn_registers_.fill(0);
    fm_key_on_.fill(false);
    timer_frames_left_ = timer_interval_frames_;
    ctc_frames_left_ = ctc_interval_frames_;
    vsync_frames_left_ = vsync_interval_frames_;
    cpu_.reset(init_pc_);
    cpu_.execute(500);
    debug_cpu_cycles_ += 500;
}

uint8_t SharpX1GenericDriver::read_io(uint16_t port)
{
    ++debug_io_reads_;
    if (port == 0x0700 || port == 0x0701) {
        if (board_ == Board::Opn) return opn_.read(port & 1);
        if (board_ == Board::Opm || board_ == Board::DualOpm) return opm1_.read(port & 1);
    }
    if ((port == 0x0702 || port == 0x0703 || port == 0x0704 || port == 0x0705)
        && board_ == Board::DualOpm)
        return opm2_.read(port & 1);
    if (port == 0x1b00 || port == 0x1c00) return psg_.read(1);
    if (port == 0x1a01) return 0x00;
    return io_[port];
}

void SharpX1GenericDriver::write_io(uint16_t port, uint8_t data)
{
    ++debug_io_writes_;
    io_[port] = data;
    if (port == 0x0000) {
        copy_bgm(data, static_cast<uint16_t>(option("mdata_addr", 0x5000)));
        ram_[0xc012] = 0xff;
    } else if (port == 0x0700 || port == 0x0701) {
        if (board_ == Board::Opn) {
            opn_.write(port & 1, data);
            record_opn_write(port & 1, data);
        } else if (board_ == Board::Opm || board_ == Board::DualOpm) {
            opm1_.write(port & 1, data);
            record_opm_write(0, port & 1, data);
        }
    } else if ((port == 0x0702 || port == 0x0703 || port == 0x0704 || port == 0x0705)
               && board_ == Board::DualOpm) {
        opm2_.write(port & 1, data);
        record_opm_write(1, port & 1, data);
    } else if (port == 0x1b00 || port == 0x1c00) {
        const uint8_t chip_port = port == 0x1b00 ? 0 : 1;
        psg_.write(chip_port, data);
        record_psg_write(chip_port, data);
    }
}

void SharpX1GenericDriver::execute_frames(int frames)
{
    const auto cycles = static_cast<uint32_t>(std::ceil(kCpuClock * frames / sample_rate_));
    cpu_.execute(cycles);
    debug_cpu_cycles_ += cycles;
}

void SharpX1GenericDriver::advance_interrupts(int frames)
{
    auto advance = [this, frames](double interval, double& left, uint8_t line, uint8_t bus) {
        if (interval <= 0.0) return;
        left -= frames;
        while (left <= 0.0) {
            pulse_irq(line, bus);
            left += interval;
        }
    };
    advance(timer_interval_frames_, timer_frames_left_, 0, 0xff);
    advance(ctc_interval_frames_, ctc_frames_left_, option("ctc0", 0) ? 0 : 3, 0xff);
    advance(vsync_interval_frames_, vsync_frames_left_, 6, 0xff);
}

void SharpX1GenericDriver::pulse_irq(uint8_t line, uint8_t bus)
{
    cpu_.set_interrupt_bus(bus);
    cpu_.raise_irq(line);
    cpu_.execute(1);
    ++debug_cpu_cycles_;
    cpu_.lower_irq(line);
}

void SharpX1GenericDriver::copy_bgm(uint32_t slot, uint16_t destination)
{
    const auto found = bgm_.find(slot);
    if (found == bgm_.end()) return;
    size_t count = found->second.size();
    const int limit = option("mdata_size", 0);
    if (limit > 0) count = std::min(count, static_cast<size_t>(limit));
    count = std::min(count, ram_.size() - destination);
    std::copy_n(found->second.begin(), count, ram_.begin() + destination);
    // Old MUCOM-X1 patches fetch their bank from host I/O memory at 5000h.
    const size_t io_count = std::min(found->second.size(), io_.size() - 0x5000);
    std::copy_n(found->second.begin(), io_count, io_.begin() + 0x5000);
}

void SharpX1GenericDriver::copy_voice(uint32_t slot, uint16_t destination)
{
    auto found = voices_.find(slot);
    if (found == voices_.end() && voices_.size() == 1) found = voices_.begin();
    if (found == voices_.end()) return;
    size_t count = found->second.size();
    const int limit = option("vdata_size", 0);
    if (limit > 0) count = std::min(count, static_cast<size_t>(limit));
    count = std::min(count, ram_.size() - destination);
    std::copy_n(found->second.begin(), count, ram_.begin() + destination);
}

int SharpX1GenericDriver::option(const char* name, int fallback) const
{
    const auto found = options_.find(name);
    return found == options_.end() ? fallback : found->second;
}

void SharpX1GenericDriver::record_psg_write(uint8_t port, uint8_t data)
{
    ++debug_chip_writes_;
    if (port == 0) psg_latch_ = data & 0x0f;
    else psg_registers_[psg_latch_] = data;
    debug_last_reg_ = psg_latch_;
    debug_last_data_ = data;
}

void SharpX1GenericDriver::record_opm_write(int chip, uint8_t port, uint8_t data)
{
    ++debug_chip_writes_;
    if (port == 0) opm_latch_[chip] = data;
    else {
        const uint8_t reg = opm_latch_[chip];
        opm_registers_[chip][reg] = data;
        if (reg == 0x08) {
            fm_key_on_[chip * 8 + (data & 7)] = (data & 0x78) != 0;
            if (data & 0x78) ++debug_keyons_;
        }
        debug_last_reg_ = reg;
        debug_last_data_ = data;
    }
}

void SharpX1GenericDriver::record_opn_write(uint8_t port, uint8_t data)
{
    ++debug_chip_writes_;
    if (port == 0) opn_latch_ = data;
    else {
        opn_registers_[opn_latch_] = data;
        if (opn_latch_ == 0x28) {
            fm_key_on_[data & 3] = (data & 0xf0) != 0;
            if (data & 0xf0) ++debug_keyons_;
        }
        debug_last_reg_ = opn_latch_;
        debug_last_data_ = data;
    }
}

} // namespace hoot
