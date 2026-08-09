# Hootplay configuration-file stage — 2026-08-07

## Goal

Move normal `hootplay` runtime configuration out of repeated command-line and
environment-variable invocations into one persistent INI file, without breaking
existing scripts or the X68000 OPM/PCM8/MIDI playback baseline.

## User-facing result

`hootplay` now searches for a configuration file in this order:

1. `--config /path/to/hootplay.ini`
2. `HOOTPLAY_CONFIG`
3. `./hootplay.ini`
4. `hootplay.ini` next to the executable

The repository contains a ready-to-edit `hootplay.ini`. If `entry` is specified
there, `hootplay` can be launched with no positional command-line argument.

Existing CLI options are retained as one-shot overrides. This deliberately keeps
old scripts working while making the persistent config the normal interface.

## Config sections

### `[player]`

Supports all existing `hootplay` CLI runtime settings:

- `catalog`
- `packs`
- `entry` / `archive`
- `sample_rate` / `rate`
- `track`
- `list`
- `mute_percussion`
- `channels`
- `wav` / `wav_path`
- `seconds` / `wav_seconds`

### `[midi]`

Friendly keys map to the existing MIDI/SC-55 backend controls:

- `backend` -> `HOOT_X68K_MIDI_BACKEND`
- `soundfont` -> `HOOT_X68K_SOUNDFONT`
- `nuked_sc55_clap` -> `HOOT_X68K_NUKED_SC55_CLAP`
- `soundcanvas_rom_path` -> `SOUNDCANVAS_ROM_PATH`
- `sc55_model` -> `HOOT_X68K_SC55_MODEL`
- `gain` -> `HOOT_X68K_MIDI_GAIN`
- `enabled` -> `HOOT_X68K_MIDI`
- `clap_path` -> `CLAP_PATH`

### `[x68k]`

Friendly keys map to the X68000 tuning controls:

- `x_load_mode`
- `cpu_clock`
- `ym2151_clock`
- `ym2151_core`
- `pcm8`
- `mfp_core`
- `mfp_bootstrap`
- `mfp_ignore_overrides`
- `startup`
- `opm_gain`
- `adpcm_gain`
- `pcm8_gain`
- `total_gain`
- `trace`
- `trace_limit`

### `[environment]`

Arbitrary advanced/legacy environment switches can be passed through directly.
This prevents the INI schema from needing a new key for every experimental
runtime switch.

## Parsing and path behavior

- INI sections and friendly key names are case-insensitive.
- `#` and `;` start full-line comments.
- Single- or double-quoted values are accepted.
- Boolean values accept `true/false`, `yes/no`, `on/off`, and `1/0`.
- Invalid typed values and unknown friendly keys are errors, avoiding silent
  misspellings.
- Relative catalog, pack, WAV, SoundFont, Nuked plugin, Sound Canvas ROM and
  trace paths are resolved relative to the INI file, not the process CWD.
- Omitted driver/backend keys leave existing environment behavior untouched.

## Precedence

For normal player options:

`CLI override > INI value > built-in default`

For named MIDI/X68000 backend settings, an explicit INI value is applied before
`HootContext` construction and therefore replaces a same-named inherited
environment setting. Omitted INI keys do not touch the environment.

## Implementation

New files:

- `src/config/hootplay_config.h`
- `src/config/hootplay_config.cpp`
- `tests/hootplay_config_test.cpp`
- `hootplay.ini`
- `md/HOOTPLAY_CONFIG.md`

`tools/hootplay/main.cpp` now performs config discovery and loading before the
legacy CLI parser, then applies CLI overrides. Track-range validation was also
moved before the keyboard thread is created, avoiding an early-return thread
lifetime hazard for an invalid configured track.

## Validation

### Unit tests

6/6 pass in Release:

- driver registry
- X68000 memory
- PCM8 mixer
- MIDI transport
- Nuked-SC55 CLAP adapter
- new hootplay INI parser

The config test covers relative paths, player settings, boolean conversion,
MIDI/SC-55 aliases, X68000 aliases, raw environment passthrough and rejection of
unknown named settings.

### Config-only real playback

A temporary `hootplay.ini` selected:

- `asuka68snd-generic`
- external pack directory
- 32 kHz output
- track 2
- one-second WAV capture

The player was invoked with **zero command-line arguments**. It automatically
found the CWD config and wrote a valid stereo WAV containing exactly 32,000
frames at 32 kHz.

### X68000 regression gates

- Plain OPM: 196/196 catalog commands pass (191 audio, 5 controls, 0 clipping).
- PCM8: 50/50 real tracks pass; aggregate counters match the previous MIDI/SC55
  baseline exactly.
- MIDI GM/GS: 30/30 Asuka tracks pass; MIDI counters match the previous baseline
  exactly (91,012 transmitted bytes, 1,797 note-ons, 868 SysEx, 0 malformed).

### Sanitizers

The new config parser and config-only `hootplay` playback pass AddressSanitizer
and UndefinedBehaviorSanitizer.

## Compatibility boundary

This stage intentionally does not remove CLI options or the existing environment
API. Other tools (`hootprobe`, `hoot2wav`) can continue to use those interfaces.
The change is scoped to making `hootplay.ini` the convenient persistent runtime
configuration for the native player.
