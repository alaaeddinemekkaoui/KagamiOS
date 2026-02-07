#include "vga/vga.h"
#include "core/idt.h"
#include "boot_info.h"
#include "shell/shell.h"
#include "core/heap.h"
#include "drivers/keyboard.h"
#include "vga/vga_terminal.h"

/* Include new modular components */
#include "include/serial.h"
#include "include/framebuffer.h"
#include "include/ascii_art.h"

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
        
        /* Draw "Press ENTER to continue..." message in green at bottom */
        int msg_y = height - 50;
        fb_print_scaled(fb, pitch, 20, msg_y, "Press ENTER to continue...", 0x00FF00, 1);
        
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
    
    /* Initialize kernel subsystems */
    heap_init();
    serial_write("Kernel: Heap initialized\n");
    
    idt_init();
    serial_write("Kernel: IDT initialized\n");
    
    keyboard_init();
    serial_write("Kernel: Keyboard driver initialized\n");
    
    /* Wait for ENTER key by directly polling PS/2 controller */
    serial_write("Keyboard: Waiting for ENTER key (polling mode)...\n");
    keyboard_wait_for_enter();
    serial_write("Keyboard: ENTER pressed!\n");
    
    /* Display welcome message on framebuffer */
    if (boot_info && boot_info->framebuffer_addr) {
        unsigned int* fb = (unsigned int*)boot_info->framebuffer_addr;
        unsigned int pitch = boot_info->framebuffer_pitch;
        unsigned int width = boot_info->framebuffer_width;
        unsigned int height = boot_info->framebuffer_height;
        
        /* Clear framebuffer with black */
        for (unsigned int i = 0; i < (width * height); i++) {
            fb[i] = 0x00000000;
        }
        
        /* Display welcome message centered on screen */
        const char* welcome = "Welcome to Kagami OS!";
        int msg_width = 21 * 8 * 2;  /* 21 characters * 8 pixels * 2 scale */
        int welcome_x = (width - msg_width) / 2;
        int welcome_y = height / 2 - 100;
        
        fb_print_scaled(fb, pitch, welcome_x, welcome_y, welcome, 0x0000FF00, 2);
        
        /* Display kernel status */
        const char* submsg = "Kernel initialized successfully!";
        int submsg_width = 33 * 8;
        int submsg_x = (width - submsg_width) / 2;
        int submsg_y = welcome_y + 60;
        
        fb_print_scaled(fb, pitch, submsg_x, submsg_y, submsg, 0x0000FF00, 1);
        
        /* Display visible prompt on screen */
        fb_print_scaled(fb, pitch, 100, height / 2 + 50, "kagami>", 0x00FFFFFF, 1);
        fb_print_scaled(fb, pitch, 160, height / 2 + 50, "_", 0x00FFFFFF, 1);
        
        /* Display help text at bottom */
        const char* help = "System is running! Press Ctrl+C to exit QEMU.";
        int help_width = 46 * 8;
        int help_x = (width - help_width) / 2;
        fb_print_scaled(fb, pitch, help_x, height - 80, help, 0x00888888, 1);
    }
    
    serial_write("\n========================================\n");
    serial_write("  KAGAMI OS - Awakening 0.1\n");
    serial_write("========================================\n");
    serial_write("Kernel: Initialized successfully!\n");
    serial_write("Framebuffer: Active\n");
    serial_write("Display: Showing welcome screen\n");
    serial_write("\nKernel halted. System stable.\n");
    serial_write("Press Ctrl+C to exit QEMU.\n\n");
    
    /* Halt forever - kernel is stable here */
    while (1) {
        __asm__ __volatile__("hlt");
    }
}
