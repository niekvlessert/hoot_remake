#!/usr/bin/env python3
"""Index PC-98 Hoot packs and probable driver binaries."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import zipfile
from pathlib import Path


DRIVER_EXTENSIONS = {".com", ".exe", ".sys", ".drv", ".bin", ".x"}
DATA_EXTENSIONS = {
    ".bgm",
    ".dat",
    ".mdt",
    ".mdz",
    ".miv",
    ".mus",
    ".m",
    ".m2",
    ".mml",
    ".pcm",
    ".pps",
    ".pvi",
    ".pzi",
    ".voi",
    ".vdt",
}
DRIVER_NAME_RE = re.compile(
    r"(pmd|fmp|mmd|hhd|play|driver|drv|snd|music|sound|trp|pcmset)",
    re.IGNORECASE,
)


def sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def classify_member(name: str, size: int) -> str:
    path = Path(name)
    suffix = path.suffix.lower()
    lower = path.name.lower()
    if suffix in DRIVER_EXTENSIONS:
        return "driver"
    if DRIVER_NAME_RE.search(lower) and size <= 256 * 1024:
        return "driver"
    if suffix in DATA_EXTENSIONS or not suffix:
        return "data"
    return "other"


def index_zip(zip_path: Path) -> dict:
    members = []
    with zipfile.ZipFile(zip_path) as archive:
        for info in archive.infolist():
            if info.is_dir():
                continue
            data = archive.read(info.filename)
            kind = classify_member(info.filename, len(data))
            members.append(
                {
                    "name": info.filename,
                    "size": len(data),
                    "kind": kind,
                    "sha256": sha256_bytes(data),
                }
            )

    drivers = [m for m in members if m["kind"] == "driver"]
    data_files = [m for m in members if m["kind"] == "data"]
    return {
        "archive": zip_path.stem,
        "path": str(zip_path),
        "zip_sha256": sha256_bytes(zip_path.read_bytes()),
        "members": members,
        "drivers": drivers,
        "data_files": data_files,
    }


def iter_zip_paths(root: Path) -> list[Path]:
    if root.is_file():
        return [root]
    return sorted(root.rglob("*.zip"))


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("path", type=Path, help="PC-98 pack zip or directory")
    parser.add_argument("--out", type=Path, required=True, help="JSON output path")
    parser.add_argument(
        "--dedupe",
        action="store_true",
        help="drop duplicate zip payloads with identical SHA-256",
    )
    args = parser.parse_args()

    packs = []
    seen_zip_hashes = set()
    for zip_path in iter_zip_paths(args.path):
        try:
            indexed = index_zip(zip_path)
        except zipfile.BadZipFile:
            continue
        if args.dedupe and indexed["zip_sha256"] in seen_zip_hashes:
            continue
        seen_zip_hashes.add(indexed["zip_sha256"])
        packs.append(indexed)

    result = {
        "root": str(args.path),
        "pack_count": len(packs),
        "driver_count": sum(len(pack["drivers"]) for pack in packs),
        "packs": packs,
    }
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(result, indent=2, sort_keys=True), encoding="utf-8")
    print(f"indexed {result['pack_count']} packs, {result['driver_count']} probable drivers")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
