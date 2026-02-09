#ifndef KEYBOARD_H
#define KEYBOARD_H

#include "types.h"

#define KB_BUFFER_SIZE 128

#define KEY_BACKSPACE 0x08
#define KEY_TAB       0x09
#define KEY_ENTER     0x0A
#define KEY_ESC       0x1B

typedef struct {
    uint8_t buffer[KB_BUFFER_SIZE];
    volatile size_t read_pos;
    volatile size_t write_pos;
    volatile uint8_t shift_pressed;
    volatile uint8_t ctrl_pressed;
    volatile uint8_t alt_pressed;
} KEYBOARD_STATE;

void keyboard_init(void);
unsigned char keyboard_wait_for_enter(void);
uint8_t keyboard_has_controller(void);
void keyboard_process_scancode(uint8_t scancode);
uint8_t keyboard_has_key(void);
uint8_t keyboard_getchar(void);
uint8_t keyboard_getchar_nonblock(void);

#endif
