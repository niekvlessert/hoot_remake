# Building the three Hoot frontends

The project is split into a replay library and frontends. SDL3 remains optional
for CLI/library-only builds (`-DHOOT_BUILD_GUI=OFF`). For `HOOT_BUILD_GUI=ON`,
both SDL3 and SDL3_ttf are mandatory because Japanese/UTF-8 rendering is a core
Hoot requirement. CMake fails rather than producing an ASCII-only `hootui`.
The native UI also refuses to start if it cannot locate a Japanese-capable system
font; use `hootui --check-font` to test this without opening a window.

## Linux

Install a C/C++17 toolchain, CMake, Ninja, zlib, SQLite3, Zstd, SDL3 and SDL3_ttf, then:

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DHOOT_BUILD_GUI=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

Run `build/hootui` or `build/hootplay`. Both bootstrap and use `~/.hoot`.
For a normal Unix installation, prefer:

```sh
cmake --install build --prefix /usr/local
```

This installs `hootui`/CLI tools in `/usr/local/bin` and `libhoot` in
`/usr/local/lib`; the installed `hootui` has a relative rpath to `../lib`.

## macOS

Install the dependencies with Homebrew, for example:

```sh
brew install sdl3 sdl3_ttf
```

Then use the same CMake commands. Configuration now fails immediately if either
GUI library is unavailable. `hootui` uses SDL3; the CLI keeps its native
AudioToolbox output path on macOS. After building, `build/hootui --check-font`
should report the selected Japanese system font before you start the UI.

## Windows

Use a recent Visual Studio toolchain and SDL3 + SDL3_ttf CMake packages:

```bat
cmake -S . -B build -DHOOT_BUILD_GUI=ON -DCMAKE_PREFIX_PATH="C:\path\to\SDL3;C:\path\to\SDL3_ttf"
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

The installed native library is `hoot.dll`; the GUI and CLI use the same C ABI
and replay core.

## Termux / Android

The code itself has no X11-specific GUI calls; `hootui` talks only to SDL3.
A plain Termux terminal, however, does not provide a graphical display surface.
Use an SDL3 build with an available Android/display backend (for example a
Termux graphical environment) or package the same SDL frontend as an Android
application. The CLI remains usable without a graphical display.

## WebAssembly

Use an Emscripten toolchain:

```sh
emcmake cmake -S . -B build-web -G Ninja -DHOOT_BUILD_GUI=ON -DHOOT_BUILD_WEB=ON
cmake --build build-web --target hootgui
```

For the browser GUI, a local Japanese-capable font is now required at build
time. It is preloaded into MEMFS and enables the Emscripten SDL3_ttf port:

```sh
emcmake cmake -S . -B build-web -G Ninja \
  -DHOOT_BUILD_GUI=ON -DHOOT_BUILD_WEB=ON \
  -DHOOT_GUI_FONT_FILE=/path/to/NotoSansCJK-Regular.ttc
```

No font is copied into the source tree.

The result is `hootweb.html` plus its JavaScript/WASM payload. The generated
catalogue is embedded, but music archives are selected at runtime with the web
file picker and written to MEMFS.

See `WASM.md` for the current limitation around native dynamic MIDI synth
plugins in browsers.
