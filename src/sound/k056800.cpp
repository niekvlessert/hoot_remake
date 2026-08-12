// SPDX-License-Identifier: BSD-3-Clause
// Based on MAME src/devices/sound/k056800.cpp. Copyright Ville Linde.
#include "sound/k056800.h"

#include <cstddef>

namespace hoot {

void K056800::reset()
{
    interrupt_pending_ = false;
    interrupt_enabled_ = false;
    host_to_sound_.fill(0);
    sound_to_host_.fill(0);
}

uint8_t K056800::host_read(uint8_t offset) const
{
    const size_t reg = offset & 7;
    return reg < sound_to_host_.size() ? sound_to_host_[reg] : 0;
}

void K056800::host_write(uint8_t offset, uint8_t data)
{
    const size_t reg = offset & 7;
    if (reg < host_to_sound_.size()) host_to_sound_[reg] = data;
    else if (reg == 7 && interrupt_enabled_) interrupt_pending_ = true;
}

uint8_t K056800::sound_read(uint8_t offset) const
{
    const size_t reg = offset & 7;
    return reg < host_to_sound_.size() ? host_to_sound_[reg] : 0;
}

void K056800::sound_write(uint8_t offset, uint8_t data)
{
    const size_t reg = offset & 7;
    if (reg < sound_to_host_.size()) {
        sound_to_host_[reg] = data;
    } else if (reg == 4) {
        interrupt_enabled_ = (data & 1) != 0;
        if (!interrupt_enabled_) interrupt_pending_ = false;
    }
}

} // namespace hoot
