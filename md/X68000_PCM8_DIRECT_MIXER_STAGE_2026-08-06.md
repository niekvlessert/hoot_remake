# X68000 PCM8 direct mixer stage — 2026-08-06

## Status

The trace-first PCM8 command endpoint has been extended into a host-side, eight-voice direct-block audio renderer. The stable YM2151, standard ADPCM, MFP, IOCS and FLOAT2 paths remain unchanged when no PCM8 voice is active.

This stage is code-complete and synthetic-test complete for direct output. It is **not yet real-pack validated**, because the three target archives are not present in the supplied workspace.

## Implemented

### Audio formats and control

- Eight independent PCM8 voices.
- Original `$000x` and PCM8A `$10xx` direct-output calls.
- X68000 ADPCM at 3.9, 5.2, 7.8, 10.4, 15.6, 20.8 and 31.2 kHz.
- Signed big-endian 16-bit PCM at the documented rates.
- Signed 8-bit PCM at the documented rates.
- Legacy volume `0x00-0x0f`, centered at `0x08`, using 2 dB steps.
- PCM8A volume `0x40-0xa0`, centered at `0x80`, using 0.8 dB steps.
- Left, right and centered pan.
- Per-channel and global pause/resume/stop behavior.
- Host-side track-switch voice reset without inventing guest trap commands.

### Decoder and resampler

- Original-Hoot-compatible low-nibble-first X68000 ADPCM.
- Original 49-step ADPCM difference/index behavior.
- Deterministic Q32 fixed-point sample-rate conversion.
- Non-interpolating sample hold, matching the old Hoot PCM8 path.
- Chunk-independent rendering: one large render call and multiple smaller calls produce the same sample stream and state.

### Guest memory and mixing

- Side-effect-free 24-bit sample reads from packed main memory, work RAM and the wrapped high compatibility page.
- Device and unmapped I/O addresses are rejected instead of returning fabricated sample bytes.
- PCM8 is accumulated in a separate signed 32-bit stereo buffer.
- The PCM8 buffer is added after YM2151 and standard ADPCM and before `total_mix`.
- `pcm_mix` controls both standard ADPCM and PCM8 by default.
- `HOOT_X68K_PCM8_GAIN` permits an independent PCM8 override.
- A zero PCM8 gain mutes output without pausing sample position or queries.

### Diagnostics and gates

`hootprobe` now reports:

- rendered PCM8 voice frames;
- consumed guest sample bytes;
- completed voices;
- sample-memory faults;
- existing commands, starts, stops, mode changes, queries, unknown calls, chain calls and unsupported channels.

Added:

- `tools/run_x68k_pcm8_smoke.py` — direct audio gate for Asuka 120%, Mad Stalker and Princess Maker;
- `reports/x68k_pcm8_catalog_inventory.json` — 17 explicit PCM8 configurations across 15 archives;
- `reports/x68k_pcm8_direct_mixer_missing_packs.json` — deterministic skipped result for the absent target packs.

## Test coverage

The PCM8 unit test now executes in Release builds; CMake explicitly undefines `NDEBUG` for test targets so `assert()` checks are not compiled away.

The mixer test covers:

- legacy and PCM8A command decoding;
- mode, address and remaining-length queries;
- direct signed 8-bit PCM;
- direct signed big-endian 16-bit PCM;
- low-nibble-first ADPCM decode values;
- left/center pan;
- channel pause/resume;
- zero-gain advancement;
- invalid sample-memory reads;
- end-of-buffer completion;
- host-side track-switch stop;
- Q32 chunk-independence at 44.1 kHz;
- both legacy and PCM8A unity-volume points.

A standalone AddressSanitizer plus UndefinedBehaviorSanitizer build of the mixer test also passes.

## Regression results

- `driver_registry`: pass.
- `x68k_memory`: pass.
- `x68k_pcm8_mixer`: pass with Release assertions active.
- Plain OPM matrix: pass.
- 196 catalog tracks/commands checked: 191 audio-active and 5 controls.
- Zero changed selected audio metrics relative to the trace-stage report.
- Zero clipping, warnings, track failures or unsupported CPU opcodes.

Report: `reports/x68k_plain_opm_pcm8_direct_mixer_regression.json`.

## Deliberate limitations

- Array-chain and linked-array-chain output are recognized but not rendered.
- Channels above seven are diagnosed and rejected.
- No real PCM8 archive has yet confirmed command modes, sample addresses, balance or audible fidelity.
- The current gate cannot promote PCM8 entries to playable until real packs consume bytes without memory faults and produce expected audio.
- This stage does not claim sample-exact parity with original Hoot.

## Required real-pack validation

Place these archives in `packs/`:

- `asuka68snd.zip`
- `madstk68snd.zip`
- `pm68snd.zip`

Then run:

```sh
python3 tools/run_x68k_pcm8_smoke.py \
  --packs packs \
  --output reports/x68k_pcm8_direct_smoke.json
```

The gate fails on unknown calls, chains, channels above seven, memory faults, absent rendered bytes, absent PCM8 frames or clipping. Its output determines whether the next phase is chain-table support or format/balance correction.

## Next work

1. Validate the three target archives and inspect actual command/format distributions.
2. Fix any direct-block ABI discrepancy exposed by those traces.
3. Implement array-chain and linked-array-chain playback if observed.
4. Compare representative WAVs with original Hoot for pitch, rate, pan, volume and ADPCM fidelity.
5. Only then broaden the gate to the remaining PCM8 catalog entries.
