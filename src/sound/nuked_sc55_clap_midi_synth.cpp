#include "sound/nuked_sc55_clap_midi_synth.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <filesystem>
#include <limits>
#include <string_view>
#include <vector>

#include "sound/clap_minimal.h"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#elif !defined(__EMSCRIPTEN__)
#include <dlfcn.h>
#endif

namespace hoot {
namespace {

#if defined(_WIN32)
using LibraryHandle = HMODULE;
LibraryHandle open_library(const std::string& path) { return LoadLibraryA(path.c_str()); }
void close_library(LibraryHandle handle) { if (handle != nullptr) FreeLibrary(handle); }
void* load_symbol(LibraryHandle handle, const char* name)
{
    return reinterpret_cast<void*>(GetProcAddress(handle, name));
}
constexpr char kPathSeparator = ';';
#elif defined(__EMSCRIPTEN__)
using LibraryHandle = void*;
LibraryHandle open_library(const std::string&) { return nullptr; }
void close_library(LibraryHandle) {}
void* load_symbol(LibraryHandle, const char*) { return nullptr; }
constexpr char kPathSeparator = ':';
#elif defined(__APPLE__)
using LibraryHandle = void*;
LibraryHandle open_library(const std::string& path) { return dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL); }
void close_library(LibraryHandle handle) { if (handle != nullptr) dlclose(handle); }
void* load_symbol(LibraryHandle handle, const char* name) { return dlsym(handle, name); }
constexpr char kPathSeparator = ':';
#else
using LibraryHandle = void*;
LibraryHandle open_library(const std::string& path) { return dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL); }
void close_library(LibraryHandle handle) { if (handle != nullptr) dlclose(handle); }
void* load_symbol(LibraryHandle handle, const char* name) { return dlsym(handle, name); }
constexpr char kPathSeparator = ':';
#endif

std::vector<std::string> split_paths(const char* value)
{
    std::vector<std::string> result;
    if (value == nullptr || value[0] == '\0') return result;
    std::string text(value);
    size_t start = 0;
    while (start <= text.size()) {
        const size_t end = text.find(kPathSeparator, start);
        const std::string item = text.substr(start, end == std::string::npos ? std::string::npos : end - start);
        if (!item.empty()) result.push_back(item);
        if (end == std::string::npos) break;
        start = end + 1;
    }
    return result;
}

bool looks_like_nuked_plugin(const std::filesystem::path& path)
{
    const auto filename = path.filename().string();
    if (filename == "Nuked-SC55.clap" || filename == "nuked-sc55.clap") return true;
    return path.extension() == ".clap" && filename.find("Nuked-SC55") != std::string::npos;
}

std::string find_in_directory(const std::filesystem::path& root)
{
    std::error_code ec;
    if (!std::filesystem::exists(root, ec)) return {};
    if (looks_like_nuked_plugin(root)) return root.string();
    if (!std::filesystem::is_directory(root, ec)) return {};
    for (std::filesystem::recursive_directory_iterator it(root,
             std::filesystem::directory_options::skip_permission_denied, ec), end;
         it != end; it.increment(ec)) {
        if (ec) { ec.clear(); continue; }
        if (looks_like_nuked_plugin(it->path())) return it->path().string();
        // A macOS .clap bundle is a directory and should be treated atomically.
        if (it->is_directory(ec) && it->path().extension() == ".clap") {
            it.disable_recursion_pending();
        }
    }
    return {};
}

std::string binary_path_for_plugin(const std::string& plugin_path)
{
#if defined(__APPLE__)
    std::filesystem::path p(plugin_path);
    std::error_code ec;
    if (std::filesystem::is_directory(p, ec) && p.extension() == ".clap") {
        const auto direct = p / "Contents" / "MacOS" / "Nuked-SC55";
        if (std::filesystem::exists(direct, ec)) return direct.string();
        const auto macos = p / "Contents" / "MacOS";
        if (std::filesystem::is_directory(macos, ec)) {
            for (const auto& entry : std::filesystem::directory_iterator(macos, ec)) {
                if (entry.is_regular_file(ec)) return entry.path().string();
            }
        }
    }
#endif
    return plugin_path;
}

const char* model_id_from_name(std::string_view model)
{
    if (model == "v1.00" || model == "1.00" || model == "sc55_v1_00")
        return "net.johnnovak.nuked_sc55_clap.sc55_v1_00";
    if (model == "v1.10" || model == "1.10" || model == "sc55_v1_10")
        return "net.johnnovak.nuked_sc55_clap.sc55_v1_10";
    if (model == "v1.20" || model == "1.20" || model == "sc55_v1_20")
        return "net.johnnovak.nuked_sc55_clap.sc55_v1_20";
    if (model == "v2.00" || model == "2.00" || model == "sc55_v2_00")
        return "net.johnnovak.nuked_sc55_clap.sc55_v2_00";
    if (model == "mk2" || model == "mkII" || model == "v1.01mk2" || model == "sc55mk2_v1_01")
        return "net.johnnovak.nuked_sc55_clap.sc55mk2_v1_01";
    return "net.johnnovak.nuked_sc55_clap.sc55_v1_21";
}

struct PendingMidi {
    bool sysex = false;
    std::array<uint8_t, 3> short_data{};
    uint8_t short_size = 0;
    std::vector<uint8_t> sysex_data;
};

struct InputEventList {
    std::vector<const clap_event_header_t*> events;
};

uint32_t HOOT_CLAP_ABI input_size(const clap_input_events_t* list)
{
    auto* state = static_cast<const InputEventList*>(list->ctx);
    return state ? static_cast<uint32_t>(state->events.size()) : 0u;
}

const clap_event_header_t* HOOT_CLAP_ABI input_get(const clap_input_events_t* list, uint32_t index)
{
    auto* state = static_cast<const InputEventList*>(list->ctx);
    if (state == nullptr || index >= state->events.size()) return nullptr;
    return state->events[index];
}

bool HOOT_CLAP_ABI output_try_push(const clap_output_events_t*, const clap_event_header_t*)
{
    return true;
}

const void* HOOT_CLAP_ABI host_get_extension(const clap_host_t*, const char*) { return nullptr; }
void HOOT_CLAP_ABI host_request_restart(const clap_host_t*) {}
void HOOT_CLAP_ABI host_request_process(const clap_host_t*) {}
void HOOT_CLAP_ABI host_request_callback(const clap_host_t*) {}

} // namespace

struct NukedSc55ClapMidiSynth::Impl {
    LibraryHandle library = nullptr;
    const clap_plugin_entry_t* entry = nullptr;
    const clap_plugin_factory_t* factory = nullptr;
    const clap_plugin_t* plugin = nullptr;
    bool entry_initialized = false;
    bool plugin_initialized = false;
    bool activated = false;
    bool processing = false;
    int sample_rate = 44100;
    int64_t steady_time = 0;
    std::string plugin_path;
    std::string model_id;
    std::deque<PendingMidi> pending;
    std::vector<float> left;
    std::vector<float> right;

    clap_host_t host{
        clap_minimal::kVersion,
        this,
        "Hoot Headless",
        "Hoot",
        "https://github.com/",
        "0.1",
        host_get_extension,
        host_request_restart,
        host_request_process,
        host_request_callback,
    };
};

NukedSc55ClapMidiSynth::NukedSc55ClapMidiSynth()
    : impl_(std::make_unique<Impl>())
{
}

NukedSc55ClapMidiSynth::~NukedSc55ClapMidiSynth()
{
    close();
}

std::string NukedSc55ClapMidiSynth::find_plugin()
{
    if (const char* value = std::getenv("HOOT_X68K_NUKED_SC55_CLAP"); value != nullptr && value[0] != '\0') {
        std::error_code ec;
        if (std::filesystem::exists(value, ec)) return std::string(value);
    }

    for (const auto& path : split_paths(std::getenv("CLAP_PATH"))) {
        if (auto found = find_in_directory(path); !found.empty()) return found;
    }

    std::vector<std::filesystem::path> roots;
#if defined(_WIN32)
    if (const char* common = std::getenv("COMMONPROGRAMFILES")) roots.emplace_back(std::filesystem::path(common) / "CLAP");
    if (const char* local = std::getenv("LOCALAPPDATA")) roots.emplace_back(std::filesystem::path(local) / "Programs" / "Common" / "CLAP");
#elif defined(__APPLE__)
    roots.emplace_back("/Library/Audio/Plug-Ins/CLAP");
    if (const char* home = std::getenv("HOME")) roots.emplace_back(std::filesystem::path(home) / "Library" / "Audio" / "Plug-Ins" / "CLAP");
#else
    roots.emplace_back("/usr/lib/clap");
    if (const char* home = std::getenv("HOME")) roots.emplace_back(std::filesystem::path(home) / ".clap");
#endif
    for (const auto& root : roots) {
        if (auto found = find_in_directory(root); !found.empty()) return found;
    }
    return {};
}

std::string NukedSc55ClapMidiSynth::selected_model_id()
{
    const char* model = std::getenv("HOOT_X68K_SC55_MODEL");
    return model_id_from_name(model ? std::string_view(model) : std::string_view("v1.21"));
}

bool NukedSc55ClapMidiSynth::open(int sample_rate, const std::string&, std::string& error)
{
    close();
    impl_->sample_rate = sample_rate > 0 ? sample_rate : 44100;
    impl_->plugin_path = find_plugin();
    if (impl_->plugin_path.empty()) {
        error = "Nuked-SC55 CLAP plugin not found; set HOOT_X68K_NUKED_SC55_CLAP or CLAP_PATH";
        return false;
    }

    const std::string library_path = binary_path_for_plugin(impl_->plugin_path);
    impl_->library = open_library(library_path);
    if (impl_->library == nullptr) {
        error = "unable to load Nuked-SC55 CLAP plugin: " + library_path;
        close();
        return false;
    }

    impl_->entry = reinterpret_cast<const clap_plugin_entry_t*>(load_symbol(impl_->library, "clap_entry"));
    if (impl_->entry == nullptr || impl_->entry->init == nullptr || impl_->entry->get_factory == nullptr) {
        error = "Nuked-SC55 CLAP library has no valid clap_entry";
        close();
        return false;
    }
    if (impl_->entry->clap_version.major != clap_minimal::kVersion.major) {
        error = "Nuked-SC55 CLAP major version is incompatible";
        close();
        return false;
    }
    if (!impl_->entry->init(impl_->plugin_path.c_str())) {
        error = "Nuked-SC55 CLAP entry initialization failed";
        close();
        return false;
    }
    impl_->entry_initialized = true;

    impl_->factory = static_cast<const clap_plugin_factory_t*>(
        impl_->entry->get_factory(clap_minimal::kPluginFactoryId));
    if (impl_->factory == nullptr || impl_->factory->create_plugin == nullptr) {
        error = "Nuked-SC55 CLAP plugin factory not found";
        close();
        return false;
    }

    impl_->model_id = selected_model_id();
    impl_->plugin = impl_->factory->create_plugin(impl_->factory, &impl_->host, impl_->model_id.c_str());
    if (impl_->plugin == nullptr) {
        error = "requested Nuked-SC55 model is unavailable: " + impl_->model_id;
        close();
        return false;
    }
    if (impl_->plugin->init == nullptr || !impl_->plugin->init(impl_->plugin)) {
        error = "Nuked-SC55 model initialization failed; verify the ROM files or SOUNDCANVAS_ROM_PATH";
        close();
        return false;
    }
    impl_->plugin_initialized = true;

    constexpr uint32_t kMaxFrames = 4096;
    if (impl_->plugin->activate == nullptr ||
        !impl_->plugin->activate(impl_->plugin, static_cast<double>(impl_->sample_rate), 1, kMaxFrames)) {
        error = "Nuked-SC55 CLAP activation failed";
        close();
        return false;
    }
    impl_->activated = true;
    if (impl_->plugin->start_processing == nullptr || !impl_->plugin->start_processing(impl_->plugin)) {
        error = "Nuked-SC55 CLAP could not start audio processing";
        close();
        return false;
    }
    impl_->processing = true;
    return true;
}

void NukedSc55ClapMidiSynth::close()
{
    if (!impl_) return;
    impl_->pending.clear();
    if (impl_->plugin != nullptr && impl_->processing && impl_->plugin->stop_processing != nullptr) {
        impl_->plugin->stop_processing(impl_->plugin);
    }
    impl_->processing = false;
    if (impl_->plugin != nullptr && impl_->activated && impl_->plugin->deactivate != nullptr) {
        impl_->plugin->deactivate(impl_->plugin);
    }
    impl_->activated = false;
    if (impl_->plugin != nullptr && impl_->plugin->destroy != nullptr) {
        impl_->plugin->destroy(impl_->plugin);
    }
    impl_->plugin = nullptr;
    impl_->plugin_initialized = false;
    impl_->factory = nullptr;
    if (impl_->entry != nullptr && impl_->entry_initialized && impl_->entry->deinit != nullptr) {
        impl_->entry->deinit();
    }
    impl_->entry_initialized = false;
    impl_->entry = nullptr;
    if (impl_->library != nullptr) close_library(impl_->library);
    impl_->library = nullptr;
    impl_->plugin_path.clear();
    impl_->model_id.clear();
    impl_->steady_time = 0;
}

void NukedSc55ClapMidiSynth::reset()
{
    if (!active()) return;
    impl_->pending.clear();
    if (impl_->plugin->reset != nullptr) impl_->plugin->reset(impl_->plugin);
}

bool NukedSc55ClapMidiSynth::active() const
{
    return impl_ && impl_->plugin != nullptr && impl_->processing;
}

const char* NukedSc55ClapMidiSynth::backend_name() const
{
    return active() ? "nuked-sc55-clap" : "none";
}

const std::string& NukedSc55ClapMidiSynth::soundfont_path() const
{
    return impl_->plugin_path;
}

void NukedSc55ClapMidiSynth::short_message(uint8_t status, uint8_t data1, uint8_t data2, uint8_t size)
{
    if (!active() || size == 0 || size > 3) return;
    PendingMidi event;
    event.short_size = size;
    event.short_data = {status, data1, data2};
    impl_->pending.push_back(std::move(event));
}

void NukedSc55ClapMidiSynth::sysex(const std::vector<uint8_t>& data)
{
    if (!active()) return;
    PendingMidi event;
    event.sysex = true;
    event.sysex_data.resize(data.size() + 2);
    event.sysex_data.front() = 0xf0;
    std::copy(data.begin(), data.end(), event.sysex_data.begin() + 1);
    event.sysex_data.back() = 0xf7;
    impl_->pending.push_back(std::move(event));
}

int NukedSc55ClapMidiSynth::render_s16(int16_t* interleaved_stereo, int frames)
{
    if (!active() || interleaved_stereo == nullptr || frames <= 0) return 0;

    constexpr int kMaxFrames = 4096;
    int rendered = 0;
    while (rendered < frames) {
        const int todo = std::min(kMaxFrames, frames - rendered);
        impl_->left.assign(static_cast<size_t>(todo), 0.0f);
        impl_->right.assign(static_cast<size_t>(todo), 0.0f);

        struct EncodedEvent {
            bool is_sysex = false;
            clap_event_midi_t midi{};
            clap_event_midi_sysex_t sysex{};
        };
        std::vector<EncodedEvent> encoded;
        std::deque<std::vector<uint8_t>> sysex_buffers;
        encoded.reserve(impl_->pending.size());
        InputEventList event_state;
        event_state.events.reserve(impl_->pending.size());

        // Deliver complete UART messages at the beginning of the next audio
        // block. UART byte timing itself has already been enforced by
        // X68kMidiTransport, and message ordering is retained exactly here.
        for (auto& pending : impl_->pending) {
            encoded.emplace_back();
            auto& holder = encoded.back();
            holder.is_sysex = pending.sysex;
            if (pending.sysex) {
                sysex_buffers.push_back(std::move(pending.sysex_data));
                holder.sysex.header.size = sizeof(holder.sysex);
                holder.sysex.header.time = 0;
                holder.sysex.header.space_id = clap_minimal::kCoreEventSpaceId;
                holder.sysex.header.type = clap_minimal::kEventMidiSysex;
                holder.sysex.port_index = 0;
                holder.sysex.buffer = sysex_buffers.back().data();
                holder.sysex.size = static_cast<uint32_t>(sysex_buffers.back().size());
                event_state.events.push_back(&holder.sysex.header);
            } else {
                holder.midi.header.size = sizeof(holder.midi);
                holder.midi.header.time = 0;
                holder.midi.header.space_id = clap_minimal::kCoreEventSpaceId;
                holder.midi.header.type = clap_minimal::kEventMidi;
                holder.midi.port_index = 0;
                holder.midi.data[0] = pending.short_data[0];
                holder.midi.data[1] = pending.short_size >= 2 ? pending.short_data[1] : 0;
                holder.midi.data[2] = pending.short_size >= 3 ? pending.short_data[2] : 0;
                event_state.events.push_back(&holder.midi.header);
            }
        }
        impl_->pending.clear();

        clap_input_events_t input_events{&event_state, input_size, input_get};
        clap_output_events_t output_events{nullptr, output_try_push};
        float* channels[2] = {impl_->left.data(), impl_->right.data()};
        clap_audio_buffer_t output_buffer{};
        output_buffer.data32 = channels;
        output_buffer.channel_count = 2;

        clap_process_t process{};
        process.steady_time = impl_->steady_time;
        process.frames_count = static_cast<uint32_t>(todo);
        process.audio_outputs = &output_buffer;
        process.audio_outputs_count = 1;
        process.in_events = &input_events;
        process.out_events = &output_events;

        const auto status = impl_->plugin->process(impl_->plugin, &process);
        if (status == clap_minimal::kProcessError) return rendered;
        impl_->steady_time += todo;

        for (int i = 0; i < todo; ++i) {
            const float l = std::clamp(impl_->left[static_cast<size_t>(i)], -1.0f, 1.0f);
            const float r = std::clamp(impl_->right[static_cast<size_t>(i)], -1.0f, 1.0f);
            interleaved_stereo[(rendered + i) * 2] = static_cast<int16_t>(std::lrint(l * 32767.0f));
            interleaved_stereo[(rendered + i) * 2 + 1] = static_cast<int16_t>(std::lrint(r * 32767.0f));
        }
        rendered += todo;
    }
    return rendered;
}

} // namespace hoot
