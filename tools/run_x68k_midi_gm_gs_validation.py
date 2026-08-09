#!/usr/bin/env python3
"""Validate X68000 CZ-6BM1 MIDI transport plus GM/GS software rendering.

Copyrighted Hoot archives and SoundFonts are not distributed. Place Asuka's
archive in --packs and select a local GM SoundFont through HOOT_X68K_SOUNDFONT
(or let the runtime locate a conventional system SoundFont). Every track is
loaded in a fresh hootprobe process so MIDI initialization state cannot leak
between tracks.
"""
from __future__ import annotations

import argparse
import json
import os
import subprocess
import sys
import tempfile
from pathlib import Path
from typing import Any

DEFAULT_ENTRIES = ("asuka68snd-generic-2", "asuka68snd-generic-3")
EXPECTED_TYPES = {
    "asuka68snd-generic-2": 4,  # GS / SC-55 class
    "asuka68snd-generic-3": 8,  # catalogue TG-100 variant through GM backend
}
COUNTERS = (
    "bytes_enqueued", "bytes_transmitted", "channel_messages",
    "system_common_messages", "sysex_messages", "sysex_bytes",
    "running_status_messages", "malformed_bytes", "note_ons", "note_offs",
    "control_changes", "program_changes", "pitch_bends", "irq_count",
    "synth_frames", "sysex_handled",
)


def parse_args() -> argparse.Namespace:
    p = argparse.ArgumentParser()
    p.add_argument("--probe", default="build/hootprobe")
    p.add_argument("--catalog", default="catalog/hoot.sqlite.zst")
    p.add_argument("--packs", default="packs")
    p.add_argument("--output", default="x68k-midi-gm-gs-validation.json")
    p.add_argument("--seconds", type=int, default=3)
    p.add_argument("--startup-grace", type=int, default=1)
    p.add_argument("--timeout", type=int, default=120)
    p.add_argument("--entry", action="append", dest="entries")
    p.add_argument("--soundfont")
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
    env = os.environ.copy()
    if args.soundfont:
        env["HOOT_X68K_SOUNDFONT"] = str(Path(args.soundfont))
    completed = subprocess.run(cmd, env=env, stdout=subprocess.PIPE,
                               stderr=subprocess.PIPE, text=True, check=False)
    if not output.is_file():
        detail = completed.stderr.strip()[-1000:]
        raise RuntimeError(
            f"hootprobe did not create {output} (exit {completed.returncode}): {detail}")
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
    if args.soundfont and not Path(args.soundfont).is_file():
        print(f"SoundFont not found: {args.soundfont}", file=sys.stderr)
        return 2

    entry_ids = tuple(args.entries or DEFAULT_ENTRIES)
    validated: list[dict[str, Any]] = []
    failures: list[str] = []
    aggregate = {name: 0 for name in COUNTERS}
    total_clipped = 0
    total_tracks = 0
    audible_tracks = 0

    with tempfile.TemporaryDirectory(prefix="hoot-x68k-midi-real-") as td:
        temp = Path(td)
        for entry_ordinal, entry_id in enumerate(entry_ids):
            meta = run_probe(args, entry_id, temp / f"{entry_ordinal}-meta.json")
            scan = meta.get("scan") or {}
            if scan.get("outcome") == "missing-archive":
                failures.append(f"{entry_id}: archive is not present")
                continue
            if scan.get("load_error"):
                failures.append(f"{entry_id}: {scan['load_error']}")
                continue

            entry_result = {key: value for key, value in meta.items() if key != "scan"}
            entry_result["scan"] = {"outcome": "audio-active", "load_error": "", "tracks": []}
            track_count = int(meta.get("track_count", 0))
            entry_counts = {name: 0 for name in COUNTERS}
            entry_clipped = 0
            entry_audible = 0

            for track_index in range(track_count):
                item = run_probe(args, entry_id,
                                 temp / f"{entry_ordinal}-track-{track_index:03d}.json",
                                 track=track_index)
                tracks = ((item.get("scan") or {}).get("tracks") or [])
                if len(tracks) != 1:
                    failures.append(f"{entry_id} track {track_index}: missing track result")
                    continue
                track = tracks[0]
                entry_result["scan"]["tracks"].append(track)
                total_tracks += 1
                result = str(track.get("result", "unknown"))
                if result == "audio-active":
                    entry_audible += 1
                    audible_tracks += 1
                else:
                    failures.append(f"{entry_id} track {track_index}: {result}")

                diagnostics = track.get("diagnostics") or {}
                if int(diagnostics.get("unsupported_opcodes", 0) or 0):
                    failures.append(f"{entry_id} track {track_index}: unsupported CPU opcode")
                if str(diagnostics.get("warning", "")):
                    failures.append(f"{entry_id} track {track_index}: {diagnostics['warning']}")
                startup = diagnostics.get("x68k_startup") or {}
                if int(startup.get("mailbox_pending", 0) or 0):
                    failures.append(f"{entry_id} track {track_index}: mailbox still pending")

                midi = diagnostics.get("midi") or {}
                expected_type = EXPECTED_TYPES.get(entry_id)
                if expected_type is not None and int(midi.get("midiout_type", -1)) != expected_type:
                    failures.append(
                        f"{entry_id} track {track_index}: midiout_type "
                        f"{midi.get('midiout_type')} != {expected_type}")
                if int(midi.get("backend_active", 0) or 0) != 1:
                    failures.append(f"{entry_id} track {track_index}: GM/GS backend inactive")
                if int(midi.get("bytes_transmitted", 0) or 0) == 0:
                    failures.append(f"{entry_id} track {track_index}: no MIDI bytes transmitted")
                if int(midi.get("channel_messages", 0) or 0) == 0:
                    failures.append(f"{entry_id} track {track_index}: no MIDI channel messages")
                if int(midi.get("note_ons", 0) or 0) == 0:
                    failures.append(f"{entry_id} track {track_index}: no note-on observed")
                if int(midi.get("synth_frames", 0) or 0) == 0:
                    failures.append(f"{entry_id} track {track_index}: no synth frames rendered")
                if int(midi.get("malformed_bytes", 0) or 0):
                    failures.append(f"{entry_id} track {track_index}: malformed MIDI bytes")
                for name in COUNTERS:
                    value = int(midi.get(name, 0) or 0)
                    entry_counts[name] += value
                    aggregate[name] += value

                clipped = int((track.get("audio") or {}).get("clipped_samples", 0) or 0)
                entry_clipped += clipped
                total_clipped += clipped
                if clipped:
                    failures.append(f"{entry_id} track {track_index}: {clipped} clipped samples")

            entry_result["validation"] = {
                "tracks_scanned": len(entry_result["scan"]["tracks"]),
                "audible_tracks": entry_audible,
                "clipped_samples": entry_clipped,
                "midi_counters": entry_counts,
            }
            validated.append(entry_result)
            print(
                f"{entry_id}: tracks={track_count} audible={entry_audible} "
                f"bytes={entry_counts['bytes_transmitted']} notes={entry_counts['note_ons']} "
                f"sysex={entry_counts['sysex_messages']} malformed={entry_counts['malformed_bytes']}"
            )

    report = {
        "format": "hoot-x68k-midi-gm-gs-validation-v1",
        "fresh_process_per_track": True,
        "probe_seconds": args.seconds,
        "startup_grace_seconds": args.startup_grace,
        "soundfont_override": args.soundfont or "",
        "entries": validated,
        "summary": {
            "requested_entries": len(entry_ids),
            "validated_entries": len(validated),
            "tracks_scanned": total_tracks,
            "audible_tracks": audible_tracks,
            "clipped_samples": total_clipped,
            "midi_counters": aggregate,
            "failures": failures,
        },
    }
    Path(args.output).write_text(
        json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    if failures:
        print("x68k MIDI GM/GS validation: FAIL", file=sys.stderr)
        for failure in failures:
            print(f"  {failure}", file=sys.stderr)
        return 1
    print("x68k MIDI GM/GS validation: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
