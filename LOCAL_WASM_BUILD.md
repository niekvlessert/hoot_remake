# Build the Hoot Web player locally

This source archive contains the same Hoot/WASM browser fixes as the hosted
player:

- Chrome WebGL uploads do not use resizable `ArrayBuffer` views.
- Pack import waits until Hoot is fully ready and always reports its result.
- Common renamed downloads such as `xak68snd(1).zip` and `ad68snd_.zip` are
  matched to their catalogue archive names.

## Requirements

- A current Emscripten SDK (`emcc`, `emcmake`, and Node.js)
- CMake and Ninja
- The `zstd` command
- A Japanese-capable TTF, OTF, or TTC font

## Configure and build

From the unpacked source directory:

```sh
emcmake cmake -S . -B build-web -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DHOOT_BUILD_GUI=ON \
  -DHOOT_BUILD_WEB=ON \
  -DBUILD_TESTING=OFF \
  -DHOOT_GUI_FONT_FILE=/absolute/path/to/Japanese-capable-font.ttc

cmake --build build-web --target hootgui --parallel
```

The browser bundle is written to:

```text
build-web/hootweb.html
build-web/hootweb.js
build-web/hootweb.wasm
build-web/hootweb.data
```

Serve the directory over HTTP; browsers should not load the bundle directly
through a `file://` URL:

```sh
python3 -m http.server 8000 --directory build-web
```

Then open `http://localhost:8000/hootweb.html`.

The WebGL compatibility rewrite runs automatically as a post-link step. If a
future Emscripten version changes the relevant generated code, the build fails
instead of silently producing the known Chrome crash.

For details about persistent browser storage, recording, MIDI limitations, and
CI, see `docs/WASM.md`.
