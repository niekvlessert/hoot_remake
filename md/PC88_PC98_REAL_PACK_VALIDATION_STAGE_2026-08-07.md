# PC-88 / PC-98 Real-Pack Validation Stage

Date: 2026-08-07

## Scope

This stage takes the generic PC-88 and PC-98 hosts from synthetic validation to real Hoot pack validation. Six user-supplied archives were used only as external test inputs and are **not** included in the source package:

- `arcus.zip`
- `ys_88.zip`
- `xtaljbox.zip`
- `slayers_98.zip`
- `tarashi2_98.zip`
- `nekoex_98.zip`

The goal was to fix reusable host/CPU/DOS/timer semantics rather than add game-specific playback hacks.

## Real-pack result

Nine catalogue configurations and 438 catalogue tracks were exercised.

| Configuration | Driver | Tracks | Result |
|---|---|---:|---|
| Arcus OPN | PC-88 / YM2203 | 28 | 28/28 audio-active |
| Arcus OPNA | PC-88 / YM2608 | 28 | 28/28 audio-active |
| Crystal Soft Jukebox OPN | PC-88 / YM2203 | 58 | 58/58 audio-active |
| Crystal Soft Jukebox OPNA | PC-88 / YM2608 | 58 | 58/58 audio-active |
| Ys OPN | PC-88 / YM2203 | 34 | 34/34 audio-active |
| Slayers OPN | PC-98 DOS / YM2203 | 83 | 83/83 audio-active |
| Tarashi 2 OPN | PC-98 DOS / YM2203 | 9 | 9/9 audio-active |
| Neko Manma EX OPN | PC-98 DOS / YM2203 | 71 | 69 audio, 1 intentional STOP, 1 PMD incompatibility |
| Neko Manma EX OPNA | PC-98 DOS / YM2608 | 69 | 69/69 audio-active |

Totals:

- PC-88: **206/206 audio-active**.
- PC-98: **230 audio-active**, 1 intentional silent `[STOP]`, 1 real failure.
- Combined: **436/438 audio-active**.
- No PC-88 clipping was observed in the 3-second-per-track sweep.
- NekoEX OPNA still has a separate headroom/fidelity issue: 33/69 tracks touched the 16-bit output limit, for 7,034 clipped samples across the validation windows. Playback itself is functional on 69/69 tracks.

## Major fixes made in this stage

### PC-88 host

The PC-88 path is now a generic Z80 host for both YM2203 OPN and YM2608 OPNA rather than a Microcabin-only path.

Implemented or corrected:

- variable BGM and voice asset sizes; the old fixed 8 KiB music slot limit is gone;
- `init_pc` and `baseclock` handling;
- separate FM Timer A and Timer B scheduling;
- VRTC periodic interrupt support;
- YM2608 bank 1 and Delta-T/ADPCM asset loading;
- deterministic RAM restore before track changes;
- legacy Hoot 32-bit track-code semantics;
- legacy host port `80h` returns bits 24..31 of the selected track code;
- PC-88 I/O aliases needed by multiple driver families.

### KMZ80 IM2 fix

A real CPU-core bug was fixed in `third_party/libkss/modules/kmz80/kmz80i.h`.

The Z80 `I` register had incorrectly been mapped to `R7`, so refresh-register changes corrupted the high byte used for IM2 interrupt-vector addressing. Falcom's Ys driver depends on IM2 and exposed this immediately.

The `I` register now maps to `REGID_I` correctly. A regression test explicitly verifies that `LD I,n` survives refresh activity.

### Ys / Falcom track selection

Ys was not primarily blocked by `wstimer`. Its patch code reads Hoot host port `80h` as a subsong selector. The old host returned `FFh`.

The generic PC-88 host now exposes the high byte of the 32-bit Hoot track code on port `80h`. With the IM2 fix and this ABI behavior, **34/34 Ys tracks play**.

### PC-98 DOS/V30 host

The generic PC-98 DOS host now supports both YM2203 and YM2608 configurations.

Implemented or corrected:

- `clockmul` timing;
- archive-backed DOS open/read/seek/close over all pack assets;
- case-insensitive DOS-style path/name resolution;
- `.COM`, raw and MZ `.EXE` shell execution;
- MZ relocation processing and CS:IP / SS:SP setup;
- resident driver parking after shell/API execution;
- DOS interrupt-vector services used by resident music drivers;
- PC-98 BIOS sound hook at physical `0000:1000`;
- separate YM Timer A/Timer B status and expiry handling;
- dynamic INT 0Bh compatibility path used by PMD/NAX variants;
- YM2608 ID probing and 256 KiB Delta-T memory for PCMSET/PMDB2;
- extended Hoot track codes such as `0x0801..0x08xx`, where the high byte selects the asset and the low byte selects an effect/subsong command.

This is what made Slayers effects, Tarashi2/NAX and NekoEX PMD/PMDB2 work through the same host rather than separate per-game implementations.

### PC-98 SSG mix semantics

Historical Hoot source documents `ssg_mix` in half-decibel units. The generic PC-98 host now applies this as:

`gain = baseline * 10^(ssg_mix / 40)`

For NekoEX OPNA, `ssg_mix=-13` is therefore -6.5 dB. This corrected the previously excessive SSG level. The remaining OPNA clipping is predominantly in the native FM/rhythm/ADPCM output path, not the SSG mix.

### libvgm sanitizer fixes

Real OPN/OPNA music exposed two signed-left-shift undefined-behavior cases in the imported libvgm `fmopn.c` core. They were converted to bit-equivalent unsigned shifts, preserving the legacy two's-complement result while making the core clean under UBSan.

## Remaining NekoEX OPN failure

Only one real track failure remains in the six-pack matrix:

- `nekoex-98-opn`, track 13, code `0x34`: `QUIZ002.FN1`

The track does generate music before the error. The failure is not an unsupported 8086 instruction that should be implemented. The supplied OPN `PMD.COM` is PMD 4.0b and its command dispatch table ends before the later PMD command used by this data. Dispatch therefore falls through into non-table bytes and eventually executes PSP command-line data as code.

The matching OPNA/PMDB2 version of `QUIZ002.FN1` plays correctly.

The player deliberately does **not** patch PMD internals or invent an x86 opcode for this case. A proper fix requires a compatible OPN PMD resident or a verified historical-driver compatibility strategy.

## Conservative catalogue coverage

`tools/run_pcx_catalog_matrix.py` currently reports:

### PC-88 OPN + OPNA

- configurations: 580
- unique archives: 467
- tracks: 12,993
- generic-core candidates: **538**
- conservatively deferred: 42

Deferred feature classes include PCMx8, SSG-PCM helpers, GVRAM/N88-ROM services and timer-specific variants. Some packs marked by conservative flags can still work; Ys is an example where `wstimer` is present but was not the actual blocker.

### PC-98 DOS OPN + OPNA

- configurations: 1,846
- unique archives: 1,512
- tracks: 49,472
- generic-core candidates: **1,619**
- conservatively deferred: 227

Most deferred configurations require PC-98 MIDI, special sound-ROM behavior or still-unimplemented timer variants.

These are structural candidates, not a claim that thousands of packs have been auditively validated. The real-pack validation count in this stage is 438 tracks.

## Regression status

### Unit/sanitizer

- Release unit tests: 8/8 pass.
- ASan + UBSan unit tests: 8/8 pass.
- Representative real PC-88/PC-98 OPN and OPNA tracks run clean under ASan + UBSan.

### X68000

The PC-88/PC-98 work did not regress the existing X68000 player:

- OPM/control smoke: 191 audio-active tracks + 5 valid control commands, 196/196 total.
- PCM8 real packs: 50/50 audio-active, zero unknown commands, zero memory faults, zero clipping.
- MIDI GM/GS: 30/30 audio-active; 91,012 transmitted MIDI bytes, 1,797 note-ons, 868 SysEx messages, zero malformed bytes and zero clipping.

## Recommended next phase

The highest-value next PC-family work is:

1. run a much larger random/sample sweep over the 538 PC-88 and 1,619 PC-98 generic-core candidates as more archives become available;
2. add PC-98 `86` sound-board support and then the large `pc98dos/beep` group;
3. add PC-88 PCMx8/SSG-PCM helper support;
4. calibrate YM2608 native FM/rhythm/ADPCM headroom against a trusted FMGEN/Hoot reference before applying any arbitrary attenuation;
5. return to PC-98 MIDI only after OPN/OPNA/86/beep coverage is mature.

