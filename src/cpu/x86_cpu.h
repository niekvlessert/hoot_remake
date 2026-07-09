#pragma once

#include <cstdint>
#include <functional>
#include <vector>

namespace hoot {

class X86Cpu {
public:
    using ReadMemoryCallback = std::function<uint8_t(uint32_t address)>;
    using WriteMemoryCallback = std::function<void(uint32_t address, uint8_t data)>;
    using IoReadCallback = std::function<uint8_t(uint16_t port)>;
    using IoWriteCallback = std::function<void(uint16_t port, uint8_t data)>;
    using InterruptCallback = std::function<void(uint8_t int_num)>;
    using TraceCallback = std::function<void(const char* type,
                                             uint8_t opcode,
                                             uint16_t from_cs,
                                             uint16_t from_ip,
                                             uint16_t to_cs,
                                             uint16_t to_ip)>;

    X86Cpu();
    ~X86Cpu() = default;

    void set_read_memory_callback(ReadMemoryCallback cb);
    void set_write_memory_callback(WriteMemoryCallback cb);
    void set_io_read_callback(IoReadCallback cb);
    void set_io_write_callback(IoWriteCallback cb);
    void set_interrupt_callback(InterruptCallback cb);
    void set_trace_callback(TraceCallback cb);

    void reset();
    int execute(int cycles);

    void set_pc(uint16_t pc) { set_cs_ip(cs_, pc); }
    void set_sp(uint16_t sp) { sp_ = sp; }
    void set_ss(uint16_t ss) { ss_ = ss; }
    void set_cs(uint16_t cs) { cs_ = cs; }
    void set_ds(uint16_t ds) { ds_ = ds; }
    void set_es(uint16_t es) { es_ = es; }

    uint16_t get_pc() const { return ip_; }
    uint16_t get_cs() const { return cs_; }
    uint16_t get_sp() const { return sp_; }
    uint16_t get_ss() const { return ss_; }
    uint16_t get_ds() const { return ds_; }
    uint16_t get_es() const { return es_; }
    uint16_t get_si() const { return si_; }
    uint16_t get_di() const { return di_; }

    uint16_t get_ax() const { return ax_; }
    uint16_t get_bx() const { return bx_; }
    uint16_t get_cx() const { return cx_; }
    uint16_t get_dx() const { return dx_; }

    void set_ax(uint16_t v) { ax_ = v; }
    void set_bx(uint16_t v) { bx_ = v; }
    void set_cx(uint16_t v) { cx_ = v; }
    void set_dx(uint16_t v) { dx_ = v; }
    void set_si(uint16_t v) { si_ = v; }
    void set_di(uint16_t v) { di_ = v; }
    void set_bp(uint16_t v) { bp_ = v; }

    uint8_t get_al() const { return static_cast<uint8_t>(ax_ & 0xff); }
    uint8_t get_ah() const { return static_cast<uint8_t>((ax_ >> 8) & 0xff); }
    uint8_t get_bl() const { return static_cast<uint8_t>(bx_ & 0xff); }
    uint8_t get_bh() const { return static_cast<uint8_t>((bx_ >> 8) & 0xff); }
    uint8_t get_cl() const { return static_cast<uint8_t>(cx_ & 0xff); }
    uint8_t get_ch() const { return static_cast<uint8_t>((cx_ >> 8) & 0xff); }
    uint8_t get_dl() const { return static_cast<uint8_t>(dx_ & 0xff); }
    uint8_t get_dh() const { return static_cast<uint8_t>((dx_ >> 8) & 0xff); }

    void set_al(uint8_t v) { ax_ = (ax_ & 0xff00) | v; }
    void set_ah(uint8_t v) { ax_ = (ax_ & 0x00ff) | (static_cast<uint16_t>(v) << 8); }
    void set_bl(uint8_t v) { bx_ = (bx_ & 0xff00) | v; }
    void set_bh(uint8_t v) { bx_ = (bx_ & 0x00ff) | (static_cast<uint16_t>(v) << 8); }
    void set_cl(uint8_t v) { cx_ = (cx_ & 0xff00) | v; }
    void set_ch(uint8_t v) { cx_ = (cx_ & 0x00ff) | (static_cast<uint16_t>(v) << 8); }
    void set_dl(uint8_t v) { dx_ = (dx_ & 0xff00) | v; }
    void set_dh(uint8_t v) { dx_ = (dx_ & 0x00ff) | (static_cast<uint16_t>(v) << 8); }

    bool get_carry() const { return (flags_ & 0x0001) != 0; }
    bool get_zero() const { return (flags_ & 0x0040) != 0; }
    bool get_sign() const { return (flags_ & 0x0080) != 0; }
    bool get_overflow() const { return (flags_ & 0x0800) != 0; }
    bool get_direction() const { return (flags_ & 0x0400) != 0; }
    bool get_interrupt_flag() const { return (flags_ & 0x0200) != 0; }

    void set_carry(bool v) { if (v) flags_ |= 0x0001; else flags_ &= ~0x0001; }
    void set_zero(bool v) { if (v) flags_ |= 0x0040; else flags_ &= ~0x0040; }
    void set_sign(bool v) { if (v) flags_ |= 0x0080; else flags_ &= ~0x0080; }
    void set_overflow(bool v) { if (v) flags_ |= 0x0800; else flags_ &= ~0x0800; }
    void set_direction(bool v) { if (v) flags_ |= 0x0400; else flags_ &= ~0x0400; }
    void set_interrupt_flag(bool v) { if (v) flags_ |= 0x0200; else flags_ &= ~0x0200; }

    void set_memory(uint32_t addr, const uint8_t* data, uint32_t size);

    uint8_t* memory() { return memory_.data(); }

    bool is_halted() const { return halted_; }
    void halt() { halted_ = true; }
    void clear_halted() { halted_ = false; }
    void clear_unsupported_status()
    {
        last_unsupported_opcode_ = 0;
        last_unsupported_cs_ = 0;
        last_unsupported_ip_ = 0;
        unsupported_count_ = 0;
    }
    uint8_t last_unsupported_opcode() const { return last_unsupported_opcode_; }
    uint16_t last_unsupported_cs() const { return last_unsupported_cs_; }
    uint16_t last_unsupported_ip() const { return last_unsupported_ip_; }
    uint32_t unsupported_count() const { return unsupported_count_; }

private:
    uint32_t get_linear(uint16_t seg, uint16_t off) const { return (static_cast<uint32_t>(seg) << 4) + off; }

    uint8_t fetch_byte();
    uint16_t fetch_word();

    uint8_t read_byte(uint32_t addr);
    uint16_t read_word(uint32_t addr);
    void write_byte(uint32_t addr, uint8_t val);
    void write_word(uint32_t addr, uint16_t val);

    void push(uint16_t val);
    uint16_t pop();

    void set_cs_ip(uint16_t seg, uint16_t ip) { cs_ = seg; ip_ = ip; }

    void do_int(uint8_t num);
    bool check_jump(bool cond);

    void update_flags_8(uint8_t result);
    void update_flags_16(uint16_t result);
    void update_flags_add_8(uint8_t a, uint8_t b, uint16_t result);
    void update_flags_add_16(uint16_t a, uint16_t b, uint32_t result);
    void update_flags_sub_8(uint8_t a, uint8_t b, uint16_t result);
    void update_flags_sub_16(uint16_t a, uint16_t b, uint32_t result);
    uint8_t shift8(uint8_t value, uint8_t op, uint8_t count);
    uint16_t shift16(uint16_t value, uint8_t op, uint8_t count);

    void execute_one();

    struct DecodedRm {
        bool is_register = false;
        uint8_t reg = 0;
        uint32_t address = 0;
        uint16_t effective_offset = 0;
    };

    uint16_t& reg16(uint8_t index);
    uint16_t reg16(uint8_t index) const;
    uint8_t reg8(uint8_t index) const;
    void set_reg8(uint8_t index, uint8_t value);
    uint16_t seg_reg(uint8_t index) const;
    void set_seg_reg(uint8_t index, uint16_t value);
    DecodedRm decode_rm(uint8_t modrm);
    uint8_t read_rm8(const DecodedRm& rm);
    uint16_t read_rm16(const DecodedRm& rm);
    void write_rm8(const DecodedRm& rm, uint8_t value);
    void write_rm16(const DecodedRm& rm, uint16_t value);
    uint16_t effective_data_segment() const;
    uint16_t consume_string_count();
    int string_step() const;
    void update_logic_flags_8(uint8_t result);
    void update_logic_flags_16(uint16_t result);
    void set_parity_from_byte(uint8_t result);
    void jump_short_if(bool condition);
    void trace_control_flow(const char* type,
                            uint8_t opcode,
                            uint16_t from_cs,
                            uint16_t from_ip,
                            uint16_t to_cs,
                            uint16_t to_ip);

    uint16_t cs_ = 0;
    uint16_t ip_ = 0;
    uint16_t sp_ = 0;
    uint16_t ss_ = 0;
    uint16_t ds_ = 0;
    uint16_t es_ = 0;

    uint16_t ax_ = 0;
    uint16_t bx_ = 0;
    uint16_t cx_ = 0;
    uint16_t dx_ = 0;
    uint16_t si_ = 0;
    uint16_t di_ = 0;
    uint16_t bp_ = 0;

    uint16_t flags_ = 0xF000;

    bool halted_ = false;
    uint8_t last_unsupported_opcode_ = 0;
    uint16_t last_unsupported_cs_ = 0;
    uint16_t last_unsupported_ip_ = 0;
    uint32_t unsupported_count_ = 0;
    bool segment_override_active_ = false;
    uint16_t segment_override_ = 0;
    enum class RepeatPrefix {
        None,
        Rep,
        Repne,
    };
    RepeatPrefix repeat_prefix_ = RepeatPrefix::None;

    ReadMemoryCallback read_memory_;
    WriteMemoryCallback write_memory_;
    IoReadCallback read_io_;
    IoWriteCallback write_io_;
    InterruptCallback interrupt_;
    TraceCallback trace_;

    std::vector<uint8_t> memory_;
};

} // namespace hoot
