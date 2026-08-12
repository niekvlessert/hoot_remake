# Master Hoot catalogue integration — 2026-08-12

## Implemented

- Added a multi-source catalogue merger in `tools/hootcatalog/hoot_master_catalog.py`.
- Merges at game-variant level rather than overwriting whole manufacturer XML files.
- Preserves existing stable game IDs when the selected source already provides one.
- Records catalogue provenance for every selected game variant.
- Added source priority/configuration in `catalog-sources/master_sources.json`.
- Added optional recovery-pack metadata from the Hoot archive recovery catalogue.
- Added SQLite metadata tables: `catalog_sources`, `game_sources`, and `pack_index`.
- Kept the existing catalogue schema version compatible with the current runtime.
- Added `tools/hootcatalog/update_master_catalog.sh` for fetch + rebuild on an Internet-connected machine.
- Added a capability census driven by the real `DriverRegistry` rather than a static platform guess.
- Updated CMake so the master catalogue path is the default while the legacy catalogue build remains selectable.

## Availability model

Catalogue membership, local pack availability, and playback backend support are now treated as three separate facts. An entry can therefore remain visible even when its archive is absent or its emulator backend is not implemented.

## Current bundled build

The reproducible build in this tree contains the bundled 2025-12-31 baseline,
the vendored community systems XML, and the recovery pack index. Other external
XML snapshots remain configured and are never silently fabricated when absent.

Current generated counts:

- 6,112 game variants
- 157,639 expanded tracks
- 4,318 unique catalogue archive names
- 2,404 recovery-index pack archives
- Gradius IV is supplied by the normal `czarek-community-systems` XML layer

## External catalogue layers configured

- community `xml/~systems~.xml` (includes arcade additions such as Gradius IV)
- HootArchive 2024 XML tree
- official 2026-01-12 Hoot XML tree when supplied locally
- current `einstein95/hoot_xml` consolidated mirror
- current Kurohane pending ASCII, BOTHTEC and Technopolis Soft XMLs

The build report records missing snapshots explicitly. Run `tools/hootcatalog/update_master_catalog.sh` on a networked development machine to fetch the auto-fetchable layers and rebuild the master database.

## Validation

- `tests/test_hoot_master_catalog.py`: 5/5 passed
- `hoot_catalog.py verify`: passed
- Zstandard integrity test on `catalog/hoot.sqlite.zst`: passed
- CMake catalogue-only target: passed
- `hootprobe` loads the enriched catalogue and produced the capability census successfully
