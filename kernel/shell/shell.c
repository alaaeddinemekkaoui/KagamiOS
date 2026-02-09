#include "shell.h"
#include "include/framebuffer.h"
#include "include/serial.h"
#include "include/ascii_art.h"
#include "commands_manual.h"
#include "drivers/input/keyboard.h"
#include "boot_info.h"
#include "core/heap.h"
#include "fs/vfs.h"
#include "drivers/storage/block.h"
#include "drivers/storage/partition.h"
#include "drivers/bus/pci.h"
#include "net/net.h"

/* Simple macros for memory access */
#define inb(port) ({ \
    unsigned char ret; \
    __asm__ __volatile__("inb %1, %0" : "=a"(ret) : "d"(port)); \
    ret; \
})

/* PS/2 Controller port definitions */
#define PS2_STATUS_PORT  0x64
#define PS2_DATA_PORT    0x60
#define PS2_STATUS_OUTPUT_BUFFER 0x01

/* Scancode to ASCII mapping (US QWERTY, for printable characters) */
static const char scancode_ascii[] = {
    0, 0, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', 0, 0,
    'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n', 0, 'a', 's',
    'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`', 0, '\\', 'z', 'x', 'c', 'v',
    'b', 'n', 'm', ',', '.', '/', 0, 0, 0, ' ', 0
};

/* Special keys */
#define SC_BACKSPACE 0x0E
#define SC_TAB       0x0F
#define SC_LSHIFT    0x2A
#define SC_RSHIFT    0x36
#define SC_CTRL      0x1D
#define SC_ESC       0x01

/* User accounts system */
typedef struct {
    char username[32];
    char password[32];
} User;

static User users[] = {
    {"root", "admin123"},
    {"", ""}
};

#define MAX_USERS 10
static int user_count = 1;
static char current_user[32] = "root";
static char current_directory[64] = "/home/root";  /* Current directory */

/* Virtual file system - files and folders */
#define MAX_FILE_CONTENT 4096
typedef struct {
    char name[32];
    char content[MAX_FILE_CONTENT];
    int size;
    int is_folder;  /* 1 = folder, 0 = file */
    char parent[64]; /* Parent directory path */
} VirtualFile;

#define INITIAL_FILE_CAPACITY 32

static const VirtualFile initial_files[] = {
    {"home", "", 0, 1, "/"},
    {"root", "", 0, 1, "/home"},
    {"readme.txt", "Welcome to Kagami OS! A magical realm of code.\nType 'ls' to explore.", 65, 0, "/home/root"},
    {"welcome.txt", "You have entered the Realm of Kagami.\nMay your code be swift and bug-free.", 70, 0, "/home/root"},
    {"spellbook.txt", "Available Spells:\n- help: Reveal all incantations\n- logo: Display realm emblem", 82, 0, "/home/root"},
    {"COMMANDS.txt", "", 0, 0, "/home/root"},
    {"documents", "", 0, 1, "/home/root"},
    {"secret.txt", "The wizard guardian of this realm welcomes you!", 47, 0, "/home/root/documents"}
};

static VirtualFile* file_system = 0;
static int file_count = 0;
static int file_capacity = 0;
static int fs_initialized = 0;

static void build_full_path(const char *name, char *out, int max_len) {
    if (!name || !out || max_len <= 0) {
        return;
    }

    if (name[0] == '/') {
        int i = 0;
        while (name[i] && i < max_len - 1) {
            out[i] = name[i];
            i++;
        }
        out[i] = 0;
        return;
    }

    int pos = 0;
    if (current_directory[0] == 0) {
        out[pos++] = '/';
    } else {
        while (current_directory[pos] && pos < max_len - 1) {
            out[pos] = current_directory[pos];
            pos++;
        }
    }

    if (pos == 0 || out[pos - 1] != '/') {
        if (pos < max_len - 1) {
            out[pos++] = '/';
        }
    }

    int i = 0;
    while (name[i] && pos < max_len - 1) {
        out[pos++] = name[i++];
    }
    out[pos] = 0;
}

static char hex_digit(uint8_t v) {
    return (v < 10) ? (char)('0' + v) : (char)('A' + (v - 10));
}

static void append_hex(char *buf, int *pos, uint32_t value, int digits) {
    for (int i = digits - 1; i >= 0; i--) {
        uint8_t nibble = (value >> (i * 4)) & 0xF;
        buf[(*pos)++] = hex_digit(nibble);
    }
}

static void append_str(char *buf, int *pos, const char *s) {
    while (*s) {
        buf[(*pos)++] = *s++;
    }
}

static int str_len(const char* s) {
    int n = 0;
    while (s && s[n]) n++;
    return n;
}

static void fs_load_manual(void) {
    int manual_idx = -1;
    for (int i = 0; i < file_count; i++) {
        int match = 1;
        const char* name = file_system[i].name;
        const char* target = "COMMANDS.txt";
        for (int j = 0; j < 32 && (name[j] || target[j]); j++) {
            if (name[j] != target[j]) {
                match = 0;
                break;
            }
            if (name[j] == 0 && target[j] == 0) {
                break;
            }
        }
        if (match) {
            manual_idx = i;
            break;
        }
    }

    if (manual_idx < 0) {
        if (file_count + 1 > file_capacity) {
            return;
        }
        manual_idx = file_count++;
        file_system[manual_idx].is_folder = 0;
        file_system[manual_idx].size = 0;
        file_system[manual_idx].name[0] = 0;
        file_system[manual_idx].parent[0] = 0;

        const char* name = "COMMANDS.txt";
        int n = 0;
        while (name[n] && n < 31) {
            file_system[manual_idx].name[n] = name[n];
            n++;
        }
        file_system[manual_idx].name[n] = 0;

        const char* parent = "/home/root";
        n = 0;
        while (parent[n] && n < 63) {
            file_system[manual_idx].parent[n] = parent[n];
            n++;
        }
        file_system[manual_idx].parent[n] = 0;
    }

    int idx = 0;
    while (commands_manual[idx] && idx < MAX_FILE_CONTENT - 1) {
        file_system[manual_idx].content[idx] = commands_manual[idx];
        idx++;
    }
    file_system[manual_idx].content[idx] = 0;
    file_system[manual_idx].size = idx;
}

static VirtualFile* fs_find_file_by_name(const char* name) {
    for (int i = 0; i < file_count; i++) {
        int match = 1;
        for (int j = 0; j < 32 && (name[j] || file_system[i].name[j]); j++) {
            if (name[j] != file_system[i].name[j]) {
                match = 0;
                break;
            }
            if (name[j] == 0 && file_system[i].name[j] == 0) {
                break;
            }
        }
        if (match) {
            return &file_system[i];
        }
    }
    return 0;
}

static void fs_init(void) {
    if (fs_initialized) {
        return;
    }

    int initial_count = (int)(sizeof(initial_files) / sizeof(initial_files[0]));
    int capacity = INITIAL_FILE_CAPACITY;
    if (capacity < initial_count) {
        capacity = initial_count;
    }

    file_system = (VirtualFile*)calloc((size_t)capacity, sizeof(VirtualFile));
    if (!file_system) {
        /* Fallback to static data if heap is unavailable */
        file_system = (VirtualFile*)initial_files;
        file_count = initial_count;
        file_capacity = initial_count;
        fs_initialized = 1;
        return;
    }

    for (int i = 0; i < initial_count; i++) {
        file_system[i] = initial_files[i];
    }

    file_count = initial_count;
    file_capacity = capacity;
    for (int i = 0; i < file_count; i++) {
        if (!file_system[i].is_folder) {
            file_system[i].size = str_len(file_system[i].content);
        }
    }
    fs_load_manual();
    fs_initialized = 1;
}

static int fs_ensure_capacity(int additional) {
    if (!fs_initialized) {
        fs_init();
    }

    if (file_count + additional <= file_capacity) {
        return 1;
    }

    int new_capacity = file_capacity > 0 ? file_capacity : INITIAL_FILE_CAPACITY;
    while (new_capacity < file_count + additional) {
        new_capacity *= 2;
    }

    VirtualFile* new_fs = (VirtualFile*)calloc((size_t)new_capacity, sizeof(VirtualFile));
    if (!new_fs) {
        return 0;
    }

    for (int i = 0; i < file_count; i++) {
        new_fs[i] = file_system[i];
    }

    file_system = new_fs;
    file_capacity = new_capacity;
    return 1;
}

/* Shell state */
static struct {
    char buffer[256];
    int pos;
    int shift_pressed;
    unsigned int cursor_x;
    unsigned int cursor_y;
    unsigned int line_height;
    unsigned int scroll_offset;  /* Lines scrolled up */
} shell_state = {0};

/* Poll PS/2 keyboard for any available scancode */
static unsigned char poll_keyboard(void) {
    unsigned char status = inb(PS2_STATUS_PORT);
    if (status & PS2_STATUS_OUTPUT_BUFFER) {
        return inb(PS2_DATA_PORT);
    }
    return 0;
}

/* Convert scancode to ASCII character */
static char scancode_to_char(unsigned char scancode, int shift) {
    if (scancode >= sizeof(scancode_ascii) / sizeof(scancode_ascii[0])) {
        return 0;
    }
    
    char c = scancode_ascii[scancode];
    
    if (shift && c >= 'a' && c <= 'z') {
        return c - 32;
    }
    
    /* Shift special characters */
    if (shift) {
        switch (c) {
            case '1': return '!';
            case '2': return '@';
            case '3': return '#';
            case '4': return '$';
            case '5': return '%';
            case '6': return '^';
            case '7': return '&';
            case '8': return '*';
            case '9': return '(';
            case '0': return ')';
            case '-': return '_';
            case '=': return '+';
            case '[': return '{';
            case ']': return '}';
            case ';': return ':';
            case '\'': return '"';
            case ',': return '<';
            case '.': return '>';
            case '/': return '?';
            case '`': return '~';
            case '\\': return '|';
        }
    }
    
    return c;
}

/* Clear a rectangular region on framebuffer */
static void fb_clear_rect(unsigned int* fb, unsigned int pitch, unsigned int width,
                          unsigned int x, unsigned int y, unsigned int w, unsigned int h,
                          unsigned int color) {
    for (unsigned int row = 0; row < h; row++) {
        for (unsigned int col = 0; col < w; col++) {
            if (x + col < width) {
                unsigned int* pixel = fb + (y + row) * (pitch / 4) + (x + col);
                *pixel = color;
            }
        }
    }
}

/* Get single character from keyboard via polling */
static char get_keyboard_char(void) {
    return (char)keyboard_getchar();
}

/* Render current input line to framebuffer */
static void render_input(unsigned int* fb, unsigned int pitch, unsigned int width,
                         const char* prompt) {
    unsigned int x = shell_state.cursor_x;
    unsigned int y = shell_state.cursor_y;
    
    /* Clear the input line area with extra space */
    fb_clear_rect(fb, pitch, width, x, y, width - x - 10, shell_state.line_height + 5, 0x000000);
    
    /* Render prompt */
    fb_print(fb, pitch, x, y, prompt, 0x0088FF00);
    
    /* Calculate position after prompt (dynamic prompt length) */
    int prompt_len = 0;
    const char* p = prompt;
    while (*p++) prompt_len++;
    unsigned int buf_x = x + (prompt_len * 8) + 5;
    
    /* Render input buffer */
    fb_print(fb, pitch, buf_x, y, shell_state.buffer, 0x00FFFFFF);
    
    /* Render cursor with better positioning */
    unsigned int cursor_pos = buf_x + (shell_state.pos * 8);
    fb_putchar(fb, pitch, cursor_pos, y, '_', 0x00FFFF);
}

/* Get current directory prompt */
static void get_dir_prompt(char* prompt_buf) {
    const char* prefix = "kagami:";
    char* ptr = prompt_buf;
    
    /* Add prefix */
    while (*prefix) {
        *ptr++ = *prefix++;
    }
    
    /* Add current directory (abbreviated if in home) */
    const char* dir = current_directory;
    
    /* Check if in user's home directory */
    char home_path[64] = "/home/";
    char* hp = home_path + 6;
    const char* user = current_user;
    while (*user) *hp++ = *user++;
    *hp = 0;
    
    /* Compare paths */
    const char* h = home_path;
    const char* d = dir;
    while (*h && *d && *h == *d) {
        h++;
        d++;
    }
    
    if (*h == 0 && (*d == 0 || *d == '/')) {
        /* We're in home or subdirectory */
        *ptr++ = '~';
        while (*d) {
            *ptr++ = *d++;
        }
    } else {
        /* Show full path */
        while (*dir) {
            *ptr++ = *dir++
;
        }
    }
    
    /* Add prompt suffix */
    *ptr++ = '>';
    *ptr++ = ' ';
    *ptr = 0;
}

/* Clear full framebuffer and redraw minimal shell header */
static void shell_clear_to_header(unsigned int* fb, unsigned int pitch, unsigned int width, unsigned int height) {
    unsigned int* fb_start = fb;
    for (unsigned int i = 0; i < (width * height); i++) {
        fb_start[i] = 0x000000;
    }

    fb_print(fb, pitch, 20, 10, "KAGAMI OS - Type 'logo' for info", 0x0088FF88);
    fb_print(fb, pitch, 20, 30, "=============================================", 0x0055AA55);
    fb_print(fb, pitch, 20, 50, "[Screen cleared - Ready for new incantations]", 0x00AAAA00);
    fb_print(fb, pitch, 20, 70, "Current path: ", 0x00888888);
    fb_print(fb, pitch, 20 + (14 * 8), 70, current_directory, 0x0088FFFF);
}

/* Simple text editor/viewer for files */
typedef struct {
    char* buffer;
    int length;
    int cursor;
    int scroll_line;
    int insert_mode;
    int dirty;
    char status[64];
} TextEditor;

static void editor_set_status(TextEditor* ed, const char* msg) {
    int i = 0;
    while (msg[i] && i < 63) {
        ed->status[i] = msg[i];
        i++;
    }
    ed->status[i] = 0;
}

static void editor_get_cursor_line_col(const char* buf, int cursor, int* out_line, int* out_col) {
    int line = 0;
    int col = 0;
    for (int i = 0; i < cursor; i++) {
        if (buf[i] == '\n') {
            line++;
            col = 0;
        } else {
            col++;
        }
    }
    *out_line = line;
    *out_col = col;
}

static int editor_line_start(const char* buf, int cursor) {
    int i = cursor;
    while (i > 0 && buf[i - 1] != '\n') {
        i--;
    }
    return i;
}

static int editor_line_end(const char* buf, int length, int cursor) {
    int i = cursor;
    while (i < length && buf[i] != '\n') {
        i++;
    }
    return i;
}

static void editor_move_left(TextEditor* ed) {
    if (ed->cursor > 0) ed->cursor--;
}

static void editor_move_right(TextEditor* ed) {
    if (ed->cursor < ed->length) ed->cursor++;
}

static void editor_move_up(TextEditor* ed) {
    int line_start = editor_line_start(ed->buffer, ed->cursor);
    if (line_start == 0) return;
    int col = ed->cursor - line_start;
    int prev_end = line_start - 1;
    int prev_start = prev_end;
    while (prev_start > 0 && ed->buffer[prev_start - 1] != '\n') {
        prev_start--;
    }
    int prev_len = prev_end - prev_start;
    ed->cursor = prev_start + (col < prev_len ? col : prev_len);
}

static void editor_move_down(TextEditor* ed) {
    int line_end = editor_line_end(ed->buffer, ed->length, ed->cursor);
    if (line_end >= ed->length) return;
    int line_start = editor_line_start(ed->buffer, ed->cursor);
    int col = ed->cursor - line_start;
    int next_start = line_end + 1;
    int next_end = editor_line_end(ed->buffer, ed->length, next_start);
    int next_len = next_end - next_start;
    ed->cursor = next_start + (col < next_len ? col : next_len);
}

static void editor_insert_char(TextEditor* ed, char c) {
    if (ed->length >= MAX_FILE_CONTENT - 1) {
        editor_set_status(ed, "Buffer full");
        return;
    }
    for (int i = ed->length; i > ed->cursor; i--) {
        ed->buffer[i] = ed->buffer[i - 1];
    }
    ed->buffer[ed->cursor] = c;
    ed->length++;
    ed->cursor++;
    ed->buffer[ed->length] = 0;
    ed->dirty = 1;
}

static void editor_backspace(TextEditor* ed) {
    if (ed->cursor <= 0) return;
    for (int i = ed->cursor - 1; i < ed->length; i++) {
        ed->buffer[i] = ed->buffer[i + 1];
    }
    ed->cursor--;
    ed->length--;
    ed->dirty = 1;
}

static void editor_render(unsigned int* fb, unsigned int pitch, unsigned int width, unsigned int height,
                          const char* filename, TextEditor* ed) {
    unsigned int* fb_start = fb;
    for (unsigned int i = 0; i < (width * height); i++) {
        fb_start[i] = 0x000000;
    }

    unsigned int top_y = 12;
    unsigned int content_y = 50;
    unsigned int footer_y = height - 22;
    unsigned int left_x = 20;
    unsigned int line_height = 16;
    unsigned int max_cols = (width - left_x - 20) / 8;
    unsigned int visible_lines = (footer_y - content_y) / line_height;

    char title[80];
    int t = 0;
    const char* head = "KAGAMI EDITOR - ";
    while (head[t]) {
        title[t] = head[t];
        t++;
    }
    int f = 0;
    while (filename[f] && t < 78) {
        title[t++] = filename[f++];
    }
    title[t] = 0;

    fb_print(fb, pitch, 20, top_y, title, 0x00FFFF00);
    fb_print(fb, pitch, 20, top_y + 18, ed->insert_mode ? "MODE: INSERT" : "MODE: NORMAL", 0x0088FFAA);

    int cursor_line = 0;
    int cursor_col = 0;
    editor_get_cursor_line_col(ed->buffer, ed->cursor, &cursor_line, &cursor_col);

    if (cursor_line < ed->scroll_line) {
        ed->scroll_line = cursor_line;
    } else if (cursor_line >= (int)(ed->scroll_line + visible_lines)) {
        ed->scroll_line = cursor_line - (int)visible_lines + 1;
    }
    if (ed->scroll_line < 0) ed->scroll_line = 0;

    int line = 0;
    int col = 0;
    unsigned int y = content_y;
    for (int i = 0; i < ed->length; i++) {
        char c = ed->buffer[i];
        if (c == '\n') {
            if (line >= ed->scroll_line && line < (int)(ed->scroll_line + visible_lines)) {
                y += line_height;
            }
            line++;
            col = 0;
            continue;
        }

        if (line >= ed->scroll_line && line < (int)(ed->scroll_line + visible_lines)) {
            if ((unsigned int)col < max_cols) {
                fb_putchar(fb, pitch, left_x + (unsigned int)col * 8, y, c, 0x00FFFFFF);
            }
        }
        col++;
    }

    if (cursor_line >= ed->scroll_line && cursor_line < (int)(ed->scroll_line + visible_lines)) {
        unsigned int cx = left_x + (unsigned int)cursor_col * 8;
        unsigned int cy = content_y + (unsigned int)(cursor_line - ed->scroll_line) * line_height;
        fb_putchar(fb, pitch, cx, cy, ed->insert_mode ? '_' : '#', 0x00FFAA00);
    }

    fb_print(fb, pitch, 20, footer_y, "Ctrl+S Save  Ctrl+Q Quit  i Insert  ESC Normal  j/k Scroll", 0x0088FF88);
    if (ed->status[0]) {
        fb_print(fb, pitch, 20, footer_y - 14, ed->status, 0x00AAAAFF);
    }
}

static unsigned char editor_get_scancode(int* shift_pressed, int* ctrl_pressed) {
    unsigned char scancode;
    while (1) {
        scancode = poll_keyboard();
        if (scancode == 0) {
            for (volatile int i = 0; i < 1000; i++);
            continue;
        }

        if (scancode == SC_LSHIFT || scancode == SC_RSHIFT) {
            *shift_pressed = 1;
            continue;
        }
        if (scancode == 0xAA || scancode == 0xB6) {
            *shift_pressed = 0;
            continue;
        }
        if (scancode == SC_CTRL) {
            *ctrl_pressed = 1;
            continue;
        }
        if (scancode == 0x9D) {
            *ctrl_pressed = 0;
            continue;
        }
        if (scancode & 0x80) {
            continue;
        }
        return scancode;
    }
}

static void open_text_editor(unsigned int* fb, unsigned int pitch, unsigned int width, unsigned int height,
                             VirtualFile* file) {
    TextEditor ed;
    ed.buffer = file->content;
    ed.length = str_len(file->content);
    ed.cursor = ed.length;
    ed.scroll_line = 0;
    ed.insert_mode = 0;
    ed.dirty = 0;
    ed.status[0] = 0;

    int shift_pressed = 0;
    int ctrl_pressed = 0;

    for (;;) {
        editor_render(fb, pitch, width, height, file->name, &ed);
        unsigned char scancode = editor_get_scancode(&shift_pressed, &ctrl_pressed);

        if (scancode == SC_ESC) {
            ed.insert_mode = 0;
            editor_set_status(&ed, "Normal mode");
            continue;
        }

        if (scancode == SC_BACKSPACE) {
            if (ed.insert_mode) {
                editor_backspace(&ed);
            }
            continue;
        }

        if (scancode == 0x1C) {
            if (ed.insert_mode) {
                editor_insert_char(&ed, '\n');
            }
            continue;
        }

        char c = scancode_to_char(scancode, shift_pressed);
        if (c == 0) {
            continue;
        }

        if (ctrl_pressed && (c == 's' || c == 'S')) {
            file->size = ed.length;
            editor_set_status(&ed, ed.dirty ? "Saved" : "No changes");
            ed.dirty = 0;
            continue;
        }

        if (ctrl_pressed && (c == 'q' || c == 'Q')) {
            break;
        }

        if (!ed.insert_mode) {
            if (c == 'i') {
                ed.insert_mode = 1;
                editor_set_status(&ed, "Insert mode");
            } else if (c == 'h') {
                editor_move_left(&ed);
            } else if (c == 'l') {
                editor_move_right(&ed);
            } else if (c == 'k') {
                editor_move_up(&ed);
            } else if (c == 'j') {
                editor_move_down(&ed);
            } else if (c == 'q') {
                break;
            }
            continue;
        }

        if (ed.insert_mode) {
            editor_insert_char(&ed, c);
        }
    }

    file->size = ed.length;
}
/* Execute shell command and return output to display */
static void execute_command(unsigned int* fb, unsigned int pitch, unsigned int width, unsigned int height) {
    char* cmd = shell_state.buffer;
    
    /* Skip leading spaces */
    while (*cmd == ' ') cmd++;
    
    /* Check command */
    if (cmd[0] == 0) {
        return;
    }
    
    /* Move output area down with spacing */
    shell_state.cursor_y += shell_state.line_height + 10;
    
    /* === HELP COMMAND === */
    if (cmd[0] == 'h' && cmd[1] == 'e' && cmd[2] == 'l' && cmd[3] == 'p') {
        char* arg = cmd + 4;
        while (*arg == ' ') arg++;
        if ((arg[0] == '-' && arg[1] == 'm') ||
            (arg[0] == '-' && arg[1] == '-' && arg[2] == 'm' && arg[3] == 'a' && arg[4] == 'n' && arg[5] == 'u' && arg[6] == 'a' && arg[7] == 'l')) {
            VirtualFile* manual = fs_find_file_by_name("COMMANDS.txt");
            if (manual && !manual->is_folder) {
                open_text_editor(fb, pitch, width, height, manual);
                shell_clear_to_header(fb, pitch, width, height);
                shell_state.cursor_y = 95;
                shell_state.scroll_offset = 0;
            } else {
                fb_print(fb, pitch, 70, shell_state.cursor_y, "Manual not found", 0x00FF4444);
                shell_state.cursor_y += shell_state.line_height + 3;
            }
            return;
        }
        if ((arg[0] == '-' && arg[1] == 'h') || 
            (arg[0] == '-' && arg[1] == '-' && arg[2] == 'h' && arg[3] == 'e' && arg[4] == 'l' && arg[5] == 'p')) {
            fb_print(fb, pitch, 70, shell_state.cursor_y, "Help Command Usage:", 0x00FFFF00);
            shell_state.cursor_y += shell_state.line_height + 5;
            fb_print(fb, pitch, 90, shell_state.cursor_y, "help         - Show all available commands", 0x00CCCCCC);
            shell_state.cursor_y += shell_state.line_height + 3;
            fb_print(fb, pitch, 90, shell_state.cursor_y, "<cmd> -h     - Show help for specific command", 0x00CCCCCC);
            shell_state.cursor_y += shell_state.line_height + 3;
            fb_print(fb, pitch, 90, shell_state.cursor_y, "<cmd> --help - Show help for specific command", 0x00CCCCCC);
            shell_state.cursor_y += shell_state.line_height + 3;
            fb_print(fb, pitch, 90, shell_state.cursor_y, "help -m / --manual - Open command manual", 0x00CCCCCC);
            shell_state.cursor_y += shell_state.line_height + 3;
            return;
        }
        fb_print(fb, pitch, 70, shell_state.cursor_y, "~ Spellbook of Incantations ~", 0x00FFFF);
        shell_state.cursor_y += shell_state.line_height + 5;
        fb_print(fb, pitch, 90, shell_state.cursor_y, "help       - Display mystical guide", 0x00CCCCCC);
        shell_state.cursor_y += shell_state.line_height + 3;
        fb_print(fb, pitch, 90, shell_state.cursor_y, "manual     - Open command manual", 0x00CCCCCC);
        shell_state.cursor_y += shell_state.line_height + 3;
        fb_print(fb, pitch, 90, shell_state.cursor_y, "logo       - Display realm emblem & info", 0x00CCCCCC);
        shell_state.cursor_y += shell_state.line_height + 3;
        fb_print(fb, pitch, 90, shell_state.cursor_y, "pwd        - Print working directory", 0x00CCCCCC);
        shell_state.cursor_y += shell_state.line_height + 3;
        fb_print(fb, pitch, 90, shell_state.cursor_y, "ls         - List files (5 per row, folders marked /)", 0x00CCCCCC);
        shell_state.cursor_y += shell_state.line_height + 3;
        fb_print(fb, pitch, 90, shell_state.cursor_y, "tree       - Show directory tree structure", 0x00CCCCCC);
        shell_state.cursor_y += shell_state.line_height + 3;
        fb_print(fb, pitch, 90, shell_state.cursor_y, "cd <folder> - Enter sacred chamber", 0x00CCCCCC);
        shell_state.cursor_y += shell_state.line_height + 3;
        fb_print(fb, pitch, 90, shell_state.cursor_y, "read <file> - Read scroll contents", 0x00CCCCCC);
        shell_state.cursor_y += shell_state.line_height + 3;
        fb_print(fb, pitch, 90, shell_state.cursor_y, "edit <file> - Open scroll in editor", 0x00CCCCCC);
        shell_state.cursor_y += shell_state.line_height + 3;
        fb_print(fb, pitch, 90, shell_state.cursor_y, "create <name> - Create file/folder (add / for folder)", 0x00CCCCCC);
        shell_state.cursor_y += shell_state.line_height + 3;
        fb_print(fb, pitch, 90, shell_state.cursor_y, "write <file> <text> - Write to scroll", 0x00CCCCCC);
        shell_state.cursor_y += shell_state.line_height + 3;
        fb_print(fb, pitch, 90, shell_state.cursor_y, "copy <src> <dest> - Duplicate scroll", 0x00CCCCCC);
        shell_state.cursor_y += shell_state.line_height + 3;
        fb_print(fb, pitch, 90, shell_state.cursor_y, "find <pattern> - Search for scrolls", 0x00CCCCCC);
        shell_state.cursor_y += shell_state.line_height + 3;
        fb_print(fb, pitch, 90, shell_state.cursor_y, "rm <file>  - Destroy scroll", 0x00CCCCCC);
        shell_state.cursor_y += shell_state.line_height + 3;
        fb_print(fb, pitch, 90, shell_state.cursor_y, "echo [text] - Speak to void", 0x00CCCCCC);
        shell_state.cursor_y += shell_state.line_height + 3;
        fb_print(fb, pitch, 90, shell_state.cursor_y, "status     - Kingdom vitals", 0x00CCCCCC);
        shell_state.cursor_y += shell_state.line_height + 3;
        fb_print(fb, pitch, 90, shell_state.cursor_y, "whoami     - Your identity", 0x00CCCCCC);
        shell_state.cursor_y += shell_state.line_height + 3;
        fb_print(fb, pitch, 90, shell_state.cursor_y, "useradd <u> - New seeker", 0x00CCCCCC);
        shell_state.cursor_y += shell_state.line_height + 3;
        fb_print(fb, pitch, 90, shell_state.cursor_y, "login <u>  - Become seeker", 0x00CCCCCC);
        shell_state.cursor_y += shell_state.line_height + 3;
            fb_print(fb, pitch, 90, shell_state.cursor_y, "disks      - Detect storage devices", 0x00CCCCCC);
            shell_state.cursor_y += shell_state.line_height + 2;
            fb_print(fb, pitch, 90, shell_state.cursor_y, "partcheck  - Verify partitions", 0x00CCCCCC);
            shell_state.cursor_y += shell_state.line_height + 2;
            fb_print(fb, pitch, 90, shell_state.cursor_y, "pci        - List PCI devices", 0x00CCCCCC);
            shell_state.cursor_y += shell_state.line_height + 2;
            fb_print(fb, pitch, 90, shell_state.cursor_y, "ip         - Show/set IP config", 0x00CCCCCC);
            shell_state.cursor_y += shell_state.line_height + 2;
            fb_print(fb, pitch, 90, shell_state.cursor_y, "ping <ip>  - ICMP echo", 0x00CCCCCC);
            shell_state.cursor_y += shell_state.line_height + 2;
            fb_print(fb, pitch, 90, shell_state.cursor_y, "whoami     - Your identity", 0x00CCCCCC);
        shell_state.cursor_y += shell_state.line_height + 3;
        fb_print(fb, pitch, 70, shell_state.cursor_y, "Tip: Use '<cmd> -h' or '<cmd> --help' for detailed info", 0x00FFAA00);
        shell_state.cursor_y += shell_state.line_height + 3;
        return;
    }

    /* === MANUAL COMMAND (open command reference) === */
    if ((cmd[0] == 'm' && cmd[1] == 'a' && cmd[2] == 'n' && (cmd[3] == 0 || cmd[3] == ' ')) ||
        (cmd[0] == 'm' && cmd[1] == 'a' && cmd[2] == 'n' && cmd[3] == 'u' && cmd[4] == 'a' && cmd[5] == 'l')) {
        char* arg = cmd + 3;
        if (cmd[3] == 'u') {
            arg = cmd + 6;
        }
        while (*arg == ' ') arg++;
        if ((arg[0] == '-' && arg[1] == 'h') ||
            (arg[0] == '-' && arg[1] == '-' && arg[2] == 'h' && arg[3] == 'e' && arg[4] == 'l' && arg[5] == 'p')) {
            fb_print(fb, pitch, 70, shell_state.cursor_y, "Manual Command Usage:", 0x00FFFF00);
            shell_state.cursor_y += shell_state.line_height + 5;
            fb_print(fb, pitch, 90, shell_state.cursor_y, "manual / man  - Open command manual", 0x00CCCCCC);
            shell_state.cursor_y += shell_state.line_height + 3;
            return;
        }
        VirtualFile* manual = fs_find_file_by_name("COMMANDS.txt");
        if (manual && !manual->is_folder) {
            open_text_editor(fb, pitch, width, height, manual);
            shell_clear_to_header(fb, pitch, width, height);
            shell_state.cursor_y = 95;
            shell_state.scroll_offset = 0;
        } else {
            fb_print(fb, pitch, 70, shell_state.cursor_y, "Manual not found", 0x00FF4444);
            shell_state.cursor_y += shell_state.line_height + 3;
        }
        return;
    }
    
    /* === CLEAR COMMAND === */
    if (cmd[0] == 'c' && cmd[1] == 'l' && cmd[2] == 'e' && cmd[3] == 'a' && cmd[4] == 'r') {
        char* arg = cmd + 5;
        while (*arg == ' ') arg++;
        if ((arg[0] == '-' && arg[1] == 'h') || 
            (arg[0] == '-' && arg[1] == '-' && arg[2] == 'h' && arg[3] == 'e' && arg[4] == 'l' && arg[5] == 'p')) {
            fb_print(fb, pitch, 70, shell_state.cursor_y, "Clear Command Usage:", 0x00FFFF00);
            shell_state.cursor_y += shell_state.line_height + 5;
            fb_print(fb, pitch, 90, shell_state.cursor_y, "clear  - Clear screen and show minimal header", 0x00CCCCCC);
            shell_state.cursor_y += shell_state.line_height + 3;
            fb_print(fb, pitch, 90, shell_state.cursor_y, "Displays current path after clearing", 0x00CCCCCC);
            shell_state.cursor_y += shell_state.line_height + 3;
            return;
        }
        /* Clear framebuffer and redraw header */
        shell_clear_to_header(fb, pitch, width, height);
        
        /* Reset cursor and scrolling */
        shell_state.cursor_y = 95;
        shell_state.scroll_offset = 0;
        shell_state.pos = 0;
        shell_state.buffer[0] = 0;
        return;
    }
    
    /* === DISKS COMMAND === */
    if (cmd[0] == 'd' && cmd[1] == 'i' && cmd[2] == 's' && cmd[3] == 'k' && cmd[4] == 's') {
        int count = block_count();
        if (count == 0) {
            fb_print(fb, pitch, 70, shell_state.cursor_y, "No disks detected", 0x00FF9999);
            shell_state.cursor_y += shell_state.line_height + 3;
            return;
        }

        for (int i = 0; i < count; i++) {
            BlockDevice *dev = block_get(i);
            if (dev && dev->name) {
                fb_print(fb, pitch, 70, shell_state.cursor_y, dev->name, 0x0088FF88);
                shell_state.cursor_y += shell_state.line_height + 3;
            }
        }
        return;
    }

    /* === PARTCHECK COMMAND === */
    if (cmd[0] == 'p' && cmd[1] == 'a' && cmd[2] == 'r' && cmd[3] == 't' && cmd[4] == 'c' && cmd[5] == 'h' && cmd[6] == 'e' && cmd[7] == 'c' && cmd[8] == 'k') {
        int count = block_count();
        if (count == 0) {
            fb_print(fb, pitch, 70, shell_state.cursor_y, "No disks detected", 0x00FF9999);
            shell_state.cursor_y += shell_state.line_height + 3;
            return;
        }

        int found_any = 0;
        for (int i = 0; i < count; i++) {
            BlockDevice *dev = block_get(i);
            PartitionInfo part;
            if (dev && find_linux_partition(dev, &part)) {
                fb_print(fb, pitch, 70, shell_state.cursor_y, "Linux partition OK", 0x0088FF88);
                shell_state.cursor_y += shell_state.line_height + 3;
                found_any = 1;
            }
        }

        if (!found_any) {
            fb_print(fb, pitch, 70, shell_state.cursor_y, "No Linux partition found", 0x00FF4444);
            shell_state.cursor_y += shell_state.line_height + 3;
        }
        return;
    }

    /* === PCI COMMAND === */
    if (cmd[0] == 'p' && cmd[1] == 'c' && cmd[2] == 'i') {
        PciDevice list[64];
        int count = pci_enumerate(list, 64);
        if (count <= 0) {
            fb_print(fb, pitch, 70, shell_state.cursor_y, "No PCI devices", 0x00FF9999);
            shell_state.cursor_y += shell_state.line_height + 3;
            return;
        }

        int limit = count;
        if (limit > 64) {
            limit = 64;
        }

        for (int i = 0; i < limit; i++) {
            char line[64];
            int pos = 0;
            append_hex(line, &pos, list[i].bus, 2);
            line[pos++] = ':';
            append_hex(line, &pos, list[i].slot, 2);
            line[pos++] = '.';
            append_hex(line, &pos, list[i].func, 1);
            line[pos++] = ' ';
            append_str(line, &pos, "ven=");
            append_hex(line, &pos, list[i].vendor_id, 4);
            line[pos++] = ' ';
            append_str(line, &pos, "dev=");
            append_hex(line, &pos, list[i].device_id, 4);
            line[pos++] = ' ';
            append_str(line, &pos, "cls=");
            append_hex(line, &pos, list[i].class_code, 2);
            line[pos++] = ':';
            append_hex(line, &pos, list[i].subclass, 2);
            line[pos++] = ':';
            append_hex(line, &pos, list[i].prog_if, 2);
            line[pos] = 0;

            fb_print(fb, pitch, 70, shell_state.cursor_y, line, 0x0088FF88);
            shell_state.cursor_y += shell_state.line_height + 3;
        }
        return;
    }

    /* === IP COMMAND === */
    if (cmd[0] == 'i' && cmd[1] == 'p') {
        char* arg = cmd + 2;
        while (*arg == ' ') arg++;

        if (arg[0] == 0) {
            uint32_t ip, mask, gw;
            char ip_str[16], mask_str[16], gw_str[16];
            net_get_ip(&ip, &mask, &gw);
            net_ip_to_str(ip, ip_str, sizeof(ip_str));
            net_ip_to_str(mask, mask_str, sizeof(mask_str));
            net_ip_to_str(gw, gw_str, sizeof(gw_str));

            fb_print(fb, pitch, 70, shell_state.cursor_y, "IP:", 0x00AAAAFF);
            shell_state.cursor_y += shell_state.line_height + 3;
            fb_print(fb, pitch, 90, shell_state.cursor_y, ip_str, 0x0088FF88);
            shell_state.cursor_y += shell_state.line_height + 3;
            fb_print(fb, pitch, 70, shell_state.cursor_y, "MASK:", 0x00AAAAFF);
            shell_state.cursor_y += shell_state.line_height + 3;
            fb_print(fb, pitch, 90, shell_state.cursor_y, mask_str, 0x0088FF88);
            shell_state.cursor_y += shell_state.line_height + 3;
            fb_print(fb, pitch, 70, shell_state.cursor_y, "GW:", 0x00AAAAFF);
            shell_state.cursor_y += shell_state.line_height + 3;
            fb_print(fb, pitch, 90, shell_state.cursor_y, gw_str, 0x0088FF88);
            shell_state.cursor_y += shell_state.line_height + 3;
            return;
        }

        if (arg[0] == 's' && arg[1] == 'e' && arg[2] == 't') {
            arg += 3;
            while (*arg == ' ') arg++;

            char ip_s[16], mask_s[16], gw_s[16];
            int idx = 0;
            while (*arg && *arg != ' ' && idx < 15) ip_s[idx++] = *arg++;
            ip_s[idx] = 0;
            while (*arg == ' ') arg++;
            idx = 0;
            while (*arg && *arg != ' ' && idx < 15) mask_s[idx++] = *arg++;
            mask_s[idx] = 0;
            while (*arg == ' ') arg++;
            idx = 0;
            while (*arg && *arg != ' ' && idx < 15) gw_s[idx++] = *arg++;
            gw_s[idx] = 0;

            uint32_t ip, mask, gw;
            if (!net_parse_ipv4(ip_s, &ip) || !net_parse_ipv4(mask_s, &mask) || !net_parse_ipv4(gw_s, &gw)) {
                fb_print(fb, pitch, 70, shell_state.cursor_y, "Usage: ip set <ip> <mask> <gw>", 0x00FFAA00);
                shell_state.cursor_y += shell_state.line_height + 3;
                return;
            }
            net_set_ip(ip, mask, gw);
            fb_print(fb, pitch, 70, shell_state.cursor_y, "IP updated", 0x0088FF88);
            shell_state.cursor_y += shell_state.line_height + 3;
            return;
        }

        fb_print(fb, pitch, 70, shell_state.cursor_y, "Usage: ip [set <ip> <mask> <gw>]", 0x00FFAA00);
        shell_state.cursor_y += shell_state.line_height + 3;
        return;
    }

    /* === PING COMMAND === */
    if (cmd[0] == 'p' && cmd[1] == 'i' && cmd[2] == 'n' && cmd[3] == 'g') {
        char* target = cmd + 4;
        while (*target == ' ') target++;
        if (target[0] == 0) {
            fb_print(fb, pitch, 70, shell_state.cursor_y, "Usage: ping <ip>", 0x00FFAA00);
            shell_state.cursor_y += shell_state.line_height + 3;
            return;
        }
        if (net_ping(target)) {
            fb_print(fb, pitch, 70, shell_state.cursor_y, "Ping OK", 0x0088FF88);
        } else {
            fb_print(fb, pitch, 70, shell_state.cursor_y, "Ping failed", 0x00FF4444);
        }
        shell_state.cursor_y += shell_state.line_height + 3;
        return;
    }

    /* === LOGO COMMAND === */
    if (cmd[0] == 'l' && cmd[1] == 'o' && cmd[2] == 'g' && cmd[3] == 'o') {
        char* arg = cmd + 4;
        while (*arg == ' ') arg++;
        if ((arg[0] == '-' && arg[1] == 'h') || 
            (arg[0] == '-' && arg[1] == '-' && arg[2] == 'h' && arg[3] == 'e' && arg[4] == 'l' && arg[5] == 'p')) {
            fb_print(fb, pitch, 70, shell_state.cursor_y, "Logo Command Usage:", 0x00FFFF00);
            shell_state.cursor_y += shell_state.line_height + 5;
            fb_print(fb, pitch, 90, shell_state.cursor_y, "logo    - Display OS emblem and system info", 0x00CCCCCC);
            shell_state.cursor_y += shell_state.line_height + 3;
            fb_print(fb, pitch, 90, shell_state.cursor_y, "Shows: Version, kernel type, shell info, file system", 0x00CCCCCC);
            shell_state.cursor_y += shell_state.line_height + 3;
            return;
        }
        int logo_scale = 2;
        for (int i = 0; i < KAGAMI_LOGO_LINES; i++) {
            fb_print_scaled(fb, pitch, 150, shell_state.cursor_y, kagami_logo[i], 0x00FF00FF, logo_scale);
            shell_state.cursor_y += 28;
        }
        shell_state.cursor_y += 12;
        fb_print(fb, pitch, 350, shell_state.cursor_y, "Version: 0.1 'Awakening'", 0x00AAAAFF);
        shell_state.cursor_y += 20;
        fb_print(fb, pitch, 350, shell_state.cursor_y, "Kernel: 64-bit UEFI", 0x00AAAAFF);
        shell_state.cursor_y += 20;
        fb_print(fb, pitch, 350, shell_state.cursor_y, "Shell: Unified Framebuffer", 0x00AAAAFF);
        shell_state.cursor_y += 20;
        fb_print(fb, pitch, 350, shell_state.cursor_y, "File System: Virtual Home", 0x00AAAAFF);
        shell_state.cursor_y += 25;
        return;
    }
    
    /* === LS COMMAND (horizontal grid layout - 5 per line) === */
    if (cmd[0] == 'l' && cmd[1] == 's') {
        char* arg = cmd + 2;
        while (*arg == ' ') arg++;
        if ((arg[0] == '-' && arg[1] == 'h') || 
            (arg[0] == '-' && arg[1] == '-' && arg[2] == 'h' && arg[3] == 'e' && arg[4] == 'l' && arg[5] == 'p')) {
            fb_print(fb, pitch, 70, shell_state.cursor_y, "List Command Usage:", 0x00FFFF00);
            shell_state.cursor_y += shell_state.line_height + 5;
            fb_print(fb, pitch, 90, shell_state.cursor_y, "ls    - List files and folders in current directory", 0x00CCCCCC);
            shell_state.cursor_y += shell_state.line_height + 3;
            fb_print(fb, pitch, 90, shell_state.cursor_y, "Format: 5 items per row, folders marked with /", 0x00CCCCCC);
            shell_state.cursor_y += shell_state.line_height + 3;
            return;
        }
        if (vfs_is_mounted()) {
            char list_buf[2048];
            if (vfs_list_dir(current_directory, list_buf, sizeof(list_buf))) {
                const char *p = list_buf;
                while (*p) {
                    char name[64];
                    int n = 0;
                    while (*p && *p != '\n' && n < (int)sizeof(name) - 1) {
                        name[n++] = *p++;
                    }
                    name[n] = 0;
                    if (*p == '\n') {
                        p++;
                    }

                    if (n > 0) {
                        fb_print(fb, pitch, 70, shell_state.cursor_y, name, 0x0088FF88);
                        shell_state.cursor_y += shell_state.line_height + 3;
                    }
                }
            } else {
                fb_print(fb, pitch, 70, shell_state.cursor_y, "Failed to list directory", 0x00FF9999);
                shell_state.cursor_y += shell_state.line_height + 5;
            }
            return;
        }

        int dir_files = 0;
        for (int i = 0; i < file_count; i++) {
            /* Check if file is in current directory - exact match only */
            int match = 1;
            int j = 0;
            
            /* Compare each character */
            while (current_directory[j] || file_system[i].parent[j]) {
                if (current_directory[j] != file_system[i].parent[j]) {
                    match = 0;
                    break;
                }
                if (current_directory[j] == 0 && file_system[i].parent[j] == 0) {
                    break;
                }
                j++;
            }
            
            if (match) {
                dir_files++;
            }
        }
        
        if (dir_files == 0) {
            fb_print(fb, pitch, 70, shell_state.cursor_y, "Chamber is empty...", 0x00FF9999);
            shell_state.cursor_y += shell_state.line_height + 5;
        } else {
            int col = 0;
            unsigned int file_x = 70;
            
            for (int i = 0; i < file_count; i++) {
                /* Check if file is in current directory - exact match only */
                int match = 1;
                int j = 0;
                
                /* Compare each character */
                while (current_directory[j] || file_system[i].parent[j]) {
                    if (current_directory[j] != file_system[i].parent[j]) {
                        match = 0;
                        break;
                    }
                    if (current_directory[j] == 0 && file_system[i].parent[j] == 0) {
                        break;
                    }
                    j++;
                }
                
                if (match) {
                    /* Display file/folder */
                    if (file_system[i].is_folder) {
                        fb_print(fb, pitch, file_x, shell_state.cursor_y, file_system[i].name, 0x0088CCFF);
                        fb_print(fb, pitch, file_x + (32 * 8), shell_state.cursor_y, "/", 0x0088CCFF);
                    } else {
                        fb_print(fb, pitch, file_x, shell_state.cursor_y, file_system[i].name, 0x0088FF88);
                    }
                    
                    col++;
                    file_x += 200;  /* Move to next column */
                    
                    /* If 5 files per line, wrap to next line */
                    if (col >= 5) {
                        col = 0;
                        file_x = 70;
                        shell_state.cursor_y += shell_state.line_height + 5;
                    }
                }
            }
            
            if (col != 0) {
                shell_state.cursor_y += shell_state.line_height + 5;
            }
        }
        return;
    }
    
    
    /* === PWD COMMAND (print working directory) === */
    if (cmd[0] == 'p' && cmd[1] == 'w' && cmd[2] == 'd') {
        char* arg = cmd + 3;
        while (*arg == ' ') arg++;
        if ((arg[0] == '-' && arg[1] == 'h') || 
            (arg[0] == '-' && arg[1] == '-' && arg[2] == 'h' && arg[3] == 'e' && arg[4] == 'l' && arg[5] == 'p')) {
            fb_print(fb, pitch, 70, shell_state.cursor_y, "Print Working Directory Usage:", 0x00FFFF00);
            shell_state.cursor_y += shell_state.line_height + 5;
            fb_print(fb, pitch, 90, shell_state.cursor_y, "pwd  - Display current directory path", 0x00CCCCCC);
            shell_state.cursor_y += shell_state.line_height + 3;
            return;
        }
        fb_print(fb, pitch, 70, shell_state.cursor_y, current_directory, 0x0088FFFF);
        shell_state.cursor_y += shell_state.line_height + 3;
        return;
    }
    
    /* === CD COMMAND (change directory) === */
    if (cmd[0] == 'c' && cmd[1] == 'd') {
        char* dirname = cmd + 2;
        while (*dirname == ' ') dirname++;
        
        if ((dirname[0] == '-' && dirname[1] == 'h') || 
            (dirname[0] == '-' && dirname[1] == '-' && dirname[2] == 'h' && dirname[3] == 'e' && dirname[4] == 'l' && dirname[5] == 'p')) {
            fb_print(fb, pitch, 70, shell_state.cursor_y, "Change Directory Usage:", 0x00FFFF00);
            shell_state.cursor_y += shell_state.line_height + 5;
            fb_print(fb, pitch, 90, shell_state.cursor_y, "cd <folder>  - Enter specified folder", 0x00CCCCCC);
            shell_state.cursor_y += shell_state.line_height + 3;
            fb_print(fb, pitch, 90, shell_state.cursor_y, "cd ..        - Go to parent directory", 0x00CCCCCC);
            shell_state.cursor_y += shell_state.line_height + 3;
            fb_print(fb, pitch, 90, shell_state.cursor_y, "cd           - Go to root directory", 0x00CCCCCC);
            shell_state.cursor_y += shell_state.line_height + 3;
            return;
        }
        
        if (!dirname || dirname[0] == 0) {
            /* cd with no args goes to root */
            current_directory[0] = '/';
            current_directory[1] = 0;
            fb_print(fb, pitch, 70, shell_state.cursor_y, "Returned to realm entrance", 0x0088FF88);
            shell_state.cursor_y += shell_state.line_height + 3;
            return;
        }
        
        /* Check if trying to go to parent */
        if (dirname[0] == '.' && dirname[1] == '.') {
            /* Find last / in current path */
            int last_slash = -1;
            for (int i = 0; current_directory[i]; i++) {
                if (current_directory[i] == '/') {
                    last_slash = i;
                }
            }
            
            if (last_slash <= 0) {
                fb_print(fb, pitch, 70, shell_state.cursor_y, "Already at root", 0x00FF9999);
                shell_state.cursor_y += shell_state.line_height + 3;
            } else {
                /* Truncate at last slash */
                current_directory[last_slash] = 0;
                if (current_directory[0] == 0) {
                    current_directory[0] = '/';
                    current_directory[1] = 0;
                }
                fb_print(fb, pitch, 70, shell_state.cursor_y, "Moved to parent directory", 0x0088FF88);
                shell_state.cursor_y += shell_state.line_height + 3;
            }
            return;
        }
        
        if (vfs_is_mounted()) {
            char target[128];
            build_full_path(dirname, target, sizeof(target));
            char list_buf[256];
            if (vfs_list_dir(target, list_buf, sizeof(list_buf))) {
                int i = 0;
                while (target[i] && i < (int)sizeof(current_directory) - 1) {
                    current_directory[i] = target[i];
                    i++;
                }
                current_directory[i] = 0;
                fb_print(fb, pitch, 70, shell_state.cursor_y, "Directory changed", 0x0088FF88);
                shell_state.cursor_y += shell_state.line_height + 3;
            } else {
                fb_print(fb, pitch, 70, shell_state.cursor_y, "Chamber not found!", 0x00FF4444);
                shell_state.cursor_y += shell_state.line_height + 3;
            }
            return;
        }

        /* Find directory */
        int found = -1;
        for (int i = 0; i < file_count; i++) {
            if (!file_system[i].is_folder) continue;
            
            int match = 1;
            for (int j = 0; dirname[j] && file_system[i].name[j]; j++) {
                if (dirname[j] != file_system[i].name[j]) {
                    match = 0;
                    break;
                }
            }
            if (match && dirname[0] == file_system[i].name[0]) {
                found = i;
                break;
            }
        }
        
        if (found >= 0) {
            /* Build new path by appending folder name */
            int path_len = 0;
            while (current_directory[path_len]) path_len++;
            
            /* Add slash if not at root */
            if (path_len > 0 && current_directory[path_len - 1] != '/') {
                current_directory[path_len++] = '/';
            }
            
            /* Append folder name */
            int k = 0;
            while (file_system[found].name[k] && path_len < 62) {
                current_directory[path_len++] = file_system[found].name[k++];
            }
            current_directory[path_len] = 0;
            
            char welcome_msg[80];
            char* w = welcome_msg;
            const char* enter_msg = "Entering chamber: ";
            while (*enter_msg) *w++ = *enter_msg++;
            k = 0;
            while (file_system[found].name[k]) *w++ = file_system[found].name[k++];
            *w = 0;
            
            fb_print(fb, pitch, 70, shell_state.cursor_y, welcome_msg, 0x0088FF88);
            shell_state.cursor_y += shell_state.line_height + 3;
        } else {
            fb_print(fb, pitch, 70, shell_state.cursor_y, "Chamber not found!", 0x00FF4444);
            shell_state.cursor_y += shell_state.line_height + 3;
        }
        return;
    }
    
    /* === READ COMMAND (read file) === */
    if (cmd[0] == 'r' && cmd[1] == 'e' && cmd[2] == 'a' && cmd[3] == 'd') {
        char* filename = cmd + 4;
        while (*filename == ' ') filename++;
        
        if ((filename[0] == '-' && filename[1] == 'h') || 
            (filename[0] == '-' && filename[1] == '-' && filename[2] == 'h' && filename[3] == 'e' && filename[4] == 'l' && filename[5] == 'p')) {
            fb_print(fb, pitch, 70, shell_state.cursor_y, "Read Command Usage:", 0x00FFFF00);
            shell_state.cursor_y += shell_state.line_height + 5;
            fb_print(fb, pitch, 90, shell_state.cursor_y, "read <file>  - Open file in editor", 0x00CCCCCC);
            shell_state.cursor_y += shell_state.line_height + 3;
            fb_print(fb, pitch, 90, shell_state.cursor_y, "Example: read readme.txt", 0x00CCCCCC);
            shell_state.cursor_y += shell_state.line_height + 3;
            return;
        }
        
        if (vfs_is_mounted()) {
            char path[128];
            build_full_path(filename, path, sizeof(path));
            char file_buf[1024];
            uint32_t read_size = 0;
            if (vfs_read_file(path, file_buf, sizeof(file_buf) - 1, &read_size)) {
                file_buf[read_size] = 0;
                fb_print(fb, pitch, 70, shell_state.cursor_y, file_buf, 0x00CCCCFF);
                shell_state.cursor_y += shell_state.line_height + 5;
            } else {
                fb_print(fb, pitch, 70, shell_state.cursor_y, "File not found", 0x00FF4444);
                shell_state.cursor_y += shell_state.line_height + 3;
            }
            return;
        }

        int found = 0;
        for (int i = 0; i < file_count; i++) {
            int match = 1;
            for (int j = 0; j < 32 && filename[j] && file_system[i].name[j]; j++) {
                if (filename[j] != file_system[i].name[j]) {
                    match = 0;
                    break;
                }
            }
            
            if (match) {
                if (file_system[i].is_folder) {
                    fb_print(fb, pitch, 70, shell_state.cursor_y, "This is sacred chamber, not a scroll!", 0x00FF9999);
                    shell_state.cursor_y += shell_state.line_height + 3;
                } else {
                    open_text_editor(fb, pitch, width, height, &file_system[i]);
                    shell_clear_to_header(fb, pitch, width, height);
                    shell_state.cursor_y = 95;
                    shell_state.scroll_offset = 0;
                }
                found = 1;
                break;
            }
        }
        
        if (!found) {
            fb_print(fb, pitch, 70, shell_state.cursor_y, "Scroll not found...", 0x00FF4444);
            shell_state.cursor_y += shell_state.line_height + 3;
        }
        return;
    }

    /* === EDIT COMMAND (open editor) === */
    if (cmd[0] == 'e' && cmd[1] == 'd' && cmd[2] == 'i' && cmd[3] == 't') {
        char* filename = cmd + 4;
        while (*filename == ' ') filename++;

        if ((filename[0] == '-' && filename[1] == 'h') ||
            (filename[0] == '-' && filename[1] == '-' && filename[2] == 'h' && filename[3] == 'e' && filename[4] == 'l' && filename[5] == 'p')) {
            fb_print(fb, pitch, 70, shell_state.cursor_y, "Edit Command Usage:", 0x00FFFF00);
            shell_state.cursor_y += shell_state.line_height + 5;
            fb_print(fb, pitch, 90, shell_state.cursor_y, "edit <file>  - Open file in editor", 0x00CCCCCC);
            shell_state.cursor_y += shell_state.line_height + 3;
            fb_print(fb, pitch, 90, shell_state.cursor_y, "Keys: i=insert, ESC=normal, Ctrl+S=save, Ctrl+Q=quit", 0x00CCCCCC);
            shell_state.cursor_y += shell_state.line_height + 3;
            return;
        }

        int found = 0;
        for (int i = 0; i < file_count; i++) {
            int match = 1;
            for (int j = 0; j < 32 && filename[j] && file_system[i].name[j]; j++) {
                if (filename[j] != file_system[i].name[j]) {
                    match = 0;
                    break;
                }
            }
            if (match) {
                if (file_system[i].is_folder) {
                    fb_print(fb, pitch, 70, shell_state.cursor_y, "This is sacred chamber, not a scroll!", 0x00FF9999);
                    shell_state.cursor_y += shell_state.line_height + 3;
                } else {
                    open_text_editor(fb, pitch, width, height, &file_system[i]);
                    shell_clear_to_header(fb, pitch, width, height);
                    shell_state.cursor_y = 95;
                    shell_state.scroll_offset = 0;
                }
                found = 1;
                break;
            }
        }

        if (!found) {
            fb_print(fb, pitch, 70, shell_state.cursor_y, "Scroll not found...", 0x00FF4444);
            shell_state.cursor_y += shell_state.line_height + 3;
        }
        return;
    }
    
    /* === CREATE COMMAND (create file/folder with path support) === */
    if (cmd[0] == 'c' && cmd[1] == 'r' && cmd[2] == 'e' && cmd[3] == 'a' && cmd[4] == 't' && cmd[5] == 'e') {
        char* path = cmd + 6;
        while (*path == ' ') path++;
        
        if ((path[0] == '-' && path[1] == 'h') || 
            (path[0] == '-' && path[1] == '-' && path[2] == 'h' && path[3] == 'e' && path[4] == 'l' && path[5] == 'p')) {
            fb_print(fb, pitch, 70, shell_state.cursor_y, "Create Command Usage:", 0x00FFFF00);
            shell_state.cursor_y += shell_state.line_height + 5;
            fb_print(fb, pitch, 90, shell_state.cursor_y, "create <file>        - Create file in current dir", 0x00CCCCCC);
            shell_state.cursor_y += shell_state.line_height + 3;
            fb_print(fb, pitch, 90, shell_state.cursor_y, "create <folder>/     - Create folder (note trailing /)", 0x00CCCCCC);
            shell_state.cursor_y += shell_state.line_height + 3;
            fb_print(fb, pitch, 90, shell_state.cursor_y, "create <dir>/<file>  - Create file in folder", 0x00CCCCCC);
            shell_state.cursor_y += shell_state.line_height + 3;
            fb_print(fb, pitch, 90, shell_state.cursor_y, "Examples:", 0x00FFAA00);
            shell_state.cursor_y += shell_state.line_height + 3;
            fb_print(fb, pitch, 90, shell_state.cursor_y, "  create test.txt      (creates file)", 0x00AAAAAA);
            shell_state.cursor_y += shell_state.line_height + 3;
            fb_print(fb, pitch, 90, shell_state.cursor_y, "  create projects/     (creates folder)", 0x00AAAAAA);
            shell_state.cursor_y += shell_state.line_height + 3;
            fb_print(fb, pitch, 90, shell_state.cursor_y, "  create docs/file.md  (file in folder)", 0x00AAAAAA);
            shell_state.cursor_y += shell_state.line_height + 3;
            return;
        }
        
        if (!fs_ensure_capacity(1)) {
            fb_print(fb, pitch, 70, shell_state.cursor_y, "Vault is full!", 0x00FF4444);
            shell_state.cursor_y += shell_state.line_height + 3;
            return;
        }
        
        if (path[0] == 0) {
            fb_print(fb, pitch, 70, shell_state.cursor_y, "Usage: create <name> or <name/> or <folder/file>", 0x00FFAA00);
            shell_state.cursor_y += shell_state.line_height + 3;
            return;
        }
        
        /* Check if path contains slash */
        int has_slash = 0;
        int slash_pos = -1;
        int len = 0;
        while (path[len] && path[len] != ' ') {
            if (path[len] == '/') {
                has_slash = 1;
                slash_pos = len;
            }
            len++;
        }
        
        /* Case 1: name/ - create folder */
        if (has_slash && slash_pos == len - 1) {
            /* Extract folder name without trailing slash */
            int name_len = 0;
            while (name_len < slash_pos && name_len < 31) {
                file_system[file_count].name[name_len] = path[name_len];
                name_len++;
            }
            file_system[file_count].name[name_len] = 0;
            file_system[file_count].content[0] = 0;
            file_system[file_count].size = 0;
            file_system[file_count].is_folder = 1;
            
            /* Set parent to current directory */
            int j = 0;
            while (current_directory[j] && j < 63) {
                file_system[file_count].parent[j] = current_directory[j];
                j++;
            }
            file_system[file_count].parent[j] = 0;
            
            file_count++;
            fb_print(fb, pitch, 70, shell_state.cursor_y, "Chamber created!", 0x0088FF88);
            shell_state.cursor_y += shell_state.line_height + 3;
            return;
        }
        
        /* Case 2: folder/file - file inside folder */
        if (has_slash && slash_pos < len - 1) {
            /* Extract folder name */
            char folder_name[32];
            int f = 0;
            while (f < slash_pos && f < 31) {
                folder_name[f] = path[f];
                f++;
            }
            folder_name[f] = 0;
            
            /* Extract file name */
            char file_name[32];
            int ff = 0;
            int p = slash_pos + 1;
            while (path[p] && path[p] != ' ' && ff < 31) {
                file_name[ff++] = path[p++];
            }
            file_name[ff] = 0;
            
            /* Check if folder exists */
            int folder_found = -1;
            for (int i = 0; i < file_count; i++) {
                if (!file_system[i].is_folder) continue;
                
                /* Check if name matches */
                int match = 1;
                for (int k = 0; k < 32; k++) {
                    if (folder_name[k] != file_system[i].name[k]) {
                        match = 0;
                        break;
                    }
                    if (folder_name[k] == 0) break;
                }
                
                /* Check if in current directory */
                if (match) {
                    int parent_match = 1;
                    for (int k = 0; k < 64; k++) {
                        if (current_directory[k] != file_system[i].parent[k]) {
                            parent_match = 0;
                            break;
                        }
                        if (current_directory[k] == 0) break;
                    }
                    if (parent_match) {
                        folder_found = i;
                        break;
                    }
                }
            }
            
            if (folder_found >= 0) {
                /* Create file inside folder */
                int name_idx = 0;
                while (file_name[name_idx] && name_idx < 31) {
                    file_system[file_count].name[name_idx] = file_name[name_idx];
                    name_idx++;
                }
                file_system[file_count].name[name_idx] = 0;
                file_system[file_count].content[0] = 0;
                file_system[file_count].size = 0;
                file_system[file_count].is_folder = 0;
                
                /* Set parent to folder path */
                int pp = 0;
                while (current_directory[pp] && pp < 62) {
                    file_system[file_count].parent[pp] = current_directory[pp];
                    pp++;
                }
                if (pp > 0 && file_system[file_count].parent[pp - 1] != '/') {
                    file_system[file_count].parent[pp++] = '/';
                }
                int fn = 0;
                while (folder_name[fn] && pp < 63) {
                    file_system[file_count].parent[pp++] = folder_name[fn++];
                }
                file_system[file_count].parent[pp] = 0;
                
                file_count++;
                fb_print(fb, pitch, 70, shell_state.cursor_y, "Scroll inscribed in chamber!", 0x0088FF88);
                shell_state.cursor_y += shell_state.line_height + 3;
            } else {
                /* Folder doesn't exist - ask to create */
                fb_print(fb, pitch, 70, shell_state.cursor_y, "Chamber not found! Create it? (y/n)", 0x00FFAA00);
                shell_state.cursor_y += shell_state.line_height + 3;
                render_input(fb, pitch, width, "[y/n]> ");
                
                /* Wait for y or n key */
                char response = 0;
                while (1) {
                    char c = get_keyboard_char();
                    if (c == 'y' || c == 'Y') {
                        response = 'y';
                        break;
                    } else if (c == 'n' || c == 'N' || c == '\n') {
                        response = 'n';
                        break;
                    }
                }
                
                shell_state.cursor_y += shell_state.line_height + 3;
                
                if (response == 'y') {
                    /* Create folder first */
                    if (!fs_ensure_capacity(2)) {
                        fb_print(fb, pitch, 70, shell_state.cursor_y, "Vault is full!", 0x00FF4444);
                        shell_state.cursor_y += shell_state.line_height + 3;
                        return;
                    }
                    
                    int fn_idx = 0;
                    while (folder_name[fn_idx] && fn_idx < 31) {
                        file_system[file_count].name[fn_idx] = folder_name[fn_idx];
                        fn_idx++;
                    }
                    file_system[file_count].name[fn_idx] = 0;
                    file_system[file_count].content[0] = 0;
                    file_system[file_count].size = 0;
                    file_system[file_count].is_folder = 1;
                    
                    int pp = 0;
                    while (current_directory[pp] && pp < 63) {
                        file_system[file_count].parent[pp] = current_directory[pp];
                        pp++;
                    }
                    file_system[file_count].parent[pp] = 0;
                    file_count++;
                    
                    /* Now create file */
                    int name_idx = 0;
                    while (file_name[name_idx] && name_idx < 31) {
                        file_system[file_count].name[name_idx] = file_name[name_idx];
                        name_idx++;
                    }
                    file_system[file_count].name[name_idx] = 0;
                    file_system[file_count].content[0] = 0;
                    file_system[file_count].size = 0;
                    file_system[file_count].is_folder = 0;
                    
                    pp = 0;
                    while (current_directory[pp] && pp < 62) {
                        file_system[file_count].parent[pp] = current_directory[pp];
                        pp++;
                    }
                    if (pp > 0 && file_system[file_count].parent[pp - 1] != '/') {
                        file_system[file_count].parent[pp++] = '/';
                    }
                    fn_idx = 0;
                    while (folder_name[fn_idx] && pp < 63) {
                        file_system[file_count].parent[pp++] = folder_name[fn_idx++];
                    }
                    file_system[file_count].parent[pp] = 0;
                    file_count++;
                    
                    fb_print(fb, pitch, 70, shell_state.cursor_y, "Chamber & scroll created!", 0x0088FF88);
                } else {
                    fb_print(fb, pitch, 70, shell_state.cursor_y, "Creation canceled.", 0x00FF9999);
                }
                shell_state.cursor_y += shell_state.line_height + 3;
            }
            return;
        }
        
        /* Case 3: simple name - create file */
        int name_len = 0;
        while (path[name_len] && path[name_len] != ' ' && name_len < 31) {
            file_system[file_count].name[name_len] = path[name_len];
            name_len++;
        }
        file_system[file_count].name[name_len] = 0;
        file_system[file_count].content[0] = 0;
        file_system[file_count].size = 0;
        file_system[file_count].is_folder = 0;
        
        /* Set parent directory */
        int j = 0;
        while (current_directory[j] && j < 63) {
            file_system[file_count].parent[j] = current_directory[j];
            j++;
        }
        file_system[file_count].parent[j] = 0;
        
        file_count++;
        
        fb_print(fb, pitch, 70, shell_state.cursor_y, "Scroll inscribed!", 0x0088FF88);
        shell_state.cursor_y += shell_state.line_height + 3;
        return;
    }
    
    /* === RM COMMAND (delete file) === */
    if (cmd[0] == 'r' && cmd[1] == 'm' && cmd[2] == ' ') {
        char* filename = cmd + 3;
        while (*filename == ' ') filename++;
        
        if ((filename[0] == '-' && filename[1] == 'h') || 
            (filename[0] == '-' && filename[1] == '-' && filename[2] == 'h' && filename[3] == 'e' && filename[4] == 'l' && filename[5] == 'p')) {
            fb_print(fb, pitch, 70, shell_state.cursor_y, "Remove Command Usage:", 0x00FFFF00);
            shell_state.cursor_y += shell_state.line_height + 5;
            fb_print(fb, pitch, 90, shell_state.cursor_y, "rm <file>  - Delete specified file", 0x00CCCCCC);
            shell_state.cursor_y += shell_state.line_height + 3;
            fb_print(fb, pitch, 90, shell_state.cursor_y, "Warning: This action cannot be undone!", 0x00FF4444);
            shell_state.cursor_y += shell_state.line_height + 3;
            return;
        }
        
        int found = -1;
        for (int i = 0; i < file_count; i++) {
            int match = 1;
            for (int j = 0; j < 32 && filename[j] && file_system[i].name[j]; j++) {
                if (filename[j] != file_system[i].name[j]) {
                    match = 0;
                    break;
                }
            }
            if (match) {
                found = i;
                break;
            }
        }
        
        if (found >= 0) {
            for (int i = found; i < file_count - 1; i++) {
                file_system[i] = file_system[i + 1];
            }
            file_count--;
            fb_print(fb, pitch, 70, shell_state.cursor_y, "Scroll destroyed!", 0x00FF9999);
            shell_state.cursor_y += shell_state.line_height + 3;
        } else {
            fb_print(fb, pitch, 70, shell_state.cursor_y, "Scroll not found!", 0x00FF4444);
            shell_state.cursor_y += shell_state.line_height + 3;
        }
        return;
    }
    
    /* === ECHO COMMAND === */
    if (cmd[0] == 'e' && cmd[1] == 'c' && cmd[2] == 'h' && cmd[3] == 'o') {
        char* text = cmd + 4;
        while (*text == ' ') text++;
        
        if ((text[0] == '-' && text[1] == 'h') || 
            (text[0] == '-' && text[1] == '-' && text[2] == 'h' && text[3] == 'e' && text[4] == 'l' && text[5] == 'p')) {
            fb_print(fb, pitch, 70, shell_state.cursor_y, "Echo Command Usage:", 0x00FFFF00);
            shell_state.cursor_y += shell_state.line_height + 5;
            fb_print(fb, pitch, 90, shell_state.cursor_y, "echo <text>  - Display text message", 0x00CCCCCC);
            shell_state.cursor_y += shell_state.line_height + 3;
            fb_print(fb, pitch, 90, shell_state.cursor_y, "Example: echo Hello World", 0x00CCCCCC);
            shell_state.cursor_y += shell_state.line_height + 3;
            return;
        }
        
        if (*text) {
            fb_print(fb, pitch, 70, shell_state.cursor_y, text, 0x00FF00);
        }
        shell_state.cursor_y += shell_state.line_height + 3;
        return;
    }
    
    /* === WRITE COMMAND (write text to file) === */
    if (cmd[0] == 'w' && cmd[1] == 'r' && cmd[2] == 'i' && cmd[3] == 't' && cmd[4] == 'e') {
        char* args = cmd + 5;
        while (*args == ' ') args++;
        
        if ((args[0] == '-' && args[1] == 'h') || 
            (args[0] == '-' && args[1] == '-' && args[2] == 'h' && args[3] == 'e' && args[4] == 'l' && args[5] == 'p')) {
            fb_print(fb, pitch, 70, shell_state.cursor_y, "Write Command Usage:", 0x00FFFF00);
            shell_state.cursor_y += shell_state.line_height + 5;
            fb_print(fb, pitch, 90, shell_state.cursor_y, "write <file> <text>  - Write text to file", 0x00CCCCCC);
            shell_state.cursor_y += shell_state.line_height + 3;
            fb_print(fb, pitch, 90, shell_state.cursor_y, "Overwrites existing content!", 0x00FF9999);
            shell_state.cursor_y += shell_state.line_height + 3;
            fb_print(fb, pitch, 90, shell_state.cursor_y, "Example: write test.txt Hello from Kagami", 0x00CCCCCC);
            shell_state.cursor_y += shell_state.line_height + 3;
            return;
        }
        
        /* Parse filename and text */
        char filename[32];
        int fn_idx = 0;
        while (*args && *args != ' ' && fn_idx < 31) {
            filename[fn_idx++] = *args++;
        }
        filename[fn_idx] = 0;
        
        while (*args == ' ') args++;
        
        if (filename[0] == 0 || *args == 0) {
            fb_print(fb, pitch, 70, shell_state.cursor_y, "Usage: write <file> <text>", 0x00FFAA00);
            shell_state.cursor_y += shell_state.line_height + 3;
            return;
        }

        if (vfs_is_mounted()) {
            char path[128];
            build_full_path(filename, path, sizeof(path));
            if (vfs_write_file(path, args, (uint32_t)str_len(args))) {
                fb_print(fb, pitch, 70, shell_state.cursor_y, "File written", 0x0088FF88);
            } else {
                fb_print(fb, pitch, 70, shell_state.cursor_y, "Write failed", 0x00FF4444);
            }
            shell_state.cursor_y += shell_state.line_height + 3;
            return;
        }
        
        /* Find file */
        int found = -1;
        for (int i = 0; i < file_count; i++) {
            if (file_system[i].is_folder) continue;
            
            int match = 1;
            for (int j = 0; j < 32; j++) {
                if (filename[j] != file_system[i].name[j]) {
                    match = 0;
                    break;
                }
                if (filename[j] == 0) break;
            }
            
            if (match) {
                found = i;
                break;
            }
        }
        
        if (found >= 0) {
            /* Write to file */
            int idx = 0;
            while (*args && idx < MAX_FILE_CONTENT - 1) {
                file_system[found].content[idx++] = *args++;
            }
            file_system[found].content[idx] = 0;
            file_system[found].size = idx;
            
            fb_print(fb, pitch, 70, shell_state.cursor_y, "Text inscribed into scroll!", 0x0088FF88);
            shell_state.cursor_y += shell_state.line_height + 3;
        } else {
            fb_print(fb, pitch, 70, shell_state.cursor_y, "Scroll not found! Use 'create' first.", 0x00FF4444);
            shell_state.cursor_y += shell_state.line_height + 3;
        }
        return;
    }
    
    /* === COPY COMMAND === */
    if (cmd[0] == 'c' && cmd[1] == 'o' && cmd[2] == 'p' && cmd[3] == 'y') {
        char* args = cmd + 4;
        while (*args == ' ') args++;
        
        if ((args[0] == '-' && args[1] == 'h') || 
            (args[0] == '-' && args[1] == '-' && args[2] == 'h' && args[3] == 'e' && args[4] == 'l' && args[5] == 'p')) {
            fb_print(fb, pitch, 70, shell_state.cursor_y, "Copy Command Usage:", 0x00FFFF00);
            shell_state.cursor_y += shell_state.line_height + 5;
            fb_print(fb, pitch, 90, shell_state.cursor_y, "copy <src> <dest>  - Copy file to new name", 0x00CCCCCC);
            shell_state.cursor_y += shell_state.line_height + 3;
            fb_print(fb, pitch, 90, shell_state.cursor_y, "Example: copy file.txt backup.txt", 0x00CCCCCC);
            shell_state.cursor_y += shell_state.line_height + 3;
            return;
        }
        
        if (!fs_ensure_capacity(1)) {
            fb_print(fb, pitch, 70, shell_state.cursor_y, "Vault is full!", 0x00FF4444);
            shell_state.cursor_y += shell_state.line_height + 3;
            return;
        }
        
        /* Parse source and destination */
        char src[32], dest[32];
        int idx = 0;
        while (*args && *args != ' ' && idx < 31) {
            src[idx++] = *args++;
        }
        src[idx] = 0;
        
        while (*args == ' ') args++;
        
        idx = 0;
        while (*args && *args != ' ' && idx < 31) {
            dest[idx++] = *args++;
        }
        dest[idx] = 0;
        
        if (src[0] == 0 || dest[0] == 0) {
            fb_print(fb, pitch, 70, shell_state.cursor_y, "Usage: copy <source> <destination>", 0x00FFAA00);
            shell_state.cursor_y += shell_state.line_height + 3;
            return;
        }
        
        /* Find source file */
        int src_idx = -1;
        for (int i = 0; i < file_count; i++) {
            if (file_system[i].is_folder) continue;
            int match = 1;
            for (int j = 0; j < 32; j++) {
                if (src[j] != file_system[i].name[j]) {
                    match = 0;
                    break;
                }
                if (src[j] == 0) break;
            }
            if (match) {
                src_idx = i;
                break;
            }
        }
        
        if (src_idx < 0) {
            fb_print(fb, pitch, 70, shell_state.cursor_y, "Source scroll not found!", 0x00FF4444);
            shell_state.cursor_y += shell_state.line_height + 3;
            return;
        }
        
        /* Copy file */
        int d = 0;
        while (dest[d] && d < 31) {
            file_system[file_count].name[d] = dest[d];
            d++;
        }
        file_system[file_count].name[d] = 0;
        
        int c = 0;
        while (file_system[src_idx].content[c] && c < MAX_FILE_CONTENT - 1) {
            file_system[file_count].content[c] = file_system[src_idx].content[c];
            c++;
        }
        file_system[file_count].content[c] = 0;
        file_system[file_count].size = file_system[src_idx].size;
        file_system[file_count].is_folder = 0;
        
        int p = 0;
        while (current_directory[p] && p < 63) {
            file_system[file_count].parent[p] = current_directory[p];
            p++;
        }
        file_system[file_count].parent[p] = 0;
        
        file_count++;
        
        fb_print(fb, pitch, 70, shell_state.cursor_y, "Scroll duplicated!", 0x0088FF88);
        shell_state.cursor_y += shell_state.line_height + 3;
        return;
    }
    
    /* === FIND COMMAND === */
    if (cmd[0] == 'f' && cmd[1] == 'i' && cmd[2] == 'n' && cmd[3] == 'd') {
        char* pattern = cmd + 4;
        while (*pattern == ' ') pattern++;
        
        if ((pattern[0] == '-' && pattern[1] == 'h') || 
            (pattern[0] == '-' && pattern[1] == '-' && pattern[2] == 'h' && pattern[3] == 'e' && pattern[4] == 'l' && pattern[5] == 'p')) {
            fb_print(fb, pitch, 70, shell_state.cursor_y, "Find Command Usage:", 0x00FFFF00);
            shell_state.cursor_y += shell_state.line_height + 5;
            fb_print(fb, pitch, 90, shell_state.cursor_y, "find <pattern>  - Search for files by name", 0x00CCCCCC);
            shell_state.cursor_y += shell_state.line_height + 3;
            fb_print(fb, pitch, 90, shell_state.cursor_y, "Searches entire file system", 0x00CCCCCC);
            shell_state.cursor_y += shell_state.line_height + 3;
            fb_print(fb, pitch, 90, shell_state.cursor_y, "Example: find readme", 0x00CCCCCC);
            shell_state.cursor_y += shell_state.line_height + 3;
            return;
        }
        
        if (pattern[0] == 0) {
            fb_print(fb, pitch, 70, shell_state.cursor_y, "Usage: find <pattern>", 0x00FFAA00);
            shell_state.cursor_y += shell_state.line_height + 3;
            return;
        }
        
        int found_any = 0;
        for (int i = 0; i < file_count; i++) {
            /* Simple substring match */
            int match = 0;
            for (int j = 0; file_system[i].name[j]; j++) {
                int sub_match = 1;
                int k = 0;
                while (pattern[k]) {
                    if (file_system[i].name[j + k] != pattern[k]) {
                        sub_match = 0;
                        break;
                    }
                    k++;
                }
                if (sub_match && pattern[0]) {
                    match = 1;
                    break;
                }
            }
            
            if (match) {
                found_any = 1;
                fb_print(fb, pitch, 70, shell_state.cursor_y, file_system[i].parent, 0x00888888);
                fb_print(fb, pitch, 70 + (32 * 8), shell_state.cursor_y, "/", 0x00888888);
                fb_print(fb, pitch, 70 + (33 * 8), shell_state.cursor_y, file_system[i].name, 0x0088FF88);
                if (file_system[i].is_folder) {
                    fb_print(fb, pitch, 70 + (65 * 8), shell_state.cursor_y, "/", 0x0088CCFF);
                }
                shell_state.cursor_y += shell_state.line_height + 3;
            }
        }
        
        if (!found_any) {
            fb_print(fb, pitch, 70, shell_state.cursor_y, "No scrolls found matching pattern", 0x00FF9999);
            shell_state.cursor_y += shell_state.line_height + 3;
        }
        return;
    }
    
    /* === TREE COMMAND === */
    if (cmd[0] == 't' && cmd[1] == 'r' && cmd[2] == 'e' && cmd[3] == 'e') {
        char* arg = cmd + 4;
        while (*arg == ' ') arg++;
        
        if ((arg[0] == '-' && arg[1] == 'h') || 
            (arg[0] == '-' && arg[1] == '-' && arg[2] == 'h' && arg[3] == 'e' && arg[4] == 'l' && arg[5] == 'p')) {
            fb_print(fb, pitch, 70, shell_state.cursor_y, "Tree Command Usage:", 0x00FFFF00);
            shell_state.cursor_y += shell_state.line_height + 5;
            fb_print(fb, pitch, 90, shell_state.cursor_y, "tree  - Display directory tree structure", 0x00CCCCCC);
            shell_state.cursor_y += shell_state.line_height + 3;
            fb_print(fb, pitch, 90, shell_state.cursor_y, "Shows all files and folders in hierarchy", 0x00CCCCCC);
            shell_state.cursor_y += shell_state.line_height + 3;
            return;
        }
        
        fb_print(fb, pitch, 70, shell_state.cursor_y, "Directory Tree:", 0x00FFFF00);
        shell_state.cursor_y += shell_state.line_height + 5;
        
        /* Display tree starting from root */
        for (int i = 0; i < file_count; i++) {
            int depth = 0;
            for (int j = 0; file_system[i].parent[j]; j++) {
                if (file_system[i].parent[j] == '/') depth++;
            }
            
            unsigned int indent = 90 + (depth * 16);
            fb_print(fb, pitch, indent, shell_state.cursor_y, file_system[i].name, 0x0088FF88);
            if (file_system[i].is_folder) {
                fb_print(fb, pitch, indent + (32 * 8), shell_state.cursor_y, "/", 0x0088CCFF);
            }
            shell_state.cursor_y += shell_state.line_height + 2;
        }
        shell_state.cursor_y += shell_state.line_height + 3;
        return;
    }
    
    /* === STATUS COMMAND === */
    if (cmd[0] == 's' && cmd[1] == 't' && cmd[2] == 'a' && cmd[3] == 't') {
        char* arg = cmd + 4;
        while (*arg == ' ') arg++;
        if ((arg[0] == 'u' && arg[1] == 's') || 
            ((arg[0] == '-' && arg[1] == 'h') || 
             (arg[0] == '-' && arg[1] == '-' && arg[2] == 'h' && arg[3] == 'e' && arg[4] == 'l' && arg[5] == 'p'))) {
            fb_print(fb, pitch, 70, shell_state.cursor_y, "Status Command Usage:", 0x00FFFF00);
            shell_state.cursor_y += shell_state.line_height + 5;
            fb_print(fb, pitch, 90, shell_state.cursor_y, "status  - Show system vitals and current path", 0x00CCCCCC);
            shell_state.cursor_y += shell_state.line_height + 3;
            fb_print(fb, pitch, 90, shell_state.cursor_y, "Displays: User, display info, shell, file system", 0x00CCCCCC);
            shell_state.cursor_y += shell_state.line_height + 3;
            return;
        }
        fb_print(fb, pitch, 70, shell_state.cursor_y, "~ The Kingdom's Vitals ~", 0x0088FF88);
        shell_state.cursor_y += shell_state.line_height + 5;
        fb_print(fb, pitch, 90, shell_state.cursor_y, "Keeper: Awakened and Wandering", 0x00CCCCCC);
        shell_state.cursor_y += shell_state.line_height + 3;
        fb_print(fb, pitch, 90, shell_state.cursor_y, "Display: GPU Framebuffer (1280x800)", 0x00CCCCCC);
        shell_state.cursor_y += shell_state.line_height + 3;
        fb_print(fb, pitch, 90, shell_state.cursor_y, "Shell: Unified with auto-scrolling", 0x00CCCCCC);
        shell_state.cursor_y += shell_state.line_height + 3;
        fb_print(fb, pitch, 90, shell_state.cursor_y, "File System: /home based structure", 0x00CCCCCC);
        shell_state.cursor_y += shell_state.line_height + 3;
        fb_print(fb, pitch, 90, shell_state.cursor_y, "Current User: ", 0x00CCCCCC);
        fb_print(fb, pitch, 90 + (14 * 8), shell_state.cursor_y, current_user, 0x0088FFFF);
        shell_state.cursor_y += shell_state.line_height + 3;
        fb_print(fb, pitch, 90, shell_state.cursor_y, "Current Path: ", 0x00CCCCCC);
        fb_print(fb, pitch, 90 + (14 * 8), shell_state.cursor_y, current_directory, 0x0088FFFF);
        shell_state.cursor_y += shell_state.line_height + 3;
        return;
    }
    
    /* === WHOAMI COMMAND === */
    if (cmd[0] == 'w' && cmd[1] == 'h' && cmd[2] == 'o' && cmd[3] == 'a' && cmd[4] == 'm' && cmd[5] == 'i') {
        char* arg = cmd + 6;
        while (*arg == ' ') arg++;
        if ((arg[0] == '-' && arg[1] == 'h') || 
            (arg[0] == '-' && arg[1] == '-' && arg[2] == 'h' && arg[3] == 'e' && arg[4] == 'l' && arg[5] == 'p')) {
            fb_print(fb, pitch, 70, shell_state.cursor_y, "Whoami Command Usage:", 0x00FFFF00);
            shell_state.cursor_y += shell_state.line_height + 5;
            fb_print(fb, pitch, 90, shell_state.cursor_y, "whoami  - Display current user and role", 0x00CCCCCC);
            shell_state.cursor_y += shell_state.line_height + 3;
            return;
        }
        char whoami_msg[96];
        char* ptr = whoami_msg;
        
        const char* prefix = "Thou art known as: ";
        while (*prefix) *ptr++ = *prefix++;
        
        const char* name = current_user;
        while (*name) *ptr++ = *name++;
        *ptr = 0;
        
        fb_print(fb, pitch, 70, shell_state.cursor_y, whoami_msg, 0x00FFFF00);
        shell_state.cursor_y += shell_state.line_height + 3;
        
        if (current_user[0] == 'r' && current_user[1] == 'o' && current_user[2] == 'o' && current_user[3] == 't') {
            fb_print(fb, pitch, 70, shell_state.cursor_y, "Thy power: ABSOLUTE - The Realm bends to thy will", 0x00FFAA00);
        } else {
            fb_print(fb, pitch, 70, shell_state.cursor_y, "Thy power: A seeker with growing influence", 0x00AAFF00);
        }
        shell_state.cursor_y += shell_state.line_height + 3;
        return;
    }
    
    /* === USERADD COMMAND === */
    if (cmd[0] == 'u' && cmd[1] == 's' && cmd[2] == 'e' && cmd[3] == 'r' && 
        cmd[4] == 'a' && cmd[5] == 'd' && cmd[6] == 'd') {
        char* username = cmd + 7;
        while (*username == ' ') username++;
        
        if ((username[0] == '-' && username[1] == 'h') || 
            (username[0] == '-' && username[1] == '-' && username[2] == 'h' && username[3] == 'e' && username[4] == 'l' && username[5] == 'p')) {
            fb_print(fb, pitch, 70, shell_state.cursor_y, "Useradd Command Usage:", 0x00FFFF00);
            shell_state.cursor_y += shell_state.line_height + 5;
            fb_print(fb, pitch, 90, shell_state.cursor_y, "useradd <name>  - Create new user with home dir", 0x00CCCCCC);
            shell_state.cursor_y += shell_state.line_height + 3;
            fb_print(fb, pitch, 90, shell_state.cursor_y, "Default password: welcome", 0x00CCCCCC);
            shell_state.cursor_y += shell_state.line_height + 3;
            fb_print(fb, pitch, 90, shell_state.cursor_y, "Creates: /home/<name> folder automatically", 0x00CCCCCC);
            shell_state.cursor_y += shell_state.line_height + 3;
            return;
        }
        
        if (user_count >= MAX_USERS) {
            fb_print(fb, pitch, 70, shell_state.cursor_y, "Realm is full!", 0x00FF4444);
            shell_state.cursor_y += shell_state.line_height + 3;
            return;
        }
        
        if (!username || username[0] == 0) {
            fb_print(fb, pitch, 70, shell_state.cursor_y, "Usage: useradd <username>", 0x00FFAA00);
            shell_state.cursor_y += shell_state.line_height + 3;
            return;
        }
        
        /* Check if user exists */
        for (int i = 0; i < user_count; i++) {
            int match = 1;
            for (int j = 0; j < 32 && username[j] && users[i].username[j]; j++) {
                if (username[j] != users[i].username[j]) {
                    match = 0;
                    break;
                }
            }
            if (match && users[i].username[0] != 0) {
                fb_print(fb, pitch, 70, shell_state.cursor_y, "Seeker already exists!", 0x00FF9999);
                shell_state.cursor_y += shell_state.line_height + 3;
                return;
            }
        }
        
        /* Create user */
        int name_len = 0;
        while (username[name_len] && username[name_len] != ' ' && name_len < 31) {
            users[user_count].username[name_len] = username[name_len];
            name_len++;
        }
        users[user_count].username[name_len] = 0;
        
        const char* default_pass = "welcome";
        int pass_len = 0;
        while (default_pass[pass_len] && pass_len < 31) {
            users[user_count].password[pass_len] = default_pass[pass_len];
            pass_len++;
        }
        users[user_count].password[pass_len] = 0;
        
        /* Create home directory for new user */
        if (!fs_ensure_capacity(1)) {
            fb_print(fb, pitch, 70, shell_state.cursor_y, "Vault is full!", 0x00FF4444);
            shell_state.cursor_y += shell_state.line_height + 3;
            return;
        }

        /* Add home folder for user */
        int u = 0;
        while (users[user_count].username[u] && u < 31) {
            file_system[file_count].name[u] = users[user_count].username[u];
            u++;
        }
        file_system[file_count].name[u] = 0;
        file_system[file_count].content[0] = 0;
        file_system[file_count].size = 0;
        file_system[file_count].is_folder = 1;
        file_system[file_count].parent[0] = '/';
        file_system[file_count].parent[1] = 'h';
        file_system[file_count].parent[2] = 'o';
        file_system[file_count].parent[3] = 'm';
        file_system[file_count].parent[4] = 'e';
        file_system[file_count].parent[5] = 0;
        file_count++;
        
        user_count++;
        
        char success_msg[80];
        char* m = success_msg;
        const char* msg_prefix = "New seeker arrived: ";
        while (*msg_prefix) *m++ = *msg_prefix++;
        int j = 0;
        while (users[user_count - 1].username[j]) *m++ = users[user_count - 1].username[j++];
        const char* home_msg = " (home created)";
        while (*home_msg) *m++ = *home_msg++;
        *m = 0;
        
        fb_print(fb, pitch, 70, shell_state.cursor_y, success_msg, 0x0088FF88);
        shell_state.cursor_y += shell_state.line_height + 3;
        return;
    }
    
    /* === LOGIN COMMAND === */
    if (cmd[0] == 'l' && cmd[1] == 'o' && cmd[2] == 'g' && cmd[3] == 'i' && cmd[4] == 'n') {
        char* username = cmd + 5;
        while (*username == ' ') username++;
        
        if ((username[0] == '-' && username[1] == 'h') || 
            (username[0] == '-' && username[1] == '-' && username[2] == 'h' && username[3] == 'e' && username[4] == 'l' && username[5] == 'p')) {
            fb_print(fb, pitch, 70, shell_state.cursor_y, "Login Command Usage:", 0x00FFFF00);
            shell_state.cursor_y += shell_state.line_height + 5;
            fb_print(fb, pitch, 90, shell_state.cursor_y, "login <user>  - Switch to different user", 0x00CCCCCC);
            shell_state.cursor_y += shell_state.line_height + 3;
            fb_print(fb, pitch, 90, shell_state.cursor_y, "Auto switches to user's home directory", 0x00CCCCCC);
            shell_state.cursor_y += shell_state.line_height + 3;
            return;
        }
        
        if (!username || username[0] == 0) {
            fb_print(fb, pitch, 70, shell_state.cursor_y, "Usage: login <username>", 0x00FFAA00);
            shell_state.cursor_y += shell_state.line_height + 3;
            return;
        }
        
        int found = -1;
        for (int i = 0; i < user_count; i++) {
            int match = 1;
            for (int j = 0; j < 32 && username[j] && users[i].username[j]; j++) {
                if (username[j] != users[i].username[j]) {
                    match = 0;
                    break;
                }
            }
            if (match && users[i].username[0] != 0) {
                found = i;
                break;
            }
        }
        
        if (found >= 0) {
            int name_len = 0;
            while (users[found].username[name_len] && name_len < 31) {
                current_user[name_len] = users[found].username[name_len];
                name_len++;
            }
            current_user[name_len] = 0;
            
            /* Switch to user's home directory */
            current_directory[0] = '/';
            current_directory[1] = 'h';
            current_directory[2] = 'o';
            current_directory[3] = 'm';
            current_directory[4] = 'e';
            current_directory[5] = '/';
            int idx = 6;
            for (int k = 0; k < name_len && idx < 62; k++) {
                current_directory[idx++] = current_user[k];
            }
            current_directory[idx] = 0;
            
            char welcome_msg[96];
            char* w = welcome_msg;
            const char* greet = "Welcome, ";
            while (*greet) *w++ = *greet++;
            name_len = 0;
            while (current_user[name_len]) *w++ = current_user[name_len++];
            *w = 0;
            
            fb_print(fb, pitch, 70, shell_state.cursor_y, welcome_msg, 0x0088FF88);
            shell_state.cursor_y += shell_state.line_height + 3;
        } else {
            fb_print(fb, pitch, 70, shell_state.cursor_y, "Seeker does not exist!", 0x00FF4444);
            shell_state.cursor_y += shell_state.line_height + 3;
        }
        return;
    }
    
    /* === UNKNOWN COMMAND === */
    fb_print(fb, pitch, 70, shell_state.cursor_y, "Unknown incantation... Type 'help' for spells.", 0x00FF4444);
    shell_state.cursor_y += shell_state.line_height + 3;
}

/* Initialize shell */
void shell_init(void) {
    fs_init();
}

/* Main framebuffer shell */
void shell_run(void) {
    /* This is now redirected to fb_shell_run from main.c */
}

/* Main framebuffer shell - called from main.c */
void fb_shell_run(BOOT_INFO* boot_info) {
    if (!boot_info || boot_info->framebuffer_addr == 0) {
        serial_write("ERROR: No framebuffer available for shell!\n");
        return;
    }

    fs_init();
    
    unsigned int* fb = (unsigned int*)(unsigned long)boot_info->framebuffer_addr;
    unsigned int pitch = boot_info->framebuffer_pitch;
    unsigned int width = boot_info->framebuffer_width;
    unsigned int height = boot_info->framebuffer_height;
    
    /* Clear framebuffer */
    for (unsigned int i = 0; i < (width * height); i++) {
        fb[i] = 0x000000;
    }
    
    /* Display large centered ASCII art logo */
    int scale = 2;
    int logo_width = 40 * 8 * scale;
    int logo_height = KAGAMI_LOGO_LINES * 28;
    unsigned int center_x = (width > (unsigned int)logo_width) ? (width - (unsigned int)logo_width) / 2 : 20;
    unsigned int start_y = (height > (unsigned int)logo_height) ? (height - (unsigned int)logo_height) / 4 : 150;

    for (int i = 0; i < KAGAMI_LOGO_LINES; i++) {
        fb_print_scaled(fb, pitch, center_x, start_y, kagami_logo[i], 0x00FF00FF, scale);
        start_y += 28;
    }
    start_y += 22;
    
    /* Fantasy welcome message - centered */
    fb_print_scaled(fb, pitch, center_x + 50, start_y, "~ The Mirror Awakens With Power ~", 0x00FFFF00, 2);
    start_y += 35;
    fb_print_scaled(fb, pitch, center_x + 25, start_y, "Enter Your Spells and Command", 0x00AAAAFF, 1);
    start_y += 25;
    fb_print(fb, pitch, center_x + 120, start_y, "Type 'help' or 'logo' to begin", 0x0088FFAA);
    start_y += 35;
    
    /* Dividing line */
    fb_print(fb, pitch, 40, start_y, "================================================================================", 0x0088FF88);
    start_y += 25;
    
    /* Initialize shell state */
    shell_state.pos = 0;
    shell_state.buffer[0] = 0;
    shell_state.cursor_x = 50;
    shell_state.cursor_y = start_y;
    shell_state.line_height = 18;
    shell_state.shift_pressed = 0;
    shell_state.scroll_offset = 0;
    
    serial_write("Unified framebuffer shell started with directory support\n");
    
    /* Main shell loop */
    while (1) {
        /* Build dynamic prompt with current directory */
        char prompt[64];
        get_dir_prompt(prompt);
        
        /* Render prompt and input */
        render_input(fb, pitch, width, prompt);
        
        /* Get character from keyboard */
        char c = get_keyboard_char();
        
        /* Handle input */
        if (c == '\n') {
            /* Execute command */
            serial_write("Command: ");
            serial_write(shell_state.buffer);
            serial_write("\n");
            
            execute_command(fb, pitch, width, height);
            
            /* Reset input buffer and move to next line */
            shell_state.buffer[0] = 0;
            shell_state.pos = 0;
            shell_state.cursor_y += shell_state.line_height + 5;
            
            /* Implement scrolling when reaching bottom of screen */
            if (shell_state.cursor_y > height - 80) {
                /* Scroll up by copying framebuffer content upward */
                unsigned int scroll_lines = 100;  /* Pixels to scroll */
                unsigned int* fb_ptr = fb;
                
                /* Copy each line up by scroll_lines pixels */
                for (unsigned int y = scroll_lines; y < height; y++) {
                    for (unsigned int x = 0; x < width; x++) {
                        fb_ptr[(y - scroll_lines) * width + x] = fb_ptr[y * width + x];
                    }
                }
                
                /* Clear the bottom portion that was scrolled up */
                for (unsigned int y = height - scroll_lines; y < height; y++) {
                    for (unsigned int x = 0; x < width; x++) {
                        fb_ptr[y * width + x] = 0x000000;
                    }
                }
                
                /* Adjust cursor position */
                shell_state.cursor_y -= scroll_lines;
                shell_state.scroll_offset += scroll_lines;
            }
        } else if (c == '\b') {
            /* Backspace */
            if (shell_state.pos > 0) {
                shell_state.pos--;
                shell_state.buffer[shell_state.pos] = 0;
            }
        } else if (c > 0 && shell_state.pos < 254) {
            /* Add character to buffer */
            shell_state.buffer[shell_state.pos] = c;
            shell_state.pos++;
            shell_state.buffer[shell_state.pos] = 0;
        }
    }
}
