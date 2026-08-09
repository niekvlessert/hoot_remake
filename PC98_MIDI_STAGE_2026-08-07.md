# PC-98 DOS MIDI stage — 2026-08-07

## Scope

Add a generic PC-98 MPU-401 MIDI transport to the existing DOS/V30 replay
host and connect the already proven software MIDI synthesizers to PC-98 output.
The implementation applies to `pc98dos/beep`, `pc98dos/opn`, `pc98dos/opna`
and `pc98dos/86`; the current catalogue uses MIDI on BEEP, OPN and one OPNA
configuration.

This stage deliberately separates **transport support** from **module
synthesis support**. MT-32/CM-64 configurations can now emit and decode their
MIDI byte stream, but still need Munt/mt32emu before they can be rendered
correctly.

## MPU-401 PC-98 transport

A new `Pc98Mpu401` component implements the replay-relevant PC-98 MPU-401
interface:

- default data port `E0D0h` and command/status port `E0D2h`;
- common jumper-address mirrors `C0D0h`, `C8D0h`, `D0D0h`, `D8D0h`, `E0D0h`,
  `E8D0h`, `F0D0h` and `F8D0h`;
- MPU status semantics for RX-empty and TX-full;
- reset command `FFh` and ACK `FEh`;
- UART-mode command `3Fh`;
- firmware-version/revision queries used by detection code;
- bounded transmit buffering/backpressure;
- 31.25 kbit/s MIDI serialization driven from rendered audio time;
- forwarding of output bytes even for conservative intelligent-mode setup,
  while counting unknown intelligent commands for future real-pack hardening;
- the shared running-status/SysEx/channel-message decoder already validated on
  the X68000 MIDI path.

The PC-98 DOS I/O dispatcher now routes these ports to the MPU before ordinary
board I/O. MIDI serialization is advanced during every render chunk and its
messages feed the selected software-synth backend.

## Software synthesis

The existing `[midi]` configuration is now shared by X68000 and PC-98:

- `midiout_type=4` (GS / SC-55): Nuked-SC55 CLAP is preferred, with
  FluidSynth fallback;
- `midiout_type=7` (GS / SC-88): FluidSynth compatibility rendering;
- `midiout_type=8` (GM): FluidSynth;
- `midiout_type=1`, `2`, `3` (MT-32 emulation / MT-32 / CM-64): transport is
  active but synthesis remains deferred until Munt/mt32emu is added.

The rendered synth output is mixed after the PC-98 OPN/OPNA/PCM86/BEEP path,
using the existing `[midi] gain` setting. No FluidSynth library, SoundFont,
Nuked plugin or Roland ROM is bundled.

## FluidSynth lifecycle hardening

The first end-to-end PC-98 DOS -> MPU -> FluidSynth fixture exposed a rare,
non-deterministic process-shutdown crash in dynamically loaded FluidSynth on
the Linux test environment. Unit-testing the transport or synth separately did
not expose it.

On systems that provide `RTLD_NODELETE`, FluidSynth is now loaded with that
flag. `dlclose()` still releases this component's handle, while the shared
object mapping remains resident until process exit. This avoids tearing down
process-global GLib/FluidSynth runtime state underneath late shutdown work.
The fix also benefits X68000 FluidSynth use.

After the change, 50 consecutive end-to-end PC-98 -> FluidSynth test processes
completed without a crash.

## Tests

Two levels of new test coverage were added:

1. `pc98_mpu401_test` exercises reset/ACK, UART mode, status, alternate PC-98
   bases, firmware query, running status and 31.25 kbit/s serialization.
2. `pc98_midi_driver_test` runs a real synthetic 8086 `.COM` program through
   the complete PC-98 DOS/V30 host. The guest writes the physical `E0D2h` and
   `E0D0h` ports, sends a program change plus note-on, and the test verifies the
   resulting MIDI counters. With `HOOT_TEST_SOUNDFONT` set, the same fixture
   also requires active FluidSynth audio.

Final source state:

- Release tests: 12/12;
- ASan + UBSan tests: 12/12;
- 50/50 repeated PC-98 -> FluidSynth integration runs successful;
- X68000 plain OPM regression: 191 audio tracks + 5 controls / 196;
- X68000 PCM8 regression: 50/50;
- X68000 GM/GS regression: 30/30, exactly 91,012 serialized MIDI bytes and
  1,797 note-ons as in the prior baseline;
- NekoEX pure BEEP replay remains 70 music tracks plus one intentional STOP;
- representative NekoEX OPNA, FC98 PC-9801-86 and Outsider PC-9801-86 tracks
  remain audio-active, with PCM86 still exercised on the 86 packs.

## Catalogue impact

The generated runtime catalogue contains 476 PC-98 DOS MIDI configurations and
10,611 expanded runtime tracks:

| MIDI class | Configurations | Runtime tracks | Current backend |
|---|---:|---:|---|
| MT-32 emulation | 103 | 2,430 | transport only |
| MT-32 | 101 | 2,417 | transport only |
| CM-64 | 13 | 337 | transport only |
| GS / SC-55 | 216 | 4,614 | Nuked-SC55 / FluidSynth |
| GS / SC-88 | 17 | 282 | FluidSynth |
| GM | 26 | 531 | FluidSynth |
| **Total** | **476** | **10,611** | |

Therefore 259 configurations / 5,427 runtime tracks are now structurally
software-renderable with the existing backends. The other 217 configurations /
5,184 tracks have their PC-98 MIDI transport in place but still require the
MT-32/CM-64 synth stage.

By PC-98 audio host, MIDI appears on 297 BEEP configurations, 178 OPN
configurations and one OPNA configuration.

These are compatibility candidates derived from the catalogue, not a claim
that all unavailable archives have been auditioned.

## Real-pack status and next validation targets

No PC-98 MIDI archive is present in the current test-pack directory, so this
stage is component-, guest-program- and regression-validated but **not yet
real-pack production-validated**.

High-value next packs are:

- `yumenosi_98.zip`: GS/SC-55 (`MMD` + `MMP_HOOT`) and OPN/OPNA alternatives;
- `charm2_98.zip`: GS/SC-55 with the `MDDRV` family;
- `mjgensk3_98.zip`: both SC-55 and SC-88 catalogue variants;
- `3x3eyes_98.zip`: GM catalogue variant.

These four packs cover several independent PC-98 MIDI driver families and all
currently software-renderable module classes. They should be the real-pack
gate before calling PC-98 MIDI production-ready.

## Remaining work

- Real-pack trace/validation of UART vs intelligent-mode behavior.
- Exact MPU-401 intelligent-mode sequencing and IRQ delivery if a real driver
  demonstrates that it needs them.
- Munt/mt32emu for MT-32 and CM-64.
- Hardware-exact SC-88 remains unavailable; FluidSynth is a compatibility
  backend for that class.
