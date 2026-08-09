# Hoot JSON and SQLite catalogues

The catalogue now has two representations with different responsibilities:

1. `catalog-src/hoot.catalog.json` and `catalog-src/shards/*.json` are the
   editable, version-control-friendly source catalogue.
2. `catalog/hoot.sqlite.zst` is the indexed runtime catalogue loaded by the
   player. It is a regular SQLite database compressed as one Zstandard frame
   at level 19.

The original `xml_20251231.zip` remains available as an import source. XML,
JSON, uncompressed SQLite, and `.sqlite.zst` are all accepted by the same
runtime loader.

## Rebuild the catalogue

From CMake:

```sh
cmake -S . -B build
cmake --build build --target hoot_catalog
```

When the replay dependencies are not present and only the data conversion is
needed:

```sh
cmake -S . -B build-catalog -DHOOT_CATALOG_ONLY=ON
cmake --build build-catalog --target hoot_catalog
```

Direct invocation:

```sh
python3 tools/hootcatalog/hoot_catalog.py build \
  --input xml_20251231.zip \
  --json-dir catalog-src \
  --overrides hoot-overrides.xml \
  --sqlite build/hoot.sqlite \
  --zstd catalog/hoot.sqlite.zst \
  --level 19
```

After editing JSON, regenerate only SQLite:

```sh
python3 tools/hootcatalog/hoot_catalog.py build-sqlite \
  --manifest catalog-src/hoot.catalog.json \
  --sqlite build/hoot.sqlite \
  --zstd catalog/hoot.sqlite.zst \
  --level 19
```

The build step requires Python 3 and the `zstd` command-line program. Runtime
loading requires SQLite3 and libzstd; it does not invoke external programs.

## Runtime use

```sh
./build/hootplay --catalog catalog/hoot.sqlite.zst --packs packs fz68snd
./build/hoot2wav --catalog catalog-src/hoot.catalog.json --list
```

The API entry point is:

```c
HootResult hoot_load_catalog(HootContext* ctx, const char* catalog_path);
```

`hoot_load_xml()` remains as a backward-compatible alias and now accepts all
supported catalogue formats.

## JSON layout

The manifest contains catalogue-wide binding rules and a list of shard files.
Each shard contains games originating from one legacy XML file. A game keeps:

- stable ID and source order;
- driver name, type, alias, and platform;
- all option blocks in their original order;
- archive name and ordered resources;
- offsets and optional CRC32 values;
- literal title entries and range entries;
- comments and original source filename.

Numeric XML values are kept as readable strings such as `"0x20000"` in JSON.
The compiler resolves them while generating SQLite.

## SQLite layout

Schema version 1 contains these tables:

- `meta`
- `games`
- `game_options`
- `assets`
- `title_entries`
- `tracks`
- `bindings`
- `binding_extensions`
- `binding_options`

`title_entries` preserves the source representation. `tracks` contains the
expanded, runtime-ready track list, so the player does not expand ranges at
startup. Ordered child records use an explicit `ordinal` column.

The compressed database is decompressed in memory and mounted read-only with
SQLite's deserialize API. Music pack ZIP files remain external.

## Overrides

`hoot-overrides.xml` continues to work for every catalogue format. The loader
uses `HOOT_OVERRIDE_XML` when set, otherwise it looks in the current directory
and beside the selected catalogue.
