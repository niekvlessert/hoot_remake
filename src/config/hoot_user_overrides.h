#pragma once

#include <filesystem>
#include <map>
#include <string>

#include "config/hoot_catalog.h"

namespace hoot {

// A local, non-destructive patch for one catalogue entry. Fields are optional;
// when a replace_* flag is set the corresponding complete collection replaces
// the base catalogue value. The Hoot UI writes complete logical entry patches,
// while hand-written files may remain partial.
struct HootEntryOverride {
    std::string id;
    bool hidden = false;
    bool create = false;

    bool has_title = false;
    std::string title;
    bool has_archive = false;
    std::string archive;
    bool has_driver_name = false;
    std::string driver_name;
    bool has_driver_type = false;
    std::string driver_type;
    bool has_driver_alias = false;
    std::string driver_alias;
    bool has_default_sample_rate = false;
    int default_sample_rate = 44100;
    bool has_refresh_hz = false;
    int refresh_hz = 60;

    bool replace_options = false;
    std::map<std::string, int> options;
    bool replace_assets = false;
    std::vector<HootAssetRef> assets;
    bool replace_tracks = false;
    std::vector<CatalogTrack> tracks;
};

struct HootUserOverrides {
    std::map<std::string, HootEntryOverride> entries;
};

// Load/save the stable per-user override format:
//   {"format":"hoot-user-overrides","version":1,"entries":{...}}
// Missing files load as an empty document. Saves are atomic (temporary file +
// rename) and UTF-8 JSON is written unescaped where possible.
bool load_hoot_user_overrides(const std::filesystem::path& path,
                              HootUserOverrides& document,
                              std::string& error);
bool save_hoot_user_overrides(const std::filesystem::path& path,
                              const HootUserOverrides& document,
                              std::string& error);

// Apply a document/file to an already loaded catalogue. Unknown ids are
// rejected unless create=true. hidden=true removes an entry from the runtime
// catalogue without deleting anything from the upstream source.
bool apply_hoot_user_overrides(const HootUserOverrides& document,
                               HootCatalog& catalog,
                               std::string& error);
bool apply_hoot_user_overrides_file(const std::filesystem::path& path,
                                    HootCatalog& catalog,
                                    std::string& error);

// Build a complete override from the currently resolved entry. Useful for
// editors: saving it preserves all information currently visible to libhoot.
HootEntryOverride complete_override_from_entry(const HootEntry& entry);

} // namespace hoot
