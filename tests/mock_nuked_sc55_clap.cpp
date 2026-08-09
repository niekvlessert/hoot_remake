#include <algorithm>
#include <cstring>

#include "sound/clap_minimal.h"

namespace {

bool note_active = false;
bool sysex_seen = false;

const char* features[] = {"instrument", "synthesizer", "stereo", nullptr};
const clap_plugin_descriptor_t descriptor{
    hoot::clap_minimal::kVersion,
    "net.johnnovak.nuked_sc55_clap.sc55_v1_21",
    "Mock Nuked SC-55",
    "Hoot tests",
    "",
    "",
    "",
    "0.0",
    "CLAP host test double",
    features,
};

bool HOOT_CLAP_ABI plugin_init(const clap_plugin_t*) { return true; }
void HOOT_CLAP_ABI plugin_destroy(const clap_plugin_t*) {}
bool HOOT_CLAP_ABI plugin_activate(const clap_plugin_t*, double, uint32_t, uint32_t) { return true; }
void HOOT_CLAP_ABI plugin_deactivate(const clap_plugin_t*) {}
bool HOOT_CLAP_ABI plugin_start(const clap_plugin_t*) { return true; }
void HOOT_CLAP_ABI plugin_stop(const clap_plugin_t*) {}
void HOOT_CLAP_ABI plugin_reset(const clap_plugin_t*) { note_active = false; }
const void* HOOT_CLAP_ABI plugin_extension(const clap_plugin_t*, const char*) { return nullptr; }
void HOOT_CLAP_ABI plugin_main_thread(const clap_plugin_t*) {}

clap_process_status HOOT_CLAP_ABI plugin_process(const clap_plugin_t*, const clap_process_t* process)
{
    if (process->in_events) {
        const uint32_t count = process->in_events->size(process->in_events);
        for (uint32_t i = 0; i < count; ++i) {
            const auto* header = process->in_events->get(process->in_events, i);
            if (!header || header->space_id != hoot::clap_minimal::kCoreEventSpaceId) continue;
            if (header->type == hoot::clap_minimal::kEventMidi) {
                const auto* event = reinterpret_cast<const clap_event_midi_t*>(header);
                const uint8_t command = event->data[0] & 0xf0;
                if (command == 0x90 && event->data[2] != 0) note_active = true;
                if (command == 0x80 || (command == 0x90 && event->data[2] == 0)) note_active = false;
                if (command == 0xb0 && (event->data[1] == 120 || event->data[1] == 123)) note_active = false;
            } else if (header->type == hoot::clap_minimal::kEventMidiSysex) {
                const auto* event = reinterpret_cast<const clap_event_midi_sysex_t*>(header);
                if (event->size >= 2 && event->buffer[0] == 0xf0 && event->buffer[event->size - 1] == 0xf7)
                    sysex_seen = true;
            }
        }
    }
    if (process->audio_outputs_count == 1 && process->audio_outputs && process->audio_outputs[0].data32) {
        auto* left = process->audio_outputs[0].data32[0];
        auto* right = process->audio_outputs[0].data32[1];
        const float level = note_active ? 0.25f : 0.0f;
        std::fill(left, left + process->frames_count, level);
        std::fill(right, right + process->frames_count, level);
    }
    return 1;
}

const clap_plugin_t plugin{
    &descriptor,
    nullptr,
    plugin_init,
    plugin_destroy,
    plugin_activate,
    plugin_deactivate,
    plugin_start,
    plugin_stop,
    plugin_reset,
    plugin_process,
    plugin_extension,
    plugin_main_thread,
};

uint32_t HOOT_CLAP_ABI factory_count(const clap_plugin_factory_t*) { return 1; }
const clap_plugin_descriptor_t* HOOT_CLAP_ABI factory_descriptor(const clap_plugin_factory_t*, uint32_t index)
{
    return index == 0 ? &descriptor : nullptr;
}
const clap_plugin_t* HOOT_CLAP_ABI factory_create(const clap_plugin_factory_t*, const clap_host_t*, const char* id)
{
    note_active = false;
    sysex_seen = false;
    return id && std::strcmp(id, descriptor.id) == 0 ? &plugin : nullptr;
}
const clap_plugin_factory_t factory{factory_count, factory_descriptor, factory_create};

bool HOOT_CLAP_ABI entry_init(const char*) { return true; }
void HOOT_CLAP_ABI entry_deinit() {}
const void* HOOT_CLAP_ABI entry_factory(const char* id)
{
    return id && std::strcmp(id, hoot::clap_minimal::kPluginFactoryId) == 0 ? &factory : nullptr;
}

} // namespace

extern "C" HOOT_CLAP_EXPORT const clap_plugin_entry_t clap_entry;
extern "C" HOOT_CLAP_EXPORT const clap_plugin_entry_t clap_entry{
    hoot::clap_minimal::kVersion,
    entry_init,
    entry_deinit,
    entry_factory,
};
