# ─────────────────────────────────────────────────────────────
# DEV-OS · Makefile (SOLO Linux)
# x86_64 · C (kernel) + ASM (bootloader) + Python (automatización)
#
# Uso (en Linux):
#   make              → compila todo y guarda log en logs/
#   make qemu         → compila + arranca QEMU (disco BIOS)
#   make clean        → limpia build/
#   make check        → verifica toolchain
#   make log          → muestra el último log
#
# Todo lo que imprime la compilación se guarda en
# logs/build-YYYYMMDD-HHMMSS.log (vía tee). No borres logs/
# a mano: usa `make clean-logs` si te molestan.
# En Windows/macOS falla a propósito con un error claro.
# ─────────────────────────────────────────────────────────────

# ── 1. Puerta: solo Linux ─────────────────────────────────────
ifeq ($(shell uname -s),Linux)
  IS_LINUX := 1
else
  $(error SOLO-LINUX: este proyecto solo compila en Linux. Estas en '$(shell uname -s)'. Compila en tu entorno Linux)
endif

# ── 2. Toolchain (sobrescribible: make CC=gcc-13 ...) ─────────
CROSS   ?=
CC      := $(CROSS)gcc
LD      := $(CROSS)ld
OBJCOPY := $(CROSS)objcopy
NASM    ?= nasm
PYTHON  ?= python3
QEMU    ?= qemu-system-x86_64

# Flags estrictos pero de kernel freestanding 64 bits.
CFLAGS  ?= -std=c11 -Wall -Wextra -Werror -ffreestanding -fno-builtin \
           -m64 -mno-red-zone -mno-mmx -mno-sse -mno-sse2 \
           -fno-pic -fno-pie -fno-stack-protector -O2 -g
LDFLAGS ?= -nostdlib -static -no-pie
NASMFLAGS_BIN := -f bin
NASMFLAGS_ELF := -f elf64

# ── 3. Rutas ──────────────────────────────────────────────────
BOOT_DIR   := boot
KERNEL_DIR := kernel
SCRIPT_DIR := scripts
BUILD      := build
LOGS       := logs

STAGE1_SRC := $(BOOT_DIR)/stage1.asm
STAGE2_SRC := $(BOOT_DIR)/stage2.asm
ENTRY_SRC  := $(KERNEL_DIR)/entry.asm
CPU_SRC    := $(KERNEL_DIR)/cpu.asm
KERNEL_SRCS := $(KERNEL_DIR)/kernel.c $(KERNEL_DIR)/print.c \
               $(KERNEL_DIR)/gdt.c $(KERNEL_DIR)/idt.c $(KERNEL_DIR)/shell.c
KERNEL_HDRS := $(KERNEL_DIR)/vga.h $(KERNEL_DIR)/io.h $(KERNEL_DIR)/print.h \
               $(KERNEL_DIR)/gdt.h $(KERNEL_DIR)/idt.h $(KERNEL_DIR)/shell.h
LINKER     := $(KERNEL_DIR)/linker.ld

STAGE1_BIN := $(BUILD)/stage1.bin
STAGE2_BIN := $(BUILD)/stage2.bin
ENTRY_O    := $(BUILD)/entry.o
CPU_O      := $(BUILD)/cpu.o
KERNEL_OBJS := $(BUILD)/kernel.o $(BUILD)/print.o $(BUILD)/gdt.o \
               $(BUILD)/idt.o $(BUILD)/shell.o
KERNEL_ELF := $(BUILD)/kernel.elf
KERNEL_BIN := $(BUILD)/kernel.bin
DISK_IMG   := $(BUILD)/disk.img
BUILD_STAMP := $(BUILD)/.stamp

# stage2 ocupa 8 sectores fijos; el kernel empieza en LBA 9.
STAGE2_SECTORS := 8
KERNEL_LBA     := 9

# Log con timestamp (se crea en el target `all`).
LOG_FILE := $(LOGS)/build-$(shell date +%Y%m%d-%H%M%S).log

.PHONY: all build image qemu qemu-debug check clean clean-logs log sizes help

# `all` envuelve a `build` con tee → todo queda en el log.
# Llama: make build 2>&1 | tee logs/build-....log
all: | $(LOGS)
	@echo "[dev-os] compilando (log: $(LOG_FILE))"
	@$(MAKE) --no-print-directory build 2>&1 | tee "$(LOG_FILE)"
	@echo "[dev-os] OK. Binarios en $(BUILD)/ · log en $(LOG_FILE)"

build: $(DISK_IMG)
	@echo "[dev-os] imagen lista: $(DISK_IMG)"
	@$(MAKE) --no-print-directory sizes

# ── stage1 (512 B, firma AA55; nasm la pone) ──────────────────
$(STAGE1_BIN): $(STAGE1_SRC) | $(BUILD_STAMP)
	@echo "[asm] stage1 $< -> $@"
	$(NASM) $(NASMFLAGS_BIN) $< -o $@

# ── kernel ELF + bin plano ────────────────────────────────────
$(ENTRY_O): $(ENTRY_SRC) | $(BUILD_STAMP)
	@echo "[asm] entry $< -> $@"
	$(NASM) $(NASMFLAGS_ELF) $< -o $@

$(CPU_O): $(CPU_SRC) | $(BUILD_STAMP)
	@echo "[asm] cpu $< -> $@"
	$(NASM) $(NASMFLAGS_ELF) $< -o $@

$(BUILD)/%.o: $(KERNEL_DIR)/%.c $(KERNEL_HDRS) | $(BUILD_STAMP)
	@echo "[cc] $< -> $@"
	$(CC) $(CFLAGS) -c $< -o $@

$(KERNEL_ELF): $(ENTRY_O) $(CPU_O) $(KERNEL_OBJS) $(LINKER)
	@echo "[ld] $@"
	$(LD) $(LDFLAGS) -T $(LINKER) $(ENTRY_O) $(CPU_O) $(KERNEL_OBJS) -o $@

$(KERNEL_BIN): $(KERNEL_ELF)
	@echo "[objcopy] $@"
	$(OBJCOPY) -O binary $< $@

# ── stage2 (necesita saber cuántos sectores ocupa el kernel) ──
# Se calcula post-kernel: ceil(size(kernel.bin)/512).
$(STAGE2_BIN): $(STAGE2_SRC) $(KERNEL_BIN) | $(BUILD_STAMP)
	@echo "[asm] stage2 (KERNEL_SECTORS auto) -> $@"
	@SECTORS=$$(( ( $$(stat -c%s $(KERNEL_BIN)) + 511 ) / 512 )); \
	echo "      kernel: $$(stat -c%s $(KERNEL_BIN)) bytes = $$SECTORS sector(es)"; \
	$(NASM) $(NASMFLAGS_BIN) $< -o $@ \
	  -DKERNEL_SECTORS=$$SECTORS -DKERNEL_LBA=$(KERNEL_LBA)

# ── imagen de disco (lo monta scripts/mkimage.py) ─────────────
$(DISK_IMG): $(STAGE1_BIN) $(STAGE2_BIN) $(KERNEL_BIN) $(SCRIPT_DIR)/mkimage.py
	@echo "[img] $@"
	$(PYTHON) $(SCRIPT_DIR)/mkimage.py \
	  --stage1 $(STAGE1_BIN) --stage2 $(STAGE2_BIN) \
	  --kernel $(KERNEL_BIN) --output $@ \
	  --stage2-sectors $(STAGE2_SECTORS) --kernel-lba $(KERNEL_LBA)

# ── utilidades ────────────────────────────────────────────────
check:
	$(PYTHON) $(SCRIPT_DIR)/check_env.py

qemu: build
	$(PYTHON) $(SCRIPT_DIR)/run_qemu.py --image $(DISK_IMG)

# QEMU parado esperando gdb: `gdb -ex 'target remote :1234' build/kernel.elf`
qemu-debug: build
	$(PYTHON) $(SCRIPT_DIR)/run_qemu.py --image $(DISK_IMG) --debug

sizes:
	@echo "── tamaños ─────────────────────────────"
	@ls -l $(STAGE1_BIN) $(STAGE2_BIN) $(KERNEL_ELF) $(KERNEL_BIN) $(DISK_IMG)
	@echo "stage1 debe ser 512 B; stage2 4096 B ($(STAGE2_SECTORS) sectores)."

log:
	@ls -t $(LOGS)/build-*.log 2>/dev/null | head -n 1 | xargs -r cat

clean:
	rm -rf $(BUILD)
	mkdir -p $(BUILD)
	@echo "[dev-os] build/ limpio (logs/ intacto)."

clean-logs:
	rm -f $(LOGS)/build-*.log
	@echo "[dev-os] logs/ limpio."

help:
	@echo "Targets: all build image qemu qemu-debug check clean log sizes help"

# Sello: crea build/ y logs/ una vez. Existe porque el target
# `build` ya ocupa ese nombre (si fuese `$(BUILD):` habría
# dependencia circular y reglas anuladas).
$(BUILD_STAMP):
	mkdir -p $(BUILD) $(LOGS)
	touch $@

$(LOGS):
	mkdir -p $@
