# PC-98 3x3EYE'S FM timer correction — 2026-08-09

## Symptom

The PC-9801 OPN version of 3x3EYE'S played approximately three times too fast.
The issue was in the generic PC-98 OPN/OPNA timer host, not in the music pack or
its catalogue `clockmul=8` setting.

## Root cause

The host reset `fm_prescaler_sel_` to selector 0, which corresponds to the OPN
1/2 prescaler path (timer divider 24). YM2203 and YM2608 actually reset to
selector 2, the 1/6 path (divider 72 for YM2203). The same wrong state existed
both in full driver cleanup and shell-runtime reconstruction.

The YM2608 interval calculation also omitted the chip's additional /2 input
stage. Its default timer divider is therefore 144, not 72.

These values are consistent with both implementations already bundled in the
source tree:

- original Hoot uses `72 / clock` for YM2203 and `72 / (clock / 2)` for YM2608;
- libvgm's OPN core resets `prescaler_sel` to 2 and calls its YM2608 prescaler
  path with `pre_divider=2`.

## Fix

- Reset the generic PC-98 FM timer selector to 2 during initial cleanup,
  explicit player reset, and shell-runtime reconstruction.
- Apply the YM2608 timer pre-divider when calculating Timer A/B intervals.
- Keep the existing 2Dh/2Eh/2Fh prescaler-select behavior; no game-specific
  timing override was added.
- Tighten the synthetic PC-98 DOS Timer B regression test for both YM2203 and
  YM2608 to require the expected default-prescaler cadence. The fixture's
  Timer B value 0 yields about 13.54 IRQs per second and roughly 67 OPN data
  writes in its one-second render, instead of roughly 202 with the broken 1/2
  reset state.

## Scope

The correction applies to all timer-driven `pc98dos/opn`, `pc98dos/opna`, and
`pc98dos/86` entries. CPU scheduling and the catalogue `clockmul` option are
unchanged; those control how much guest code can execute between audio frames,
not the hardware Timer A/B period.

## Real-pack verification

The supplied `3x3eyes_98.zip` was used as an external test input and is not
included in the source archive. `M01.DAT` programs YM2203 Timer B to `8Ch`.
At 44.1 kHz the corrected default divider schedules the first music tick at
frame 1,476 (about 29.88 Hz); the broken divider scheduled it at frame 492
(about 89.63 Hz).

A ten-second render of `M01.DAT` produced the same result with 64, 256, 1,024,
and 4,096-frame caller buffers: 4,920 OPN writes, 135 FM key-ons, and zero
unsupported V30 opcodes. This confirms that the corrected tempo is independent
of Web/SDL audio-buffer size. The unmodified source produced 15,442 OPN writes
and 318 FM key-ons over the same ten seconds, directly reproducing the reported
fast playback.
