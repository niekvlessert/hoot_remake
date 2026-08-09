# Driver capabilities and `hootprobe`

The port uses one central `DriverRegistry` for driver selection, capability
classification, player listing, and batch probing. Unsupported catalogue
entries are no longer accepted and rendered as silent audio.

## Capability levels

| Level | Meaning |
|---|---|
| `unsupported` | No replay host is registered for the catalogue driver. |
| `recognized` | A host recognizes the driver family and may attempt it, but this executable/variant has no compatibility claim. |
| `experimental` | A relevant execution path exists, with known incomplete or unverified hardware/OS behaviour. |
| `playable` | The required basic host path is implemented and is expected to produce audio, but has not been reference-audio verified for this entry. |
| `verified` | Reserved for entries covered by a reference-audio regression test. No entry is promoted here merely because it produced non-silent audio. |

The probe result always includes a replay-host ID and a reason. Examples include
missing MIDI synthesis, experimental X68000 MFP behaviour, incomplete PCM8, or
an unclassified PC-98 DOS shell executable.

C API:

```c
HootDriverProbe probe;
HootResult result = hoot_probe_entry(ctx, entry_id, &probe);
printf("%s: %s\n", hoot_support_status_name(probe.status), probe.reason);
```

`hoot_load_entry()` now returns `HOOT_ERROR_UNSUPPORTED` before pack loading when
no replay host exists.

## Batch compatibility scan

Build and run:

```sh
cmake -S . -B build
cmake --build build --target hootprobe

./build/hootprobe \
  --catalog catalog/hoot.sqlite.zst \
  --packs packs \
  --seconds 5 \
  --output compatibility.json
```

The default scan:

- considers every catalogue entry;
- reports only entries whose archive ZIP exists locally;
- validates all catalogue assets and known fallback rules;
- tests the first track for five seconds;
- records peak, RMS, non-zero and clipped samples;
- records chip writes, key-ons, CPU position and warnings;
- records PC-9801-86 FIFO/DAC counters and PC-98 PIT-speaker divider, gate,
  VRTC/INT 0Ah and rendered-audio diagnostics;
- detects explicit unsupported V30/x86 opcodes;
- isolates every entry in a child process on POSIX systems;
- reports process crashes and hard timeouts without aborting the full scan.

Useful variants:

```sh
# Test every track in each locally present pack. Startup grace prevents
# drivers with a legitimate initialization delay from being marked silent.
./build/hootprobe --catalog catalog/hoot.sqlite.zst --packs packs \
  --all-tracks --seconds 5 --startup-grace 3 --timeout 20 \
  --output compatibility-all.json

# Inspect one catalogue entry
./build/hootprobe --catalog catalog/hoot.sqlite.zst --packs packs \
  --entry fz68snd-generic --output fz68-probe.json

# Inspect one zero-based track and allow up to three seconds for startup
./build/hootprobe --catalog catalog/hoot.sqlite.zst --packs packs \
  --entry xak68snd-generic --track 39 --seconds 3 --startup-grace 3 \
  --output xak-track-39.json

# Inspect every entry using one archive
./build/hootprobe --catalog catalog/hoot.sqlite.zst --packs packs \
  --archive fz68snd --output fz68-variants.json

# Restrict the scan to the generic X68000 catalogue driver
./build/hootprobe --catalog catalog/hoot.sqlite.zst --packs packs \
  --catalog-driver x68k/generic --seconds 5 --output x68k-compatibility.json

# Include catalogue entries whose ZIP is absent
./build/hootprobe --catalog catalog/hoot.sqlite.zst --packs packs \
  --include-missing --output complete-inventory.json
```

`--timeout` is a per-track soft limit. The POSIX parent also applies a hard
process limit derived from the number of tracks selected. On Windows,
`hootprobe` currently runs entries in-process and therefore cannot recover from
a native crash; the JSON setting `process_isolation` records this distinction.

## Scan outcomes

Important entry outcomes are:

- `audio-active`: audio exceeded the configured silence peak and no diagnosed CPU error occurred;
- `partial-silent`: at least one non-control track was active and at least one stayed silent;
- `control-silent` and `control-tail` at track level: STOP/FADE commands that either produced silence or only a release tail;
- `control-only`: an entry contained only recognized control commands;
- `warning`: audio was active but the replay host reported a warning;
- `silent`: rendered audio stayed at or below `--silence-peak`;
- `cpu-error`: the V30/x86 core encountered unsupported opcodes;
- `track-error`, `render-error`, or `load-error`;
- `missing-assets`, `missing-archive`, or `archive-error`;
- `unsupported`;
- `crash` or `timeout` when process isolation catches a failure.

`first_audible_frame` records delayed starts. `--startup-grace` searches for the first audible sample before collecting the requested analysis duration, so legitimate OPMDRV initialization delays are not false silence.

Non-silent audio is only a smoke-test result. It does not establish correct
pitch, tempo, instruments, channel balance, loops, or track length. Promotion
to `verified` requires comparison with a trusted original Hoot render.

## Current catalogue classification

With the 2025-12-31 catalogue included in this tree, the registry currently
classifies 5,253 entries as:

| Capability | Entries |
|---|---:|
| `playable` | 311 |
| `experimental` | 613 |
| `recognized` | 1,474 |
| `unsupported` | 2,855 |

These counts describe implemented host routing, not successful pack playback.
A real `hootprobe` run against the user's local pack directory is the source of
truth for load and smoke-test coverage.


## X68000 MFP/IOCS regression gate

When `shooting68snd.zip`, `ngear68snd.zip`, `nvgml68snd.zip`, and
`a268snd.zip` are available locally, the dedicated gate scans all 483 tracks:

```sh
./tools/run_x68k_mfp_smoke.py --packs packs \
  --output x68k-mfp-iocs-smoke.json
```

The gate includes FLOAT2/line-F and the wrapped high-address workspace used by
A-Train II.
