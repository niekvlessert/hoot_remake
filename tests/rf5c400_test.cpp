#include <algorithm>
#include <cstdint>
#include <iostream>
#include <vector>

#include "sound/rf5c400.h"

int main()
{
    hoot::Rf5c400 chip;
    if (!chip.initialize(16934400, 44100)) return 1;
    std::vector<uint8_t> rom(32, 0);
    for (size_t i = 0; i < rom.size(); i += 2) {
        const int16_t sample = (i & 2) ? int16_t{12000} : int16_t{6000};
        rom[i] = static_cast<uint8_t>(sample);
        rom[i + 1] = static_cast<uint8_t>(sample >> 8);
    }
    chip.set_rom(std::move(rom));
    chip.write16(0x400 + 0x00, 0x0000);
    chip.write16(0x400 + 0x01, 0x0000);
    chip.write16(0x400 + 0x02, 0x1000);
    chip.write16(0x400 + 0x03, 0x0007);
    chip.write16(0x400 + 0x04, 0x0000);
    chip.write16(0x400 + 0x05, 0x0000);
    chip.write16(0x400 + 0x06, 0x0000);
    chip.write16(0x400 + 0x08, 0x0000);
    chip.write16(0x400 + 0x09, 0x0100);
    chip.write16(0x400 + 0x0c, 0x0100);
    chip.write16(0x01, 0x0060);

    std::vector<int16_t> output(64 * 2);
    chip.render_s16(output.data(), 64);
    if (!chip.channel_active(0)
        || std::none_of(output.begin(), output.end(), [](int16_t value) { return value != 0; })) {
        std::cerr << "RF5C400 produced no active PCM output\n";
        return 1;
    }
    chip.set_channel_muted(0, true);
    chip.render_s16(output.data(), 8);
    if (std::any_of(output.begin(), output.begin() + 16,
                    [](int16_t value) { return value != 0; })) {
        std::cerr << "RF5C400 channel mute did not silence output\n";
        return 1;
    }
    return 0;
}
