#include <cassert>
#include <filesystem>
#include <fstream>
#include <string>

#include "config/hoot_catalog.h"
#include "config/hoot_catalog_loader.h"
#include "config/hoot_user_overrides.h"


static void set_env(const char* name, const std::string& value)
{
#if defined(_WIN32)
    _putenv_s(name, value.c_str());
#else
    setenv(name, value.c_str(), 1);
#endif
}

static void unset_env(const char* name)
{
#if defined(_WIN32)
    _putenv_s(name, "");
#else
    unsetenv(name);
#endif
}

int main()
{
    namespace fs = std::filesystem;
    const fs::path root = fs::temp_directory_path() / "hoot-user-overrides-test";
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root);
    const fs::path path = root / "user-overrides.json";

    hoot::HootEntry base;
    base.id = "jp-test";
    base.title = "元のタイトル";
    base.archive = "oldpack";
    base.driver_name = "x68k/generic";
    base.default_sample_rate = 44100;
    base.refresh_hz = 60;
    base.options["clockmul"] = 8;
    base.assets.push_back({"file", "A.DAT", "", 0, 0, false});
    base.tracks.push_back({1, "一曲目", ""});

    hoot::HootCatalog catalog;
    catalog.add_entry(base);

    hoot::HootUserOverrides doc;
    auto patch = hoot::complete_override_from_entry(base);
    patch.title = "編集済み 日本語";
    patch.archive = "newpack";
    patch.options["clockmul"] = 4;
    patch.tracks[0].title = "変更した曲";
    patch.tracks.push_back({2, "二曲目", "VOICE.DAT"});
    doc.entries[patch.id] = patch;

    std::string error;
    assert(hoot::save_hoot_user_overrides(path, doc, error));
    assert(fs::is_regular_file(path));

    hoot::HootUserOverrides reloaded;
    assert(hoot::load_hoot_user_overrides(path, reloaded, error));
    assert(reloaded.entries.size() == 1);
    assert(reloaded.entries.at("jp-test").title == "編集済み 日本語");
    assert(reloaded.entries.at("jp-test").tracks.size() == 2);
    assert(reloaded.entries.at("jp-test").tracks[1].voice_bank == "VOICE.DAT");

    assert(hoot::apply_hoot_user_overrides(reloaded, catalog, error));
    const auto* edited = catalog.find("jp-test");
    assert(edited != nullptr);
    assert(edited->title == "編集済み 日本語");
    assert(edited->archive == "newpack");
    assert(edited->options.at("clockmul") == 4);
    assert(edited->tracks[0].title == "変更した曲");
    assert(edited->tracks[1].title == "二曲目");

    // The normal catalogue loader applies the per-user layer after a JSON
    // base catalogue. XML and SQLite use the same common hook.
    const fs::path shard = root / "base.json";
    {
        std::ofstream out(shard, std::ios::binary);
        out << R"JSON({"format":"hoot-catalog-shard","version":1,"games":[{"id":"jp-test","title":"元のタイトル","archive":"oldpack","driver":{"name":"x68k","type":"generic"},"options":[{"name":"clockmul","value":8}],"assets":[],"title_entries":[{"kind":"title","code":1,"title":"一曲目"}]}]})JSON";
    }
    set_env("HOOT_USER_OVERRIDES", path.string());
    hoot::HootCatalog layered;
    hoot::HootCatalogLoader loader;
    assert(loader.load_file(shard.string(), layered, error));
    const auto* layered_entry = layered.find("jp-test");
    assert(layered_entry != nullptr);
    assert(layered_entry->title == "編集済み 日本語");
    assert(layered_entry->archive == "newpack");
    assert(layered_entry->tracks.size() == 2);
    unset_env("HOOT_USER_OVERRIDES");

    // New local variants are allowed only when create=true and may carry a
    // complete copied track/asset/option definition.
    hoot::HootUserOverrides created_doc;
    hoot::HootEntryOverride created;
    created.id = "jp-test-user";
    created.create = true;
    created.has_title = true; created.title = "ユーザー版";
    created.has_archive = true; created.archive = "oldpack";
    created.has_driver_name = true; created.driver_name = "x68k";
    created.has_driver_type = true; created.driver_type = "generic";
    created.has_default_sample_rate = true; created.default_sample_rate = 44100;
    created.has_refresh_hz = true; created.refresh_hz = 60;
    created.replace_options = true;
    created.replace_assets = true;
    created.replace_tracks = true; created.tracks.push_back({1, "新しい曲", ""});
    created_doc.entries[created.id] = created;
    assert(hoot::apply_hoot_user_overrides(created_doc, catalog, error));
    assert(catalog.find("jp-test-user") != nullptr);
    assert(catalog.find("jp-test-user")->tracks[0].title == "新しい曲");

    // Unknown entries must be explicit creations.
    hoot::HootUserOverrides bad;
    hoot::HootEntryOverride unknown;
    unknown.id = "unknown";
    unknown.has_title = true;
    unknown.title = "bad";
    bad.entries["unknown"] = unknown;
    assert(!hoot::apply_hoot_user_overrides(bad, catalog, error));

    fs::remove_all(root, ec);
    return 0;
}
