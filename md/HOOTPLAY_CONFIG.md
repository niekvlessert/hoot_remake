# hootplay configuration

`hootplay.ini` is the normal place for runtime settings. The shipped reference
file lists every supported setting, but every setting is commented out by
default. Therefore placing the reference file next to `hootplay` does not
change playback behavior until a setting is explicitly enabled.

## Search and precedence

At native startup `hootplay` and `hootui` first create `~/.hoot`. If the current
directory contains `./catalog` and/or `./hootplay.ini`, they are copied into
`~/.hoot` once; existing user files are never overwritten. If no config exists,
a complete reference config is generated.

The first configuration file found is used:

1. `--config /path/to/hootplay.ini`
2. `HOOTPLAY_CONFIG`
3. `~/.hoot/hootplay.ini`
4. `./hootplay.ini`
5. `hootplay.ini` next to the executable (legacy fallback)

If no setting is enabled in the file, historical built-in defaults, catalog
metadata and automatic driver detection remain in effect. Existing CLI options
remain valid as one-shot overrides. For player settings the effective
precedence is `CLI > config > built-in default`.

Relative file paths are resolved relative to the directory containing the INI
file.

## Sections

The exhaustive reference file contains these sections:

- `[general]`: catalog/XML override controls.
- `[player]`: catalog, packs, entry, sample rate, track, listing, channel mute,
  percussion mute and WAV rendering.
- `[midi]`: MIDI transport/backend selection, Munt/libmt32emu MT-32 and CM-32L ROM paths, CM-32P internal/card PCM ROM paths, FluidSynth SoundFonts, Nuked-SC55 CLAP/ROM paths, SC-55 model, MIDI gain and CLAP search path.
- `[x68k]`: Human68k loader, CPU/OPM clocks, YM2151 core, PCM8, MFP/startup,
  X68000 mix gains and tracing.
- `[psg]`: common PSG/SSG channel, gain and diagnostic controls.
- `[pc98]`: PC-98 tracing, OPN diagnostics, compatibility switch, MMD timer
  override and PIT-speaker gain.
- `[pc88]`: Micro Cabin/Xak II IRQ-bus and trace controls.
- `[environment]`: verbatim passthrough for future/experimental variables not
  yet represented by a named key.

The root `hootplay.ini` is the canonical list of all supported keys and includes
English comments describing each setting and its normal default or purpose.

## Disabled-by-default reference style

Every setting is shipped like this:

```ini
[player]
# sample_rate = 44100
# track = 1

[midi]
# backend = auto
# soundfont = soundfonts/GeneralUser_GS.sf2
```

Remove the leading `#` only for settings you want to override. This is
intentional: a reference configuration should not silently override catalog
metadata or driver autodetection simply because the file exists.

## Boolean compatibility switches

Some legacy driver switches are implemented internally as presence-only
environment variables. Their named INI keys behave like normal booleans:
`true` enables them and `false` explicitly disables/unsets them for the
`hootplay` process. Examples include `psg.disabled`, `psg.solo`,
`pc98.trace_opn` and `pc98.disable_opn_tl_compat`.

## Advanced environment passthrough

The passthrough remains available as an escape hatch:

```ini
[environment]
# SOME_FUTURE_HOOT_SETTING = value
```

Prefer the named keys in the other sections whenever one exists. Unknown keys
in named sections are errors so typographical mistakes cannot silently alter
playback behavior.


## GUI editor

`hootui` exposes all named keys in the Settings dialog (Ctrl+,). Saving is
validated through the same INI parser used at startup and uses an atomic
temporary-file replacement. Unknown/ad-hoc `[environment]` entries are kept
unchanged.
