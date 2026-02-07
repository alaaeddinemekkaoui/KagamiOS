# Kagami OS - Interactive Shell Guide

## 🎮 Running the OS

```bash
make bios-run    # Run with BIOS bootloader
make uefi-run    # Run with UEFI bootloader (if configured)
```

## ⌨️ Interactive Features

### Starting the Shell
1. Boot the OS - you'll see the Kagami OS logo and initialization messages
2. Press any key when prompted to start the interactive shell
3. You'll see the `kagami>` prompt - you can now type commands!

### Available Commands

| Command | Description | Example |
|---------|-------------|---------|
| `help` | Show all available commands | `help` |
| `clear` | Clear the screen | `clear` |
| `status` | Show system status (kernel, interrupts, memory) | `status` |
| `bootinfo` | Display boot information (drive, memory, bootloader type) | `bootinfo` |
| `echo <text>` | Echo text to the screen | `echo Hello World!` |
| `meminfo` | Show memory usage statistics | `meminfo` |
| `reboot` | Reboot the system | `reboot` |

### Keyboard Features
- **Type naturally** - all alphanumeric keys and symbols work
- **Backspace** - delete the last character
- **Enter** - execute the command
- **Shift** - capitalize letters and access symbols
- **Tab** - insert 4 spaces

## 🏗️ Architecture

### New Modules Added

1. **[kernel/keyboard.c](kernel/keyboard.c)** - PS/2 keyboard driver
   - Circular buffer for input
   - Scancode to ASCII conversion
   - Shift/Ctrl/Alt modifier support
   
2. **[kernel/vga_terminal.c](kernel/vga_terminal.c)** - Terminal output system
   - Cursor positioning
   - Character-by-character output
   - Automatic scrolling
   - Backspace handling

3. **[kernel/shell.c](kernel/shell.c)** - Interactive command shell
   - Command parsing and execution
   - 7 built-in commands
   - Extensible command table

### Interrupt Flow

```
Keyboard press → Hardware interrupt (IRQ1)
    ↓
IDT routes to isr_keyboard (interrupts.asm)
    ↓
Calls keyboard_isr() (idt.c)
    ↓
Calls keyboard_process_scancode() (keyboard.c)
    ↓
Converts to ASCII and stores in circular buffer
    ↓
Shell reads via keyboard_getchar() (blocking)
    ↓
Echoes character to VGA terminal
    ↓
On Enter: parses and executes command
```

## 🧪 Testing Commands

Try these command sequences:

```
kagami> help
kagami> status
kagami> bootinfo
kagami> echo Kagami OS is running!
kagami> meminfo
kagami> clear
kagami> reboot
```

## 🔧 Adding New Commands

To add a new command:

1. Declare the handler in **kernel/shell.h**:
   ```c
   void cmd_mycommand(char* args);
   ```

2. Add to the command table in **kernel/shell.c**:
   ```c
   {"mycommand", "Description", cmd_mycommand},
   ```

3. Implement the function in **kernel/shell.c**:
   ```c
   void cmd_mycommand(char* args) {
       terminal_write("Hello from my command!\n");
   }
   ```

4. Rebuild: `make bios`

## 📊 Current System Specs

- **Mode**: 64-bit long mode
- **Display**: 80x25 VGA text mode
- **Memory**: 1MB heap at 0x110000
- **Interrupts**: Full IDT with 256 vectors
- **Keyboard**: PS/2 with US QWERTY layout
- **Commands**: 7 built-in commands

Enjoy your fully interactive OS! 🎉
