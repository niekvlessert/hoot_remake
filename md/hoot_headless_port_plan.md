# Hoot Headless Port Plan

## Goal

Port the useful parts of the old Hoot source tree into a modern, embeddable music replay library for integration into a multi-format audio player. The first deliverable is **not** a GUI player. It is:

1. A portable C/C++ library: `libhootcore`.
2. A small CLI tool: `hoot2wav`.
3. A narrow first playback target: **PC-88 or PC-98 Microcabin music**, preferably a Hoot-supported Microcabin-related driver/data set.
4. Later: WebAssembly build suitable for a browser or WebView audio player.

The port should preserve Hoot's core value: replaying original/native game music driver data, rather than only playing VGM/S98 logs.

Assumption for the first implementation pass: the **Hoot source tree and a small set of Hoot-compatible music/test packs are already present in the local project directory**. The first tooling should therefore work against local files first, without download logic, remote catalog syncing, or bundled copyrighted assets.

---

## Starting point: local source and test packs

The project directory is assumed to already contain:

```text
hoot-port/
  hootsrc20011006.cab              # original source archive, if still kept
  hoot-source/ or hoot-original/    # extracted Hoot source tree
  packs/                           # a few local Hoot-compatible test packs
  hoot.xml or reduced test XML      # catalogue/config used by the packs
```

`hootsrc20011006.cab` is an old Microsoft Cabinet archive containing a Visual C++/Windows-era Hoot source tree. If the CAB has already been extracted, treat the extracted folder as the canonical local input and keep the CAB only as provenance. The file list indicates a roughly modular structure:

```text
hoot.cpp / hoot.h / MainFrm.cpp / MFC resources
ssSoundDriverManager.cpp
ssSoundStream.cpp
ssDriverBinder.cpp
ssConfigLoader.cpp
ssConfig.cpp
ssFile.cpp
ssFolder.cpp
ssTimer.cpp
ssTimerManager.cpp
ssUnZip.cpp

/drivers
  kss.cpp
  mucom88.cpp
  mucom2608.cpp
  scc.cpp
  mxdrv.cpp
  x68k.cpp
  mistyblue.cpp
  zavas.cpp
  starcruiser.cpp
  scheme.cpp
  snatcher.cpp
  silpheed.cpp
  ...

/sound
  ssYM2203.cpp
  ssYM2608.cpp
  ssYM2151.cpp
  ssYM2413.cpp
  ssAY8910.cpp
  ss051649.cpp
  ssADPCM.cpp
  ssMSM6258V.cpp
  ssPCM8.cpp
  ...

/mame/cpu
  cpuintrf.c
  memory.h
  m68000.h
  m6809.h
  h6280.h
  ...
```

This is a good sign: the code already separates driver logic, sound-chip emulation, configuration, and output. The bad sign is that the outer application is old Windows/MFC-style code and should be removed from the target architecture.

The local packs should be treated as **test fixtures**, not as redistributable project content. The repository should support a local `packs/` directory ignored by Git, while regression metadata and hashes can be committed separately.

---

## Non-goals

Do **not** port these in the first phase:

- MFC GUI.
- Windows resource files.
- Original Hoot playlist UI.
- DirectSound/waveOut-style real-time output.
- Full support for every Hoot driver.
- Browser UI.
- Full Hoot XML catalogue compatibility on day one.

The first successful milestone is simply:

```text
known Hoot-supported PC-88/PC-98 track
        -> libhootcore
        -> hoot2wav
        -> correct WAV output
```

---

## Target architecture

```text
+------------------+
| Audio player app |
+------------------+
          |
          v
+------------------+        +----------------+
|   libhootcore    |<------>| Asset provider |
+------------------+        +----------------+
          |
          v
+------------------+
| Driver module    |
| e.g. mucom88,    |
| mucom2608,       |
| zavas, etc.      |
+------------------+
          |
          v
+------------------+
| CPU/chip layer   |
| YM2203/YM2608/   |
| SSG/ADPCM/etc.   |
+------------------+
          |
          v
+------------------+
| PCM render API   |
+------------------+
```

The library should expose a pull-based rendering model:

```cpp
HootContext* hoot_create(const HootConfig* config);
void hoot_destroy(HootContext* ctx);

HootResult hoot_load_xml(HootContext* ctx, const char* xml_path);
HootResult hoot_load_entry(HootContext* ctx, const char* entry_id);
HootResult hoot_select_track(HootContext* ctx, int track_index);
HootResult hoot_reset(HootContext* ctx);

int hoot_render_s16(HootContext* ctx, int16_t* interleaved_stereo, int frames);
int hoot_render_float(HootContext* ctx, float* interleaved_stereo, int frames);

HootResult hoot_get_track_info(HootContext* ctx, HootTrackInfo* out);
```

The key design rule:

> The core never owns the UI, output device, browser, or file picker. It only loads assets and renders samples.

---

## Repository layout

Recommended new repository structure:

```text
hoot-port/
  CMakeLists.txt
  README.md
  LICENSES.md

  third_party/
    hoot-original/
      # read-only imported original source snapshot, copied from local Hoot source

  packs/
    # local Hoot-compatible test packs; ignored by Git by default
    # keep only README/hash metadata in version control

  src/
    core/
      hoot_context.cpp
      hoot_context.h
      hoot_api.h
      hoot_errors.h
      hoot_track_info.h

    compat/
      win32_compat.h
      mfc_stubs.h
      path_compat.cpp
      endian_compat.h
      fixed_width_types.h

    config/
      hoot_xml_loader.cpp
      hoot_xml_loader.h
      hoot_catalog.cpp
      hoot_catalog.h

    drivers/
      # gradually imported from original /drivers
      mucom88.cpp
      mucom2608.cpp
      zavas.cpp
      mistyblue.cpp
      ...

    sound/
      # gradually imported from original /sound
      ssYM2203.cpp
      ssYM2608.cpp
      ssAY8910.cpp
      ssADPCM.cpp
      ...

    cpu/
      # imported or replaced CPU cores

    io/
      asset_provider.h
      filesystem_asset_provider.cpp
      memory_asset_provider.cpp

  tools/
    hoot2wav/
      main.cpp
      wav_writer.cpp
      wav_writer.h

  tests/
    unit/
    regression/
    fixtures/
      README.md

  wasm/
    CMakeLists.txt
    hoot_wasm_bindings.cpp
    audio_worklet_bridge.cpp
    demo/
```

Suggested `.gitignore` entries:

```text
/packs/*
!/packs/README.md
!/packs/*.sha256
/local-packs/
/build*/
```

---

## Phase 0 — Local source and pack inventory

### Objective

Establish a reproducible source snapshot and identify the local Hoot-compatible packs that can be used as first test fixtures.

### Tasks

- Locate the already-present Hoot source directory in the project tree.
- If only `hootsrc20011006.cab` is present, extract it using `cabextract`, `7z`, or another CAB/LZX-capable extractor.
- Commit the extracted source as a read-only vendor snapshot under `third_party/hoot-original/`.
- Locate the already-present local test packs, for example under `packs/`, `hoot-packs/`, or a configured external path.
- Do **not** commit copyrighted game/music pack contents unless they are explicitly redistributable. Instead, commit fixture metadata, expected filenames, hashes, and setup notes.
- Generate an inventory:
  - all source files;
  - all Windows-only files;
  - all driver modules;
  - all chip modules;
  - all CPU dependencies;
  - all XML/config files;
  - local pack directories and candidate Microcabin entries;
  - required asset filenames for the first target.
- Record exact hashes for provenance and reproducibility:

```bash
sha256sum hootsrc20011006.cab 2>/dev/null || true
find packs -type f -maxdepth 3 -print0 | xargs -0 sha256sum > PACK_HASHES.txt
```

### Deliverable

```text
third_party/hoot-original/
SOURCE_INVENTORY.md
SOURCE_HASHES.txt
PACK_INVENTORY.md
PACK_HASHES.txt
```

### Exit criteria

- Original tree is preserved unchanged.
- Build target files have been separated from GUI/resource files.
- Local pack directory is discovered and documented.
- Candidate Microcabin-related drivers and pack entries are identified.

---

## Phase 1 — Build a native headless skeleton

### Objective

Create a modern CMake project that can compile a minimal subset without GUI or audio device output.

### Tasks

- Create top-level `CMakeLists.txt`.
- Compile a tiny static library `libhootcore` with only:
  - core context;
  - error handling;
  - asset provider interface;
  - placeholder render function.
- Add `hoot2wav` CLI with dummy silence output.
- Add CI build for:
  - macOS clang;
  - Linux clang/gcc;
  - optional Windows MSVC later.

### CLI shape

```bash
hoot2wav --catalog hoot.xml --packs ./packs --entry fray_pc98 --track 1 --seconds 90 --out fray_01.wav
```

Initial supported options:

```text
--catalog <path>      Hoot XML or reduced test XML
--packs <path>        Local directory containing already-present Hoot-compatible test packs
--entry <id>          Game/driver entry id
--track <number>      Track number or index
--seconds <n>         Render duration
--rate <hz>           Output sample rate, default 44100 or 48000
--out <path>          Output WAV
--verbose             Print driver/chip/timing information
```

### Deliverable

- `libhootcore.a` builds.
- `hoot2wav` writes a valid silent WAV.

### Exit criteria

- No Windows GUI headers are needed.
- No real-time audio dependencies exist.

---

## Phase 2 — Remove platform assumptions

### Objective

Create compatibility shims for old Windows/VC assumptions without changing music behaviour.

### Likely issues

| Area | Fix |
|---|---|
| `windows.h` types | Replace with fixed-width C/C++ types. |
| `CString` / MFC containers | Replace with `std::string`, `std::vector`, or thin adapters. |
| Path separators | Normalize to `/` internally. |
| Win32 file APIs | Route through `AssetProvider`. |
| Critical sections | Use `std::mutex` only where needed. Avoid locks in render path. |
| Old integer assumptions | Use explicit `uint8_t`, `int16_t`, `uint32_t`. |
| Precompiled headers | Remove `StdAfx.h` dependency from ported files. |
| Timers | Replace with deterministic sample/frame stepping. |

### Deliverable

- `src/compat/` layer.
- Compile selected non-GUI support classes.

### Exit criteria

- Hoot support code compiles under modern clang without MFC.
- Remaining Windows dependencies are listed and isolated.

---

## Phase 3 — Asset and XML loading

### Objective

Load enough Hoot XML/config data to initialize one known driver entry.

### Strategy

Do not implement the full Hoot catalogue first. Implement a reduced subset needed for the first test.

Create an internal model:

```cpp
struct HootEntry {
    std::string id;
    std::string title;
    std::string driver_name;
    std::vector<HootAssetRef> assets;
    std::vector<HootTrackInfo> tracks;
    int default_sample_rate;
    int refresh_hz;
};
```

### Asset provider abstraction

```cpp
class AssetProvider {
public:
    virtual ~AssetProvider() = default;
    virtual bool exists(std::string_view path) = 0;
    virtual std::vector<uint8_t> read_all(std::string_view path) = 0;
};
```

Implement:

- `FilesystemAssetProvider` for CLI/native tests against the already-local Hoot source/packs directory.
- `MemoryAssetProvider` for future WASM/app embedding.

For the first milestone, prefer explicit local paths:

```bash
hoot2wav --catalog ./hoot.xml --packs ./packs --entry <id> --track 1 --seconds 120 --out test.wav
```

The core should not assume that packs live next to the executable; path resolution belongs in the asset provider or CLI layer.

### Deliverable

- Can parse or manually define one reduced Hoot entry.
- Can load required binary files through `AssetProvider`.

### Exit criteria

- `hoot2wav --list` can show available entries/tracks from a test XML.
- Missing assets produce clear errors.

---

## Phase 4 — First Microcabin test target selection

### Objective

Pick one narrow PC-88/PC-98 Microcabin-related target and avoid boiling the ocean.

### Candidate approach

Start with whichever Hoot driver has the smallest dependency graph and available test data. From the uploaded source file list, likely candidates to inspect first include:

```text
drivers/zavas.cpp
drivers/mistyblue.cpp
drivers/mucom88.cpp
drivers/mucom2608.cpp
```

For Microcabin specifically, prioritize actual Microcabin titles such as Fray/Xak/Zavas where Hoot has a usable entry and compatible driver data. If Fray is not directly represented in this older Hoot source snapshot, use another Microcabin PC-88/PC-98 title first, then generalize.

### Selection criteria

Choose the first target by this order:

1. Hoot driver source exists in the uploaded tree.
2. Required CPU core/chip modules are present.
3. Required music data can be obtained legally by the user from their own media or licensed release.
4. A reference output exists from original Hoot, emulator capture, or VGM/S98 rip.
5. The driver has limited dependencies.

### Deliverable

`FIRST_TARGET.md`:

```text
Game/title:
Machine:
Driver file:
Chip modules:
CPU modules:
Required assets:
Reference audio:
Known track list:
Risks:
```

### Exit criteria

- One target is selected.
- Required driver, chip, CPU, and asset dependencies are known.

---

## Phase 5 — Port one driver path

### Objective

Make one selected PC-88/PC-98 driver initialize and advance deterministically.

### Tasks

- Import the chosen driver module into `src/drivers/`.
- Stub only what is absolutely necessary.
- Route all file reads through `AssetProvider`.
- Route all chip writes to a chip manager.
- Replace UI callbacks with logging or no-op hooks.
- Add verbose tracing:

```text
[driver] init
[driver] load asset X size=N
[chip] YM2203 write reg=0x28 val=0xF0
[chip] YM2608 write port=0 reg=0x2D val=0x00
[timing] frame=1234 samples=735
```

### Deliverable

- Driver can load and initialize one track.
- Driver can run for N ticks without crashing.
- Register writes are visible in trace logs.

### Exit criteria

- First non-silent chip register activity is produced.
- No GUI/Win32/audio-device calls remain in the driver path.

---

## Phase 6 — Chip rendering path

### Objective

Convert driver chip writes into PCM samples.

### First chip targets

For PC-88/PC-98, expect these first:

| Machine | Common chip path | Notes |
|---|---|---|
| PC-88 | YM2203 / OPNA subset / SSG | FM + SSG. |
| PC-98 | YM2608 / OPNA | FM + SSG + rhythm + ADPCM depending on title. |

### Tasks

- Import `ssYM2203` and/or `ssYM2608`.
- Identify their expected clock and sample-rate configuration.
- Build a `ChipManager`:

```cpp
class ChipManager {
public:
    void write_ym2203(uint8_t addr, uint8_t data);
    void write_ym2608(uint8_t port, uint8_t addr, uint8_t data);
    void render(int16_t* stereo, int frames);
};
```

- Normalize output to interleaved stereo.
- Decide internal sample rate, probably 44100 or 48000 Hz.
- Avoid changing original FM core math unless necessary.

### Deliverable

- `hoot2wav` renders audible audio for the first target.

### Exit criteria

- WAV duration is correct.
- Track is recognizable.
- No major pitch/timing drift versus reference.

---

## Phase 7 — Timing correctness

### Objective

Fix tempo, pitch, and sequencing accuracy.

### Key checks

| Risk | Test |
|---|---|
| Wrong machine refresh rate | Compare song length and tempo to reference. |
| Wrong FM clock | Compare pitch against reference. |
| Wrong timer stepping | Log YM timer use and driver IRQ cadence. |
| Wrong integer widths | Audit signed/unsigned math in driver and chip code. |
| Wrong ADPCM/rhythm clock | Compare percussion timing and pitch. |
| Buffer-size dependency | Render same track with 64, 256, 1024, and 4096 frame chunks and compare output hash or near-hash. |

### Regression render command

```bash
hoot2wav --xml test.xml --entry first_target --track 1 --seconds 120 --out out.wav --trace out.trace
```

### Reference comparison

Maintain:

```text
tests/regression/reference/
  first_target_track01_reference.wav
  first_target_track01_notes.md
```

Use practical tests:

- total rendered duration;
- tempo by downbeat alignment;
- pitch by detected note frequency;
- register trace comparison if available;
- listening test.

### Deliverable

- `TIMING_NOTES.md` for the first target.
- Repeatable regression render.

### Exit criteria

- First target is close enough to Hoot/original reference that remaining differences are documented and bounded.

---

## Phase 8 — Public library API

### Objective

Expose a stable embeddable API for the future audio player.

### C API first

Prefer a C ABI even if the implementation is C++:

```c
typedef struct HootContext HootContext;

typedef struct HootConfig {
    int sample_rate;
    int channels;
    int log_level;
} HootConfig;

HootContext* hoot_create(const HootConfig* config);
void hoot_destroy(HootContext* ctx);

int hoot_load_catalog(HootContext* ctx, const char* xml_path);
int hoot_load_entry(HootContext* ctx, const char* entry_id);
int hoot_select_track(HootContext* ctx, int track_index);
int hoot_render_s16(HootContext* ctx, int16_t* interleaved, int frames);
int hoot_seek_ms(HootContext* ctx, int milliseconds);
int hoot_reset(HootContext* ctx);
const char* hoot_last_error(HootContext* ctx);
```

### Why C ABI

- Easy integration from C, C++, Rust, Swift, Kotlin/NDK, and WASM.
- Avoids exposing C++ STL ABI.
- Easier dynamic loading/plugin design.

### Deliverable

- `include/hoot/hoot.h`.
- Minimal API documentation.

### Exit criteria

- CLI uses only the public API.
- No internal headers leak into `tools/hoot2wav`.

---

## Phase 9 — CLI `hoot2wav`

### Objective

Create a practical tool for testing and conversion.

### Commands

```bash
# List entries from a catalog
hoot2wav --catalog hoot.xml --packs ./packs --list

# List tracks from one entry
hoot2wav --catalog hoot.xml --packs ./packs --entry fray_pc98 --list-tracks

# Render fixed length
hoot2wav --catalog hoot.xml --packs ./packs --entry fray_pc98 --track 1 --seconds 120 --out fray_01.wav

# Render with fade
hoot2wav --catalog hoot.xml --packs ./packs --entry fray_pc98 --track 1 --seconds 120 --fade 8 --out fray_01.wav

# Verbose trace
hoot2wav --catalog hoot.xml --packs ./packs --entry fray_pc98 --track 1 --seconds 30 --trace fray_01.trace --out fray_01.wav
```

### WAV format

Start simple:

- 16-bit signed PCM.
- Interleaved stereo.
- 44100 Hz or 48000 Hz.

Later:

- Float WAV.
- Loop detection.
- Metadata chunks.
- Batch conversion.

### Deliverable

- Robust `hoot2wav` with useful error messages.

### Exit criteria

- Can be used by scripts for regression tests.

---

## Phase 10 — WASM preparation

### Objective

Make the core compile cleanly to WebAssembly after the native headless path is proven.

### Rules

- Do not start with WASM.
- Do not debug musical correctness in the browser first.
- First make native WAV rendering correct.

### WASM-specific requirements

- No filesystem dependency in core.
- No threads initially.
- No exceptions across JS/C boundary, unless deliberately enabled.
- No blocking operations in audio render.
- Use memory asset provider or Emscripten virtual FS.
- AudioWorklet pulls samples from WASM.

### Proposed WASM API

```cpp
extern "C" {
    int hootwasm_create(int sample_rate);
    int hootwasm_load_entry_from_memory(const uint8_t* data, int size);
    int hootwasm_select_track(int handle, int track);
    int hootwasm_render_float(int handle, float* out, int frames);
    void hootwasm_destroy(int handle);
}
```

### Browser integration shape

```text
Main thread
  - file picker/import
  - catalog UI
  - track selection
  - IndexedDB storage

AudioWorklet thread
  - calls WASM render function
  - receives commands through message port
  - never performs file I/O
```

### Deliverable

- `wasm/` proof of concept rendering one already-loaded track.

### Exit criteria

- Same target renders correctly to WAV natively and audibly in browser/WebView.

---

## Phase 11 — Expansion after first success

Only expand after the first Microcabin PC-88/PC-98 target is correct.

Recommended next targets:

1. More tracks from the same game.
2. Another game using the same driver.
3. Generic `mucom88` path.
4. Generic `mucom2608` path.
5. MSX KSS/SCC path.
6. X68000/MXDRV path.
7. Additional arcade/console driver paths.

Expansion rule:

> Every new driver must enter through a regression fixture and `hoot2wav` render test.

---

## Testing strategy

### Unit tests

- XML parsing.
- Asset lookup.
- WAV writer.
- Ring buffer / render buffer sizes.
- Error handling.

### Driver smoke tests

- Load entry.
- Select track.
- Render 1 second.
- Confirm non-zero audio.
- Confirm no crashes or NaNs.

### Determinism tests

Render the same track with different chunk sizes:

```bash
hoot2wav --chunk 64   ... --out chunk64.wav
hoot2wav --chunk 256  ... --out chunk256.wav
hoot2wav --chunk 1024 ... --out chunk1024.wav
```

Expected result:

- Ideally identical PCM.
- If not bit-identical, differences must be explained and bounded.

### Musical regression tests

- Compare against original Hoot output where available.
- Compare against emulator/VGM/S98 reference where available.
- Check tempo alignment at 30, 60, and 120 seconds.
- Check pitch of sustained FM notes.
- Check ADPCM/rhythm timing if used.

---

## Main risks

| Risk | Impact | Mitigation |
|---|---:|---|
| Original source depends heavily on MFC/Win32 | High | Port only core paths; use compatibility shims. |
| Driver requires exact CPU/timer behavior | High | Preserve original stepping model; add trace logs. |
| Unknown asset formats | High | Start with one known Hoot-supported target. |
| Wrong FM chip clock | High | Compare pitch with reference output. |
| ADPCM/rhythm sample ROM assumptions | Medium/high | Document required ROM/assets; fail clearly if missing. |
| Browser real-time audio glitches | Medium | Use AudioWorklet only after native render is stable. |
| Legal/data ambiguity | Medium | User imports their own game/music data; do not bundle copyrighted assets. |
| Too much scope | Very high | First target only; no GUI; no full catalogue initially. |

---

## Definition of done for the first milestone

The first milestone is complete when:

```bash
hoot2wav --catalog microcabin_test.xml --packs ./packs --entry <first_target> --track 1 --seconds 120 --out test.wav
```

produces a WAV file that:

- is audible and recognizable;
- has correct tempo;
- has correct pitch;
- does not depend on render buffer size;
- can be reproduced from a clean checkout;
- uses only the public `libhootcore` API from the CLI;
- does not include MFC, Win32 audio, or GUI code in the core.

---

## Suggested immediate next actions

1. Locate the existing local Hoot source tree and local test packs in the project directory.
2. Extract the CAB only if the source tree is not already extracted.
3. Create `SOURCE_INVENTORY.md` and `PACK_INVENTORY.md` from the local tree.
4. Inspect these files first:

```text
drivers/mucom88.cpp
drivers/mucom2608.cpp
drivers/zavas.cpp
drivers/mistyblue.cpp
sound/ssYM2203.cpp
sound/ssYM2608.cpp
ssSoundDriverManager.cpp
ssDriverBinder.cpp
ssConfigLoader.cpp
ssSoundStream.cpp
```

5. Choose the first Microcabin PC-88/PC-98 target based on actual local pack availability and dependency complexity.
6. Build the silent `hoot2wav` skeleton with `--catalog` and `--packs`.
7. Port one driver path until it emits YM2203/YM2608 register writes.
8. Only then connect the chip renderer and compare WAV output.

---

## Practical recommendation

Treat Hoot as a **driver-hosting framework**, not as a player to port literally.

The correct first product is:

```text
libhootcore + hoot2wav + one verified Microcabin PC-88/PC-98 driver path
```

After that, WebAssembly becomes a packaging problem instead of a reverse-engineering and timing problem.
