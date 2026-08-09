# PC-88 / PC-98 Generic Replay Stage — 2026-08-07

## Goal

Replace narrow game-specific replay assumptions with reusable Hoot-compatible machine hosts, prioritizing PC-88 OPN/OPNA first and then PC-98 DOS OPN/OPNA.

## Implemented PC-88 generic host

The new `Pc88GenericDriver` is registered for both `pc88/opn` and `pc88/opna`.

Implemented host behavior:

- Z80 execution through the existing CPU abstraction.
- YM2203 for OPN entries and YM2608 for OPNA entries.
- Catalog `init_pc` support.
- Catalog `baseclock` support as the virtual Z80 clock in MHz.
- Variable-size BGM and voice slots; the previous fixed 8 KiB BGM limit is removed.
- Catalog `mdata_addr`, `mdata_size`, `vdata_addr`, and `vdata_size` support.
- Hoot-style host ports for play/track selection and slot loading.
- OPN ports `44h/45h` and PC-88 aliases `A8h/A9h`.
- OPNA bank-1 ports `46h/47h` and aliases `ACh/ADh`.
- Timer A and Timer B interrupt scheduling.
- RTC/VRTC periodic interrupt source when requested by the catalog.
- OPNA ADPCM assets can be loaded into YM2608 Delta-T memory.
- New generic PC-88 trace and IRQ configuration variables, with legacy Xak names retained as fallback only.

A synthetic regression pack intentionally uses a 12 KiB BGM block and a non-default `init_pc`, proving that the old 8 KiB/Microcabin-only assumptions are no longer required.

## Implemented PC-98 DOS generic host improvements

The existing `Pc98DosDriver` has been generalized for both `pc98dos/opn` and `pc98dos/opna`.

Implemented/changed behavior:

- `pc98dos/opn` consistently selects YM2203, including non-shell paths.
- `pc98dos/opna` selects YM2608.
- YM2608 receives the original FMGEN-size 256 KiB Delta-T/ADPCM RAM window for guest uploads.
- Catalog `clockmul` now controls V30 execution pacing instead of a hard-coded multiplier.
- Generic shell-driven entries are now exposed as experimental rather than merely recognized.
- Resident DOS drivers are parked correctly after a host API call.
- Interrupt-vector calls preserve the resident driver's halted state.

The resident-driver fix addresses a real host bug: the old code parked the CPU at offset `00F1h` while leaving the shell program segment in CS, although the HLT trampoline exists in segment zero. This could execute PSP/program garbage after a shell API call.

## Catalog structural coverage

These numbers describe configurations that fit the implemented generic host contract. They are **not reference-audio validation claims**.

### PC-88 OPN + OPNA

- Configurations: 580
- Unique archives: 467
- Catalog tracks: 12,993
- Generic core candidates: 538
- Deferred feature configurations: 42

Deferred feature occurrences can overlap:

- SSG-PCM helper: 9
- PCMx8 helper: 12
- wstimer scheduling: 8
- vstimer scheduling: 1
- GVRAM side effects: 12
- N88-BASIC ROM services: 12

### PC-98 DOS OPN

- Configurations: 1,667
- Unique archives: 1,504
- Catalog tracks: 43,837
- Generic core candidates: 1,447
- Deferred feature configurations: 220

### PC-98 DOS OPNA

- Configurations: 179
- Unique archives: 176
- Catalog tracks: 5,635
- Generic core candidates: 172
- Deferred feature configurations: 7

### PC-98 DOS OPN + OPNA combined

- Configurations: 1,846
- Unique archives: 1,512
- Catalog tracks: 49,472
- Generic core candidates: 1,619
- Deferred feature configurations: 227

Deferred feature occurrences can overlap:

- PC-98 MIDI output/backend: 179
- wstimer scheduling: 34
- dummy sound-ROM behavior: 16

## Validation performed

Synthetic host tests:

- PC-88 YM2203: audible.
- PC-88 YM2608: audible.
- PC-98 DOS YM2203: audible.
- PC-98 DOS YM2608: audible.
- Release unit suite: 8/8.
- AddressSanitizer + UndefinedBehaviorSanitizer unit suite: 8/8.

Existing X68000 regressions remain intact:

- OPM/control: 191 audible + 5 controls.
- PCM8: 50/50 tracks audible in the supplied Asuka/Mad Stalker packs.
- MIDI: 30/30 Asuka GS/GM tracks audible; MIDI counters match the previous baseline exactly.

## Important validation limitation

No real PC-88 or PC-98 music pack ZIP was available in the current workspace. Therefore the catalog counts above are structural compatibility candidates only. Real driver-family validation should now be done with representative packs from several unrelated publishers/drivers rather than only Microcabin.

Recommended first real-pack sweep:

PC-88:

- one MUCOM-family OPN pack;
- one XTALSOFT pack;
- one Falcom or Wolfteam pack;
- one OPNA pack with ADPCM use;
- one pack using RTC/VRTC or non-default `baseclock`.

PC-98 DOS:

- PMD/PMD_98;
- NLP_HOOT;
- USD_98;
- NAX or MDRV;
- one OPNA pack using PCMSET/ADPCM.

## Next compatibility work

Highest-value remaining items:

1. Run broad real-pack sweeps and fix generic host ABI/timing differences by driver family.
2. PC-88 PCMx8 and SSG-PCM helpers.
3. PC-88 N88-ROM/GVRAM compatibility only where real packs demonstrate need.
4. PC-98 wstimer behavior.
5. PC-98 MIDI transport/synthesis reuse from the already implemented X68000 MIDI backend where practical.
6. Then expand to PC-9801-86 PCM and beeper routes.
