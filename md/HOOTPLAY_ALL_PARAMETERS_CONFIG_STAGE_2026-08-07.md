# Hootplay exhaustive configuration stage — 2026-08-07

## Goal

Make `hootplay.ini` an exhaustive, English-language reference for all currently
supported `hootplay` runtime parameters, with every override disabled by
default.

## Changes

- Replaced the partially active sample configuration with a fully commented
  reference configuration.
- Added named `[general]`, `[psg]`, `[pc98]` and `[pc88]` settings alongside
  the existing `[player]`, `[midi]`, `[x68k]` and `[environment]` sections.
- Added named configuration mappings for all current Hoot runtime environment
  controls that affect player/driver behavior, except process-discovery
  variables such as `HOME` and `LOCALAPPDATA`.
- Kept `HOOTPLAY_CONFIG` outside the INI because it selects which INI file is
  loaded rather than changing playback after that file has been selected.
- Added normal boolean handling for legacy presence-only environment flags.
  Explicit `false` now unsets such a flag in the `hootplay` process instead of
  accidentally enabling it by exporting a string value of `0`.
- Retained `[environment]` as a forward-compatible escape hatch.

## Compatibility

Existing CLI options still work as one-shot overrides. Existing environment
variables still work. An unmodified shipped `hootplay.ini` has no active keys,
so it preserves built-in defaults and catalog/autodetection behavior.

## Validation

- Release build succeeds.
- 6/6 unit tests pass.
- The exhaustive reference INI parses successfully with all options commented.
- `hootplay --config hootplay.ini --list` succeeds and uses the historical
  built-in catalog default.
