# Hoot PC-98 Porting Workflow Setup

## Goal

Set up a small Mac-based reverse-engineering workflow for Hoot PC-98 drivers, so you can generate compact traces, disassembly windows, divergence reports, and AI context packs instead of sending huge source files or binary dumps to AI.

## 1. Install Mac build tools

```bash
xcode-select --install
```

Install Homebrew if needed:

```bash
/bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"
brew update
```

## 2. Install required packages

Minimal set:

```bash
brew install git cmake ninja python@3.12 jq ripgrep fd ghidra radare2 sdl2 libsndfile ffmpeg sox
```

Optional but useful:

```bash
brew install rizin cutter dosbox-x mame ccache
```

## 3. Create Python environment

```bash
python3.12 -m venv ~/venvs/hoot-re
source ~/venvs/hoot-re/bin/activate
pip install --upgrade pip
pip install capstone construct rich typer pydantic
```

## 4. Add the PC-98 tools to your Hoot repo

Unzip `hoot_pc98_tools.zip` into your Hoot port repo so you have:

```text
tools/pc98/
  common.py
  driver_indexer.py
  disasm_export.py
  trace_compact.py
  trace_compare.py
  signature_scan.py
  make_context_pack.py
```

Create analysis folders:

```bash
mkdir -p analysis/drivers analysis/disasm analysis/traces analysis/reports analysis/context-packs analysis/signatures
```

## 5. First: index all PC-98 drivers/libs

```bash
source ~/venvs/hoot-re/bin/activate

python tools/pc98/driver_indexer.py /path/to/hoot-packs/pc98 \
  --out analysis/reports/driver_index.json
```

Then cluster similar driver families:

```bash
python tools/pc98/signature_scan.py analysis/reports/driver_index.json \
  --out analysis/signatures/families.json
```

Use this to identify which drivers are probably PMD/FMP/custom/etc. and avoid solving the same family repeatedly.

## 6. Export small disassembly windows

For a suspicious offset or failing `CS:IP`:

```bash
python tools/pc98/disasm_export.py /path/to/driver.drv \
  --base 0x100 \
  --around 0x200 \
  --bytes 512 \
  --out analysis/disasm/driver_0200.md
```

Use Ghidra for deeper manual analysis only after the automated reports point to the right function.

## 7. Add NDJSON tracing to your C++ emulator

In `x86_cpu.cpp` / `pc98_dos_driver.cpp`, emit events like:

```json
{"tick":37,"type":"call","cs":4660,"ip":512}
{"tick":37,"type":"out","cs":4660,"ip":687,"port":392,"value":40}
{"tick":37,"type":"out","cs":4660,"ip":690,"port":394,"value":240}
{"tick":37,"type":"int","cs":4660,"ip":704,"int":33,"ah":63}
```

Recommended event types:

```text
out
in
int
call
ret
jmp
mem_write
note
```

Add CLI flags to your test runner later:

```text
--trace-io
--trace-int
--trace-call
--trace-port 0x188,0x18a,0x18c,0x18e
--stop-on-unimplemented
--trace-out analysis/traces/driver.ndjson
```

## 8. Compact traces

```bash
python tools/pc98/trace_compact.py analysis/traces/driver.ndjson \
  --tick 37 \
  --window 3 \
  --out analysis/reports/driver_tick37.txt
```

This produces a short text trace suitable for AI.

## 9. Compare expected vs actual behavior

```bash
python tools/pc98/trace_compare.py \
  --expected analysis/traces/reference.ndjson \
  --actual analysis/traces/newport.ndjson \
  --out analysis/reports/divergence.txt
```

The key output is the first divergence, not the whole trace.

## 10. Generate an AI context pack

```bash
python tools/pc98/make_context_pack.py \
  --driver /path/to/driver.drv \
  --divergence analysis/reports/divergence.txt \
  --disasm analysis/disasm/driver_0200.md \
  --trace analysis/traces/driver.ndjson \
  --tick 37 \
  --out analysis/context-packs/driver_issue_001.md
```

Give only this context pack to AI.

## Recommended workflow

1. Index all drivers.
2. Cluster driver families.
3. Add NDJSON trace output to the emulator.
4. Run one failing driver/song.
5. Compact the trace around the failing tick.
6. Compare against a reference trace if available.
7. Export disassembly around the failing `CS:IP`.
8. Generate an AI context pack.
9. Ask AI for the missing PC-98/DOS/music-driver behavior.
10. Implement one small fix and rerun the same trace.

## Rule

Do not feed whole source trees, whole driver binaries, or massive instruction logs to AI. Feed only:

- first divergence;
- 50-150 relevant instructions;
- compact I/O/INT/CALL trace;
- CPU state around the failure;
- one clear question.
