; boot.asm - YD-OS stage 1 (512 bytes)
; Loads the kernel track-safely (one sector at a time) to 0x1000:0000 (phys 0x10000),
; then switches the CPU to 32-bit protected mode and jumps to it.
; Build: nasm -f bin -dKERNEL_SECTORS=<n> boot.asm -o boot.bin
[bits 16]
[org 0x7c00]

%ifndef KERNEL_SECTORS
%define KERNEL_SECTORS 40
%endif
KERNEL_SEG  equ 0x1000
KERNEL_ADDR equ 0x10000

start:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7c00
    sti
    mov [drive], dl

    mov si, msg_load
    call print

    mov ax, KERNEL_SEG
    mov es, ax
    mov word [lba], 1               ; sector 0 is this boot sector
    mov word [left], KERNEL_SECTORS
    mov byte [tries], 5

.next:
    cmp byte [drive], 0x80
    jae .lba_read

    ; ---- floppy: LBA -> CHS (18 sectors/track, 2 heads) ----
    mov ax, [lba]
    xor dx, dx
    mov cx, 18
    div cx                          ; ax = track, dx = sector-1
    mov cl, dl
    inc cl                          ; sector 1..18
    mov dh, al
    and dh, 1                       ; head
    shr ax, 1
    mov ch, al                      ; cylinder
    mov dl, [drive]
    xor bx, bx
    mov ax, 0x0201                  ; read 1 sector to es:bx
    int 0x13
    jmp .check

.lba_read:                          ; ---- hard disk / USB: int 13h extensions ----
    mov ax, [lba]
    mov [dap_lba], ax
    mov ax, es
    mov [dap_seg], ax
    mov si, dap
    mov dl, [drive]
    mov ah, 0x42
    int 0x13

.check:
    jnc .ok
    dec byte [tries]
    jz disk_error
    xor ax, ax                      ; reset controller and retry
    mov dl, [drive]
    int 0x13
    jmp .next

.ok:
    mov byte [tries], 5
    mov ax, es
    add ax, 0x20                    ; next 512 bytes
    mov es, ax
    inc word [lba]
    dec word [left]
    jnz .next

    ; ---- enter protected mode ----
    cli
    lgdt [gdt_descriptor]
    mov eax, cr0
    or eax, 1
    mov cr0, eax
    jmp CODE_SEG:init_pm

disk_error:
    mov si, msg_err
    call print
.hang:
    hlt
    jmp .hang

print:                              ; ds:si -> zero-terminated string
    lodsb
    test al, al
    jz .done
    mov ah, 0x0e
    xor bx, bx
    int 0x10
    jmp print
.done:
    ret

msg_load: db "YD-OS: loading kernel...", 13, 10, 0
msg_err:  db "YD-OS: disk read error", 0

drive: db 0
tries: db 5
lba:   dw 1
left:  dw 0
dap:                                ; disk address packet for int 13h ah=42h
    db 0x10, 0
    dw 1
    dw 0                            ; offset
dap_seg: dw 0
dap_lba: dd 0, 0

align 8
gdt_start:
    dq 0
gdt_code:
    dw 0xffff, 0x0000
    db 0x00, 10011010b, 11001111b, 0x00
gdt_data:
    dw 0xffff, 0x0000
    db 0x00, 10010010b, 11001111b, 0x00
gdt_end:
gdt_descriptor:
    dw gdt_end - gdt_start - 1
    dd gdt_start
CODE_SEG equ gdt_code - gdt_start
DATA_SEG equ gdt_data - gdt_start

[bits 32]
init_pm:
    mov ax, DATA_SEG
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov esp, 0x90000
    call KERNEL_ADDR
.halt:
    cli
    hlt
    jmp .halt

times 510 - ($ - $$) db 0
dw 0xaa55
