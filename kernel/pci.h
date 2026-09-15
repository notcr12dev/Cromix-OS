#ifndef PCI_H
#define PCI_H

/* Cronix OS · PCI config-space access (ports 0xCF8/0xCFC). */
#include <stdint.h>

uint32_t pci_read32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t off);
void pci_write32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t off,
                 uint32_t v);
/* First device with vendor:device. 0 on found. */
int pci_find(uint16_t ven, uint16_t dev,
             uint8_t *bus, uint8_t *slot, uint8_t *func);
uint32_t pci_bar_mem(uint8_t bus, uint8_t slot, uint8_t func, int bar);
void pci_enable_mmio_master(uint8_t bus, uint8_t slot, uint8_t func);

#endif /* PCI_H */
