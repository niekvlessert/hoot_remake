#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace hoot {

class AssetProvider {
public:
    virtual ~AssetProvider() = default;
    virtual bool exists(std::string_view path) const = 0;
    virtual std::vector<uint8_t> read_all(std::string_view path) const = 0;
};

} // namespace hoot
