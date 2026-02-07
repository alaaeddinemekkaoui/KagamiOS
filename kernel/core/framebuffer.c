#include "framebuffer.h"
#include "font.h"

/* Framebuffer pixel write operation */
void fb_putpixel(unsigned int* fb, unsigned int pitch, unsigned int x, unsigned int y, unsigned int color) {
    fb[y * (pitch / 4) + x] = color;
}

/* Draw single character using bitmap font */
void fb_putchar(unsigned int* fb, unsigned int pitch, unsigned int x, unsigned int y, char c, unsigned int color) {
    if (c < 0 || c >= 128) return;
    
    const unsigned char* glyph = font_8x8[(int)c];
    for (int row = 0; row < 8; row++) {
        unsigned char bits = glyph[row];
        for (int col = 0; col < 8; col++) {
            if (bits & (1 << (7 - col))) {
                fb_putpixel(fb, pitch, x + col, y + row, color);
            }
        }
    }
}

/* Print string to framebuffer */
void fb_print(unsigned int* fb, unsigned int pitch, unsigned int x, unsigned int y, const char* str, unsigned int color) {
    unsigned int cur_x = x;
    unsigned int cur_y = y;
    
    while (*str) {
        if (*str == '\n') {
            cur_x = x;
            cur_y += 8;
        } else {
            fb_putchar(fb, pitch, cur_x, cur_y, *str, color);
            cur_x += 8;
        }
        str++;
    }
}

/* Draw scaled character (larger) */
void fb_putchar_scaled(unsigned int* fb, unsigned int pitch, unsigned int x, unsigned int y, char c, unsigned int color, int scale) {
    if (c < 0 || c >= 128) return;
    
    const unsigned char* glyph = font_8x8[(int)c];
    for (int row = 0; row < 8; row++) {
        unsigned char bits = glyph[row];
        for (int col = 0; col < 8; col++) {
            if (bits & (1 << (7 - col))) {
                /* Draw scaled pixel block */
                for (int sy = 0; sy < scale; sy++) {
                    for (int sx = 0; sx < scale; sx++) {
                        fb_putpixel(fb, pitch, x + col * scale + sx, y + row * scale + sy, color);
                    }
                }
            }
        }
    }
}

/* Print scaled string to framebuffer */
void fb_print_scaled(unsigned int* fb, unsigned int pitch, unsigned int x, unsigned int y, const char* str, unsigned int color, int scale) {
    unsigned int cur_x = x;
    unsigned int cur_y = y;
    unsigned int char_width = 8 * scale;
    unsigned int char_height = 8 * scale;
    
    while (*str) {
        if (*str == '\n') {
            cur_x = x;
            cur_y += char_height;
        } else {
            fb_putchar_scaled(fb, pitch, cur_x, cur_y, *str, color, scale);
            cur_x += char_width;
        }
        str++;
    }
}
