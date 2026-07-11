#!/usr/bin/env python3
"""Export compact 16-bit disassembly windows for PC-98 driver binaries."""

from __future__ import annotations

import argparse
import shutil
import subprocess
import tempfile
import zipfile
from pathlib import Path


def read_input(path: Path, member: str | None) -> tuple[bytes, str]:
    if member:
        with zipfile.ZipFile(path) as archive:
            return archive.read(member), f"{path}:{member}"
    return path.read_bytes(), str(path)


def fallback_hexdump(data: bytes, origin: int) -> str:
    lines = []
    for offset in range(0, len(data), 16):
        chunk = data[offset : offset + 16]
        hex_bytes = " ".join(f"{b:02x}" for b in chunk)
        ascii_bytes = "".join(chr(b) if 32 <= b < 127 else "." for b in chunk)
        lines.append(f"{origin + offset:04x}: {hex_bytes:<47} {ascii_bytes}")
    return "\n".join(lines)


def ndisasm(data: bytes, origin: int) -> str | None:
    tool = shutil.which("ndisasm")
    if not tool:
        return None
    with tempfile.NamedTemporaryFile() as tmp:
        tmp.write(data)
        tmp.flush()
        proc = subprocess.run(
            [tool, "-b16", f"-o0x{origin:x}", tmp.name],
            check=False,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )
    if proc.returncode != 0:
        return None
    return proc.stdout.rstrip()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input", type=Path, help="driver binary or zip pack")
    parser.add_argument("--member", help="zip member name to disassemble")
    parser.add_argument("--base", type=lambda s: int(s, 0), default=0x100)
    parser.add_argument("--around", type=lambda s: int(s, 0), default=0x100)
    parser.add_argument("--bytes", type=lambda s: int(s, 0), default=512)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()

    raw, label = read_input(args.input, args.member)
    start = max(0, args.around - args.base - (args.bytes // 2))
    end = min(len(raw), start + args.bytes)
    window = raw[start:end]
    origin = args.base + start
    body = ndisasm(window, origin) or fallback_hexdump(window, origin)

    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(
        "\n".join(
            [
                f"# Disassembly: {label}",
                "",
                f"- base: `0x{args.base:04x}`",
                f"- around: `0x{args.around:04x}`",
                f"- window: `0x{origin:04x}`-`0x{origin + len(window):04x}`",
                "",
                "```asm",
                body,
                "```",
                "",
            ]
        ),
        encoding="utf-8",
    )
    print(f"wrote {args.out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
