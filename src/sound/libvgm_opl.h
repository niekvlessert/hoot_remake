#pragma once

#include <array>
#include <cstdint>
#include <vector>

namespace hoot {

class LibvgmOpl {
public:
    enum class Model { YM3812, YMF262 };

    LibvgmOpl();
    ~LibvgmOpl();
    LibvgmOpl(const LibvgmOpl&) = delete;
    LibvgmOpl& operator=(const LibvgmOpl&) = delete;

    bool initialize(Model model, uint32_t clock, uint32_t sample_rate);
    void reset();
    void write(uint8_t port, uint8_t data);
    uint8_t read(uint8_t port) const;
    void render_s16(int16_t* interleaved_stereo, int frames);
    Model model() const { return model_; }
    uint8_t register_value(int bank, uint8_t reg) const { return registers_[bank & 1][reg]; }

private:
    void shutdown();

    void* chip_ = nullptr;
    Model model_ = Model::YM3812;
    uint32_t sample_rate_ = 44100;
    std::array<uint8_t, 2> address_latch_{};
    std::array<std::array<uint8_t, 256>, 2> registers_{};
    std::vector<int32_t> left_;
    std::vector<int32_t> right_;
};

} // namespace hoot
