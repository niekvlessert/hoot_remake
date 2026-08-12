# Multi-source Hoot master catalogue

The cross-platform port no longer assumes that one frozen Hoot XML tree is the
complete catalogue. Hoot metadata exists in overlapping official, maintained,
and community collections, and some community XML contains playable original-Hoot
entries which do not occur in the normal manufacturer XML tree.

## Three independent states

The library treats these as separate facts:

1. **Catalogue present** — a game/variant is defined by an imported XML source.
2. **Pack present** — the required archive is physically available in the user's
   pack search path. The recovery pack index is metadata only; it does not pretend
   that an archive is installed locally.
3. **Backend supported** — `DriverRegistry::probe()` reports whether this port has
   a replay host for the catalogue driver.

An unsupported game must stay in the catalogue. The UI can therefore show a
present pack as unsupported instead of silently hiding it.

## Merge precedence

`catalog-sources/master_sources.json` is the source-of-truth configuration.
Higher numeric priority wins only for the **same conservative game variant**.
Different OPN/OPNA/MIDI variants sharing an archive are retained independently.
Per-record provenance is retained in JSON and in SQLite table `game_sources`.

The bundled policy is:

- community `~systems~.xml` — priority 50;
- optional HootArchive 2024 tree — priority 100;
- bundled known-good 2025-12-31 catalogue — priority 200;
- optional official 2026-01-12 tree — priority 230;
- current consolidated manufacturer mirror — priority 240;
- current Kurohane pending XML overlays — priority 300.

The lower-priority community source remains valuable for entries absent from the
newer manufacturer trees, including arcade additions.

## Updating public snapshots

On an Internet-connected machine:

```sh
python3 tools/hootcatalog/hoot_master_catalog.py fetch \
  --sources catalog-sources/master_sources.json
```

This downloads only sources with a configured `fetch_url` into their configured
snapshot paths. It currently covers the consolidated manufacturer mirror,
community `~systems~.xml`, and downloadable Kurohane `uns_*.xml` overlays.
Large HootArchive and complete official release trees are intentionally not
silently downloaded; copy them to the paths documented in
`master_sources.json` if you want them in the merge.

Then rebuild:

```sh
python3 tools/hootcatalog/hoot_master_catalog.py build-master \
  --sources catalog-sources/master_sources.json \
  --output catalog-master \
  --overrides hoot-overrides.xml \
  --pack-catalog catalog-sources/hoot_archive_catalog.json \
  --pack-index catalog-master/hoot.pack-index.json \
  --sqlite catalog-master/hoot.sqlite \
  --zstd catalog/hoot.sqlite.zst \
  --level 19 \
  --report reports/hoot_master_catalog_build.json
```

CMake uses the master builder by default (`HOOT_USE_MASTER_CATALOG=ON`). Set it
to `OFF` only for the legacy unpacked `xmlsrc/hoot.xml` path.

## SQLite compatibility

The runtime schema remains version 1. Existing loaders ignore the additional
metadata tables:

- `catalog_sources` — configured source status and priority;
- `game_sources` — per-game provenance and selected record;
- `pack_index` — recovered pack metadata keyed to the game variant.

The extra tables are attached **before** zstd compression, so the distributed
normal `catalog/hoot.sqlite.zst` runtime catalogue contains the provenance data.

## Capability census

`hootprobe --include-missing` probes `DriverRegistry` without requiring packs to
be installed. With an empty pack directory it executes no music code but still
reports authoritative capability status for every catalogue entry. The helper
`tools/hootcatalog/catalog_census.py` turns that output into JSON and Markdown
breakdowns by driver and platform.

## Missing snapshots are explicit

A configured source that is unavailable is written to the build report with
`status: missing`. The builder never invents driver/ROM/track XML from a title
list. This is deliberate: those fields are required to reproduce original Hoot
playback correctly.
