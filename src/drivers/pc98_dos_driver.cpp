#include "drivers/pc98_dos_driver.h"

#include <algorithm>
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

    ym2608_ = std::make_unique<LibvgmYm2608>();
    if (!ym2608_->initialize(7'967'264, static_cast<uint32_t>(sample_rate_))) {
        error = "failed to initialize YM2608 sound chip";
        return HOOT_ERROR_UNSUPPORTED;
    }

    if (!setup_memory()) {
        error = "failed to setup PC-98 memory";
        return HOOT_ERROR_UNSUPPORTED;
    }

    setup_interrupt_vectors();
    setup_pit();
    reset_cpu_context();

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

    const auto voice = files_by_slot_.find(voice_slot);
    if (voice != files_by_slot_.end()) {
        selected_voice_path_ = voice->second.path;
    }

    playing_ = true;
    reset_cpu_context();
    run_cpu_steps(20000);

    error.clear();
    return HOOT_OK;
}

void Pc98DosDriver::reset()
{
    selected_track_ = 0;
    selected_code_ = 0;
    selected_bgm_path_.clear();
    selected_voice_path_.clear();
    playing_ = false;
    pit_counter_ = 0;
    if (ym2608_) {
        ym2608_->reset();
    }
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

    run_cpu_steps(frames * 8);

    if (ym2608_) {
        ym2608_->render_s16(interleaved_stereo, frames);
    } else {
        std::fill(interleaved_stereo, interleaved_stereo + (frames * 2), int16_t{0});
    }
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
    run_cpu_steps(frames * 8);

    if (ym2608_) {
        ym2608_->render_s16(mix_buffer_.data(), frames);
        for (int i = 0; i < frames * 2; ++i) {
            interleaved_stereo[i] = static_cast<float>(mix_buffer_[i]) / 32768.0f;
        }
    } else {
        std::fill(interleaved_stereo, interleaved_stereo + (frames * 2), 0.0f);
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
    out.debug_cpu_cycles = static_cast<uint32_t>(std::min<uint64_t>(executed_cpu_steps_, UINT32_MAX));
    out.debug_io_reads = static_cast<uint32_t>(files_by_slot_.size());
    out.debug_io_writes = selected_code_;
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
    driver_type_ = DriverType::Unknown;
    cpu_.reset();
    ym2608_.reset();
    int_vector_table_.clear();
    dos_memory_.clear();
    sample_rate_ = 44100;
    selected_track_ = 0;
    selected_code_ = 0;
    loaded_ = false;
    playing_ = false;
    pit_counter_ = 0;
    executed_cpu_steps_ = 0;
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

    if (!driver_data_.empty()) {
        auto copy_size = std::min(static_cast<size_t>(driver_data_.size()),
                                 static_cast<size_t>(1024 * 1024 - kDosEntryPoint));
        std::memcpy(mem + kDosEntryPoint, driver_data_.data(), copy_size);
    }

    for (const auto& [slot, file] : files_by_slot_) {
        if (file.data.size() > 1024 * 1024) {
            continue;
        }
        std::memcpy(mem + (slot * 16), file.data.data(), file.data.size());
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
    if (!ym2608_) {
        return 0xFF;
    }

    if (port == 0x88 || port == 0x89 || port == 0x8A || port == 0x8B) {
        return ym2608_->read(0);
    }
    if (port == 0x8C || port == 0x8D || port == 0x8E || port == 0x8F) {
        return ym2608_->read(1);
    }

    return 0xFF;
}

void Pc98DosDriver::write_io_port(uint16_t port, uint8_t data)
{
    if (!ym2608_) {
        return;
    }

    if (port == 0x88 || port == 0x89) {
        ym2608_->write(0, data);
    } else if (port == 0x8A || port == 0x8B) {
        ym2608_->write(0, data);
    } else if (port == 0x8C || port == 0x8D) {
        ym2608_->write(1, data);
    } else if (port == 0x8E || port == 0x8F) {
        ym2608_->write(1, data);
    }

    if (port == kPitIoport) {
    }
}

void Pc98DosDriver::handle_interrupt(uint8_t int_num)
{
    (void)int_num;
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
    cpu_->set_cs(0);
    cpu_->set_ds(0);
    cpu_->set_es(0);
    cpu_->set_ss(0);
    cpu_->set_sp(0xFFFE);
    cpu_->set_pc(static_cast<uint16_t>(kDosEntryPoint));
    executed_cpu_steps_ = 0;
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
