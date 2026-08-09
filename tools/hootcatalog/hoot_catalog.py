#!/usr/bin/env python3
"""Build Hoot JSON source catalogues and a compressed SQLite runtime catalogue.

The JSON directory is the editable/source form.  The SQLite database is the
runtime form.  The SQLite file is optionally compressed with Zstandard; level
19 is the project default.
"""

from __future__ import annotations

import argparse
import json
import os
import re
import shutil
import sqlite3
import subprocess
import sys
import tempfile
import zipfile
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Iterable
import xml.etree.ElementTree as ET

FORMAT_VERSION = 1
SQLITE_FORMAT = "hoot-sqlite"
JSON_MANIFEST_FORMAT = "hoot-catalog-manifest"
JSON_SHARD_FORMAT = "hoot-catalog-shard"


def comment_parser() -> ET.XMLParser:
    return ET.XMLParser(target=ET.TreeBuilder(insert_comments=True))


def is_comment(node: ET.Element) -> bool:
    return node.tag is ET.Comment


def text(node: ET.Element | None) -> str:
    return "" if node is None or node.text is None else node.text.strip()


def parse_num(value: str | int | None) -> int | None:
    if value is None:
        return None
    if isinstance(value, int):
        return value
    value = value.strip()
    if not value:
        return None
    sign = -1 if value.startswith("-") else 1
    if sign < 0:
        value = value[1:]
    base = 16 if value.lower().startswith("0x") else 10
    if base == 16:
        value = value[2:]
    return sign * int(value, base)


def slugify(value: str) -> str:
    out: list[str] = []
    last_dash = False
    for ch in value:
        if ch.isascii() and ch.isalnum():
            out.append(ch.lower())
            last_dash = False
        elif not last_dash and out:
            out.append("-")
            last_dash = True
    while out and out[-1] == "-":
        out.pop()
    return "".join(out) or "entry"


def stable_id(game: dict[str, Any], seen: dict[str, int]) -> str:
    archive = game.get("archive") or slugify(game.get("title", "entry"))
    driver_type = game.get("driver", {}).get("type", "")
    base = slugify(f"{archive}-{driver_type}" if driver_type else archive)
    count = seen.get(base, 0)
    seen[base] = count + 1
    return base if count == 0 else f"{base}-{count + 1}"


def format_code(fmt: str, code: int) -> str:
    """Small printf implementation for Hoot's %x/%X/%d/%u title ranges."""
    out: list[str] = []
    i = 0
    while i < len(fmt):
        if fmt[i] != "%":
            out.append(fmt[i])
            i += 1
            continue
        if i + 1 < len(fmt) and fmt[i + 1] == "%":
            out.append("%")
            i += 2
            continue
        j = i + 1
        zero = False
        width = 0
        if j < len(fmt) and fmt[j] == "0":
            zero = True
            j += 1
        while j < len(fmt) and fmt[j].isdigit():
            width = width * 10 + int(fmt[j])
            j += 1
        if j >= len(fmt) or fmt[j] not in "xXdu":
            out.append("%")
            i += 1
            continue
        conv = fmt[j]
        if conv == "x":
            rendered = f"{code:x}"
        elif conv == "X":
            rendered = f"{code:X}"
        else:
            rendered = str(code)
        if width > len(rendered):
            rendered = ("0" if zero else " ") * (width - len(rendered)) + rendered
        out.append(rendered)
        i = j + 1
    return "".join(out)


def node_comments(nodes: Iterable[ET.Element]) -> list[str]:
    return [node.text or "" for node in nodes if is_comment(node)]


def parse_options(parent: ET.Element | None) -> list[dict[str, str]]:
    if parent is None:
        return []
    result: list[dict[str, str]] = []
    for child in list(parent):
        if child.tag == "option":
            result.append({
                "name": child.attrib.get("name", ""),
                "value": child.attrib.get("value", "0"),
            })
    return result


def parse_title_entries(parent: ET.Element | None) -> list[dict[str, Any]]:
    if parent is None:
        return []
    entries: list[dict[str, Any]] = []
    for child in list(parent):
        if is_comment(child):
            entries.append({"kind": "comment", "text": child.text or ""})
        elif child.tag == "title":
            item: dict[str, Any] = {
                "kind": "title",
                "code": child.attrib.get("code", "0"),
                "title": text(child),
            }
            if "type" in child.attrib:
                item["type"] = child.attrib["type"]
            entries.append(item)
        elif child.tag == "range":
            item = {
                "kind": "range",
                "min": child.attrib.get("min", "0"),
                "max": child.attrib.get("max", "0"),
                "title_format": text(child),
            }
            for key in ("extcode", "start"):
                if key in child.attrib:
                    item[key] = child.attrib[key]
            entries.append(item)
    return entries


def parse_game(node: ET.Element, source_file: str, source_order: int) -> dict[str, Any]:
    result: dict[str, Any] = {
        "title": text(node.find("name")),
        "driver": {},
        "options": [],
        "assets": [],
        "title_entries": [],
        "comments": node_comments(list(node)),
        "source_file": source_file,
        "source_order": source_order,
    }
    driver = node.find("driver")
    if driver is not None:
        result["driver"] = {
            "name": text(driver),
            "type": driver.attrib.get("type", ""),
        }
    alias = node.find("driveralias")
    if alias is not None:
        result["driver"]["alias"] = text(alias)
        result["driver"]["platform"] = alias.attrib.get("type", "")

    # Some catalogue files contain more than one <options> block. Preserve all.
    for options in node.findall("options"):
        result["options"].extend(parse_options(options))

    romlist = node.find("romlist")
    if romlist is not None:
        result["archive"] = romlist.attrib.get("archive", "")
        for rom in list(romlist):
            if rom.tag != "rom":
                continue
            item: dict[str, Any] = {
                "type": rom.attrib.get("type", ""),
                "path": text(rom),
            }
            for key in ("offset", "crc32"):
                if key in rom.attrib:
                    item[key] = rom.attrib[key]
            result["assets"].append(item)

    result["title_entries"] = parse_title_entries(node.find("titlelist"))
    return result


def parse_bind(node: ET.Element) -> dict[str, Any]:
    driver = node.find("driver")
    exts = node.find("exts")
    return {
        "extensions": [text(ext).lower() for ext in (list(exts) if exts is not None else []) if ext.tag == "ext"],
        "driver": {
            "name": text(driver),
            "type": "" if driver is None else driver.attrib.get("type", ""),
        },
        "options": parse_options(node.find("options")),
    }

def load_overrides(path: Path | None) -> list[dict[str, Any]]:
    if path is None:
        return []
    root = ET.parse(path, parser=comment_parser()).getroot()
    if root.tag != "hoot-overrides":
        raise RuntimeError(f"override root must be <hoot-overrides>: {path}")
    rules: list[dict[str, Any]] = []
    for game in list(root):
        if game.tag != "game":
            continue
        rules.append({
            "id": game.attrib.get("id", ""),
            "archive": game.attrib.get("archive", ""),
            "items": [item for item in list(game) if not is_comment(item)],
        })
    return rules


def apply_overrides_to_game(game: dict[str, Any], rules: list[dict[str, Any]]) -> None:
    for rule in rules:
        if rule["id"] and rule["id"] != game.get("id"):
            continue
        if rule["archive"] and rule["archive"] != game.get("archive"):
            continue
        if not rule["id"] and not rule["archive"]:
            continue
        for item in rule["items"]:
            if item.tag == "option":
                game["options"].append({
                    "name": item.attrib.get("name", ""),
                    "value": item.attrib.get("value", "0"),
                })
            elif item.tag == "asset":
                filename = item.attrib.get("file", "")
                matching = [asset for asset in game["assets"] if asset.get("path") == filename]
                if not matching:
                    raise RuntimeError(f"override asset {filename} was not found in {game.get('id')}")
                for asset in matching:
                    asset["transform"] = item.attrib.get("transform", "")
            elif item.tag == "voicebank":
                asset_type = "voicebank:" + item.attrib.get("id", "")
                game["assets"] = [asset for asset in game["assets"] if asset.get("type") != asset_type]
                asset: dict[str, Any] = {
                    "type": asset_type,
                    "path": item.attrib.get("file", ""),
                    "transform": item.attrib.get("transform", ""),
                }
                if "offset" in item.attrib:
                    asset["offset"] = item.attrib["offset"]
                game["assets"].append(asset)
            elif item.tag == "track":
                code = parse_num(item.attrib.get("code"))
                matched = False
                for title_entry in game["title_entries"]:
                    if title_entry.get("kind") == "title" and parse_num(title_entry.get("code")) == code:
                        title_entry["voice_bank"] = item.attrib.get("voicebank", "")
                        matched = True
                if not matched:
                    raise RuntimeError(
                        f"override track code {item.attrib.get('code', '')} was not found in {game.get('id')}"
                    )


@dataclass
class XmlInput:
    root_xml: Path
    temp_dir: tempfile.TemporaryDirectory[str] | None = None

    def close(self) -> None:
        if self.temp_dir is not None:
            self.temp_dir.cleanup()


def resolve_xml_input(input_path: Path) -> XmlInput:
    if input_path.is_file() and zipfile.is_zipfile(input_path):
        td = tempfile.TemporaryDirectory(prefix="hoot-xml-")
        with zipfile.ZipFile(input_path) as archive:
            archive.extractall(td.name)
        candidates = sorted(Path(td.name).rglob("hoot.xml"))
        if not candidates:
            td.cleanup()
            raise RuntimeError(f"{input_path}: no hoot.xml in archive")
        return XmlInput(candidates[0], td)
    if input_path.is_dir():
        candidate = input_path / "hoot.xml"
        if not candidate.exists():
            candidates = sorted(input_path.rglob("hoot.xml"))
            if not candidates:
                raise RuntimeError(f"{input_path}: no hoot.xml found")
            candidate = candidates[0]
        return XmlInput(candidate)
    if input_path.is_file():
        return XmlInput(input_path)
    raise RuntimeError(f"input does not exist: {input_path}")


def discover_xml_files(root_xml: Path) -> list[Path]:
    result: list[Path] = []
    visited: set[Path] = set()

    def visit(path: Path) -> None:
        path = path.resolve()
        if path in visited:
            return
        visited.add(path)
        result.append(path)
        root = ET.parse(path, parser=comment_parser()).getroot()
        childlists = root.find("childlists")
        if childlists is None:
            return
        for child in list(childlists):
            if child.tag != "list":
                continue
            rel = text(child).replace("\\", "/")
            if rel:
                visit((path.parent / rel).resolve())

    visit(root_xml)
    return result


def import_xml(input_path: Path, json_dir: Path, overrides_path: Path | None = None) -> Path:
    source = resolve_xml_input(input_path)
    try:
        root_xml = source.root_xml.resolve()
        base_dir = root_xml.parent
        files = discover_xml_files(root_xml)
        json_dir.mkdir(parents=True, exist_ok=True)
        shards_dir = json_dir / "shards"
        if shards_dir.exists():
            shutil.rmtree(shards_dir)
        shards_dir.mkdir(parents=True)

        seen_ids: dict[str, int] = {}
        override_rules = load_overrides(overrides_path)
        global_order = 0
        includes: list[str] = []
        total_games = 0
        total_titles = 0
        total_assets = 0
        total_ranges = 0
        bindings: list[dict[str, Any]] = []

        for index, xml_file in enumerate(files):
            root = ET.parse(xml_file, parser=comment_parser()).getroot()
            relative = xml_file.relative_to(base_dir).as_posix()
            games: list[dict[str, Any]] = []
            file_comments: list[str] = []
            for child in list(root):
                if is_comment(child):
                    file_comments.append(child.text or "")
                elif child.tag == "bind":
                    bindings.append(parse_bind(child))
                elif child.tag == "game":
                    game = parse_game(child, relative, global_order)
                    game["id"] = stable_id(game, seen_ids)
                    apply_overrides_to_game(game, override_rules)
                    global_order += 1
                    games.append(game)
                    total_games += 1
                    total_assets += len(game["assets"])
                    for title_entry in game["title_entries"]:
                        if title_entry["kind"] == "title":
                            total_titles += 1
                        elif title_entry["kind"] == "range":
                            total_ranges += 1

            shard_name = f"{index:03d}-{slugify(Path(relative).stem)}.json"
            shard_rel = f"shards/{shard_name}"
            shard = {
                "format": JSON_SHARD_FORMAT,
                "version": FORMAT_VERSION,
                "source_file": relative,
                "date": root.attrib.get("date", ""),
                "comments": file_comments,
                "games": games,
            }
            (json_dir / shard_rel).write_text(
                json.dumps(shard, ensure_ascii=False, indent=2) + "\n",
                encoding="utf-8",
            )
            includes.append(shard_rel)

        manifest = {
            "format": JSON_MANIFEST_FORMAT,
            "version": FORMAT_VERSION,
            "source": input_path.name,
            "root_xml": root_xml.name,
            "overrides": "" if overrides_path is None else overrides_path.name,
            "bindings": bindings,
            "includes": includes,
            "statistics": {
                "files": len(files),
                "games": total_games,
                "literal_titles": total_titles,
                "ranges": total_ranges,
                "assets": total_assets,
            },
        }
        manifest_path = json_dir / "hoot.catalog.json"
        manifest_path.write_text(
            json.dumps(manifest, ensure_ascii=False, indent=2) + "\n",
            encoding="utf-8",
        )
        return manifest_path
    finally:
        source.close()


def iter_json_games(manifest_path: Path) -> Iterable[dict[str, Any]]:
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    if manifest.get("format") == JSON_SHARD_FORMAT:
        yield from manifest.get("games", [])
        return
    if manifest.get("format") != JSON_MANIFEST_FORMAT:
        raise RuntimeError(f"unsupported JSON format in {manifest_path}")
    for include in manifest.get("includes", []):
        shard_path = manifest_path.parent / include
        shard = json.loads(shard_path.read_text(encoding="utf-8"))
        if shard.get("format") != JSON_SHARD_FORMAT:
            raise RuntimeError(f"unsupported shard format in {shard_path}")
        yield from shard.get("games", [])


def expanded_tracks(game: dict[str, Any]) -> Iterable[dict[str, Any]]:
    ordinal = 0
    for entry in game.get("title_entries", []):
        kind = entry.get("kind")
        if kind == "title":
            yield {
                "ordinal": ordinal,
                "code": parse_num(entry.get("code")) or 0,
                "title": entry.get("title", ""),
                "voice_bank": entry.get("voice_bank", ""),
            }
            ordinal += 1
        elif kind == "range":
            minimum = parse_num(entry.get("min")) or 0
            maximum = parse_num(entry.get("max")) or minimum
            start = parse_num(entry.get("start"))
            for code in range(minimum, maximum + 1):
                display_code = code if start is None else start + (code - minimum)
                yield {
                    "ordinal": ordinal,
                    "code": code,
                    "title": format_code(entry.get("title_format", ""), display_code),
                    "voice_bank": "",
                }
                ordinal += 1


def load_manifest(manifest_path: Path) -> dict[str, Any]:
    return json.loads(manifest_path.read_text(encoding="utf-8"))


def build_sqlite(manifest_path: Path, sqlite_path: Path) -> dict[str, int]:
    sqlite_path.parent.mkdir(parents=True, exist_ok=True)
    if sqlite_path.exists():
        sqlite_path.unlink()

    manifest = load_manifest(manifest_path)
    conn = sqlite3.connect(sqlite_path)
    try:
        conn.executescript(
            """
            PRAGMA page_size=4096;
            PRAGMA journal_mode=OFF;
            PRAGMA synchronous=OFF;
            PRAGMA temp_store=MEMORY;
            PRAGMA foreign_keys=ON;
            PRAGMA user_version=1;

            CREATE TABLE meta (
                key TEXT PRIMARY KEY,
                value TEXT NOT NULL
            ) WITHOUT ROWID;

            CREATE TABLE games (
                id TEXT PRIMARY KEY,
                source_order INTEGER NOT NULL UNIQUE,
                source_file TEXT NOT NULL,
                title TEXT NOT NULL,
                driver_name TEXT NOT NULL,
                driver_type TEXT NOT NULL,
                driver_alias TEXT NOT NULL,
                platform TEXT NOT NULL,
                archive TEXT NOT NULL,
                default_sample_rate INTEGER NOT NULL,
                refresh_hz INTEGER NOT NULL
            ) WITHOUT ROWID;

            CREATE TABLE game_options (
                game_id TEXT NOT NULL REFERENCES games(id) ON DELETE CASCADE,
                ordinal INTEGER NOT NULL,
                name TEXT NOT NULL,
                value INTEGER NOT NULL,
                value_text TEXT NOT NULL,
                PRIMARY KEY (game_id, ordinal)
            ) WITHOUT ROWID;

            CREATE TABLE assets (
                game_id TEXT NOT NULL REFERENCES games(id) ON DELETE CASCADE,
                ordinal INTEGER NOT NULL,
                type TEXT NOT NULL,
                path TEXT NOT NULL,
                transform TEXT NOT NULL,
                offset INTEGER NOT NULL,
                crc32 INTEGER,
                PRIMARY KEY (game_id, ordinal)
            ) WITHOUT ROWID;

            CREATE TABLE title_entries (
                game_id TEXT NOT NULL REFERENCES games(id) ON DELETE CASCADE,
                ordinal INTEGER NOT NULL,
                kind TEXT NOT NULL,
                code INTEGER,
                title TEXT,
                title_type TEXT,
                range_min INTEGER,
                range_max INTEGER,
                extcode INTEGER,
                start_code INTEGER,
                title_format TEXT,
                comment_text TEXT,
                PRIMARY KEY (game_id, ordinal)
            ) WITHOUT ROWID;

            CREATE TABLE tracks (
                game_id TEXT NOT NULL REFERENCES games(id) ON DELETE CASCADE,
                ordinal INTEGER NOT NULL,
                code INTEGER NOT NULL,
                title TEXT NOT NULL,
                voice_bank TEXT NOT NULL,
                PRIMARY KEY (game_id, ordinal)
            ) WITHOUT ROWID;

            CREATE TABLE bindings (
                binding_id INTEGER PRIMARY KEY,
                driver_name TEXT NOT NULL,
                driver_type TEXT NOT NULL
            );

            CREATE TABLE binding_extensions (
                binding_id INTEGER NOT NULL REFERENCES bindings(binding_id) ON DELETE CASCADE,
                ordinal INTEGER NOT NULL,
                extension TEXT NOT NULL,
                PRIMARY KEY (binding_id, ordinal)
            ) WITHOUT ROWID;

            CREATE TABLE binding_options (
                binding_id INTEGER NOT NULL REFERENCES bindings(binding_id) ON DELETE CASCADE,
                ordinal INTEGER NOT NULL,
                name TEXT NOT NULL,
                value INTEGER NOT NULL,
                value_text TEXT NOT NULL,
                PRIMARY KEY (binding_id, ordinal)
            ) WITHOUT ROWID;

            CREATE INDEX games_archive_idx ON games(archive);
            CREATE INDEX games_driver_idx ON games(driver_name, driver_type);
            CREATE INDEX tracks_code_idx ON tracks(game_id, code);
            """
        )
        meta = {
            "format": SQLITE_FORMAT,
            "schema_version": str(FORMAT_VERSION),
            "json_manifest": manifest_path.name,
            "source": str(manifest.get("source", "")),
            "zstd_level": "19",
        }
        conn.executemany("INSERT INTO meta(key,value) VALUES(?,?)", meta.items())

        for binding_id, binding in enumerate(manifest.get("bindings", []), start=1):
            driver = binding.get("driver", {})
            conn.execute(
                "INSERT INTO bindings(binding_id,driver_name,driver_type) VALUES(?,?,?)",
                (binding_id, driver.get("name", ""), driver.get("type", "")),
            )
            conn.executemany(
                "INSERT INTO binding_extensions(binding_id,ordinal,extension) VALUES(?,?,?)",
                [(binding_id, i, ext) for i, ext in enumerate(binding.get("extensions", []))],
            )
            option_rows = []
            for i, option in enumerate(binding.get("options", [])):
                raw = str(option.get("value", "0"))
                option_rows.append((binding_id, i, option.get("name", ""), parse_num(raw) or 0, raw))
            conn.executemany(
                "INSERT INTO binding_options(binding_id,ordinal,name,value,value_text) VALUES(?,?,?,?,?)",
                option_rows,
            )

        counts = {"games": 0, "options": 0, "assets": 0, "title_entries": 0, "tracks": 0}
        for game in iter_json_games(manifest_path):
            driver = game.get("driver", {})
            conn.execute(
                """INSERT INTO games(
                       id,source_order,source_file,title,driver_name,driver_type,
                       driver_alias,platform,archive,default_sample_rate,refresh_hz)
                   VALUES(?,?,?,?,?,?,?,?,?,?,?)""",
                (
                    game.get("id", ""),
                    int(game.get("source_order", counts["games"])),
                    game.get("source_file", ""),
                    game.get("title", ""),
                    driver.get("name", ""),
                    driver.get("type", ""),
                    driver.get("alias", ""),
                    driver.get("platform", ""),
                    game.get("archive", ""),
                    int(game.get("default_sample_rate", 44100)),
                    int(game.get("refresh_hz", 60)),
                ),
            )
            game_id = game.get("id", "")
            counts["games"] += 1

            option_rows = []
            for i, option in enumerate(game.get("options", [])):
                raw = str(option.get("value", "0"))
                option_rows.append((game_id, i, option.get("name", ""), parse_num(raw) or 0, raw))
            conn.executemany(
                "INSERT INTO game_options(game_id,ordinal,name,value,value_text) VALUES(?,?,?,?,?)",
                option_rows,
            )
            counts["options"] += len(option_rows)

            asset_rows = []
            for i, asset in enumerate(game.get("assets", [])):
                crc = parse_num(asset.get("crc32"))
                asset_rows.append((
                    game_id,
                    i,
                    asset.get("type", ""),
                    asset.get("path", ""),
                    asset.get("transform", ""),
                    parse_num(asset.get("offset")) or 0,
                    crc,
                ))
            conn.executemany(
                "INSERT INTO assets(game_id,ordinal,type,path,transform,offset,crc32) VALUES(?,?,?,?,?,?,?)",
                asset_rows,
            )
            counts["assets"] += len(asset_rows)

            title_rows = []
            for i, entry in enumerate(game.get("title_entries", [])):
                kind = entry.get("kind", "")
                title_rows.append((
                    game_id,
                    i,
                    kind,
                    parse_num(entry.get("code")),
                    entry.get("title"),
                    entry.get("type"),
                    parse_num(entry.get("min")),
                    parse_num(entry.get("max")),
                    parse_num(entry.get("extcode")),
                    parse_num(entry.get("start")),
                    entry.get("title_format"),
                    entry.get("text") if kind == "comment" else None,
                ))
            conn.executemany(
                """INSERT INTO title_entries(
                       game_id,ordinal,kind,code,title,title_type,range_min,range_max,
                       extcode,start_code,title_format,comment_text)
                   VALUES(?,?,?,?,?,?,?,?,?,?,?,?)""",
                title_rows,
            )
            counts["title_entries"] += len(title_rows)

            track_rows = [
                (game_id, track["ordinal"], track["code"], track["title"], track["voice_bank"])
                for track in expanded_tracks(game)
            ]
            conn.executemany(
                "INSERT INTO tracks(game_id,ordinal,code,title,voice_bank) VALUES(?,?,?,?,?)",
                track_rows,
            )
            counts["tracks"] += len(track_rows)

        conn.commit()
        integrity = conn.execute("PRAGMA integrity_check").fetchone()[0]
        if integrity != "ok":
            raise RuntimeError(f"SQLite integrity check failed: {integrity}")
        conn.execute("VACUUM")
        conn.commit()
        return counts
    finally:
        conn.close()


def compress_zstd(sqlite_path: Path, output_path: Path, level: int) -> None:
    output_path.parent.mkdir(parents=True, exist_ok=True)
    zstd = shutil.which("zstd")
    if zstd is None:
        raise RuntimeError("zstd executable not found; install zstd to build the compressed catalogue")
    subprocess.run(
        [zstd, f"-{level}", "--force", "--quiet", "-o", str(output_path), str(sqlite_path)],
        check=True,
    )


def verify_sqlite(sqlite_path: Path) -> dict[str, int]:
    conn = sqlite3.connect(f"file:{sqlite_path}?mode=ro", uri=True)
    try:
        format_value = conn.execute("SELECT value FROM meta WHERE key='format'").fetchone()
        if not format_value or format_value[0] != SQLITE_FORMAT:
            raise RuntimeError("not a Hoot SQLite catalogue")
        return {
            "games": conn.execute("SELECT count(*) FROM games").fetchone()[0],
            "options": conn.execute("SELECT count(*) FROM game_options").fetchone()[0],
            "assets": conn.execute("SELECT count(*) FROM assets").fetchone()[0],
            "title_entries": conn.execute("SELECT count(*) FROM title_entries").fetchone()[0],
            "tracks": conn.execute("SELECT count(*) FROM tracks").fetchone()[0],
        }
    finally:
        conn.close()


def command_build(args: argparse.Namespace) -> int:
    manifest = import_xml(Path(args.input), Path(args.json_dir), Path(args.overrides) if args.overrides else None)
    counts = build_sqlite(manifest, Path(args.sqlite))
    if args.zstd:
        compress_zstd(Path(args.sqlite), Path(args.zstd), args.level)
    print(f"JSON manifest: {manifest}")
    print(f"SQLite: {args.sqlite}")
    if args.zstd:
        print(f"Zstandard level {args.level}: {args.zstd}")
    print("Counts: " + ", ".join(f"{key}={value}" for key, value in counts.items()))
    return 0


def command_import(args: argparse.Namespace) -> int:
    manifest = import_xml(Path(args.input), Path(args.json_dir), Path(args.overrides) if args.overrides else None)
    print(manifest)
    return 0


def command_sqlite(args: argparse.Namespace) -> int:
    counts = build_sqlite(Path(args.manifest), Path(args.sqlite))
    if args.zstd:
        compress_zstd(Path(args.sqlite), Path(args.zstd), args.level)
    print("Counts: " + ", ".join(f"{key}={value}" for key, value in counts.items()))
    return 0


def command_verify(args: argparse.Namespace) -> int:
    counts = verify_sqlite(Path(args.sqlite))
    print("Counts: " + ", ".join(f"{key}={value}" for key, value in counts.items()))
    return 0


def make_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="command", required=True)

    build = sub.add_parser("build", help="XML/ZIP -> sharded JSON -> SQLite -> zstd")
    build.add_argument("--input", required=True, help="hoot.xml, XML directory, or XML zip")
    build.add_argument("--json-dir", required=True)
    build.add_argument("--overrides", help="optional hoot-overrides.xml to compile into JSON/SQLite")
    build.add_argument("--sqlite", required=True)
    build.add_argument("--zstd")
    build.add_argument("--level", type=int, default=19)
    build.set_defaults(func=command_build)

    imp = sub.add_parser("import-xml", help="XML/ZIP -> sharded JSON")
    imp.add_argument("--input", required=True)
    imp.add_argument("--json-dir", required=True)
    imp.add_argument("--overrides", help="optional hoot-overrides.xml to compile into JSON")
    imp.set_defaults(func=command_import)

    sql = sub.add_parser("build-sqlite", help="JSON manifest -> SQLite and optional zstd")
    sql.add_argument("--manifest", required=True)
    sql.add_argument("--sqlite", required=True)
    sql.add_argument("--zstd")
    sql.add_argument("--level", type=int, default=19)
    sql.set_defaults(func=command_sqlite)

    verify = sub.add_parser("verify", help="verify a generated SQLite catalogue")
    verify.add_argument("--sqlite", required=True)
    verify.set_defaults(func=command_verify)
    return parser


def main() -> int:
    try:
        args = make_parser().parse_args()
        if hasattr(args, "level") and not (1 <= args.level <= 22):
            raise RuntimeError("zstd level must be between 1 and 22")
        return args.func(args)
    except (OSError, RuntimeError, ValueError, subprocess.CalledProcessError, sqlite3.Error) as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
