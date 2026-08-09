#!/usr/bin/env python3
"""Inventory PC-98 DOS MIDI configurations against the MPU-401/synth backends."""
from __future__ import annotations
import argparse, collections, json
from pathlib import Path

MIDI_CLASS = {
    1: ("mt32-emulation", "munt-mt32"),
    2: ("mt32", "munt-mt32"),
    3: ("cm64", "munt-cm32l-la-partial"),
    4: ("gs-sc55", "nuked-sc55-or-fluidsynth"),
    7: ("gs-sc88", "fluidsynth"),
    8: ("gm", "fluidsynth"),
}

def iter_games(root: Path):
    for file in sorted((root / "catalog-src" / "shards").glob("*.json")):
        data = json.loads(file.read_text(encoding="utf-8"))
        for game in data.get("games", []):
            drv = game.get("driver", {})
            if drv.get("name") != "pc98dos":
                continue
            opts = {x.get("name", ""): x.get("value", "") for x in game.get("options", [])}
            try:
                enabled = int(str(opts.get("midiout", 0)), 0) != 0
            except Exception:
                enabled = bool(opts.get("midiout"))
            if not enabled:
                continue
            try:
                midi_type = int(str(opts.get("midiout_type", -1)), 0)
            except Exception:
                midi_type = -1
            yield game, midi_type

def track_count(game):
    return sum(1 for x in game.get("title_entries", []) if x.get("kind") == "title")

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--root", default=".")
    ap.add_argument("--packs")
    ap.add_argument("--output", default="pc98_midi_catalog_matrix.json")
    a = ap.parse_args()
    root = Path(a.root)
    packs = Path(a.packs) if a.packs else None
    rows = list(iter_games(root))
    groups = collections.defaultdict(lambda: {"configurations": 0, "tracks": 0, "archives": set(), "present": 0})
    by_driver = collections.Counter()
    driver_tracks = collections.Counter()
    render_configs = render_tracks = partial_configs = partial_tracks = 0
    transport_configs = transport_tracks = 0
    unknown_configs = unknown_tracks = 0
    for game, midi_type in rows:
        ntracks = track_count(game)
        cls, backend = MIDI_CLASS.get(midi_type, (f"unknown-{midi_type}", "unknown"))
        g = groups[(midi_type, cls, backend)]
        g["configurations"] += 1
        g["tracks"] += ntracks
        archive = game.get("archive", "")
        if archive:
            g["archives"].add(archive)
            if packs and (packs / f"{archive}.zip").exists():
                g["present"] += 1
        driver = game.get("driver", {}).get("type", "")
        by_driver[driver] += 1
        driver_tracks[driver] += ntracks
        if backend in ("munt-mt32", "nuked-sc55-or-fluidsynth", "fluidsynth"):
            render_configs += 1; render_tracks += ntracks
        elif backend == "munt-cm32l-la-partial":
            partial_configs += 1; partial_tracks += ntracks
        elif backend == "transport-only":
            transport_configs += 1; transport_tracks += ntracks
        else:
            unknown_configs += 1; unknown_tracks += ntracks
    detail=[]
    for (midi_type, cls, backend), g in sorted(groups.items()):
        detail.append({
            "midiout_type": midi_type,
            "class": cls,
            "backend": backend,
            "configurations": g["configurations"],
            "tracks": g["tracks"],
            "unique_archives": len(g["archives"]),
            "locally_present_configurations": g["present"],
        })
    out = {
        "pc98dos_midi": {
            "configurations": len(rows),
            "tracks": sum(track_count(g) for g,_ in rows),
            "software_renderable_configurations": render_configs,
            "software_renderable_tracks": render_tracks,
            "partially_renderable_configurations": partial_configs,
            "partially_renderable_tracks": partial_tracks,
            "transport_only_configurations": transport_configs,
            "transport_only_tracks": transport_tracks,
            "unknown_configurations": unknown_configs,
            "unknown_tracks": unknown_tracks,
            "by_driver_type": {k: {"configurations": by_driver[k], "tracks": driver_tracks[k]} for k in sorted(by_driver)},
            "by_midiout_type": detail,
        }
    }
    Path(a.output).write_text(json.dumps(out, indent=2, ensure_ascii=False)+"\n", encoding="utf-8")
    s=out["pc98dos_midi"]
    print(f"PC-98 DOS MIDI: configs={s['configurations']} tracks={s['tracks']} renderable={s['software_renderable_configurations']}/{s['software_renderable_tracks']} partial={s['partially_renderable_configurations']}/{s['partially_renderable_tracks']} transport-only={s['transport_only_configurations']}/{s['transport_only_tracks']}")
    for row in detail:
        print(f"  type {row['midiout_type']}: {row['class']} configs={row['configurations']} tracks={row['tracks']} backend={row['backend']}")

if __name__ == "__main__":
    main()
