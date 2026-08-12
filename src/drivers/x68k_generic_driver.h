#pragma once

#include <array>
#include <cstdint>
#include <fstream>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "drivers/hoot_driver.h"
#include "cpu/musashi_bus.h"
#include "sound/libvgm_okim6258.h"
#include "sound/libvgm_ym2151.h"
#include "sound/midi_synth.h"
#include "sound/midi_visualizer.h"
#include "sound/x68k_midi_transport.h"
#include "sound/x68k_pcm8_mixer.h"

namespace hoot {

class X68kGenericDriver final : public HootDriver, public MusashiBus {
public:
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

    uint8_t read_memory_8(uint32_t address) override;
    void write_memory_8(uint32_t address, uint8_t data) override;
    int acknowledge_interrupt(int level) override;
    void instruction_hook(uint32_t pc) override;

private:
    enum class StartupPolicy : uint8_t {
        Auto = 0,
        Native = 1,
        LegacyHoot = 2,
    };

    struct VoiceBankImage {
        uint32_t offset = 0;
        std::vector<uint8_t> data;
    };

    // Modern Hoot X68000 catalogues place packed music/PCM data throughout
    // the writable main-memory window, up to the start of the real I/O area.
    // Device addresses at 0xe80000 and above are still decoded before the
    // fallback scratch window.
    static constexpr size_t kRomSize = 0xe80000;
    static constexpr size_t kRamSize = 0x10000;
    static constexpr size_t kScratchSize = 0x100000;
    static constexpr size_t kHighMemorySize = 0x10000;

    void clear();
    void execute_seconds(double seconds);
    void execute_with_audio_clock(double seconds);
    void update_ym2151_timer(uint8_t reg, uint8_t data);
    void update_ym2151_irq();
    void initialize_mfp();
    void update_mfp(int executed_cycles);
    uint8_t read_mfp(uint32_t address);
    void write_mfp(uint32_t address, uint8_t data);
    void write_mfp_gpio3(int state);
    void update_mfp_irq();
    void reset_ym2151_timers();
    double ym2151_timer_a_cycles() const;
    double ym2151_timer_b_cycles() const;
    void open_trace_from_environment();
    void trace_ym2151(uint8_t reg, uint8_t data);
    void trace_io(const char* operation, uint32_t address, uint8_t data);
    void trace_pcm8(const X68kPcm8Mixer::CommandResult& result,
                    uint32_t d0, uint32_t d1, uint32_t d2, uint32_t a1);
    uint8_t read_midi(uint32_t address);
    void write_midi(uint32_t address, uint8_t data);
    void update_midi(int executed_cycles);
    void handle_midi_message(const X68kMidiMessage& message);
    void raise_midi_irq(uint8_t vector, uint8_t flag);
    void update_cpu_irq();
    void reset_midi_synth_mode();
    void handle_host_callback(uint8_t vector);
    void handle_iocs_call();
    void write_memory_32(uint32_t address, uint32_t value) override;
    uint8_t iocs_adpcm_mode_to_ppi(uint32_t mode) const;
    uint32_t read_memory_32(uint32_t address) override;
    bool read_pcm_memory_8(uint32_t address, uint8_t& value) const;
    bool bus_address_readable(uint32_t address, uint32_t size) const;
    bool bus_address_writable(uint32_t address, uint32_t size) const;
    uint32_t read_memory_sized(uint32_t address, uint32_t size);
    void write_memory_sized(uint32_t address, uint32_t size, uint32_t value);
    uint32_t read_be32(size_t offset) const;
    void select_voice_bank(const HootEntry& entry, int track_index);
    void diagnose_opmdrv_voices(const HootEntry& entry, int track_index);
    void activate_legacy_startup();
    bool dispatch_mailbox_command(uint16_t command, uint32_t max_cycles = 100000);
    void post_mailbox_command_fixed(uint16_t command, uint32_t cycles = 100000);

    std::array<uint8_t, kRomSize> rom_{};
    // Immutable pack image used to give each selected track a clean machine.
    std::array<uint8_t, kRomSize> rom_image_{};
    std::array<uint8_t, kRamSize> ram_{};
    std::array<uint8_t, kScratchSize> scratch_{};
    // Original Hoot maps every unmapped 64 KiB bank to one writable
    // dummy page. Some modern bootstraps rely on the top bank for wrapped
    // A6-relative workspace. Preserve that compatibility page at 0xff0000.
    std::array<uint8_t, kHighMemorySize> high_memory_{};
    std::map<std::string, VoiceBankImage> voice_banks_;
    uint32_t active_voice_bank_offset_ = 0x20000;
    std::string track_warning_;
    std::string driver_warning_;
    int sample_rate_ = 44100;
    double cpu_clock_hz_ = 10000000.0;
    double render_cycle_remainder_ = 0.0;
    int cpu_cycle_debt_ = 0;
    uint32_t ym2151_clock_hz_ = 4000000;
    int selected_track_ = 0;
    uint32_t selected_code_ = 0;
    bool has_selected_track_ = false;
    uint32_t reset_sp_ = 0;
    uint32_t reset_pc_ = 0;
    uint32_t memdump_address_ = 0;
    uint64_t loaded_code_bytes_ = 0;
    uint64_t debug_cpu_cycles_ = 0;
    uint64_t debug_io_reads_ = 0;
    uint64_t debug_io_writes_ = 0;
    uint64_t debug_ym2151_writes_ = 0;
    uint64_t debug_ym2151_keyons_ = 0;
    uint64_t debug_ym2151_irqs_ = 0;
    uint64_t debug_unhandled_exceptions_ = 0;
    uint8_t ym2151_timer_a_high_ = 0;
    uint8_t ym2151_timer_a_low_ = 0;
    uint8_t ym2151_timer_b_ = 0;
    uint8_t ym2151_timer_control_ = 0;
    uint8_t ym2151_timer_status_ = 0;
    bool ym2151_irq_asserted_ = false;
    double ym2151_timer_a_remaining_ = 0.0;
    double ym2151_timer_b_remaining_ = 0.0;
    uint64_t debug_adpcm_writes_ = 0;
    uint64_t debug_adpcm_starts_ = 0;
    uint32_t adpcm_address_ = 0;
    uint32_t adpcm_size_ = 0;
    double opm_gain_ = 192.0 / 256.0;
    double adpcm_gain_ = 0.40;
    double pcm8_gain_ = 0.40;
    double total_gain_ = 1.0;
    bool mute_percussion_ = false;
    uint32_t opm_mute_mask_ = 0;
    uint32_t ui_opm_mute_mask_ = 0;
    uint32_t ui_pcm8_mute_mask_ = 0;
    uint16_t ui_midi_mute_mask_ = 0;
    bool ui_adpcm_muted_ = false;
    uint8_t current_ym2151_reg_ = 0;
    std::array<uint8_t, 256> ym2151_registers_{};
    std::array<bool, 8> ym2151_key_on_{};
    uint8_t debug_last_ym2151_reg_ = 0;
    uint8_t debug_last_ym2151_data_ = 0;
    uint8_t mailbox_flag_ = 0;
    uint16_t mailbox_code_ = 0;
    bool midi_enabled_ = false;
    int midiout_type_ = -1;
    double midi_gain_ = 0.70;
    bool pcm8_enabled_ = false;
    bool dmaint_enabled_ = false;
    bool dma_irq_pending_ = false;
    uint8_t dma_niv_ = 0x6a;
    uint8_t dma_eiv_ = 0x6e;
    uint64_t debug_dma_irqs_ = 0;
    bool mfp_enabled_ = false;
    bool mame_mfp_ = false;
    bool mfp_bootstrap_ = true;
    bool mfp_ignore_overrides_ = false;
    StartupPolicy startup_policy_ = StartupPolicy::Auto;
    bool legacy_startup_active_ = false;
    uint64_t startup_fallbacks_ = 0;
    bool mfp_irq_asserted_ = false;
    bool mfp_suspended_ = false;
    bool mfp_trap_bridge_ = false;
    uint8_t mfp_trap_magic_ = 0;
    uint32_t iocs_opm_interrupt_handler_ = 0;
    std::map<uint16_t, uint32_t> iocs_vectors_;
    int mfp_timer_divider_ = 1;
    int mfp_sound_timer_ = -1;
    uint8_t mfp_initial_ierb_ = 0x3e;
    uint8_t mfp_initial_imrb_ = 0x3e;
    uint8_t mfp_regs_[24]{};
    uint8_t mfp_gpio_input_ = 0;
    // MFP timer counters treat a zero data register as 0x100. Keep the
    // current count wide enough to represent that value.
    uint16_t mfp_timer_values_[4]{};
    double mfp_timer_accumulators_[4]{};
    uint8_t midi_reg_high_ = 0;
    uint8_t midi_vector_ = 0;
    uint8_t midi_int_enable_ = 0;
    uint8_t midi_int_vect_ = 0x10;
    uint8_t midi_int_flag_ = 0;
    uint8_t midi_r05_ = 0;
    uint32_t midi_g_timer_max_ = 0;
    uint32_t midi_m_timer_max_ = 0;
    int64_t midi_g_timer_value_ = 0;
    int64_t midi_m_timer_value_ = 0;
    bool midi_irq_asserted_ = false;
    uint64_t debug_midi_irq_count_ = 0;
    uint64_t debug_midi_synth_frames_ = 0;
    uint64_t debug_midi_sysex_handled_ = 0;
    X68kMidiTransport midi_transport_;
    std::unique_ptr<MidiSynth> midi_synth_;
    MidiVisualizer midi_visualizer_;
    bool loaded_ = false;
    bool has_opmdrv_voice_transform_ = false;
    std::ofstream trace_;
    uint64_t trace_events_ = 0;
    uint64_t trace_limit_ = 0;
    std::array<uint32_t, 32> recent_pcs_{};
    size_t recent_pc_cursor_ = 0;
    bool default_exception_traced_ = false;
    std::vector<int16_t> mix_buffer_;
    std::vector<int16_t> startup_preroll_;
    size_t startup_preroll_offset_ = 0;
    std::vector<int32_t> pcm8_mix_buffer_;
    std::vector<int16_t> midi_mix_buffer_;
    LibvgmYm2151 ym2151_;
    LibvgmOkim6258 adpcm_;
    X68kPcm8Mixer pcm8_;
};

} // namespace hoot
