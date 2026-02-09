#include "gop_terminal.h"
#include "include/serial.h"

#define FB_CHAR_W 8
#define FB_CHAR_H 8

static GOP_TERMINAL terminal = {
    .cursor_row = 0,
    .cursor_col = 0,
    .color = GOP_COLOR_WHITE
};

static unsigned int* fb_ptr = 0;
static unsigned int fb_pitch = 0;
static unsigned int fb_width = 0;
static unsigned int fb_height = 0;
static unsigned int fb_cols = 0;
static unsigned int fb_rows = 0;

/* GOP color palette (RGB) corresponding to VGA colors */
static const unsigned int gop_palette[16] = {
    0x000000, /* black */
    0x0000AA, /* blue */
    0x00AA00, /* green */
    0x00AAAA, /* cyan */
    0xAA0000, /* red */
    0xAA00AA, /* magenta */
    0xAA5500, /* brown */
    0xAAAAAA, /* light grey */
    0x555555, /* dark grey */
    0x5555FF, /* light blue */
    0x55FF55, /* light green */
    0x55FFFF, /* light cyan */
    0xFF5555, /* light red */
    0xFF55FF, /* light magenta */
    0xFFFF55, /* light brown */
    0xFFFFFF  /* white */
};

static inline unsigned int gop_color_lookup(uint8_t color) {
    return gop_palette[color & 0x0F];
}

void gop_terminal_init(unsigned int* fb, unsigned int pitch, unsigned int width, unsigned int height) {
    fb_ptr = fb;
    fb_pitch = pitch;
    fb_width = width;
    fb_height = height;
    fb_cols = width / FB_CHAR_W;
    fb_rows = height / FB_CHAR_H;
    
    terminal.cursor_row = 0;
    terminal.cursor_col = 0;
    terminal.color = GOP_COLOR_WHITE;
    
    /* Log initialization */
    char buf[80];
    int pos = 0;
    const char *msg = "GOP Terminal: Initialized ";
    while (*msg) buf[pos++] = *msg++;
    /* width */
    if (fb_width >= 1000) buf[pos++] = '0' + (fb_width / 1000);
    if (fb_width >= 100) buf[pos++] = '0' + ((fb_width / 100) % 10);
    if (fb_width >= 10) buf[pos++] = '0' + ((fb_width / 10) % 10);
    buf[pos++] = '0' + (fb_width % 10);
    msg = "x";
    while (*msg) buf[pos++] = *msg++;
    /* height */
    if (fb_height >= 1000) buf[pos++] = '0' + (fb_height / 1000);
    if (fb_height >= 100) buf[pos++] = '0' + ((fb_height / 100) % 10);
    if (fb_height >= 10) buf[pos++] = '0' + ((fb_height / 10) % 10);
    buf[pos++] = '0' + (fb_height % 10);
    msg = " cols=";
    while (*msg) buf[pos++] = *msg++;
    /* cols */
    if (fb_cols >= 10) buf[pos++] = '0' + (fb_cols / 10);
    buf[pos++] = '0' + (fb_cols % 10);
    msg = " rows=";
    while (*msg) buf[pos++] = *msg++;
    /* rows */
    if (fb_rows >= 10) buf[pos++] = '0' + (fb_rows / 10);
    buf[pos++] = '0' + (fb_rows % 10);
    buf[pos++] = '\n';
    buf[pos] = 0;
    serial_write(buf);
}

void gop_terminal_clear(void) {
    if (!fb_ptr || fb_width == 0 || fb_height == 0) {
        return;
    }

    unsigned int stride = fb_pitch / 4;
    for (unsigned int y = 0; y < fb_height; y++) {
        unsigned int* row = fb_ptr + y * stride;
        for (unsigned int x = 0; x < fb_width; x++) {
            row[x] = 0x000000;
        }
    }

    terminal.cursor_row = 0;
    terminal.cursor_col = 0;
}

void gop_terminal_set_cursor(size_t row, size_t col) {
    if (row < fb_rows) {
        terminal.cursor_row = row;
    }
    if (col < fb_cols) {
        terminal.cursor_col = col;
    }
}

void gop_terminal_get_cursor(size_t* row, size_t* col) {
    *row = terminal.cursor_row;
    *col = terminal.cursor_col;
}

void gop_terminal_set_color(uint8_t color) {
    terminal.color = color;
}

void gop_terminal_scroll(void) {
    if (!fb_ptr || fb_rows == 0) {
        return;
    }

    unsigned int stride = fb_pitch / 4;
    unsigned int scroll_px = FB_CHAR_H;

    /* Scroll framebuffer up by one text line */
    for (unsigned int y = scroll_px; y < fb_height; y++) {
        unsigned int* dst = fb_ptr + (y - scroll_px) * stride;
        unsigned int* src = fb_ptr + y * stride;
        for (unsigned int x = 0; x < fb_width; x++) {
            dst[x] = src[x];
        }
    }

    /* Clear bottom line */
    for (unsigned int y = fb_height - scroll_px; y < fb_height; y++) {
        unsigned int* row = fb_ptr + y * stride;
        for (unsigned int x = 0; x < fb_width; x++) {
            row[x] = 0x000000;
        }
    }

    terminal.cursor_row = fb_rows - 1;
    terminal.cursor_col = 0;
}

void gop_terminal_putchar(char c) {
    if (!fb_ptr || fb_cols == 0 || fb_rows == 0) {
        serial_write("GOP Terminal: putchar called but framebuffer not initialized\n");
        return;
    }

    if (c == '\n') {
        terminal.cursor_col = 0;
        terminal.cursor_row++;
        if (terminal.cursor_row >= fb_rows) {
            gop_terminal_scroll();
        }
        return;
    }

    if (c == '\b') {
        gop_terminal_backspace();
        return;
    }

    if (c == '\t') {
        for (int i = 0; i < 4; i++) {
            gop_terminal_putchar(' ');
        }
        return;
    }

    unsigned int x = (unsigned int)terminal.cursor_col * FB_CHAR_W;
    unsigned int y = (unsigned int)terminal.cursor_row * FB_CHAR_H;
    
    fb_putchar(fb_ptr, fb_pitch, x, y, c, gop_color_lookup(terminal.color));

    terminal.cursor_col++;
    if (terminal.cursor_col >= fb_cols) {
        terminal.cursor_col = 0;
        terminal.cursor_row++;
        if (terminal.cursor_row >= fb_rows) {
            gop_terminal_scroll();
        }
    }
}

void gop_terminal_write(const char* str) {
    while (*str) {
        gop_terminal_putchar(*str++);
    }
}

void gop_terminal_backspace(void) {
    if (!fb_ptr || terminal.cursor_col == 0) {
        return;
    }

    terminal.cursor_col--;
    unsigned int x = (unsigned int)terminal.cursor_col * FB_CHAR_W;
    unsigned int y = (unsigned int)terminal.cursor_row * FB_CHAR_H;
    fb_putchar(fb_ptr, fb_pitch, x, y, ' ', 0x000000);
}
