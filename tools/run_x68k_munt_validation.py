#!/usr/bin/env python3
"""Validate X68000 MT-32 paths through CZ-6BM1 transport and Munt/mt32emu.

The script does not distribute libmt32emu or Roland ROMs. It runs every track
in a fresh hootprobe process so initialization state cannot leak between
tracks. By default it covers two independent real packs and both Hoot MT-32
catalogue classes (native MT-32 and MT-32-emulation driver variants).
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

DEFAULT_ENTRIES = (
    "ad68snd-generic-2", "ad68snd-generic-3",
    "paro68snd-generic-2", "paro68snd-generic-3",
)
EXPECTED_TYPES = {
    "ad68snd-generic-2": 2,
    "ad68snd-generic-3": 1,
    "paro68snd-generic-2": 2,
    "paro68snd-generic-3": 1,
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
    p.add_argument("--output", default="x68k-munt-validation.json")
    p.add_argument("--seconds", type=int, default=8)
    p.add_argument("--startup-grace", type=int, default=1)
    p.add_argument("--timeout", type=int, default=120)
    p.add_argument("--entry", action="append", dest="entries")
    p.add_argument("--library", help="explicit libmt32emu path")
    p.add_argument("--mt32-rom-path", help="directory containing user MT-32 ROMs")
    p.add_argument("--cm32l-rom-path", help="directory containing user CM-32L ROMs")
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
    env["HOOT_X68K_MIDI_BACKEND"] = "munt"
    if args.library:
        env["HOOT_MT32EMU_LIBRARY"] = str(Path(args.library))
    if args.mt32_rom_path:
        env["HOOT_MT32_ROM_PATH"] = str(Path(args.mt32_rom_path))
    if args.cm32l_rom_path:
        env["HOOT_CM32L_ROM_PATH"] = str(Path(args.cm32l_rom_path))
    completed = subprocess.run(cmd, env=env, stdout=subprocess.PIPE,
                               stderr=subprocess.PIPE, text=True, check=False)
    if not output.is_file():
        detail = completed.stderr.strip()[-1200:]
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
    for optional in (args.library, args.mt32_rom_path, args.cm32l_rom_path):
        if optional and not Path(optional).exists():
            print(f"path not found: {optional}", file=sys.stderr)
            return 2

    entry_ids = tuple(args.entries or DEFAULT_ENTRIES)
    validated: list[dict[str, Any]] = []
    failures: list[str] = []
    aggregate = {name: 0 for name in COUNTERS}
    total_tracks = audible_tracks = note_active_tracks = 0

    with tempfile.TemporaryDirectory(prefix="hoot-x68k-munt-real-") as td:
        temp = Path(td)
        for ordinal, entry_id in enumerate(entry_ids):
            meta = run_probe(args, entry_id, temp / f"{ordinal}-meta.json")
            scan = meta.get("scan") or {}
            if scan.get("outcome") == "missing-archive":
                failures.append(f"{entry_id}: archive is not present")
                continue
            if scan.get("load_error"):
                failures.append(f"{entry_id}: {scan['load_error']}")
                continue

            result = {key: value for key, value in meta.items() if key != "scan"}
            result["scan"] = {"outcome": "audio-active", "load_error": "", "tracks": []}
            track_count = int(meta.get("track_count", 0))
            counts = {name: 0 for name in COUNTERS}
            entry_audible = entry_note_active = 0

            for track_index in range(track_count):
                item = run_probe(args, entry_id, temp / f"{ordinal}-track-{track_index:03d}.json", track_index)
                tracks = ((item.get("scan") or {}).get("tracks") or [])
                if len(tracks) != 1:
                    failures.append(f"{entry_id} track {track_index}: missing track result")
                    continue
                track = tracks[0]
                result["scan"]["tracks"].append(track)
                total_tracks += 1
                if track.get("result") == "audio-active":
                    audible_tracks += 1
                    entry_audible += 1
                else:
                    failures.append(f"{entry_id} track {track_index}: {track.get('result', 'unknown')}")

                diag = track.get("diagnostics") or {}
                if int(diag.get("unsupported_opcodes", 0) or 0):
                    failures.append(f"{entry_id} track {track_index}: unsupported CPU opcode")
                if str(diag.get("warning", "")):
                    failures.append(f"{entry_id} track {track_index}: {diag['warning']}")
                midi = diag.get("midi") or {}
                expected = EXPECTED_TYPES.get(entry_id)
                if expected is not None and int(midi.get("midiout_type", -1)) != expected:
                    failures.append(f"{entry_id} track {track_index}: midiout_type {midi.get('midiout_type')} != {expected}")
                if int(midi.get("backend_active", 0) or 0) != 1:
                    failures.append(f"{entry_id} track {track_index}: Munt backend inactive")
                if str(midi.get("backend", "")) != "munt-mt32":
                    failures.append(f"{entry_id} track {track_index}: backend is {midi.get('backend')!r}")
                if int(midi.get("bytes_transmitted", 0) or 0) == 0:
                    failures.append(f"{entry_id} track {track_index}: no MIDI bytes transmitted")
                if int(midi.get("synth_frames", 0) or 0) == 0:
                    failures.append(f"{entry_id} track {track_index}: no Munt synth frames rendered")
                if int(midi.get("malformed_bytes", 0) or 0):
                    failures.append(f"{entry_id} track {track_index}: malformed MIDI bytes")
                if int(midi.get("note_ons", 0) or 0) != 0:
                    note_active_tracks += 1
                    entry_note_active += 1
                for name in COUNTERS:
                    value = int(midi.get(name, 0) or 0)
                    counts[name] += value
                    aggregate[name] += value

            # Some hybrid OPM+MT-32 packs legitimately have a few OPM-only
            # cues. Require the MIDI synth to be exercised by the entry, not by
            # every individual catalog command.
            if track_count and entry_note_active == 0:
                failures.append(f"{entry_id}: no track produced an MT-32 note-on")
            result["validation"] = {
                "tracks_scanned": len(result["scan"]["tracks"]),
                "audible_tracks": entry_audible,
                "note_active_tracks": entry_note_active,
                "midi_counters": counts,
            }
            validated.append(result)
            print(f"{entry_id}: tracks={track_count} audible={entry_audible} "
                  f"note-active={entry_note_active} bytes={counts['bytes_transmitted']} "
                  f"sysex={counts['sysex_messages']} malformed={counts['malformed_bytes']}")

    report = {
        "format": "hoot-x68k-munt-validation-v1",
        "fresh_process_per_track": True,
        "probe_seconds": args.seconds,
        "startup_grace_seconds": args.startup_grace,
        "library_override": args.library or "",
        "mt32_rom_path_override": args.mt32_rom_path or "",
        "cm32l_rom_path_override": args.cm32l_rom_path or "",
        "entries": validated,
        "summary": {
            "requested_entries": len(entry_ids),
            "validated_entries": len(validated),
            "tracks_scanned": total_tracks,
            "audible_tracks": audible_tracks,
            "note_active_tracks": note_active_tracks,
            "midi_counters": aggregate,
            "failures": failures,
        },
    }
    Path(args.output).write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    if failures:
        print("x68k Munt validation: FAIL", file=sys.stderr)
        for failure in failures:
            print(f"  {failure}", file=sys.stderr)
        return 1
    print("x68k Munt validation: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
