#include "sound/x68k_midi_transport.h"

#include <algorithm>
#include <utility>

namespace hoot {

void X68kMidiTransport::reset()
{
    fifo_.clear();
    cycles_until_tx_ = kCpuCyclesPerByte;
    stats_ = {};
    running_status_ = 0;
    current_status_ = 0;
    expected_size_ = 0;
    message_size_ = 0;
    message_data_[0] = 0;
    message_data_[1] = 0;
    using_running_status_ = false;
    in_sysex_ = false;
    sysex_.clear();
}

void X68kMidiTransport::set_sink(Sink sink)
{
    sink_ = std::move(sink);
}

void X68kMidiTransport::enqueue(uint8_t byte)
{
    const bool was_full = fifo_.size() >= kHardwareFifoBytes;
    const bool was_empty = fifo_.empty();
    fifo_.push_back(byte);
    ++stats_.bytes_enqueued;
    stats_.peak_fifo_bytes = std::max<uint32_t>(stats_.peak_fifo_bytes,
                                                static_cast<uint32_t>(fifo_.size()));
    if (!was_full && fifo_.size() >= kHardwareFifoBytes) {
        ++stats_.fifo_full_transitions;
    }
    if (was_empty) {
        // PX68K restarts the serializer timer when the first queued byte is
        // written after an empty period.
        cycles_until_tx_ = kCpuCyclesPerByte;
    }
}

void X68kMidiTransport::advance_cycles(uint32_t cycles)
{
    if (fifo_.empty() || cycles == 0) {
        return;
    }

    uint32_t remaining = cycles;
    while (!fifo_.empty() && remaining >= cycles_until_tx_) {
        remaining -= cycles_until_tx_;
        const uint8_t byte = fifo_.front();
        fifo_.pop_front();
        transmit_one(byte);
        ++stats_.bytes_transmitted;
        cycles_until_tx_ = kCpuCyclesPerByte;
    }
    if (!fifo_.empty()) {
        cycles_until_tx_ -= remaining;
    } else {
        cycles_until_tx_ = kCpuCyclesPerByte;
    }
}

uint8_t X68kMidiTransport::expected_size(uint8_t status)
{
    if (status >= 0x80 && status <= 0xef) {
        switch (status & 0xf0) {
        case 0xc0:
        case 0xd0:
            return 2;
        default:
            return 3;
        }
    }
    switch (status) {
    case 0xf1:
    case 0xf3:
        return 2;
    case 0xf2:
        return 3;
    case 0xf6:
        return 1;
    default:
        return 0;
    }
}

void X68kMidiTransport::begin_status(uint8_t status)
{
    current_status_ = status;
    expected_size_ = expected_size(status);
    message_size_ = 1;
    using_running_status_ = false;
    if (status < 0xf0) {
        running_status_ = status;
    } else if (status < 0xf8) {
        running_status_ = 0;
    }
    stats_.last_status = status;
    if (expected_size_ == 1) {
        emit_short();
    }
}

void X68kMidiTransport::emit_short()
{
    if (expected_size_ == 0 || message_size_ != expected_size_) {
        return;
    }

    X68kMidiMessage message;
    message.kind = current_status_ < 0xf0
        ? X68kMidiMessage::Kind::Channel
        : X68kMidiMessage::Kind::SystemCommon;
    message.status = current_status_;
    message.size = expected_size_;
    if (expected_size_ >= 2) {
        message.data1 = message_data_[0];
    }
    if (expected_size_ >= 3) {
        message.data2 = message_data_[1];
    }

    if (message.kind == X68kMidiMessage::Kind::Channel) {
        ++stats_.channel_messages;
        if (using_running_status_) {
            ++stats_.running_status_messages;
        }
        switch (message.status & 0xf0) {
        case 0x80:
            ++stats_.note_off_messages;
            break;
        case 0x90:
            if (message.data2 == 0) {
                ++stats_.note_off_messages;
            } else {
                ++stats_.note_on_messages;
            }
            break;
        case 0xb0:
            ++stats_.control_changes;
            break;
        case 0xc0:
            ++stats_.program_changes;
            break;
        case 0xe0:
            ++stats_.pitch_bends;
            break;
        default:
            break;
        }
    } else {
        ++stats_.system_common_messages;
    }

    if (sink_) {
        sink_(message);
    }

    // Channel voice messages retain running status. System common messages do
    // not. A following data byte starts another message using running status.
    message_size_ = 0;
    expected_size_ = 0;
    current_status_ = 0;
    using_running_status_ = false;
}

void X68kMidiTransport::transmit_one(uint8_t byte)
{
    // System realtime messages may occur anywhere and do not affect running
    // status or an in-progress SysEx. Hoot/PX68K traditionally discarded them
    // for external synth output; retain that behavior but count them.
    if (byte >= 0xf8) {
        ++stats_.realtime_bytes;
        return;
    }

    if (in_sysex_) {
        if (byte == 0xf7) {
            X68kMidiMessage message;
            message.kind = X68kMidiMessage::Kind::SysEx;
            message.status = 0xf0;
            message.size = 0;
            message.sysex = sysex_;
            ++stats_.sysex_messages;
            stats_.sysex_bytes += sysex_.size();
            stats_.last_status = 0xf0;
            if (sink_) {
                sink_(message);
            }
            sysex_.clear();
            in_sysex_ = false;
            running_status_ = 0;
            current_status_ = 0;
            expected_size_ = 0;
            message_size_ = 0;
            return;
        }
        if ((byte & 0x80) != 0) {
            // Any non-realtime status except EOX aborts the malformed SysEx and
            // is then interpreted as a fresh MIDI status byte.
            ++stats_.malformed_bytes;
            sysex_.clear();
            in_sysex_ = false;
        } else {
            sysex_.push_back(byte);
            return;
        }
    }

    if ((byte & 0x80) != 0) {
        if (byte == 0xf0) {
            in_sysex_ = true;
            sysex_.clear();
            running_status_ = 0;
            current_status_ = 0;
            expected_size_ = 0;
            message_size_ = 0;
            return;
        }
        if (byte == 0xf7) {
            ++stats_.malformed_bytes;
            return;
        }
        const uint8_t size = expected_size(byte);
        if (size == 0) {
            ++stats_.malformed_bytes;
            if (byte < 0xf8) {
                running_status_ = 0;
            }
            return;
        }
        begin_status(byte);
        return;
    }

    if (message_size_ == 0) {
        if (running_status_ == 0) {
            ++stats_.malformed_bytes;
            return;
        }
        current_status_ = running_status_;
        expected_size_ = expected_size(running_status_);
        message_size_ = 1;
        using_running_status_ = true;
    }

    if (message_size_ >= expected_size_ || message_size_ == 0) {
        ++stats_.malformed_bytes;
        return;
    }
    message_data_[message_size_ - 1] = byte & 0x7f;
    ++message_size_;
    if (message_size_ == expected_size_) {
        emit_short();
    }
}

} // namespace hoot
