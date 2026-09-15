/* Cronix OS · e1000.c: legacy descriptors, polling.
 * RX tail = last descriptor HW may use; TX tail = next to fill. */
#include "e1000.h"

#include "heap.h"
#include "io.h"
#include "pci.h"
#include "print.h"

/* Register offsets (dwords from MMIO base). */
#define R_CTRL 0x0000u
#define R_STATUS 0x0008u
#define R_RCTL 0x0100u
#define R_TCTL 0x0400u
#define R_TIPG 0x0410u
#define R_RDBAL 0x2800u
#define R_RDBAH 0x2804u
#define R_RDLEN 0x2808u
#define R_RDH 0x2810u
#define R_RDT 0x2818u
#define R_TDBAL 0x3800u
#define R_TDBAH 0x3804u
#define R_TDLEN 0x3808u
#define R_TDH 0x3810u
#define R_TDT 0x3818u
#define R_RAL0 0x5400u
#define R_RAH0 0x5404u
#define R_MTA 0x5200u /* 128 dwords of multicast filter */

#define RXN 128
#define TXN 32
#define RXBUF 2048

struct __attribute__((packed)) rx_desc {
    uint64_t addr;
    uint16_t len;
    uint16_t csum;
    uint8_t status;
    uint8_t err;
    uint16_t special;
};

struct __attribute__((packed)) tx_desc {
    uint64_t addr;
    uint16_t len;
    uint8_t cso;
    uint8_t cmd;
    uint8_t status;
    uint8_t css;
    uint16_t special;
};

static volatile uint32_t *g_mmio;
static struct rx_desc g_rx[RXN] __attribute__((aligned(16)));
static struct tx_desc g_tx[TXN] __attribute__((aligned(16)));
static uint8_t *g_rxbuf[RXN];
static uint32_t g_tx_tail;
static uint32_t g_rx_cur;

static uint32_t reg(uint32_t off)
{
    return g_mmio[off / 4];
}

static void regw(uint32_t off, uint32_t v)
{
    g_mmio[off / 4] = v;
}

/* Map one 2MB page into our identity tables (PD at 0x72000). */
static void map_2mb(uint32_t phys)
{
    volatile uint32_t *pd = (volatile uint32_t *)0x72000;
    uint32_t idx = phys >> 21;
    pd[idx] = (phys & 0xFFE00000u) | 0x93u; /* P+RW+PS+PCD (uncached) */
    /* Flush TLB: reload CR3 (needs a 64-bit operand). */
    {
        uint64_t cr3;
        __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
        __asm__ volatile("mov %0, %%cr3" : : "r"(cr3) : "memory");
    }
}

int e1000_init(void)
{
    uint8_t bus;
    uint8_t slot;
    uint8_t func;
    uint32_t bar;
    uint32_t ral;
    uint32_t rah;

    if (pci_find(0x8086, 0x100E, &bus, &slot, &func) != 0) {
        k_print("e1000: no 8086:100E NIC found\n");
        return -1;
    }
    pci_enable_mmio_master(bus, slot, func);
    bar = pci_bar_mem(bus, slot, func, 0);
    if (bar == 0) {
        k_print("e1000: BAR0 empty\n");
        return -1;
    }
    map_2mb(bar); /* MMIO lives high; map it uncached */
    g_mmio = (volatile uint32_t *)(uintptr_t)bar;

    /* Link check (STATUS.LU = bit 1). QEMU reports up. */
    if (!(reg(R_STATUS) & 0x02u)) {
        k_print("e1000: link down\n");
        return -1;
    }
    /* Drop all multicast. */
    for (uint32_t i = 0; i < 128; i++) {
        regw(R_MTA + i * 4, 0);
    }
    /* RX ring. */
    for (uint32_t i = 0; i < RXN; i++) {
        g_rxbuf[i] = kmalloc(RXBUF);
        if (g_rxbuf[i] == (uint8_t *)0) {
            return -1;
        }
        g_rx[i].addr = (uint64_t)g_rxbuf[i];
        g_rx[i].status = 0;
    }
    regw(R_RDBAL, (uint32_t)(uintptr_t)g_rx);
    regw(R_RDBAH, 0);
    regw(R_RDLEN, RXN * 16);
    regw(R_RDH, 0);
    regw(R_RDT, RXN - 1);
    g_rx_cur = 0;
    /* RCTL: EN | BAM (broadcast) | 2048B buffers | strip CRC. */
    regw(R_RCTL, (1u << 1) | (1u << 15) | (1u << 26));
    /* TX ring. */
    for (uint32_t i = 0; i < TXN; i++) {
        g_tx[i].addr = 0;
        g_tx[i].cmd = 0;
        g_tx[i].status = 0;
    }
    regw(R_TDBAL, (uint32_t)(uintptr_t)g_tx);
    regw(R_TDBAH, 0);
    regw(R_TDLEN, TXN * 16);
    regw(R_TDH, 0);
    regw(R_TDT, 0);
    g_tx_tail = 0;
    /* TCTL: EN | PSP | CT=0x0F | COLD=0x3FF. Inter-packet gap. */
    regw(R_TCTL, (1u << 1) | (1u << 3) | (0x0Fu << 4) | (0x3FFu << 12));
    regw(R_TIPG, 0x602008u);

    ral = reg(R_RAL0);
    rah = reg(R_RAH0);
    k_print("e1000: up, MMIO ");
    k_print_hex64(bar);
    k_print(" MAC ");
    {
        uint8_t m[6];
        e1000_mac(m);
        for (int i = 0; i < 6; i++) {
            uint8_t b = m[i];
            k_putc("0123456789ABCDEF"[b >> 4]);
            k_putc("0123456789ABCDEF"[b & 0xF]);
            if (i < 5) {
                k_putc(':');
            }
        }
        k_print("\n");
    }
    (void)ral;
    (void)rah;
    return 0;
}

void e1000_mac(uint8_t out[6])
{
    uint32_t ral = reg(R_RAL0);
    uint32_t rah = reg(R_RAH0);
    out[0] = (uint8_t)(ral & 0xFF);
    out[1] = (uint8_t)((ral >> 8) & 0xFF);
    out[2] = (uint8_t)((ral >> 16) & 0xFF);
    out[3] = (uint8_t)((ral >> 24) & 0xFF);
    out[4] = (uint8_t)(rah & 0xFF);
    out[5] = (uint8_t)((rah >> 8) & 0xFF);
}

int e1000_send(const uint8_t *frame, uint16_t len)
{
    uint32_t t = g_tx_tail;
    uint64_t deadline;

    if (len < 14 || len > 1514) {
        return -1;
    }
    g_tx[t].addr = (uint64_t)frame;
    g_tx[t].len = len;
    g_tx[t].cmd = 0x0Bu; /* EOP | IFCS | RS */
    g_tx[t].status = 0;
    g_tx_tail = (t + 1) % TXN;
    regw(R_TDT, g_tx_tail);
    deadline = rdtsc() + 3000000000ull;
    while (!(g_tx[t].status & 0x01u)) {
        if (rdtsc() > deadline) {
            return -1;
        }
    }
    return 0;
}

int e1000_recv(uint8_t *buf, uint16_t *len, uint64_t timeout_cycles)
{
    uint64_t deadline = rdtsc() + timeout_cycles;

    for (;;) {
        if (g_rx[g_rx_cur].status & 0x01u) {
            uint16_t n = g_rx[g_rx_cur].len;
            uint32_t i = g_rx_cur;
            if (n > RXBUF) {
                n = RXBUF;
            }
            for (uint16_t k = 0; k < n; k++) {
                buf[k] = g_rxbuf[i][k];
            }
            *len = n;
            g_rx[i].status = 0;
            g_rx_cur = (i + 1) % RXN;
            regw(R_RDT, i); /* hand it back to HW */
            return 1;
        }
        if (rdtsc() > deadline) {
            return 0;
        }
    }
}
