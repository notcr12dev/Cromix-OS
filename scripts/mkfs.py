#!/usr/bin/env python3
"""Cronix OS - mkfs.py: format a FAT16 volume inside disk.img.

Creates the system dirs /bin /sys /home (single level).
Must match kernel/fat.c (FS_LBA) and the Makefile (FS_MB).

Usage:
    python3 scripts/mkfs.py --image build/disk.img --lba 2048 --size-mb 16

WARNING: wipes the region. `make` runs this on every fresh image,
so files stored in QEMU do not survive a rebuild.
"""
from __future__ import annotations

import argparse
import pathlib
import struct
import sys

SECTOR = 512
SPC = 8            # sectors per cluster (4 KB clusters)
ROOT_ENTRIES = 512
FATS = 2
RESERVED = 1


def parse_args() -> argparse.Namespace:
    p = argparse.ArgumentParser(description="Format Cronix OS FAT16 volume")
    p.add_argument("--image", required=True)
    p.add_argument("--lba", type=int, default=2048)
    p.add_argument("--size-mb", type=int, default=16)
    return p.parse_args()


def dir_entry(name11: bytes, attr: int, cluster: int, size: int) -> bytes:
    e = bytearray(32)
    e[0:11] = name11
    e[11] = attr
    e[26:28] = struct.pack("<H", cluster)
    e[28:32] = struct.pack("<I", size)
    return bytes(e)


def pad83(name: str) -> bytes:
    base, _, ext = name.upper().partition(".")
    return (base[:8].ljust(8) + ext[:3].ljust(3)).encode("ascii")


def main() -> int:
    a = parse_args()
    total = a.size_mb * 1024 * 1024 // SECTOR
    root_sec = ROOT_ENTRIES * 32 // SECTOR

    fatsz = 1
    for _ in range(5):  # converge FAT size <-> cluster count
        data = total - RESERVED - FATS * fatsz - root_sec
        clusters = data // SPC
        fatsz = (clusters * 2 + SECTOR - 1) // SECTOR
    data = total - RESERVED - FATS * fatsz - root_sec
    clusters = data // SPC
    if not 4085 <= clusters <= 65525:
        print(f"[mkfs] ERROR: {clusters} clusters, not FAT16 range", file=sys.stderr)
        return 1

    img_path = pathlib.Path(a.image)
    if not img_path.exists():
        print(f"[mkfs] ERROR: {a.image} missing - run `make build` first",
              file=sys.stderr)
        return 1
    need = (a.lba + total) * SECTOR
    if img_path.stat().st_size < need:
        print(f"[mkfs] ERROR: image too small for FS at LBA {a.lba}", file=sys.stderr)
        return 1

    # --- boot sector + BPB ---
    boot = bytearray(SECTOR)
    boot[0:3] = b"\xEB\x3C\x90"
    boot[3:11] = b"CRONIXFS"
    struct.pack_into("<H", boot, 11, SECTOR)
    boot[13] = SPC
    struct.pack_into("<H", boot, 14, RESERVED)
    boot[16] = FATS
    struct.pack_into("<H", boot, 17, ROOT_ENTRIES)
    struct.pack_into("<H", boot, 19, total if total < 65535 else 0)
    boot[21] = 0xF8
    struct.pack_into("<H", boot, 22, fatsz)
    struct.pack_into("<I", boot, 32, total)
    boot[38] = 0x29
    struct.pack_into("<I", boot, 39, 0xC9071)
    boot[43:54] = b"CRONIX VOL "
    boot[54:62] = b"FAT16   "
    boot[510] = 0x55
    boot[511] = 0xAA

    # --- FATs: media + end-of-chain for clusters 2,3,4 (bin/sys/home) ---
    fat = bytearray(fatsz * SECTOR)
    struct.pack_into("<HHHH", fat, 0, 0xFFF8, 0xFFFF, 0xFFFF, 0xFFFF)
    fat[4 + 2 * 2:4 + 2 * 2 + 2] = struct.pack("<H", 0xFFFF)  # cluster 4

    # --- root dir: BIN=2 SYS=3 HOME=4 ---
    root = bytearray(root_sec * SECTOR)
    root[0:32] = dir_entry(pad83("BIN"), 0x10, 2, 0)
    root[32:64] = dir_entry(pad83("SYS"), 0x10, 3, 0)
    root[64:96] = dir_entry(pad83("HOME"), 0x10, 4, 0)

    # --- subdir clusters: '.' + '..', rest zero ---
    def subdir(self_cl: int) -> bytes:
        d = bytearray(SPC * SECTOR)
        d[0:32] = dir_entry(pad83("."), 0x10, self_cl, 0)
        d[32:64] = dir_entry(pad83(".."), 0x10, 0, 0)
        return bytes(d)

    with open(img_path, "r+b") as f:
        base = a.lba * SECTOR
        f.seek(base)
        f.write(boot)
        for _ in range(FATS):
            f.write(fat)
        f.write(root)
        data_off = base + (RESERVED + FATS * fatsz + root_sec) * SECTOR
        f.seek(data_off)
        f.write(subdir(2))
        f.write(subdir(3))
        f.write(subdir(4))
        # rest of the volume stays zero = free clusters

    print(f"[mkfs] OK {a.image}: FAT16 {a.size_mb}MB @LBA {a.lba}, "
          f"{clusters} clusters, dirs /bin /sys /home")
    return 0


if __name__ == "__main__":
    sys.exit(main())
