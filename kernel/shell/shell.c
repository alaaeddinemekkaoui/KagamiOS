#include "shell.h"
#include "include/framebuffer.h"
#include "include/serial.h"
#include "drivers/keyboard.h"
#include "boot_info.h"

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
typedef struct {
    char name[32];
    char content[256];
    int size;
    int is_folder;  /* 1 = folder, 0 = file */
    char parent[64]; /* Parent directory path */
} VirtualFile;

static VirtualFile file_system[] = {
    {"home", "", 0, 1, "/"},
    {"root", "", 0, 1, "/home"},
    {"readme.txt", "Welcome to Kagami OS! A magical realm of code.\nType 'ls' to explore.", 65, 0, "/home/root"},
    {"welcome.txt", "You have entered the Realm of Kagami.\nMay your code be swift and bug-free.", 70, 0, "/home/root"},
    {"spellbook.txt", "Available Spells:\n- help: Reveal all incantations\n- logo: Display realm emblem", 82, 0, "/home/root"},
    {"documents", "", 0, 1, "/home/root"},
    {"secret.txt", "The wizard guardian of this realm welcomes you!", 47, 0, "/home/root/documents"},
    {"", "", 0, 0, ""}
};

#define MAX_FILES 30
static int file_count = 7;

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
    unsigned char scancode;
    int shift_pressed = 0;
    
    while (1) {
        scancode = poll_keyboard();
        
        if (scancode > 0) {
            /* Handle shift key */
            if (scancode == SC_LSHIFT || scancode == SC_RSHIFT) {
                shift_pressed = 1;
                continue;
            }
            if (scancode == 0xAA || scancode == 0xB6) {
                shift_pressed = 0;
                continue;
            }
            
            /* Ignore key releases */
            if (scancode & 0x80) {
                continue;
            }
            
            /* Handle backspace */
            if (scancode == SC_BACKSPACE) {
                return '\b';
            }
            
            /* Handle enter */
            if (scancode == 0x1C) {
                return '\n';
            }
            
            /* Convert to ASCII */
            char c = scancode_to_char(scancode, shift_pressed);
            if (c > 0) {
                return c;
            }
        }
        
        /* Small delay to avoid busy-waiting */
        for (volatile int i = 0; i < 1000; i++);
    }
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
        fb_print(fb, pitch, 70, shell_state.cursor_y, "~ Spellbook of Incantations ~", 0x00FFFF);
        shell_state.cursor_y += shell_state.line_height + 5;
        fb_print(fb, pitch, 90, shell_state.cursor_y, "help       - Display mystical guide", 0x00CCCCCC);
        shell_state.cursor_y += shell_state.line_height + 3;
        fb_print(fb, pitch, 90, shell_state.cursor_y, "logo       - Display realm emblem & info", 0x00CCCCCC);
        shell_state.cursor_y += shell_state.line_height + 3;
        fb_print(fb, pitch, 90, shell_state.cursor_y, "ls         - List files (5 per row, folders marked /)", 0x00CCCCCC);
        shell_state.cursor_y += shell_state.line_height + 3;
        fb_print(fb, pitch, 90, shell_state.cursor_y, "cd <folder> - Enter sacred chamber", 0x00CCCCCC);
        shell_state.cursor_y += shell_state.line_height + 3;
        fb_print(fb, pitch, 90, shell_state.cursor_y, "cat <file> - Read scroll contents", 0x00CCCCCC);
        shell_state.cursor_y += shell_state.line_height + 3;
        fb_print(fb, pitch, 90, shell_state.cursor_y, "touch <file> - Create new scroll", 0x00CCCCCC);
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
        fb_print(fb, pitch, 90, shell_state.cursor_y, "clear      - Refresh realm", 0x00CCCCCC);
        shell_state.cursor_y += shell_state.line_height + 3;
        return;
    }
    
    /* === CLEAR COMMAND === */
    if (cmd[0] == 'c' && cmd[1] == 'l' && cmd[2] == 'e' && cmd[3] == 'a' && cmd[4] == 'r') {
        /* Clear framebuffer */
        unsigned int* fb_start = fb;
        for (unsigned int i = 0; i < (width * height); i++) {
            fb_start[i] = 0x000000;
        }
        
        /* Redraw minimal header */
        fb_print(fb, pitch, 20, 10, "KAGAMI OS - Type 'logo' for info", 0x0088FF88);
        fb_print(fb, pitch, 20, 30, "=============================================", 0x0055AA55);
        
        /* Reset cursor and scrolling */
        shell_state.cursor_y = 50;
        shell_state.scroll_offset = 0;
        shell_state.pos = 0;
        shell_state.buffer[0] = 0;
        return;
    }
    
    /* === LOGO COMMAND === */
    if (cmd[0] == 'l' && cmd[1] == 'o' && cmd[2] == 'g' && cmd[3] == 'o') {
        fb_print_scaled(fb, pitch, 150, shell_state.cursor_y, "  _  __   _    ____    _    __  __ ___", 0x00FF00FF, 2);
        shell_state.cursor_y += 28;
        fb_print_scaled(fb, pitch, 150, shell_state.cursor_y, " | |/ /  / \\  / ___|  / \\  |  \\/  |_ _|", 0x00FF00FF, 2);
        shell_state.cursor_y += 28;
        fb_print_scaled(fb, pitch, 150, shell_state.cursor_y, " | ' /  / _ \\| |  _  / _ \\ | |\\/| || |", 0x00FF00FF, 2);
        shell_state.cursor_y += 28;
        fb_print_scaled(fb, pitch, 150, shell_state.cursor_y, " | . \\ / ___ \\ |_| |/ ___ \\| |  | || |", 0x00FF00FF, 2);
        shell_state.cursor_y += 28;
        fb_print_scaled(fb, pitch, 150, shell_state.cursor_y, " |_|\\_\\_/   \\_\\____/_/   \\_\\_|  |_|___|", 0x00FF00FF, 2);
        shell_state.cursor_y += 40;
        fb_print_scaled(fb, pitch, 250, shell_state.cursor_y, "K A G A M I   O S", 0x00FFFF00, 2);
        shell_state.cursor_y += 30;
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
        int dir_files = 0;
        for (int i = 0; i < file_count; i++) {
            /* Check if file is in current directory */
            int match = 1;
            for (int j = 0; current_directory[j] && file_system[i].parent[j]; j++) {
                if (current_directory[j] != file_system[i].parent[j]) {
                    match = 0;
                    break;
                }
            }
            if (match && current_directory[0] == file_system[i].parent[0]) {
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
                /* Check if file is in current directory */
                int match = 1;
                for (int j = 0; current_directory[j] && file_system[i].parent[j]; j++) {
                    if (current_directory[j] != file_system[i].parent[j]) {
                        match = 0;
                        break;
                    }
                }
                if (match && current_directory[0] == file_system[i].parent[0]) {
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
    
    /* === CD COMMAND (change directory) === */
    if (cmd[0] == 'c' && cmd[1] == 'd') {
        char* dirname = cmd + 2;
        while (*dirname == ' ') dirname++;
        
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
    
    /* === CAT COMMAND (read file) === */
    if (cmd[0] == 'c' && cmd[1] == 'a' && cmd[2] == 't') {
        char* filename = cmd + 3;
        while (*filename == ' ') filename++;
        
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
                } else {
                    fb_print(fb, pitch, 70, shell_state.cursor_y, file_system[i].content, 0x0088FFFF);
                }
                shell_state.cursor_y += shell_state.line_height + 3;
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
    
    /* === TOUCH COMMAND (create file) === */
    if (cmd[0] == 't' && cmd[1] == 'o' && cmd[2] == 'u' && cmd[3] == 'c' && cmd[4] == 'h') {
        if (file_count >= MAX_FILES) {
            fb_print(fb, pitch, 70, shell_state.cursor_y, "Vault is full!", 0x00FF4444);
            shell_state.cursor_y += shell_state.line_height + 3;
            return;
        }
        
        char* filename = cmd + 5;
        while (*filename == ' ') filename++;
        
        if (filename[0] == 0) {
            fb_print(fb, pitch, 70, shell_state.cursor_y, "Usage: touch <filename>", 0x00FFAA00);
            shell_state.cursor_y += shell_state.line_height + 3;
            return;
        }
        
        /* Create new file in current directory */
        int name_len = 0;
        while (filename[name_len] && filename[name_len] != ' ' && name_len < 31) {
            file_system[file_count].name[name_len] = filename[name_len];
            name_len++;
        }
        file_system[file_count].name[name_len] = 0;
        file_system[file_count].content[0] = 0;
        file_system[file_count].size = 0;
        file_system[file_count].is_folder = 0;
        
        /* Set parent directory */
        int j = 0;
        while (current_directory[j] && j < 31) {
            file_system[file_count].parent[j] = current_directory[j];
            j++;
        }
        file_system[file_count].parent[j] = 0;
        
        file_count++;
        
        fb_print(fb, pitch, 70, shell_state.cursor_y, "New scroll inscribed!", 0x0088FF88);
        shell_state.cursor_y += shell_state.line_height + 3;
        return;
    }
    
    /* === RM COMMAND (delete file) === */
    if (cmd[0] == 'r' && cmd[1] == 'm' && cmd[2] == ' ') {
        char* filename = cmd + 3;
        while (*filename == ' ') filename++;
        
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
        
        if (*text) {
            fb_print(fb, pitch, 70, shell_state.cursor_y, text, 0x00FF00);
        }
        shell_state.cursor_y += shell_state.line_height + 3;
        return;
    }
    
    /* === STATUS COMMAND === */
    if (cmd[0] == 's' && cmd[1] == 't' && cmd[2] == 'a' && cmd[3] == 't') {
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
        fb_print(fb, pitch, 90, shell_state.cursor_y, "Current Path: ", 0x00CCCCCC);
        fb_print(fb, pitch, 90 + (14 * 8), shell_state.cursor_y, current_directory, 0x0088FFFF);
        shell_state.cursor_y += shell_state.line_height + 3;
        return;
    }
    
    /* === WHOAMI COMMAND === */
    if (cmd[0] == 'w' && cmd[1] == 'h' && cmd[2] == 'o' && cmd[3] == 'a' && cmd[4] == 'm' && cmd[5] == 'i') {
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
        if (user_count >= MAX_USERS) {
            fb_print(fb, pitch, 70, shell_state.cursor_y, "Realm is full!", 0x00FF4444);
            shell_state.cursor_y += shell_state.line_height + 3;
            return;
        }
        
        char* username = cmd + 7;
        while (*username == ' ') username++;
        
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
        if (file_count < MAX_FILES - 1) {
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
        }
        
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
    /* Initialization if needed */
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
    
    unsigned int* fb = (unsigned int*)(unsigned long)boot_info->framebuffer_addr;
    unsigned int pitch = boot_info->framebuffer_pitch;
    unsigned int width = boot_info->framebuffer_width;
    unsigned int height = boot_info->framebuffer_height;
    
    /* Clear framebuffer */
    for (unsigned int i = 0; i < (width * height); i++) {
        fb[i] = 0x000000;
    }
    
    /* Display large centered ASCII art logo */
    unsigned int center_x = (width / 2) - 350;
    unsigned int start_y = 150;
    
    fb_print_scaled(fb, pitch, center_x, start_y, "  _  __   _    ____    _    __  __ ___", 0x00FF00FF, 2);
    start_y += 28;
    fb_print_scaled(fb, pitch, center_x, start_y, " | |/ /  / \\  / ___|  / \\  |  \\/  |_ _|", 0x00FF00FF, 2);
    start_y += 28;
    fb_print_scaled(fb, pitch, center_x, start_y, " | ' /  / _ \\| |  _  / _ \\ | |\\/| || |", 0x00FF00FF, 2);
    start_y += 28;
    fb_print_scaled(fb, pitch, center_x, start_y, " | . \\ / ___ \\ |_| |/ ___ \\| |  | || |", 0x00FF00FF, 2);
    start_y += 28;
    fb_print_scaled(fb, pitch, center_x, start_y, " |_|\\_\\_/   \\_\\____/_/   \\_\\_|  |_|___|", 0x00FF00FF, 2);
    start_y += 50;
    
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
