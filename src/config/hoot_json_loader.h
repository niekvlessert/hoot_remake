#pragma once

#include <string>

#include "config/hoot_catalog.h"

namespace hoot {

class HootJsonLoader {
public:
    bool load_file(const std::string& path, HootCatalog& catalog, std::string& error) const;
    bool load_string(const std::string& json, HootCatalog& catalog, std::string& error) const;
};

} // namespace hoot
