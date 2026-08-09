# libhoot public API

`libhoot` is the platform-neutral replay library. It contains the catalogue,
pack loading, replay hosts, sound-chip emulation and optional MIDI synth
backends. It has no SDL dependency.

Installable public headers are under `include/hoot`; the primary header is
`hoot/hoot_api.h`. The ABI is C-compatible so frontends do not need to share
C++ runtime details with the replay core.

The frontend-facing API covers:

- context creation and runtime pack-directory selection;
- catalogue loading, entry/archive lookup and enumeration;
- track enumeration, selection and reset;
- interleaved stereo `s16` and float rendering;
- track metadata/warnings;
- versioned `HootVisualState` telemetry for Hoot-style visualizers.

`HootVisualState` publishes architecture, CPU, device, driver, CPU registers,
a 512-byte driver-work window and up to 32 logical sound channels. Channels
carry note/key state, activity, volume, pan, instrument and level estimates.
Consumers must treat the structure as a snapshot and honor `abi_version` and
`struct_size`.

The replay core is intentionally independent of windowing and audio-device
APIs. A client may call `hoot_render_s16()` from its own scheduling model and
then obtain a telemetry snapshot with `hoot_get_visual_state()`.

## Text encoding

All public catalogue, entry, track, warning and visual-state strings are UTF-8.
This includes Japanese Hoot XML metadata. Modern UTF-8 XML is consumed directly;
legacy Hoot XML declared as Shift_JIS/CP932 is normalized to UTF-8 by the XML loader
on native platforms (Windows uses CP932 conversion; POSIX builds use iconv when
available). Fixed-size character arrays in the C ABI are truncated only at UTF-8
codepoint boundaries, so callers never receive a partial multibyte character from
libhoot. Frontends should treat these buffers as UTF-8 rather than converting them
through a locale/code page.


## Product split

The native build intentionally exposes three distinct products:

- `libhoot`: the reusable replay/telemetry library;
- `hootplay`: the command-line player, using the public `libhoot` API for
  catalogue discovery and playback;
- `hootui`: the SDL3 graphical player, also using the public `libhoot` API.

`hootprobe` and `hoot2wav` remain utility frontends. Frontend-only parsing of
`hootplay.ini` is kept outside `libhoot`, so the library itself has no UI,
SDL, terminal or configuration-file dependency.
