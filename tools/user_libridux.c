/*
 * libridux.c - Userspace SDK implementation for the Ridux R3 WM protocol.
 *
 * Freestanding (no libc, no glibc, no startup objects). Compiled with
 * -ffreestanding -fno-pie -nostdlib and linked into Ring 3 ELF apps.
 *
 * The drawing primitives operate directly on the framebuffer the kernel
 * mapped into our address space at rd_window_open() time. Format is
 * XRGB8888, stride = width * 4 bytes.
 */
#include "user_libridux.h"
#include "font8x16.h"

/* Wire-format struct - must match src/ridux_r3wm.h. */
typedef struct {
    const char *title;
    rd_u32 width;
    rd_u32 height;
    rd_i32 wid;
    rd_u64 fb_user_va;
    rd_u32 stride;
    rd_u32 flags;
} rd_open_args_t;

/* ---- syscall helpers (Linux ABI for write/exit/yield, Ridux for the
 *      window protocol). The kernel registers both ranges in
 *      g_syscall_table[]. ---- */

rd_i64 rd_write(int fd, const void *buf, rd_size n) {
    /* SYS_write = 1 in Linux x86-64. */
    return rd_syscall(1, (rd_u64)fd, (rd_u64)(rd_size)buf, (rd_u64)n, 0, 0, 0);
}

void rd_exit(int code) {
    /* SYS_exit_group = 231. */
    rd_syscall(231, (rd_u64)code, 0, 0, 0, 0, 0);
    /* unreachable */
    for (;;) { __asm__ __volatile__("hlt"); }
}

void rd_sleep_ticks(rd_u32 ticks) {
    /* No real sleep syscall in this tiny SDK yet. Keep the loop boring:
     * some VMs get grumpy around fancy idle instructions in CPL=3. */
    rd_u32 i;
    volatile rd_u32 spin;
    for (i = 0; i < ticks; ++i) {
        for (spin = 0; spin < 20000u; ++spin) { }
    }
}

int rd_window_open_flags(const char *title, rd_u32 w, rd_u32 h,
                         rd_u32 flags, rd_window_t *out) {
    rd_open_args_t args;
    rd_i64 rc;
    if (!out) return -1;
    args.title = title;
    args.width = w;
    args.height = h;
    args.wid = -1;
    args.fb_user_va = 0;
    args.stride = 0;
    args.flags = flags;
    rc = rd_syscall(RIDUX_SYS_WINDOW_OPEN,
                    (rd_u64)(rd_size)&args, 0, 0, 0, 0, 0);
    if (rc < 0) return (int)rc;
    out->wid = args.wid;
    out->width = args.width;
    out->height = args.height;
    out->stride = args.stride;
    out->fb = (rd_u32 *)(rd_size)args.fb_user_va;
    return 0;
}

int rd_window_open(const char *title, rd_u32 w, rd_u32 h, rd_window_t *out) {
    return rd_window_open_flags(title, w, h, 0, out);
}

int rd_window_present(rd_window_t *win, int x, int y, int w, int h) {
    if (!win) return -1;
    return (int)rd_syscall(RIDUX_SYS_WINDOW_PRESENT,
                           (rd_u64)win->wid,
                           (rd_u64)(rd_i64)x, (rd_u64)(rd_i64)y,
                           (rd_u64)(rd_i64)w, (rd_u64)(rd_i64)h, 0);
}

int rd_window_poll(rd_window_t *win, rd_event_t *ev, int max_events) {
    if (!win || !ev || max_events <= 0) return 0;
    return (int)rd_syscall(RIDUX_SYS_WINDOW_POLL,
                           (rd_u64)win->wid,
                           (rd_u64)(rd_size)ev,
                           (rd_u64)max_events, 0, 0, 0);
}

void rd_window_close(rd_window_t *win) {
    if (!win) return;
    rd_syscall(RIDUX_SYS_WINDOW_CLOSE, (rd_u64)win->wid, 0, 0, 0, 0, 0);
    win->wid = -1;
    win->fb = (rd_u32 *)0;
}

int rd_shell_exec(const char *command, char *out, rd_u32 out_len) {
    if (!command || !out || out_len == 0) return -1;
    out[0] = 0;
    return (int)rd_syscall(RIDUX_SYS_SHELL_EXEC,
                           (rd_u64)(rd_size)command,
                           (rd_u64)(rd_size)out,
                           (rd_u64)out_len, 0, 0, 0);
}

int rd_desktop_state(rd_desktop_state_t *out) {
    if (!out) return -1;
    return (int)rd_syscall(RIDUX_SYS_DESKTOP_STATE,
                           (rd_u64)(rd_size)out, 0, 0, 0, 0, 0);
}

/* ---- Drawing primitives ---- */

void rd_pixel(rd_window_t *win, int x, int y, rd_u32 color) {
    if (!win || !win->fb) return;
    if (x < 0 || y < 0 || (rd_u32)x >= win->width || (rd_u32)y >= win->height) return;
    win->fb[(rd_size)y * (win->stride / 4) + (rd_size)x] = color;
}

void rd_fill_rect(rd_window_t *win, int x, int y, int w, int h, rd_u32 color) {
    int row, col;
    rd_u32 *line;
    rd_u32 pitch_pixels;
    if (!win || !win->fb || w <= 0 || h <= 0) return;
    if (x < 0)            { w += x; x = 0; }
    if (y < 0)            { h += y; y = 0; }
    if ((rd_u32)x >= win->width)  return;
    if ((rd_u32)y >= win->height) return;
    if ((rd_u32)(x + w) > win->width)  w = (int)win->width  - x;
    if ((rd_u32)(y + h) > win->height) h = (int)win->height - y;
    if (w <= 0 || h <= 0) return;
    pitch_pixels = win->stride / 4;
    for (row = 0; row < h; ++row) {
        line = win->fb + (rd_size)(y + row) * pitch_pixels + (rd_size)x;
        for (col = 0; col < w; ++col) line[col] = color;
    }
}

void rd_clear(rd_window_t *win, rd_u32 color) {
    if (!win) return;
    rd_fill_rect(win, 0, 0, (int)win->width, (int)win->height, color);
}

/* ---- Texto con la misma fuente antialias del kernel ---- */

static rd_u32 rd_blend_xrgb(rd_u32 dst, rd_u32 src, rd_u32 alpha) {
    rd_u32 inv;
    rd_u32 sr, sg, sb, dr, dg, db;
    if (alpha == 0u) return dst;
    if (alpha >= 255u) return src & 0x00FFFFFFu;
    inv = 255u - alpha;
    sr = (src >> 16) & 255u; sg = (src >> 8) & 255u; sb = src & 255u;
    dr = (dst >> 16) & 255u; dg = (dst >> 8) & 255u; db = dst & 255u;
    return (((sr * alpha + dr * inv + 127u) / 255u) << 16) |
           (((sg * alpha + dg * inv + 127u) / 255u) << 8) |
            ((sb * alpha + db * inv + 127u) / 255u);
}

static void rd_pixel_alpha(rd_window_t *win, int x, int y, rd_u32 color, rd_u32 alpha) {
    rd_u32 *p;
    if (!win || !win->fb || alpha == 0u) return;
    if (x < 0 || y < 0 || (rd_u32)x >= win->width || (rd_u32)y >= win->height) return;
    p = win->fb + (rd_size)y * (win->stride / 4u) + (rd_size)x;
    if (alpha >= 255u) *p = color & 0x00FFFFFFu;
    else *p = rd_blend_xrgb(*p, color, alpha);
}

static rd_u8 rd_text_curve(rd_u32 cov, int scale) {
    int cut = 7;
    int v = (int)cov;
    if (scale >= 5) cut = 16;
    else if (scale >= 3) cut = 12;
    else if (scale >= 2) cut = 10;
    if (v <= cut) return 0;
    if (v >= 255 - cut) return 255;
    v = (v - cut) * 255 / (255 - cut * 2);
    if (v < 0) v = 0;
    if (v > 255) v = 255;
    return (rd_u8)v;
}

static rd_u8 rd_sample_glyph_hi(const rd_u8 glyph[RIDUX_FONT_HI_H][RIDUX_FONT_HI_W],
                                int fx, int fy) {
    int x0, x1, y0, y1;
    rd_u32 wx, wy, c00, c01, c10, c11, top, bot, cov;
    if (fx < 0) fx = 0;
    if (fy < 0) fy = 0;
    x0 = fx >> 16;
    y0 = fy >> 16;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x0 >= RIDUX_FONT_HI_W) x0 = RIDUX_FONT_HI_W - 1;
    if (y0 >= RIDUX_FONT_HI_H) y0 = RIDUX_FONT_HI_H - 1;
    x1 = x0 + 1; if (x1 >= RIDUX_FONT_HI_W) x1 = RIDUX_FONT_HI_W - 1;
    y1 = y0 + 1; if (y1 >= RIDUX_FONT_HI_H) y1 = RIDUX_FONT_HI_H - 1;
    wx = (rd_u32)(fx & 0xFFFF);
    wy = (rd_u32)(fy & 0xFFFF);
    c00 = glyph[y0][x0]; c01 = glyph[y0][x1];
    c10 = glyph[y1][x0]; c11 = glyph[y1][x1];
    top = (c00 * (65536u - wx) + c01 * wx) >> 16;
    bot = (c10 * (65536u - wx) + c11 * wx) >> 16;
    cov = (top * (65536u - wy) + bot * wy) >> 16;
    if (cov > 255u) cov = 255u;
    return (rd_u8)cov;
}

static int rd_glyph_ink_bounds(rd_u8 cc, int *left, int *right) {
    const rd_u8(*glyph)[RIDUX_FONT_W] = FONT8X16_AA[cc];
    int x, y;
    int min_x = RIDUX_FONT_W;
    int max_x = -1;
    if (cc == ' ') return 0;
    for (y = 0; y < RIDUX_FONT_H; ++y) {
        for (x = 0; x < RIDUX_FONT_W; ++x) {
            if (glyph[y][x] > 10u) {
                if (x < min_x) min_x = x;
                if (x > max_x) max_x = x;
            }
        }
    }
    if (max_x < min_x) return 0;
    *left = min_x;
    *right = max_x;
    return 1;
}

static int rd_glyph_advance(rd_u8 cc) {
    int left = 0;
    int right = 0;
    int width;
    if (cc == ' ') return 4;
    if (!rd_glyph_ink_bounds(cc, &left, &right)) return 5;
    width = right - left + 1;
    width += 1;
    if (width < 3) width = 3;
    if (width > RIDUX_FONT_W) width = RIDUX_FONT_W;
    return width;
}

static void rd_glyph_aa(rd_window_t *win, int x, int y, int scale, char ch, rd_u32 color) {
    rd_u8 cc = (rd_u8)(unsigned char)ch;
    if (scale <= 1) {
        const rd_u8(*glyph)[RIDUX_FONT_W] = FONT8X16_AA[cc];
        int row, col;
        for (row = 0; row < RIDUX_FONT_H; ++row) {
            for (col = 0; col < RIDUX_FONT_W; ++col) {
                rd_u8 cov = rd_text_curve(glyph[row][col], 1);
                if (cov) rd_pixel_alpha(win, x + col, y + row, color, cov);
            }
        }
        return;
    }
    {
        const rd_u8(*glyph)[RIDUX_FONT_HI_W] = FONT32X64_AA[cc];
        int out_w = RIDUX_FONT_W * scale;
        int out_h = RIDUX_FONT_H * scale;
        int ox, oy;
        for (oy = 0; oy < out_h; ++oy) {
            int fy = (int)(((((rd_u64)oy << 16) + 32768ull) * (rd_u64)RIDUX_FONT_HI_H) /
                           (rd_u64)out_h) - 32768;
            for (ox = 0; ox < out_w; ++ox) {
                int fx = (int)(((((rd_u64)ox << 16) + 32768ull) * (rd_u64)RIDUX_FONT_HI_W) /
                               (rd_u64)out_w) - 32768;
                rd_u8 cov = rd_text_curve(rd_sample_glyph_hi(glyph, fx, fy), scale);
                if (cov) rd_pixel_alpha(win, x + ox, y + oy, color, cov);
            }
        }
    }
}

int rd_text_width(const char *s) {
    int w = 0;
    if (!s) return 0;
    while (*s) {
        w += rd_glyph_advance((rd_u8)(unsigned char)*s);
        ++s;
    }
    return w;
}

int rd_text_width_scaled(const char *s, int scale) {
    if (scale < 1) scale = 1;
    return rd_text_width(s) * scale;
}

void rd_text(rd_window_t *win, int x, int y, const char *s, rd_u32 color) {
    if (!win || !s) return;
    while (*s) {
        int left = 0;
        int right = 0;
        rd_u8 cc = (rd_u8)(unsigned char)*s;
        if (rd_glyph_ink_bounds(cc, &left, &right)) {
            rd_glyph_aa(win, x - left, y, 1, *s, color);
        }
        x += rd_glyph_advance(cc);
        ++s;
    }
}

void rd_text_scaled(rd_window_t *win, int x, int y, int scale,
                    const char *s, rd_u32 color) {
    if (!win || !s || scale < 1) return;
    while (*s) {
        int left = 0;
        int right = 0;
        rd_u8 cc = (rd_u8)(unsigned char)*s;
        if (rd_glyph_ink_bounds(cc, &left, &right)) {
            rd_glyph_aa(win, x - left * scale, y, scale, *s, color);
        }
        x += rd_glyph_advance(cc) * scale;
        ++s;
    }
}
