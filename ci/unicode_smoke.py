#!/usr/bin/env python3
import os
import pathlib
import subprocess
import sys

root = pathlib.Path(__file__).resolve().parents[1]
build = pathlib.Path(sys.argv[1]).resolve()
exe = build / ("hootplay.exe" if os.name == "nt" else "hootplay")
catalog = root / "catalog" / "hoot.sqlite.zst"
if not exe.is_file():
    raise SystemExit(f"missing executable: {exe}")
if not catalog.is_file():
    raise SystemExit(f"missing catalog: {catalog}")

env = os.environ.copy()
# Keep first-run state isolated and make the UTF-8 subprocess contract explicit.
env["HOME"] = str(build / "ci-home")
env["PYTHONUTF8"] = "1"
proc = subprocess.run(
    [str(exe), "--catalog", str(catalog), "--list"],
    cwd=root,
    env=env,
    stdout=subprocess.PIPE,
    stderr=subprocess.PIPE,
    encoding="utf-8",
    errors="strict",
    check=False,
)
if proc.returncode != 0:
    sys.stderr.write(proc.stderr)
    raise SystemExit(proc.returncode)
required = ["スレイヤーズ", "ドラゴン", "PC-9801", "Gradius IV"]
missing = [text for text in required if text not in proc.stdout]
if missing:
    # Keep diagnostics printable on Windows' legacy CP1252 console. The
    # catalogue comparison above remains a real Unicode check.
    raise SystemExit(f"UTF-8 catalog smoke failed; missing {ascii(missing)}")
# Use escaped Unicode in the status line so the check also works when Python
# inherits a non-UTF-8 console encoding (notably GitHub Actions on Windows).
print("UTF-8/Japanese catalogue smoke passed:", ", ".join(ascii(text) for text in required))
