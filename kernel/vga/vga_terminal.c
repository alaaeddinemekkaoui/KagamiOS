#include "vga_terminal.h"

static VGA_TERMINAL terminal = {
    .cursor_row = 0,
    .cursor_col = 0,
    .color = (VGA_COLOR_BLACK << 4) | VGA_COLOR_WHITE
};

/* VGA memory */
static volatile uint16_t* vga_buffer = (uint16_t*)0xB8000;

static inline uint16_t vga_entry(unsigned char uc, uint8_t color) {
    return (uint16_t)uc | ((uint16_t)color << 8);
}

void terminal_init(void) {
    terminal.cursor_row = 0;
    terminal.cursor_col = 0;
    terminal.color = (VGA_COLOR_BLACK << 4) | VGA_COLOR_WHITE;
}

void terminal_clear(void) {
    for (size_t y = 0; y < VGA_HEIGHT; y++) {
        for (size_t x = 0; x < VGA_WIDTH; x++) {
            const size_t index = y * VGA_WIDTH + x;
            vga_buffer[index] = vga_entry(' ', terminal.color);
        }
    }
    terminal.cursor_row = 0;
    terminal.cursor_col = 0;
}

void terminal_set_cursor(size_t row, size_t col) {
    terminal.cursor_row = row;
    terminal.cursor_col = col;
}

void terminal_get_cursor(size_t* row, size_t* col) {
    *row = terminal.cursor_row;
    *col = terminal.cursor_col;
}

void terminal_set_color(uint8_t color) {
    terminal.color = color;
}

void terminal_scroll(void) {
    /* Move all lines up by one */
    for (size_t y = 0; y < VGA_HEIGHT - 1; y++) {
        for (size_t x = 0; x < VGA_WIDTH; x++) {
            vga_buffer[y * VGA_WIDTH + x] = vga_buffer[(y + 1) * VGA_WIDTH + x];
        }
    }
    
    /* Clear bottom line */
    for (size_t x = 0; x < VGA_WIDTH; x++) {
        vga_buffer[(VGA_HEIGHT - 1) * VGA_WIDTH + x] = vga_entry(' ', terminal.color);
    }
    
    terminal.cursor_row = VGA_HEIGHT - 1;
    terminal.cursor_col = 0;
}

void terminal_putchar(char c) {
    if (c == '\n') {
        terminal.cursor_col = 0;
        terminal.cursor_row++;
        if (terminal.cursor_row >= VGA_HEIGHT) {
            terminal_scroll();
        }
        return;
    }
    
    if (c == '\b') {
        terminal_backspace();
        return;
    }
    
    if (c == '\t') {
        /* Tab = 4 spaces */
        for (int i = 0; i < 4; i++) {
            terminal_putchar(' ');
        }
        return;
    }
    
    const size_t index = terminal.cursor_row * VGA_WIDTH + terminal.cursor_col;
    vga_buffer[index] = vga_entry(c, terminal.color);
    
    terminal.cursor_col++;
    if (terminal.cursor_col >= VGA_WIDTH) {
        terminal.cursor_col = 0;
        terminal.cursor_row++;
        if (terminal.cursor_row >= VGA_HEIGHT) {
            terminal_scroll();
        }
    }
}

void terminal_write(const char* str) {
    while (*str) {
        terminal_putchar(*str++);
    }
}

void terminal_backspace(void) {
    if (terminal.cursor_col > 0) {
        terminal.cursor_col--;
        const size_t index = terminal.cursor_row * VGA_WIDTH + terminal.cursor_col;
        vga_buffer[index] = vga_entry(' ', terminal.color);
    }
}
