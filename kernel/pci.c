/* Cronix OS · pci.c: type-1 config cycles, no BIOS help. */
#include "pci.h"

#include "io.h"

#define PCI_ADDR 0xCF8u
#define PCI_DATA 0xCFCu

static void pci_addr(uint8_t bus, uint8_t slot, uint8_t func, uint8_t off)
{
    uint32_t a = 0x80000000u | ((uint32_t)bus << 16) |
                 ((uint32_t)slot << 11) | ((uint32_t)func << 8) |
                 (off & 0xFCu);
    outl(PCI_ADDR, a);
}

uint32_t pci_read32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t off)
{
    pci_addr(bus, slot, func, off);
    return inl(PCI_DATA);
}

void pci_write32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t off,
                 uint32_t v)
{
    pci_addr(bus, slot, func, off);
    outl(PCI_DATA, v);
}

int pci_find(uint16_t ven, uint16_t dev,
             uint8_t *bus, uint8_t *slot, uint8_t *func)
{
    for (uint32_t b = 0; b < 256; b++) {
        for (uint32_t s = 0; s < 32; s++) {
            for (uint32_t f = 0; f < 8; f++) {
                uint32_t id = pci_read32((uint8_t)b, (uint8_t)s,
                                         (uint8_t)f, 0);
                if ((id & 0xFFFFu) == 0xFFFFu) {
                    continue; /* empty slot */
                }
                if ((id & 0xFFFFu) == ven && (id >> 16) == dev) {
                    *bus = (uint8_t)b;
                    *slot = (uint8_t)s;
                    *func = (uint8_t)f;
                    return 0;
                }
                if (f == 0) {
                    /* Single-function device: header type bit 7 clear. */
                    uint32_t hdr = pci_read32((uint8_t)b, (uint8_t)s, 0, 12);
                    if (!((hdr >> 16) & 0x80u)) {
                        break;
                    }
                }
            }
        }
    }
    return -1;
}

uint32_t pci_bar_mem(uint8_t bus, uint8_t slot, uint8_t func, int bar)
{
    uint32_t b = pci_read32(bus, slot, func, (uint8_t)(0x10 + bar * 4));
    return b & 0xFFFFFFF0u; /* mem BAR: low 4 bits are flags */
}

void pci_enable_mmio_master(uint8_t bus, uint8_t slot, uint8_t func)
{
    uint32_t cmd = pci_read32(bus, slot, func, 4);
    cmd |= 0x06u; /* bit1 = memory space, bit2 = bus master */
    pci_write32(bus, slot, func, 4, cmd);
}
