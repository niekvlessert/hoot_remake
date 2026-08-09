#include <cstdint>
#include <iostream>
#include <vector>

#include "sound/pc98_mpu401.h"

int main()
{
    hoot::Pc98Mpu401 mpu;
    std::vector<hoot::X68kMidiMessage> messages;
    mpu.reset();
    mpu.set_sink([&](const hoot::X68kMidiMessage& m) { messages.push_back(m); });

    // Default PC-98 MPU base and status semantics.
    if (!mpu.handles_port(0xe0d0) || !mpu.handles_port(0xe0d2)) return 1;
    if ((mpu.read_port(0xe0d2) & 0x80) == 0) return 2; // no input available

    mpu.write_port(0xe0d2, 0xff);
    if ((mpu.read_port(0xe0d2) & 0x80) != 0) return 3;
    if (mpu.read_port(0xe0d0) != 0xfe) return 4;

    mpu.write_port(0xe0d2, 0x3f);
    if (mpu.read_port(0xe0d0) != 0xfe || !mpu.uart_mode()) return 5;

    // One Note On plus running-status Note On.
    mpu.write_port(0xe0d0, 0x90);
    mpu.write_port(0xe0d0, 60);
    mpu.write_port(0xe0d0, 100);
    mpu.write_port(0xe0d0, 64);
    mpu.write_port(0xe0d0, 80);
    mpu.advance_frames(4410, 44100); // 100 ms, plenty for five MIDI bytes
    if (messages.size() != 2) return 6;
    if (messages[0].status != 0x90 || messages[0].data1 != 60 || messages[0].data2 != 100) return 7;
    if (messages[1].status != 0x90 || messages[1].data1 != 64 || messages[1].data2 != 80) return 8;

    // Common alternate MPU-PC98 address and firmware query.
    mpu.write_port(0xc8d2, 0xac);
    if (mpu.read_port(0xc8d0) != 0xfe) return 9;
    if (mpu.read_port(0xc8d0) != 0x15) return 10;
    if (mpu.active_base() != 0xc8d0) return 11;

    const auto& st = mpu.stats();
    const auto& mt = mpu.transport_stats();
    if (st.reset_commands != 1 || st.uart_commands != 1 || st.version_queries != 1) return 12;
    if (mt.bytes_transmitted != 5 || mt.note_on_messages != 2 || mt.running_status_messages != 1) return 13;

    // Real PC-98 residents use MPU intelligent-mode host clocks and WSD
    // direct-send messages.  Exercise the subset needed by MMD-family packs.
    hoot::Pc98Mpu401 intelligent;
    std::vector<hoot::X68kMidiMessage> intelligent_messages;
    intelligent.reset();
    intelligent.set_sink([&](const hoot::X68kMidiMessage& m) { intelligent_messages.push_back(m); });
    auto command_ack = [&](uint8_t command) {
        intelligent.write_port(0xe0d2, command);
        if ((intelligent.read_port(0xe0d2) & 0x80) != 0) return false;
        return intelligent.read_port(0xe0d0) == 0xfe;
    };
    if (!command_ack(0xc5)) return 14;        // 120 clocks/quarter
    if (!command_ack(0xe0)) return 15;
    intelligent.write_port(0xe0d0, 100);      // tempo
    if (!command_ack(0xe7)) return 16;
    intelligent.write_port(0xe0d0, 0x04);     // clock-to-host divisor 1
    if (!command_ack(0x95)) return 17;         // clock-to-host on
    if (!command_ack(0x0a)) return 18;         // play + MIDI start
    if (!intelligent.intelligent_clock_active()) return 19;
    const int until_clock = intelligent.frames_until_intelligent_event(44100);
    if (until_clock <= 0 || until_clock == 0x7fffffff) return 20;
    intelligent.advance_frames(until_clock, 44100);
    if (!intelligent.irq_pending() || intelligent.read_port(0xe0d0) != 0xfd) return 21;

    if (!command_ack(0xd0)) return 22;
    intelligent.write_port(0xe0d0, 0x90);
    intelligent.write_port(0xe0d0, 67);
    intelligent.write_port(0xe0d0, 96);
    intelligent.advance_frames(4410, 44100);
    if (intelligent_messages.empty()) return 23;
    bool saw_note = false;
    for (const auto& m : intelligent_messages) {
        if (m.status == 0x90 && m.data1 == 67 && m.data2 == 96) saw_note = true;
    }
    if (!saw_note || intelligent.stats().intelligent_direct_messages != 1) return 24;
    while ((intelligent.read_port(0xe0d2) & 0x80) == 0) {
        (void)intelligent.read_port(0xe0d0);
    }
    if (!command_ack(0x94) || intelligent.intelligent_clock_active()) return 25;
    return 0;
}
