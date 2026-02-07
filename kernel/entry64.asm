BITS 64

GLOBAL _start
EXTERN kernel_main

_start:
    call kernel_main
.hang:
    hlt
    jmp .hang
