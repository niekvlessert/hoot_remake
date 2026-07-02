#include "sound/libvgm_ym2608.h"

#include <algorithm>
#include <cstdlib>
#include <cmath>

namespace hoot {

LibvgmYm2608::LibvgmYm2608() = default;

LibvgmYm2608::~LibvgmYm2608()
{
    if (chip_ != nullptr) {
        ym2608_shutdown(chip_);
        chip_ = nullptr;
    }
    if (ssg_ != nullptr) {
        ay8910_stop(ssg_);
        ssg_ = nullptr;
    }
}

bool LibvgmYm2608::initialize(uint32_t clock, uint32_t sample_rate)
{
    if (chip_ != nullptr) {
        ym2608_shutdown(chip_);
        chip_ = nullptr;
    }
    if (ssg_ != nullptr) {
        ay8910_stop(ssg_);
        ssg_ = nullptr;
    }
    sample_rate_ = sample_rate;
    ssg_sample_rate_ = 0;
    ssg_phase_ = 0.0;
    ssg_gain_ = 0.90;
    if (const char* value = std::getenv("HOOT_PSG_GAIN")) {
        ssg_gain_ = std::clamp(std::strtod(value, nullptr), 0.0, 4.0);
    }
    if (std::getenv("HOOT_DISABLE_PSG") != nullptr) {
        ssg_gain_ = 0.0;
    }

    chip_ = ym2608_init(this, clock, sample_rate, nullptr, nullptr);
    if (chip_ == nullptr) {
        return false;
    }
    if (ay8910_start(&ssg_, clock / 4, AYTYPE_YM2608, YM2149_PIN26_HIGH) == 0 || ssg_ == nullptr) {
        ym2608_shutdown(chip_);
        chip_ = nullptr;
        return false;
    }
    const ssg_callbacks callbacks = {
        &LibvgmYm2608::set_ssg_clock,
        &LibvgmYm2608::write_ssg,
        &LibvgmYm2608::read_ssg,
        &LibvgmYm2608::reset_ssg,
    };
    ym2608_link_ssg(chip_, &callbacks, this);
    update_ssg_sample_rate();
    ym2608_reset_chip(chip_);
    update_ssg_sample_rate();
    return true;
}

void LibvgmYm2608::reset()
{
    if (chip_ != nullptr) {
        ym2608_reset_chip(chip_);
        ssg_phase_ = 0.0;
        update_ssg_sample_rate();
    }
}

void LibvgmYm2608::write(uint8_t port, uint8_t data)
{
    if (chip_ != nullptr) {
        ym2608_write(chip_, static_cast<uint8_t>(port & 1), data);
    }
}

uint8_t LibvgmYm2608::read(uint8_t port)
{
    if (chip_ == nullptr) {
        return 0xff;
    }
    return ym2608_read(chip_, static_cast<uint8_t>(port & 1));
}

void LibvgmYm2608::render_s16(int16_t* interleaved_stereo, int frames)
{
    if (interleaved_stereo == nullptr || frames <= 0) {
        return;
    }
    if (chip_ == nullptr) {
        std::fill(interleaved_stereo, interleaved_stereo + (frames * 2), int16_t{0});
        return;
    }

    left_.assign(static_cast<size_t>(frames), 0);
    right_.assign(static_cast<size_t>(frames), 0);
    DEV_SMPL* buffers[2] = {left_.data(), right_.data()};
    ym2608_update_one(chip_, static_cast<UINT32>(frames), buffers);

    if (ssg_ != nullptr && ssg_sample_rate_ != 0 && sample_rate_ != 0) {
        const double ratio = static_cast<double>(ssg_sample_rate_) / static_cast<double>(sample_rate_);
        const double end_phase = ssg_phase_ + (static_cast<double>(frames) * ratio);
        const auto ssg_frames = static_cast<int>(std::floor(end_phase));
        if (ssg_frames > 0) {
            ssg_left_.assign(static_cast<size_t>(ssg_frames), 0);
            ssg_right_.assign(static_cast<size_t>(ssg_frames), 0);
            DEV_SMPL* ssg_buffers[2] = {ssg_left_.data(), ssg_right_.data()};
            ay8910_update_one(ssg_, static_cast<UINT32>(ssg_frames), ssg_buffers);

            int64_t ssg_left_sum = 0;
            int64_t ssg_right_sum = 0;
            for (int i = 0; i < ssg_frames; ++i) {
                ssg_left_sum += ssg_left_[i];
                ssg_right_sum += ssg_right_[i];
            }
            const auto ssg_left_dc = static_cast<int32_t>(ssg_left_sum / ssg_frames);
            const auto ssg_right_dc = static_cast<int32_t>(ssg_right_sum / ssg_frames);

            for (int i = 0; i < frames; ++i) {
                const double source_start = ssg_phase_ + (static_cast<double>(i) * ratio);
                const double source_end = ssg_phase_ + (static_cast<double>(i + 1) * ratio);
                const auto first = static_cast<size_t>(std::floor(source_start));
                auto last = static_cast<size_t>(std::floor(source_end));
                if (last <= first) {
                    last = first + 1;
                }
                last = std::min(last, static_cast<size_t>(ssg_frames));
                if (first < last) {
                    int64_t left_sum = 0;
                    int64_t right_sum = 0;
                    for (size_t source_index = first; source_index < last; ++source_index) {
                        left_sum += ssg_left_[source_index];
                        right_sum += ssg_right_[source_index];
                    }
                    const auto count = static_cast<int32_t>(last - first);
                    const auto ssg_left_sample = static_cast<int32_t>(left_sum / count) - ssg_left_dc;
                    const auto ssg_right_sample = static_cast<int32_t>(right_sum / count) - ssg_right_dc;
                    left_[i] += static_cast<int32_t>(std::lround(static_cast<double>(ssg_left_sample) * ssg_gain_));
                    right_[i] += static_cast<int32_t>(std::lround(static_cast<double>(ssg_right_sample) * ssg_gain_));
                }
            }
            ssg_phase_ = end_phase - static_cast<double>(ssg_frames);
        } else {
            ssg_phase_ = end_phase;
        }
    }

    for (int i = 0; i < frames; ++i) {
        const auto l = std::clamp(left_[i], -32768, 32767);
        const auto r = std::clamp(right_[i], -32768, 32767);
        interleaved_stereo[i * 2] = static_cast<int16_t>(l);
        interleaved_stereo[i * 2 + 1] = static_cast<int16_t>(r);
    }
}

void LibvgmYm2608::set_mute_mask(uint32_t mask)
{
    if (chip_ != nullptr) {
        ym2608_set_mute_mask(chip_, mask);
    }
}

void LibvgmYm2608::set_ssg_clock(void* param, uint32_t clock)
{
    auto* self = static_cast<LibvgmYm2608*>(param);
    if (self->ssg_ != nullptr) {
        ay8910_set_clock(self->ssg_, clock);
        self->update_ssg_sample_rate();
    }
}

void LibvgmYm2608::write_ssg(void* param, uint8_t address, uint8_t data)
{
    auto* self = static_cast<LibvgmYm2608*>(param);
    if (self->ssg_ != nullptr) {
        ay8910_write(self->ssg_, address, data);
    }
}

uint8_t LibvgmYm2608::read_ssg(void* param, uint8_t address)
{
    auto* self = static_cast<LibvgmYm2608*>(param);
    if (self->ssg_ == nullptr) {
        return 0xff;
    }
    return ay8910_read(self->ssg_, address);
}

void LibvgmYm2608::reset_ssg(void* param)
{
    auto* self = static_cast<LibvgmYm2608*>(param);
    if (self->ssg_ != nullptr) {
        ay8910_reset(self->ssg_);
        self->ssg_phase_ = 0.0;
        self->update_ssg_sample_rate();
    }
}

void LibvgmYm2608::update_ssg_sample_rate()
{
    if (ssg_ != nullptr) {
        ssg_sample_rate_ = ay8910_get_sample_rate(ssg_);
    }
}

} // namespace hoot