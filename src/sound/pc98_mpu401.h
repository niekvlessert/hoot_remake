#pragma once

#include <array>
#include <cstdint>
#include <deque>

#include "sound/x68k_midi_transport.h"

namespace hoot {

struct Pc98Mpu401Stats {
    uint64_t data_writes = 0;
    uint64_t data_reads = 0;
    uint64_t status_reads = 0;
    uint64_t command_writes = 0;
    uint64_t reset_commands = 0;
    uint64_t uart_commands = 0;
    uint64_t version_queries = 0;
    uint64_t revision_queries = 0;
    uint64_t unknown_commands = 0;
    uint64_t intelligent_passthrough_bytes = 0;
    uint64_t intelligent_clock_bytes = 0;
    uint64_t intelligent_direct_messages = 0;
    uint64_t ack_bytes = 0;
    uint32_t last_base = 0;
    uint8_t last_command = 0;
};

// Roland MPU-PC98 compatible interface used by PC-98 DOS residents.  Besides
// UART mode, a deliberately small but real intelligent-mode subset is kept:
// host-clock generation, tempo/timebase setup and WSD direct MIDI output.
// Those operations are sufficient for common PC-98 resident drivers while
// leaving the full eight-track hardware sequencer out of the replay host.
class Pc98Mpu401 {
public:
    using Sink = X68kMidiTransport::Sink;

    void reset();
    void set_sink(Sink sink);

    bool handles_port(uint16_t port) const;
    uint8_t read_port(uint16_t port);
    void write_port(uint16_t port, uint8_t data);
    void advance_frames(int frames, int sample_rate);
    void advance_cpu_time(int executed_steps, double steps_per_second);
    int frames_until_intelligent_event(int sample_rate) const;

    bool uart_mode() const { return uart_mode_; }
    bool irq_pending() const { return !response_fifo_.empty(); }
    bool intelligent_clock_active() const;
    uint32_t active_base() const { return active_base_; }
    const Pc98Mpu401Stats& stats() const { return stats_; }
    const X68kMidiTransportStats& transport_stats() const { return transport_.stats(); }

private:
    static bool decode_port(uint16_t port, uint16_t& base, bool& command_port);
    void push_response(uint8_t byte);
    void handle_command(uint8_t command);
    void handle_intelligent_data(uint8_t data);
    void enqueue_midi_byte(uint8_t data);
    void advance_intelligent_seconds(double seconds);
    double intelligent_event_rate() const;
    void reset_intelligent_state();

    X68kMidiTransport transport_;
    std::deque<uint8_t> response_fifo_;
    Pc98Mpu401Stats stats_{};
    bool uart_mode_ = false;
    uint32_t active_base_ = 0xe0d0;
    double serial_cycle_remainder_ = 0.0;

    bool intelligent_playing_ = false;
    bool clock_to_host_ = false;
    uint8_t intelligent_timebase_ = 120;
    uint8_t intelligent_tempo_ = 100;
    uint8_t intelligent_relative_tempo_ = 0x40;
    uint8_t clock_to_host_rate_ = 1;
    uint8_t pending_parameter_command_ = 0;
    uint8_t direct_track_ = 0;
    bool direct_system_ = false;
    bool direct_message_active_ = false;
    int direct_bytes_remaining_ = 0;
    std::array<uint8_t, 8> direct_running_status_{};
    double intelligent_event_phase_ = 0.0;
};

} // namespace hoot
