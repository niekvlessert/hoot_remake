#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include "config/hoot_catalog.h"
#include "drivers/driver_registry.h"

#if defined(_WIN32)
static void set_env(const char* name, const char* value) { _putenv_s(name, value); }
#else
static void set_env(const char* name, const char* value) { setenv(name, value, 1); }
#endif

int main(int argc, char** argv) {
    if (argc != 3) {
        std::cerr << "usage: x68k_mxdrv_driver_test PACKS_DIR MOCK_MDXMINI\n";
        return 2;
    }
    set_env("HOOT_MDXMINI_LIBRARY", argv[2]);

    hoot::HootEntry e;
    e.id = "synthetic-x68k-mxdrv";
    e.title = "Synthetic MXDRV";
    e.driver_name = "x68k/mxdrv";
    e.archive = "x68k_mxdrv_synthetic";
    e.assets.push_back({"data", "ONE.MDX", {}, 1, 0, false});
    e.assets.push_back({"data", "TWO.mdx", {}, 2, 0, false});
    e.assets.push_back({"data", "BANK.PDX", {}, 0xffffffffu, 0, false});
    e.tracks.push_back({1, "one", {}});
    e.tracks.push_back({2, "two", {}});

    auto probe = hoot::DriverRegistry::instance().probe(e);
    assert(probe.status == hoot::DriverSupportStatus::Experimental);
    assert(probe.driver_id == "x68k-mxdrv-mdxmini");

    auto d = hoot::DriverRegistry::instance().create(e);
    assert(d);
    std::string error;
    assert(d->load(e, argv[1], 48000, error) == HOOT_OK);
    assert(d->select_track(e, 1, error) == HOOT_OK);

    std::vector<int16_t> audio(1024 * 2);
    assert(d->render_s16(audio.data(), 1024) == 1024);
    int peak = 0;
    for (auto s : audio) peak = std::max(peak, std::abs(static_cast<int>(s)));
    assert(peak >= 1200);
    // Verify the full stereo buffer was written; this catches passing frame
    // counts to mdxmini where its ABI requires byte counts.
    assert(audio[audio.size() - 2] != 0);
    assert(audio[audio.size() - 1] != 0);

    HootTrackInfo info{};
    d->fill_track_info(e, 1, info);
    assert(std::string(info.driver) == "x68k-mxdrv-mdxmini");
    assert(info.sample_rate == 48000);
    return 0;
}
