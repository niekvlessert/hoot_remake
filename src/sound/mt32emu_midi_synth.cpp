#include "sound/mt32emu_midi_synth.h"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <filesystem>
#include <system_error>
#include <vector>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#elif !defined(__EMSCRIPTEN__)
#include <dlfcn.h>
#endif

namespace hoot {
namespace {

using mt32emu_bit32u = unsigned int;
using mt32emu_bit16s = signed short;
using mt32emu_bit8u = unsigned char;
using mt32emu_return_code = int;
using mt32emu_context = struct mt32emu_data*;
using mt32emu_const_context = const struct mt32emu_data*;
// mt32emu_report_handler_i is a union of interface pointers. A null union is
// the documented way to create a context without callbacks.
union mt32emu_report_handler_i {
    const void* v0;
    const void* v1;
    const void* v2;
};

#if defined(_WIN32)
using LibraryHandle = HMODULE;
void close_library(LibraryHandle handle) { if (handle != nullptr) FreeLibrary(handle); }
void* load_symbol(LibraryHandle handle, const char* name)
{
    return reinterpret_cast<void*>(GetProcAddress(handle, name));
}
LibraryHandle open_library(const char* name) { return LoadLibraryA(name); }
#elif defined(__EMSCRIPTEN__)
using LibraryHandle = void*;
void close_library(LibraryHandle) {}
void* load_symbol(LibraryHandle, const char*) { return nullptr; }
LibraryHandle open_library(const char*) { return nullptr; }
#else
using LibraryHandle = void*;
void close_library(LibraryHandle handle) { if (handle != nullptr) dlclose(handle); }
void* load_symbol(LibraryHandle handle, const char* name) { return dlsym(handle, name); }
LibraryHandle open_library(const char* name) { return dlopen(name, RTLD_NOW | RTLD_LOCAL); }
#endif

template <typename T>
bool bind_symbol(LibraryHandle handle, const char* name, T& out)
{
    out = reinterpret_cast<T>(load_symbol(handle, name));
    return out != nullptr;
}

bool is_regular_file(const std::filesystem::directory_entry& entry)
{
    std::error_code ec;
    return entry.is_regular_file(ec) && !ec;
}

std::vector<std::filesystem::path> enumerate_rom_files(const std::filesystem::path& root)
{
    std::vector<std::filesystem::path> result;
    std::error_code ec;
    if (std::filesystem::is_regular_file(root, ec)) {
        result.push_back(root);
        return result;
    }
    ec.clear();
    if (!std::filesystem::is_directory(root, ec)) {
        return result;
    }
    std::filesystem::recursive_directory_iterator it(
        root, std::filesystem::directory_options::skip_permission_denied, ec);
    const std::filesystem::recursive_directory_iterator end;
    for (; !ec && it != end; it.increment(ec)) {
        if (is_regular_file(*it)) {
            result.push_back(it->path());
        }
    }
    std::sort(result.begin(), result.end());
    return result;
}

std::string env_existing_path(const char* name)
{
    if (const char* value = std::getenv(name); value != nullptr && value[0] != '\0') {
        std::error_code ec;
        if (std::filesystem::exists(value, ec) && !ec) return value;
    }
    return {};
}

} // namespace

struct Mt32EmuMidiSynth::Impl {
    explicit Impl(Mt32EmuModel requested_model) : model(requested_model) {}

    Mt32EmuModel model;
    LibraryHandle library = nullptr;
    mt32emu_context context = nullptr;
    std::string rom_path;
    std::string machine_id;
    std::string version;

    using CreateContext = mt32emu_context (*)(mt32emu_report_handler_i, void*);
    using FreeContext = void (*)(mt32emu_context);
    using AddRomFile = mt32emu_return_code (*)(mt32emu_context, const char*);
    using GetMachineIds = size_t (*)(const char**, size_t);
    using AddMachineRomFile = mt32emu_return_code (*)(mt32emu_context, const char*, const char*);
    using SetStereoOutputSamplerate = void (*)(mt32emu_context, double);
    using SetMidiDelayMode = void (*)(mt32emu_context, int);
    using OpenSynth = mt32emu_return_code (*)(mt32emu_const_context);
    using CloseSynth = void (*)(mt32emu_const_context);
    using IsOpen = int (*)(mt32emu_const_context);
    using PlayShortMessage = void (*)(mt32emu_const_context, mt32emu_bit32u);
    using PlayMsg = mt32emu_return_code (*)(mt32emu_const_context, mt32emu_bit32u);
    using PlaySysex = mt32emu_return_code (*)(mt32emu_const_context, const mt32emu_bit8u*, mt32emu_bit32u);
    using RenderBit16s = void (*)(mt32emu_const_context, mt32emu_bit16s*, mt32emu_bit32u);
    using GetLibraryVersionString = const char* (*)();

    CreateContext create_context = nullptr;
    FreeContext free_context = nullptr;
    AddRomFile add_rom_file = nullptr;
    GetMachineIds get_machine_ids = nullptr;          // >= 2.5; optional
    AddMachineRomFile add_machine_rom_file = nullptr; // >= 2.5; optional fallback exists
    SetStereoOutputSamplerate set_stereo_output_samplerate = nullptr;
    SetMidiDelayMode set_midi_delay_mode = nullptr;
    OpenSynth open_synth = nullptr;
    CloseSynth close_synth = nullptr;
    IsOpen is_open = nullptr;
    PlayShortMessage play_short_message = nullptr;
    PlayMsg play_msg = nullptr;
    PlaySysex play_sysex = nullptr;
    RenderBit16s render_bit16s = nullptr;
    GetLibraryVersionString get_library_version_string = nullptr;
};

Mt32EmuMidiSynth::Mt32EmuMidiSynth(Mt32EmuModel model)
    : impl_(std::make_unique<Impl>(model))
{
}

Mt32EmuMidiSynth::~Mt32EmuMidiSynth()
{
    close();
}

std::string Mt32EmuMidiSynth::find_default_rom_path(Mt32EmuModel model)
{
    if (model == Mt32EmuModel::CM32L) {
        if (auto path = env_existing_path("HOOT_CM32L_ROM_PATH"); !path.empty()) return path;
    } else {
        if (auto path = env_existing_path("HOOT_MT32_ROM_PATH"); !path.empty()) return path;
    }
    if (auto path = env_existing_path("HOOT_MUNT_ROM_PATH"); !path.empty()) return path;

    const char* candidates_mt32[] = {
        "roms/mt32", "roms/munt/mt32", "roms/munt", "roms"
    };
    const char* candidates_cm32l[] = {
        "roms/cm32l", "roms/cm64", "roms/munt/cm32l", "roms/munt", "roms"
    };
    if (model == Mt32EmuModel::CM32L) {
        for (const char* candidate : candidates_cm32l) {
            std::error_code ec;
            if (std::filesystem::exists(candidate, ec) && !ec) return candidate;
        }
    } else {
        for (const char* candidate : candidates_mt32) {
            std::error_code ec;
            if (std::filesystem::exists(candidate, ec) && !ec) return candidate;
        }
    }
    return {};
}

bool Mt32EmuMidiSynth::open(int sample_rate, const std::string& rom_path, std::string& error)
{
    close();

    if (const char* configured = std::getenv("HOOT_MT32EMU_LIBRARY"); configured != nullptr && configured[0] != '\0') {
        impl_->library = open_library(configured);
    }
#if defined(_WIN32)
    const char* libraries[] = {"mt32emu.dll", "libmt32emu.dll"};
#elif defined(__APPLE__)
    const char* libraries[] = {"libmt32emu.2.dylib", "libmt32emu.dylib",
                               "/opt/homebrew/lib/libmt32emu.dylib", "/usr/local/lib/libmt32emu.dylib"};
#else
    const char* libraries[] = {"libmt32emu.so.2", "libmt32emu.so"};
#endif
    if (impl_->library == nullptr) {
        for (const char* name : libraries) {
            impl_->library = open_library(name);
            if (impl_->library != nullptr) break;
        }
    }
    if (impl_->library == nullptr) {
        error = "Munt/mt32emu runtime library not found; install libmt32emu or set HOOT_MT32EMU_LIBRARY";
        return false;
    }

    bool ok = true;
    ok &= bind_symbol(impl_->library, "mt32emu_create_context", impl_->create_context);
    ok &= bind_symbol(impl_->library, "mt32emu_free_context", impl_->free_context);
    ok &= bind_symbol(impl_->library, "mt32emu_add_rom_file", impl_->add_rom_file);
    bind_symbol(impl_->library, "mt32emu_get_machine_ids", impl_->get_machine_ids);
    bind_symbol(impl_->library, "mt32emu_add_machine_rom_file", impl_->add_machine_rom_file);
    ok &= bind_symbol(impl_->library, "mt32emu_set_stereo_output_samplerate", impl_->set_stereo_output_samplerate);
    bind_symbol(impl_->library, "mt32emu_set_midi_delay_mode", impl_->set_midi_delay_mode);
    ok &= bind_symbol(impl_->library, "mt32emu_open_synth", impl_->open_synth);
    ok &= bind_symbol(impl_->library, "mt32emu_close_synth", impl_->close_synth);
    ok &= bind_symbol(impl_->library, "mt32emu_is_open", impl_->is_open);
    bind_symbol(impl_->library, "mt32emu_play_short_message", impl_->play_short_message);
    ok &= bind_symbol(impl_->library, "mt32emu_play_msg", impl_->play_msg);
    ok &= bind_symbol(impl_->library, "mt32emu_play_sysex", impl_->play_sysex);
    ok &= bind_symbol(impl_->library, "mt32emu_render_bit16s", impl_->render_bit16s);
    bind_symbol(impl_->library, "mt32emu_get_library_version_string", impl_->get_library_version_string);
    if (!ok) {
        error = "Munt/mt32emu runtime is missing required C API symbols";
        close();
        return false;
    }

    impl_->rom_path = rom_path.empty() ? find_default_rom_path(impl_->model) : rom_path;
    if (impl_->rom_path.empty()) {
        error = impl_->model == Mt32EmuModel::CM32L
            ? "no CM-32L/CM-64 LA ROM path found; set midi.cm32l_rom_path or HOOT_CM32L_ROM_PATH"
            : "no MT-32 ROM path found; set midi.mt32_rom_path or HOOT_MT32_ROM_PATH";
        close();
        return false;
    }
    const auto files = enumerate_rom_files(impl_->rom_path);
    if (files.empty()) {
        error = "Munt ROM path contains no files: " + impl_->rom_path;
        close();
        return false;
    }

    std::vector<std::string> machine_candidates;
    const char* override_env = impl_->model == Mt32EmuModel::CM32L ? "HOOT_CM32L_MACHINE" : "HOOT_MT32_MACHINE";
    if (const char* machine = std::getenv(override_env); machine != nullptr && machine[0] != '\0') {
        machine_candidates.emplace_back(machine);
    } else if (impl_->get_machine_ids != nullptr) {
        const size_t count = impl_->get_machine_ids(nullptr, 0);
        std::vector<const char*> ids(count, nullptr);
        if (count != 0) impl_->get_machine_ids(ids.data(), ids.size());
        const char* prefix = impl_->model == Mt32EmuModel::CM32L ? "cm32l" : "mt32";
        for (const char* id : ids) {
            if (id != nullptr && std::string(id).rfind(prefix, 0) == 0) machine_candidates.emplace_back(id);
        }
    }
    // Fallback IDs for older 2.5+ libraries that export machine-specific ROM
    // loading but not enumeration, and for ABI-compatible test modules.
    if (machine_candidates.empty()) {
        if (impl_->model == Mt32EmuModel::CM32L) {
            machine_candidates = {"cm32l_1_02", "cm32l_1_00", "cm32ln_1_00"};
        } else {
            machine_candidates = {"mt32_1_07", "mt32_1_06", "mt32_1_05", "mt32_1_04",
                                  "mt32_bluer", "mt32_2_07", "mt32_2_06", "mt32_2_04", "mt32_2_03"};
        }
    }

    auto make_context = [&]() -> bool {
        mt32emu_report_handler_i no_handler{};
        impl_->context = impl_->create_context(no_handler, nullptr);
        if (impl_->context == nullptr) return false;
        impl_->set_stereo_output_samplerate(impl_->context, static_cast<double>(sample_rate));
        // Hoot's PC-98 MPU-401 and X68000 CZ-6BM1 transports already model
        // 31.25 kbit/s cable serialization. Disable mt32emu's optional second
        // cable-delay stage while retaining its internal synth/MCU behavior.
        if (impl_->set_midi_delay_mode != nullptr) impl_->set_midi_delay_mode(impl_->context, 0); // MT32EMU_MDM_IMMEDIATE
        return true;
    };
    auto destroy_context = [&]() {
        if (impl_->context != nullptr) impl_->free_context(impl_->context);
        impl_->context = nullptr;
    };

    bool opened = false;
    if (impl_->add_machine_rom_file != nullptr) {
        for (const std::string& machine : machine_candidates) {
            if (!make_context()) break;
            for (const auto& file : files) {
                impl_->add_machine_rom_file(impl_->context, machine.c_str(), file.string().c_str());
            }
            if (impl_->open_synth(impl_->context) == 0 && impl_->is_open(impl_->context)) {
                impl_->machine_id = machine;
                opened = true;
                break;
            }
            destroy_context();
        }
    } else {
        // Compatibility path for pre-2.5 libmt32emu. Keep MT-32 and CM-32L
        // ROMs in separate configured directories when using such a runtime.
        if (make_context()) {
            for (const auto& file : files) {
                impl_->add_rom_file(impl_->context, file.string().c_str());
            }
            opened = impl_->open_synth(impl_->context) == 0 && impl_->is_open(impl_->context);
            if (opened) impl_->machine_id = "auto";
        }
    }

    if (!opened) {
        error = std::string("Munt could not find a compatible ") +
            (impl_->model == Mt32EmuModel::CM32L ? "CM-32L/CM-64 LA" : "MT-32") +
            " control+PCM ROM pair in " + impl_->rom_path;
        close();
        return false;
    }
    if (impl_->get_library_version_string != nullptr) {
        if (const char* version = impl_->get_library_version_string(); version != nullptr) impl_->version = version;
    }
    return true;
}

void Mt32EmuMidiSynth::close()
{
    if (!impl_) return;
    if (impl_->context != nullptr) {
        if (impl_->is_open != nullptr && impl_->is_open(impl_->context) && impl_->close_synth != nullptr) {
            impl_->close_synth(impl_->context);
        }
        if (impl_->free_context != nullptr) impl_->free_context(impl_->context);
    }
    impl_->context = nullptr;
    if (impl_->library != nullptr) close_library(impl_->library);
    impl_->library = nullptr;
    impl_->rom_path.clear();
    impl_->machine_id.clear();
    impl_->version.clear();
}

void Mt32EmuMidiSynth::reset()
{
    if (!active()) return;
    impl_->close_synth(impl_->context);
    impl_->open_synth(impl_->context);
}

bool Mt32EmuMidiSynth::active() const
{
    return impl_ && impl_->context != nullptr && impl_->is_open != nullptr && impl_->is_open(impl_->context) != 0;
}

const char* Mt32EmuMidiSynth::backend_name() const
{
    return active() ? (impl_->model == Mt32EmuModel::CM32L ? "munt-cm32l" : "munt-mt32") : "none";
}

const std::string& Mt32EmuMidiSynth::soundfont_path() const
{
    return impl_->rom_path;
}

void Mt32EmuMidiSynth::short_message(uint8_t status, uint8_t data1, uint8_t data2, uint8_t size)
{
    if (!active() || size == 0) return;
    mt32emu_bit32u message = status;
    if (size >= 2) message |= static_cast<mt32emu_bit32u>(data1 & 0x7f) << 8;
    if (size >= 3) message |= static_cast<mt32emu_bit32u>(data2 & 0x7f) << 16;
    if (impl_->play_short_message != nullptr) impl_->play_short_message(impl_->context, message);
    else impl_->play_msg(impl_->context, message);
}

void Mt32EmuMidiSynth::sysex(const std::vector<uint8_t>& data)
{
    if (!active() || data.empty()) return;
    // Hoot's transport strips F0/F7, while mt32emu_play_sysex() expects a
    // complete, well-formed SysEx message.
    std::vector<uint8_t> framed(data.size() + 2);
    framed.front() = 0xf0;
    std::copy(data.begin(), data.end(), framed.begin() + 1);
    framed.back() = 0xf7;
    impl_->play_sysex(impl_->context, framed.data(), static_cast<mt32emu_bit32u>(framed.size()));
}

int Mt32EmuMidiSynth::render_s16(int16_t* interleaved_stereo, int frames)
{
    if (!active() || interleaved_stereo == nullptr || frames <= 0) return 0;
    impl_->render_bit16s(impl_->context, reinterpret_cast<mt32emu_bit16s*>(interleaved_stereo),
                        static_cast<mt32emu_bit32u>(frames));
    return frames;
}

} // namespace hoot
