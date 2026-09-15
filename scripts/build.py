#!/usr/bin/env python3
"""Cronix OS - build.py: build everything, keep the log.

Same as `make`, but guarantees a log even when invoking
single targets. Usage (on Linux):

    python3 scripts/build.py               # == make
    python3 scripts/build.py --target qemu # build and boot QEMU
    python3 scripts/build.py --no-log      # skip saving the log

The log lands in logs/build-YYYYMMDD-HHMMSS.log and holds
the `make` output plus the toolchain in use.
"""
from __future__ import annotations

import argparse
import datetime
import pathlib
import platform
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
LOGS = ROOT / "logs"


def parse_args() -> argparse.Namespace:
    p = argparse.ArgumentParser(description="Build Cronix OS with log")
    p.add_argument("--target", default="", help="make target ('' = all)")
    p.add_argument("--no-log", action="store_true", help="do not save log")
    return p.parse_args()


def main() -> int:
    a = parse_args()
    if platform.system() != "Linux":
        print("[build] ERROR: builds on Linux only.", file=sys.stderr)
        return 1

    LOGS.mkdir(parents=True, exist_ok=True)
    stamp = datetime.datetime.now().strftime("%Y%m%d-%H%M%S")
    log_path = LOGS / f"build-{stamp}.log"

    cmd = ["make"]
    if a.target:
        cmd.append(a.target)
    print(f"[build] {' '.join(cmd)} (log: {log_path if not a.no_log else 'OFF'})")

    proc = subprocess.run(cmd, cwd=ROOT, text=True,
                          stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    out = proc.stdout or ""
    # Echo to console + save to log (like `tee`).
    sys.stdout.write(out)
    if not a.no_log:
        header = (f"# Cronix OS build log {stamp}\n# cmd: {' '.join(cmd)}\n"
                  f"# platform: {platform.platform()}\n\n")
        log_path.write_text(header + out, encoding="utf-8")
        print(f"[build] exit={proc.returncode} log={log_path}")
    return proc.returncode


if __name__ == "__main__":
    sys.exit(main())
