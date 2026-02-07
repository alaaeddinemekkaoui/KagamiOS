# Kagami OS — Awakening

## 📚 Overview

This is a minimal operating system that boots from scratch using:
- **Custom bootloader** written in x86 assembly
- **32-bit protected mode**
- **Kernel written in C**
- **Runs on bare metal** (tested in QEMU)

---

## 🏗️ Architecture

```
┌─────────────────────────────────────────┐
│  BIOS (loads first 512 bytes)           │
└─────────────────┬───────────────────────┘
                  │
                  ▼
┌─────────────────────────────────────────┐
│  Bootloader (boot.asm - 512 bytes)      │
│  • Sets up segments                     │
│  • Loads kernel from disk               │
│  • Enables A20 line                     │
│  • Switches to protected mode           │
│  • Jumps to kernel                      │
└─────────────────┬───────────────────────┘
                  │
                  ▼
┌─────────────────────────────────────────┐
│  Kernel Entry (entry.asm)               │
│  • Calls kernel_main()                  │
└─────────────────┬───────────────────────┘
                  │
                  ▼
┌─────────────────────────────────────────┐
│  Kernel (kernel.c)                      │
│  • Clears screen                        │
│  • Writes to VGA text memory            │
│  • Enters infinite loop                 │
└─────────────────────────────────────────┘
```

---

## 📁 Project Structure

```
myos/
├── boot/
│   └── boot.asm          # Bootloader (512 bytes, sector 0)
├── kernel/
│   ├── entry.asm         # Kernel entry point
│   └── kernel.c          # C kernel code
├── linker.ld             # Linker script
├── run.sh                # Build and run script
└── build/                # Generated files (created by run.sh)
    ├── boot.bin
    ├── kernel.bin
    ├── kernel.elf
    └── os.img
```

---

## 🔧 Prerequisites

Install the required tools:

```bash
# Ubuntu/Debian
sudo apt-get install nasm gcc qemu-system-x86 binutils

# Arch Linux
sudo pacman -S nasm gcc qemu binutils

# macOS (using Homebrew)
brew install nasm i686-elf-gcc qemu
```

---

## 🚀 Quick Start

```bash
# Make the script executable
chmod +x run.sh

# Build and run
./run.sh
```

You should see:
```
Loading kernel... OK

=====================================
  Kagami OS — Awakening
=====================================

Kernel loaded successfully!
Running in 32-bit protected mode.

System Status: OPERATIONAL
```

---

## 📖 Step-by-Step Explanation

### **Step 1: Bootloader (boot/boot.asm)**

The bootloader is the first code that runs. It's loaded by the BIOS at address `0x7C00`.

**What it does:**
1. **Initialize segments** - Set up DS, ES, SS to 0
2. **Set up stack** - Stack pointer at 0x7C00
3. **Load kernel** - Use BIOS INT 13h to read kernel from disk
4. **Enable A20** - Allow access to memory above 1MB
5. **Load GDT** - Global Descriptor Table for protected mode
6. **Switch to protected mode** - Set PE bit in CR0
7. **Jump to kernel** - Far jump to 32-bit code

**Key Concepts:**

- **Real Mode (16-bit)**: Initial mode, limited to 1MB memory
- **Protected Mode (32-bit)**: Full 4GB memory access, no BIOS interrupts
- **GDT**: Defines memory segments (code and data)
- **A20 Line**: Hardware workaround to access memory above 1MB

### **Step 2: Kernel Entry (kernel/entry.asm)**

Simple assembly stub that calls the C kernel.

```asm
_start:
    call kernel_main    # Jump to C code
    hlt                 # Halt if kernel returns
```

### **Step 3: Kernel (kernel/kernel.c)**

Main C code that runs in protected mode.

**VGA Text Mode:**
- Memory address: `0xB8000`
- Format: `[char][attribute]` pairs
- 80 columns × 25 rows
- Attribute byte: `(background << 4) | foreground`

**Example:**
```c
volatile char* vga = (volatile char*)0xB8000;
vga[0] = 'H';     // Character
vga[1] = 0x0F;    // White on black
```

### **Step 4: Linker Script (linker.ld)**

Tells the linker where to place code sections in memory.

```
. = 1M;              # Kernel loads at 1MB (0x100000)
.text : { ... }      # Code section
.data : { ... }      # Data section
.bss  : { ... }      # Uninitialized data
```

### **Step 5: Build Process**

```bash
# 1. Assemble bootloader to raw binary
nasm -f bin boot/boot.asm -o build/boot.bin

# 2. Assemble kernel entry to object file
nasm -f elf32 kernel/entry.asm -o build/entry.o

# 3. Compile C kernel
gcc -m32 -ffreestanding -c kernel/kernel.c -o build/kernel.o

# 4. Link kernel
ld -m elf_i386 -T linker.ld -o build/kernel.elf entry.o kernel.o

# 5. Convert to binary
objcopy -O binary build/kernel.elf build/kernel.bin

# 6. Create disk image
dd if=build/boot.bin of=build/os.img bs=512 count=1 conv=notrunc
dd if=build/kernel.bin of=build/os.img bs=512 seek=1 conv=notrunc
```

### **Step 6: Run in QEMU**

```bash
qemu-system-i386 -drive format=raw,file=build/os.img
```

---

## 🔍 Troubleshooting

### "DISK ERROR" message

- **Cause**: Bootloader can't read kernel from disk
- **Fix**: Ensure `KERNEL_SECTORS` in boot.asm is large enough

```bash
# Check kernel size
ls -lh build/kernel.bin

# Calculate sectors needed (divide by 512, round up)
# Update KERNEL_SECTORS in boot/boot.asm
```

### Blank screen after boot

- **Cause**: Kernel not loaded or linker address mismatch
- **Fix**: Check linker.ld has `. = 1M;` and bootloader jumps to `0x100000`

### GCC errors about missing headers

- **Cause**: Using hosted environment
- **Fix**: Add `-ffreestanding -nostdlib -nostdinc` flags

---

## 📚 Next Steps

Now that you have a working kernel, you can add:

1. **Keyboard input** - Read from port 0x60
2. **Interrupts** - Set up IDT and handle keyboard/timer
3. **Memory management** - Page tables, heap allocator
4. **Multitasking** - Process switching
5. **File system** - Read/write files
6. **Shell** - Command line interface

---

## 📖 Resources

- [OSDev Wiki](https://wiki.osdev.org/) - Comprehensive OS development guide
- [Intel x86 Manual](https://software.intel.com/content/www/us/en/develop/articles/intel-sdm.html)
- [Writing a Simple Operating System from Scratch](https://www.cs.bham.ac.uk/~exr/lectures/opsys/10_11/lectures/os-dev.pdf)

---

## 📄 License

This is educational code - use it however you want!

---

**Congratulations! You've built an operating system from scratch! 🎉**
