#include "drivers/pc98_dos_driver.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <map>
#include <sstream>

#include "io/zip_archive.h"

namespace {

template <size_t N>
void copy_c_string(char (&dest)[N], const std::string& source)
{
    static_assert(N > 0, "destination must have room for a terminator");
    const auto count = std::min(source.size(), N - 1);
    std::memcpy(dest, source.data(), count);
    dest[count] = '\0';
}

bool has_negative_offset(uint32_t offset)
{
    return offset == UINT32_MAX;
}

std::string hex_slot(uint32_t slot)
{
    std::ostringstream out;
    out << "0x" << std::hex << slot;
    return out.str();
}

std::string to_lower(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(), ::tolower);
    return s;
}

std::string shell_executable_name(const std::string& shell)
{
    auto name = to_lower(shell);
    if (std::filesystem::path(name).extension().empty()) {
        name += ".com";
    }
    return name;
}

} // namespace

namespace hoot {

Pc98DosDriver::Pc98DosDriver()
{
}

Pc98DosDriver::~Pc98DosDriver()
{
}

HootResult Pc98DosDriver::load(const HootEntry& entry,
                               const std::string& packs_path,
                               int sample_rate,
                               std::string& error)
{
    clear();
    sample_rate_ = sample_rate;

    const auto archive_path = std::filesystem::path(packs_path) / (entry.archive + ".zip");
    ZipArchive archive;
    if (!archive.open(archive_path, error)) {
        return HOOT_ERROR_IO;
    }

    std::map<std::string, LoadedFile> files_by_name;
    std::vector<std::string> shell_names;
    std::vector<HootAssetRef> conin_assets;
    for (const auto& asset : entry.assets) {
        if (asset.type == "device") {
            auto driver_name = asset.path;
            auto data = archive.read(driver_name, error);
            if (!error.empty()) {
                return HOOT_ERROR_IO;
            }
            driver_data_ = std::move(data);
            continue;
        }
        if (asset.type == "shell") {
            shell_command_ = std::vector<uint8_t>(asset.path.begin(), asset.path.end());
            shell_command_.push_back(0);
            shell_names.push_back(asset.path);
            continue;
        }
        if (asset.type == "conin") {
            conin_assets.push_back(asset);
            continue;
        }
        if (asset.type != "file") {
            continue;
        }

        auto data = archive.read(asset.path, error);
        if (!error.empty()) {
            return HOOT_ERROR_IO;
        }

        auto lower_path = to_lower(asset.path);
        files_by_name[lower_path] = LoadedFile{asset.path, data};
        if (lower_path == "pmd.com" || lower_path == "pmd.pmd" || lower_path == "pmd") {
            driver_data_ = data;
            driver_type_ = DriverType::PMD;
        } else if (lower_path == "mmd.sys" || lower_path == "mmd2.sys") {
            driver_data_ = data;
            driver_type_ = DriverType::MMD;
        }

        if (!has_negative_offset(asset.offset)) {
            files_by_slot_[asset.offset] = LoadedFile{asset.path, data};
        }
    }

    for (const auto& asset : conin_assets) {
        if (has_negative_offset(asset.offset)) {
            continue;
        }
        const auto found = files_by_name.find(to_lower(asset.path));
        if (found != files_by_name.end()) {
            files_by_slot_[asset.offset] = found->second;
        }
    }

    if (driver_data_.empty()) {
        auto load_shell = [&](const std::string& shell) {
            const auto found = files_by_name.find(shell_executable_name(shell));
            if (found == files_by_name.end()) {
                return false;
            }
            driver_data_ = found->second.data;
            driver_type_ = DriverType::Shell;
            return true;
        };
        for (const auto& shell : shell_names) {
            if (shell_executable_name(shell) == "cplay98.com" && shell_names.size() > 1) {
                continue;
            }
            if (load_shell(shell)) {
                break;
            }
        }
        for (const auto& shell : shell_names) {
            if (!driver_data_.empty()) {
                break;
            }
            load_shell(shell);
        }
    }

    if (driver_data_.empty()) {
        error = "pc98dos entry did not provide a driver binary or runnable shell program";
        return HOOT_ERROR_NOT_FOUND;
    }

    cpu_ = std::make_unique<X86Cpu>();
    cpu_->set_read_memory_callback([this](uint32_t addr) { return read_memory_byte(addr); });
    cpu_->set_write_memory_callback([this](uint32_t addr, uint8_t d) { write_memory_byte(addr, d); });
    cpu_->set_io_read_callback([this](uint16_t p) { return read_io_port(p); });
    cpu_->set_io_write_callback([this](uint16_t p, uint8_t d) { write_io_port(p, d); });
    cpu_->set_interrupt_callback([this](uint8_t i) { handle_interrupt(i); });

    use_ym2203_ = driver_type_ == DriverType::Shell && entry.driver_name == "pc98dos/opn";
    if (use_ym2203_) {
        ym2203_ = std::make_unique<LibvgmYm2203>();
        if (!ym2203_->initialize(3'993'632, static_cast<uint32_t>(sample_rate_))) {
            error = "failed to initialize YM2203 sound chip";
            return HOOT_ERROR_UNSUPPORTED;
        }
        if (std::getenv("HOOT_PSG_GAIN") == nullptr
            && std::getenv("HOOT_DISABLE_PSG") == nullptr) {
            ym2203_->set_ssg_gain(0.45);
        }
    } else {
        ym2608_ = std::make_unique<LibvgmYm2608>();
        if (!ym2608_->initialize(7'967'264, static_cast<uint32_t>(sample_rate_))) {
            error = "failed to initialize YM2608 sound chip";
            return HOOT_ERROR_UNSUPPORTED;
        }
    }
    trace_opna_ = std::getenv("HOOT_TRACE_PC98_OPN") != nullptr;
    trace_opna_limit_ = 512;
    if (const char* value = std::getenv("HOOT_TRACE_PC98_OPN_LIMIT")) {
        trace_opna_limit_ = static_cast<uint32_t>(std::max(0L, std::strtol(value, nullptr, 0)));
    }
    disable_opn_tl_compat_ = std::getenv("HOOT_DISABLE_OPN_TL_COMPAT") != nullptr;
    if (ym2608_) {
        ym2608_->write(0, 0x29);
        ym2608_->write(1, 0x00);
    }

    if (!setup_memory()) {
        error = "failed to setup PC-98 memory";
        return HOOT_ERROR_UNSUPPORTED;
    }

    setup_interrupt_vectors();
    setup_pit();
    reset_cpu_context();
    if (driver_type_ == DriverType::Shell) {
        install_shell_driver();
    }

    loaded_ = true;
    return HOOT_OK;
}

HootResult Pc98DosDriver::select_track(const HootEntry& entry,
                                       int track_index,
                                       std::string& error)
{
    if (!loaded_) {
        error = "pc98dos driver is not loaded";
        return HOOT_ERROR_NOT_LOADED;
    }
    if (track_index < 0 || static_cast<size_t>(track_index) >= entry.tracks.size()) {
        error = "track index is outside the catalog track list";
        return HOOT_ERROR_INVALID_ARGUMENT;
    }

    selected_track_ = track_index;
    selected_code_ = entry.tracks[track_index].code;

    const uint32_t voice_slot = (selected_code_ >> 16) & 0xff;
    const uint32_t bgm_slot = selected_code_ & 0xffff;
    selected_voice_path_.clear();
    selected_bgm_path_.clear();

    const auto bgm = files_by_slot_.find(bgm_slot);
    if (bgm == files_by_slot_.end()) {
        error = "pc98dos track references missing BGM slot " + hex_slot(bgm_slot);
        return HOOT_ERROR_NOT_FOUND;
    }
    selected_bgm_path_ = bgm->second.path;
    selected_bgm_data_ = bgm->second.data;
    selected_file_offset_ = 0;
    selected_file_open_ = false;
    rendered_frames_ = 0;
    trace_opna_events_ = 0;

    const auto voice = files_by_slot_.find(voice_slot);
    if (voice != files_by_slot_.end()) {
        selected_voice_path_ = voice->second.path;
    }

    playing_ = true;
    timer_frames_until_tick_ = sample_rate_ > 0
        ? static_cast<double>(sample_rate_) / 60.0
        : 735.0;
    if (driver_type_ == DriverType::Shell) {
        auto* mem = cpu_->memory();
        const uint32_t bridge_buffer = (static_cast<uint32_t>(kProgramSegment) << 4) + kBridgeBufferOffset;
        std::fill(mem + bridge_buffer, mem + bridge_buffer + 0x4f, 0);
        const std::string filename = std::filesystem::path(selected_bgm_path_).filename().string();
        const auto copy_size = std::min<size_t>(filename.size(), 0x4e);
        if (copy_size > 0) {
            std::memcpy(mem + bridge_buffer, filename.data(), copy_size);
        }
        call_shell_player_api(0x0900, kProgramSegment, kBridgeBufferOffset);
        call_shell_player_api(static_cast<uint16_t>(voice_slot & 0xff));
    } else {
        reset_cpu_context();
        run_cpu_steps(20000);
    }

    error.clear();
    return HOOT_OK;
}

void Pc98DosDriver::reset()
{
    selected_track_ = 0;
    selected_code_ = 0;
    selected_bgm_path_.clear();
    selected_voice_path_.clear();
    selected_bgm_data_.clear();
    selected_file_offset_ = 0;
    selected_file_open_ = false;
    playing_ = false;
    pit_counter_ = 0;
    timer_frames_until_tick_ = 0.0;
    reset_opn();
    reset_cpu_context();
}

int Pc98DosDriver::render_s16(int16_t* interleaved_stereo, int frames)
{
    if (interleaved_stereo == nullptr || frames < 0) {
        return 0;
    }
    if (!loaded_ || !playing_) {
        std::fill(interleaved_stereo, interleaved_stereo + (frames * 2), int16_t{0});
        return frames;
    }

    if (driver_type_ == DriverType::Shell) {
        const double frames_per_tick = sample_rate_ > 0
            ? static_cast<double>(sample_rate_) / 60.0
            : 735.0;
        int rendered = 0;
        while (rendered < frames) {
            if (timer_frames_until_tick_ <= 0.0) {
                trigger_interrupt_vector(0x14, 200000);
                timer_frames_until_tick_ += frames_per_tick;
            }
            const int chunk = std::min(
                std::max(1, static_cast<int>(std::ceil(timer_frames_until_tick_))),
                frames - rendered);
            run_cpu_steps(chunk * 8);
            render_opn(interleaved_stereo + (rendered * 2), chunk);
            timer_frames_until_tick_ -= static_cast<double>(chunk);
            rendered += chunk;
            rendered_frames_ += static_cast<uint64_t>(chunk);
        }
        return frames;
    }

    run_cpu_steps(frames * 8);

    render_opn(interleaved_stereo, frames);
    rendered_frames_ += static_cast<uint64_t>(frames);
    return frames;
}

int Pc98DosDriver::render_float(float* interleaved_stereo, int frames)
{
    if (interleaved_stereo == nullptr || frames < 0) {
        return 0;
    }
    if (!loaded_ || !playing_) {
        std::fill(interleaved_stereo, interleaved_stereo + (frames * 2), 0.0f);
        return frames;
    }

    mix_buffer_.resize(static_cast<size_t>(frames) * 2);
    render_s16(mix_buffer_.data(), frames);
    for (int i = 0; i < frames * 2; ++i) {
        interleaved_stereo[i] = static_cast<float>(mix_buffer_[i]) / 32768.0f;
    }
    return frames;
}

void Pc98DosDriver::fill_track_info(const HootEntry& entry,
                                     int track_index,
                                     HootTrackInfo& out) const
{
    std::memset(&out, 0, sizeof(out));
    out.track_index = track_index;
    out.sample_rate = sample_rate_;
    out.debug_cpu_cycles = cpu_ && cpu_->unsupported_count() != 0
        ? cpu_->unsupported_count()
        : static_cast<uint32_t>(std::min<uint64_t>(executed_cpu_steps_, UINT32_MAX));
    out.debug_io_reads = static_cast<uint32_t>(files_by_slot_.size());
    out.debug_io_writes = selected_code_;
    out.debug_opn_writes = debug_opna_writes_;
    out.debug_opn_keyons = debug_opna_keyons_;
    out.debug_pc = cpu_ && cpu_->unsupported_count() != 0
        ? ((static_cast<uint32_t>(cpu_->last_unsupported_cs()) << 16) | cpu_->last_unsupported_ip())
        : (cpu_ ? ((static_cast<uint32_t>(cpu_->get_cs()) << 16) | cpu_->get_pc()) : 0);
    out.debug_last_opn_reg = debug_last_opna_reg_;
    out.debug_last_opn_data = debug_last_opna_data_;
    out.debug_port_writes_00 = cpu_ ? cpu_->last_unsupported_opcode() : 0;
    out.debug_port_writes_01 = static_cast<uint64_t>(debug_fm_keyons_by_channel_[0])
        | (static_cast<uint64_t>(debug_fm_keyons_by_channel_[1]) << 16)
        | (static_cast<uint64_t>(debug_fm_keyons_by_channel_[2]) << 32)
        | (static_cast<uint64_t>(debug_fm_keyons_by_channel_[3]) << 48);
    out.debug_port_writes_02 = static_cast<uint64_t>(debug_fm_keyons_by_channel_[4])
        | (static_cast<uint64_t>(debug_fm_keyons_by_channel_[5]) << 32);
    out.debug_port_writes_03 = debug_opna_ssg_writes_;
    out.debug_port_writes_32 = debug_opna_rhythm_writes_;
    uint32_t tl_summary = 0;
    for (uint8_t ch = 0; ch < 3; ++ch) {
        const uint8_t carrier_tl = static_cast<uint8_t>(opna_registers_[0][0x4c + ch] & 0x7f);
        tl_summary |= static_cast<uint32_t>(carrier_tl) << (ch * 8);
    }
    out.debug_port_writes_44 = tl_summary;
    out.debug_port_writes_45 = static_cast<uint64_t>(debug_opna_rhythm_keyons_)
        | (static_cast<uint64_t>(debug_opna_rhythm_keyoffs_) << 24)
        | (static_cast<uint64_t>(debug_last_rhythm_command_) << 48)
        | (static_cast<uint64_t>(opna_registers_[0][0x11] & 0x3f) << 56);
    copy_c_string(out.driver, name());
    if (track_index >= 0 && static_cast<size_t>(track_index) < entry.tracks.size()) {
        copy_c_string(out.title, entry.tracks[track_index].title);
    } else {
        copy_c_string(out.title, entry.title);
    }
}

const char* Pc98DosDriver::name() const
{
    return "pc98dos-v30-opna";
}

void Pc98DosDriver::clear()
{
    files_by_slot_.clear();
    driver_data_.clear();
    shell_command_.clear();
    selected_bgm_path_.clear();
    selected_voice_path_.clear();
    selected_bgm_data_.clear();
    selected_file_offset_ = 0;
    selected_file_open_ = false;
    driver_type_ = DriverType::Unknown;
    cpu_.reset();
    ym2203_.reset();
    ym2608_.reset();
    use_ym2203_ = false;
    int_vector_table_.clear();
    dos_memory_.clear();
    sample_rate_ = 44100;
    selected_track_ = 0;
    selected_code_ = 0;
    loaded_ = false;
    playing_ = false;
    pit_counter_ = 0;
    executed_cpu_steps_ = 0;
    timer_frames_until_tick_ = 0.0;
    current_opna_address_[0] = 0;
    current_opna_address_[1] = 0;
    debug_opna_writes_ = 0;
    debug_opna_keyons_ = 0;
    debug_opna_keyoffs_ = 0;
    debug_last_key_command_ = 0;
    debug_opna_bank1_writes_ = 0;
    debug_opna_ssg_writes_ = 0;
    debug_opna_rhythm_writes_ = 0;
    debug_opna_rhythm_keyons_ = 0;
    debug_opna_rhythm_keyoffs_ = 0;
    debug_last_rhythm_command_ = 0;
    debug_ssg_writes_by_reg_.fill(0);
    debug_last_ssg_regs_.fill(0);
    debug_fm_keyons_by_channel_.fill(0);
    debug_keyon_masks_.fill(0);
    debug_last_opna_reg_ = 0;
    debug_last_opna_data_ = 0;
    trace_opna_ = false;
    trace_opna_events_ = 0;
    trace_opna_limit_ = 0;
    rendered_frames_ = 0;
    for (auto& bank : opna_registers_) {
        bank.fill(0);
    }
    debug_file_opens_ = 0;
    debug_file_open_matches_ = 0;
    debug_file_reads_ = 0;
    debug_last_open_name_ = 0;
}

bool Pc98DosDriver::setup_memory()
{
    if (!cpu_) {
        return false;
    }

    auto* mem = cpu_->memory();
    if (!mem) {
        return false;
    }

    std::memset(mem, 0, 1024 * 1024);
    mem[kIretOffset] = 0xcf;
    mem[kHaltOffset] = 0xf4;

    if (!driver_data_.empty()) {
        const uint32_t entry_linear = (static_cast<uint32_t>(kProgramSegment) << 4) + kDosEntryPoint;
        auto copy_size = std::min(static_cast<size_t>(driver_data_.size()),
                                 static_cast<size_t>(1024 * 1024 - entry_linear));
        std::memcpy(mem + entry_linear, driver_data_.data(), copy_size);
    }

    if (driver_type_ != DriverType::Shell) {
        for (const auto& [slot, file] : files_by_slot_) {
            if (file.data.size() > 1024 * 1024) {
                continue;
            }
            std::memcpy(mem + (slot * 16), file.data.data(), file.data.size());
        }
    }

    return true;
}

void Pc98DosDriver::setup_interrupt_vectors()
{
    int_vector_table_.resize(256 * 4, 0);

    if (cpu_) {
        auto* mem = cpu_->memory();
        if (mem) {
            uint16_t ivt_base = 0x0000;

            mem[ivt_base + 0] = 0x00;
            mem[ivt_base + 1] = 0x10;
            mem[ivt_base + 2] = 0x00;
            mem[ivt_base + 3] = 0x00;

            mem[ivt_base + 0x1C * 4 + 0] = 0x00;
            mem[ivt_base + 0x1C * 4 + 1] = 0x10;
            mem[ivt_base + 0x1C * 4 + 2] = 0x00;
            mem[ivt_base + 0x1C * 4 + 3] = 0x00;

            setup_interrupt_vector(0x18, 0x0000, kIretOffset);
            setup_interrupt_vector(0x21, 0x0000, kIretOffset);
            setup_interrupt_vector(0x04, 0x0000, kIretOffset);
        }
    }
}

void Pc98DosDriver::setup_pit()
{
    const uint32_t pit_clock = 14318184 / 12;
    pit_rate_ = pit_clock / 60;
    pit_target_ = 0x10000 - pit_rate_;
    pit_counter_ = 0;
}

uint8_t Pc98DosDriver::read_memory_byte(uint32_t address)
{
    if (address >= 1024 * 1024) {
        return 0xFF;
    }
    if (!cpu_) {
        return 0xFF;
    }
    return cpu_->memory()[address];
}

void Pc98DosDriver::write_memory_byte(uint32_t address, uint8_t data)
{
    if (address >= 1024 * 1024) {
        return;
    }
    if (!cpu_) {
        return;
    }
    cpu_->memory()[address] = data;
}

uint8_t Pc98DosDriver::read_io_port(uint16_t port)
{
    if (!ym2203_ && !ym2608_) {
        return 0xFF;
    }

    if (port == 0x88 || port == 0x8B || port == 0x188) {
        return read_opn(0);
    }
    if (port == 0x89 || port == 0x8A || port == 0x18A) {
        return opna_registers_[0][current_opna_address_[0]];
    }
    if (port == 0x8C || port == 0x8F || port == 0x18C) {
        return read_opn(2);
    }
    if (port == 0x8D || port == 0x8E || port == 0x18E) {
        return opna_registers_[1][current_opna_address_[1]];
    }

    return 0xFF;
}

void Pc98DosDriver::write_io_port(uint16_t port, uint8_t data)
{
    if (!ym2203_ && !ym2608_) {
        return;
    }

    if (port == 0x88 || port == 0x188) {
        current_opna_address_[0] = data;
        write_opn(0, data);
    } else if (port == 0x89 || port == 0x8A || port == 0x18A) {
        const uint8_t chip_data = (current_opna_address_[0] >= 0xb4 && current_opna_address_[0] <= 0xb6)
            ? static_cast<uint8_t>(data | 0xc0)
            : data;
        ++debug_opna_writes_;
        debug_last_opna_reg_ = current_opna_address_[0];
        debug_last_opna_data_ = chip_data;
        if (current_opna_address_[0] < 0x10) {
            ++debug_opna_ssg_writes_;
            ++debug_ssg_writes_by_reg_[current_opna_address_[0] & 0x0f];
            debug_last_ssg_regs_[current_opna_address_[0] & 0x0f] = chip_data;
            if (trace_opna_ && trace_opna_events_ < trace_opna_limit_) {
                std::fprintf(stderr,
                             "pc98opn frame=%llu ssg r%02x=%02x regs[6,7,8,9,a,b,c,d]=%02x,%02x,%02x,%02x,%02x,%02x,%02x,%02x\n",
                             static_cast<unsigned long long>(rendered_frames_),
                             current_opna_address_[0],
                             chip_data,
                             debug_last_ssg_regs_[0x06],
                             debug_last_ssg_regs_[0x07],
                             debug_last_ssg_regs_[0x08],
                             debug_last_ssg_regs_[0x09],
                             debug_last_ssg_regs_[0x0a],
                             debug_last_ssg_regs_[0x0b],
                             debug_last_ssg_regs_[0x0c],
                             debug_last_ssg_regs_[0x0d]);
                ++trace_opna_events_;
            }
        } else if (current_opna_address_[0] < 0x20) {
            ++debug_opna_rhythm_writes_;
            if (current_opna_address_[0] == 0x10) {
                debug_last_rhythm_command_ = chip_data;
                if ((chip_data & 0x80) != 0) {
                    ++debug_opna_rhythm_keyoffs_;
                } else if ((chip_data & 0x3f) != 0) {
                    ++debug_opna_rhythm_keyons_;
                }
            }
        }
        if (current_opna_address_[0] == 0x28 && (data & 0xf0) != 0) {
            ++debug_opna_keyons_;
            debug_last_key_command_ = data;
            ++debug_keyon_masks_[(data >> 4) & 0x0f];
            uint8_t channel = data & 0x03;
            if (channel != 3) {
                const uint8_t base_channel = channel;
                if ((data & 0x04) != 0) {
                    channel = static_cast<uint8_t>(channel + 3);
                }
                ++debug_fm_keyons_by_channel_[channel];
                if (trace_opna_ && trace_opna_events_ < trace_opna_limit_) {
                    const uint8_t alg = static_cast<uint8_t>(opna_registers_[0][0xb0 + base_channel] & 0x07);
                    const uint8_t feedback = static_cast<uint8_t>((opna_registers_[0][0xb0 + base_channel] >> 3) & 0x07);
                    std::fprintf(stderr,
                                 "pc98opn frame=%llu fm-keyon cmd=%02x ch=%u alg=%u fb=%u tl=%02x,%02x,%02x,%02x ar=%02x,%02x,%02x,%02x rr=%02x,%02x,%02x,%02x\n",
                                 static_cast<unsigned long long>(rendered_frames_),
                                 data,
                                 channel,
                                 alg,
                                 feedback,
                                 opna_registers_[0][0x40 + base_channel],
                                 opna_registers_[0][0x44 + base_channel],
                                 opna_registers_[0][0x48 + base_channel],
                                 opna_registers_[0][0x4c + base_channel],
                                 opna_registers_[0][0x50 + base_channel],
                                 opna_registers_[0][0x54 + base_channel],
                                 opna_registers_[0][0x58 + base_channel],
                                 opna_registers_[0][0x5c + base_channel],
                                 opna_registers_[0][0x80 + base_channel],
                                 opna_registers_[0][0x84 + base_channel],
                                 opna_registers_[0][0x88 + base_channel],
                                 opna_registers_[0][0x8c + base_channel]);
                    ++trace_opna_events_;
                }
            }
        } else if (current_opna_address_[0] == 0x28) {
            ++debug_opna_keyoffs_;
            debug_last_key_command_ = data;
        }
        opna_registers_[0][current_opna_address_[0]] = chip_data;
        write_opn(1, chip_data);
        if ((current_opna_address_[0] >= 0x40 && current_opna_address_[0] <= 0x4e)
            || (current_opna_address_[0] >= 0xb0 && current_opna_address_[0] <= 0xb2)) {
            apply_opn_fm_tl_compat(static_cast<uint8_t>(current_opna_address_[0] & 0x03));
        }
    } else if (port == 0x8C || port == 0x18C) {
        current_opna_address_[1] = data;
        write_opn(2, data);
    } else if (port == 0x8D || port == 0x8E || port == 0x18E) {
        ++debug_opna_writes_;
        ++debug_opna_bank1_writes_;
        debug_last_opna_reg_ = static_cast<uint16_t>(0x100 | current_opna_address_[1]);
        debug_last_opna_data_ = data;
        opna_registers_[1][current_opna_address_[1]] = data;
        write_opn(3, data);
    }

    if (port == kPitIoport) {
    }
}

void Pc98DosDriver::write_opn(uint8_t port, uint8_t data)
{
    if (ym2203_) {
        if ((port & 2) != 0) {
            return;
        }
        ym2203_->write(static_cast<uint8_t>(port & 1), data);
    } else if (ym2608_) {
        ym2608_->write(static_cast<uint8_t>(port & 3), data);
    }
}

uint8_t Pc98DosDriver::read_opn(uint8_t port)
{
    if (ym2203_) {
        if ((port & 2) != 0) {
            return 0xff;
        }
        return ym2203_->read(static_cast<uint8_t>(port & 1));
    }
    if (ym2608_) {
        return ym2608_->read(static_cast<uint8_t>(port & 3));
    }
    return 0xff;
}

void Pc98DosDriver::render_opn(int16_t* interleaved_stereo, int frames)
{
    if (ym2203_) {
        ym2203_->render_s16(interleaved_stereo, frames);
    } else if (ym2608_) {
        ym2608_->render_s16(interleaved_stereo, frames);
    } else if (interleaved_stereo != nullptr && frames > 0) {
        std::fill(interleaved_stereo, interleaved_stereo + (frames * 2), int16_t{0});
    }
}

void Pc98DosDriver::reset_opn()
{
    if (ym2203_) {
        ym2203_->reset();
    }
    if (ym2608_) {
        ym2608_->reset();
        ym2608_->write(0, 0x29);
        ym2608_->write(1, 0x00);
    }
}

void Pc98DosDriver::apply_opn_fm_tl_compat(uint8_t channel)
{
    if (disable_opn_tl_compat_ || (!ym2203_ && !ym2608_) || channel >= 3) {
        return;
    }

    const uint8_t algorithm = opna_registers_[0][0xb0 + channel] & 0x07;
    const uint8_t source_tl = opna_registers_[0][0x44 + channel] & 0x7f;
    if (source_tl >= 0x7f) {
        return;
    }
    bool changed = false;

    const auto lower_tl = [&](uint8_t reg) {
        const uint8_t current = opna_registers_[0][reg] & 0x7f;
        if (current <= source_tl) {
            return;
        }
        if (trace_opna_ && trace_opna_events_ < trace_opna_limit_) {
            std::fprintf(stderr,
                         "pc98opn frame=%llu tl-compat ch=%u alg=%u reg=%02x %02x->%02x\n",
                         static_cast<unsigned long long>(rendered_frames_),
                         channel,
                         algorithm,
                         reg,
                         current,
                         source_tl);
            ++trace_opna_events_;
        }
        opna_registers_[0][reg] = source_tl;
        write_opn(0, reg);
        write_opn(1, source_tl);
        changed = true;
    };

    if (algorithm <= 3) {
        lower_tl(static_cast<uint8_t>(0x4c + channel));
    } else if (algorithm == 4) {
        lower_tl(static_cast<uint8_t>(0x48 + channel));
        lower_tl(static_cast<uint8_t>(0x4c + channel));
    } else if (algorithm == 5 || algorithm == 6) {
        lower_tl(static_cast<uint8_t>(0x44 + channel));
        lower_tl(static_cast<uint8_t>(0x48 + channel));
        lower_tl(static_cast<uint8_t>(0x4c + channel));
    }
    if (changed) {
        write_opn(0, current_opna_address_[0]);
    }
}

void Pc98DosDriver::handle_interrupt(uint8_t int_num)
{
    if (int_num == 0x21) {
        handle_dos_interrupt();
    }
}

void Pc98DosDriver::handle_dos_interrupt()
{
    if (!cpu_) {
        return;
    }

    switch (cpu_->get_ah()) {
    case 0x09:
        cpu_->set_carry(false);
        break;
    case 0x25:
        setup_interrupt_vector(cpu_->get_al(), cpu_->get_ds(), cpu_->get_dx());
        cpu_->set_carry(false);
        break;
    case 0x3d:
        dos_open_file();
        break;
    case 0x3e:
        dos_close_file();
        break;
    case 0x3f:
        dos_read_file();
        break;
    case 0x31:
    case 0x4c:
        cpu_->set_carry(false);
        cpu_->halt();
        break;
    case 0x4a:
        cpu_->set_carry(false);
        break;
    default:
        cpu_->set_carry(false);
        break;
    }
}

std::string Pc98DosDriver::read_dos_string(uint16_t segment, uint16_t offset) const
{
    std::string result;
    if (!cpu_ || !cpu_->memory()) {
        return result;
    }
    uint32_t addr = (static_cast<uint32_t>(segment) << 4) + offset;
    for (size_t i = 0; i < 256 && addr < 1024 * 1024; ++i, ++addr) {
        const char ch = static_cast<char>(cpu_->memory()[addr]);
        if (ch == '\0' || ch == '$' || ch == '\r' || ch == '\n') {
            break;
        }
        result.push_back(ch);
    }
    return result;
}

void Pc98DosDriver::dos_open_file()
{
    if (!cpu_) {
        return;
    }
    ++debug_file_opens_;
    const auto raw_name = read_dos_string(cpu_->get_ds(), cpu_->get_dx());
    debug_last_open_name_ = 0;
    for (size_t i = 0; i < std::min<size_t>(raw_name.size(), 4); ++i) {
        debug_last_open_name_ |= static_cast<uint32_t>(static_cast<uint8_t>(raw_name[i])) << (i * 8);
    }
    const auto requested_path = std::filesystem::path(raw_name).filename();
    const auto selected_path = std::filesystem::path(selected_bgm_path_).filename();
    const auto requested = to_lower(requested_path.string());
    const auto requested_stem = to_lower(requested_path.stem().string());
    const auto selected = to_lower(selected_path.string());
    const auto selected_stem = to_lower(selected_path.stem().string());
    if (!selected_bgm_data_.empty()
        && (requested == selected || (!requested_stem.empty() && requested_stem == selected_stem))) {
        ++debug_file_open_matches_;
        selected_file_offset_ = 0;
        selected_file_open_ = true;
        cpu_->set_ax(selected_file_handle_);
        cpu_->set_carry(false);
        return;
    }
    cpu_->set_ax(0x0002);
    cpu_->set_carry(true);
}

void Pc98DosDriver::dos_read_file()
{
    if (!cpu_) {
        return;
    }
    if (!selected_file_open_ || cpu_->get_bx() != selected_file_handle_) {
        cpu_->set_ax(0x0006);
        cpu_->set_carry(true);
        return;
    }
    ++debug_file_reads_;
    const auto requested = static_cast<size_t>(cpu_->get_cx());
    const auto remaining = selected_bgm_data_.size() > selected_file_offset_
        ? selected_bgm_data_.size() - selected_file_offset_
        : 0;
    const auto count = std::min(requested, remaining);
    const uint32_t dest = (static_cast<uint32_t>(cpu_->get_ds()) << 4) + cpu_->get_dx();
    for (size_t i = 0; i < count && dest + i < 1024 * 1024; ++i) {
        write_memory_byte(dest + static_cast<uint32_t>(i), selected_bgm_data_[selected_file_offset_ + i]);
    }
    selected_file_offset_ += count;
    cpu_->set_ax(static_cast<uint16_t>(count));
    cpu_->set_carry(false);
}

void Pc98DosDriver::dos_close_file()
{
    if (!cpu_) {
        return;
    }
    if (cpu_->get_bx() == selected_file_handle_) {
        selected_file_open_ = false;
    }
    cpu_->set_carry(false);
}

void Pc98DosDriver::pit_timer_tick()
{
    pit_counter_++;
    if (pit_counter_ >= pit_rate_) {
        pit_counter_ = 0;
    }
}

void Pc98DosDriver::reset_cpu_context()
{
    if (!cpu_) {
        return;
    }

    cpu_->reset();
    cpu_->set_cs(kProgramSegment);
    cpu_->set_ds(kProgramSegment);
    cpu_->set_es(kProgramSegment);
    cpu_->set_ss(kProgramSegment);
    cpu_->set_sp(0xFFFE);
    cpu_->set_pc(static_cast<uint16_t>(kDosEntryPoint));
    executed_cpu_steps_ = 0;
}

void Pc98DosDriver::push_cpu_word(uint16_t value)
{
    const uint16_t sp = static_cast<uint16_t>(cpu_->get_sp() - 2);
    cpu_->set_sp(sp);
    const uint32_t addr = (static_cast<uint32_t>(cpu_->get_ss()) << 4) + sp;
    write_memory_byte(addr, static_cast<uint8_t>(value & 0xff));
    write_memory_byte(addr + 1, static_cast<uint8_t>((value >> 8) & 0xff));
}

void Pc98DosDriver::setup_interrupt_vector(uint8_t vector, uint16_t segment, uint16_t offset)
{
    const uint32_t addr = static_cast<uint32_t>(vector) * 4;
    write_memory_byte(addr, static_cast<uint8_t>(offset & 0xff));
    write_memory_byte(addr + 1, static_cast<uint8_t>((offset >> 8) & 0xff));
    write_memory_byte(addr + 2, static_cast<uint8_t>(segment & 0xff));
    write_memory_byte(addr + 3, static_cast<uint8_t>((segment >> 8) & 0xff));
}

void Pc98DosDriver::trigger_interrupt_vector(uint8_t vector, int steps)
{
    if (!cpu_) {
        return;
    }
    const uint32_t addr = static_cast<uint32_t>(vector) * 4;
    const uint16_t offset = static_cast<uint16_t>(read_memory_byte(addr)
        | (read_memory_byte(addr + 1) << 8));
    const uint16_t segment = static_cast<uint16_t>(read_memory_byte(addr + 2)
        | (read_memory_byte(addr + 3) << 8));
    if (offset == 0 && segment == 0) {
        return;
    }
    cpu_->clear_halted();
    cpu_->set_ss(kProgramSegment);
    cpu_->set_sp(0xfffe);
    push_cpu_word(0x0200);
    push_cpu_word(0x0000);
    push_cpu_word(kHaltOffset);
    cpu_->set_cs(segment);
    cpu_->set_pc(offset);
    run_cpu_steps(steps);
    cpu_->clear_halted();
}

void Pc98DosDriver::install_shell_driver()
{
    reset_cpu_context();
    run_cpu_steps(2000000);
    cpu_->clear_halted();
}

void Pc98DosDriver::call_shell_player_api(uint16_t ax, uint16_t ds, uint16_t dx)
{
    if (!cpu_) {
        return;
    }
    cpu_->clear_halted();
    cpu_->set_ax(ax);
    cpu_->set_ds(ds);
    cpu_->set_dx(dx);
    trigger_interrupt_vector(0x7f, 500000);
}

void Pc98DosDriver::run_cpu_steps(int steps)
{
    if (!cpu_ || steps <= 0) {
        return;
    }

    const int executed = cpu_->execute(steps);
    executed_cpu_steps_ += static_cast<uint64_t>(std::max(0, executed));
    pit_timer_tick();
}

} // namespace hoot
