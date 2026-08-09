# PC-98 MIDI real-pack validation stage — 2026-08-07

> **Status update (2026-08-07):** MT-32 synthesis described here as deferred/transport-only is superseded by `MUNT_MT32_STAGE_2026-08-07.md`. CM-64 now has Munt CM-32L/LA synthesis, but its separate CM-32P section remains unimplemented.

## Scope

This stage continues `PC98_MIDI_STAGE_2026-08-07.md` by taking the generic
PC-98 MPU-401 transport out of synthetic-fixture-only status and validating it
against four real Hoot archives:

- `3x3eyes_98.zip` — GM, Nihon Create `NC` / `NC_98` family;
- `charm2_98.zip` — GS/SC-55, `MDDRV` family;
- `mjgensk3_98.zip` — both GS/SC-55 and GS/SC-88 catalogue variants, `MMD` family;
- `yumenosi_98.zip` — GS/SC-55, `MMD` + `MMP_HOOT` family.

The goal was not to add pack-specific MIDI parsing. The real packs were used to
find missing PC-98 DOS, MPU-401 intelligent-mode and interrupt-host behavior,
then fix those mechanisms generically.

No copyrighted pack, SoundFont, Roland ROM or Nuked-SC55 plugin is bundled in
the project.

## Result

The real-pack gate now passes for the supplied GM/GS material:

- 93/93 actual music tracks eventually emit note-on data;
- the two additional Yume no Sei entries (`GS_RESET.MMD` and `GMINIT.MMD`) emit
  their expected initialization MIDI but are not songs;
- the 3x3EYE'S `0xFE` title is a Hoot control entry (`loop exit`), not a BGM
  slot, and is intentionally excluded from the music count;
- all 95 loadable song/init entries complete the three-second transport smoke
  run with `unsupported=0` and `malformed=0`;
- the few tracks without a note-on in the first three seconds were checked for
  15 seconds and do become note-active;
- representative 20-second FluidSynth renders are audio-active for every
  supplied driver/module family.

This is enough to call the PC-98 GM/GS transport real-pack validated for the
`NC`, `MDDRV` and `MMD` families represented here. It is not a claim that every
one of the 476 catalogue MIDI configurations has been auditioned.

## Real-pack failures found and generic fixes

### 1. PC-98 MPU-401 intelligent-mode clock was missing

`MMD.COM` successfully detected and reset the MPU, but then configured
intelligent mode and waited for host-clock events. The previous implementation
ACKed conservative intelligent-mode setup commands but never generated the
`FDh` clock-to-host byte or an MPU IRQ. Consequently `MMP_HOOT` could install
and call MMD but playback never advanced.

`Pc98Mpu401` now implements the replay-relevant intelligent-mode subset:

- `C2h..C8h` timebase selection;
- `E0h` tempo parameter;
- `E7h` clock-to-host interval parameter;
- `94h` / `95h` clock-to-host off/on;
- transport/play control through `00h..2Fh`;
- `D0h..D7h` Write System Data / direct channel-message paths used by real
  residents;
- `DFh` direct system/SysEx transfer;
- `FDh` clock-to-host events;
- IRQ-pending state tied to unread host-clock responses;
- time advancement both from rendered audio frames and from guest CPU time
  while synchronous setup code is executing.

The existing UART path and 31.25 kbit/s serialization remain in place.

### 2. PC-98 PIC state and MPU IRQ routing were missing

Real MMD code probes candidate MPU interrupt lines by changing PC-98 PIC masks.
The DOS host previously treated the relevant ports as generic I/O and therefore
could not reproduce the probe.

The host now stores the master/slave PIC masks at ports `02h` and `0Ah`, finds
an active unmasked vector for a pending MPU IRQ, remembers the selected line,
and delivers the interrupt through the normal V30 interrupt-vector machinery.

### 3. BEEP MIDI residents need INT 08h timer service

The `NC` and `MDDRV` families install their playback ISR on INT `08h` rather
than using the Hoot helper vector as their sequencer clock. The helper loads or
selects the song, while the resident timer advances playback.

For PC-98 BEEP+MIDI entries, the host now services an installed INT `08h` at
the existing legacy 60 Hz cadence. The pre-install INT `08h` vector is also
initialized to an inert `IRET`, because TSRs save and may chain the previous
handler.

### 4. Guest-time MPU progress is required during synchronous setup

MMD can fill the bounded MPU transmit FIFO while still executing its resident
setup/API call. If the emulator runs a huge uninterrupted guest instruction
batch, MIDI serialization never gets a chance to drain the FIFO and the guest
waits forever for TX-ready.

`run_cpu_steps()` therefore uses bounded 20,000-step quanta while a MIDI shell
bridge/setup path is active. Between quanta it advances MPU time, drains the
31.25 kbit/s transport and services an MPU IRQ if one became pending.

This is a scheduling fix, not an enlarged or unbounded MIDI FIFO.

### 5. Hoot bridge stdin semantics differ by helper family

The first implementation treated the selected `conin` asset as file contents
for all shell helpers. That is correct for `MMP_HOOT`, which asks MMD for a
buffer and streams the song bytes there.

It is wrong for `NC_98` and `MDDRV_98`: those helpers read stdin as a DOS
**filename** and pass that name to a resident API, after which the resident
opens the file itself.

The bridge now distinguishes these cases:

- `MMP_HOOT`: stdin contains the selected song body;
- `NC_98` / `MDDRV_98`: stdin contains the selected basename.

No per-song exception is involved.

### 6. DOS DTA / FindFirst behavior was required by NC

`NC.COM` does not use FindFirst only as an existence check. It calls DOS
`AH=1Ah` to select a DTA, then `AH=4Eh`, and reads the 32-bit file size from the
returned DTA before allocating/loading the song.

The PC-98 DOS shim now implements:

- DTA selection through `AH=1Ah`;
- `AH=4Eh` FindFirst over the in-memory archive files;
- normal-file attribute;
- 32-bit size at DTA offset `1Ah`;
- an ASCIIZ basename at DTA offset `1Eh`.

This is what made the NC family able to load real MIDI files rather than merely
open the helper executable.

### 7. VRTC edge polling was required by a real Hoot helper

A PC-98 helper waits for a clear/set VRTC transition while loading. The
audio-only replay host has no video subsystem, so port `A0h` now supplies a
minimal alternating bit-5 VRTC phase sufficient for that synchronization.

### 8. The foreground HLT park trampoline was incorrect

This was the most serious stability bug exposed by Yume no Sei.

The old park address contained only a single `HLT` byte. On x86, IP has already
advanced past `HLT` when an interrupt is taken from the halted state. The MPU
ISR therefore `IRET`ed to `0000:00F2` rather than back to the `HLT` byte at
`0000:00F1`. The CPU then executed the interrupt-vector table as machine code.
One accidental IVT instruction sequence eventually wrote over resident MMD
code, after which playback jumped into the song buffer and produced unsupported
opcodes/corruption.

The park code is now:

```text
00F1: HLT
00F2: JMP SHORT 00F1
```

Thus any interrupt return resumes on the short jump and immediately parks on
`HLT` again. With this fix Yume no Sei runs cleanly instead of corrupting MMD
after roughly 5.5 seconds.

### 9. Large synchronous resident API calls were truncated

After all other fixes, 3x3EYE'S `M13.MID` (37,534 bytes, 19 valid `MTrk`
chunks) still emitted only NC's 480 controller-reset messages and no notes.
The song itself was valid and NC supports the 19-track header.

Trace comparison showed that the host parked the foreground CPU while NC was
still inside its `INT 42h` song-load call, scanning for the final `MTrk`
chunks. The bridge had a hard 500,000-instruction ceiling and treated reaching
that ceiling like an API return.

The synchronous shell API ceiling is now 5,000,000 instructions. The existing
interrupt trampoline makes `run_cpu_steps()` stop naturally as soon as the
resident `IRET`s, so normal calls do not consume the full ceiling. The larger
number is only a safety bound for genuinely long synchronous calls.

After this fix `M13.MID` is audio-active. A 20-second FluidSynth run produced:

- 1,805 MIDI bytes transmitted;
- 601 channel messages;
- 12 note-ons / 6 note-offs;
- 0 malformed MIDI bytes;
- 0 unsupported V30 opcodes;
- non-zero rendered audio.

## MIDI diagnostics

`hoot2wav --verbose` now prints the MIDI diagnostics already exposed by the
core track-info structure:

- module type;
- backend kind and active state;
- enqueued/transmitted bytes;
- channel messages and SysEx count;
- note-on, note-off, CC, program-change and pitch-bend counters;
- running-status and malformed-byte counters;
- current/peak FIFO occupancy;
- MIDI IRQ count;
- rendered synth frames;
- BEEP audible/rendered frame counters.

This made the real-pack gate deterministic without requiring waveform-only
inspection.

## Real-pack matrix

Three-second transport-only runs used a fresh `hoot2wav` process per track and
software synthesis disabled. The purpose of this pass was guest/transport
correctness rather than SoundFont quality.

| Catalogue entry | Runs | Loadable/clean | Note-active within 3 s | Notes |
|---|---:|---:|---:|---|
| `3x3eyes-98-beep` | 16 | 15/15 | 14 | track 15 is control code `0xFE`; M15 starts later |
| `charm2-98-beep` | 15 | 15/15 | 15 | all active |
| `mjgensk3-98-beep` | 24 | 24/24 | 22 | tracks 0 and 3 have longer intros |
| `mjgensk3-98-beep-2` | 24 | 24/24 | 22 | same SC-88 catalogue material |
| `yumenosi-98-beep` | 17 | 17/17 | 15 | two entries are reset/init assets |
| **Total** | **96** | **95/95 loadable entries clean** | **88** | one non-BGM control entry |

`clean` means both `unsupported=0` and MIDI `malformed=0`.

The five musical entries without a note-on inside three seconds were rerun for
15 seconds:

| Entry / track | Note-ons in 15 s |
|---|---:|
| 3x3EYE'S M15 | 1 |
| Mahjong M55 track 0 | 130 |
| Mahjong M55 track 3 | 93 |
| Mahjong M88 track 0 | 130 |
| Mahjong M88 track 3 | 93 |

Together with the fixed M13, this gives **93/93 actual music tracks eventually
note-active**.

The complete machine-readable matrix is stored in
`PC98_MIDI_REALPACK_VALIDATION_2026-08-07.json`.

## Representative software-synth validation

A local TimGM6mb SoundFont was used only as a readily available FluidSynth
fixture. It is not bundled and is not meant to be an exact SC-55/SC-88 sound
source.

Twenty-second representative runs:

| Entry | MIDI class | TX bytes | Note on/off | MPU IRQ | Audio peak / RMS |
|---|---|---:|---:|---:|---:|
| 3x3EYE'S M13 | GM | 1,805 | 12 / 6 | 0 | 2028 / 183.949 |
| Charm2 C001 | SC-55 class | 1,233 | 164 / 200 | 0 | 15182 / 3484.06 |
| Mahjong MF3_02.M55 | SC-55 class | 3,219 | 305 / 300 | 1,601 | 7970 / 1351.93 |
| Mahjong MF3_02.M88 | SC-88 class | 3,219 | 305 / 300 | 1,601 | 7970 / 1351.93 |
| Yume no Sei YUME_1 | SC-55 class | 3,711 | 368 / 363 | 2,000 | 15917 / 1957.67 |

Every representative run had `unsupported=0`, `malformed=0`, an active synth
backend and 882,000 rendered synth frames.

### Expected FluidSynth compatibility warnings

Charm2 and the SC-88 Mahjong variant request banks that are not present in the
generic test SoundFont, so FluidSynth substitutes instruments. That is a
SoundFont/module-fidelity issue, not a transport failure.

Mahjong also emits one Roland DT1 message for address `40 01 3F` with checksum
`00`. FluidSynth correctly reports that the checksum should be `80` and drops
that message. A PC-98 I/O trace shows the original guest MMD driver itself
writing the exact invalid sequence through MPU direct-system mode:

```text
F0 41 10 42 12 40 01 3F 00 00 F7
```

Therefore the emulated MPU/transport is not corrupting this SysEx. It is left
unchanged, as a replay host should not silently rewrite guest MIDI data.

## Automated tests

Final source state after all real-pack fixes:

- Release CTest: **12/12 passed**;
- ASan + UBSan CTest: **12/12 passed** (`ASAN_OPTIONS=detect_leaks=0`);
- `pc98_mpu401_test` now covers both UART and the intelligent-mode subset:
  timebase/tempo, clock-to-host, `FDh` IRQ event and `D0h` direct send;
- `pc98_midi_driver_test` still passes the synthetic real-V30 `.COM` -> physical
  PC-98 MPU ports -> MIDI decoder -> optional FluidSynth path.

The prior X68000/PC-98 non-MIDI unit tests remain part of the same 12-test
suite, so the PC-98 changes did not require disabling existing test coverage.

## Source files changed from the previous PC-98 MIDI stage

Runtime / host:

- `src/sound/pc98_mpu401.h`
- `src/sound/pc98_mpu401.cpp`
- `src/drivers/pc98_dos_driver.h`
- `src/drivers/pc98_dos_driver.cpp`

Diagnostics / tests:

- `tools/hoot2wav/main.cpp`
- `tests/pc98_mpu401_test.cpp`

Documentation / results added by this stage:

- `md/PC98_MIDI_REALPACK_STAGE_2026-08-07.md`
- `md/PC98_MIDI_REALPACK_VALIDATION_2026-08-07.json`

## Production assessment

For the real packs supplied in this stage, the PC-98 GM/GS transport is now
usable rather than merely structurally present. Three distinct resident-driver
families (`NC`, `MDDRV`, `MMD`) execute their genuine DOS/MPU code and produce
stable MIDI streams.

The remaining catalogue caveats are unchanged in principle:

1. MT-32/CM-64 configurations still have transport only and need Munt/mt32emu.
2. SC-88 still uses FluidSynth as a compatibility backend; hardware-exact
   SC-88 synthesis has not been implemented.
3. SC-55 quality depends on Nuked-SC55 being available, otherwise FluidSynth is
   the fallback and the chosen SoundFont determines fidelity.
4. The four archives are a strong real-pack gate, but do not prove every other
   PC-98 MIDI driver family in the catalogue.
5. Full 8259/PIC command/EOI semantics are still simplified; add more only if a
   new real resident demonstrates a dependency that the current mask/IRQ model
   cannot satisfy.

## Recommended next work

The highest-value next stage is **Munt/mt32emu integration for PC-98 and the
already shared X68000 MIDI path**. The catalogue has 217 MT-32/CM-64
configurations / 5,184 runtime tracks whose transport already exists but which
still lack correct synthesis.

After Munt, use several real MT-32 packs from independent PC-98 driver families
as the same type of production gate used here. Do not expand intelligent-mode
emulation speculatively unless one of those real drivers proves additional MPU
sequencer commands or IRQ semantics are required.
