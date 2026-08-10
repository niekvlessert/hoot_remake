#include "config/hoot_app_paths.h"
#include "config/hoot_settings.h"

#include <cassert>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;

static void set_home(const std::string& value)
{
#if defined(_WIN32)
    _putenv_s("HOME", value.c_str());
#else
    setenv("HOME", value.c_str(), 1);
#endif
}

int main()
{
    const fs::path original_cwd = fs::current_path();
    const char* old_home_raw = std::getenv("HOME");
    const std::string old_home = old_home_raw ? old_home_raw : "";

    const fs::path root = fs::temp_directory_path() / "hoot_app_paths_settings_test";
    fs::remove_all(root);
    const fs::path source = root / "portable";
    const fs::path home = root / "home";
    fs::create_directories(source / "catalog");
    fs::create_directories(home);
    {
        std::ofstream(source / "catalog" / "hoot.sqlite.zst", std::ios::binary) << "catalog";
        std::ofstream(source / "hootplay.ini")
            << "[player]\n"
               "catalog = catalog/hoot.sqlite.zst\n"
               "sample_rate = 48000\n"
               "\n[midi]\n"
               "backend = auto\n";
    }

    set_home(home.string());
    fs::current_path(source);

    hoot::HootAppPaths paths;
    std::string error;
    assert(hoot::bootstrap_hoot_home(paths, error));
    assert(paths.home == home / ".hoot");
    assert(paths.imported_config);
    assert(paths.imported_catalog);
    assert(fs::is_regular_file(paths.config));
    assert(fs::is_regular_file(paths.default_catalog));
    assert(paths.user_overrides == paths.catalog_dir / "user-overrides.json");
    assert(fs::is_directory(paths.roms_dir / "mt32"));
    assert(fs::is_directory(paths.roms_dir / "cm32l"));
    assert(fs::is_directory(paths.roms_dir / "cm32p"));
    assert(fs::is_directory(paths.roms_dir / "sc55"));

    // ROM resources in ~/.hoot are usable without enabling path settings.
    // Explicit environment/config values must still win.
#if defined(_WIN32)
    _putenv_s("HOOT_MT32_ROM_PATH", "");
    _putenv_s("SOUNDCANVAS_ROM_PATH", "");
    _putenv_s("HOOT_USER_OVERRIDES", "");
#else
    unsetenv("HOOT_MT32_ROM_PATH");
    unsetenv("SOUNDCANVAS_ROM_PATH");
    unsetenv("HOOT_USER_OVERRIDES");
#endif
    hoot::apply_hoot_home_resource_defaults(paths);
    assert(std::getenv("HOOT_MT32_ROM_PATH"));
    assert(fs::path(std::getenv("HOOT_MT32_ROM_PATH")) == paths.roms_dir / "mt32");
    assert(std::getenv("HOOT_USER_OVERRIDES"));
    assert(fs::path(std::getenv("HOOT_USER_OVERRIDES")) == paths.user_overrides);
#if defined(_WIN32)
    _putenv_s("HOOT_MT32_ROM_PATH", "explicit-roms");
#else
    setenv("HOOT_MT32_ROM_PATH", "explicit-roms", 1);
#endif
    hoot::apply_hoot_home_resource_defaults(paths);
    assert(std::string(std::getenv("HOOT_MT32_ROM_PATH")) == "explicit-roms");

    // Existing home state must never be overwritten by a later portable copy.
    {
        std::ofstream(paths.config, std::ios::trunc) << "[player]\nsample_rate = 32000\n";
        std::ofstream(source / "hootplay.ini", std::ios::trunc) << "[player]\nsample_rate = 22050\n";
    }
    hoot::HootAppPaths second;
    assert(hoot::bootstrap_hoot_home(second, error));
    assert(!second.imported_config);
    {
        std::ifstream existing(second.config);
        std::string all((std::istreambuf_iterator<char>(existing)), std::istreambuf_iterator<char>());
        assert(all.find("32000") != std::string::npos);
        assert(all.find("22050") == std::string::npos);
    }

    // Settings editor round-trip: known fields remain typed/config-compatible.
    hoot::HootSettingsDocument doc;
    hoot::reset_hoot_settings(doc);
    for (auto& item : doc.values) {
        if (!item.spec) continue;
        if (std::string(item.spec->section) == "player" && std::string(item.spec->key) == "catalog") {
            item.enabled = true;
            item.value = "catalog/hoot.sqlite.zst";
        }
        if (std::string(item.spec->section) == "midi" && std::string(item.spec->key) == "backend") {
            item.enabled = true;
            item.value = "nuked-sc55";
        }
        if (std::string(item.spec->section) == "x68k" && std::string(item.spec->key) == "pcm8") {
            item.enabled = true;
            item.value = "true";
        }
    }
    doc.environment["CUSTOM_HOOT_TEST"] = "abc";
    assert(hoot::save_hoot_settings_document(paths.config.string(), doc, error));

    hoot::HootSettingsDocument loaded;
    assert(hoot::load_hoot_settings_document(paths.config.string(), loaded, error));
    bool saw_backend = false;
    bool saw_pcm8 = false;
    for (const auto& item : loaded.values) {
        if (!item.spec) continue;
        if (std::string(item.spec->section) == "midi" && std::string(item.spec->key) == "backend") {
            saw_backend = item.enabled && item.value == "nuked-sc55";
        }
        if (std::string(item.spec->section) == "x68k" && std::string(item.spec->key) == "pcm8") {
            saw_pcm8 = item.enabled && item.value == "true";
        }
    }
    assert(saw_backend);
    assert(saw_pcm8);
    assert(loaded.environment.at("CUSTOM_HOOT_TEST") == "abc");

    fs::current_path(original_cwd);
    if (!old_home.empty()) set_home(old_home);
    fs::remove_all(root);
    return 0;
}
