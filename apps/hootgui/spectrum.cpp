#include "spectrum.h"

#include <algorithm>
#include <cmath>

namespace hootgui {
namespace {
constexpr double kPi = 3.14159265358979323846;
}

SpectrumAnalyzer::SpectrumAnalyzer(int sample_rate, size_t window_frames)
    : sample_rate_(std::max(sample_rate, 8000)),
      window_frames_(std::max<size_t>(window_frames, 256)),
      samples_(window_frames_ * 2, 0.0f)
{
}

void SpectrumAnalyzer::reset(int sample_rate)
{
    sample_rate_ = std::max(sample_rate, 8000);
    std::fill(samples_.begin(), samples_.end(), 0.0f);
    left_.fill(0.0f);
    right_.fill(0.0f);
    write_frame_ = 0;
    valid_frames_ = 0;
}

void SpectrumAnalyzer::push_s16(const int16_t* input, size_t frames)
{
    if (!input) return;
    for (size_t i = 0; i < frames; ++i) {
        samples_[write_frame_ * 2] = static_cast<float>(input[i * 2]) / 32768.0f;
        samples_[write_frame_ * 2 + 1] = static_cast<float>(input[i * 2 + 1]) / 32768.0f;
        write_frame_ = (write_frame_ + 1) % window_frames_;
        valid_frames_ = std::min(window_frames_, valid_frames_ + 1);
    }
}

float SpectrumAnalyzer::calculate_bin(int channel, double frequency) const
{
    if (valid_frames_ < 64 || frequency <= 0.0 || frequency >= sample_rate_ * 0.49) return 0.0f;

    // Goertzel over the latest window. A Hann window keeps the retro bar graph
    // readable instead of letting one strong FM carrier smear across every bin.
    const size_t n = valid_frames_;
    const double omega = 2.0 * kPi * frequency / static_cast<double>(sample_rate_);
    const double coeff = 2.0 * std::cos(omega);
    double s1 = 0.0;
    double s2 = 0.0;
    const size_t first = (write_frame_ + window_frames_ - n) % window_frames_;
    for (size_t i = 0; i < n; ++i) {
        const size_t frame = (first + i) % window_frames_;
        const double w = n > 1 ? 0.5 - 0.5 * std::cos((2.0 * kPi * i) / (n - 1)) : 1.0;
        const double sample = static_cast<double>(samples_[frame * 2 + static_cast<size_t>(channel)]) * w;
        const double s0 = sample + coeff * s1 - s2;
        s2 = s1;
        s1 = s0;
    }
    const double power = std::max(0.0, s1 * s1 + s2 * s2 - coeff * s1 * s2);
    const double amplitude = 2.0 * std::sqrt(power) / std::max<double>(1.0, static_cast<double>(n));
    // Log-compress to a visually useful 0..1 range, roughly -72..0 dB.
    const double db = 20.0 * std::log10(std::max(amplitude, 0.00025));
    return static_cast<float>(std::clamp((db + 72.0) / 72.0, 0.0, 1.0));
}

void SpectrumAnalyzer::calculate()
{
    const double low = 45.0;
    const double high = std::min(18000.0, sample_rate_ * 0.47);
    const double ratio = high / low;
    for (size_t i = 0; i < kBins; ++i) {
        const double t = static_cast<double>(i) / static_cast<double>(kBins - 1);
        const double f = low * std::pow(ratio, t);
        float l = 0.0f;
        float r = 0.0f;
        // Each displayed column represents a band, not one infinitesimally
        // narrow DFT frequency. Probe across that band so arbitrary musical
        // notes do not disappear between the logarithmic column centres.
        for (double scale : {0.94, 0.97, 1.0, 1.03, 1.06}) {
            l = std::max(l, calculate_bin(0, f * scale));
            r = std::max(r, calculate_bin(1, f * scale));
        }
        left_[i] = l;
        right_[i] = r;
    }
}

} // namespace hootgui
