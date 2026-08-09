#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace hootgui {

class SpectrumAnalyzer {
public:
    static constexpr size_t kBins = 48;

    explicit SpectrumAnalyzer(int sample_rate = 44100, size_t window_frames = 2048);
    void reset(int sample_rate);
    void push_s16(const int16_t* interleaved_stereo, size_t frames);
    void calculate();

    const std::array<float, kBins>& left() const { return left_; }
    const std::array<float, kBins>& right() const { return right_; }

private:
    float calculate_bin(int channel, double frequency) const;

    int sample_rate_ = 44100;
    size_t window_frames_ = 2048;
    std::vector<float> samples_;
    size_t write_frame_ = 0;
    size_t valid_frames_ = 0;
    std::array<float, kBins> left_{};
    std::array<float, kBins> right_{};
};

} // namespace hootgui
