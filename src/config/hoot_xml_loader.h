#pragma once

#include <string>

#include "config/hoot_catalog.h"

namespace hoot {

class HootXmlLoader {
public:
    bool load_file(const std::string& path, HootCatalog& catalog, std::string& error) const;
    bool load_string(const std::string& xml, HootCatalog& catalog, std::string& error) const;
};

} // namespace hoot
