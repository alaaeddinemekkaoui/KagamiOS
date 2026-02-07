#include "vga.h"

#define VGA_ADDRESS 0xB8000

static inline uint16_t vga_entry(char ch, uint8_t color) {
    return (uint16_t)ch | ((uint16_t)color << 8);
}

void vga_clear(uint8_t color) {
    volatile uint16_t* vga = (volatile uint16_t*)VGA_ADDRESS;
    uint16_t blank = vga_entry(' ', color);
    for (size_t i = 0; i < VGA_WIDTH * VGA_HEIGHT; ++i) {
        vga[i] = blank;
    }
}

void vga_write_at(const char* str, size_t row, size_t col, uint8_t color) {
    volatile uint16_t* vga = (volatile uint16_t*)VGA_ADDRESS;
    size_t idx = row * VGA_WIDTH + col;
    for (size_t i = 0; str[i] != '\0'; ++i) {
        if (col + i >= VGA_WIDTH) {
            break;
        }
        vga[idx + i] = vga_entry(str[i], color);
    }
}
