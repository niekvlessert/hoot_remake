#include "cpu/x86_cpu.h"

#include <algorithm>
#include <cstring>
#include <utility>

namespace hoot {

namespace {

uint16_t sign_extend8(uint8_t value)
{
    return static_cast<uint16_t>(static_cast<int16_t>(static_cast<int8_t>(value)));
}

uint16_t segment_for_prefix(uint8_t opcode, uint16_t es, uint16_t cs, uint16_t ss, uint16_t ds)
{
    switch (opcode) {
    case 0x26:
        return es;
    case 0x2E:
        return cs;
    case 0x36:
        return ss;
    default:
        return ds;
    }
}

} // namespace

X86Cpu::X86Cpu()
{
    memory_.resize(1024 * 1024, 0);
    reset();
}

void X86Cpu::set_read_memory_callback(ReadMemoryCallback cb)
{
    read_memory_ = std::move(cb);
}

void X86Cpu::set_write_memory_callback(WriteMemoryCallback cb)
{
    write_memory_ = std::move(cb);
}

void X86Cpu::set_io_read_callback(IoReadCallback cb)
{
    read_io_ = std::move(cb);
}

void X86Cpu::set_io_write_callback(IoWriteCallback cb)
{
    write_io_ = std::move(cb);
}

void X86Cpu::set_interrupt_callback(InterruptCallback cb)
{
    interrupt_ = std::move(cb);
}

void X86Cpu::reset()
{
    cs_ = 0;
    ip_ = 0;
    sp_ = 0;
    ss_ = 0;
    ds_ = 0;
    es_ = 0;
    ax_ = 0;
    bx_ = 0;
    cx_ = 0;
    dx_ = 0;
    si_ = 0;
    di_ = 0;
    bp_ = 0;
    flags_ = 0xF000;
    halted_ = false;
    last_unsupported_opcode_ = 0;
    last_unsupported_cs_ = 0;
    last_unsupported_ip_ = 0;
    unsupported_count_ = 0;
    segment_override_active_ = false;
    segment_override_ = 0;
    repeat_prefix_ = RepeatPrefix::None;
}

uint8_t X86Cpu::fetch_byte()
{
    const uint32_t addr = get_linear(cs_, ip_);
    const uint8_t val = read_byte(addr);
    ip_++;
    return val;
}

uint16_t X86Cpu::fetch_word()
{
    const uint32_t addr = get_linear(cs_, ip_);
    const uint16_t val = read_word(addr);
    ip_ += 2;
    return val;
}

uint8_t X86Cpu::read_byte(uint32_t addr)
{
    addr &= 0xFFFFF;
    if (read_memory_) {
        return read_memory_(addr);
    }
    if (addr < memory_.size()) {
        return memory_[addr];
    }
    return 0xFF;
}

uint16_t X86Cpu::read_word(uint32_t addr)
{
    const uint16_t lo = read_byte(addr);
    const uint16_t hi = read_byte(addr + 1);
    return static_cast<uint16_t>(lo | (hi << 8));
}

void X86Cpu::write_byte(uint32_t addr, uint8_t val)
{
    addr &= 0xFFFFF;
    if (write_memory_) {
        write_memory_(addr, val);
        return;
    }
    if (addr < memory_.size()) {
        memory_[addr] = val;
    }
}

void X86Cpu::write_word(uint32_t addr, uint16_t val)
{
    write_byte(addr, static_cast<uint8_t>(val & 0xFF));
    write_byte(addr + 1, static_cast<uint8_t>((val >> 8) & 0xFF));
}

void X86Cpu::push(uint16_t val)
{
    sp_ -= 2;
    write_word(get_linear(ss_, sp_), val);
}

uint16_t X86Cpu::pop()
{
    const uint16_t val = read_word(get_linear(ss_, sp_));
    sp_ += 2;
    return val;
}

void X86Cpu::set_memory(uint32_t addr, const uint8_t* data, uint32_t size)
{
    if (addr >= memory_.size()) {
        return;
    }
    size = std::min<uint32_t>(size, static_cast<uint32_t>(memory_.size() - addr));
    if (data && size > 0) {
        std::memcpy(memory_.data() + addr, data, size);
    }
}

uint16_t& X86Cpu::reg16(uint8_t index)
{
    switch (index & 7) {
    case 0: return ax_;
    case 1: return cx_;
    case 2: return dx_;
    case 3: return bx_;
    case 4: return sp_;
    case 5: return bp_;
    case 6: return si_;
    default: return di_;
    }
}

uint16_t X86Cpu::reg16(uint8_t index) const
{
    switch (index & 7) {
    case 0: return ax_;
    case 1: return cx_;
    case 2: return dx_;
    case 3: return bx_;
    case 4: return sp_;
    case 5: return bp_;
    case 6: return si_;
    default: return di_;
    }
}

uint8_t X86Cpu::reg8(uint8_t index) const
{
    switch (index & 7) {
    case 0: return get_al();
    case 1: return get_cl();
    case 2: return get_dl();
    case 3: return get_bl();
    case 4: return get_ah();
    case 5: return get_ch();
    case 6: return get_dh();
    default: return get_bh();
    }
}

void X86Cpu::set_reg8(uint8_t index, uint8_t value)
{
    switch (index & 7) {
    case 0: set_al(value); break;
    case 1: set_cl(value); break;
    case 2: set_dl(value); break;
    case 3: set_bl(value); break;
    case 4: set_ah(value); break;
    case 5: set_ch(value); break;
    case 6: set_dh(value); break;
    default: set_bh(value); break;
    }
}

uint16_t X86Cpu::seg_reg(uint8_t index) const
{
    switch (index & 3) {
    case 0: return es_;
    case 1: return cs_;
    case 2: return ss_;
    default: return ds_;
    }
}

void X86Cpu::set_seg_reg(uint8_t index, uint16_t value)
{
    switch (index & 3) {
    case 0: es_ = value; break;
    case 1: cs_ = value; break;
    case 2: ss_ = value; break;
    default: ds_ = value; break;
    }
}

X86Cpu::DecodedRm X86Cpu::decode_rm(uint8_t modrm)
{
    DecodedRm rm;
    const uint8_t mod = (modrm >> 6) & 3;
    const uint8_t index = modrm & 7;

    if (mod == 3) {
        rm.is_register = true;
        rm.reg = index;
        return rm;
    }

    uint16_t base = 0;
    uint16_t segment = ds_;
    switch (index) {
    case 0: base = bx_ + si_; break;
    case 1: base = bx_ + di_; break;
    case 2: base = bp_ + si_; segment = ss_; break;
    case 3: base = bp_ + di_; segment = ss_; break;
    case 4: base = si_; break;
    case 5: base = di_; break;
    case 6:
        if (mod == 0) {
            base = fetch_word();
        } else {
            base = bp_;
            segment = ss_;
        }
        break;
    case 7: base = bx_; break;
    }

    if (mod == 1) {
        base = static_cast<uint16_t>(base + sign_extend8(fetch_byte()));
    } else if (mod == 2) {
        base = static_cast<uint16_t>(base + fetch_word());
    }

    rm.effective_offset = base;
    if (segment_override_active_) {
        segment = segment_override_;
    }
    rm.address = get_linear(segment, base);
    return rm;
}

uint8_t X86Cpu::read_rm8(const DecodedRm& rm)
{
    return rm.is_register ? reg8(rm.reg) : read_byte(rm.address);
}

uint16_t X86Cpu::read_rm16(const DecodedRm& rm)
{
    return rm.is_register ? reg16(rm.reg) : read_word(rm.address);
}

void X86Cpu::write_rm8(const DecodedRm& rm, uint8_t value)
{
    if (rm.is_register) {
        set_reg8(rm.reg, value);
    } else {
        write_byte(rm.address, value);
    }
}

void X86Cpu::write_rm16(const DecodedRm& rm, uint16_t value)
{
    if (rm.is_register) {
        reg16(rm.reg) = value;
    } else {
        write_word(rm.address, value);
    }
}

uint16_t X86Cpu::effective_data_segment() const
{
    return segment_override_active_ ? segment_override_ : ds_;
}

uint16_t X86Cpu::consume_string_count()
{
    if (repeat_prefix_ == RepeatPrefix::None) {
        return 1;
    }
    const uint16_t count = cx_;
    cx_ = 0;
    return count;
}

int X86Cpu::string_step() const
{
    return get_direction() ? -1 : 1;
}

void X86Cpu::set_parity_from_byte(uint8_t result)
{
    bool parity = true;
    for (int i = 0; i < 8; ++i) {
        parity = parity != ((result & (1u << i)) != 0);
    }
    if (parity) {
        flags_ |= 0x0004;
    } else {
        flags_ &= ~0x0004;
    }
}

void X86Cpu::update_flags_8(uint8_t result)
{
    set_zero(result == 0);
    set_sign((result & 0x80) != 0);
    set_parity_from_byte(result);
    set_carry(false);
    set_overflow(false);
}

void X86Cpu::update_flags_16(uint16_t result)
{
    set_zero(result == 0);
    set_sign((result & 0x8000) != 0);
    set_parity_from_byte(static_cast<uint8_t>(result));
    set_carry(false);
    set_overflow(false);
}

void X86Cpu::update_logic_flags_8(uint8_t result)
{
    update_flags_8(result);
}

void X86Cpu::update_logic_flags_16(uint16_t result)
{
    update_flags_16(result);
}

void X86Cpu::update_flags_add_8(uint8_t a, uint8_t b, uint16_t result)
{
    const uint8_t r = static_cast<uint8_t>(result);
    set_zero(r == 0);
    set_sign((r & 0x80) != 0);
    set_parity_from_byte(r);
    set_carry(result > 0xFF);
    set_overflow(((~(a ^ b) & (a ^ r)) & 0x80) != 0);
}

void X86Cpu::update_flags_add_16(uint16_t a, uint16_t b, uint32_t result)
{
    const uint16_t r = static_cast<uint16_t>(result);
    set_zero(r == 0);
    set_sign((r & 0x8000) != 0);
    set_parity_from_byte(static_cast<uint8_t>(r));
    set_carry(result > 0xFFFF);
    set_overflow(((~(a ^ b) & (a ^ r)) & 0x8000) != 0);
}

void X86Cpu::update_flags_sub_8(uint8_t a, uint8_t b, uint16_t result)
{
    const uint8_t r = static_cast<uint8_t>(result);
    set_zero(r == 0);
    set_sign((r & 0x80) != 0);
    set_parity_from_byte(r);
    set_carry(a < b);
    set_overflow((((a ^ b) & (a ^ r)) & 0x80) != 0);
}

void X86Cpu::update_flags_sub_16(uint16_t a, uint16_t b, uint32_t result)
{
    const uint16_t r = static_cast<uint16_t>(result);
    set_zero(r == 0);
    set_sign((r & 0x8000) != 0);
    set_parity_from_byte(static_cast<uint8_t>(r));
    set_carry(a < b);
    set_overflow((((a ^ b) & (a ^ r)) & 0x8000) != 0);
}

uint8_t X86Cpu::shift8(uint8_t value, uint8_t op, uint8_t count)
{
    count &= 0x1f;
    if (count == 0) {
        return value;
    }
    uint8_t result = value;
    if (op <= 3) {
        const uint8_t rotate_count = static_cast<uint8_t>(count & 0x07);
        if (rotate_count == 0) {
            return result;
        }
        for (uint8_t i = 0; i < rotate_count; ++i) {
            switch (op) {
            case 0: { // ROL
                const bool high = (result & 0x80) != 0;
                result = static_cast<uint8_t>((result << 1) | (high ? 1 : 0));
                set_carry(high);
                break;
            }
            case 1: { // ROR
                const bool low = (result & 0x01) != 0;
                result = static_cast<uint8_t>((result >> 1) | (low ? 0x80 : 0));
                set_carry(low);
                break;
            }
            case 2: { // RCL
                const bool old_carry = get_carry();
                const bool high = (result & 0x80) != 0;
                result = static_cast<uint8_t>((result << 1) | (old_carry ? 1 : 0));
                set_carry(high);
                break;
            }
            case 3: { // RCR
                const bool old_carry = get_carry();
                const bool low = (result & 0x01) != 0;
                result = static_cast<uint8_t>((result >> 1) | (old_carry ? 0x80 : 0));
                set_carry(low);
                break;
            }
            }
        }
        if (count == 1) {
            if (op == 0 || op == 2) {
                set_overflow(((result & 0x80) != 0) != get_carry());
            } else if (op == 1) {
                set_overflow(((result ^ (result << 1)) & 0x80) != 0);
            }
        }
        return result;
    }
    for (uint8_t i = 0; i < count; ++i) {
        switch (op) {
        case 4: // SHL/SAL
            set_carry((result & 0x80) != 0);
            result = static_cast<uint8_t>(result << 1);
            break;
        case 5: // SHR
            set_carry((result & 0x01) != 0);
            result = static_cast<uint8_t>(result >> 1);
            break;
        case 7: // SAR
            set_carry((result & 0x01) != 0);
            result = static_cast<uint8_t>((result >> 1) | (result & 0x80));
            break;
        default:
            return result;
        }
    }
    set_zero(result == 0);
    set_sign((result & 0x80) != 0);
    set_parity_from_byte(result);
    if (count == 1) {
        if (op == 4) {
            set_overflow(((result & 0x80) != 0) != get_carry());
        } else if (op == 5) {
            set_overflow((value & 0x80) != 0);
        } else if (op == 7) {
            set_overflow(false);
        }
    }
    return result;
}

uint16_t X86Cpu::shift16(uint16_t value, uint8_t op, uint8_t count)
{
    count &= 0x1f;
    if (count == 0) {
        return value;
    }
    uint16_t result = value;
    if (op <= 3) {
        const uint8_t rotate_count = static_cast<uint8_t>(count & 0x0f);
        if (rotate_count == 0) {
            return result;
        }
        for (uint8_t i = 0; i < rotate_count; ++i) {
            switch (op) {
            case 0: { // ROL
                const bool high = (result & 0x8000) != 0;
                result = static_cast<uint16_t>((result << 1) | (high ? 1 : 0));
                set_carry(high);
                break;
            }
            case 1: { // ROR
                const bool low = (result & 0x0001) != 0;
                result = static_cast<uint16_t>((result >> 1) | (low ? 0x8000 : 0));
                set_carry(low);
                break;
            }
            case 2: { // RCL
                const bool old_carry = get_carry();
                const bool high = (result & 0x8000) != 0;
                result = static_cast<uint16_t>((result << 1) | (old_carry ? 1 : 0));
                set_carry(high);
                break;
            }
            case 3: { // RCR
                const bool old_carry = get_carry();
                const bool low = (result & 0x0001) != 0;
                result = static_cast<uint16_t>((result >> 1) | (old_carry ? 0x8000 : 0));
                set_carry(low);
                break;
            }
            }
        }
        if (count == 1) {
            if (op == 0 || op == 2) {
                set_overflow(((result & 0x8000) != 0) != get_carry());
            } else if (op == 1) {
                set_overflow(((result ^ (result << 1)) & 0x8000) != 0);
            }
        }
        return result;
    }
    for (uint8_t i = 0; i < count; ++i) {
        switch (op) {
        case 4: // SHL/SAL
            set_carry((result & 0x8000) != 0);
            result = static_cast<uint16_t>(result << 1);
            break;
        case 5: // SHR
            set_carry((result & 0x0001) != 0);
            result = static_cast<uint16_t>(result >> 1);
            break;
        case 7: // SAR
            set_carry((result & 0x0001) != 0);
            result = static_cast<uint16_t>((result >> 1) | (result & 0x8000));
            break;
        default:
            return result;
        }
    }
    set_zero(result == 0);
    set_sign((result & 0x8000) != 0);
    set_parity_from_byte(static_cast<uint8_t>(result));
    if (count == 1) {
        if (op == 4) {
            set_overflow(((result & 0x8000) != 0) != get_carry());
        } else if (op == 5) {
            set_overflow((value & 0x8000) != 0);
        } else if (op == 7) {
            set_overflow(false);
        }
    }
    return result;
}

bool X86Cpu::check_jump(bool cond)
{
    return cond;
}

void X86Cpu::jump_short_if(bool condition)
{
    const int8_t disp = static_cast<int8_t>(fetch_byte());
    if (condition) {
        ip_ = static_cast<uint16_t>(ip_ + disp);
    }
}

void X86Cpu::do_int(uint8_t num)
{
    if (interrupt_) {
        interrupt_(num);
    }

    const uint32_t ivt_addr = static_cast<uint32_t>(num) * 4;
    const uint16_t target_ip = read_word(ivt_addr);
    const uint16_t target_cs = read_word(ivt_addr + 2);

    push(flags_);
    push(cs_);
    push(ip_);
    set_interrupt_flag(false);
    cs_ = target_cs;
    ip_ = target_ip;
}

int X86Cpu::execute(int cycles)
{
    int executed = 0;
    halted_ = false;

    while (executed < cycles && !halted_) {
        execute_one();
        executed++;
    }

    return executed;
}

void X86Cpu::execute_one()
{
    const uint16_t opcode_ip = ip_;
    const uint8_t opcode = fetch_byte();

    switch (opcode) {
    case 0x06:
        push(es_);
        break;
    case 0x07:
        es_ = pop();
        break;
    case 0x00: case 0x02: { // ADD r/m8,r8 | ADD r8,r/m8
        const uint8_t modrm = fetch_byte();
        const uint8_t reg = (modrm >> 3) & 7;
        const auto rm = decode_rm(modrm);
        const uint8_t src = opcode == 0x00 ? reg8(reg) : read_rm8(rm);
        const uint8_t dst = opcode == 0x00 ? read_rm8(rm) : reg8(reg);
        const uint16_t result = static_cast<uint16_t>(dst) + src;
        if (opcode == 0x00) write_rm8(rm, static_cast<uint8_t>(result));
        else set_reg8(reg, static_cast<uint8_t>(result));
        update_flags_add_8(dst, src, result);
        break;
    }
    case 0x01: case 0x03: { // ADD r/m16,r16 | ADD r16,r/m16
        const uint8_t modrm = fetch_byte();
        const uint8_t reg = (modrm >> 3) & 7;
        const auto rm = decode_rm(modrm);
        const uint16_t src = opcode == 0x01 ? reg16(reg) : read_rm16(rm);
        const uint16_t dst = opcode == 0x01 ? read_rm16(rm) : reg16(reg);
        const uint32_t result = static_cast<uint32_t>(dst) + src;
        if (opcode == 0x01) write_rm16(rm, static_cast<uint16_t>(result));
        else reg16(reg) = static_cast<uint16_t>(result);
        update_flags_add_16(dst, src, result);
        break;
    }
    case 0x04: {
        const uint8_t old = get_al();
        const uint8_t imm = fetch_byte();
        const uint16_t result = static_cast<uint16_t>(old) + imm;
        set_al(static_cast<uint8_t>(result));
        update_flags_add_8(old, imm, result);
        break;
    }
    case 0x05: {
        const uint16_t old = ax_;
        const uint16_t imm = fetch_word();
        const uint32_t result = static_cast<uint32_t>(old) + imm;
        ax_ = static_cast<uint16_t>(result);
        update_flags_add_16(old, imm, result);
        break;
    }
    case 0x08: case 0x0A: { // OR r/m8,r8 | OR r8,r/m8
        const uint8_t modrm = fetch_byte();
        const uint8_t reg = (modrm >> 3) & 7;
        const auto rm = decode_rm(modrm);
        const uint8_t src = opcode == 0x08 ? reg8(reg) : read_rm8(rm);
        const uint8_t dst = opcode == 0x08 ? read_rm8(rm) : reg8(reg);
        const uint8_t result = static_cast<uint8_t>(dst | src);
        if (opcode == 0x08) write_rm8(rm, result);
        else set_reg8(reg, result);
        update_logic_flags_8(result);
        break;
    }
    case 0x09: case 0x0B: { // OR r/m16,r16 | OR r16,r/m16
        const uint8_t modrm = fetch_byte();
        const uint8_t reg = (modrm >> 3) & 7;
        const auto rm = decode_rm(modrm);
        const uint16_t src = opcode == 0x09 ? reg16(reg) : read_rm16(rm);
        const uint16_t dst = opcode == 0x09 ? read_rm16(rm) : reg16(reg);
        const uint16_t result = static_cast<uint16_t>(dst | src);
        if (opcode == 0x09) write_rm16(rm, result);
        else reg16(reg) = result;
        update_logic_flags_16(result);
        break;
    }
    case 0x0C:
        set_al(static_cast<uint8_t>(get_al() | fetch_byte()));
        update_logic_flags_8(get_al());
        break;
    case 0x0D:
        ax_ = static_cast<uint16_t>(ax_ | fetch_word());
        update_logic_flags_16(ax_);
        break;
    case 0x0E:
        push(cs_);
        break;

    case 0x16:
        push(ss_);
        break;
    case 0x18: case 0x1A: case 0x10: case 0x12: { // SBB/ADC byte
        const uint8_t modrm = fetch_byte();
        const uint8_t reg = (modrm >> 3) & 7;
        const auto rm = decode_rm(modrm);
        const bool adc = opcode == 0x10 || opcode == 0x12;
        const bool rm_dest = opcode == 0x10 || opcode == 0x18;
        const uint8_t src = rm_dest ? reg8(reg) : read_rm8(rm);
        const uint8_t dst = rm_dest ? read_rm8(rm) : reg8(reg);
        const uint8_t carry = get_carry() ? 1 : 0;
        if (adc) {
            const uint16_t result = static_cast<uint16_t>(dst) + src + carry;
            if (rm_dest) write_rm8(rm, static_cast<uint8_t>(result));
            else set_reg8(reg, static_cast<uint8_t>(result));
            update_flags_add_8(dst, static_cast<uint8_t>(src + carry), result);
        } else {
            const uint16_t result = static_cast<uint16_t>(dst) - src - carry;
            if (rm_dest) write_rm8(rm, static_cast<uint8_t>(result));
            else set_reg8(reg, static_cast<uint8_t>(result));
            update_flags_sub_8(dst, static_cast<uint8_t>(src + carry), result);
        }
        break;
    }
    case 0x19: case 0x1B: case 0x11: case 0x13: { // SBB/ADC word
        const uint8_t modrm = fetch_byte();
        const uint8_t reg = (modrm >> 3) & 7;
        const auto rm = decode_rm(modrm);
        const bool adc = opcode == 0x11 || opcode == 0x13;
        const bool rm_dest = opcode == 0x11 || opcode == 0x19;
        const uint16_t src = rm_dest ? reg16(reg) : read_rm16(rm);
        const uint16_t dst = rm_dest ? read_rm16(rm) : reg16(reg);
        const uint16_t carry = get_carry() ? 1 : 0;
        if (adc) {
            const uint32_t result = static_cast<uint32_t>(dst) + src + carry;
            if (rm_dest) write_rm16(rm, static_cast<uint16_t>(result));
            else reg16(reg) = static_cast<uint16_t>(result);
            update_flags_add_16(dst, static_cast<uint16_t>(src + carry), result);
        } else {
            const uint32_t result = static_cast<uint32_t>(dst) - src - carry;
            if (rm_dest) write_rm16(rm, static_cast<uint16_t>(result));
            else reg16(reg) = static_cast<uint16_t>(result);
            update_flags_sub_16(dst, static_cast<uint16_t>(src + carry), result);
        }
        break;
    }
    case 0x17:
        ss_ = pop();
        break;
    case 0x1E:
        push(ds_);
        break;
    case 0x1F:
        ds_ = pop();
        break;

    case 0x20: case 0x22: case 0x30: case 0x32: { // AND/XOR byte
        const uint8_t modrm = fetch_byte();
        const uint8_t reg = (modrm >> 3) & 7;
        const auto rm = decode_rm(modrm);
        const bool xor_op = opcode >= 0x30;
        const uint8_t src = (opcode == 0x20 || opcode == 0x30) ? reg8(reg) : read_rm8(rm);
        const uint8_t dst = (opcode == 0x20 || opcode == 0x30) ? read_rm8(rm) : reg8(reg);
        const uint8_t result = xor_op ? static_cast<uint8_t>(dst ^ src) : static_cast<uint8_t>(dst & src);
        if (opcode == 0x20 || opcode == 0x30) write_rm8(rm, result);
        else set_reg8(reg, result);
        update_logic_flags_8(result);
        break;
    }
    case 0x21: case 0x23: case 0x31: case 0x33: { // AND/XOR word
        const uint8_t modrm = fetch_byte();
        const uint8_t reg = (modrm >> 3) & 7;
        const auto rm = decode_rm(modrm);
        const bool xor_op = opcode >= 0x30;
        const uint16_t src = (opcode == 0x21 || opcode == 0x31) ? reg16(reg) : read_rm16(rm);
        const uint16_t dst = (opcode == 0x21 || opcode == 0x31) ? read_rm16(rm) : reg16(reg);
        const uint16_t result = xor_op ? static_cast<uint16_t>(dst ^ src) : static_cast<uint16_t>(dst & src);
        if (opcode == 0x21 || opcode == 0x31) write_rm16(rm, result);
        else reg16(reg) = result;
        update_logic_flags_16(result);
        break;
    }
    case 0x24: set_al(static_cast<uint8_t>(get_al() & fetch_byte())); update_logic_flags_8(get_al()); break;
    case 0x25: ax_ = static_cast<uint16_t>(ax_ & fetch_word()); update_logic_flags_16(ax_); break;
    case 0x34: set_al(static_cast<uint8_t>(get_al() ^ fetch_byte())); update_logic_flags_8(get_al()); break;
    case 0x35: ax_ = static_cast<uint16_t>(ax_ ^ fetch_word()); update_logic_flags_16(ax_); break;

    case 0x28: case 0x2A: case 0x38: case 0x3A: { // SUB/CMP byte
        const uint8_t modrm = fetch_byte();
        const uint8_t reg = (modrm >> 3) & 7;
        const auto rm = decode_rm(modrm);
        const bool cmp = opcode >= 0x38;
        const bool rm_dest = opcode == 0x28 || opcode == 0x38;
        const uint8_t src = rm_dest ? reg8(reg) : read_rm8(rm);
        const uint8_t dst = rm_dest ? read_rm8(rm) : reg8(reg);
        const uint16_t result = static_cast<uint16_t>(dst) - src;
        if (!cmp) {
            if (rm_dest) write_rm8(rm, static_cast<uint8_t>(result));
            else set_reg8(reg, static_cast<uint8_t>(result));
        }
        update_flags_sub_8(dst, src, result);
        break;
    }
    case 0x29: case 0x2B: case 0x39: case 0x3B: { // SUB/CMP word
        const uint8_t modrm = fetch_byte();
        const uint8_t reg = (modrm >> 3) & 7;
        const auto rm = decode_rm(modrm);
        const bool cmp = opcode >= 0x38;
        const bool rm_dest = opcode == 0x29 || opcode == 0x39;
        const uint16_t src = rm_dest ? reg16(reg) : read_rm16(rm);
        const uint16_t dst = rm_dest ? read_rm16(rm) : reg16(reg);
        const uint32_t result = static_cast<uint32_t>(dst) - src;
        if (!cmp) {
            if (rm_dest) write_rm16(rm, static_cast<uint16_t>(result));
            else reg16(reg) = static_cast<uint16_t>(result);
        }
        update_flags_sub_16(dst, src, result);
        break;
    }
    case 0x2C: case 0x3C: {
        const uint8_t old = get_al();
        const uint8_t imm = fetch_byte();
        const uint16_t result = static_cast<uint16_t>(old) - imm;
        if (opcode == 0x2C) set_al(static_cast<uint8_t>(result));
        update_flags_sub_8(old, imm, result);
        break;
    }
    case 0x2D: case 0x3D: {
        const uint16_t old = ax_;
        const uint16_t imm = fetch_word();
        const uint32_t result = static_cast<uint32_t>(old) - imm;
        if (opcode == 0x2D) ax_ = static_cast<uint16_t>(result);
        update_flags_sub_16(old, imm, result);
        break;
    }

    case 0x40: case 0x41: case 0x42: case 0x43:
    case 0x44: case 0x45: case 0x46: case 0x47: {
        const uint8_t reg = opcode - 0x40;
        const uint16_t old = reg16(reg);
        const uint32_t result = static_cast<uint32_t>(old) + 1;
        const bool carry = get_carry();
        reg16(reg) = static_cast<uint16_t>(result);
        update_flags_add_16(old, 1, result);
        set_carry(carry);
        break;
    }
    case 0x48: case 0x49: case 0x4A: case 0x4B:
    case 0x4C: case 0x4D: case 0x4E: case 0x4F: {
        const uint8_t reg = opcode - 0x48;
        const uint16_t old = reg16(reg);
        const uint32_t result = static_cast<uint32_t>(old) - 1;
        const bool carry = get_carry();
        reg16(reg) = static_cast<uint16_t>(result);
        update_flags_sub_16(old, 1, result);
        set_carry(carry);
        break;
    }

    case 0x50: case 0x51: case 0x52: case 0x53:
    case 0x54: case 0x55: case 0x56: case 0x57:
        push(reg16(opcode - 0x50));
        break;
    case 0x58: case 0x59: case 0x5A: case 0x5B:
    case 0x5C: case 0x5D: case 0x5E: case 0x5F:
        reg16(opcode - 0x58) = pop();
        break;
    case 0x60: { // PUSHA
        const uint16_t old_sp = sp_;
        push(ax_);
        push(cx_);
        push(dx_);
        push(bx_);
        push(old_sp);
        push(bp_);
        push(si_);
        push(di_);
        break;
    }
    case 0x61: { // POPA
        di_ = pop();
        si_ = pop();
        bp_ = pop();
        (void)pop();
        bx_ = pop();
        dx_ = pop();
        cx_ = pop();
        ax_ = pop();
        break;
    }
    case 0x68:
        push(fetch_word());
        break;
    case 0x6A:
        push(sign_extend8(fetch_byte()));
        break;

    case 0x70: jump_short_if(get_overflow()); break;
    case 0x71: jump_short_if(!get_overflow()); break;
    case 0x72: jump_short_if(get_carry()); break;
    case 0x73: jump_short_if(!get_carry()); break;
    case 0x74: jump_short_if(get_zero()); break;
    case 0x75: jump_short_if(!get_zero()); break;
    case 0x76: jump_short_if(get_carry() || get_zero()); break;
    case 0x77: jump_short_if(!get_carry() && !get_zero()); break;
    case 0x78: jump_short_if(get_sign()); break;
    case 0x79: jump_short_if(!get_sign()); break;
    case 0x7A: jump_short_if((flags_ & 0x0004) != 0); break;
    case 0x7B: jump_short_if((flags_ & 0x0004) == 0); break;
    case 0x7C: jump_short_if(get_sign() != get_overflow()); break;
    case 0x7D: jump_short_if(get_sign() == get_overflow()); break;
    case 0x7E: jump_short_if(get_zero() || (get_sign() != get_overflow())); break;
    case 0x7F: jump_short_if(!get_zero() && (get_sign() == get_overflow())); break;

    case 0x80: case 0x82: case 0x81: case 0x83: {
        const uint8_t modrm = fetch_byte();
        const uint8_t op = (modrm >> 3) & 7;
        const auto rm = decode_rm(modrm);
        if (opcode == 0x80 || opcode == 0x82) {
            const uint8_t old = read_rm8(rm);
            const uint8_t imm = fetch_byte();
            uint8_t result = old;
            if (op == 0) {
                const uint16_t r = static_cast<uint16_t>(old) + imm;
                result = static_cast<uint8_t>(r);
                write_rm8(rm, result);
                update_flags_add_8(old, imm, r);
            } else if (op == 4) {
                result = static_cast<uint8_t>(old & imm);
                write_rm8(rm, result);
                update_logic_flags_8(result);
            } else if (op == 5 || op == 7) {
                const uint16_t r = static_cast<uint16_t>(old) - imm;
                result = static_cast<uint8_t>(r);
                if (op == 5) write_rm8(rm, result);
                update_flags_sub_8(old, imm, r);
            }
        } else {
            const uint16_t old = read_rm16(rm);
            const uint16_t imm = opcode == 0x83 ? sign_extend8(fetch_byte()) : fetch_word();
            if (op == 0) {
                const uint32_t r = static_cast<uint32_t>(old) + imm;
                write_rm16(rm, static_cast<uint16_t>(r));
                update_flags_add_16(old, imm, r);
            } else if (op == 4) {
                const uint16_t r = static_cast<uint16_t>(old & imm);
                write_rm16(rm, r);
                update_logic_flags_16(r);
            } else if (op == 5 || op == 7) {
                const uint32_t r = static_cast<uint32_t>(old) - imm;
                if (op == 5) write_rm16(rm, static_cast<uint16_t>(r));
                update_flags_sub_16(old, imm, r);
            }
        }
        break;
    }

    case 0x84: case 0x85: { // TEST r/m,r
        const uint8_t modrm = fetch_byte();
        const uint8_t reg = (modrm >> 3) & 7;
        const auto rm = decode_rm(modrm);
        if (opcode == 0x84) {
            update_logic_flags_8(static_cast<uint8_t>(read_rm8(rm) & reg8(reg)));
        } else {
            update_logic_flags_16(static_cast<uint16_t>(read_rm16(rm) & reg16(reg)));
        }
        break;
    }

    case 0x88: case 0x8A: { // MOV r/m8,r8 | MOV r8,r/m8
        const uint8_t modrm = fetch_byte();
        const uint8_t reg = (modrm >> 3) & 7;
        const auto rm = decode_rm(modrm);
        if (opcode == 0x88) write_rm8(rm, reg8(reg));
        else set_reg8(reg, read_rm8(rm));
        break;
    }
    case 0x89: case 0x8B: { // MOV r/m16,r16 | MOV r16,r/m16
        const uint8_t modrm = fetch_byte();
        const uint8_t reg = (modrm >> 3) & 7;
        const auto rm = decode_rm(modrm);
        if (opcode == 0x89) write_rm16(rm, reg16(reg));
        else reg16(reg) = read_rm16(rm);
        break;
    }
    case 0x8C: {
        const uint8_t modrm = fetch_byte();
        const auto rm = decode_rm(modrm);
        write_rm16(rm, seg_reg((modrm >> 3) & 3));
        break;
    }
    case 0x8D: {
        const uint8_t modrm = fetch_byte();
        const uint8_t reg = (modrm >> 3) & 7;
        const auto rm = decode_rm(modrm);
        reg16(reg) = rm.effective_offset;
        break;
    }
    case 0x8E: {
        const uint8_t modrm = fetch_byte();
        const auto rm = decode_rm(modrm);
        set_seg_reg((modrm >> 3) & 3, read_rm16(rm));
        break;
    }
    case 0x8F: {
        const uint8_t modrm = fetch_byte();
        const auto rm = decode_rm(modrm);
        write_rm16(rm, pop());
        break;
    }
    case 0x90:
        break;
    case 0x91: case 0x92: case 0x93:
    case 0x94: case 0x95: case 0x96: case 0x97: {
        const uint8_t reg = opcode - 0x90;
        std::swap(ax_, reg16(reg));
        break;
    }
    case 0x98: // CBW
        ax_ = static_cast<uint16_t>(static_cast<int16_t>(static_cast<int8_t>(get_al())));
        break;
    case 0x99: // CWD
        dx_ = (ax_ & 0x8000) ? 0xFFFF : 0x0000;
        break;
    case 0x9C:
        push(flags_);
        break;
    case 0x9D:
        flags_ = pop();
        break;

    case 0xA0: set_al(read_byte(get_linear(effective_data_segment(), fetch_word()))); break;
    case 0xA1: ax_ = read_word(get_linear(effective_data_segment(), fetch_word())); break;
    case 0xA2: write_byte(get_linear(effective_data_segment(), fetch_word()), get_al()); break;
    case 0xA3: write_word(get_linear(effective_data_segment(), fetch_word()), ax_); break;
    case 0xA4: { // MOVSB
        const uint16_t count = consume_string_count();
        const int step = string_step();
        for (uint16_t i = 0; i < count; ++i) {
            write_byte(get_linear(es_, di_), read_byte(get_linear(effective_data_segment(), si_)));
            si_ = static_cast<uint16_t>(si_ + step);
            di_ = static_cast<uint16_t>(di_ + step);
        }
        break;
    }
    case 0xA5: { // MOVSW
        const uint16_t count = consume_string_count();
        const int step = string_step() * 2;
        for (uint16_t i = 0; i < count; ++i) {
            write_word(get_linear(es_, di_), read_word(get_linear(effective_data_segment(), si_)));
            si_ = static_cast<uint16_t>(si_ + step);
            di_ = static_cast<uint16_t>(di_ + step);
        }
        break;
    }
    case 0xA6: case 0xA7: { // CMPSB/CMPSW
        const uint16_t count = consume_string_count();
        const int step = string_step() * (opcode == 0xA6 ? 1 : 2);
        for (uint16_t i = 0; i < count; ++i) {
            if (opcode == 0xA6) {
                const uint8_t a = read_byte(get_linear(effective_data_segment(), si_));
                const uint8_t b = read_byte(get_linear(es_, di_));
                update_flags_sub_8(a, b, static_cast<uint16_t>(a) - b);
            } else {
                const uint16_t a = read_word(get_linear(effective_data_segment(), si_));
                const uint16_t b = read_word(get_linear(es_, di_));
                update_flags_sub_16(a, b, static_cast<uint32_t>(a) - b);
            }
            si_ = static_cast<uint16_t>(si_ + step);
            di_ = static_cast<uint16_t>(di_ + step);
            if (repeat_prefix_ == RepeatPrefix::Rep && !get_zero()) break;
            if (repeat_prefix_ == RepeatPrefix::Repne && get_zero()) break;
        }
        break;
    }
    case 0xA8:
        update_logic_flags_8(static_cast<uint8_t>(get_al() & fetch_byte()));
        break;
    case 0xA9:
        update_logic_flags_16(static_cast<uint16_t>(ax_ & fetch_word()));
        break;
    case 0xAA: { // STOSB
        const uint16_t count = consume_string_count();
        const int step = string_step();
        for (uint16_t i = 0; i < count; ++i) {
            write_byte(get_linear(es_, di_), get_al());
            di_ = static_cast<uint16_t>(di_ + step);
        }
        break;
    }
    case 0xAB: { // STOSW
        const uint16_t count = consume_string_count();
        const int step = string_step() * 2;
        for (uint16_t i = 0; i < count; ++i) {
            write_word(get_linear(es_, di_), ax_);
            di_ = static_cast<uint16_t>(di_ + step);
        }
        break;
    }
    case 0xAC: { // LODSB
        const uint16_t count = consume_string_count();
        const int step = string_step();
        for (uint16_t i = 0; i < count; ++i) {
            set_al(read_byte(get_linear(effective_data_segment(), si_)));
            si_ = static_cast<uint16_t>(si_ + step);
        }
        break;
    }
    case 0xAD: { // LODSW
        const uint16_t count = consume_string_count();
        const int step = string_step() * 2;
        for (uint16_t i = 0; i < count; ++i) {
            ax_ = read_word(get_linear(effective_data_segment(), si_));
            si_ = static_cast<uint16_t>(si_ + step);
        }
        break;
    }
    case 0xAE: case 0xAF: { // SCASB/SCASW
        const uint16_t count = consume_string_count();
        const int step = string_step() * (opcode == 0xAE ? 1 : 2);
        for (uint16_t i = 0; i < count; ++i) {
            if (opcode == 0xAE) {
                const uint8_t b = read_byte(get_linear(es_, di_));
                update_flags_sub_8(get_al(), b, static_cast<uint16_t>(get_al()) - b);
            } else {
                const uint16_t b = read_word(get_linear(es_, di_));
                update_flags_sub_16(ax_, b, static_cast<uint32_t>(ax_) - b);
            }
            di_ = static_cast<uint16_t>(di_ + step);
            if (repeat_prefix_ == RepeatPrefix::Rep && !get_zero()) break;
            if (repeat_prefix_ == RepeatPrefix::Repne && get_zero()) break;
        }
        break;
    }

    case 0xB0: case 0xB1: case 0xB2: case 0xB3:
    case 0xB4: case 0xB5: case 0xB6: case 0xB7:
        set_reg8(opcode - 0xB0, fetch_byte());
        break;
    case 0xB8: case 0xB9: case 0xBA: case 0xBB:
    case 0xBC: case 0xBD: case 0xBE: case 0xBF:
        reg16(opcode - 0xB8) = fetch_word();
        break;

    case 0xC2: {
        const uint16_t imm = fetch_word();
        ip_ = pop();
        sp_ = static_cast<uint16_t>(sp_ + imm);
        break;
    }
    case 0xC3:
        ip_ = pop();
        break;
    case 0xC4: case 0xC5: { // LES/LDS r16,m16:16
        const uint8_t modrm = fetch_byte();
        const uint8_t reg = (modrm >> 3) & 7;
        const auto rm = decode_rm(modrm);
        if (!rm.is_register) {
            reg16(reg) = read_word(rm.address);
            if (opcode == 0xC4) {
                es_ = read_word(rm.address + 2);
            } else {
                ds_ = read_word(rm.address + 2);
            }
        }
        break;
    }
    case 0xC6: case 0xC7: {
        const uint8_t modrm = fetch_byte();
        const auto rm = decode_rm(modrm);
        if (opcode == 0xC6) write_rm8(rm, fetch_byte());
        else write_rm16(rm, fetch_word());
        break;
    }
    case 0xCB:
        ip_ = pop();
        cs_ = pop();
        break;
    case 0xCC:
        do_int(3);
        break;
    case 0xCD:
        do_int(fetch_byte());
        break;
    case 0xCF:
        ip_ = pop();
        cs_ = pop();
        flags_ = pop();
        break;

    case 0xC0: case 0xC1: case 0xD0: case 0xD1: case 0xD2: case 0xD3: {
        const uint8_t modrm = fetch_byte();
        const uint8_t op = (modrm >> 3) & 7;
        const auto rm = decode_rm(modrm);
        const uint8_t count = (opcode == 0xC0 || opcode == 0xC1)
            ? fetch_byte()
            : ((opcode == 0xD2 || opcode == 0xD3) ? get_cl() : 1);
        if (opcode == 0xC0 || opcode == 0xD0 || opcode == 0xD2) {
            const uint8_t old = read_rm8(rm);
            write_rm8(rm, shift8(old, op, count));
        } else {
            const uint16_t old = read_rm16(rm);
            write_rm16(rm, shift16(old, op, count));
        }
        break;
    }

    case 0xE4: {
        const uint8_t port = fetch_byte();
        set_al(read_io_ ? read_io_(port) : 0xFF);
        break;
    }
    case 0xE5: {
        const uint8_t port = fetch_byte();
        set_ax(read_io_ ? read_io_(port) : 0xFFFF);
        break;
    }
    case 0xE6: {
        const uint8_t port = fetch_byte();
        if (write_io_) write_io_(port, get_al());
        break;
    }
    case 0xE7: {
        const uint8_t port = fetch_byte();
        if (write_io_) {
            write_io_(port, get_al());
            write_io_(static_cast<uint16_t>(port + 1), get_ah());
        }
        break;
    }
    case 0xE8: {
        const int16_t disp = static_cast<int16_t>(fetch_word());
        push(ip_);
        ip_ = static_cast<uint16_t>(ip_ + disp);
        break;
    }
    case 0xE9: {
        const int16_t disp = static_cast<int16_t>(fetch_word());
        ip_ = static_cast<uint16_t>(ip_ + disp);
        break;
    }
    case 0xE0: case 0xE1: case 0xE2: case 0xE3: {
        const int8_t disp = static_cast<int8_t>(fetch_byte());
        bool take = false;
        if (opcode == 0xE3) {
            take = cx_ == 0;
        } else {
            cx_ = static_cast<uint16_t>(cx_ - 1);
            if (opcode == 0xE0) {
                take = cx_ != 0 && !get_zero();
            } else if (opcode == 0xE1) {
                take = cx_ != 0 && get_zero();
            } else {
                take = cx_ != 0;
            }
        }
        if (take) {
            ip_ = static_cast<uint16_t>(ip_ + disp);
        }
        break;
    }
    case 0xEA: {
        const uint16_t off = fetch_word();
        const uint16_t seg = fetch_word();
        set_cs_ip(seg, off);
        break;
    }
    case 0xEB:
        jump_short_if(true);
        break;
    case 0xEC:
        set_al(read_io_ ? read_io_(dx_) : 0xFF);
        break;
    case 0xED:
        set_ax(read_io_ ? read_io_(dx_) : 0xFFFF);
        break;
    case 0xEE:
        if (write_io_) write_io_(dx_, get_al());
        break;
    case 0xEF:
        if (write_io_) {
            write_io_(dx_, get_al());
            write_io_(static_cast<uint16_t>(dx_ + 1), get_ah());
        }
        break;

    case 0x26: case 0x2E: case 0x36: case 0x3E: {
        const bool old_active = segment_override_active_;
        const uint16_t old_override = segment_override_;
        segment_override_active_ = true;
        segment_override_ = segment_for_prefix(opcode, es_, cs_, ss_, ds_);
        execute_one();
        segment_override_active_ = old_active;
        segment_override_ = old_override;
        break;
    }

    case 0xF0:
        break;
    case 0xF2: case 0xF3: {
        const RepeatPrefix old_repeat = repeat_prefix_;
        repeat_prefix_ = opcode == 0xF2 ? RepeatPrefix::Repne : RepeatPrefix::Rep;
        execute_one();
        repeat_prefix_ = old_repeat;
        break;
    }
    case 0xF4:
        halted_ = true;
        break;
    case 0xF5:
        set_carry(!get_carry());
        break;
    case 0xF6: case 0xF7: {
        const uint8_t modrm = fetch_byte();
        const uint8_t op = (modrm >> 3) & 7;
        const auto rm = decode_rm(modrm);
        if (opcode == 0xF6) {
            const uint8_t old = read_rm8(rm);
            if (op == 0 || op == 1) {
                const uint8_t imm = fetch_byte();
                update_logic_flags_8(static_cast<uint8_t>(old & imm));
            } else if (op == 2) {
                write_rm8(rm, static_cast<uint8_t>(~old));
            } else if (op == 3) {
                const uint8_t result = static_cast<uint8_t>(-static_cast<int8_t>(old));
                write_rm8(rm, result);
                update_flags_sub_8(0, old, static_cast<uint16_t>(0) - old);
            } else if (op == 4) {
                const uint16_t result = static_cast<uint16_t>(get_al()) * old;
                ax_ = result;
                set_carry((result & 0xff00) != 0);
                set_overflow(get_carry());
            } else if (op == 6) {
                if (old == 0) {
                    halted_ = true;
                    break;
                }
                const uint16_t dividend = ax_;
                set_al(static_cast<uint8_t>(dividend / old));
                set_ah(static_cast<uint8_t>(dividend % old));
            }
        } else {
            const uint16_t old = read_rm16(rm);
            if (op == 0 || op == 1) {
                const uint16_t imm = fetch_word();
                update_logic_flags_16(static_cast<uint16_t>(old & imm));
            } else if (op == 2) {
                write_rm16(rm, static_cast<uint16_t>(~old));
            } else if (op == 3) {
                const uint16_t result = static_cast<uint16_t>(-static_cast<int16_t>(old));
                write_rm16(rm, result);
                update_flags_sub_16(0, old, static_cast<uint32_t>(0) - old);
            } else if (op == 4) {
                const uint32_t result = static_cast<uint32_t>(ax_) * old;
                ax_ = static_cast<uint16_t>(result & 0xffff);
                dx_ = static_cast<uint16_t>((result >> 16) & 0xffff);
                set_carry(dx_ != 0);
                set_overflow(get_carry());
            } else if (op == 6) {
                if (old == 0) {
                    halted_ = true;
                    break;
                }
                const uint32_t dividend = (static_cast<uint32_t>(dx_) << 16) | ax_;
                ax_ = static_cast<uint16_t>(dividend / old);
                dx_ = static_cast<uint16_t>(dividend % old);
            }
        }
        break;
    }
    case 0xF8: set_carry(true); break;
    case 0xF9: set_carry(false); break;
    case 0xFA: set_interrupt_flag(false); break;
    case 0xFB: set_interrupt_flag(true); break;
    case 0xFC: set_direction(false); break;
    case 0xFD: set_direction(true); break;

    case 0xFE: case 0xFF: {
        const uint8_t modrm = fetch_byte();
        const uint8_t op = (modrm >> 3) & 7;
        const auto rm = decode_rm(modrm);
        if (opcode == 0xFE) {
            const uint8_t old = read_rm8(rm);
            if (op == 0) {
                const uint16_t r = static_cast<uint16_t>(old) + 1;
                const bool carry = get_carry();
                write_rm8(rm, static_cast<uint8_t>(r));
                update_flags_add_8(old, 1, r);
                set_carry(carry);
            } else if (op == 1) {
                const uint16_t r = static_cast<uint16_t>(old) - 1;
                const bool carry = get_carry();
                write_rm8(rm, static_cast<uint8_t>(r));
                update_flags_sub_8(old, 1, r);
                set_carry(carry);
            }
        } else {
            const uint16_t old = read_rm16(rm);
            switch (op) {
            case 0: {
                const uint32_t r = static_cast<uint32_t>(old) + 1;
                const bool carry = get_carry();
                write_rm16(rm, static_cast<uint16_t>(r));
                update_flags_add_16(old, 1, r);
                set_carry(carry);
                break;
            }
            case 1: {
                const uint32_t r = static_cast<uint32_t>(old) - 1;
                const bool carry = get_carry();
                write_rm16(rm, static_cast<uint16_t>(r));
                update_flags_sub_16(old, 1, r);
                set_carry(carry);
                break;
            }
            case 2:
                push(ip_);
                ip_ = old;
                break;
            case 3:
                push(cs_);
                push(ip_);
                ip_ = old;
                cs_ = rm.is_register ? cs_ : read_word(rm.address + 2);
                break;
            case 4:
                ip_ = old;
                break;
            case 5:
                ip_ = old;
                cs_ = rm.is_register ? cs_ : read_word(rm.address + 2);
                break;
            case 6:
                push(old);
                break;
            default:
                break;
            }
        }
        break;
    }

    default:
        last_unsupported_opcode_ = opcode;
        last_unsupported_cs_ = cs_;
        last_unsupported_ip_ = opcode_ip;
        ++unsupported_count_;
        halted_ = true;
        break;
    }
}

} // namespace hoot
