# X68000 MIDI transport and GM/GS rendering stage — 2026-08-07

> **Status update (2026-08-07):** MT-32 synthesis described here as deferred/transport-only is superseded by `MUNT_MT32_STAGE_2026-08-07.md`. CM-64 now has Munt CM-32L/LA synthesis, but its separate CM-32P section remains unimplemented.

## Scope

This stage implements the first two MIDI phases for `x68k/generic`:

1. emulate the X68000 MIDI transmit path closely enough for real ZMSC drivers to
   leave their initialization loops and emit timed MIDI messages;
2. render GM/GS-compatible catalogue variants through an optional software
   synthesizer and mix that audio with the existing YM2151/ADPCM/PCM8 path.

It does not claim MT-32/CM-64, SC-55/SC-88 or TG-100 hardware-exact synthesis.

## CZ-6BM1 transport

The old implementation only counted bytes written to the MIDI device. Its FIFO
also drained incorrectly: every short CPU slice independently divided its cycle
count by 3200, discarding the fractional progress. With normal ~256-cycle CPU
slices the virtual 256-byte FIFO could therefore become permanently full.

The new transport has:

- a 256-byte hardware ready/full boundary;
- persistent serial timing at one byte per 3200 10 MHz CPU cycles, matching
  31.25 kbit/s MIDI with ten serial bits per byte;
- queueing beyond the ready boundary, matching the legacy PX68K/Hoot-style
  buffered counter while status correctly reports full;
- TX-ready interrupt generation when the buffered count drops below 256;
- persistent MIDI/general timer counters and IRQ4 arbitration;
- running-status decoding;
- note on/off, poly pressure, controllers, program changes, channel pressure
  and pitch bend;
- system-common and realtime-byte handling;
- arbitrary SysEx framing and forwarding;
- detailed `hootprobe` transport counters and optional X68000 trace events.

The first MIDI cue uses a bounded poll-to-ack handshake because real ZMSC MIDI
images serialize a large module/part reset before acknowledging the mailbox.
Subsequent track commands retain the established fixed timing so the existing
non-MIDI regression is unaffected.

## FluidSynth GM/GS fallback

FluidSynth is loaded dynamically at runtime; it is not a link-time dependency.
Likewise, no SoundFont is bundled.

A SoundFont is selected in this order:

1. `HOOT_X68K_SOUNDFONT`;
2. `HOOT_MIDI_SOUNDFONT`;
3. conventional system GM SoundFont locations.

`HOOT_X68K_MIDI_GAIN` controls the MIDI contribution to the final stereo mix.
If FluidSynth or an SF2 is unavailable, MIDI transport remains active and
observable, but no software-synth audio is produced.

FluidSynth rendering is enabled only for the catalogue classes currently mapped
to GM/GS-compatible use:

- `midiout_type=4`: GS / SC-55 class;
- `midiout_type=7`: later GS / SC-88 class;
- `midiout_type=8`: GM class (including the Asuka TG-100 catalogue variant as
  a compatibility fallback).

MT-32/CM-64 and other module classes keep transport only until a dedicated
backend is implemented.

The FluidSynth adapter ignores MIDI Channel Mode controller 122 and 124–127.
The Asuka initialization stream sends these to each multitimbral part; passing
them directly to FluidSynth changes its basic-channel topology and causes valid
later note-ons on several channels to fail. Controllers 120, 121 and 123 retain
their reset/all-notes semantics. The raw transport is not modified; this is a
software-synth compatibility rule.

## Real-pack validation

`asuka68snd.zip` supplies two relevant variants:

| Entry | Catalogue class | Tracks | Result |
|---|---:|---:|---|
| `asuka68snd-generic-2` | 4 / GS | 15 | 15/15 audio-active |
| `asuka68snd-generic-3` | 8 / TG-100 catalogue variant via GM fallback | 15 | 15/15 audio-active |
| **Total** |  | **30** | **30/30** |

The validation starts every track in a fresh `hootprobe` process, renders three
seconds with a fixed local GM SoundFont, and requires an acknowledged mailbox,
active synth backend, transmitted bytes, parsed channel messages, at least one
note-on, rendered synth frames, no malformed MIDI bytes and no clipping.

Aggregate observed transport activity:

- 91,023 bytes enqueued;
- 91,012 bytes serialized during the bounded render windows;
- 34,614 channel messages;
- 868 SysEx messages / 7,868 SysEx payload bytes;
- 21,837 running-status messages;
- 1,797 note-ons and 1,649 note-offs;
- 28,419 controller changes;
- 688 program changes;
- 2,061 pitch bends;
- zero malformed bytes;
- zero clipped output samples.

A few bytes can remain queued when a three-second scan ends in the middle of a
MIDI stream. That is expected and is not a transport failure.

The supplied generic SoundFont cannot reproduce every Roland/Yamaha bank or
module-specific SysEx parameter. Therefore this stage proves functional
GM/GS-compatible playback, timing/backpressure and event delivery, not exact
SC-55, SC-88 or TG-100 timbre/effects.

## Regression

- All four unit tests pass, including the new MIDI transport test.
- All 196 established plain-OPM commands are exactly equal to the previous
  production baseline across track outcomes, rendered/first-audible frames,
  peak/RMS/nonzero/clipping metrics, CPU/I/O/chip counters, startup state and
  PCM8 diagnostics.
- All 50 real-pack PCM8 tracks are exactly equal to their previous production
  baseline across the same reported audio/driver state plus PCM8 counters.
- AddressSanitizer and UndefinedBehaviorSanitizer pass all unit tests and
  representative Asuka GS, Asuka TG-100/GM, Asuka PCM8 and Mad Stalker PCM8
  tracks.

## Repeatable gates

```sh
cmake --build build
ctest --test-dir build --output-on-failure

./tools/run_x68k_midi_gm_gs_validation.py \
  --packs packs \
  --soundfont /path/to/gm.sf2 \
  --output reports/x68k_midi_gm_gs_validation_2026-08-07.json

./tools/run_x68k_pack_smoke.py \
  --packs packs \
  --output reports/x68k_plain_opm_midi_stage_regression_2026-08-07.json

./tools/run_x68k_pcm8_realpack_validation.py \
  --packs packs \
  --output reports/x68k_pcm8_midi_stage_regression_2026-08-07.json
```

## Remaining work

- Dedicated MT-32/CM-64 synthesis, preferably mt32emu/Munt.
- Better Roland GS / Yamaha TG-100 module fidelity than a generic GM SoundFont.
- Reference-WAV comparison for timing, balances, patches, effects and SysEx.
- MIDI input is not needed for Hoot music replay and is not emulated here.
- PCM8 array-chain/linked-chain remains separate unfinished work.
