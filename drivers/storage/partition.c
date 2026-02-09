#include "partition.h"
#include "serial.h"
#include "klog.h"

#define GPT_HEADER_LBA 1
#define GPT_ENTRIES_LBA 2
#define GPT_SIGNATURE 0x5452415020494645ULL

static const uint8_t GPT_LINUX_FS_GUID[16] = {
    0xAF, 0x3D, 0xC6, 0x0F,
    0x83, 0x84,
    0x72, 0x47,
    0x8E, 0x79,
    0x3D, 0x69, 0xD8, 0x47, 0x7D, 0xE4
};

#pragma pack(push, 1)
typedef struct {
    uint64_t signature;
    uint32_t revision;
    uint32_t header_size;
    uint32_t header_crc32;
    uint32_t reserved;
    uint64_t current_lba;
    uint64_t backup_lba;
    uint64_t first_usable_lba;
    uint64_t last_usable_lba;
    uint8_t disk_guid[16];
    uint64_t entries_lba;
    uint32_t num_entries;
    uint32_t entry_size;
    uint32_t entries_crc32;
} GptHeader;

typedef struct {
    uint8_t type_guid[16];
    uint8_t unique_guid[16];
    uint64_t first_lba;
    uint64_t last_lba;
    uint64_t attributes;
    uint16_t name[36];
} GptEntry;

typedef struct {
    uint8_t status;
    uint8_t chs_first[3];
    uint8_t type;
    uint8_t chs_last[3];
    uint32_t lba_first;
    uint32_t lba_count;
} MbrPartition;
#pragma pack(pop)

static int guid_equal(const uint8_t *a, const uint8_t *b) {
    for (int i = 0; i < 16; i++) {
        if (a[i] != b[i]) {
            return 0;
        }
    }
    return 1;
}

int gpt_find_linux_partition(BlockDevice *dev, PartitionInfo *out) {
    if (!dev || !dev->read || !out) {
        return 0;
    }

    uint8_t header_buf[BLOCK_SECTOR_SIZE];
    if (!dev->read(dev, GPT_HEADER_LBA, 1, header_buf)) {
        serial_write("GPT: failed to read header\n");
        KERR("GPT: failed to read header");
        return 0;
    }

    GptHeader *hdr = (GptHeader *)header_buf;
    if (hdr->signature != GPT_SIGNATURE) {
        serial_write("GPT: invalid signature\n");
        KERR("GPT: invalid signature");
        return 0;
    }

    uint32_t entry_size = hdr->entry_size;
    uint32_t num_entries = hdr->num_entries;
    uint64_t entries_lba = hdr->entries_lba;

    if (entry_size < sizeof(GptEntry) || entry_size > 4096) {
        serial_write("GPT: unsupported entry size\n");
        return 0;
    }

    uint8_t entry_buf[BLOCK_SECTOR_SIZE];
    uint32_t entries_per_sector = BLOCK_SECTOR_SIZE / entry_size;

    for (uint32_t idx = 0; idx < num_entries; idx++) {
        uint64_t lba = entries_lba + (idx / entries_per_sector);
        uint32_t offset = (idx % entries_per_sector) * entry_size;

        if (!dev->read(dev, lba, 1, entry_buf)) {
            serial_write("GPT: failed to read entry\n");
            KERR("GPT: failed to read entry");
            return 0;
        }

        GptEntry *entry = (GptEntry *)(entry_buf + offset);
        if (entry->first_lba == 0 || entry->last_lba == 0) {
            continue;
        }

        if (guid_equal(entry->type_guid, GPT_LINUX_FS_GUID)) {
            out->first_lba = entry->first_lba;
            out->last_lba = entry->last_lba;
            serial_write("GPT: found Linux filesystem partition\n");
            KLOG("GPT: found Linux filesystem partition");
            return 1;
        }
    }

    return 0;
}

int mbr_find_linux_partition(BlockDevice *dev, PartitionInfo *out) {
    if (!dev || !dev->read || !out) {
        return 0;
    }

    uint8_t mbr[BLOCK_SECTOR_SIZE];
    if (!dev->read(dev, 0, 1, mbr)) {
        serial_write("MBR: failed to read sector\n");
        KERR("MBR: failed to read sector");
        return 0;
    }

    if (mbr[510] != 0x55 || mbr[511] != 0xAA) {
        serial_write("MBR: invalid signature\n");
        KERR("MBR: invalid signature");
        return 0;
    }

    MbrPartition *parts = (MbrPartition *)(mbr + 446);
    for (int i = 0; i < 4; i++) {
        if (parts[i].type == 0x83 && parts[i].lba_count > 0) {
            out->first_lba = parts[i].lba_first;
            out->last_lba = parts[i].lba_first + parts[i].lba_count - 1;
            serial_write("MBR: found Linux partition\n");
            KLOG("MBR: found Linux partition");
            return 1;
        }
    }

    return 0;
}

int find_linux_partition(BlockDevice *dev, PartitionInfo *out) {
    if (gpt_find_linux_partition(dev, out)) {
        return 1;
    }

    if (mbr_find_linux_partition(dev, out)) {
        return 1;
    }

    if (raw_find_ext4(dev, out)) {
        return 1;
    }

    serial_write("Partition: no Linux partition found\n");
    KERR("Partition: no Linux partition found");
    return 0;
}

int raw_find_ext4(BlockDevice *dev, PartitionInfo *out) {
    if (!dev || !dev->read || !out) {
        return 0;
    }

    uint8_t buf[BLOCK_SECTOR_SIZE * 2];
    if (!dev->read(dev, 2, 2, buf)) {
        return 0;
    }

    uint16_t magic = *(uint16_t *)(buf + 56);
    if (magic != 0xEF53) {
        return 0;
    }

    out->first_lba = 0;
    out->last_lba = dev->total_sectors ? dev->total_sectors - 1 : 0;
    serial_write("Partition: raw ext4 detected\n");
    KLOG("Partition: raw ext4 detected");
    return 1;
}
