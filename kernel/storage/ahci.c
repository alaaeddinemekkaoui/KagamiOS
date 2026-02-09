#include "ahci.h"
#include "block.h"
#include "../bus/pci.h"
#include "../include/serial.h"

#define AHCI_CLASS 0x01
#define AHCI_SUBCLASS 0x06
#define AHCI_PROGIF 0x01

#define SATA_SIG_ATAPI 0xEB140101
#define SATA_SIG_ATA   0x00000101

#define HBA_PxIS_TFES (1 << 30)
#define HBA_PxCMD_ST  0x0001
#define HBA_PxCMD_FRE 0x0010
#define HBA_PxCMD_FR  0x4000
#define HBA_PxCMD_CR  0x8000

typedef volatile struct {
    uint32_t cap;
    uint32_t ghc;
    uint32_t is;
    uint32_t pi;
    uint32_t vs;
    uint32_t ccc_ctl;
    uint32_t ccc_pts;
    uint32_t em_loc;
    uint32_t em_ctl;
    uint32_t cap2;
    uint32_t bohc;
    uint8_t  reserved[0xA0 - 0x2C];
    uint8_t  vendor[0x100 - 0xA0];
} HBA_MEM;

typedef volatile struct {
    uint32_t clb;
    uint32_t clbu;
    uint32_t fb;
    uint32_t fbu;
    uint32_t is;
    uint32_t ie;
    uint32_t cmd;
    uint32_t rsv0;
    uint32_t tfd;
    uint32_t sig;
    uint32_t ssts;
    uint32_t sctl;
    uint32_t serr;
    uint32_t sact;
    uint32_t ci;
    uint32_t sntf;
    uint32_t fbs;
    uint32_t rsv1[11];
    uint32_t vendor[4];
} HBA_PORT;

typedef struct {
    uint8_t  cfl:5;
    uint8_t  a:1;
    uint8_t  w:1;
    uint8_t  p:1;
    uint8_t  r:1;
    uint8_t  b:1;
    uint8_t  c:1;
    uint8_t  rsv0:1;
    uint8_t  pmp:4;
    uint16_t prdtl;
    uint32_t prdbc;
    uint32_t ctba;
    uint32_t ctbau;
    uint32_t rsv1[4];
} HBA_CMD_HEADER;

typedef struct {
    uint32_t dba;
    uint32_t dbau;
    uint32_t rsv0;
    uint32_t dbc:22;
    uint32_t rsv1:9;
    uint32_t i:1;
} HBA_PRDT_ENTRY;

typedef struct {
    uint8_t  fis_type;
    uint8_t  pmport:4;
    uint8_t  rsv0:3;
    uint8_t  c:1;
    uint8_t  command;
    uint8_t  featurel;
    uint8_t  lba0;
    uint8_t  lba1;
    uint8_t  lba2;
    uint8_t  device;
    uint8_t  lba3;
    uint8_t  lba4;
    uint8_t  lba5;
    uint8_t  featureh;
    uint8_t  countl;
    uint8_t  counth;
    uint8_t  icc;
    uint8_t  control;
    uint8_t  rsv1[4];
} FIS_REG_H2D;

typedef struct {
    uint8_t  cfis[64];
    uint8_t  acmd[16];
    uint8_t  rsv[48];
    HBA_PRDT_ENTRY prdt_entry[1];
} HBA_CMD_TBL;

typedef struct {
    HBA_MEM *abar;
    HBA_PORT *port;
    BlockDevice dev;
    uint8_t port_index;
} AhciDevice;

static AhciDevice g_ahci;
static int g_ahci_ready = 0;

static void stop_cmd(HBA_PORT *port) {
    port->cmd &= ~HBA_PxCMD_ST;
    port->cmd &= ~HBA_PxCMD_FRE;
    while (port->cmd & (HBA_PxCMD_FR | HBA_PxCMD_CR)) {
    }
}

static void start_cmd(HBA_PORT *port) {
    while (port->cmd & HBA_PxCMD_CR) {
    }
    port->cmd |= HBA_PxCMD_FRE;
    port->cmd |= HBA_PxCMD_ST;
}

static int ahci_port_rebase(HBA_PORT *port, int portno) {
    stop_cmd(port);

    static uint8_t clb[32][1024] __attribute__((aligned(1024)));
    static uint8_t fb[32][256] __attribute__((aligned(256)));
    static uint8_t ctba[32][256] __attribute__((aligned(128)));

    port->clb = (uint32_t)(uint64_t)clb[portno];
    port->clbu = 0;
    port->fb = (uint32_t)(uint64_t)fb[portno];
    port->fbu = 0;

    HBA_CMD_HEADER *cmd_header = (HBA_CMD_HEADER *)(uint64_t)port->clb;
    for (int i = 0; i < 32; i++) {
        cmd_header[i].prdtl = 1;
        cmd_header[i].ctba = (uint32_t)(uint64_t)(ctba[portno] + (i * 256));
        cmd_header[i].ctbau = 0;
    }

    start_cmd(port);
    return 1;
}

static int ahci_read(HBA_PORT *port, uint64_t lba, uint32_t count, void *buffer) {
    port->is = (uint32_t)-1;

    HBA_CMD_HEADER *cmd_header = (HBA_CMD_HEADER *)(uint64_t)port->clb;
    cmd_header[0].cfl = sizeof(FIS_REG_H2D) / sizeof(uint32_t);
    cmd_header[0].w = 0;
    cmd_header[0].prdtl = 1;

    HBA_CMD_TBL *cmd_tbl = (HBA_CMD_TBL *)(uint64_t)cmd_header[0].ctba;
    for (int i = 0; i < 64; i++) {
        cmd_tbl->cfis[i] = 0;
    }

    cmd_tbl->prdt_entry[0].dba = (uint32_t)(uint64_t)buffer;
    cmd_tbl->prdt_entry[0].dbau = 0;
    cmd_tbl->prdt_entry[0].dbc = (count * 512) - 1;
    cmd_tbl->prdt_entry[0].i = 1;

    FIS_REG_H2D *cmd_fis = (FIS_REG_H2D *)cmd_tbl->cfis;
    cmd_fis->fis_type = 0x27;
    cmd_fis->c = 1;
    cmd_fis->command = 0x25; /* READ DMA EXT */
    cmd_fis->lba0 = (uint8_t)lba;
    cmd_fis->lba1 = (uint8_t)(lba >> 8);
    cmd_fis->lba2 = (uint8_t)(lba >> 16);
    cmd_fis->lba3 = (uint8_t)(lba >> 24);
    cmd_fis->lba4 = (uint8_t)(lba >> 32);
    cmd_fis->lba5 = (uint8_t)(lba >> 40);
    cmd_fis->device = 1 << 6;
    cmd_fis->countl = count & 0xFF;
    cmd_fis->counth = (count >> 8) & 0xFF;

    while (port->tfd & (0x80 | 0x08)) {
    }

    port->ci = 1;

    while (1) {
        if ((port->ci & 1) == 0) {
            break;
        }
        if (port->is & HBA_PxIS_TFES) {
            return 0;
        }
    }

    return 1;
}

static int ahci_block_read(BlockDevice *dev, uint64_t lba, uint32_t count, void *buffer) {
    AhciDevice *ahci = (AhciDevice *)dev->driver_data;
    if (!ahci || !ahci->port) {
        return 0;
    }
    return ahci_read(ahci->port, lba, count, buffer);
}

BlockDevice *ahci_get_device(void) {
    if (!g_ahci_ready) {
        return 0;
    }
    return &g_ahci.dev;
}

int ahci_init(void) {
    PciDevice dev;
    if (!pci_find_class(AHCI_CLASS, AHCI_SUBCLASS, AHCI_PROGIF, &dev)) {
        serial_write("AHCI: no controller found\n");
        return 0;
    }

    uint32_t bar5 = pci_read32(dev.bus, dev.slot, dev.func, 0x24);
    uint32_t bar5_high = 0;
    if (bar5 & 0x4) {
        bar5_high = pci_read32(dev.bus, dev.slot, dev.func, 0x28);
    }
    uint64_t abar = ((uint64_t)bar5_high << 32) | (bar5 & 0xFFFFFFF0);

    uint32_t command = pci_read32(dev.bus, dev.slot, dev.func, 0x04);
    command |= (1 << 2) | (1 << 1); /* Bus master + memory space */
    pci_write32(dev.bus, dev.slot, dev.func, 0x04, command);

    HBA_MEM *hba = (HBA_MEM *)(uint64_t)abar;
    uint32_t ports = hba->pi;

    for (uint8_t i = 0; i < 32; i++) {
        if ((ports & (1 << i)) == 0) {
            continue;
        }
        HBA_PORT *port = (HBA_PORT *)((uint8_t *)hba + 0x100 + i * sizeof(HBA_PORT));
        uint32_t sig = port->sig;
        if (sig != SATA_SIG_ATA) {
            continue;
        }

        ahci_port_rebase(port, i);

        g_ahci.abar = hba;
        g_ahci.port = port;
        g_ahci.port_index = i;

        g_ahci.dev.name = "ahci0";
        g_ahci.dev.sector_size = 512;
        g_ahci.dev.total_sectors = 0;
        g_ahci.dev.driver_data = &g_ahci;
        g_ahci.dev.read = ahci_block_read;
        g_ahci.dev.write = 0;

        g_ahci_ready = 1;
        block_register(&g_ahci.dev);
        serial_write("AHCI: SATA device ready\n");
        return 1;
    }

    serial_write("AHCI: no SATA device found\n");
    return 0;
}
