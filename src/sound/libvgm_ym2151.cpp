#include "sound/libvgm_ym2151.h"

#include <algorithm>
#include <cstring>

extern "C" {
#include "EmuStructs.h"
#include "ym2151.h"
void ym2151_set_irq_handler(void* chip, void (*handler)(void* param, uint8_t irq));
}

namespace hoot {
namespace {

using WriteA8D8 = void (*)(void*, uint8_t, uint8_t);
using ReadA8D8 = uint8_t (*)(void*, uint8_t);

const DEVDEF_RWFUNC* find_rw_func(const DEV_DEF* def, uint8_t func_type, uint8_t rw_type)
{
    if (def == nullptr || def->rwFuncs == nullptr) {
        return nullptr;
    }
    for (const auto* func = def->rwFuncs; func->funcPtr != nullptr; ++func) {
        if (func->funcType == func_type && func->rwType == rw_type) {
            return func;
        }
    }
    return nullptr;
}

} // namespace

LibvgmYm2151::LibvgmYm2151() = default;

LibvgmYm2151::~LibvgmYm2151()
{
    if (chip_ != nullptr) {
        static_cast<const DEV_DEF*>(dev_def_)->Stop(chip_);
        chip_ = nullptr;
    }
}

bool LibvgmYm2151::initialize(uint32_t clock, uint32_t sample_rate)
{
    if (chip_ != nullptr) {
        static_cast<const DEV_DEF*>(dev_def_)->Stop(chip_);
        chip_ = nullptr;
    }

    sample_rate_ = sample_rate;
    DEV_GEN_CFG config{};
    config.clock = clock;
    config.srMode = DEVRI_SRMODE_CUSTOM;
    config.smplRate = sample_rate;

    DEV_INFO info{};
    if (devDef_YM2151_MAME.Start(&config, &info) != 0 || info.dataPtr == nullptr) {
        return false;
    }
    chip_ = info.dataPtr;
    dev_def_ = info.devDef;
    reset();
    return true;
}

void LibvgmYm2151::reset()
{
    if (chip_ != nullptr) {
        static_cast<const DEV_DEF*>(dev_def_)->Reset(chip_);
    }
}

void LibvgmYm2151::set_irq_handler(void (*handler)(void* param, uint8_t irq), void* param)
{
    if (chip_ != nullptr) {
        ym2151_set_irq_handler(chip_, handler);
        (void)param;
    }
}

void LibvgmYm2151::write(uint8_t port, uint8_t data)
{
    if (chip_ == nullptr) {
        return;
    }
    const auto* func = find_rw_func(static_cast<const DEV_DEF*>(dev_def_),
                                    RWF_REGISTER | RWF_WRITE,
                                    DEVRW_A8D8);
    if (func != nullptr) {
        reinterpret_cast<WriteA8D8>(func->funcPtr)(chip_, static_cast<uint8_t>(port & 1), data);
    }
}

uint8_t LibvgmYm2151::read(uint8_t port)
{
    if (chip_ == nullptr) {
        return 0xff;
    }
    const auto* func = find_rw_func(static_cast<const DEV_DEF*>(dev_def_),
                                    RWF_REGISTER | RWF_READ,
                                    DEVRW_A8D8);
    if (func == nullptr) {
        return 0xff;
    }
    return reinterpret_cast<ReadA8D8>(func->funcPtr)(chip_, static_cast<uint8_t>(port & 1));
}

void LibvgmYm2151::render_s16(int16_t* interleaved_stereo, int frames)
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
    static_cast<const DEV_DEF*>(dev_def_)->Update(chip_, static_cast<uint32_t>(frames), buffers);

    for (int i = 0; i < frames; ++i) {
        const auto l = std::clamp(left_[i], -32768, 32767);
        const auto r = std::clamp(right_[i], -32768, 32767);
        interleaved_stereo[i * 2] = static_cast<int16_t>(l);
        interleaved_stereo[i * 2 + 1] = static_cast<int16_t>(r);
    }
}

} // namespace hoot
