; ─────────────────────────────────────────────────────────────
; Cronix OS · stage1 (MBR)
; 16-bit real mode. Fits in 512 bytes with 0xAA55 signature.
; Does ONE thing: load stage2 (8 sectors, LBA 1..8) at 0x7E00
; via BIOS int 0x13 AH=02h, then jump there.
; Arch: x86_64 (legacy BIOS boot, QEMU).
; Build: nasm -f bin boot/stage1.asm -o build/stage1.bin
; ─────────────────────────────────────────────────────────────
BITS 16
ORG 0x7C00

STAGE2_LOAD_SEG EQU 0x0000
STAGE2_LOAD_OFF EQU 0x7E00
STAGE2_SECTORS  EQU 8          ; must match Makefile/mkimage.py
STAGE2_START_LBA EQU 1         ; LBA sector 1 == CHS sector 2

start:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00             ; stack grows down from 0x7C00
    sti
    mov [boot_drive], dl       ; BIOS leaves drive in DL (0x00 floppy / 0x80 HDD)

    mov si, msg_s1
    call bios_print

    ; --- load stage2: ES:BX = 0x0000:0x7E00 ---
    mov ax, STAGE2_LOAD_SEG
    mov es, ax
    mov bx, STAGE2_LOAD_OFF
    mov ah, 0x02               ; read sectors
    mov al, STAGE2_SECTORS
    mov ch, 0                  ; cylinder 0
    mov cl, STAGE2_START_LBA + 1 ; BIOS sector = LBA+1 (sectors 2..9)
    mov dh, 0                  ; head 0
    mov dl, [boot_drive]
    int 0x13
    jc disk_error               ; CF=1 -> error

    mov si, msg_ok
    call bios_print

    mov dl, [boot_drive]        ; reload: int 0x13 may clobber DL
    jmp 0x0000:0x7E00           ; jump to stage2 (DL = boot drive)

disk_error:
    mov si, msg_err
    call bios_print
    cli
.hang:
    hlt
    jmp .hang

; SI -> zero-terminated ASCII. Uses int 0x10 AH=0x0E.
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
msg_s1: db '[S1] Cronix OS boot', 13, 10, 0
msg_ok: db '[S1] stage2 OK', 13, 10, 0
msg_err: db '[S1] DISK ERROR', 13, 10, 0

    times 510 - ($ - $$) db 0
    dw 0xAA55
