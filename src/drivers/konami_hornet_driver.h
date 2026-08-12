#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "cpu/musashi_bus.h"
#include "drivers/hoot_driver.h"
#include "sound/k056800.h"
#include "sound/rf5c400.h"

namespace hoot {

class KonamiHornetDriver final : public HootDriver, public MusashiBus {
public:
    HootResult load(const HootEntry& entry, const std::string& packs_path,
                    int sample_rate, std::string& error) override;
    HootResult select_track(const HootEntry& entry, int track_index,
                            std::string& error) override;
    void reset() override;
    int render_s16(int16_t* interleaved_stereo, int frames) override;
    int render_float(float* interleaved_stereo, int frames) override;
    void fill_track_info(const HootEntry& entry, int track_index,
                         HootTrackInfo& out) const override;
    void fill_visual_state(const HootEntry& entry, int track_index,
                           HootVisualState& out) const override;
    bool channel_mute_supported(int kind, int index) const override;
    bool set_channel_muted(int kind, int index, bool muted) override;
    void clear_channel_mutes() override;
    const char* name() const override { return "konami-hornet-rf5c400"; }

    uint8_t read_memory_8(uint32_t address) override;
    uint16_t read_memory_16(uint32_t address) override;
    void write_memory_8(uint32_t address, uint8_t data) override;
    void write_memory_16(uint32_t address, uint16_t data) override;
    int acknowledge_interrupt(int level) override;

    bool command_interface_ready() const { return interface_.interrupt_enabled(); }
    uint64_t rf5c400_writes() const { return rf5c400_writes_; }

private:
    static constexpr uint32_t kCpuClock = 16000000;
    static constexpr uint32_t kSoundClock = 16934400;
    static constexpr size_t kProgramSize = 0x80000;
    static constexpr size_t kRamSize = 0x10000;
    static constexpr size_t kRfRegisterWords = 0x800;

    void execute_cycles(int cycles);
    void update_cpu_irq();
    bool run_until_interface_ready();
    void send_command(uint32_t code);

    std::array<uint8_t, kProgramSize> program_{};
    std::array<uint8_t, kRamSize> ram_{};
    std::array<uint16_t, kRfRegisterWords> rf_registers_{};
    Rf5c400 rf5c400_;
    K056800 interface_;
    int sample_rate_ = 44100;
    double pcm_gain_ = 1.0;
    double cpu_cycle_fraction_ = 0.0;
    double periodic_irq_cycles_ = 0.0;
    bool timer_irq_enabled_ = false;
    bool timer_irq_asserted_ = false;
    bool loaded_ = false;
    int selected_track_ = -1;
    uint32_t selected_code_ = 0;
    uint64_t rendered_frames_ = 0;
    uint64_t cpu_cycles_ = 0;
    uint64_t io_reads_ = 0;
    uint64_t io_writes_ = 0;
    uint64_t rf5c400_writes_ = 0;
};

} // namespace hoot
