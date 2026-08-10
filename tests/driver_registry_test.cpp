#include <iostream>
#include <memory>
#include <string>

#include "drivers/driver_registry.h"

namespace {

bool expect_status(const hoot::HootEntry& entry,
                   hoot::DriverSupportStatus expected,
                   const char* label)
{
    const auto probe = hoot::DriverRegistry::instance().probe(entry);
    if (probe.status != expected) {
        std::cerr << label << ": expected "
                  << hoot::driver_support_status_name(expected)
                  << ", got " << hoot::driver_support_status_name(probe.status)
                  << " (" << probe.reason << ")\n";
        return false;
    }
    if (expected != hoot::DriverSupportStatus::Unsupported
        && !hoot::DriverRegistry::instance().create(entry)) {
        std::cerr << label << ": supported probe did not create a driver\n";
        return false;
    }
    return true;
}

} // namespace

int main()
{
    bool ok = true;

    hoot::HootEntry x68k;
    x68k.driver_name = "x68k/generic";
    ok &= expect_status(x68k, hoot::DriverSupportStatus::Playable, "plain x68k");

    x68k.options["midiout"] = 1;
    ok &= expect_status(x68k, hoot::DriverSupportStatus::Experimental, "x68k midi");

    hoot::HootEntry x68k_normal_mt32 = x68k;
    x68k_normal_mt32.title = "[X68000] Example (OPM+MT-32)";
    x68k_normal_mt32.options["midiout_type"] = 0;
    const auto normal_probe = hoot::DriverRegistry::instance().probe(x68k_normal_mt32);
    ok &= expect_status(x68k_normal_mt32, hoot::DriverSupportStatus::Experimental, "x68k normal mt32");
    if (normal_probe.reason.find("Munt") == std::string::npos) ok = false;

    hoot::HootEntry x68k_m1 = x68k;
    x68k_m1.title = "[X68000] Example (M1)";
    x68k_m1.options["midiout_type"] = 5;
    const auto m1_probe = hoot::DriverRegistry::instance().probe(x68k_m1);
    ok &= expect_status(x68k_m1, hoot::DriverSupportStatus::Experimental, "x68k m1 compatibility");
    if (m1_probe.reason.find("M1") == std::string::npos || m1_probe.reason.find("FluidSynth") == std::string::npos) ok = false;

    hoot::HootEntry x68k_vermouth = x68k;
    x68k_vermouth.title = "[X68000] Example (Vermouth)";
    x68k_vermouth.options["midiout_type"] = 6;
    const auto vermouth_probe = hoot::DriverRegistry::instance().probe(x68k_vermouth);
    ok &= expect_status(x68k_vermouth, hoot::DriverSupportStatus::Experimental, "x68k vermouth compatibility");
    if (vermouth_probe.reason.find("Vermouth") == std::string::npos || vermouth_probe.reason.find("FluidSynth") == std::string::npos) ok = false;

    hoot::HootEntry opmdrv;
    opmdrv.driver_name = "x68k/generic";
    opmdrv.assets.push_back({"x", "OPMDRV.X", {}, 0, 0, false});
    const auto opmdrv_probe = hoot::DriverRegistry::instance().probe(opmdrv);
    ok &= expect_status(opmdrv, hoot::DriverSupportStatus::Playable, "x68k OPMDRV");
    if (opmdrv_probe.reason.find("OPMDRV") == std::string::npos) {
        std::cerr << "x68k OPMDRV: family-specific reason missing: "
                  << opmdrv_probe.reason << "\n";
        ok = false;
    }

    hoot::HootEntry float2;
    float2.driver_name = "x68k/generic";
    float2.options["mfp"] = 1;
    float2.assets.push_back({"x", "FLOAT2.X", {}, 0, 0, false});
    const auto float2_probe = hoot::DriverRegistry::instance().probe(float2);
    ok &= expect_status(float2, hoot::DriverSupportStatus::Experimental, "x68k FLOAT2");
    if (float2_probe.reason.find("FLOAT2") == std::string::npos) {
        std::cerr << "x68k FLOAT2: implementation reason missing: "
                  << float2_probe.reason << "\n";
        ok = false;
    }

    hoot::HootEntry pc88opn;
    pc88opn.driver_name = "pc88/opn";
    ok &= expect_status(pc88opn, hoot::DriverSupportStatus::Experimental, "generic pc88 opn");

    hoot::HootEntry pc88opna;
    pc88opna.driver_name = "pc88/opna";
    pc88opna.options["use_pcmx8"] = 1;
    ok &= expect_status(pc88opna, hoot::DriverSupportStatus::Experimental, "generic pc88 opna");

    hoot::HootEntry pc88vados;
    pc88vados.driver_name = "pc88vados/opn";
    pc88vados.assets.push_back({"shell", "mmd2va", {}, 0, 0, false});
    const auto pc88vados_probe = hoot::DriverRegistry::instance().probe(pc88vados);
    ok &= expect_status(pc88vados, hoot::DriverSupportStatus::Experimental, "pc88va dos opn");
    if (pc88vados_probe.driver_id != "pc88vados-v50-opn") {
        std::cerr << "pc88va dos opn: wrong driver id: " << pc88vados_probe.driver_id << "\n";
        ok = false;
    }

    hoot::HootEntry pc98;
    pc98.driver_name = "pc98dos/opn";
    pc98.assets.push_back({"shell", "pmd_98", {}, 0, 0, false});
    ok &= expect_status(pc98, hoot::DriverSupportStatus::Experimental, "pc98 pmd bridge");

    pc98.assets.clear();
    pc98.assets.push_back({"shell", "unknown_player", {}, 0, 0, false});
    ok &= expect_status(pc98, hoot::DriverSupportStatus::Experimental, "pc98 generic shell");

    hoot::HootEntry pc98_midi = pc98;
    pc98_midi.options["midiout"] = 1;
    pc98_midi.options["midiout_type"] = 4;
    const auto pc98_midi_probe = hoot::DriverRegistry::instance().probe(pc98_midi);
    ok &= expect_status(pc98_midi, hoot::DriverSupportStatus::Experimental, "pc98 gs midi");
    if (pc98_midi_probe.reason.find("MPU-401") == std::string::npos
        || pc98_midi_probe.reason.find("software") == std::string::npos) {
        std::cerr << "pc98 gs midi: implementation reason missing: "
                  << pc98_midi_probe.reason << "\n";
        ok = false;
    }

    hoot::HootEntry pc98_bare;
    pc98_bare.driver_name = "pc98vx/opn";
    pc98_bare.options["bootcs"] = 0x0060;
    pc98_bare.options["bootip"] = 0x0000;
    pc98_bare.options["funcvect"] = 0x7f;
    pc98_bare.assets.push_back({"code", "player.bin", {}, 0x00600, 0, false});
    pc98_bare.assets.push_back({"bgm", "song.dat", {}, 1, 0, false});
    const auto pc98_bare_implicit_probe = hoot::DriverRegistry::instance().probe(pc98_bare);
    ok &= expect_status(pc98_bare, hoot::DriverSupportStatus::Experimental,
                        "pc98 bare dynamic data buffer");
    if (pc98_bare_implicit_probe.reason.find("07D0h-07D7h") == std::string::npos) {
        std::cerr << "pc98 bare dynamic data buffer: loader ABI missing: "
                  << pc98_bare_implicit_probe.reason << "\n";
        ok = false;
    }

    hoot::HootEntry microcabin;
    microcabin.driver_name = "pc98dos/opn";
    microcabin.driver_alias = "MICROCABIN/NEC PC-9801";
    ok &= expect_status(microcabin, hoot::DriverSupportStatus::Experimental,
                        "microcabin pc98");

    hoot::HootEntry msx;
    msx.driver_name = "msx/generic";
    ok &= expect_status(msx, hoot::DriverSupportStatus::Unsupported, "msx");

    return ok ? 0 : 1;
}
