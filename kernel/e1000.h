#ifndef E1000_H
#define E1000_H

/* Cronix OS · Intel e1000 (QEMU default NIC, 8086:100E).
 * Polling only, no interrupts. 2048-byte RX buffers.
 * All addresses identity-mapped: DMA-safe while under 16MB. */
#include <stdint.h>

int e1000_init(void); /* 0 on OK; maps MMIO, inits RX/TX rings */
void e1000_mac(uint8_t out[6]);
/* len 14..1514. Returns 0 sent, -1 timeout. */
int e1000_send(const uint8_t *frame, uint16_t len);
/* Copies one frame to buf (cap 2048). 1 = got frame, 0 = timeout. */
int e1000_recv(uint8_t *buf, uint16_t *len, uint64_t timeout_cycles);

#endif /* E1000_H */
