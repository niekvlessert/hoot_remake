# Hoot Headless Port

This tree is the start of a modern, embeddable Hoot replay core. The first
milestone intentionally avoids the old MFC GUI and real-time Windows audio
output. It builds:

- `hootcore`: a small static library with a pull-based render API.
- `hoot2wav`: a CLI that reads a local catalog and writes a valid WAV file.
- `hootplay`: a small native player CLI with keyboard controls.
- `hoot_catalog`: the XML-to-JSON-to-SQLite catalogue build pipeline.
- `hootprobe`: an isolated batch compatibility scanner with capability, asset, CPU and audio diagnostics.

The preferred editable catalogue is `catalog-src/hoot.catalog.json`; the
preferred runtime file is `catalog/hoot.sqlite.zst`, compressed with Zstandard
level 19. Legacy XML remains supported. See [CATALOG_FORMAT.md](CATALOG_FORMAT.md).
Driver support and compatibility scanning are documented in [COMPATIBILITY_PROBE.md](COMPATIBILITY_PROBE.md).

Xak II PC-98 now plays through a headless Microcabin driver, including archive
validation, RAM/code placement, BGM slot loading, voice asset loading,
track-code selection, KMZ80 execution, timer-paced IRQs, and libvgm YM2203 OPN
and SSG/PSG register handling. Verbose mode reports CPU, I/O, and OPN counters
for playback debugging.

## Build

```sh
cmake -S . -B build
cmake --build build
cmake --build build --target hoot_catalog
```

## Examples

```sh
./build/hoot2wav --catalog catalog/hoot.sqlite.zst --list
./build/hoot2wav --catalog tests/fixtures/cabin98xml.txt --packs packs --entry xak2-98-opn --track 41 --seconds 20 --out /tmp/xak2-fight-smoke.wav --verbose
./build/hootprobe --catalog catalog/hoot.sqlite.zst --packs packs --seconds 5 --output compatibility.json
./build/hootprobe --catalog catalog/hoot.sqlite.zst --packs packs --catalog-driver x68k/generic --seconds 5 --output x68k-compatibility.json
```

Local music packs belong in `packs/` or `local-packs/` and should not be
committed unless their license explicitly allows redistribution.

## Native Player

`hootplay` is a small macOS-capable player. Runtime settings now live in
`hootplay.ini`; the repository contains a documented default configuration.
When launched from the repository root, a normal invocation is simply:

```sh
./build/hootplay fz68snd
```

The config can contain the catalog, pack directory, sample rate, starting track,
channel/percussion settings, WAV capture settings, MIDI/FluidSynth/Nuked-SC55
paths and the X68000 runtime tuning switches. It can also contain `entry`, in
which case no positional argument is required:

```sh
./build/hootplay
```

Use another config with `--config /path/to/hootplay.ini` or
`HOOTPLAY_CONFIG=/path/to/hootplay.ini`. Existing options such as `--catalog`,
`--packs`, `--rate` and `--track` remain supported as one-run overrides. See
`md/HOOTPLAY_CONFIG.md` for the complete key list and precedence rules.

It starts at the configured track. Controls while playing: Space pauses/resumes,
`N` goes to the next track, `P` goes to the previous track, and `Q` quits.
Supported entries can be listed by setting `list = true` or with the legacy
`--list` override.

## Generate a Full Xak II Track WAV

Put the Xak II PC-98 music pack in `packs/`, then render a track with
`hoot2wav`. Full track end or loop detection is not implemented yet, so choose a
long enough `--seconds` value for the song you want to capture.

For example, render `FIGHT!.BGM` to a three-minute WAV:

```sh
./build/hoot2wav \
  --catalog tests/fixtures/cabin98xml.txt \
  --packs packs \
  --entry xak2-98-opn \
  --track 41 \
  --seconds 180 \
  --out xak2-fight-full.wav \
  --verbose
```

Useful Xak II track numbers:

- `3`: `ATOWN.BGM`
- `20`: `BOSS_0.BGM`
- `41`: `FIGHT!.BGM`

To see the available track numbers and titles from the Hoot catalog, run:

```sh
./build/hoot2wav --catalog tests/fixtures/cabin98xml.txt --list
```

The number in brackets is the value to pass to `--track`. For example,
`[41] FIGHT!.BGM : ...` means render it with `--track 41`.

The friendly titles come from the catalog `titlelist`, not from the raw game
disk by itself. The original catalog text is CP932/Shift-JIS era Japanese, so
some terminals may show mojibake. To view the title catalog as UTF-8:

```sh
iconv -f CP932 -t UTF-8 tests/fixtures/cabin98xml.txt
```

The default PSG gain is `0.90`. To tune it for a render, set `HOOT_PSG_GAIN`,
for example:

```sh
HOOT_PSG_GAIN=0.75 ./build/hoot2wav \
  --catalog tests/fixtures/cabin98xml.txt \
  --packs packs \
  --entry xak2-98-opn \
  --track 20 \
  --seconds 180 \
  --out xak2-boss0-full.wav
```

To render FM-only audio for comparison, set `HOOT_DISABLE_PSG=1`.

## PC-88 / PC-98 DOS Status

The headless replay core now has shared generic hosts for PC-88 OPN/OPNA and
PC-98 DOS/V30 OPN/OPNA.  The PC-98 host provides DOS file services, COM/MZ-EXE
loading, resident shell support, YM Timer A/B scheduling, the PC-98 BIOS sound
hook, the legacy INT 0Bh resident path, and YM2203/YM2608 rendering.  The real
pack validation set (Arcus, XTALSOFT, Ys, Slayers, Tarashi2 and Neko Manma EX)
covers 438 catalog tracks: 436 render audio, one catalog entry is an intentional
STOP control, and one NekoEX OPN track is incompatible with the PMD 4.0b binary
shipped in that pack while its OPNA/PMDB2 counterpart works.

`pc98dos/86` is now routed through the same DOS/V30 + YM2608 host with a separate
PC-9801-86 linear-PCM device.  The board interface at A460h-A46Ch implements the
sound-ID/extended-FM latch, PCM volume, FIFO control/status, low-water threshold,
all eight rate selectors, signed 8/16-bit mono/stereo DAC modes, FIFO streaming
and low-water IRQ delivery.  Hoot's `pcm_mix` option controls the separate PCM86
mix gain.  A synthetic resident driver test writes 4096 bytes through A46Ch and
verifies that PCM86 audio, FIFO consumption and diagnostics all advance.

The current catalog contains 162 `pc98dos/86` configurations / 6678 tracks.  156
fit the generic DOS+OPNA+PCM86 model without another declared machine feature.
Six entries request `extramsize` and remain explicitly deferred until the
EMS/extra-memory sample-storage behavior is validated with real packs.  Real
PC-9801-86 music archives were not available during this implementation stage,
so PCM86 is hardware/API validated but not yet claimed as broad real-pack
validated.  Use `tools/run_pcx_catalog_matrix.py` for the current coverage
inventory.

## Ys X68000 / ys68snd Status

`ys68snd.zip` is an X68000 `x68k/generic` Hoot entry. The port now recognizes
that driver path and validates/loading the `ys68.bin` code asset instead of
silently rendering an empty WAV.

The X68000 host now uses the standalone Musashi 68000 core from
`third_party/px68k-libretro`, with `ys68.bin` mapped at `0x000000`, RAM at
`0xf00000`, the Hoot play mailbox at `0xe00000`, and YM2151 access at
`0xe90001/0xe90003`. MAME confirms the X68000 hardware clocks: 10 MHz 68000,
4 MHz YM2151, and 4 MHz OKIM6258.

The play-start/timer interrupt handshake is implemented: the driver now returns
YM2151 status reads, routes YM2151 timer IRQs to 68000 IRQ6, and acknowledges
them with an autovector. Track 1 (`01 Feena`) reaches real YM2151 key-on writes
and renders audible FM:

```sh
./build/hoot2wav \
  --catalog hootsrc20011006/hoot.xml \
  --packs packs \
  --entry ys68snd-generic \
  --track 1 \
  --seconds 10 \
  --out /tmp/ys68-feena.wav \
  --verbose
```

The X68000 ADPCM/OKIM6258 path is implemented for the Hoot-style memory sample
playback used by Ys. The driver mirrors the original Hoot behavior: DMA setup
writes capture the sample address from 68000 `A1` and size from `D2`, status
`0x88` starts playback, and the lower PPI bits control pan/rate. The sample
decoder follows Hoot's memory ADPCM behavior: low nibble first, no
interpolation, and the same short noise-reduction release tail. This is enough
for Ys percussion and sound effects, but it is not a complete X68000 DMA
device yet.

The X68000 loader now keeps a packed-data image from `0x000000` through
`0xe7ffff`. Device and mailbox addresses inside that range retain priority.
This removes the previous 2 MiB loader ceiling; the current catalogue contains
11 generic X68000 entries with assets above that ceiling, reaching `0xe78000`.
A regression test covers ordinary packed data, the high boundary, the mailbox,
I/O scratch fallback and work RAM.

Catalogue mix controls are applied relative to Hoot's historical defaults:
`opm_mix`, `pcm_mix`, and `total_mix`. Runtime overrides are available through
`HOOT_X68K_OPM_GAIN`, `HOOT_X68K_ADPCM_GAIN`, `HOOT_X68K_PCM8_GAIN`, and `HOOT_X68K_TOTAL_GAIN`.
The default ADPCM mix gain remains `0.40`. For example:

```sh
HOOT_X68K_ADPCM_GAIN=0.55 ./build/hoot2wav \
  --catalog hootsrc20011006/hoot.xml \
  --packs packs \
  --entry ys68snd-generic \
  --track 12 \
  --seconds 120 \
  --out ys68-final-battle.wav
```

Both the signed-16 and float render APIs now use the same X68000 replay and
mixing path; float clients no longer receive a silent placeholder stream.

### X68000 PCM8 real-pack status

The generic host now accepts the real Hoot trap-#2 PCM8 callback marker `$22`
(`$02` remains a synthetic-fixture compatibility alias), implements ZMSC's
`$01fe/$01ff` resident lifecycle calls, and mixes eight direct PCM8 voices.
Direct blocks support low-nibble-first ADPCM, signed big-endian 16-bit PCM and
signed 8-bit PCM with fixed-point resampling.

The supplied `asuka68snd.zip` and `madstk68snd.zip` were tested with a fresh
process for every catalog track. All 50 tracks are audio-active. Across those
tracks the current 3-second production gate observes 786 PCM8 commands, 494
direct starts, 4,315,540 rendered PCM8 voice frames and 763,022 consumed guest
sample bytes, with no unknown or unimplemented calls, channels above seven,
memory faults or clipped samples. Startup selection is automatic: Asuka stays
on the native MFP path while Mad Stalker falls back to Hoot's direct IRQ6 path
and OPM vector `$43`; no per-entry startup override is required.

Run the same gate when the copyrighted packs are locally available:

```sh
./tools/run_x68k_pcm8_realpack_validation.py \
  --packs packs \
  --output reports/x68k_pcm8_realpack_validation.json
```

Array-chain and linked-array-chain PCM8 commands remain recognized but are not
rendered; neither supplied pack issued them. See
`md/X68000_PCM8_REAL_PACK_VALIDATION_STAGE_2026-08-07.md` for the exact evidence
boundary and remaining work.

### X68000 MIDI GM/GS status

The generic host now emulates the CZ-6BM1 transmit FIFO/timers instead of only
counting writes. MIDI bytes leave the 256-byte hardware boundary at the real
31.25 kbit/s wire rate, with persistent cycle accounting, and are decoded into
running-status channel messages, system messages and SysEx. `hootprobe` reports
transport and synth counters per track.

For catalogue `midiout_type=4` (GS / SC-55), the default `auto` backend now
tries an externally installed Nuked-SC55 CLAP plug-in first. The plug-in and
original Roland ROM dumps are deliberately not distributed with Hoot. Point to
the plug-in with `HOOT_X68K_NUKED_SC55_CLAP=/path/Nuked-SC55.clap`; Nuked can
find ROMs through `SOUNDCANVAS_ROM_PATH`. `HOOT_X68K_SC55_MODEL` selects
`v1.00`, `v1.10`, `v1.20`, `v1.21` (default), `v2.00` or `mk2`.

If Nuked or its ROM set is unavailable, SC-55 automatically falls back to the
dynamically loaded FluidSynth backend and a local GM/GS SoundFont. SC-88-class
`midiout_type=7` and GM `midiout_type=8` continue to use FluidSynth. Select an
SF2 with `HOOT_X68K_SOUNDFONT=/path/to/file.sf2`; `HOOT_X68K_MIDI_GAIN` adjusts
the common MIDI mix level. When an entry explicitly targets SC-88, Hoot emits
an English runtime warning that FluidSynth is compatibility rendering rather
than hardware-exact SC-88, points to `[midi] soundfont` in `hootplay.ini`, and
states that authentic output requires a real SC-88 or a dedicated SC-88-compatible
renderer. GM-only entries do not receive this warning.

MT-32 classes `midiout_type=1` and `2` use an externally installed
Munt/libmt32emu runtime. `midiout_type=3` now builds a composite CM-64: Munt
renders the CM-32L-compatible LA section and Hoot's built-in high-level CM-32P
renderer supplies the six PCM parts from user-provided IC18/IC19/IC20 dumps.
Configure `HOOT_MT32_ROM_PATH`, `HOOT_CM32L_ROM_PATH` and
`HOOT_CM32P_ROM_PATH`. Optional SN-U110 card ROMs can be configured generically
or as `HOOT_CM32P_CARD_ROM_07` / `_10`; named catalogue variants select those
automatically. If the CM-32P ROM set is absent, `auto` keeps the earlier
CM-32L/LA-only fallback. The same backends are shared by PC-98 and X68000.
`HOOT_X68K_MIDI_BACKEND=auto|cm64|cm32p|munt|nuked-sc55|fluidsynth|none` can
override backend selection. Hoot's emulated MIDI interfaces already model
31.25 kbit/s wire timing, so mt32emu is configured for immediate input delay.
The CM-32P renderer is compatibility-oriented rather than firmware/hardware
cycle-exact; Roland ROMs are never bundled.

With `asuka68snd.zip`, all 15 GS and all 15 TG-100/GM-catalogue tracks remain
audio-active in a fresh-process-per-track 3-second validation when the
FluidSynth fallback is used. The Nuked CLAP host path is covered by a dynamic
mock-plugin test; authentic SC-55 audio itself requires the user's external
Nuked installation and ROM set. Run the GM/GS regression with:

```sh
./tools/run_x68k_midi_gm_gs_validation.py \
  --packs packs \
  --soundfont /path/to/gm.sf2
```

Fantasy Zone (`fz68snd.zip`) also runs through the same `x68k/generic` path.
It uses the generic mailbox plus a wider `0xe00000-0xefffff` private
work/stack window, with the known Hoot device addresses still taking priority.
For example:

```sh
./build/hoot2wav \
  --catalog hootsrc20011006/hoot.xml \
  --packs packs \
  --entry fz68snd-generic \
  --track 0 \
  --seconds 120 \
  --out fz68-opaopa.wav
```

The compatibility memory model also retains the final 64 KiB of the
24-bit address space. Human68k/XC routines in A-Train II deliberately use
negative frame offsets with `A6=0`, wrapping locals to `0xfffff8` and
`0xfffffc`; discarding those writes prevented FLOAT2/A2 initialization.

### X68000 MFP/IOCS smoke gate

With the four local reference packs present, run all 483 MFP/FLOAT2 tracks:

```sh
./tools/run_x68k_mfp_smoke.py --packs packs
```

The companion plain-OPM gate scans the six fixed baseline entries without
including their MIDI variants:

```sh
./tools/run_x68k_pack_smoke.py --packs packs
```

Together the two gates cover 679 catalog commands: 674 audio-active tracks and
five STOP/FADE controls. See `md/X68K_MFP_IOCS_RESULTS.md` and
`md/X68K_PACK_SMOKE_RESULTS.md` for the exact evidence boundary.

### Selectable X68000 MFP backend

The X68000 generic driver has two MFP implementations for comparison. The
default `hoot` backend keeps the current post-OS bootstrap behavior. The
optional `mame` backend follows the BSD-licensed MAME MC68901 behavior for
prescalers, `IPR & IMR` interrupt routing, and YM2151's active-low GPIO3
connection, while using the same Hoot post-OS bootstrap so standalone entries
remain playable.

The original Hoot generic X68000 driver did not use an MFP. It mapped the
68000 ROM/RAM and sound devices, reset the CPU, and delivered YM2151 Timer A/B
events directly as IRQ6. Reproduce that startup path with:

```sh
HOOT_X68K_STARTUP=hoot ./build/hootplay ...
```

This explicitly bypasses MFP emulation and all MFP-specific overrides.
It is appropriate for older generic entries such as `fz68snd` and
`nama68snd`; newer entries such as Neural Gear use MFP Timer C and should use
the normal bootstrapped MFP path instead.

Use the environment variable for an A/B run:

```sh
HOOT_X68K_MFP_CORE=hoot ./build/hoot2wav \
  --catalog hootsrc20011006/hoot.xml --packs packs \
  --entry ngear68snd-generic --track 0 --seconds 2 --out /dev/null --verbose

HOOT_X68K_MFP_CORE=mame ./build/hoot2wav \
  --catalog hootsrc20011006/hoot.xml --packs packs \
  --entry ngear68snd-generic --track 0 --seconds 2 --out /dev/null --verbose
```

To compare without the MFP-specific entries from `hoot-overrides.xml`, add:

```sh
HOOT_X68K_MFP_CORE=mame HOOT_X68K_MFP_IGNORE_OVERRIDES=1 ./build/hoot2wav \
  --catalog hootsrc20011006/hoot.xml --packs packs \
  --entry ngear68snd-generic --track 0 --seconds 2 --out /dev/null --verbose
```

This ignores only `mfp_timer_divider`, `mfp_sound_timer`,
`mfp_initial_ierb`, and `mfp_initial_imrb`. Other catalog and asset overrides
remain active.

For the strict power-on experiment, bypass that bootstrap explicitly:

```sh
HOOT_X68K_MFP_CORE=mame HOOT_X68K_MFP_BOOTSTRAP=reset ./build/hoot2wav \
  --catalog hootsrc20011006/hoot.xml --packs packs \
  --entry ngear68snd-generic --track 0 --seconds 2 --out /dev/null --verbose
```

The strict reset mode may be silent because standalone Hoot entries expect the
original resident OS to initialize the MFP.

### Trace an X68000 MFP entry

Set `HOOT_X68K_TRACE` to write a trace while running a short entry. The trace
includes the initial MFP register state, all accesses in `0xe88000-0xe89fff`,
IRQ6 assertion/acknowledgement, delivered MFP vectors, and YM2151 writes:

```sh
HOOT_X68K_MFP_CORE=mame \
HOOT_X68K_TRACE=/tmp/ngear-mfp.trace \
HOOT_X68K_TRACE_LIMIT=10000 \
./build/hoot2wav \
  --catalog packs/hoot20251231/hoot.xml \
  --packs packs/czarek/hoot/x68k \
  --entry ngear68snd-generic --track 0 --seconds 1 \
  --out /dev/null --verbose

rg 'mfp-|irq6-|ym2151' /tmp/ngear-mfp.trace
```

The `pc=`, `cycles=`, `addr=`, and `data=` fields make it possible to line up
MFP timer expiry, IRQ6 acknowledge, vector delivery, and subsequent OPM
writes. `HOOT_X68K_TRACE_LIMIT=0` means no event limit.

## Packaging source dependencies

Use `tools/package_dependencies.sh` to create a clean archive of libkss,
libvgm and px68k-libretro, including libkss's recursively pinned submodules.
See `md/DEPENDENCY_PACKAGE.md`.
