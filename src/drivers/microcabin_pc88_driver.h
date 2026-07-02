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

namespace hoot {

class MicrocabinPc88Driver final : public HootDriver {
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
    const char* name() const override;

private:
    static constexpr size_t kRamSize = 0x10000;
    static constexpr size_t kBgmSlotSize = 8 * 1024;
    static constexpr size_t kMaxSlots = 128;

    void clear();
    uint8_t read_memory(uint16_t address) const;
    void write_memory(uint16_t address, uint8_t data);
    uint8_t read_io(uint16_t port);
    void write_io(uint16_t port, uint8_t data);
    void execute_seconds(double seconds);
    void update_opn_timer(uint8_t reg, uint8_t data);
    void refresh_irq_interval();
    void open_trace_from_environment();
    void trace_event(const char* kind, uint32_t a = 0, uint32_t b = 0);
    bool should_trace_memory(uint16_t address) const;

    std::array<uint8_t, kRamSize> ram_{};
    std::array<uint8_t, 0x100> io_{};
    std::array<uint8_t, kMaxSlots> bgm_present_{};
    std::array<uint32_t, kMaxSlots> bgm_size_{};
    std::vector<uint8_t> bgm_;
    std::map<uint32_t, std::vector<uint8_t>> voices_;
    std::map<std::string, int> options_;
    int sample_rate_ = 44100;
    int selected_track_ = 0;
    uint32_t selected_code_ = 0;
    bool play_pending_ = false;
    bool loaded_ = false;
    uint64_t debug_cpu_cycles_ = 0;
    uint64_t debug_io_reads_ = 0;
    uint64_t debug_io_writes_ = 0;
    uint64_t debug_opn_writes_ = 0;
    uint64_t debug_opn_keyons_ = 0;
    uint8_t debug_last_opn_reg_ = 0;
    uint8_t debug_last_opn_data_ = 0;
    uint8_t current_opn_reg_ = 0;
    uint8_t opn_timer_b_ = 0;
    uint8_t opn_mode_ = 0;
    uint8_t opn_prescaler_sel_ = 2;
    int irq_interval_frames_ = 0;
    int irq_frames_until_next_ = 0;
    std::array<uint64_t, 0x100> debug_port_writes_{};
    std::ofstream trace_;
    uint64_t trace_limit_ = 0;
    uint64_t trace_events_ = 0;
    bool trace_limit_reported_ = false;
    Kmz80Cpu cpu_;
    LibvgmYm2203 ym2203_;
};

} // namespace hoot
