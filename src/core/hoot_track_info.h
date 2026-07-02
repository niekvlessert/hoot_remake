#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
    HOOT_TRACK_TITLE_MAX = 128,
    HOOT_DRIVER_NAME_MAX = 64
};

typedef struct HootTrackInfo {
    int track_index;
    char title[HOOT_TRACK_TITLE_MAX];
    char driver[HOOT_DRIVER_NAME_MAX];
    int sample_rate;
    uint64_t debug_cpu_cycles;
    uint64_t debug_io_reads;
    uint64_t debug_io_writes;
    uint64_t debug_opn_writes;
    uint64_t debug_opn_keyons;
    uint32_t debug_pc;
    uint32_t debug_last_opn_reg;
    uint32_t debug_last_opn_data;
    uint64_t debug_port_writes_00;
    uint64_t debug_port_writes_01;
    uint64_t debug_port_writes_02;
    uint64_t debug_port_writes_03;
    uint64_t debug_port_writes_32;
    uint64_t debug_port_writes_44;
    uint64_t debug_port_writes_45;
} HootTrackInfo;

#ifdef __cplusplus
}
#endif
