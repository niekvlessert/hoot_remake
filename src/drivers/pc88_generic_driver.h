#pragma once

#include <array>
#include <cstdint>
#include <fstream>
#include <map>
#include <string>
#include <vector>

#include "cpu/kmz80_cpu.h"
#include "drivers/hoot_driver.h"
#include "sound/libvgm_ym2203.h"
#include "sound/libvgm_ym2608.h"

namespace hoot {

// Generic headless host for the Hoot pc88/opn and pc88/opna catalog ABIs.
// The guest patch remains responsible for game/driver-specific music logic;
// this class supplies the PC-88 Z80, interrupt/timer and OPN/OPNA services.
class Pc88GenericDriver final : public HootDriver {
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

private:
    static constexpr size_t kRamSize = 0x10000;
    static constexpr double kDefaultCpuClock = 4000000.0;
    static constexpr uint32_t kOpmClock = 3993600;
    static constexpr uint32_t kOpnaClock = 7987200;

    void clear();
    void restart_guest_for_track();
    uint8_t read_memory(uint16_t address) const;
    void write_memory(uint16_t address, uint8_t data);
    uint8_t read_io(uint16_t port);
    void write_io(uint16_t port, uint8_t data);
    void execute_seconds(double seconds);
    void update_fm_timer(uint8_t reg, uint8_t data);
    void refresh_fm_irq_interval();
    void schedule_irq_sources(int& todo);
    void advance_irq_sources(int frames);
    void raise_irq(uint8_t bus, const char* source);
    void copy_bgm_to_ram(uint32_t slot, uint16_t destination, size_t limit = 0);
    void copy_voice_to_ram(uint32_t slot);
    uint8_t chip_read(uint8_t port);
    void chip_write(uint8_t port, uint8_t data);
    void load_opna_adpcm_assets();
    void open_trace_from_environment();
    void trace_event(const char* kind, uint32_t a = 0, uint32_t b = 0);
    bool should_trace_memory(uint16_t address) const;
    int option(const char* name, int fallback) const;
    bool option_enabled(const char* name) const;

    std::array<uint8_t, kRamSize> ram_{};
    std::array<uint8_t, kRamSize> initial_ram_{};
    std::array<uint8_t, 0x100> io_{};
    std::map<uint32_t, std::vector<uint8_t>> bgm_;
    std::map<uint32_t, std::vector<uint8_t>> voices_;
    std::map<uint32_t, std::vector<uint8_t>> adpcm_assets_;
    std::map<std::string, int> options_;
    int sample_rate_ = 44100;
    double cpu_clock_hz_ = kDefaultCpuClock;
    int selected_track_ = 0;
    uint32_t selected_code_ = 0;
    uint16_t init_pc_ = 0;
    bool play_pending_ = false;
    bool loaded_ = false;
    bool use_opna_ = false;
    bool use_periodic_irq_ = false;
    int periodic_irq_interval_frames_ = 0;
    int periodic_irq_frames_until_next_ = 0;
    uint64_t debug_cpu_cycles_ = 0;
    uint64_t debug_io_reads_ = 0;
    uint64_t debug_io_writes_ = 0;
    uint64_t debug_opn_writes_ = 0;
    uint64_t debug_opn_keyons_ = 0;
    uint8_t debug_last_opn_reg_ = 0;
    uint8_t debug_last_opn_data_ = 0;
    std::array<uint8_t, 2> current_fm_reg_{};
    std::array<std::array<uint8_t, 256>, 2> opn_registers_{};
    std::array<bool, 6> opn_key_on_{};
    uint16_t fm_timer_a_ = 0;
    uint8_t fm_timer_b_ = 0;
    uint8_t fm_mode_ = 0;
    uint8_t fm_irq_bus_ = 0x08;
    int fm_timer_a_interval_frames_ = 0;
    int fm_timer_a_frames_until_next_ = 0;
    int fm_timer_b_interval_frames_ = 0;
    int fm_timer_b_frames_until_next_ = 0;
    std::array<uint64_t, 0x100> debug_port_writes_{};
    std::ofstream trace_;
    uint64_t trace_limit_ = 0;
    uint64_t trace_events_ = 0;
    bool trace_limit_reported_ = false;
    Kmz80Cpu cpu_;
    LibvgmYm2203 ym2203_;
    LibvgmYm2608 ym2608_;
    uint32_t ui_opn_mute_mask_ = 0;
    uint32_t ui_ssg_mute_mask_ = 0;
};

} // namespace hoot
