#ifndef SHELL_H
#define SHELL_H

/* DEV-OS · mini-shell por polling PS/2 (puertos 0x64/0x60).
 * Sin IRQ: lee scancodes set 1, eco en VGA + serie.
 * Comandos: help, info, clear, halt, reboot. No retorna. */
void shell_run(void);

#endif /* SHELL_H */
