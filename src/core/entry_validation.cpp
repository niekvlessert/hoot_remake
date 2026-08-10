#include "core/entry_validation.h"

#include <algorithm>
#include <cstdint>
#include <set>
#include <vector>

#include "io/d88_image.h"
#include "io/zip_archive.h"

namespace {

bool has_catalog_asset(const hoot::ZipArchive& archive,
                       const hoot::HootEntry& entry,
                       const hoot::HootAssetRef& asset,
                       const std::filesystem::path& packs_path)
{
    if (asset.type == "device" || asset.type == "shell") {
        return true;
    }
    // pc88vados catalogue entries name DOS-side bridge binaries with offset
    // -1. They are host plumbing rather than music data; the native bridge
    // supplies their behavior and must not require those binaries in a pack.
    if (entry.driver_name.rfind("pc88vados/", 0) == 0
        && asset.type == "file" && asset.offset == UINT32_MAX) {
        return true;
    }
    if (archive.contains(asset.path)) {
        return true;
    }
    if (asset.path.find('_') != std::string::npos) {
        auto alternate = asset.path;
        *std::find(alternate.begin(), alternate.end(), '_') = '/';
        if (archive.contains(alternate)) {
            return true;
        }
    }
    if (entry.archive == "ad68snd" && asset.path == "kmdrv.bin"
        && archive.contains("ad68snd.bin") && archive.contains("KMDRV.X")
        && archive.contains("ADPCM_BG.DAT") && archive.contains("ADPCM_SE.DAT")
        && archive.contains("SOUND_BG.DAT") && archive.contains("SOUND_SE.DAT")
        && archive.contains("VOICE_BG.DAT") && archive.contains("VOICE_SE.DAT")
        && archive.contains("TABLE_BG.DAT") && archive.contains("YUSEN_TB.DAT")) {
        return true;
    }

    auto has_gazzel_d88_driver_member = [&]() {
        if (entry.archive == "xak2_98" || (asset.path != "MMD.COM" && asset.path != "MMD2.COM")) {
            return false;
        }
        hoot::D88Image d88;
        std::string error;
        const auto d88_path = packs_path / "Xak - The Tower of Gazzel (Disk 3).d88";
        if (!d88.open(d88_path, error)) {
            return false;
        }
        if (asset.path == "MMD.COM") {
            const auto data = d88.read_data(0, 0x04, 0x02, 0, 0x06, 0x02, 0x01, 0x09, 0x200, error);
            return data.size() >= 0x20 && data[0x10] == 0xf3 && data[0x11] == 0xe5;
        }
        const auto data = d88.read_data(0, 0x0b, 0x05, 1, 0x09, 0x02, 0x01, 0x09, 0xc00, error);
        return data.size() >= 4 && data[0] == 0xe5 && data[1] == 0xd5 && data[2] == 0xc5;
    };
    if (has_gazzel_d88_driver_member()) {
        return true;
    }

    struct Fallback {
        std::string archive;
        std::string member;
    };
    std::vector<Fallback> fallbacks;
    if (asset.path == "PATCH") {
        if (entry.archive == "xak2_98") {
            fallbacks.push_back({"cabin98", "PATCH_XAK2_88/PATCH"});
        }
        if (entry.archive == "gazzel_98" || entry.archive == "fray_98") {
            fallbacks.push_back({"cabin98", "PATCH_GAZZEL_88/PATCH"});
        }
    } else if (asset.path == "MMD.COM") {
        fallbacks.push_back({"xak2_98", "MMD.COM"});
    } else if (asset.path == "MMD2.COM") {
        fallbacks.push_back({"xak2_98", "MMD2.COM"});
    }

    for (const auto& fallback : fallbacks) {
        hoot::ZipArchive fallback_archive;
        std::string error;
        if (fallback_archive.open(packs_path / (fallback.archive + ".zip"), error)
            && fallback_archive.contains(fallback.member)) {
            return true;
        }
    }
    return false;
}

} // namespace

namespace hoot {

EntryAssetValidation validate_entry_assets(const HootEntry& entry,
                                           const std::filesystem::path& packs_path)
{
    EntryAssetValidation result;
    if (entry.archive.empty()) {
        return result;
    }

    result.archive_required = true;
    result.archive_path = packs_path / (entry.archive + ".zip");
    result.archive_present = std::filesystem::is_regular_file(result.archive_path);
    if (!result.archive_present) {
        result.error = "archive not found: " + result.archive_path.string();
        return result;
    }

    ZipArchive archive;
    if (!archive.open(result.archive_path, result.error)) {
        return result;
    }

    std::set<std::string> checked;
    for (const auto& asset : entry.assets) {
        if (asset.path.empty() || !checked.insert(asset.path).second) {
            continue;
        }
        if (!has_catalog_asset(archive, entry, asset, packs_path)) {
            result.missing_assets.push_back(asset.path);
        }
    }
    return result;
}

} // namespace hoot
