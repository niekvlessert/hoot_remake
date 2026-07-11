#include "drivers/microcabin_pc98dos_driver.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <sstream>

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

std::string first_token(const std::string& text)
{
    const auto pos = text.find_first_of(" \t\r\n");
    if (pos == std::string::npos) {
        return text;
    }
    return text.substr(0, pos);
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

} // namespace

HootResult MicrocabinPc98DosDriver::load(const HootEntry& entry,
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

    for (const auto& asset : entry.assets) {
        if (asset.type == "device") {
            mmd_device_command_ = asset.path;
            auto driver_name = first_token(asset.path);
            auto data = archive.read(driver_name, error);
            if (!error.empty()) {
                return HOOT_ERROR_IO;
            }
            mmd_sys_ = std::move(data);
            continue;
        }
        if (asset.type == "shell") {
            shell_command_ = asset.path;
            continue;
        }
        if (asset.type != "file") {
            continue;
        }

        auto data = archive.read(asset.path, error);
        if (!error.empty()) {
            return HOOT_ERROR_IO;
        }
        if (asset.path == "MMD.SYS" || asset.path == "MMD2.SYS" || asset.path == "mmd.sys") {
            mmd_sys_ = data;
        } else if (first_token(asset.path) == first_token(shell_command_)
            || asset.path == "mmd2.com" || asset.path == "mmd3.com") {
            mmd_helper_ = data;
        }
        if (!has_negative_offset(asset.offset)) {
            files_by_slot_[asset.offset] = LoadedFile{asset.path, std::move(data)};
        }
    }

    if (mmd_sys_.empty()) {
        error = "pc98dos Microcabin entry did not provide MMD.SYS/MMD2.SYS";
        return HOOT_ERROR_NOT_FOUND;
    }
    if (shell_command_.empty()) {
        error = "pc98dos Microcabin entry did not provide a shell command";
        return HOOT_ERROR_NOT_FOUND;
    }
    if (mmd_helper_.empty()) {
        error = "pc98dos Microcabin entry did not provide helper executable " + shell_command_;
        return HOOT_ERROR_NOT_FOUND;
    }
    if (!setup_runtime(error)) {
        return HOOT_ERROR_UNSUPPORTED;
    }

    loaded_ = true;
    return HOOT_OK;
}

HootResult MicrocabinPc98DosDriver::select_track(const HootEntry& entry,
                                                 int track_index,
                                                 std::string& error)
{
    if (!loaded_) {
        error = "pc98dos Microcabin driver is not loaded";
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
        error = "pc98dos Microcabin track references missing BGM slot "
            + hex_slot(bgm_slot);
        return HOOT_ERROR_NOT_FOUND;
    }
    selected_bgm_path_ = bgm->second.path;
    selected_bgm_data_ = bgm->second.data;
    selected_file_offset_ = 0;

    const auto voice = files_by_slot_.find(voice_slot);
    if (voice != files_by_slot_.end()) {
        selected_voice_path_ = voice->second.path;
        selected_voice_data_ = voice->second.data;
    } else {
        selected_voice_data_.clear();
    }

    command_pending_ = true;
    command_latch_ = 0;
    command_low_ = static_cast<uint8_t>(bgm_slot & 0xff);
    command_high_ = static_cast<uint8_t>(voice_slot & 0xff);
    mmd_timer_frames_until_tick_ = 0.0;
    playing_ = true;
    call_mmd_api(0x0000);
    if (mmd2_api_) {
        if (!selected_voice_data_.empty()) {
            load_mmd_stream(0x11, selected_voice_data_);
        }
    } else if (!selected_voice_data_.empty()) {
        load_mmd_stream(0x0c, selected_voice_data_);
        call_mmd_api(static_cast<uint16_t>(0x0d00 | (voice_slot & 0xff)));
    }
    load_mmd_stream(0x10, selected_bgm_data_);
    call_mmd_api(0x0100);

    error.clear();
    return HOOT_OK;
}

void MicrocabinPc98DosDriver::reset()
{
    selected_track_ = 0;
    selected_code_ = 0;
    selected_bgm_path_.clear();
    selected_voice_path_.clear();
    selected_bgm_data_.clear();
    selected_voice_data_.clear();
    selected_file_offset_ = 0;
    mmd_timer_frames_until_tick_ = 0.0;
    playing_ = false;
    command_pending_ = false;
    if (ym2608_) {
        ym2608_->reset();
    }
    reset_runtime();
}

int MicrocabinPc98DosDriver::render_s16(int16_t* interleaved_stereo, int frames)
{
    if (interleaved_stereo == nullptr || frames < 0) {
        return 0;
    }
    if (!loaded_ || !playing_) {
        std::fill(interleaved_stereo, interleaved_stereo + (frames * 2), int16_t{0});
        return frames;
    }
    if (!ym2608_) {
        std::fill(interleaved_stereo, interleaved_stereo + (frames * 2), int16_t{0});
        return frames;
    }

    const double timer_rate_hz = mmd_timer_rate_hz();
    const double frames_per_tick = timer_rate_hz > 0.0
        ? static_cast<double>(sample_rate_) / timer_rate_hz
        : static_cast<double>(sample_rate_) / 60.0;
    int rendered = 0;
    while (rendered < frames) {
        if (mmd_timer_frames_until_tick_ <= 0.0) {
            call_mmd_timer();
            mmd_timer_frames_until_tick_ += frames_per_tick;
        }
        const int chunk = std::min(
            std::max(1, static_cast<int>(std::ceil(mmd_timer_frames_until_tick_))),
            frames - rendered);
        run_cpu_steps(chunk * 12);
        ym2608_->render_s16(interleaved_stereo + (rendered * 2), chunk);
        mmd_timer_frames_until_tick_ -= static_cast<double>(chunk);
        rendered += chunk;
    }
    return frames;
}

int MicrocabinPc98DosDriver::render_float(float* interleaved_stereo, int frames)
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

void MicrocabinPc98DosDriver::fill_track_info(const HootEntry& entry,
                                              int track_index,
                                              HootTrackInfo& out) const
{
    std::memset(&out, 0, sizeof(out));
    out.track_index = track_index;
    out.sample_rate = sample_rate_;
    out.debug_cpu_cycles = cpu_ && cpu_->unsupported_count() != 0
        ? cpu_->unsupported_count()
        : static_cast<uint32_t>(std::min<uint64_t>(executed_cpu_steps_, UINT32_MAX));
    out.debug_io_reads = debug_io_reads_;
    out.debug_io_writes = debug_io_writes_;
    out.debug_opn_writes = debug_opna_writes_;
    out.debug_opn_keyons = debug_opna_keyons_;
    out.debug_pc = cpu_ && cpu_->unsupported_count() != 0
        ? ((static_cast<uint32_t>(cpu_->last_unsupported_cs()) << 16) | cpu_->last_unsupported_ip())
        : (cpu_ ? ((static_cast<uint32_t>(cpu_->get_cs()) << 16) | cpu_->get_pc()) : 0);
    const uint32_t mmd_base = (static_cast<uint32_t>(kMmdSegment) << 4) + kMmdOffset;
    const auto* mem = cpu_ ? cpu_->memory() : nullptr;
    out.debug_last_opn_reg = mem ? mem[mmd_base + 0x152c] : debug_last_int_;
    out.debug_last_opn_data = mem ? static_cast<uint32_t>(mem[mmd_base + 0x152e]
        | (mem[mmd_base + 0x152f] << 8)) : debug_last_ah_;
    out.debug_port_writes_00 = debug_mmd_select_errors_;
    out.debug_port_writes_01 = static_cast<uint64_t>(debug_fm_keyons_by_channel_[0])
        | (static_cast<uint64_t>(debug_fm_keyons_by_channel_[1]) << 16)
        | (static_cast<uint64_t>(debug_fm_keyons_by_channel_[2]) << 32)
        | (static_cast<uint64_t>(debug_fm_keyons_by_channel_[3]) << 48);
    out.debug_port_writes_02 = static_cast<uint64_t>(debug_fm_keyons_by_channel_[4])
        | (static_cast<uint64_t>(debug_fm_keyons_by_channel_[5]) << 32);
    out.debug_port_writes_03 = debug_opna_ssg_writes_;
    out.debug_port_writes_32 = mem ? static_cast<uint32_t>(mem[mmd_base + 0x17d8]
        | (mem[mmd_base + 0x17e9] << 8)
        | (mem[mmd_base + 0x1594] << 16)) : debug_file_seeks_;
    out.debug_port_writes_44 = debug_opna_adpcm_b_writes_;
    out.debug_port_writes_45 = (static_cast<uint32_t>(debug_mmd_play_errors_) << 24)
        | (static_cast<uint32_t>(debug_last_mmd_command_) << 8)
        | debug_last_mmd_return_;
    copy_c_string(out.driver, name());
    if (track_index >= 0 && static_cast<size_t>(track_index) < entry.tracks.size()) {
        copy_c_string(out.title, entry.tracks[track_index].title);
    } else {
        copy_c_string(out.title, entry.title);
    }
}

const char* MicrocabinPc98DosDriver::name() const
{
    return "microcabin-pc98dos-opn";
}

void MicrocabinPc98DosDriver::clear()
{
    files_by_slot_.clear();
    mmd_sys_.clear();
    mmd_helper_.clear();
    shell_command_.clear();
    mmd_device_command_.clear();
    selected_bgm_path_.clear();
    selected_voice_path_.clear();
    selected_bgm_data_.clear();
    selected_voice_data_.clear();
    cpu_.reset();
    ym2608_.reset();
    mix_buffer_.clear();
    sample_rate_ = 44100;
    selected_track_ = 0;
    selected_code_ = 0;
    loaded_ = false;
    playing_ = false;
    command_pending_ = false;
    command_latch_ = 0;
    command_low_ = 0xff;
    command_high_ = 0xff;
    mmd2_api_ = false;
    early_mmd2_layout_ = false;
    mmd_timer_vector_ = 0;
    selected_file_offset_ = 0;
    mmd_timer_frames_until_tick_ = 0.0;
    executed_cpu_steps_ = 0;
    debug_io_reads_ = 0;
    debug_io_writes_ = 0;
    debug_int21_ = 0;
    debug_intd2_ = 0;
    debug_opna_writes_ = 0;
    debug_opna_keyons_ = 0;
    debug_opna_bank1_writes_ = 0;
    debug_opna_ssg_writes_ = 0;
    debug_opna_rhythm_writes_ = 0;
    debug_opna_adpcm_b_writes_ = 0;
    debug_fm_keyons_by_channel_.fill(0);
    debug_mailbox_reads_ = 0;
    debug_file_reads_ = 0;
    debug_file_seeks_ = 0;
    debug_mmd_errors_ = 0;
    debug_mmd_select_errors_ = 0;
    debug_mmd_bgm_load_errors_ = 0;
    debug_mmd_voice_load_errors_ = 0;
    debug_mmd_play_errors_ = 0;
    debug_last_int_ = 0;
    debug_last_ah_ = 0;
    debug_last_mmd_command_ = 0;
    debug_last_mmd_return_ = 0;
    debug_last_mailbox_port_ = 0;
    debug_last_mailbox_value_ = 0;
    current_opna_address_[0] = 0;
    current_opna_address_[1] = 0;
    for (auto& bank : opna_registers_) {
        bank.fill(0);
    }
}

bool MicrocabinPc98DosDriver::setup_runtime(std::string& error)
{
    cpu_ = std::make_unique<X86Cpu>();
    cpu_->set_read_memory_callback([this](uint32_t addr) { return read_memory_byte(addr); });
    cpu_->set_write_memory_callback([this](uint32_t addr, uint8_t data) { write_memory_byte(addr, data); });
    cpu_->set_io_read_callback([this](uint16_t port) { return read_io_port(port); });
    cpu_->set_io_write_callback([this](uint16_t port, uint8_t data) { write_io_port(port, data); });
    cpu_->set_interrupt_callback([this](uint8_t int_num) { handle_interrupt(int_num); });

    ym2608_ = std::make_unique<LibvgmYm2608>();
    if (!ym2608_->initialize(7'987'200, static_cast<uint32_t>(sample_rate_))) {
        error = "failed to initialize YM2608 for Microcabin PC-98 DOS driver";
        return false;
    }

    auto* mem = cpu_->memory();
    if (mem == nullptr || cpu_->memory() == nullptr) {
        error = "failed to allocate PC-98 DOS memory";
        return false;
    }
    std::fill(mem, mem + kMemorySize, 0);
    mem[kIretOffset] = 0xcf;
    mem[kApiReturnOffset] = 0xf4;
    mem[(static_cast<uint32_t>(kMmdSegment) << 4) + kMmdReturnOffset] = 0xf4;

    const auto helper_linear = (static_cast<uint32_t>(kHelperSegment) << 4) + kHelperOffset;
    std::copy_n(mmd_helper_.begin(),
                std::min<size_t>(mmd_helper_.size(), kMemorySize - helper_linear),
                mem + helper_linear);

    const auto mmd_linear = (static_cast<uint32_t>(kMmdSegment) << 4) + kMmdOffset;
    std::copy_n(mmd_sys_.begin(),
                std::min<size_t>(mmd_sys_.size(), kMemorySize - mmd_linear),
                mem + mmd_linear);

    const uint16_t device_interrupt_offset = mmd_sys_.size() >= 10
        ? static_cast<uint16_t>(mmd_sys_[8] | (static_cast<uint16_t>(mmd_sys_[9]) << 8))
        : 0;
    mmd2_api_ = mmd_sys_.size() >= 16
        && std::memcmp(mmd_sys_.data() + 10, "MMD2", 4) == 0
        && device_interrupt_offset != 0x0057;
    early_mmd2_layout_ = device_interrupt_offset == 0x0041;

    setup_interrupt_vector(0x21, kIretSegment, kIretOffset);
    setup_interrupt_vector(0xD2, kMmdSegment, 0x009c);
    setup_interrupt_vector(0x7F, kIretSegment, kIretOffset);
    initialize_mmd_device();
    reset_runtime();
    run_cpu_steps(200000);
    cpu_->set_cs(kHelperSegment);
    cpu_->set_ds(kHelperSegment);
    cpu_->set_es(kHelperSegment);
    cpu_->set_pc(0x012c);
    debug_io_reads_ = 0;
    debug_io_writes_ = 0;
    debug_int21_ = 0;
    debug_intd2_ = 0;
    debug_opna_writes_ = 0;
    debug_opna_keyons_ = 0;
    debug_opna_bank1_writes_ = 0;
    debug_opna_ssg_writes_ = 0;
    debug_opna_rhythm_writes_ = 0;
    debug_opna_adpcm_b_writes_ = 0;
    debug_fm_keyons_by_channel_.fill(0);
    debug_mailbox_reads_ = 0;
    debug_file_reads_ = 0;
    debug_file_seeks_ = 0;
    debug_mmd_errors_ = 0;
    debug_mmd_select_errors_ = 0;
    debug_mmd_bgm_load_errors_ = 0;
    debug_mmd_voice_load_errors_ = 0;
    debug_mmd_play_errors_ = 0;
    debug_last_int_ = 0;
    debug_last_ah_ = 0;
    debug_last_mmd_command_ = 0;
    debug_last_mmd_return_ = 0;
    debug_last_mailbox_port_ = 0;
    debug_last_mailbox_value_ = 0;
    return true;
}

void MicrocabinPc98DosDriver::setup_interrupt_vector(uint8_t vector, uint16_t segment, uint16_t offset)
{
    if (!cpu_) {
        return;
    }
    auto* mem = cpu_->memory();
    const uint32_t base = static_cast<uint32_t>(vector) * 4;
    mem[base + 0] = static_cast<uint8_t>(offset & 0xff);
    mem[base + 1] = static_cast<uint8_t>((offset >> 8) & 0xff);
    mem[base + 2] = static_cast<uint8_t>(segment & 0xff);
    mem[base + 3] = static_cast<uint8_t>((segment >> 8) & 0xff);
}

void MicrocabinPc98DosDriver::initialize_mmd_device()
{
    if (!cpu_ || mmd_sys_.size() < 10) {
        return;
    }

    // MMD2 follows the DOS character-device header offsets. Keep the existing
    // MMD bridge entry points for later resident builds, whose pack bootstrap
    // contract uses the relocated callbacks directly.
    const uint16_t strategy_offset = mmd2_api_
        ? static_cast<uint16_t>(mmd_sys_[6] | (static_cast<uint16_t>(mmd_sys_[7]) << 8))
        : 0x004e;
    const uint16_t interrupt_offset = mmd2_api_
        ? static_cast<uint16_t>(mmd_sys_[8] | (static_cast<uint16_t>(mmd_sys_[9]) << 8))
        : 0x0057;

    const std::string declaration = mmd_device_command_.empty()
        ? std::string("MMD.SYS 3072 2048")
        : mmd_device_command_;
    const auto arguments = declaration.find_first_of(" \t");
    const std::string command = mmd2_api_
        ? (arguments == std::string::npos ? std::string() : declaration.substr(arguments))
        : declaration;
    for (size_t i = 0; i < command.size() && kDeviceCommandOffset + i < kMemorySize; ++i) {
        write_memory_byte(kDeviceCommandOffset + static_cast<uint32_t>(i),
                          static_cast<uint8_t>(command[i]));
    }
    write_memory_byte(kDeviceCommandOffset + static_cast<uint32_t>(std::min<size_t>(command.size(), 250)), 0x0d);
    write_memory_byte(kDeviceCommandOffset + static_cast<uint32_t>(std::min<size_t>(command.size() + 1, 251)), 0x00);

    for (uint16_t i = 0; i < 0x20; ++i) {
        write_memory_byte(kDeviceRequestOffset + i, 0);
    }
    write_memory_byte(kDeviceRequestOffset + 0x00, 0x18);
    write_memory_byte(kDeviceRequestOffset + 0x02, 0x00);
    write_memory_byte(kDeviceRequestOffset + 0x12, static_cast<uint8_t>(kDeviceCommandOffset & 0xff));
    write_memory_byte(kDeviceRequestOffset + 0x13, static_cast<uint8_t>((kDeviceCommandOffset >> 8) & 0xff));
    write_memory_byte(kDeviceRequestOffset + 0x14, 0x00);
    write_memory_byte(kDeviceRequestOffset + 0x15, 0x00);

    cpu_->set_es(0);
    cpu_->set_bx(kDeviceRequestOffset);
    cpu_->set_ss(kStackSegment);
    cpu_->set_sp(kStackPointer);
    cpu_->set_cs(kMmdSegment);
    cpu_->set_pc(strategy_offset);
    push_cpu_word(kHelperSegment);
    push_cpu_word(kApiReturnOffset);
    run_cpu_steps(1000);

    cpu_->set_es(0);
    cpu_->set_bx(kDeviceRequestOffset);
    cpu_->set_ss(kStackSegment);
    cpu_->set_sp(kStackPointer);
    cpu_->set_cs(kMmdSegment);
    cpu_->set_pc(interrupt_offset);
    push_cpu_word(kHelperSegment);
    push_cpu_word(kApiReturnOffset);
    run_cpu_steps(200000);
    const uint32_t mmd_base = (static_cast<uint32_t>(kMmdSegment) << 4) + kMmdOffset;
    if (early_mmd2_layout_) {
        uint32_t bgm_capacity = 0;
        uint32_t voice_capacity = 0;
        std::istringstream arguments_stream(command);
        arguments_stream >> bgm_capacity >> voice_capacity;
        bgm_capacity = std::min<uint32_t>((bgm_capacity + 15) & ~uint32_t{15}, 0xffff);
        voice_capacity = std::min<uint32_t>((voice_capacity + 15) & ~uint32_t{15}, 0xffff);
        const uint16_t initialized_bgm_capacity = static_cast<uint16_t>(
            read_memory_byte(mmd_base + 0x0c84)
            | (read_memory_byte(mmd_base + 0x0c85) << 8));
        if (bgm_capacity != 0 && initialized_bgm_capacity < bgm_capacity) {
            const auto write_mmd_word = [&](uint16_t offset, uint16_t value) {
                write_memory_byte(mmd_base + offset, static_cast<uint8_t>(value));
                write_memory_byte(mmd_base + offset + 1, static_cast<uint8_t>(value >> 8));
            };
            const uint16_t bgm_buffer_end = static_cast<uint16_t>(0x0f86 + bgm_capacity + 0x0400);
            const uint16_t resident_end = static_cast<uint16_t>(bgm_buffer_end + voice_capacity);
            const uint16_t timer_stack = static_cast<uint16_t>(resident_end + 0x0200);
            write_mmd_word(0x0c84, static_cast<uint16_t>(bgm_capacity));
            write_mmd_word(0x0c86, static_cast<uint16_t>(voice_capacity));
            write_mmd_word(0x0c82, static_cast<uint16_t>(bgm_capacity + voice_capacity + 0x0400));
            write_mmd_word(0x0c8a, bgm_buffer_end);
            write_mmd_word(0x0c8c, timer_stack);
            write_mmd_word(0x025c, static_cast<uint16_t>(timer_stack - 2));
            write_mmd_word(0x025e, kMmdSegment);
        }
    } else if (mmd2_api_ && interrupt_offset == 0x005d) {
        uint32_t bgm_capacity = 0;
        uint32_t voice_capacity = 0;
        std::istringstream arguments_stream(command);
        arguments_stream >> bgm_capacity >> voice_capacity;
        bgm_capacity = std::min<uint32_t>((bgm_capacity + 15) & ~uint32_t{15}, 0xffff);
        voice_capacity = std::min<uint32_t>((voice_capacity + 15) & ~uint32_t{15}, 0xffff);
        const uint16_t initialized_bgm_capacity = static_cast<uint16_t>(
            read_memory_byte(mmd_base + 0x0d16)
            | (read_memory_byte(mmd_base + 0x0d17) << 8));
        if (bgm_capacity != 0 && initialized_bgm_capacity < bgm_capacity) {
            const auto write_mmd_word = [&](uint16_t offset, uint16_t value) {
                write_memory_byte(mmd_base + offset, static_cast<uint8_t>(value));
                write_memory_byte(mmd_base + offset + 1, static_cast<uint8_t>(value >> 8));
            };
            const uint16_t bgm_buffer_end = static_cast<uint16_t>(0x109c + bgm_capacity + 0x0400);
            const uint16_t resident_end = static_cast<uint16_t>(bgm_buffer_end + voice_capacity);
            const uint16_t timer_stack = static_cast<uint16_t>(resident_end + 0x0200);
            write_mmd_word(0x0d16, static_cast<uint16_t>(bgm_capacity));
            write_mmd_word(0x0d18, static_cast<uint16_t>(voice_capacity));
            write_mmd_word(0x0d14, static_cast<uint16_t>(bgm_capacity + voice_capacity + 0x0400));
            write_mmd_word(0x0d1c, bgm_buffer_end);
            write_mmd_word(0x0d1e, timer_stack);
            write_mmd_word(0x02a0, static_cast<uint16_t>(timer_stack - 2));
            write_mmd_word(0x02a2, kMmdSegment);
        }
    }
    const uint8_t device_flags = read_memory_byte(mmd_base + 0x152c);
    const uint16_t address_port = static_cast<uint16_t>(read_memory_byte(mmd_base + 0x152e)
        | (read_memory_byte(mmd_base + 0x152f) << 8));
    const uint16_t data_port = static_cast<uint16_t>(read_memory_byte(mmd_base + 0x1530)
        | (read_memory_byte(mmd_base + 0x1531) << 8));
    if ((device_flags & 0x01) != 0 && address_port == 0 && data_port == 0) {
        write_memory_byte(mmd_base + 0x152e, 0x88);
        write_memory_byte(mmd_base + 0x152f, 0x01);
        write_memory_byte(mmd_base + 0x1530, 0x8a);
        write_memory_byte(mmd_base + 0x1531, 0x01);
    }
    const uint16_t bgm_size = static_cast<uint16_t>(read_memory_byte(mmd_base + 0x153e)
        | (read_memory_byte(mmd_base + 0x153f) << 8));
    const uint16_t voice_size = static_cast<uint16_t>(read_memory_byte(mmd_base + 0x1540)
        | (read_memory_byte(mmd_base + 0x1541) << 8));
    if (bgm_size < 0x0400 || voice_size == 0) {
        const uint16_t voice_buffer = 0x19e8;
        const uint16_t bgm_buffer = 0x21e8;
        const uint16_t end_buffer = 0x2de8;
        write_memory_byte(mmd_base + 0x153e, 0x00);
        write_memory_byte(mmd_base + 0x153f, 0x0c);
        write_memory_byte(mmd_base + 0x1540, 0x00);
        write_memory_byte(mmd_base + 0x1541, 0x08);
        write_memory_byte(mmd_base + 0x1544, static_cast<uint8_t>(voice_buffer & 0xff));
        write_memory_byte(mmd_base + 0x1545, static_cast<uint8_t>((voice_buffer >> 8) & 0xff));
        write_memory_byte(mmd_base + 0x1542, static_cast<uint8_t>(bgm_buffer & 0xff));
        write_memory_byte(mmd_base + 0x1543, static_cast<uint8_t>((bgm_buffer >> 8) & 0xff));
        write_memory_byte(mmd_base + 0x1546, static_cast<uint8_t>(end_buffer & 0xff));
        write_memory_byte(mmd_base + 0x1547, static_cast<uint8_t>((end_buffer >> 8) & 0xff));
    }
    const uint16_t output_callback = static_cast<uint16_t>(read_memory_byte(mmd_base + 0x19be)
        | (read_memory_byte(mmd_base + 0x19bf) << 8));
    if (output_callback == 0) {
        write_memory_byte(mmd_base + 0x19be, 0x01);
        write_memory_byte(mmd_base + 0x19bf, 0x10);
    }
    cpu_->set_cs(kHelperSegment);
    cpu_->set_pc(kApiReturnOffset);
}

void MicrocabinPc98DosDriver::push_cpu_word(uint16_t value)
{
    if (!cpu_) {
        return;
    }
    const auto sp = static_cast<uint16_t>(cpu_->get_sp() - 2);
    cpu_->set_sp(sp);
    const uint32_t linear = (static_cast<uint32_t>(cpu_->get_ss()) << 4) + sp;
    write_memory_byte(linear, static_cast<uint8_t>(value & 0xff));
    write_memory_byte(linear + 1, static_cast<uint8_t>((value >> 8) & 0xff));
}

void MicrocabinPc98DosDriver::trigger_interrupt_vector(uint8_t vector)
{
    if (!cpu_) {
        return;
    }
    const uint32_t base = static_cast<uint32_t>(vector) * 4;
    const uint16_t target_offset = static_cast<uint16_t>(read_memory_byte(base) | (read_memory_byte(base + 1) << 8));
    const uint16_t target_segment = static_cast<uint16_t>(read_memory_byte(base + 2) | (read_memory_byte(base + 3) << 8));
    if (target_segment == 0 && target_offset == 0) {
        return;
    }

    push_cpu_word(0xf000);
    push_cpu_word(cpu_->get_cs());
    push_cpu_word(cpu_->get_pc());
    cpu_->set_cs(target_segment);
    cpu_->set_pc(target_offset);
}

void MicrocabinPc98DosDriver::reset_runtime()
{
    if (!cpu_) {
        return;
    }
    cpu_->reset();
    cpu_->set_cs(kHelperSegment);
    cpu_->set_ds(kHelperSegment);
    cpu_->set_es(kHelperSegment);
    cpu_->set_ss(kStackSegment);
    cpu_->set_sp(kStackPointer);
    cpu_->set_pc(kHelperOffset);
    executed_cpu_steps_ = 0;
}

void MicrocabinPc98DosDriver::run_cpu_steps(int steps)
{
    if (!cpu_ || steps <= 0) {
        return;
    }
    const auto executed = cpu_->execute(steps);
    executed_cpu_steps_ += static_cast<uint64_t>(std::max(0, executed));
}

uint8_t MicrocabinPc98DosDriver::read_memory_byte(uint32_t address)
{
    if (!cpu_ || address >= kMemorySize) {
        return 0xff;
    }
    return cpu_->memory()[address];
}

void MicrocabinPc98DosDriver::write_memory_byte(uint32_t address, uint8_t data)
{
    if (!cpu_ || address >= kMemorySize) {
        return;
    }
    cpu_->memory()[address] = data;
}

uint8_t MicrocabinPc98DosDriver::read_io_port(uint16_t port)
{
    ++debug_io_reads_;
    switch (port) {
    case 0x007e0:
        ++debug_mailbox_reads_;
        debug_last_mailbox_port_ = static_cast<uint8_t>(port & 0xff);
        if (command_pending_) {
            command_pending_ = false;
            debug_last_mailbox_value_ = command_latch_;
            return command_latch_;
        }
        debug_last_mailbox_value_ = 0;
        return 0;
    case 0x007e2:
        ++debug_mailbox_reads_;
        debug_last_mailbox_port_ = static_cast<uint8_t>(port & 0xff);
        debug_last_mailbox_value_ = command_low_;
        return command_low_;
    case 0x007e3:
        ++debug_mailbox_reads_;
        debug_last_mailbox_port_ = static_cast<uint8_t>(port & 0xff);
        debug_last_mailbox_value_ = command_high_;
        return command_high_;
    case 0x007e4:
        ++debug_mailbox_reads_;
        debug_last_mailbox_port_ = static_cast<uint8_t>(port & 0xff);
        debug_last_mailbox_value_ = selected_bgm_data_.empty() ? 0 : selected_file_handle_;
        return debug_last_mailbox_value_;
    case 0x0088:
    case 0x008b:
        return ym2608_ ? ym2608_->read(0) : 0xff;
    case 0x0089:
    case 0x008a:
        return opna_registers_[0][current_opna_address_[0]];
    case 0x008c:
    case 0x008f:
        return ym2608_ ? ym2608_->read(1) : 0xff;
    case 0x008d:
    case 0x008e:
        return opna_registers_[1][current_opna_address_[1]];
    case 0x0188:
        return ym2608_ ? ym2608_->read(0) : 0xff;
    case 0x018a:
        return opna_registers_[0][current_opna_address_[0]];
    case 0x018c:
        return ym2608_ ? ym2608_->read(1) : 0xff;
    case 0x018e:
        return opna_registers_[1][current_opna_address_[1]];
    default:
        return 0xff;
    }
}

void MicrocabinPc98DosDriver::write_io_port(uint16_t port, uint8_t data)
{
    ++debug_io_writes_;
    if (!ym2608_) {
        return;
    }
    if (port == 0x0088 || port == 0x0188) {
        current_opna_address_[0] = data;
        ym2608_->write(0, data);
    } else if (port == 0x0089 || port == 0x008a || port == 0x018a) {
        ++debug_opna_writes_;
        if (current_opna_address_[0] < 0x10) {
            ++debug_opna_ssg_writes_;
        } else if (current_opna_address_[0] >= 0x10 && current_opna_address_[0] < 0x20) {
            ++debug_opna_rhythm_writes_;
        }
        if (current_opna_address_[0] == 0x28 && (data & 0xf0) != 0) {
            ++debug_opna_keyons_;
            uint8_t channel = data & 0x03;
            if (channel != 3) {
                if ((data & 0x04) != 0) {
                    channel = static_cast<uint8_t>(channel + 3);
                }
                if (channel < debug_fm_keyons_by_channel_.size()) {
                    ++debug_fm_keyons_by_channel_[channel];
                }
            }
        }
        opna_registers_[0][current_opna_address_[0]] = data;
        ym2608_->write(1, data);
    } else if (port == 0x008c || port == 0x018c) {
        current_opna_address_[1] = data;
        ym2608_->write(2, data);
    } else if (port == 0x008d || port == 0x008e || port == 0x018e) {
        ++debug_opna_writes_;
        ++debug_opna_bank1_writes_;
        if (current_opna_address_[1] < 0x10) {
            ++debug_opna_adpcm_b_writes_;
        }
        opna_registers_[1][current_opna_address_[1]] = data;
        ym2608_->write(3, data);
    }
}

void MicrocabinPc98DosDriver::handle_interrupt(uint8_t int_num)
{
    debug_last_int_ = int_num;
    if (int_num == 0x21) {
        handle_dos_interrupt();
    } else if (int_num == 0xD2) {
        handle_mmd_interrupt();
    }
}

void MicrocabinPc98DosDriver::handle_dos_interrupt()
{
    if (!cpu_) {
        return;
    }
    ++debug_int21_;
    const uint8_t ah = cpu_->get_ah();
    debug_last_ah_ = ah;
    switch (ah) {
    case 0x25: {
        const uint8_t vector = cpu_->get_al();
        const uint16_t segment = cpu_->get_ds();
        const uint16_t offset = cpu_->get_dx();
        setup_interrupt_vector(vector, segment, offset);
        if (mmd_timer_vector_ == 0 && segment == kMmdSegment
            && vector != 0xD2 && offset != 0x0078) {
            mmd_timer_vector_ = vector;
        }
        cpu_->set_carry(false);
        break;
    }
    case 0x3f:
        dos_read_selected_file();
        break;
    case 0x42:
        dos_seek_selected_file();
        break;
    default:
        cpu_->set_carry(false);
        break;
    }
}

void MicrocabinPc98DosDriver::handle_mmd_interrupt()
{
    if (!cpu_) {
        return;
    }
    ++debug_intd2_;
    debug_last_ah_ = cpu_->get_ah();
}

void MicrocabinPc98DosDriver::dos_read_selected_file()
{
    if (!cpu_) {
        return;
    }
    ++debug_file_reads_;
    const auto requested = static_cast<size_t>(cpu_->get_cx());
    const auto remaining = selected_bgm_data_.size() > selected_file_offset_
        ? selected_bgm_data_.size() - selected_file_offset_
        : 0;
    const auto count = std::min(requested, remaining);
    const uint32_t dest = (static_cast<uint32_t>(cpu_->get_ds()) << 4) + cpu_->get_dx();
    for (size_t i = 0; i < count && dest + i < kMemorySize; ++i) {
        write_memory_byte(dest + static_cast<uint32_t>(i), selected_bgm_data_[selected_file_offset_ + i]);
    }
    selected_file_offset_ += count;
    cpu_->set_ax(static_cast<uint16_t>(count));
    cpu_->set_carry(false);
}

void MicrocabinPc98DosDriver::dos_seek_selected_file()
{
    if (!cpu_) {
        return;
    }
    ++debug_file_seeks_;
    const uint32_t offset = (static_cast<uint32_t>(cpu_->get_cx()) << 16) | cpu_->get_dx();
    switch (cpu_->get_al()) {
    case 0:
        selected_file_offset_ = std::min<size_t>(offset, selected_bgm_data_.size());
        break;
    case 1:
        selected_file_offset_ = std::min<size_t>(selected_file_offset_ + offset, selected_bgm_data_.size());
        break;
    case 2:
        selected_file_offset_ = offset > selected_bgm_data_.size() ? 0 : selected_bgm_data_.size() - offset;
        break;
    default:
        break;
    }
    cpu_->set_ax(static_cast<uint16_t>(selected_file_offset_ & 0xffff));
    cpu_->set_dx(static_cast<uint16_t>((selected_file_offset_ >> 16) & 0xffff));
    cpu_->set_carry(false);
}

void MicrocabinPc98DosDriver::copy_to_transfer_buffer(const uint8_t* data, size_t size)
{
    const uint32_t dest = (static_cast<uint32_t>(kTransferSegment) << 4) + kTransferOffset;
    const auto count = std::min(size, kTransferSize);
    for (size_t i = 0; i < count && dest + i < kMemorySize; ++i) {
        write_memory_byte(dest + static_cast<uint32_t>(i), data[i]);
    }
}

void MicrocabinPc98DosDriver::load_mmd_stream(uint8_t command, const std::vector<uint8_t>& data)
{
    size_t offset = 0;
    while (offset < data.size()) {
        const auto count = std::min(kTransferSize, data.size() - offset);
        copy_to_transfer_buffer(data.data() + offset, count);
        call_mmd_api(static_cast<uint16_t>(command) << 8,
                     kTransferOffset,
                     static_cast<uint16_t>(count),
                     kTransferSegment);
        offset += count;
    }
}

void MicrocabinPc98DosDriver::call_mmd_api(uint16_t ax, uint16_t bx, uint16_t cx, uint16_t dx)
{
    if (!cpu_) {
        return;
    }

    cpu_->set_ax(ax);
    cpu_->set_bx(bx);
    cpu_->set_cx(cx);
    cpu_->set_dx(dx);
    cpu_->set_ds(kTransferSegment);
    cpu_->set_es(kTransferSegment);
    cpu_->set_ss(kStackSegment);
    cpu_->set_sp(kStackPointer);
    cpu_->set_cs(kHelperSegment);
    cpu_->set_pc(kApiReturnOffset);
    trigger_interrupt_vector(0xD2);
    run_cpu_steps(2000000);
    debug_last_mmd_command_ = ax;
    debug_last_mmd_return_ = cpu_->get_al();
    if (debug_last_mmd_return_ != 0) {
        ++debug_mmd_errors_;
        const uint8_t command = static_cast<uint8_t>((ax >> 8) & 0xff);
        if (command == 0x03) {
            ++debug_mmd_select_errors_;
        } else if (command == 0x10) {
            ++debug_mmd_bgm_load_errors_;
        } else if (command == 0x11) {
            ++debug_mmd_voice_load_errors_;
        } else if (command == 0x01) {
            ++debug_mmd_play_errors_;
        }
    }
    cpu_->set_cs(kHelperSegment);
    cpu_->set_pc(kApiReturnOffset);
}

void MicrocabinPc98DosDriver::call_mmd_timer()
{
    if (!cpu_) {
        return;
    }

    cpu_->set_ss(kStackSegment);
    cpu_->set_sp(kStackPointer);
    if (mmd2_api_) {
        if (mmd_timer_vector_ == 0) {
            return;
        }
        cpu_->set_cs(kHelperSegment);
        cpu_->set_pc(kApiReturnOffset);
        trigger_interrupt_vector(mmd_timer_vector_);
    } else {
        const uint32_t mmd_base = (static_cast<uint32_t>(kMmdSegment) << 4) + kMmdOffset;
        const uint16_t timer_stack = static_cast<uint16_t>(read_memory_byte(mmd_base + 0x036e)
            | (read_memory_byte(mmd_base + 0x036f) << 8));
        if (timer_stack == 0) {
            write_memory_byte(mmd_base + 0x036e, 0xe6);
            write_memory_byte(mmd_base + 0x036f, 0x2f);
            write_memory_byte(mmd_base + 0x0370, static_cast<uint8_t>(kMmdSegment));
            write_memory_byte(mmd_base + 0x0371, static_cast<uint8_t>(kMmdSegment >> 8));
        }
        cpu_->set_ds(kMmdSegment);
        cpu_->set_es(kMmdSegment);
        push_cpu_word(0x0200);
        push_cpu_word(kMmdSegment);
        push_cpu_word(kMmdReturnOffset);
        cpu_->set_cs(kMmdSegment);
        cpu_->set_pc(0x0376);
    }
    run_cpu_steps(2000000);
    cpu_->set_cs(kHelperSegment);
    cpu_->set_pc(kApiReturnOffset);
}

double MicrocabinPc98DosDriver::mmd_timer_rate_hz() const
{
    if (const char* value = std::getenv("HOOT_MMD_TIMER_HZ")) {
        const double override_hz = std::strtod(value, nullptr);
        if (override_hz > 0.0) {
            return override_hz;
        }
    }

    constexpr double kPc98OpnaClock = 7987200.0;
    // The original MMD2 DOS host dispatches two service phases per raw Timer A
    // period. Later resident builds use the established half-rate callback.
    const double opna_timer_clock_divisor = mmd2_api_ ? 36.0 : 144.0;
    const uint16_t timer_a = static_cast<uint16_t>(
        (static_cast<uint16_t>(opna_registers_[0][0x24]) << 2)
        | (opna_registers_[0][0x25] & 0x03));
    const int period = 1024 - static_cast<int>(timer_a);
    if (period <= 0) {
        return 60.0;
    }
    return (kPc98OpnaClock / opna_timer_clock_divisor) / static_cast<double>(period);
}

} // namespace hoot
