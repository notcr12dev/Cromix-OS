#!/usr/bin/env python3
"""DEV-OS · mkimage.py — compone build/disk.img (arranque BIOS).

Layout fijo (debe coincidir con boot/*.asm y el Makefile):
    LBA 0      : stage1 (exacto 512 B, firma 0xAA55)
    LBA 1..8   : stage2 (rellenado a STAGE2_SECTORS*512, por defecto 8)
    LBA 9..    : kernel.bin (plano)
    resto      : ceros hasta --size-mb (por defecto 8 MB)

Uso (lo llama el Makefile; manual):
    python3 scripts/mkimage.py --stage1 build/stage1.bin \\
        --stage2 build/stage2.bin --kernel build/kernel.bin \\
        --output build/disk.img

Arranca con: qemu-system-x86_64 -drive format=raw,file=build/disk.img
"""
from __future__ import annotations

import argparse
import pathlib
import sys

SECTOR = 512


def parse_args() -> argparse.Namespace:
    p = argparse.ArgumentParser(description="Compone disk.img de DEV-OS")
    p.add_argument("--stage1", required=True, help="stage1.bin (512 B)")
    p.add_argument("--stage2", required=True, help="stage2.bin (<= N sectores)")
    p.add_argument("--kernel", required=True, help="kernel.bin plano")
    p.add_argument("--output", required=True, help="imagen de salida")
    p.add_argument("--stage2-sectors", type=int, default=8)
    p.add_argument("--kernel-lba", type=int, default=9)
    p.add_argument("--size-mb", type=int, default=8)
    return p.parse_args()


def main() -> int:
    a = parse_args()
    s1 = pathlib.Path(a.stage1).read_bytes()
    s2 = pathlib.Path(a.stage2).read_bytes()
    kern = pathlib.Path(a.kernel).read_bytes()

    if len(s1) != SECTOR:
        print(f"[mkimage] ERROR: stage1 mide {len(s1)}, debe ser 512", file=sys.stderr)
        return 1
    if s1[510] != 0x55 or s1[511] != 0xAA:
        print("[mkimage] ERROR: stage1 sin firma 0xAA55", file=sys.stderr)
        return 1
    if a.kernel_lba != a.stage2_sectors + 1:
        print(f"[mkimage] ERROR: kernel-lba ({a.kernel_lba}) != "
              f"stage2-sectors+1 ({a.stage2_sectors + 1})", file=sys.stderr)
        return 1
    max_s2 = a.stage2_sectors * SECTOR
    if len(s2) > max_s2:
        print(f"[mkimage] ERROR: stage2 mide {len(s2)}, máximo {max_s2}", file=sys.stderr)
        return 1
    if len(s2) < max_s2:
        s2 = s2 + b"\x00" * (max_s2 - len(s2))

    total = a.size_mb * 1024 * 1024
    need = len(s1) + len(s2) + len(kern)
    if need > total:
        print(f"[mkimage] ERROR: contenido ({need}) > imagen ({total})", file=sys.stderr)
        return 1

    img = bytearray(total)
    img[0:512] = s1
    img[512:512 + len(s2)] = s2
    k_off = a.kernel_lba * SECTOR
    img[k_off:k_off + len(kern)] = kern

    pathlib.Path(a.output).parent.mkdir(parents=True, exist_ok=True)
    pathlib.Path(a.output).write_bytes(bytes(img))
    n_sec = (len(kern) + SECTOR - 1) // SECTOR
    print(f"[mkimage] OK {a.output}: stage1=512B stage2={len(s2)}B "
          f"kernel={len(kern)}B ({n_sec} sectores, LBA {a.kernel_lba}) "
          f"total={a.size_mb}MB")
    return 0


if __name__ == "__main__":
    sys.exit(main())
