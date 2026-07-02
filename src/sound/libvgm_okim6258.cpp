#include "sound/libvgm_okim6258.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace hoot {
namespace {

constexpr int kIncrShift = 12;
constexpr std::array<int, 8> kIndexShift = {-1, -1, -1, -1, 2, 4, 6, 8};
constexpr std::array<uint8_t, 30> kReleaseData = {
    0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80,
    0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80,
    0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80,
};

std::array<int, 49 * 16> build_diff_lookup()
{
    constexpr int nbl2bit[16][4] = {
        {1, 0, 0, 0},  {1, 0, 0, 1},  {1, 0, 1, 0},  {1, 0, 1, 1},
        {1, 1, 0, 0},  {1, 1, 0, 1},  {1, 1, 1, 0},  {1, 1, 1, 1},
        {-1, 0, 0, 0}, {-1, 0, 0, 1}, {-1, 0, 1, 0}, {-1, 0, 1, 1},
        {-1, 1, 0, 0}, {-1, 1, 0, 1}, {-1, 1, 1, 0}, {-1, 1, 1, 1},
    };

    std::array<int, 49 * 16> table{};
    for (int step = 0; step <= 48; ++step) {
        const auto stepval = static_cast<int>(std::floor(16.0 * std::pow(11.0 / 10.0, step)));
        for (int nib = 0; nib < 16; ++nib) {
            table[step * 16 + nib] = nbl2bit[nib][0]
                * (stepval * nbl2bit[nib][1]
                   + stepval / 2 * nbl2bit[nib][2]
                   + stepval / 4 * nbl2bit[nib][3]
                   + stepval / 8);
        }
    }
    return table;
}

const std::array<int, 49 * 16>& diff_lookup()
{
    static const auto table = build_diff_lookup();
    return table;
}

} // namespace

LibvgmOkim6258::LibvgmOkim6258() = default;
LibvgmOkim6258::~LibvgmOkim6258() = default;

bool LibvgmOkim6258::initialize(uint32_t output_sample_rate)
{
    output_sample_rate_ = output_sample_rate;
    reset();
    return true;
}

void LibvgmOkim6258::reset()
{
    pan_and_rate_ = 0x08;
    playback_data_ = nullptr;
    playback_size_ = 0;
    sample_index_ = 0;
    signal_ = 0;
    prev_signal_ = 0;
    step_ = 0;
    count_ = 0;
    playing_ = false;
    release_ = false;
}

void LibvgmOkim6258::stop()
{
    playing_ = false;
    sample_index_ = 0;
    if (signal_ != 0) {
        release_ = true;
        playback_data_ = kReleaseData.data();
        playback_size_ = 26;
        count_ = 0;
    } else {
        release_ = false;
        playback_data_ = nullptr;
        playback_size_ = 0;
        step_ = 0;
    }
}

void LibvgmOkim6258::set_pan_and_rate(uint8_t value)
{
    pan_and_rate_ = value;
}

bool LibvgmOkim6258::play_memory(const uint8_t* data, size_t size)
{
    if (data == nullptr || size == 0) {
        stop();
        return false;
    }

    playback_data_ = data;
    playback_size_ = size;
    sample_index_ = 0;
    signal_ = 0;
    prev_signal_ = 0;
    step_ = 0;
    count_ = 0;
    playing_ = true;
    release_ = false;
    fetch();
    return true;
}

void LibvgmOkim6258::mix_s16(int16_t* interleaved_stereo, int frames, double gain)
{
    if (interleaved_stereo == nullptr || frames <= 0 || output_sample_rate_ == 0) {
        return;
    }

    const int incr = (current_native_rate() << kIncrShift) / static_cast<int>(output_sample_rate_);
    const int pan = pan_mask();

    for (int i = 0; i < frames; ++i) {
        if (playing_ || release_) {
            while (count_ >= (1 << kIncrShift)) {
                fetch();
                count_ -= (1 << kIncrShift);
            }

            const auto sample = static_cast<int32_t>(std::lround(static_cast<double>(prev_signal_ * 16) * gain));
            const auto out_index = i * 2;
            if ((pan & 1) != 0) {
                const auto mixed = std::clamp(static_cast<int32_t>(interleaved_stereo[out_index]) + sample, -32768, 32767);
                interleaved_stereo[out_index] = static_cast<int16_t>(mixed);
            }
            if ((pan & 2) != 0) {
                const auto mixed = std::clamp(static_cast<int32_t>(interleaved_stereo[out_index + 1]) + sample, -32768, 32767);
                interleaved_stereo[out_index + 1] = static_cast<int16_t>(mixed);
            }

            count_ += incr;
        }
    }
}

void LibvgmOkim6258::fetch()
{
    if (playback_data_ == nullptr) {
        playing_ = false;
        release_ = false;
        return;
    }

    if (sample_index_ / 2 >= static_cast<int>(playback_size_)) {
        if (playing_) {
            playing_ = false;
            release_ = true;
            playback_data_ = kReleaseData.data();
            playback_size_ = 26;
            sample_index_ = 0;
        } else if (release_) {
            release_ = false;
            step_ = 0;
            signal_ = 0;
        }
        return;
    }

    prev_signal_ = signal_;
    const auto val = static_cast<int>(playback_data_[sample_index_ / 2] >> (((sample_index_ & 1) << 2) ^ 0));
    ++sample_index_;

    signal_ += diff_lookup()[step_ * 16 + (val & 15)];
    signal_ = std::clamp(signal_, -2048, 2047);

    step_ += kIndexShift[val & 7];
    step_ = std::clamp(step_, 0, 48);
}

int LibvgmOkim6258::current_native_rate() const
{
    switch ((pan_and_rate_ >> 2) & 0x03) {
    case 0x00:
        return 7800;
    case 0x01:
        return 10400;
    case 0x02:
        return 15600;
    default:
        return 15600;
    }
}

int LibvgmOkim6258::pan_mask() const
{
    const auto swapped = static_cast<int>(((pan_and_rate_ & 0x01) << 1) | ((pan_and_rate_ & 0x02) >> 1));
    return swapped ^ 0x03;
}

} // namespace hoot
