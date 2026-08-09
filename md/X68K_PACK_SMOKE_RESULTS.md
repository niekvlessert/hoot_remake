# X68000 provided-pack smoke results

## Scope and evidence level

This report covers the six user-provided X68000 archives available during the
2026-08-06 compatibility pass. It is a runtime smoke test, not reference-audio
verification. It proves successful asset loading, 68000 execution, YM2151/ADPCM
activity, track selection, rendering, and absence of diagnosed crashes,
unsupported opcodes, real silent music tracks, and clipping during the sampled
window. It does not yet prove exact pitch, tempo, envelopes, instruments,
channel balance, loops, or track lengths.

The packs themselves are copyrighted and are not included in the project.
Canonical archive names are required by the catalog. The uploaded
`ad68snd_.zip` and `xak68snd(1).zip` were tested as `ad68snd.zip` and
`xak68snd.zip`.

## Command

```sh
./build/hootprobe \
  --catalog catalog/hoot.sqlite.zst \
  --packs packs \
  --catalog-driver x68k/generic \
  --all-tracks \
  --seconds 3 \
  --startup-grace 3 \
  --timeout 20 \
  --output x68k-provided-packs-final.json
```

For a repeatable pass/fail gate, use:

```sh
./tools/run_x68k_pack_smoke.py --packs packs
```

## Plain OPM results

| Catalog entry | Archive | Tracks | Audible | STOP/FADE | Real silent | Clipped samples | Result |
|---|---|---:|---:|---:|---:|---:|---|
| `fz68snd-generic` | `fz68snd.zip` | 34 | 32 | 2 | 0 | 0 | pass |
| `ys68snd-generic` | `ys68snd.zip` | 43 | 42 | 1 | 0 | 0 | pass |
| `paro68snd-generic` | `paro68snd.zip` | 36 | 36 | 0 | 0 | 0 | pass |
| `ad68snd-generic` | `ad68snd.zip` | 22 | 22 | 0 | 0 | 0 | pass |
| `xak68snd-generic` | `xak68snd.zip` | 48 | 47 | 1 | 0 | 0 | pass |
| `gra68snd-generic` | `gra68snd.zip` | 13 | 12 | 1 | 0 | 0 | pass |
| **Total** |  | **196** | **191** | **5** | **0** | **0** | **pass** |

Fantasy Zone has both a STOP command and a FADE OUT command. In a sequential
scan, STOP can contain a short release tail from the preceding track; this is
reported as `control-tail`, not as a music track. A fresh selection of that
STOP command is silent.

Xak tracks 34 and 39 initially looked silent with a two-second probe. Their
first audible samples occur at approximately 2.011 and 2.314 seconds. The new
startup-grace measurement classifies them correctly and records the exact
`first_audible_frame`.

## Mixer correction

Original Hoot mixer values use `0x100` as unity. The generic X68000 catalog
commonly specifies `opm_mix=0xc0`, which is 75%, not 100%. The port previously
normalized `0xc0` to unity. Correcting the YM2151 gain to `value / 256.0`
removed the Xak clipping observed in the smoke window:

- before: 2,294 clipped samples, peak at full scale;
- after: 0 clipped samples, maximum observed peak 25,971.

The default generic X68000 OPM gain is therefore now 0.75, matching Hoot's
mixer semantics. Runtime environment overrides remain available.

## MIDI variants

The archives also expose seven experimental MIDI variants:

- Parodius OPM+MT-32 variants: 12 OPM-active tracks and 24 MIDI-only silent
  tracks per entry;
- After Dark MT-32, GS, GM, and Vermouth variants: all tracks silent.

This is expected. MIDI command output exists, but the headless player has no
MT-32/GM/GS/Vermouth synthesizer backend. These entries remain
`experimental`; they are not counted as plain-OPM failures.

## Next validation step

The supplied OPM and MFP/FLOAT2 matrices are now covered. The next validation
step is trusted original-Hoot WAV comparison for pitch, tempo, instruments,
balance and loop timing, followed by PCM8 and rendered MIDI support.

The plain-OPM gate is deterministic and scans only the six proven generic
entries, excluding MIDI variants:

```sh
./tools/run_x68k_pack_smoke.py --packs packs \
  --output x68k-pack-smoke.json
```

Trusted original-Hoot WAV captures for one representative track from each
family are needed before promoting any entry from `playable` to `verified`.


## MFP, IOCS and FLOAT2 results

A later pass added four more supplied archives and uses a separate gate:

```sh
./tools/run_x68k_mfp_smoke.py --packs packs
```

| Catalog entry | Archive | Tracks | Audio-active | Clipped samples | Result |
|---|---|---:|---:|---:|---|
| `shooting68snd-generic` | `shooting68snd.zip` | 9 | 9 | 0 | pass |
| `ngear68snd-generic` | `ngear68snd.zip` | 17 | 17 | 2 | pass |
| `nvgml68snd-generic` | `nvgml68snd.zip` | 443 | 443 | 0 | pass |
| `a268snd-generic` | `a268snd.zip` | 14 | 14 | 0 | pass |
| **Total** |  | **483** | **483** | **2** | **pass** |

The two clipped Neural Gear samples are within the existing eight-sample smoke
allowance and were previously measured as four samples over a ten-second run of
one track. No global gain reduction was applied without trusted reference audio.

A-Train II required a generic memory-model correction. Its compiler-generated
A2.X code keeps `A6` at zero and accesses local scratch values at negative
frame offsets. The 24-bit address bus wraps these to `0xfffff8` and
`0xfffffc`. Those writes were previously discarded, so the initialization loop
never completed. The final 64 KiB is now retained as writable compatibility
workspace. This also lets the existing FLOAT2.X line-F dispatcher and IOCS
vector setup finish without an A-Train-specific branch or address bypass.

These MFP/FLOAT2 entries remain classified as `experimental`, because the
smoke test does not establish sample-exact parity with original Hoot.
