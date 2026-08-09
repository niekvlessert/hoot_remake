#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include "config/hoot_catalog.h"
#include "drivers/driver_registry.h"

namespace {

bool run_case(const char* packs_dir, const std::string& driver_name, const std::string& expected_id)
{
    hoot::HootEntry entry;
    entry.id = "synthetic-" + driver_name;
    entry.title = entry.id;
    entry.driver_name = driver_name;
    entry.archive = "pc98dos_generic_synthetic";
    entry.options["clockmul"] = 14; // ensure non-default catalog timing is accepted
    entry.assets.push_back({"file", "synthetic.com", {}, UINT32_MAX, 0, false});
    entry.assets.push_back({"file", "track.dat", {}, 1, 0, false});
    entry.assets.push_back({"file", "track.dat", {}, 8, 0, false});
    entry.assets.push_back({"shell", "synthetic", {}, 0, 0, false});
    entry.tracks.push_back({1, "synthetic tone", {}});
    // Hoot range/effect codes use the high byte as the file slot and the low
    // byte as the resident-driver command (e.g. 0801h -> slot 08h, command 01h).
    entry.tracks.push_back({0x0801, "synthetic extended effect", {}});

    const auto probe = hoot::DriverRegistry::instance().probe(entry);
    if (probe.status != hoot::DriverSupportStatus::Experimental
        || probe.driver_id != expected_id) {
        std::cerr << "registry mismatch for " << driver_name << ": "
                  << hoot::driver_support_status_name(probe.status)
                  << " " << probe.driver_id << "\n";
        return false;
    }
    auto driver = hoot::DriverRegistry::instance().create(entry);
    if (!driver) return false;
    std::string error;
    if (driver->load(entry, packs_dir, 44100, error) != HOOT_OK) {
        std::cerr << "load failed for " << driver_name << ": " << error << "\n";
        return false;
    }
    if (driver->select_track(entry, 0, error) != HOOT_OK) {
        std::cerr << "select failed for " << driver_name << ": " << error << "\n";
        return false;
    }
    // Verify the extended effect-code fallback before rendering the normal
    // synthetic tone. The shell program is resident, so returning to track 0
    // after this also exercises deterministic track switching.
    if (driver->select_track(entry, 1, error) != HOOT_OK) {
        std::cerr << "extended-code select failed for " << driver_name << ": " << error << "\n";
        return false;
    }
    if (driver->select_track(entry, 0, error) != HOOT_OK) {
        std::cerr << "reselect failed for " << driver_name << ": " << error << "\n";
        return false;
    }
    std::vector<int16_t> audio(44100 * 2);
    driver->render_s16(audio.data(), 44100);
    int peak = 0;
    size_t nonzero = 0;
    for (auto v : audio) {
        peak = std::max(peak, std::abs(static_cast<int>(v)));
        if (v) ++nonzero;
    }
    HootTrackInfo info{};
    driver->fill_track_info(entry, 0, info);
    HootVisualState visual{};
    driver->fill_visual_state(entry, 0, visual);
    if (std::string(visual.architecture).rfind("PC-98", 0) != 0 || visual.channel_count < 6
        || visual.register_count < 10 || visual.driver_work_size != 0) {
        std::cerr << "incomplete " << driver_name << " visual telemetry architecture="
                  << visual.architecture << " channels=" << visual.channel_count
                  << " regs=" << visual.register_count
                  << " work=" << visual.driver_work_size << "\n";
        return false;
    }
    if (peak < 100 || nonzero < 1000 || info.debug_opn_writes < 4
        || info.debug_unsupported_opcodes != 0) {
        std::cerr << "bad " << driver_name << " render peak=" << peak
                  << " nonzero=" << nonzero
                  << " writes=" << info.debug_opn_writes
                  << " unsupported=" << info.debug_unsupported_opcodes
                  << " opcode=" << info.debug_last_unsupported_opcode
                  << " cs=" << info.debug_last_unsupported_cs
                  << " ip=" << info.debug_last_unsupported_ip << "\n";
        return false;
    }
    return true;
}

} // namespace

int main(int argc, char** argv)
{
    if (argc != 2) {
        std::cerr << "usage: pc98dos_generic_driver_test PACKS_DIR\n";
        return 2;
    }
    if (!run_case(argv[1], "pc98dos/opn", "pc98dos-v30-opn")) return 1;
    if (!run_case(argv[1], "pc98dos/opna", "pc98dos-v30-opna")) return 1;
    if (!run_case(argv[1], "pc98dos/86", "pc98dos-v30-86")) return 1;
    if (!run_case(argv[1], "pc9821dos/opn", "pc9821dos-386-opn")) return 1;
    if (!run_case(argv[1], "pc9821dos/opna", "pc9821dos-386-opna")) return 1;
    if (!run_case(argv[1], "pc9821dos/86", "pc9821dos-386-86")) return 1;

    // Hoot's SS_98/MDRV_98 style bridge reads a DOS filename from stdin,
    // not the selected file body, and installs its actual playback clock on a
    // resident interrupt (SS uses INT 14h).  This synthetic helper emits a PSG
    // tone only if it receives "track.dat" and only from INT 14h, protecting
    // both pieces of the legacy helper ABI used by real PC-98 OPN packs.
    hoot::HootEntry filename_entry;
    filename_entry.id = "synthetic-pc98dos-filename-bridge";
    filename_entry.title = filename_entry.id;
    filename_entry.driver_name = "pc98dos/opn";
    filename_entry.archive = "pc98dos_filename_bridge_synthetic";
    filename_entry.assets.push_back({"file", "SS.COM", {}, UINT32_MAX, 0, false});
    filename_entry.assets.push_back({"file", "SS_98.COM", {}, UINT32_MAX, 0, false});
    filename_entry.assets.push_back({"file", "track.dat", {}, 1, 0, false});
    filename_entry.assets.push_back({"conin", "track.dat", {}, 1, 0, false});
    filename_entry.assets.push_back({"shell", "SS", {}, 0, 0, false});
    filename_entry.assets.push_back({"shell", "SS_98", {}, 0, 0, false});
    filename_entry.tracks.push_back({1, "filename bridge tone", {}});
    auto filename_driver = hoot::DriverRegistry::instance().create(filename_entry);
    if (!filename_driver) return 1;
    std::string filename_error;
    if (filename_driver->load(filename_entry, argv[1], 44100, filename_error) != HOOT_OK
        || filename_driver->select_track(filename_entry, 0, filename_error) != HOOT_OK) {
        std::cerr << "filename bridge fixture setup failed: " << filename_error << "\n";
        return 1;
    }
    std::vector<int16_t> filename_audio(44100 * 2);
    filename_driver->render_s16(filename_audio.data(), 44100);
    int filename_peak = 0;
    size_t filename_nonzero = 0;
    for (const auto v : filename_audio) {
        filename_peak = std::max(filename_peak, std::abs(static_cast<int>(v)));
        if (v != 0) ++filename_nonzero;
    }
    HootTrackInfo filename_info{};
    filename_driver->fill_track_info(filename_entry, 0, filename_info);
    if (filename_peak < 100 || filename_nonzero < 1000
        || filename_info.debug_opn_writes < 4
        || filename_info.debug_unsupported_opcodes != 0) {
        std::cerr << "bad filename/INT14 bridge render peak=" << filename_peak
                  << " nonzero=" << filename_nonzero
                  << " writes=" << filename_info.debug_opn_writes
                  << " unsupported=" << filename_info.debug_unsupported_opcodes << "\n";
        return 1;
    }

    // MDRV-style residents use the YM2203 timer overflow through INT 0Bh
    // rather than the 60 Hz bridge vector.  This helper starts Timer B only
    // after receiving the filename and emits its PSG tone from INT 0Bh.
    hoot::HootEntry timer_entry;
    timer_entry.id = "synthetic-pc98dos-timer-bridge";
    timer_entry.title = timer_entry.id;
    timer_entry.driver_name = "pc98dos/opn";
    timer_entry.archive = "pc98dos_timer_bridge_synthetic";
    timer_entry.assets.push_back({"file", "MDRV.COM", {}, UINT32_MAX, 0, false});
    timer_entry.assets.push_back({"file", "MDRV_98.COM", {}, UINT32_MAX, 0, false});
    timer_entry.assets.push_back({"file", "track.dat", {}, 1, 0, false});
    timer_entry.assets.push_back({"conin", "track.dat", {}, 1, 0, false});
    timer_entry.assets.push_back({"shell", "MDRV", {}, 0, 0, false});
    timer_entry.assets.push_back({"shell", "MDRV_98", {}, 0, 0, false});
    timer_entry.tracks.push_back({1, "YM timer bridge tone", {}});
    auto timer_driver = hoot::DriverRegistry::instance().create(timer_entry);
    if (!timer_driver) return 1;
    std::string timer_error;
    if (timer_driver->load(timer_entry, argv[1], 44100, timer_error) != HOOT_OK
        || timer_driver->select_track(timer_entry, 0, timer_error) != HOOT_OK) {
        std::cerr << "timer bridge fixture setup failed: " << timer_error << "\n";
        return 1;
    }
    std::vector<int16_t> timer_audio(44100 * 2);
    timer_driver->render_s16(timer_audio.data(), 44100);
    int timer_peak = 0;
    size_t timer_nonzero = 0;
    for (const auto v : timer_audio) {
        timer_peak = std::max(timer_peak, std::abs(static_cast<int>(v)));
        if (v != 0) ++timer_nonzero;
    }
    HootTrackInfo timer_info{};
    timer_driver->fill_track_info(timer_entry, 0, timer_info);
    // The fixture programs YM2203 Timer B=0 at the reset/default 1/6
    // prescaler. That is about 13.54 IRQs/s. Each IRQ performs five OPN data
    // writes, in addition to the two setup writes, so one second must stay
    // around 67 writes. A reset selector of 0 (/2) produces about 202 writes
    // and makes real timer-driven packs such as 3x3EYE'S play 3x too fast.
    if (timer_peak < 100 || timer_nonzero < 1000
        || timer_info.debug_opn_writes < 60
        || timer_info.debug_opn_writes > 80
        || timer_info.debug_unsupported_opcodes != 0) {
        std::cerr << "bad YM timer/INT0B bridge render peak=" << timer_peak
                  << " nonzero=" << timer_nonzero
                  << " writes=" << timer_info.debug_opn_writes
                  << " unsupported=" << timer_info.debug_unsupported_opcodes << "\n";
        return 1;
    }

    // YM2608 has an extra /2 input stage in its OPN timer block. The same
    // register values must consequently produce the same wall-clock cadence
    // as YM2203, not twice the IRQ rate.
    timer_entry.id = "synthetic-pc98dos-opna-timer-bridge";
    timer_entry.driver_name = "pc98dos/opna";
    auto opna_timer_driver = hoot::DriverRegistry::instance().create(timer_entry);
    if (!opna_timer_driver) return 1;
    std::string opna_timer_error;
    if (opna_timer_driver->load(timer_entry, argv[1], 44100, opna_timer_error) != HOOT_OK
        || opna_timer_driver->select_track(timer_entry, 0, opna_timer_error) != HOOT_OK) {
        std::cerr << "OPNA timer bridge fixture setup failed: " << opna_timer_error << "\n";
        return 1;
    }
    std::vector<int16_t> opna_timer_audio(44100 * 2);
    opna_timer_driver->render_s16(opna_timer_audio.data(), 44100);
    int opna_timer_peak = 0;
    size_t opna_timer_nonzero = 0;
    for (const auto v : opna_timer_audio) {
        opna_timer_peak = std::max(opna_timer_peak, std::abs(static_cast<int>(v)));
        if (v != 0) ++opna_timer_nonzero;
    }
    HootTrackInfo opna_timer_info{};
    opna_timer_driver->fill_track_info(timer_entry, 0, opna_timer_info);
    if (opna_timer_peak < 100 || opna_timer_nonzero < 1000
        || opna_timer_info.debug_opn_writes < 60
        || opna_timer_info.debug_opn_writes > 80
        || opna_timer_info.debug_unsupported_opcodes != 0) {
        std::cerr << "bad YM2608 timer cadence render peak=" << opna_timer_peak
                  << " nonzero=" << opna_timer_nonzero
                  << " writes=" << opna_timer_info.debug_opn_writes
                  << " unsupported=" << opna_timer_info.debug_unsupported_opcodes << "\n";
        return 1;
    }

    // Separate PCM-only fixture proves the 86-board path itself, rather than
    // merely observing the YM2608 that shares the board.
    hoot::HootEntry pcm_entry;
    pcm_entry.id = "synthetic-pc98dos-86-pcm";
    pcm_entry.title = pcm_entry.id;
    pcm_entry.driver_name = "pc98dos/86";
    pcm_entry.archive = "pc98dos_pcm86_synthetic";
    pcm_entry.assets.push_back({"file", "synthetic86.com", {}, UINT32_MAX, 0, false});
    pcm_entry.assets.push_back({"file", "track.dat", {}, 1, 0, false});
    pcm_entry.assets.push_back({"shell", "synthetic86", {}, 0, 0, false});
    pcm_entry.tracks.push_back({1, "synthetic 86PCM", {}});
    auto pcm_driver = hoot::DriverRegistry::instance().create(pcm_entry);
    if (!pcm_driver) return 1;
    std::string pcm_error;
    if (pcm_driver->load(pcm_entry, argv[1], 44100, pcm_error) != HOOT_OK
        || pcm_driver->select_track(pcm_entry, 0, pcm_error) != HOOT_OK) {
        std::cerr << "PCM86 fixture setup failed: " << pcm_error << "\n";
        return 1;
    }
    std::vector<int16_t> pcm_audio(4096 * 2);
    pcm_driver->render_s16(pcm_audio.data(), 4096);
    int pcm_peak = 0;
    size_t pcm_nonzero = 0;
    for (const auto v : pcm_audio) {
        pcm_peak = std::max(pcm_peak, std::abs(static_cast<int>(v)));
        if (v != 0) ++pcm_nonzero;
    }
    HootTrackInfo pcm_info{};
    pcm_driver->fill_track_info(pcm_entry, 0, pcm_info);
    if (pcm_peak < 1000 || pcm_nonzero < 1000
        || pcm_info.debug_pcm86_fifo_writes < 4000
        || pcm_info.debug_pcm86_fifo_reads < 4000
        || pcm_info.debug_pcm86_rendered_source_frames < 2000
        || pcm_info.debug_pcm86_fifo_overflows != 0
        || pcm_info.debug_unsupported_opcodes != 0) {
        std::cerr << "bad PCM86 render peak=" << pcm_peak
                  << " nonzero=" << pcm_nonzero
                  << " writes=" << pcm_info.debug_pcm86_fifo_writes
                  << " reads=" << pcm_info.debug_pcm86_fifo_reads
                  << " source_frames=" << pcm_info.debug_pcm86_rendered_source_frames
                  << " overflows=" << pcm_info.debug_pcm86_fifo_overflows
                  << " unsupported=" << pcm_info.debug_unsupported_opcodes << "\n";
        return 1;
    }
    return 0;
}
