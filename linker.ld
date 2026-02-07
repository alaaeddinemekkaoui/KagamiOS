/* ============================================
 * LINKER SCRIPT
 * ============================================
 * This tells the linker where to place each
 * section of the kernel in memory
 * ============================================
 */

OUTPUT_FORMAT(elf64-x86-64)
ENTRY(_start)              /* Entry point is _start from entry64.asm */

SECTIONS {
    . = 1M;                /* Kernel starts at 1MB in memory */

    .text : {
        *(.text)           /* Code section */
        *(.text*)
    }

    .rodata : {
        *(.rodata)         /* Read-only data */
        *(.rodata*)
    }

    .data : {
        *(.data)           /* Initialized data */
        *(.data*)
    }

    .bss : {
        *(.bss)            /* Uninitialized data */
        *(.bss*)
        *(COMMON)
    }
}
