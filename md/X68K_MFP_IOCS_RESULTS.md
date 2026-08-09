# X68000 MFP/IOCS/FLOAT2 completion

## Final result

The supplied MFP/IOCS matrix passes 483 of 483 catalog tracks:

- Shooting: 9/9;
- Neural Gear: 17/17;
- Namco Video Game Music Library: 443/443;
- A-Train II: 14/14.

The gate renders one measured second after up to five seconds of startup grace,
checks every track, rejects load/render errors, unsupported CPU opcodes,
unexpected warnings and excessive clipping, and writes a JSON report.

```sh
./tools/run_x68k_mfp_smoke.py --packs packs \
  --output x68k-mfp-iocs-smoke.json
```

## Implemented generic behavior

- IOCS `_OPMINTST` installs the callback as X68000 vector `$43`.
- IOCS `_B_INTVCS` distinguishes hardware vectors `$000-$0ff` from software
  IOCS vectors `$100-$1ff` and updates the guest dispatch table.
- Human68k line-F `$ff09` (`_PRINT`) is accepted as a silent console call so
  resident drivers can complete installation in a headless host.
- Original Hoot's writable dummy-page behavior is retained for the final
  64 KiB of the 24-bit address space, providing wrapped compatibility
  workspace.
- Trace mode records the recent PC sequence and exception stack the first time
  execution enters the generic default exception handler.

## A-Train II root cause

The apparent FLOAT2/ROM exception was a secondary symptom. A2.X contains
compiler-generated routines called by the bootstrap with `A6=0`. Their local
variables use `-8(A6)` and `-4(A6)`, which become addresses `0xfffff8` and
`0xfffffc` on a 24-bit 68000. The old host returned zero for every read and
discarded every write there. Its initialization loops therefore never
converged, while timer interrupts continued to fire.

No pack-specific branch was needed. Preserving those wrapped writes allows the
normal A2.X, FLOAT2.X, OPMDRV, IOCS and MFP code paths to complete. Pending MFP
sources that still point at the bootstrap's default sink are individually
masked and returned through a compatibility-page RTE trampoline.

## Evidence boundary

This is runtime smoke coverage, not sample-exact verification. The entries stay
`experimental` until compared with trusted original-Hoot renders. MIDI-only
variants still require a synthesizer backend, and PCM8 remains a separate
compatibility target.
