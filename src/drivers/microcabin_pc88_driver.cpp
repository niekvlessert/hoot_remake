#include "drivers/microcabin_pc88_driver.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iomanip>
#include <string_view>

#include "io/d88_image.h"
#include "io/zip_archive.h"

namespace hoot {
namespace {

template <size_t N>
void copy_c_string(char (&dest)[N], const std::string& source)
{
    static_assert(N > 0, "destination must have room for a terminator");
    const auto count = std::min(source.size(), N - 1);
    std::memcpy(dest, source.data(), count);
    dest[count] = '\0';
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

HootResult MicrocabinPc88Driver::load(const HootEntry& entry,
                                      const std::string& packs_path,
                                      int sample_rate,
                                      std::string& error)
{
    clear();
    sample_rate_ = sample_rate;
    options_ = entry.options;
    bgm_.resize(kBgmSlotSize * kMaxSlots);
    io_.fill(0xff);
    open_trace_from_environment();
    trace_event("load-begin");

    if (!ym2203_.initialize(3993600, static_cast<uint32_t>(sample_rate_))) {
        error = "unable to initialize libvgm YM2203 core";
        return HOOT_ERROR_UNSUPPORTED;
    }
    cpu_.set_memory_callbacks(
        [this](uint16_t address) { return read_memory(address); },
        [this](uint16_t address, uint8_t data) { write_memory(address, data); });
    cpu_.set_io_callbacks(
        [this](uint16_t port) { return read_io(port); },
        [this](uint16_t port, uint8_t data) { write_io(port, data); });
    uint8_t interrupt_bus = 0x08;
    if (const char* value = std::getenv("HOOT_XAK2_IRQ_BUS")) {
        interrupt_bus = static_cast<uint8_t>(std::strtoul(value, nullptr, 0));
    }
    cpu_.set_interrupt_bus(interrupt_bus);

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
                error = "code asset offset is outside RAM: " + asset.path;
                return HOOT_ERROR_PARSE;
            }
            const auto count = std::min<size_t>(data.size(), ram_.size() - asset.offset);
            std::copy_n(data.begin(), count, ram_.begin() + asset.offset);
        } else if (asset.type == "bgm") {
            if (asset.offset >= kMaxSlots) {
                error = "BGM slot is outside supported range: " + asset.path;
                return HOOT_ERROR_PARSE;
            }
            const auto base = asset.offset * kBgmSlotSize;
            const auto count = std::min<size_t>(data.size(), kBgmSlotSize);
            std::copy_n(data.begin(), count, bgm_.begin() + static_cast<std::ptrdiff_t>(base));
            bgm_present_[asset.offset] = 1;
            bgm_size_[asset.offset] = static_cast<uint32_t>(count);
        } else if (asset.type == "voice") {
            voices_[asset.offset] = std::move(data);
        }
    }

    loaded_ = true;
    cpu_.reset(0x0000);
    cpu_.set_auto_irq_clear(true);
    trace_event("cpu-reset", cpu_.pc());
    execute_seconds(500.0 / 4000000.0);
    trace_event("load-end", cpu_.pc());
    return HOOT_OK;
}

HootResult MicrocabinPc88Driver::select_track(const HootEntry& entry,
                                              int track_index,
                                              std::string& error)
{
    if (!loaded_) {
        error = "Microcabin driver is not loaded";
        return HOOT_ERROR_NOT_LOADED;
    }
    if (track_index < 0 || static_cast<size_t>(track_index) >= entry.tracks.size()) {
        error = "track index is outside the catalog track list";
        return HOOT_ERROR_INVALID_ARGUMENT;
    }

    selected_track_ = track_index;
    selected_code_ = entry.tracks[track_index].code;
    trace_event("select-track", static_cast<uint32_t>(track_index), selected_code_);
    if (selected_code_ < kMaxSlots && bgm_present_[selected_code_]) {
        const auto mdata_addr = static_cast<uint16_t>(options_.count("mdata_addr") ? options_["mdata_addr"] : 0xf800);
        const auto mdata_size = static_cast<size_t>(options_.count("mdata_size") ? options_["mdata_size"] : kBgmSlotSize);
        const auto source_offset = static_cast<size_t>(selected_code_) * kBgmSlotSize;
        const auto count = std::min({mdata_size, static_cast<size_t>(bgm_size_[selected_code_]), ram_.size() - mdata_addr});
        std::copy_n(
            bgm_.begin() + static_cast<std::ptrdiff_t>(source_offset),
            count,
            ram_.begin() + mdata_addr);
    }
    const auto voice = voices_.find(selected_code_);
    if (voice != voices_.end()) {
        const auto vdata_addr = static_cast<uint16_t>(options_.count("vdata_addr") ? options_["vdata_addr"] : 0xf400);
        const auto count = std::min(voice->second.size(), ram_.size() - vdata_addr);
        std::copy_n(voice->second.begin(), count, ram_.begin() + vdata_addr);
    }
    play_pending_ = true;
    execute_seconds(100000.0 / 4000000.0);
    trace_event("select-track-done", cpu_.pc(), play_pending_ ? 1 : 0);
    return HOOT_OK;
}

void MicrocabinPc88Driver::reset()
{
    selected_track_ = 0;
    selected_code_ = 0;
    play_pending_ = false;
    cpu_.reset(0x0000);
    ym2203_.reset();
    trace_event("reset", cpu_.pc());
}

int MicrocabinPc88Driver::render_s16(int16_t* interleaved_stereo, int frames)
{
    if (interleaved_stereo == nullptr || frames < 0) {
        return 0;
    }
    constexpr int kChunkFrames = 256;
    int rendered = 0;
    while (rendered < frames) {
        int todo = std::min(kChunkFrames, frames - rendered);
        if (irq_interval_frames_ > 0) {
            if (irq_frames_until_next_ <= 0 && (io_[0x32] & 0x80) == 0) {
                trace_event("irq-raise", cpu_.pc(), io_[0x32]);
                cpu_.raise_irq();
                irq_frames_until_next_ += irq_interval_frames_;
            }
            todo = std::min(todo, std::max(1, irq_frames_until_next_));
        } else if ((io_[0x32] & 0x80) == 0) {
            trace_event("irq-raise", cpu_.pc(), io_[0x32]);
            cpu_.raise_irq();
        }
        execute_seconds(static_cast<double>(todo) / static_cast<double>(sample_rate_));
        cpu_.lower_irq();
        ym2203_.render_s16(interleaved_stereo + (rendered * 2), todo);
        if (irq_interval_frames_ > 0) {
            irq_frames_until_next_ -= todo;
        }
        rendered += todo;
    }
    return frames;
}

int MicrocabinPc88Driver::render_float(float* interleaved_stereo, int frames)
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

void MicrocabinPc88Driver::fill_track_info(const HootEntry& entry,
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

const char* MicrocabinPc88Driver::name() const
{
    return "microcabin-pc88-opn";
}

void MicrocabinPc88Driver::clear()
{
    ram_.fill(0);
    io_.fill(0xff);
    bgm_present_.fill(0);
    bgm_size_.fill(0);
    bgm_.clear();
    voices_.clear();
    options_.clear();
    selected_track_ = 0;
    selected_code_ = 0;
    play_pending_ = false;
    sample_rate_ = 44100;
    loaded_ = false;
    debug_cpu_cycles_ = 0;
    debug_io_reads_ = 0;
    debug_io_writes_ = 0;
    debug_opn_writes_ = 0;
    debug_opn_keyons_ = 0;
    debug_last_opn_reg_ = 0;
    debug_last_opn_data_ = 0;
    current_opn_reg_ = 0;
    opn_timer_b_ = 0;
    opn_mode_ = 0;
    opn_prescaler_sel_ = 2;
    irq_interval_frames_ = 0;
    irq_frames_until_next_ = 0;
    debug_port_writes_.fill(0);
    if (trace_.is_open()) {
        trace_.close();
    }
    trace_limit_ = 0;
    trace_events_ = 0;
    trace_limit_reported_ = false;
}

uint8_t MicrocabinPc88Driver::read_memory(uint16_t address) const
{
    return ram_[address];
}

void MicrocabinPc88Driver::write_memory(uint16_t address, uint8_t data)
{
    ram_[address] = data;
    if (should_trace_memory(address)) {
        trace_event("mem-write", address, data);
    }
}

uint8_t MicrocabinPc88Driver::read_io(uint16_t port)
{
    ++debug_io_reads_;
    const auto low_port = static_cast<uint8_t>(port & 0xff);
    switch (low_port) {
    case 0x00:
        if (play_pending_) {
            play_pending_ = false;
            io_[low_port] = 1;
        } else {
            io_[low_port] = 0;
        }
        trace_event("io-read", low_port, io_[low_port]);
        return io_[low_port];
    case 0x01:
        io_[low_port] = static_cast<uint8_t>(selected_code_ & 0xff);
        trace_event("io-read", low_port, io_[low_port]);
        return io_[low_port];
    case 0x44:
    case 0x45:
        io_[low_port] = ym2203_.read(low_port);
        trace_event("io-read", low_port, io_[low_port]);
        return io_[low_port];
    default:
        if (low_port == 0x32 || low_port == 0xe4 || low_port <= 0x03) {
            trace_event("io-read", low_port, io_[low_port]);
        }
        return io_[low_port];
    }
}

void MicrocabinPc88Driver::write_io(uint16_t port, uint8_t data)
{
    ++debug_io_writes_;
    const auto low_port = static_cast<uint8_t>(port & 0xff);
    ++debug_port_writes_[low_port];
    switch (low_port) {
    case 0x00:
    case 0x01:
    case 0x11:
        trace_event("bgm-slot-write", low_port, data);
        if (data < kMaxSlots && bgm_present_[data]) {
            std::copy_n(
                bgm_.begin() + static_cast<std::ptrdiff_t>(data * kBgmSlotSize),
                std::min<size_t>(kBgmSlotSize, bgm_size_[data]),
                ram_.begin() + 0xc000);
        }
        break;
    case 0x44:
    case 0x45:
        ++debug_opn_writes_;
        if ((low_port & 1) == 0) {
            current_opn_reg_ = data;
            trace_event("opn-addr", low_port, data);
        } else {
            debug_last_opn_reg_ = current_opn_reg_;
            debug_last_opn_data_ = data;
            if (current_opn_reg_ == 0x28 && (data & 0xf0) != 0) {
                ++debug_opn_keyons_;
            }
            trace_event("opn-data", current_opn_reg_, data);
            update_opn_timer(current_opn_reg_, data);
        }
        ym2203_.write(low_port, data);
        break;
    default:
        if (low_port == 0x32 || low_port == 0xe4 || low_port <= 0x03) {
            trace_event("io-write", low_port, data);
        }
        break;
    }
    io_[low_port] = data;
}

void MicrocabinPc88Driver::execute_seconds(double seconds)
{
    if (seconds <= 0.0) {
        return;
    }
    const auto cycles = static_cast<uint32_t>(4000000.0 * seconds);
    debug_cpu_cycles_ += cycles;
    cpu_.execute(cycles);
}

void MicrocabinPc88Driver::update_opn_timer(uint8_t reg, uint8_t data)
{
    switch (reg) {
    case 0x26:
        opn_timer_b_ = data;
        refresh_irq_interval();
        break;
    case 0x27:
        opn_mode_ = data;
        refresh_irq_interval();
        break;
    case 0x2d:
        opn_prescaler_sel_ |= 0x02;
        refresh_irq_interval();
        break;
    case 0x2e:
        opn_prescaler_sel_ |= 0x01;
        refresh_irq_interval();
        break;
    case 0x2f:
        opn_prescaler_sel_ = 0;
        refresh_irq_interval();
        break;
    default:
        break;
    }
}

void MicrocabinPc88Driver::refresh_irq_interval()
{
    if ((opn_mode_ & 0x0a) != 0x0a || opn_timer_b_ == 0xff) {
        irq_interval_frames_ = 0;
        irq_frames_until_next_ = 0;
        return;
    }

    static constexpr int kTimerPrescalerBySel[4] = {24, 24, 72, 36};
    const int timer_prescaler = kTimerPrescalerBySel[opn_prescaler_sel_ & 0x03];
    const double timer_seconds =
        static_cast<double>((256 - opn_timer_b_) << 4) * static_cast<double>(timer_prescaler) / 3993600.0;
    irq_interval_frames_ = std::max(1, static_cast<int>(std::lround(timer_seconds * sample_rate_)));
    if (irq_frames_until_next_ <= 0) {
        irq_frames_until_next_ = irq_interval_frames_;
    }
}

void MicrocabinPc88Driver::open_trace_from_environment()
{
    const char* trace_path = std::getenv("HOOT_XAK2_TRACE");
    if (trace_path == nullptr || trace_path[0] == '\0') {
        return;
    }

    trace_.open(trace_path, std::ios::out | std::ios::trunc);
    const char* limit = std::getenv("HOOT_XAK2_TRACE_LIMIT");
    if (limit != nullptr && limit[0] != '\0') {
        trace_limit_ = std::strtoull(limit, nullptr, 10);
    }
    if (trace_.is_open()) {
        trace_ << "# hoot xak2 trace\n";
        trace_ << "# columns: event cycles pc a b\n";
    }
}

void MicrocabinPc88Driver::trace_event(const char* kind, uint32_t a, uint32_t b)
{
    if (!trace_.is_open()) {
        return;
    }
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

bool MicrocabinPc88Driver::should_trace_memory(uint16_t address) const
{
    if (address == 0x965c || address == 0x965d || address == 0x00ff) {
        return true;
    }
    if (address >= 0xf400 && address < 0xfc00) {
        return true;
    }
    if (address >= 0xf800) {
        return true;
    }
    if (address >= 0xe560 && address < 0xf000) {
        return true;
    }
    return false;
}

} // namespace hoot
