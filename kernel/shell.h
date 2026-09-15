#ifndef SHELL_H
#define SHELL_H

/* Cronix OS · mini-shell over PS/2 polling (ports 0x64/0x60).
 * No IRQ: reads set-1 scancodes, echoes to VGA + serial.
 * Commands: help, info, clear, halt, reboot. Never returns. */
void shell_run(void);

#endif /* SHELL_H */
