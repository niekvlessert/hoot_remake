#include "sound/vermouth_midi_synth.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <limits>
#include <vector>

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
void close_library(LibraryHandle h) { if (h != nullptr) FreeLibrary(h); }
void* load_symbol(LibraryHandle h, const char* name) { return reinterpret_cast<void*>(GetProcAddress(h, name)); }
LibraryHandle open_library(const char* name) { return LoadLibraryA(name); }
#elif defined(__EMSCRIPTEN__)
using LibraryHandle = void*;
void close_library(LibraryHandle) {}
void* load_symbol(LibraryHandle, const char*) { return nullptr; }
LibraryHandle open_library(const char*) { return nullptr; }
#else
using LibraryHandle = void*;
void close_library(LibraryHandle h) { if (h != nullptr) dlclose(h); }
void* load_symbol(LibraryHandle h, const char* name) { return dlsym(h, name); }
LibraryHandle open_library(const char* name) { return dlopen(name, RTLD_NOW | RTLD_LOCAL); }
#endif

template <typename T>
bool bind_symbol(LibraryHandle h, const char* name, T& out)
{
    out = reinterpret_cast<T>(load_symbol(h, name));
    return out != nullptr;
}

std::string lower(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

std::string getenv_string(const char* name)
{
    const char* value = std::getenv(name);
    return value != nullptr ? std::string(value) : std::string{};
}

std::string default_soundfont()
{
    const char* vars[] = {"HOOT_VERMOUTH_SOUNDFONT", "HOOT_X68K_SOUNDFONT", "HOOT_MIDI_SOUNDFONT"};
    for (const char* var : vars) {
        const auto value = getenv_string(var);
        if (!value.empty() && std::filesystem::exists(value)) return value;
    }
    const char* candidates[] = {
        "/usr/share/sounds/sf2/default-GM.sf2",
        "/usr/share/sounds/sf2/FluidR3_GM.sf2",
        "/usr/share/sounds/sf2/TimGM6mb.sf2",
        "/opt/homebrew/share/sounds/sf2/FluidR3_GM.sf2",
        "/usr/local/share/sounds/sf2/default-GM.sf2",
    };
    for (const char* path : candidates) if (std::filesystem::exists(path)) return path;
    return {};
}

} // namespace

struct VermouthMidiSynth::Impl {
    LibraryHandle library = nullptr;
    void* module = nullptr;
    void* handle = nullptr;
    std::string soundfont;
    std::string abi;
    std::vector<int32_t> render_buffer;

    using ModuleCreateLegacy = void* (*)(uint32_t);
    using ModuleCreateFluid = void* (*)(wchar_t*, uint32_t);
    using ModuleDestroy = void (*)(void*);
    using ModuleLoadAll = void (*)(void*);
    using MidiCreate = void* (*)(void*, uint32_t);
    using MidiDestroy = void (*)(void*);
    using ShortMsg = void (*)(void*, uint32_t);
    using LongMsg = void (*)(void*, const void*, uint32_t);
    using Get32 = uint32_t (*)(void*, int32_t*, uint32_t);
    using SetGain = void (*)(void*, int);

    void* module_create_symbol = nullptr;
    ModuleDestroy module_destroy = nullptr;
    ModuleLoadAll module_loadall = nullptr;
    MidiCreate midi_create = nullptr;
    MidiDestroy midi_destroy = nullptr;
    ShortMsg shortmsg = nullptr;
    LongMsg longmsg = nullptr;
    Get32 get32 = nullptr;
    SetGain setgain = nullptr;
};

VermouthMidiSynth::VermouthMidiSynth() : impl_(std::make_unique<Impl>()) {}
VermouthMidiSynth::~VermouthMidiSynth() { close(); }

bool VermouthMidiSynth::open(int sample_rate, const std::string& soundfont, std::string& error)
{
    close();
    impl_->abi = lower(getenv_string("HOOT_VERMOUTH_ABI"));
    if (impl_->abi.empty() || impl_->abi == "auto") impl_->abi = "fluidsynth";
    if (impl_->abi != "legacy" && impl_->abi != "fluidsynth" && impl_->abi != "sf2") {
        error = "invalid Vermouth ABI; set [midi] vermouth_abi=legacy or fluidsynth";
        return false;
    }
    if (impl_->abi == "sf2") impl_->abi = "fluidsynth";

    const auto explicit_library = getenv_string("HOOT_VERMOUTH_LIBRARY");
    if (!explicit_library.empty()) impl_->library = open_library(explicit_library.c_str());
    if (impl_->library == nullptr) {
#if defined(_WIN32)
        const char* names[] = {"vermouth.dll"};
#elif defined(__APPLE__)
        const char* names[] = {"libvermouth.dylib", "vermouth.dylib"};
#else
        const char* names[] = {"libvermouth.so", "vermouth.so"};
#endif
        for (const char* name : names) {
            impl_->library = open_library(name);
            if (impl_->library != nullptr) break;
        }
    }
    if (impl_->library == nullptr) {
        error = "Vermouth runtime library not found; set [midi] vermouth_library";
        return false;
    }

    bool ok = true;
    impl_->module_create_symbol = load_symbol(impl_->library, "midimod_create");
    ok &= impl_->module_create_symbol != nullptr;
    ok &= bind_symbol(impl_->library, "midimod_destroy", impl_->module_destroy);
    ok &= bind_symbol(impl_->library, "midimod_loadall", impl_->module_loadall);
    ok &= bind_symbol(impl_->library, "midiout_create", impl_->midi_create);
    ok &= bind_symbol(impl_->library, "midiout_destroy", impl_->midi_destroy);
    ok &= bind_symbol(impl_->library, "midiout_shortmsg", impl_->shortmsg);
    ok &= bind_symbol(impl_->library, "midiout_longmsg", impl_->longmsg);
    ok &= bind_symbol(impl_->library, "midiout_get32", impl_->get32);
    // Older Vermouth builds may not export the optional gain setter.
    bind_symbol(impl_->library, "midiout_setgain", impl_->setgain);
    if (!ok) {
        error = "Vermouth runtime is missing required Hoot ABI symbols";
        close();
        return false;
    }

    if (impl_->abi == "legacy") {
        auto create = reinterpret_cast<Impl::ModuleCreateLegacy>(impl_->module_create_symbol);
        impl_->module = create(static_cast<uint32_t>(sample_rate));
        if (impl_->module == nullptr) {
            error = "legacy Vermouth could not initialize; place timidity.cfg and its GUS patch set where Vermouth can find them";
            close();
            return false;
        }
        impl_->soundfont = "timidity.cfg / GUS patches";
    } else {
        impl_->soundfont = soundfont.empty() ? default_soundfont() : soundfont;
        if (impl_->soundfont.empty()) {
            error = "Vermouth FluidSynth ABI needs an SF2; set [midi] vermouth_soundfont or midi.soundfont";
            close();
            return false;
        }
        std::wstring wide = std::filesystem::path(impl_->soundfont).wstring();
        auto create = reinterpret_cast<Impl::ModuleCreateFluid>(impl_->module_create_symbol);
        impl_->module = create(wide.data(), static_cast<uint32_t>(sample_rate));
        if (impl_->module == nullptr) {
            error = "Vermouth could not initialize SoundFont: " + impl_->soundfont;
            close();
            return false;
        }
    }

    impl_->module_loadall(impl_->module);
    // Historical Hoot/NP2 callers pass the host render block as worksize. The
    // current FluidSynth drop-in ignores it, while original Vermouth uses it
    // for its internal mixer workspace.
    impl_->handle = impl_->midi_create(impl_->module, 4096);
    if (impl_->handle == nullptr) {
        error = "Vermouth could not create a playback handle";
        close();
        return false;
    }
    if (impl_->setgain != nullptr) impl_->setgain(impl_->handle, 100);
    reset();
    return true;
}

void VermouthMidiSynth::close()
{
    if (!impl_) return;
    if (impl_->handle != nullptr && impl_->midi_destroy != nullptr) impl_->midi_destroy(impl_->handle);
    impl_->handle = nullptr;
    if (impl_->module != nullptr && impl_->module_destroy != nullptr) impl_->module_destroy(impl_->module);
    impl_->module = nullptr;
    if (impl_->library != nullptr) close_library(impl_->library);
    impl_->library = nullptr;
    impl_->soundfont.clear();
    impl_->render_buffer.clear();
    impl_->module_create_symbol = nullptr;
}

void VermouthMidiSynth::reset()
{
    if (!active()) return;
    // GM System On is what historical Vermouth Hoot routes expect.
    const uint8_t gm[] = {0xf0, 0x7e, 0x7f, 0x09, 0x01, 0xf7};
    impl_->longmsg(impl_->handle, gm, sizeof(gm));
    for (uint8_t ch = 0; ch < 16; ++ch) {
        short_message(static_cast<uint8_t>(0xb0 | ch), 123, 0, 3); // all notes off
        short_message(static_cast<uint8_t>(0xb0 | ch), 121, 0, 3); // reset controllers
    }
}

bool VermouthMidiSynth::active() const { return impl_ && impl_->handle != nullptr; }
const char* VermouthMidiSynth::backend_name() const { return active() ? "vermouth" : "none"; }
const std::string& VermouthMidiSynth::soundfont_path() const { return impl_->soundfont; }

void VermouthMidiSynth::short_message(uint8_t status, uint8_t data1, uint8_t data2, uint8_t size)
{
    if (!active() || size == 0) return;
    uint32_t packed = status;
    if (size >= 2) packed |= static_cast<uint32_t>(data1 & 0x7f) << 8;
    if (size >= 3) packed |= static_cast<uint32_t>(data2 & 0x7f) << 16;
    impl_->shortmsg(impl_->handle, packed);
}

void VermouthMidiSynth::sysex(const std::vector<uint8_t>& data)
{
    if (!active() || data.empty()) return;
    std::vector<uint8_t> framed;
    if (data.front() != 0xf0) framed.push_back(0xf0);
    framed.insert(framed.end(), data.begin(), data.end());
    if (framed.back() != 0xf7) framed.push_back(0xf7);
    impl_->longmsg(impl_->handle, framed.data(), static_cast<uint32_t>(framed.size()));
}

int VermouthMidiSynth::render_s16(int16_t* interleaved_stereo, int frames)
{
    if (!active() || interleaved_stereo == nullptr || frames <= 0) return 0;
    int done = 0;
    while (done < frames) {
        const int chunk = std::min(frames - done, 4096);
        impl_->render_buffer.assign(static_cast<size_t>(chunk) * 2, 0);
        const auto got = impl_->get32(impl_->handle, impl_->render_buffer.data(), static_cast<uint32_t>(chunk));
        if (got == 0) {
            std::fill(interleaved_stereo + static_cast<size_t>(done) * 2,
                      interleaved_stereo + static_cast<size_t>(frames) * 2, 0);
            break;
        }
        const int count = std::min<int>(chunk, static_cast<int>(got));
        for (int i = 0; i < count * 2; ++i) {
            const auto value = std::clamp<int32_t>(impl_->render_buffer[static_cast<size_t>(i)],
                                                   std::numeric_limits<int16_t>::min(),
                                                   std::numeric_limits<int16_t>::max());
            interleaved_stereo[static_cast<size_t>(done) * 2 + static_cast<size_t>(i)] = static_cast<int16_t>(value);
        }
        if (count < chunk) {
            std::fill(interleaved_stereo + static_cast<size_t>(done + count) * 2,
                      interleaved_stereo + static_cast<size_t>(done + chunk) * 2, 0);
        }
        done += chunk;
    }
    return frames;
}

} // namespace hoot
