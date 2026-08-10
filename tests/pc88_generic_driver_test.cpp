#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include "config/hoot_catalog.h"
#include "cpu/kmz80_cpu.h"
#include "drivers/driver_registry.h"

namespace {


bool run_kmz80_i_register_case()
{
    std::array<uint8_t, 65536> memory{};
    // LD A,90h / LD I,A / a run of NOPs. The Z80 refresh register changes
    // during execution, but I must remain 90h for IM2 vector addressing.
    memory[0x0000] = 0x3e;
    memory[0x0001] = 0x90;
    memory[0x0002] = 0xed;
    memory[0x0003] = 0x47;
    memory[0x0004] = 0x18;
    memory[0x0005] = 0xfe;

    hoot::Kmz80Cpu cpu;
    cpu.set_memory_callbacks(
        [&memory](uint16_t address) { return memory[address]; },
        [&memory](uint16_t address, uint8_t data) { memory[address] = data; });
    cpu.reset(0x0000);
    cpu.execute(200);
    if (cpu.interrupt_page() != 0x90) {
        std::cerr << "KMZ80 I register was corrupted by refresh state: 0x"
                  << std::hex << static_cast<int>(cpu.interrupt_page()) << std::dec << "\n";
        return false;
    }
    return true;
}

bool run_extended_code_and_restart_case(const std::string& packs)
{
    hoot::HootEntry entry;
    entry.id = "synthetic-pc88-extcode";
    entry.title = entry.id;
    entry.driver_name = "pc88/opn";
    entry.archive = "pc88_generic_synthetic";
    entry.options["init_pc"] = 0x8000;
    entry.options["mdata_addr"] = 0x4000;
    entry.options["mdata_size"] = 0x3000;
    entry.assets.push_back({"code", "PATCH_EXT", {}, 0x8000, 0, false});
    entry.assets.push_back({"bgm", "BGM.BIN", {}, 1, 0, false});
    // The low byte selects asset slot 1. Falcom-style patches consume the
    // fourth code byte through PC-88 host port 80h; value 08 selects SSG
    // volume register 8 in this fixture.
    entry.tracks.push_back({0x08000001u, "extended code tone", {}});

    auto driver = hoot::DriverRegistry::instance().create(entry);
    if (!driver) return false;
    std::string error;
    if (driver->load(entry, packs, 44100, error) != HOOT_OK) {
        std::cerr << "extended-code load failed: " << error << "\n";
        return false;
    }

    for (int pass = 0; pass < 2; ++pass) {
        if (driver->select_track(entry, 0, error) != HOOT_OK) {
            std::cerr << "extended-code select failed on pass " << pass << ": " << error << "\n";
            return false;
        }
        std::vector<int16_t> audio(22050 * 2);
        driver->render_s16(audio.data(), 22050);
        int peak = 0;
        for (auto sample : audio) peak = std::max(peak, std::abs(static_cast<int>(sample)));
        if (peak < 100) {
            std::cerr << "port-80/restart regression on pass " << pass << ", peak=" << peak << "\n";
            return false;
        }
    }
    return true;
}

bool run_case(const std::string& packs, const char* driver_name, bool opna)
{
    hoot::HootEntry entry;
    entry.id = opna ? "synthetic-pc88-opna" : "synthetic-pc88-opn";
    entry.title = entry.id;
    entry.driver_name = driver_name;
    entry.archive = "pc88_generic_synthetic";
    entry.options["init_pc"] = 0x8000;
    entry.options["baseclock"] = 12;
    entry.options["mdata_addr"] = 0x4000;
    entry.options["mdata_size"] = 0x3000; // deliberately > old 8 KiB limit
    entry.options["vdata_addr"] = 0x7000;
    entry.options["vdata_size"] = 0x100;
    entry.assets.push_back({"code", "PATCH", {}, 0x8000, 0, false});
    entry.assets.push_back({"bgm", "BGM.BIN", {}, 1, 0, false});
    entry.assets.push_back({"voice", "VOICE.BIN", {}, 0, 0, false});
    if (opna) {
        entry.assets.push_back({"adpcm", "ADPCM.BIN", {}, 0x200, 0, false});
    }
    entry.tracks.push_back({1, "synthetic tone", {}});

    const auto probe = hoot::DriverRegistry::instance().probe(entry);
    if (probe.status != hoot::DriverSupportStatus::Experimental || probe.driver_id != "pc88-generic") {
        std::cerr << entry.id << ": registry did not select generic PC-88 host: "
                  << hoot::driver_support_status_name(probe.status) << " " << probe.driver_id << "\n";
        return false;
    }

    auto driver = hoot::DriverRegistry::instance().create(entry);
    if (!driver) return false;
    std::string error;
    if (driver->load(entry, packs, 44100, error) != HOOT_OK) {
        std::cerr << entry.id << ": load failed: " << error << "\n";
        return false;
    }
    if (driver->select_track(entry, 0, error) != HOOT_OK) {
        std::cerr << entry.id << ": select failed: " << error << "\n";
        return false;
    }
    std::vector<int16_t> audio(44100 * 2);
    if (driver->render_s16(audio.data(), 44100) != 44100) return false;
    int peak = 0;
    size_t nonzero = 0;
    for (auto s : audio) {
        peak = std::max(peak, std::abs(static_cast<int>(s)));
        if (s != 0) ++nonzero;
    }
    HootTrackInfo info{};
    driver->fill_track_info(entry, 0, info);
    HootVisualState visual{};
    driver->fill_visual_state(entry, 0, visual);
    if (std::string(visual.architecture) != "PC-88" || visual.channel_count < 6
        || visual.register_count < 8 || visual.driver_work_size != 0) {
        std::cerr << entry.id << ": incomplete visual telemetry architecture="
                  << visual.architecture << " channels=" << visual.channel_count
                  << " regs=" << visual.register_count
                  << " work=" << visual.driver_work_size << "\n";
        return false;
    }
    if (peak < 100 || nonzero < 1000 || info.debug_opn_writes < 8 || info.debug_pc < 0x8000) {
        std::cerr << entry.id << ": bad output peak=" << peak << " nonzero=" << nonzero
                  << " writes=" << info.debug_opn_writes << " pc=0x" << std::hex << info.debug_pc << std::dec << "\n";
        return false;
    }

    // Host-side channel mute must silence voices without stopping the guest
    // sequencer. Mute every published voice, render forward, then clear and
    // verify that later key/activity becomes audible again.
    for (uint32_t i = 0; i < visual.channel_count; ++i) {
        const auto& ch = visual.channels[i];
        if (driver->channel_mute_supported(ch.kind, ch.index))
            driver->set_channel_muted(ch.kind, ch.index, true);
    }
    // Let the SSG DC-blocker's short filter tail settle before measuring.
    std::vector<int16_t> settle_audio(4096 * 2);
    driver->render_s16(settle_audio.data(), 4096);
    std::vector<int16_t> muted_audio(4096 * 2);
    driver->render_s16(muted_audio.data(), 4096);
    int muted_peak = 0;
    for (auto sample : muted_audio) muted_peak = std::max(muted_peak, std::abs(static_cast<int>(sample)));
    if (muted_peak > 512) {
        std::cerr << entry.id << ": channel mute leaked audio peak=" << muted_peak << "\n";
        return false;
    }
    driver->clear_channel_mutes();
    std::vector<int16_t> unmuted_audio(8192 * 2);
    driver->render_s16(unmuted_audio.data(), 8192);
    int unmuted_peak = 0;
    for (auto sample : unmuted_audio) unmuted_peak = std::max(unmuted_peak, std::abs(static_cast<int>(sample)));
    if (unmuted_peak < 100) {
        std::cerr << entry.id << ": audio did not recover after clear_channel_mutes peak=" << unmuted_peak << "\n";
        return false;
    }
    return true;
}

} // namespace

int main(int argc, char** argv)
{
    if (argc != 2) {
        std::cerr << "usage: pc88_generic_driver_test PACKS_DIR\n";
        return 2;
    }
    bool ok = run_kmz80_i_register_case();
    ok &= run_case(argv[1], "pc88/opn", false);
    ok &= run_case(argv[1], "pc88/opna", true);
    ok &= run_extended_code_and_restart_case(argv[1]);
    return ok ? 0 : 1;
}
