# PC-9801-86 Real-Pack Validation Stage — 2026-08-07

## Scope

This stage validates the generic `pc98dos/86` implementation against two real Hoot archives rather than synthetic fixtures only:

- `fc98v12.zip` — 52 catalog tracks, EMMDRV startup, 4 MiB `extramsize`, P86DRV/PMD86/PMDPCM86.
- `outsider_98.zip` — 48 catalog entries: 10 music tracks plus 38 `EFFEC.DAT` sound-effect entries, P86DRV/PMD86/PMDPCM86.

The user-supplied archives are test inputs only and are **not included** in the source release.

## Real-driver issues found and fixed

### 1. PC-9801-86 extended OPNA decode gate

Real PMD86 board detection disables the extended YM2608 port pair through the PC-9801-86 function register at `A460h` and expects the disabled `18Ch/18Eh` ports to float high (`FFh`). The synthetic PCM86 fixture did not exercise this probe.

The host now gates the extended OPNA port pair according to the PC-9801-86 latch. This makes PMD86 detect and initialize the board normally.

### 2. DOS TSR memory overlap

The shell loader formerly placed helper programs at fixed 0x400-paragraph intervals. PMD86 remains resident with a footprint of roughly 36 KiB, so later helpers such as PMDPCM86 and PMD_98 could overwrite resident PMD86 code.

The DOS host now:

- records the paragraph count supplied to INT 21h/AH=31 (Terminate and Stay Resident),
- reserves the loaded program image before guest AH=48 allocations,
- advances a conventional-memory high-water mark using the actual resident/image footprint,
- places subsequent shell helpers above all prior DOS allocations.

### 3. Host bridge buffer corrupting P86DRV

The generic PC-98 bridge used a fixed filename buffer at `1000:0174`. P86DRV itself remains resident in segment `1000`, and this host write overwrote its INT 65h special-entry table. The later apparent unsupported x86 opcodes were consequence damage from the corrupted driver, not missing CPU instructions.

The bridge buffer is now allocated from free DOS conventional memory after all resident programs.

### 4. Deterministic PC-9801-86 track switching

Every FC98 track worked in an isolated process, but tracks 24–27 failed in a single long-lived player session because PMD/P86 resident heaps, vectors and board state were reused across song changes.

For `pc98dos/86`, every track selection now rebuilds the DOS resident environment deterministically:

- guest memory / IVT,
- DOS allocator and handles,
- PMD/P86/EMM residents,
- PIT and FM timers,
- YM2608 state,
- PC-9801-86 PCM state,
- bridge scratch allocation.

Track changes are relatively infrequent; correctness is preferred over preserving TSR startup time. After this change FC98 is 52/52 in one sequential player process.

### 5. YM2608 sanitizer cleanup

Two legacy signed left shifts in the embedded libvgm YM2608 mixer were reached by the real packs. They were rewritten as bit-equivalent unsigned shifts so negative two's-complement sample values retain the same result without C undefined behavior.

## Real-pack results

Three-second render window per track, sequentially in a single driver process:

| Entry | Catalog entries | Audio-active | PCM86 used | Unsupported x86 | FIFO overflows |
|---|---:|---:|---:|---:|---:|
| FC98 v1.2 (`fc98v12-86`) | 52 | 52 | 41 | 0 | 0 |
| Outsider (`outsider-98-86`) | 48 | 10 | 10 | 0 | 0 |

The Outsider split is important:

- tracks 0–9 are the 10 music tracks: **10/10 audio-active and 10/10 use PCM86**;
- tracks 10–47 are `EFFEC.DAT` sound-effect entries: 38/38 currently silent in the 86 variant.

Therefore the real music result for these two archives is **62/62 playable music tracks**.

### PCM activity

Across the 3-second sequential scans:

- FC98: 2,581,248 FIFO writes, 2,211,532 FIFO reads, zero FIFO overflows.
- Outsider: 605,440 FIFO writes, 422,860 FIFO reads, zero FIFO overflows.

FC98 has limited 16-bit mix headroom saturation in 9 tracks (782 clipped output samples across 52 × 3 seconds). This is a fidelity/headroom calibration issue, not a driver or FIFO failure. Outsider has zero clipped samples in the same window.

## EMS / `extramsize`

FC98 v1.2 explicitly uses `extramsize=0x400000` and an EMMDRV startup path. All 52 tracks work, so this real EMMDRV/4 MiB startup pattern is now validated.

The catalog still contains five other `pc98dos/86` configurations with extra-memory options that have not been supplied. The capability description therefore remains conservative: arbitrary EMS-backed sample layouts are not claimed as universally validated yet.

## Regression results

### Existing PC-88 / PC-98 real packs

Nine previously validated OPN/OPNA configurations were rechecked:

- 438 catalog tracks total,
- 436 audio-active,
- 2 intentional `SILENCE.FN1 : [STOP]` controls,
- zero unsupported CPU opcodes.

Tarashi2 track 8 has a delayed start and was separately confirmed audio-active with a 10-second startup window.

### X68000

The unrelated X68000 baselines remain intact:

- OPM: 196 catalog commands = 191 audio-active + 5 controls, no failures.
- PCM8: 50/50 audible, zero PCM8 memory faults, zero unsupported channels/functions, zero clipping.
- MIDI GM/GS: 30/30 audible, 91,012 transmitted MIDI bytes, 1,797 note-ons, 868 SysEx messages, zero malformed bytes and zero clipping.

## Test gates

- Release build: 9/9 unit tests pass.
- AddressSanitizer + UndefinedBehaviorSanitizer: 9/9 unit tests pass.
- Real FC98 track 0 and Outsider track 0 pass under ASan/UBSan after the YM2608 cleanup.
- FC98 sequential playback: 52/52.
- Outsider sequential music playback: 10/10.

## Remaining PC-9801-86 work

1. Determine whether the 38 Outsider `EFFEC.DAT` entries should be supported as sound effects or deliberately excluded from music-oriented validation.
2. Validate the other five `extramsize`/EMS PC-9801-86 archives when available.
3. Calibrate OPNA + PCM86 mix headroom against reference output for the small amount of clipping seen in FC98.
4. Broaden the real-pack sample beyond PMD86/P86DRV to other PC-9801-86 driver families.
