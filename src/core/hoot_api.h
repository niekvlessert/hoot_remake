#pragma once

#include <stdint.h>

#include "hoot_errors.h"
#include "hoot_track_info.h"
#include "hoot_visual_state.h"
#include "hoot_library_info.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct HootContext HootContext;

enum { HOOT_API_VERSION = 1 };

#if defined(_WIN32) && defined(HOOT_BUILD_SHARED)
#  define HOOT_API __declspec(dllexport)
#elif defined(_WIN32) && !defined(HOOT_STATIC)
#  define HOOT_API __declspec(dllimport)
#elif defined(__GNUC__)
#  define HOOT_API __attribute__((visibility("default")))
#else
#  define HOOT_API
#endif

typedef struct HootConfig {
    int sample_rate;
    const char* packs_path;
} HootConfig;

typedef enum HootSupportStatus {
    HOOT_SUPPORT_UNSUPPORTED = 0,
    HOOT_SUPPORT_RECOGNIZED = 1,
    HOOT_SUPPORT_EXPERIMENTAL = 2,
    HOOT_SUPPORT_PLAYABLE = 3,
    HOOT_SUPPORT_VERIFIED = 4
} HootSupportStatus;

enum {
    HOOT_PROBE_DRIVER_ID_MAX = 64,
    HOOT_PROBE_REASON_MAX = 256
};

typedef struct HootDriverProbe {
    HootSupportStatus status;
    char driver_id[HOOT_PROBE_DRIVER_ID_MAX];
    char reason[HOOT_PROBE_REASON_MAX];
} HootDriverProbe;

HOOT_API uint32_t hoot_get_api_version(void);
HOOT_API HootContext* hoot_create(const HootConfig* config);
HOOT_API void hoot_destroy(HootContext* ctx);
HOOT_API HootResult hoot_set_packs_path(HootContext* ctx, const char* packs_path);

HOOT_API HootResult hoot_load_catalog(HootContext* ctx, const char* catalog_path);
// Backward-compatible alias; accepts all formats supported by hoot_load_catalog.
HOOT_API HootResult hoot_load_xml(HootContext* ctx, const char* xml_path);
HOOT_API HootResult hoot_load_entry(HootContext* ctx, const char* entry_id);
HOOT_API HootResult hoot_probe_entry(HootContext* ctx, const char* entry_id, HootDriverProbe* out);
HOOT_API const char* hoot_support_status_name(HootSupportStatus status);
HOOT_API HootResult hoot_select_track(HootContext* ctx, int track_index);
HOOT_API HootResult hoot_reset(HootContext* ctx);

HOOT_API int hoot_render_s16(HootContext* ctx, int16_t* interleaved_stereo, int frames);
HOOT_API int hoot_render_float(HootContext* ctx, float* interleaved_stereo, int frames);

HOOT_API HootResult hoot_get_track_info(HootContext* ctx, HootTrackInfo* out);
HOOT_API const char* hoot_last_error(HootContext* ctx);
/* Library/front-end discovery helpers. */
HOOT_API int hoot_get_entry_count(HootContext* ctx);
HOOT_API HootResult hoot_get_entry_info(HootContext* ctx, int index, HootEntryInfo* out);
HOOT_API HootResult hoot_find_entry(HootContext* ctx, const char* id_or_archive, HootEntryInfo* out);
HOOT_API HootResult hoot_load_archive(HootContext* ctx, const char* archive_or_zip);
HOOT_API int hoot_get_track_count(HootContext* ctx);
HOOT_API HootResult hoot_get_catalog_track_info(HootContext* ctx, int track_index, HootCatalogTrackInfo* out);
HOOT_API HootResult hoot_get_entry_catalog_track_info(HootContext* ctx, int entry_index, int track_index, HootEntryTrackInfo* out);
HOOT_API HootResult hoot_get_entry_driver_info(HootContext* ctx, int entry_index, HootEntryDriverInfo* out);
HOOT_API int hoot_get_entry_option_count(HootContext* ctx, int entry_index);
HOOT_API HootResult hoot_get_entry_option_info(HootContext* ctx, int entry_index, int option_index, HootEntryOptionInfo* out);
HOOT_API int hoot_get_entry_asset_count(HootContext* ctx, int entry_index);
HOOT_API HootResult hoot_get_entry_asset_info(HootContext* ctx, int entry_index, int asset_index, HootEntryAssetInfo* out);
HOOT_API HootResult hoot_get_visual_state(HootContext* ctx, HootVisualState* out);
// Channel indices refer to the current HootVisualState.channels[] ordering.
HOOT_API int hoot_can_mute_channel(HootContext* ctx, int visual_channel_index);
HOOT_API HootResult hoot_set_channel_muted(HootContext* ctx, int visual_channel_index, int muted);
HOOT_API HootResult hoot_clear_channel_mutes(HootContext* ctx);


#ifdef __cplusplus
}
#endif
