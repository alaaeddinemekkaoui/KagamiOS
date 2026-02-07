#include "serial.h"

/* I/O port operations */
static inline void outb(unsigned short port, unsigned char val) {
    __asm__ __volatile__("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline unsigned char inb(unsigned short port) {
    unsigned char ret;
    __asm__ __volatile__("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

/* Serial port initialization (COM1 at 0x3F8) */
void serial_init(void) {
    outb(0x3F8 + 1, 0x00);    /* Disable all interrupts */
    outb(0x3F8 + 3, 0x80);    /* Enable DLAB (set baud rate divisor) */
    outb(0x3F8 + 0, 0x03);    /* Set divisor to 3 (lo byte) 38400 baud */
    outb(0x3F8 + 1, 0x00);    /*                  (hi byte) */
    outb(0x3F8 + 3, 0x03);    /* 8 bits, no parity, one stop bit */
    outb(0x3F8 + 2, 0xC7);    /* Enable FIFO, clear them, 14-byte threshold */
    outb(0x3F8 + 4, 0x0B);    /* IRQs enabled, RTS/DSR set */
}

/* Check if transmit buffer is empty */
static int serial_is_transmit_empty(void) {
    return inb(0x3F8 + 5) & 0x20;
}

/* Write single character to serial */
void serial_write_char(char c) {
    while (!serial_is_transmit_empty()) { }
    outb(0x3F8, (unsigned char)c);
}

/* Write string to serial */
void serial_write(const char* s) {
    while (*s) {
        if (*s == '\n') {
            serial_write_char('\r');
        }
        serial_write_char(*s++);
    }
}
