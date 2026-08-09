#include "sound/pc98_mpu401.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace hoot {
namespace {
constexpr std::array<uint16_t, 8> kPc98MpuBases = {
    0xc0d0, 0xc8d0, 0xd0d0, 0xd8d0, 0xe0d0, 0xe8d0, 0xf0d0, 0xf8d0,
};
constexpr uint8_t kMpuAck = 0xfe;
constexpr uint8_t kMpuClock = 0xfd;

int midi_message_length(uint8_t status)
{
    if (status < 0x80) return 0;
    if (status < 0xf0) {
        const uint8_t high = static_cast<uint8_t>(status & 0xf0);
        return (high == 0xc0 || high == 0xd0) ? 2 : 3;
    }
    switch (status) {
    case 0xf1: return 2;
    case 0xf2: return 3;
    case 0xf3: return 2;
    case 0xf6: return 1;
    default: return 1;
    }
}
}

void Pc98Mpu401::reset_intelligent_state()
{
    intelligent_playing_ = false;
    clock_to_host_ = false;
    intelligent_timebase_ = 120;
    intelligent_tempo_ = 100;
    intelligent_relative_tempo_ = 0x40;
    clock_to_host_rate_ = 1;
    pending_parameter_command_ = 0;
    direct_track_ = 0;
    direct_system_ = false;
    direct_message_active_ = false;
    direct_bytes_remaining_ = 0;
    direct_running_status_.fill(0);
    intelligent_event_phase_ = 0.0;
}

void Pc98Mpu401::reset()
{
    transport_.reset();
    response_fifo_.clear();
    stats_ = {};
    uart_mode_ = false;
    active_base_ = 0xe0d0;
    serial_cycle_remainder_ = 0.0;
    reset_intelligent_state();
}

void Pc98Mpu401::set_sink(Sink sink)
{
    transport_.set_sink(std::move(sink));
}

bool Pc98Mpu401::decode_port(uint16_t port, uint16_t& base, bool& command_port)
{
    for (const uint16_t candidate : kPc98MpuBases) {
        if (port == candidate) {
            base = candidate;
            command_port = false;
            return true;
        }
        if (port == static_cast<uint16_t>(candidate + 2)) {
            base = candidate;
            command_port = true;
            return true;
        }
    }
    return false;
}

bool Pc98Mpu401::handles_port(uint16_t port) const
{
    uint16_t base = 0;
    bool command = false;
    return decode_port(port, base, command);
}

void Pc98Mpu401::push_response(uint8_t byte)
{
    response_fifo_.push_back(byte);
    if (byte == kMpuAck) ++stats_.ack_bytes;
    if (byte == kMpuClock) ++stats_.intelligent_clock_bytes;
}

uint8_t Pc98Mpu401::read_port(uint16_t port)
{
    uint16_t base = 0;
    bool command_port = false;
    if (!decode_port(port, base, command_port)) return 0xff;
    active_base_ = base;
    stats_.last_base = base;
    if (command_port) {
        ++stats_.status_reads;
        uint8_t status = 0;
        if (response_fifo_.empty()) status |= 0x80; // RX empty
        if (transport_.tx_full()) status |= 0x40;  // TX full
        return status;
    }

    ++stats_.data_reads;
    if (response_fifo_.empty()) return 0xff;
    const uint8_t value = response_fifo_.front();
    response_fifo_.pop_front();
    return value;
}

void Pc98Mpu401::enqueue_midi_byte(uint8_t data)
{
    if (transport_.tx_ready()) transport_.enqueue(data);
}

void Pc98Mpu401::handle_command(uint8_t command)
{
    stats_.last_command = command;
    ++stats_.command_writes;

    // A new command terminates any incomplete WSD transfer, matching the MPU
    // command parser's command/data distinction.
    direct_message_active_ = false;
    direct_system_ = false;
    direct_bytes_remaining_ = 0;

    if (command == 0xff) { // reset to intelligent mode
        ++stats_.reset_commands;
        transport_.reset();
        response_fifo_.clear();
        uart_mode_ = false;
        reset_intelligent_state();
        push_response(kMpuAck);
        return;
    }
    if (command == 0x3f) { // UART mode
        ++stats_.uart_commands;
        uart_mode_ = true;
        pending_parameter_command_ = 0;
        push_response(kMpuAck);
        return;
    }
    if (command == 0xac) {
        ++stats_.version_queries;
        push_response(kMpuAck);
        push_response(0x15);
        return;
    }
    if (command == 0xad) {
        ++stats_.revision_queries;
        push_response(kMpuAck);
        push_response(0x01);
        return;
    }

    // Commands 00h..2Fh combine MIDI transport control in bits 0..1 with
    // intelligent sequencer stop/play in bits 2..3.
    if (command <= 0x2f) {
        switch (command & 0x03) {
        case 1: enqueue_midi_byte(0xfc); break;
        case 2: enqueue_midi_byte(0xfa); break;
        case 3: enqueue_midi_byte(0xfb); break;
        default: break;
        }
        switch (command & 0x0c) {
        case 0x04:
            intelligent_playing_ = false;
            intelligent_event_phase_ = 0.0;
            break;
        case 0x08:
            intelligent_playing_ = true;
            intelligent_event_phase_ = 0.0;
            break;
        default:
            break;
        }
        push_response(kMpuAck);
        return;
    }

    if (command >= 0xd0 && command <= 0xd7) {
        direct_track_ = static_cast<uint8_t>(command & 7);
        direct_message_active_ = true;
        direct_system_ = false;
        direct_bytes_remaining_ = 0;
        push_response(kMpuAck);
        return;
    }
    if (command == 0xdf) {
        direct_message_active_ = true;
        direct_system_ = true;
        direct_bytes_remaining_ = 0;
        push_response(kMpuAck);
        return;
    }

    switch (command) {
    case 0x94:
        clock_to_host_ = false;
        intelligent_event_phase_ = 0.0;
        break;
    case 0x95:
        clock_to_host_ = true;
        intelligent_event_phase_ = 0.0;
        break;
    case 0xb9: // clear play map / all notes off
        for (uint8_t channel = 0; channel < 16; ++channel) {
            enqueue_midi_byte(static_cast<uint8_t>(0xb0 | channel));
            enqueue_midi_byte(0x7b);
            enqueue_midi_byte(0x00);
        }
        break;
    case 0xc2: intelligent_timebase_ = 48; break;
    case 0xc3: intelligent_timebase_ = 72; break;
    case 0xc4: intelligent_timebase_ = 96; break;
    case 0xc5: intelligent_timebase_ = 120; break;
    case 0xc6: intelligent_timebase_ = 144; break;
    case 0xc7: intelligent_timebase_ = 168; break;
    case 0xc8: intelligent_timebase_ = 192; break;
    case 0xe0: case 0xe1: case 0xe2: case 0xe4: case 0xe6:
    case 0xe7: case 0xec: case 0xed: case 0xee: case 0xef:
        pending_parameter_command_ = command;
        break;
    case 0x8e: case 0x8f: case 0xb8: case 0xba:
        break; // acknowledged state-management commands
    default:
        ++stats_.unknown_commands;
        break;
    }
    push_response(kMpuAck);
}

void Pc98Mpu401::handle_intelligent_data(uint8_t data)
{
    if (pending_parameter_command_ != 0) {
        const uint8_t command = pending_parameter_command_;
        pending_parameter_command_ = 0;
        switch (command) {
        case 0xe0:
            intelligent_tempo_ = std::max<uint8_t>(1, data);
            intelligent_event_phase_ = 0.0;
            break;
        case 0xe1:
            intelligent_relative_tempo_ = std::max<uint8_t>(1, data);
            intelligent_event_phase_ = 0.0;
            break;
        case 0xe7:
            clock_to_host_rate_ = std::max<uint8_t>(1, static_cast<uint8_t>(data >> 2));
            intelligent_event_phase_ = 0.0;
            break;
        default:
            break;
        }
        return;
    }

    if (!direct_message_active_) {
        // Full MPU hardware-sequencer track uploads are intentionally not yet
        // implemented.  Do not misinterpret their timing bytes as raw MIDI.
        ++stats_.intelligent_passthrough_bytes;
        return;
    }

    ++stats_.intelligent_passthrough_bytes;
    if (direct_system_) {
        enqueue_midi_byte(data);
        if (direct_bytes_remaining_ == 0) {
            if (data == 0xf0) {
                direct_bytes_remaining_ = -1; // SysEx until F7
            } else {
                direct_bytes_remaining_ = midi_message_length(data) - 1;
                if (direct_bytes_remaining_ <= 0) {
                    direct_message_active_ = false;
                    ++stats_.intelligent_direct_messages;
                }
            }
        } else if (direct_bytes_remaining_ < 0) {
            if (data == 0xf7) {
                direct_message_active_ = false;
                direct_bytes_remaining_ = 0;
                ++stats_.intelligent_direct_messages;
            }
        } else if (--direct_bytes_remaining_ == 0) {
            direct_message_active_ = false;
            ++stats_.intelligent_direct_messages;
        }
        return;
    }

    if (direct_bytes_remaining_ == 0) {
        uint8_t status = data;
        if (status < 0x80 && direct_running_status_[direct_track_] != 0) {
            status = direct_running_status_[direct_track_];
            enqueue_midi_byte(status);
        } else {
            enqueue_midi_byte(data);
            if (status < 0xf0) direct_running_status_[direct_track_] = status;
        }
        const int length = midi_message_length(status);
        direct_bytes_remaining_ = std::max(0, length - 1 - (data < 0x80 ? 1 : 0));
        if (direct_bytes_remaining_ == 0) {
            direct_message_active_ = false;
            ++stats_.intelligent_direct_messages;
        }
        return;
    }

    enqueue_midi_byte(data);
    if (--direct_bytes_remaining_ == 0) {
        direct_message_active_ = false;
        ++stats_.intelligent_direct_messages;
    }
}

void Pc98Mpu401::write_port(uint16_t port, uint8_t data)
{
    uint16_t base = 0;
    bool command_port = false;
    if (!decode_port(port, base, command_port)) return;
    active_base_ = base;
    stats_.last_base = base;
    if (command_port) {
        handle_command(data);
        return;
    }

    ++stats_.data_writes;
    if (uart_mode_) {
        enqueue_midi_byte(data);
    } else {
        handle_intelligent_data(data);
    }
}

double Pc98Mpu401::intelligent_event_rate() const
{
    if (!intelligent_clock_active()) return 0.0;
    const double relative = static_cast<double>(intelligent_relative_tempo_) / 64.0;
    const double base_clock = static_cast<double>(intelligent_tempo_)
        * static_cast<double>(intelligent_timebase_) * relative / 60.0;
    return base_clock / static_cast<double>(std::max<uint8_t>(1, clock_to_host_rate_));
}

bool Pc98Mpu401::intelligent_clock_active() const
{
    return !uart_mode_ && intelligent_playing_ && clock_to_host_;
}

void Pc98Mpu401::advance_intelligent_seconds(double seconds)
{
    if (seconds <= 0.0) return;
    const double rate = intelligent_event_rate();
    if (rate <= 0.0) return;

    intelligent_event_phase_ += seconds * rate;
    while (intelligent_event_phase_ >= 1.0) {
        intelligent_event_phase_ -= 1.0;
        // A real intelligent-mode MPU holds its IRQ while host data is
        // pending.  Do not pile up clock bytes behind an unserviced IRQ.
        if (response_fifo_.empty()) push_response(kMpuClock);
    }
}

void Pc98Mpu401::advance_cpu_time(int executed_steps, double steps_per_second)
{
    if (executed_steps <= 0 || steps_per_second <= 0.0) return;
    const double seconds = static_cast<double>(executed_steps) / steps_per_second;
    // Shell installers can spend substantial time polling the MPU before the
    // first audio frame is rendered. Serial transmission continues during
    // that guest CPU time on real hardware, otherwise a 256-byte TX FIFO can
    // deadlock a perfectly valid resident during initialization.
    const double exact_cycles = seconds * 10'000'000.0 + serial_cycle_remainder_;
    const uint32_t cycles = static_cast<uint32_t>(std::floor(exact_cycles));
    serial_cycle_remainder_ = exact_cycles - static_cast<double>(cycles);
    transport_.advance_cycles(cycles);
    advance_intelligent_seconds(seconds);
}

int Pc98Mpu401::frames_until_intelligent_event(int sample_rate) const
{
    const double rate = intelligent_event_rate();
    if (sample_rate <= 0 || rate <= 0.0 || !response_fifo_.empty()) {
        return std::numeric_limits<int>::max();
    }
    const double events_remaining = std::max(0.0, 1.0 - intelligent_event_phase_);
    return std::max(1, static_cast<int>(std::ceil(events_remaining
        * static_cast<double>(sample_rate) / rate)));
}

void Pc98Mpu401::advance_frames(int frames, int sample_rate)
{
    if (frames <= 0 || sample_rate <= 0) return;

    const double exact = static_cast<double>(frames) * 10'000'000.0
        / static_cast<double>(sample_rate) + serial_cycle_remainder_;
    const uint32_t cycles = static_cast<uint32_t>(std::floor(exact));
    serial_cycle_remainder_ = exact - static_cast<double>(cycles);
    transport_.advance_cycles(cycles);

    advance_intelligent_seconds(static_cast<double>(frames) / static_cast<double>(sample_rate));
}

} // namespace hoot
