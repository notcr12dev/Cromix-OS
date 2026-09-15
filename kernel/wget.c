/* Cronix OS · wget.c: parse URL, DNS, GET, save body. */
#include "wget.h"

#include "heap.h"
#include "net.h"
#include "print.h"
#include "vfs.h"

#define WGET_MAX (1024 * 1024)

static int is_digit(char c)
{
    return c >= '0' && c <= '9';
}

/* Dotted decimal -> ip. 0 on OK. */
static int parse_ip(const char *s, uint8_t ip[4])
{
    for (int part = 0; part < 4; part++) {
        uint32_t v = 0;
        int digits = 0;
        while (is_digit(*s)) {
            v = v * 10 + (uint32_t)(*s - '0');
            s++;
            digits++;
        }
        if (digits == 0 || v > 255) {
            return -1;
        }
        ip[part] = (uint8_t)v;
        if (part < 3) {
            if (*s != '.') {
                return -1;
            }
            s++;
        }
    }
    return *s == '\0' || *s == '/' || *s == ':' ? 0 : -1;
}

/* Split "http://host[:port]/path". host_out 64B, file_out 96B. */
static int parse_url(const char *url, char *host_out, uint16_t *port,
                     char *file_out)
{
    const char *p = url;
    int hi = 0;
    int fi = 0;
    const char *scheme = "http://";
    for (int i = 0; scheme[i] != '\0'; i++) {
        if (p[i] != scheme[i]) {
            return -1;
        }
    }
    p += 7;
    *port = 80;
    while (*p != '\0' && *p != '/' && *p != ':' && hi < 63) {
        host_out[hi++] = *p++;
    }
    host_out[hi] = '\0';
    if (hi == 0) {
        return -1;
    }
    if (*p == ':') {
        uint32_t v = 0;
        p++;
        if (!is_digit(*p)) {
            return -1;
        }
        while (is_digit(*p)) {
            v = v * 10 + (uint32_t)(*p - '0');
            p++;
        }
        if (v == 0 || v > 65535) {
            return -1;
        }
        *port = (uint16_t)v;
    }
    if (*p == '\0') {
        file_out[0] = '/';
        file_out[1] = '\0';
        return 0;
    }
    while (*p != '\0' && fi < 95) {
        file_out[fi++] = *p++;
    }
    file_out[fi] = '\0';
    return file_out[0] == '/' ? 0 : -1;
}

int wget_run(const char *url, const char *path)
{
    char host[64];
    char file[96];
    uint16_t port;
    uint8_t ip[4];
    struct tcp_sock sock;
    uint8_t *body;
    uint32_t got = 0;
    char req[256];
    int ri = 0;
    int hdr_end = -1;

    if (path[0] == '\0') {
        k_print("wget: usage: wget http://host/file /home/file\n");
        return -1;
    }
    if (parse_url(url, host, &port, file) != 0) {
        k_print("wget: bad URL (want http://host[:port]/path)\n");
        return -1;
    }
    k_print("wget: resolving ");
    k_print(host);
    k_print("...\n");
    if (parse_ip(host, ip) != 0) {
        if (net_dns(host, ip) != 0) {
            k_print("wget: DNS failed\n");
            return -1;
        }
    }
    k_print("wget: connecting...\n");
    if (tcp_connect(&sock, ip, port) != 0) {
        k_print("wget: connect failed\n");
        return -1;
    }
    /* GET request. */
    {
        const char *a = "GET ";
        const char *b = " HTTP/1.0\r\nHost: ";
        const char *c = "\r\nUser-Agent: cronix-wget\r\n\r\n";
        while (*a != '\0' && ri < 250) {
            req[ri++] = *a++;
        }
        a = file;
        while (*a != '\0' && ri < 250) {
            req[ri++] = *a++;
        }
        a = b;
        while (*a != '\0' && ri < 250) {
            req[ri++] = *a++;
        }
        a = host;
        while (*a != '\0' && ri < 250) {
            req[ri++] = *a++;
        }
        a = c;
        while (*a != '\0' && ri < 250) {
            req[ri++] = *a++;
        }
    }
    if (tcp_send(&sock, (const uint8_t *)req, (uint32_t)ri) != 0) {
        k_print("wget: send failed\n");
        tcp_close(&sock);
        return -1;
    }
    body = kmalloc(WGET_MAX);
    if (body == (uint8_t *)0) {
        k_print("wget: out of memory\n");
        tcp_close(&sock);
        return -1;
    }
    k_print("wget: downloading...\n");
    for (;;) {
        uint8_t chunk[1500];
        int n = tcp_recv(&sock, chunk, sizeof(chunk));
        if (n < 0) {
            k_print("wget: transfer timeout\n");
            kfree(body);
            tcp_close(&sock);
            return -1;
        }
        if (n == 0) {
            break; /* peer FIN */
        }
        if (got + (uint32_t)n > WGET_MAX) {
            k_print("wget: file too big (>1MB)\n");
            kfree(body);
            tcp_close(&sock);
            return -1;
        }
        for (int i = 0; i < n; i++) {
            body[got++] = chunk[i];
        }
        /* Find end of HTTP headers once we have enough. */
        if (hdr_end < 0 && got > 4) {
            for (uint32_t i = 0; i + 3 < got; i++) {
                if (body[i] == '\r' && body[i + 1] == '\n' &&
                    body[i + 2] == '\r' && body[i + 3] == '\n') {
                    hdr_end = (int)(i + 4);
                    break;
                }
            }
        }
    }
    tcp_close(&sock);
    if (hdr_end < 0) {
        k_print("wget: bad HTTP reply\n");
        kfree(body);
        return -1;
    }
    /* Status check: "HTTP/1.x 200". */
    if (!(got >= 12 && body[9] == '2' && body[10] == '0' && body[11] == ' ')) {
        k_print("wget: server error reply\n");
        kfree(body);
        return -1;
    }
    {
        uint32_t blen = got - (uint32_t)hdr_end;
        uint8_t *bstart = body + hdr_end;
        if (vfs_save(path, bstart, blen) != 0) {
            k_print("wget: save failed (path? disk full?)\n");
            kfree(body);
            return -1;
        }
        k_print("wget: saved ");
        k_print_hex64(blen);
        k_print(" bytes to ");
        k_print(path);
        k_print("\n");
    }
    kfree(body);
    return 0;
}
