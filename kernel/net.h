#ifndef NET_H
#define NET_H

/* Cronix OS · minimal net: ARP + IPv4 + TCP client + DNS.
 * Static SLIRP config: IP 10.0.2.15/24, GW 10.0.2.2, DNS 10.0.2.3.
 * Polling, one TCP socket at a time, no RX queue: every recv
 * pumps ARP replies too. QEMU user networking required. */
#include <stdint.h>

int net_init(void); /* e1000 + gateway ARP; idempotent, 0 on OK */
int net_dns(const char *name, uint8_t out_ip[4]); /* A record */

struct tcp_sock {
    uint8_t ip[4];
    uint16_t port;
    uint32_t snd_nxt;
    uint32_t rcv_nxt;
    int open;
};

int tcp_connect(struct tcp_sock *s, const uint8_t ip[4], uint16_t port);
int tcp_send(struct tcp_sock *s, const uint8_t *data, uint32_t len);
int tcp_recv(struct tcp_sock *s, uint8_t *buf, uint32_t cap);
/* >0 data bytes, 0 = peer FIN, -1 = timeout/error */
void tcp_close(struct tcp_sock *s);

#endif /* NET_H */
