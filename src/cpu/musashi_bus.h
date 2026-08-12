#pragma once

#include <cstdint>

namespace hoot {

// Musashi exposes one process-global set of memory callbacks.  Drivers using
// the core select their bus immediately before reset or execution.
class MusashiBus {
public:
    virtual ~MusashiBus() = default;

    virtual uint8_t read_memory_8(uint32_t address) = 0;
    virtual void write_memory_8(uint32_t address, uint8_t data) = 0;
    virtual int acknowledge_interrupt(int level) = 0;
    virtual void instruction_hook(uint32_t) {}

    virtual uint16_t read_memory_16(uint32_t address)
    {
        return static_cast<uint16_t>((read_memory_8(address) << 8)
                                     | read_memory_8(address + 1));
    }
    virtual uint32_t read_memory_32(uint32_t address)
    {
        return (static_cast<uint32_t>(read_memory_16(address)) << 16)
            | read_memory_16(address + 2);
    }
    virtual void write_memory_16(uint32_t address, uint16_t data)
    {
        write_memory_8(address, static_cast<uint8_t>(data >> 8));
        write_memory_8(address + 1, static_cast<uint8_t>(data));
    }
    virtual void write_memory_32(uint32_t address, uint32_t data)
    {
        write_memory_16(address, static_cast<uint16_t>(data >> 16));
        write_memory_16(address + 2, static_cast<uint16_t>(data));
    }
};

void musashi_set_active_bus(MusashiBus* bus);
MusashiBus* musashi_active_bus();
void musashi_instruction_hook_callback(unsigned int pc);

} // namespace hoot
