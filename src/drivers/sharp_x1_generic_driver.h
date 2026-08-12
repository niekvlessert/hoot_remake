#pragma once

#include <array>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "cpu/kmz80_cpu.h"
#include "drivers/hoot_driver.h"
#include "sound/libvgm_ym2151.h"
#include "sound/libvgm_ym2203.h"

namespace hoot {

// Sharp X1 replay host used by the x1/psg, x1/opm, x1/opmx2 and x1/opn
// catalogue ABIs. Game-specific patches still run as native Z80 code; this
// class supplies the machine memory, 16-bit I/O space, timers and sound boards.
class SharpX1GenericDriver final : public HootDriver {
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
    const char* name() const override;

private:
    enum class Board { Psg, Opm, DualOpm, Opn };

    void clear();
    void restart_guest();
    uint8_t read_io(uint16_t port);
    void write_io(uint16_t port, uint8_t data);
    void execute_frames(int frames);
    void advance_interrupts(int frames);
    void pulse_irq(uint8_t line, uint8_t bus);
    void copy_bgm(uint32_t slot, uint16_t destination);
    void copy_voice(uint32_t slot, uint16_t destination);
    int option(const char* name, int fallback) const;
    void record_psg_write(uint8_t port, uint8_t data);
    void record_opm_write(int chip, uint8_t port, uint8_t data);
    void record_opn_write(uint8_t port, uint8_t data);

    static constexpr double kCpuClock = 4000000.0;
    static constexpr uint32_t kPsgHostClock = 8000000; // YM2203 SSG clock / 4 = 2 MHz
    static constexpr uint32_t kOpmClock = 4000000;
    static constexpr uint32_t kOpnClock = 4000000;

    std::array<uint8_t, 0x10000> ram_{};
    std::array<uint8_t, 0x10000> initial_ram_{};
    std::array<uint8_t, 0x10000> io_{};
    std::array<uint8_t, 16> psg_registers_{};
    std::array<std::array<uint8_t, 256>, 2> opm_registers_{};
    std::array<uint8_t, 256> opn_registers_{};
    std::array<bool, 16> fm_key_on_{};
    std::map<uint32_t, std::vector<uint8_t>> bgm_;
    std::map<uint32_t, std::vector<uint8_t>> voices_;
    std::map<std::string, int> options_;
    Board board_ = Board::Psg;
    int sample_rate_ = 44100;
    int selected_track_ = 0;
    uint32_t selected_code_ = 0;
    uint16_t init_pc_ = 0xc000;
    bool loaded_ = false;
    int psgpcm_slice_frames_ = 0;
    double timer_interval_frames_ = 0.0;
    double timer_frames_left_ = 0.0;
    double ctc_interval_frames_ = 0.0;
    double ctc_frames_left_ = 0.0;
    double vsync_interval_frames_ = 0.0;
    double vsync_frames_left_ = 0.0;
    uint8_t psg_latch_ = 0;
    std::array<uint8_t, 2> opm_latch_{};
    uint8_t opn_latch_ = 0;
    uint64_t debug_cpu_cycles_ = 0;
    uint64_t debug_io_reads_ = 0;
    uint64_t debug_io_writes_ = 0;
    uint64_t debug_chip_writes_ = 0;
    uint64_t debug_keyons_ = 0;
    uint8_t debug_last_reg_ = 0;
    uint8_t debug_last_data_ = 0;
    uint32_t psg_mute_mask_ = 0;
    std::array<uint32_t, 2> opm_mute_mask_{};
    uint32_t opn_mute_mask_ = 0;
    std::vector<int16_t> psg_audio_;
    std::vector<int16_t> opm1_audio_;
    std::vector<int16_t> opm2_audio_;
    std::vector<int16_t> opn_audio_;
    Kmz80Cpu cpu_;
    LibvgmYm2203 psg_;
    LibvgmYm2151 opm1_;
    LibvgmYm2151 opm2_;
    LibvgmYm2203 opn_;
};

} // namespace hoot
