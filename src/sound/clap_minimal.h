#pragma once

// Minimal CLAP 1.x ABI surface used by the optional Nuked-SC55 host.
// Struct layouts and constants are derived from the official CLAP headers.
// CLAP is MIT licensed; see md/LICENSES.md.

#include <cstdint>

#if defined(_WIN32)
#define HOOT_CLAP_ABI __cdecl
#define HOOT_CLAP_EXPORT __declspec(dllexport)
#else
#define HOOT_CLAP_ABI
#define HOOT_CLAP_EXPORT __attribute__((visibility("default")))
#endif

extern "C" {

struct clap_version_t {
    uint32_t major;
    uint32_t minor;
    uint32_t revision;
};

struct clap_event_transport_t;
struct clap_plugin_t;
struct clap_host_t;
struct clap_process_t;

struct clap_plugin_descriptor_t {
    clap_version_t clap_version;
    const char* id;
    const char* name;
    const char* vendor;
    const char* url;
    const char* manual_url;
    const char* support_url;
    const char* version;
    const char* description;
    const char* const* features;
};

struct clap_event_header_t {
    uint32_t size;
    uint32_t time;
    uint16_t space_id;
    uint16_t type;
    uint32_t flags;
};

struct clap_event_midi_t {
    clap_event_header_t header;
    uint16_t port_index;
    uint8_t data[3];
};

struct clap_event_midi_sysex_t {
    clap_event_header_t header;
    uint16_t port_index;
    const uint8_t* buffer;
    uint32_t size;
};

struct clap_input_events_t {
    void* ctx;
    uint32_t (HOOT_CLAP_ABI *size)(const clap_input_events_t* list);
    const clap_event_header_t* (HOOT_CLAP_ABI *get)(const clap_input_events_t* list,
                                                     uint32_t index);
};

struct clap_output_events_t {
    void* ctx;
    bool (HOOT_CLAP_ABI *try_push)(const clap_output_events_t* list,
                                    const clap_event_header_t* event);
};

struct clap_audio_buffer_t {
    float** data32;
    double** data64;
    uint32_t channel_count;
    uint32_t latency;
    uint64_t constant_mask;
};

using clap_process_status = int32_t;

struct clap_process_t {
    int64_t steady_time;
    uint32_t frames_count;
    const clap_event_transport_t* transport;
    const clap_audio_buffer_t* audio_inputs;
    clap_audio_buffer_t* audio_outputs;
    uint32_t audio_inputs_count;
    uint32_t audio_outputs_count;
    const clap_input_events_t* in_events;
    const clap_output_events_t* out_events;
};

struct clap_host_t {
    clap_version_t clap_version;
    void* host_data;
    const char* name;
    const char* vendor;
    const char* url;
    const char* version;
    const void* (HOOT_CLAP_ABI *get_extension)(const clap_host_t* host,
                                               const char* extension_id);
    void (HOOT_CLAP_ABI *request_restart)(const clap_host_t* host);
    void (HOOT_CLAP_ABI *request_process)(const clap_host_t* host);
    void (HOOT_CLAP_ABI *request_callback)(const clap_host_t* host);
};

struct clap_plugin_t {
    const clap_plugin_descriptor_t* desc;
    void* plugin_data;
    bool (HOOT_CLAP_ABI *init)(const clap_plugin_t* plugin);
    void (HOOT_CLAP_ABI *destroy)(const clap_plugin_t* plugin);
    bool (HOOT_CLAP_ABI *activate)(const clap_plugin_t* plugin,
                                    double sample_rate,
                                    uint32_t min_frames_count,
                                    uint32_t max_frames_count);
    void (HOOT_CLAP_ABI *deactivate)(const clap_plugin_t* plugin);
    bool (HOOT_CLAP_ABI *start_processing)(const clap_plugin_t* plugin);
    void (HOOT_CLAP_ABI *stop_processing)(const clap_plugin_t* plugin);
    void (HOOT_CLAP_ABI *reset)(const clap_plugin_t* plugin);
    clap_process_status (HOOT_CLAP_ABI *process)(const clap_plugin_t* plugin,
                                                 const clap_process_t* process);
    const void* (HOOT_CLAP_ABI *get_extension)(const clap_plugin_t* plugin,
                                               const char* id);
    void (HOOT_CLAP_ABI *on_main_thread)(const clap_plugin_t* plugin);
};

struct clap_plugin_factory_t {
    uint32_t (HOOT_CLAP_ABI *get_plugin_count)(const clap_plugin_factory_t* factory);
    const clap_plugin_descriptor_t* (HOOT_CLAP_ABI *get_plugin_descriptor)(
        const clap_plugin_factory_t* factory, uint32_t index);
    const clap_plugin_t* (HOOT_CLAP_ABI *create_plugin)(const clap_plugin_factory_t* factory,
                                                        const clap_host_t* host,
                                                        const char* plugin_id);
};

struct clap_plugin_entry_t {
    clap_version_t clap_version;
    bool (HOOT_CLAP_ABI *init)(const char* plugin_path);
    void (HOOT_CLAP_ABI *deinit)();
    const void* (HOOT_CLAP_ABI *get_factory)(const char* factory_id);
};

} // extern "C"

namespace hoot::clap_minimal {

inline constexpr clap_version_t kVersion{1, 2, 10};
inline constexpr const char* kPluginFactoryId = "clap.plugin-factory";
inline constexpr uint16_t kCoreEventSpaceId = 0;
inline constexpr uint16_t kEventMidi = 10;
inline constexpr uint16_t kEventMidiSysex = 11;
inline constexpr clap_process_status kProcessError = 0;

} // namespace hoot::clap_minimal
