/* Cronix OS · ata.c: PIO data-in/out, one command per call. */
#include "ata.h"

#include "io.h"

#define P_DATA 0x1F0u
#define P_COUNT 0x1F2u
#define P_LBA0 0x1F3u
#define P_LBA1 0x1F4u
#define P_LBA2 0x1F5u
#define P_DRIVE 0x1F6u
#define P_STATUS 0x1F7u
#define P_CMD P_STATUS

#define ST_BSY 0x80u
#define ST_RDY 0x40u
#define ST_DRQ 0x08u
#define ST_ERR 0x01u

static void ata_wait_ready(void)
{
    while (inb(P_STATUS) & ST_BSY) {
        /* spin */
    }
}

static void ata_select(uint32_t lba)
{
    outb(P_DRIVE, (uint8_t)(0xE0 | ((lba >> 24) & 0x0F)));
    /* 400ns delay: 4 reads of the alt-status port. */
    inb(0x3F6u);
    inb(0x3F6u);
    inb(0x3F6u);
    inb(0x3F6u);
}

static int ata_wait_drq(void)
{
    for (;;) {
        uint8_t s = inb(P_STATUS);
        if (s & ST_ERR) {
            return -1;
        }
        if ((s & (ST_BSY | ST_DRQ)) == ST_DRQ) {
            return 0;
        }
    }
}

int ata_read(uint32_t lba, uint8_t count, void *buf)
{
    uint16_t *p = (uint16_t *)buf;

    ata_wait_ready();
    ata_select(lba);
    outb(P_COUNT, count);
    outb(P_LBA0, (uint8_t)(lba & 0xFF));
    outb(P_LBA1, (uint8_t)((lba >> 8) & 0xFF));
    outb(P_LBA2, (uint8_t)((lba >> 16) & 0xFF));
    outb(P_CMD, 0x20); /* READ SECTORS */
    for (uint8_t s = 0; s < count; s++) {
        if (ata_wait_drq() != 0) {
            return -1;
        }
        for (int i = 0; i < 256; i++) {
            p[s * 256 + i] = inw(P_DATA);
        }
    }
    return 0;
}

int ata_write(uint32_t lba, uint8_t count, const void *buf)
{
    const uint16_t *p = (const uint16_t *)buf;

    ata_wait_ready();
    ata_select(lba);
    outb(P_COUNT, count);
    outb(P_LBA0, (uint8_t)(lba & 0xFF));
    outb(P_LBA1, (uint8_t)((lba >> 8) & 0xFF));
    outb(P_LBA2, (uint8_t)((lba >> 16) & 0xFF));
    outb(P_CMD, 0x30); /* WRITE SECTORS */
    for (uint8_t s = 0; s < count; s++) {
        if (ata_wait_drq() != 0) {
            return -1;
        }
        for (int i = 0; i < 256; i++) {
            outw(P_DATA, p[s * 256 + i]);
        }
    }
    outb(P_CMD, 0xE7); /* CACHE FLUSH */
    ata_wait_ready();
    return 0;
}
