# AGENTS.md — DEV-OS (kernel x86_64)

> Este archivo es la **fuente de verdad para agentes** (y para ti cuando
> pidas cambios). Edítalo con precisión: lo que pongas aquí dirige cómo
> se escribe código. Idioma del proyecto: **español** en docs y mensajes,
> **inglés** en identificadores de código.

## 1. Qué es (y qué NO es)

- **Es**: un kernel mínimo de 64 bits para arrancar en QEMU (BIOS legacy)
  y servir de base a un futuro SO enfocado en desarrollo.
- **NO es**: un SO completo (sin IDT/GDT propia, sin memoria virtual
  avanzada, sin scheduler, sin syscalls, sin FS, sin red).
- Arranque: `stage1 (MBR 512 B)` → `stage2 (loader, 8 sectores)` →
  `kernel` en `0x100000`, todo en modo **long mode** ya activo.
- Plataforma: **solo x86_64, solo BIOS legacy, solo Linux para compilar**,
  ejecución en `qemu-system-x86_64`.

## 2. Estructura (no mover sin actualizar Makefile + mkimage.py)

```text
Makefile             # solo-Linux; `all` guarda log en logs/
boot/stage1.asm      # MBR: carga 8 sectores a 0x7E00, jmp. 512 B + AA55
boot/stage2.asm      # A20, check LM, carga kernel LBA 9→0x10000 (EDD),
                     # pm32: copia a 0x100000, pagina 2MB 0-4MB, salto a 64b
kernel/entry.asm     # _start: pila 16KB, call kmain (ELF64)
kernel/kernel.c      # kmain: VGA 80x25 + COM1, hlt (freestanding C11)
kernel/vga.h         # celdas/colores VGA texto
kernel/linker.ld     # ENTRY(_start), base 0x100000
scripts/check_env.py # verifica toolchain (no compila)
scripts/mkimage.py   # compone build/disk.img (layout LBA fijo)
scripts/build.py     # `make` con log garantizado en logs/
scripts/run_qemu.py  # QEMU BIOS con disk.img (+ --debug para gdb)
build/               # generado (ignorado): *.bin *.o *.elf *.img
logs/                # generado (versionado parcial): build-*.log
```

**Layout de disco (contrato sagrado)**: LBA 0 = stage1 · LBA 1–8 = stage2
(4096 B) · LBA 9+ = `kernel.bin`. Si cambias tamaños, toca a la vez:
`stage1.asm (STAGE2_SECTORS)`, `stage2.asm`, `Makefile`, `mkimage.py`.

## 3. Toolchain y comandos (Linux)

- Requisitos: `nasm`, `x86_64-elf-gcc` (o `gcc` multilib como plan B),
  `ld`, `objcopy`, `make`, `python3`, `qemu-system-x86_64`.
  Ej: `sudo apt install build-essential nasm qemu-system-x86 gdb`.
- Verificar: `make check` · Compilar: `make` (o `python3 scripts/build.py`).
- Probar: `make qemu` · Depurar: `make qemu-debug` + `gdb build/kernel.elf`
  (`target remote :1234`). · Limpiar: `make clean` (respeta `logs/`).
- **Todo `make` guarda log** en `logs/build-YYYYMMDD-HHMMSS.log` vía `tee`.
  Adjunta ese log cuando pidas ayuda con un fallo de compilación.
- Sobrescribir toolchain: `make CC=clang` o `CROSS=x86_64-elf- make`.
- **Prohibido compilar en Windows/macOS**: el Makefile aborta con error.
  En este entorno Windows el agente **no compila**: solo escribe código.

## 4. Convenciones de código

- **C (kernel)**: C11 `freestanding`, sin libc (`-ffreestanding`,
  `-fno-builtin`, `-mno-red-zone`, `-mno-sse`). Nada de `#include <stdio.h>`
  ni `malloc` salvo que lo implementes tú. Funciones pequeñas, `static`
  por defecto, comentarios `/* */` solo donde el porqué no sea obvio.
  `-Wall -Wextra -Werror`: compila sin warnings o no compila.
- **ASM (boot)**: NASM, etiquetas minúsculas, constantes `%define`/`EQU`
  arriba, cada bloque con comentario de qué modo CPU usa
  (`16 real / 32 protegido / 64 long`). stage1 ≤ 512 B con `times`+`AA55`;
  stage2 = exactamente 4096 B (`times 4096-($-$$)`).
- **Python (scripts)**: `python3`, `argparse`, funciones `main() -> int`,
  mensajes `[tag] texto`, errores a `stderr` con `exit != 0`. Sin
  dependencias externas (solo stdlib) para que funcionen en cualquier Linux.
- Commits: mensajes cortos en español, ej: `kernel: imprime banner en serie`.

## 5. Definición de "hecho" (DoD)

Un cambio está hecho solo si: `make` termina en 0 en Linux, `make qemu`
muestra el banner `DEV-OS kernel x86_64 ... OK` en VGA y serie, no hay
warnings nuevos, el log queda en `logs/`, y `mkimage.py` no protesta del
layout (firma AA55, tamaños LBA).

## 6. Si QEMU se cierra solo (sin error)

Casi seguro es un **triple-fault** (p. ej. paginación mal activada,
`CR3` sin cargar, GDT rota). Diagnóstico por descarte:

1. Mira la **serie** (`-serial stdio`): el último `[S..]` impreso dice
   hasta dónde llegó (`[S1]` → stage1, `[S2] ...` → stage2).
2. Mira la **esquina superior izquierda del VGA**: `3` = se entró en
   32 bits, `6` = se entró en 64 bits. Si no hay ni `3`, el fallo está
   en modo real (disco/A20/CPUID); si hay `3` pero no `6`, en paginación.
3. `run_qemu.py` **no** pasa `-no-reboot` por defecto a propósito: un
   fallo se ve como reinicio en bucle, no como cierre mudo. Solo usa
   `--no-reboot` si lo pides.
4. Depuración fina: `make qemu-debug` + `gdb build/kernel.elf`
   (`target remote :1234`, `continue`, `info registers`).

## 7. Cómo pedirme cambios (plantilla — cópiala y rellena)

```text
Objetivo: [ej: añadir IDT con excepciones 0-31 que impriman en VGA]
Alcance: [solo kernel/ | solo boot/ | scripts | Makefile]
Restricciones: [ej: no usar GRUB, máx X líneas, sin libc]
Verificación: [ej: `make qemu` debe mostrar "..." en serie]
No hacer: [ej: no tocar stage1, no añadir dependencias pip]
```

Sé específico en **dónde** (archivo/función), **qué debe pasar en QEMU** y
**qué está prohibido**. Sin esos tres datos, preguntaré antes de codificar.

## 8. Roadmap sugerido (elige uno por vez)

1. `IDT + ISR 0-31` con volcado de registros en VGA/serie.
2. `GDT` propia + `TSS` mínima.
3. `PMM` (bitmap sobre `memmap` pasada por stage2) + `kheap`.
4. `reloj/PIT` + `teclado PS/2` → mini-shell en serie.
5. `syscalls` + ` ring3` + `ELF` básico.

---
*Última revisión: 2026-09-15. Si cambias el layout de arranque, actualiza
este archivo EL MISMO commit.*
