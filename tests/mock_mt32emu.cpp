#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>

#if defined(_WIN32)
#define EXPORT extern "C" __declspec(dllexport)
#else
#define EXPORT extern "C" __attribute__((visibility("default")))
#endif

using bit32u = unsigned int;
using bit16s = signed short;
using bit8u = unsigned char;
using return_code = int;
struct mt32emu_data {
    bool control = false;
    bool pcm = false;
    bool open = false;
    double sample_rate = 0.0;
    int midi_delay_mode = -1;
    unsigned short_messages = 0;
    unsigned note_ons = 0;
    unsigned sysex_messages = 0;
    bit32u last_short = 0;
    unsigned last_sysex_len = 0;
    bit8u last_sysex_first = 0;
    bit8u last_sysex_last = 0;
};
using context = mt32emu_data*;
using const_context = const mt32emu_data*;
union report_handler_i { const void* v0; const void* v1; const void* v2; };

EXPORT context mt32emu_create_context(report_handler_i, void*) { return new mt32emu_data; }
EXPORT void mt32emu_free_context(context c) { delete c; }
EXPORT size_t mt32emu_get_machine_ids(const char** ids, size_t size)
{
    static const char* machines[] = {"mt32_1_07", "cm32l_1_02"};
    if (ids != nullptr) {
        const size_t n = size < 2 ? size : 2;
        for (size_t i = 0; i < n; ++i) ids[i] = machines[i];
    }
    return 2;
}
EXPORT return_code mt32emu_add_rom_file(context, const char*) { return -1; }
EXPORT return_code mt32emu_add_machine_rom_file(context c, const char* machine, const char* filename)
{
    const std::string m = machine ? machine : "";
    const std::string f = filename ? filename : "";
    const bool mt = m.rfind("mt32_", 0) == 0;
    const bool cm = m.rfind("cm32l", 0) == 0;
    if ((mt && f.find("mt32") == std::string::npos) || (cm && f.find("cm32l") == std::string::npos)) return 0;
    if (f.find("control") != std::string::npos) { c->control = true; return 1; }
    if (f.find("pcm") != std::string::npos) { c->pcm = true; return 2; }
    return 0;
}
EXPORT void mt32emu_set_stereo_output_samplerate(context c, double rate) { c->sample_rate = rate; }
EXPORT void mt32emu_set_midi_delay_mode(context c, int mode) { c->midi_delay_mode = mode; }
EXPORT return_code mt32emu_open_synth(const_context cc)
{
    auto c = const_cast<context>(cc);
    if (!c->control || !c->pcm) return -4;
    c->open = true;
    c->short_messages = 0;
    c->note_ons = 0;
    c->sysex_messages = 0;
    return 0;
}
EXPORT void mt32emu_close_synth(const_context cc) { const_cast<context>(cc)->open = false; }
EXPORT int mt32emu_is_open(const_context c) { return c && c->open ? 1 : 0; }
EXPORT void mt32emu_play_short_message(const_context cc, bit32u message)
{
    auto c = const_cast<context>(cc); ++c->short_messages; c->last_short = message;
    const bit8u status = static_cast<bit8u>(message & 0xffu);
    const bit8u velocity = static_cast<bit8u>((message >> 16) & 0x7fu);
    if ((status & 0xf0u) == 0x90u && velocity != 0) ++c->note_ons;
}
EXPORT return_code mt32emu_play_msg(const_context cc, bit32u message)
{
    mt32emu_play_short_message(cc, message); return 0;
}
EXPORT return_code mt32emu_play_sysex(const_context cc, const bit8u* data, bit32u len)
{
    auto c = const_cast<context>(cc); ++c->sysex_messages; c->last_sysex_len = len;
    if (len) { c->last_sysex_first = data[0]; c->last_sysex_last = data[len - 1]; }
    return 0;
}
EXPORT void mt32emu_render_bit16s(const_context cc, bit16s* out, bit32u frames)
{
    const auto c = const_cast<context>(cc);
    for (bit32u i = 0; i < frames; ++i) {
        if (c->note_ons == 0) {
            out[i * 2] = 0;
            out[i * 2 + 1] = 0;
        } else {
            out[i * 2] = static_cast<bit16s>(100 + c->short_messages + (c->midi_delay_mode == 0 ? 0 : 1000));
            out[i * 2 + 1] = static_cast<bit16s>(200 + c->sysex_messages +
                (c->last_sysex_first == 0xf0 && c->last_sysex_last == 0xf7 ? 10 : 0));
        }
    }
}
EXPORT const char* mt32emu_get_library_version_string() { return "2.7.2-mock"; }
