# ─────────────────────────────────────────────────────────────
# Cronix OS · Makefile (Linux ONLY)
# x86_64 · C (kernel) + ASM (bootloader) + Python (automation)
#
# Usage (on Linux):
#   make              → build all, save log to logs/
#   make qemu         → build + boot QEMU (BIOS disk)
#   make clean        → wipe build/
#   make check        → check toolchain
#   make log          → show latest log
#
# Everything the build prints goes to
# logs/build-YYYYMMDD-HHMMSS.log (via tee). Do not delete logs/
# by hand: use `make clean-logs`.
# Fails on purpose on Windows/macOS with a clear error.
# ─────────────────────────────────────────────────────────────

# ── 1. Gate: Linux only ───────────────────────────────────────
ifeq ($(shell uname -s),Linux)
  IS_LINUX := 1
else
  $(error LINUX-ONLY: this project builds on Linux only. You are on '$(shell uname -s)'. Build in your Linux environment)
endif

# ── 2. Toolchain (override: make CC=gcc-13 ...) ─────────────
CROSS   ?=
CC      := $(CROSS)gcc
LD      := $(CROSS)ld
OBJCOPY := $(CROSS)objcopy
NASM    ?= nasm
PYTHON  ?= python3
QEMU    ?= qemu-system-x86_64

# Strict but freestanding 64-bit kernel flags.
CFLAGS  ?= -std=c11 -Wall -Wextra -Werror -ffreestanding -fno-builtin \
           -m64 -mno-red-zone -mno-mmx -mno-sse -mno-sse2 \
           -fno-pic -fno-pie -fno-stack-protector -O2 -g
LDFLAGS ?= -nostdlib -static -no-pie
NASMFLAGS_BIN := -f bin
NASMFLAGS_ELF := -f elf64

# ── 3. Paths ──────────────────────────────────────────────────
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
               $(KERNEL_DIR)/gdt.c $(KERNEL_DIR)/idt.c $(KERNEL_DIR)/shell.c \
               $(KERNEL_DIR)/heap.c $(KERNEL_DIR)/ata.c $(KERNEL_DIR)/fat.c \
               $(KERNEL_DIR)/vfs.c $(KERNEL_DIR)/edit.c \
               $(KERNEL_DIR)/pci.c $(KERNEL_DIR)/e1000.c \
               $(KERNEL_DIR)/net.c $(KERNEL_DIR)/wget.c
KERNEL_HDRS := $(KERNEL_DIR)/vga.h $(KERNEL_DIR)/io.h $(KERNEL_DIR)/print.h \
               $(KERNEL_DIR)/gdt.h $(KERNEL_DIR)/idt.h $(KERNEL_DIR)/shell.h \
               $(KERNEL_DIR)/heap.h $(KERNEL_DIR)/ata.h $(KERNEL_DIR)/fat.h \
               $(KERNEL_DIR)/vfs.h $(KERNEL_DIR)/edit.h \
               $(KERNEL_DIR)/pci.h $(KERNEL_DIR)/e1000.h \
               $(KERNEL_DIR)/net.h $(KERNEL_DIR)/wget.h
LINKER     := $(KERNEL_DIR)/linker.ld

STAGE1_BIN := $(BUILD)/stage1.bin
STAGE2_BIN := $(BUILD)/stage2.bin
ENTRY_O    := $(BUILD)/entry.o
CPU_O      := $(BUILD)/cpu.o
KERNEL_OBJS := $(BUILD)/kernel.o $(BUILD)/print.o $(BUILD)/gdt.o \
               $(BUILD)/idt.o $(BUILD)/shell.o $(BUILD)/heap.o \
               $(BUILD)/ata.o $(BUILD)/fat.o $(BUILD)/vfs.o \
               $(BUILD)/edit.o $(BUILD)/pci.o $(BUILD)/e1000.o \
               $(BUILD)/net.o $(BUILD)/wget.o
KERNEL_ELF := $(BUILD)/kernel.elf
KERNEL_BIN := $(BUILD)/kernel.bin
DISK_IMG   := $(BUILD)/disk.img
BUILD_STAMP := $(BUILD)/.stamp

# FAT16 volume: fixed LBA + size (kernel/fat.h FS_LBA must match).
FS_LBA := 2048
FS_MB  := 16
IMG_MB := 20
# stage2 is a fixed 8 sectors; kernel starts at LBA 9.
STAGE2_SECTORS := 8
KERNEL_LBA     := 9

# Timestamped log (created by the `all` target).
LOG_FILE := $(LOGS)/build-$(shell date +%Y%m%d-%H%M%S).log

.PHONY: all build image qemu qemu-debug check clean clean-logs log sizes help fs

# `all` wraps `build` with tee → everything lands in the log.
# Runs: make build 2>&1 | tee logs/build-....log
all: | $(LOGS)
	@echo "[cronix] building (log: $(LOG_FILE))"
	@$(MAKE) --no-print-directory build 2>&1 | tee "$(LOG_FILE)"
	@echo "[cronix] OK. Binaries in $(BUILD)/ · log at $(LOG_FILE)"

build: $(DISK_IMG)
	@echo "[cronix] image ready: $(DISK_IMG)"
	@$(MAKE) --no-print-directory sizes

# ── stage1 (512 B, AA55 signature; nasm emits it) ─────────────
$(STAGE1_BIN): $(STAGE1_SRC) | $(BUILD_STAMP)
	@echo "[asm] stage1 $< -> $@"
	$(NASM) $(NASMFLAGS_BIN) $< -o $@

# ── kernel ELF + flat binary ──────────────────────────────────
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

# ── stage2 (needs the kernel sector count) ───────────────────
# Computed post-kernel: ceil(size(kernel.bin)/512).
$(STAGE2_BIN): $(STAGE2_SRC) $(KERNEL_BIN) | $(BUILD_STAMP)
	@echo "[asm] stage2 (auto KERNEL_SECTORS) -> $@"
	@SECTORS=$$(( ( $$(stat -c%s $(KERNEL_BIN)) + 511 ) / 512 )); \
	echo "      kernel: $$(stat -c%s $(KERNEL_BIN)) bytes = $$SECTORS sector(s)"; \
	$(NASM) $(NASMFLAGS_BIN) $< -o $@ \
	  -DKERNEL_SECTORS=$$SECTORS -DKERNEL_LBA=$(KERNEL_LBA)

# ── disk image (built by scripts/mkimage.py, formatted by mkfs.py)
# NOTE: formatting wipes the FS region, so files saved in QEMU
# do NOT survive a rebuild. Back them up via serial if needed.
$(DISK_IMG): $(STAGE1_BIN) $(STAGE2_BIN) $(KERNEL_BIN) $(SCRIPT_DIR)/mkimage.py $(SCRIPT_DIR)/mkfs.py
	@echo "[img] $@"
	$(PYTHON) $(SCRIPT_DIR)/mkimage.py \
	  --stage1 $(STAGE1_BIN) --stage2 $(STAGE2_BIN) \
	  --kernel $(KERNEL_BIN) --output $@ \
	  --stage2-sectors $(STAGE2_SECTORS) --kernel-lba $(KERNEL_LBA) \
	  --size-mb $(IMG_MB)
	$(PYTHON) $(SCRIPT_DIR)/mkfs.py --image $@ \
	  --lba $(FS_LBA) --size-mb $(FS_MB)

# ── helpers ─────────────────────────────────────────────────────
check:
	$(PYTHON) $(SCRIPT_DIR)/check_env.py

# Reformat the FAT16 volume of an existing image (wipes files).
fs:
	$(PYTHON) $(SCRIPT_DIR)/mkfs.py --image $(DISK_IMG) \
	  --lba $(FS_LBA) --size-mb $(FS_MB)

qemu: build
	$(PYTHON) $(SCRIPT_DIR)/run_qemu.py --image $(DISK_IMG)

# QEMU halted waiting for gdb: `gdb -ex 'target remote :1234' build/kernel.elf`
qemu-debug: build
	$(PYTHON) $(SCRIPT_DIR)/run_qemu.py --image $(DISK_IMG) --debug

sizes:
	@echo "── sizes ───────────────────────────────"
	@ls -l $(STAGE1_BIN) $(STAGE2_BIN) $(KERNEL_ELF) $(KERNEL_BIN) $(DISK_IMG)
	@echo "stage1 must be 512 B; stage2 4096 B ($(STAGE2_SECTORS) sectors)."

log:
	@ls -t $(LOGS)/build-*.log 2>/dev/null | head -n 1 | xargs -r cat

clean:
	rm -rf $(BUILD)
	mkdir -p $(BUILD)
	@echo "[cronix] build/ clean (logs/ kept)."

clean-logs:
	rm -f $(LOGS)/build-*.log
	@echo "[cronix] logs/ clean."

help:
	@echo "Targets: all build image qemu qemu-debug check clean log sizes help"

# Stamp: creates build/ and logs/ once. Exists because the
# `build` target already owns that name (a `$(BUILD):` rule
# would cause a circular dependency and clobbered rules).
$(BUILD_STAMP):
	mkdir -p $(BUILD) $(LOGS)
	touch $@

$(LOGS):
	mkdir -p $@
