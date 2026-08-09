#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
    HOOT_TRACK_TITLE_MAX = 128,
    HOOT_DRIVER_NAME_MAX = 64,
    HOOT_TRACK_WARNING_MAX = 256
};

typedef struct HootTrackInfo {
    int track_index;
    char title[HOOT_TRACK_TITLE_MAX];
    char driver[HOOT_DRIVER_NAME_MAX];
    char warning[HOOT_TRACK_WARNING_MAX];
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
    uint64_t debug_unsupported_opcodes;
    uint32_t debug_last_unsupported_opcode;
    uint32_t debug_last_unsupported_cs;
    uint32_t debug_last_unsupported_ip;
    uint64_t debug_pcm8_commands;
    uint64_t debug_pcm8_starts;
    uint64_t debug_pcm8_stops;
    uint64_t debug_pcm8_mode_changes;
    uint64_t debug_pcm8_queries;
    uint64_t debug_pcm8_unimplemented;
    uint64_t debug_pcm8_unknown;
    uint64_t debug_pcm8_unsupported_channels;
    uint64_t debug_pcm8_rendered_voice_frames;
    uint64_t debug_pcm8_rendered_source_bytes;
    uint64_t debug_pcm8_completed_voices;
    uint64_t debug_pcm8_memory_faults;
    uint32_t debug_pcm8_active_voices;
    uint32_t debug_pcm8_last_d0;
    uint32_t debug_pcm8_last_d1;
    uint32_t debug_pcm8_last_d2;
    uint32_t debug_pcm8_last_a1;
    uint32_t debug_pcm8_last_kind;
    int32_t debug_pcm8_last_channel;
    uint64_t debug_midi_bytes_enqueued;
    uint64_t debug_midi_bytes_transmitted;
    uint64_t debug_midi_channel_messages;
    uint64_t debug_midi_system_common_messages;
    uint64_t debug_midi_sysex_messages;
    uint64_t debug_midi_sysex_bytes;
    uint64_t debug_midi_realtime_bytes;
    uint64_t debug_midi_running_status_messages;
    uint64_t debug_midi_malformed_bytes;
    uint64_t debug_midi_note_ons;
    uint64_t debug_midi_note_offs;
    uint64_t debug_midi_control_changes;
    uint64_t debug_midi_program_changes;
    uint64_t debug_midi_pitch_bends;
    uint64_t debug_midi_fifo_full_transitions;
    uint64_t debug_midi_irq_count;
    uint64_t debug_midi_synth_frames;
    uint64_t debug_midi_sysex_handled;
    uint32_t debug_midi_fifo_bytes;
    uint32_t debug_midi_peak_fifo_bytes;
    uint32_t debug_midi_last_status;
    uint32_t debug_midi_backend_active;
    uint32_t debug_midi_backend_kind; // 0 none, 1 FluidSynth, 2 Nuked-SC55 CLAP, 3 Munt MT-32, 4 Munt CM-32L,
                                      // 5 full CM-64 (Munt LA + CM-32P), 6 CM-32P only, 7 Vermouth
    int32_t debug_midiout_type;
    uint32_t debug_x68k_startup_policy;
    uint32_t debug_x68k_startup_mode;
    uint64_t debug_x68k_startup_fallbacks;
    uint32_t debug_x68k_mailbox_pending;
    uint64_t debug_pcm86_port_writes;
    uint64_t debug_pcm86_fifo_writes;
    uint64_t debug_pcm86_fifo_reads;
    uint64_t debug_pcm86_rendered_frames;
    uint64_t debug_pcm86_rendered_source_frames;
    uint64_t debug_pcm86_irq_requests;
    uint64_t debug_pcm86_irq_deliveries;
    uint64_t debug_pcm86_fifo_overflows;
    uint32_t debug_pcm86_fifo_bytes;
    uint32_t debug_pcm86_peak_fifo_bytes;
    uint32_t debug_pcm86_fifo_threshold;
    uint32_t debug_pcm86_fifo_control;
    uint32_t debug_pcm86_dac_control;
    uint32_t debug_pcm86_volume_code;
    uint32_t debug_pcm86_source_rate_millihz;
    uint64_t debug_beep_pit_data_writes;
    uint64_t debug_beep_pit_control_writes;
    uint64_t debug_beep_ppi_writes;
    uint64_t debug_beep_gate_changes;
    uint64_t debug_beep_divider_changes;
    uint64_t debug_beep_rendered_frames;
    uint64_t debug_beep_audible_frames;
    uint64_t debug_beep_vrtc_irqs;
    uint32_t debug_beep_divider;
    uint32_t debug_beep_frequency_millihz;
    uint32_t debug_beep_enabled;
    uint32_t debug_beep_mode;
    uint32_t debug_beep_min_divider;
    uint32_t debug_beep_max_divider;
} HootTrackInfo;

#ifdef __cplusplus
}
#endif
