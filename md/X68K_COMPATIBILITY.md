# X68000 compatibility pass

## Scope

This pass targets the existing `x68k/generic` replay host. MSX is deliberately
out of scope. The current catalogue contains 731 generic X68000 entries and
16,885 tracks.

## Implemented in this pass

- The complete project and bundled dependencies build successfully.
- The X68000 packed-data image now spans `0x000000-0xe7ffff` instead of only
  2 MiB. Exact device, mailbox and work-RAM mappings retain priority.
- A memory regression test covers data at `0x300000`, `0xe00010`, and
  `0xe78000`, plus mailbox, I/O fallback, work RAM, and the wrapped
  `0xfffff8/0xfffffc` workspace used by Human68k/XC code with `A6=0`.
- Catalogue `opm_mix`, `pcm_mix`, and `total_mix` values now affect rendering.
- `HOOT_X68K_OPM_GAIN`, `HOOT_X68K_ADPCM_GAIN`, and
  `HOOT_X68K_TOTAL_GAIN` provide explicit runtime overrides.
- `render_float()` now renders the same replay as `render_s16()` rather than a
  silent placeholder and uses a non-aliasing conversion buffer.
- `hootprobe --catalog-driver x68k/generic` restricts a scan to X68000 entries.
- Capability reasons identify OPMDRV-family entries explicitly.

The catalogue has 11 generic X68000 entries with assets at or above the old
2 MiB boundary. The highest asset offset is `0xe78000`. It has 136 OPMDRV
entries; 109 have none of the currently declared MIDI, MFP, PCM8, or special
DMA limitations and are the first high-yield compatibility group.

## Run the X68000 scan

```sh
./build/hootprobe \
  --catalog catalog/hoot.sqlite.zst \
  --packs packs \
  --catalog-driver x68k/generic \
  --seconds 5 \
  --timeout 20 \
  --output x68k-compatibility.json
```

Use `--all-tracks` only after the first-track smoke scan is stable. Include
`--include-missing` to produce a complete inventory even when most ZIP files
are not locally available.

## Real-pack smoke coverage

Six provided archives have now been scanned across every catalog track. The
plain OPM entries cover 196 catalog commands: 191 audible tracks and five
STOP/FADE controls. There were no real silent tracks, load/CPU/render errors,
unsupported opcodes, or clipped samples after correcting Hoot mixer semantics.
See `md/X68K_PACK_SMOKE_RESULTS.md` and the generated JSON report for details.

The generic host now includes CZ-6BM1-compatible MIDI transmit timing plus
module-specific software synthesis. Munt/libmt32emu renders `midiout_type` 1/2
(MT-32) and the CM-32L-compatible LA section of type 3 (CM-64); Hoot's CM-32P
renderer supplies the type-3 PCM section from user-provided Roland PCM ROMs.
FluidSynth and, for SC-55, optional Nuked-SC55 cover types 4/7/8. M1 and
Vermouth module classes still need dedicated synthesis. The CM-32P and generic
SoundFont paths are compatibility renderers, not cycle-/hardware-exact module
emulation.

A second MFP/IOCS matrix now passes all 483 catalog tracks from Shooting,
Neural Gear, Namco Video Game Music Library, and A-Train II. A-Train II was
blocked because compiled A2.X routines intentionally address local variables as
negative offsets from `A6=0`; on the 24-bit 68000 those accesses wrap to
`0xfffff8` and `0xfffffc`. The player discarded that top-of-address-space
workspace as unmapped I/O. Preserving a writable final 64 KiB allows its
FLOAT2.X/line-F bootstrap, OPMDRV installation, MFP interrupts, five MU data
tracks, Bolero track, and eight sound-effect commands to run. All 14 A-Train
entries are audio-active with no clipping or diagnosed CPU/render failures.

## MFP/IOCS smoke matrix

| Archive | Coverage | Tracks | Result |
|---|---|---:|---|
| `shooting68snd.zip` | Compact OPMDRV baseline | 9 | 9/9 audio-active |
| `ngear68snd.zip` | MFP plus FM/ADPCM | 17 | 17/17 audio-active |
| `nvgml68snd.zip` | Large OPMDRV/MFP catalogue and >2 MiB data | 443 | 443/443 audio-active |
| `a268snd.zip` | FLOAT2.X, line-F, IOCS, MFP and wrapped high workspace | 14 | 14/14 audio-active |
| **Total** |  | **483** | **483/483** |

Run the repeatable gate with:

```sh
./tools/run_x68k_mfp_smoke.py --packs packs
```

Together with the six-entry plain-OPM matrix, the supplied non-MIDI
regression set now covers 679 catalog commands: 674 audio-active tracks and
five STOP/FADE controls, with no failed commands.

The next high-value work is authentic-ROM reference-audio comparison for the
Munt/CM-64 path, real-pack validation of CM-32P/SN-U110 usage, M1/Vermouth
synthesis, and broader MIDI/PCM8 pack validation. More generic smoke packs
remain useful, but they are no longer required to demonstrate the implemented
OPMDRV/MFP/FLOAT2 paths.

## MIDI transport and GM/GS rendering

The X68000 host emulates the CZ-6BM1 transmit-side behavior used by the supplied
ZMSC drivers: a 256-byte ready/full boundary, persistent 31.25 kbit/s serial
timing (3200 10 MHz 68000 cycles per 10-bit MIDI byte), running status, channel
voice messages, system-common bytes, realtime handling and arbitrary SysEx.
Transmit timing uses a persistent cycle accumulator; the old per-slice integer
drain could leave the FIFO permanently full.

For `midiout_type` 4, 7 and 8 the player can dynamically load FluidSynth and a
local GM SoundFont. No FluidSynth library or SoundFont is bundled. Set
`HOOT_X68K_SOUNDFONT` (or `HOOT_MIDI_SOUNDFONT`) to select an SF2 explicitly;
`HOOT_X68K_MIDI_GAIN` adjusts the MIDI contribution before the final X68000
mix. If FluidSynth or an SF2 is absent, the guest MIDI transport still runs and
reports diagnostics, but no software-synth audio is mixed.

Asuka 120% provides the current real-pack gate. Fifteen GS tracks and fifteen
TG-100/GM-catalogue tracks are each started in a fresh process and all 30 are
audio-active with the software backend. The 3-second gate transmits 91,012 MIDI
bytes, parses 34,614 channel messages and 868 SysEx messages, observes 1,797
note-ons, and reports zero malformed bytes or clipped samples. The TG-100 entry
is rendered as a GM-compatible fallback; it is not Yamaha TG-100 emulation.
Likewise, GS bank/SysEx behavior is not claimed to be bit-identical to SC-55 or
SC-88 hardware.

Run the gate with:

```sh
./tools/run_x68k_midi_gm_gs_validation.py \
  --packs packs \
  --soundfont /path/to/gm.sf2 \
  --output reports/x68k_midi_gm_gs_validation.json
```


### MT-32 / Munt rendering

For `midiout_type` 1 and 2, `auto` creates a dynamically loaded Munt/mt32emu
backend and selects a ROM-compatible MT-32 machine. Type 3 first tries a full
CM-64 composite: a Munt CM-32L machine for LA plus Hoot's CM-32P PCM renderer.
No libmt32emu binary or Roland ROM is bundled. Configure `HOOT_MT32_ROM_PATH`,
`HOOT_CM32L_ROM_PATH`, `HOOT_CM32P_ROM_PATH` and, if normal dynamic-library
discovery is insufficient, `HOOT_MT32EMU_LIBRARY`. Optional SN-U110 cards use
`HOOT_CM32P_CARD_ROM`; catalogue variants naming SN-U110-07 or SN-U110-10 can
select `HOOT_CM32P_CARD_ROM_07` or `_10` automatically. If PCM ROMs are missing,
`auto` falls back to CM-32L/LA only.

The CZ-6BM1 host already serializes bytes at the MIDI wire rate. The Munt
backend therefore selects immediate MIDI input delay, preventing mt32emu from
adding a second cable-delay stage while retaining its synth/MCU emulation.

The ABI/backend path is covered by a no-ROM redistribution test module. The
real-pack integration gate runs Akumajou Dracula and Parodius through both
MT-32 catalogue variants with a fresh process per track. Authentic audio
comparison remains conditional on a user-supplied real libmt32emu runtime and
legally obtained Roland ROMs. Run it with:

```sh
./tools/run_x68k_munt_validation.py \
  --packs packs \
  --mt32-rom-path /path/to/mt32-roms \
  --output reports/x68k_munt_validation.json
```

## Current evidence boundary

The complete codebase compiles and all synthetic tests pass. The provided plain
OPM matrix is smoke-tested, but no claim about exact pitch, tempo, instruments,
balance, loops, or track duration is made without trusted original-Hoot renders.

## PCM8 direct-block stage

The X68000 host now implements eight-voice direct PCM8 output for low-nibble-first X68000 ADPCM, signed big-endian 16-bit PCM and signed 8-bit PCM at all documented PCM8/PCM8A rates. It uses safe 24-bit guest-memory reads, deterministic Q32 sample-rate conversion and a separate 32-bit stereo accumulation buffer after YM2151 and standard ADPCM. `hootprobe` records rendered voice frames, consumed source bytes, completed voices and memory faults.

The implementation is synthetic-tested and preserves the complete plain-OPM regression matrix. Array-chain calls and channels above seven remain explicit limitations. Real-pack validation is still pending because `asuka68snd.zip`, `madstk68snd.zip` and `pm68snd.zip` are not present. See `X68000_PCM8_DIRECT_MIXER_STAGE_2026-08-06.md`.
