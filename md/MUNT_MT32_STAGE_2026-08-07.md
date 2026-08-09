# Munt / mt32emu MT-32 stage — 2026-08-07

> **Status update:** the CM-64 partial-status notes in this historical stage are superseded by `CM32P_STAGE_2026-08-07.md`; MT-32/Munt details remain current.

## Scope

Add a shared optional Munt/libmt32emu software-synth backend to the existing
PC-98 MPU-401 and X68000 CZ-6BM1 MIDI transports. The goal is to render Hoot
`midiout_type=1` (MT-32 emulation) and `midiout_type=2` (MT-32) correctly with
user-supplied Roland ROMs, without making libmt32emu a build dependency or
redistributing copyrighted ROM images.

`midiout_type=3` (CM-64) is deliberately classified as **partial**: Munt can
provide the CM-32L-compatible LA sound source, but the separate CM-32P PCM
sound source contained in a physical CM-64 is outside mt32emu and remains
unimplemented.

## Runtime backend

New files:

- `src/sound/mt32emu_midi_synth.h`
- `src/sound/mt32emu_midi_synth.cpp`

The backend implements the existing `MidiSynth` abstraction and dynamically
loads the mt32emu C API. Hoot therefore still builds and runs when Munt is not
installed. Runtime search covers conventional Windows, macOS and Linux library
names, with `HOOT_MT32EMU_LIBRARY` as an explicit override.

Required C API calls are loaded dynamically:

- context create/free;
- ROM loading;
- output sample-rate selection;
- synth open/close/state;
- short MIDI and SysEx input;
- native signed 16-bit interleaved stereo rendering.

For Munt 2.5+ the backend uses machine-aware ROM loading. When available it
queries the runtime's current machine IDs and selects IDs beginning with
`mt32` or `cm32l` rather than assuming a single ROM revision. Explicit
`HOOT_MT32_MACHINE` and `HOOT_CM32L_MACHINE` overrides remain available.
Older runtimes fall back to generic ROM loading, for which the two ROM families
must be stored in separate configured directories.

## MIDI timing

Both Hoot transports already serialize MIDI at the physical 31.25 kbit/s wire
rate before decoded messages are presented to a synth. mt32emu also has an
optional MIDI-cable delay model. If left enabled, that would count cable time
twice. The backend therefore selects `MT32EMU_MDM_IMMEDIATE` when the API is
available. Internal mt32emu LA synthesis/MCU behavior remains in the synth;
only the redundant external cable-delay stage is disabled.

## ROM selection and configuration

`hootplay.ini` now exposes:

```ini
[midi]
# backend = auto
# mt32emu_library = lib/libmt32emu.so
# munt_rom_path = roms/munt
# mt32_rom_path = roms/mt32
# cm32l_rom_path = roms/cm32l
# mt32_machine = mt32_1_07
# cm32l_machine = cm32l_1_02
```

Environment equivalents:

- `HOOT_MT32EMU_LIBRARY`
- `HOOT_MUNT_ROM_PATH`
- `HOOT_MT32_ROM_PATH`
- `HOOT_CM32L_ROM_PATH`
- `HOOT_MT32_MACHINE`
- `HOOT_CM32L_MACHINE`

`[midi] backend=auto` now chooses:

- type 1/2: Munt MT-32;
- type 3: Munt CM-32L/LA (partial CM-64);
- type 4: Nuked-SC55 first, FluidSynth fallback;
- type 7/8: FluidSynth.

`backend=munt` forces the Munt path for types 1-3 and emits a clear warning for
other module classes.

No Munt binary or Roland ROM is bundled.

## Platform integration

### PC-98

The existing DOS/V30 host, PC-98 MPU-401, intelligent-mode IRQ/timing work and
shared MIDI decoder feed the Munt backend with no pack-specific shortcut.
Backend debug kinds are now:

- `0`: none;
- `1`: FluidSynth;
- `2`: Nuked-SC55;
- `3`: Munt MT-32;
- `4`: Munt CM-32L.

The existing synthetic real-8086 `.COM` MPU fixture has been extended so the
same guest program is run once through its existing GS path and once with
`midiout_type=2` through Munt. The latter requires backend kind 3, transmitted
MIDI, a note-on, rendered synth frames and non-zero audio from the ABI test
module.

### X68000

The existing CZ-6BM1 byte transport and decoder feed the same Munt backend.
No new MIDI-device addresses or X68000 driver-family special cases are needed.
`hootprobe` reports `munt-mt32` or `munt-cm32l` in its MIDI diagnostics.

## Tests

A new `mock_mt32emu` module implements the small libmt32emu ABI surface used by
Hoot. It does not emulate MT-32 sound; it exists to prove dynamic loading,
model-specific ROM selection, message framing, reset behavior and PCM mixing
without requiring copyrighted ROMs in CI.

`mt32emu_midi_synth_test` verifies:

- MT-32 and CM-32L model separation;
- machine-aware ROM loading;
- short MIDI delivery;
- SysEx F0/F7 framing (Hoot's decoder stores the inner body);
- reset/reopen behavior;
- native stereo render integration;
- rejection of an incompatible ROM family.

Final Release test suite at this stage: **13/13**. The same **13/13** also pass under ASan+UBSan.

## X68000 real-pack ABI gate

The provided real Hoot archives were run through the complete X68000 guest
path with `mock_mt32emu` standing in only for the final synth library. Every
track starts in a fresh `hootprobe` process and runs for eight seconds.

| Entry | Class | Runs | Audio-active | Note-active | Malformed MIDI |
|---|---|---:|---:|---:|---:|
| `ad68snd-generic-2` | MT-32 | 22 | 22 | 22 | 0 |
| `ad68snd-generic-3` | MT-32 emulation | 22 | 22 | 22 | 0 |
| `paro68snd-generic-2` | OPM + MT-32 | 36 | 36 | 33 | 0 |
| `paro68snd-generic-3` | OPM + MT-32 emulation | 36 | 36 | 33 | 0 |
| **Total** | | **116** | **116** | **110** | **0** |

Parodius' three non-note-active commands in each variant are hybrid OPM-only
cues; they remain audio-active and still execute cleanly. Akumajou Dracula is
the stronger pure-MT-32 gate: all 44 runs across the two MT-32 variants issue
notes through the backend.

This validates the host/guest/transport/dynamic-backend integration. It is
**not** a claim of authentic MT-32 sound quality because the environment does
not contain a real libmt32emu install plus legally obtained Roland ROMs.

The pre-existing six-pack plain-OPM regression is unchanged after this stage:
196 catalog commands, 191 audio-active tracks, five intentional STOP/FADE
controls and zero failures. Representative PC-98 GM/GS real-pack runs through
FluidSynth also remain audio-active with zero malformed MIDI and zero
unsupported CPU opcodes.

The repeatable gate is `tools/run_x68k_munt_validation.py`.

## Catalogue impact

### PC-98 DOS MIDI

Current catalogue-title inventory after this stage:

| MIDI type | Configurations | Title tracks | Backend |
|---|---:|---:|---|
| MT-32 emulation (1) | 103 | 2,389 | Munt MT-32 |
| MT-32 (2) | 101 | 2,376 | Munt MT-32 |
| CM-64 (3) | 13 | 337 | Munt CM-32L/LA, partial |
| SC-55/GS (4) | 216 | 4,614 | Nuked-SC55 / FluidSynth |
| SC-88/GS (7) | 17 | 282 | FluidSynth compatibility |
| GM (8) | 26 | 518 | FluidSynth |
| **Total** | **476** | **10,516** | |

Thus **463/476 configurations and 10,179/10,516 title tracks** now have a full
software synth class available. The remaining 13/337 are not transport-only:
they have CM-32L/LA rendering, but are kept partial because CM-32P is missing.
There are now zero PC-98 MIDI configurations in this inventory that are merely
"transport only".

These title counts are produced by the current catalogue-source scanner and
are not identical to older expanded-runtime-track counts in previous handover
documents.

### X68000 MIDI

The current catalogue contains 143 MIDI configurations:

- 48 type-1 + 44 type-2 = **92 MT-32 configurations now Munt-backed**;
- 9 type-3 CM-64 configurations are CM-32L/LA partial;
- 35 existing type-4/7/8 configurations have SC-55/SC-88/GM software backends;
- 7 configurations remain without the requested module synth: two unspecified
  type-0 entries, one M1 type-5 entry and four Vermouth type-6 entries.

So **127/143 X68000 MIDI configurations are fully synth-backed**, 9/143 are
CM-64-LA partial and 7/143 still need another module backend.

## Evidence boundary / remaining work

1. Run the real backend with an installed current libmt32emu and user-owned
   MT-32/CM-32L ROMs and compare audio against trusted hardware/reference
   captures. The current environment has neither ROM set, so authentic timbre
   validation cannot be performed here.
2. Validate real PC-98 MT-32 packs. PC-98 integration is end-to-end synthetic
   tested, but the supplied PC-98 real-pack set in this stage contains GM/GS,
   not MT-32 archives.
3. Add CM-32P synthesis if full CM-64 compatibility is required, especially
   for Japanese X68000 software that actually uses channels 11-16/CM-32P.
4. X68000 M1 and Vermouth module classes remain separate future backends.
5. Do not bundle Roland ROMs in project or dependency archives.
