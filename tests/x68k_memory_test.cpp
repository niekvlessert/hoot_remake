#include <cstdint>
#include <iostream>
#include <memory>

#include "drivers/x68k_generic_driver.h"

namespace {

bool expect_byte(hoot::X68kGenericDriver& driver,
                 uint32_t address,
                 uint8_t expected,
                 const char* label)
{
    const uint8_t actual = driver.read_memory_8(address);
    if (actual == expected) {
        return true;
    }
    std::cerr << label << ": address 0x" << std::hex << address
              << " expected 0x" << static_cast<unsigned>(expected)
              << " got 0x" << static_cast<unsigned>(actual) << std::dec << '\n';
    return false;
}

} // namespace

int main()
{
    auto driver = std::make_unique<hoot::X68kGenericDriver>();
    bool ok = true;

    // The expanded main-memory image must retain data used by modern packs.
    driver->write_memory_8(0x300000, 0x5a);
    ok &= expect_byte(*driver, 0x300000, 0x5a, "mid main memory");

    driver->write_memory_8(0xe78000, 0xa5);
    ok &= expect_byte(*driver, 0xe78000, 0xa5, "top of main memory");

    // Most of the old broad Hoot scratch window is real packed-data memory.
    driver->write_memory_8(0xe00020, 0x33);
    ok &= expect_byte(*driver, 0xe00020, 0x33, "high packed-data memory");

    // The mailbox still takes priority over the overlapping memory image.
    driver->write_memory_8(0xe00000, 0x01);
    ok &= expect_byte(*driver, 0xe00000, 0x01, "mailbox priority");

    // Unimplemented holes in the I/O page retain the compatibility scratch RAM.
    driver->write_memory_8(0xe81000, 0x44);
    ok &= expect_byte(*driver, 0xe81000, 0x44, "I/O scratch fallback");

    driver->write_memory_8(0xf00010, 0x55);
    ok &= expect_byte(*driver, 0xf00010, 0x55, "work RAM");

    // Some Human68k/XC generated routines deliberately keep A6 at zero and
    // use negative frame offsets. On a 24-bit 68000 those locals wrap to the
    // final bytes of the address space. A-Train II uses 0xfffff8/0xfffffc.
    driver->write_memory_8(0xfffff8, 0x66);
    driver->write_memory_8(0xfffffc, 0x77);
    ok &= expect_byte(*driver, 0xfffff8, 0x66, "wrapped high workspace -8");
    ok &= expect_byte(*driver, 0xfffffc, 0x77, "wrapped high workspace -4");

    return ok ? 0 : 1;
}
