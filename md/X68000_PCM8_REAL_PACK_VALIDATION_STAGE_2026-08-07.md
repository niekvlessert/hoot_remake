# X68000 PCM8 Real-Pack Validation Stage — 2026-08-07

## Scope

This stage validates the direct-block PCM8 implementation against the two real
Hoot archives supplied for the task:

- `asuka68snd.zip` — Asuka 120% BURNING Fest. OPM entry;
- `madstk68snd.zip` — Mad Stalker -FULL METAL FORCE-.

The archives are test inputs only and are not included in the source package.
The previous direct-mixer stage is the baseline. Plain OPM playback, the
existing IOCS/OKIM6258 path and the current MFP implementation must remain
unchanged.

## Real compatibility faults found

The initial mixer was not reached because the two ZMSC bootstraps exposed
several missing host details:

1. ZMSC used Human68k line-F calls `_VERNUM` (`FF30`), `_BUS_ERR` (`FFF7`) and
   `_FPUTS` (`FF1E`) during startup. The headless host now supplies the required
   version result, safe bus-probe/copy behavior and successful discarded text
   output.
2. The old Hoot direct-startup path reset the 68000 but did not run a pre-play
   CPU slice. Mad Stalker's resident samples the play mailbox during one-time
   startup, so the extra pre-run left it idle. The pre-run is now skipped only
   for `hoot_startup=1`; native MFP entries retain it.
3. Direct Hoot YM2151 IRQ6 delivery must return X68000 OPM vector `$43` after
   IOCS `_OPMINTST` installs a handler. Returning the level-6 autovector
   acknowledged the timer but never entered ZMSC's music handler.
4. Real Hoot PCM8 trap #2 stubs write host callback marker `$22`, not `$02`.
   `$22` is now canonical while `$02` remains accepted for old synthetic tests.
5. ZMSC brackets playback with PCM8 resident lifecycle functions `$01fe` and
   `$01ff`. Both are implemented as successful no-ops because the embedded
   resident cannot be unloaded by a separate guest process.
6. `madstk68snd-generic` now carries `hoot_startup=1` in the canonical JSON
   catalog and rebuilt compressed SQLite catalog. No environment override is
   required.

## Mixer behavior exercised

Both music drivers issued legacy direct channel commands. Representative
traces used volume 8, 15.6 kHz ADPCM and centered pan. The renderer consumed
samples directly from guest memory, advanced every voice with deterministic
Q32 phase accumulation, mixed into the existing signed 32-bit stereo
accumulator and left clipping to the common final output stage.

The Mad Stalker catalog also contains 22 sound-effect entries (`SE 0x65` through
`SE 0x7a`). Those tracks are audio-active through the existing IOCS
`_ADPCMOUT`/OKIM6258 path and correctly need no direct PCM8 start.

## Real-pack results

Every track was launched through a fresh `hootprobe` process, preventing state
from a previous track from masking startup, timer or resident defects.

| Entry | Tracks | Audio-active | PCM8 commands | Direct starts | Voice frames | Source bytes | Memory faults |
|---|---:|---:|---:|---:|---:|---:|---:|
| Asuka 120% OPM | 16 | 16 | 371 | 323 | 2,906,109 | 513,816 | 0 |
| Mad Stalker | 34 | 34 | 413 | 170 | 1,399,860 | 247,513 | 0 |
| **Total** | **50** | **50** | **784** | **493** | **4,305,969** | **761,329** | **0** |

Additional aggregate results:

- 175 direct stop operations;
- 248 naturally completed PCM8 voices;
- zero array-chain or linked-array-chain calls;
- zero unknown functions;
- zero unsupported channels above seven;
- zero clipped output samples;
- zero CPU errors and zero driver warnings.

Primary report:
`reports/x68k_pcm8_realpack_validation_2026-08-07.json`.

A bounded representative command-trace inventory is in
`reports/x68k_pcm8_realpack_command_inventory_2026-08-07.json`.

## Regression and safety checks

- `driver_registry`: pass;
- `x68k_memory`: pass;
- `x68k_pcm8_mixer`: pass with Release assertions enabled;
- standalone AddressSanitizer + UndefinedBehaviorSanitizer mixer test: pass;
- six-entry plain OPM matrix: pass;
- 196 catalog commands: 191 audio-active and five controls;
- zero changes in result, rendered-frame count, first-audible frame, peak, RMS,
  nonzero sample count, clipping, CPU counters, I/O counters, chip writes,
  key-ons, final YM register/data or warnings compared with the direct-mixer
  baseline.

Regression report:
`reports/x68k_plain_opm_pcm8_realpack_regression_2026-08-07.json`.

## Evidence boundary and remaining work

This stage validates direct PCM8 playback for two ZMSC-based archives. It does
not claim sample-exact parity with original Hoot and it does not establish that
all 15 PCM8 archives use the same subset.

Still open:

1. Validate `pm68snd.zip`, which was not supplied.
2. Broaden testing to non-ZMSC PCM8 users such as MCDRV and OPMDRV2 when packs
   become available.
3. Implement array-chain and linked-array-chain playback only when a real pack
   demonstrates the descriptor ABI and requires it.
4. Compare representative WAV output against original Hoot for ADPCM decoder
   parity, pitch, pan and level.
5. MIDI/MT-32/GM synthesis remains intentionally outside this player path.
