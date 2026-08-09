#include "sound/libvgm_opl.h"

#include <algorithm>

extern "C" {
#include "../../stdtype.h"
typedef void (*DEVCB_LOG)(void* userParam, void* source, UINT8 level, const char* message);
#define SNDDEV_YM3812
#include "fmopl.h"
#include "ymf262.h"
}

namespace hoot {

LibvgmOpl::LibvgmOpl() = default;
LibvgmOpl::~LibvgmOpl() { shutdown(); }

void LibvgmOpl::shutdown()
{
    if (!chip_) return;
    if (model_ == Model::YMF262) ymf262_shutdown(chip_);
    else ym3812_shutdown(chip_);
    chip_ = nullptr;
}

bool LibvgmOpl::initialize(Model model, uint32_t clock, uint32_t sample_rate)
{
    shutdown();
    model_ = model;
    sample_rate_ = sample_rate;
    chip_ = model == Model::YMF262 ? ymf262_init(clock, sample_rate)
                                   : ym3812_init(clock, sample_rate);
    if (!chip_) return false;
    reset();
    return true;
}

void LibvgmOpl::reset()
{
    address_latch_.fill(0);
    for (auto& bank : registers_) bank.fill(0);
    if (!chip_) return;
    if (model_ == Model::YMF262) ymf262_reset_chip(chip_);
    else ym3812_reset_chip(chip_);
}

void LibvgmOpl::write(uint8_t port, uint8_t data)
{
    const uint8_t normalized = model_ == Model::YMF262 ? static_cast<uint8_t>(port & 3) : static_cast<uint8_t>(port & 1);
    const int bank = (normalized >> 1) & 1;
    if ((normalized & 1u) == 0) address_latch_[bank] = data;
    else registers_[bank][address_latch_[bank]] = data;
    if (!chip_) return;
    if (model_ == Model::YMF262) ymf262_write(chip_, static_cast<uint8_t>(port & 3), data);
    else ym3812_write(chip_, static_cast<uint8_t>(port & 1), data);
}

uint8_t LibvgmOpl::read(uint8_t port) const
{
    if (!chip_) return 0xff;
    return model_ == Model::YMF262 ? ymf262_read(chip_, static_cast<uint8_t>(port & 3))
                                   : ym3812_read(chip_, static_cast<uint8_t>(port & 1));
}

void LibvgmOpl::render_s16(int16_t* interleaved_stereo, int frames)
{
    if (!interleaved_stereo || frames <= 0) return;
    if (!chip_) {
        std::fill(interleaved_stereo, interleaved_stereo + frames * 2, int16_t{0});
        return;
    }
    left_.assign(static_cast<size_t>(frames), 0);
    right_.assign(static_cast<size_t>(frames), 0);
    DEV_SMPL* buffers[2] = { left_.data(), right_.data() };
    if (model_ == Model::YMF262) ymf262_update_one(chip_, static_cast<UINT32>(frames), buffers);
    else ym3812_update_one(chip_, static_cast<UINT32>(frames), buffers);
    for (int i = 0; i < frames; ++i) {
        interleaved_stereo[i * 2] = static_cast<int16_t>(std::clamp(left_[i], -32768, 32767));
        interleaved_stereo[i * 2 + 1] = static_cast<int16_t>(std::clamp(right_[i], -32768, 32767));
    }
}

} // namespace hoot
