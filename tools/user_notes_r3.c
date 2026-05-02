/*
 * notes-r3.c - First Ring 3 RiduxOS app using the native R3 WM protocol.
 *
 * Opens a 480x300 window via libridux, renders a simple notes UI, lets
 * the user click to drop colored "ink" dots, and exits cleanly when the
 * close button is pressed. Proves the end-to-end CPL=3 -> kernel WM ->
 * shared framebuffer -> screen path.
 *
 * Compiled as a freestanding ELF64 binary; lives at /bin/notes-r3.elf in
 * the rootfs and is launched from the Ring 3 Demo applet.
 */

#include "user_libridux.h"

#define WIN_W 480
#define WIN_H 300

#define COL_BG       0x1A1B26u  /* deep slate */
#define COL_PANEL    0x24283Bu
#define COL_ACCENT   0x7AA2F7u  /* azure */
#define COL_TEXT     0xC0CAF5u  /* off-white */
#define COL_MUTED    0x565F89u  /* dim text */
#define COL_INK_A    0xF7768Eu  /* coral */
#define COL_INK_B    0x9ECE6Au  /* mint */
#define COL_INK_C    0xE0AF68u  /* amber */

static rd_u32 g_seed = 0xC0FFEE11u;

static rd_u32 lcg(void) {
    g_seed = g_seed * 1664525u + 1013904223u;
    return g_seed;
}

static void draw_static_chrome(rd_window_t *win, int dot_count) {
    char hdr[48];
    int i;
    char dot_buf[24];
    int v;

    rd_clear(win, COL_BG);

    /* Top accent strip */
    rd_fill_rect(win, 0, 0, WIN_W, 4, COL_ACCENT);

    /* Header band */
    rd_fill_rect(win, 0, 8, WIN_W, 44, COL_PANEL);
    rd_text_scaled(win, 16, 18, 2, "Notes  Ring 3", COL_TEXT);

    /* Subtitle */
    rd_text(win, 16, 60, "User-mode CPL=3 ELF rendering via shared FB", COL_MUTED);
    rd_text(win, 16, 76, "Click anywhere below to drop ink. Close when done.",
            COL_MUTED);

    /* Canvas frame */
    rd_fill_rect(win, 12, 96, WIN_W - 24, WIN_H - 96 - 12, COL_PANEL);
    rd_fill_rect(win, 14, 98, WIN_W - 28, WIN_H - 96 - 16, 0x101218u);

    /* Counter */
    for (i = 0; i < (int)sizeof(hdr); ++i) hdr[i] = 0;
    {
        const char *p = "ink dots: ";
        int j = 0;
        while (p[j]) { hdr[j] = p[j]; ++j; }
        v = dot_count;
        if (v < 0) v = 0;
        if (v == 0) {
            hdr[j++] = '0';
        } else {
            int k = 0;
            while (v > 0 && k < (int)sizeof(dot_buf) - 1) {
                dot_buf[k++] = '0' + (v % 10);
                v /= 10;
            }
            while (k > 0) hdr[j++] = dot_buf[--k];
        }
        hdr[j] = 0;
    }
    rd_text(win, WIN_W - 120, 30, hdr, COL_ACCENT);
}

static void drop_dot(rd_window_t *win, int x, int y) {
    rd_u32 c;
    int r;
    /* Clamp to canvas region */
    if (x < 18) x = 18;
    if (y < 102) y = 102;
    if (x > WIN_W - 18) x = WIN_W - 18;
    if (y > WIN_H - 18) y = WIN_H - 18;
    switch (lcg() % 3u) {
        case 0:  c = COL_INK_A; break;
        case 1:  c = COL_INK_B; break;
        default: c = COL_INK_C; break;
    }
    /* Soft "ink dot": filled square with a darker outline. */
    r = 5 + (int)(lcg() % 4u);
    rd_fill_rect(win, x - r - 1, y - r - 1, 2 * r + 2, 2 * r + 2, 0x000000u);
    rd_fill_rect(win, x - r,     y - r,     2 * r,     2 * r,     c);
}

int main(void) {
    rd_window_t win;
    rd_event_t  events[16];
    int rc, n, i;
    int dot_count = 0;
    int running = 1;
    int dirty   = 1;

    rd_write(1, "notes-r3: starting\n", 19);

    rc = rd_window_open("Notes Ring 3", WIN_W, WIN_H, &win);
    if (rc < 0) {
        rd_write(1, "notes-r3: window_open failed\n", 29);
        return 1;
    }
    rd_write(1, "notes-r3: window opened\n", 24);

    while (running) {
        if (dirty) {
            draw_static_chrome(&win, dot_count);
            rd_window_present(&win, 0, 0, WIN_W, WIN_H);
            dirty = 0;
        }

        n = rd_window_poll(&win, events, 16);
        for (i = 0; i < n; ++i) {
            rd_event_t *e = &events[i];
            if (e->type == RIDUX_EVENT_CLOSE) {
                running = 0;
                break;
            }
            if (e->type == RIDUX_EVENT_MOUSE_DOWN) {
                drop_dot(&win, e->x, e->y);
                ++dot_count;
                /* Partial present so we send only the dot region. */
                rd_window_present(&win, e->x - 12, e->y - 12, 24, 24);
                dirty = 1;  /* refresh counter on next frame */
            }
            if (e->type == RIDUX_EVENT_KEY_DOWN) {
                /* ESC closes the window (scancode 1 on PS/2). */
                if (e->scancode == 1u) running = 0;
            }
        }

        rd_sleep_ticks(1);
    }

    rd_write(1, "notes-r3: exiting\n", 18);
    rd_window_close(&win);
    return 0;
}
