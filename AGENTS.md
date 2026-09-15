# AGENTS.md — Cronix OS (x86_64 kernel)

> This file is the **source of truth for agents** (and for you when
> requesting changes). Edit it with care: what you put here drives
> how code gets written. Project language: **English** everywhere
> (docs, messages, comments, identifiers).

## 1. What it is (and what it is NOT)

- **Is**: a minimal 64-bit kernel booting in QEMU (legacy BIOS),
  base for a future dev-focused OS.
- **Is NOT**: a full OS (no TSS yet, no advanced VM, no scheduler,
  no syscalls, no FS, no net).
- Boot: `stage1 (MBR 512 B)` → `stage2 (loader, 8 sectors)` →
  `kernel` at `0x100000`, all in active **long mode**.
- Platform: **x86_64 only, legacy BIOS only, Linux-only builds**,
  runs on `qemu-system-x86_64`.

## 2. Layout (never move without updating Makefile + mkimage.py)

```text
Makefile             # Linux-only; `all` saves a log to logs/
boot/stage1.asm      # MBR: loads 8 sectors to 0x7E00, jumps. 512 B + AA55
boot/stage2.asm      # A20, LM check, loads kernel LBA 9→0x10000 (EDD),
                     # pm32: copy to 0x100000, 2MB paging 0-4MB, jump to 64b
kernel/entry.asm     # _start: 16KB stack, call kmain (ELF64)
kernel/cpu.asm       # gdt_flush, idt_load, ISR stubs 0-31 + IRQ 32-255
kernel/kernel.c      # kmain: print+gdt+idt+sti, then shell_run (C11)
kernel/print.h/.c    # VGA 80x25 + COM1 (dual echo), hex, backspace
kernel/io.h          # inb/outb/io_wait inline
kernel/gdt.h/.c      # Own GDT: null+code+data, gdt_init
kernel/idt.h/.c      # IDT 256: 0-31 dump+halt, PIC 0x20/0x28 masked
kernel/shell.h/.c    # mini-shell, PS/2 polling: help info clear halt reboot
kernel/vga.h         # VGA text cells/colors
kernel/linker.ld     # ENTRY(_start), base 0x100000
scripts/check_env.py # toolchain check (builds nothing)
scripts/mkimage.py   # assembles build/disk.img (fixed LBA layout)
scripts/build.py     # `make` with guaranteed log in logs/
scripts/run_qemu.py  # BIOS QEMU with disk.img (+ --debug for gdb)
build/               # generated (ignored): *.bin *.o *.elf *.img
logs/                # generated (partly versioned): build-*.log
```

**Disk layout (sacred contract)**: LBA 0 = stage1 · LBA 1–8 = stage2
(4096 B) · LBA 9+ = `kernel.bin`. Changing sizes means touching at
once: `stage1.asm (STAGE2_SECTORS)`, `stage2.asm`, `Makefile`, `mkimage.py`.

## 3. Toolchain and commands (Linux)

- Needs: `nasm`, `x86_64-elf-gcc` (or multilib `gcc` as fallback),
  `ld`, `objcopy`, `make`, `python3`, `qemu-system-x86_64`.
  E.g.: `sudo apt install build-essential nasm qemu-system-x86 gdb`.
- Check: `make check` · Build: `make` (or `python3 scripts/build.py`).
- Run: `make qemu` · Debug: `make qemu-debug` + `gdb build/kernel.elf`
  (`target remote :1234`). · Clean: `make clean` (keeps `logs/`).
- **Every `make` saves a log** to `logs/build-YYYYMMDD-HHMMSS.log` via `tee`.
  Attach that log when asking for build help.
- Override toolchain: `make CC=clang` or `CROSS=x86_64-elf- make`.
- **No Windows/macOS builds**: the Makefile aborts with an error.
  In this Windows environment the agent **never builds**: code only.

## 4. Code conventions

- **C (kernel)**: C11 `freestanding`, no libc (`-ffreestanding`,
  `-fno-builtin`, `-mno-red-zone`, `-mno-sse`). No `#include <stdio.h>`
  or `malloc` unless you implement it yourself. Small functions, `static`
  by default, `/* */` comments only where the why is not obvious.
  `-Wall -Wextra -Werror`: zero warnings or no build.
- **ASM (boot)**: NASM, lowercase labels, `%define`/`EQU` constants
  on top, each block tagged with its CPU mode
  (`16 real / 32 protected / 64 long`). stage1 ≤ 512 B with `times`+`AA55`;
  stage2 = exactly 4096 B (`times 4096-($-$$)`).
- **Python (scripts)**: `python3`, `argparse`, `main() -> int`,
  `[tag] text` messages, errors to `stderr` with `exit != 0`. No
  third-party deps (stdlib only) so they run on any Linux.
- Commits: short English messages, e.g. `kernel: print banner on serial`.

## 5. Definition of done (DoD)

Done means: `make` exits 0 on Linux, `make qemu` shows the
`Cronix OS kernel x86_64 ... OK` banner on VGA and serial, no new
warnings, the log sits in `logs/`, and `mkimage.py` accepts the
layout (AA55 signature, LBA sizes).

## 6. If QEMU exits on its own (no error)

Almost surely a **triple-fault** (e.g. paging enabled wrong,
`CR3` never loaded, broken GDT). Triage:

1. Watch **serial** (`-serial stdio`): the last `[S..]` line tells
   how far it got (`[S1]` → stage1, `[S2] ...` → stage2).
2. Watch the **top-left VGA corner**: `3` = 32-bit reached,
   `6` = 64-bit reached. No `3` means real-mode failure
   (disk/A20/CPUID); `3` without `6` means paging failure.
3. `run_qemu.py` passes **no** `-no-reboot` by default on purpose: a
   fault shows as a reboot loop, not a silent exit. Use
   `--no-reboot` only when asked.
4. Fine debug: `make qemu-debug` + `gdb build/kernel.elf`
   (`target remote :1234`, `continue`, `info registers`).

## 7. How to request changes (template — copy and fill in)

```text
Goal: [e.g. add IDT with exceptions 0-31 printing to VGA]
Scope: [kernel/ only | boot/ only | scripts | Makefile]
Constraints: [e.g. no GRUB, max X lines, no libc]
Check: [e.g. `make qemu` must show "..." on serial]
Do not: [e.g. do not touch stage1, no new pip deps]
```

State **where** (file/function), **what QEMU must show**, and
**what is forbidden**. Without those three, ask before coding.

## 8. Roadmap (one at a time)

1. `IDT + ISR 0-31` with VGA/serial dump — DONE (polling, no IRQ).
2. Own `GDT` — DONE (null+code+data; `TSS` missing).
3. Own shell — DONE (PS/2 polling; IRQ1 + scroll missing).
4. `PMM` (bitmap over `memmap` from stage2) + `kheap`.
5. `PIT` clock + IRQ keyboard.
6. `syscalls` + `ring3` + basic `ELF`.

---
*Last review: 2026-09-15. Changing the boot layout means updating
this file IN THE SAME commit.*
