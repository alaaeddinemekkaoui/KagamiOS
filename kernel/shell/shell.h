#ifndef SHELL_H
#define SHELL_H

#include "types.h"
#include "boot_info.h"
#include "vga/vga_terminal.h"
#include "display/gop_terminal.h"

#define SHELL_COMMAND_MAX 80
#define SHELL_HISTORY_MAX 10

/* Shell command handler prototype */
typedef void (*shell_cmd_handler_t)(char* args);

/* Shell command entry */
typedef struct {
    const char* name;
    const char* help;
    shell_cmd_handler_t handler;
} SHELL_COMMAND;

/* Shell context */
typedef struct {
    char input_buffer[SHELL_COMMAND_MAX];
    size_t input_pos;
    
    char history[SHELL_HISTORY_MAX][SHELL_COMMAND_MAX];
    size_t history_pos;
    size_t history_count;
    
    uint8_t running;
} SHELL_CONTEXT;

/* Public API */
void shell_init(void);
void shell_run(void);
void shell_handle_keystroke(uint8_t scancode);
void shell_execute_command(const char* cmd);
void fb_shell_run(BOOT_INFO* boot_info);

/* Built-in commands */
void cmd_help(char* args);
void cmd_clear(char* args);
void cmd_status(char* args);
void cmd_bootinfo(char* args);
void cmd_echo(char* args);
void cmd_meminfo(char* args);
void cmd_reboot(char* args);

#endif /* SHELL_H */
