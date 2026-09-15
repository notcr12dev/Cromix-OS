#!/usr/bin/env python3
"""Cronix OS - run_qemu.py: boot build/disk.img in QEMU (legacy BIOS).

Usage:
    python3 scripts/run_qemu.py [--image build/disk.img] [--debug] [--serial stdio]

--debug  : start QEMU with -s -S (gdb waits on :1234).
           Attach with: gdb -ex 'target remote :1234' build/kernel.elf
--serial : COM1 target; default shows serial on stdio.
"""
from __future__ import annotations

import argparse
import pathlib
import shutil
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent


def parse_args() -> argparse.Namespace:
    p = argparse.ArgumentParser(description="Boot Cronix OS in QEMU")
    p.add_argument("--image", default=str(ROOT / "build" / "disk.img"))
    p.add_argument("--debug", action="store_true", help="-s -S (wait for gdb)")
    p.add_argument("--serial", default="stdio", help="-serial target (stdio|none|...)")
    p.add_argument("--memory", default="128M")
    p.add_argument("--no-reboot", action="store_true",
                   help="close QEMU on triple-fault (default OFF: "
                        "a fault shows as a reboot loop, not a silent exit)")
    return p.parse_args()


def main() -> int:
    a = parse_args()
    qemu = shutil.which("qemu-system-x86_64")
    if not qemu:
        print("[qemu] ERROR: missing qemu-system-x86_64 "
              "(sudo apt install qemu-system-x86)", file=sys.stderr)
        return 1
    if not pathlib.Path(a.image).exists():
        print(f"[qemu] ERROR: {a.image} missing - run `make` first.",
              file=sys.stderr)
        return 1

    cmd = [qemu,
           "-drive", f"format=raw,file={a.image}",
           "-m", a.memory,
           "-serial", a.serial,
           "-display", "gtk"]
    # NOTE: -no-reboot used to be always on; it made triple-faults
    # close the window with no message. Now it is opt-in only.
    if a.no_reboot:
        cmd += ["-no-reboot"]
    if a.debug:
        cmd += ["-s", "-S"]
        print("[qemu] DEBUG: halted, attach gdb on :1234 then type 'continue'")
    print("[qemu]", " ".join(cmd))
    return subprocess.run(cmd).returncode


if __name__ == "__main__":
    sys.exit(main())
