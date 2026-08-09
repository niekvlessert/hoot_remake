#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <vector>

#include "spectrum.h"

int main()
{
    constexpr int rate = 44100;
    constexpr double pi = 3.14159265358979323846;
    hootgui::SpectrumAnalyzer analyzer(rate, 2048);
    std::vector<int16_t> pcm(4096 * 2);
    for (size_t i = 0; i < pcm.size() / 2; ++i) {
        const double t = static_cast<double>(i) / rate;
        const int16_t left = static_cast<int16_t>(std::sin(2.0 * pi * 440.0 * t) * 12000.0);
        const int16_t right = static_cast<int16_t>(std::sin(2.0 * pi * 1760.0 * t) * 10000.0);
        pcm[i * 2] = left;
        pcm[i * 2 + 1] = right;
    }
    analyzer.push_s16(pcm.data(), pcm.size() / 2);
    analyzer.calculate();
    const auto& l = analyzer.left();
    const auto& r = analyzer.right();
    assert(*std::max_element(l.begin(), l.end()) > 0.4f);
    assert(*std::max_element(r.begin(), r.end()) > 0.4f);
    for (float v : l) assert(std::isfinite(v) && v >= 0.0f && v <= 1.0f);
    for (float v : r) assert(std::isfinite(v) && v >= 0.0f && v <= 1.0f);
    return 0;
}
