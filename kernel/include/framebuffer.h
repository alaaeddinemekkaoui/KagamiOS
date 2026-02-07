#ifndef FRAMEBUFFER_H
#define FRAMEBUFFER_H

/* Framebuffer operations */
void fb_putpixel(unsigned int* fb, unsigned int pitch, unsigned int x, unsigned int y, unsigned int color);
void fb_putchar(unsigned int* fb, unsigned int pitch, unsigned int x, unsigned int y, char c, unsigned int color);
void fb_print(unsigned int* fb, unsigned int pitch, unsigned int x, unsigned int y, const char* str, unsigned int color);

/* Scaled versions for larger text */
void fb_putchar_scaled(unsigned int* fb, unsigned int pitch, unsigned int x, unsigned int y, char c, unsigned int color, int scale);
void fb_print_scaled(unsigned int* fb, unsigned int pitch, unsigned int x, unsigned int y, const char* str, unsigned int color, int scale);

#endif /* FRAMEBUFFER_H */
