#include "shell.h"
#include "vga.h"
#include "boot_info.h"
#include "keyboard.h"
#include "vga_terminal.h"
#include "heap.h"

static SHELL_CONTEXT shell_ctx = {
    .input_pos = 0,
    .history_pos = 0,
    .history_count = 0,
    .running = 1
};

/* Command table */
static SHELL_COMMAND commands[] = {
    {"help", "Show available commands", cmd_help},
    {"clear", "Clear the screen", cmd_clear},
    {"status", "Show system status", cmd_status},
    {"bootinfo", "Show boot information", cmd_bootinfo},
    {"echo", "Echo text to screen", cmd_echo},
    {"meminfo", "Show memory information", cmd_meminfo},
    {"reboot", "Reboot the system", cmd_reboot},
};

#define NUM_COMMANDS (sizeof(commands) / sizeof(SHELL_COMMAND))

/* PS/2 scancode to ASCII conversion (US layout, simplified) */
static const char scancode_to_ascii[] = {
    0, 0, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', 
    '\b', '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']',
    '\n', 0, 'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`',
    0, '\\', 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0, '*',
    0, ' ', 0
};

/* Clear screen */
void cmd_clear(char* args) {
    (void)args;
    terminal_clear();
}

/* Show available commands */
void cmd_help(char* args) {
    (void)args;
    const uint8_t color = (VGA_COLOR_BLACK << 4) | VGA_COLOR_LIGHT_CYAN;
    
    terminal_set_color(color);
    terminal_write("Available commands:\n");
    
    for (size_t i = 0; i < NUM_COMMANDS; i++) {
        terminal_write("  ");
        terminal_write(commands[i].name);
        terminal_write(" - ");
        terminal_write(commands[i].help);
        terminal_write("\n");
    }
}

/* Show system status */
void cmd_status(char* args) {
    (void)args;
    const uint8_t color = (VGA_COLOR_BLACK << 4) | VGA_COLOR_LIGHT_GREEN;
    
    terminal_set_color(color);
    terminal_write("System Status:\n");
    terminal_write("  Kernel: OPERATIONAL\n");
    terminal_write("  Interrupts: ENABLED\n");
    terminal_write("  Memory: 64 MB\n");
    terminal_write("  Display: 80x25 VGA Text Mode\n");
}

/* Show boot information */
void cmd_bootinfo(char* args) {
    (void)args;
    const uint8_t color = (VGA_COLOR_BLACK << 4) | VGA_COLOR_LIGHT_MAGENTA;
    const uint8_t error_color = (VGA_COLOR_BLACK << 4) | VGA_COLOR_LIGHT_RED;
    
    terminal_set_color(color);
    terminal_write("Boot Information:\n");
    
    if (!boot_info_valid()) {
        terminal_set_color(error_color);
        terminal_write("ERROR: Boot info not available\n");
        return;
    }
    
    BOOT_INFO* info = get_boot_info();
    
    char buf[80];
    
    /* Boot drive */
    buf[0] = ' ';
    buf[1] = ' ';
    buf[2] = 'D';
    buf[3] = 'r';
    buf[4] = 'i';
    buf[5] = 'v';
    buf[6] = 'e';
    buf[7] = ':';
    buf[8] = ' ';
    buf[9] = '0';
    buf[10] = 'x';
    uint32_t drive = info->boot_drive;
    buf[11] = (drive >> 4) < 10 ? '0' + (drive >> 4) : 'A' + (drive >> 4) - 10;
    buf[12] = (drive & 0x0F) < 10 ? '0' + (drive & 0x0F) : 'A' + (drive & 0x0F) - 10;
    buf[13] = '\0';
    terminal_write(buf);
    terminal_write("\n");
    
    /* Memory size */
    buf[0] = ' ';
    buf[1] = ' ';
    buf[2] = 'M';
    buf[3] = 'e';
    buf[4] = 'm';
    buf[5] = ':';
    buf[6] = ' ';
    uint32_t mem_kb = info->memory_size_kb;
    uint32_t mem_mb = mem_kb / 1024;
    
    size_t pos = 7;
    if (mem_mb >= 100) {
        buf[pos++] = '0' + (mem_mb / 100);
        buf[pos++] = '0' + ((mem_mb / 10) % 10);
        buf[pos++] = '0' + (mem_mb % 10);
    } else if (mem_mb >= 10) {
        buf[pos++] = '0' + (mem_mb / 10);
        buf[pos++] = '0' + (mem_mb % 10);
    } else {
        buf[pos++] = '0' + mem_mb;
    }
    buf[pos++] = ' ';
    buf[pos++] = 'M';
    buf[pos++] = 'B';
    buf[pos] = '\0';
    terminal_write(buf);
    terminal_write("\n");
    
    /* Bootloader type */
    const char* bootloader = "BIOS Stage2";
    if (info->bootloader_type == 1) {
        bootloader = "UEFI";
    }
    
    buf[0] = ' ';
    buf[1] = ' ';
    buf[2] = 'B';
    buf[3] = 'o';
    buf[4] = 'o';
    buf[5] = 't';
    buf[6] = 'l';
    buf[7] = 'o';
    buf[8] = 'a';
    buf[9] = 'd';
    buf[10] = 'e';
    buf[11] = 'r';
    buf[12] = ':';
    buf[13] = ' ';
    const char* src = bootloader;
    pos = 14;
    while (*src && pos < 75) {
        buf[pos++] = *src++;
    }
    buf[pos] = '\0';
    terminal_write(buf);
    terminal_write("\n");
}

void shell_init(void) {
    shell_ctx.input_pos = 0;
    shell_ctx.running = 1;
}

void shell_run(void) {
    const uint8_t prompt_color = (VGA_COLOR_BLACK << 4) | VGA_COLOR_LIGHT_CYAN;
    const uint8_t input_color = (VGA_COLOR_BLACK << 4) | VGA_COLOR_WHITE;
    
    /* Initialize terminal */
    terminal_clear();
    terminal_set_color(prompt_color);
    terminal_write("\n  Kagami OS Interactive Shell\n");
    terminal_write("  Type 'help' for available commands\n\n");
    
    shell_ctx.running = 1;
    
    while (shell_ctx.running) {
        /* Print prompt */
        terminal_set_color(prompt_color);
        terminal_write("kagami> ");
        terminal_set_color(input_color);
        
        /* Initialize input buffer */
        shell_ctx.input_pos = 0;
        for (size_t i = 0; i < SHELL_COMMAND_MAX; i++) {
            shell_ctx.input_buffer[i] = 0;
        }
        
        /* Read command from keyboard */
        while (1) {
            uint8_t ch = keyboard_getchar();
            
            if (ch == '\n') {
                /* Execute command */
                terminal_putchar('\n');
                shell_ctx.input_buffer[shell_ctx.input_pos] = '\0';
                
                if (shell_ctx.input_pos > 0) {
                    shell_execute_command(shell_ctx.input_buffer);
                }
                break;
            } else if (ch == '\b') {
                /* Backspace */
                if (shell_ctx.input_pos > 0) {
                    shell_ctx.input_pos--;
                    shell_ctx.input_buffer[shell_ctx.input_pos] = '\0';
                    terminal_backspace();
                }
            } else if (ch >= 32 && ch <= 126 && shell_ctx.input_pos < SHELL_COMMAND_MAX - 1) {
                /* Printable ASCII */
                shell_ctx.input_buffer[shell_ctx.input_pos++] = ch;
                terminal_putchar(ch);
            }
        }
    }
}

void shell_execute_command(const char* cmd) {
    if (!cmd || *cmd == '\0') {
        return;
    }
    
    /* Find command in table */
    for (size_t i = 0; i < NUM_COMMANDS; i++) {
        const char* name = commands[i].name;
        const char* p = cmd;
        
        /* Match command name */
        while (*name && *p && *name == *p) {
            name++;
            p++;
        }
        
        if (*name == '\0' && (*p == '\0' || *p == ' ')) {
            /* Found command - skip whitespace and get args */
            while (*p == ' ') {
                p++;
            }
            
            commands[i].handler((char*)p);
            return;
        }
    }
    
    /* Command not found */
    const uint8_t color = (VGA_COLOR_BLACK << 4) | VGA_COLOR_LIGHT_RED;
    terminal_set_color(color);
    terminal_write("Error: Command not found\n");
}

void shell_handle_keystroke(uint8_t scancode) {
    if (scancode > sizeof(scancode_to_ascii)) {
        return;
    }
    
    char c = scancode_to_ascii[scancode];
    
    if (c == '\n') {
        /* Execute command */
        shell_ctx.input_buffer[shell_ctx.input_pos] = '\0';
        shell_execute_command(shell_ctx.input_buffer);
        shell_ctx.input_pos = SHELL_COMMAND_MAX;  /* Signal end of input */
    } else if (c == '\b' && shell_ctx.input_pos > 0) {
        /* Backspace */
        shell_ctx.input_pos--;
        shell_ctx.input_buffer[shell_ctx.input_pos] = '\0';
    } else if (c >= 32 && c <= 126 && shell_ctx.input_pos < SHELL_COMMAND_MAX - 1) {
        /* Printable ASCII */
        shell_ctx.input_buffer[shell_ctx.input_pos++] = c;
    }
}

/* Echo command - prints arguments */
void cmd_echo(char* args) {
    const uint8_t color = (VGA_COLOR_BLACK << 4) | VGA_COLOR_WHITE;
    terminal_set_color(color);
    if (args && *args) {
        terminal_write(args);
    }
    terminal_write("\n");
}

/* Show memory information */
void cmd_meminfo(char* args) {
    (void)args;
    const uint8_t color = (VGA_COLOR_BLACK << 4) | VGA_COLOR_LIGHT_BROWN;
    
    terminal_set_color(color);
    terminal_write("Memory Information:\n");
    
    /* Get heap info from heap module */
    extern size_t heap_used(void);
    extern size_t heap_total(void);
    
    char buf[80];
    size_t used_kb = heap_used() / 1024;
    size_t total_kb = heap_total() / 1024;
    
    terminal_write("  Heap: ");
    
    /* Format used KB */
    size_t pos = 0;
    if (used_kb >= 100) {
        buf[pos++] = '0' + (used_kb / 100);
        buf[pos++] = '0' + ((used_kb / 10) % 10);
        buf[pos++] = '0' + (used_kb % 10);
    } else if (used_kb >= 10) {
        buf[pos++] = '0' + (used_kb / 10);
        buf[pos++] = '0' + (used_kb % 10);
    } else {
        buf[pos++] = '0' + used_kb;
    }
    buf[pos++] = 'K';
    buf[pos++] = 'B';
    buf[pos++] = ' ';
    buf[pos++] = '/';
    buf[pos++] = ' ';
    
    /* Format total KB */
    if (total_kb >= 100) {
        buf[pos++] = '0' + (total_kb / 100);
        buf[pos++] = '0' + ((total_kb / 10) % 10);
        buf[pos++] = '0' + (total_kb % 10);
    } else if (total_kb >= 10) {
        buf[pos++] = '0' + (total_kb / 10);
        buf[pos++] = '0' + (total_kb % 10);
    } else {
        buf[pos++] = '0' + total_kb;
    }
    buf[pos++] = 'K';
    buf[pos++] = 'B';
    buf[pos] = '\0';
    
    terminal_write(buf);
    terminal_write("\n");
}

/* Reboot command */
void cmd_reboot(char* args) {
    (void)args;
    const uint8_t color = (VGA_COLOR_BLACK << 4) | VGA_COLOR_LIGHT_RED;
    terminal_set_color(color);
    terminal_write("Rebooting system...\n");
    
    /* Triple fault method to reboot */
    __asm__ __volatile__(
        "cli\n"
        "lidt 0\n"  /* Load null IDT */
        "int $3\n"  /* Trigger interrupt - causes triple fault */
    );
    
    /* Should never reach here */
    while (1) {
        __asm__ __volatile__("hlt");
    }
}
