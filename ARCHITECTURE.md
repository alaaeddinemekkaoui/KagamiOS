# Kernel Subsystems: Shell vs Terminal

## Quick Explanation

### **Terminal (VGA Terminal)**
- **What it is**: The _output/input display layer_  
- **Location**: `kernel/vga/vga_terminal.c`
- **Responsibility**:
  - Manages text display on the screen
  - Handles character rendering
  - Manages cursor position
  - Provides functions like `vga_terminal_write()`, `vga_terminal_clear()`, `vga_terminal_putchar()`
  - Acts as an abstraction over raw VGA memory or framebuffer
  - **Read/Write**: Raw text I/O with the user

- **Analogy**: Think of it as a "text device" - like a monitor/keyboard interface

### **Shell (Command Interpreter)**
- **What it is**: The _command processor and program executor_
- **Location**: `kernel/shell/shell.c`
- **Responsibility**:
  - Displays the command prompt (e.g., `>` or `kagami$`)
  - Reads user input from the terminal
  - Parses commands entered by the user
  - Executes built-in commands (like `ls`, `echo`, `help`)
  - May launch external programs
  - Handles errors and displays results
  - **Read/Write**: Commands and program logic

- **Analogy**: Think of it as an "interactive interpreter" - like bash in Linux

## Boot Sequence in Kagami OS

```
1. UEFI Bootloader
    ↓
2. Logo Display (Cyan ASCII art on framebuffer)
    ↓
3. "Press ENTER to continue..." message
    ↓
4. Kernel waits for ENTER key (keyboard_getchar blocking call)
    ↓
5. Kernel initializes subsystems:
    - heap_init()      → Memory management ready
    - idt_init()       → Interrupt handling setup
    - keyboard_init()  → Keyboard driver ready
    - idt_load()       → Interrupts ENABLED
    ↓
6. Shell starts (shell_run())
    ↓
7. Terminal shows shell prompt
    ↓
8. User types commands
    ↓
9. Shell parses and executes
    ↓
10. Terminal displays output
```

## Code Stack Example

When user types `echo hello`:

```
Terminal (input) → receives 'e', 'c', 'h', 'o', ' ', 'h', 'e', 'l', 'l', 'o', ENTER
    ↓
    vga_terminal_putchar() displays each character
    ↓
Shell (processing) → reads line: "echo hello"
    ↓
    parses command: cmd="echo", args="hello"
    ↓
    finds and executes echo command
    ↓
    outputs: "hello"
    ↓
Terminal (output) → displays result via vga_terminal_write()
    ↓
Shell → displays new prompt
```

## File Organization

```
kernel/
├── main.c                    # Kernel entry - initializes boot sequence
├── entry64.asm              # Bootloader jump point
├── interrupts.asm           # Interrupt handler stubs
│
├── vga/                     # Terminal/Display Layer
│   ├── vga.c                # Raw VGA hardware driver
│   ├── vga.h
│   ├── vga_terminal.c       # Text terminal abstraction
│   ├── vga_terminal.h
│
├── shell/                   # Command Shell Layer
│   ├── shell.c              # Command parsing and execution
│   ├── shell.h
│
├── drivers/                 # Hardware Drivers
│   ├── keyboard.c           # PS/2 keyboard driver
│   ├── keyboard.h
│
├── core/                    # Core Kernel Services
│   ├── idt.c                # Interrupt Descriptor Table
│   ├── idt.h
│   ├── heap.c               # Memory allocator
│   ├── heap.h
│   ├── serial.c             # Debug serial port (COM1)
│   ├── framebuffer.c        # GOP framebuffer rendering
│   ├── font.c               # 8x8 bitmap font
│
└── include/                 # Unified headers
    ├── ascii_art.h          # Logo definitions
    ├── framebuffer.h        # Framebuffer API
    ├── font.h               # Font API
    ├── serial.h             # Serial API
```

## Key Initialization in boot sequence:

1. **heap_init()** - Sets up memory allocator (needed by everything)
2. **idt_init()** - Sets up interrupt table (needed for hardware events)
3. **keyboard_init()** - Sets up keyboard driver (needs IDT)
4. **idt_load()** - **ENABLES INTERRUPTS** (critical - do this last!)
5. **shell_init()** - Prepares shell (needs terminal ready)
6. **shell_run()** - Starts interactive loop (blocks forever)

## Why This Order Matters

- Must init heap BEFORE IDT (IDT needs memory)
- Must init keyboard BEFORE idt_load (keyboard ISR needs to be registered)
- Must idt_load() LAST (enables all interrupt handlers)

## Terminal Input Flow

```
Keyboard Hardware → IRQ1 → interrupt handler → keyboard_process_scancode()
    ↓
    Registers key in keyboard buffer
    ↓
Shell calls keyboard_getchar() → reads from buffer
    ↓
Shell processes command
```

## References

- **Terminal**: `kernel/vga/vga_terminal.h` - provides `vga_write()`, `vga_read()`
- **Shell**: `kernel/shell/shell.h` - provides `shell_run()`, `shell_init()`
- **Keyboard**: `kernel/drivers/keyboard.h` - provides `keyboard_getchar()`
- **Boot Sequence**: `kernel/main.c` - orchestrates initialization
