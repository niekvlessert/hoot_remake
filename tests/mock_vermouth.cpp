#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <cwchar>

#if defined(_WIN32)
#define EXPORT extern "C" __declspec(dllexport)
#else
#define EXPORT extern "C" __attribute__((visibility("default")))
#endif

struct Module { uint32_t rate; };
struct Handle { uint32_t rate; uint32_t worksize; bool note; double phase; uint32_t shorts; uint32_t sysex; };

#if defined(MOCK_VERMOUTH_LEGACY)
EXPORT void* midimod_create(uint32_t rate)
{
    if (rate < 8000) return nullptr;
    return new Module{rate};
}
#else
EXPORT void* midimod_create(wchar_t* sf2, uint32_t rate)
{
    if (sf2 == nullptr || sf2[0] == 0 || rate < 8000) return nullptr;
    return new Module{rate};
}
#endif
EXPORT void midimod_destroy(void* p) { delete static_cast<Module*>(p); }
EXPORT void midimod_loadall(void*) {}
EXPORT void* midiout_create(void* p, uint32_t worksize)
{
    if (!p) return nullptr;
    return new Handle{static_cast<Module*>(p)->rate, worksize, false, 0.0, 0, 0};
}
EXPORT void midiout_destroy(void* p) { delete static_cast<Handle*>(p); }
EXPORT void midiout_setgain(void*, int) {}
EXPORT void midiout_shortmsg(void* p, uint32_t msg)
{
    auto* h = static_cast<Handle*>(p);
    const uint8_t st = msg & 0xff;
    const uint8_t vel = (msg >> 16) & 0x7f;
    if ((st & 0xf0) == 0x90) h->note = vel != 0;
    if ((st & 0xf0) == 0x80) h->note = false;
    ++h->shorts;
}
EXPORT void midiout_longmsg(void* p, const void*, uint32_t size)
{
    if (p && size >= 2) ++static_cast<Handle*>(p)->sysex;
}
EXPORT uint32_t midiout_get32(void* p, int32_t* pcm, uint32_t frames)
{
    auto* h = static_cast<Handle*>(p);
    if (!h || !pcm) return 0;
    for (uint32_t i = 0; i < frames; ++i) {
        int32_t v = 0;
        if (h->note) {
            v = static_cast<int32_t>(std::sin(h->phase) * 12000.0);
            h->phase += 2.0 * 3.141592653589793 * 440.0 / h->rate;
        }
        // A tiny positive signature also verifies that SysEx and short-message
        // calls reached the ABI without dominating the audible test tone.
        v += static_cast<int32_t>(h->shorts + h->sysex);
        pcm[i * 2] = v;
        pcm[i * 2 + 1] = -v;
    }
    return frames;
}
