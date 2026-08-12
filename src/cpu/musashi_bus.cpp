#include "cpu/musashi_bus.h"

extern "C" {
#include "m68k.h"
}

namespace hoot {
namespace {
MusashiBus* active_bus = nullptr;
}

void musashi_set_active_bus(MusashiBus* bus) { active_bus = bus; }
MusashiBus* musashi_active_bus() { return active_bus; }
void musashi_instruction_hook_callback(unsigned int pc)
{
    if (active_bus != nullptr) active_bus->instruction_hook(pc);
}
} // namespace hoot

extern "C" {

signed int my_irqh_callback(signed int level)
{
    auto* bus = hoot::musashi_active_bus();
    return bus != nullptr ? bus->acknowledge_interrupt(level) : M68K_INT_ACK_AUTOVECTOR;
}

uint32_t m68k_read_memory_8(uint32_t address)
{
    auto* bus = hoot::musashi_active_bus();
    return bus != nullptr ? bus->read_memory_8(address) : 0xff;
}

uint32_t m68k_read_memory_16(uint32_t address)
{
    auto* bus = hoot::musashi_active_bus();
    return bus != nullptr ? bus->read_memory_16(address) : 0xffff;
}

uint32_t m68k_read_memory_32(uint32_t address)
{
    auto* bus = hoot::musashi_active_bus();
    return bus != nullptr ? bus->read_memory_32(address) : 0xffffffffu;
}

void m68k_write_memory_8(uint32_t address, uint32_t value)
{
    if (auto* bus = hoot::musashi_active_bus()) {
        bus->write_memory_8(address, static_cast<uint8_t>(value));
    }
}

void m68k_write_memory_16(uint32_t address, uint32_t value)
{
    if (auto* bus = hoot::musashi_active_bus()) {
        bus->write_memory_16(address, static_cast<uint16_t>(value));
    }
}

void m68k_write_memory_32(uint32_t address, uint32_t value)
{
    if (auto* bus = hoot::musashi_active_bus()) {
        bus->write_memory_32(address, value);
    }
}

void m68k_write_memory_32_pd(uint32_t address, uint32_t value)
{
    if (auto* bus = hoot::musashi_active_bus()) {
        bus->write_memory_16(address + 2, static_cast<uint16_t>(value >> 16));
        bus->write_memory_16(address, static_cast<uint16_t>(value));
    }
}

uint32_t m68k_read_immediate_16(uint32_t address) { return m68k_read_memory_16(address); }
uint32_t m68k_read_immediate_32(uint32_t address) { return m68k_read_memory_32(address); }
uint32_t m68k_read_pcrelative_8(uint32_t address) { return m68k_read_memory_8(address); }
uint32_t m68k_read_pcrelative_16(uint32_t address) { return m68k_read_memory_16(address); }
uint32_t m68k_read_pcrelative_32(uint32_t address) { return m68k_read_memory_32(address); }
uint32_t m68k_read_disassembler_8(uint32_t address) { return m68k_read_memory_8(address); }
uint32_t m68k_read_disassembler_16(uint32_t address) { return m68k_read_memory_16(address); }
uint32_t m68k_read_disassembler_32(uint32_t address) { return m68k_read_memory_32(address); }

}
