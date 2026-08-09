#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "config/hoot_catalog.h"

namespace hoot {

struct EntryAssetValidation {
    std::filesystem::path archive_path;
    bool archive_required = false;
    bool archive_present = true;
    std::vector<std::string> missing_assets;
    std::string error;

    bool ok() const
    {
        return archive_present && missing_assets.empty() && error.empty();
    }
};

EntryAssetValidation validate_entry_assets(const HootEntry& entry,
                                           const std::filesystem::path& packs_path);

} // namespace hoot
