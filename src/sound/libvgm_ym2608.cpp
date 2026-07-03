#include "sound/libvgm_ym2608.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>

namespace hoot {
namespace {

uint8_t parse_psg_channel_mask()
{
    const char* value = std::getenv("HOOT_PSG_CHANNELS");
    if (value == nullptr) {
        value = std::getenv("HOOT_PSG_ONLY");
    }
    if (value == nullptr || *value == '\0') {
        return 0x07;
    }

    uint8_t mask = 0;
    for (const char* p = value; *p != '\0'; ++p) {
        switch (*p) {
        case 'A':
        case 'a':
        case '0':
            mask |= 0x01;
            break;
        case 'B':
        case 'b':
        case '1':
            mask |= 0x02;
            break;
        case 'C':
        case 'c':
        case '2':
            mask |= 0x04;
            break;
        default:
            break;
        }
    }
    return mask;
}

bool env_is_zero(const char* name)
{
    const char* value = std::getenv(name);
    return value != nullptr && (std::strcmp(value, "0") == 0 || std::strcmp(value, "false") == 0);
}

} // namespace

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
    ssg_polarity_ = 1.0;
    ssg_dc_prev_in_left_ = 0.0;
    ssg_dc_prev_in_right_ = 0.0;
    ssg_dc_prev_out_left_ = 0.0;
    ssg_dc_prev_out_right_ = 0.0;
    reset_debug_ssg_state();
    debug_psg_channel_mask_ = parse_psg_channel_mask();
    debug_psg_disable_tone_ = env_is_zero("HOOT_PSG_TONE");
    debug_psg_disable_noise_ = env_is_zero("HOOT_PSG_NOISE");
    debug_psg_raw_dc_ = std::getenv("HOOT_PSG_RAW") != nullptr;
    if (const char* value = std::getenv("HOOT_PSG_GAIN")) {
        ssg_gain_ = std::clamp(std::strtod(value, nullptr), 0.0, 4.0);
    }
    if (std::getenv("HOOT_DISABLE_PSG") != nullptr) {
        ssg_gain_ = 0.0;
    }
    if (const char* value = std::getenv("HOOT_PSG_INVERT")) {
        ssg_polarity_ = std::strtol(value, nullptr, 0) != 0 ? -1.0 : 1.0;
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
    ym2608_write(chip_, 0, 0x29);
    ym2608_write(chip_, 1, 0x80);
    update_ssg_sample_rate();
    return true;
}

void LibvgmYm2608::reset()
{
    if (chip_ != nullptr) {
        ym2608_reset_chip(chip_);
        ym2608_write(chip_, 0, 0x29);
        ym2608_write(chip_, 1, 0x80);
        ssg_phase_ = 0.0;
        ssg_dc_prev_in_left_ = 0.0;
        ssg_dc_prev_in_right_ = 0.0;
        ssg_dc_prev_out_left_ = 0.0;
        ssg_dc_prev_out_right_ = 0.0;
        reset_debug_ssg_state();
        update_ssg_sample_rate();
    }
}

void LibvgmYm2608::write(uint8_t port, uint8_t data)
{
    if (chip_ != nullptr) {
        ym2608_write(chip_, static_cast<uint8_t>(port & 3), data);
    }
}

uint8_t LibvgmYm2608::read(uint8_t port)
{
    if (chip_ == nullptr) {
        return 0xff;
    }
    return ym2608_read(chip_, static_cast<uint8_t>(port & 3));
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
    if (std::getenv("HOOT_SOLO_PSG") != nullptr) {
        std::fill(left_.begin(), left_.end(), 0);
        std::fill(right_.begin(), right_.end(), 0);
    }

    if (ssg_ != nullptr && ssg_sample_rate_ != 0 && sample_rate_ != 0) {
        const double ratio = static_cast<double>(ssg_sample_rate_) / static_cast<double>(sample_rate_);
        const double end_phase = ssg_phase_ + (static_cast<double>(frames) * ratio);
        const auto ssg_frames = static_cast<int>(std::floor(end_phase));
        if (ssg_frames > 0) {
            ssg_left_.assign(static_cast<size_t>(ssg_frames), 0);
            ssg_right_.assign(static_cast<size_t>(ssg_frames), 0);
            DEV_SMPL* ssg_buffers[2] = {ssg_left_.data(), ssg_right_.data()};
            ay8910_update_one(ssg_, static_cast<UINT32>(ssg_frames), ssg_buffers);

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
                    const double ssg_left_sample = static_cast<double>(left_sum) / static_cast<double>(count);
                    const double ssg_right_sample = static_cast<double>(right_sum) / static_cast<double>(count);
                    double filtered_left = ssg_left_sample;
                    double filtered_right = ssg_right_sample;
                    if (!debug_psg_raw_dc_) {
                        constexpr double kDcBlock = 0.9995;
                        filtered_left = ssg_left_sample - ssg_dc_prev_in_left_
                            + (kDcBlock * ssg_dc_prev_out_left_);
                        filtered_right = ssg_right_sample - ssg_dc_prev_in_right_
                            + (kDcBlock * ssg_dc_prev_out_right_);
                        ssg_dc_prev_in_left_ = ssg_left_sample;
                        ssg_dc_prev_in_right_ = ssg_right_sample;
                        ssg_dc_prev_out_left_ = filtered_left;
                        ssg_dc_prev_out_right_ = filtered_right;
                    }
                    left_[i] += static_cast<int32_t>(std::lround(filtered_left * ssg_gain_ * ssg_polarity_));
                    right_[i] += static_cast<int32_t>(std::lround(filtered_right * ssg_gain_ * ssg_polarity_));
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

void LibvgmYm2608::set_ssg_gain(double gain)
{
    ssg_gain_ = std::clamp(gain, 0.0, 4.0);
}

void LibvgmYm2608::set_ssg_inverted(bool inverted)
{
    ssg_polarity_ = inverted ? -1.0 : 1.0;
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
        data = self->filter_debug_ssg_write(address, data);
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
        self->ssg_dc_prev_in_left_ = 0.0;
        self->ssg_dc_prev_in_right_ = 0.0;
        self->ssg_dc_prev_out_left_ = 0.0;
        self->ssg_dc_prev_out_right_ = 0.0;
        self->reset_debug_ssg_state();
        self->update_ssg_sample_rate();
    }
}

void LibvgmYm2608::reset_debug_ssg_state()
{
    debug_ssg_latch_ = 0;
    debug_ssg_regs_.fill(0);
}

uint8_t LibvgmYm2608::filter_debug_ssg_write(uint8_t address, uint8_t data)
{
    if ((address & 1) == 0) {
        debug_ssg_latch_ = static_cast<uint8_t>(data & 0x0f);
        return data;
    }

    const uint8_t reg = debug_ssg_latch_;
    uint8_t filtered = data;
    if (reg == 7) {
        for (uint8_t channel = 0; channel < 3; ++channel) {
            if ((debug_psg_channel_mask_ & (1u << channel)) == 0) {
                filtered = static_cast<uint8_t>(filtered | (1u << channel) | (1u << (channel + 3)));
            }
        }
        if (debug_psg_disable_tone_) {
            filtered = static_cast<uint8_t>(filtered | 0x07);
        }
        if (debug_psg_disable_noise_) {
            filtered = static_cast<uint8_t>(filtered | 0x38);
        }
    } else if (reg >= 8 && reg <= 10) {
        const uint8_t channel = static_cast<uint8_t>(reg - 8);
        if ((debug_psg_channel_mask_ & (1u << channel)) == 0) {
            filtered = 0;
        }
    }
    debug_ssg_regs_[reg] = filtered;
    return filtered;
}

void LibvgmYm2608::update_ssg_sample_rate()
{
    if (ssg_ != nullptr) {
        ssg_sample_rate_ = ay8910_get_sample_rate(ssg_);
    }
}

} // namespace hoot
