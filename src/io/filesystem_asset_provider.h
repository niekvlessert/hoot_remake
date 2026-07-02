#pragma once

#include <filesystem>

#include "io/asset_provider.h"

namespace hoot {

class FilesystemAssetProvider final : public AssetProvider {
public:
    explicit FilesystemAssetProvider(std::filesystem::path root);

    bool exists(std::string_view path) const override;
    std::vector<uint8_t> read_all(std::string_view path) const override;

private:
    std::filesystem::path resolve(std::string_view path) const;

    std::filesystem::path root_;
};

} // namespace hoot
