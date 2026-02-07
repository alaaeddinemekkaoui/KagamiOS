#include <efi.h>
#include <efilib.h>

#define BOOT_INFO_MAGIC 0x4B414741
#define BOOT_INFO_ADDR  0x90500
#define KERNEL_LOAD_ADDR 0x100000
#define KERNEL_RESERVED_SIZE (2 * 1024 * 1024) /* Reserve 2 MiB for kernel */

typedef struct {
    UINT32 magic;
    UINT32 boot_drive;
    UINT32 memory_size_kb;
    UINT32 reserved_low;
    UINT16 screen_width;
    UINT16 screen_height;
    UINT32 boot_partition_lba;
    UINT32 boot_partition_size;
    UINT32 memory_regions;
    UINT32 memory_map_addr;
    UINT8  bootloader_type;
    UINT8  reserved[3];
    UINT32 checksum;
    
    /* GOP framebuffer info */
    UINT64 framebuffer_addr;
    UINT32 framebuffer_width;
    UINT32 framebuffer_height;
    UINT32 framebuffer_pitch;
    UINT32 framebuffer_bpp;
} __attribute__((packed)) BOOT_INFO;

typedef void (*kernel_entry_t)(void);

EFI_STATUS EFIAPI efi_main(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable) {
    InitializeLib(ImageHandle, SystemTable);

    Print(L"[DEBUG] Bootloader started\n");
    uefi_call_wrapper(BS->Stall, 1, 500000);
    
    /* Reserve kernel load region so UEFI allocations don't overwrite it */
    EFI_PHYSICAL_ADDRESS kernel_addr = KERNEL_LOAD_ADDR;
    UINTN kernel_pages = (KERNEL_RESERVED_SIZE + 4095) / 4096;
    EFI_STATUS status = uefi_call_wrapper(BS->AllocatePages, 4,
                                          AllocateAddress,
                                          EfiLoaderData,
                                          kernel_pages,
                                          &kernel_addr);
    if (EFI_ERROR(status)) {
        Print(L"ERROR: Cannot reserve kernel memory at 0x%lx (status: 0x%x)\n", kernel_addr, status);
        uefi_call_wrapper(BS->Stall, 1, 3000000);
        return status;
    }

    /* Clear screen */
    uefi_call_wrapper(ST->ConOut->ClearScreen, 1, ST->ConOut);
    Print(L"[DEBUG] Screen cleared\n");
    uefi_call_wrapper(BS->Stall, 1, 500000);
    
    uefi_call_wrapper(ST->ConOut->SetCursorPosition, 3, ST->ConOut, 0, 0);
    uefi_call_wrapper(ST->ConOut->EnableCursor, 2, ST->ConOut, FALSE);
    
    Print(L"[DEBUG] Cursor disabled\n");
    uefi_call_wrapper(BS->Stall, 1, 500000);
    
    /* Now show ASCII art */
    uefi_call_wrapper(ST->ConOut->SetAttribute, 2, ST->ConOut, EFI_TEXT_ATTR(EFI_LIGHTCYAN, EFI_BLACK));
    
    Print(L"\n\n\n");
    Print(L"  _  __   _    ____    _    __  __ ___\n");
    Print(L" | |/ /  / \\  / ___|  / \\  |  \\/  |_ _|\n");
    Print(L" | ' /  / _ \\| |  _  / _ \\ | |\\/| || |\n");
    Print(L" | . \\ / ___ \\ |_| |/ ___ \\| |  | || |\n");
    Print(L" |_|\\_\\_/   \\_\\____/_/   \\_\\_|  |_|___|\n");
    Print(L"\n");
    Print(L" K A G A M I   O S  -  U E F I  B o o t l o a d e r\n");
    Print(L" \"Awakening\"\n");
    
    Print(L"\n[DEBUG] ASCII art displayed\n");
    uefi_call_wrapper(BS->Stall, 1, 2000000);

    /* Now clear and show boot information */
    uefi_call_wrapper(ST->ConOut->ClearScreen, 1, ST->ConOut);
    uefi_call_wrapper(ST->ConOut->SetAttribute, 2, ST->ConOut, EFI_TEXT_ATTR(EFI_LIGHTGREEN, EFI_BLACK));
    
    Print(L"\n=== KAGAMI OS BOOTLOADER ===\n\n");
    
    /* Get GOP (Graphics Output Protocol) for framebuffer access */
    EFI_GUID gop_guid = EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID;
    EFI_GRAPHICS_OUTPUT_PROTOCOL *gop = NULL;
    UINTN gop_handle_count = 0;
    EFI_HANDLE *gop_handles = NULL;
    
    status = uefi_call_wrapper(BS->LocateHandleBuffer, 5, ByProtocol, &gop_guid, NULL, &gop_handle_count, &gop_handles);
    
    if (!EFI_ERROR(status) && gop_handle_count > 0) {
        status = uefi_call_wrapper(BS->HandleProtocol, 3, gop_handles[0], &gop_guid, (VOID**)&gop);
        if (!EFI_ERROR(status)) {
            Print(L"GOP: %ux%u framebuffer at 0x%lx\n",
                  gop->Mode->Info->HorizontalResolution,
                  gop->Mode->Info->VerticalResolution,
                  gop->Mode->FrameBufferBase);
        } else {
            Print(L"WARNING: Cannot get GOP protocol (status: 0x%x)\n", status);
            gop = NULL;
        }
    } else {
        Print(L"WARNING: No GOP handles found (status: 0x%x)\n", status);
        gop = NULL;
    }
    
    /* Get memory map with dynamic buffer sizing */
    UINTN memory_map_size = 0;
    UINTN map_key = 0;
    UINTN descriptor_size = 0;
    UINT32 descriptor_version = 0;
    EFI_MEMORY_DESCRIPTOR *memory_map = NULL;
    UINTN buffer_size = 0;
    status = EFI_SUCCESS;
    
    Print(L"Gathering system information...\n");
    
    /* Loop to handle buffer sizing */
    int buffer_multiplier = 2;
    int attempt = 0;
    
    while (attempt < 10) {
        attempt++;
        
        memory_map_size = 0;
        uefi_call_wrapper(BS->GetMemoryMap, 5, &memory_map_size, NULL, &map_key, &descriptor_size, &descriptor_version);
        
        buffer_size = memory_map_size * buffer_multiplier;
        
        Print(L"[DEBUG] Attempt %d: Need %u bytes, allocating %u bytes (x%d multiplier)\n", 
              attempt, memory_map_size, buffer_size, buffer_multiplier);
        
        if (memory_map != NULL) {
            uefi_call_wrapper(BS->FreePool, 1, memory_map);
        }
        
        status = uefi_call_wrapper(BS->AllocatePool, 3, EfiLoaderData, buffer_size, (VOID**)&memory_map);
        if (EFI_ERROR(status)) {
            Print(L"ERROR: Cannot allocate memory (status: 0x%x)\n", status);
            uefi_call_wrapper(BS->Stall, 1, 3000000);
            return status;
        }
        
        memory_map_size = buffer_size;
        status = uefi_call_wrapper(BS->GetMemoryMap, 5, &memory_map_size, memory_map, &map_key, &descriptor_size, &descriptor_version);
        
        if (status == EFI_SUCCESS) {
            Print(L"[DEBUG] Success! %u bytes used of %u allocated\n", memory_map_size, buffer_size);
            break;
        } else if (status == EFI_BUFFER_TOO_SMALL) {
            Print(L"[DEBUG] Buffer too small (%u needed), increasing multiplier\n", memory_map_size);
            buffer_multiplier++;
        } else {
            Print(L"ERROR: GetMemoryMap failed with status 0x%x\n", status);
            uefi_call_wrapper(BS->Stall, 1, 3000000);
            return status;
        }
        
        uefi_call_wrapper(BS->Stall, 1, 200000);
    }
    
    if (EFI_ERROR(status)) {
        Print(L"ERROR: Failed to get memory map after %d attempts\n", attempt);
        uefi_call_wrapper(BS->Stall, 1, 5000000);
        return status;
    }
    
    uefi_call_wrapper(BS->Stall, 1, 500000);
    
    /* Calculate total memory */
    UINTN total_memory = 0;
    EFI_MEMORY_DESCRIPTOR *desc = memory_map;
    for (UINTN i = 0; i < memory_map_size / descriptor_size; i++) {
        total_memory += desc->NumberOfPages * 4096;
        desc = (EFI_MEMORY_DESCRIPTOR*)((UINT8*)desc + descriptor_size);
    }
    
    Print(L"TOTAL MEMORY: %u MB\n", total_memory / (1024 * 1024));
    Print(L"KERNEL: Pre-loaded at 0x%lx by QEMU\n", kernel_addr);
    
    /* Setup boot info structure */
    BOOT_INFO *info = (BOOT_INFO*)BOOT_INFO_ADDR;
    info->magic = BOOT_INFO_MAGIC;
    info->boot_drive = 0x80;
    info->memory_size_kb = (UINT32)(total_memory / 1024);
    info->reserved_low = 640;
    info->screen_width = 80;
    info->screen_height = 25;
    info->boot_partition_lba = 0;
    info->boot_partition_size = 0;
    info->memory_regions = 0;
    info->memory_map_addr = 0;
    info->bootloader_type = 1;
    info->reserved[0] = 0;
    info->reserved[1] = 0;
    info->reserved[2] = 0;
    
    /* GOP framebuffer info */
    if (gop != NULL) {
        info->framebuffer_addr = gop->Mode->FrameBufferBase;
        info->framebuffer_width = gop->Mode->Info->HorizontalResolution;
        info->framebuffer_height = gop->Mode->Info->VerticalResolution;
        info->framebuffer_pitch = gop->Mode->Info->PixelsPerScanLine * 4;
        info->framebuffer_bpp = 32;
    } else {
        info->framebuffer_addr = 0;
        info->framebuffer_width = 0;
        info->framebuffer_height = 0;
        info->framebuffer_pitch = 0;
        info->framebuffer_bpp = 0;
    }
    
    info->checksum = info->magic + info->boot_drive + info->memory_size_kb;
    
    Print(L"BOOT INFO: Ready at 0x90500\n");
    Print(L"\n[*] Exiting UEFI Boot Services...\n");
    Print(L"[DEBUG] Buffer size: %u bytes\n", buffer_size);
    uefi_call_wrapper(BS->Stall, 1, 1000000);
    
    /* Exit boot services - NO Print() between GetMemoryMap and ExitBootServices! */
    int attempts = 0;
    
    while (attempts < 10) {
        UINTN temp_size = buffer_size;
        status = uefi_call_wrapper(BS->GetMemoryMap, 5, &temp_size, memory_map, &map_key, &descriptor_size, &descriptor_version);
        
        if (status == EFI_BUFFER_TOO_SMALL) {
            Print(L"ERROR: Buffer too small (%u bytes needed, have %u)\n", temp_size, buffer_size);
            uefi_call_wrapper(BS->Stall, 1, 5000000);
            while(1) { __asm__ __volatile__("hlt"); }
        }
        
        if (EFI_ERROR(status)) {
            Print(L"ERROR: GetMemoryMap failed (status=0x%x)\n", status);
            uefi_call_wrapper(BS->Stall, 1, 5000000);
            while(1) { __asm__ __volatile__("hlt"); }
        }
        
        /* IMMEDIATELY call ExitBootServices - NO Print() here! */
        status = uefi_call_wrapper(BS->ExitBootServices, 2, ImageHandle, map_key);
        if (!EFI_ERROR(status)) {
            break;
        }
        
        attempts++;
    }
    
    if (EFI_ERROR(status)) {
        Print(L"ERROR: Cannot exit boot services after %d attempts (status: 0x%x)\n", attempts, status);
        uefi_call_wrapper(BS->Stall, 1, 5000000);
        while(1) { __asm__ __volatile__("hlt"); }
    }
    
    /* SUCCESS! */
    __asm__ __volatile__("cli");
    
    __asm__ __volatile__(
        "mov %0, %%rax\n"
        "jmp *%%rax\n"
        :
        : "r"(kernel_addr)
        : "rax"
    );
    
    while (1) {
        __asm__ __volatile__("hlt");
    }
    
    return EFI_SUCCESS;
}
