# PC-9801 Ys II real-pack validation — 2026-08-08

Pack tested: `ys2_98.zip`
Catalogue entry: `ys2-98-opn` (`pc98vx/opn`)

## Root cause

The extracted Falcom player programs YM2203 Timer B and installs its sound IRQ on
PC-98 slave-PIC vector **INT 14h**. The generic host previously delivered FM timer
overflows only to INT 0Bh. Initialization therefore completed and OPN registers
were touched, but the sequencer never received its periodic timer interrupt and
rendered silence.

The fix is generic: FM timer delivery now resolves the active interrupt vector
from the PC-98 master/slave 8259 mask state (vector bases 08h and 10h), while
preserving the existing INT 0Bh and legacy far-pointer fallbacks. There is no
Ys-II-specific playback path.

## Validation

`hootprobe --archive ys2_98 --all-tracks --seconds 2 --startup-grace 0`
was run against the production SQLite/Zstd catalogue and the supplied real pack.
All **49/49 tracks** returned `audio-active` with no unsupported x86 opcodes.
A representative title track produced YM writes/key-ons and non-zero PCM output.

The concrete Ys II entry is therefore reported as `PLAYABLE`; other generic
`pc98vx/opn` entries remain `EXPERIMENTAL` until separately real-pack validated.

## Regression coverage

`pc98_bare_driver_test` now includes a synthetic PC-98VX guest that arms YM2203
Timer B, unmasks slave-PIC bit 4, installs an ISR on INT 14h and only makes its
SSG tone audible from that ISR. This prevents future regressions back to a
hard-coded INT 0Bh timer delivery path. The fixture source is
`tests/fixtures/pc98_pic14_timer.S`.
