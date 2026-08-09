#include "config/hoot_catalog_loader.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

#include "config/hoot_json_loader.h"
#include "config/hoot_user_overrides.h"
#include "config/hoot_sqlite_loader.h"
#include "config/hoot_xml_loader.h"

namespace hoot {
namespace {

std::string lowercase(std::string value)
{
    for (auto& ch : value) {
        if (ch >= 'A' && ch <= 'Z') ch = static_cast<char>(ch - 'A' + 'a');
    }
    return value;
}

bool starts_with(const std::string& path, const char* signature, size_t length)
{
    std::ifstream input(path, std::ios::binary);
    if (!input) return false;
    std::string value(length, '\0');
    input.read(value.data(), static_cast<std::streamsize>(length));
    return static_cast<size_t>(input.gcount()) == length && value.compare(0, length, signature, length) == 0;
}


bool apply_user_overrides(HootCatalog& catalog, std::string& error)
{
    const char* configured = std::getenv("HOOT_USER_OVERRIDES");
    if (!configured || !configured[0]) return true;
    const std::filesystem::path path(configured);
    // The per-user file is intentionally optional; hootui creates it only
    // after the first edit.
    if (!std::filesystem::exists(path)) return true;
    return apply_hoot_user_overrides_file(path, catalog, error);
}

bool apply_external_overrides(const std::string& catalog_path,
                              HootCatalog& catalog,
                              std::string& error)
{
    std::filesystem::path override_path;
    if (const char* configured = std::getenv("HOOT_OVERRIDE_XML")) {
        override_path = configured;
        if (!std::filesystem::exists(override_path)) {
            error = "configured override file does not exist: " + override_path.string();
            return false;
        }
    } else {
        const auto cwd_override = std::filesystem::current_path() / "hoot-overrides.xml";
        const auto catalog_override = std::filesystem::path(catalog_path).parent_path() / "hoot-overrides.xml";
        if (std::filesystem::exists(cwd_override)) override_path = cwd_override;
        else if (std::filesystem::exists(catalog_override)) override_path = catalog_override;
    }
    return override_path.empty() || apply_hoot_overrides(override_path.string(), catalog, error);
}

} // namespace

bool HootCatalogLoader::load_file(const std::string& path, HootCatalog& catalog, std::string& error) const
{
    if (!std::filesystem::exists(path)) {
        error = "catalogue does not exist: " + path;
        return false;
    }
    const auto extension = lowercase(std::filesystem::path(path).extension().string());
    if (extension == ".xml" || starts_with(path, "<?xml", 5) || starts_with(path, "<hoot", 5)) {
        HootXmlLoader loader;
        if (!loader.load_file(path, catalog, error)) return false;
        return apply_user_overrides(catalog, error);
    }
    if (extension == ".json" || starts_with(path, "{", 1)) {
        HootJsonLoader loader;
        if (!loader.load_file(path, catalog, error)) return false;
        if (!apply_external_overrides(path, catalog, error)) return false;
        return apply_user_overrides(catalog, error);
    }
    if (extension == ".sqlite" || extension == ".db" || extension == ".zst"
        || starts_with(path, "SQLite format 3\0", 16)
        || starts_with(path, "\x28\xb5\x2f\xfd", 4)) {
        HootSqliteLoader loader;
        if (!loader.load_file(path, catalog, error)) return false;
        if (!apply_external_overrides(path, catalog, error)) return false;
        return apply_user_overrides(catalog, error);
    }
    error = "unsupported catalogue format: " + path;
    return false;
}

} // namespace hoot
