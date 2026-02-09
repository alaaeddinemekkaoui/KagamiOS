#include "idt.h"
#include "keyboard.h"
#include "include/serial.h"

/* IDT table - 256 entries for 256 possible interrupt vectors */
static IDT_DESCRIPTOR idt[256];
static IDT_REGISTER idt_reg;

/* Keyboard handler counter (for demonstration) */
static volatile uint32_t keyboard_presses = 0;

/* Interrupt debug hook (serial-safe) */
static void debug_show_keypress(void) {
    /* Keep minimal to avoid heavy I/O in ISR */
}

/* Exception handler stubs (defined in interrupts.asm) */
extern void isr_divide_error(void);
extern void isr_debug(void);
extern void isr_nmi(void);
extern void isr_breakpoint(void);
extern void isr_overflow(void);
extern void isr_bound(void);
extern void isr_invalid_opcode(void);
extern void isr_device_na(void);
extern void isr_double_fault(void);
extern void isr_tss(void);
extern void isr_segment(void);
extern void isr_stack(void);
extern void isr_general_protection(void);
extern void isr_page_fault(void);
extern void isr_floating_point(void);
extern void isr_alignment(void);
extern void isr_machine_check(void);
extern void isr_simd(void);
extern void isr_keyboard(void);

static void append_hex64(char *buf, int *pos, uint64_t value) {
    const char *hex = "0123456789ABCDEF";
    for (int i = 15; i >= 0; i--) {
        buf[(*pos)++] = hex[(value >> (i * 4)) & 0xF];
    }
}

/* Generic exception handler for all unhandled exceptions */
void default_exception_handler(uint8_t vector, uint64_t error_code, uint64_t rip) {
    char buf[80];
    int pos = 0;

    const char *prefix = "EXCEPTION: vec=";
    while (*prefix) {
        buf[pos++] = *prefix++;
    }

    /* vector in decimal (0-255) */
    if (vector >= 100) {
        buf[pos++] = '0' + (vector / 100);
        buf[pos++] = '0' + ((vector / 10) % 10);
        buf[pos++] = '0' + (vector % 10);
    } else if (vector >= 10) {
        buf[pos++] = '0' + (vector / 10);
        buf[pos++] = '0' + (vector % 10);
    } else {
        buf[pos++] = '0' + vector;
    }

    const char *mid = " rip=0x";
    while (*mid) {
        buf[pos++] = *mid++;
    }
    append_hex64(buf, &pos, rip);

    const char *mid2 = " err=0x";
    while (*mid2) {
        buf[pos++] = *mid2++;
    }
    append_hex64(buf, &pos, error_code);

    buf[pos++] = '\n';
    buf[pos] = 0;

    serial_write(buf);

    while (1) {
        __asm__ __volatile__("cli; hlt");
    }
}

/* Entry point for all exception handlers (called from interrupts.asm) */
void exception_handler(uint8_t vector, uint64_t error_code, uint64_t rip) {
    default_exception_handler(vector, error_code, rip);
}

/* Keyboard interrupt handler (PS/2 controller) */
void keyboard_isr(void) {
    uint8_t scancode;
    __asm__ __volatile__("inb $0x60, %0" : "=a"(scancode));
    
    keyboard_presses++;
    debug_show_keypress();
    keyboard_process_scancode(scancode);
}

void idt_set_descriptor(uint8_t vector, uint64_t handler, uint8_t flags) {
    idt[vector].offset_low = handler & 0xFFFF;
    idt[vector].segment = 0x08;  /* Kernel code segment */
    idt[vector].ist = 0;
    idt[vector].attributes = flags;
    idt[vector].offset_mid = (handler >> 16) & 0xFFFF;
    idt[vector].offset_high = (handler >> 32) & 0xFFFFFFFF;
    idt[vector].reserved = 0;
}

void idt_init(void) {
    /* Clear the entire IDT */
    for (int i = 0; i < 256; i++) {
        idt[i].attributes = 0;
    }
    
    /* Set up exception handlers for CPU exceptions */
    idt_set_descriptor(VECTOR_DIVIDE_ERROR, (uint64_t)isr_divide_error, 
                       IDT_FLAGS_PRESENT | IDT_FLAGS_INTERRUPT);
    idt_set_descriptor(VECTOR_DEBUG, (uint64_t)isr_debug, 
                       IDT_FLAGS_PRESENT | IDT_FLAGS_INTERRUPT);
    idt_set_descriptor(VECTOR_NMI, (uint64_t)isr_nmi, 
                       IDT_FLAGS_PRESENT | IDT_FLAGS_INTERRUPT);
    idt_set_descriptor(VECTOR_BREAKPOINT, (uint64_t)isr_breakpoint, 
                       IDT_FLAGS_PRESENT | IDT_FLAGS_INTERRUPT);
    idt_set_descriptor(VECTOR_OVERFLOW, (uint64_t)isr_overflow, 
                       IDT_FLAGS_PRESENT | IDT_FLAGS_INTERRUPT);
    idt_set_descriptor(VECTOR_BOUND, (uint64_t)isr_bound, 
                       IDT_FLAGS_PRESENT | IDT_FLAGS_INTERRUPT);
    idt_set_descriptor(VECTOR_INVALID_OPCODE, (uint64_t)isr_invalid_opcode, 
                       IDT_FLAGS_PRESENT | IDT_FLAGS_INTERRUPT);
    idt_set_descriptor(VECTOR_DEVICE_NA, (uint64_t)isr_device_na, 
                       IDT_FLAGS_PRESENT | IDT_FLAGS_INTERRUPT);
    idt_set_descriptor(VECTOR_DOUBLE_FAULT, (uint64_t)isr_double_fault, 
                       IDT_FLAGS_PRESENT | IDT_FLAGS_INTERRUPT);
    idt_set_descriptor(VECTOR_TSS, (uint64_t)isr_tss, 
                       IDT_FLAGS_PRESENT | IDT_FLAGS_INTERRUPT);
    idt_set_descriptor(VECTOR_SEGMENT, (uint64_t)isr_segment, 
                       IDT_FLAGS_PRESENT | IDT_FLAGS_INTERRUPT);
    idt_set_descriptor(VECTOR_STACK, (uint64_t)isr_stack, 
                       IDT_FLAGS_PRESENT | IDT_FLAGS_INTERRUPT);
    idt_set_descriptor(VECTOR_GENERAL_PROTECT, (uint64_t)isr_general_protection, 
                       IDT_FLAGS_PRESENT | IDT_FLAGS_INTERRUPT);
    idt_set_descriptor(VECTOR_PAGE_FAULT, (uint64_t)isr_page_fault, 
                       IDT_FLAGS_PRESENT | IDT_FLAGS_INTERRUPT);
    idt_set_descriptor(VECTOR_FLOAT, (uint64_t)isr_floating_point, 
                       IDT_FLAGS_PRESENT | IDT_FLAGS_INTERRUPT);
    idt_set_descriptor(VECTOR_ALIGN, (uint64_t)isr_alignment, 
                       IDT_FLAGS_PRESENT | IDT_FLAGS_INTERRUPT);
    idt_set_descriptor(VECTOR_MACHINE_CHECK, (uint64_t)isr_machine_check, 
                       IDT_FLAGS_PRESENT | IDT_FLAGS_INTERRUPT);
    idt_set_descriptor(VECTOR_SIMD, (uint64_t)isr_simd, 
                       IDT_FLAGS_PRESENT | IDT_FLAGS_INTERRUPT);
    
    /* Set up hardware interrupts (IRQs remapped to 32+) */
    /* IRQ0 (timer) - vector 32 */
    /* IRQ1 (keyboard) - vector 33 */
    idt_set_descriptor(33, (uint64_t)isr_keyboard, 
                       IDT_FLAGS_PRESENT | IDT_FLAGS_INTERRUPT);
    
    idt_reg.base = (uint64_t)&idt;
    idt_reg.limit = sizeof(idt) - 1;
}

/* I/O wait - small delay for old hardware */
static inline void io_wait(void) {
    __asm__ __volatile__("outb %%al, $0x80" : : "a"(0));
}

/* Initialize PIC (Programmable Interrupt Controller) */
static void pic_init(void) {
    uint8_t mask1, mask2;
    
    /* Save masks */
    __asm__ __volatile__("inb $0x21, %%al" : "=a"(mask1));
    __asm__ __volatile__("inb $0xA1, %%al" : "=a"(mask2));
    
    /* ICW1: Start initialization sequence (cascade mode) */
    __asm__ __volatile__("outb %%al, $0x20" : : "a"((uint8_t)0x11));
    io_wait();
    __asm__ __volatile__("outb %%al, $0xA0" : : "a"((uint8_t)0x11));
    io_wait();
    
    /* ICW2: Vector offset (master: 32-39, slave: 40-47) */
    __asm__ __volatile__("outb %%al, $0x21" : : "a"((uint8_t)32));
    io_wait();
    __asm__ __volatile__("outb %%al, $0xA1" : : "a"((uint8_t)40));
    io_wait();
    
    /* ICW3: Tell master about slave on IRQ2, tell slave its cascade identity */
    __asm__ __volatile__("outb %%al, $0x21" : : "a"((uint8_t)0x04));
    io_wait();
    __asm__ __volatile__("outb %%al, $0xA1" : : "a"((uint8_t)0x02));
    io_wait();
    
    /* ICW4: 8086/88 mode */
    __asm__ __volatile__("outb %%al, $0x21" : : "a"((uint8_t)0x01));
    io_wait();
    __asm__ __volatile__("outb %%al, $0xA1" : : "a"((uint8_t)0x01));
    io_wait();
    
    /* Mask all IRQs except IRQ1 (keyboard) */
    __asm__ __volatile__("outb %%al, $0x21" : : "a"((uint8_t)0xFD));
    io_wait();
    __asm__ __volatile__("outb %%al, $0xA1" : : "a"((uint8_t)0xFF));
    io_wait();
}

void idt_load(void) {
    __asm__ __volatile__(
        "lidt %0"
        :
        : "m"(idt_reg)
    );
    
    /* Initialize and enable PIC */
    pic_init();
}

void idt_enable_interrupts(void) {
    __asm__ __volatile__("sti");
}
