#!/usr/bin/env python3
"""Compact PC-98 NDJSON traces around useful failure points."""

from __future__ import annotations

import argparse
import json
from pathlib import Path


def load_events(path: Path) -> list[dict]:
    events = []
    for line_no, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        line = line.strip()
        if not line:
            continue
        try:
            event = json.loads(line)
        except json.JSONDecodeError as exc:
            events.append({"type": "parse_error", "line": line_no, "error": str(exc)})
            continue
        event["_line"] = line_no
        events.append(event)
    return events


def matches_port(event: dict, ports: set[int] | None) -> bool:
    return ports is None or event.get("port") in ports


def format_event(event: dict) -> str:
    event_type = event.get("type", "?")
    frame = event.get("frame", "-")
    step = event.get("step", "-")
    loc = ""
    if "cs" in event and "ip" in event:
        loc = f" {event['cs']:04x}:{event['ip']:04x}"
    elif "from_cs" in event and "from_ip" in event:
        loc = f" {event['from_cs']:04x}:{event['from_ip']:04x}"
    if event_type in {"in", "out"}:
        reg = ""
        if "opna_reg" in event:
            reg = f" opna[{event.get('opna_bank', 0)}].{event['opna_reg']:02x}"
        return (
            f"line={event.get('_line')} frame={frame} step={step}{loc} "
            f"{event_type} port=0x{event.get('port', 0):04x} value=0x{event.get('value', 0):02x}{reg}"
        )
    if event_type in {"call", "callf", "jmp", "jmpf", "ret", "retf", "iret", "unsupported"}:
        return (
            f"line={event.get('_line')} frame={frame} step={step} {event_type} "
            f"op=0x{event.get('opcode', 0):02x} "
            f"{event.get('from_cs', 0):04x}:{event.get('from_ip', 0):04x}"
            f" -> {event.get('to_cs', 0):04x}:{event.get('to_ip', 0):04x}"
        )
    if event_type == "int":
        return (
            f"line={event.get('_line')} frame={frame} step={step}{loc} "
            f"int=0x{event.get('int', 0):02x} ax=0x{event.get('ax', 0):04x}"
        )
    return "line={} {}".format(event.get("_line"), json.dumps(event, sort_keys=True))


def parse_ports(value: str | None) -> set[int] | None:
    if not value:
        return None
    return {int(part.strip(), 0) for part in value.split(",") if part.strip()}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("trace", type=Path)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--type", dest="event_type", help="event type to keep")
    parser.add_argument("--port", help="comma-separated port filter, e.g. 0x188,0x18a")
    parser.add_argument("--frame", type=int, help="center output around this frame")
    parser.add_argument("--line", type=int, help="center output around this NDJSON line")
    parser.add_argument("--window", type=int, default=25, help="events before/after center")
    parser.add_argument(
        "--first-unsupported",
        action="store_true",
        help="center output around the first unsupported CPU event",
    )
    args = parser.parse_args()

    events = load_events(args.trace)
    ports = parse_ports(args.port)
    filtered = [
        event
        for event in events
        if (args.event_type is None or event.get("type") == args.event_type)
        and matches_port(event, ports)
    ]

    center_index = None
    if args.first_unsupported:
        for index, event in enumerate(filtered):
            if event.get("type") == "unsupported":
                center_index = index
                break
    elif args.line is not None:
        center_index = min(
            range(len(filtered)),
            key=lambda i: abs(int(filtered[i].get("_line", 0)) - args.line),
            default=None,
        )
    elif args.frame is not None:
        center_index = min(
            range(len(filtered)),
            key=lambda i: abs(int(filtered[i].get("frame", 0)) - args.frame),
            default=None,
        )

    if center_index is None:
        start = 0
        end = min(len(filtered), args.window * 2 + 1)
    else:
        start = max(0, center_index - args.window)
        end = min(len(filtered), center_index + args.window + 1)

    lines = [
        f"# Compact trace: {args.trace}",
        "",
        f"- source events: `{len(events)}`",
        f"- filtered events: `{len(filtered)}`",
        f"- output slice: `{start}`-`{end}`",
        "",
        "```text",
    ]
    lines.extend(format_event(event) for event in filtered[start:end])
    lines.extend(["```", ""])

    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text("\n".join(lines), encoding="utf-8")
    print(f"wrote {args.out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
