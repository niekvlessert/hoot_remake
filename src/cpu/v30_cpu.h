#pragma once

#include <cstdint>
#include <functional>

namespace hoot {

class V30Cpu {
public:
    using ReadMemoryCallback = std::function<uint8_t(uint32_t address)>;
    using WriteMemoryCallback = std::function<void(uint32_t address, uint8_t data)>;
    using ReadIoCallback = std::function<uint8_t(uint16_t port)>;
    using WriteIoCallback = std::function<void(uint16_t port, uint8_t data)>;
    using InterruptCallback = std::function<void(uint8_t int_num)>;

    V30Cpu();
    ~V30Cpu() = default;

    void set_read_memory_callback(ReadMemoryCallback cb);
    void set_write_memory_callback(WriteMemoryCallback cb);
    void set_read_io_callback(ReadIoCallback cb);
    void set_write_io_callback(WriteIoCallback cb);
    void set_interrupt_callback(InterruptCallback cb);

    void reset(uint16_t pc = 0xFFFF);
    uint32_t execute(uint32_t cycles);

    uint16_t pc() const;
    void set_pc(uint16_t pc);

    uint16_t sp() const;
    void set_sp(uint16_t sp);

    uint16_t cs() const;
    void set_cs(uint16_t cs);

    uint16_t ds() const;
    void set_ds(uint16_t ds);

    uint16_t es() const;
    void set_es(uint16_t es);

    uint16_t ss() const;
    void set_ss(uint16_t ss);

    uint8_t al() const;
    void set_al(uint8_t v);
    uint8_t ah() const;
    void set_ah(uint8_t v);
    uint16_t ax() const;
    void set_ax(uint16_t v);

    uint8_t bl() const;
    void set_bl(uint8_t v);
    uint8_t bh() const;
    void set_bh(uint8_t v);
    uint16_t bx() const;
    void set_bx(uint16_t v);

    uint8_t cl() const;
    void set_cl(uint8_t v);
    uint8_t ch() const;
    void set_ch(uint8_t v);
    uint16_t cx() const;
    void set_cx(uint16_t v);

    uint8_t dl() const;
    void set_dl(uint8_t v);
    uint8_t dh() const;
    void set_dh(uint8_t v);
    uint16_t dx() const;
    void set_dx(uint16_t v);

    uint16_t si() const;
    void set_si(uint16_t v);
    uint16_t di() const;
    void set_di(uint16_t v);
    uint16_t bp() const;
    void set_bp(uint16_t v);

    uint32_t flags() const;
    void set_flags(uint32_t f);

    bool carry() const;
    bool parity() const;
    bool adjust() const;
    bool zero() const;
    bool sign() const;
    bool trace() const;
    bool interrupt() const;
    bool direction() const;
    bool overflow() const;

    void set_carry(bool v);
    void set_zero(bool v);
    void set_sign(bool v);
    void set_overflow(bool v);
    void set_interrupt(bool v);
    void set_direction(bool v);

    void raise_irq(uint8_t line);
    void lower_irq(uint8_t line);
    bool irq_pending() const;
    uint8_t pending_irq() const;

    void trigger_interrupt(uint8_t int_num);

    static constexpr uint32_t kMemorySize = 1024 * 1024;

    uint8_t* memory() { return memory_; }
    const uint8_t* memory() const { return memory_; }

private:
    uint8_t memory_[kMemorySize]{};

    uint16_t pc_ = 0;
    uint16_t sp_ = 0;
    uint16_t cs_ = 0;
    uint16_t ds_ = 0;
    uint16_t es_ = 0;
    uint16_t ss_ = 0;

    uint16_t ax_ = 0;
    uint16_t bx_ = 0;
    uint16_t cx_ = 0;
    uint16_t dx_ = 0;
    uint16_t si_ = 0;
    uint16_t di_ = 0;
    uint16_t bp_ = 0;

    uint32_t flags_ = 0;

    uint8_t irq_lines_ = 0;
    uint8_t pending_irq_ = 0;

    ReadMemoryCallback read_memory_;
    WriteMemoryCallback write_memory_;
    ReadIoCallback read_io_;
    WriteIoCallback write_io_;
    InterruptCallback interrupt_;
};

} // namespace hoot