#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <vector>

namespace hoot {

struct X68kMidiMessage {
    enum class Kind : uint8_t {
        Channel = 0,
        SystemCommon,
        SysEx,
    };

    Kind kind = Kind::Channel;
    uint8_t status = 0;
    uint8_t data1 = 0;
    uint8_t data2 = 0;
    uint8_t size = 0;
    std::vector<uint8_t> sysex;
};

struct X68kMidiTransportStats {
    uint64_t bytes_enqueued = 0;
    uint64_t bytes_transmitted = 0;
    uint64_t channel_messages = 0;
    uint64_t system_common_messages = 0;
    uint64_t sysex_messages = 0;
    uint64_t sysex_bytes = 0;
    uint64_t realtime_bytes = 0;
    uint64_t running_status_messages = 0;
    uint64_t malformed_bytes = 0;
    uint64_t note_on_messages = 0;
    uint64_t note_off_messages = 0;
    uint64_t control_changes = 0;
    uint64_t program_changes = 0;
    uint64_t pitch_bends = 0;
    uint64_t fifo_full_transitions = 0;
    uint32_t peak_fifo_bytes = 0;
    uint8_t last_status = 0;
};

class X68kMidiTransport {
public:
    using Sink = std::function<void(const X68kMidiMessage&)>;

    static constexpr uint32_t kCpuCyclesPerByte = 3200;
    static constexpr size_t kHardwareFifoBytes = 256;

    void reset();
    void set_sink(Sink sink);
    void enqueue(uint8_t byte);
    void advance_cycles(uint32_t cycles);

    size_t buffered_bytes() const { return fifo_.size(); }
    bool tx_ready() const { return fifo_.size() < kHardwareFifoBytes; }
    bool tx_full() const { return !tx_ready(); }
    const X68kMidiTransportStats& stats() const { return stats_; }

private:
    void transmit_one(uint8_t byte);
    void begin_status(uint8_t status);
    void emit_short();
    static uint8_t expected_size(uint8_t status);

    std::deque<uint8_t> fifo_;
    uint32_t cycles_until_tx_ = kCpuCyclesPerByte;
    Sink sink_;
    X68kMidiTransportStats stats_{};

    uint8_t running_status_ = 0;
    uint8_t current_status_ = 0;
    uint8_t expected_size_ = 0;
    uint8_t message_size_ = 0;
    uint8_t message_data_[2]{};
    bool using_running_status_ = false;
    bool in_sysex_ = false;
    std::vector<uint8_t> sysex_;
};

} // namespace hoot
