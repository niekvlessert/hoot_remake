# SC-88 runtime warning stage — 2026-08-08

## Scope

Make SC-88 catalogue targets explicit at runtime instead of silently presenting
FluidSynth compatibility rendering as if it were hardware-exact Roland SC-88.

## Behaviour

When a PC-98 or X68000 entry uses Hoot `midiout_type=7` and the selected synth
backend is FluidSynth, Hoot now reports this English warning:

> Roland SC-88 target: FluidSynth compatibility rendering is active, not hardware-exact SC-88. Set [midi] soundfont in hootplay.ini for the closest available compatible bank; authentic playback requires a real SC-88 or a dedicated SC-88-compatible renderer.

The warning is exactly 255 characters, fitting the existing 256-byte
`HootTrackInfo::warning` buffer including its terminating NUL.

GM-only `midiout_type=8` entries do not receive the warning. A future native
SC-88 backend can avoid it by reporting a backend other than `fluidsynth`.

`hootplay.ini` also documents that `[midi] soundfont` is a compatibility bank
for SC-88 rather than exact hardware synthesis.

## Validation

- Release suite: 20/20 tests pass.
- `pc98_midi_driver_test` now checks the SC-88 warning whenever the test
  environment supplies a real FluidSynth SoundFont.
- Real-pack check with `mjgensk3_98.zip`, entry `mjgensk3-98-beep-2`, track 0:
  - target: SC-88 (`midiout_type=7`)
  - backend: FluidSynth
  - warning: present with the complete text above
  - audio active
  - 25 note-ons in the short check
  - 0 malformed MIDI bytes
  - 0 unsupported CPU opcodes

## MXDRV packs received

The supplied MXDRV validation packs are structurally suitable for the next
real-pack gate:

- `kowin368snd.zip`: 2 MDX tracks, each with its matching PDX sample bank.
- `sz2w68snd.zip`: 13 MDX tracks, no external PDX files in the archive.

They are not redistributed in project source packages.
