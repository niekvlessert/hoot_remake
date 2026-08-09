# WebAssembly / browser frontend

The SDL frontend is also the browser frontend. It is cross-compiled with
Emscripten and linked to the same static Hoot replay core; there is no separate
JavaScript replay engine.

## Build

Use a current Emscripten SDK, the host `zstd` command, and provide a Japanese-capable font at build time:

```sh
emcmake cmake -S . -B build-web -G Ninja \
  -DHOOT_BUILD_GUI=ON \
  -DHOOT_BUILD_WEB=ON \
  -DBUILD_TESTING=OFF \
  -DHOOT_GUI_FONT_FILE=/path/to/Japanese-capable-font.ttc
cmake --build build-web --target hootgui
```

The build intentionally fails when `HOOT_GUI_FONT_FILE` is absent. Hoot is a
Japanese-music player and the browser has no portable filesystem path to a
system font, so an ASCII-only web build is not considered valid.

The resulting deployment consists of `hootweb.html`, `hootweb.js`,
`hootweb.wasm` and `hootweb.data`. During the host build, the shipped compact
`catalog/hoot.sqlite.zst` is expanded with the `zstd` command and the resulting
immutable SQLite catalogue is preloaded as `/catalog/hoot.sqlite`. The Wasm
runtime itself therefore does not link zstd solely to decompress a startup
resource. Emscripten's SDL3, SDL3_ttf, SQLite and zlib ports are used.

The link step also runs `tools/patch_emscripten_webgl.mjs`. This keeps growing
Wasm memory enabled while ensuring WebGL texture uploads use a fixed
`ArrayBuffer` view. It prevents Chrome's `texSubImage2D` “must not be resizable”
exception. Node.js is therefore required during the web link step; it is also
included with normal Emscripten SDK installations.

## Persistent browser home

The web shell mounts IDBFS at `/hoot` before C++ `main()` starts. This is the
browser equivalent of native `~/.hoot`:

```text
/hoot/
  hootplay.ini
  hootui-state.ini
  packs/
  catalog/user-overrides.json
  recordings/
```

Pack uploads, Settings changes, Library state and Catalog Editor overrides are
synchronized to IndexedDB and survive page reloads in the same browser/site
origin. The immutable base catalogue and UI font remain in the application
bundle.

**Open...** in the SDL UI and the HTML toolbar both open the browser file picker.
ZIPs are copied to `/hoot/packs`, indexed by the normal Library code, persisted,
and then passed through the same archive/variant-selection path as native Hoot.
Multiple ZIPs may be imported at once. ZIPs can also be dragged onto the canvas.
The button becomes active only after C++ `main()` has completed its startup
path. Import always reports progress or a concrete error, and common browser
download renames such as `xak68snd(1).zip` and trailing-underscore names such as
`ad68snd_.zip` are retried under their catalogue archive name.

## Audio and WAV recording

Playback uses SDL3's audio stream API. The shell resumes browser audio after a
user pointer/key gesture to satisfy browser autoplay policies.

Playback -> Record WAV writes a normal 16-bit stereo WAV to `/hoot/recordings`.
Stopping recording finalizes the RIFF header and downloads the WAV through the
browser while also leaving the persistent copy in IDBFS.

## MIDI/backend limitations

Native dynamically loaded synth libraries/plugins are not magically available
inside WebAssembly. PC-88/PC-98/X68000 chip replay runs through the same core,
but Munt, FluidSynth, Nuked-SC55, Vermouth, mdxmini and similar native dynamic
backends need dedicated static/WASM ports before hardware-equivalent browser
MIDI can be claimed. Hoot's normal hardware/backend warning overlay remains the
source of truth when such a backend is unavailable.

## CI

`.github/workflows/ci.yml` contains a Web/WASM job. It installs a Japanese Noto
font on the runner, configures a real Emscripten build, checks that the required
ports exist, builds `hootweb`, and verifies that the deployable bundle contains
the IDBFS/persistent-pack wiring plus the preloaded Japanese font and SQLite
catalogue.
