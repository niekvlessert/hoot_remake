#include <cassert>
#include <cstdint>
#include <vector>

#include "sound/x68k_midi_transport.h"

int main()
{
    hoot::X68kMidiTransport midi;
    std::vector<hoot::X68kMidiMessage> messages;
    midi.set_sink([&](const auto& message) { messages.push_back(message); });

    midi.enqueue(0x90);
    midi.enqueue(60);
    midi.enqueue(100);
    midi.enqueue(61);   // running status
    midi.enqueue(90);
    assert(midi.buffered_bytes() == 5);
    midi.advance_cycles(3199);
    assert(messages.empty());
    midi.advance_cycles(1);
    assert(messages.empty());
    midi.advance_cycles(3200 * 4);
    assert(messages.size() == 2);
    assert(messages[0].status == 0x90 && messages[0].data1 == 60 && messages[0].data2 == 100);
    assert(messages[1].status == 0x90 && messages[1].data1 == 61 && messages[1].data2 == 90);
    assert(midi.stats().running_status_messages == 1);

    midi.enqueue(0xf0);
    midi.enqueue(0x7e);
    midi.enqueue(0x7f);
    midi.enqueue(0x09);
    midi.enqueue(0x01);
    midi.enqueue(0xf7);
    midi.advance_cycles(3200 * 6);
    assert(messages.size() == 3);
    assert(messages[2].kind == hoot::X68kMidiMessage::Kind::SysEx);
    assert((messages[2].sysex == std::vector<uint8_t>{0x7e, 0x7f, 0x09, 0x01}));

    midi.reset();
    for (size_t i = 0; i < hoot::X68kMidiTransport::kHardwareFifoBytes; ++i) {
        midi.enqueue(0xfe);
    }
    assert(midi.tx_full());
    midi.advance_cycles(3200);
    assert(midi.tx_ready());
    assert(midi.stats().peak_fifo_bytes == 256);
    assert(midi.stats().fifo_full_transitions == 1);

    return 0;
}
