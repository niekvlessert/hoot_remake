# Licenses

This port currently contains new scaffolding code plus the unmodified local
Hoot source snapshot kept for provenance.

- New port scaffolding: license undecided.
- Original Hoot source snapshot: see the upstream files in
  `hootsrc20011006/` and any accompanying documentation.
- `third_party/libkss/modules/kmz80`: libkss license is ISC-like; see
  `third_party/libkss/LICENSE.md`.
- `third_party/libvgm/emu/cores/fmopn.c`: source header declares GPL-2.0+.
  Linking this YM2203 path therefore has GPL implications for distributed
  binaries unless the core is replaced with a permissive alternative.
- Local game/music packs: not redistributed by this project unless explicitly
  licensed for that purpose.
