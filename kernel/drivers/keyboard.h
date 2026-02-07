#ifndef KEYBOARD_H
#define KEYBOARD_H

#include "types.h"

/* Keyboard buffer size */
#define KB_BUFFER_SIZE 128

/* Special keys */
#define KEY_BACKSPACE 0x08
#define KEY_TAB       0x09
#define KEY_ENTER     0x0A
#define KEY_ESC       0x1B

/* Keyboard state */
typedef struct {
    uint8_t buffer[KB_BUFFER_SIZE];
    volatile size_t read_pos;
    volatile size_t write_pos;
    volatile uint8_t shift_pressed;
    volatile uint8_t ctrl_pressed;
    volatile uint8_t alt_pressed;
} KEYBOARD_STATE;

/* Initialize keyboard driver */
void keyboard_init(void);

/* Poll PS/2 controller directly for ENTER key (no interrupts, no buffer) */
unsigned char keyboard_wait_for_enter(void);

/* Process scancode from PS/2 keyboard (called by ISR) */
void keyboard_process_scancode(uint8_t scancode);

/* Check if a key is available */
uint8_t keyboard_has_key(void);

/* Get next key from buffer (blocking) */
uint8_t keyboard_getchar(void);

/* Get next key from buffer (non-blocking, returns 0 if none) */
uint8_t keyboard_getchar_nonblock(void);

#endif /* KEYBOARD_H */
