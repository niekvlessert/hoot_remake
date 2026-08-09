#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
    HOOT_VISUAL_ABI_VERSION = 1,
    HOOT_VISUAL_ARCH_MAX = 32,
    HOOT_VISUAL_CPU_MAX = 32,
    HOOT_VISUAL_DEVICE_MAX = 64,
    HOOT_VISUAL_DRIVER_MAX = 64,
    HOOT_VISUAL_CHANNELS_MAX = 32,
    HOOT_VISUAL_REGISTERS_MAX = 32,
    HOOT_VISUAL_CHANNEL_LABEL_MAX = 32,
    HOOT_VISUAL_REGISTER_LABEL_MAX = 12,
    HOOT_VISUAL_REGISTER_VALUE_MAX = 20,
    HOOT_VISUAL_DRIVER_WORK_MAX = 512
};

typedef enum HootVisualChannelKind {
    HOOT_VISUAL_CHANNEL_UNKNOWN = 0,
    HOOT_VISUAL_CHANNEL_FM = 1,
    HOOT_VISUAL_CHANNEL_SSG = 2,
    HOOT_VISUAL_CHANNEL_ADPCM = 3,
    HOOT_VISUAL_CHANNEL_RHYTHM = 4,
    HOOT_VISUAL_CHANNEL_PCM = 5,
    HOOT_VISUAL_CHANNEL_MIDI = 6,
    HOOT_VISUAL_CHANNEL_BEEP = 7,
    HOOT_VISUAL_CHANNEL_OPL = 8
} HootVisualChannelKind;

typedef struct HootVisualChannel {
    int kind;
    int index;
    int active;
    int midi_note;       /* 0..127 primary/last note, -1 when unavailable. */
    uint64_t key_mask_lo; /* MIDI notes 0..63 currently active. */
    uint64_t key_mask_hi; /* MIDI notes 64..127 currently active. */
    int volume;          /* 0..127. */
    int pan;             /* -64 left .. 0 centre .. +63 right. */
    int instrument;      /* -1 when unavailable. */
    float level;         /* 0..1 activity estimate, not a calibrated dB value. */
    char label[HOOT_VISUAL_CHANNEL_LABEL_MAX];
} HootVisualChannel;

typedef struct HootVisualRegister {
    char label[HOOT_VISUAL_REGISTER_LABEL_MAX];
    char value[HOOT_VISUAL_REGISTER_VALUE_MAX];
} HootVisualRegister;

typedef struct HootVisualState {
    uint32_t abi_version;
    uint32_t struct_size;
    char architecture[HOOT_VISUAL_ARCH_MAX];
    char cpu[HOOT_VISUAL_CPU_MAX];
    char device[HOOT_VISUAL_DEVICE_MAX];
    char driver[HOOT_VISUAL_DRIVER_MAX];

    uint64_t rendered_frames;
    uint32_t sample_rate;
    uint32_t channel_count;
    HootVisualChannel channels[HOOT_VISUAL_CHANNELS_MAX];

    uint32_t register_count;
    HootVisualRegister registers[HOOT_VISUAL_REGISTERS_MAX];

    uint32_t driver_work_base;
    uint32_t driver_work_size;
    uint8_t driver_work[HOOT_VISUAL_DRIVER_WORK_MAX];
} HootVisualState;

#ifdef __cplusplus
}
#endif
