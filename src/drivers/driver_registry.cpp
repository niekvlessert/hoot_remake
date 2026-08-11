#include "drivers/driver_registry.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <sstream>

#include "drivers/microcabin_pc88_driver.h"
#include "drivers/pc88_generic_driver.h"
#include "drivers/microcabin_pc98dos_driver.h"
#include "drivers/pc98_dos_driver.h"
#include "drivers/x68k_generic_driver.h"
#include "drivers/x68k_mxdrv_driver.h"

namespace {

using hoot::DriverProbeResult;
using hoot::DriverSupportStatus;
using hoot::HootEntry;

bool option_enabled(const HootEntry& entry, const char* name)
{
    const auto it = entry.options.find(name);
    return it != entry.options.end() && it->second != 0;
}

int option_value(const HootEntry& entry, const char* name, int fallback = 0)
{
    const auto it = entry.options.find(name);
    return it == entry.options.end() ? fallback : it->second;
}

std::string lowercase(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

std::string first_token(const std::string& command)
{
    const auto start = command.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) {
        return {};
    }
    const auto end = command.find_first_of(" \t\r\n", start);
    return command.substr(start, end == std::string::npos ? std::string::npos : end - start);
}

std::string shell_name(const std::string& command)
{
    auto result = lowercase(std::filesystem::path(first_token(command)).filename().string());
    if (std::filesystem::path(result).extension().empty()) {
        result += ".com";
    }
    return result;
}

std::vector<std::string> shell_names(const HootEntry& entry)
{
    std::vector<std::string> result;
    for (const auto& asset : entry.assets) {
        if (asset.type == "shell" && !asset.path.empty()) {
            result.push_back(shell_name(asset.path));
        }
    }
    return result;
}

bool contains_shell(const std::vector<std::string>& shells, const char* name)
{
    return std::find(shells.begin(), shells.end(), lowercase(name)) != shells.end();
}

bool has_asset_filename(const HootEntry& entry, const char* filename)
{
    const auto wanted = lowercase(filename);
    return std::any_of(entry.assets.begin(), entry.assets.end(), [&](const auto& asset) {
        return lowercase(std::filesystem::path(asset.path).filename().string()) == wanted;
    });
}

DriverProbeResult probe_pc88_generic(const HootEntry& entry)
{
    if (entry.driver_name != "pc88/opn" && entry.driver_name != "pc88/opna") {
        return {};
    }
    std::vector<std::string> limitations;
    if (option_enabled(entry, "use_ssgpcm")) limitations.emplace_back("SSG PCM helper path is not yet implemented");
    if (option_enabled(entry, "use_pcmx8")) limitations.emplace_back("PCMx8 helper path is not yet implemented");
    if (option_enabled(entry, "use_gvram")) limitations.emplace_back("GVRAM side effects are stubbed");
    if (option_enabled(entry, "use_n88rom")) limitations.emplace_back("N88-BASIC ROM services are not yet emulated");
    std::ostringstream reason;
    reason << "generic PC-88 Z80/" << (entry.driver_name == "pc88/opna" ? "YM2608 OPNA" : "YM2203 OPN")
           << " host implements init_pc/baseclock, variable BGM/voice sizes, FM Timer A/B, RTC/VRTC interrupts and Hoot host I/O";
    if (!limitations.empty()) {
        reason << "; remaining limitations: ";
        for (size_t i = 0; i < limitations.size(); ++i) {
            if (i) reason << ", ";
            reason << limitations[i];
        }
    }
    return {DriverSupportStatus::Experimental, "pc88-generic", reason.str()};
}

DriverProbeResult probe_microcabin_pc98(const HootEntry& entry)
{
    if (entry.driver_name != "pc98dos/opn"
        || lowercase(entry.driver_alias).find("microcabin") == std::string::npos) {
        return {};
    }
    return {DriverSupportStatus::Experimental,
            "microcabin-pc98dos-opna",
            "specialized Microcabin PC-98 DOS bridge; only the implemented MMD/HHD-style command paths are expected to work"};
}

DriverProbeResult probe_pcatdos(const HootEntry& entry)
{
    if (entry.driver_name != "pcatdos/adlib") {
        return {};
    }

    const auto shells = shell_names(entry);
    if (shells.empty()) {
        return {DriverSupportStatus::Recognized,
                "pcatdos-adlib",
                "IBM PC/AT DOS AdLib entry recognized, but no shell command is present"};
    }

    return {DriverSupportStatus::Experimental,
            "pcatdos-adlib",
            "generic IBM PC/AT DOS shell host with YM3812 OPL2 at 388h/389h, 8253 PIT IRQ0 timing, 8259 PIC masks and PMD/PMD_98 bridge support"};
}

DriverProbeResult probe_pc98dos(const HootEntry& entry)
{
    const bool is_pc9821 = entry.driver_name.rfind("pc9821dos/", 0) == 0;
    const bool is_pc98dos = entry.driver_name.rfind("pc98dos/", 0) == 0;
    if (!is_pc9821 && !is_pc98dos) return {};
    const auto slash = entry.driver_name.find('/');
    const std::string type = slash == std::string::npos ? std::string{} : entry.driver_name.substr(slash + 1);
    const bool is_opn = type == "opn";
    const bool is_opna = type == "opna";
    const bool is_86 = type == "86";
    const bool is_beep = type == "beep";
    const bool is_amd98 = type == "amd98";
    const bool is_sound_orchestra = type == "soundorchestra";
    const bool is_sb16 = type == "soundblaster16";
    const bool is_otomi = type == "otomichan";
    const bool is_otomix2 = type == "otomix2";
    const bool special_v30 = is_amd98 || is_sound_orchestra || is_sb16 || is_otomi || is_otomix2;
    if (!is_opn && !is_opna && !is_86 && !is_beep && !special_v30) {
        return {};
    }
    if (is_pc9821 && special_v30) return {};

    const char* prefix = is_pc9821 ? "pc9821dos-386" : "pc98dos-v30";
    std::string suffix = is_opn ? "opn" : is_86 ? "86" : is_beep ? "beep" : is_opna ? "opna" : type;
    std::string driver_id = std::string(prefix) + "-" + suffix;
    const char* chip_name = is_opn ? "YM2203 OPN"
                        : is_86 ? "YM2608 OPNA + PC-9801-86 PCM"
                        : is_beep ? "PC-98 PIT speaker"
                        : is_amd98 ? "AMD-98 triple PSG + rhythm"
                        : is_sound_orchestra ? "Sound Orchestra YM2203 + YM3812 OPL2"
                        : is_sb16 ? "Sound Blaster 16(98) YMF262 OPL3"
                        : is_otomix2 ? "Otomi-chan x2/PX multi-OPNA + PCM86"
                        : is_otomi ? "Otomi-chan/PX multi-OPNA"
                                  : "YM2608 OPNA";
    const auto shells = shell_names(entry);
    if (shells.empty()) {
        std::ostringstream reason;
        reason << "PC-98 DOS/" << suffix
               << " entry recognized, but no shell command is present";
        return {DriverSupportStatus::Recognized, driver_id, reason.str()};
    }

    std::ostringstream reason;
    reason << "generic " << (is_pc9821 ? "PC-9821 DOS/386-compatible" : "PC-98 DOS/V30")
           << " shell host with " << chip_name
           << ", DOS file services, clockmul timing and interrupt-vector bridge";
    if (is_sound_orchestra) reason << "; PC-98 ports 188h/18Ah and 18Ch/18Eh are split between OPN and OPL2";
    if (is_sb16) reason << "; SB16(98) OPL3 port aliases plus CT1741 reset/version/test detection are implemented; catalogue music is treated as OPL3 synthesis, not arbitrary SB16 DMA audio";
    if (is_amd98) reason << "; D8h-DEh triple-PSG latch protocol, timer IRQ and four-voice rhythm approximation are implemented";
    if (is_otomi || is_otomix2) reason << "; PX 088h/188h/488h/588h multi-OPNA decode is implemented";
    if (is_otomix2) reason << "; fifth 288h OPNA and PC-9801-86 PCM are enabled";
    if (is_opna || is_86) {
        reason << "; 256 KiB OPNA Delta-T RAM is allocated for guest ADPCM uploads";
    }
    if (is_86) {
        reason << "; A460-A46C PC-9801-86 PCM FIFO/DAC is emulated";
        if (option_enabled(entry, "extramsize")) {
            reason << "; EMMDRV/4 MiB extramsize startup is real-pack validated on FC98 v1.2, but arbitrary EMS-backed sample layouts remain pack-dependent";
        }
    }
    if (contains_shell(shells, "hhd_98.com") || contains_shell(shells, "pmd_98.com")) {
        reason << "; HHD_98/PMD_98 bridge is implemented";
    }
    if (is_beep && contains_shell(shells, "pmdb.com")) {
        reason << "; PMDB PIT-channel-1/PPI-buzzer/INT-0Ah path is real-pack validated on NekoEX";
    }
    if (option_enabled(entry, "midiout") || option_enabled(entry, "midiout_type")) {
        const int midiout_type = option_value(entry, "midiout_type", -1);
        if (midiout_type == 1 || midiout_type == 2 || midiout_type == 3) {
            if (midiout_type == 3) {
                reason << "; MPU-401 PC-98 MIDI transport and full CM-64 software rendering are implemented when user-supplied CM-32L and CM-32P ROMs are available; CM-32L-only fallback remains available";
            } else {
                reason << "; MPU-401 PC-98 MIDI transport and Munt/mt32emu LA synthesis are implemented";
            }
        } else if (midiout_type == 4 || midiout_type == 7 || midiout_type == 8) {
            reason << "; MPU-401 PC-98 MIDI transport and software GM/GS rendering are implemented";
            if (midiout_type == 4) {
                reason << " with Nuked-SC55 preferred and FluidSynth fallback";
            } else {
                reason << " through FluidSynth";
            }
        } else {
            reason << "; MPU-401 PC-98 MIDI transport is implemented, but this MIDI module class still needs a dedicated synth backend";
        }
    }
    if (option_enabled(entry, "wstimer")) {
        reason << "; wstimer-specific scheduling remains experimental";
    }
    return {DriverSupportStatus::Experimental, driver_id, reason.str()};
}



DriverProbeResult probe_pc98_bare(const HootEntry& entry)
{
    const bool is_pc98 = entry.driver_name.rfind("pc98/", 0) == 0;
    const bool is_pc98vx = entry.driver_name.rfind("pc98vx/", 0) == 0;
    if (!is_pc98 && !is_pc98vx) return {};

    const auto slash = entry.driver_name.find('/');
    const std::string type = slash == std::string::npos ? std::string{} : entry.driver_name.substr(slash + 1);
    const bool is_opn = type == "opn";
    const bool is_opna = type == "opna";
    const bool is_86 = type == "86";
    const bool is_beep = type == "beep";
    const bool is_amd98 = type == "amd98";
    const bool is_sound_orchestra = type == "soundorchestra";
    const bool is_legacy_opn = type == "burai" || type == "rashin";
    if (!is_opn && !is_opna && !is_86 && !is_beep && !is_amd98 && !is_sound_orchestra && !is_legacy_opn) return {};

    bool has_code = false;
    for (const auto& asset : entry.assets) {
        if (asset.type == "code") has_code = true;
    }
    const std::string prefix = is_pc98vx ? "pc98vx-bare" : "pc98-bare";
    const std::string id = prefix + std::string("-") + (is_opn || is_legacy_opn ? "opn" : is_86 ? "86" : is_beep ? "beep" : is_amd98 ? "amd98" : is_sound_orchestra ? "soundorchestra" : "opna");
    if (!has_code) {
        return {DriverSupportStatus::Recognized, id,
                "PC-98 bare replay entry recognized, but no code asset is present"};
    }
    std::ostringstream reason;
    reason << "generic " << (is_pc98vx ? "PC-98VX" : "PC-98")
           << " bare replay host loads extracted real-mode code, implements the original Hoot 07D0h-07D7h dynamic/extended BGM loader ABI, honors bootcs/bootip/funcvect and segmented asset addressing, and drives 60 Hz VRTC plus FM timers with ";
    if (is_opn || is_legacy_opn) reason << "YM2203 OPN";
    else if (is_86) reason << "YM2608 OPNA + PC-9801-86 PCM";
    else if (is_beep) reason << "PC-98 PIT speaker";
    else if (is_amd98) reason << "AMD-98 triple PSG + rhythm";
    else if (is_sound_orchestra) reason << "Sound Orchestra YM2203 + YM3812 OPL2";
    else reason << "YM2608 OPNA";
    if (entry.archive == "ys2_98" && entry.driver_name == "pc98vx/opn") {
        reason << "; real-pack validated with Ys II: all 49 catalogue tracks start and produce OPN audio, including slave-PIC INT 14h YM timer delivery";
        return {DriverSupportStatus::Playable, id, reason.str()};
    }
    reason << "; structurally validated with synthetic guest code; real-pack validation is still required";
    return {DriverSupportStatus::Experimental, id, reason.str()};
}

DriverProbeResult probe_pc88va_bare(const HootEntry& entry)
{
    if (entry.driver_name.rfind("pc88va/", 0) != 0) return {};
    const auto slash = entry.driver_name.find('/');
    const std::string type = slash == std::string::npos
        ? std::string{} : entry.driver_name.substr(slash + 1);
    if (type != "opn" && type != "opna") return {};

    const std::string id = std::string("pc88va-bare-") + type;
    bool has_code = false;
    for (const auto& asset : entry.assets) {
        if (asset.type == "code") has_code = true;
    }
    if (!has_code) {
        return {DriverSupportStatus::Recognized, id,
                "PC-88VA bare replay entry recognized, but no code asset is present"};
    }
    return {DriverSupportStatus::Experimental, id,
            std::string("generic PC-88VA V50 bare replay host loads catalog code and BGM assets, implements the Hoot control/loader port ABI, vertical-sync and FM timer interrupts, and PC-88VA FM port aliases with ")
                + (type == "opn" ? "YM2203 OPN" : "YM2608 OPNA")};
}

DriverProbeResult probe_pc88vados(const HootEntry& entry)
{
    if (entry.driver_name.rfind("pc88vados/", 0) != 0) return {};
    const auto slash = entry.driver_name.find('/');
    const std::string type = slash == std::string::npos
        ? std::string{} : entry.driver_name.substr(slash + 1);
    if (type != "opn" && type != "opna") return {};

    const std::string id = std::string("pc88vados-v50-") + type;
    const auto shells = shell_names(entry);
    if (shells.empty()) {
        return {DriverSupportStatus::Recognized, id,
                "PC-88VA DOS replay entry recognized, but no shell command is present"};
    }
    return {DriverSupportStatus::Experimental, id,
            std::string("generic PC-88VA DOS MMD compatibility bridge with archive file/device services and ")
                + (type == "opn" ? "YM2203 OPN" : "YM2608 OPNA")};
}

DriverProbeResult probe_x68k_mxdrv(const HootEntry& entry)
{
    if (entry.driver_name != "x68k/mxdrv") return {};
    bool has_mdx = false;
    for (const auto& asset : entry.assets) {
        if (asset.type == "data") {
            auto p = lowercase(asset.path);
            if (p.size() >= 4 && p.substr(p.size() - 4) == ".mdx") has_mdx = true;
        }
    }
    if (!has_mdx) return {DriverSupportStatus::Recognized, "x68k-mxdrv-mdxmini", "MXDRV entry has no MDX asset"};
    return {DriverSupportStatus::Experimental, "x68k-mxdrv-mdxmini",
            "direct MDX/PDX playback through the optional mdxmini runtime (YM2151 plus up to eight PCM channels); set HOOT_MDXMINI_LIBRARY if it is not on the system library path"};
}

DriverProbeResult probe_x68k(const HootEntry& entry)
{
    if (entry.driver_name != "x68k/generic") {
        return {};
    }

    std::vector<std::string> limitations;
    if (has_asset_filename(entry, "FLOAT2.X")) {
        limitations.emplace_back(
            "FLOAT2.X line-F dispatch and the Hoot-compatible 0xffffxx workspace are smoke-tested; broader MFP timing remains experimental");
    }
    if (option_enabled(entry, "midiout") || option_enabled(entry, "midiout_type")
        || option_enabled(entry, "mt32")) {
        const int midiout_type = option_value(entry, "midiout_type", -1);
        if (midiout_type == 1 || midiout_type == 2 || midiout_type == 3) {
            limitations.emplace_back(midiout_type == 3
                ? "CZ-6BM1 MIDI transport and full CM-64 rendering are implemented with Munt/mt32emu for CM-32L plus the built-in CM-32P PCM renderer when user-supplied ROMs are available; without CM-32P ROMs auto mode falls back to LA-only"
                : "CZ-6BM1 MIDI transport and Munt/mt32emu MT-32 synthesis are implemented when user-supplied ROMs are available");
        } else if (midiout_type == 0
                   && (lowercase(entry.title).find("mt-32") != std::string::npos
                       || lowercase(entry.title).find("mt32") != std::string::npos)) {
            limitations.emplace_back(
                "CZ-6BM1 NORMAL MIDI entry is explicitly tagged MT-32 by the catalogue and is routed through Munt/mt32emu");
        } else if (midiout_type == 4 || midiout_type == 7 || midiout_type == 8) {
            limitations.emplace_back(
                "CZ-6BM1 MIDI transport and optional FluidSynth GM/GS rendering are implemented; generic SoundFont rendering is not hardware-exact SC-55/SC-88/TG-100 emulation");
        } else if (midiout_type == 6) {
            limitations.emplace_back(
                "CZ-6BM1 Vermouth MIDI transport can use the native Vermouth ABI (legacy GUS/TiMidity or compatible drop-in), with FluidSynth GM compatibility fallback");
        } else if (midiout_type == 5) {
            limitations.emplace_back(
                "CZ-6BM1 Korg M1 MIDI transport is implemented; FluidSynth can use a dedicated midi.m1_soundfont for compatibility, while authentic M1 synthesis still requires an external licensed M1 implementation");
        } else {
            limitations.emplace_back(
                "CZ-6BM1 MIDI transport is implemented, but this MIDI module class still lacks a synthesizer backend");
        }
    }
    if (option_enabled(entry, "pcm8")) {
        limitations.emplace_back("PCM8 direct-block, array-chain and linked-array-chain ADPCM/signed-PCM output are implemented; direct-block playback is production-validated with Asuka 120% and Mad Stalker, while chain modes currently have conformance-test coverage");
    }
    if (option_enabled(entry, "mfp")) {
        limitations.emplace_back("automatic native/legacy startup selection is validated with Asuka 120% and Mad Stalker; broader MFP/timer behavior remains experimental");
    }
    if (option_enabled(entry, "dmaint")) {
        limitations.emplace_back("channel-3 ADPCM DMA completion IRQ3 with programmable 68450 normal interrupt vector is implemented and conformance-tested; real-pack dmaint validation is still required");
    }

    if (!limitations.empty()) {
        std::ostringstream reason;
        for (size_t i = 0; i < limitations.size(); ++i) {
            if (i != 0) {
                reason << "; ";
            }
            reason << limitations[i];
        }
        return {DriverSupportStatus::Experimental, "x68k-generic", reason.str()};
    }

    if (has_asset_filename(entry, "OPMDRV.X") || has_asset_filename(entry, "OPMDRV.BIN")) {
        return {DriverSupportStatus::Playable,
                "x68k-generic",
                "OPMDRV-family X68000 entry uses the implemented 68000/YM2151/ADPCM path and expanded 0x000000-0xe7ffff packed-data memory; reference audio is still required"};
    }

    return {DriverSupportStatus::Playable,
            "x68k-generic",
            "basic X68000 YM2151/ADPCM path with expanded packed-data memory is implemented; this entry is not yet reference-audio verified"};
}

} // namespace

namespace hoot {

const char* driver_support_status_name(DriverSupportStatus status)
{
    switch (status) {
    case DriverSupportStatus::Unsupported: return "unsupported";
    case DriverSupportStatus::Recognized: return "recognized";
    case DriverSupportStatus::Experimental: return "experimental";
    case DriverSupportStatus::Playable: return "playable";
    case DriverSupportStatus::Verified: return "verified";
    }
    return "unsupported";
}

const DriverRegistry& DriverRegistry::instance()
{
    static const DriverRegistry registry;
    return registry;
}

DriverRegistry::DriverRegistry()
{
    registrations_.push_back({
        "pc88-generic",
        probe_pc88_generic,
        [] { return std::make_unique<Pc88GenericDriver>(); }});
    registrations_.push_back({
        "microcabin-pc98dos-opna",
        probe_microcabin_pc98,
        [] { return std::make_unique<MicrocabinPc98DosDriver>(); }});
    registrations_.push_back({
        "pcatdos-adlib",
        probe_pcatdos,
        [] { return std::make_unique<Pc98DosDriver>(); }});
    registrations_.push_back({
        "pc88va-bare",
        probe_pc88va_bare,
        [] { return std::make_unique<Pc98DosDriver>(); }});
    registrations_.push_back({
        "pc88vados-v50",
        probe_pc88vados,
        [] { return std::make_unique<MicrocabinPc98DosDriver>(); }});
    registrations_.push_back({
        "pc98-bare",
        probe_pc98_bare,
        [] { return std::make_unique<Pc98DosDriver>(); }});
    registrations_.push_back({
        "pc98dos-v30-opna",
        probe_pc98dos,
        [] { return std::make_unique<Pc98DosDriver>(); }});
    registrations_.push_back({
        "x68k-mxdrv-mdxmini",
        probe_x68k_mxdrv,
        [] { return std::make_unique<X68kMxdrvDriver>(); }});
    registrations_.push_back({
        "x68k-generic",
        probe_x68k,
        [] { return std::make_unique<X68kGenericDriver>(); }});
}

DriverProbeResult DriverRegistry::probe(const HootEntry& entry) const
{
    for (const auto& registration : registrations_) {
        auto result = registration.probe(entry);
        if (result.status != DriverSupportStatus::Unsupported) {
            if (result.driver_id.empty()) {
                result.driver_id = registration.id;
            }
            return result;
        }
    }

    std::string reason = "no registered replay host for catalog driver ";
    reason += entry.driver_name.empty() ? "<empty>" : entry.driver_name;
    return {DriverSupportStatus::Unsupported, {}, std::move(reason)};
}

std::unique_ptr<HootDriver> DriverRegistry::create(const HootEntry& entry) const
{
    for (const auto& registration : registrations_) {
        const auto result = registration.probe(entry);
        if (result.status != DriverSupportStatus::Unsupported) {
            return registration.factory();
        }
    }
    return nullptr;
}

const std::vector<DriverRegistry::Registration>& DriverRegistry::registrations() const
{
    return registrations_;
}

} // namespace hoot
