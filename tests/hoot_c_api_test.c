#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "core/hoot_api.h"

int main(void)
{
    if (hoot_get_api_version() != HOOT_API_VERSION) {
        fprintf(stderr, "libhoot API version mismatch\n");
        return 1;
    }
    if (HOOT_VISUAL_ABI_VERSION != 1) {
        fprintf(stderr, "unexpected visual ABI version\n");
        return 1;
    }

    HootConfig config;
    memset(&config, 0, sizeof(config));
    config.sample_rate = 44100;
    config.packs_path = ".";
    HootContext* ctx = hoot_create(&config);
    if (ctx == NULL) {
        fprintf(stderr, "hoot_create failed\n");
        return 1;
    }
    if (hoot_get_entry_count(ctx) != 0) {
        fprintf(stderr, "empty context unexpectedly has catalogue entries\n");
        hoot_destroy(ctx);
        return 1;
    }
    /* Additive visual-channel mute API must be linkable and reject an empty
       context cleanly rather than exposing frontend-specific state. */
    if (hoot_can_mute_channel(ctx, 0) != 0 ||
        hoot_set_channel_muted(ctx, 0, 1) == HOOT_OK ||
        hoot_clear_channel_mutes(ctx) == HOOT_OK) {
        fprintf(stderr, "empty-context channel mute API returned an invalid success\n");
        hoot_destroy(ctx);
        return 1;
    }
    hoot_destroy(ctx);
    return 0;
}
