#ifndef IDT_H
#define IDT_H

#include "types.h"

/* IDT Descriptor - 16 bytes in 64-bit mode */
typedef struct {
    uint16_t offset_low;      /* 0-15   bits of ISR handler address */
    uint16_t segment;         /* Code segment selector (0x08 for kernel) */
    uint8_t  ist;             /* Interrupt Stack Table (0 for now) */
    uint8_t  attributes;      /* Type and flags */
    uint16_t offset_mid;      /* 16-31  bits of ISR handler address */
    uint32_t offset_high;     /* 32-63  bits of ISR handler address */
    uint32_t reserved;        /* Reserved, must be 0 */
} __attribute__((packed)) IDT_DESCRIPTOR;

/* IDT Register (for LIDT instruction) */
typedef struct {
    uint16_t limit;           /* Size of IDT - 1 */
    uint64_t base;            /* Base address of IDT */
} __attribute__((packed)) IDT_REGISTER;

/* Exception handler prototype */
typedef void (*exception_handler_t)(void);

/* Public function declarations */
void idt_init(void);
void idt_set_descriptor(uint8_t vector, uint64_t handler, uint8_t flags);
void idt_load(void);
void idt_enable_interrupts(void);

/* Exception codes */
#define IDT_FLAGS_PRESENT     0x80
#define IDT_FLAGS_RING0       0x00
#define IDT_FLAGS_INTERRUPT   0x0E
#define IDT_FLAGS_TRAP        0x0F

/* Exception vectors */
#define VECTOR_DIVIDE_ERROR     0
#define VECTOR_DEBUG            1
#define VECTOR_NMI              2
#define VECTOR_BREAKPOINT       3
#define VECTOR_OVERFLOW         4
#define VECTOR_BOUND            5
#define VECTOR_INVALID_OPCODE   6
#define VECTOR_DEVICE_NA        7
#define VECTOR_DOUBLE_FAULT     8
#define VECTOR_TSS              10
#define VECTOR_SEGMENT          11
#define VECTOR_STACK            12
#define VECTOR_GENERAL_PROTECT  13
#define VECTOR_PAGE_FAULT       14
#define VECTOR_FLOAT            16
#define VECTOR_ALIGN            17
#define VECTOR_MACHINE_CHECK    18
#define VECTOR_SIMD             19

#endif /* IDT_H */
