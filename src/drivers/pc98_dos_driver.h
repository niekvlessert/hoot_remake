#pragma once

#include <cstdint>
#include <array>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "cpu/x86_cpu.h"
#include "drivers/hoot_driver.h"
#include "sound/libvgm_ym2608.h"

namespace hoot {

class Pc98DosDriver final : public HootDriver {
public:
    Pc98DosDriver();
    ~Pc98DosDriver() override;

    HootResult load(const HootEntry& entry,
                    const std::string& packs_path,
                    int sample_rate,
                    std::string& error) override;
    HootResult select_track(const HootEntry& entry,
                            int track_index,
                            std::string& error) override;
    void reset() override;
    int render_s16(int16_t* interleaved_stereo, int frames) override;
    int render_float(float* interleaved_stereo, int frames) override;
    void fill_track_info(const HootEntry& entry,
                         int track_index,
                         HootTrackInfo& out) const override;
    const char* name() const override;

private:
    struct LoadedFile {
        std::string path;
        std::vector<uint8_t> data;
    };

    enum class DriverType {
        Unknown,
        PMD,
        MMD,
        Shell,
    };

    void clear();
    bool setup_memory();
    void setup_interrupt_vectors();
    void setup_pit();
    uint8_t read_memory_byte(uint32_t address);
    void write_memory_byte(uint32_t address, uint8_t data);
    uint8_t read_io_port(uint16_t port);
    void write_io_port(uint16_t port, uint8_t data);
    void apply_opn_fm_tl_compat(uint8_t channel);
    void handle_interrupt(uint8_t int_num);
    void handle_dos_interrupt();
    std::string read_dos_string(uint16_t segment, uint16_t offset) const;
    void dos_open_file();
    void dos_read_file();
    void dos_close_file();
    void pit_timer_tick();
    void reset_cpu_context();
    void run_cpu_steps(int steps);
    void push_cpu_word(uint16_t value);
    void setup_interrupt_vector(uint8_t vector, uint16_t segment, uint16_t offset);
    void trigger_interrupt_vector(uint8_t vector, int steps = 200000);
    void install_shell_driver();
    void call_shell_player_api(uint16_t ax, uint16_t ds = 0, uint16_t dx = 0);
    bool is_playing() const { return playing_; }

    std::map<uint32_t, LoadedFile> files_by_slot_;
    std::vector<uint8_t> driver_data_;
    std::vector<uint8_t> shell_command_;
    std::string selected_bgm_path_;
    std::string selected_voice_path_;
    std::vector<uint8_t> selected_bgm_data_;
    size_t selected_file_offset_ = 0;
    uint16_t selected_file_handle_ = 5;
    bool selected_file_open_ = false;
    DriverType driver_type_ = DriverType::Unknown;

    std::unique_ptr<X86Cpu> cpu_;
    std::unique_ptr<LibvgmYm2608> ym2608_;

    std::vector<int16_t> mix_buffer_;

    std::vector<uint8_t> int_vector_table_;
    std::vector<uint8_t> dos_memory_;

    int sample_rate_ = 44100;
    int selected_track_ = 0;
    uint32_t selected_code_ = 0;
    bool loaded_ = false;
    bool playing_ = false;

    uint32_t pit_counter_ = 0;
    uint32_t pit_rate_ = 0;
    uint32_t pit_target_ = 0;
    uint64_t executed_cpu_steps_ = 0;
    double timer_frames_until_tick_ = 0.0;
    uint8_t current_opna_address_[2] = {0, 0};
    uint32_t debug_opna_writes_ = 0;
    uint32_t debug_opna_keyons_ = 0;
    uint32_t debug_opna_keyoffs_ = 0;
    uint8_t debug_last_key_command_ = 0;
    uint32_t debug_opna_bank1_writes_ = 0;
    uint32_t debug_opna_ssg_writes_ = 0;
    uint32_t debug_opna_rhythm_writes_ = 0;
    std::array<uint32_t, 6> debug_fm_keyons_by_channel_{};
    std::array<uint32_t, 16> debug_keyon_masks_{};
    uint16_t debug_last_opna_reg_ = 0;
    uint8_t debug_last_opna_data_ = 0;
    std::array<std::array<uint8_t, 256>, 2> opna_registers_{};
    uint32_t debug_file_opens_ = 0;
    uint32_t debug_file_open_matches_ = 0;
    uint32_t debug_file_reads_ = 0;
    uint32_t debug_last_open_name_ = 0;

    static constexpr uint32_t kDosMemorySize = 64 * 1024;
    static constexpr uint16_t kProgramSegment = 0x1000;
    static constexpr uint32_t kDosEntryPoint = 0x0100;
    static constexpr uint16_t kIretOffset = 0x00f0;
    static constexpr uint16_t kHaltOffset = 0x00f1;
    static constexpr uint16_t kBridgeBufferOffset = 0x0174;
    static constexpr uint16_t kResidentDataOffset = 0x4000;
    static constexpr uint16_t kTransferOffset = 0x8000;
    static constexpr uint16_t kPitIoport = 0x0080;
    static constexpr uint8_t kYm2608Clock = 8;
};

} // namespace hoot
