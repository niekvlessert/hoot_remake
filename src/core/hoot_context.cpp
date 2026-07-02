#include "core/hoot_context.h"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <set>
#include <vector>

#include "config/hoot_xml_loader.h"
#include "drivers/microcabin_pc88_driver.h"
#include "drivers/microcabin_pc98dos_driver.h"
#include "drivers/pc98_dos_driver.h"
#include "drivers/x68k_generic_driver.h"
#include "io/d88_image.h"
#include "io/filesystem_asset_provider.h"
#include "io/zip_archive.h"

namespace {

template <size_t N>
void copy_c_string(char (&dest)[N], const std::string& source)
{
    static_assert(N > 0, "destination must have room for a terminator");
    const auto count = std::min(source.size(), N - 1);
    std::memcpy(dest, source.data(), count);
    dest[count] = '\0';
}

bool has_catalog_asset(const hoot::ZipArchive& archive,
                       const hoot::HootEntry& entry,
                       const hoot::HootAssetRef& asset,
                       const std::filesystem::path& packs_path)
{
    if (asset.type == "device" || asset.type == "shell") {
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

std::unique_ptr<hoot::HootDriver> create_driver_for_entry(const hoot::HootEntry& entry)
{
    if (entry.driver_alias == "microcabin/pc88"
        || (entry.driver_name == "pc88/opn" && entry.archive == "xak2_98")) {
        return std::make_unique<hoot::MicrocabinPc88Driver>();
    }
    if (entry.driver_name == "pc98dos/opn") {
        if (entry.driver_alias.find("MICROCABIN") != std::string::npos) {
            return std::make_unique<hoot::MicrocabinPc98DosDriver>();
        }
        return std::make_unique<hoot::Pc98DosDriver>();
    }
    if (entry.driver_name == "x68k/generic") {
        return std::make_unique<hoot::X68kGenericDriver>();
    }
    return nullptr;
}

} // namespace

HootContext::HootContext(const HootConfig* config)
{
    if (config != nullptr) {
        if (config->sample_rate > 0) {
            sample_rate = config->sample_rate;
        }
        if (config->packs_path != nullptr) {
            packs_path = config->packs_path;
        }
    }

    asset_provider = std::make_unique<hoot::FilesystemAssetProvider>(packs_path);
}

void HootContext::set_error(std::string message)
{
    last_error = std::move(message);
}

extern "C" {

HootContext* hoot_create(const HootConfig* config)
{
    return new HootContext(config);
}

void hoot_destroy(HootContext* ctx)
{
    delete ctx;
}

HootResult hoot_load_xml(HootContext* ctx, const char* xml_path)
{
    if (ctx == nullptr || xml_path == nullptr) {
        return HOOT_ERROR_INVALID_ARGUMENT;
    }

    ctx->catalog.clear();
    ctx->current_entry = nullptr;

    hoot::HootXmlLoader loader;
    std::string error;
    if (!loader.load_file(xml_path, ctx->catalog, error)) {
        ctx->set_error(error);
        return HOOT_ERROR_PARSE;
    }
    return HOOT_OK;
}

HootResult hoot_load_entry(HootContext* ctx, const char* entry_id)
{
    if (ctx == nullptr || entry_id == nullptr) {
        return HOOT_ERROR_INVALID_ARGUMENT;
    }

    const auto* entry = ctx->catalog.find(entry_id);
    if (entry == nullptr) {
        ctx->set_error(std::string("entry not found: ") + entry_id);
        return HOOT_ERROR_NOT_FOUND;
    }
    if (!entry->archive.empty()) {
        const auto archive_path = std::filesystem::path(ctx->packs_path) / (entry->archive + ".zip");
        hoot::ZipArchive archive;
        std::string error;
        if (!archive.open(archive_path, error)) {
            ctx->set_error(error);
            return HOOT_ERROR_IO;
        }

        std::set<std::string> checked;
        for (const auto& asset : entry->assets) {
            if (asset.path.empty() || checked.find(asset.path) != checked.end()) {
                continue;
            }
            checked.insert(asset.path);
            if (!has_catalog_asset(archive, *entry, asset, std::filesystem::path(ctx->packs_path))) {
                ctx->set_error("missing archive member in " + archive_path.string() + ": " + asset.path);
                return HOOT_ERROR_NOT_FOUND;
            }
        }
    }

    auto driver = create_driver_for_entry(*entry);
    if (driver != nullptr) {
        std::string error;
        const auto result = driver->load(*entry, ctx->packs_path, ctx->sample_rate, error);
        if (result != HOOT_OK) {
            ctx->set_error(error);
            return result;
        }
    }

    ctx->current_entry = entry;
    ctx->current_driver = std::move(driver);
    ctx->selected_track = 0;
    return HOOT_OK;
}

HootResult hoot_select_track(HootContext* ctx, int track_index)
{
    if (ctx == nullptr || track_index < 0) {
        return HOOT_ERROR_INVALID_ARGUMENT;
    }
    if (ctx->current_driver != nullptr && ctx->current_entry != nullptr) {
        std::string error;
        const auto result = ctx->current_driver->select_track(*ctx->current_entry, track_index, error);
        if (result != HOOT_OK) {
            ctx->set_error(error);
            return result;
        }
    }
    ctx->selected_track = track_index;
    return HOOT_OK;
}

HootResult hoot_reset(HootContext* ctx)
{
    if (ctx == nullptr) {
        return HOOT_ERROR_INVALID_ARGUMENT;
    }
    if (ctx->current_driver != nullptr) {
        ctx->current_driver->reset();
    }
    return HOOT_OK;
}

int hoot_render_s16(HootContext* ctx, int16_t* interleaved_stereo, int frames)
{
    if (ctx == nullptr || interleaved_stereo == nullptr || frames < 0) {
        return 0;
    }
    if (ctx->current_driver != nullptr) {
        return ctx->current_driver->render_s16(interleaved_stereo, frames);
    }
    std::fill(interleaved_stereo, interleaved_stereo + (frames * 2), int16_t{0});
    return frames;
}

int hoot_render_float(HootContext* ctx, float* interleaved_stereo, int frames)
{
    if (ctx == nullptr || interleaved_stereo == nullptr || frames < 0) {
        return 0;
    }
    if (ctx->current_driver != nullptr) {
        return ctx->current_driver->render_float(interleaved_stereo, frames);
    }
    std::fill(interleaved_stereo, interleaved_stereo + (frames * 2), 0.0f);
    return frames;
}

HootResult hoot_get_track_info(HootContext* ctx, HootTrackInfo* out)
{
    if (ctx == nullptr || out == nullptr) {
        return HOOT_ERROR_INVALID_ARGUMENT;
    }
    if (ctx->current_entry == nullptr) {
        ctx->set_error("no entry loaded");
        return HOOT_ERROR_NOT_LOADED;
    }

    if (ctx->current_driver != nullptr) {
        ctx->current_driver->fill_track_info(*ctx->current_entry, ctx->selected_track, *out);
        return HOOT_OK;
    }

    std::memset(out, 0, sizeof(*out));
    out->track_index = ctx->selected_track;
    out->sample_rate = ctx->sample_rate;
    copy_c_string(out->driver, ctx->current_entry->driver_name);

    if (ctx->selected_track >= 0
        && static_cast<size_t>(ctx->selected_track) < ctx->current_entry->tracks.size()) {
        copy_c_string(out->title, ctx->current_entry->tracks[ctx->selected_track].title);
    } else {
        copy_c_string(out->title, ctx->current_entry->title);
    }

    return HOOT_OK;
}

const char* hoot_last_error(HootContext* ctx)
{
    if (ctx == nullptr) {
        return "null context";
    }
    return ctx->last_error.c_str();
}

} // extern "C"
