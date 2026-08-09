#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <vector>

#include "sound/libvgm_opl.h"

namespace {
void program_tone(hoot::LibvgmOpl& opl)
{
    auto reg = [&](uint8_t r, uint8_t v) { opl.write(0, r); opl.write(1, v); };
    reg(0x20, 0x01); reg(0x40, 0x18); reg(0x60, 0xf0); reg(0x80, 0x77);
    reg(0x23, 0x01); reg(0x43, 0x00); reg(0x63, 0xf0); reg(0x83, 0x77);
    reg(0xc0, 0x01);
    reg(0xa0, 0x98);
    reg(0xb0, 0x31);
}

void run(hoot::LibvgmOpl::Model model, uint32_t clock)
{
    hoot::LibvgmOpl opl;
    assert(opl.initialize(model, clock, 44100));
    program_tone(opl);
    std::vector<int16_t> audio(4096 * 2);
    opl.render_s16(audio.data(), 4096);
    int peak = 0;
    size_t nonzero = 0;
    for (const auto v : audio) {
        peak = std::max(peak, std::abs(static_cast<int>(v)));
        if (v != 0) ++nonzero;
    }
    assert(peak > 100);
    assert(nonzero > 1000);
}
}

int main()
{
    run(hoot::LibvgmOpl::Model::YM3812, 3579545u);
    run(hoot::LibvgmOpl::Model::YMF262, 14318180u);
    return 0;
}
