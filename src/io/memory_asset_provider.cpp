#include "io/memory_asset_provider.h"

#include <stdexcept>

namespace hoot {

void MemoryAssetProvider::put(std::string path, std::vector<uint8_t> data)
{
    assets_[std::move(path)] = std::move(data);
}

bool MemoryAssetProvider::exists(std::string_view path) const
{
    return assets_.find(path) != assets_.end();
}

std::vector<uint8_t> MemoryAssetProvider::read_all(std::string_view path) const
{
    const auto found = assets_.find(path);
    if (found == assets_.end()) {
        throw std::runtime_error("memory asset not found");
    }
    return found->second;
}

} // namespace hoot
