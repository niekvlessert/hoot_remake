#!/usr/bin/env python3
"""Summarize a Hoot master catalogue and DriverRegistry capability probe."""
from __future__ import annotations
import argparse, collections, json, sqlite3
from pathlib import Path


def rows_sorted(counter):
    return [{"name": k, "count": v} for k, v in sorted(counter.items(), key=lambda kv: (-kv[1], kv[0].casefold()))]


def build(sqlite_path: Path, probe_path: Path) -> dict:
    probe = json.loads(probe_path.read_text(encoding="utf-8"))
    conn = sqlite3.connect(f"file:{sqlite_path}?mode=ro", uri=True)
    try:
        game_rows = {r[0]: {"platform": r[1] or "<unspecified>", "driver": r[2] or "<empty>", "driver_type": r[3] or "<empty>", "archive": r[4] or ""}
                     for r in conn.execute("SELECT id,platform,driver_name,driver_type,archive FROM games")}
        known_pack = {r[0]: bool(r[1]) for r in conn.execute("SELECT game_id,catalogued_pack FROM pack_index")}
        source_rows = [dict(zip(("name","priority","kind","date","url","path","status","games","note"), row))
                       for row in conn.execute("SELECT source_name,priority,kind,source_date,source_url,source_path,status,game_count,note FROM catalog_sources ORDER BY priority DESC,source_name")]
    finally:
        conn.close()

    cap = collections.Counter()
    cap_platform = collections.defaultdict(collections.Counter)
    unsupported_driver = collections.Counter()
    unsupported_platform = collections.Counter()
    supported_driver = collections.Counter()
    pack_cap = collections.Counter()
    for entry in probe.get("entries", []):
        gid = entry.get("id", "")
        row = game_rows.get(gid, {})
        platform = row.get("platform", "<unspecified>")
        driver = row.get("driver", entry.get("catalog_driver", "<empty>"))
        status = entry.get("capability", {}).get("status", "unknown")
        cap[status] += 1
        cap_platform[platform][status] += 1
        if status == "unsupported":
            unsupported_driver[driver] += 1
            unsupported_platform[platform] += 1
        else:
            supported_driver[driver] += 1
        pack_cap[("known_pack" if known_pack.get(gid, False) else "no_recovery_pack", status)] += 1

    platforms=[]
    for platform, counts in sorted(cap_platform.items(), key=lambda kv: (-sum(kv[1].values()), kv[0].casefold())):
        platforms.append({"platform": platform, "total": sum(counts.values()), **dict(counts)})

    return {
        "format": "hoot-master-census",
        "version": 1,
        "catalog_entries": len(game_rows),
        "capabilities": dict(cap),
        "sources": source_rows,
        "unsupported_by_driver": rows_sorted(unsupported_driver),
        "unsupported_by_platform": rows_sorted(unsupported_platform),
        "supported_by_driver": rows_sorted(supported_driver),
        "platform_capabilities": platforms,
        "pack_capabilities": [
            {"pack_state": k[0], "capability": k[1], "count": v}
            for k, v in sorted(pack_cap.items(), key=lambda kv: (kv[0][0], kv[0][1]))
        ],
    }


def write_md(data: dict, path: Path):
    lines=["# Hoot master compatibility census", "",
           f"Catalogue entries: **{data['catalog_entries']:,}**", "",
           "## DriverRegistry capability", ""]
    for k,v in sorted(data["capabilities"].items(), key=lambda kv:(-kv[1],kv[0])):
        lines.append(f"- {k}: **{v:,}**")
    lines += ["", "## Unsupported entries by catalogue driver", "", "| Driver | Entries |", "|---|---:|"]
    for row in data["unsupported_by_driver"][:40]: lines.append(f"| `{row['name']}` | {row['count']:,} |")
    lines += ["", "## Unsupported entries by platform", "", "| Platform | Entries |", "|---|---:|"]
    for row in data["unsupported_by_platform"]: lines.append(f"| {row['name']} | {row['count']:,} |")
    lines += ["", "## Capability by platform", "", "| Platform | Total | Unsupported | Experimental | Playable | Verified | Recognized |", "|---|---:|---:|---:|---:|---:|---:|"]
    for row in data["platform_capabilities"]:
        lines.append(f"| {row['platform']} | {row['total']:,} | {row.get('unsupported',0):,} | {row.get('experimental',0):,} | {row.get('playable',0):,} | {row.get('verified',0):,} | {row.get('recognized',0):,} |")
    lines += ["", "`Unsupported` means the current `DriverRegistry` has no replay host for that catalogue entry. It does not mean the pack is absent. Pack presence is a separate axis.", ""]
    path.parent.mkdir(parents=True, exist_ok=True); path.write_text("\n".join(lines), encoding="utf-8")


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--sqlite',required=True); ap.add_argument('--probe',required=True); ap.add_argument('--json',required=True); ap.add_argument('--markdown',required=True)
    a=ap.parse_args(); data=build(Path(a.sqlite),Path(a.probe))
    Path(a.json).write_text(json.dumps(data,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    write_md(data,Path(a.markdown))
    print(json.dumps({"catalog_entries":data['catalog_entries'],"capabilities":data['capabilities'],"unsupported_drivers":len(data['unsupported_by_driver'])},ensure_ascii=False))
if __name__=='__main__': main()
