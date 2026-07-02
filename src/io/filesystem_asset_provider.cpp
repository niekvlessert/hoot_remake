#include "io/filesystem_asset_provider.h"

#include <fstream>
#include <iterator>
#include <stdexcept>

namespace hoot {

FilesystemAssetProvider::FilesystemAssetProvider(std::filesystem::path root)
    : root_(std::move(root))
{
}

bool FilesystemAssetProvider::exists(std::string_view path) const
{
    return std::filesystem::exists(resolve(path));
}

std::vector<uint8_t> FilesystemAssetProvider::read_all(std::string_view path) const
{
    const auto full_path = resolve(path);
    std::ifstream input(full_path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("unable to open asset: " + full_path.string());
    }
    return std::vector<uint8_t>(
        std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>());
}

std::filesystem::path FilesystemAssetProvider::resolve(std::string_view path) const
{
    std::filesystem::path requested{std::string(path)};
    if (requested.is_absolute()) {
        return requested;
    }
    return root_ / requested;
}

} // namespace hoot
