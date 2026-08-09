#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace hoot {

// PC-9801-86 linear PCM FIFO/DAC.  This intentionally models the board-level
// programming interface separately from the YM2608 on the same card.
class Pc98Pcm86 {
public:
    struct Stats {
        uint64_t port_writes = 0;
        uint64_t fifo_writes = 0;
        uint64_t fifo_reads = 0;
        uint64_t rendered_frames = 0;
        uint64_t rendered_source_frames = 0;
        uint64_t irq_requests = 0;
        uint64_t irq_deliveries = 0;
        uint64_t fifo_overflows = 0;
        uint32_t peak_fifo_bytes = 0;
    };

    bool initialize(uint32_t output_rate);
    void reset();

    bool handles_port(uint16_t port) const;
    uint8_t read_port(uint16_t port);
    void write_port(uint16_t port, uint8_t value);

    // Add PCM86 output into an existing signed-16 stereo buffer.
    void mix_s16(int16_t* interleaved_stereo, int frames, double gain = 1.0);

    bool playback_enabled() const { return (fifo_control_ & 0x80) != 0; }
    bool interrupt_enabled() const { return (fifo_control_ & 0x20) != 0; }
    bool irq_pending() const { return irq_pending_ && !irq_delivered_; }
    void mark_irq_delivered();
    int frames_until_irq() const;

    uint32_t fifo_bytes() const { return fifo_count_; }
    uint32_t fifo_threshold() const { return fifo_threshold_; }
    double source_rate() const;
    uint8_t dac_control() const { return dac_control_; }
    uint8_t fifo_control() const { return fifo_control_; }
    uint8_t volume_code() const { return volume_code_; }
    bool extended_opna_enabled() const { return (ext_function_ & 0x01) != 0; }
    const Stats& stats() const { return stats_; }

private:
    static constexpr uint32_t kFifoCapacity = 0x8000;

    int bytes_per_source_frame() const;
    bool fetch_source_frame(int32_t& left, int32_t& right);
    uint8_t pop_byte();
    void clear_fifo();
    void refresh_irq();
    void push_byte(uint8_t value);

    uint32_t output_rate_ = 44100;
    std::array<uint8_t, kFifoCapacity> fifo_{};
    uint32_t fifo_read_ = 0;
    uint32_t fifo_write_ = 0;
    uint32_t fifo_count_ = 0;
    uint32_t fifo_threshold_ = 0x80;

    uint8_t ext_function_ = 0;
    uint8_t fifo_control_ = 0;
    uint8_t dac_control_ = 0x32;
    uint8_t volume_code_ = 15;

    uint64_t phase_q32_ = 0;
    uint64_t step_q32_ = 0;
    int32_t current_left_ = 0;
    int32_t current_right_ = 0;
    bool have_current_ = false;

    bool irq_armed_ = false;
    bool irq_pending_ = false;
    bool irq_delivered_ = false;
    Stats stats_{};
};

} // namespace hoot
