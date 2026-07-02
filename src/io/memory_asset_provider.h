#pragma once

#include <map>

#include "io/asset_provider.h"

namespace hoot {

class MemoryAssetProvider final : public AssetProvider {
public:
    void put(std::string path, std::vector<uint8_t> data);

    bool exists(std::string_view path) const override;
    std::vector<uint8_t> read_all(std::string_view path) const override;

private:
    std::map<std::string, std::vector<uint8_t>, std::less<>> assets_;
};

} // namespace hoot
