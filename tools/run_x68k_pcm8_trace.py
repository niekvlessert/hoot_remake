#!/usr/bin/env python3
"""Validate the X68000 PCM8 trap-#2 command stream before audio mixing.

Place one or more PCM8 Hoot archives in --packs. By default this scans the
three handover targets: Asuka 120%, Mad Stalker and Princess Maker. The gate
requires recognized eight-channel commands and at least one normal-output
start, while reporting chain calls and wider-channel PCM8A use as explicit
next implementation work.
"""

from __future__ import annotations

import argparse
import json
import subprocess
import sys
import tempfile
from pathlib import Path
from typing import Any

DEFAULT_ENTRIES = (
    "asuka68snd-generic",
    "madstk68snd-generic",
    "pm68snd-generic",
)


def arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--probe", default="build/hootprobe")
    parser.add_argument("--catalog", default="catalog/hoot.sqlite.zst")
    parser.add_argument("--packs", default="packs")
    parser.add_argument("--output", default="x68k-pcm8-trace.json")
    parser.add_argument("--seconds", type=int, default=3)
    parser.add_argument("--startup-grace", type=int, default=5)
    parser.add_argument("--timeout", type=int, default=120)
    parser.add_argument("--max-tracks", type=int, default=3)
    parser.add_argument("--entry", action="append", dest="entries",
                        help="catalog entry id; repeat to override defaults")
    parser.add_argument("--skip-missing", action="store_true",
                        help="record unavailable archives instead of failing")
    return parser.parse_args()


def die(message: str) -> None:
    print(f"x68k PCM8 trace: {message}", file=sys.stderr)
    raise SystemExit(1)


def run_entry(args: argparse.Namespace, entry_id: str, output: Path) -> dict[str, Any]:
    command = [
        str(Path(args.probe)),
        "--catalog", str(Path(args.catalog)),
        "--packs", str(Path(args.packs)),
        "--entry", entry_id,
        "--seconds", str(args.seconds),
        "--startup-grace", str(args.startup_grace),
        "--timeout", str(args.timeout),
        "--max-tracks", str(args.max_tracks),
        "--output", str(output),
        "--quiet",
    ]
    subprocess.run(command, check=False)
    if not output.is_file():
        die(f"hootprobe did not create a report for {entry_id}")
    report = json.loads(output.read_text(encoding="utf-8"))
    entries = report.get("entries") or []
    if len(entries) != 1:
        die(f"expected one report entry for {entry_id}, got {len(entries)}")
    return entries[0]


def pcm8_totals(entry: dict[str, Any]) -> dict[str, int]:
    totals = {
        "commands": 0,
        "starts": 0,
        "stops": 0,
        "mode_changes": 0,
        "queries": 0,
        "unimplemented": 0,
        "unknown": 0,
        "unsupported_channels": 0,
    }
    tracks = (entry.get("scan") or {}).get("tracks") or []
    for track in tracks:
        pcm8 = (track.get("diagnostics") or {}).get("pcm8") or {}
        for key in totals:
            totals[key] += int(pcm8.get(key, 0) or 0)
    return totals


def main() -> int:
    args = arguments()
    if args.seconds <= 0 or args.startup_grace < 0 or args.timeout <= 0 or args.max_tracks <= 0:
        die("invalid duration, timeout or track limit")
    for required in (Path(args.probe), Path(args.catalog)):
        if not required.is_file():
            die(f"file not found: {required}")
    packs = Path(args.packs)
    if not packs.is_dir():
        die(f"pack directory not found: {packs}")

    entry_ids = tuple(args.entries or DEFAULT_ENTRIES)
    scans: list[dict[str, Any]] = []
    failures: list[str] = []
    skipped: list[str] = []

    with tempfile.TemporaryDirectory(prefix="hoot-x68k-pcm8-") as temp:
        for ordinal, entry_id in enumerate(entry_ids):
            entry = run_entry(args, entry_id, Path(temp) / f"{ordinal}.json")
            scans.append(entry)
            scan = entry.get("scan") or {}
            if scan.get("outcome") == "missing-archive":
                message = f"{entry_id}: archive is not present"
                if args.skip_missing:
                    skipped.append(message)
                    print(message)
                    continue
                failures.append(message)
                continue
            if scan.get("load_error"):
                failures.append(f"{entry_id}: {scan['load_error']}")
                continue

            totals = pcm8_totals(entry)
            if totals["commands"] == 0:
                failures.append(f"{entry_id}: no trap-#2 PCM8 commands observed")
            if totals["starts"] == 0:
                failures.append(f"{entry_id}: no normal-output PCM8 starts observed")
            if totals["unknown"]:
                failures.append(f"{entry_id}: {totals['unknown']} unknown PCM8 functions")
            if totals["unimplemented"]:
                failures.append(
                    f"{entry_id}: {totals['unimplemented']} recognized chain calls need implementation")
            if totals["unsupported_channels"]:
                failures.append(
                    f"{entry_id}: {totals['unsupported_channels']} calls use channels above 7")
            print(
                f"{entry_id}: commands={totals['commands']} starts={totals['starts']} "
                f"unknown={totals['unknown']} chain={totals['unimplemented']} "
                f"wide-ch={totals['unsupported_channels']}")

    aggregate = {
        "format": "hoot-x68k-pcm8-trace-v1",
        "probe_seconds": args.seconds,
        "startup_grace_seconds": args.startup_grace,
        "max_tracks": args.max_tracks,
        "entries": scans,
        "summary": {
            "requested_entries": len(entry_ids),
            "scanned_entries": len(scans) - len(skipped),
            "skipped": skipped,
            "failures": failures,
        },
    }
    Path(args.output).write_text(
        json.dumps(aggregate, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")

    if failures:
        print("x68k PCM8 trace: FAIL", file=sys.stderr)
        for failure in failures:
            print(f"  {failure}", file=sys.stderr)
        return 1
    if skipped:
        print("x68k PCM8 trace: SKIPPED (packs missing)")
    else:
        print("x68k PCM8 trace: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
