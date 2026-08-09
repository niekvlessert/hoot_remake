#pragma once

#include <map>
#include <set>
#include <string>

namespace hoot {

struct HootplayFileConfig {
    bool has_catalog = false;
    std::string catalog;
    bool has_packs = false;
    std::string packs;
    bool has_entry = false;
    std::string entry;
    bool has_rate = false;
    int rate = 44100;
    bool has_list = false;
    bool list = false;
    bool has_mute_percussion = false;
    bool mute_percussion = false;
    bool has_channels = false;
    std::string channels;
    bool has_wav_path = false;
    std::string wav_path;
    bool has_wav_seconds = false;
    int wav_seconds = 0;
    bool has_track = false;
    int track = 1;
    bool has_ui_font = false;
    std::string ui_font;

    // Environment variables are applied before HootContext construction. This
    // keeps driver/backend tuning out of the command line while preserving the
    // existing environment-variable API for other tools and advanced users.
    std::map<std::string, std::string> environment;
    std::set<std::string> unset_environment;
};

// Loads one INI-style hootplay configuration file. Relative path values are
// resolved against the directory containing the configuration file.
bool load_hootplay_config(const std::string& path,
                          HootplayFileConfig& config,
                          std::string& error);

// Apply backend/driver settings parsed from hootplay.ini to the process
// environment used by the existing replay backends.  Frontends should call
// this before constructing a HootContext so CLI and GUI use identical synth,
// ROM and driver tuning settings.
void apply_hootplay_environment(const HootplayFileConfig& config);

} // namespace hoot
