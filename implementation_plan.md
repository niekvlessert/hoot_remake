# Support PC-9801 Audio Playback

This plan tracks the work needed for full PC-9801 sound playback. PC-98 DOS
catalog entries can now be parsed, loaded, and connected to a YM2608 wrapper,
but complete playback still depends on finishing the DOS/V30 or x86 execution
layer that runs PMD, MMD, and similar sound drivers.

## Current Status

### Completed

- [x] **YM2608 Sound Chip Wrapper** (`libvgm_ym2608.h/cpp`)
  - OPNA output through libvgm.
  - FM, SSG/PSG, ADPCM, and rhythm-facing register paths are available.
  - PC-98 OPNA ports are routed through the driver I/O callbacks.

- [x] **PC-98 DOS Driver Scaffold** (`pc98_dos_driver.h/cpp`)
  - Loads PMD/MMD-style driver binaries and catalog-referenced data files.
  - Sets up PC-98 memory, interrupt vectors, PIT placeholders, and YM2608 I/O.
  - Selects catalog tracks and resolves referenced BGM/voice slots.

- [x] **Microcabin PC-98 DOS Catalog Handling**
  - `mmd98`/Microcabin entries validate and resolve MMD assets.
  - Playback is still blocked on a real DOS/V30 host layer.

- [x] **Hoot Context Integration**
  - `pc98dos/opn` entries route to the PC-98 DOS driver path.
  - Microcabin PC-98 DOS entries keep their specialized loader.

### In Progress

- [ ] **Option B: Minimal x86 Interpreter** (`x86_cpu.h/cpp`)
  - Partly implemented and wired into `Pc98DosDriver`.
  - Provides registers, segmented memory, callbacks, interrupts, stack helpers,
    8086 ModRM effective-address decoding, basic control flow, I/O, and a
    growing instruction dispatcher.
  - Added segment override handling, `F6/F7` TEST/NEG/NOT support, TEST
    r/m forms, and basic string instructions (`MOVS`, `STOS`, `LODS`, `CMPS`,
    `SCAS`) with `REP`/`REPNE`, enough for the Microcabin `MMD.SYS` data-copy
    path to execute further.
  - `Pc98DosDriver` now performs bounded CPU warm-up on track selection and
    bounded CPU stepping during audio render so this path is exercised.
  - Microcabin `MMD.SYS` now reaches OPNA register writes and YM2608 key-ons
    during Fray render tests.
  - Added more 8086 coverage found in Fray/MMD hot paths: `ADC`/`SBB`,
    immediate and CL/1-count shifts (`C0/C1/D0-D3`), unsigned `MUL`/`DIV`,
    `PUSHF`/`POPF`, `LOOP`/`LOOPE`/`LOOPNE`/`JCXZ`, and immediate `TEST`
    (`A8/A9`).

- [ ] **Microcabin MMD.SYS Runtime**
  - Added a minimal DOS device-init request, PC-98 OPNA port readback mirror,
    OPNA Timer A-paced playback through MMD's full resident timer ISR wrapper
    (`MMD.SYS:0376`), derived from the MMD-programmed `0x24/0x25` timer
    registers, and fallback resident callback/buffer seeding for incomplete
    DOS device-init state.
  - Current Fray status: `fray-98-opn` track 8 detects/uses OPNA ports
    (`0x188/0x18a`) and can load BGM/voice buffers with sane fallback sizes
    (`3072`/`2048`). `AH=01` play completes with a larger bounded CPU budget,
    `[19be]` is seeded when the partial device init leaves it unset, and the
    OPN sequence engine at `MMD.SYS:0628` now produces YM2608 key-ons and
    nonzero WAV output. Fray's init programs Timer A to `0x2ff`, which is
    about `215.824 Hz` with the current PC-98 YM2608 cadence, with MMD's
    internal `0x40` sequencer phase advancing note counters at about
    `53.956 Hz`. `HOOT_MMD_TIMER_HZ` can override the host timer rate for
    reference matching without rebuilding.
  - Fixed the YM2608 wrapper to pass through all four OPNA ports (`0..3`)
    instead of collapsing them to port `0/1`, so port-1 FM/ADPCM register
    writes reach channels 4-6 and the extended OPNA bank.
  - Replaced chunk-local PSG DC subtraction with a persistent DC blocker; the
    old per-buffer average removal could cancel slow PSG square/noise parts
    because MMD renders in small timer-sized chunks.

### Not Yet Complete

- [ ] **Full DOS/V30 Runtime Behavior**
  - DOS interrupt handling is minimal.
  - PIT/PIC/timer behavior is still skeletal.
  - PMD/MMD driver command entrypoints and timing need to be exercised against
    real packs.
  - Microcabin MMD now has partial `INT D2` host behavior and Fray OPNA output,
    but the DOS device-init model still relies on targeted fallback state.

- [ ] **Legacy `V30Cpu` Wrapper**
  - `V30Cpu::execute()` remains a stub.
  - This path is not the active playback route while Option B is being built.

## Next Steps for Full Implementation

1. **Option A: Create 86Box CPU stubs**
   - Integrate the 86Box V20/V30 CPU core by stubbing or adapting its required
     memory, I/O, PIC, PIT, timer, and machine infrastructure.
   - Highest accuracy, but significant integration work.

2. **Option B: Implement a minimal x86 interpreter**
   - Continue the partially implemented `X86Cpu` route.
   - Add the missing instructions, addressing modes, flags, interrupts, and
     timing needed by PMD/MMD drivers.
   - Immediate next target: trace `MMD.SYS` commands `AH=10`, `AH=11`,
     `AH=01`, and periodic `AH=06/AL=08` for Fray to find why sequence data
     is loaded without producing YM2608 key-ons.
   - Next concrete Fray target: finish the DOS device-init/runtime path that
     sets `MMD.SYS` resident output callbacks, especially `[19be]`, instead of
     relying on fallback buffer seeding.
   - Current recommended path because it is already started and wired into
     `Pc98DosDriver`.

3. **Option C: Use the YM2608 directly with state machines for common driver patterns**
   - Bypass full CPU emulation for narrowly understood driver formats.
   - Potentially faster for specific games, but less general and more brittle
     than a working CPU/DOS runtime.

## Verification Plan

### Automated Checks

- Build with:
  ```bash
  cmake --build build
  ```
- Confirm X68k playback still resolves quickly and starts:
  ```bash
  build/hootplay --catalog packs/hoot20251231/hoot.xml packs/czarek/hoot/x68k/ad68snd.zip
  ```

### Manual PC-98 Verification

- Test a PMD/MMD PC-98 pack once the CPU/DOS path advances:
  ```bash
  build/hootplay --catalog packs/hoot20251231/hoot.xml packs/czarek/hoot/pc98/yumenosi_98.zip
  ```
- Success means audible YM2608 playback with FM, SSG, ADPCM/rhythm as applicable,
  plus working track selection, pause/resume, and stable timing.
