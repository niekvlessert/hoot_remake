#pragma once

#include <cstdint>
#include <functional>

extern "C" {
#include "kmz80.h"
}

namespace hoot {

class Kmz80Cpu {
public:
    using ReadCallback = std::function<uint8_t(uint16_t)>;
    using WriteCallback = std::function<void(uint16_t, uint8_t)>;

    Kmz80Cpu();

    void set_memory_callbacks(ReadCallback read, WriteCallback write);
    void set_io_callbacks(ReadCallback read, WriteCallback write);
    void reset(uint16_t pc = 0x0000);
    uint32_t execute(uint32_t cycles);
    void raise_irq(uint8_t bit = 0);
    void lower_irq(uint8_t bit = 0);
    void set_interrupt_bus(uint8_t value);
    void set_auto_irq_clear(bool enabled);
    uint16_t pc() const;
    void set_pc(uint16_t pc);

private:
    static Uint32 read_memory(void* user, Uint32 address);
    static void write_memory(void* user, Uint32 address, Uint32 data);
    static Uint32 read_io(void* user, Uint32 address);
    static void write_io(void* user, Uint32 address, Uint32 data);
    static Uint32 read_bus(void* user, Uint32 mode);

    KMZ80_CONTEXT context_{};
    uint8_t interrupt_bus_ = 0xff;
    ReadCallback read_memory_;
    WriteCallback write_memory_;
    ReadCallback read_io_;
    WriteCallback write_io_;
};

} // namespace hoot
