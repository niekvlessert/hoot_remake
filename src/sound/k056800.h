// SPDX-License-Identifier: BSD-3-Clause
// Standalone adaptation of MAME's Konami K056800 device by Ville Linde.
#pragma once

#include <array>
#include <cstdint>

namespace hoot {

class K056800 {
public:
    void reset();
    uint8_t host_read(uint8_t offset) const;
    void host_write(uint8_t offset, uint8_t data);
    uint8_t sound_read(uint8_t offset) const;
    void sound_write(uint8_t offset, uint8_t data);
    bool interrupt_enabled() const { return interrupt_enabled_; }
    bool interrupt_asserted() const { return interrupt_pending_ && interrupt_enabled_; }

private:
    bool interrupt_pending_ = false;
    bool interrupt_enabled_ = false;
    std::array<uint8_t, 4> host_to_sound_{};
    std::array<uint8_t, 2> sound_to_host_{};
};

} // namespace hoot
