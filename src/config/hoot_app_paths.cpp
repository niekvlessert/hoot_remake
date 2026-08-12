#include "config/hoot_app_paths.h"

#include <cstdlib>
#include <fstream>
#include <system_error>

namespace hoot {
namespace {

std::filesystem::path env_path(const char* name)
{
    const char* value = std::getenv(name);
    return value && value[0] ? std::filesystem::path(value) : std::filesystem::path{};
}

bool copy_tree_if_missing(const std::filesystem::path& source,
                          const std::filesystem::path& destination,
                          std::string& error)
{
    if (!std::filesystem::is_directory(source) || std::filesystem::exists(destination)) return true;
    std::error_code ec;
    std::filesystem::create_directories(destination.parent_path(), ec);
    if (ec) {
        error = "unable to create " + destination.parent_path().string() + ": " + ec.message();
        return false;
    }
    std::filesystem::copy(source, destination,
                          std::filesystem::copy_options::recursive |
                          std::filesystem::copy_options::copy_symlinks,
                          ec);
    if (ec) {
        error = "unable to import " + source.string() + " to " + destination.string() + ": " + ec.message();
        return false;
    }
    return true;
}

bool copy_file_if_missing(const std::filesystem::path& source,
                          const std::filesystem::path& destination,
                          std::string& error)
{
    if (!std::filesystem::is_regular_file(source) || std::filesystem::exists(destination)) return true;
    std::error_code ec;
    if (!std::filesystem::copy_file(source, destination, std::filesystem::copy_options::none, ec)) {
        if (!ec) return true;
        error = "unable to import " + source.string() + " to " + destination.string() + ": " + ec.message();
        return false;
    }
    return true;
}

std::string checksum_token(const std::filesystem::path& marker)
{
    std::ifstream input(marker, std::ios::binary);
    std::string token;
    input >> token;
    return token;
}

bool update_managed_catalog(const std::filesystem::path& source_dir,
                            const std::filesystem::path& destination_dir,
                            std::string& error)
{
    const auto source_catalog = source_dir / "hoot.sqlite.zst";
    const auto source_marker = source_dir / "hoot.sqlite.zst.sha256";
    const auto destination_catalog = destination_dir / "hoot.sqlite.zst";
    const auto destination_marker = destination_dir / "hoot.sqlite.zst.sha256";
    if (!std::filesystem::is_regular_file(source_catalog) ||
        !std::filesystem::is_regular_file(source_marker) ||
        !std::filesystem::is_regular_file(destination_catalog) ||
        !std::filesystem::is_regular_file(destination_marker)) return true;

    const std::string source_checksum = checksum_token(source_marker);
    const std::string destination_checksum = checksum_token(destination_marker);
    if (source_checksum.empty() || destination_checksum.empty() ||
        source_checksum == destination_checksum) return true;

    std::error_code ec;
    const auto temporary_catalog = destination_catalog.string() + ".update";
    const auto temporary_marker = destination_marker.string() + ".update";
    std::filesystem::copy_file(source_catalog, temporary_catalog,
                               std::filesystem::copy_options::overwrite_existing, ec);
    if (!ec) std::filesystem::copy_file(source_marker, temporary_marker,
                                        std::filesystem::copy_options::overwrite_existing, ec);
    // copy_file(overwrite_existing) is portable to Windows, where rename does
    // not replace an existing destination. Publish the marker last so an
    // interrupted catalogue copy remains eligible for repair next launch.
    if (!ec) std::filesystem::copy_file(temporary_catalog, destination_catalog,
                                        std::filesystem::copy_options::overwrite_existing, ec);
    if (!ec) std::filesystem::copy_file(temporary_marker, destination_marker,
                                        std::filesystem::copy_options::overwrite_existing, ec);
    if (!ec) {
        std::filesystem::remove(temporary_catalog, ec);
        if (!ec) std::filesystem::remove(temporary_marker, ec);
    }
    if (ec) {
        std::error_code cleanup_ec;
        std::filesystem::remove(temporary_catalog, cleanup_ec);
        std::filesystem::remove(temporary_marker, cleanup_ec);
        error = "unable to update managed catalogue in " + destination_dir.string() + ": " + ec.message();
        return false;
    }
    return true;
}

void set_environment_if_empty(const char* name, const std::filesystem::path& value)
{
    const char* current = std::getenv(name);
    if (current && current[0]) return;
    const std::string text = value.string();
#if defined(_WIN32)
    _putenv_s(name, text.c_str());
#else
    setenv(name, text.c_str(), 0);
#endif
}

} // namespace

std::filesystem::path hoot_user_home()
{
    if (auto home = env_path("HOME"); !home.empty()) return home / ".hoot";
#if defined(_WIN32)
    if (auto profile = env_path("USERPROFILE"); !profile.empty()) return profile / ".hoot";
    const auto drive = env_path("HOMEDRIVE");
    const auto path = env_path("HOMEPATH");
    if (!drive.empty() && !path.empty())
        return std::filesystem::path(drive.string() + path.string()) / ".hoot";
#endif
    std::error_code ec;
    const auto cwd = std::filesystem::current_path(ec);
    return ec ? std::filesystem::path(".hoot") : cwd / ".hoot";
}

void apply_hoot_home_resource_defaults(const HootAppPaths& paths)
{
    const auto roms = paths.roms_dir.empty() ? (paths.home / "roms") : paths.roms_dir;
    set_environment_if_empty("HOOT_MUNT_ROM_PATH", roms / "munt");
    set_environment_if_empty("HOOT_MT32_ROM_PATH", roms / "mt32");
    set_environment_if_empty("HOOT_CM32L_ROM_PATH", roms / "cm32l");
    set_environment_if_empty("HOOT_CM32P_ROM_PATH", roms / "cm32p");
    set_environment_if_empty("SOUNDCANVAS_ROM_PATH", roms / "sc55");
    set_environment_if_empty("HOOT_CM32P_CARD_ROM", roms / "cm32p" / "SN-U110.bin");
    set_environment_if_empty("HOOT_CM32P_CARD_ROM_07", roms / "cm32p" / "SN-U110-07.bin");
    set_environment_if_empty("HOOT_CM32P_CARD_ROM_10", roms / "cm32p" / "SN-U110-10.bin");
    if (!paths.user_overrides.empty()) set_environment_if_empty("HOOT_USER_OVERRIDES", paths.user_overrides);
}

const std::string& default_hootplay_ini()
{
    static const std::string text = R"HOOTINI(# Hoot native-player configuration.
# Generated on first start. Every optional replay/backend setting is disabled
# by default. Uncomment it here or enable it in hootui > Settings.
# Relative paths are resolved from ~/.hoot.

[general]
# override_xml = hoot-overrides.xml
# hootui catalog edits are stored separately in catalog/user-overrides.json
# and are automatically applied after the base XML/JSON/SQLite catalog.

[player]
# The first-start bootstrap imports ./catalog here when it exists.
catalog = catalog/hoot.sqlite.zst
# Pack ZIP directory. Keep disabled to use the process working directory.
# packs = packs
# entry = asuka68snd-generic-2
sample_rate = 44100
# track = 1
# list = false
# mute_percussion = false
# channels = 2-5
# wav = captures/track.wav
# seconds = 30

[gui]
# Japanese-capable TrueType/OpenType font. Automatic system-font discovery is used when disabled.
# font = fonts/NotoSansCJK-Regular.ttc

[midi]
# enabled = true
# backend = auto
# mt32emu_library = lib/libmt32emu.so
# vermouth_library = lib/vermouth.so
# vermouth_abi = legacy
# vermouth_soundfont = soundfonts/GeneralUser_GS.sf2
# munt_rom_path = roms/munt
# mt32_rom_path = roms/mt32
# cm32l_rom_path = roms/cm32l
# cm32p_rom_path = roms/cm32p
# cm32p_card_rom = roms/cm32p/SN-U110.bin
# cm32p_card_rom_07 = roms/cm32p/SN-U110-07.bin
# cm32p_card_rom_10 = roms/cm32p/SN-U110-10.bin
# mt32_machine = mt32_1_07
# cm32l_machine = cm32l_1_02
# soundfont = soundfonts/GeneralUser_GS.sf2
# m1_soundfont = soundfonts/Korg_M1_compatible.sf2
# legacy_soundfont = soundfonts/GeneralUser_GS.sf2
# nuked_sc55_clap = plugins/Nuked-SC55.clap
# soundcanvas_rom_path = roms/sc55
# sc55_model = v1.21
# gain = 0.70
# clap_path = /usr/local/lib/clap

[x68k]
# mdxmini_library = lib/libmdxmini.so
# x_load_mode = auto
# cpu_clock = 10000000
# ym2151_clock = 4000000
# ym2151_core = nuked
# pcm8 = true
# mfp_core = hoot
# mfp_bootstrap = true
# mfp_ignore_overrides = false
# startup = auto
# opm_gain = 0.75
# adpcm_gain = 0.40
# pcm8_gain = 0.40
# total_gain = 1.0
# trace = logs/x68k.trace
# trace_limit = 10000

[psg]
# disabled = false
# solo = false
# channels = ABC
# only = ABC
# gain = 0.90
# invert = false
# tone = true
# noise = true
# raw = false

[pc98]
# trace = logs/pc98.trace
# trace_limit = 20000
# trace_opn = false
# trace_opn_limit = 512
# trace_dos = false
# disable_opn_tl_compat = false
# mmd_timer_hz = 120.0
# beep_gain = 1.0
# trace_x86_unsupported = false

[pc88]
# irq_bus = 0x08
# trace = logs/pc88.trace
# trace_limit = 10000

[environment]
# Advanced pass-through, one NAME=value pair per line.
)HOOTINI";
    return text;
}

bool bootstrap_hoot_home(HootAppPaths& paths, std::string& error)
{
    error.clear();
    paths = {};
    paths.home = hoot_user_home();
    paths.config = paths.home / "hootplay.ini";
    paths.catalog_dir = paths.home / "catalog";
    paths.default_catalog = paths.catalog_dir / "hoot.sqlite.zst";
    paths.user_overrides = paths.catalog_dir / "user-overrides.json";
    paths.roms_dir = paths.home / "roms";

    std::error_code ec;
    const bool existed = std::filesystem::is_directory(paths.home);
    std::filesystem::create_directories(paths.home, ec);
    if (ec) {
        error = "unable to create Hoot home " + paths.home.string() + ": " + ec.message();
        return false;
    }
    paths.created_home = !existed;

    // Stable per-user locations for external ROMs. Creating the directories is
    // harmless and lets both frontends give precise missing-ROM diagnostics.
    for (const char* name : {"munt", "mt32", "cm32l", "cm32p", "sc55"}) {
        std::filesystem::create_directories(paths.roms_dir / name, ec);
        if (ec) {
            error = "unable to create ROM directory " + (paths.roms_dir / name).string() + ": " + ec.message();
            return false;
        }
    }

    const auto cwd = std::filesystem::current_path(ec);
    if (ec) {
        error = "unable to determine current directory: " + ec.message();
        return false;
    }

    const bool had_config = std::filesystem::exists(paths.config);
    const bool had_catalog = std::filesystem::exists(paths.catalog_dir);
    if (cwd.lexically_normal() != paths.home.lexically_normal()) {
        if (!copy_file_if_missing(cwd / "hootplay.ini", paths.config, error)) return false;
        if (!copy_tree_if_missing(cwd / "catalog", paths.catalog_dir, error)) return false;
        // A catalogue imported on first launch is managed only while both its
        // source and destination retain the shipped checksum marker. This lets
        // application updates refresh stale data without replacing a user's
        // custom marker-less catalogue.
        if (!update_managed_catalog(cwd / "catalog", paths.catalog_dir, error)) return false;
    }
    paths.imported_config = !had_config && std::filesystem::is_regular_file(paths.config);
    paths.imported_catalog = !had_catalog && std::filesystem::is_directory(paths.catalog_dir);

    if (!std::filesystem::exists(paths.config)) {
        std::ofstream out(paths.config, std::ios::binary | std::ios::trunc);
        if (!out) {
            error = "unable to create default config: " + paths.config.string();
            return false;
        }
        const auto& text = default_hootplay_ini();
        out.write(text.data(), static_cast<std::streamsize>(text.size()));
        if (!out) {
            error = "unable to write default config: " + paths.config.string();
            return false;
        }
    }
    return true;
}

} // namespace hoot
