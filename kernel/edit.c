/* Cronix OS · edit.c: buffer + cursor + redraw + ^O/^X.
 * Own raw keyboard reader: handles 0xE0 arrows and ctrl keys. */
#include "edit.h"

#include <stdint.h>

#include "fat.h"
#include "heap.h"
#include "io.h"
#include "print.h"
#include "vga.h"

#define EDIT_MAX 32768
#define EDIT_ROWS 24 /* last VGA row = status bar */

static uint8_t *g_buf;
static uint32_t g_len;
static uint32_t g_cur;   /* cursor offset into buffer */
static uint32_t g_top;   /* first visible offset */
static int g_dirty;
static char g_path[32];

/* --- raw keys: -1 ignore, -2 up, -3 down, -4 left, -5 right,
 * 0x0F = ^O save, 0x18 = ^X quit, else char/event. --- */
static uint8_t ed_read(void)
{
    while ((inb(0x64) & 0x01) == 0) {
        __asm__ volatile("pause");
    }
    return inb(0x60);
}

static int ed_printable(uint8_t c, int shift);

static int ed_next(int *shift, int *ctrl)
{
    uint8_t c = ed_read();
    if (c == 0xE0) {
        uint8_t e = ed_read();
        if (e == 0x48) {
            return -2;
        }
        if (e == 0x50) {
            return -3;
        }
        if (e == 0x4B) {
            return -4;
        }
        if (e == 0x4D) {
            return -5;
        }
        return -1;
    }
    if (c == 0x2A || c == 0x36) {
        *shift = 1;
        return -1;
    }
    if (c == 0xAA || c == 0xB6) {
        *shift = 0;
        return -1;
    }
    if (c == 0x1D) {
        *ctrl = 1;
        return -1;
    }
    if (c == 0x9D) {
        *ctrl = 0;
        return -1;
    }
    if (c & 0x80) {
        return -1;
    }
    if (*ctrl) {
        if (c == 0x18) {
            return 0x0F; /* ^O */
        }
        if (c == 0x2D) {
            return 0x18; /* ^X */
        }
        return -1;
    }
    if (c == 0x1C) {
        return '\n';
    }
    if (c == 0x0E) {
        return '\b';
    }
    if (c == 0x39) {
        return ' ';
    }
    return ed_printable(c, *shift);
}

/* Printable set-1 letters/digits for the editor (shift aware). */
static int ed_printable(uint8_t c, int shift)
{
    static const char LO[128] = {
        [0x02] = '1', [0x03] = '2', [0x04] = '3', [0x05] = '4',
        [0x06] = '5', [0x07] = '6', [0x08] = '7', [0x09] = '8',
        [0x0A] = '9', [0x0B] = '0', [0x0C] = '-', [0x0D] = '=',
        [0x10] = 'q', [0x11] = 'w', [0x12] = 'e', [0x13] = 'r',
        [0x14] = 't', [0x15] = 'y', [0x16] = 'u', [0x17] = 'i',
        [0x18] = 'o', [0x19] = 'p', [0x1A] = '[', [0x1B] = ']',
        [0x1E] = 'a', [0x1F] = 's', [0x20] = 'd', [0x21] = 'f',
        [0x22] = 'g', [0x23] = 'h', [0x24] = 'j', [0x25] = 'k',
        [0x26] = 'l', [0x27] = ';', [0x28] = '\'', [0x29] = '`',
        [0x2B] = '\\',
        [0x2C] = 'z', [0x2D] = 'x', [0x2E] = 'c', [0x2F] = 'v',
        [0x30] = 'b', [0x31] = 'n', [0x32] = 'm', [0x33] = ',',
        [0x34] = '.', [0x35] = '/',
    };
    static const char HI[128] = {
        [0x02] = '!', [0x03] = '@', [0x04] = '#', [0x05] = '$',
        [0x06] = '%', [0x07] = '^', [0x08] = '&', [0x09] = '*',
        [0x0A] = '(', [0x0B] = ')', [0x0C] = '_', [0x0D] = '+',
        [0x10] = 'Q', [0x11] = 'W', [0x12] = 'E', [0x13] = 'R',
        [0x14] = 'T', [0x15] = 'Y', [0x16] = 'U', [0x17] = 'I',
        [0x18] = 'O', [0x19] = 'P', [0x1A] = '{', [0x1B] = '}',
        [0x1E] = 'A', [0x1F] = 'S', [0x20] = 'D', [0x21] = 'F',
        [0x22] = 'G', [0x23] = 'H', [0x24] = 'J', [0x25] = 'K',
        [0x26] = 'L', [0x27] = ':', [0x28] = '"', [0x29] = '~',
        [0x2B] = '|',
        [0x2C] = 'Z', [0x2D] = 'X', [0x2E] = 'C', [0x2F] = 'V',
        [0x30] = 'B', [0x31] = 'N', [0x32] = 'M', [0x33] = '<',
        [0x34] = '>', [0x35] = '?',
    };
    char ch;
    if (c >= 128) {
        return -1;
    }
    ch = shift ? HI[c] : LO[c];
    return ch == 0 ? -1 : (int)ch;
}

static void ed_insert(uint8_t c)
{
    if (g_len + 1 >= EDIT_MAX) {
        return;
    }
    for (uint32_t i = g_len; i > g_cur; i--) {
        g_buf[i] = g_buf[i - 1];
    }
    g_buf[g_cur++] = c;
    g_len++;
    g_dirty = 1;
}

static void ed_backspace(void)
{
    if (g_cur == 0) {
        return;
    }
    for (uint32_t i = g_cur - 1; i < g_len - 1; i++) {
        g_buf[i] = g_buf[i + 1];
    }
    g_cur--;
    g_len--;
    g_dirty = 1;
}

/* Line/col of cursor, plus keep g_top so cursor stays visible. */
static void ed_redraw(void)
{
    uint32_t line = 0;
    uint32_t col = 0;
    uint32_t i;
    uint32_t r;
    uint32_t cc;

    for (i = 0; i < g_cur; i++) {
        if (g_buf[i] == '\n') {
            line++;
            col = 0;
        } else {
            col++;
        }
    }
    if (line < g_top) {
        g_top = line;
    }
    if (line >= g_top + EDIT_ROWS) {
        g_top = line - EDIT_ROWS + 1;
    }
    vga_clear();
    /* Stream buffer cells from the first visible line. */
    i = 0;
    {
        uint32_t l = 0;
        while (i < g_len && l < g_top) {
            if (g_buf[i++] == '\n') {
                l++;
            }
        }
    }
    r = 0;
    cc = 0;
    while (i < g_len && r < EDIT_ROWS) {
        uint8_t c = g_buf[i++];
        if (c == '\n') {
            r++;
            cc = 0;
            continue;
        }
        if (cc < VGA_COLS) {
            VGA_MEM[r * VGA_COLS + cc] =
                vga_cell((char)c, vga_attr(VGA_LIGHT_GREY, VGA_BLACK));
            cc++;
        }
    }
    /* Hardware cursor on the editing position. */
    {
        uint32_t crow = line - g_top;
        uint32_t ccol = col;
        if (ccol > VGA_COLS - 1) {
            ccol = VGA_COLS - 1;
        }
        if (crow > EDIT_ROWS - 1) {
            crow = EDIT_ROWS - 1;
        }
        k_setcursor((uint8_t)ccol, (uint8_t)crow);
    }
    /* Status bar. */
    k_color(VGA_BLACK, VGA_LIGHT_GREY);
    /* paint last row */
    for (uint32_t x = 0; x < VGA_COLS; x++) {
        VGA_MEM[24 * VGA_COLS + x] =
            vga_cell(' ', vga_attr(VGA_BLACK, VGA_LIGHT_GREY));
    }
    {
        const char *s = "^O save  ^X quit  ";
        uint32_t x = 0;
        while (*s != '\0') {
            VGA_MEM[24 * VGA_COLS + x++] =
                vga_cell(*s++, vga_attr(VGA_BLACK, VGA_LIGHT_GREY));
        }
        s = g_path;
        while (*s != '\0' && x < VGA_COLS - 8) {
            VGA_MEM[24 * VGA_COLS + x++] =
                vga_cell(*s++, vga_attr(VGA_BLACK, VGA_LIGHT_GREY));
        }
        if (g_dirty) {
            const char *m = "[+]";
            while (*m != '\0' && x < VGA_COLS) {
                VGA_MEM[24 * VGA_COLS + x++] =
                    vga_cell(*m++, vga_attr(VGA_BLACK, VGA_LIGHT_GREY));
            }
        }
    }
    k_color(VGA_LIGHT_GREY, VGA_BLACK);
}

static int ed_save(void)
{
    char dir[16];
    char file[16];
    int di = 0;
    int fi = 0;
    const char *p = g_path;

    if (p[0] != '/') {
        return -1;
    }
    p++;
    while (*p != '\0' && *p != '/' && di < 12) {
        char c = *p++;
        if (c >= 'a' && c <= 'z') {
            c -= (char)32;
        }
        dir[di++] = c;
    }
    dir[di] = '\0';
    if (*p == '/') {
        p++;
    }
    while (*p != '\0' && fi < 12) {
        file[fi++] = *p++;
    }
    file[fi] = '\0';
    if (dir[0] == '\0' || file[0] == '\0') {
        return -1;
    }
    if (fat_write(dir, file, g_buf, g_len) != 0) {
        return -1;
    }
    g_dirty = 0;
    return 0;
}

void edit_file(const char *path)
{
    uint8_t *loaded = (uint8_t *)0;
    uint32_t llen = 0;
    char dir[16];
    char file[16];
    int di = 0;
    int fi = 0;
    const char *p = path;
    int shift = 0;
    int ctrl = 0;
    int quit_arm = 0;

    /* Split path for the initial load. */
    if (p[0] == '/') {
        p++;
    }
    while (*p != '\0' && *p != '/' && di < 12) {
        char c = *p++;
        if (c >= 'a' && c <= 'z') {
            c -= (char)32;
        }
        dir[di++] = c;
    }
    dir[di] = '\0';
    if (*p == '/') {
        p++;
    }
    while (*p != '\0' && fi < 12) {
        file[fi++] = *p++;
    }
    file[fi] = '\0';

    g_buf = kmalloc(EDIT_MAX);
    if (g_buf == (uint8_t *)0) {
        k_print("edit: out of memory\n");
        return;
    }
    g_len = 0;
    g_cur = 0;
    g_top = 0;
    g_dirty = 0;
    {
        int i = 0;
        while (path[i] != '\0' && i < 31) {
            g_path[i] = path[i];
            i++;
        }
        g_path[i] = '\0';
    }
    if (dir[0] != '\0' && file[0] != '\0') {
        if (fat_read(dir, file, &loaded, &llen) == 0) {
            uint32_t n = llen < EDIT_MAX - 1 ? llen : EDIT_MAX - 1;
            for (uint32_t i = 0; i < n; i++) {
                g_buf[i] = loaded[i];
            }
            g_len = n;
            g_cur = n;
            kfree(loaded);
        }
    }

    ed_redraw();
    for (;;) {
        int ev = ed_next(&shift, &ctrl);
        if (ev == -1) {
            continue;
        }
        if (ev == -2) { /* up */
            uint32_t col = 0;
            uint32_t i = g_cur;
            while (i > 0 && g_buf[i - 1] != '\n') {
                i--;
                col++;
            }
            if (i > 0) { /* there is a line above */
                uint32_t end = i - 1; /* the '\n' */
                uint32_t start = end;
                while (start > 0 && g_buf[start - 1] != '\n') {
                    start--;
                }
                uint32_t ll = end - start;
                g_cur = start + (col < ll ? col : ll);
            }
            quit_arm = 0;
        } else if (ev == -3) { /* down */
            uint32_t i = g_cur;
            uint32_t col = 0;
            uint32_t j = g_cur;
            while (j > 0 && g_buf[j - 1] != '\n') {
                j--;
                col++;
            }
            while (i < g_len && g_buf[i] != '\n') {
                i++;
            }
            if (i < g_len) {
                uint32_t start = i + 1;
                uint32_t end = start;
                while (end < g_len && g_buf[end] != '\n') {
                    end++;
                }
                uint32_t ll = end - start;
                g_cur = start + (col < ll ? col : ll);
            }
            quit_arm = 0;
        } else if (ev == -4) { /* left */
            if (g_cur > 0) {
                g_cur--;
            }
            quit_arm = 0;
        } else if (ev == -5) { /* right */
            if (g_cur < g_len) {
                g_cur++;
            }
            quit_arm = 0;
        } else if (ev == '\b') {
            ed_backspace();
            quit_arm = 0;
        } else if (ev == '\n') {
            ed_insert('\n');
            quit_arm = 0;
        } else if (ev == 0x0F) { /* ^O */
            if (ed_save() == 0) {
                k_print("\n[saved]\n");
            } else {
                k_print("\n[save FAILED]\n");
            }
            /* re-arm loop redraws anyway */
            quit_arm = 0;
        } else if (ev == 0x18) { /* ^X */
            if (g_dirty && !quit_arm) {
                quit_arm = 1; /* press ^X again to quit unsaved */
            } else {
                break;
            }
        } else { /* printable char */
            ed_insert((uint8_t)ev);
            quit_arm = 0;
        }
        ed_redraw();
    }
    kfree(g_buf);
    g_buf = (uint8_t *)0;
    k_color(VGA_LIGHT_GREY, VGA_BLACK);
    vga_clear();
}
