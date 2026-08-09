#!/usr/bin/env python3
"""Audit every explicit X68000 generic PCM8 catalogue configuration.

The matrix is catalogue-complete even when copyrighted archives are absent.
Available configurations are executed one fresh hootprobe process per track.
Configurations with midiout are treated as external-synth targets: CPU/driver
health is validated, but local rendered silence is not an audio failure.
"""

from __future__ import annotations

import argparse
import json
import os
import subprocess
import sys
import tempfile
from collections import Counter
from pathlib import Path
from typing import Any, Iterable

PCM8_COUNTERS = (
    "commands", "starts", "stops", "mode_changes", "queries",
    "unimplemented", "unknown", "unsupported_channels",
    "rendered_voice_frames", "rendered_source_bytes", "completed_voices",
    "memory_faults",
)
HARD_COUNTERS = ("unimplemented", "unknown", "unsupported_channels", "memory_faults")


def parse_args() -> argparse.Namespace:
    p = argparse.ArgumentParser()
    p.add_argument("--probe", default="build/hootprobe")
    p.add_argument("--catalog", default="catalog/hoot.sqlite.zst")
    p.add_argument("--catalog-json", default="catalog-src")
    p.add_argument("--packs", default="packs")
    p.add_argument("--output", default="x68k-pcm8-catalog-matrix.json")
    p.add_argument("--seconds", type=int, default=2)
    p.add_argument("--startup-grace", type=int, default=1)
    p.add_argument("--timeout", type=int, default=60)
    p.add_argument("--require-all", action="store_true",
                   help="fail when any referenced archive is unavailable")
    return p.parse_args()


def number(value: Any) -> int:
    text = str(value or "0").strip()
    try:
        return int(text, 0)
    except ValueError:
        return 0


def walk_games(node: Any) -> Iterable[dict[str, Any]]:
    if isinstance(node, dict):
        if "driver" in node and "id" in node and "archive" in node:
            yield node
        for value in node.values():
            yield from walk_games(value)
    elif isinstance(node, list):
        for value in node:
            yield from walk_games(value)


def pcm8_entries(json_dir: Path) -> list[dict[str, Any]]:
    entries: list[dict[str, Any]] = []
    for path in sorted((json_dir / "shards").glob("*.json")):
        doc = json.loads(path.read_text(encoding="utf-8"))
        for game in walk_games(doc):
            driver = game.get("driver") or {}
            opts = {str(o.get("name")): o.get("value") for o in game.get("options", [])}
            if (driver.get("name"), driver.get("type")) != ("x68k", "generic"):
                continue
            if number(opts.get("pcm8")) == 0:
                continue
            entries.append({
                "id": str(game["id"]),
                "title": str(game.get("title", "")),
                "archive": str(game["archive"]),
                "source_file": str(game.get("source_file", "")),
                "options": opts,
                "external_midi": number(opts.get("midiout")) != 0,
            })
    entries.sort(key=lambda item: item["id"])
    return entries


def run_probe(args: argparse.Namespace, entry_id: str, output: Path,
              track: int | None = None) -> dict[str, Any]:
    cmd = [
        str(Path(args.probe)), "--catalog", str(Path(args.catalog)),
        "--packs", str(Path(args.packs)), "--entry", entry_id,
        "--seconds", str(args.seconds), "--startup-grace", str(args.startup_grace),
        "--timeout", str(args.timeout), "--output", str(output), "--quiet",
    ]
    cmd += ["--track", str(track)] if track is not None else ["--max-tracks", "1"]
    env = os.environ.copy()
    env["HOOT_X68K_STARTUP"] = "auto"
    completed = subprocess.run(cmd, check=False, env=env)
    if not output.is_file():
        raise RuntimeError(f"hootprobe did not create {output} (exit {completed.returncode})")
    report = json.loads(output.read_text(encoding="utf-8"))
    found = report.get("entries") or []
    if len(found) != 1:
        raise RuntimeError(f"expected one entry for {entry_id}, got {len(found)}")
    return found[0]


def compact_track(track: dict[str, Any]) -> dict[str, Any]:
    diag = track.get("diagnostics") or {}
    return {
        "index": int(track.get("index", -1)),
        "code": int(track.get("code", 0)),
        "title": str(track.get("title", "")),
        "result": str(track.get("result", "unknown")),
        "error": str(track.get("error", "")),
        "first_audible_frame": int(track.get("first_audible_frame", -1)),
        "audio": track.get("audio") or {},
        "unsupported_opcodes": int(diag.get("unsupported_opcodes", 0) or 0),
        "warning": str(diag.get("warning", "")),
        "x68k_startup": diag.get("x68k_startup") or {},
        "pcm8": diag.get("pcm8") or {},
    }


def main() -> int:
    args = parse_args()
    if args.seconds <= 0 or args.startup_grace < 0 or args.timeout <= 0:
        print("invalid duration or timeout", file=sys.stderr)
        return 2
    for required in (Path(args.probe), Path(args.catalog)):
        if not required.is_file():
            print(f"file not found: {required}", file=sys.stderr)
            return 2
    json_dir = Path(args.catalog_json)
    packs = Path(args.packs)
    if not (json_dir / "shards").is_dir() or not packs.is_dir():
        print("catalog JSON shards or pack directory not found", file=sys.stderr)
        return 2

    catalog_entries = pcm8_entries(json_dir)
    failures: list[str] = []
    manual_startup_overrides = [
        item["id"] for item in catalog_entries if "hoot_startup" in item["options"]
    ]
    if manual_startup_overrides:
        failures.append(
            "manual hoot_startup overrides remain: " + ", ".join(manual_startup_overrides))
    results: list[dict[str, Any]] = []
    status_counts: Counter[str] = Counter()
    startup_modes: Counter[str] = Counter()
    aggregate = {name: 0 for name in PCM8_COUNTERS}
    tracks_executed = 0
    tracks_audible = 0
    clipped_total = 0

    with tempfile.TemporaryDirectory(prefix="hoot-x68k-pcm8-matrix-") as td:
        temp = Path(td)
        for ordinal, spec in enumerate(catalog_entries):
            archive_path = packs / f"{spec['archive']}.zip"
            row: dict[str, Any] = {
                **spec,
                "archive_present": archive_path.is_file(),
                "track_count": 0,
                "status": "missing-archive",
                "tracks": [],
                "validation": {},
            }
            if not archive_path.is_file():
                status_counts[row["status"]] += 1
                if args.require_all:
                    failures.append(f"{spec['id']}: missing {archive_path.name}")
                results.append(row)
                print(f"{spec['id']}: missing {archive_path.name}")
                continue

            try:
                meta = run_probe(args, spec["id"], temp / f"{ordinal:02d}-meta.json")
            except Exception as exc:  # catalogue matrix must retain later rows
                row["status"] = "probe-error"
                row["validation"] = {"error": str(exc)}
                failures.append(f"{spec['id']}: {exc}")
                status_counts[row["status"]] += 1
                results.append(row)
                continue

            scan = meta.get("scan") or {}
            if scan.get("load_error"):
                row["status"] = "load-error"
                row["validation"] = {"error": scan["load_error"]}
                failures.append(f"{spec['id']}: {scan['load_error']}")
                status_counts[row["status"]] += 1
                results.append(row)
                continue

            track_count = int(meta.get("track_count", 0) or 0)
            row["track_count"] = track_count
            counters = {name: 0 for name in PCM8_COUNTERS}
            bad_tracks: list[str] = []
            audible = 0
            clipped = 0
            warnings = 0
            unsupported = 0
            fallbacks = 0

            for track_index in range(track_count):
                item = run_probe(
                    args, spec["id"],
                    temp / f"{ordinal:02d}-track-{track_index:03d}.json",
                    track=track_index,
                )
                tracks = ((item.get("scan") or {}).get("tracks") or [])
                if len(tracks) != 1:
                    bad_tracks.append(f"track {track_index}: no result")
                    continue
                track = compact_track(tracks[0])
                row["tracks"].append(track)
                tracks_executed += 1
                result = track["result"]
                if result == "audio-active":
                    audible += 1
                    tracks_audible += 1
                elif spec["external_midi"] and result == "silent":
                    pass
                elif result not in ("control-silent", "control-tail"):
                    bad_tracks.append(f"track {track_index}: {result}")
                if track["error"]:
                    bad_tracks.append(f"track {track_index}: {track['error']}")
                if track["warning"]:
                    warnings += 1
                    bad_tracks.append(f"track {track_index}: warning: {track['warning']}")
                unsupported += track["unsupported_opcodes"]
                if track["unsupported_opcodes"]:
                    bad_tracks.append(
                        f"track {track_index}: {track['unsupported_opcodes']} unsupported opcodes")
                clipped_here = int(track["audio"].get("clipped_samples", 0) or 0)
                clipped += clipped_here
                clipped_total += clipped_here
                pcm8 = track["pcm8"]
                for name in PCM8_COUNTERS:
                    value = int(pcm8.get(name, 0) or 0)
                    counters[name] += value
                    aggregate[name] += value
                startup = track["x68k_startup"]
                resolved = str(startup.get("resolved", "unknown"))
                startup_modes[resolved] += 1
                fallbacks += int(startup.get("fallbacks", 0) or 0)

            if bad_tracks:
                row["status"] = "failed"
                failures.extend(f"{spec['id']}: {message}" for message in bad_tracks)
            elif spec["external_midi"]:
                row["status"] = "external-midi-backend-required"
            else:
                row["status"] = "validated"

            if not spec["external_midi"]:
                if audible == 0:
                    failures.append(f"{spec['id']}: no audible track")
                    row["status"] = "failed"
                for name in HARD_COUNTERS:
                    if counters[name]:
                        failures.append(f"{spec['id']}: {counters[name]} {name}")
                        row["status"] = "failed"
                if counters["commands"] == 0:
                    failures.append(f"{spec['id']}: no PCM8 commands")
                    row["status"] = "failed"
                if counters["starts"] == 0 or counters["rendered_voice_frames"] == 0:
                    failures.append(f"{spec['id']}: no rendered direct PCM8 playback")
                    row["status"] = "failed"
            if clipped:
                failures.append(f"{spec['id']}: {clipped} clipped samples")
                row["status"] = "failed"

            row["validation"] = {
                "tracks_scanned": len(row["tracks"]),
                "audible_tracks": audible,
                "warnings": warnings,
                "unsupported_opcodes": unsupported,
                "clipped_samples": clipped,
                "startup_fallbacks": fallbacks,
                "pcm8_counters": counters,
            }
            status_counts[row["status"]] += 1
            results.append(row)
            print(
                f"{spec['id']}: {row['status']} tracks={track_count} audible={audible} "
                f"startup-fallbacks={fallbacks} pcm8-starts={counters['starts']}")

    report = {
        "format": "hoot-x68k-pcm8-catalog-matrix-v1",
        "catalog_complete": True,
        "fresh_process_per_track": True,
        "startup_policy": "auto",
        "probe_seconds": args.seconds,
        "startup_grace_seconds": args.startup_grace,
        "entries": results,
        "summary": {
            "catalog_pcm8_configurations": len(catalog_entries),
            "distinct_archives": len({item["archive"] for item in catalog_entries}),
            "available_configurations": sum(1 for item in results if item["archive_present"]),
            "missing_configurations": sum(1 for item in results if not item["archive_present"]),
            "tracks_executed": tracks_executed,
            "audible_tracks": tracks_audible,
            "statuses": dict(sorted(status_counts.items())),
            "startup_resolutions": dict(sorted(startup_modes.items())),
            "pcm8_counters": aggregate,
            "clipped_samples": clipped_total,
            "require_all": args.require_all,
            "manual_startup_overrides": manual_startup_overrides,
            "failures": failures,
        },
    }
    Path(args.output).write_text(
        json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")

    if failures:
        print("x68k PCM8 catalogue matrix: FAIL", file=sys.stderr)
        for failure in failures:
            print(f"  {failure}", file=sys.stderr)
        return 1
    if report["summary"]["missing_configurations"]:
        print("x68k PCM8 catalogue matrix: PASS (available archives; missing archives recorded)")
    else:
        print("x68k PCM8 catalogue matrix: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
