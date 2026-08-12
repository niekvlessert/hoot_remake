#include <cassert>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#include "drivers/driver_registry.h"

int main(int argc, char** argv)
{
    if (argc != 2) {
        std::cerr << "usage: sharp_x1_generic_driver_test FIXTURE_DIR\n";
        return 2;
    }

    for (const std::string type : {"psg", "opm", "opmx2", "opn"}) {
        hoot::HootEntry entry;
        entry.title = "Synthetic Sharp X1";
        entry.driver_name = "x1/" + type;
        entry.archive = "sharp_x1_synthetic";
        entry.options["init_pc"] = 0x100;
        entry.options["mdata_addr"] = 0x4000;
        entry.assets.push_back({"code", "PATCH", {}, 0, 0, false});
        entry.assets.push_back({"bgm", "SONG", {}, 1, 0, false});
        entry.assets.push_back({"data", "PORTS", {}, 0x5000, 0, false});
        entry.tracks.push_back({1, "Synthetic track", {}});

        auto driver = hoot::DriverRegistry::instance().create(entry);
        assert(driver);
        std::string error;
        assert(driver->load(entry, argv[1], 44100, error) == HOOT_OK);
        assert(driver->select_track(entry, 0, error) == HOOT_OK);
        std::vector<int16_t> audio(2048 * 2);
        assert(driver->render_s16(audio.data(), 2048) == 2048);

        HootTrackInfo info{};
        driver->fill_track_info(entry, 0, info);
        assert(info.debug_cpu_cycles > 0);
        assert(info.debug_io_writes >= 8);

        HootVisualState visual{};
        driver->fill_visual_state(entry, 0, visual);
        assert(std::string(visual.architecture) == "Sharp X1");
        assert(visual.channel_count >= 3);
    }
    return 0;
}
