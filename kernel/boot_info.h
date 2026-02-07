#ifndef BOOT_INFO_H
#define BOOT_INFO_H

#include "types.h"

/* Boot information structure passed from bootloader to kernel
 * Located at physical address 0x90500
 */

#define BOOT_INFO_MAGIC        0x4B414741  /* "KAGA" in ASCII */
#define BOOT_INFO_ADDR         0x90500

typedef struct {
    uint32_t magic;              /* Magic number for validation (0x4B414741) */
    uint32_t boot_drive;         /* BIOS drive number (0x80 for first HDD) */
    uint32_t memory_size_kb;     /* Total system memory in KB */
    uint32_t reserved_low;       /* Reserved memory below 1MB in KB */
    
    uint16_t screen_width;       /* Screen width in characters (usually 80) */
    uint16_t screen_height;      /* Screen height in characters (usually 25) */
    
    /* Partition info (for file system access) */
    uint32_t boot_partition_lba; /* LBA of boot partition */
    uint32_t boot_partition_size;/* Size of boot partition in sectors */
    
    /* Extended memory map info */
    uint32_t memory_regions;     /* Number of memory regions in map */
    uint32_t memory_map_addr;    /* Physical address of memory map (or 0 if none) */
    
    /* Bootloader identification */
    uint8_t  bootloader_type;    /* 0=BIOS stage2, 1=UEFI, 2=other */
    uint8_t  reserved[3];        /* Padding */
    
    uint32_t checksum;           /* Simple checksum for data validation */
    
    /* GOP framebuffer info (UEFI only) */
    uint64_t framebuffer_addr;   /* Physical address of framebuffer */
    uint32_t framebuffer_width;  /* Width in pixels */
    uint32_t framebuffer_height; /* Height in pixels */
    uint32_t framebuffer_pitch;  /* Bytes per scanline */
    uint32_t framebuffer_bpp;    /* Bits per pixel (usually 32) */
} __attribute__((packed)) BOOT_INFO;

/* Bootloader types */
#define BOOTLOADER_BIOS_STAGE2   0
#define BOOTLOADER_UEFI          1

/* Memory map entry (for extended info) */
typedef struct {
    uint64_t base_addr;
    uint64_t length;
    uint32_t type;               /* 1=usable, 2=reserved, 3=ACPI, 4=NVS */
    uint32_t acpi_attr;
} __attribute__((packed)) MEMORY_MAP_ENTRY;

/* Get boot info pointer */
static inline BOOT_INFO* get_boot_info(void) {
    return (BOOT_INFO*)0x90500;
}

/* Validate boot info structure */
static inline uint8_t boot_info_valid(void) {
    BOOT_INFO* info = get_boot_info();
    return info->magic == BOOT_INFO_MAGIC;
}

#endif /* BOOT_INFO_H */
