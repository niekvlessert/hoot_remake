#!/usr/bin/env python3
"""Run the X68000 MFP/IOCS compatibility matrix.

The copyrighted Hoot packs are not distributed with this project. Place these
archives in --packs: shooting68snd.zip, ngear68snd.zip, nvgml68snd.zip and
a268snd.zip. All four entries form the pass gate, including A-Train II's
FLOAT2.X/line-F and wrapped-high-workspace path.
"""

from __future__ import annotations

import argparse
import json
import subprocess
import sys
import tempfile
from pathlib import Path
from typing import Any

WORKING = {
    "shooting68snd-generic": {"tracks": 9, "max_clipped": 0},
    "ngear68snd-generic": {"tracks": 17, "max_clipped": 8},
    "nvgml68snd-generic": {"tracks": 443, "max_clipped": 0},
    "a268snd-generic": {"tracks": 14, "max_clipped": 0},
}


def arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--probe", default="build/hootprobe")
    parser.add_argument("--catalog", default="catalog/hoot.sqlite.zst")
    parser.add_argument("--packs", default="packs")
    parser.add_argument("--output", default="x68k-mfp-iocs-smoke.json")
    parser.add_argument("--seconds", type=int, default=1)
    parser.add_argument("--startup-grace", type=int, default=5)
    parser.add_argument("--timeout", type=int, default=240)
    return parser.parse_args()


def die(message: str) -> None:
    print(f"x68k MFP/IOCS smoke: {message}", file=sys.stderr)
    raise SystemExit(1)


def run_entry(args: argparse.Namespace, entry_id: str, output: Path) -> dict[str, Any]:
    command = [
        str(Path(args.probe)),
        "--catalog", str(Path(args.catalog)),
        "--packs", str(Path(args.packs)),
        "--entry", entry_id,
        "--all-tracks",
        "--seconds", str(args.seconds),
        "--startup-grace", str(args.startup_grace),
        "--timeout", str(args.timeout),
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


def main() -> int:
    args = arguments()
    if args.seconds <= 0 or args.startup_grace < 0 or args.timeout <= 0:
        die("invalid duration or timeout")
    for required in (Path(args.probe), Path(args.catalog)):
        if not required.is_file():
            die(f"file not found: {required}")
    if not Path(args.packs).is_dir():
        die(f"pack directory not found: {args.packs}")

    entry_ids = list(WORKING)

    entries: list[dict[str, Any]] = []
    failures: list[str] = []
    with tempfile.TemporaryDirectory(prefix="hoot-x68k-mfp-") as temp:
        for entry_id in entry_ids:
            entry = run_entry(args, entry_id, Path(temp) / f"{entry_id}.json")
            entries.append(entry)
            tracks = (entry.get("scan") or {}).get("tracks") or []

            expected = WORKING[entry_id]
            if len(tracks) != expected["tracks"]:
                failures.append(f"{entry_id}: expected {expected['tracks']} tracks, got {len(tracks)}")
            bad = [track for track in tracks if track.get("result") != "audio-active"]
            if bad:
                failures.append(f"{entry_id}: {len(bad)} tracks were not audio-active")
            errors = [track for track in tracks if track.get("error")]
            if errors:
                failures.append(f"{entry_id}: {len(errors)} track errors")
            unsupported = sum(int((track.get("diagnostics") or {}).get("unsupported_opcodes", 0) or 0)
                              for track in tracks)
            if unsupported:
                failures.append(f"{entry_id}: {unsupported} unsupported CPU opcodes")
            warnings = [str((track.get("diagnostics") or {}).get("warning", ""))
                        for track in tracks]
            if any(warnings):
                failures.append(f"{entry_id}: unexpected driver warning(s)")
            clipped = sum(int((track.get("audio") or {}).get("clipped_samples", 0) or 0)
                          for track in tracks)
            if clipped > expected["max_clipped"]:
                failures.append(
                    f"{entry_id}: {clipped} clipped samples exceed allowance {expected['max_clipped']}")
            print(f"{entry_id}: {len(tracks)} audio-active tracks, clipped={clipped}")

    aggregate = {
        "format": "hoot-x68k-mfp-iocs-smoke-v2",
        "probe_seconds": args.seconds,
        "startup_grace_seconds": args.startup_grace,
        "entries": entries,
        "summary": {
            "working_entries": len(WORKING),
            "working_tracks": sum(len((entry.get("scan") or {}).get("tracks") or [])
                                  for entry in entries if entry.get("id") in WORKING),
            "known_limitations": 0,
            "failures": failures,
        },
    }
    Path(args.output).write_text(json.dumps(aggregate, ensure_ascii=False, indent=2) + "\n",
                                 encoding="utf-8")
    if failures:
        print("x68k MFP/IOCS smoke: FAIL", file=sys.stderr)
        for failure in failures:
            print(f"  {failure}", file=sys.stderr)
        return 1
    print("x68k MFP/IOCS smoke: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
