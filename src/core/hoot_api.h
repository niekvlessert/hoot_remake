#pragma once

#include <stdint.h>

#include "core/hoot_errors.h"
#include "core/hoot_track_info.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct HootContext HootContext;

typedef struct HootConfig {
    int sample_rate;
    const char* packs_path;
} HootConfig;

HootContext* hoot_create(const HootConfig* config);
void hoot_destroy(HootContext* ctx);

HootResult hoot_load_xml(HootContext* ctx, const char* xml_path);
HootResult hoot_load_entry(HootContext* ctx, const char* entry_id);
HootResult hoot_select_track(HootContext* ctx, int track_index);
HootResult hoot_reset(HootContext* ctx);

int hoot_render_s16(HootContext* ctx, int16_t* interleaved_stereo, int frames);
int hoot_render_float(HootContext* ctx, float* interleaved_stereo, int frames);

HootResult hoot_get_track_info(HootContext* ctx, HootTrackInfo* out);
const char* hoot_last_error(HootContext* ctx);

#ifdef __cplusplus
}
#endif
