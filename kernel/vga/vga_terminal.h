#ifndef VGA_TERMINAL_H
#define VGA_TERMINAL_H

#include "types.h"
#include "include/framebuffer.h"

/* Terminal state */
typedef struct {
    size_t cursor_row;
    size_t cursor_col;
    uint8_t color;
} VGA_TERMINAL;

/* VGA color indexes (mapped to RGB for framebuffer) */
enum vga_color {
    VGA_COLOR_BLACK = 0x0,
    VGA_COLOR_BLUE = 0x1,
    VGA_COLOR_GREEN = 0x2,
    VGA_COLOR_CYAN = 0x3,
    VGA_COLOR_RED = 0x4,
    VGA_COLOR_MAGENTA = 0x5,
    VGA_COLOR_BROWN = 0x6,
    VGA_COLOR_LIGHT_GREY = 0x7,
    VGA_COLOR_DARK_GREY = 0x8,
    VGA_COLOR_LIGHT_BLUE = 0x9,
    VGA_COLOR_LIGHT_GREEN = 0xA,
    VGA_COLOR_LIGHT_CYAN = 0xB,
    VGA_COLOR_LIGHT_RED = 0xC,
    VGA_COLOR_LIGHT_MAGENTA = 0xD,
    VGA_COLOR_LIGHT_BROWN = 0xE,
    VGA_COLOR_WHITE = 0xF
};

/* Bind framebuffer for terminal output */
void terminal_bind_framebuffer(unsigned int* fb, unsigned int pitch, unsigned int width, unsigned int height);

/* Initialize terminal */
void terminal_init(void);

/* Write a character at cursor position and advance */
void terminal_putchar(char c);

/* Write a string at cursor position */
void terminal_write(const char* str);

/* Clear terminal */
void terminal_clear(void);

/* Set cursor position */
void terminal_set_cursor(size_t row, size_t col);

/* Get cursor position */
void terminal_get_cursor(size_t* row, size_t* col);

/* Set terminal color */
void terminal_set_color(uint8_t color);

/* Scroll terminal up one line */
void terminal_scroll(void);

/* Handle backspace */
void terminal_backspace(void);

#endif /* VGA_TERMINAL_H */
