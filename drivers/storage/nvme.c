#include "nvme.h"
#include "block.h"
#include "drivers/bus/pci.h"
#include "serial.h"

#define NVME_CLASS 0x01
#define NVME_SUBCLASS 0x08
#define NVME_PROGIF 0x02

#define NVME_ADMIN_Q_DEPTH 16
#define NVME_IO_Q_DEPTH 16

#define NVME_REG_CAP   0x00
#define NVME_REG_CC    0x14
#define NVME_REG_CSTS  0x1C
#define NVME_REG_AQA   0x24
#define NVME_REG_ASQ   0x28
#define NVME_REG_ACQ   0x30

#define NVME_CC_EN     (1U << 0)
#define NVME_CSTS_RDY  (1U << 0)

#define NVME_OPC_ADMIN_CREATE_IO_CQ 0x05
#define NVME_OPC_ADMIN_CREATE_IO_SQ 0x01
#define NVME_OPC_ADMIN_IDENTIFY     0x06
#define NVME_OPC_NVM_READ           0x02
#define NVME_OPC_NVM_WRITE          0x01

typedef struct {
    uint32_t cdw0;
    uint32_t nsid;
    uint64_t rsvd2;
    uint64_t mptr;
    uint64_t prp1;
    uint64_t prp2;
    uint32_t cdw10;
    uint32_t cdw11;
    uint32_t cdw12;
    uint32_t cdw13;
    uint32_t cdw14;
    uint32_t cdw15;
} NvmeCmd;

typedef struct {
    uint32_t cdw0;
    uint32_t rsvd;
    uint16_t sq_head;
    uint16_t sq_id;
    uint16_t cid;
    uint16_t status;
} NvmeCpl;

typedef struct {
    NvmeCmd *sq;
    NvmeCpl *cq;
    uint16_t sq_tail;
    uint16_t cq_head;
    uint8_t cq_phase;
    uint16_t qid;
    uint16_t qdepth;
} NvmeQueue;

typedef struct {
    volatile uint8_t *mmio;
    NvmeQueue admin_q;
    NvmeQueue io_q;
    BlockDevice dev;
    uint32_t lba_size;
    uint64_t lba_count;
} NvmeController;

static NvmeController g_nvme;
static int g_nvme_ready = 0;

static inline uint32_t nvme_read32(volatile uint8_t *mmio, uint32_t off) {
    return *(volatile uint32_t *)(mmio + off);
}

static inline void nvme_write32(volatile uint8_t *mmio, uint32_t off, uint32_t val) {
    *(volatile uint32_t *)(mmio + off) = val;
}

static inline void nvme_write64(volatile uint8_t *mmio, uint32_t off, uint64_t val) {
    *(volatile uint64_t *)(mmio + off) = val;
}

static uint32_t nvme_db_offset(uint16_t qid, int is_cq) {
    return 0x1000 + (qid * 2 + (is_cq ? 1 : 0)) * 4;
}

static int nvme_wait_ready(volatile uint8_t *mmio, int ready) {
    for (int i = 0; i < 1000000; i++) {
        uint32_t csts = nvme_read32(mmio, NVME_REG_CSTS);
        if (((csts & NVME_CSTS_RDY) != 0) == ready) {
            return 1;
        }
    }
    return 0;
}

static int nvme_submit_cmd(NvmeController *ctrl, NvmeQueue *q, NvmeCmd *cmd, uint16_t *out_cid) {
    uint16_t cid = q->sq_tail;
    cmd->cdw0 |= cid;
    q->sq[q->sq_tail] = *cmd;
    q->sq_tail = (q->sq_tail + 1) % q->qdepth;
    nvme_write32(ctrl->mmio, nvme_db_offset(q->qid, 0), q->sq_tail);

    while (1) {
        NvmeCpl *cpl = &q->cq[q->cq_head];
        if ((cpl->status & 1) == q->cq_phase) {
            uint16_t status = (uint16_t)(cpl->status >> 1);
            if (out_cid) {
                *out_cid = cpl->cid;
            }
            q->cq_head = (q->cq_head + 1) % q->qdepth;
            if (q->cq_head == 0) {
                q->cq_phase ^= 1;
            }
            nvme_write32(ctrl->mmio, nvme_db_offset(q->qid, 1), q->cq_head);
            return status == 0;
        }
    }
}

static int nvme_identify(NvmeController *ctrl) {
    static uint8_t identify_buf[4096] __attribute__((aligned(4096)));

    NvmeCmd cmd = {0};
    cmd.cdw0 = NVME_OPC_ADMIN_IDENTIFY;
    cmd.nsid = 1;
    cmd.prp1 = (uint64_t)(uintptr_t)identify_buf;
    cmd.cdw10 = 0; /* Identify controller */

    if (!nvme_submit_cmd(ctrl, &ctrl->admin_q, &cmd, 0)) {
        return 0;
    }

    cmd = (NvmeCmd){0};
    cmd.cdw0 = NVME_OPC_ADMIN_IDENTIFY;
    cmd.nsid = 1;
    cmd.prp1 = (uint64_t)(uintptr_t)identify_buf;
    cmd.cdw10 = 1; /* Identify namespace */

    if (!nvme_submit_cmd(ctrl, &ctrl->admin_q, &cmd, 0)) {
        return 0;
    }

    uint32_t nsze = *(uint32_t *)(identify_buf + 0);
    uint32_t lbaf = identify_buf[26] & 0x0F;
    uint32_t lba_shift = identify_buf[128 + lbaf * 4 + 2];

    ctrl->lba_size = 1U << lba_shift;
    ctrl->lba_count = nsze;
    return 1;
}

static int nvme_read_blocks(NvmeController *ctrl, uint64_t lba, uint32_t count, void *buffer) {
    uint32_t lba_size = ctrl->lba_size ? ctrl->lba_size : 512;
    uint32_t bytes = count * lba_size;
    uint8_t *dst = (uint8_t *)buffer;

    while (bytes > 0) {
        uint32_t chunk = bytes > 4096 ? 4096 : bytes;
        static uint8_t prp_buf[4096] __attribute__((aligned(4096)));

        NvmeCmd cmd = {0};
        cmd.cdw0 = NVME_OPC_NVM_READ;
        cmd.nsid = 1;
        cmd.prp1 = (uint64_t)(uintptr_t)prp_buf;
        cmd.cdw10 = (uint32_t)lba;
        cmd.cdw11 = (uint32_t)(lba >> 32);
        cmd.cdw12 = (chunk / lba_size) - 1;

        if (!nvme_submit_cmd(ctrl, &ctrl->io_q, &cmd, 0)) {
            return 0;
        }

        for (uint32_t i = 0; i < chunk; i++) {
            dst[i] = prp_buf[i];
        }

        dst += chunk;
        bytes -= chunk;
        lba += chunk / lba_size;
    }

    return 1;
}

static int nvme_write_blocks(NvmeController *ctrl, uint64_t lba, uint32_t count, const void *buffer) {
    uint32_t lba_size = ctrl->lba_size ? ctrl->lba_size : 512;
    uint32_t bytes = count * lba_size;
    const uint8_t *src = (const uint8_t *)buffer;

    while (bytes > 0) {
        uint32_t chunk = bytes > 4096 ? 4096 : bytes;
        static uint8_t prp_buf[4096] __attribute__((aligned(4096)));

        for (uint32_t i = 0; i < chunk; i++) {
            prp_buf[i] = src[i];
        }

        NvmeCmd cmd = {0};
        cmd.cdw0 = NVME_OPC_NVM_WRITE;
        cmd.nsid = 1;
        cmd.prp1 = (uint64_t)(uintptr_t)prp_buf;
        cmd.cdw10 = (uint32_t)lba;
        cmd.cdw11 = (uint32_t)(lba >> 32);
        cmd.cdw12 = (chunk / lba_size) - 1;

        if (!nvme_submit_cmd(ctrl, &ctrl->io_q, &cmd, 0)) {
            return 0;
        }

        src += chunk;
        bytes -= chunk;
        lba += chunk / lba_size;
    }

    return 1;
}

static int nvme_block_read(BlockDevice *dev, uint64_t lba, uint32_t count, void *buffer) {
    NvmeController *ctrl = (NvmeController *)dev->driver_data;
    if (!ctrl) {
        return 0;
    }
    return nvme_read_blocks(ctrl, lba, count, buffer);
}

static int nvme_block_write(BlockDevice *dev, uint64_t lba, uint32_t count, const void *buffer) {
    NvmeController *ctrl = (NvmeController *)dev->driver_data;
    if (!ctrl) {
        return 0;
    }
    return nvme_write_blocks(ctrl, lba, count, buffer);
}

BlockDevice *nvme_get_device(void) {
    if (!g_nvme_ready) {
        return 0;
    }
    return &g_nvme.dev;
}

int nvme_init(void) {
    PciDevice dev;
    if (!pci_find_class(NVME_CLASS, NVME_SUBCLASS, NVME_PROGIF, &dev)) {
        serial_write("NVMe: no controller found\n");
        return 0;
    }

    uint32_t bar0 = pci_read32(dev.bus, dev.slot, dev.func, 0x10);
    uint32_t bar0_high = 0;
    if (bar0 & 0x4) {
        bar0_high = pci_read32(dev.bus, dev.slot, dev.func, 0x14);
    }
    uint64_t mmio_base = ((uint64_t)bar0_high << 32) | (bar0 & 0xFFFFFFF0);

    uint32_t command = pci_read32(dev.bus, dev.slot, dev.func, 0x04);
    command |= (1 << 2) | (1 << 1);
    pci_write32(dev.bus, dev.slot, dev.func, 0x04, command);

    g_nvme.mmio = (volatile uint8_t *)(uintptr_t)mmio_base;

    nvme_write32(g_nvme.mmio, NVME_REG_CC, 0);
    if (!nvme_wait_ready(g_nvme.mmio, 0)) {
        serial_write("NVMe: controller disable failed\n");
        return 0;
    }

    static NvmeCmd admin_sq[NVME_ADMIN_Q_DEPTH] __attribute__((aligned(4096)));
    static NvmeCpl admin_cq[NVME_ADMIN_Q_DEPTH] __attribute__((aligned(4096)));

    g_nvme.admin_q.sq = admin_sq;
    g_nvme.admin_q.cq = admin_cq;
    g_nvme.admin_q.sq_tail = 0;
    g_nvme.admin_q.cq_head = 0;
    g_nvme.admin_q.cq_phase = 1;
    g_nvme.admin_q.qid = 0;
    g_nvme.admin_q.qdepth = NVME_ADMIN_Q_DEPTH;

    nvme_write32(g_nvme.mmio, NVME_REG_AQA, ((NVME_ADMIN_Q_DEPTH - 1) << 16) | (NVME_ADMIN_Q_DEPTH - 1));
    nvme_write64(g_nvme.mmio, NVME_REG_ASQ, (uint64_t)(uintptr_t)admin_sq);
    nvme_write64(g_nvme.mmio, NVME_REG_ACQ, (uint64_t)(uintptr_t)admin_cq);

    uint32_t cc = (6 << 16) | (4 << 20) | NVME_CC_EN;
    nvme_write32(g_nvme.mmio, NVME_REG_CC, cc);
    if (!nvme_wait_ready(g_nvme.mmio, 1)) {
        serial_write("NVMe: controller enable failed\n");
        return 0;
    }

    static NvmeCmd io_sq[NVME_IO_Q_DEPTH] __attribute__((aligned(4096)));
    static NvmeCpl io_cq[NVME_IO_Q_DEPTH] __attribute__((aligned(4096)));

    g_nvme.io_q.sq = io_sq;
    g_nvme.io_q.cq = io_cq;
    g_nvme.io_q.sq_tail = 0;
    g_nvme.io_q.cq_head = 0;
    g_nvme.io_q.cq_phase = 1;
    g_nvme.io_q.qid = 1;
    g_nvme.io_q.qdepth = NVME_IO_Q_DEPTH;

    NvmeCmd cmd = {0};
    cmd.cdw0 = NVME_OPC_ADMIN_CREATE_IO_CQ;
    cmd.cdw10 = (g_nvme.io_q.qdepth - 1) | (g_nvme.io_q.qid << 16);
    cmd.cdw11 = 1 | (1 << 16);
    cmd.prp1 = (uint64_t)(uintptr_t)io_cq;
    if (!nvme_submit_cmd(&g_nvme, &g_nvme.admin_q, &cmd, 0)) {
        serial_write("NVMe: create IO CQ failed\n");
        return 0;
    }

    cmd = (NvmeCmd){0};
    cmd.cdw0 = NVME_OPC_ADMIN_CREATE_IO_SQ;
    cmd.cdw10 = (g_nvme.io_q.qdepth - 1) | (g_nvme.io_q.qid << 16);
    cmd.cdw11 = 1 | (1 << 16);
    cmd.prp1 = (uint64_t)(uintptr_t)io_sq;
    if (!nvme_submit_cmd(&g_nvme, &g_nvme.admin_q, &cmd, 0)) {
        serial_write("NVMe: create IO SQ failed\n");
        return 0;
    }

    if (!nvme_identify(&g_nvme)) {
        serial_write("NVMe: identify failed\n");
        return 0;
    }

    g_nvme.dev.name = "nvme0";
    g_nvme.dev.sector_size = g_nvme.lba_size ? g_nvme.lba_size : 512;
    g_nvme.dev.total_sectors = g_nvme.lba_count;
    g_nvme.dev.driver_data = &g_nvme;
    g_nvme.dev.read = nvme_block_read;
    g_nvme.dev.write = nvme_block_write;

    block_register(&g_nvme.dev);
    g_nvme_ready = 1;
    serial_write("NVMe: controller ready\n");
    return 1;
}
