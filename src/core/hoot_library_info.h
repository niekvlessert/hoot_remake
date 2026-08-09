#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
    HOOT_ENTRY_ID_MAX = 128,
    HOOT_ENTRY_TITLE_MAX = 256,
    HOOT_ENTRY_ARCHIVE_MAX = 128,
    HOOT_ENTRY_DRIVER_MAX = 64,
    HOOT_CATALOG_TRACK_TITLE_MAX = 256,
    HOOT_CATALOG_VOICE_BANK_MAX = 128,
    HOOT_ENTRY_OPTION_NAME_MAX = 128,
    HOOT_ENTRY_ASSET_TYPE_MAX = 64,
    HOOT_ENTRY_ASSET_PATH_MAX = 512,
    HOOT_ENTRY_ASSET_TRANSFORM_MAX = 128,
    HOOT_ENTRY_DRIVER_ALIAS_MAX = 256
};

typedef struct HootEntryInfo {
    int index;
    int track_count;
    int refresh_hz;
    int default_sample_rate;
    char id[HOOT_ENTRY_ID_MAX];
    char title[HOOT_ENTRY_TITLE_MAX];
    char archive[HOOT_ENTRY_ARCHIVE_MAX];
    char driver[HOOT_ENTRY_DRIVER_MAX];
} HootEntryInfo;

typedef struct HootCatalogTrackInfo {
    int index;
    uint32_t code;
    char title[HOOT_CATALOG_TRACK_TITLE_MAX];
} HootCatalogTrackInfo;

typedef struct HootEntryTrackInfo {
    int index;
    uint32_t code;
    char title[HOOT_CATALOG_TRACK_TITLE_MAX];
    char voice_bank[HOOT_CATALOG_VOICE_BANK_MAX];
} HootEntryTrackInfo;


typedef struct HootEntryDriverInfo {
    int index;
    char alias[HOOT_ENTRY_DRIVER_ALIAS_MAX];
} HootEntryDriverInfo;

typedef struct HootEntryOptionInfo {
    int index;
    int value;
    char name[HOOT_ENTRY_OPTION_NAME_MAX];
} HootEntryOptionInfo;

typedef struct HootEntryAssetInfo {
    int index;
    uint32_t offset;
    uint32_t crc32;
    int has_crc32;
    char type[HOOT_ENTRY_ASSET_TYPE_MAX];
    char path[HOOT_ENTRY_ASSET_PATH_MAX];
    char transform[HOOT_ENTRY_ASSET_TRANSFORM_MAX];
} HootEntryAssetInfo;

#ifdef __cplusplus
}
#endif
