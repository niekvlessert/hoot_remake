# Source Inventory

Canonical source snapshot:

- `third_party/hoot-original/`
- Original extracted folder kept in place: `hootsrc20011006/`
- Source archive hash recorded in `SOURCE_HASHES.txt`

Counts:

- 171 files under `hootsrc20011006/`

Windows-only or GUI-facing files:

- `hoot.cpp`, `hoot.h`, `MainFrm.cpp`, `MainFrm.h`
- `hoot.rc`, `hootjpn.rc`, `resource.h`, `resource_jp.h`, `res/*`
- `StdAfx.cpp`, `StdAfx.h`
- `ms/ddutil.cpp`, `ms/ddutil.h`
- `ssSoundDriverManager.cpp` currently owns DirectSound, WinMM timer, MFC thread, and display concerns.
- `ssConfigLoader.cpp` uses MSXML COM in the original tree.

Driver modules:

- `angelus.cpp`
- `ds4.cpp`
- `ed2.cpp`
- `firehawk.cpp`
- `hes.cpp`
- `kss.cpp`
- `midgarts.cpp`
- `mistyblue.cpp`
- `mucom2608.cpp`
- `mucom88.cpp`
- `mucomx1.cpp`
- `mxdrv.cpp`
- `revolter.cpp`
- `scc.cpp`
- `scheme.cpp`
- `silpheed.cpp`
- `snatcher.cpp`
- `starcruiser.cpp`
- `starship.cpp`
- `tnmbox.cpp`
- `trpscr.cpp`
- `x68k.cpp`
- `xtalsoft.cpp`
- `zavas.cpp`

Sound chip modules:

- `ssYM2203`, `ssYM2608`, `ssYM2151`, `ssYM2413`, `ssYM2610`, `ssYM3438`, `ssYM3812`
- `ssAY8910`, `ssSN76496`, `ss051649`
- `ssADPCM`, `ssMSM6258V`, `ssMSM6295`, `ssPCM8`
- `ss005289`, `ss007232`, `ss053260`, `ss054539`
- `ssC30`, `ssC140`, `ssPCEPSG`, `ssQSound`, `ssSEGAPCM`, `ssSEGAPCM2`
- support/mixing helpers: `ssFMTimer`, `ssMemoryDump`, `ssMono2151`, `ssStereo2151`, `ssTaito2610`

CPU dependencies:

- Z80 via `RAZE/` references in drivers such as `mucom88.cpp`
- MAME CPU interface stubs under `mame/cpu/`
- Headers for `m6800`, `m6809`, `m68000`, `h6280`

Config/catalog files:

- `hootsrc20011006/hoot.xml`
- `hootsrc20011006/hoot.ini`
- `hootsrc20011006/config.xml` is referenced by original code but not present in this snapshot.
- `hootsrc20011006/hoot.xsl`
- `hootsrc20011006/drivers.xsl`

Port boundary established in this pass:

- New code lives under `src/` and `tools/`.
- Original source snapshot remains unmodified.
- The old GUI/audio-device manager is not compiled into `hootcore`.
