# Hoot per-user home + Settings UI stage — 2026-08-08

## Runtime home

Native `hootplay` and `hootui` now bootstrap `~/.hoot` at startup.

First-run import is deliberately non-destructive:

- if `./hootplay.ini` exists and `~/.hoot/hootplay.ini` does not, it is copied;
- if `./catalog/` exists and `~/.hoot/catalog/` does not, it is recursively copied;
- existing files in `~/.hoot` are never overwritten;
- if no config is available, Hoot creates a complete reference config in
  `~/.hoot/hootplay.ini`.

Normal config lookup is now:

1. `--config`
2. `HOOTPLAY_CONFIG`
3. `~/.hoot/hootplay.ini`
4. `./hootplay.ini`
5. config beside the executable (legacy fallback)

The default native catalogue becomes `~/.hoot/catalog/hoot.sqlite.zst` when it
exists. Pack ZIP files are not moved automatically.

## UI executable and installation

The native CMake target remains `hootgui` internally, but its produced executable
is now named `hootui`.

Use `cmake --install build --prefix /usr/local` rather than copying the binary by
itself. The install layout is `/usr/local/bin/hootui` + `/usr/local/lib/libhoot`.
The installed frontend rpath points to the matching `../lib` directory.

Direct runtime dependencies are SDL3, SDL3_ttf for Unicode/Japanese text, and
`libhoot`; `libhoot` links zlib, SQLite3 and Zstd. FluidSynth, libmt32emu/Munt,
Vermouth, mdxmini and Nuked-SC55 are optional dynamically-loaded replay backends.

## Settings UI

The top-bar Settings item and Ctrl+, open a modal editor. It covers every one of
the 72 named settings currently accepted by the INI parser, grouped into:

- General
- Player
- Interface
- MIDI
- X68000
- PSG / SSG
- PC-98
- PC-88
- Environment (existing arbitrary variables are displayed and preserved)

Each named setting has an explicit enable/disable state, preserving the project's
"disabled means catalogue/autodetection/default" semantics. Boolean and choice
settings cycle directly. Text, numeric and path values use SDL text input.
Saving validates the generated INI through the production parser before an atomic
replace. Runtime/backend/catalog/audio/font changes take effect on restart.

## Validation

- Existing suite plus new home/settings round-trip test: 26/26 passing.
- Real first-run runtime test with temporary HOME imported the project catalogue
  and config and successfully listed Japanese catalogue titles.
- GUI sources syntax-check clean with SDL3_ttf enabled and with the ASCII fallback
  path. The current container does not contain native SDL3 development packages,
  so a real SDL window was not launched here.
