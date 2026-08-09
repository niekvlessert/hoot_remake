#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

#include "config/hoot_catalog.h"
#include "drivers/driver_registry.h"

namespace {
bool run_case(const char* packs, const std::string& driver_name, const std::string& shell,
              const std::string& expected_id, bool bare = false)
{
    hoot::HootEntry e;
    e.id = "special-" + driver_name;
    e.title = e.id;
    e.driver_name = driver_name;
    e.archive = "pc98_special_boards_synthetic";
    if (bare) {
        e.options["bootcs"] = 0x0060;
        e.options["bootip"] = 0;
        e.options["funcvect"] = 0; // tone is programmed by the boot stub itself
        e.assets.push_back({"code", shell + ".com", {}, 0x600, 0, false});
        // no BGM buffer: code-only legacy wrappers select an internal subsong
        e.tracks.push_back({1, "tone", {}});
    } else {
        e.assets.push_back({"file", shell + ".com", {}, UINT32_MAX, 0, false});
        e.assets.push_back({"file", "track.dat", {}, 1, 0, false});
        e.assets.push_back({"shell", shell, {}, 0, 0, false});
        e.tracks.push_back({1, "tone", {}});
    }
    const auto probe = hoot::DriverRegistry::instance().probe(e);
    if (probe.status != hoot::DriverSupportStatus::Experimental || probe.driver_id != expected_id) {
        std::cerr << "probe " << driver_name << " => " << hoot::driver_support_status_name(probe.status)
                  << " " << probe.driver_id << " " << probe.reason << "\n";
        return false;
    }
    auto d = hoot::DriverRegistry::instance().create(e);
    if (!d) return false;
    std::string error;
    if (d->load(e, packs, 44100, error) != HOOT_OK) {
        std::cerr << "load " << driver_name << ": " << error << "\n";
        return false;
    }
    if (d->select_track(e, 0, error) != HOOT_OK) {
        std::cerr << "select " << driver_name << ": " << error << "\n";
        return false;
    }
    std::vector<int16_t> audio(8192 * 2);
    d->render_s16(audio.data(), 8192);
    int peak=0; size_t nonzero=0;
    for (auto v:audio) { peak=std::max(peak,std::abs((int)v)); if(v) ++nonzero; }
    HootTrackInfo info{}; d->fill_track_info(e,0,info);
    if (peak < 100 || nonzero < 1000 || info.debug_unsupported_opcodes != 0) {
        std::cerr << "render " << driver_name << " peak=" << peak << " nz=" << nonzero
                  << " unsupported=" << info.debug_unsupported_opcodes << "\n";
        return false;
    }
    return true;
}
}

int main(int argc,char**argv)
{
    if(argc!=2) return 2;
    if(!run_case(argv[1],"pc98dos/soundorchestra","so","pc98dos-v30-soundorchestra")) return 1;
    if(!run_case(argv[1],"pc98dos/soundblaster16","sb","pc98dos-v30-soundblaster16")) return 1;
    if(!run_case(argv[1],"pc98dos/amd98","amd","pc98dos-v30-amd98")) return 1;
    if(!run_case(argv[1],"pc98dos/otomichan","px","pc98dos-v30-otomichan")) return 1;
    if(!run_case(argv[1],"pc98dos/otomix2","px","pc98dos-v30-otomix2")) return 1;
    if(!run_case(argv[1],"pc98vx/soundorchestra","so","pc98vx-bare-soundorchestra", true)) return 1;
    if(!run_case(argv[1],"pc98vx/amd98","amd","pc98vx-bare-amd98", true)) return 1;
    if(!run_case(argv[1],"pc98/burai","px","pc98-bare-opn", true)) return 1;
    if(!run_case(argv[1],"pc98/rashin","px","pc98-bare-opn", true)) return 1;
    return 0;
}
