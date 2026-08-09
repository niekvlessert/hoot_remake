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

## Optional Nuked-SC55 CLAP runtime backend

Hoot contains a small CLAP 1.x host adapter but does **not** include Nuked-SC55,
its CLAP plug-in binary, or any Roland ROM image. CLAP ABI definitions used by
that adapter are derived from the MIT-licensed CLAP headers (Copyright
Alexandre Bique and contributors).

Nuked-SC55-CLAP is a separately installed optional runtime component and is
licensed by its upstream project under the original MAME-style terms. Upstream
explicitly restricts commercial SC-55 emulation hardware, commercial music
production, and inclusion of the plug-in in commercial software packages.
Users must review and comply with that upstream license themselves. Original
Roland firmware/wave ROM dumps are external user-provided resources and are
never distributed by this project.

## Optional Munt / libmt32emu runtime backend

Hoot dynamically loads Munt's `libmt32emu` C API at runtime and does **not**
vendor or redistribute Munt, Roland control ROMs, Roland PCM ROMs, or derived
firmware data. Munt/mt32emu is an external LGPL-2.1-or-later project; users who
distribute a libmt32emu binary must comply with its upstream license. Roland
ROM images remain user-provided resources. Hoot keeps MT-32 and CM-32L ROM
selection separate because their behavior is not interchangeable for every
title.

## CM-32P compatibility renderer

Hoot's built-in CM-32P renderer does **not** contain Roland firmware, PCM data
or instrument ROM content. The PCM ROM layout, address/data descrambling,
sample/tone table interpretation and Roland PCM-chip behavior were implemented
from public hardware documentation and the BSD-3-Clause MAME CM-32P / Roland LP
source by Valley Bell and MAMEdev contributors. Hoot does not vendor those MAME
source files or device framework; the relevant algorithms and hardware mappings
were independently integrated into Hoot's MIDI-synth abstraction. Preserve the
upstream BSD-3-Clause attribution when redistributing derived source. Reference:
`https://github.com/mamedev/mame/blob/master/src/mame/roland/roland_cm32p.cpp`
and `https://github.com/mamedev/mame/blob/master/src/devices/sound/roland_lp.cpp`.

Original Roland CM-32P and SN-U110 ROM images are copyrighted external
user-provided resources and are never distributed by this project.
