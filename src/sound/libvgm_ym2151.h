#pragma once

#include <cstdint>
#include <vector>

namespace hoot {

class LibvgmYm2151 {
public:
    LibvgmYm2151();
    ~LibvgmYm2151();

    LibvgmYm2151(const LibvgmYm2151&) = delete;
    LibvgmYm2151& operator=(const LibvgmYm2151&) = delete;

    bool initialize(uint32_t clock,
                    uint32_t sample_rate,
                    bool use_nuked_core = false);
    void reset();
    void set_irq_handler(void (*handler)(void* param, uint8_t irq), void* param);
    void set_mute_mask(uint32_t mask);
    void write(uint8_t port, uint8_t data);
    uint8_t read(uint8_t port);
    void render_s16(int16_t* interleaved_stereo, int frames);
    bool uses_nuked_core() const;

private:
    void* chip_ = nullptr;
    const void* dev_def_ = nullptr;
    bool using_nuked_core_ = false;
    uint32_t sample_rate_ = 44100;
    std::vector<int32_t> left_;
    std::vector<int32_t> right_;
};

} // namespace hoot
