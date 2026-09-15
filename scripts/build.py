#!/usr/bin/env python3
"""DEV-OS · build.py — compila todo y guarda el log.

Es lo mismo que `make`, pero garantiza el log aunque invoques
targets sueltos. Uso (en Linux):

    python3 scripts/build.py               # == make
    python3 scripts/build.py --target qemu # compila y arranca QEMU
    python3 scripts/build.py --no-log      # sin guardar log

El log queda en logs/build-YYYYMMDD-HHMMSS.log e incluye
la salida de `make` + toolchain usada.
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
    p = argparse.ArgumentParser(description="Compila DEV-OS con log")
    p.add_argument("--target", default="", help="target de make ('' = all)")
    p.add_argument("--no-log", action="store_true", help="no guardar log")
    return p.parse_args()


def main() -> int:
    a = parse_args()
    if platform.system() != "Linux":
        print("[build] ERROR: solo se compila en Linux.", file=sys.stderr)
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
    # Eco en consola + guardado en log (como `tee`).
    sys.stdout.write(out)
    if not a.no_log:
        header = (f"# DEV-OS build log {stamp}\n# cmd: {' '.join(cmd)}\n"
                  f"# platform: {platform.platform()}\n\n")
        log_path.write_text(header + out, encoding="utf-8")
        print(f"[build] exit={proc.returncode} log={log_path}")
    return proc.returncode


if __name__ == "__main__":
    sys.exit(main())
