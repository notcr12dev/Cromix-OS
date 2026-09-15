; ─────────────────────────────────────────────────────────────
; Cronix OS · stage2 (loader)
; Entered at 0x7E00 in 16-bit real mode (jumped from stage1).
; Steps:
;   1. A20 on
;   2. Check CPUID + Long Mode (0x80000001:EDX bit 29)
;   3. Load kernel (LBA 9, KERNEL_SECTORS) at 0x10000 via int13 EDD
;   4. 32-bit protected mode -> copy kernel 0x10000 -> 0x100000
;   5. Identity paging 0-16MB (2MB pages), EFER.LME, CR0.PG
;   6. Far jump to 64-bit, jmp to 0x100000 (kernel entry)
;
; Disk layout (see scripts/mkimage.py):
;   LBA sector 0 : stage1 (512 B)
;   LBA sectors 1..8 : stage2 (8 x 512 = 4096 B, zero-padded)
;   LBA sectors 9..  : kernel.bin (KERNEL_SECTORS sectors)
;
; Build (done by the Makefile, which injects KERNEL_SECTORS):
;   nasm -f bin boot/stage2.asm -o build/stage2.bin \
;       -DKERNEL_SECTORS=32 -DKERNEL_LBA=9
; ─────────────────────────────────────────────────────────────
BITS 16
ORG 0x7E00

%ifndef KERNEL_SECTORS
%define KERNEL_SECTORS 64
%endif
%ifndef KERNEL_LBA
%define KERNEL_LBA 9
%endif

KERNEL_STAGE_SEG EQU 0x1000    ; ES=0x1000 -> physical 0x10000 (staging < 1MB)
KERNEL_STAGE_OFF EQU 0x0000
KERNEL_HIGH      EQU 0x100000  ; final destination (1 MB)
PML4_ADDR        EQU 0x70000
PDPT_ADDR        EQU 0x71000
PD_ADDR          EQU 0x72000

stage2_start:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00
    sti
    mov [boot_drive], dl

    mov si, msg_s2
    call bios_print

    ; ── 1. A20 ──────────────────────────────────────────────
    ; Fast try via BIOS; fallback to keyboard controller.
    mov ax, 0x2401
    int 0x15                   ; ES irrelevant here
.a20_kbd:
    call a20_wait_in
    mov al, 0xD1
    out 0x64, al
    call a20_wait_in
    mov al, 0xDF
    out 0x60, al
    call a20_wait_in
    mov al, 0xFF
    out 0x64, al
    call a20_wait_in
    mov si, msg_a20
    call bios_print

    ; ── 2. CPUID + Long Mode ────────────────────────────────
    pushfd
    pop eax
    mov ecx, eax
    xor eax, 0x200000          ; flip ID bit
    push eax
    popfd
    pushfd
    pop eax
    xor eax, ecx
    jz no_longmode             ; no CPUID
    mov eax, 0x80000000
    cpuid
    cmp eax, 0x80000001
    jb no_longmode
    mov eax, 0x80000001
    cpuid
    test edx, 1 << 29          ; LM bit
    jz no_longmode
    mov si, msg_lm
    call bios_print

    ; ── 3. Load kernel at 0x10000 (EDD int 0x13 AH=42h) ─────
    mov si, msg_load
    call bios_print
    mov dl, [boot_drive]
    mov si, dap
    mov ah, 0x42
    int 0x13
    jc disk_error
    mov si, msg_load_ok
    call bios_print

    ; ── 4. 32-bit protected mode ────────────────────────────
    cli
    lgdt [gdt32_desc]
    mov eax, cr0
    or eax, 1                  ; PE
    mov cr0, eax
    jmp 0x08:pm32_entry

disk_error:
    mov si, msg_derr
    call bios_print
    cli
.hang:
    hlt
    jmp .hang

no_longmode:
    mov si, msg_nolm
    call bios_print
    cli
.hang2:
    hlt
    jmp .hang2

; ── 16-bit helpers ──────────────────────────────────────────
a20_wait_in:
    in al, 0x64
    test al, 2
    jnz a20_wait_in
    ret

bios_print:
    pusha
    push ds
    xor ax, ax
    mov ds, ax
.loop:
    lodsb
    test al, al
    jz .done
    mov ah, 0x0E
    xor bh, bh
    mov bl, 0x07
    int 0x10
    jmp .loop
.done:
    pop ds
    popa
    ret

boot_drive: db 0
msg_s2:      db '[S2] loader start', 13, 10, 0
msg_a20:     db '[S2] A20 on', 13, 10, 0
msg_lm:      db '[S2] long mode OK', 13, 10, 0
msg_load:    db '[S2] loading kernel...', 13, 10, 0
msg_load_ok: db '[S2] kernel staged at 0x10000', 13, 10, 0
msg_derr:    db '[S2] KERNEL DISK ERROR', 13, 10, 0
msg_nolm:    db '[S2] ERROR: CPU has no x86_64', 13, 10, 0

ALIGN 4
; Disk Address Packet (EDD)
dap:
    db 0x10                    ; packet size
    db 0x00                    ; reserved
    dw KERNEL_SECTORS          ; sector count
    dw KERNEL_STAGE_OFF        ; buffer offset
    dw KERNEL_STAGE_SEG        ; buffer segment (0x1000 -> 0x10000)
    dd KERNEL_LBA              ; start LBA (low dword)
    dd 0x00000000              ; start LBA (high dword)

; 32-bit GDT: null, flat code, flat data
ALIGN 8
gdt32:
    dq 0x0000000000000000
    dq 0x00CF9A000000FFFF      ; code: base 0, 4GB limit, 32-bit
    dq 0x00CF92000000FFFF      ; data: base 0, 4GB limit
gdt32_desc:
    dw gdt32_desc - gdt32 - 1
    dd gdt32

; 64-bit GDT: null, code64, data64
ALIGN 8
gdt64:
    dq 0x0000000000000000
    dq 0x00209A0000000000      ; code64: L=1, executable, readable
    dq 0x0000920000000000      ; data64
gdt64_desc:
    dw gdt64_desc - gdt64 - 1
    dq gdt64

; ─────────────────────────────────────────────────────────────
; 32-bit (protected). Copy kernel, set up paging, enter long mode.
; ─────────────────────────────────────────────────────────────
BITS 32
pm32_entry:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov esp, 0x7C00

    ; Direct VGA marker (no more BIOS): '3' top-left = 32-bit reached.
    mov word [0xB8000], 0x1F33

    ; Copy kernel 0x10000 -> 0x100000 (KERNEL_SECTORS * 512 bytes)
    mov esi, 0x10000
    mov edi, KERNEL_HIGH
    mov ecx, (KERNEL_SECTORS * 512) / 4
    cld
    rep movsd

    ; Clear tables (3 x 4KB)
    mov edi, PML4_ADDR
    mov ecx, (4096 * 3) / 4
    xor eax, eax
    cld
    rep stosd

    ; PML4[0] = PDPT | P+RW
    mov eax, PDPT_ADDR
    or eax, 0x03
    mov [PML4_ADDR], eax
    ; PDPT[0] = PD | P+RW
    mov eax, PD_ADDR
    or eax, 0x03
    mov [PDPT_ADDR], eax
    ; PD: 8 x 2MB entries -> identity map 0-16MB.
    ; Kernel heap lives at 4-8MB, so it must be mapped here.
    mov edi, PD_ADDR
    mov eax, 0x00000083        ; 2MB, P+RW+PS
    mov ecx, 8
.pd_fill:
    mov [edi], eax
    add eax, 0x200000
    add edi, 8
    loop .pd_fill

    ; CR3 -> PML4. MANDATORY: without it, enabling PG translates
    ; through whatever tables the BIOS left -> triple fault,
    ; and QEMU exits with no message.
    mov eax, PML4_ADDR
    mov cr3, eax

    ; PAE + LME + PG
    mov eax, cr4
    or eax, 1 << 5             ; PAE
    mov cr4, eax
    mov ecx, 0xC0000080        ; EFER
    rdmsr
    or eax, 1 << 8             ; LME
    wrmsr
    mov eax, cr0
    or eax, 1 << 31            ; PG
    mov cr0, eax

    lgdt [gdt64_desc]
    jmp 0x08:lm64_entry

; ─────────────────────────────────────────────────────────────
; 64-bit. Segments, stack, jump to kernel at 0x100000.
; ─────────────────────────────────────────────────────────────
BITS 64
lm64_entry:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov rsp, 0x90000           ; temp stack below 1MB (mapped)
    ; VGA marker: '6' = 64-bit reached, jumping to kernel.
    mov word [0xB8002], 0x1F36
    jmp KERNEL_HIGH            ; kernel entry (_start)

; Pad to 8 sectors (4096 B). mkimage.py checks this.
    times 4096 - ($ - $$) db 0
