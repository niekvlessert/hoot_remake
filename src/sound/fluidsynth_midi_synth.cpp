#include "sound/fluidsynth_midi_synth.h"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <vector>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#elif !defined(__EMSCRIPTEN__)
#include <dlfcn.h>
#endif

namespace hoot {
namespace {

struct fluid_settings_t;
struct fluid_synth_t;

#if defined(_WIN32)
using LibraryHandle = HMODULE;
void close_library(LibraryHandle handle) { if (handle != nullptr) FreeLibrary(handle); }
void* load_symbol(LibraryHandle handle, const char* name)
{
    return reinterpret_cast<void*>(GetProcAddress(handle, name));
}
#elif defined(__EMSCRIPTEN__)
using LibraryHandle = void*;
void close_library(LibraryHandle) {}
void* load_symbol(LibraryHandle, const char*) { return nullptr; }
#else
using LibraryHandle = void*;
void close_library(LibraryHandle handle) { if (handle != nullptr) dlclose(handle); }
void* load_symbol(LibraryHandle handle, const char* name) { return dlsym(handle, name); }
#endif

template <typename T>
bool bind_symbol(LibraryHandle handle, const char* name, T& out)
{
    out = reinterpret_cast<T>(load_symbol(handle, name));
    return out != nullptr;
}

} // namespace

struct FluidSynthMidiSynth::Impl {
    LibraryHandle library = nullptr;
    fluid_settings_t* settings = nullptr;
    fluid_synth_t* synth = nullptr;
    std::string soundfont;

    using NewSettings = fluid_settings_t* (*)();
    using DeleteSettings = void (*)(fluid_settings_t*);
    using SettingsSetNum = int (*)(fluid_settings_t*, const char*, double);
    using SettingsSetInt = int (*)(fluid_settings_t*, const char*, int);
    using NewSynth = fluid_synth_t* (*)(fluid_settings_t*);
    using DeleteSynth = void (*)(fluid_synth_t*);
    using SfLoad = int (*)(fluid_synth_t*, const char*, int);
    using NoteOn = int (*)(fluid_synth_t*, int, int, int);
    using NoteOff = int (*)(fluid_synth_t*, int, int);
    using KeyPressure = int (*)(fluid_synth_t*, int, int, int);
    using ControlChange = int (*)(fluid_synth_t*, int, int, int);
    using ProgramChange = int (*)(fluid_synth_t*, int, int);
    using ChannelPressure = int (*)(fluid_synth_t*, int, int);
    using PitchBend = int (*)(fluid_synth_t*, int, int);
    using SystemReset = int (*)(fluid_synth_t*);
    using SysEx = int (*)(fluid_synth_t*, const char*, int, char*, int*, int*, int);
    using WriteS16 = int (*)(fluid_synth_t*, int, void*, int, int, void*, int, int);

    NewSettings new_settings = nullptr;
    DeleteSettings delete_settings = nullptr;
    SettingsSetNum settings_setnum = nullptr;
    SettingsSetInt settings_setint = nullptr;
    NewSynth new_synth = nullptr;
    DeleteSynth delete_synth = nullptr;
    SfLoad sfload = nullptr;
    NoteOn noteon = nullptr;
    NoteOff noteoff = nullptr;
    KeyPressure key_pressure = nullptr;
    ControlChange cc = nullptr;
    ProgramChange program_change = nullptr;
    ChannelPressure channel_pressure = nullptr;
    PitchBend pitch_bend = nullptr;
    SystemReset system_reset = nullptr;
    SysEx sysex = nullptr;
    WriteS16 write_s16 = nullptr;
};

FluidSynthMidiSynth::FluidSynthMidiSynth()
    : impl_(std::make_unique<Impl>())
{
}

FluidSynthMidiSynth::~FluidSynthMidiSynth()
{
    close();
}

std::string FluidSynthMidiSynth::find_default_soundfont()
{
    const char* variables[] = {"HOOT_X68K_SOUNDFONT", "HOOT_MIDI_SOUNDFONT"};
    for (const char* variable : variables) {
        if (const char* value = std::getenv(variable); value != nullptr && value[0] != '\0') {
            if (std::filesystem::exists(value)) {
                return value;
            }
        }
    }

    const char* candidates[] = {
        "/usr/share/sounds/sf2/default-GM.sf2",
        "/usr/share/sounds/sf2/FluidR3_GM.sf2",
        "/usr/share/sounds/sf2/TimGM6mb.sf2",
        "/usr/local/share/sounds/sf2/default-GM.sf2",
        "/opt/homebrew/share/sounds/sf2/FluidR3_GM.sf2",
        "/opt/homebrew/share/sounds/sf2/TimGM6mb.sf2",
    };
    for (const char* candidate : candidates) {
        if (std::filesystem::exists(candidate)) {
            return candidate;
        }
    }
    return {};
}

bool FluidSynthMidiSynth::open(int sample_rate, const std::string& soundfont, std::string& error)
{
    close();
#if defined(_WIN32)
    const char* libraries[] = {"libfluidsynth-3.dll", "libfluidsynth.dll", "fluidsynth.dll"};
    for (const char* name : libraries) {
        impl_->library = LoadLibraryA(name);
        if (impl_->library != nullptr) break;
    }
#elif defined(__EMSCRIPTEN__)
    (void)sample_rate;
    (void)soundfont;
    error = "FluidSynth native dynamic backend is unavailable in WebAssembly";
    return false;
#elif defined(__APPLE__)
    const char* libraries[] = {"libfluidsynth.3.dylib", "libfluidsynth.dylib",
                               "/opt/homebrew/lib/libfluidsynth.dylib",
                               "/usr/local/lib/libfluidsynth.dylib"};
    for (const char* name : libraries) {
        impl_->library = dlopen(name, RTLD_NOW | RTLD_LOCAL);
        if (impl_->library != nullptr) break;
    }
#else
    const char* libraries[] = {"libfluidsynth.so.3", "libfluidsynth.so.2", "libfluidsynth.so"};
    for (const char* name : libraries) {
        int flags = RTLD_NOW | RTLD_LOCAL;
#ifdef RTLD_NODELETE
        // FluidSynth can pull in GLib worker/runtime state. Some distro builds
        // are not safe to fully unload while that process-global state is
        // winding down, which showed up as a rare shutdown crash in repeated
        // headless renders. Keep the code mapping resident; dlclose() still
        // releases our reference and the OS reclaims everything at exit.
        flags |= RTLD_NODELETE;
#endif
        impl_->library = dlopen(name, flags);
        if (impl_->library != nullptr) break;
    }
#endif
    if (impl_->library == nullptr) {
        error = "FluidSynth runtime library not found";
        return false;
    }

    bool ok = true;
    ok &= bind_symbol(impl_->library, "new_fluid_settings", impl_->new_settings);
    ok &= bind_symbol(impl_->library, "delete_fluid_settings", impl_->delete_settings);
    ok &= bind_symbol(impl_->library, "fluid_settings_setnum", impl_->settings_setnum);
    ok &= bind_symbol(impl_->library, "fluid_settings_setint", impl_->settings_setint);
    ok &= bind_symbol(impl_->library, "new_fluid_synth", impl_->new_synth);
    ok &= bind_symbol(impl_->library, "delete_fluid_synth", impl_->delete_synth);
    ok &= bind_symbol(impl_->library, "fluid_synth_sfload", impl_->sfload);
    ok &= bind_symbol(impl_->library, "fluid_synth_noteon", impl_->noteon);
    ok &= bind_symbol(impl_->library, "fluid_synth_noteoff", impl_->noteoff);
    ok &= bind_symbol(impl_->library, "fluid_synth_key_pressure", impl_->key_pressure);
    ok &= bind_symbol(impl_->library, "fluid_synth_cc", impl_->cc);
    ok &= bind_symbol(impl_->library, "fluid_synth_program_change", impl_->program_change);
    ok &= bind_symbol(impl_->library, "fluid_synth_channel_pressure", impl_->channel_pressure);
    ok &= bind_symbol(impl_->library, "fluid_synth_pitch_bend", impl_->pitch_bend);
    ok &= bind_symbol(impl_->library, "fluid_synth_system_reset", impl_->system_reset);
    ok &= bind_symbol(impl_->library, "fluid_synth_sysex", impl_->sysex);
    ok &= bind_symbol(impl_->library, "fluid_synth_write_s16", impl_->write_s16);
    if (!ok) {
        error = "FluidSynth runtime is missing required API symbols";
        close();
        return false;
    }

    impl_->settings = impl_->new_settings();
    if (impl_->settings == nullptr) {
        error = "unable to create FluidSynth settings";
        close();
        return false;
    }
    impl_->settings_setnum(impl_->settings, "synth.sample-rate", static_cast<double>(sample_rate));
    impl_->settings_setnum(impl_->settings, "synth.gain", 0.5);
    impl_->settings_setint(impl_->settings, "synth.chorus.active", 1);
    impl_->settings_setint(impl_->settings, "synth.reverb.active", 1);
    impl_->synth = impl_->new_synth(impl_->settings);
    if (impl_->synth == nullptr) {
        error = "unable to create FluidSynth synthesizer";
        close();
        return false;
    }

    impl_->soundfont = soundfont.empty() ? find_default_soundfont() : soundfont;
    if (impl_->soundfont.empty()) {
        error = "no GM SoundFont found; set HOOT_X68K_SOUNDFONT";
        close();
        return false;
    }
    if (impl_->sfload(impl_->synth, impl_->soundfont.c_str(), 1) < 0) {
        error = "FluidSynth could not load SoundFont: " + impl_->soundfont;
        close();
        return false;
    }
    impl_->system_reset(impl_->synth);
    return true;
}

void FluidSynthMidiSynth::close()
{
    if (!impl_) return;
    if (impl_->synth != nullptr && impl_->delete_synth != nullptr) {
        impl_->delete_synth(impl_->synth);
    }
    impl_->synth = nullptr;
    if (impl_->settings != nullptr && impl_->delete_settings != nullptr) {
        impl_->delete_settings(impl_->settings);
    }
    impl_->settings = nullptr;
    if (impl_->library != nullptr) {
        close_library(impl_->library);
    }
    impl_->library = nullptr;
    impl_->soundfont.clear();
}

void FluidSynthMidiSynth::reset()
{
    if (impl_->synth != nullptr) {
        impl_->system_reset(impl_->synth);
    }
}

bool FluidSynthMidiSynth::active() const
{
    return impl_ && impl_->synth != nullptr;
}

const char* FluidSynthMidiSynth::backend_name() const
{
    return active() ? "fluidsynth" : "none";
}

const std::string& FluidSynthMidiSynth::soundfont_path() const
{
    return impl_->soundfont;
}

void FluidSynthMidiSynth::short_message(uint8_t status, uint8_t data1, uint8_t data2, uint8_t size)
{
    if (!active()) return;
    const int channel = status & 0x0f;
    switch (status & 0xf0) {
    case 0x80:
        impl_->noteoff(impl_->synth, channel, data1 & 0x7f);
        break;
    case 0x90:
        if ((data2 & 0x7f) == 0) {
            impl_->noteoff(impl_->synth, channel, data1 & 0x7f);
        } else {
            impl_->noteon(impl_->synth, channel, data1 & 0x7f, data2 & 0x7f);
        }
        break;
    case 0xa0:
        impl_->key_pressure(impl_->synth, channel, data1 & 0x7f, data2 & 0x7f);
        break;
    case 0xb0: {
        const int controller = data1 & 0x7f;
        // X68000 MIDI drivers commonly send the classic Omni/Mono/Poly
        // channel-mode controllers (122, 124..127) while initializing every
        // part. FluidSynth's basic-channel implementation interprets those as
        // channel-group topology changes, which can make later multitimbral
        // note-ons fail. Real GM/GS modules keep their independent parts
        // usable here, so ignore only the topology/local-control commands.
        if (controller == 122 || controller >= 124) {
            break;
        }
        impl_->cc(impl_->synth, channel, controller, data2 & 0x7f);
        break;
    }
    case 0xc0:
        impl_->program_change(impl_->synth, channel, data1 & 0x7f);
        break;
    case 0xd0:
        impl_->channel_pressure(impl_->synth, channel, data1 & 0x7f);
        break;
    case 0xe0:
        impl_->pitch_bend(impl_->synth, channel,
                          (static_cast<int>(data2 & 0x7f) << 7) | (data1 & 0x7f));
        break;
    case 0xf0:
        if (status == 0xff) {
            impl_->system_reset(impl_->synth);
        }
        break;
    default:
        (void)size;
        break;
    }
}

void FluidSynthMidiSynth::sysex(const std::vector<uint8_t>& data)
{
    if (!active() || data.empty()) return;
    int handled = 0;
    impl_->sysex(impl_->synth,
                 reinterpret_cast<const char*>(data.data()),
                 static_cast<int>(data.size()),
                 nullptr, nullptr, &handled, 0);
}

int FluidSynthMidiSynth::render_s16(int16_t* interleaved_stereo, int frames)
{
    if (!active() || interleaved_stereo == nullptr || frames <= 0) {
        return 0;
    }
    const int result = impl_->write_s16(impl_->synth, frames,
                                        interleaved_stereo, 0, 2,
                                        interleaved_stereo, 1, 2);
    return result >= 0 ? frames : 0;
}

} // namespace hoot
