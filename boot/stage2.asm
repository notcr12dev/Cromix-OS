; ─────────────────────────────────────────────────────────────
; DEV-OS · stage2 (loader)
; Entra en 0x7E00 en modo real 16 bits (saltado desde stage1).
; Pasos:
;   1. A20 on
;   2. Comprobar CPUID + Long Mode (0x80000001:EDX bit 29)
;   3. Cargar kernel (LBA 9, KERNEL_SECTORS) a 0x10000 vía int13 EDD
;   4. Modo protegido 32 bits -> copiar kernel 0x10000 -> 0x100000
;   5. Paginación identity 0-4MB (páginas 2MB), EFER.LME, CR0.PG
;   6. Salto lejano a 64 bits y jmp a 0x100000 (entry del kernel)
;
; Layout de disco (ver scripts/mkimage.py):
;   sector LBA 0 : stage1 (512 B)
;   sectores LBA 1..8 : stage2 (8 x 512 = 4096 B, relleno con ceros)
;   sectores LBA 9..  : kernel.bin (KERNEL_SECTORS sectores)
;
; Ensamblar (lo hace el Makefile, que inyecta KERNEL_SECTORS):
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

KERNEL_STAGE_SEG EQU 0x1000    ; ES=0x1000 -> físico 0x10000 (staging < 1MB)
KERNEL_STAGE_OFF EQU 0x0000
KERNEL_HIGH      EQU 0x100000  ; destino final (1 MB)
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
    ; Intento rápido por BIOS; si falla, vía controlador de teclado.
    mov ax, 0x2401
    int 0x15                   ; ES irrelevante aquí
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
    jz no_longmode             ; CPUID no disponible
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

    ; ── 3. Cargar kernel a 0x10000 (EDD int 0x13 AH=42h) ────
    mov si, msg_load
    call bios_print
    mov dl, [boot_drive]
    mov si, dap
    mov ah, 0x42
    int 0x13
    jc disk_error
    mov si, msg_load_ok
    call bios_print

    ; ── 4. Modo protegido 32 bits ───────────────────────────
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

; ── helpers 16 bits ─────────────────────────────────────────
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
msg_nolm:    db '[S2] ERROR: CPU sin x86_64', 13, 10, 0

ALIGN 4
; Disk Address Packet (EDD)
dap:
    db 0x10                    ; tamaño del paquete
    db 0x00                    ; reservado
    dw KERNEL_SECTORS          ; nº sectores
    dw KERNEL_STAGE_OFF        ; offset buffer
    dw KERNEL_STAGE_SEG        ; segmento buffer (0x1000 -> 0x10000)
    dd KERNEL_LBA              ; LBA inicial (dword bajo)
    dd 0x00000000              ; LBA alto

; GDT 32 bits: null, code flat, data flat
ALIGN 8
gdt32:
    dq 0x0000000000000000
    dq 0x00CF9A000000FFFF      ; code: base 0, límite 4GB, 32 bits
    dq 0x00CF92000000FFFF      ; data: base 0, límite 4GB
gdt32_desc:
    dw gdt32_desc - gdt32 - 1
    dd gdt32

; GDT 64 bits: null, code64, data64
ALIGN 8
gdt64:
    dq 0x0000000000000000
    dq 0x00209A0000000000      ; code64: L=1, ejecutable, leído
    dq 0x0000920000000000      ; data64
gdt64_desc:
    dw gdt64_desc - gdt64 - 1
    dq gdt64

; ─────────────────────────────────────────────────────────────
; 32 bits (protegido). Copia kernel, pagina, entra en long mode.
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

    ; Chivato VGA directo (ya no hay BIOS int 0x10):
    ; '3' arriba a la izquierda = llegamos a 32 bits.
    mov word [0xB8000], 0x1F33

    ; Copiar kernel 0x10000 -> 0x100000 (KERNEL_SECTORS * 512 bytes)
    mov esi, 0x10000
    mov edi, KERNEL_HIGH
    mov ecx, (KERNEL_SECTORS * 512) / 4
    cld
    rep movsd

    ; Limpiar tablas (3 x 4KB)
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
    ; PD[0] = 0x00000083 (2MB, P+RW+PS) -> cubre 0-2MB
    mov dword [PD_ADDR], 0x00000083
    ; PD[1] = 0x20000083 -> cubre 2-4MB (kernel en 0x100000 ✓)
    mov dword [PD_ADDR + 8], 0x20000083

    ; CR3 -> PML4. ¡OBLIGATORIO! Sin esto, al activar PG la CPU
    ; traduce con las tablas que dejara la BIOS -> triple-fault
    ; y QEMU se cierra sin mensaje.
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
; 64 bits. Segmentos, pila y salto al kernel en 0x100000.
; ─────────────────────────────────────────────────────────────
BITS 64
lm64_entry:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov rsp, 0x90000           ; pila temporal bajo 1MB (mapeada)
    ; Chivato VGA: '6' = llegamos a 64 bits, saltamos al kernel.
    mov word [0xB8002], 0x1F36
    jmp KERNEL_HIGH            ; entry del kernel (_start)

; Relleno hasta 8 sectores (4096 B). mkimage.py lo verifica.
    times 4096 - ($ - $$) db 0
