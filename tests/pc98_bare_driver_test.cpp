#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include "config/hoot_catalog.h"
#include "drivers/driver_registry.h"

namespace {

bool run_case(const char* packs_dir, const std::string& driver_name,
              const std::string& code_path, uint32_t code_address, bool segmented,
              const std::string& expected_id, bool expect_beep = false,
              const std::string& expected_architecture = "PC-98")
{
    hoot::HootEntry entry;
    entry.id = "synthetic-" + driver_name;
    entry.title = entry.id;
    entry.driver_name = driver_name;
    entry.archive = "pc98_bare_synthetic";
    entry.options["bootcs"] = 0x0060;
    entry.options["bootip"] = 0;
    entry.options["funcvect"] = 0x7f;
    entry.options["dataaddr"] = 0x10000;
    entry.options["data2addr"] = 0x10100;
    entry.options["filesize"] = 0x100;
    entry.options["file2size"] = 0x100;
    if (segmented) entry.options["adressing"] = 1; // spelling used by legacy PC98VX catalogue
    entry.assets.push_back({"code", code_path, {}, code_address, 0, false});
    entry.assets.push_back({"bgm", "bgm.dat", {}, 1, 0, false});
    entry.assets.push_back({"bgm2", "voice.dat", {}, 1, 0, false});
    entry.tracks.push_back({0x00010001, "dual-buffer synthetic tone", {}});

    const auto probe = hoot::DriverRegistry::instance().probe(entry);
    if (probe.status != hoot::DriverSupportStatus::Experimental || probe.driver_id != expected_id) {
        std::cerr << "registry mismatch: " << hoot::driver_support_status_name(probe.status)
                  << " " << probe.driver_id << "\n";
        return false;
    }
    auto driver = hoot::DriverRegistry::instance().create(entry);
    if (!driver) return false;
    std::string error;
    if (driver->load(entry, packs_dir, 44100, error) != HOOT_OK) {
        std::cerr << "bare load failed: " << error << "\n";
        return false;
    }
    if (driver->select_track(entry, 0, error) != HOOT_OK) {
        std::cerr << "bare select failed: " << error << "\n";
        return false;
    }
    std::vector<int16_t> audio(44100 * 2);
    driver->render_s16(audio.data(), 44100);
    int peak = 0;
    size_t nonzero = 0;
    for (auto v : audio) {
        peak = std::max(peak, std::abs(static_cast<int>(v)));
        if (v != 0) ++nonzero;
    }
    HootTrackInfo info{};
    driver->fill_track_info(entry, 0, info);
    HootVisualState visual{};
    driver->fill_visual_state(entry, 0, visual);
    const bool activity_ok = expect_beep
        ? (info.debug_beep_audible_frames > 1000 && info.debug_beep_pit_data_writes >= 2)
        : (info.debug_opn_writes >= 4);
    const bool runtime_name_ok = expected_architecture != "PC-88VA"
        || std::string(driver->name()) == expected_id;
    if (peak < 100 || nonzero < 1000 || !activity_ok || info.debug_unsupported_opcodes != 0
        || visual.architecture != expected_architecture || !runtime_name_ok) {
        std::cerr << "bad bare render peak=" << peak << " nonzero=" << nonzero
                  << " writes=" << info.debug_opn_writes
                  << " beep_frames=" << info.debug_beep_audible_frames
                  << " beep_writes=" << info.debug_beep_pit_data_writes
                  << " unsupported=" << info.debug_unsupported_opcodes
                  << " opcode=" << info.debug_last_unsupported_opcode
                  << " cs=" << info.debug_last_unsupported_cs
                  << " ip=" << info.debug_last_unsupported_ip
                  << " architecture=" << visual.architecture
                  << " driver=" << driver->name() << "\n";
        return false;
    }
    return true;
}

} // namespace

bool run_dynamic_dataaddr_case(const char* packs_dir)
{
    hoot::HootEntry entry;
    entry.id = "synthetic-pc98vx-dynamic-data";
    entry.title = entry.id;
    entry.driver_name = "pc98vx/opn";
    entry.archive = "pc98_bare_synthetic";
    entry.options["bootcs"] = 0x0060;
    entry.options["bootip"] = 0;
    entry.options["funcvect"] = 0x7f;
    entry.assets.push_back({"code", "dynamic.bin", {}, 0x00000600, 0, false});
    entry.assets.push_back({"bgm", "bgm.dat", {}, 1, 0, false});
    entry.tracks.push_back({0x00000001, "dynamic Hoot BGM buffer", {}});

    const auto probe = hoot::DriverRegistry::instance().probe(entry);
    if (probe.status != hoot::DriverSupportStatus::Experimental
        || probe.reason.find("07D0h-07D7h") == std::string::npos) {
        std::cerr << "dynamic-buffer probe mismatch: "
                  << hoot::driver_support_status_name(probe.status) << " " << probe.reason << "\n";
        return false;
    }
    auto driver = hoot::DriverRegistry::instance().create(entry);
    if (!driver) return false;
    std::string error;
    if (driver->load(entry, packs_dir, 44100, error) != HOOT_OK) {
        std::cerr << "dynamic-buffer load failed: " << error << "\n";
        return false;
    }
    if (driver->select_track(entry, 0, error) != HOOT_OK) {
        std::cerr << "dynamic-buffer select failed: " << error << "\n";
        return false;
    }
    std::vector<int16_t> audio(44100 * 2);
    driver->render_s16(audio.data(), 44100);
    int peak = 0;
    size_t nonzero = 0;
    for (auto v : audio) {
        peak = std::max(peak, std::abs(static_cast<int>(v)));
        if (v != 0) ++nonzero;
    }
    HootTrackInfo info{};
    driver->fill_track_info(entry, 0, info);
    if (peak < 100 || nonzero < 1000 || info.debug_opn_writes < 4
        || info.debug_unsupported_opcodes != 0) {
        std::cerr << "dynamic Hoot loader did not produce audio: peak=" << peak
                  << " nonzero=" << nonzero << " writes=" << info.debug_opn_writes
                  << " unsupported=" << info.debug_unsupported_opcodes << "\n";
        return false;
    }
    return true;
}


bool run_pic14_timer_case(const char* packs_dir, const std::string& driver_name)
{
    hoot::HootEntry entry;
    entry.id = "synthetic-pic14-timer-" + driver_name;
    entry.title = entry.id;
    entry.driver_name = driver_name;
    entry.archive = "pc98_bare_synthetic";
    entry.options["bootcs"] = 0x0060;
    entry.options["bootip"] = 0;
    entry.options["funcvect"] = 0x7f;
    entry.assets.push_back({"code", "pic14.bin", {}, 0x00000600, 0, false});
    entry.tracks.push_back({0x00000000, "slave-PIC INT14 timer IRQ", {}});

    auto driver = hoot::DriverRegistry::instance().create(entry);
    if (!driver) return false;
    std::string error;
    if (driver->load(entry, packs_dir, 44100, error) != HOOT_OK
        || driver->select_track(entry, 0, error) != HOOT_OK) {
        std::cerr << "PIC14 timer start failed: " << error << "\n";
        return false;
    }
    std::vector<int16_t> audio(44100 * 2);
    driver->render_s16(audio.data(), 44100);
    int peak = 0;
    size_t nonzero = 0;
    for (auto v : audio) {
        peak = std::max(peak, std::abs(static_cast<int>(v)));
        if (v != 0) ++nonzero;
    }
    HootTrackInfo info{};
    driver->fill_track_info(entry, 0, info);
    if (peak < 100 || nonzero < 1000 || info.debug_unsupported_opcodes != 0) {
        std::cerr << "slave-PIC INT14 FM timer was not delivered: peak=" << peak
                  << " nonzero=" << nonzero
                  << " unsupported=" << info.debug_unsupported_opcodes << "\n";
        return false;
    }
    return true;
}

bool run_streamed_loader_case(const char* packs_dir)
{
    hoot::HootEntry entry;
    entry.id = "synthetic-pc98vx-streamed-loader";
    entry.title = entry.id;
    entry.driver_name = "pc98vx/opn";
    entry.archive = "pc98_bare_synthetic";
    entry.options["bootcs"] = 0x0060;
    entry.options["bootip"] = 0;
    entry.options["funcvect"] = 0x7f;
    entry.assets.push_back({"code", "streamed.bin", {}, 0x00000600, 0, false});
    entry.assets.push_back({"bgm", "bgm.dat", {}, 1, 0, false});
    entry.assets.push_back({"bgm2", "voice.dat", {}, 1, 0, false});
    entry.tracks.push_back({0x43210001, "direct/extended Hoot loaders", {}});

    auto driver = hoot::DriverRegistry::instance().create(entry);
    if (!driver) return false;
    std::string error;
    if (driver->load(entry, packs_dir, 44100, error) != HOOT_OK
        || driver->select_track(entry, 0, error) != HOOT_OK) {
        std::cerr << "streamed-loader start failed: " << error << "\n";
        return false;
    }
    std::vector<int16_t> audio(44100 * 2);
    driver->render_s16(audio.data(), 44100);
    int peak = 0;
    size_t nonzero = 0;
    for (auto v : audio) {
        peak = std::max(peak, std::abs(static_cast<int>(v)));
        if (v != 0) ++nonzero;
    }
    HootTrackInfo info{};
    driver->fill_track_info(entry, 0, info);
    if (peak < 100 || nonzero < 1000 || info.debug_opn_writes < 4
        || info.debug_unsupported_opcodes != 0) {
        std::cerr << "direct/extended Hoot loader failed: peak=" << peak
                  << " nonzero=" << nonzero << " writes=" << info.debug_opn_writes
                  << " unsupported=" << info.debug_unsupported_opcodes << "\n";
        return false;
    }
    return true;
}

int main(int argc, char** argv)
{
    if (argc != 2) {
        std::cerr << "usage: pc98_bare_driver_test FIXTURE_DIR\n";
        return 2;
    }
    if (!run_case(argv[1], "pc98/opn", "player.bin", 0x00000600, false, "pc98-bare-opn")) return 1;
    if (!run_case(argv[1], "pc98vx/opn", "player.bin", 0x00600000, true, "pc98vx-bare-opn")) return 1;
    if (!run_case(argv[1], "pc98/opna", "player.bin", 0x00000600, false, "pc98-bare-opna")) return 1;
    if (!run_case(argv[1], "pc98vx/86", "player.bin", 0x00000600, false, "pc98vx-bare-86")) return 1;
    if (!run_case(argv[1], "pc98vx/beep", "beep.bin", 0x00000600, false, "pc98vx-bare-beep", true)) return 1;
    if (!run_case(argv[1], "pc88va/opn", "player.bin", 0x00000600, false,
                  "pc88va-bare-opn", false, "PC-88VA")) return 1;
    if (!run_case(argv[1], "pc88va/opna", "player.bin", 0x00000600, false,
                  "pc88va-bare-opna", false, "PC-88VA")) return 1;
    if (!run_dynamic_dataaddr_case(argv[1])) return 1;
    if (!run_pic14_timer_case(argv[1], "pc98vx/opn")) return 1;
    if (!run_pic14_timer_case(argv[1], "pc88va/opn")) return 1;
    if (!run_streamed_loader_case(argv[1])) return 1;
    return 0;
}
