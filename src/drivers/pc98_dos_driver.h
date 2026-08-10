#pragma once

#include <cstdint>
#include <array>
#include <fstream>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "cpu/x86_cpu.h"
#include "drivers/hoot_driver.h"
#include "sound/libvgm_ym2203.h"
#include "sound/libvgm_ym2608.h"
#include "sound/libvgm_opl.h"
#include "sound/pc98_pcm86.h"
#include "sound/pc98_beep.h"
#include "sound/pc98_mpu401.h"
#include "sound/midi_synth.h"
#include "sound/midi_visualizer.h"

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
    void fill_visual_state(const HootEntry& entry, int track_index,
                           HootVisualState& out) const override;
    bool channel_mute_supported(int kind, int index) const override;
    bool set_channel_muted(int kind, int index, bool muted) override;
    void clear_channel_mutes() override;
    const char* name() const override;

private:
    struct LoadedFile {
        std::string path;
        std::vector<uint8_t> data;
    };

    struct ShellProgram {
        std::string command;
        std::vector<uint8_t> data;
    };

    struct DosOpenFile {
        const std::vector<uint8_t>* data = nullptr;
        size_t offset = 0;
        std::string path;
    };

    struct BareChunk {
        uint32_t address = 0;
        std::vector<uint8_t> data;
    };

    enum class DriverType {
        Unknown,
        PMD,
        MMD,
        Shell,
        Bare,
    };

    void clear();
    bool setup_memory();
    void setup_interrupt_vectors();
    void setup_pit();
    uint8_t read_memory_byte(uint32_t address) const;
    void write_memory_byte(uint32_t address, uint8_t data);
    uint32_t bare_linear_address(uint16_t segment, uint16_t offset) const;
    bool bare_load_asset(bool secondary, uint32_t slot, uint32_t address,
                         uint32_t reserve, uint32_t file_offset, uint32_t requested_size,
                         std::string* error = nullptr);
    void execute_bare_hoot_function(uint8_t function);
    uint8_t read_io_port(uint16_t port);
    void write_io_port(uint16_t port, uint8_t data);
    void write_opn(uint8_t port, uint8_t data);
    uint8_t read_opn(uint8_t port);
    void render_opn(int16_t* interleaved_stereo, int frames);
    bool read_special_board_port(uint16_t port, uint8_t& value);
    bool write_special_board_port(uint16_t port, uint8_t data);
    void render_special_boards(int16_t* interleaved_stereo, int frames);
    void reset_special_boards();
    void arm_amd98_timer();
    void service_amd98_timer_irq();
    void trigger_amd98_rhythm(uint8_t map);
    void setup_midi(const HootEntry& entry);
    void reset_midi_synth_mode();
    void handle_midi_message(const X68kMidiMessage& message);
    void render_midi(int16_t* interleaved_stereo, int frames);
    void reset_opn();
    void apply_opn_fm_tl_compat(uint8_t channel);
    void handle_interrupt(uint8_t int_num);
    void handle_dos_interrupt();
    std::string read_dos_string(uint16_t segment, uint16_t offset) const;
    void dos_open_file();
    void dos_find_first();
    void dos_read_file();
    void dos_close_file();
    void dos_seek_file();
    const LoadedFile* find_dos_file(const std::string& name) const;
    void pit_timer_tick();
    void reset_cpu_context(uint16_t segment = kProgramSegment);
    void run_cpu_steps(int steps);
    void push_cpu_word(uint16_t value);
    void setup_interrupt_vector(uint8_t vector, uint16_t segment, uint16_t offset);
    bool is_interrupt_vector_active(uint8_t vector) const;
    void trigger_interrupt_vector(uint8_t vector, int steps = 200000);
    void trigger_async_interrupt_vector(uint8_t vector, int steps = 256);
    void trigger_far_interrupt(uint16_t segment, uint16_t offset, int steps = 200000);
    void trigger_near_subroutine(uint16_t segment, uint16_t offset, int steps = 200000);
    void update_fm_timer(uint8_t reg, uint8_t data);
    void refresh_fm_irq_interval();
    void service_fm_timer_irq(uint8_t status_bit);
    void service_pcm86_irq();
    void service_mpu401_irq();
    int current_mpu_irq_line() const;
    void install_shell_driver();
    bool rebuild_shell_runtime();
    void load_shell_program(const ShellProgram& program, uint16_t segment);
    void setup_shell_psp(const std::string& command, uint16_t segment);
    void run_shell_program(const ShellProgram& program, uint16_t segment, int steps = 2000000);
    void call_shell_player_api(uint16_t ax, uint16_t ds = 0, uint16_t dx = 0);
    void load_hhd98_track();
    void emit_trace_event(const std::string& json);
    void trace_cpu_event(const char* type,
                         uint8_t opcode,
                         uint16_t from_cs,
                         uint16_t from_ip,
                         uint16_t to_cs,
                         uint16_t to_ip);
    void trace_io_event(const char* type, uint16_t port, uint8_t value);
    void trace_interrupt_event(uint8_t int_num);
    bool is_playing() const { return playing_; }

    std::map<uint32_t, LoadedFile> files_by_slot_;
    std::map<uint32_t, LoadedFile> files2_by_slot_;
    std::map<std::string, LoadedFile> files_by_name_;
    std::vector<BareChunk> bare_chunks_;
    std::map<uint16_t, DosOpenFile> dos_open_files_;
    uint16_t next_dos_handle_ = 5;
    uint16_t dos_dta_segment_ = 0;
    uint16_t dos_dta_offset_ = 0x0080;
    std::vector<uint8_t> driver_data_;
    std::vector<uint8_t> shell_command_;
    std::vector<ShellProgram> shell_programs_;
    std::string selected_bgm_path_;
    std::string selected_voice_path_;
    std::vector<uint8_t> selected_bgm_data_;
    size_t selected_file_offset_ = 0;
    uint16_t selected_file_handle_ = 5;
    bool selected_file_open_ = false;
    bool bridge_load_pending_ = false;
    bool bridge_command_active_ = false;
    uint8_t bridge_command_ = 0xff;
    uint16_t bridge_argument_ = 0xffff;
    DriverType driver_type_ = DriverType::Unknown;
    bool uses_hhd98_bridge_ = false;
    bool uses_pmd98_bridge_ = false;
    bool bridge_stdin_filename_ = false;
    bool bare_mode_ = false;
    bool pc88va_mode_ = false;
    bool bare_segmented_addresses_ = false;
    uint16_t bare_boot_cs_ = 0x0060;
    uint16_t bare_boot_ip_ = 0x0000;
    uint32_t bare_data_address_ = 0;
    uint32_t bare_data2_address_ = 0;
    bool bare_has_data_address_ = false;
    bool bare_has_data2_address_ = false;
    uint32_t bare_file_size_ = 0;
    uint32_t bare_file2_size_ = 0;
    std::array<uint16_t, 3> bare_hoot_params_{};
    uint8_t bare_hoot_status_ = 0;
    uint8_t bare_interrupt_reason_ = 0xff;
    bool bare_load_occurred_ = false;
    bool bare_hoot_interrupts_enabled_ = true;
    uint8_t function_vector_ = 0x7f;

    std::unique_ptr<X86Cpu> cpu_;
    std::unique_ptr<LibvgmYm2203> ym2203_;
    std::unique_ptr<LibvgmYm2608> ym2608_;
    std::unique_ptr<LibvgmOpl> opl_;
    std::array<std::unique_ptr<LibvgmYm2203>, 3> amd_psg_{};
    std::array<std::unique_ptr<LibvgmYm2608>, 5> px_opna_{};
    std::unique_ptr<Pc98Pcm86> pcm86_;
    std::unique_ptr<Pc98Beep> beep_;
    std::unique_ptr<Pc98Mpu401> mpu401_;
    std::unique_ptr<MidiSynth> midi_synth_;
    MidiVisualizer midi_visualizer_;
    bool use_ym2203_ = false;
    bool use_sound_orchestra_ = false;
    bool use_sb16_ = false;
    bool use_amd98_ = false;
    bool use_px_ = false;
    bool use_px2_ = false;
    bool pc9821_mode_ = false;
    bool use_beep_ = false;
    bool use_pcm86_ = false;
    double opl_gain_ = 1.0;
    std::array<uint8_t, 3> amd_psg_address_{};
    std::array<uint8_t, 3> amd_psg_portb_{};
    uint8_t amd_psg3_address_ = 0;
    uint16_t amd_timer_count_ = 0;
    uint8_t amd_timer_control_ = 0;
    bool amd_timer_low_pending_ = true;
    int amd_timer_interval_frames_ = 0;
    int amd_timer_frames_until_next_ = 0;
    std::array<double, 4> amd_rhythm_phase_{};
    std::array<double, 4> amd_rhythm_level_{};
    std::array<std::array<uint8_t, 2>, 5> px_address_{};
    std::vector<uint8_t> sb_dsp_fifo_;
    size_t sb_dsp_fifo_read_ = 0;
    uint8_t sb_dsp_command_ = 0;
    int sb_dsp_args_needed_ = 0;
    std::array<uint8_t, 4> sb_dsp_args_{};
    int sb_dsp_args_received_ = 0;
    bool sb_reset_high_ = false;
    bool sb_speaker_on_ = false;
    uint8_t sb_test_reg_ = 0;
    bool midi_enabled_ = false;
    int midiout_type_ = -1;
    double midi_gain_ = 0.70;
    uint64_t debug_midi_irq_count_ = 0;
    uint64_t debug_midi_synth_frames_ = 0;
    uint64_t debug_midi_sysex_handled_ = 0;
    int selected_mpu_irq_line_ = -1;
    uint8_t pic_master_mask_ = 0xff;
    uint8_t pic_slave_mask_ = 0xff;
    bool vrtc_phase_ = false;
    std::string driver_warning_;
    double pcm86_gain_ = 1.0;
    double beep_gain_ = 1.0;
    uint32_t ui_opn_mute_mask_ = 0;
    uint32_t ui_ssg_mute_mask_ = 0;
    uint32_t ui_opl_mute_mask_ = 0;
    uint16_t ui_midi_mute_mask_ = 0;
    bool ui_pcm86_muted_ = false;
    bool ui_beep_muted_ = false;
    uint64_t debug_beep_vrtc_irqs_ = 0;
    int clock_multiplier_ = 8;

    std::vector<int16_t> mix_buffer_;
    std::vector<int16_t> board_mix_buffer_;

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
    uint16_t fm_timer_a_ = 0;
    uint8_t fm_timer_b_ = 0;
    uint8_t fm_mode_ = 0;
    uint8_t fm_status_ = 0;
    int fm_timer_a_interval_frames_ = 0;
    int fm_timer_a_frames_until_next_ = 0;
    int fm_timer_b_interval_frames_ = 0;
    int fm_timer_b_frames_until_next_ = 0;
    uint32_t debug_fm_timer_irqs_ = 0;
    uint8_t current_opna_address_[2] = {0, 0};
    uint32_t debug_opna_writes_ = 0;
    uint32_t debug_opna_keyons_ = 0;
    uint32_t debug_opna_keyoffs_ = 0;
    uint8_t debug_last_key_command_ = 0;
    uint32_t debug_opna_bank1_writes_ = 0;
    uint32_t debug_opna_ssg_writes_ = 0;
    uint32_t debug_opna_rhythm_writes_ = 0;
    uint32_t debug_opna_rhythm_keyons_ = 0;
    uint32_t debug_opna_rhythm_keyoffs_ = 0;
    uint8_t debug_last_rhythm_command_ = 0;
    std::array<uint32_t, 16> debug_ssg_writes_by_reg_{};
    std::array<uint8_t, 16> debug_last_ssg_regs_{};
    std::array<uint32_t, 6> debug_fm_keyons_by_channel_{};
    std::array<uint32_t, 16> debug_keyon_masks_{};
    uint16_t debug_last_opna_reg_ = 0;
    uint8_t debug_last_opna_data_ = 0;
    bool trace_opna_ = false;
    uint32_t trace_opna_events_ = 0;
    uint32_t trace_opna_limit_ = 0;
    bool disable_opn_tl_compat_ = false;
    uint64_t rendered_frames_ = 0;
    std::array<std::array<uint8_t, 256>, 2> opna_registers_{};
    std::array<bool, 6> opna_key_on_{};
    uint32_t debug_file_opens_ = 0;
    uint32_t debug_file_open_matches_ = 0;
    uint32_t debug_file_reads_ = 0;
    uint32_t debug_last_open_name_ = 0;
    uint16_t dos_alloc_segment_ = 0x2000;
    uint16_t bridge_buffer_segment_ = 0;
    uint16_t current_shell_tsr_paragraphs_ = 0;
    uint16_t current_psp_segment_ = kProgramSegment;
    uint16_t shell_entry_cs_ = kProgramSegment;
    uint16_t shell_entry_ip_ = kDosEntryPoint;
    uint16_t shell_stack_ss_ = kProgramSegment;
    uint16_t shell_stack_sp_ = 0xfffe;
    size_t installed_shell_programs_ = 0;
    bool trace_dos_ = false;
    std::ofstream trace_file_;
    uint32_t trace_events_ = 0;
    uint32_t trace_event_limit_ = 0;
    bool trace_pc98_ = false;
    bool shell_async_interrupts_ = false;
    bool suppress_async_interrupts_ = false;

    static constexpr uint32_t kDosMemorySize = 64 * 1024;
    static constexpr uint16_t kProgramSegment = 0x1000;
    static constexpr uint32_t kDosEntryPoint = 0x0100;
    static constexpr uint16_t kIretOffset = 0x00f0;
    static constexpr uint16_t kHaltOffset = 0x00f1;
    static constexpr uint16_t kPc88vaHaltSegment = 0xf000;
    static constexpr uint16_t kPc88vaHaltOffset = 0xfff0;
    static constexpr uint16_t kBridgeBufferOffset = 0x0000;
    static constexpr uint16_t kBridgeBufferParagraphs = 0x0010;
    static constexpr uint16_t kResidentDataOffset = 0x4000;
    static constexpr uint16_t kTransferOffset = 0x8000;
    static constexpr uint16_t kPitIoport = 0x0080;
    static constexpr uint8_t kYm2608Clock = 8;
};

} // namespace hoot
