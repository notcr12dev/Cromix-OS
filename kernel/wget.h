#ifndef WGET_H
#define WGET_H

/* Cronix OS · wget: HTTP/1.0 GET over the minimal TCP stack.
 * http:// URLs only (no HTTPS/TLS). Saves body to a FAT path.
 * Needs QEMU user networking (SLIRP). 0 on OK, -1 on error. */
int wget_run(const char *url, const char *path);

#endif /* WGET_H */
