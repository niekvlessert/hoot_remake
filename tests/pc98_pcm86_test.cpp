#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <vector>

#include "sound/pc98_pcm86.h"

namespace {

bool test_stereo8()
{
    hoot::Pc98Pcm86 pcm;
    if (!pcm.initialize(44100)) return false;
    if (pcm.read_port(0xa460) != 0x40) return false;

    pcm.write_port(0xa466, 0xa0); // max volume
    pcm.write_port(0xa46a, 0x70); // signed 8-bit stereo
    for (int i = 0; i < 128; ++i) {
        pcm.write_port(0xa46c, (i & 1) ? 0xc0 : 0x40);
    }
    pcm.write_port(0xa468, 0x80); // 44.1 kHz, playback on

    std::vector<int16_t> audio(64 * 2);
    pcm.mix_s16(audio.data(), 64);
    if (audio[0] <= 1000 || audio[1] >= -1000) {
        std::cerr << "stereo8 polarity mismatch L=" << audio[0]
                  << " R=" << audio[1] << "\n";
        return false;
    }
    const auto& stats = pcm.stats();
    return stats.fifo_writes == 128 && stats.fifo_reads >= 126
        && stats.rendered_source_frames >= 63 && stats.fifo_overflows == 0;
}

bool test_big_endian_16()
{
    hoot::Pc98Pcm86 pcm;
    if (!pcm.initialize(44100)) return false;
    pcm.write_port(0xa466, 0xa0);
    pcm.write_port(0xa46a, 0x30); // signed 16-bit stereo
    // L=+0x4000, R=-0x4000, big-endian.
    for (uint8_t byte : {uint8_t{0x40}, uint8_t{0x00}, uint8_t{0xc0}, uint8_t{0x00}}) {
        pcm.write_port(0xa46c, byte);
    }
    pcm.write_port(0xa468, 0x80);
    int16_t audio[2] = {};
    pcm.mix_s16(audio, 1);
    if (audio[0] != 0x4000 || audio[1] != static_cast<int16_t>(-0x4000)) {
        std::cerr << "16-bit endian mismatch L=" << audio[0]
                  << " R=" << audio[1] << "\n";
        return false;
    }
    return true;
}

bool test_rate_and_low_water_irq()
{
    hoot::Pc98Pcm86 pcm;
    if (!pcm.initialize(44100)) return false;
    pcm.write_port(0xa46a, 0x70); // stereo8
    pcm.write_port(0xa468, 0x20); // threshold programming enabled
    pcm.write_port(0xa46a, 0x00); // 128-byte low-water threshold
    pcm.write_port(0xa468, 0x00);
    for (int i = 0; i < 512; ++i) pcm.write_port(0xa46c, static_cast<uint8_t>(i));
    pcm.write_port(0xa468, 0xa2); // 22.05 kHz, IRQ + playback
    if (std::abs(pcm.source_rate() - 22050.0) > 0.01) return false;
    const int until = pcm.frames_until_irq();
    if (until <= 0 || until > 1000) return false;
    std::vector<int16_t> audio(static_cast<size_t>(until + 8) * 2);
    pcm.mix_s16(audio.data(), until + 8);
    if (!pcm.irq_pending()) {
        std::cerr << "low-water IRQ did not assert\n";
        return false;
    }
    pcm.mark_irq_delivered();
    if (pcm.stats().irq_requests == 0 || pcm.stats().irq_deliveries == 0) return false;
    pcm.write_port(0xa468, 0x82); // falling bit 4/5 acknowledgement style
    return true;
}

bool test_all_rates_and_channel_modes()
{
    static constexpr double rates[8] = {
        44100.0, 33075.0, 22050.0, 16537.5,
        11025.0, 8268.75, 5501.25, 4134.375,
    };
    hoot::Pc98Pcm86 pcm;
    if (!pcm.initialize(48000)) return false;
    for (int rate = 0; rate < 8; ++rate) {
        pcm.write_port(0xa468, static_cast<uint8_t>(rate));
        if (std::abs(pcm.source_rate() - rates[rate]) > 0.01) {
            std::cerr << "rate mismatch selector=" << rate
                      << " got=" << pcm.source_rate() << "\n";
            return false;
        }
    }

    struct ModeCase { uint8_t mode; int bytes; };
    for (const ModeCase mc : {ModeCase{0x10,2}, ModeCase{0x20,2}, ModeCase{0x30,4},
                              ModeCase{0x50,1}, ModeCase{0x60,1}, ModeCase{0x70,2}}) {
        pcm.reset();
        pcm.write_port(0xa466, 0xa0);
        pcm.write_port(0xa46a, mc.mode);
        // Enough non-zero bytes for several source frames in every format.
        for (int i = 0; i < mc.bytes * 8; ++i) pcm.write_port(0xa46c, 0x40);
        pcm.write_port(0xa468, 0x80);
        int16_t audio[2] = {};
        pcm.mix_s16(audio, 1);
        if (audio[0] == 0 && audio[1] == 0) {
            std::cerr << "DAC mode produced silence mode=" << static_cast<int>(mc.mode) << "\n";
            return false;
        }
    }
    return true;
}

} // namespace

int main()
{
    if (!test_stereo8()) return 1;
    if (!test_big_endian_16()) return 1;
    if (!test_rate_and_low_water_irq()) return 1;
    if (!test_all_rates_and_channel_modes()) return 1;
    return 0;
}
