#ifndef GOP_TERMINAL_H
#define GOP_TERMINAL_H

#include "types.h"
#include "include/framebuffer.h"

/* Terminal state for GOP framebuffer */
typedef struct {
    size_t cursor_row;
    size_t cursor_col;
    uint8_t color;
} GOP_TERMINAL;

/* VGA color palette (mapped to RGB for GOP framebuffer) */
enum gop_color {
    GOP_COLOR_BLACK = 0x0,
    GOP_COLOR_BLUE = 0x1,
    GOP_COLOR_GREEN = 0x2,
    GOP_COLOR_CYAN = 0x3,
    GOP_COLOR_RED = 0x4,
    GOP_COLOR_MAGENTA = 0x5,
    GOP_COLOR_BROWN = 0x6,
    GOP_COLOR_LIGHT_GREY = 0x7,
    GOP_COLOR_DARK_GREY = 0x8,
    GOP_COLOR_LIGHT_BLUE = 0x9,
    GOP_COLOR_LIGHT_GREEN = 0xA,
    GOP_COLOR_LIGHT_CYAN = 0xB,
    GOP_COLOR_LIGHT_RED = 0xC,
    GOP_COLOR_LIGHT_MAGENTA = 0xD,
    GOP_COLOR_LIGHT_BROWN = 0xE,
    GOP_COLOR_WHITE = 0xF
};

/* Initialize GOP terminal with framebuffer info */
void gop_terminal_init(unsigned int* fb, unsigned int pitch, unsigned int width, unsigned int height);

/* Write a character at cursor position and advance */
void gop_terminal_putchar(char c);

/* Write a string at cursor position */
void gop_terminal_write(const char* str);

/* Clear terminal */
void gop_terminal_clear(void);

/* Set cursor position */
void gop_terminal_set_cursor(size_t row, size_t col);

/* Get cursor position */
void gop_terminal_get_cursor(size_t* row, size_t* col);

/* Set terminal color */
void gop_terminal_set_color(uint8_t color);

/* Scroll terminal up one line */
void gop_terminal_scroll(void);

/* Handle backspace */
void gop_terminal_backspace(void);

#endif /* GOP_TERMINAL_H */
