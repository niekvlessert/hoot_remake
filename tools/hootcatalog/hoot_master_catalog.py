#!/usr/bin/env python3
"""Merge Hoot catalogue sources without hiding unsupported playback backends.

The legacy Hoot catalogue is distributed as several overlapping XML trees.  This
helper imports each tree with ``hoot_catalog.py``, merges game records at record
level, preserves source provenance, optionally joins the recovered Hoot pack
index, and emits a normal Hoot JSON manifest.  The existing runtime SQLite
builder can consume the output unchanged.

The merge deliberately separates three concepts:

* catalogue presence: a game definition exists;
* pack availability: a known downloadable/local archive exists;
* replay support: determined at runtime by DriverRegistry, not by this tool.

This means an arcade entry can remain visible in the library even while its
ArcadeHost/backend is not implemented yet.
"""
from __future__ import annotations

import argparse
import copy
import hashlib
import importlib.util
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import urllib.parse
import urllib.request
import zipfile
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Iterable

HERE = Path(__file__).resolve().parent
CORE_PATH = HERE / "hoot_catalog.py"
_spec = importlib.util.spec_from_file_location("hoot_catalog_core", CORE_PATH)
if _spec is None or _spec.loader is None:
    raise RuntimeError(f"unable to load {CORE_PATH}")
core = importlib.util.module_from_spec(_spec)
sys.modules[_spec.name] = core
_spec.loader.exec_module(core)

MASTER_FORMAT = "hoot-master-catalog"
MASTER_VERSION = 1
SOURCE_MANIFEST_FORMAT = "hoot-catalog-sources"


@dataclass(frozen=True)
class SourceSpec:
    name: str
    priority: int
    path: Path
    kind: str = "xml"
    date: str = ""
    url: str = ""
    note: str = ""


def clean(value: Any) -> str:
    return str(value or "").strip()


def normalize(value: Any) -> str:
    value = clean(value).casefold()
    value = re.sub(r"\s+", " ", value)
    return value


def variant_identity(game: dict[str, Any]) -> tuple[str, str, str, str, str]:
    """Conservative cross-source identity.

    Archive is the physical pack key. Driver name/type and normalized title keep
    deliberate OPN/OPNA/MIDI variants separate.  Driver alias platform is used
    as a final discriminator when two machines reuse an archive name.
    """
    driver = game.get("driver", {})
    archive = normalize(game.get("archive"))
    title = normalize(game.get("title"))
    if not archive:
        archive = "@title:" + title
    return (
        archive,
        normalize(driver.get("name")),
        normalize(driver.get("type")),
        normalize(driver.get("platform")),
        title,
    )


def record_digest(game: dict[str, Any]) -> str:
    relevant = {
        "title": game.get("title", ""),
        "driver": game.get("driver", {}),
        "options": game.get("options", []),
        "archive": game.get("archive", ""),
        "assets": game.get("assets", []),
        "title_entries": game.get("title_entries", []),
        "default_sample_rate": game.get("default_sample_rate", 44100),
        "refresh_hz": game.get("refresh_hz", 60),
    }
    blob = json.dumps(relevant, ensure_ascii=False, sort_keys=True, separators=(",", ":")).encode("utf-8")
    return hashlib.sha256(blob).hexdigest()


def source_ref(spec: SourceSpec, game: dict[str, Any], selected: bool) -> dict[str, Any]:
    return {
        "source": spec.name,
        "priority": spec.priority,
        "kind": spec.kind,
        "date": spec.date,
        "url": spec.url,
        "source_file": game.get("source_file", ""),
        "source_order": int(game.get("source_order", 0)),
        "record_sha256": record_digest(game),
        "selected": bool(selected),
    }


def load_source_manifest(path: Path) -> list[SourceSpec]:
    data = json.loads(path.read_text(encoding="utf-8"))
    if data.get("format") != SOURCE_MANIFEST_FORMAT:
        raise RuntimeError(f"{path}: expected format={SOURCE_MANIFEST_FORMAT}")
    base = path.parent
    out: list[SourceSpec] = []
    for item in data.get("sources", []):
        raw = clean(item.get("path"))
        if not raw:
            continue
        p = Path(os.path.expandvars(os.path.expanduser(raw)))
        if not p.is_absolute():
            p = (base / p).resolve()
        out.append(SourceSpec(
            name=clean(item.get("name")) or p.stem,
            priority=int(item.get("priority", 0)),
            path=p,
            kind=clean(item.get("kind")) or "xml",
            date=clean(item.get("date")),
            url=clean(item.get("url")),
            note=clean(item.get("note")),
        ))
    return out


def manifest_for_source(spec: SourceSpec, temp_root: Path, overrides: Path | None) -> Path:
    if spec.path.suffix.lower() == ".json":
        data = json.loads(spec.path.read_text(encoding="utf-8"))
        if data.get("format") in (core.JSON_MANIFEST_FORMAT, core.JSON_SHARD_FORMAT):
            return spec.path
    out = temp_root / core.slugify(spec.name)
    return core.import_xml(spec.path, out, overrides)


def read_manifest_games(manifest: Path) -> list[dict[str, Any]]:
    return [copy.deepcopy(game) for game in core.iter_json_games(manifest)]


def read_manifest_bindings(manifest: Path) -> list[dict[str, Any]]:
    data = json.loads(manifest.read_text(encoding="utf-8"))
    return copy.deepcopy(data.get("bindings", []))


def merge_sources(specs: list[SourceSpec], output_dir: Path, overrides: Path | None = None) -> Path:
    if not specs:
        raise RuntimeError("at least one catalogue source is required")
    output_dir.mkdir(parents=True, exist_ok=True)
    shards = output_dir / "shards"
    if shards.exists():
        shutil.rmtree(shards)
    shards.mkdir(parents=True)

    # Lower priority is ingested first. A higher-priority exact variant replaces
    # the selected payload while all provenance records are retained.
    selected: dict[tuple[str, str, str, str, str], dict[str, Any]] = {}
    selected_spec: dict[tuple[str, str, str, str, str], SourceSpec] = {}
    provenance: dict[tuple[str, str, str, str, str], list[dict[str, Any]]] = {}
    bindings: list[dict[str, Any]] = []
    binding_seen: set[str] = set()
    source_stats: list[dict[str, Any]] = []

    with tempfile.TemporaryDirectory(prefix="hoot-master-") as td:
        temp_root = Path(td)
        for spec in sorted(specs, key=lambda s: (s.priority, s.name.casefold())):
            if not spec.path.exists():
                source_stats.append({
                    "name": spec.name, "priority": spec.priority, "kind": spec.kind,
                    "date": spec.date, "url": spec.url, "path": str(spec.path),
                    "status": "missing", "games": 0, "note": spec.note,
                })
                continue
            manifest = manifest_for_source(spec, temp_root, overrides)
            games = read_manifest_games(manifest)
            source_stats.append({
                "name": spec.name, "priority": spec.priority, "kind": spec.kind,
                "date": spec.date, "url": spec.url, "path": str(spec.path),
                "status": "loaded", "games": len(games), "note": spec.note,
            })
            for binding in read_manifest_bindings(manifest):
                token = json.dumps(binding, ensure_ascii=False, sort_keys=True)
                if token not in binding_seen:
                    binding_seen.add(token)
                    bindings.append(binding)
            for game in games:
                key = variant_identity(game)
                old_spec = selected_spec.get(key)
                replace = old_spec is None or spec.priority > old_spec.priority
                if old_spec is not None and spec.priority == old_spec.priority:
                    # Stable tie-break: later source name sorts last, but don't
                    # let traversal order make a catalogue non-deterministic.
                    replace = spec.name.casefold() > old_spec.name.casefold()
                provenance.setdefault(key, []).append(source_ref(spec, game, replace))
                if replace:
                    # Clear previous selected flags for this canonical variant.
                    for item in provenance[key][:-1]:
                        item["selected"] = False
                    selected[key] = game
                    selected_spec[key] = spec

    games = list(selected.values())
    games.sort(key=lambda g: (
        normalize(g.get("driver", {}).get("platform")),
        normalize(g.get("title")),
        normalize(g.get("archive")),
        normalize(g.get("driver", {}).get("name")),
        normalize(g.get("driver", {}).get("type")),
    ))

    # Preserve an upstream stable ID whenever possible. This is important for
    # user overrides and bookmarks. Only synthesize a new/suffixed ID when two
    # independently imported source trees selected different variants with the
    # same ID or an entry had no ID at all.
    used_ids: set[str] = set()
    generated_seen: dict[str, int] = {}
    for order, game in enumerate(games):
        key = variant_identity(game)
        game["source_order"] = order
        candidate_id = clean(game.get("id"))
        if not candidate_id or candidate_id in used_ids:
            candidate_id = core.stable_id(game, generated_seen)
            while candidate_id in used_ids:
                candidate_id = core.stable_id(game, generated_seen)
        game["id"] = candidate_id
        used_ids.add(candidate_id)
        game["selected_source"] = selected_spec[key].name
        game["catalog_provenance"] = sorted(
            provenance[key], key=lambda p: (-int(p["priority"]), p["source"].casefold())
        )

    # Shard by selected source. This keeps diffs readable while the runtime sees
    # one ordinary JSON manifest.
    grouped: dict[str, list[dict[str, Any]]] = {}
    for game in games:
        grouped.setdefault(clean(game.get("selected_source")) or "unknown", []).append(game)
    includes: list[str] = []
    for index, source_name in enumerate(sorted(grouped, key=str.casefold)):
        rel = f"shards/{index:03d}-{core.slugify(source_name)}.json"
        shard = {
            "format": core.JSON_SHARD_FORMAT,
            "version": core.FORMAT_VERSION,
            "source_file": source_name,
            "date": "",
            "comments": ["Merged master catalogue shard; per-game catalog_provenance is authoritative."],
            "games": grouped[source_name],
        }
        (output_dir / rel).write_text(json.dumps(shard, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
        includes.append(rel)

    literal_titles = 0
    ranges = 0
    assets = 0
    for game in games:
        assets += len(game.get("assets", []))
        for item in game.get("title_entries", []):
            if item.get("kind") == "title": literal_titles += 1
            elif item.get("kind") == "range": ranges += 1

    manifest = {
        "format": core.JSON_MANIFEST_FORMAT,
        "version": core.FORMAT_VERSION,
        "master_format": MASTER_FORMAT,
        "master_version": MASTER_VERSION,
        "source": "merged-master",
        "root_xml": "",
        "overrides": "" if overrides is None else overrides.name,
        "bindings": bindings,
        "sources": source_stats,
        "includes": includes,
        "statistics": {
            "files": len(includes),
            "games": len(games),
            "literal_titles": literal_titles,
            "ranges": ranges,
            "assets": assets,
            "loaded_sources": sum(1 for s in source_stats if s["status"] == "loaded"),
            "missing_sources": sum(1 for s in source_stats if s["status"] != "loaded"),
        },
    }
    path = output_dir / "hoot.catalog.json"
    path.write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    return path


def load_pack_catalog(path: Path | None) -> dict[str, dict[str, Any]]:
    if path is None:
        return {}
    data = json.loads(path.read_text(encoding="utf-8"))
    packs = data.get("packs", []) if isinstance(data, dict) else data
    out: dict[str, dict[str, Any]] = {}
    for pack in packs:
        archive = normalize(pack.get("archive"))
        if archive:
            out[archive] = pack
    return out


def enrich_pack_metadata(manifest_path: Path, pack_catalog: Path, output_path: Path) -> dict[str, int]:
    packs = load_pack_catalog(pack_catalog)
    games = list(core.iter_json_games(manifest_path))
    matched = 0
    archives: set[str] = set()
    rows: list[dict[str, Any]] = []
    for game in games:
        archive = clean(game.get("archive"))
        if not archive:
            continue
        key = normalize(archive)
        archives.add(key)
        pack = packs.get(key)
        row = {
            "game_id": game.get("id", ""),
            "archive": archive,
            "catalogued_pack": pack is not None,
        }
        if pack is not None:
            matched += 1
            row.update({
                "pack_title": pack.get("title", ""),
                "pack_system": pack.get("system", ""),
                "pack_system_id": pack.get("system_id", ""),
                "pack_url": pack.get("url", ""),
                "pack_file_count": int(pack.get("file_count", 0) or 0),
                "pack_uncompressed_bytes": int(pack.get("uncompressed_bytes", 0) or 0),
                "pack_metadata_matched": bool(pack.get("metadata_matched", False)),
                "pack_source_xml": pack.get("source_xml", ""),
            })
        rows.append(row)
    result = {
        "format": "hoot-master-pack-index",
        "version": 1,
        "source": str(pack_catalog),
        "statistics": {
            "catalog_games": len(games),
            "unique_catalog_archives": len(archives),
            "pack_index_archives": len(packs),
            "game_rows_with_known_pack": matched,
        },
        "games": rows,
    }
    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text(json.dumps(result, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    return result["statistics"]


def attach_sqlite_metadata(sqlite_path: Path, manifest_path: Path, pack_index_path: Path | None = None) -> dict[str, int]:
    import sqlite3
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    conn = sqlite3.connect(sqlite_path)
    try:
        conn.executescript("""
        CREATE TABLE IF NOT EXISTS catalog_sources (
            source_name TEXT PRIMARY KEY,
            priority INTEGER NOT NULL,
            kind TEXT NOT NULL,
            source_date TEXT NOT NULL,
            source_url TEXT NOT NULL,
            source_path TEXT NOT NULL,
            status TEXT NOT NULL,
            game_count INTEGER NOT NULL,
            note TEXT NOT NULL
        ) WITHOUT ROWID;
        CREATE TABLE IF NOT EXISTS game_sources (
            game_id TEXT NOT NULL REFERENCES games(id) ON DELETE CASCADE,
            ordinal INTEGER NOT NULL,
            source_name TEXT NOT NULL,
            priority INTEGER NOT NULL,
            kind TEXT NOT NULL,
            source_date TEXT NOT NULL,
            source_url TEXT NOT NULL,
            source_file TEXT NOT NULL,
            source_order INTEGER NOT NULL,
            record_sha256 TEXT NOT NULL,
            selected INTEGER NOT NULL,
            PRIMARY KEY(game_id, ordinal)
        ) WITHOUT ROWID;
        CREATE TABLE IF NOT EXISTS pack_index (
            game_id TEXT PRIMARY KEY REFERENCES games(id) ON DELETE CASCADE,
            archive TEXT NOT NULL,
            catalogued_pack INTEGER NOT NULL,
            pack_title TEXT NOT NULL,
            pack_system TEXT NOT NULL,
            pack_system_id TEXT NOT NULL,
            pack_url TEXT NOT NULL,
            pack_file_count INTEGER NOT NULL,
            pack_uncompressed_bytes INTEGER NOT NULL,
            pack_metadata_matched INTEGER NOT NULL,
            pack_source_xml TEXT NOT NULL
        ) WITHOUT ROWID;
        CREATE INDEX IF NOT EXISTS game_sources_name_idx ON game_sources(source_name);
        CREATE INDEX IF NOT EXISTS pack_index_archive_idx ON pack_index(archive);
        """)
        conn.execute("INSERT OR REPLACE INTO meta(key,value) VALUES('master_catalog_version',?)", (str(MASTER_VERSION),))
        for source in manifest.get("sources", []):
            conn.execute(
                "INSERT OR REPLACE INTO catalog_sources VALUES(?,?,?,?,?,?,?,?,?)",
                (source.get("name", ""), int(source.get("priority", 0)), source.get("kind", ""),
                 source.get("date", ""), source.get("url", ""), source.get("path", ""),
                 source.get("status", ""), int(source.get("games", 0)), source.get("note", "")),
            )
        game_map = {game.get("id", ""): game for game in core.iter_json_games(manifest_path)}
        for game_id, game in game_map.items():
            for ordinal, p in enumerate(game.get("catalog_provenance", [])):
                conn.execute(
                    "INSERT OR REPLACE INTO game_sources VALUES(?,?,?,?,?,?,?,?,?,?,?)",
                    (game_id, ordinal, p.get("source", ""), int(p.get("priority", 0)), p.get("kind", ""),
                     p.get("date", ""), p.get("url", ""), p.get("source_file", ""),
                     int(p.get("source_order", 0)), p.get("record_sha256", ""), int(bool(p.get("selected")))),
                )
        pack_rows = 0
        if pack_index_path is not None and pack_index_path.exists():
            data = json.loads(pack_index_path.read_text(encoding="utf-8"))
            for item in data.get("games", []):
                conn.execute(
                    "INSERT OR REPLACE INTO pack_index VALUES(?,?,?,?,?,?,?,?,?,?,?)",
                    (item.get("game_id", ""), item.get("archive", ""), int(bool(item.get("catalogued_pack"))),
                     item.get("pack_title", ""), item.get("pack_system", ""), item.get("pack_system_id", ""),
                     item.get("pack_url", ""), int(item.get("pack_file_count", 0)),
                     int(item.get("pack_uncompressed_bytes", 0)), int(bool(item.get("pack_metadata_matched"))),
                     item.get("pack_source_xml", "")),
                )
                pack_rows += 1
        conn.commit()
        return {
            "sources": conn.execute("SELECT count(*) FROM catalog_sources").fetchone()[0],
            "game_sources": conn.execute("SELECT count(*) FROM game_sources").fetchone()[0],
            "pack_rows": pack_rows,
        }
    finally:
        conn.close()


def fetch_sources(config_path: Path, destination: Path | None = None) -> dict[str, str]:
    """Fetch public source snapshots listed in ``master_sources.json``.

    By default each source is written to its configured ``path``.  Passing a
    destination directory is useful for inspection, but a normal update should
    omit it so a subsequent build consumes exactly what was fetched.
    """
    data = json.loads(config_path.read_text(encoding="utf-8"))
    base = config_path.parent
    if destination is not None:
        destination.mkdir(parents=True, exist_ok=True)
    result: dict[str, str] = {}
    for item in data.get("sources", []):
        url = clean(item.get("fetch_url"))
        if not url:
            continue
        name = clean(item.get("name")) or "source"
        configured = clean(item.get("path"))
        if destination is None:
            if not configured:
                result[name] = "SKIPPED: no configured path"
                continue
            target = Path(os.path.expandvars(os.path.expanduser(configured)))
            if not target.is_absolute():
                target = (base / target).resolve()
        else:
            filename = clean(item.get("fetch_filename")) or Path(urllib.parse.urlparse(url).path).name or (core.slugify(name) + ".download")
            target = destination / filename
        target.parent.mkdir(parents=True, exist_ok=True)
        temporary = target.with_name(target.name + ".download")
        request = urllib.request.Request(url, headers={"User-Agent": "HootMasterCatalog/1"})
        try:
            with urllib.request.urlopen(request, timeout=90) as response, temporary.open("wb") as output:
                shutil.copyfileobj(response, output)
            os.replace(temporary, target)
            sha = hashlib.sha256(target.read_bytes()).hexdigest()
            result[name] = f"{target} sha256={sha}"
        except Exception as exc:
            temporary.unlink(missing_ok=True)
            result[name] = f"ERROR: {exc}"
    return result


def source_collision_report(manifest_path: Path) -> dict[str, Any]:
    """Summarize physical archive variants without collapsing them."""
    by_archive: dict[str, list[dict[str, str]]] = {}
    for game in core.iter_json_games(manifest_path):
        archive = clean(game.get("archive"))
        if not archive:
            continue
        driver = game.get("driver", {})
        by_archive.setdefault(normalize(archive), []).append({
            "id": clean(game.get("id")),
            "archive": archive,
            "title": clean(game.get("title")),
            "driver": clean(driver.get("name")),
            "driver_type": clean(driver.get("type")),
            "platform": clean(driver.get("platform")),
            "selected_source": clean(game.get("selected_source")),
        })
    collisions = [rows for rows in by_archive.values() if len(rows) > 1]
    collisions.sort(key=lambda rows: (rows[0]["archive"].casefold(), len(rows)))
    return {
        "unique_archives": len(by_archive),
        "multi_variant_archives": len(collisions),
        "extra_variant_rows": sum(len(rows) - 1 for rows in collisions),
        "archives": collisions,
    }


def build_master(sources_path: Path, output_dir: Path, sqlite_path: Path,
                 zstd_path: Path | None = None, pack_catalog: Path | None = None,
                 pack_index_path: Path | None = None, overrides: Path | None = None,
                 level: int = 19, report_path: Path | None = None) -> dict[str, Any]:
    """Build merged JSON + SQLite, attaching metadata before compression."""
    specs = load_source_manifest(sources_path)
    manifest = merge_sources(specs, output_dir, overrides)
    pack_stats: dict[str, int] = {}
    if pack_catalog is not None:
        if pack_index_path is None:
            pack_index_path = output_dir / "hoot.pack-index.json"
        pack_stats = enrich_pack_metadata(manifest, pack_catalog, pack_index_path)
    sqlite_stats = core.build_sqlite(manifest, sqlite_path)
    metadata_stats = attach_sqlite_metadata(sqlite_path, manifest, pack_index_path)
    verify_stats = core.verify_sqlite(sqlite_path)
    if zstd_path is not None:
        core.compress_zstd(sqlite_path, zstd_path, level)

    manifest_data = json.loads(manifest.read_text(encoding="utf-8"))
    result: dict[str, Any] = {
        "format": "hoot-master-build-report",
        "version": 1,
        "manifest": str(manifest),
        "sqlite": str(sqlite_path),
        "zstd": "" if zstd_path is None else str(zstd_path),
        "sources": manifest_data.get("sources", []),
        "statistics": manifest_data.get("statistics", {}),
        "sqlite_counts": sqlite_stats,
        "verify_counts": verify_stats,
        "metadata_counts": metadata_stats,
        "pack_statistics": pack_stats,
        "archive_variants": source_collision_report(manifest),
    }
    if report_path is not None:
        report_path.parent.mkdir(parents=True, exist_ok=True)
        report_path.write_text(json.dumps(result, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    return result


def write_markdown_summary(build_report: dict[str, Any], output_path: Path) -> None:
    stats = build_report.get("statistics", {})
    pack = build_report.get("pack_statistics", {})
    sources = build_report.get("sources", [])
    lines = [
        "# Hoot master catalogue build report",
        "",
        "This report distinguishes **catalogue presence**, **known pack metadata**, and",
        "**runtime backend support**. Backend support is intentionally not inferred here;",
        "the C++ `DriverRegistry::probe()` remains authoritative for playback capability.",
        "",
        "## Catalogue",
        "",
        f"- Games: **{stats.get('games', 0):,}**",
        f"- Literal track titles: **{stats.get('literal_titles', 0):,}**",
        f"- Track ranges: **{stats.get('ranges', 0):,}**",
        f"- Assets: **{stats.get('assets', 0):,}**",
        f"- Loaded sources: **{stats.get('loaded_sources', 0)}**",
        f"- Missing configured sources: **{stats.get('missing_sources', 0)}**",
        "",
        "## Pack index",
        "",
        f"- Unique catalogue archives: **{pack.get('unique_catalog_archives', 0):,}**",
        f"- Recovery-index archives: **{pack.get('pack_index_archives', 0):,}**",
        f"- Game rows matched to recovery metadata: **{pack.get('game_rows_with_known_pack', 0):,}**",
        "",
        "## Sources",
        "",
        "| Priority | Source | Status | Games | Date | Kind |",
        "|---:|---|---|---:|---|---|",
    ]
    for source in sorted(sources, key=lambda item: (-int(item.get("priority", 0)), str(item.get("name", "")).casefold())):
        lines.append(
            f"| {int(source.get('priority', 0))} | {source.get('name', '')} | "
            f"{source.get('status', '')} | {int(source.get('games', 0)):,} | "
            f"{source.get('date', '')} | {source.get('kind', '')} |"
        )
    lines += [
        "",
        "A configured source marked `missing` was **not** silently substituted with guessed",
        "game definitions. Run the catalogue `fetch` command on an Internet-connected host",
        "and rebuild to ingest it.",
    ]
    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def command_merge(args: argparse.Namespace) -> int:
    specs = load_source_manifest(Path(args.sources))
    manifest = merge_sources(specs, Path(args.output), Path(args.overrides) if args.overrides else None)
    print(manifest)
    return 0


def command_enrich(args: argparse.Namespace) -> int:
    stats = enrich_pack_metadata(Path(args.manifest), Path(args.pack_catalog), Path(args.output))
    print("Counts: " + ", ".join(f"{k}={v}" for k, v in stats.items()))
    return 0


def command_attach(args: argparse.Namespace) -> int:
    stats = attach_sqlite_metadata(Path(args.sqlite), Path(args.manifest), Path(args.pack_index) if args.pack_index else None)
    print("Counts: " + ", ".join(f"{k}={v}" for k, v in stats.items()))
    return 0


def command_build_master(args: argparse.Namespace) -> int:
    report = build_master(
        Path(args.sources), Path(args.output), Path(args.sqlite),
        Path(args.zstd) if args.zstd else None,
        Path(args.pack_catalog) if args.pack_catalog else None,
        Path(args.pack_index) if args.pack_index else None,
        Path(args.overrides) if args.overrides else None,
        int(args.level), Path(args.report) if args.report else None,
    )
    if args.markdown_report:
        write_markdown_summary(report, Path(args.markdown_report))
    print("Counts: " + ", ".join(f"{k}={v}" for k, v in report.get("verify_counts", {}).items()))
    missing = [s["name"] for s in report.get("sources", []) if s.get("status") != "loaded"]
    if missing:
        print("Missing configured sources: " + ", ".join(missing))
    return 0


def command_fetch(args: argparse.Namespace) -> int:
    result = fetch_sources(Path(args.sources), Path(args.destination) if args.destination else None)
    for name, value in result.items():
        print(f"{name}: {value}")
    return 0


def make_parser() -> argparse.ArgumentParser:
    ap = argparse.ArgumentParser(description=__doc__)
    sub = ap.add_subparsers(dest="command", required=True)
    p = sub.add_parser("merge", help="merge local catalogue sources into one JSON catalogue")
    p.add_argument("--sources", required=True, help="master_sources.json")
    p.add_argument("--output", required=True, help="output JSON catalogue directory")
    p.add_argument("--overrides")
    p.set_defaults(func=command_merge)
    p = sub.add_parser("enrich-packs", help="join recovered Hoot pack metadata by archive name")
    p.add_argument("--manifest", required=True)
    p.add_argument("--pack-catalog", required=True)
    p.add_argument("--output", required=True)
    p.set_defaults(func=command_enrich)
    p = sub.add_parser("attach-sqlite", help="add provenance and pack-index tables to an existing runtime SQLite catalogue")
    p.add_argument("--sqlite", required=True)
    p.add_argument("--manifest", required=True)
    p.add_argument("--pack-index")
    p.set_defaults(func=command_attach)
    p = sub.add_parser("build-master", help="merge sources -> pack index -> SQLite metadata -> optional zstd")
    p.add_argument("--sources", required=True)
    p.add_argument("--output", required=True, help="output JSON catalogue directory")
    p.add_argument("--sqlite", required=True)
    p.add_argument("--zstd")
    p.add_argument("--pack-catalog")
    p.add_argument("--pack-index")
    p.add_argument("--overrides")
    p.add_argument("--level", type=int, default=19)
    p.add_argument("--report")
    p.add_argument("--markdown-report")
    p.set_defaults(func=command_build_master)
    p = sub.add_parser("fetch", help="fetch configured public source snapshots")
    p.add_argument("--sources", required=True)
    p.add_argument("--destination", help="optional inspection directory; omit to use each configured source path")
    p.set_defaults(func=command_fetch)
    return ap


def main() -> int:
    try:
        args = make_parser().parse_args()
        return args.func(args)
    except Exception as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
