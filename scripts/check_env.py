#!/usr/bin/env python3
"""Cronix OS - check_env.py: verify the Linux toolchain.

Usage:
    python3 scripts/check_env.py
    make check

Fails (exit 1) if anything is missing. Compiles nothing.
Fails on Windows/macOS with a note: this project is Linux-only.
"""
from __future__ import annotations

import os
import platform
import shutil
import sys

REQUIRED = ["nasm", "make"]
# C compiler: prefer x86_64-elf-gcc, accept gcc/clang as fallback.
COMPILERS = ["x86_64-elf-gcc", "gcc", "clang"]
OPTIONAL = ["qemu-system-x86_64", "ld", "objcopy"]


def check_one(name: str) -> str | None:
    path = shutil.which(name)
    return path


def main() -> int:
    print("[check] Cronix OS - x86_64 - Linux-only")
    print(f"[check] host: {platform.system()} {platform.machine()}")

    if platform.system() != "Linux":
        print("[check] ERROR: this project builds on Linux ONLY. "
              "Use your Linux environment.", file=sys.stderr)
        return 1

    ok = True
    for tool in REQUIRED:
        path = check_one(tool)
        print(f"[check] {'OK  ':>5} {tool} -> {path}" if path
              else f"[check] MISSING {tool} (sudo apt install {tool})")
        ok = ok and bool(path)

    found_cc = None
    for cc in COMPILERS:
        path = check_one(cc)
        if path:
            found_cc = (cc, path)
            break
    if found_cc:
        print(f"[check] {'OK  ':>5} compiler {found_cc[0]} -> {found_cc[1]}")
        if found_cc[0] != "x86_64-elf-gcc":
            print("[check] WARNING: no x86_64-elf-gcc, using system gcc; "
                  "make sure to build freestanding (-ffreestanding -m64).")
    else:
        print("[check] MISSING C compiler (x86_64-elf-gcc or gcc). "
              "E.g.: sudo apt install build-essential nasm qemu-system-x86", file=sys.stderr)
        ok = False

    for tool in OPTIONAL:
        path = check_one(tool)
        print(f"[check] {'optional OK':>12} {tool} -> {path}" if path
              else f"[check] {'optional --':>12} {tool} not found (recommended)")

    # Report CROSS from the environment (the Makefile uses it).
    cross = os.environ.get("CROSS", "")
    if cross:
        print(f"[check] CROSS='{cross}' (Makefile prefix)")

    print("[check] environment OK" if ok else "[check] environment INCOMPLETE")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
