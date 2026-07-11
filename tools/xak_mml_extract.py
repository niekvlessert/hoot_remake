#!/usr/bin/env python3
"""Extract and detokenize Xak X68000 OPMDRV BGM files.

The original game stores many .BGM files as compact OPMDRV token streams.
This tool reads a Human68k HDM disk image, follows the FAT12 cluster chain,
and expands the tokens back into valid textual MML.
"""

from __future__ import annotations

import argparse
from pathlib import Path


SECTOR_SIZE = 1024
FAT_OFFSET = 0x0C00
ROOT_OFFSET = 0x1400
ROOT_ENTRIES = 192
DATA_OFFSET = 0x2C00


def fat12_next(image: bytes, cluster: int) -> int:
    offset = FAT_OFFSET + cluster + cluster // 2
    if offset + 1 >= len(image):
        raise ValueError(f"FAT entry for cluster {cluster} is outside the image")
    pair = image[offset] | (image[offset + 1] << 8)
    return (pair >> 4) & 0xFFF if cluster & 1 else pair & 0xFFF


def dos_name(name: str) -> bytes:
    path = Path(name)
    stem = path.stem.upper().encode("ascii")
    suffix = path.suffix[1:].upper().encode("ascii")
    if len(stem) > 8 or len(suffix) > 3:
        raise ValueError("Human68k root names must fit the 8.3 format")
    return stem.ljust(8) + suffix.ljust(3)


def extract_root_file(image: bytes, name: str) -> bytes:
    wanted = dos_name(name)
    for index in range(ROOT_ENTRIES):
        offset = ROOT_OFFSET + index * 32
        entry = image[offset:offset + 32]
        if len(entry) != 32 or entry[0] == 0:
            continue
        if entry[0] == 0xE5 or entry[11] & 0x18:
            continue
        if entry[:11] != wanted:
            continue

        cluster = int.from_bytes(entry[26:28], "little")
        size = int.from_bytes(entry[28:32], "little")
        output = bytearray()
        visited: set[int] = set()
        while 2 <= cluster < 0xFF8 and len(output) < size:
            if cluster in visited:
                raise ValueError(f"loop in FAT chain at cluster {cluster}")
            visited.add(cluster)
            data_offset = DATA_OFFSET + (cluster - 2) * SECTOR_SIZE
            output.extend(image[data_offset:data_offset + SECTOR_SIZE])
            cluster = fat12_next(image, cluster)
        if len(output) < size:
            raise ValueError(f"truncated FAT chain for {name}")
        return bytes(output[:size])
    raise FileNotFoundError(f"{name} was not found in the HDM root directory")


def detokenize(data: bytes) -> str:
    output: list[str] = []
    cursor = 0
    note_names = "ABCDEFG"

    def argument(command: str) -> None:
        nonlocal cursor
        if cursor >= len(data):
            raise ValueError(f"truncated argument for {command}")
        output.append(f"{command}{data[cursor]}")
        cursor += 1

    while cursor < len(data):
        token = data[cursor]
        cursor += 1
        if token == 0x1A:
            output.append("\x1a")
            break
        if token == 0xA0:
            argument("@")
        elif 0xA1 <= token <= 0xA7:
            argument(note_names[token - 0xA1])
        elif 0x81 <= token <= 0x87:
            output.append(note_names[token - 0x81] + "#")
        elif token == 0xAC:
            argument("L")
        elif token == 0xAF:
            argument("O")
        elif token == 0xB0:
            argument("P")
        elif token == 0xB1:
            argument("Q")
        elif token == 0xB2:
            argument("R")
        elif token == 0xB4:
            argument("T")
        elif token == 0xB6:
            argument("V")
            if len(output) >= 2 and output[-2].endswith("("):
                output.append(",")
        elif token == 0xB9:
            if cursor + 1 >= len(data):
                raise ValueError("truncated Y register command")
            output.append(f"Y{data[cursor]},{data[cursor + 1]}")
            cursor += 2
        elif token == 0xC0:
            if cursor >= len(data):
                raise ValueError("truncated numeric literal")
            output.append(str(data[cursor]))
            cursor += 1
            if cursor < len(data) and data[cursor] != ord(")"):
                output.append(",")
        elif 0xC1 <= token <= 0xC8:
            output.append(f"(T{token - 0xC0})")
        elif token < 0x80:
            output.append(chr(token))
        else:
            raise ValueError(f"unsupported OPMDRV token 0x{token:02x} at 0x{cursor - 1:x}")

    return "".join(output)


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Extract an original Xak X68000 BGM and convert it to textual MML")
    parser.add_argument("disk", type=Path, help="original Xak .hdm disk image")
    parser.add_argument("name", help="8.3 BGM filename, for example TOWN1.BGM")
    parser.add_argument("--output", "-o", type=Path, required=True, help="output MML path")
    parser.add_argument(
        "--xak-x", type=Path,
        help="optional extracted XAK.X used to record/validate source provenance")
    args = parser.parse_args()

    image = args.disk.read_bytes()
    if len(image) != 77 * 2 * 8 * SECTOR_SIZE:
        raise SystemExit(f"unsupported HDM size: {len(image)} bytes")
    if args.xak_x is not None:
        executable = args.xak_x.read_bytes()
        if len(executable) < 0x40 or executable[:2] != b"HU":
            raise SystemExit(f"not a Human68k executable: {args.xak_x}")
        if b"syoki2.bgm" not in executable.lower():
            raise SystemExit(f"XAK.X signature not found in: {args.xak_x}")

    tokenized = extract_root_file(image, args.name)
    mml = detokenize(tokenized)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(mml.encode("ascii"))
    print(f"extracted {args.name}: {len(tokenized)} tokenized bytes -> {len(mml)} MML bytes")
    print(f"wrote {args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
