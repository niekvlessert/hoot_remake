#pragma once

#include <string>

#include "config/hoot_catalog.h"

namespace hoot {

bool apply_hoot_overrides(const std::string& path, HootCatalog& catalog, std::string& error);

class HootXmlLoader {
public:
    bool load_file(const std::string& path, HootCatalog& catalog, std::string& error) const;
    bool load_string(const std::string& xml, HootCatalog& catalog, std::string& error) const;
};

} // namespace hoot
