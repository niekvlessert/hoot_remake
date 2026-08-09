#pragma once

#include <string>

#include "config/hoot_catalog.h"

namespace hoot {

// Format-neutral catalogue loader. Supported inputs are legacy Hoot XML,
// editable JSON manifests/shards, SQLite databases, and zstd-compressed
// SQLite databases.
class HootCatalogLoader {
public:
    bool load_file(const std::string& path, HootCatalog& catalog, std::string& error) const;
};

} // namespace hoot
