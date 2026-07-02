#include "cpu/v30_cpu.h"

#include <cstring>

namespace hoot {

V30Cpu::V30Cpu()
{
    std::memset(memory_, 0, sizeof(memory_));
}

void V30Cpu::set_read_memory_callback(ReadMemoryCallback cb)
{
    read_memory_ = std::move(cb);
}

void V30Cpu::set_write_memory_callback(WriteMemoryCallback cb)
{
    write_memory_ = std::move(cb);
}

void V30Cpu::set_read_io_callback(ReadIoCallback cb)
{
    read_io_ = std::move(cb);
}

void V30Cpu::set_write_io_callback(WriteIoCallback cb)
{
    write_io_ = std::move(cb);
}

void V30Cpu::set_interrupt_callback(InterruptCallback cb)
{
    interrupt_ = std::move(cb);
}

void V30Cpu::reset(uint16_t pc)
{
    pc_ = pc;
    sp_ = 0;
    cs_ = 0;
    ds_ = 0;
    es_ = 0;
    ss_ = 0;
    ax_ = 0;
    bx_ = 0;
    cx_ = 0;
    dx_ = 0;
    si_ = 0;
    di_ = 0;
    bp_ = 0;
    flags_ = 0;
    irq_lines_ = 0;
    pending_irq_ = 0;
    std::memset(memory_, 0, sizeof(memory_));
}

uint32_t V30Cpu::execute(uint32_t cycles)
{
    (void)cycles;
    return 0;
}

uint16_t V30Cpu::pc() const { return pc_; }
void V30Cpu::set_pc(uint16_t pc) { pc_ = pc; }

uint16_t V30Cpu::sp() const { return sp_; }
void V30Cpu::set_sp(uint16_t sp) { sp_ = sp; }

uint16_t V30Cpu::cs() const { return cs_; }
void V30Cpu::set_cs(uint16_t cs) { cs_ = cs; }

uint16_t V30Cpu::ds() const { return ds_; }
void V30Cpu::set_ds(uint16_t ds) { ds_ = ds; }

uint16_t V30Cpu::es() const { return es_; }
void V30Cpu::set_es(uint16_t es) { es_ = es; }

uint16_t V30Cpu::ss() const { return ss_; }
void V30Cpu::set_ss(uint16_t ss) { ss_ = ss; }

uint8_t V30Cpu::al() const { return static_cast<uint8_t>(ax_ & 0xff); }
void V30Cpu::set_al(uint8_t v) { ax_ = (ax_ & 0xff00) | v; }
uint8_t V30Cpu::ah() const { return static_cast<uint8_t>((ax_ >> 8) & 0xff); }
void V30Cpu::set_ah(uint8_t v) { ax_ = (ax_ & 0x00ff) | (static_cast<uint16_t>(v) << 8); }
uint16_t V30Cpu::ax() const { return ax_; }
void V30Cpu::set_ax(uint16_t v) { ax_ = v; }

uint8_t V30Cpu::bl() const { return static_cast<uint8_t>(bx_ & 0xff); }
void V30Cpu::set_bl(uint8_t v) { bx_ = (bx_ & 0xff00) | v; }
uint8_t V30Cpu::bh() const { return static_cast<uint8_t>((bx_ >> 8) & 0xff); }
void V30Cpu::set_bh(uint8_t v) { bx_ = (bx_ & 0x00ff) | (static_cast<uint16_t>(v) << 8); }
uint16_t V30Cpu::bx() const { return bx_; }
void V30Cpu::set_bx(uint16_t v) { bx_ = v; }

uint8_t V30Cpu::cl() const { return static_cast<uint8_t>(cx_ & 0xff); }
void V30Cpu::set_cl(uint8_t v) { cx_ = (cx_ & 0xff00) | v; }
uint8_t V30Cpu::ch() const { return static_cast<uint8_t>((cx_ >> 8) & 0xff); }
void V30Cpu::set_ch(uint8_t v) { cx_ = (cx_ & 0x00ff) | (static_cast<uint16_t>(v) << 8); }
uint16_t V30Cpu::cx() const { return cx_; }
void V30Cpu::set_cx(uint16_t v) { cx_ = v; }

uint8_t V30Cpu::dl() const { return static_cast<uint8_t>(dx_ & 0xff); }
void V30Cpu::set_dl(uint8_t v) { dx_ = (dx_ & 0xff00) | v; }
uint8_t V30Cpu::dh() const { return static_cast<uint8_t>((dx_ >> 8) & 0xff); }
void V30Cpu::set_dh(uint8_t v) { dx_ = (dx_ & 0x00ff) | (static_cast<uint16_t>(v) << 8); }
uint16_t V30Cpu::dx() const { return dx_; }
void V30Cpu::set_dx(uint16_t v) { dx_ = v; }

uint16_t V30Cpu::si() const { return si_; }
void V30Cpu::set_si(uint16_t v) { si_ = v; }

uint16_t V30Cpu::di() const { return di_; }
void V30Cpu::set_di(uint16_t v) { di_ = v; }

uint16_t V30Cpu::bp() const { return bp_; }
void V30Cpu::set_bp(uint16_t v) { bp_ = v; }

uint32_t V30Cpu::flags() const { return flags_; }
void V30Cpu::set_flags(uint32_t f) { flags_ = f; }

bool V30Cpu::carry() const { return (flags_ & 0x0001) != 0; }
bool V30Cpu::parity() const { return (flags_ & 0x0004) != 0; }
bool V30Cpu::adjust() const { return (flags_ & 0x0010) != 0; }
bool V30Cpu::zero() const { return (flags_ & 0x0040) != 0; }
bool V30Cpu::sign() const { return (flags_ & 0x0080) != 0; }
bool V30Cpu::trace() const { return (flags_ & 0x0100) != 0; }
bool V30Cpu::interrupt() const { return (flags_ & 0x0200) != 0; }
bool V30Cpu::direction() const { return (flags_ & 0x0400) != 0; }
bool V30Cpu::overflow() const { return (flags_ & 0x0800) != 0; }

void V30Cpu::set_carry(bool v)
{
    if (v) flags_ |= 0x0001; else flags_ &= ~0x0001;
}
void V30Cpu::set_zero(bool v)
{
    if (v) flags_ |= 0x0040; else flags_ &= ~0x0040;
}
void V30Cpu::set_sign(bool v)
{
    if (v) flags_ |= 0x0080; else flags_ &= ~0x0080;
}
void V30Cpu::set_overflow(bool v)
{
    if (v) flags_ |= 0x0800; else flags_ &= ~0x0800;
}
void V30Cpu::set_interrupt(bool v)
{
    if (v) flags_ |= 0x0200; else flags_ &= ~0x0200;
}
void V30Cpu::set_direction(bool v)
{
    if (v) flags_ |= 0x0400; else flags_ &= ~0x0400;
}

void V30Cpu::raise_irq(uint8_t line)
{
    irq_lines_ |= (1u << (line & 7));
}

void V30Cpu::lower_irq(uint8_t line)
{
    irq_lines_ &= ~(1u << (line & 7));
}

bool V30Cpu::irq_pending() const
{
    return irq_lines_ != 0 && interrupt();
}

uint8_t V30Cpu::pending_irq() const
{
    for (uint8_t i = 0; i < 8; ++i) {
        if (irq_lines_ & (1u << i)) {
            return i;
        }
    }
    return 0;
}

void V30Cpu::trigger_interrupt(uint8_t int_num)
{
    if (interrupt_) {
        interrupt_(int_num);
    }
}

} // namespace hoot