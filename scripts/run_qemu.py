#!/usr/bin/env python3
"""DEV-OS · run_qemu.py — arranca build/disk.img en QEMU (BIOS legacy).

Uso:
    python3 scripts/run_qemu.py [--image build/disk.img] [--debug] [--serial stdio]

--debug  : para QEMU con -s -S (gdb espera en :1234).
           Conecta con: gdb -ex 'target remote :1234' build/kernel.elf
--serial : redirige COM1; por defecto muestra serie en stdio.
"""
from __future__ import annotations

import argparse
import pathlib
import shutil
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent


def parse_args() -> argparse.Namespace:
    p = argparse.ArgumentParser(description="Arranca DEV-OS en QEMU")
    p.add_argument("--image", default=str(ROOT / "build" / "disk.img"))
    p.add_argument("--debug", action="store_true", help="-s -S (espera gdb)")
    p.add_argument("--serial", default="stdio", help="destino -serial (stdio|none|...)")
    p.add_argument("--memory", default="128M")
    p.add_argument("--no-reboot", action="store_true",
                   help="cierra QEMU ante un triple-fault (por defecto NO: "
                        "así un fallo se ve como reinicio en bucle, no como cierre mudo)")
    return p.parse_args()


def main() -> int:
    a = parse_args()
    qemu = shutil.which("qemu-system-x86_64")
    if not qemu:
        print("[qemu] ERROR: falta qemu-system-x86_64 "
              "(sudo apt install qemu-system-x86)", file=sys.stderr)
        return 1
    if not pathlib.Path(a.image).exists():
        print(f"[qemu] ERROR: no existe {a.image} — ejecuta `make` antes.",
              file=sys.stderr)
        return 1

    cmd = [qemu,
           "-drive", f"format=raw,file={a.image}",
           "-m", a.memory,
           "-serial", a.serial,
           "-display", "gtk"]
    # NOTA: antes se pasaba siempre -no-reboot; eso hacía que un
    # triple-fault cerrara la ventana sin ningún mensaje. Ahora solo
    # se pasa si se pide explícitamente con --no-reboot.
    if a.no_reboot:
        cmd += ["-no-reboot"]
    if a.debug:
        cmd += ["-s", "-S"]
        print("[qemu] DEBUG: parado, conecta gdb en :1234 y escribe 'continue'")
    print("[qemu]", " ".join(cmd))
    return subprocess.run(cmd).returncode


if __name__ == "__main__":
    sys.exit(main())
