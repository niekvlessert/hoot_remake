#include <cassert>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

#include "config/hoot_catalog.h"
#include "drivers/driver_registry.h"
#include "drivers/x68k_generic_driver.h"
#include "m68k.h"

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "usage: x68k_dmaint_test FIXTURE_DIR\n";
        return 2;
    }
    hoot::HootEntry e;
    e.id = "synthetic-x68k-dmaint";
    e.title = "Synthetic X68000 DMA interrupt";
    e.driver_name = "x68k/generic";
    e.archive = "x68k_dmaint_synthetic";
    e.options["dmaint"] = 1;
    e.assets.push_back({"code", "driver.bin", {}, 0, 0, false});
    e.tracks.push_back({0, "dma", {}});

    const auto probe = hoot::DriverRegistry::instance().probe(e);
    if (probe.status == hoot::DriverSupportStatus::Recognized) {
        std::cerr << "dmaint remained recognized-only: " << probe.reason << "\n";
        return 1;
    }
    auto base = hoot::DriverRegistry::instance().create(e);
    auto* d = dynamic_cast<hoot::X68kGenericDriver*>(base.get());
    assert(d);
    std::string error;
    assert(d->load(e, argv[1], 44100, error) == HOOT_OK);

    // The Hoot X68000 ADPCM shim obtains the transfer address/length from
    // A1/D2 when the guest touches the corresponding channel-3 DMA ports.
    m68k_set_reg(M68K_REG_A1, 0x180);
    m68k_set_reg(M68K_REG_D2, 4);
    d->write_memory_8(0xe840e5, 0x55); // channel-3 normal interrupt vector
    assert(d->read_memory_8(0xe840e5) == 0x55);
    d->write_memory_8(0xe840ca, 0x00);
    d->write_memory_8(0xe840cc, 0x00);
    d->write_memory_8(0xe840c7, 0x88);

    std::vector<int16_t> audio(24 * 2);
    assert(d->render_s16(audio.data(), 24) == 24);
    // Completion at the tail of the render quantum must assert X68000 IRQ3
    // and deliver the configured 68450 channel-3 NIV.
    HootVisualState visual{};
    d->fill_visual_state(e, 0, visual);
    if (std::string(visual.architecture) != "X68000" || visual.channel_count < 9
        || visual.register_count < 10 || visual.driver_work_size == 0) {
        std::cerr << "incomplete X68000 visual telemetry: " << visual.architecture
                  << " channels=" << visual.channel_count
                  << " regs=" << visual.register_count
                  << " work=" << visual.driver_work_size << "\n";
        return 1;
    }

    const int vector = d->acknowledge_interrupt(3);
    if (vector != 0x55) {
        std::cerr << "expected DMA vector 0x55, got 0x" << std::hex << vector << "\n";
        return 1;
    }
    return 0;
}
