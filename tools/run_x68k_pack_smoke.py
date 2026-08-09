#!/usr/bin/env python3
"""Run the deterministic plain-OPM X68000 regression matrix.

The copyrighted Hoot packs are not distributed with this project. Place these
archives in --packs: ad68snd.zip, fz68snd.zip, gra68snd.zip, paro68snd.zip,
xak68snd.zip and ys68snd.zip. Only the proven generic OPM entries are scanned;
MIDI variants are intentionally excluded from this gate.
"""

from __future__ import annotations

import argparse
import json
import subprocess
import sys
import tempfile
from pathlib import Path
from typing import Any

BASELINE = {
    "ad68snd-generic": {"tracks": 22, "audio": 22, "controls": 0},
    "fz68snd-generic": {"tracks": 34, "audio": 32, "controls": 2},
    "gra68snd-generic": {"tracks": 13, "audio": 12, "controls": 1},
    "paro68snd-generic": {"tracks": 36, "audio": 36, "controls": 0},
    "xak68snd-generic": {"tracks": 48, "audio": 47, "controls": 1},
    "ys68snd-generic": {"tracks": 43, "audio": 42, "controls": 1},
}
CONTROL_RESULTS = {"control-silent", "control-tail"}


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--probe", default="build/hootprobe", help="hootprobe executable")
    parser.add_argument("--catalog", default="catalog/hoot.sqlite.zst")
    parser.add_argument("--packs", default="packs")
    parser.add_argument("--output", default="x68k-pack-smoke.json")
    parser.add_argument("--seconds", type=int, default=1)
    parser.add_argument("--startup-grace", type=int, default=3)
    parser.add_argument("--timeout", type=int, default=60)
    return parser.parse_args()


def fail(message: str) -> None:
    print(f"x68k OPM smoke: {message}", file=sys.stderr)
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
        fail(f"hootprobe did not create a report for {entry_id}")
    report = json.loads(output.read_text(encoding="utf-8"))
    entries = report.get("entries") or []
    if len(entries) != 1:
        fail(f"expected one report entry for {entry_id}, got {len(entries)}")
    return entries[0]


def main() -> int:
    args = parse_args()
    if args.seconds <= 0 or args.startup_grace < 0 or args.timeout <= 0:
        fail("invalid duration or timeout")
    for required in (Path(args.probe), Path(args.catalog)):
        if not required.is_file():
            fail(f"file not found: {required}")
    if not Path(args.packs).is_dir():
        fail(f"pack directory not found: {args.packs}")

    entries: list[dict[str, Any]] = []
    failures: list[str] = []
    total_audio = 0
    total_controls = 0

    with tempfile.TemporaryDirectory(prefix="hoot-x68k-opm-") as temp:
        for entry_id, expected in BASELINE.items():
            entry = run_entry(args, entry_id, Path(temp) / f"{entry_id}.json")
            entries.append(entry)
            tracks = (entry.get("scan") or {}).get("tracks") or []
            audio = [track for track in tracks if track.get("result") == "audio-active"]
            controls = [track for track in tracks if track.get("result") in CONTROL_RESULTS]
            bad = [track for track in tracks
                   if track.get("result") not in CONTROL_RESULTS | {"audio-active"}]
            errors = [track for track in tracks if track.get("error")]
            unsupported = sum(
                int((track.get("diagnostics") or {}).get("unsupported_opcodes", 0) or 0)
                for track in tracks
            )
            warnings = [
                str((track.get("diagnostics") or {}).get("warning", ""))
                for track in tracks
            ]
            clipped = sum(
                int((track.get("audio") or {}).get("clipped_samples", 0) or 0)
                for track in tracks
            )

            if len(tracks) != expected["tracks"]:
                failures.append(
                    f"{entry_id}: expected {expected['tracks']} tracks, got {len(tracks)}")
            if len(audio) != expected["audio"]:
                failures.append(
                    f"{entry_id}: expected {expected['audio']} audio tracks, got {len(audio)}")
            if len(controls) != expected["controls"]:
                failures.append(
                    f"{entry_id}: expected {expected['controls']} controls, got {len(controls)}")
            if bad:
                failures.append(f"{entry_id}: {len(bad)} unexpected track outcomes")
            if errors:
                failures.append(f"{entry_id}: {len(errors)} track errors")
            if unsupported:
                failures.append(f"{entry_id}: {unsupported} unsupported CPU opcodes")
            if any(warnings):
                failures.append(f"{entry_id}: unexpected driver warning(s)")
            if clipped:
                failures.append(f"{entry_id}: {clipped} clipped samples")

            total_audio += len(audio)
            total_controls += len(controls)
            print(
                f"{entry_id}: audio={len(audio)}, controls={len(controls)}, clipped={clipped}")

    aggregate = {
        "format": "hoot-x68k-plain-opm-smoke-v2",
        "probe_seconds": args.seconds,
        "startup_grace_seconds": args.startup_grace,
        "entries": entries,
        "summary": {
            "entries": len(entries),
            "catalog_commands": total_audio + total_controls,
            "audio_active_tracks": total_audio,
            "control_commands": total_controls,
            "failures": failures,
        },
    }
    Path(args.output).write_text(
        json.dumps(aggregate, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")

    if failures:
        print("x68k OPM smoke: FAIL", file=sys.stderr)
        for item in failures:
            print(f"  {item}", file=sys.stderr)
        return 1
    print("x68k OPM smoke: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
