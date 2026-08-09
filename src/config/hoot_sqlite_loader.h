#pragma once

#include <string>

#include "config/hoot_catalog.h"

namespace hoot {

class HootSqliteLoader {
public:
    bool load_file(const std::string& path, HootCatalog& catalog, std::string& error) const;
};

} // namespace hoot
