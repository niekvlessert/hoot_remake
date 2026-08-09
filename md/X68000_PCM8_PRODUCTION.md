# X68000 PCM8 production integration

## Scope

This stage completes the requested catalogue matrix and production integration
for the implemented direct-block PCM8 path. PCM8 array-chain playback remains
out of scope; GM/GS MIDI synthesis was added in the subsequent MIDI stage.

## Automatic startup

`x68k/generic` now defaults to an automatic startup policy:

1. Start in the native Human68k/MFP environment.
2. Post the first mailbox cue and retain a 50 ms audio probe.
3. If the cue is not consumed, or no YM key-on/PCM8 voice starts, reset the
   machine and retry once with the original Hoot direct-IRQ environment.
4. Return the retained probe samples before newly rendered samples, so the
   detection window does not cut the opening attack.

Asuka 120% resolves to native startup. Mad Stalker resolves to the Hoot-compatible
IRQ path. The old per-entry `hoot_startup=1` catalogue option was removed by
rebuilding the catalogue from the original XML plus normal overrides. Parsing of
the option remains only for compatibility with third-party/custom catalogues.

`HOOT_X68K_STARTUP=auto|native|hoot` remains available for diagnostics.

## Catalogue matrix

The catalogue contains 17 PCM8 configurations across 15 archives. The matrix
script enumerates the JSON catalogue, so configurations cannot silently fall
out of the gate.

Locally available during this stage:

- Asuka 120% OPM: 16 tracks, all audible, native startup.
- Asuka 120% GS: 15 tracks, CPU/driver healthy, external MIDI backend required.
- Asuka 120% TG-100: 15 tracks, CPU/driver healthy, external MIDI backend required.
- Mad Stalker: 34 tracks, all audible, automatic Hoot-compatible startup.

The remaining 13 configurations are represented in the report as missing
archives. They were not claimed as runtime validated.

## Validated direct PCM8 feature set

- Eight voices.
- X68000 ADPCM.
- Signed 8-bit PCM.
- Big-endian signed 16-bit PCM.
- Documented frequency, volume and pan modes.
- Stop, pause/resume, mode and status commands.
- Deterministic resampling and separate 32-bit accumulation.
- Safe sample-memory reads.
- Automatic cleanup on reset and track changes.

Direct-block PCM8 is considered production-validated for the supplied packs.
The overall X68000 generic driver remains experimental where broader MFP timing,
MIDI synthesis or array-chain PCM8 may be required.

## Remaining limitations

- PCM8 array-chain and linked-array-chain commands are decoded but not rendered.
- Channels above 7 are rejected and reported.
- GM/GS-compatible catalogue types now have an optional FluidSynth backend;
  MT-32/CM-64 and other module classes still need dedicated synthesis.
- Thirteen PCM8 configurations still require their archives for runtime tests.
- Reference-WAV quality comparison was not part of this requested stage.

## Gates

```sh
cmake --build build
ctest --test-dir build --output-on-failure
./tools/run_x68k_pcm8_catalog_matrix.py --packs packs
./tools/run_x68k_pcm8_realpack_validation.py --packs packs
./tools/run_x68k_pack_smoke.py --packs packs
```

## Regression and sanitizer notes

The generic non-PCM8 mailbox path retains the historical fixed 100,000-cycle
window. Only the first non-MIDI PCM8 cue uses poll-to-ack and the retained audio
probe. This keeps all 196 plain-OPM command results and reported audio/driver
metrics exactly equal to the previous baseline.

Sanitizer execution exposed signed-left-shift undefined behavior in the embedded
Musashi bit-test implementation and the libvgm YM2151 feedback/phase arithmetic.
Those operations now shift unsigned representations and cast back, preserving
the intended two's-complement bit pattern. Unit tests and representative Asuka
and Mad Stalker tracks pass with AddressSanitizer and UndefinedBehaviorSanitizer.
