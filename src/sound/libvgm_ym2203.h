#pragma once

#include <cstdint>
#include <array>
#include <vector>

namespace hoot {

class LibvgmYm2203 {
public:
    LibvgmYm2203();
    ~LibvgmYm2203();

    LibvgmYm2203(const LibvgmYm2203&) = delete;
    LibvgmYm2203& operator=(const LibvgmYm2203&) = delete;

    bool initialize(uint32_t clock, uint32_t sample_rate);
    void reset();
    void write(uint8_t port, uint8_t data);
    uint8_t read(uint8_t port);
    void render_s16(int16_t* interleaved_stereo, int frames);
    void set_ssg_gain(double gain);

private:
    static void set_ssg_clock(void* param, uint32_t clock);
    static void write_ssg(void* param, uint8_t address, uint8_t data);
    static uint8_t read_ssg(void* param, uint8_t address);
    static void reset_ssg(void* param);

    void reset_debug_ssg_state();
    uint8_t filter_debug_ssg_write(uint8_t address, uint8_t data);
    void update_ssg_sample_rate();

    void* chip_ = nullptr;
    void* ssg_ = nullptr;
    uint32_t sample_rate_ = 44100;
    uint32_t ssg_sample_rate_ = 0;
    double ssg_phase_ = 0.0;
    double ssg_gain_ = 0.90;
    double ssg_dc_prev_in_left_ = 0.0;
    double ssg_dc_prev_in_right_ = 0.0;
    double ssg_dc_prev_out_left_ = 0.0;
    double ssg_dc_prev_out_right_ = 0.0;
    uint8_t debug_psg_channel_mask_ = 0x07;
    bool debug_psg_disable_tone_ = false;
    bool debug_psg_disable_noise_ = false;
    bool debug_psg_raw_dc_ = false;
    uint8_t debug_ssg_latch_ = 0;
    std::array<uint8_t, 16> debug_ssg_regs_{};
    std::vector<int32_t> left_;
    std::vector<int32_t> right_;
    std::vector<int32_t> ssg_left_;
    std::vector<int32_t> ssg_right_;
};

} // namespace hoot
