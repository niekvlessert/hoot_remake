#!/usr/bin/env python3
"""Inventory PC-88 and PC-98 compatibility against the current generic hosts."""
from __future__ import annotations
import argparse, collections, json, os
from pathlib import Path

PC88_DEFERRED = {
    "use_ssgpcm": "SSG PCM helper",
    "use_pcmx8": "PCMx8 helper",
    "use_gvram": "GVRAM side effects",
    "use_n88rom": "N88-BASIC ROM services",
    "wstimer": "wstimer scheduling",
    "vstimer": "vstimer scheduling",
}
PC98_DEFERRED = {
    "wstimer": "wstimer scheduling",
    "dummysndrom": "dummy sound-ROM behavior",
}

def optmap(game):
    out = {}
    for item in game.get("options", []):
        out[item.get("name", "")] = item.get("value", "")
    return out

def enabled(value):
    try:
        return int(str(value), 0) != 0
    except Exception:
        return bool(value)

def iter_games(root: Path):
    for f in sorted((root / "catalog-src" / "shards").glob("*.json")):
        d = json.loads(f.read_text(encoding="utf-8"))
        for g in d.get("games", []):
            yield g

def archive_exists(packs: Path | None, archive: str):
    if packs is None: return None
    return (packs / f"{archive}.zip").exists()

def count_tracks(game):
    return sum(1 for x in game.get("title_entries", []) if x.get("kind") == "title")

def classify_pc88(game):
    opts = optmap(game)
    limitations=[]
    for key,label in PC88_DEFERRED.items():
        if key in opts and (key == "baseclock" or enabled(opts[key])):
            limitations.append(label)
    return limitations

def shell_token(command):
    tok=(command or "").strip().split()
    return Path(tok[0]).name.lower() if tok else ""

def classify_pc98(game):
    opts=optmap(game)
    limitations=[]
    shells=[a for a in game.get("assets",[]) if a.get("type")=="shell"]
    if not shells: limitations.append("no shell command")
    for key,label in PC98_DEFERRED.items():
        if key in opts and enabled(opts[key]): limitations.append(label)
    if enabled(opts.get("midiout", 0)):
        try:
            midi_type = int(str(opts.get("midiout_type", -1)), 0)
        except Exception:
            midi_type = -1
        if midi_type == 3:
            limitations.append("CM-64 CM-32P section is not emulated; Munt covers the CM-32L-compatible LA section")
        elif midi_type not in (1, 2, 4, 7, 8):
            limitations.append("MIDI transport only; dedicated synth backend required")
    if game.get("driver", {}).get("type") == "86" and "extramsize" in opts and enabled(opts["extramsize"]):
        limitations.append("EMS/extra-memory sample storage")
    return limitations

def summarize(root: Path, packs: Path | None):
    pc88=[]; pc98_opn=[]; pc98_opna=[]; pc98_86=[]; pc98_beep=[]
    for g in iter_games(root):
        drv=g.get("driver",{})
        if drv.get("name")=="pc88" and drv.get("type") in ("opn","opna"):
            pc88.append(g)
        if drv.get("name")=="pc98dos" and drv.get("type")=="opn":
            pc98_opn.append(g)
        if drv.get("name")=="pc98dos" and drv.get("type")=="opna":
            pc98_opna.append(g)
        if drv.get("name")=="pc98dos" and drv.get("type")=="86":
            pc98_86.append(g)
        if drv.get("name")=="pc98dos" and drv.get("type")=="beep":
            pc98_beep.append(g)

    def summarize_group(games, kind):
        lim_counter=collections.Counter(); shell_counter=collections.Counter(); type_counter=collections.Counter()
        core=[]; deferred=[]; present=0; missing=0; unknown=0
        archives=set(); track_total=0
        for g in games:
            archives.add(g.get("archive", "")); track_total += count_tracks(g)
            limitations=classify_pc88(g) if kind=="pc88" else classify_pc98(g)
            (core if not limitations else deferred).append(g.get("id",""))
            for x in limitations: lim_counter[x]+=1
            if kind=="pc88": type_counter[g.get("driver",{}).get("type","")]+=1
            else:
                for a in g.get("assets",[]):
                    if a.get("type")=="shell": shell_counter[shell_token(a.get("path",""))]+=1
            ex=archive_exists(packs,g.get("archive",""))
            if ex is True: present+=1
            elif ex is False: missing+=1
            else: unknown+=1
        return {
            "configurations":len(games), "unique_archives":len(archives), "catalog_tracks":track_total,
            "generic_core_candidates":len(core), "deferred_feature_configurations":len(deferred),
            "limitations":dict(lim_counter), "driver_types":dict(type_counter),
            "top_shells":shell_counter.most_common(40),
            "pack_presence":{"present":present,"missing":missing,"not_checked":unknown},
            "core_candidate_ids":core,
            "deferred_ids":deferred,
        }
    return {
        "pc88": summarize_group(pc88, "pc88"),
        "pc98dos_opn": summarize_group(pc98_opn, "pc98"),
        "pc98dos_opna": summarize_group(pc98_opna, "pc98"),
        "pc98dos_86": summarize_group(pc98_86, "pc98_86"),
        "pc98dos_beep": summarize_group(pc98_beep, "pc98_beep"),
        "pc98dos_opn_opna": summarize_group(pc98_opn + pc98_opna, "pc98"),
        "pc98dos_all_opn_family": summarize_group(pc98_opn + pc98_opna + pc98_86, "pc98"),
        "pc98dos_all_audio": summarize_group(pc98_opn + pc98_opna + pc98_86 + pc98_beep, "pc98"),
    }

def main():
    ap=argparse.ArgumentParser()
    ap.add_argument("--root", default=".")
    ap.add_argument("--packs")
    ap.add_argument("--output", default="pcx_catalog_matrix.json")
    a=ap.parse_args()
    result=summarize(Path(a.root), Path(a.packs) if a.packs else None)
    Path(a.output).write_text(json.dumps(result,indent=2,ensure_ascii=False)+"\n",encoding="utf-8")
    for name in ("pc88", "pc98dos_opn", "pc98dos_opna", "pc98dos_86", "pc98dos_beep", "pc98dos_opn_opna", "pc98dos_all_opn_family", "pc98dos_all_audio"):
        r=result[name]
        print(f"{name}: configs={r['configurations']} archives={r['unique_archives']} tracks={r['catalog_tracks']} core={r['generic_core_candidates']} deferred={r['deferred_feature_configurations']}")
        print("  limitations:", r["limitations"])
        print("  packs:", r["pack_presence"])
if __name__=="__main__": main()
