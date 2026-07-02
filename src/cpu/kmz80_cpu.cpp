#include "cpu/kmz80_cpu.h"

#include <cstring>

namespace hoot {

Kmz80Cpu::Kmz80Cpu()
{
    std::memset(&context_, 0, sizeof(context_));
    context_.user = this;
    context_.memread = &Kmz80Cpu::read_memory;
    context_.memwrite = &Kmz80Cpu::write_memory;
    context_.ioread = &Kmz80Cpu::read_io;
    context_.iowrite = &Kmz80Cpu::write_io;
    context_.busread = &Kmz80Cpu::read_bus;
    kmz80_reset(&context_);
}

void Kmz80Cpu::set_memory_callbacks(ReadCallback read, WriteCallback write)
{
    read_memory_ = std::move(read);
    write_memory_ = std::move(write);
}

void Kmz80Cpu::set_io_callbacks(ReadCallback read, WriteCallback write)
{
    read_io_ = std::move(read);
    write_io_ = std::move(write);
}

void Kmz80Cpu::reset(uint16_t pc)
{
    kmz80_reset(&context_);
    set_pc(pc);
}

uint32_t Kmz80Cpu::execute(uint32_t cycles)
{
    return kmz80_exec(&context_, cycles);
}

void Kmz80Cpu::raise_irq(uint8_t bit)
{
    context_.regs8[REGID_INTREQ] |= static_cast<uint8_t>(1u << bit);
}

void Kmz80Cpu::lower_irq(uint8_t bit)
{
    context_.regs8[REGID_INTREQ] &= static_cast<uint8_t>(~(1u << bit));
}

void Kmz80Cpu::set_interrupt_bus(uint8_t value)
{
    interrupt_bus_ = value;
}

void Kmz80Cpu::set_auto_irq_clear(bool enabled)
{
    constexpr uint32_t kAutoIrqClear = 1u << 1;
    if (enabled) {
        context_.exflag |= kAutoIrqClear;
    } else {
        context_.exflag &= ~kAutoIrqClear;
    }
}

uint16_t Kmz80Cpu::pc() const
{
    return static_cast<uint16_t>(context_.pc & 0xffff);
}

void Kmz80Cpu::set_pc(uint16_t pc)
{
    context_.pc = pc;
}

Uint32 Kmz80Cpu::read_memory(void* user, Uint32 address)
{
    auto* cpu = static_cast<Kmz80Cpu*>(user);
    if (cpu->read_memory_) {
        return cpu->read_memory_(static_cast<uint16_t>(address));
    }
    return 0xff;
}

void Kmz80Cpu::write_memory(void* user, Uint32 address, Uint32 data)
{
    auto* cpu = static_cast<Kmz80Cpu*>(user);
    if (cpu->write_memory_) {
        cpu->write_memory_(static_cast<uint16_t>(address), static_cast<uint8_t>(data));
    }
}

Uint32 Kmz80Cpu::read_io(void* user, Uint32 address)
{
    auto* cpu = static_cast<Kmz80Cpu*>(user);
    if (cpu->read_io_) {
        return cpu->read_io_(static_cast<uint16_t>(address));
    }
    return 0xff;
}

void Kmz80Cpu::write_io(void* user, Uint32 address, Uint32 data)
{
    auto* cpu = static_cast<Kmz80Cpu*>(user);
    if (cpu->write_io_) {
        cpu->write_io_(static_cast<uint16_t>(address), static_cast<uint8_t>(data));
    }
}

Uint32 Kmz80Cpu::read_bus(void* user, Uint32)
{
    auto* cpu = static_cast<Kmz80Cpu*>(user);
    return cpu->interrupt_bus_;
}

} // namespace hoot
