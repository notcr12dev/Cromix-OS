#!/usr/bin/env python3
"""DEV-OS · check_env.py — verifica el toolchain en Linux.

Uso:
    python3 scripts/check_env.py
    make check

Falla (exit 1) si falta algo. No compila nada.
En Windows/macOS avisa y falla: el proyecto es SOLO-Linux.
"""
from __future__ import annotations

import os
import platform
import shutil
import sys

REQUIRED = ["nasm", "make"]
# Compilador cruzado: preferimos x86_64-elf-gcc, aceptamos gcc/clang como plan B.
COMPILERS = ["x86_64-elf-gcc", "gcc", "clang"]
OPTIONAL = ["qemu-system-x86_64", "ld", "objcopy"]


def check_one(name: str) -> str | None:
    path = shutil.which(name)
    return path


def main() -> int:
    print("[check] DEV-OS · x86_64 · solo-Linux")
    print(f"[check] sistema: {platform.system()} {platform.machine()}")

    if platform.system() != "Linux":
        print("[check] ERROR: este proyecto SOLO compila en Linux. "
              "Usa tu entorno Linux.", file=sys.stderr)
        return 1

    ok = True
    for tool in REQUIRED:
        path = check_one(tool)
        print(f"[check] {'OK  ':>5} {tool} -> {path}" if path
              else f"[check] FALTA {tool} (sudo apt install {tool})")
        ok = ok and bool(path)

    found_cc = None
    for cc in COMPILERS:
        path = check_one(cc)
        if path:
            found_cc = (cc, path)
            break
    if found_cc:
        print(f"[check] {'OK  ':>5} compilador {found_cc[0]} -> {found_cc[1]}")
        if found_cc[0] != "x86_64-elf-gcc":
            print("[check] AVISO: sin x86_64-elf-gcc usas el gcc del sistema; "
                  "asegúrate de compilar freestanding (-ffreestanding -m64).")
    else:
        print("[check] FALTA compilador C (x86_64-elf-gcc o gcc). "
              "Ej: sudo apt install build-essential nasm qemu-system-x86", file=sys.stderr)
        ok = False

    for tool in OPTIONAL:
        path = check_one(tool)
        print(f"[check] {'opcional OK':>12} {tool} -> {path}" if path
              else f"[check] {'opcional --':>12} {tool} no encontrado (recomendado)")

    # Si hay CROSS en el entorno, infórmalo (el Makefile lo usa).
    cross = os.environ.get("CROSS", "")
    if cross:
        print(f"[check] CROSS='{cross}' (prefijo del Makefile)")

    print("[check] entorno OK" if ok else "[check] entorno INCOMPLETO")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
