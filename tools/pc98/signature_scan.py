#!/usr/bin/env python3
"""Cluster PC-98 drivers from a driver_indexer.py report."""

from __future__ import annotations

import argparse
import json
from collections import defaultdict
from pathlib import Path


def family_key(member: dict) -> str:
    name = Path(member["name"]).name.lower()
    stem = Path(name).stem
    suffix = Path(name).suffix
    size_bucket = int(member["size"]) // 512
    return f"{stem}:{suffix}:{size_bucket}"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("index", type=Path, help="driver_indexer JSON report")
    parser.add_argument("--out", type=Path, required=True, help="family JSON output path")
    args = parser.parse_args()

    report = json.loads(args.index.read_text(encoding="utf-8"))
    exact = defaultdict(list)
    fuzzy = defaultdict(list)

    for pack in report.get("packs", []):
        for driver in pack.get("drivers", []):
            item = {
                "archive": pack["archive"],
                "pack_path": pack["path"],
                "name": driver["name"],
                "size": driver["size"],
                "sha256": driver["sha256"],
            }
            exact[driver["sha256"]].append(item)
            fuzzy[family_key(driver)].append(item)

    exact_families = [
        {"kind": "exact", "signature": key, "count": len(items), "members": items}
        for key, items in exact.items()
    ]
    fuzzy_families = [
        {"kind": "name_size", "signature": key, "count": len(items), "members": items}
        for key, items in fuzzy.items()
        if len(items) > 1
    ]
    exact_families.sort(key=lambda f: (-f["count"], f["members"][0]["name"].lower()))
    fuzzy_families.sort(key=lambda f: (-f["count"], f["signature"]))

    result = {
        "source": str(args.index),
        "exact_family_count": len(exact_families),
        "similar_family_count": len(fuzzy_families),
        "exact_families": exact_families,
        "similar_families": fuzzy_families,
    }
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(result, indent=2, sort_keys=True), encoding="utf-8")
    print(
        f"wrote {len(exact_families)} exact families and "
        f"{len(fuzzy_families)} similar families"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
