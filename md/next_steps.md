# Hoot Headless Port: Next Steps

## Purpose

This document defines the next engineering steps for turning the existing local Hoot source tree and local Hoot-compatible packs into a modern, headless replay library.

The current focus is **not WebAssembly**. The focus is first to support as much of Hoot's playback capability as practical on a current native platform, with a clean library API and a small `hoot2wav` conversion tool.

The guiding split is:

```text
Port from Hoot:
  native music-driver/game-driver hosts
  catalog binding
  pack lookup
  driver init/play/track-selection logic
  CPU/memory/IO glue where required
  timing behaviour where required

Replace or import from modern sources:
  sound chip emulation where libvgm has suitable cores
  CPU cores where newer MAME or another maintained core is safer
  XML parsing
  ZIP/deflate handling
  filesystem/path handling
  audio output/WAV writing
  build system
```

The primary output should be:

```text
libhootcore.a / libhootcore.so / libhootcore.dylib
hoot2wav
```

The local project directory is assumed to already contain:

```text
hoot-port/
  hootsrc20011006.cab
  hoot-source/ or hoot-original/
  hoot.xml or reduced test catalog
  packs/
    # local Hoot-compatible packs, not redistributed
```

---

## Immediate non-goals

Do not spend time on these yet:

- WebAssembly.
- Browser AudioWorklet.
- Browser UI.
- Native GUI.
- Exact clone of Hoot's Windows UI.
- Rewriting all drivers from scratch.
- Building a remote pack downloader.
- Redistributing copyrighted game packs.

The next phase should produce a **native headless playback core** first.

---

## Target architecture

```text
hoot2wav / audio player integration
        |
        v
libhootcore
        |
        +-- catalog/config loader
        +-- asset provider
        +-- driver registry
        +-- driver host modules ported from Hoot
        +-- CPU/memory/IO runtime where needed
        +-- chip-write bus
        +-- libvgm-backed chip renderer
        +-- PCM render API
```

Preferred internal model:

```text
Hoot driver module
  -> chip write events
  -> chip bus adapter
  -> libvgm chip core
  -> mixed stereo PCM
```

Do **not** let the old Hoot code write directly to platform audio. The new core should be pull-based:

```cpp
int hoot_render_s16(HootContext* ctx, int16_t* interleaved_stereo, int frames);
int hoot_render_float(HootContext* ctx, float* interleaved_stereo, int frames);
```

---

## Core rule: preserve drivers, replace infrastructure

The most important rule is:

> Keep Hoot's driver behaviour as unchanged as possible, but replace old Windows-era infrastructure around it.

This avoids repeating the common failure mode seen in tracker/replayer ports: pitch, tempo, envelope or vibrato bugs caused by reimplementing too much musical logic.

---

## Code to port from Hoot

### 1. Driver registry and binding layer

Port these early because they determine how Hoot maps catalog entries to driver modules and data files.

Likely Hoot files:

```text
ssSoundDriverManager.cpp/.h
ssDriverBinder.cpp/.h
ssConfig.cpp/.h
ssConfigLoader.cpp/.h
hoot.xml handling code
```

Modern target:

```text
src/core/driver_registry.cpp
src/core/driver_registry.h
src/config/hoot_catalog.cpp
src/config/hoot_catalog.h
src/config/hoot_xml_loader.cpp
src/config/hoot_xml_loader.h
```

Expected work:

- Remove MFC/Win32 types.
- Replace `CString`/Windows strings with `std::string` or `std::u8string` where needed.
- Normalize path handling through an `AssetProvider` interface.
- Keep original driver names and catalog IDs where possible.
- Preserve the Hoot XML semantics, even if the parser implementation changes.

Priority: **critical**.

---

### 2. Asset and pack loading semantics

Port the semantics, not necessarily the old implementation.

Likely Hoot files:

```text
ssFile.cpp/.h
ssFolder.cpp/.h
ssUnZip.cpp/.h
zlib/
```

Modern target:

```text
src/io/asset_provider.h
src/io/filesystem_asset_provider.cpp
src/io/zip_asset_provider.cpp
src/io/memory_asset_provider.cpp
```

Expected work:

- Support loose files in `packs/`.
- Support ZIP packs if Hoot packs are ZIP-based.
- Keep case-insensitive lookup option, because old catalogs/packs may have inconsistent casing.
- Add deterministic search order:

```text
1. explicit --packs path
2. catalog-relative path
3. optional extra search paths
```

Priority: **critical**.

Recommended modern source:

- Use current `zlib` or `miniz` for ZIP/deflate instead of keeping old bundled code unless the old code has Hoot-specific pack behaviour.

---

### 3. Timing and scheduler layer

Port or carefully replicate Hoot's timing model.

Likely Hoot files:

```text
ssTimer.cpp/.h
ssTimerManager.cpp/.h
sound/ssFMTimer.cpp/.h
ssSoundStream.cpp/.h  # only for timing clues; not for actual output
```

Modern target:

```text
src/core/scheduler.cpp
src/core/scheduler.h
src/core/clock_domain.cpp
src/core/clock_domain.h
```

Expected work:

- Separate wall-clock/audio-device timing from emulated driver timing.
- Preserve per-driver tick cadence.
- Support FM timer A/B if drivers rely on YM2203/YM2608 timer behaviour.
- Define clock domains explicitly:

```text
CPU cycles
VBlank/frame ticks
FM timer ticks
sample frames
```

Priority: **critical**.

Risk: high. Incorrect timing produces wrong tempo, broken envelopes, or subtle pitch/modulation drift.

---

### 4. Driver host modules

Port all Hoot driver modules gradually. Do not rewrite their musical logic.

Likely Hoot directory:

```text
drivers/
```

Initial candidates to inspect and port:

```text
mucom88.cpp       # PC-88 / YM2203 path
mucom2608.cpp     # PC-98 / YM2608 path
zavas.cpp         # likely Microcabin-related PC-88/game-specific path
mistyblue.cpp     # likely Microcabin-related path
xtalsoft.cpp      # PC-88/game-driver family candidate
kss.cpp           # MSX KSS path
scc.cpp           # MSX SCC path
mxdrv.cpp         # X68000 MDX/MXDRV path
x68k.cpp          # X68000 host path
hes.cpp           # PC Engine/HES path
snatcher.cpp      # MSX/game-specific path
silpheed.cpp      # game-specific path
starcruiser.cpp   # game-specific path
scheme.cpp        # game-specific path
revolter.cpp      # game-specific path
ed2.cpp           # game-specific path
```

Modern target:

```text
src/drivers/<same_name>.cpp
src/drivers/<same_name>.h
```

Expected work:

- Add a common driver interface:

```cpp
class IHootDriver {
public:
    virtual ~IHootDriver() = default;
    virtual HootResult load(const HootDriverLoadRequest& request) = 0;
    virtual HootResult startTrack(int track) = 0;
    virtual void reset() = 0;
    virtual int render(int16_t* stereo, int frames) = 0;
    virtual HootDriverInfo info() const = 0;
};
```

- Replace direct Hoot sound object ownership with a `ChipBus` or `ChipWriteSink`.
- Preserve driver initialization order.
- Preserve song-selection quirks.
- Preserve memory patching logic.
- Preserve any hardcoded addresses until tests prove they can be generalized.

Priority: **critical**.

---

### 5. CPU, memory and IO glue used by drivers

Port this only as needed by selected drivers, but keep the abstraction broad enough to support everything later.

Likely Hoot files/directories:

```text
mame/cpu/cpuintrf.c
mame/cpu/memory.h
mame/cpu/h6280.*
mame/cpu/m6800.*
mame/cpu/m68000.*
mame/cpu/m6809.*
RAZE/ or other Z80-related code
```

Modern target:

```text
src/cpu/
src/memory/
src/io/
```

Expected work:

- Identify which drivers execute guest CPU code and which are direct replayers.
- For CPU-backed drivers, keep exact memory maps and IO callbacks.
- Standardize callbacks:

```cpp
uint8_t mem_read(uint32_t address);
void mem_write(uint32_t address, uint8_t value);
uint8_t io_read(uint16_t port);
void io_write(uint16_t port, uint8_t value);
void irq(int line, bool state);
```

Priority: **high**, but driver-dependent.

Recommended modern source:

- Consider newer MAME CPU cores where licensing and integration are acceptable.
- For Z80, evaluate whether Hoot's original core is sufficient or whether a modern maintained Z80 core is easier to test.
- For 68000/X68000-heavy paths, prefer a maintained core over trying to modernize old MAME fragments unless the Hoot integration depends tightly on them.

---

### 6. Hoot-specific chip routing and configuration

Port the routing logic, but not necessarily the chip emulation itself.

Likely Hoot files:

```text
sound/ssSoundChip.*
sound/ssSoundSystem.*
sound/ssSoundModule.*
```

Modern target:

```text
src/chip/chip_bus.cpp
src/chip/chip_bus.h
src/chip/chip_instance.h
src/chip/libvgm_chip_adapter.cpp
src/chip/libvgm_chip_adapter.h
```

Expected work:

- Define a common write API:

```cpp
struct ChipBus {
    void writeYM2203(int chip, uint8_t addr, uint8_t data);
    void writeYM2608(int chip, uint8_t port, uint8_t addr, uint8_t data);
    void writeYM2151(int chip, uint8_t addr, uint8_t data);
    void writeYM2413(int chip, uint8_t addr, uint8_t data);
    void writeYM2610(int chip, uint8_t port, uint8_t addr, uint8_t data);
    void writeAY8910(int chip, uint8_t addr, uint8_t data);
    void writeSCC(int chip, uint16_t addr, uint8_t data);
    void writePCM(int chip, uint8_t addr, uint8_t data);
};
```

- Support multiple chips per driver where Hoot allows it.
- Preserve chip clock rates from Hoot XML/config.
- Preserve stereo pan/routing metadata if present.
- Allow chip-write logging for future VGM/S98 export.

Priority: **critical**.

---

## Code to replace with modern sources

### 1. Sound chip emulation

Preferred source: **libvgm**.

Replace Hoot sound cores with libvgm-backed cores where equivalents are available.

Hoot chip cores likely replaceable by libvgm:

```text
YM2203 / OPN
YM2608 / OPNA
YM2151 / OPM
YM2413 / OPLL
YM2610 / OPNB
YM3438 / OPN2C
YM3812 / OPL2
AY-3-8910 / SSG
SN76496
HuC6280 PSG
Konami K051649 / SCC
Konami K007232
Konami K053260
Konami K054539
Namco C140
Sega PCM
QSound
MSM6295
```

Keep Hoot chip cores temporarily only when:

- libvgm lacks a matching chip/core,
- Hoot's output is known to depend on a specific nonstandard behaviour,
- or replacing the core blocks the first milestone.

Important policy:

```text
First make the driver emit correct chip writes.
Then decide whether libvgm or the original Hoot core renders those writes closer to reference.
```

For PC-88/PC-98 Microcabin, start with:

```text
YM2203 via libvgm
YM2608 via libvgm
SSG path inside OPN/OPNA
OPNA rhythm/ADPCM path
```

Priority: **critical**.

---

### 2. XML parser

Preferred source: **tinyxml2** or another small maintained XML parser.

Replace old Hoot XML parsing internals if they are tied to MFC/Win32.

Modern target:

```text
src/config/hoot_xml_loader.cpp
```

Rules:

- Preserve Hoot XML semantics.
- Do not invent a new catalog format yet.
- Add a normalized internal `HootCatalog` model.
- Add parser tests using reduced XML fixtures.

Priority: **high**.

---

### 3. ZIP/deflate

Preferred source: current `zlib`, `miniz`, or `libarchive` depending on desired footprint.

Replace:

```text
ssUnZip.cpp
old bundled zlib if outdated
```

Keep old Hoot unzip logic only if it supports Hoot-specific quirks not handled by normal ZIP libraries.

Priority: **medium-high**.

---

### 4. Build system

Preferred source: new CMake project.

Do not keep old Visual Studio project files as primary build artifacts.

Modern target:

```text
CMakeLists.txt
cmake/FindLibVGM.cmake or FetchContent/libvgm integration
```

Targets:

```text
hootcore
hoot2wav
hoot_tests
```

Priority: **critical**.

---

### 5. WAV writer

Use a tiny new implementation or a small permissive library.

Modern target:

```text
tools/hoot2wav/wav_writer.cpp
```

Requirements:

- 16-bit PCM WAV first.
- Stereo.
- Fixed sample rates: start with 44100 and 48000.
- Deterministic output.
- Optional loop count / fade-out later.

Priority: **critical**.

---

### 6. Tests and reference comparison

Use new code.

Modern target:

```text
tests/regression/
tests/fixtures/
tools/compare_audio/
```

Regression method:

```text
1. Render reference WAV from original Hoot on Windows if possible.
2. Render new WAV from libhootcore.
3. Compare duration, RMS, peak, and optionally spectrogram/chip-write logs.
4. Store only hashes/metrics in Git, not copyrighted packs.
```

Priority: **critical**.

---

## Code to discard or quarantine

Do not port these into the core:

```text
MFC window classes
MainFrm.cpp
resource scripts
menu/dialog code
playlist UI
DirectSound/waveOut output code
Win32 registry/config UI
Windows message loop dependencies
```

Quarantine them under:

```text
third_party/hoot-original/
```

Use them only for reference.

---

## Support-everything roadmap

The phrase “support everything” should be implemented in layers, not as one large rewrite.

### Layer 0: Inventory

Create an inventory from the local Hoot source and catalog:

```text
scripts/inventory_hoot.py
```

Output:

```text
docs/hoot_inventory.md
```

Inventory should list:

- driver modules,
- sound chip modules,
- CPU cores,
- XML driver IDs,
- required data files per game,
- chip clocks,
- target machines,
- local pack coverage.

Exit criterion:

```text
We know which Hoot entries can be tested using the local packs already present.
```

---

### Layer 1: PC-88 / YM2203

Goal:

```text
PC-88-style driver -> YM2203/SSG -> WAV
```

Candidate drivers:

```text
mucom88
zavas
mistyblue
xtalsoft
```

Why first:

- It matches the Microcabin-first goal.
- YM2203 is simpler than YM2608.
- PC-88 music is a clean first proof of driver-host correctness.

Exit criterion:

```text
hoot2wav can render at least one local PC-88 Microcabin track to WAV.
```

---

### Layer 2: PC-98 / YM2608

Goal:

```text
PC-98-style driver -> YM2608/SSG/rhythm/ADPCM -> WAV
```

Candidate drivers:

```text
mucom2608
other PC-98-specific game drivers found in catalog inventory
```

Why second:

- It expands from OPN to OPNA.
- It validates rhythm/ADPCM paths.
- It covers many PC-98 game-music cases.

Exit criterion:

```text
hoot2wav can render at least one local PC-98 Microcabin or similar track to WAV.
```

---

### Layer 3: MSX / KSS / SCC / OPLL

Goal:

```text
MSX driver/data -> PSG/SCC/OPLL -> WAV
```

Candidate drivers:

```text
kss
scc
snatcher
```

Why third:

- It aligns with the broader MSX/SCC interest.
- libKSS may be a better source for some behaviour than old Hoot code.
- SCC and OPLL support can reuse libvgm where suitable.

Modern source candidates:

```text
libKSS for MSX-native replay behaviour
libvgm for chip rendering
```

Exit criterion:

```text
At least one KSS/SCC/MSX-MUSIC entry renders through the unified libhootcore API.
```

---

### Layer 4: X68000 / MXDRV

Goal:

```text
X68000/MXDRV-style data -> YM2151 + ADPCM/PCM -> WAV
```

Candidate drivers:

```text
mxdrv
x68k
```

Modern source candidates:

```text
maintained MXDRV-related replayers if available
modern 68000 core if CPU-backed execution is needed
libvgm YM2151 and PCM/ADPCM cores
```

Exit criterion:

```text
At least one local X68000/MXDRV entry renders correctly.
```

---

### Layer 5: PC Engine / HES

Goal:

```text
HES/HuC6280 driver -> HuC6280 PSG -> WAV
```

Candidate drivers:

```text
hes
```

Modern source candidates:

```text
modern HuC6280 CPU core, possibly from MAME
libvgm or modern PSG core for HuC6280 sound
```

Exit criterion:

```text
At least one HES entry renders correctly.
```

---

### Layer 6: Arcade/game-specific drivers

Goal:

```text
game-specific driver hosts -> mixed FM/PCM chips -> WAV
```

Candidate drivers:

```text
silpheed
starcruiser
scheme
revolter
ed2
angelus
firehawk
midgarts
trpscr
```

Modern source candidates:

```text
libvgm for chip rendering
MAME for CPU cores and missing chips where needed
Hoot original code for game-specific init/play logic
```

Exit criterion:

```text
Each driver has at least one local or user-provided pack that renders through hoot2wav.
```

---

## First implementation sequence

### Step 1: Create source inventory

Commands/tools to create:

```text
scripts/inventory_hoot.py
```

Tasks:

- Scan `hoot-source/`.
- List all `drivers/*.cpp`.
- List all `sound/ss*.cpp`.
- List all CPU directories/files.
- Parse `hoot.xml` enough to map entries to drivers and files.
- Check which referenced packs/files exist locally.

Deliverable:

```text
docs/hoot_inventory.md
```

---

### Step 2: Create modern repository skeleton

Create:

```text
src/core/
src/config/
src/io/
src/drivers/
src/chip/
src/cpu/
tools/hoot2wav/
tests/
third_party/hoot-original/
```

Add CMake targets:

```text
hootcore
hoot2wav
hoot_tests
```

---

### Step 3: Implement asset provider

Implement:

```cpp
class AssetProvider {
public:
    virtual bool exists(std::string_view path) = 0;
    virtual std::vector<uint8_t> readAll(std::string_view path) = 0;
    virtual std::vector<std::string> list(std::string_view path) = 0;
};
```

Backends:

```text
FilesystemAssetProvider
ZipAssetProvider
CompositeAssetProvider
```

---

### Step 4: Implement reduced catalog loader

Implement enough Hoot XML support to load local test entries.

Do not aim for full XML compatibility before the first song renders.

Output model:

```cpp
struct HootEntry {
    std::string id;
    std::string title;
    std::string driver;
    std::vector<HootAssetRef> files;
    std::map<std::string, std::string> params;
};
```

---

### Step 5: Implement chip bus and libvgm adapter

Start with:

```text
YM2203
YM2608
```

Later add:

```text
YM2151
YM2413
YM2610
YM3812
AY/SSG
SCC/K051649
SN76496
HuC6280 PSG
PCM/ADPCM chips
```

Add optional chip-write logging:

```text
--dump-writes out.chiplog
```

This will make debugging much easier.

---

### Step 6: Port first driver host

Pick the first driver based on inventory and local packs.

Preferred order:

```text
1. zavas or mistyblue if local Microcabin pack uses it
2. mucom88 if local PC-88 pack uses it
3. mucom2608 if local PC-98 pack is easier to trigger
```

Port with minimal behavioural changes.

Add adapter shims instead of editing the driver deeply.

---

### Step 7: Build `hoot2wav`

CLI shape:

```text
hoot2wav \
  --catalog ./hoot.xml \
  --packs ./packs \
  --entry <entry-id> \
  --track 1 \
  --seconds 180 \
  --samplerate 48000 \
  --out out.wav
```

Optional debug options:

```text
--dump-writes out.chiplog
--dump-events out.events.jsonl
--list-entries
--list-tracks <entry-id>
--driver-trace
```

---

### Step 8: Compare against reference

Reference options:

```text
Original Hoot on Windows -> WAV capture
Existing VGM/S98 rip -> rendered WAV
Emulator capture -> rendered WAV
Known soundtrack recording -> rough listening reference only
```

Minimum checks:

```text
duration roughly correct
no silence
no clipping
tempo correct
pitch roughly correct
track starts correctly
loop/fade behaves predictably
```

Better checks:

```text
chip-write log comparison where possible
RMS/peak comparison
spectrogram inspection
manual AB listening
```

---

## Decision table: port vs replace

| Component | Port from Hoot | Replace / modern source | Notes |
|---|---:|---:|---|
| Driver modules | Yes | No, except known better replayers | Core value of Hoot. |
| Hoot XML semantics | Yes | Parser implementation can change | Preserve catalog meaning. |
| Pack lookup semantics | Yes | ZIP implementation can change | Keep search behaviour. |
| Sound chip cores | Usually no | Prefer libvgm | Keep Hoot core only for mismatches. |
| CPU cores | Maybe | Prefer newer maintained cores if practical | Must preserve driver timing and memory maps. |
| Timers/scheduler | Mostly yes | Modernize API only | High correctness risk. |
| Windows GUI | No | None | Discard. |
| Windows audio output | No | New render callback/WAV writer | Discard. |
| XML parser | No | tinyxml2 or similar | Preserve schema behaviour. |
| ZIP/deflate | No if generic | zlib/miniz/libarchive | Keep only Hoot quirks. |
| Build system | No | CMake | Start clean. |
| Tests | New | New | Required for compatibility. |

---

## Risks and mitigations

### Risk: libvgm core sounds different from Hoot/FMgen

Mitigation:

- Make chip backend selectable.
- Start with libvgm.
- Keep old Hoot/FMgen-based cores available behind a compile flag only if needed.
- Compare against original Hoot output and known VGM/S98 renders.

### Risk: Hoot driver depends on old CPU core quirks

Mitigation:

- First port with original CPU core when easiest.
- Add abstraction layer.
- Later swap to modern core behind the same interface.

### Risk: catalog and pack lookup consume too much time

Mitigation:

- Add reduced local catalog fixtures.
- Add `--driver` and explicit file arguments for early testing.
- Implement full Hoot XML compatibility incrementally.

### Risk: “support everything” becomes too broad

Mitigation:

- Inventory first.
- One driver at a time.
- One verified local test pack per driver.
- Mark drivers as:

```text
not started
compiles
loads
renders silence
renders recognizable music
reference-matched
```

---

## First milestone definition

Milestone 1 is complete when this works:

```text
hoot2wav \
  --catalog ./hoot.xml \
  --packs ./packs \
  --entry <local-pc88-or-pc98-microcabin-entry> \
  --track 1 \
  --seconds 120 \
  --out microcabin_test.wav
```

And the result is:

```text
recognizable music
correct rough tempo
correct rough pitch
no obvious missing channels
no major distortion
```

---

## Second milestone definition

Milestone 2 is complete when:

```text
At least one PC-88/YM2203 entry and one PC-98/YM2608 entry render through the same libhootcore API.
```

The same `hoot2wav` CLI should work for both.

---

## Third milestone definition

Milestone 3 is complete when:

```text
A driver inventory exists and every Hoot driver is classified as:
  A. easy direct port
  B. needs CPU/memory host
  C. needs missing chip/core work
  D. blocked by unknown pack/catalog issue
  E. deferred
```

At that point, “support everything” becomes a queue rather than a vague target.

---

## Open questions to answer during inventory

1. Which local packs are already present?
2. Which catalog entries do those packs satisfy?
3. Which Hoot driver IDs map to Microcabin PC-88/PC-98 games?
4. Which drivers directly generate chip writes versus run guest CPU code?
5. Which Hoot sound cores have no clean libvgm replacement?
6. Which CPU cores are actually needed for the local test packs?
7. Does Hoot use FMgen-specific YM2203/YM2608 behaviour that differs audibly from libvgm defaults?
8. Are loop points represented in Hoot XML, driver code, or only by user convention?
9. Are pack filenames case-sensitive in practice?
10. Can we produce deterministic chip-write logs before generating audio?

---

## Recommended development order summary

```text
1. Inventory local source, catalog and packs.
2. Create modern CMake skeleton.
3. Implement AssetProvider.
4. Implement reduced Hoot XML loader.
5. Implement ChipBus.
6. Wire libvgm for YM2203/YM2608.
7. Port first Microcabin-relevant driver host.
8. Build hoot2wav.
9. Render first PC-88 or PC-98 WAV.
10. Compare against reference.
11. Expand driver-by-driver.
```

