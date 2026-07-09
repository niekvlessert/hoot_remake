#include "drivers/pc98_dos_driver.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iomanip>
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

std::string first_token(const std::string& command)
{
    const auto start = command.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) {
        return {};
    }
    const auto end = command.find_first_of(" \t\r\n", start);
    return command.substr(start, end == std::string::npos ? std::string::npos : end - start);
}

std::string shell_executable_name(const std::string& shell)
{
    auto name = to_lower(first_token(shell));
    if (std::filesystem::path(name).extension().empty()) {
        name += ".com";
    }
    return name;
}

std::string json_escape(const std::string& value)
{
    std::ostringstream out;
    for (const unsigned char ch : value) {
        switch (ch) {
        case '\\': out << "\\\\"; break;
        case '"': out << "\\\""; break;
        case '\b': out << "\\b"; break;
        case '\f': out << "\\f"; break;
        case '\n': out << "\\n"; break;
        case '\r': out << "\\r"; break;
        case '\t': out << "\\t"; break;
        default:
            if (ch < 0x20) {
                out << "\\u"
                    << std::hex << std::setw(4) << std::setfill('0') << static_cast<int>(ch)
                    << std::dec << std::setfill(' ');
            } else {
                out << static_cast<char>(ch);
            }
            break;
        }
    }
    return out.str();
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
    function_vector_ = 0x7f;
    const auto funcvect = entry.options.find("funcvect");
    if (funcvect != entry.options.end()) {
        function_vector_ = static_cast<uint8_t>(funcvect->second & 0xff);
    }

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

    auto load_shell = [&](const std::string& shell) {
        const auto found = files_by_name.find(shell_executable_name(shell));
        if (found == files_by_name.end()) {
            return false;
        }
        driver_data_ = found->second.data;
        driver_type_ = DriverType::Shell;
        shell_command_ = std::vector<uint8_t>(shell.begin(), shell.end());
        shell_command_.push_back(0);
        return true;
    };

    if (!shell_names.empty()) {
        for (const auto& shell : shell_names) {
            const auto found = files_by_name.find(shell_executable_name(shell));
            if (found != files_by_name.end()) {
                shell_programs_.push_back(ShellProgram{shell, found->second.data});
                if (shell_executable_name(shell) == "hhd_98.com") {
                    uses_hhd98_bridge_ = true;
                } else if (shell_executable_name(shell) == "pmd_98.com") {
                    uses_pmd98_bridge_ = true;
                }
            }
        }
        for (const auto& shell : shell_names) {
            if (shell_executable_name(shell) == "cplay98.com" && shell_names.size() > 1) {
                continue;
            }
            if (load_shell(shell)) {
                break;
            }
        }
    }

    if (driver_data_.empty()) {
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
    cpu_->set_trace_callback([this](const char* type,
                                    uint8_t opcode,
                                    uint16_t from_cs,
                                    uint16_t from_ip,
                                    uint16_t to_cs,
                                    uint16_t to_ip) {
        trace_cpu_event(type, opcode, from_cs, from_ip, to_cs, to_ip);
    });

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
    trace_dos_ = std::getenv("HOOT_TRACE_PC98_DOS") != nullptr;
    trace_pc98_ = false;
    trace_events_ = 0;
    trace_event_limit_ = 20000;
    if (const char* value = std::getenv("HOOT_PC98_TRACE_LIMIT")) {
        trace_event_limit_ = static_cast<uint32_t>(std::max(0L, std::strtol(value, nullptr, 0)));
    }
    if (const char* trace_path = std::getenv("HOOT_PC98_TRACE")) {
        trace_file_.open(trace_path, std::ios::out | std::ios::trunc);
        trace_pc98_ = trace_file_.is_open();
        if (trace_pc98_) {
            std::ostringstream event;
            event << "{\"type\":\"meta\",\"driver\":\"" << json_escape(entry.driver_name)
                  << "\",\"archive\":\"" << json_escape(entry.archive)
                  << "\",\"sample_rate\":" << sample_rate_ << "}";
            emit_trace_event(event.str());
        }
    }
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
        selected_file_open_ = true;
        selected_file_offset_ = 0;
        bridge_load_pending_ = true;
        constexpr uint16_t kTrackShellSegment = 0x3000;
        for (size_t i = installed_shell_programs_; i < shell_programs_.size(); ++i) {
            run_shell_program(shell_programs_[i],
                              static_cast<uint16_t>(kTrackShellSegment + ((i - installed_shell_programs_) * 0x0400)));
        }
        auto* mem = cpu_->memory();
        const uint32_t bridge_buffer = (static_cast<uint32_t>(kProgramSegment) << 4) + kBridgeBufferOffset;
        std::fill(mem + bridge_buffer, mem + bridge_buffer + 0x4f, 0);
        const std::string filename = std::filesystem::path(selected_bgm_path_).filename().string();
        const auto copy_size = std::min<size_t>(filename.size(), 0x4e);
        if (copy_size > 0) {
            std::memcpy(mem + bridge_buffer, filename.data(), copy_size);
        }
        if (uses_hhd98_bridge_) {
            load_hhd98_track();
        } else {
            selected_file_open_ = true;
            selected_file_offset_ = 0;
            bridge_load_pending_ = true;
            if (uses_pmd98_bridge_) {
                call_shell_player_api(static_cast<uint16_t>(selected_code_ & 0xff));
            } else {
                call_shell_player_api(0x0900, kProgramSegment, kBridgeBufferOffset);
                call_shell_player_api(static_cast<uint16_t>(voice_slot & 0xff));
            }
        }
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
                const uint32_t hook_addr = static_cast<uint32_t>(function_vector_) * 4;
                const uint16_t hook_offset = static_cast<uint16_t>(read_memory_byte(hook_addr)
                    | (read_memory_byte(hook_addr + 1) << 8));
                const uint16_t hook_segment = static_cast<uint16_t>(read_memory_byte(hook_addr + 2)
                    | (read_memory_byte(hook_addr + 3) << 8));
                if (uses_hhd98_bridge_) {
                    const uint32_t int60 = 0x60 * 4;
                    const uint16_t hhd_segment = static_cast<uint16_t>(read_memory_byte(int60 + 2)
                        | (read_memory_byte(int60 + 3) << 8));
                    setup_interrupt_vector(0x7e, hhd_segment, 0x064b);
                    trigger_interrupt_vector(0x7e, 200000);
                } else {
                    uint8_t vector = (hook_offset != 0 || hook_segment != 0) ? function_vector_ : 0x14;
                    if (!bridge_load_pending_ && is_interrupt_vector_active(0x0b)) {
                        vector = 0x0b;
                    }
                    trigger_interrupt_vector(vector, 200000);
                }
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
    bridge_load_pending_ = false;
    bridge_command_active_ = false;
    bridge_command_ = 0xff;
    bridge_argument_ = 0xffff;
    driver_type_ = DriverType::Unknown;
    uses_hhd98_bridge_ = false;
    uses_pmd98_bridge_ = false;
    function_vector_ = 0x7f;
    cpu_.reset();
    ym2203_.reset();
    ym2608_.reset();
    use_ym2203_ = false;
    shell_programs_.clear();
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
    dos_alloc_segment_ = 0x2000;
    installed_shell_programs_ = 0;
    trace_dos_ = false;
    if (trace_file_.is_open()) {
        trace_file_.close();
    }
    trace_pc98_ = false;
    trace_events_ = 0;
    trace_event_limit_ = 0;
    shell_async_interrupts_ = false;
    suppress_async_interrupts_ = false;
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

    if (driver_type_ == DriverType::Shell) {
        const uint32_t psp_linear = static_cast<uint32_t>(kProgramSegment) << 4;
        mem[psp_linear + 0x0000] = 0xcd;
        mem[psp_linear + 0x0001] = 0x20;
        const std::string command(reinterpret_cast<const char*>(shell_command_.data()),
                                  shell_command_.empty() ? 0 : shell_command_.size() - 1);
        const auto token_end = command.find_first_of(" \t");
        const auto args_start = token_end == std::string::npos
            ? std::string::npos
            : command.find_first_not_of(" \t", token_end);
        std::string args = args_start == std::string::npos ? std::string() : command.substr(args_start);
        if (args.size() > 126) {
            args.resize(126);
        }
        mem[psp_linear + 0x80] = static_cast<uint8_t>(args.size());
        if (!args.empty()) {
            std::memcpy(mem + psp_linear + 0x81, args.data(), args.size());
        }
        mem[psp_linear + 0x81 + args.size()] = '\r';
    }

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
            setup_interrupt_vector(0x0b, 0x0000, kIretOffset);
            setup_interrupt_vector(0x2f, 0x0000, kIretOffset);
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
    uint8_t value = 0xff;
    if (port == 0x07e0) {
        value = bridge_command_active_ ? bridge_command_ : (bridge_load_pending_ ? 0x00 : 0x01);
        trace_io_event("in", port, value);
        return value;
    }
    if (port == 0x07e2) {
        value = static_cast<uint8_t>(bridge_argument_ & 0xff);
        trace_io_event("in", port, value);
        return value;
    }
    if (port == 0x07e3) {
        value = static_cast<uint8_t>((bridge_argument_ >> 8) & 0xff);
        trace_io_event("in", port, value);
        return value;
    }
    if (!ym2203_ && !ym2608_) {
        trace_io_event("in", port, value);
        return value;
    }

    if (port == 0x88 || port == 0x8B || port == 0x188) {
        if (use_ym2203_ && uses_hhd98_bridge_ && cpu_ && cpu_->get_pc() == 0x067d) {
            if (trace_dos_) {
                std::fprintf(stderr, "pc98dos hhd timer-status pc=%04x\n", cpu_->get_pc());
            }
            value = 0x03;
            trace_io_event("in", port, value);
            return value;
        }
        value = use_ym2203_ ? 0x00 : read_opn(0);
        trace_io_event("in", port, value);
        return value;
    }
    if (port == 0x89 || port == 0x8A || port == 0x18A) {
        value = opna_registers_[0][current_opna_address_[0]];
        trace_io_event("in", port, value);
        return value;
    }
    if (port == 0x8C || port == 0x8F || port == 0x18C) {
        value = read_opn(2);
        trace_io_event("in", port, value);
        return value;
    }
    if (port == 0x8D || port == 0x8E || port == 0x18E) {
        value = opna_registers_[1][current_opna_address_[1]];
        trace_io_event("in", port, value);
        return value;
    }

    trace_io_event("in", port, value);
    return value;
}

void Pc98DosDriver::write_io_port(uint16_t port, uint8_t data)
{
    trace_io_event("out", port, data);
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
    trace_interrupt_event(int_num);
    if (trace_dos_) {
        std::fprintf(stderr,
                     "pc98dos int%02x ax=%04x bx=%04x cx=%04x dx=%04x ds=%04x cs:ip=%04x:%04x\n",
                     int_num,
                     cpu_ ? cpu_->get_ax() : 0,
                     cpu_ ? cpu_->get_bx() : 0,
                     cpu_ ? cpu_->get_cx() : 0,
                     cpu_ ? cpu_->get_dx() : 0,
                     cpu_ ? cpu_->get_ds() : 0,
                     cpu_ ? cpu_->get_cs() : 0,
                     cpu_ ? cpu_->get_pc() : 0);
    }
    if (int_num == 0x21) {
        handle_dos_interrupt();
    } else if (int_num == 0x18 && cpu_ && cpu_->get_ax() == 0x9801) {
        cpu_->halt();
    }
}

void Pc98DosDriver::handle_dos_interrupt()
{
    if (!cpu_) {
        return;
    }

    if (trace_dos_) {
        std::fprintf(stderr,
                     "pc98dos int21 ah=%02x al=%02x ax=%04x bx=%04x cx=%04x dx=%04x si=%04x ds=%04x es=%04x cs:ip=%04x:%04x\n",
                     cpu_->get_ah(),
                     cpu_->get_al(),
                     cpu_->get_ax(),
                     cpu_->get_bx(),
                     cpu_->get_cx(),
                     cpu_->get_dx(),
                     cpu_->get_si(),
                     cpu_->get_ds(),
                     cpu_->get_es(),
                     cpu_->get_cs(),
                     cpu_->get_pc());
    }

    switch (cpu_->get_ah()) {
    case 0x09:
        cpu_->set_carry(false);
        break;
    case 0x25:
        setup_interrupt_vector(cpu_->get_al(), cpu_->get_ds(), cpu_->get_dx());
        cpu_->set_carry(false);
        break;
    case 0x35: {
        const uint32_t addr = static_cast<uint32_t>(cpu_->get_al()) * 4;
        cpu_->set_bx(static_cast<uint16_t>(read_memory_byte(addr)
            | (read_memory_byte(addr + 1) << 8)));
        cpu_->set_es(static_cast<uint16_t>(read_memory_byte(addr + 2)
            | (read_memory_byte(addr + 3) << 8)));
        if (trace_dos_) {
            std::fprintf(stderr,
                         "pc98dos get-vector int=%02x -> %04x:%04x\n",
                         cpu_->get_al(),
                         cpu_->get_es(),
                         cpu_->get_bx());
        }
        cpu_->set_carry(false);
        break;
    }
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
    case 0x48: {
        const uint16_t paragraphs = cpu_->get_bx();
        if (paragraphs == 0xffff || paragraphs == 0) {
            cpu_->set_ax(0x0008);
            cpu_->set_bx(0x1000);
            cpu_->set_carry(true);
            break;
        }
        const uint16_t segment = dos_alloc_segment_;
        dos_alloc_segment_ = static_cast<uint16_t>(dos_alloc_segment_ + paragraphs + 1);
        cpu_->set_ax(segment);
        if (trace_dos_) {
            std::fprintf(stderr,
                         "pc98dos alloc paragraphs=%04x -> %04x\n",
                         paragraphs,
                         segment);
        }
        cpu_->set_carry(false);
        break;
    }
    case 0x49:
        cpu_->set_carry(false);
        break;
    case 0x4a:
        cpu_->set_carry(false);
        break;
    case 0x62:
        cpu_->set_bx(kProgramSegment);
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
        std::ostringstream event;
        event << "{\"type\":\"file_open\",\"result\":\"match\",\"name\":\"" << json_escape(raw_name)
              << "\",\"selected\":\"" << json_escape(selected_bgm_path_)
              << "\",\"handle\":" << selected_file_handle_ << "}";
        emit_trace_event(event.str());
        if (trace_dos_) {
            std::fprintf(stderr, "pc98dos open match \"%s\" handle=%u\n", raw_name.c_str(), selected_file_handle_);
        }
        cpu_->set_carry(false);
        return;
    }
    if (trace_dos_) {
        std::fprintf(stderr, "pc98dos open miss \"%s\" selected=\"%s\"\n", raw_name.c_str(), selected_bgm_path_.c_str());
    }
    {
        std::ostringstream event;
        event << "{\"type\":\"file_open\",\"result\":\"miss\",\"name\":\"" << json_escape(raw_name)
              << "\",\"selected\":\"" << json_escape(selected_bgm_path_) << "\"}";
        emit_trace_event(event.str());
    }
    cpu_->set_ax(0x0002);
    cpu_->set_carry(true);
}

void Pc98DosDriver::dos_read_file()
{
    if (!cpu_) {
        return;
    }
    const uint16_t handle = cpu_->get_bx();
    if (!selected_file_open_ || (handle != selected_file_handle_ && handle != 0)) {
        if (trace_dos_) {
            std::fprintf(stderr,
                         "pc98dos read rejected handle=%u open=%u selected-size=%zu\n",
                         handle,
                         selected_file_open_ ? 1 : 0,
                         selected_bgm_data_.size());
        }
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
    uint16_t dest_segment = cpu_->get_ds();
    uint16_t dest_offset = cpu_->get_dx();
    if (handle == 0 && dest_segment == 0x0000 && dest_offset == 0x07e0) {
        const uint32_t int60 = 0x60 * 4;
        const uint16_t hhd_segment = static_cast<uint16_t>(read_memory_byte(int60 + 2)
            | (read_memory_byte(int60 + 3) << 8));
        if (hhd_segment != 0) {
            dest_segment = hhd_segment;
            dest_offset = 0x1dbb;
        }
    }
    const uint32_t dest = (static_cast<uint32_t>(dest_segment) << 4) + dest_offset;
    for (size_t i = 0; i < count && dest + i < 1024 * 1024; ++i) {
        write_memory_byte(dest + static_cast<uint32_t>(i), selected_bgm_data_[selected_file_offset_ + i]);
    }
    selected_file_offset_ += count;
    cpu_->set_ax(static_cast<uint16_t>(count));
    if (handle == 0) {
        bridge_load_pending_ = false;
    }
    {
        std::ostringstream event;
        event << "{\"type\":\"file_read\",\"handle\":" << handle
              << ",\"request\":" << requested
              << ",\"count\":" << count
              << ",\"offset\":" << selected_file_offset_
              << ",\"dst_seg\":" << dest_segment
              << ",\"dst_off\":" << dest_offset << "}";
        emit_trace_event(event.str());
    }
    if (trace_dos_) {
        std::fprintf(stderr,
                     "pc98dos read handle=%u request=%zu count=%zu offset=%zu dst=%04x:%04x\n",
                     handle,
                     requested,
                     count,
                     selected_file_offset_,
                     dest_segment,
                     dest_offset);
    }
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

void Pc98DosDriver::reset_cpu_context(uint16_t segment)
{
    if (!cpu_) {
        return;
    }

    cpu_->reset();
    cpu_->set_cs(segment);
    cpu_->set_ds(segment);
    cpu_->set_es(segment);
    cpu_->set_ss(segment);
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
    {
        std::ostringstream event;
        event << "{\"type\":\"set_vector\",\"int\":" << static_cast<unsigned>(vector)
              << ",\"seg\":" << segment
              << ",\"off\":" << offset << "}";
        emit_trace_event(event.str());
    }
    if (trace_dos_) {
        std::fprintf(stderr, "pc98dos set-vector int=%02x -> %04x:%04x\n", vector, segment, offset);
    }
}

bool Pc98DosDriver::is_interrupt_vector_active(uint8_t vector)
{
    const uint32_t addr = static_cast<uint32_t>(vector) * 4;
    const uint16_t offset = static_cast<uint16_t>(read_memory_byte(addr)
        | (read_memory_byte(addr + 1) << 8));
    const uint16_t segment = static_cast<uint16_t>(read_memory_byte(addr + 2)
        | (read_memory_byte(addr + 3) << 8));
    if (offset == 0 && segment == 0) {
        return false;
    }
    if (segment == 0 && offset == kIretOffset) {
        return false;
    }
    if (segment == 0 && offset == kHaltOffset) {
        return false;
    }
    return true;
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

void Pc98DosDriver::trigger_async_interrupt_vector(uint8_t vector, int steps)
{
    if (!cpu_ || suppress_async_interrupts_) {
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
    if (segment == 0 && (offset == kIretOffset || offset == kHaltOffset)) {
        return;
    }

    suppress_async_interrupts_ = true;
    const bool was_halted = cpu_->is_halted();
    cpu_->clear_halted();
    push_cpu_word(0x0200);
    push_cpu_word(cpu_->get_cs());
    push_cpu_word(cpu_->get_pc());
    cpu_->set_cs(segment);
    cpu_->set_pc(offset);
    run_cpu_steps(steps);
    if (was_halted) {
        cpu_->halt();
    } else {
        cpu_->clear_halted();
    }
    suppress_async_interrupts_ = false;
}

void Pc98DosDriver::trigger_near_subroutine(uint16_t segment, uint16_t offset, int steps)
{
    if (!cpu_ || segment == 0) {
        return;
    }
    cpu_->clear_halted();
    cpu_->set_cs(segment);
    cpu_->set_ds(segment);
    cpu_->set_es(segment);
    cpu_->set_ss(segment);
    cpu_->set_sp(0xfffe);
    const uint32_t halt_linear = (static_cast<uint32_t>(segment) << 4) + kHaltOffset;
    write_memory_byte(halt_linear, 0xf4);
    push_cpu_word(kHaltOffset);
    cpu_->set_pc(offset);
    run_cpu_steps(steps);
    cpu_->clear_halted();
}

void Pc98DosDriver::install_shell_driver()
{
    if (shell_programs_.empty()) {
        reset_cpu_context();
        run_cpu_steps(2000000);
        cpu_->clear_halted();
        return;
    }

    run_shell_program(shell_programs_.front(), kProgramSegment);
    if (uses_hhd98_bridge_) {
        const uint32_t base = static_cast<uint32_t>(kProgramSegment) << 4;
        write_memory_byte(base + 0x1d6f, 0x01);
        write_memory_byte(base + 0x1d70, 0x00);
    }
    installed_shell_programs_ = 1;
}

void Pc98DosDriver::load_shell_program(const ShellProgram& program, uint16_t segment)
{
    if (!cpu_ || program.data.empty()) {
        return;
    }
    auto* mem = cpu_->memory();
    const uint32_t entry_linear = (static_cast<uint32_t>(segment) << 4) + kDosEntryPoint;
    std::fill(mem + entry_linear, mem + entry_linear + 0xff00, 0);
    const auto copy_size = std::min(static_cast<size_t>(program.data.size()),
                                    static_cast<size_t>(1024 * 1024 - entry_linear));
    std::memcpy(mem + entry_linear, program.data.data(), copy_size);
}

void Pc98DosDriver::setup_shell_psp(const std::string& command, uint16_t segment)
{
    if (!cpu_) {
        return;
    }
    auto* mem = cpu_->memory();
    if (segment > 0) {
        const uint32_t mcb_linear = static_cast<uint32_t>(segment - 1) << 4;
        mem[mcb_linear + 0x00] = 'M';
        mem[mcb_linear + 0x01] = static_cast<uint8_t>(segment & 0xff);
        mem[mcb_linear + 0x02] = static_cast<uint8_t>((segment >> 8) & 0xff);
        const uint16_t paragraphs = static_cast<uint16_t>(0x9fff - segment);
        mem[mcb_linear + 0x03] = static_cast<uint8_t>(paragraphs & 0xff);
        mem[mcb_linear + 0x04] = static_cast<uint8_t>((paragraphs >> 8) & 0xff);
        if (trace_dos_) {
            std::fprintf(stderr,
                         "pc98dos shell-mcb segment=%04x mcb=%04x size=%04x\n",
                         segment,
                         static_cast<uint16_t>(segment - 1),
                         paragraphs);
        }
    }
    const uint32_t psp_linear = static_cast<uint32_t>(segment) << 4;
    mem[psp_linear + 0x0000] = 0xcd;
    mem[psp_linear + 0x0001] = 0x20;
    std::fill(mem + psp_linear + 0x5c, mem + psp_linear + 0x100, 0);
    const auto token_end = command.find_first_of(" \t");
    const auto args_start = token_end == std::string::npos
        ? std::string::npos
        : command.find_first_not_of(" \t", token_end);
    std::string args = args_start == std::string::npos ? std::string() : command.substr(args_start);
    if (args.size() > 126) {
        args.resize(126);
    }
    mem[psp_linear + 0x80] = static_cast<uint8_t>(args.size());
    if (!args.empty()) {
        std::memcpy(mem + psp_linear + 0x81, args.data(), args.size());
    }
    mem[psp_linear + 0x81 + args.size()] = '\r';
}

void Pc98DosDriver::run_shell_program(const ShellProgram& program, uint16_t segment, int steps)
{
    if (trace_dos_) {
        std::fprintf(stderr, "pc98dos run-shell segment=%04x command=\"%s\"\n", segment, program.command.c_str());
    }
    load_shell_program(program, segment);
    setup_shell_psp(program.command, segment);
    if (trace_dos_) {
        const uint32_t psp = static_cast<uint32_t>(segment) << 4;
        const uint8_t length = read_memory_byte(psp + 0x80);
        std::fprintf(stderr, "pc98dos shell-tail len=%u text=\"", length);
        for (uint8_t i = 0; i < length && i < 127; ++i) {
            const char ch = static_cast<char>(read_memory_byte(psp + 0x81 + i));
            std::fputc((ch >= 0x20 && ch < 0x7f) ? ch : '.', stderr);
        }
        std::fprintf(stderr, "\"\n");
    }
    reset_cpu_context(segment);
    const bool old_shell_async = shell_async_interrupts_;
    shell_async_interrupts_ = true;
    run_cpu_steps(steps);
    shell_async_interrupts_ = old_shell_async;
    if (trace_dos_) {
        const auto print_vector = [this](uint8_t vector) {
            const uint32_t addr = static_cast<uint32_t>(vector) * 4;
            const uint16_t offset = static_cast<uint16_t>(read_memory_byte(addr)
                | (read_memory_byte(addr + 1) << 8));
            const uint16_t seg = static_cast<uint16_t>(read_memory_byte(addr + 2)
                | (read_memory_byte(addr + 3) << 8));
            std::fprintf(stderr, "pc98dos vector int=%02x is %04x:%04x\n", vector, seg, offset);
        };
        std::fprintf(stderr,
                     "pc98dos shell-done cs:ip=%04x:%04x unsupported=%u opcode=%02x reads=%u opn=%u hhd[1d65,1d66]=%02x,%02x\n",
                     cpu_->get_cs(),
                     cpu_->get_pc(),
                     cpu_->unsupported_count(),
                     cpu_->last_unsupported_opcode(),
                     debug_file_reads_,
                     debug_opna_writes_,
                     read_memory_byte((static_cast<uint32_t>(segment) << 4) + 0x1d65),
                     read_memory_byte((static_cast<uint32_t>(segment) << 4) + 0x1d66));
        print_vector(0x14);
        print_vector(0x08);
        print_vector(0x1c);
        print_vector(0x60);
        print_vector(0x62);
        print_vector(0x7f);
    }
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
    bridge_command_active_ = true;
    bridge_command_ = 0x00;
    bridge_argument_ = ax;
    trigger_interrupt_vector(function_vector_, 500000);
    bridge_command_active_ = false;
    cpu_->clear_unsupported_status();
    cpu_->clear_halted();
    cpu_->set_cs(kProgramSegment);
    cpu_->set_ds(kProgramSegment);
    cpu_->set_es(kProgramSegment);
    cpu_->set_ss(kProgramSegment);
    cpu_->set_sp(0xfffe);
    cpu_->set_pc(kHaltOffset);
}

void Pc98DosDriver::load_hhd98_track()
{
    if (!cpu_ || selected_bgm_data_.empty()) {
        return;
    }

    const uint32_t int60 = 0x60 * 4;
    const uint16_t hhd_segment = static_cast<uint16_t>(read_memory_byte(int60 + 2)
        | (read_memory_byte(int60 + 3) << 8));
    if (hhd_segment == 0) {
        return;
    }

    const uint32_t hhd_base = static_cast<uint32_t>(hhd_segment) << 4;
    const auto set_voice_table_ptr = [this, hhd_base]() {
        const uint16_t voice_table = static_cast<uint16_t>(read_memory_byte(hhd_base + 0x1dc0)
            | (read_memory_byte(hhd_base + 0x1dc1) << 8));
        const uint16_t voice_table_ptr = static_cast<uint16_t>(0x1dbb + voice_table);
        write_memory_byte(hhd_base + 0x1d7a, static_cast<uint8_t>(voice_table_ptr & 0xff));
        write_memory_byte(hhd_base + 0x1d7b, static_cast<uint8_t>((voice_table_ptr >> 8) & 0xff));
    };

    const uint32_t int7f = 0x7f * 4;
    const uint16_t bridge_offset = static_cast<uint16_t>(read_memory_byte(int7f)
        | (read_memory_byte(int7f + 1) << 8));
    const uint16_t bridge_segment = static_cast<uint16_t>(read_memory_byte(int7f + 2)
        | (read_memory_byte(int7f + 3) << 8));

    if (bridge_offset != 0 || bridge_segment != 0) {
        selected_file_open_ = true;
        selected_file_offset_ = 0;
        trigger_interrupt_vector(0x7f, 5000000);
        set_voice_table_ptr();
    } else {
        cpu_->set_ax(0x0200);
        trigger_interrupt_vector(0x60, 5000000);

        cpu_->set_ax(0x0000);
        trigger_interrupt_vector(0x60, 5000000);

        uint16_t dest_segment = cpu_->get_ds();
        uint16_t dest_offset = cpu_->get_dx();
        if (dest_segment == 0 || dest_offset == 0x07e0) {
            dest_segment = hhd_segment;
            dest_offset = 0x1dbb;
        }

        const uint32_t dest = (static_cast<uint32_t>(dest_segment) << 4) + dest_offset;
        const size_t count = std::min(selected_bgm_data_.size(), static_cast<size_t>(1024 * 1024 - dest));
        for (size_t i = 0; i < count; ++i) {
            write_memory_byte(dest + static_cast<uint32_t>(i), selected_bgm_data_[i]);
        }
        selected_file_offset_ = count;
        ++debug_file_reads_;

        if (trace_dos_) {
            std::fprintf(stderr,
                         "pc98dos hhd-load bytes=%zu dst=%04x:%04x\n",
                         count,
                         dest_segment,
                         dest_offset);
        }

        write_memory_byte(hhd_base + 0x1684, 0xeb);
        write_memory_byte(hhd_base + 0x1685, 0x17);
        write_memory_byte(hhd_base + 0x1686, 0x03);
        write_memory_byte(hhd_base + 0x1687, 0x01);
        write_memory_byte(hhd_base + 0x1688, 0x76);
        write_memory_byte(hhd_base + 0x1689, 0x03);
        set_voice_table_ptr();
    }

    if (read_memory_byte(hhd_base + 0x1957) == 0 && read_memory_byte(hhd_base + 0x1958) == 0) {
        for (uint16_t offset = 0x1956; offset < 0x1956 + 0x033e; ++offset) {
            write_memory_byte(hhd_base + offset, 0);
        }
        const uint16_t channel_blocks[] = {
            0x1956, 0x19a9, 0x19fc,
            0x1a4f, 0x1aa2, 0x1af5,
            0x1b48, 0x1b9b, 0x1bee,
        };
        for (uint8_t i = 0; i < 9; ++i) {
            const uint16_t block = channel_blocks[i];
            const uint8_t channel_id = i < 6 ? static_cast<uint8_t>(i % 3) : static_cast<uint8_t>(i);
            write_memory_byte(hhd_base + block, channel_id);
            write_memory_byte(hhd_base + block + 0x06, 0x01);
            write_memory_byte(hhd_base + block + 0x08, 0x0a);
            write_memory_byte(hhd_base + block + 0x44, 0x01);
            write_memory_byte(hhd_base + block + 0x45, 0xc0);
            const uint16_t sequence_offset = static_cast<uint16_t>(read_memory_byte(hhd_base + 0x1dc2 + (i * 2))
                | (read_memory_byte(hhd_base + 0x1dc3 + (i * 2)) << 8));
            if (sequence_offset == 0) {
                continue;
            }
            const uint16_t sequence_ptr = static_cast<uint16_t>(0x1dbb + sequence_offset);
            write_memory_byte(hhd_base + block + 0x01, static_cast<uint8_t>(sequence_ptr & 0xff));
            write_memory_byte(hhd_base + block + 0x02, static_cast<uint8_t>((sequence_ptr >> 8) & 0xff));
            cpu_->set_ax(sequence_ptr);
            cpu_->set_di(block);
            trigger_near_subroutine(hhd_segment, 0x02dd, 500000);
        }
        write_memory_byte(hhd_base + 0x1d61, 0x00);
        write_memory_byte(hhd_base + 0x1d96, 0x00);
        write_memory_byte(hhd_base + 0x1d97, 0x00);
    }
    if (trace_dos_) {
        const uint32_t base = static_cast<uint32_t>(hhd_segment) << 4;
        const auto word_at = [this, base](uint16_t offset) {
            return static_cast<uint16_t>(read_memory_byte(base + offset)
                | (read_memory_byte(base + offset + 1) << 8));
        };
        std::fprintf(stderr,
                     "pc98dos hhd-ch pc-after=%04x:%04x ptrs=%04x,%04x,%04x,%04x,%04x,%04x tempo=%02x div=%02x\n",
                     cpu_->get_cs(),
                     cpu_->get_pc(),
                     word_at(0x1957),
                     word_at(0x19aa),
                     word_at(0x19fd),
                     word_at(0x1a50),
                     word_at(0x1aa3),
                     word_at(0x1af6),
                     read_memory_byte(base + 0x1dbd),
                     read_memory_byte(base + 0x1d61));
    }
    cpu_->clear_unsupported_status();
    cpu_->clear_halted();
    cpu_->set_cs(kProgramSegment);
    cpu_->set_ds(kProgramSegment);
    cpu_->set_es(kProgramSegment);
    cpu_->set_ss(kProgramSegment);
    cpu_->set_sp(0xfffe);
    cpu_->set_pc(kHaltOffset);
}

void Pc98DosDriver::emit_trace_event(const std::string& json)
{
    if (!trace_pc98_ || !trace_file_.is_open()) {
        return;
    }
    if (trace_event_limit_ != 0 && trace_events_ >= trace_event_limit_) {
        return;
    }
    trace_file_ << json << '\n';
    ++trace_events_;
}

void Pc98DosDriver::trace_cpu_event(const char* type,
                                    uint8_t opcode,
                                    uint16_t from_cs,
                                    uint16_t from_ip,
                                    uint16_t to_cs,
                                    uint16_t to_ip)
{
    std::ostringstream event;
    event << "{\"type\":\"" << type
          << "\",\"frame\":" << rendered_frames_
          << ",\"step\":" << executed_cpu_steps_
          << ",\"opcode\":" << static_cast<unsigned>(opcode)
          << ",\"from_cs\":" << from_cs
          << ",\"from_ip\":" << from_ip
          << ",\"to_cs\":" << to_cs
          << ",\"to_ip\":" << to_ip;
    if (cpu_) {
        event << ",\"ax\":" << cpu_->get_ax()
              << ",\"bx\":" << cpu_->get_bx()
              << ",\"cx\":" << cpu_->get_cx()
              << ",\"dx\":" << cpu_->get_dx()
              << ",\"si\":" << cpu_->get_si()
              << ",\"di\":" << cpu_->get_di()
              << ",\"ds\":" << cpu_->get_ds()
              << ",\"es\":" << cpu_->get_es()
              << ",\"ss\":" << cpu_->get_ss()
              << ",\"sp\":" << cpu_->get_sp();
    }
    event << "}";
    emit_trace_event(event.str());
}

void Pc98DosDriver::trace_io_event(const char* type, uint16_t port, uint8_t value)
{
    std::ostringstream event;
    event << "{\"type\":\"" << type
          << "\",\"frame\":" << rendered_frames_
          << ",\"step\":" << executed_cpu_steps_
          << ",\"port\":" << port
          << ",\"value\":" << static_cast<unsigned>(value);
    if (cpu_) {
        event << ",\"cs\":" << cpu_->get_cs()
              << ",\"ip\":" << cpu_->get_pc()
              << ",\"ax\":" << cpu_->get_ax()
              << ",\"dx\":" << cpu_->get_dx();
    }
    if (port == 0x89 || port == 0x8a || port == 0x18a) {
        event << ",\"opna_bank\":0,\"opna_reg\":" << static_cast<unsigned>(current_opna_address_[0]);
    } else if (port == 0x8d || port == 0x8e || port == 0x18e) {
        event << ",\"opna_bank\":1,\"opna_reg\":" << static_cast<unsigned>(current_opna_address_[1]);
    }
    event << "}";
    emit_trace_event(event.str());
}

void Pc98DosDriver::trace_interrupt_event(uint8_t int_num)
{
    std::ostringstream event;
    event << "{\"type\":\"int\",\"frame\":" << rendered_frames_
          << ",\"step\":" << executed_cpu_steps_
          << ",\"int\":" << static_cast<unsigned>(int_num);
    if (cpu_) {
        event << ",\"cs\":" << cpu_->get_cs()
              << ",\"ip\":" << cpu_->get_pc()
              << ",\"ax\":" << cpu_->get_ax()
              << ",\"bx\":" << cpu_->get_bx()
              << ",\"cx\":" << cpu_->get_cx()
              << ",\"dx\":" << cpu_->get_dx()
              << ",\"ds\":" << cpu_->get_ds()
              << ",\"es\":" << cpu_->get_es();
    }
    event << "}";
    emit_trace_event(event.str());
}

void Pc98DosDriver::run_cpu_steps(int steps)
{
    if (!cpu_ || steps <= 0) {
        return;
    }

    int remaining = steps;
    while (remaining > 0 && !cpu_->is_halted()) {
        const int quantum = (shell_async_interrupts_ && !suppress_async_interrupts_)
            ? std::min(remaining, 20000)
            : remaining;
        const int executed = cpu_->execute(quantum);
        executed_cpu_steps_ += static_cast<uint64_t>(std::max(0, executed));
        remaining -= std::max(1, executed);
        pit_timer_tick();
        if (executed <= 0) {
            break;
        }
        if (shell_async_interrupts_
            && !suppress_async_interrupts_
            && cpu_->get_interrupt_flag()
            && is_interrupt_vector_active(0x08)) {
            trigger_async_interrupt_vector(0x08, 64);
        }
    }
}

} // namespace hoot
