# Hoot Headless Port

This tree is the start of a modern, embeddable Hoot replay core. The first
milestone intentionally avoids the old MFC GUI and real-time Windows audio
output. It builds:

- `hootcore`: a small static library with a pull-based render API.
- `hoot2wav`: a CLI that reads a local catalog and writes a valid WAV file.
- `hootplay`: a small native player CLI with keyboard controls.

Xak II PC-98 now plays through a headless Microcabin driver, including archive
validation, RAM/code placement, BGM slot loading, voice asset loading,
track-code selection, KMZ80 execution, timer-paced IRQs, and libvgm YM2203 OPN
and SSG/PSG register handling. Verbose mode reports CPU, I/O, and OPN counters
for playback debugging.

## Build

```sh
cmake -S . -B build
cmake --build build
```

## Examples

```sh
./build/hoot2wav --catalog hootsrc20011006/hoot.xml --list
./build/hoot2wav --catalog tests/fixtures/cabin98xml.txt --packs packs --entry xak2-98-opn --track 41 --seconds 20 --out /tmp/xak2-fight-smoke.wav --verbose
```

Local music packs belong in `packs/` or `local-packs/` and should not be
committed unless their license explicitly allows redistribution.

## Native Player

`hootplay` is a small macOS-capable player. By default it looks for `hoot.xml`
and the referenced archive zip in the current directory. Pass the archive name
from the catalog, not the zip filename extension:

```sh
./build/hootplay fz68snd
```

If the catalog or packs live elsewhere:

```sh
./build/hootplay \
  --catalog hootsrc20011006/hoot.xml \
  --packs packs \
  fz68snd
```

It starts at the first track. Controls while playing: Space pauses/resumes,
`N` goes to the next track, `P` goes to the previous track, and `Q` quits.
Supported entries can be listed with:

```sh
./build/hootplay --catalog hootsrc20011006/hoot.xml --list
```

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

## mmd98 / PC-98 DOS Status

The `packs/mmd98/xml.txt` catalog is recognized for Microcabin
`pc98dos/opn` entries. The port now validates the archive layout and decodes
track codes into the referenced voice and BGM files. For example, Fray track 7
resolves to `FRAY.BIN` plus `F_THEME.BGM`.

Actual playback is not implemented yet for this path. These entries use a
PC-98 DOS helper (`mmd2.com`/`mmd3.com`) and `MMD.SYS`/`MMD2.SYS`; Hoot drives
them through DOS interrupt and INT D2 calls. That needs a PC-98 DOS/V30 host or
a native implementation of the MMD driver calls before it can generate YM2203
writes.

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

The default ADPCM mix gain is `0.40`. To tune it for a render, set
`HOOT_X68K_ADPCM_GAIN`, for example:

```sh
HOOT_X68K_ADPCM_GAIN=0.55 ./build/hoot2wav \
  --catalog hootsrc20011006/hoot.xml \
  --packs packs \
  --entry ys68snd-generic \
  --track 12 \
  --seconds 120 \
  --out ys68-final-battle.wav
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
