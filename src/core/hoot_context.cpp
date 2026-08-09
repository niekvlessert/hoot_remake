#include "core/hoot_context.h"
#include "core/utf8_util.h"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <vector>
#include <cctype>

#include "config/hoot_catalog_loader.h"
#include "core/entry_validation.h"
#include "drivers/driver_registry.h"
#include "io/filesystem_asset_provider.h"

namespace {

template <size_t N>
void copy_c_string(char (&dest)[N], const std::string& source)
{
    hoot::utf8::copy_c_string(dest, source);
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

uint32_t hoot_get_api_version(void) { return HOOT_API_VERSION; }

HootContext* hoot_create(const HootConfig* config)
{
    return new HootContext(config);
}

void hoot_destroy(HootContext* ctx)
{
    delete ctx;
}

HootResult hoot_set_packs_path(HootContext* ctx, const char* packs_path)
{
    if (ctx == nullptr || packs_path == nullptr) return HOOT_ERROR_INVALID_ARGUMENT;
    ctx->packs_path = packs_path;
    ctx->asset_provider = std::make_unique<hoot::FilesystemAssetProvider>(ctx->packs_path);
    return HOOT_OK;
}

HootResult hoot_load_catalog(HootContext* ctx, const char* catalog_path)
{
    if (ctx == nullptr || catalog_path == nullptr) {
        return HOOT_ERROR_INVALID_ARGUMENT;
    }

    ctx->catalog.clear();
    ctx->current_entry = nullptr;
    ctx->current_driver.reset();
    ctx->selected_track = 0;
    ctx->rendered_frames = 0;

    hoot::HootCatalogLoader loader;
    std::string error;
    if (!loader.load_file(catalog_path, ctx->catalog, error)) {
        ctx->set_error(error);
        return HOOT_ERROR_PARSE;
    }
    return HOOT_OK;
}

HootResult hoot_load_xml(HootContext* ctx, const char* xml_path)
{
    return hoot_load_catalog(ctx, xml_path);
}

HootResult hoot_load_entry(HootContext* ctx, const char* entry_id)
{
    if (ctx == nullptr || entry_id == nullptr) {
        return HOOT_ERROR_INVALID_ARGUMENT;
    }

    ctx->current_entry = nullptr;
    ctx->current_driver.reset();
    ctx->selected_track = 0;
    ctx->rendered_frames = 0;

    const auto* entry = ctx->catalog.find(entry_id);
    if (entry == nullptr) {
        ctx->set_error(std::string("entry not found: ") + entry_id);
        return HOOT_ERROR_NOT_FOUND;
    }
    const auto probe = hoot::DriverRegistry::instance().probe(*entry);
    if (!probe.supported()) {
        ctx->set_error(probe.reason);
        return HOOT_ERROR_UNSUPPORTED;
    }

    const auto validation = hoot::validate_entry_assets(*entry, std::filesystem::path(ctx->packs_path));
    if (!validation.archive_present || !validation.error.empty()) {
        ctx->set_error(validation.error);
        return HOOT_ERROR_IO;
    }
    if (!validation.missing_assets.empty()) {
        ctx->set_error("missing archive member in " + validation.archive_path.string()
            + ": " + validation.missing_assets.front());
        return HOOT_ERROR_NOT_FOUND;
    }

    auto driver = hoot::DriverRegistry::instance().create(*entry);
    if (driver == nullptr) {
        ctx->set_error("driver registry matched entry but did not create a driver: " + probe.driver_id);
        return HOOT_ERROR_UNSUPPORTED;
    }
    {
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

HootResult hoot_probe_entry(HootContext* ctx, const char* entry_id, HootDriverProbe* out)
{
    if (ctx == nullptr || entry_id == nullptr || out == nullptr) {
        return HOOT_ERROR_INVALID_ARGUMENT;
    }
    const auto* entry = ctx->catalog.find(entry_id);
    if (entry == nullptr) {
        ctx->set_error(std::string("entry not found: ") + entry_id);
        return HOOT_ERROR_NOT_FOUND;
    }

    const auto probe = hoot::DriverRegistry::instance().probe(*entry);
    std::memset(out, 0, sizeof(*out));
    out->status = static_cast<HootSupportStatus>(probe.status);
    copy_c_string(out->driver_id, probe.driver_id);
    copy_c_string(out->reason, probe.reason);
    return HOOT_OK;
}

const char* hoot_support_status_name(HootSupportStatus status)
{
    return hoot::driver_support_status_name(static_cast<hoot::DriverSupportStatus>(status));
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
    ctx->rendered_frames = 0;
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
    ctx->rendered_frames = 0;
    return HOOT_OK;
}

int hoot_render_s16(HootContext* ctx, int16_t* interleaved_stereo, int frames)
{
    if (ctx == nullptr || interleaved_stereo == nullptr || frames < 0) {
        return 0;
    }
    if (ctx->current_driver != nullptr) {
        const int rendered = ctx->current_driver->render_s16(interleaved_stereo, frames);
        if (rendered > 0) ctx->rendered_frames += static_cast<uint64_t>(rendered);
        return rendered;
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
        const int rendered = ctx->current_driver->render_float(interleaved_stereo, frames);
        if (rendered > 0) ctx->rendered_frames += static_cast<uint64_t>(rendered);
        return rendered;
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


int hoot_get_entry_count(HootContext* ctx)
{
    if (ctx == nullptr) return 0;
    return static_cast<int>(ctx->catalog.entries().size());
}

HootResult hoot_get_entry_info(HootContext* ctx, int index, HootEntryInfo* out)
{
    if (ctx == nullptr || out == nullptr || index < 0) return HOOT_ERROR_INVALID_ARGUMENT;
    const auto& entries = ctx->catalog.entries();
    if (static_cast<size_t>(index) >= entries.size()) return HOOT_ERROR_NOT_FOUND;
    const auto& entry = entries[static_cast<size_t>(index)];
    std::memset(out, 0, sizeof(*out));
    out->index = index;
    out->track_count = static_cast<int>(entry.tracks.size());
    out->refresh_hz = entry.refresh_hz;
    out->default_sample_rate = entry.default_sample_rate;
    copy_c_string(out->id, entry.id);
    copy_c_string(out->title, entry.title);
    copy_c_string(out->archive, entry.archive);
    copy_c_string(out->driver, entry.driver_name);
    return HOOT_OK;
}

HootResult hoot_find_entry(HootContext* ctx, const char* id_or_archive, HootEntryInfo* out)
{
    if (ctx == nullptr || id_or_archive == nullptr || out == nullptr) return HOOT_ERROR_INVALID_ARGUMENT;
    std::string needle = id_or_archive;
    std::filesystem::path path(needle);
    auto ext = path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    if (ext == ".zip") needle = path.stem().string();
    const auto& entries = ctx->catalog.entries();
    const hoot::HootEntry* first_archive = nullptr;
    int first_archive_index = -1;
    for (size_t i = 0; i < entries.size(); ++i) {
        const auto& entry = entries[i];
        if (entry.id == needle) return hoot_get_entry_info(ctx, static_cast<int>(i), out);
        if (entry.archive == needle && first_archive == nullptr) {
            first_archive = &entry;
            first_archive_index = static_cast<int>(i);
        }
        if (entry.archive == needle && hoot::DriverRegistry::instance().probe(entry).supported()) {
            return hoot_get_entry_info(ctx, static_cast<int>(i), out);
        }
    }
    if (first_archive != nullptr) return hoot_get_entry_info(ctx, first_archive_index, out);
    ctx->set_error("entry/archive not found: " + needle);
    return HOOT_ERROR_NOT_FOUND;
}

HootResult hoot_load_archive(HootContext* ctx, const char* archive_or_zip)
{
    if (ctx == nullptr || archive_or_zip == nullptr) return HOOT_ERROR_INVALID_ARGUMENT;
    HootEntryInfo info{};
    const auto result = hoot_find_entry(ctx, archive_or_zip, &info);
    if (result != HOOT_OK) return result;
    return hoot_load_entry(ctx, info.id);
}

int hoot_get_track_count(HootContext* ctx)
{
    if (ctx == nullptr || ctx->current_entry == nullptr) return 0;
    return static_cast<int>(ctx->current_entry->tracks.size());
}

HootResult hoot_get_entry_catalog_track_info(HootContext* ctx, int entry_index, int track_index, HootEntryTrackInfo* out)
{
    if (ctx == nullptr || out == nullptr || entry_index < 0 || track_index < 0) return HOOT_ERROR_INVALID_ARGUMENT;
    const auto& entries = ctx->catalog.entries();
    if (static_cast<size_t>(entry_index) >= entries.size()) return HOOT_ERROR_NOT_FOUND;
    const auto& tracks = entries[static_cast<size_t>(entry_index)].tracks;
    if (static_cast<size_t>(track_index) >= tracks.size()) return HOOT_ERROR_NOT_FOUND;
    const auto& track = tracks[static_cast<size_t>(track_index)];
    std::memset(out, 0, sizeof(*out));
    out->index = track_index;
    out->code = track.code;
    copy_c_string(out->title, track.title);
    copy_c_string(out->voice_bank, track.voice_bank);
    return HOOT_OK;
}

HootResult hoot_get_catalog_track_info(HootContext* ctx, int track_index, HootCatalogTrackInfo* out)
{
    if (ctx == nullptr || out == nullptr || track_index < 0) return HOOT_ERROR_INVALID_ARGUMENT;
    if (ctx->current_entry == nullptr) return HOOT_ERROR_NOT_LOADED;
    if (static_cast<size_t>(track_index) >= ctx->current_entry->tracks.size()) return HOOT_ERROR_NOT_FOUND;
    const auto& track = ctx->current_entry->tracks[static_cast<size_t>(track_index)];
    std::memset(out, 0, sizeof(*out));
    out->index = track_index;
    out->code = track.code;
    copy_c_string(out->title, track.title);
    return HOOT_OK;
}

HootResult hoot_get_entry_driver_info(HootContext* ctx, int entry_index, HootEntryDriverInfo* out)
{
    if (!ctx || !out || entry_index < 0) return HOOT_ERROR_INVALID_ARGUMENT;
    const auto& entries = ctx->catalog.entries();
    if (static_cast<size_t>(entry_index) >= entries.size()) return HOOT_ERROR_NOT_FOUND;
    std::memset(out, 0, sizeof(*out));
    out->index = entry_index;
    copy_c_string(out->alias, entries[static_cast<size_t>(entry_index)].driver_alias);
    return HOOT_OK;
}

int hoot_get_entry_option_count(HootContext* ctx, int entry_index)
{
    if (!ctx || entry_index < 0 || static_cast<size_t>(entry_index) >= ctx->catalog.entries().size()) return 0;
    return static_cast<int>(ctx->catalog.entries()[static_cast<size_t>(entry_index)].options.size());
}

HootResult hoot_get_entry_option_info(HootContext* ctx, int entry_index, int option_index, HootEntryOptionInfo* out)
{
    if (!ctx || !out || entry_index < 0 || option_index < 0) return HOOT_ERROR_INVALID_ARGUMENT;
    const auto& entries = ctx->catalog.entries();
    if (static_cast<size_t>(entry_index) >= entries.size()) return HOOT_ERROR_NOT_FOUND;
    const auto& options = entries[static_cast<size_t>(entry_index)].options;
    if (static_cast<size_t>(option_index) >= options.size()) return HOOT_ERROR_NOT_FOUND;
    auto it = options.begin();
    std::advance(it, option_index);
    std::memset(out, 0, sizeof(*out));
    out->index = option_index;
    out->value = it->second;
    copy_c_string(out->name, it->first);
    return HOOT_OK;
}

int hoot_get_entry_asset_count(HootContext* ctx, int entry_index)
{
    if (!ctx || entry_index < 0 || static_cast<size_t>(entry_index) >= ctx->catalog.entries().size()) return 0;
    return static_cast<int>(ctx->catalog.entries()[static_cast<size_t>(entry_index)].assets.size());
}

HootResult hoot_get_entry_asset_info(HootContext* ctx, int entry_index, int asset_index, HootEntryAssetInfo* out)
{
    if (!ctx || !out || entry_index < 0 || asset_index < 0) return HOOT_ERROR_INVALID_ARGUMENT;
    const auto& entries = ctx->catalog.entries();
    if (static_cast<size_t>(entry_index) >= entries.size()) return HOOT_ERROR_NOT_FOUND;
    const auto& assets = entries[static_cast<size_t>(entry_index)].assets;
    if (static_cast<size_t>(asset_index) >= assets.size()) return HOOT_ERROR_NOT_FOUND;
    const auto& asset = assets[static_cast<size_t>(asset_index)];
    std::memset(out, 0, sizeof(*out));
    out->index = asset_index;
    out->offset = asset.offset;
    out->crc32 = asset.crc32;
    out->has_crc32 = asset.has_crc32 ? 1 : 0;
    copy_c_string(out->type, asset.type);
    copy_c_string(out->path, asset.path);
    copy_c_string(out->transform, asset.transform);
    return HOOT_OK;
}

HootResult hoot_get_visual_state(HootContext* ctx, HootVisualState* out)
{
    if (ctx == nullptr || out == nullptr) return HOOT_ERROR_INVALID_ARGUMENT;
    std::memset(out, 0, sizeof(*out));
    out->abi_version = HOOT_VISUAL_ABI_VERSION;
    out->struct_size = sizeof(*out);
    out->sample_rate = static_cast<uint32_t>(ctx->sample_rate);
    out->rendered_frames = ctx->rendered_frames;
    if (ctx->current_entry == nullptr) {
        ctx->set_error("no entry loaded");
        return HOOT_ERROR_NOT_LOADED;
    }
    if (ctx->current_driver != nullptr) {
        ctx->current_driver->fill_visual_state(*ctx->current_entry, ctx->selected_track, *out);
    }
    // The driver may intentionally leave generic fields blank.
    if (out->driver[0] == '\0' && ctx->current_driver != nullptr) copy_c_string(out->driver, ctx->current_driver->name());
    out->sample_rate = static_cast<uint32_t>(ctx->sample_rate);
    out->rendered_frames = ctx->rendered_frames;
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
