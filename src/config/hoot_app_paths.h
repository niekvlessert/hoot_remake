#pragma once

#include <filesystem>
#include <string>

namespace hoot {

struct HootAppPaths {
    std::filesystem::path home;
    std::filesystem::path config;
    std::filesystem::path catalog_dir;
    std::filesystem::path default_catalog;
    std::filesystem::path user_overrides;
    std::filesystem::path roms_dir;
    bool created_home = false;
    bool imported_config = false;
    bool imported_catalog = false;
};

// Return the per-user Hoot directory. On Unix/macOS this is ~/.hoot. On
// Windows it is %USERPROFILE%\.hoot (HOME is honoured when explicitly set).
std::filesystem::path hoot_user_home();

// Prepare the per-user runtime directory. The first invocation imports a
// ./hootplay.ini and ./catalog directory from the current working directory
// when present. Shipped catalogues carrying matching checksum marker files are
// refreshed on later application updates; marker-less custom catalogues are
// never overwritten. If no config exists, a complete disabled-by-default
// config is generated in ~/.hoot.
bool bootstrap_hoot_home(HootAppPaths& paths, std::string& error);

// Install non-destructive per-user runtime defaults: ROM search paths rooted
// in ~/.hoot/roms and the optional ~/.hoot/catalog/user-overrides.json layer.
// Explicit environment variables and hootplay.ini settings still take precedence.
void apply_hoot_home_resource_defaults(const HootAppPaths& paths);

// Full reference config used for first-run creation and by the Settings UI.
const std::string& default_hootplay_ini();

} // namespace hoot
