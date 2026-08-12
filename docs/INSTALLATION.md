# Native installation and runtime dependencies

Native builds produce `hootui` (CMake target `hootgui`), `hootplay`, `hoot2wav`,
`hootprobe` and the shared `libhoot` library.

For a prefix installation use:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DHOOT_BUILD_GUI=ON
cmake --build build
sudo cmake --install build --prefix /usr/local
```

`hootui` is installed in `/usr/local/bin` and `libhoot` in `/usr/local/lib`.
The installed Unix GUI uses `$ORIGIN/../lib` (macOS: `@loader_path/../lib`) so
it can find the matching `libhoot` without requiring the catalogue or config to
live beside the executable.

## Required runtime libraries

For the normal shared build `hootui` directly needs:

- `libhoot` from this project;
- SDL3;
- SDL3_ttf for full Unicode/Japanese text (without it the build can fall back to
  SDL3's ASCII debug font);
- the C/C++ runtime for the platform.

`libhoot` in turn links to zlib, SQLite3 and Zstd. On platforms where iconv is a
separate library it may also be linked. SDL3 can itself depend on or dynamically
load the platform's display and audio backends.

These replay backends are optional and loaded dynamically only when selected by
catalogue/configuration: FluidSynth, Munt/libmt32emu, Vermouth, mdxmini and the
Nuked-SC55 CLAP plugin. Their SoundFonts, MT-32/CM-32L/CM-32P/Sound Canvas ROMs
and expansion-card ROMs are user data and are not bundled.

PC-88 catalogue entries that enable `use_n88rom` can use an original 32 KiB
N88-BASIC image named `N88.ROM` or `PC88.ROM`. Put it at the configured packs
root or in its `roms/` subdirectory. A pack may also contain the image itself.
The ROM is optional and is not bundled; without one, the PC-88 host keeps work
RAM visible so packs that do not actually call BASIC services still run.

Copying only a build-tree `hootui` binary to `/usr/local/bin` is therefore not a
complete installation: the matching `libhoot` and SDL runtime libraries must be
visible to the dynamic loader. `cmake --install` is the supported layout.

## Per-user data

On native startup, `hootplay` and `hootui` create `~/.hoot`. If the launch
working directory contains a `catalog/` directory or `hootplay.ini`, each is
copied into `~/.hoot` once if the destination does not already exist. Existing
user files are never overwritten. Later starts use `~/.hoot/hootplay.ini` by
default, so installing the executable in `/usr/local/bin` does not require any
writable files there.
