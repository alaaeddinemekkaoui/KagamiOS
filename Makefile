# =========================
# Kagami OS - UEFI Boot
# =========================

# Tools
ASM      = nasm
CC       = gcc
LD       = ld
OBJCOPY  = objcopy
QEMU     = qemu-system-x86_64

# Directories
BUILD_DIR = build
BOOT_DIR  = boot
KERNEL_DIR = kernel
ESP_DIR   = $(BUILD_DIR)/esp
EFI_BOOT_DIR = $(ESP_DIR)/EFI/BOOT

# =========================
# GNU-EFI paths (auto-detect)
# =========================
EFI_INC        ?= /usr/include/efi
EFI_INC_ARCH   ?= /usr/include/efi/x86_64
EFI_LIB        ?= /usr/lib
EFI_LIB_GNU    ?= /usr/lib
EFI_CRT        ?= /usr/lib/crt0-efi-x86_64.o
EFI_LDS        ?= /usr/lib/elf_x86_64_efi.lds

# =========================
# Flags
# =========================

# UEFI C flags
CC_FLAGS_UEFI = \
	-I$(EFI_INC) -I$(EFI_INC_ARCH) \
	-fpic -fshort-wchar -mno-red-zone \
	-ffreestanding -fno-stack-protector \
	-Wall -Wextra

# Kernel flags
ASM_FLAGS = -f elf64
CC_FLAGS_KERNEL = \
	-m64 -ffreestanding -mno-red-zone \
	-fno-pie -fno-pic \
	-fno-builtin -fno-stack-protector \
	-nostdlib -nostdinc \
	-I. \
	-Idrivers \
	-Idrivers/bus \
	-Idrivers/storage \
	-Idrivers/input \
	-Idrivers/video \
	-Idrivers/net \
	-Inet \
	-Ifs \
	-Ifs/ext4 \
	-I$(KERNEL_DIR) \
	-I$(KERNEL_DIR)/include \
	-I$(KERNEL_DIR)/core \
	-I$(KERNEL_DIR)/bus \
	-I$(KERNEL_DIR)/shell \
	-I$(KERNEL_DIR)/vga \
	-I$(BUILD_DIR)/generated \
	-Wall -Wextra

LD_FLAGS_KERNEL = -m elf_x86_64 -T linker.ld

# =========================
# Output files
# =========================
KERNEL_ELF = $(BUILD_DIR)/kernel.elf
KERNEL_BIN = $(EFI_BOOT_DIR)/kernel.bin
DISK_IMG   = $(BUILD_DIR)/disk.img

UEFI_OBJ = $(BUILD_DIR)/uefi_main.o
UEFI_SO  = $(BUILD_DIR)/BOOTX64.so
UEFI_EFI = $(EFI_BOOT_DIR)/BOOTX64.EFI

# =========================
# Targets
# =========================
.PHONY: all clean run uefi-shell
.PHONY: disk

all: $(UEFI_EFI)

# -------------------------
# Directories
# -------------------------
$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BUILD_DIR)/generated:
	mkdir -p $(BUILD_DIR)/generated

$(BUILD_DIR)/generated/ascii_art_data.h: $(KERNEL_DIR)/ascii/kagami_logo.txt Makefile | $(BUILD_DIR)/generated
	@echo "[GEN] ascii_art_data.h from kagami_logo.txt"
	@awk 'BEGIN { \
		print "#ifndef KAGAMI_ASCII_ART_DATA_H"; \
		print "#define KAGAMI_ASCII_ART_DATA_H"; \
		print "static const char* kagami_logo[] = {"; \
	} \
	{ \
		gsub(/\\/, "\\\\"); \
		gsub(/"/, "\\\""); \
		print "    \"" $$0 "\","; \
	} \
	END { \
		print "    0"; \
		print "};"; \
		print "#define KAGAMI_LOGO_LINES " NR; \
		print "#endif"; \
	}' $< > $@

$(BUILD_DIR)/generated/commands_manual.h: $(KERNEL_DIR)/ascii/COMMANDS.txt Makefile | $(BUILD_DIR)/generated
	@echo "[GEN] commands_manual.h from COMMANDS.txt"
	@awk 'BEGIN { \
		print "#ifndef KAGAMI_COMMANDS_MANUAL_H"; \
		print "#define KAGAMI_COMMANDS_MANUAL_H"; \
		print "static const char commands_manual[] ="; \
	} \
	{ \
		gsub(/\\/, "\\\\"); \
		gsub(/"/, "\\\""); \
		print "\"" $$0 "\\n\""; \
	} \
	END { \
		print ";"; \
		print "#endif"; \
	}' $< > $@

$(EFI_BOOT_DIR):
	mkdir -p $(EFI_BOOT_DIR)

# =========================
# Kernel build
# =========================
# Assembly files
$(BUILD_DIR)/entry.o: $(KERNEL_DIR)/entry64.asm | $(BUILD_DIR)
	$(ASM) $(ASM_FLAGS) $< -o $@

$(BUILD_DIR)/interrupts.o: $(KERNEL_DIR)/interrupts.asm | $(BUILD_DIR)
	$(ASM) $(ASM_FLAGS) $< -o $@

# C files - compile from any subdirectory
# Search order: root, core, shell, vga, drivers
vpath %.c $(KERNEL_DIR):$(KERNEL_DIR)/core:$(KERNEL_DIR)/shell:$(KERNEL_DIR)/vga:drivers/bus:drivers/storage:drivers/input:drivers/video:drivers/net:fs:fs/ext4:net

$(BUILD_DIR)/%.o: %.c $(BUILD_DIR)/generated/ascii_art_data.h $(BUILD_DIR)/generated/commands_manual.h | $(BUILD_DIR)
	$(CC) $(CC_FLAGS_KERNEL) -I$(KERNEL_DIR)/include -I$(KERNEL_DIR) -c $< -o $@

# All kernel object files
KERNEL_OBJS = \
	$(BUILD_DIR)/entry.o \
	$(BUILD_DIR)/interrupts.o \
	$(BUILD_DIR)/main.o \
	$(BUILD_DIR)/vga.o \
	$(BUILD_DIR)/idt.o \
	$(BUILD_DIR)/heap.o \
	$(BUILD_DIR)/keyboard.o \
	$(BUILD_DIR)/shell.o \
	$(BUILD_DIR)/vga_terminal.o \
	$(BUILD_DIR)/serial.o \
	$(BUILD_DIR)/framebuffer.o \
	$(BUILD_DIR)/font.o \
	$(BUILD_DIR)/block.o \
	$(BUILD_DIR)/vfs.o \
	$(BUILD_DIR)/ext4.o \
	$(BUILD_DIR)/ahci.o \
	$(BUILD_DIR)/nvme.o \
	$(BUILD_DIR)/pci.o \
	$(BUILD_DIR)/partition.o \
	$(BUILD_DIR)/rtl8139.o \
	$(BUILD_DIR)/net.o

$(KERNEL_ELF): $(KERNEL_OBJS)
	$(LD) $(LD_FLAGS_KERNEL) $^ -o $@

$(KERNEL_BIN): $(KERNEL_ELF) | $(EFI_BOOT_DIR)
	$(OBJCOPY) -O binary $< $@

# =========================
# UEFI bootloader build
# =========================
$(UEFI_OBJ): $(BOOT_DIR)/uefi_main.c | $(BUILD_DIR)
	$(CC) $(CC_FLAGS_UEFI) -c $< -o $@

$(UEFI_SO): $(UEFI_OBJ)
	$(LD) \
		-shared \
		-Bsymbolic \
		-L$(EFI_LIB) \
		-T $(EFI_LDS) \
		$(EFI_CRT) \
		$< \
		-o $@ \
		-lefi -lgnuefi

$(UEFI_EFI): $(UEFI_SO) $(KERNEL_BIN) | $(EFI_BOOT_DIR)
	@echo "[EFI] Creating BOOTX64.EFI..."
	@$(OBJCOPY) \
		-j .text -j .sdata -j .data -j .dynamic \
		-j .dynsym -j .rel -j .rela -j .reloc \
		--target=efi-app-x86_64 \
		$< $@
	@echo "[COPY] Copying kernel to ESP..."
	@cp $(KERNEL_BIN) $(ESP_DIR)/kernel.bin

# =========================
# Run in QEMU (UEFI)
# =========================
run: all disk
	@echo "Starting Kagami OS (GUI)..."
	@rm -f /tmp/OVMF_VARS_4M.fd
	@cp /usr/share/OVMF/OVMF_VARS_4M.fd /tmp/OVMF_VARS_4M.fd
	@qemu-system-x86_64 \
		-m 512M \
		-drive if=pflash,format=raw,readonly=on,file=/usr/share/OVMF/OVMF_CODE_4M.fd \
		-drive if=pflash,format=raw,file=/tmp/OVMF_VARS_4M.fd \
		-drive format=raw,file=fat:rw:$(ESP_DIR) \
		-device ahci,id=ahci \
		-drive if=none,id=disk,file=$(DISK_IMG),format=raw \
		-device ide-hd,drive=disk,bus=ahci.0 \
		-netdev user,id=net0 \
		-device rtl8139,netdev=net0 \
		-boot menu=on \
		-display gtk \
		-serial stdio

disk:
	@bash tools/mkimg.sh $(DISK_IMG) 10G

run-headless: all
	@echo "Starting Kagami OS (headless)..."
	@rm -f /tmp/OVMF_VARS_4M.fd
	@cp /usr/share/OVMF/OVMF_VARS_4M.fd /tmp/OVMF_VARS_4M.fd
	@qemu-system-x86_64 \
		-m 512M \
		-drive if=pflash,format=raw,readonly=on,file=/usr/share/OVMF/OVMF_CODE_4M.fd \
		-drive if=pflash,format=raw,file=/tmp/OVMF_VARS_4M.fd \
		-device loader,file=$(KERNEL_BIN),addr=0x100000 \
		-hda fat:rw:$(ESP_DIR) \
		-netdev user,id=net0 \
		-device rtl8139,netdev=net0 \
		-boot menu=on \
		-nographic

uefi-run: run

# =========================
# UEFI Shell (debug)
# =========================
uefi-shell: all
	@rm -f /tmp/OVMF_VARS_4M.fd
	@cp /usr/share/OVMF/OVMF_VARS_4M.fd /tmp/OVMF_VARS_4M.fd
	@qemu-system-x86_64 \
		-m 512M \
		-drive if=pflash,format=raw,readonly=on,file=/usr/share/OVMF/OVMF_CODE_4M.fd \
		-drive if=pflash,format=raw,file=/tmp/OVMF_VARS_4M.fd \
		-drive format=raw,file=fat:rw:$(ESP_DIR) \
		-nographic

# =========================
# Clean
# =========================
clean:
	rm -rf $(BUILD_DIR)
