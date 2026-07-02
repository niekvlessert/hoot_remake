# Pack Inventory

Local pack/test material found at project root:

| File | Notes |
|---|---|
| `fray_98.zip` | PC-98 Microcabin Fray material; contains `MMD.SYS`, `mmd2.com`, `FRAY.BIN`, and multiple `.BGM` files. |
| `xak_98.zip` | PC-98 Microcabin Xak material; contains `MMD2.SYS`, `mmd2.com`, multiple `.BIN` and `.BGM` files, including nested `DEMO/` and `KAX/` paths. |
| `xak2_98.zip` | PC-98 Microcabin Xak II material; contains `MMD.COM`, `MMD2.COM`, `PATCH`, `.BIN`, and `.BGM` files. |
| `Xak-II-Rising-of-the-Redmoon_PC-88_EN.zip` | PC-88 disk image ZIP set for Xak II. |
| `cabin98.zip` | Helper/archive material; contains `cabin98xml.txt`, rename scripts, patch folders, and utility binaries. |

Hashes are recorded in `PACK_HASHES.txt`.

Candidate Microcabin-related entries:

- `fray_98.zip`: likely PC-98 MMD/MMD2 style path, not directly represented by a first-class `fray` entry in the original `hootsrc20011006/hoot.xml`.
- `xak_98.zip` and `xak2_98.zip`: likely PC-98 MMD/MMD2 style path; may map through generated XML in `cabin98.zip` rather than the old bundled catalog.
- `Xak-II-Rising-of-the-Redmoon_PC-88_EN.zip`: likely requires extraction from disk images before a Hoot driver can consume music assets.

First practical next step:

- `cabin98.zip`'s `cabin98xml.txt` has been extracted to `tests/fixtures/cabin98xml.txt`.
- Xak II PC-98 maps cleanly to local `xak2_98.zip` as catalog id `xak2-98-opn`.
- Use `hoot2wav --catalog tests/fixtures/cabin98xml.txt --packs . --entry xak2-98-opn ...` for the current asset-backed smoke path.
- FRAY PC-98 appears as catalog id `fray-98-opn`, but local `fray_98.zip` lacks `PATCH` and needs fixture preparation before validation can pass.

Redistribution note:

- These files are treated as local fixtures only. The project should commit hashes and setup notes, not copyrighted pack contents.
