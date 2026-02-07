#ifndef VGA_TERMINAL_H
#define VGA_TERMINAL_H

#include "types.h"
#include "vga.h"

/* Terminal state */
typedef struct {
    size_t cursor_row;
    size_t cursor_col;
    uint8_t color;
} VGA_TERMINAL;

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
