#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <vector>

#include "sound/pc98_beep.h"

int main()
{
    hoot::Pc98Beep beep;
    if (!beep.initialize(48000, 2457600)) return 1;

    // PMDB programs channel 1 through the high-address PIT alias. A divider
    // of 2458 is approximately 1 kHz at a 2.4576 MHz PC-98 PIT clock.
    beep.write_port(0x3fdf, 0x76); // ch1, LSB/MSB, mode 3
    beep.write_port(0x3fdb, 0x9a);
    beep.write_port(0x3fdb, 0x09);
    if (std::abs(beep.frequency() - 1000.0) > 2.0) {
        std::cerr << "unexpected frequency " << beep.frequency() << "\n";
        return 2;
    }

    // 8255 BSR 06h resets PC3 => buzzer enabled.
    beep.write_port(0x37, 0x06);
    if (!beep.speaker_enabled()) return 3;
    std::vector<int16_t> audio(4800 * 2, 0);
    beep.mix_s16(audio.data(), 4800, 1.0);
    auto [mn, mx] = std::minmax_element(audio.begin(), audio.end());
    if (*mn >= 0 || *mx <= 0) return 4;

    // 07h sets PC3 => inhibit.
    beep.write_port(0x37, 0x07);
    if (beep.speaker_enabled()) return 5;
    std::fill(audio.begin(), audio.end(), 0);
    beep.mix_s16(audio.data(), 4800, 1.0);
    if (std::any_of(audio.begin(), audio.end(), [](int16_t v) { return v != 0; })) return 6;

    const auto& st = beep.stats();
    if (st.pit_data_writes != 2 || st.pit_control_writes != 1 || st.gate_changes != 2 || st.audible_frames != 4800) return 7;
    return 0;
}
