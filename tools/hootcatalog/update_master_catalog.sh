#!/usr/bin/env sh
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$ROOT"
python3 tools/hootcatalog/hoot_master_catalog.py fetch \
  --sources catalog-sources/master_sources.json
python3 tools/hootcatalog/hoot_master_catalog.py build-master \
  --sources catalog-sources/master_sources.json \
  --output catalog-master \
  --overrides hoot-overrides.xml \
  --pack-catalog catalog-sources/hoot_archive_catalog.json \
  --pack-index catalog-master/hoot.pack-index.json \
  --sqlite catalog-master/hoot.sqlite \
  --zstd catalog/hoot.sqlite.zst \
  --level 19 \
  --report reports/hoot_master_catalog_build.json \
  --markdown-report reports/HOOT_MASTER_CATALOG_BUILD.md
