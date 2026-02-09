#include "keyboard.h"
#include "klog.h"

static KEYBOARD_STATE kb_state = {
    .read_pos = 0,
    .write_pos = 0,
    .shift_pressed = 0,
    .ctrl_pressed = 0,
    .alt_pressed = 0
};

static inline unsigned char inb(unsigned short port) {
    unsigned char ret;
    __asm__ __volatile__("inb %1, %0" : "=a"(ret) : "d"(port));
    return ret;
}

static inline void outb(unsigned short port, unsigned char value) {
    __asm__ __volatile__("outb %0, %1" : : "a"(value), "d"(port));
}

#define PS2_STATUS_PORT  0x64
#define PS2_DATA_PORT    0x60

#define PS2_STATUS_OUTPUT_BUFFER 0x01
#define PS2_STATUS_INPUT_BUFFER  0x02

static int ps2_wait_input_clear(void) {
    for (int i = 0; i < 100000; i++) {
        if ((inb(PS2_STATUS_PORT) & PS2_STATUS_INPUT_BUFFER) == 0) {
            return 1;
        }
    }
    return 0;
}

static int ps2_wait_output_full(void) {
    for (int i = 0; i < 100000; i++) {
        if (inb(PS2_STATUS_PORT) & PS2_STATUS_OUTPUT_BUFFER) {
            return 1;
        }
    }
    return 0;
}
uint8_t keyboard_has_controller(void) {
    unsigned char status = inb(PS2_STATUS_PORT);

    /* 0xFF is a common sign of a non-existent controller on x86 */
    if (status == 0xFF) {
        return 0;
    }

    return 1;
}

static const char scancode_to_ascii_lower[] = {
    0,    0,   '1',  '2',  '3',  '4',  '5',  '6',  '7',  '8',  '9',  '0',  '-',  '=', '\b', '\t',
    'q',  'w', 'e',  'r',  't',  'y',  'u',  'i',  'o',  'p',  '[',  ']', '\n',    0,  'a',  's',
    'd',  'f', 'g',  'h',  'j',  'k',  'l',  ';', '\'',  '`',    0, '\\',  'z',  'x',  'c',  'v',
    'b',  'n', 'm',  ',',  '.',  '/',    0,  '*',    0,  ' ',    0,    0,    0,    0,    0,    0,
};

static const char scancode_to_ascii_upper[] = {
    0,    0,   '!',  '@',  '#',  '$',  '%',  '^',  '&',  '*',  '(',  ')',  '_',  '+', '\b', '\t',
    'Q',  'W', 'E',  'R',  'T',  'Y',  'U',  'I',  'O',  'P',  '{',  '}', '\n',    0,  'A',  'S',
    'D',  'F', 'G',  'H',  'J',  'K',  'L',  ':',  '"',  '~',    0,  '|',  'Z',  'X',  'C',  'V',
    'B',  'N', 'M',  '<',  '>',  '?',    0,  '*',    0,  ' ',    0,    0,    0,    0,    0,    0,
};

#define SC_LSHIFT_PRESS   0x2A
#define SC_RSHIFT_PRESS   0x36
#define SC_LSHIFT_RELEASE 0xAA
#define SC_RSHIFT_RELEASE 0xB6
#define SC_CTRL_PRESS     0x1D
#define SC_CTRL_RELEASE   0x9D
#define SC_ALT_PRESS      0x38
#define SC_ALT_RELEASE    0xB8
#define SC_CAPSLOCK       0x3A

unsigned char keyboard_wait_for_enter(void) {
    const int timeout_loops = 5000000;
    int loops = 0;

    while (loops < timeout_loops) {
        unsigned char status = inb(PS2_STATUS_PORT);
        if (status & PS2_STATUS_OUTPUT_BUFFER) {
            unsigned char scancode = inb(PS2_DATA_PORT);
            if (scancode == 0x1C) {
                return '\n';
            }
        }
        for (volatile int i = 0; i < 1000; i++);
        loops++;
    }

    return '\n';
}

void keyboard_init(void) {
    kb_state.read_pos = 0;
    kb_state.write_pos = 0;
    kb_state.shift_pressed = 0;
    kb_state.ctrl_pressed = 0;
    kb_state.alt_pressed = 0;

    /* Basic PS/2 controller init: enable port 1 and IRQ1, enable scanning */
    if (!ps2_wait_input_clear()) {
        KERR("PS2: input buffer busy");
    }
    outb(PS2_STATUS_PORT, 0xAD); /* Disable port 1 while configuring */

    /* Flush output buffer */
    if (inb(PS2_STATUS_PORT) & PS2_STATUS_OUTPUT_BUFFER) {
        inb(PS2_DATA_PORT);
    }

    /* Read controller config byte */
    if (!ps2_wait_input_clear()) {
        KERR("PS2: input buffer busy before config read");
    }
    outb(PS2_STATUS_PORT, 0x20);
    if (!ps2_wait_output_full()) {
        KERR("PS2: no config byte");
    }
    unsigned char config = inb(PS2_DATA_PORT);

    /* Enable IRQ1 (bit 0) and ensure port 1 clock is enabled (bit 4 cleared) */
    config |= 0x01;
    config &= (unsigned char)~0x10;

    if (!ps2_wait_input_clear()) {
        KERR("PS2: input buffer busy before config write");
    }
    outb(PS2_STATUS_PORT, 0x60);
    if (!ps2_wait_input_clear()) {
        KERR("PS2: input buffer busy on config write");
    }
    outb(PS2_DATA_PORT, config);

    /* Enable port 1 */
    if (!ps2_wait_input_clear()) {
        KERR("PS2: input buffer busy before enable");
    }
    outb(PS2_STATUS_PORT, 0xAE);

    /* Enable keyboard scanning */
    if (!ps2_wait_input_clear()) {
        KERR("PS2: input buffer busy before scan enable");
    }
    outb(PS2_DATA_PORT, 0xF4);
}

void keyboard_process_scancode(uint8_t scancode) {
    if (scancode == SC_LSHIFT_PRESS || scancode == SC_RSHIFT_PRESS) {
        kb_state.shift_pressed = 1;
        return;
    }
    if (scancode == SC_LSHIFT_RELEASE || scancode == SC_RSHIFT_RELEASE) {
        kb_state.shift_pressed = 0;
        return;
    }
    if (scancode == SC_CTRL_PRESS) {
        kb_state.ctrl_pressed = 1;
        return;
    }
    if (scancode == SC_CTRL_RELEASE) {
        kb_state.ctrl_pressed = 0;
        return;
    }
    if (scancode == SC_ALT_PRESS) {
        kb_state.alt_pressed = 1;
        return;
    }
    if (scancode == SC_ALT_RELEASE) {
        kb_state.alt_pressed = 0;
        return;
    }

    if (scancode & 0x80) {
        return;
    }

    char ascii = 0;
    if (scancode < sizeof(scancode_to_ascii_lower)) {
        if (kb_state.shift_pressed) {
            ascii = scancode_to_ascii_upper[scancode];
        } else {
            ascii = scancode_to_ascii_lower[scancode];
        }
    }

    if (ascii != 0) {
        size_t next_write = (kb_state.write_pos + 1) % KB_BUFFER_SIZE;
        if (next_write != kb_state.read_pos) {
            kb_state.buffer[kb_state.write_pos] = ascii;
            kb_state.write_pos = next_write;
        }
    }
}

uint8_t keyboard_has_key(void) {
    return kb_state.read_pos != kb_state.write_pos;
}

uint8_t keyboard_getchar(void) {
    while (!keyboard_has_key()) {
        __asm__ __volatile__("hlt");
    }

    uint8_t ch = kb_state.buffer[kb_state.read_pos];
    kb_state.read_pos = (kb_state.read_pos + 1) % KB_BUFFER_SIZE;
    return ch;
}

uint8_t keyboard_getchar_nonblock(void) {
    if (!keyboard_has_key()) {
        return 0;
    }

    uint8_t ch = kb_state.buffer[kb_state.read_pos];
    kb_state.read_pos = (kb_state.read_pos + 1) % KB_BUFFER_SIZE;
    return ch;
}
