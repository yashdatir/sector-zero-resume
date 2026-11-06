[bits 32]
[extern kmain]
global _start
section .text
_start:
    call kmain
.hang:
    cli
    hlt
    jmp .hang
