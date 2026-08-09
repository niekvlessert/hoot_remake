# PC-98 DOS BEEP stage — 2026-08-07

## Scope

Add a real PC-98 PIT-speaker path to the generic DOS/V30 replay host used by
`pc98dos/beep`, validate it with a real PMDB pack, and preserve all existing
PC-88, PC-98 OPN/OPNA/86, and X68000 replay paths.

## Implementation

A dedicated `Pc98Beep` device now models the audio-relevant PC-98 timer and
system-PPI behavior:

- PIT channel 1 data at `73h` and the `3FDBh` alias;
- PIT control at `77h` and the `3FDFh` alias;
- 8255 system PPI Port C and bit set/reset commands at `35h`/`37h`;
- PC3 buzzer inhibit semantics (`PC3=0` enables the buzzer);
- 16-bit PIT divisor programming including the hardware zero-as-65536 rule;
- PC-98 5/10 MHz-family PIT clock of 2.4576 MHz for the BEEP host;
- deterministic square-wave rendering into the existing signed-16 stereo mix;
- configurable gain through `HOOT_PC98_BEEP_GAIN` / `[pc98] beep_gain`.

The real NekoEX `PMDB.COM` also established the playback scheduler contract:
it installs its music ISR on INT `0Ah`. The generic PC-98 render loop now
feeds that interrupt at the existing 60 Hz coarse/VRTC cadence for the BEEP
host. This is deliberately separate from YM Timer A/B emulation used by OPN
and OPNA drivers.

## Diagnostics

`HootTrackInfo` and `hootprobe` now expose a `beep` diagnostic object with:

- PIT data/control writes;
- PPI writes;
- gate and divisor changes;
- rendered/audible frames;
- INT 0Ah/VRTC deliveries;
- current divisor, frequency, enable state, PIT mode;
- minimum and maximum programmed divisors.

## Real-pack validation

`nekoex_98.zip`, entry `nekoex-98-beep`, was tested in two modes at 44.1 kHz:

1. every track from a fresh process;
2. all tracks sequentially in one player instance.

At a four-second render window both modes produce:

- 70/70 music tracks audio-active;
- one intentional `SILENCE.FN1 : [STOP]` control silent;
- zero unsupported x86 opcodes;
- zero clipped output samples.

`RPG011.FN1` contains roughly two seconds of leading silence, which is why a
shorter two-second smoke test initially classified it as silent. It becomes
normally active in the four-second validation.

## Catalogue impact

The current catalogue contains 384 `pc98dos/beep` configurations in 277 unique
archives, representing 11,158 catalogue tracks.

- 87 configurations have no currently deferred MIDI/wstimer requirement and
  are direct generic BEEP-host candidates.
- 297 configurations request PC-98 MIDI output/synthesis. The PIT speaker host
  now exists for them, but their external MIDI music remains deferred until a
  PC-98 MIDI transport/backend is added.
- Five entries request `wstimer`; these overlap the deferred set and remain
  scheduling-specific validation targets.

These counts are structural compatibility candidates, not a claim that all
unavailable packs have been auditioned.

## Regression status

Final source state:

- Release unit tests: 10/10;
- ASan + UBSan unit tests: 10/10;
- NekoEX BEEP full playlist under ASan/UBSan: clean;
- previous PC-88/PC-98 real-pack matrix: 436 audio-active + 2 intentional STOP
  controls / 438, zero unsupported opcodes;
- FC98 v1.2 and Outsider representative PC-9801-86 tracks remain audio-active
  and continue to exercise PCM86;
- X68000 OPM: 191 audio + 5 controls / 196;
- X68000 PCM8: 50/50 audio-active;
- X68000 MIDI: 30/30 audio-active.

## Remaining BEEP work

The largest remaining blocker is not the speaker device. Most unvalidated BEEP
catalogue variants combine the PC-98 speaker/timer host with external MIDI.
A PC-98 MIDI transport feeding the already existing GM/GS/SC-55 synth backend
would therefore unlock substantially more of this catalogue family than adding
more speaker-specific heuristics.
