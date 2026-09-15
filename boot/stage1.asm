; ─────────────────────────────────────────────────────────────
; DEV-OS · stage1 (MBR)
; 16 bits, real mode. Cabe en 512 bytes con firma 0xAA55.
; Hace UNA sola cosa: cargar stage2 (8 sectores, LBA 1..8)
; en 0x7E00 con BIOS int 0x13 AH=02h y saltar allí.
; Arquitectura: x86_64 (arranque vía BIOS legacy, QEMU).
; Ensamblar: nasm -f bin boot/stage1.asm -o build/stage1.bin
; ─────────────────────────────────────────────────────────────
BITS 16
ORG 0x7C00

STAGE2_LOAD_SEG EQU 0x0000
STAGE2_LOAD_OFF EQU 0x7E00
STAGE2_SECTORS  EQU 8          ; debe coincidir con Makefile/mkimage.py
STAGE2_START_LBA EQU 1         ; sector LBA 1 == CHS sector 2

start:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00             ; pila crece hacia abajo desde 0x7C00
    sti
    mov [boot_drive], dl       ; DL lo pone la BIOS (0x00 floppy / 0x80 HDD)

    mov si, msg_s1
    call bios_print

    ; --- cargar stage2: ES:BX = 0x0000:0x7E00 ---
    mov ax, STAGE2_LOAD_SEG
    mov es, ax
    mov bx, STAGE2_LOAD_OFF
    mov ah, 0x02               ; leer sectores
    mov al, STAGE2_SECTORS
    mov ch, 0                  ; cilindro 0
    mov cl, STAGE2_START_LBA + 1 ; sector BIOS = LBA+1 (sectores 2..9)
    mov dh, 0                  ; cabeza 0
    mov dl, [boot_drive]
    int 0x13
    jc disk_error               ; CF=1 -> error

    mov si, msg_ok
    call bios_print

    mov dl, [boot_drive]        ; recarga: int 0x13 puede tocar DL
    jmp 0x0000:0x7E00           ; saltar a stage2 (DL = disco de arranque)

disk_error:
    mov si, msg_err
    call bios_print
    cli
.hang:
    hlt
    jmp .hang

; SI -> cadena ASCIIZ. Usa int 0x10 AH=0x0E.
bios_print:
    pusha
.loop:
    lodsb
    test al, al
    jz .done
    mov ah, 0x0E
    mov bh, 0x00
    mov bl, 0x07
    int 0x10
    jmp .loop
.done:
    popa
    ret

boot_drive: db 0
msg_s1: db '[S1] DEV-OS boot', 13, 10, 0
msg_ok: db '[S1] stage2 OK', 13, 10, 0
msg_err: db '[S1] DISK ERROR', 13, 10, 0

    times 510 - ($ - $$) db 0
    dw 0xAA55
