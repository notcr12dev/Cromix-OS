/* Cronix OS · net.c: one-shot ARP, raw IP, stop-and-wait TCP. */
#include "net.h"

#include "e1000.h"
#include "io.h"
#include "print.h"

#define T_SHORT 1500000000ull
#define T_LONG 9000000000ull
#define MSS 1460

static const uint8_t MY_IP[4] = { 10, 0, 2, 15 };
static const uint8_t GW_IP[4] = { 10, 0, 2, 2 };
static const uint8_t DNS_IP[4] = { 10, 0, 2, 3 };

static uint8_t g_mac[6];
static uint8_t g_gwmac[6];
static int g_up;
static uint16_t g_ipid;

static uint8_t g_tx[2048];
static uint8_t g_rx[2048];

static uint16_t be16(const uint8_t *p)
{
    return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}

static void wbe16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)(v & 0xFF);
}

static uint32_t be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | p[3];
}

static void wbe32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)((v >> 16) & 0xFF);
    p[2] = (uint8_t)((v >> 8) & 0xFF);
    p[3] = (uint8_t)(v & 0xFF);
}

static int ip_eq(const uint8_t a[4], const uint8_t b[4])
{
    return a[0] == b[0] && a[1] == b[1] && a[2] == b[2] && a[3] == b[3];
}

static uint16_t checksum(const uint8_t *p, uint32_t len)
{
    uint32_t sum = 0;
    while (len > 1) {
        sum += (uint32_t)(((uint16_t)p[0] << 8) | p[1]);
        p += 2;
        len -= 2;
    }
    if (len) {
        sum += (uint32_t)((uint16_t)p[0] << 8);
    }
    while (sum >> 16) {
        sum = (sum & 0xFFFFu) + (sum >> 16);
    }
    return (uint16_t)~sum;
}

static void eth_build(uint8_t *f, const uint8_t dst[6], uint16_t type)
{
    for (int i = 0; i < 6; i++) {
        f[i] = dst[i];
        f[6 + i] = g_mac[i];
    }
    wbe16(f + 12, type);
}

/* Answer ARP requests for us (keeps SLIRP happy). */
static void arp_maybe_reply(const uint8_t *f, uint16_t flen)
{
    /* eth(14) + arp(28); op==1, target IP == ours. */
    if (flen < 42 || be16(f + 12) != 0x0806) {
        return;
    }
    if (be16(f + 20) != 1 || !ip_eq(f + 38, MY_IP)) {
        return;
    }
    {
        uint8_t r[42];
        static const uint8_t bcast[6] = { 0 };
        (void)bcast;
        for (int i = 0; i < 6; i++) {
            r[i] = f[6 + i]; /* to sender */
        }
        for (int i = 0; i < 6; i++) {
            r[6 + i] = g_mac[i];
        }
        wbe16(r + 12, 0x0806);
        wbe16(r + 14, 1);      /* hw eth */
        wbe16(r + 16, 0x0800); /* proto IP */
        r[18] = 6;
        r[19] = 4;
        wbe16(r + 20, 2); /* reply */
        for (int i = 0; i < 6; i++) {
            r[22 + i] = g_mac[i];
        }
        for (int i = 0; i < 4; i++) {
            r[28 + i] = MY_IP[i];
        }
        for (int i = 0; i < 6; i++) {
            r[32 + i] = f[6 + i];
        }
        for (int i = 0; i < 4; i++) {
            r[38 + i] = f[28 + i];
        }
        e1000_send(r, 42);
    }
}

static int arp_resolve(const uint8_t ip[4], uint8_t mac[6])
{
    uint8_t q[42];
    static const uint8_t bc[6] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };

    eth_build(q, bc, 0x0806);
    wbe16(q + 14, 1);
    wbe16(q + 16, 0x0800);
    q[18] = 6;
    q[19] = 4;
    wbe16(q + 20, 1); /* request */
    for (int i = 0; i < 6; i++) {
        q[22 + i] = g_mac[i];
    }
    for (int i = 0; i < 4; i++) {
        q[28 + i] = MY_IP[i];
    }
    for (int i = 0; i < 6; i++) {
        q[32 + i] = 0;
    }
    for (int i = 0; i < 4; i++) {
        q[38 + i] = ip[i];
    }
    for (int try = 0; try < 4; try++) {
        uint64_t deadline = rdtsc() + T_SHORT;
        e1000_send(q, 42);
        for (;;) {
            uint16_t rl;
            if (e1000_recv(g_rx, &rl, T_SHORT / 4)) {
                if (rl >= 42 && be16(g_rx + 12) == 0x0806 &&
                    be16(g_rx + 20) == 2 && ip_eq(g_rx + 38, MY_IP) &&
                    ip_eq(g_rx + 28, ip)) {
                    for (int i = 0; i < 6; i++) {
                        mac[i] = g_rx[22 + i];
                    }
                    return 0;
                }
                arp_maybe_reply(g_rx, rl);
            }
            if (rdtsc() > deadline) {
                break;
            }
        }
    }
    return -1;
}

/* Raw IPv4 send. Payload already after the 20-byte header space. */
static int ip_send(const uint8_t dst[4], uint8_t proto,
                   const uint8_t *payload, uint16_t plen)
{
    uint8_t *ip = g_tx + 14;
    uint16_t total = (uint16_t)(20 + plen);

    if ((uint32_t)20 + plen + 14 > sizeof(g_tx)) {
        return -1;
    }
    eth_build(g_tx, g_gwmac, 0x0800);
    ip[0] = 0x45;
    ip[1] = 0;
    wbe16(ip + 2, total);
    wbe16(ip + 4, g_ipid++);
    wbe16(ip + 6, 0x4000); /* DF */
    ip[8] = 64;
    ip[9] = proto;
    wbe16(ip + 10, 0);
    for (int i = 0; i < 4; i++) {
        ip[12 + i] = MY_IP[i];
        ip[16 + i] = dst[i];
    }
    for (uint16_t i = 0; i < plen; i++) {
        ip[20 + i] = payload[i];
    }
    wbe16(ip + 10, checksum(ip, 20));
    return e1000_send(g_tx, (uint16_t)(14 + total));
}

/* Wait one IPv4 packet of proto for us. Points into g_rx. */
static int ip_recv(uint8_t proto, const uint8_t **payload, uint16_t *plen,
                   uint64_t timeout)
{
    uint64_t deadline = rdtsc() + timeout;

    for (;;) {
        uint16_t rl;
        if (e1000_recv(g_rx, &rl, T_SHORT / 8)) {
            uint16_t et = be16(g_rx + 12);
            if (et == 0x0806) {
                arp_maybe_reply(g_rx, rl);
            } else if (et == 0x0800 && rl >= 34) {
                const uint8_t *ip = g_rx + 14;
                uint8_t ihl = (ip[0] & 0x0F) * 4;
                uint16_t total = be16(ip + 2);
                if (ip[9] == proto && ip_eq(ip + 16, MY_IP) &&
                    ihl >= 20 && total <= rl - 14) {
                    *payload = ip + ihl;
                    *plen = (uint16_t)(total - ihl);
                    return 0;
                }
            }
        }
        if (rdtsc() > deadline) {
            return -1;
        }
    }
}

int net_init(void)
{
    if (g_up) {
        return 0;
    }
    if (e1000_init() != 0) {
        return -1;
    }
    e1000_mac(g_mac);
    k_print("net: my IP 10.0.2.15 gw 10.0.2.2 dns 10.0.2.3\n");
    if (arp_resolve(GW_IP, g_gwmac) != 0) {
        k_print("net: gateway ARP failed\n");
        return -1;
    }
    k_print("net: gateway MAC ok\n");
    g_up = 1;
    return 0;
}

/* --- UDP (DNS transport) --- */

static int udp_send(const uint8_t dst[4], uint16_t dport,
                    const uint8_t *data, uint16_t len)
{
    static uint8_t u[1472];
    static uint16_t sport = 40000;
    if ((uint32_t)len + 8 > sizeof(u)) {
        return -1;
    }
    sport = (uint16_t)(40000 + (rdtsc() % 20000));
    wbe16(u, sport);
    wbe16(u + 2, dport);
    wbe16(u + 4, (uint16_t)(len + 8));
    wbe16(u + 6, 0);
    for (uint16_t i = 0; i < len; i++) {
        u[8 + i] = data[i];
    }
    /* checksum 0 = skipped (SLIRP accepts). */
    if (ip_send(dst, 17, u, (uint16_t)(len + 8)) != 0) {
        return -1;
    }
    return sport;
}

/* Wait UDP datagram to sport. Points into g_rx payload. */
static int udp_recv(uint16_t sport, const uint8_t **data, uint16_t *len,
                    uint64_t timeout)
{
    const uint8_t *p;
    uint16_t pl;
    if (ip_recv(17, &p, &pl, timeout) != 0) {
        return -1;
    }
    if (pl < 8 || be16(p + 2) != sport) {
        return -1; /* wrong port: drop (single socket anyway) */
    }
    *data = p + 8;
    *len = (uint16_t)(pl - 8);
    return 0;
}

int net_dns(const char *name, uint8_t out_ip[4])
{
    static uint8_t q[512];
    uint16_t txid = (uint16_t)rdtsc();
    uint32_t qlen;
    int sport;

    if (!g_up && net_init() != 0) {
        return -1;
    }
    /* Header. */
    wbe16(q, txid);
    wbe16(q + 2, 0x0100); /* recursion wanted */
    wbe16(q + 4, 1);
    wbe16(q + 6, 0);
    wbe16(q + 8, 0);
    wbe16(q + 10, 0);
    qlen = 12;
    /* QNAME labels. */
    {
        const char *s = name;
        while (*s != '\0') {
            const char *dot = s;
            uint32_t lab = 0;
            while (*dot != '\0' && *dot != '.') {
                dot++;
                lab++;
            }
            if (lab == 0 || lab > 63 || qlen + lab + 1 >= sizeof(q) - 4) {
                return -1;
            }
            q[qlen++] = (uint8_t)lab;
            for (uint32_t i = 0; i < lab; i++) {
                q[qlen++] = (uint8_t)s[i];
            }
            s = (*dot == '.') ? dot + 1 : dot;
        }
        q[qlen++] = 0;
    }
    wbe16(q + qlen, 1); /* A */
    qlen += 2;
    wbe16(q + qlen, 1); /* IN */
    qlen += 2;

    for (int try = 0; try < 3; try++) {
        const uint8_t *d;
        uint16_t dl;
        sport = udp_send(DNS_IP, 53, q, (uint16_t)qlen);
        if (sport < 0) {
            return -1;
        }
        /* Poll until our TXID shows up or time runs out. */
        {
            uint64_t deadline = rdtsc() + T_SHORT * 2;
            while (rdtsc() < deadline) {
                if (udp_recv((uint16_t)sport, &d, &dl, T_SHORT / 4) == 0 &&
                    dl >= 12 && be16(d) == txid && (d[2] & 0x80)) {
                    /* Skip question section. */
                    uint32_t o = 12;
                    while (o < dl && d[o] != 0) {
                        o += d[o] + 1;
                    }
                    o += 5; /* zero + QTYPE + QCLASS */
                    /* First A answer wins. */
                    while (o + 10 < dl) {
                        uint16_t type;
                        uint16_t rlen;
                        if ((d[o] & 0xC0) == 0xC0) {
                            o += 2; /* compressed name */
                        } else {
                            while (o < dl && d[o] != 0) {
                                o += d[o] + 1;
                            }
                            o += 1;
                        }
                        if (o + 10 > dl) {
                            break;
                        }
                        type = be16(d + o);
                        rlen = be16(d + o + 8);
                        o += 10;
                        if (type == 1 && rlen == 4 && o + 4 <= dl) {
                            for (int i = 0; i < 4; i++) {
                                out_ip[i] = d[o + i];
                            }
                            return 0;
                        }
                        o += rlen;
                    }
                    return -1; /* reply parsed, no A record */
                }
            }
        }
    }
    return -1;
}

/* --- TCP --- */

static uint16_t tcp_sum(const uint8_t sip[4], const uint8_t dip[4],
                        const uint8_t *seg, uint16_t slen)
{
    uint32_t sum = 0;
    sum += (uint32_t)(((uint16_t)sip[0] << 8) | sip[1]);
    sum += (uint32_t)(((uint16_t)sip[2] << 8) | sip[3]);
    sum += (uint32_t)(((uint16_t)dip[0] << 8) | dip[1]);
    sum += (uint32_t)(((uint16_t)dip[2] << 8) | dip[3]);
    sum += 6; /* TCP */
    sum += slen;
    for (uint16_t i = 0; i + 1 < slen; i += 2) {
        sum += (uint32_t)(((uint16_t)seg[i] << 8) | seg[i + 1]);
    }
    if (slen & 1) {
        sum += (uint32_t)((uint16_t)seg[slen - 1] << 8);
    }
    while (sum >> 16) {
        sum = (sum & 0xFFFFu) + (sum >> 16);
    }
    return (uint16_t)~sum;
}

static uint16_t g_sport;

static int tcp_tx(struct tcp_sock *s, uint32_t seq, uint32_t ack,
                  uint8_t flags, const uint8_t *data, uint16_t len)
{
    static uint8_t seg[1500];
    if ((uint32_t)len + 20 > sizeof(seg)) {
        return -1;
    }
    wbe16(seg, g_sport);
    wbe16(seg + 2, s->port);
    wbe32(seg + 4, seq);
    wbe32(seg + 8, ack);
    seg[12] = 0x50; /* data offset 5, no options */
    seg[13] = flags;
    wbe16(seg + 14, 65535);
    wbe16(seg + 16, 0);
    wbe16(seg + 18, 0);
    for (uint16_t i = 0; i < len; i++) {
        seg[20 + i] = data[i];
    }
    wbe16(seg + 16, tcp_sum(MY_IP, s->ip, seg, (uint16_t)(20 + len)));
    return ip_send(s->ip, 6, seg, (uint16_t)(20 + len));
}

/* Wait a TCP segment from s. Returns payload via statics. */
static int tcp_wait(struct tcp_sock *s, uint32_t *seq, uint32_t *ack,
                    uint8_t *flags, const uint8_t **pl, uint16_t *pln,
                    uint64_t timeout)
{
    const uint8_t *p;
    uint16_t len;
    uint64_t deadline = rdtsc() + timeout;

    while (rdtsc() < deadline) {
        if (ip_recv(6, &p, &len, T_SHORT / 8) == 0 && len >= 20) {
            uint8_t hlen = (p[12] >> 4) * 4;
            if (hlen >= 20 && be16(p) == s->port && be16(p + 2) == g_sport &&
                hlen <= len) {
                *seq = be32(p + 4);
                *ack = be32(p + 8);
                *flags = p[13];
                *pl = p + hlen;
                *pln = (uint16_t)(len - hlen);
                return 0;
            }
        }
    }
    return -1;
}

int tcp_connect(struct tcp_sock *s, const uint8_t ip[4], uint16_t port)
{
    uint32_t iss;

    if (!g_up && net_init() != 0) {
        return -1;
    }
    for (int i = 0; i < 4; i++) {
        s->ip[i] = ip[i];
    }
    s->port = port;
    g_sport = (uint16_t)(40000 + (rdtsc() % 20000));
    iss = (uint32_t)rdtsc();
    s->snd_nxt = iss + 1;
    s->open = 0;
    for (int try = 0; try < 4; try++) {
        uint32_t seq;
        uint32_t ack;
        uint8_t flags;
        const uint8_t *pl;
        uint16_t pln;
        tcp_tx(s, iss, 0, 0x02, (const uint8_t *)0, 0); /* SYN */
        if (tcp_wait(s, &seq, &ack, &flags, &pl, &pln, T_SHORT * 2) == 0 &&
            (flags & 0x12) == 0x12 && ack == iss + 1) {
            s->rcv_nxt = seq + 1;
            tcp_tx(s, s->snd_nxt, s->rcv_nxt, 0x10,
                   (const uint8_t *)0, 0); /* ACK */
            s->open = 1;
            return 0;
        }
    }
    return -1;
}

int tcp_send(struct tcp_sock *s, const uint8_t *data, uint32_t len)
{
    uint32_t off = 0;
    if (!s->open) {
        return -1;
    }
    while (off < len) {
        uint16_t chunk = (uint16_t)(len - off > MSS ? MSS : len - off);
        int ok = 0;
        for (int try = 0; try < 4 && !ok; try++) {
            uint32_t seq;
            uint32_t ack;
            uint8_t flags;
            const uint8_t *pl;
            uint16_t pln;
            tcp_tx(s, s->snd_nxt, s->rcv_nxt, 0x18, data + off, chunk);
            if (tcp_wait(s, &seq, &ack, &flags, &pl, &pln, T_SHORT) == 0 &&
                ack >= s->snd_nxt + chunk) {
                s->snd_nxt = ack;
                if (pln > 0 && seq == s->rcv_nxt) {
                    s->rcv_nxt += pln; /* piggybacked data: swallow */
                }
                tcp_tx(s, s->snd_nxt, s->rcv_nxt, 0x10,
                       (const uint8_t *)0, 0);
                ok = 1;
            }
        }
        if (!ok) {
            return -1;
        }
        off += chunk;
    }
    return 0;
}

int tcp_recv(struct tcp_sock *s, uint8_t *buf, uint32_t cap)
{
    uint64_t deadline = rdtsc() + T_LONG;
    if (!s->open) {
        return -1;
    }
    while (rdtsc() < deadline) {
        uint32_t seq;
        uint32_t ack;
        uint8_t flags;
        const uint8_t *pl;
        uint16_t pln;
        if (tcp_wait(s, &seq, &ack, &flags, &pl, &pln, T_SHORT / 4) != 0) {
            continue;
        }
        if (flags & 0x04) {
            return -1; /* RST */
        }
        if (seq == s->rcv_nxt && pln > 0) {
            uint32_t n = pln < cap ? pln : cap;
            for (uint32_t i = 0; i < n; i++) {
                buf[i] = pl[i];
            }
            s->rcv_nxt += pln;
            tcp_tx(s, s->snd_nxt, s->rcv_nxt, 0x10,
                   (const uint8_t *)0, 0);
            if (flags & 0x01) { /* FIN with data: ack it too */
                s->rcv_nxt += 1;
                tcp_tx(s, s->snd_nxt, s->rcv_nxt, 0x10,
                       (const uint8_t *)0, 0);
            }
            return (int)n;
        }
        if ((flags & 0x01) && seq == s->rcv_nxt && pln == 0) {
            s->rcv_nxt += 1;
            tcp_tx(s, s->snd_nxt, s->rcv_nxt, 0x10,
                   (const uint8_t *)0, 0);
            return 0; /* peer FIN = clean EOF */
        }
        /* Out-of-order: re-ACK current position. */
        tcp_tx(s, s->snd_nxt, s->rcv_nxt, 0x10, (const uint8_t *)0, 0);
    }
    return -1;
}

void tcp_close(struct tcp_sock *s)
{
    if (!s->open) {
        return;
    }
    tcp_tx(s, s->snd_nxt, s->rcv_nxt, 0x11, (const uint8_t *)0, 0);
    s->open = 0;
}
