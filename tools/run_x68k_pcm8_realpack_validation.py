#!/usr/bin/env python3
"""Validate X68000 PCM8 against real Hoot archives, one fresh process per track.

The archives are copyrighted and are intentionally not distributed. Place the
requested ZIPs in --packs. Each track is loaded in a new hootprobe process so a
track switch cannot hide bootstrap, timer, or PCM8 resident state problems.
"""

from __future__ import annotations

import argparse
import json
import subprocess
import sys
import tempfile
from pathlib import Path
from typing import Any

DEFAULT_ENTRIES = ("asuka68snd-generic", "madstk68snd-generic")
EXPECTED_STARTUP = {
    "asuka68snd-generic": ("native", 0),
    "madstk68snd-generic": ("hoot", 1),
}
COUNTERS = (
    "commands", "starts", "stops", "mode_changes", "queries",
    "unimplemented", "unknown", "unsupported_channels",
    "rendered_voice_frames", "rendered_source_bytes", "completed_voices",
    "memory_faults",
)


def parse_args() -> argparse.Namespace:
    p = argparse.ArgumentParser()
    p.add_argument("--probe", default="build/hootprobe")
    p.add_argument("--catalog", default="catalog/hoot.sqlite.zst")
    p.add_argument("--packs", default="packs")
    p.add_argument("--output", default="x68k-pcm8-realpack-validation.json")
    p.add_argument("--seconds", type=int, default=3)
    p.add_argument("--startup-grace", type=int, default=1)
    p.add_argument("--timeout", type=int, default=120)
    p.add_argument("--entry", action="append", dest="entries")
    p.add_argument("--skip-missing", action="store_true")
    return p.parse_args()


def run_probe(args: argparse.Namespace, entry_id: str, output: Path,
              track: int | None = None) -> dict[str, Any]:
    cmd = [
        str(Path(args.probe)), "--catalog", str(Path(args.catalog)),
        "--packs", str(Path(args.packs)), "--entry", entry_id,
        "--seconds", str(args.seconds), "--startup-grace", str(args.startup_grace),
        "--timeout", str(args.timeout), "--output", str(output), "--quiet",
    ]
    cmd += ["--track", str(track)] if track is not None else ["--max-tracks", "1"]
    completed = subprocess.run(cmd, check=False)
    if not output.is_file():
        raise RuntimeError(f"hootprobe did not create {output} (exit {completed.returncode})")
    report = json.loads(output.read_text(encoding="utf-8"))
    entries = report.get("entries") or []
    if len(entries) != 1:
        raise RuntimeError(f"expected one entry for {entry_id}, got {len(entries)}")
    return entries[0]


def main() -> int:
    args = parse_args()
    if args.seconds <= 0 or args.startup_grace < 0 or args.timeout <= 0:
        print("invalid duration or timeout", file=sys.stderr)
        return 2
    for required in (Path(args.probe), Path(args.catalog)):
        if not required.is_file():
            print(f"file not found: {required}", file=sys.stderr)
            return 2
    if not Path(args.packs).is_dir():
        print(f"pack directory not found: {args.packs}", file=sys.stderr)
        return 2

    entry_ids = tuple(args.entries or DEFAULT_ENTRIES)
    validated: list[dict[str, Any]] = []
    failures: list[str] = []
    skipped: list[str] = []
    aggregate = {name: 0 for name in COUNTERS}
    total_clipped = 0
    outcome_counts: dict[str, int] = {}
    startup_resolutions: dict[str, int] = {}
    startup_fallbacks = 0

    with tempfile.TemporaryDirectory(prefix="hoot-x68k-pcm8-real-") as td:
        temp = Path(td)
        for entry_ordinal, entry_id in enumerate(entry_ids):
            meta = run_probe(args, entry_id, temp / f"{entry_ordinal}-meta.json")
            scan = meta.get("scan") or {}
            if scan.get("outcome") == "missing-archive":
                message = f"{entry_id}: archive is not present"
                (skipped if args.skip_missing else failures).append(message)
                continue
            if scan.get("load_error"):
                failures.append(f"{entry_id}: {scan['load_error']}")
                continue

            entry_result = {key: value for key, value in meta.items() if key != "scan"}
            entry_result["scan"] = {"outcome": "audio-active", "load_error": "", "tracks": []}
            track_count = int(meta.get("track_count", 0))
            entry_counters = {name: 0 for name in COUNTERS}
            entry_clipped = 0
            entry_audible = 0
            entry_worst: list[str] = []

            for track_index in range(track_count):
                item = run_probe(
                    args, entry_id,
                    temp / f"{entry_ordinal}-track-{track_index:03d}.json",
                    track=track_index,
                )
                tracks = ((item.get("scan") or {}).get("tracks") or [])
                if len(tracks) != 1:
                    failures.append(f"{entry_id} track {track_index}: missing track result")
                    continue
                track = tracks[0]
                entry_result["scan"]["tracks"].append(track)
                result = str(track.get("result", "unknown"))
                outcome_counts[result] = outcome_counts.get(result, 0) + 1
                if result == "audio-active":
                    entry_audible += 1
                elif result not in ("control-silent", "control-tail"):
                    entry_worst.append(f"track {track_index}: {result}")

                diagnostics = track.get("diagnostics") or {}
                startup = diagnostics.get("x68k_startup") or {}
                resolved = str(startup.get("resolved", "unknown"))
                fallback_count = int(startup.get("fallbacks", 0) or 0)
                startup_resolutions[resolved] = startup_resolutions.get(resolved, 0) + 1
                startup_fallbacks += fallback_count
                expected = EXPECTED_STARTUP.get(entry_id)
                if expected is not None and (resolved, fallback_count) != expected:
                    failures.append(
                        f"{entry_id} track {track_index}: startup {(resolved, fallback_count)} "
                        f"!= expected {expected}")
                pcm8 = diagnostics.get("pcm8") or {}
                for name in COUNTERS:
                    value = int(pcm8.get(name, 0) or 0)
                    entry_counters[name] += value
                    aggregate[name] += value
                clipped = int((track.get("audio") or {}).get("clipped_samples", 0) or 0)
                entry_clipped += clipped
                total_clipped += clipped

            if entry_worst:
                entry_result["scan"]["outcome"] = "partial-or-error"
                failures.extend(f"{entry_id} {message}" for message in entry_worst)
            if entry_counters["commands"] == 0:
                failures.append(f"{entry_id}: no trap-#2 PCM8 commands observed")
            if entry_counters["starts"] == 0:
                failures.append(f"{entry_id}: no direct PCM8 starts observed")
            if entry_counters["rendered_voice_frames"] == 0:
                failures.append(f"{entry_id}: no PCM8 voice frames rendered")
            if entry_counters["rendered_source_bytes"] == 0:
                failures.append(f"{entry_id}: no PCM8 source bytes consumed")
            for name in ("unimplemented", "unknown", "unsupported_channels", "memory_faults"):
                if entry_counters[name]:
                    failures.append(f"{entry_id}: {entry_counters[name]} {name}")
            if entry_clipped:
                failures.append(f"{entry_id}: {entry_clipped} clipped samples")
            if entry_audible == 0:
                failures.append(f"{entry_id}: no audible tracks")

            entry_result["validation"] = {
                "tracks_scanned": len(entry_result["scan"]["tracks"]),
                "audible_tracks": entry_audible,
                "clipped_samples": entry_clipped,
                "pcm8_counters": entry_counters,
            }
            validated.append(entry_result)
            print(
                f"{entry_id}: tracks={track_count} audible={entry_audible} "
                f"starts={entry_counters['starts']} frames={entry_counters['rendered_voice_frames']} "
                f"bytes={entry_counters['rendered_source_bytes']} faults={entry_counters['memory_faults']}"
            )

    report = {
        "format": "hoot-x68k-pcm8-realpack-validation-v1",
        "fresh_process_per_track": True,
        "probe_seconds": args.seconds,
        "startup_grace_seconds": args.startup_grace,
        "entries": validated,
        "summary": {
            "requested_entries": len(entry_ids),
            "validated_entries": len(validated),
            "outcomes": outcome_counts,
            "pcm8_counters": aggregate,
            "clipped_samples": total_clipped,
            "startup_resolutions": startup_resolutions,
            "startup_fallbacks": startup_fallbacks,
            "skipped": skipped,
            "failures": failures,
        },
    }
    Path(args.output).write_text(
        json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    if failures:
        print("x68k PCM8 real-pack validation: FAIL", file=sys.stderr)
        for failure in failures:
            print(f"  {failure}", file=sys.stderr)
        return 1
    print("x68k PCM8 real-pack validation: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
