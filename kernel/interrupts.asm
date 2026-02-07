; Interrupt Service Routines (ISRs) for 64-bit kernel
; These handlers save context, call C handler, and restore context

BITS 64

; External C handlers
EXTERN exception_handler
EXTERN keyboard_isr

; Exception handler macro - for exceptions with error code
%macro ISR_ERROR 2
GLOBAL isr_%1
isr_%1:
    ; Error code is already on stack
    push rax
    push rcx
    push rdx
    push rsi
    push rdi
    push r8
    push r9
    push r10
    push r11
    
    mov rdi, %2        ; Vector number as first arg (RDI)
    mov rsi, [rsp + 80]; Error code (was at RSP before pushes, now at RSP+80)
    mov rdx, [rsp + 88]; RIP from exception frame
    call exception_handler
    
    pop r11
    pop r10
    pop r9
    pop r8
    pop rdi
    pop rsi
    pop rdx
    pop rcx
    pop rax
    add rsp, 8         ; Skip error code
    iretq
%endmacro

; Exception handler macro - for exceptions without error code (we inject one)
%macro ISR_NOERR 2
GLOBAL isr_%1
isr_%1:
    push qword 0       ; Inject error code 0
    push rax
    push rcx
    push rdx
    push rsi
    push rdi
    push r8
    push r9
    push r10
    push r11
    
    mov rdi, %2        ; Vector number as first arg (RDI)
    mov rsi, 0         ; Error code = 0 (second arg, RSI)
    mov rdx, [rsp + 80]; RIP from exception frame (11 pushes × 8 bytes)
    call exception_handler
    
    pop r11
    pop r10
    pop r9
    pop r8
    pop rdi
    pop rsi
    pop rdx
    pop rcx
    pop rax
    add rsp, 8         ; Skip injected error code only
    iretq
%endmacro

; CPU Exceptions (0-19)
ISR_NOERR divide_error,       0   ; 0 - Divide by zero
ISR_NOERR debug,              1   ; 1 - Debug
ISR_NOERR nmi,                2   ; 2 - Non-maskable interrupt
ISR_NOERR breakpoint,         3   ; 3 - Breakpoint
ISR_NOERR overflow,           4   ; 4 - Overflow
ISR_NOERR bound,              5   ; 5 - Bound range exceeded
ISR_NOERR invalid_opcode,     6   ; 6 - Invalid opcode
ISR_NOERR device_na,          7   ; 7 - Device not available
ISR_ERROR double_fault,       8   ; 8 - Double fault (has error code)
ISR_NOERR tss,                10  ; 10 - Invalid TSS (skip 9)
ISR_ERROR segment,            11  ; 11 - Segment not present
ISR_ERROR stack,              12  ; 12 - Stack segment fault
ISR_ERROR general_protection, 13  ; 13 - General protection fault
ISR_ERROR page_fault,         14  ; 14 - Page fault
ISR_NOERR floating_point,     16  ; 16 - x87 floating point (skip 15)
ISR_NOERR alignment,          17  ; 17 - Alignment check
ISR_NOERR machine_check,      18  ; 18 - Machine check
ISR_NOERR simd,               19  ; 19 - SIMD floating point

; Interrupt Handlers (32+)
GLOBAL isr_keyboard
isr_keyboard:
    push rax
    push rcx
    push rdx
    push rsi
    push rdi
    push r8
    push r9
    push r10
    push r11
    
    call keyboard_isr
    
    ; Send EOI (End of Interrupt) to PIC
    mov al, 0x20
    out 0x20, al       ; EOI to master PIC
    
    pop r11
    pop r10
    pop r9
    pop r8
    pop rdi
    pop rsi
    pop rdx
    pop rcx
    pop rax
    iretq
