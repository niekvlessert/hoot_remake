#pragma once

#include <cstdint>

namespace hoot {

class Pc98Beep {
public:
    struct Stats {
        uint64_t pit_data_writes = 0;
        uint64_t pit_control_writes = 0;
        uint64_t ppi_writes = 0;
        uint64_t gate_changes = 0;
        uint64_t divider_changes = 0;
        uint64_t rendered_frames = 0;
        uint64_t audible_frames = 0;
        uint32_t min_divider = 0;
        uint32_t max_divider = 0;
    };

    bool initialize(uint32_t output_rate, uint32_t pit_clock = 2457600u);
    void reset();

    bool handles_port(uint16_t port) const;
    uint8_t read_port(uint16_t port) const;
    void write_port(uint16_t port, uint8_t data);

    void mix_s16(int16_t* interleaved_stereo, int frames, double gain = 1.0);

    uint32_t pit_clock() const { return pit_clock_; }
    uint16_t divider() const { return divider_ == 0 ? 0xffffu : divider_; }
    double frequency() const;
    bool speaker_enabled() const { return speaker_enabled_; }
    uint8_t mode() const { return mode_; }
    uint8_t ppi_port_c() const { return ppi_port_c_; }
    const Stats& stats() const { return stats_; }

private:
    void write_counter(uint8_t data);
    void apply_control(uint8_t data);
    void set_speaker_enabled(bool enabled);
    void commit_divider(uint16_t value);

    uint32_t output_rate_ = 44100;
    uint32_t pit_clock_ = 2457600;
    uint16_t divider_ = 1229;
    uint8_t mode_ = 3;
    uint8_t access_mode_ = 3; // 1 LSB, 2 MSB, 3 LSB then MSB
    uint8_t write_phase_ = 0;
    uint8_t pending_lsb_ = 0;
    uint8_t ppi_port_c_ = 0xf8; // BIOS startup: buzzer inhibited (PC3=1)
    bool speaker_enabled_ = false;
    double phase_ = 0.0;
    Stats stats_{};
};

} // namespace hoot
