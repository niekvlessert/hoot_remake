# Classic visual fidelity + channel mute/solo stage (2026-08-09)

This stage builds on `hoot_project_playback_wasm_fixed_source_2026-08-09`.

## Visual fidelity

- center-origin stereo channel meters retained and refined
- fast attack, short peak hold and smoother release
- spectrum analyser now has a fast attack / smooth decay instead of snapping to zero
- active keyboard keys use the classic cool Hoot-style highlight
- channel label/meter/tone regions no longer overlap
- current track marquee remains the actual playing title

## Channel mute / solo

libhoot now exposes additive per-visual-channel mute calls. Implemented host-side
mute targets include YM2151, YM2203/YM2608 FM+SSG+rhythm+ADPCM, OPL, X68000 PCM8,
PC-98 PCM86/BEEP and MIDI channels where the driver exposes those voices. Guest
driver timing/state always keeps running.

The SDL UI supports click-to-mute, right/Shift/double-click solo, number-key mute,
Shift+number solo and `U` to clear channel state. Master `M` remains independent.

The mdxmini/MXDRV compatibility path has no public per-voice mute API and therefore
reports channel mute as unsupported rather than faking it.
