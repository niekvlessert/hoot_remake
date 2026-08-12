#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "config/hoot_catalog.h"
#include "cpu/kmz80_cpu.h"
#include "drivers/driver_registry.h"
#include "sound/libvgm_ym2608.h"

namespace {

bool run_pcmx8_memory_mode_case()
{
    hoot::LibvgmYm2608 chip;
    if (!chip.initialize(7987200, 44100)) return false;

    std::array<uint8_t, 64> adpcm{};
    adpcm[4] = 0x14;
    adpcm[32] = 0x82;
    chip.allocate_adpcm_memory(static_cast<uint32_t>(adpcm.size()));
    chip.write_adpcm_memory(0, adpcm.data(), static_cast<uint32_t>(adpcm.size()));

    auto write_adpcm = [&chip](uint8_t reg, uint8_t data) {
        chip.write(2, reg);
        chip.write(3, data);
    };
    auto read_at_start_one = [&chip, &write_adpcm]() {
        write_adpcm(0x02, 0x01);
        write_adpcm(0x03, 0x00);
        write_adpcm(0x04, 0x02);
        write_adpcm(0x05, 0x00);
        write_adpcm(0x00, 0x20);
        chip.write(2, 0x08);
        (void)chip.read(3);
        (void)chip.read(3);
        return chip.read(3);
    };

    chip.set_adpcm_memory_x8(true);
    const uint8_t x8_value = read_at_start_one();
    chip.set_adpcm_memory_x8(false);
    const uint8_t x1_value = read_at_start_one();
    if (x8_value != 0x82 || x1_value != 0x14) {
        std::cerr << "PCMx8 ADPCM address mode mismatch: x8=0x" << std::hex
                  << static_cast<int>(x8_value) << " x1=0x"
                  << static_cast<int>(x1_value) << std::dec << "\n";
        return false;
    }
    return true;
}


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

bool run_n88_rom_and_gvram_case(const std::string& packs)
{
    std::vector<uint8_t> payload;
    auto emit = [&payload](std::initializer_list<uint8_t> bytes) {
        payload.insert(payload.end(), bytes.begin(), bytes.end());
    };
    std::vector<size_t> fail_jumps;
    auto emit_fail_jump = [&]() {
        emit({0xc2, 0x00, 0x00}); // JP NZ,fail
        fail_jumps.push_back(payload.size() - 2);
    };

    // Preserve distinct values in work RAM and two GVRAM planes, then switch
    // among them through the real PC-88 5ch-5fh bank-select ports.
    emit({0x3e, 0x11, 0x32, 0x00, 0xc0});
    emit({0xaf, 0xd3, 0x5c, 0x3a, 0x00, 0xc0, 0xfe, 0x00});
    emit_fail_jump();
    emit({0x3e, 0x22, 0x32, 0x00, 0xc0});
    emit({0xaf, 0xd3, 0x5d, 0x3a, 0x00, 0xc0, 0xfe, 0x00});
    emit_fail_jump();
    emit({0x3e, 0x33, 0x32, 0x00, 0xc0});
    emit({0xaf, 0xd3, 0x5c, 0x3a, 0x00, 0xc0, 0xfe, 0x22});
    emit_fail_jump();
    emit({0xaf, 0xd3, 0x5f, 0x3a, 0x00, 0xc0, 0xfe, 0x11});
    emit_fail_jump();

    // Success installs an IM2 vector for RTC line 2 (low byte 04h), programs
    // the PC-88 E4/E6 interrupt controller and waits. The ISR, not foreground
    // code, emits the tone, so silence means RTC delivery or vectoring failed.
    emit({0x31, 0x00, 0xff}); // LD SP,ff00h
    emit({0x3e, 0x00});
    const size_t isr_low = payload.size() - 1;
    emit({0x32, 0x04, 0x20});
    emit({0x3e, 0x00});
    const size_t isr_high = payload.size() - 1;
    emit({0x32, 0x05, 0x20});
    emit({0x3e, 0x20, 0xed, 0x47}); // LD I,A
    emit({0x3e, 0x05, 0xd3, 0xe4}); // admit controller lines 0..4
    emit({0x3e, 0x01, 0xd3, 0xe6}); // RTC enabled, VRTC disabled
    emit({0xed, 0x5e, 0xfb, 0x76, 0x18, 0xfd}); // IM 2 / EI / HALT loop

    const uint16_t fail_address = static_cast<uint16_t>(0x0100 + payload.size());
    emit({0x18, 0xfe});
    const uint16_t isr_address = static_cast<uint16_t>(0x0100 + payload.size());
    payload[isr_low] = static_cast<uint8_t>(isr_address & 0xff);
    payload[isr_high] = static_cast<uint8_t>(isr_address >> 8);
    emit({0xaf, 0xd3, 0x44, 0x3e, 0x40, 0xd3, 0x45});
    emit({0x3e, 0x01, 0xd3, 0x44, 0xaf, 0xd3, 0x45});
    emit({0x3e, 0x07, 0xd3, 0x44, 0x3e, 0x3e, 0xd3, 0x45});
    emit({0x3e, 0x08, 0xd3, 0x44, 0x3e, 0x0f, 0xd3, 0x45});
    emit({0xfb, 0xed, 0x4d}); // EI / RETI
    for (const size_t operand : fail_jumps) {
        payload[operand] = static_cast<uint8_t>(fail_address & 0xff);
        payload[operand + 1] = static_cast<uint8_t>(fail_address >> 8);
    }

    std::array<uint8_t, 0x8000> rom{};
    rom.fill(0xff);
    // The reset code copies the test to underlying RAM, installs a JP at the
    // instruction immediately after OUT (31h), then selects full-RAM mode.
    // Reaching the payload therefore proves both ROM fetch and RAM write-through.
    const std::array<uint8_t, 30> boot{{
        0xf3,
        0x21, 0x00, 0x02,
        0x11, 0x00, 0x01,
        0x01, static_cast<uint8_t>(payload.size()), 0x00,
        0xed, 0xb0,
        0x3e, 0xc3, 0x32, 0x1e, 0x00,
        0xaf, 0x32, 0x1f, 0x00,
        0x3e, 0x01, 0x32, 0x20, 0x00,
        0x3e, 0x02, 0xd3, 0x31,
    }};
    std::copy(boot.begin(), boot.end(), rom.begin());
    std::copy(payload.begin(), payload.end(), rom.begin() + 0x0200);

    const auto temp = std::filesystem::temp_directory_path() / "hoot_pc88_n88_gvram_test";
    std::error_code fs_error;
    std::filesystem::remove_all(temp, fs_error);
    std::filesystem::create_directories(temp, fs_error);
    if (fs_error) {
        std::cerr << "unable to create PC-88 test directory: " << fs_error.message() << "\n";
        return false;
    }
    std::filesystem::copy_file(
        std::filesystem::path(packs) / "pc88_generic_synthetic.zip",
        temp / "pc88_generic_synthetic.zip",
        std::filesystem::copy_options::overwrite_existing, fs_error);
    if (fs_error) {
        std::cerr << "unable to copy PC-88 test archive: " << fs_error.message() << "\n";
        return false;
    }
    {
        std::ofstream output(temp / "N88.ROM", std::ios::binary);
        output.write(reinterpret_cast<const char*>(rom.data()), static_cast<std::streamsize>(rom.size()));
    }

    hoot::HootEntry entry;
    entry.id = "synthetic-pc88-n88-gvram";
    entry.title = entry.id;
    entry.driver_name = "pc88/opn";
    entry.archive = "pc88_generic_synthetic";
    entry.options["init_pc"] = 0;
    entry.options["use_n88rom"] = 1;
    entry.options["use_gvram"] = 1;
    entry.options["use_rtc"] = 1;
    entry.tracks.push_back({0, "ROM and GVRAM test", {}});

    bool passed = false;
    {
        auto driver = hoot::DriverRegistry::instance().create(entry);
        std::string error;
        if (driver && driver->load(entry, temp.string(), 44100, error) == HOOT_OK
            && driver->select_track(entry, 0, error) == HOOT_OK) {
            std::vector<int16_t> audio(22050 * 2);
            driver->render_s16(audio.data(), 22050);
            int peak = 0;
            for (const auto sample : audio) peak = std::max(peak, std::abs(static_cast<int>(sample)));
            passed = peak >= 100;
            if (!passed) std::cerr << "N88 ROM/GVRAM test stayed silent, peak=" << peak << "\n";
        } else {
            std::cerr << "N88 ROM/GVRAM driver setup failed: " << error << "\n";
        }
    }
    std::filesystem::remove_all(temp, fs_error);
    return passed;
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
    entry.options["use_ssgpcm"] = opna ? 0 : 1;
    if (opna) entry.options["use_pcmx8"] = 1;
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
    if (probe.reason.find("helper path is not yet implemented") != std::string::npos) {
        std::cerr << entry.id << ": helper was still reported as unimplemented: "
                  << probe.reason << "\n";
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
    ok &= run_pcmx8_memory_mode_case();
    ok &= run_case(argv[1], "pc88/opn", false);
    ok &= run_case(argv[1], "pc88/opna", true);
    ok &= run_extended_code_and_restart_case(argv[1]);
    ok &= run_n88_rom_and_gvram_case(argv[1]);
    return ok ? 0 : 1;
}
