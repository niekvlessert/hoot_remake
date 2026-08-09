#!/usr/bin/env python3
"""Run the direct-block X68000 PCM8 audio gate.

The copyrighted target archives are not distributed with the project. Place
one or more of asuka68snd.zip, madstk68snd.zip and pm68snd.zip in --packs. The
gate requires recognized direct output calls, consumed guest sample bytes and
rendered PCM8 voice frames. Array chains, channels above seven, unknown calls,
memory faults and clipping remain hard failures in this stage.
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

COUNTERS = (
    "commands",
    "starts",
    "stops",
    "mode_changes",
    "queries",
    "unimplemented",
    "unknown",
    "unsupported_channels",
    "rendered_voice_frames",
    "rendered_source_bytes",
    "completed_voices",
    "memory_faults",
)


def arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--probe", default="build/hootprobe")
    parser.add_argument("--catalog", default="catalog/hoot.sqlite.zst")
    parser.add_argument("--packs", default="packs")
    parser.add_argument("--output", default="x68k-pcm8-smoke.json")
    parser.add_argument("--seconds", type=int, default=5)
    parser.add_argument("--startup-grace", type=int, default=5)
    parser.add_argument("--timeout", type=int, default=120)
    parser.add_argument("--max-tracks", type=int, default=3)
    parser.add_argument("--entry", action="append", dest="entries",
                        help="catalog entry id; repeat to override defaults")
    parser.add_argument("--skip-missing", action="store_true",
                        help="record unavailable archives instead of failing")
    return parser.parse_args()


def fail(message: str) -> None:
    print(f"x68k PCM8 smoke: {message}", file=sys.stderr)
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
        fail(f"hootprobe did not create a report for {entry_id}")
    report = json.loads(output.read_text(encoding="utf-8"))
    entries = report.get("entries") or []
    if len(entries) != 1:
        fail(f"expected one report entry for {entry_id}, got {len(entries)}")
    return entries[0]


def totals(entry: dict[str, Any]) -> tuple[dict[str, int], int, int]:
    result = {key: 0 for key in COUNTERS}
    clipped = 0
    audible_tracks = 0
    tracks = (entry.get("scan") or {}).get("tracks") or []
    for track in tracks:
        diagnostics = track.get("diagnostics") or {}
        pcm8 = diagnostics.get("pcm8") or {}
        for key in COUNTERS:
            result[key] += int(pcm8.get(key, 0) or 0)
        clipped += int((track.get("audio") or {}).get("clipped_samples", 0) or 0)
        if track.get("result") == "audio-active":
            audible_tracks += 1
    return result, clipped, audible_tracks


def main() -> int:
    args = arguments()
    if args.seconds <= 0 or args.startup_grace < 0 or args.timeout <= 0 or args.max_tracks <= 0:
        fail("invalid duration, timeout or track limit")
    for required in (Path(args.probe), Path(args.catalog)):
        if not required.is_file():
            fail(f"file not found: {required}")
    packs = Path(args.packs)
    if not packs.is_dir():
        fail(f"pack directory not found: {packs}")

    entry_ids = tuple(args.entries or DEFAULT_ENTRIES)
    scans: list[dict[str, Any]] = []
    failures: list[str] = []
    skipped: list[str] = []
    aggregate_counters = {key: 0 for key in COUNTERS}

    with tempfile.TemporaryDirectory(prefix="hoot-x68k-pcm8-smoke-") as temp:
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

            counters, clipped, audible_tracks = totals(entry)
            for key, value in counters.items():
                aggregate_counters[key] += value

            if counters["commands"] == 0:
                failures.append(f"{entry_id}: no trap-#2 PCM8 commands observed")
            if counters["starts"] == 0:
                failures.append(f"{entry_id}: no direct PCM8 starts observed")
            if counters["rendered_voice_frames"] == 0:
                failures.append(f"{entry_id}: no PCM8 voice frames rendered")
            if counters["rendered_source_bytes"] == 0:
                failures.append(f"{entry_id}: no guest PCM8 bytes consumed")
            if counters["unknown"]:
                failures.append(f"{entry_id}: {counters['unknown']} unknown PCM8 functions")
            if counters["unimplemented"]:
                failures.append(
                    f"{entry_id}: {counters['unimplemented']} array-chain calls need implementation")
            if counters["unsupported_channels"]:
                failures.append(
                    f"{entry_id}: {counters['unsupported_channels']} calls use channels above 7")
            if counters["memory_faults"]:
                failures.append(f"{entry_id}: {counters['memory_faults']} PCM8 memory faults")
            if clipped:
                failures.append(f"{entry_id}: {clipped} clipped output samples")
            if audible_tracks == 0:
                failures.append(f"{entry_id}: no audible scanned track")

            print(
                f"{entry_id}: starts={counters['starts']} frames={counters['rendered_voice_frames']} "
                f"bytes={counters['rendered_source_bytes']} completed={counters['completed_voices']} "
                f"faults={counters['memory_faults']} clipped={clipped}")

    aggregate = {
        "format": "hoot-x68k-pcm8-direct-smoke-v1",
        "probe_seconds": args.seconds,
        "startup_grace_seconds": args.startup_grace,
        "max_tracks": args.max_tracks,
        "entries": scans,
        "summary": {
            "requested_entries": len(entry_ids),
            "scanned_entries": len(scans) - len(skipped),
            "counters": aggregate_counters,
            "skipped": skipped,
            "failures": failures,
        },
    }
    Path(args.output).write_text(
        json.dumps(aggregate, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")

    if failures:
        print("x68k PCM8 smoke: FAIL", file=sys.stderr)
        for item in failures:
            print(f"  {item}", file=sys.stderr)
        return 1
    if skipped:
        print("x68k PCM8 smoke: SKIPPED (packs missing)")
    else:
        print("x68k PCM8 smoke: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
