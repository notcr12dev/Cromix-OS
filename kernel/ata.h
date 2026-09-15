#ifndef ATA_H
#define ATA_H

/* Cronix OS · ATA PIO driver (primary bus, master). LBA28.
 * Polling only, no IRQ/DMA. Works on QEMU HDD images.
 * count: 1-255 sectors. Returns 0 on OK, -1 on device error. */
#include <stdint.h>

int ata_read(uint32_t lba, uint8_t count, void *buf);
int ata_write(uint32_t lba, uint8_t count, const void *buf);

#endif /* ATA_H */
