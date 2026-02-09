#include "core/idt.h"
#include "boot_info.h"
#include "shell/shell.h"
#include "core/heap.h"
#include "drivers/input/keyboard.h"
#include "drivers/storage/ahci.h"
#include "drivers/storage/nvme.h"
#include "drivers/storage/block.h"
#include "drivers/storage/partition.h"
#include "fs/ext4/ext4.h"
#include "fs/vfs.h"
#include "net/net.h"

/* Framebuffer and system components */
#include "include/serial.h"
#include "include/framebuffer.h"
#include "include/ascii_art.h"
#include "klog.h"

void kernel_main(void) {
    serial_init();

    /* Get boot info */
    BOOT_INFO* boot_info = get_boot_info();
    
    if (!boot_info_valid()) {
        serial_write("ERROR: Invalid boot info!\n");
        while (1) { __asm__ __volatile__("hlt"); }
    }
    
    serial_write("Boot info valid\n");
    
    /* Check if we have a framebuffer */
    if (boot_info->framebuffer_addr != 0) {
        unsigned int* fb = (unsigned int*)(unsigned long)boot_info->framebuffer_addr;
        unsigned int pitch = boot_info->framebuffer_pitch;
        unsigned int width = boot_info->framebuffer_width;
        unsigned int height = boot_info->framebuffer_height;
        
        serial_write("Using GOP framebuffer\n");
        
        /* Clear screen to black */
        for (unsigned int y = 0; y < height; y++) {
            for (unsigned int x = 0; x < width; x++) {
                fb_putpixel(fb, pitch, x, y, 0x000000);
            }
        }
        
        serial_write("Screen cleared\n");
        
        /* Calculate dimensions for scaled logo (2x scale) */
        int scale = 2;
        int logo_width = 40 * 8 * scale;
        int logo_height = 8 * 8 * scale;
        
        /* Center logo on screen */
        int center_x = ((int)width > logo_width) ? ((int)width - logo_width) / 2 : 20;
        int center_y = ((int)height > logo_height) ? ((int)height - logo_height) / 2 : 50;
        unsigned int logo_x = (unsigned int)center_x;
        unsigned int logo_y = (unsigned int)center_y;
        
        /* Draw Kagami OS ASCII art logo in cyan, scaled 2x */
        for (int i = 0; i < KAGAMI_LOGO_LINES; i++) {
            fb_print_scaled(fb, pitch, logo_x, logo_y + (i * 16 * scale), kagami_logo[i], 0x00FFFF, scale);
        }
        
        serial_write("ASCII art logo drawn\n");
        
        /* Draw keyboard message at bottom */
        int msg_y = height - 50;
        if (keyboard_has_controller()) {
            fb_print_scaled(fb, pitch, 20, msg_y, "Press ENTER to continue...", 0x00FF00, 1);
        } else {
            fb_print_scaled(fb, pitch, 20, msg_y, "No keyboard detected", 0x00FF8800, 1);
        }

        klog_init_fb(fb, pitch, width, height);
        KLOG("Framebuffer logger initialized");
        
    } else {
        serial_write("No framebuffer available, trying VGA\n");
        
        /* Fallback to VGA text mode */
        volatile uint16_t* vga = (uint16_t*)0xB8000;
        vga[0] = 'K' | (0x0A << 8);
        vga[1] = 'E' | (0x0A << 8);
        vga[2] = 'R' | (0x0A << 8);
        vga[3] = 'N' | (0x0A << 8);
        vga[4] = 'E' | (0x0A << 8);
        vga[5] = 'L' | (0x0A << 8);
        vga[6] = ' ' | (0x0A << 8);
        vga[7] = 'O' | (0x0A << 8);
        vga[8] = 'K' | (0x0A << 8);
        vga[9] = '!' | (0x0A << 8);
    }
    
    serial_write("Kernel: Waiting for ENTER to boot...\n");
    KLOG("Kernel: Waiting for ENTER to boot...");
    
    /* Initialize kernel subsystems */
    heap_init();
    serial_write("Kernel: Heap initialized\n");
    KLOG("Kernel: Heap initialized");
    
    idt_init();
    serial_write("Kernel: IDT initialized\n");
    KLOG("Kernel: IDT initialized");

    idt_load();
    serial_write("Kernel: IDT loaded\n");
    KLOG("Kernel: IDT loaded");
    
    keyboard_init();
    serial_write("Kernel: Keyboard driver initialized\n");
    KLOG("Kernel: Keyboard driver initialized");

    idt_enable_interrupts();
    serial_write("Kernel: Interrupts enabled\n");
    KLOG("Kernel: Interrupts enabled");

    ahci_init();
    nvme_init();

    net_init();

    static Ext4Fs root_fs;
    if (block_count() > 0) {
        BlockDevice *dev = block_get(0);
        PartitionInfo part;
        if (find_linux_partition(dev, &part)) {
            if (ext4_mount(&root_fs, dev, part.first_lba)) {
                vfs_mount_ext4(&root_fs);
                serial_write("EXT4: filesystem mounted\n");
                KLOG("EXT4: filesystem mounted");
            } else {
                serial_write("EXT4: mount failed\n");
                KERR("EXT4: mount failed");
            }
        }
    }
    
    /* Wait for ENTER key using the buffered keyboard driver */
    serial_write("Keyboard: Waiting for ENTER key (buffered mode)...\n");
    KLOG("Keyboard: Waiting for ENTER key (buffered mode)...");
    if (keyboard_has_controller()) {
        const int timeout_loops = 3000000;
        int loops = 0;
        while (loops < timeout_loops) {
            uint8_t ch = keyboard_getchar_nonblock();
            if (ch == '\n' || ch == '\r') {
                serial_write("Keyboard: ENTER pressed!\n");
                KLOG("Keyboard: ENTER pressed!");
                break;
            }
            for (volatile int i = 0; i < 1000; i++);
            loops++;
        }
        if (loops >= timeout_loops) {
            serial_write("Keyboard: timeout, auto-continue\n");
            KERR("Keyboard: timeout, auto-continue");
        }
    } else {
        serial_write("Keyboard: Not detected, auto-continue\n");
        KERR("Keyboard: Not detected, auto-continue");
    }
    serial_write("Kernel: Initialized successfully!\n");
    serial_write("Framebuffer: Active\n");
    serial_write("Display: Starting interactive shell...\n\n");
    KLOG("Kernel: Initialized successfully!");
    KLOG("Display: Starting interactive shell...");
    
    /* Start interactive framebuffer shell with GPU rendering */
    /* Shell will display ASCII art logo and fantasy welcome message */
    fb_shell_run(boot_info);
    
    /* If shell exits, halt */
    while (1) {
        __asm__ __volatile__("hlt");
    }
}
