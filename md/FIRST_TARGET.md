# First Target

Status: selected and loaded by the first headless driver scaffold.

Game/title: Xak II - Rising of The Redmoon - (PC98)
Machine: PC-98 music data using the PC-88 OPN Microcabin driver path
Catalog entry: `tests/fixtures/cabin98xml.txt`, generated id `xak2-98-opn`
Archive: `xak2_98.zip`
Driver route: `pc88/opn`, with `driveralias` `microcabin/pc88`
Required code assets: `PATCH`, `MMD.COM`, `MMD2.COM`
Chip modules expected next: `ssYM2203`/OPN path first, then voice/data handling as needed
CPU modules expected next: Z80 via the old `RAZE`-backed `mucom88.cpp` route
Reference audio: still needed from original Hoot or a trusted capture
Known risk: this is not a native PC-98 driver in the old source; the companion metadata intentionally reuses the PC-88 Microcabin driver with patch/options.

Implemented so far:

- `xak2-98-opn` validates and loads `xak2_98.zip`.
- Code assets are copied into the old driver RAM layout at their configured offsets.
- BGM assets are copied into 8 KiB slots by Hoot title code.
- Voice assets are loaded by title code for the future Microcabin voice path.
- `hoot_select_track` primes the Microcabin patch handshake: port `0x00` reports a pending play command and port `0x01` returns the selected Hoot track code.
- KMZ80 from libkss is wired for Z80 execution.
- libvgm FMOPN is wired for YM2203 register writes and sample rendering.
- Rendering remains silent as of this checkpoint: the CPU reaches setup OPN writes, but not YM2203 key-on writes yet. The likely remaining issue is the exact Microcabin interrupt/vector and sequencer handoff.

FRAY note:

- `fray-98-opn` is present in `tests/fixtures/cabin98xml.txt`, but the local `fray_98.zip` is not complete for that XML. It lacks `PATCH`, uses `MMD.SYS` instead of `MMD.COM`, and uses lowercase `mmd2.com`. The port now does case-insensitive ZIP lookup, but FRAY still needs a merged/generated local fixture before it can pass asset validation.

Recommended first investigation order:

| Candidate | Machine | Driver file | Main chip modules | Notes |
|---|---|---|---|---|
| Microcabin Xak II PC-98 | PC-98 via PC-88 OPN driver | `hootsrc20011006/drivers/mucom88.cpp` | `ssYM2203` | Selected; local `xak2_98.zip` validates against `cabin98xml.txt`. |
| Mucom88 | PC-88 | `hootsrc20011006/drivers/mucom88.cpp` | `ssYM2203` | Present, Z80/RAZE dependency, likely useful for PC-88 fixtures. |
| Mucom2608 | PC-98 | `hootsrc20011006/drivers/mucom2608.cpp` | `ssYM2608` | Present, likely relevant for PC-98 material. |
| Zavas | PC-88/98 candidate | `hootsrc20011006/drivers/zavas.cpp` | inspect next | Microcabin-related name in source tree. |
| Misty Blue | PC-98 candidate | `hootsrc20011006/drivers/mistyblue.cpp` | inspect next | Microcabin-related candidate in source tree. |

Open items before locking target:

- Match local pack contents to catalog entries.
- Confirm exact required asset filenames and hashes.
- Capture reference output from original Hoot or another trusted replay path.
- List CPU core requirements for the chosen driver.
