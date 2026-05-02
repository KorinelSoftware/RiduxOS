#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "font8x16.h"
#include "assets.h"

#define FB_MAX_WIDTH  1920
#define FB_MAX_HEIGHT 1080
#define FLUSH_MAX_COMMANDS 16384
#define FLUSH_MAX_TEXT     160
#define FLUSH_MAX_SCISSOR  8
#define GLASS_CORNER_MAX   64
#define GLASS_FLAG_NOISE    0x01u
#define GLASS_FLAG_TOP_GLOW 0x02u

#ifndef RIDUX_FONT_W
#define RIDUX_FONT_W 8
#endif
#ifndef RIDUX_FONT_H
#define RIDUX_FONT_H 16
#endif
#ifndef RIDUX_FONT_HI_W
#define RIDUX_FONT_HI_W 32
#endif
#ifndef RIDUX_FONT_HI_H
#define RIDUX_FONT_HI_H 64
#endif

typedef struct {
    uint8_t *address;
    uint32_t pitch;
    uint32_t width;
    uint32_t height;
    uint8_t  bpp;
    uint8_t  red_pos, red_size;
    uint8_t  green_pos, green_size;
    uint8_t  blue_pos, blue_size;
    bool     ready;
} framebuffer_t;

typedef struct {
    int x, y, w, h;
} ui_rect_t;

typedef enum {
    FLUSH_CMD_CLEAR = 0,
    FLUSH_CMD_RECT,
    FLUSH_CMD_ROUND_RECT,
    FLUSH_CMD_ROUND_RECT4,
    FLUSH_CMD_STROKE_RECT,
    FLUSH_CMD_STROKE_ROUND,
    FLUSH_CMD_VGRADIENT,
    FLUSH_CMD_HGRADIENT,
    FLUSH_CMD_RADIAL,
    FLUSH_CMD_CIRCLE,
    FLUSH_CMD_RING,
    FLUSH_CMD_LINE,
    FLUSH_CMD_TEXT,
    FLUSH_CMD_TEXT_SCALED,
    FLUSH_CMD_IMAGE,
    FLUSH_CMD_IMAGE_TINT,
    FLUSH_CMD_BLUR,
    FLUSH_CMD_GLASS,
    FLUSH_CMD_SHADOW,
    FLUSH_CMD_NOISE,
    FLUSH_CMD_SCISSOR_PUSH,
    FLUSH_CMD_SCISSOR_POP
} flush_cmd_type_t;

typedef struct {
    flush_cmd_type_t type;
    int      x, y, w, h;
    int      radius;
    int      radius2;
    int      x2, y2;
    uint32_t color_a;
    uint32_t color_b;
    uint8_t  alpha;
    uint8_t  tint_alpha;
    uint8_t  scale;
    uint8_t  flags;
    const ridux_image_t *image;
    char text[FLUSH_MAX_TEXT];
} flush_cmd_t;

extern framebuffer_t g_fb;
extern bool g_use_backbuffer;
extern bool g_fb_fast_bgra;
extern uint32_t g_backbuffer[FB_MAX_WIDTH * FB_MAX_HEIGHT];

static flush_cmd_t g_flush_queue[FLUSH_MAX_COMMANDS];
static size_t      g_flush_count;
static uint32_t    g_blur_scratch[FB_MAX_WIDTH];
static ui_rect_t   g_scissor_stack[FLUSH_MAX_SCISSOR];
static int         g_scissor_top;
static uint32_t    g_glass_corner_tl[GLASS_CORNER_MAX * GLASS_CORNER_MAX];
static uint32_t    g_glass_corner_tr[GLASS_CORNER_MAX * GLASS_CORNER_MAX];
static uint32_t    g_glass_corner_bl[GLASS_CORNER_MAX * GLASS_CORNER_MAX];
static uint32_t    g_glass_corner_br[GLASS_CORNER_MAX * GLASS_CORNER_MAX];

static void *k_memset(void *dst, int value, size_t size) {
    uint8_t *p = (uint8_t *)dst;
    size_t i;
    for (i = 0; i < size; ++i) p[i] = (uint8_t)value;
    return dst;
}
static void *k_memcpy(void *dst, const void *src, size_t size) {
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    size_t i;
    for (i = 0; i < size; ++i) d[i] = s[i];
    return dst;
}
static size_t k_strlen(const char *s) {
    size_t n = 0;
    while (s[n]) ++n;
    return n;
}
static void k_strlcpy(char *dst, const char *src, size_t dst_size) {
    size_t i = 0;
    if (dst_size == 0) return;
    while (i + 1 < dst_size && src[i]) { dst[i] = src[i]; ++i; }
    dst[i] = '\0';
}
static int k_max_i(int a, int b) { return a > b ? a : b; }
static int k_min_i(int a, int b) { return a < b ? a : b; }
static int k_abs_i(int x) { return x < 0 ? -x : x; }
static uint32_t k_isqrt(uint32_t x) {
    uint32_t r = 0;
    uint32_t bit = 1u << 30;
    while (bit > x) bit >>= 2;
    while (bit != 0) {
        if (x >= r + bit) {
            x -= r + bit;
            r = (r >> 1) + bit;
        } else {
            r >>= 1;
        }
        bit >>= 2;
    }
    return r;
}

/* ============================================================
 * [4] Framebuffer low-level + scissor
 * ============================================================ */

static uint32_t channel_mask(uint8_t bits) {
    if (bits == 0) return 0u;
    if (bits >= 32) return 0xFFFFFFFFu;
    return (1u << bits) - 1u;
}
static uint8_t unpack_channel(uint32_t pixel, uint8_t pos, uint8_t bits) {
    uint32_t mask = channel_mask(bits), raw;
    if (mask == 0) return 0;
    raw = (pixel >> pos) & mask;
    return (uint8_t)((raw * 255u) / mask);
}
static uint32_t pack_channel(uint8_t v, uint8_t pos, uint8_t bits) {
    uint32_t mask = channel_mask(bits), scaled;
    if (mask == 0) return 0;
    scaled = ((uint32_t)v * mask + 127u) / 255u;
    return (scaled & mask) << pos;
}

/* ---- Fast path helpers for 32bpp BGRA (0x00RRGGBB). No divisions. ---- */
static inline uint32_t fast_rgb(uint8_t r, uint8_t g, uint8_t b) {
    return ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}
/* Classic SIMD-in-a-register alpha blend using channel-pair masks. */
static inline uint32_t fast_blend_bgra(uint32_t dst, uint32_t src, uint32_t a) {
    /* a is 0..255; use 256-a as inverse so we can shift by 8 instead of /255. */
    uint32_t inv = 256u - a;
    uint32_t s_rb = (src & 0x00FF00FFu);
    uint32_t s_g  = (src & 0x0000FF00u);
    uint32_t d_rb = (dst & 0x00FF00FFu);
    uint32_t d_g  = (dst & 0x0000FF00u);
    uint32_t o_rb = ((s_rb * a + d_rb * inv) >> 8) & 0x00FF00FFu;
    uint32_t o_g  = ((s_g  * a + d_g  * inv) >> 8) & 0x0000FF00u;
    return o_rb | o_g;
}

uint32_t rgb(uint8_t r, uint8_t g, uint8_t b) {
    if (g_fb_fast_bgra) return fast_rgb(r, g, b);
    return pack_channel(r, g_fb.red_pos, g_fb.red_size) |
           pack_channel(g, g_fb.green_pos, g_fb.green_size) |
           pack_channel(b, g_fb.blue_pos, g_fb.blue_size);
}
uint32_t rgb_hex(uint32_t hex) {
    if (g_fb_fast_bgra) return hex & 0x00FFFFFFu;
    return rgb((uint8_t)(hex >> 16), (uint8_t)(hex >> 8), (uint8_t)hex);
}
uint32_t blend_color(uint32_t dst, uint32_t src, uint8_t alpha) {
    uint8_t sr, sg, sb, dr, dg, db, rr, rg, rb;
    if (g_fb_fast_bgra) return fast_blend_bgra(dst, src, (uint32_t)alpha);
    sr = unpack_channel(src, g_fb.red_pos, g_fb.red_size);
    sg = unpack_channel(src, g_fb.green_pos, g_fb.green_size);
    sb = unpack_channel(src, g_fb.blue_pos, g_fb.blue_size);
    dr = unpack_channel(dst, g_fb.red_pos, g_fb.red_size);
    dg = unpack_channel(dst, g_fb.green_pos, g_fb.green_size);
    db = unpack_channel(dst, g_fb.blue_pos, g_fb.blue_size);
    rr = (uint8_t)(((uint16_t)sr * alpha + (uint16_t)dr * (255u - alpha)) / 255u);
    rg = (uint8_t)(((uint16_t)sg * alpha + (uint16_t)dg * (255u - alpha)) / 255u);
    rb = (uint8_t)(((uint16_t)sb * alpha + (uint16_t)db * (255u - alpha)) / 255u);
    return rgb(rr, rg, rb);
}

static bool scissor_contains(int x, int y) {
    if (g_scissor_top <= 0) return true;
    ui_rect_t r = g_scissor_stack[g_scissor_top - 1];
    return x >= r.x && y >= r.y && x < r.x + r.w && y < r.y + r.h;
}
static void scissor_push(ui_rect_t r) {
    if (g_scissor_top < FLUSH_MAX_SCISSOR) {
        if (g_scissor_top > 0) {
            ui_rect_t p = g_scissor_stack[g_scissor_top - 1];
            int x = k_max_i(p.x, r.x);
            int y = k_max_i(p.y, r.y);
            int x2 = k_min_i(p.x + p.w, r.x + r.w);
            int y2 = k_min_i(p.y + p.h, r.y + r.h);
            r.x = x; r.y = y;
            r.w = k_max_i(0, x2 - x);
            r.h = k_max_i(0, y2 - y);
        }
        g_scissor_stack[g_scissor_top++] = r;
    }
}
static void scissor_pop(void) {
    if (g_scissor_top > 0) --g_scissor_top;
}

static uint32_t fb_get_pixel(int x, int y) {
    size_t idx;
    uint8_t *p;
    if (!g_fb.ready || x < 0 || y < 0 || (uint32_t)x >= g_fb.width || (uint32_t)y >= g_fb.height) return 0u;
    if (g_use_backbuffer) {
        idx = (size_t)y * FB_MAX_WIDTH + (size_t)x;
        return g_backbuffer[idx];
    }
    p = g_fb.address + (size_t)y * g_fb.pitch + (size_t)x * 4u;
    return *((uint32_t *)p);
}
static void fb_put_pixel(int x, int y, uint32_t color) {
    size_t idx;
    uint8_t *p;
    if (!g_fb.ready || x < 0 || y < 0 || (uint32_t)x >= g_fb.width || (uint32_t)y >= g_fb.height) return;
    if (!scissor_contains(x, y)) return;
    if (g_use_backbuffer) {
        idx = (size_t)y * FB_MAX_WIDTH + (size_t)x;
        g_backbuffer[idx] = color;
        return;
    }
    p = g_fb.address + (size_t)y * g_fb.pitch + (size_t)x * 4u;
    *((uint32_t *)p) = color;
}
static void fb_blend_pixel(int x, int y, uint32_t color, uint8_t alpha) {
    uint32_t dst;
    if (alpha == 0) return;
    if (alpha == 255) { fb_put_pixel(x, y, color); return; }
    dst = fb_get_pixel(x, y);
    fb_put_pixel(x, y, blend_color(dst, color, alpha));
}
/* Snapshot of the last presented frame, used to compute per-row dirty
 * spans so fb_present only writes the changed portion of each row to
 * the MMIO framebuffer. This is the single biggest perf win for VBox /
 * QEMU style guests where MMIO writes are orders of magnitude slower
 * than RAM-to-RAM copies. */
static uint32_t g_last_presented[FB_MAX_WIDTH * FB_MAX_HEIGHT];
static bool     g_last_presented_valid;

void fb_present_invalidate(void) {
    g_last_presented_valid = false;
}

/* ----------------------------------------------------------------
 * Cursor underbuffer: a tiny scratch area used by the WM cursor
 * fast-path. The WM saves the clean composited pixels at the
 * cursor position into here BEFORE drawing the cursor sprite.
 * On the next frame, restore() repaints those clean pixels back
 * into g_backbuffer, erasing the old cursor without needing to
 * re-composite the whole scene.
 * ---------------------------------------------------------------- */
#define FLUSH_CURSOR_MAX 32
static uint32_t g_cursor_under[FLUSH_CURSOR_MAX * FLUSH_CURSOR_MAX];
static int      g_cursor_under_x;
static int      g_cursor_under_y;
static int      g_cursor_under_w;
static int      g_cursor_under_h;
static bool     g_cursor_under_valid;

void flush_cursor_save_under(int x, int y, int w, int h) {
    int i, j;
    g_cursor_under_valid = false;
    if (!g_fb.ready || !g_use_backbuffer) return;
    if (w > FLUSH_CURSOR_MAX) w = FLUSH_CURSOR_MAX;
    if (h > FLUSH_CURSOR_MAX) h = FLUSH_CURSOR_MAX;
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (w <= 0 || h <= 0) return;
    if ((uint32_t)(x + w) > g_fb.width)  w = (int)g_fb.width  - x;
    if ((uint32_t)(y + h) > g_fb.height) h = (int)g_fb.height - y;
    if (w <= 0 || h <= 0) return;
    for (j = 0; j < h; ++j) {
        uint32_t *src = g_backbuffer + (size_t)(y + j) * FB_MAX_WIDTH + (size_t)x;
        uint32_t *dst = g_cursor_under + (size_t)j * FLUSH_CURSOR_MAX;
        for (i = 0; i < w; ++i) dst[i] = src[i];
    }
    g_cursor_under_x = x; g_cursor_under_y = y;
    g_cursor_under_w = w; g_cursor_under_h = h;
    g_cursor_under_valid = true;
}

void flush_cursor_restore_under(void) {
    int i, j;
    if (!g_cursor_under_valid || !g_fb.ready || !g_use_backbuffer) return;
    for (j = 0; j < g_cursor_under_h; ++j) {
        uint32_t *src = g_cursor_under + (size_t)j * FLUSH_CURSOR_MAX;
        uint32_t *dst = g_backbuffer
                      + (size_t)(g_cursor_under_y + j) * FB_MAX_WIDTH
                      + (size_t)g_cursor_under_x;
        for (i = 0; i < g_cursor_under_w; ++i) dst[i] = src[i];
    }
    g_cursor_under_valid = false;
}

void flush_cursor_under_invalidate(void) {
    g_cursor_under_valid = false;
}

void fb_present(void) {
    uint32_t y;
    if (!g_fb.ready || !g_use_backbuffer) return;

    if (!g_last_presented_valid) {
        for (y = 0; y < g_fb.height; ++y) {
            uint8_t *dst = g_fb.address + (size_t)y * g_fb.pitch;
            uint32_t *src = g_backbuffer + (size_t)y * FB_MAX_WIDTH;
            uint32_t *snap = g_last_presented + (size_t)y * FB_MAX_WIDTH;
            k_memcpy(dst, src, (size_t)g_fb.width * 4u);
            k_memcpy(snap, src, (size_t)g_fb.width * 4u);
        }
        g_last_presented_valid = true;
        return;
    }

    /* Diff each row in 64-bit chunks: find the leftmost / rightmost
     * differing pair-of-pixels, then memcpy that span (and update the
     * snapshot for that span only). For frames where only the cursor
     * moves, this drops the per-frame MMIO traffic from ~8 MB to a few
     * KB and is what makes the cursor feel instantaneous. */
    for (y = 0; y < g_fb.height; ++y) {
        uint32_t *cur  = g_backbuffer    + (size_t)y * FB_MAX_WIDTH;
        uint32_t *snap = g_last_presented + (size_t)y * FB_MAX_WIDTH;
        uint64_t *cur64  = (uint64_t *)cur;
        uint64_t *snap64 = (uint64_t *)snap;
        uint32_t pairs = (g_fb.width + 1u) >> 1;
        int first = -1, last = -1;
        uint32_t i;
        for (i = 0; i < pairs; ++i) {
            if (cur64[i] != snap64[i]) {
                if (first < 0) first = (int)i;
                last = (int)i;
            }
        }
        if (first < 0) continue;
        {
            int dirty_start = first * 2;
            int dirty_end   = (last + 1) * 2;
            size_t bytes;
            if ((uint32_t)dirty_end > g_fb.width) dirty_end = (int)g_fb.width;
            bytes = (size_t)(dirty_end - dirty_start) * 4u;
            k_memcpy(g_fb.address + (size_t)y * g_fb.pitch + (size_t)dirty_start * 4u,
                     cur + dirty_start, bytes);
            k_memcpy(snap + dirty_start, cur + dirty_start, bytes);
        }
    }
}

/* ============================================================
 * [5] Flush graphics API (draw primitives + queue)
 * ============================================================ */

/* Fast horizontal row fill that respects the active scissor and uses
 * the BGRA fast path on the backbuffer. The inner loops are tight so
 * solid rectangles (which dominate the WM) compose in a single tight
 * pass per row instead of per-pixel function calls. */
static void fill_row_alpha(int x, int y, int w, uint32_t color, uint8_t alpha) {
    int x0, x1, n, i;
    if (alpha == 0 || w <= 0 || !g_fb.ready) return;
    if (y < 0 || (uint32_t)y >= g_fb.height) return;
    x0 = x; x1 = x + w;
    if (g_scissor_top > 0) {
        ui_rect_t r = g_scissor_stack[g_scissor_top - 1];
        if (y < r.y || y >= r.y + r.h) return;
        if (x0 < r.x) x0 = r.x;
        if (x1 > r.x + r.w) x1 = r.x + r.w;
    }
    if (x0 < 0) x0 = 0;
    if ((uint32_t)x1 > g_fb.width) x1 = (int)g_fb.width;
    if (x0 >= x1) return;
    n = x1 - x0;
    if (g_use_backbuffer) {
        uint32_t *row = &g_backbuffer[(size_t)y * FB_MAX_WIDTH + (size_t)x0];
        if (alpha == 255) {
            for (i = 0; i < n; ++i) row[i] = color;
            return;
        }
        if (g_fb_fast_bgra) {
            uint32_t a = alpha;
            for (i = 0; i < n; ++i) row[i] = fast_blend_bgra(row[i], color, a);
            return;
        }
        for (i = 0; i < n; ++i) row[i] = blend_color(row[i], color, alpha);
        return;
    }
    /* Direct framebuffer (no backbuffer) — slower per-pixel path. */
    for (i = 0; i < n; ++i) fb_blend_pixel(x0 + i, y, color, alpha);
}

void draw_rect_alpha(int x, int y, int w, int h, uint32_t color, uint8_t alpha) {
    int iy;
    if (w <= 0 || h <= 0 || alpha == 0) return;
    for (iy = 0; iy < h; ++iy) fill_row_alpha(x, y + iy, w, color, alpha);
}

static void draw_stroke_rect(int x, int y, int w, int h, int thickness, uint32_t color, uint8_t alpha) {
    int t = thickness < 1 ? 1 : thickness;
    if (w <= 0 || h <= 0) return;
    draw_rect_alpha(x, y, w, t, color, alpha);
    draw_rect_alpha(x, y + h - t, w, t, color, alpha);
    draw_rect_alpha(x, y, t, h, color, alpha);
    draw_rect_alpha(x + w - t, y, t, h, color, alpha);
}

void draw_vgradient_alpha(int x, int y, int w, int h, uint32_t top, uint32_t bottom, uint8_t alpha) {
    int iy;
    if (w <= 0 || h <= 0 || alpha == 0) return;
    for (iy = 0; iy < h; ++iy) {
        uint8_t tr = unpack_channel(top, g_fb.red_pos, g_fb.red_size);
        uint8_t tg = unpack_channel(top, g_fb.green_pos, g_fb.green_size);
        uint8_t tb = unpack_channel(top, g_fb.blue_pos, g_fb.blue_size);
        uint8_t br = unpack_channel(bottom, g_fb.red_pos, g_fb.red_size);
        uint8_t bg = unpack_channel(bottom, g_fb.green_pos, g_fb.green_size);
        uint8_t bb = unpack_channel(bottom, g_fb.blue_pos, g_fb.blue_size);
        int denom = (h == 1) ? 1 : (h - 1);
        uint8_t rr = (uint8_t)((int)tr + ((int)(br - tr) * iy) / denom);
        uint8_t rg = (uint8_t)((int)tg + ((int)(bg - tg) * iy) / denom);
        uint8_t rb = (uint8_t)((int)tb + ((int)(bb - tb) * iy) / denom);
        uint32_t color = rgb(rr, rg, rb);
        draw_rect_alpha(x, y + iy, w, 1, color, alpha);
    }
}

static void draw_hgradient_alpha(int x, int y, int w, int h, uint32_t left, uint32_t right, uint8_t alpha) {
    int ix;
    if (w <= 0 || h <= 0 || alpha == 0) return;
    for (ix = 0; ix < w; ++ix) {
        uint8_t lr = unpack_channel(left, g_fb.red_pos, g_fb.red_size);
        uint8_t lg = unpack_channel(left, g_fb.green_pos, g_fb.green_size);
        uint8_t lb = unpack_channel(left, g_fb.blue_pos, g_fb.blue_size);
        uint8_t rr2 = unpack_channel(right, g_fb.red_pos, g_fb.red_size);
        uint8_t rg2 = unpack_channel(right, g_fb.green_pos, g_fb.green_size);
        uint8_t rb2 = unpack_channel(right, g_fb.blue_pos, g_fb.blue_size);
        int denom = (w == 1) ? 1 : (w - 1);
        uint8_t r = (uint8_t)((int)lr + ((int)(rr2 - lr) * ix) / denom);
        uint8_t g = (uint8_t)((int)lg + ((int)(rg2 - lg) * ix) / denom);
        uint8_t b = (uint8_t)((int)lb + ((int)(rb2 - lb) * ix) / denom);
        draw_rect_alpha(x + ix, y, 1, h, rgb(r, g, b), alpha);
    }
}

static void draw_radial_alpha(int cx, int cy, int radius, uint32_t inner, uint32_t outer, uint8_t alpha) {
    int y, x;
    if (radius <= 0 || alpha == 0) return;
    for (y = -radius; y <= radius; ++y) {
        for (x = -radius; x <= radius; ++x) {
            int d2 = x * x + y * y;
            int r2 = radius * radius;
            if (d2 <= r2) {
                uint32_t d = k_isqrt((uint32_t)d2);
                uint8_t t = (uint8_t)((d * 255u) / (uint32_t)(radius == 0 ? 1 : radius));
                uint32_t c = blend_color(outer, inner, (uint8_t)(255u - t));
                fb_blend_pixel(cx + x, cy + y, c, alpha);
            }
        }
    }
}

/* ---- Sub-pixel anti-aliasing helpers ----
 *
 * All coverage helpers use 4×4 supersampling at eighth-of-a-pixel
 * offsets. This delivers 17 distinct coverage levels (0..16/16) along
 * curve borders, which is enough to make rounded corners and circles
 * look smooth (no more visible "steps") even at small radii. The math
 * stays in 32-bit integer space because dx, dy and r are bounded by
 * the screen dimensions and we compute (8r)² + safety which is well
 * under INT32_MAX for any sensible radius (r ≤ 1024 → 64×r² ≤ 6.7e7).
 *
 * Performance: results are deterministic per (dx_abs, dy_abs, r) so we
 * cache them in a per-radius lookup table on first use. Subsequent
 * lookups become a single byte load per pixel, which is what makes
 * rounded panels effectively free to draw at full resolution.
 */
static const int FLUSH_SSAA_OFF[4] = { -3, -1, 1, 3 };

#define COV_LUT_MAX_R 32
static uint8_t g_cov_lut[COV_LUT_MAX_R + 1][COV_LUT_MAX_R + 1][COV_LUT_MAX_R + 1];
static uint8_t g_cov_lut_ready[COV_LUT_MAX_R + 1];

static uint8_t cov_compute(int dx_abs, int dy_abs, int r) {
    int x8, y8, r2_64;
    int fx, fy, nx, ny;
    int hits, sx, sy;
    if (r <= 0) return 0;
    x8 = dx_abs * 8;
    y8 = dy_abs * 8;
    r2_64 = r * r * 64;
    fx = x8 + 3; fy = y8 + 3;
    if (fx * fx + fy * fy <= r2_64) return 255;
    nx = x8 - 3; if (nx < 0) nx = 0;
    ny = y8 - 3; if (ny < 0) ny = 0;
    if (nx * nx + ny * ny > r2_64) return 0;
    hits = 0;
    for (sy = 0; sy < 4; ++sy) {
        int q = y8 + FLUSH_SSAA_OFF[sy];
        int yy = q * q;
        for (sx = 0; sx < 4; ++sx) {
            int p = x8 + FLUSH_SSAA_OFF[sx];
            if (p * p + yy <= r2_64) ++hits;
        }
    }
    return (uint8_t)((hits * 255) / 16);
}

static void cov_lut_fill(int r) {
    int dx, dy;
    for (dx = 0; dx <= r; ++dx)
        for (dy = 0; dy <= r; ++dy)
            g_cov_lut[r][dx][dy] = cov_compute(dx, dy, r);
    g_cov_lut_ready[r] = 1;
}

static uint8_t cov_lookup(int dx_abs, int dy_abs, int r) {
    if (r <= 0) return 0;
    if (dx_abs < 0) dx_abs = -dx_abs;
    if (dy_abs < 0) dy_abs = -dy_abs;
    if (r <= COV_LUT_MAX_R && dx_abs <= r && dy_abs <= r) {
        if (!g_cov_lut_ready[r]) cov_lut_fill(r);
        return g_cov_lut[r][dx_abs][dy_abs];
    }
    return cov_compute(dx_abs, dy_abs, r);
}

/* Coverage of a unit pixel centered at (x, y) inside a circle of radius
 * r centered at the origin. Returns 0..255. */
static uint8_t circle_pixel_cov(int x, int y, int r) {
    return cov_lookup(x, y, r);
}

static void draw_circle_alpha(int cx, int cy, int radius, uint32_t color, uint8_t alpha) {
    int y, x;
    if (radius <= 0 || alpha == 0) return;
    if (radius == 1) {
        fb_blend_pixel(cx, cy, color, alpha);
        return;
    }
    for (y = -radius; y <= radius; ++y) {
        for (x = -radius; x <= radius; ++x) {
            uint8_t cov = circle_pixel_cov(x, y, radius);
            uint8_t a;
            if (!cov) continue;
            a = (cov == 255 && alpha == 255) ? 255
                : (uint8_t)(((uint16_t)cov * alpha + 127u) / 255u);
            if (!a) continue;
            fb_blend_pixel(cx + x, cy + y, color, a);
        }
    }
}

static void draw_ring_alpha(int cx, int cy, int radius, int thickness, uint32_t color, uint8_t alpha) {
    int y, x;
    int inner;
    if (radius <= 0 || thickness <= 0 || alpha == 0) return;
    if (thickness > radius) thickness = radius;
    inner = radius - thickness;
    for (y = -radius; y <= radius; ++y) {
        for (x = -radius; x <= radius; ++x) {
            uint8_t cov_out = circle_pixel_cov(x, y, radius);
            uint8_t cov_in  = (inner > 0) ? circle_pixel_cov(x, y, inner) : 0;
            uint8_t cov = (cov_out > cov_in) ? (uint8_t)(cov_out - cov_in) : 0;
            uint8_t a;
            if (!cov) continue;
            a = (uint8_t)(((uint16_t)cov * alpha + 127u) / 255u);
            if (!a) continue;
            fb_blend_pixel(cx + x, cy + y, color, a);
        }
    }
}

/* Coverage of a corner pixel of a rounded rect. dx, dy are positive
 * integer distances from the corner-curve center toward the pixel
 * (1 = innermost pixel of the corner, r = corner-most pixel). */
static uint8_t rounded_corner_cov(int dx, int dy, int r) {
    if (r <= 0) return 255;
    return cov_lookup(dx, dy, r);
}

static uint8_t rounded_pixel_cov(int ix, int iy, int w, int h, int r) {
    if (r <= 0) return 255;
    if (ix < r && iy < r) return rounded_corner_cov(r - ix, r - iy, r);
    if (ix >= w - r && iy < r) return rounded_corner_cov(ix - (w - r - 1), r - iy, r);
    if (ix < r && iy >= h - r) return rounded_corner_cov(r - ix, iy - (h - r - 1), r);
    if (ix >= w - r && iy >= h - r) return rounded_corner_cov(ix - (w - r - 1), iy - (h - r - 1), r);
    return 255;
}

/* Helper: AA-rasterize one r×r corner of a rounded rect.
 * `corner`:  0=TL, 1=TR, 2=BR, 3=BL.
 * Coordinates are the bounding rect (x, y, w, h). */
static void rasterize_corner(int x, int y, int w, int h, int r, int corner,
                             uint32_t color, uint8_t alpha) {
    int iy, ix;
    if (r <= 0) return;
    for (iy = 0; iy < r; ++iy) {
        int dy = r - iy;
        for (ix = 0; ix < r; ++ix) {
            int dx = r - ix;
            uint8_t cov = rounded_corner_cov(dx, dy, r);
            uint8_t a;
            int px, py;
            if (!cov) continue;
            a = (cov == 255 && alpha == 255) ? 255
                : (uint8_t)(((uint16_t)cov * alpha + 127u) / 255u);
            if (!a) continue;
            switch (corner) {
                case 0: px = x + ix;             py = y + iy;             break;
                case 1: px = x + w - 1 - ix;     py = y + iy;             break;
                case 2: px = x + w - 1 - ix;     py = y + h - 1 - iy;     break;
                default: px = x + ix;            py = y + h - 1 - iy;     break;
            }
            fb_blend_pixel(px, py, color, a);
        }
    }
}

/* Per-corner-radii rounded rectangle fill. The four radii (r_tl, r_tr,
 * r_br, r_bl) can independently be 0 (square corner) up to min(w,h)/2.
 * Uniform draw_rounded_rect_alpha delegates here. */
static void draw_rounded_rect_alpha4(int x, int y, int w, int h,
                                     int r_tl, int r_tr, int r_br, int r_bl,
                                     uint32_t color, uint8_t alpha) {
    int top_max, bot_max;
    int half;
    if (w <= 0 || h <= 0 || alpha == 0) return;
    if (r_tl < 0) r_tl = 0;
    if (r_tr < 0) r_tr = 0;
    if (r_br < 0) r_br = 0;
    if (r_bl < 0) r_bl = 0;
    half = (w < h ? w : h) / 2;
    if (r_tl > half) r_tl = half;
    if (r_tr > half) r_tr = half;
    if (r_br > half) r_br = half;
    if (r_bl > half) r_bl = half;
    /* Clamp adjacent-corner sums against the side they share. */
    if (r_tl + r_tr > w) { r_tl = w / 2; r_tr = w - r_tl; }
    if (r_bl + r_br > w) { r_bl = w / 2; r_br = w - r_bl; }
    if (r_tl + r_bl > h) { r_tl = h / 2; r_bl = h - r_tl; }
    if (r_tr + r_br > h) { r_tr = h / 2; r_br = h - r_tr; }

    top_max = r_tl > r_tr ? r_tl : r_tr;
    bot_max = r_bl > r_br ? r_bl : r_br;

    /* Middle band: rows that aren't constrained by any corner curve. */
    if (h > top_max + bot_max)
        draw_rect_alpha(x, y + top_max, w, h - top_max - bot_max, color, alpha);

    /* Top band fills (between TL and TR corners + slabs under shorter
     * top corners so the band lines up with the taller one). */
    if (top_max > 0) {
        if (w > r_tl + r_tr)
            draw_rect_alpha(x + r_tl, y, w - r_tl - r_tr, top_max, color, alpha);
        if (r_tl < top_max)
            draw_rect_alpha(x, y + r_tl, r_tl, top_max - r_tl, color, alpha);
        if (r_tr < top_max)
            draw_rect_alpha(x + w - r_tr, y + r_tr, r_tr, top_max - r_tr, color, alpha);
    }
    /* Bottom band, mirror of top. */
    if (bot_max > 0) {
        if (w > r_bl + r_br)
            draw_rect_alpha(x + r_bl, y + h - bot_max, w - r_bl - r_br, bot_max, color, alpha);
        if (r_bl < bot_max)
            draw_rect_alpha(x, y + h - bot_max, r_bl, bot_max - r_bl, color, alpha);
        if (r_br < bot_max)
            draw_rect_alpha(x + w - r_br, y + h - bot_max, r_br, bot_max - r_br, color, alpha);
    }

    /* Per-corner SSAA. */
    rasterize_corner(x, y, w, h, r_tl, 0, color, alpha);
    rasterize_corner(x, y, w, h, r_tr, 1, color, alpha);
    rasterize_corner(x, y, w, h, r_br, 2, color, alpha);
    rasterize_corner(x, y, w, h, r_bl, 3, color, alpha);
}

/* Uniform-radius rounded rect — thin wrapper. */
static void draw_rounded_rect_alpha(int x, int y, int w, int h, int r, uint32_t color, uint8_t alpha) {
    draw_rounded_rect_alpha4(x, y, w, h, r, r, r, r, color, alpha);
}

static void draw_rounded_rect_outline(int x, int y, int w, int h, int r, int thickness,
                                      uint32_t color, uint8_t alpha) {
    int iy, ix;
    int t = thickness < 1 ? 1 : thickness;
    int ir;
    if (w <= 0 || h <= 0 || alpha == 0) return;
    if (r < 0) r = 0;
    if (r * 2 > w) r = w / 2;
    if (r * 2 > h) r = h / 2;
    if (t * 2 > w) t = w / 2;
    if (t * 2 > h) t = h / 2;
    if (t < 1) t = 1;

    if (r == 0) {
        draw_rect_alpha(x, y, w, t, color, alpha);
        draw_rect_alpha(x, y + h - t, w, t, color, alpha);
        if (h > 2 * t) {
            draw_rect_alpha(x, y + t, t, h - 2 * t, color, alpha);
            draw_rect_alpha(x + w - t, y + t, t, h - 2 * t, color, alpha);
        }
        return;
    }
    /* Straight strokes between the corner curves. */
    if (w > 2 * r) {
        draw_rect_alpha(x + r, y,             w - 2 * r, t, color, alpha);
        draw_rect_alpha(x + r, y + h - t,     w - 2 * r, t, color, alpha);
    }
    if (h > 2 * r) {
        draw_rect_alpha(x,         y + r,     t, h - 2 * r, color, alpha);
        draw_rect_alpha(x + w - t, y + r,     t, h - 2 * r, color, alpha);
    }
    /* Corner ring slice = outer disc minus inner disc, both AA'd. */
    ir = r - t;
    if (ir < 0) ir = 0;
    for (iy = 0; iy < r; ++iy) {
        int dy = r - iy;
        for (ix = 0; ix < r; ++ix) {
            int dx = r - ix;
            uint8_t cov_out = rounded_corner_cov(dx, dy, r);
            uint8_t cov_in  = (ir > 0) ? rounded_corner_cov(dx, dy, ir) : 0;
            uint8_t cov = (cov_out > cov_in) ? (uint8_t)(cov_out - cov_in) : 0;
            uint8_t a;
            if (!cov) continue;
            a = (uint8_t)(((uint16_t)cov * alpha + 127u) / 255u);
            if (!a) continue;
            fb_blend_pixel(x + ix,           y + iy,           color, a);
            fb_blend_pixel(x + w - 1 - ix,   y + iy,           color, a);
            fb_blend_pixel(x + ix,           y + h - 1 - iy,   color, a);
            fb_blend_pixel(x + w - 1 - ix,   y + h - 1 - iy,   color, a);
        }
    }
}

/* Bresenham-like line with thickness. */
static void draw_line_alpha(int x0, int y0, int x1, int y1, int thickness, uint32_t color, uint8_t alpha) {
    int dx = k_abs_i(x1 - x0), dy = k_abs_i(y1 - y0);
    int sx = x0 < x1 ? 1 : -1;
    int sy = y0 < y1 ? 1 : -1;
    int err = dx - dy;
    int t = thickness < 1 ? 1 : thickness;
    if (alpha == 0) return;
    for (;;) {
        if (t <= 1) {
            uint8_t aa = (uint8_t)(alpha / 3u);
            fb_blend_pixel(x0, y0, color, alpha);
            /* Add minor orthogonal coverage to reduce stair-step aliasing. */
            if (dx >= dy) {
                if (aa) {
                    fb_blend_pixel(x0, y0 - 1, color, aa);
                    fb_blend_pixel(x0, y0 + 1, color, aa);
                }
            } else {
                if (aa) {
                    fb_blend_pixel(x0 - 1, y0, color, aa);
                    fb_blend_pixel(x0 + 1, y0, color, aa);
                }
            }
        } else {
            int r = t / 2;
            uint8_t aa = (uint8_t)(alpha / 3u);
            draw_circle_alpha(x0, y0, r, color, alpha);
            if (aa && r + 1 <= 12) draw_ring_alpha(x0, y0, r + 1, 1, color, aa);
        }
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 > -dy) { err -= dy; x0 += sx; }
        if (e2 < dx)  { err += dx; y0 += sy; }
    }
}

/* Bilinear image sample in 16.16 fixed-point source space.
 * Returns source-style 0xAARRGGBB. */
static uint32_t sample_image_bilinear(const ridux_image_t *img, int fx, int fy) {
    int ix0, iy0, ix1, iy1;
    uint32_t wx, wy;
    uint32_t p00, p01, p10, p11;
    uint32_t a_top, a_bot, a;
    uint32_t r_top, r_bot, r;
    uint32_t g_top, g_bot, g;
    uint32_t b_top, b_bot, b;

    if (fx < 0) fx = 0;
    if (fy < 0) fy = 0;

    ix0 = fx >> 16;
    iy0 = fy >> 16;
    if (ix0 >= (int)img->width) ix0 = (int)img->width - 1;
    if (iy0 >= (int)img->height) iy0 = (int)img->height - 1;
    ix1 = ix0 + 1; if (ix1 >= (int)img->width) ix1 = ix0;
    iy1 = iy0 + 1; if (iy1 >= (int)img->height) iy1 = iy0;
    wx = (uint32_t)(fx & 0xFFFF);
    wy = (uint32_t)(fy & 0xFFFF);

    p00 = img->pixels[(size_t)iy0 * img->width + (size_t)ix0];
    p01 = img->pixels[(size_t)iy0 * img->width + (size_t)ix1];
    p10 = img->pixels[(size_t)iy1 * img->width + (size_t)ix0];
    p11 = img->pixels[(size_t)iy1 * img->width + (size_t)ix1];

    a_top = ((((p00 >> 24) & 0xFFu) * (65536u - wx)) + (((p01 >> 24) & 0xFFu) * wx)) >> 16;
    a_bot = ((((p10 >> 24) & 0xFFu) * (65536u - wx)) + (((p11 >> 24) & 0xFFu) * wx)) >> 16;
    a = ((a_top * (65536u - wy)) + (a_bot * wy)) >> 16;

    r_top = ((((p00 >> 16) & 0xFFu) * (65536u - wx)) + (((p01 >> 16) & 0xFFu) * wx)) >> 16;
    r_bot = ((((p10 >> 16) & 0xFFu) * (65536u - wx)) + (((p11 >> 16) & 0xFFu) * wx)) >> 16;
    r = ((r_top * (65536u - wy)) + (r_bot * wy)) >> 16;

    g_top = ((((p00 >> 8) & 0xFFu) * (65536u - wx)) + (((p01 >> 8) & 0xFFu) * wx)) >> 16;
    g_bot = ((((p10 >> 8) & 0xFFu) * (65536u - wx)) + (((p11 >> 8) & 0xFFu) * wx)) >> 16;
    g = ((g_top * (65536u - wy)) + (g_bot * wy)) >> 16;

    b_top = (((p00 & 0xFFu) * (65536u - wx)) + ((p01 & 0xFFu) * wx)) >> 16;
    b_bot = (((p10 & 0xFFu) * (65536u - wx)) + ((p11 & 0xFFu) * wx)) >> 16;
    b = ((b_top * (65536u - wy)) + (b_bot * wy)) >> 16;

    return ((a & 0xFFu) << 24) | ((r & 0xFFu) << 16) | ((g & 0xFFu) << 8) | (b & 0xFFu);
}

static uint32_t sample_image_bilinear_4tap(const ridux_image_t *img, int fx, int fy,
                                           int step_x, int step_y) {
    int ox = step_x / 4;
    int oy = step_y / 4;
    uint32_t p0 = sample_image_bilinear(img, fx - ox, fy - oy);
    uint32_t p1 = sample_image_bilinear(img, fx + ox, fy - oy);
    uint32_t p2 = sample_image_bilinear(img, fx - ox, fy + oy);
    uint32_t p3 = sample_image_bilinear(img, fx + ox, fy + oy);

    uint32_t a = ((p0 >> 24) & 0xFFu) + ((p1 >> 24) & 0xFFu) + ((p2 >> 24) & 0xFFu) + ((p3 >> 24) & 0xFFu);
    uint32_t r = ((p0 >> 16) & 0xFFu) + ((p1 >> 16) & 0xFFu) + ((p2 >> 16) & 0xFFu) + ((p3 >> 16) & 0xFFu);
    uint32_t g = ((p0 >> 8) & 0xFFu) + ((p1 >> 8) & 0xFFu) + ((p2 >> 8) & 0xFFu) + ((p3 >> 8) & 0xFFu);
    uint32_t b = (p0 & 0xFFu) + (p1 & 0xFFu) + (p2 & 0xFFu) + (p3 & 0xFFu);

    return ((a >> 2) << 24) | ((r >> 2) << 16) | ((g >> 2) << 8) | (b >> 2);
}

void draw_image_scaled_alpha(int x, int y, int w, int h, const ridux_image_t *img, uint8_t alpha) {
    int dy;
    int step_x, step_y;
    bool minify_hi;
    if (w <= 0 || h <= 0 || !img || !img->pixels || !img->width || !img->height) return;
    step_x = (int)(((uint64_t)img->width << 16) / (uint64_t)w);
    step_y = (int)(((uint64_t)img->height << 16) / (uint64_t)h);
    minify_hi = (step_x > (int)(1u << 16)) || (step_y > (int)(1u << 16));
    for (dy = 0; dy < h; ++dy) {
        int fy = (int)(((((uint64_t)dy << 16) + 32768ull) * (uint64_t)img->height) / (uint64_t)h) - 32768;
        int dx;
        for (dx = 0; dx < w; ++dx) {
            int fx = (int)(((((uint64_t)dx << 16) + 32768ull) * (uint64_t)img->width) / (uint64_t)w) - 32768;
            uint32_t packed;
            uint8_t sa, sr, sg, sb;
            uint32_t src;
            packed = minify_hi
                ? sample_image_bilinear_4tap(img, fx, fy, step_x, step_y)
                : sample_image_bilinear(img, fx, fy);
            sa = (uint8_t)((packed >> 24) & 0xFFu);
            if (!sa) continue;
            if (alpha != 255) {
                sa = (uint8_t)(((uint16_t)sa * alpha) / 255u);
                if (!sa) continue;
            }
            sr = (uint8_t)((packed >> 16) & 0xFFu);
            sg = (uint8_t)((packed >> 8) & 0xFFu);
            sb = (uint8_t)(packed & 0xFFu);
            src = rgb(sr, sg, sb);
            fb_blend_pixel(x + dx, y + dy, src, sa);
        }
    }
}

/* Image drawn with a color tint (useful for theme-colored icons). */
static void draw_image_tinted(int x, int y, int w, int h, const ridux_image_t *img,
                              uint32_t tint, uint8_t tint_alpha, uint8_t alpha) {
    int dy;
    int step_x, step_y;
    bool minify_hi;
    if (w <= 0 || h <= 0 || !img || !img->pixels || !img->width || !img->height) return;
    step_x = (int)(((uint64_t)img->width << 16) / (uint64_t)w);
    step_y = (int)(((uint64_t)img->height << 16) / (uint64_t)h);
    minify_hi = (step_x > (int)(1u << 16)) || (step_y > (int)(1u << 16));
    for (dy = 0; dy < h; ++dy) {
        int fy = (int)(((((uint64_t)dy << 16) + 32768ull) * (uint64_t)img->height) / (uint64_t)h) - 32768;
        int dx;
        for (dx = 0; dx < w; ++dx) {
            int fx = (int)(((((uint64_t)dx << 16) + 32768ull) * (uint64_t)img->width) / (uint64_t)w) - 32768;
            uint32_t packed;
            uint8_t sa, sr, sg, sb;
            uint32_t src;
            packed = minify_hi
                ? sample_image_bilinear_4tap(img, fx, fy, step_x, step_y)
                : sample_image_bilinear(img, fx, fy);
            sa = (uint8_t)((packed >> 24) & 0xFFu);
            if (!sa) continue;
            if (alpha != 255) {
                sa = (uint8_t)(((uint16_t)sa * alpha) / 255u);
                if (!sa) continue;
            }
            sr = (uint8_t)((packed >> 16) & 0xFFu);
            sg = (uint8_t)((packed >> 8) & 0xFFu);
            sb = (uint8_t)(packed & 0xFFu);
            src = rgb(sr, sg, sb);
            if (tint_alpha) src = blend_color(src, tint, tint_alpha);
            fb_blend_pixel(x + dx, y + dy, src, sa);
        }
    }
}

/* Drop shadow: soft, offset, rounded. */
static void draw_shadow(int x, int y, int w, int h, int radius, int spread, uint32_t color, uint8_t alpha) {
    int i;
    int s = spread < 1 ? 1 : spread;
    if (w <= 0 || h <= 0 || alpha == 0) return;
    if (s > 18) s = 18;

    /* Layered rounded fill recovers depth body while preserving rounded edges. */
    for (i = s; i >= 1; --i) {
        int grow = i;
        uint32_t level = (uint32_t)(s - i + 1);
        uint32_t den = (uint32_t)(s * (s + 1));
        uint8_t a = (uint8_t)((alpha * level) / (den ? den : 1u));
        if (a == 0) continue;
        draw_rounded_rect_alpha(x - grow,
                                y - grow + grow / 3,
                                w + grow * 2,
                                h + grow * 2,
                                radius + grow,
                                color, a);
    }
}

/* In-place separable box blur on backbuffer (a rect region).
 * Fast path (BGRA 32bpp): sliding-window sums with O(1) per output pixel
 * regardless of radius. Division replaced by multiply-by-reciprocal.
 * This is the same technique Skia / Android use for software blur. */
static void blur_rect(int x, int y, int w, int h, int radius, int passes) {
    int px, py, pass, k;
    int kern;
    uint32_t inv_count;
    if (!g_use_backbuffer || radius <= 0 || passes <= 0 || w <= 0 || h <= 0) return;
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if ((uint32_t)(x + w) > g_fb.width)  w = (int)g_fb.width  - x;
    if ((uint32_t)(y + h) > g_fb.height) h = (int)g_fb.height - y;
    if (w <= 0 || h <= 0) return;
    if (radius > 12) radius = 12;
    kern = radius * 2 + 1;
    inv_count = 65536u / (uint32_t)kern;

    if (!g_fb_fast_bgra) {
        /* Fallback: original slow path (naive, div-based). */
        for (pass = 0; pass < passes; ++pass) {
            for (py = 0; py < h; ++py) {
                uint32_t *row = &g_backbuffer[(size_t)(y + py) * FB_MAX_WIDTH + (size_t)x];
                for (px = 0; px < w; ++px) {
                    uint32_t r = 0, g = 0, b = 0;
                    int count = 0;
                    for (k = -radius; k <= radius; ++k) {
                        int sx = px + k;
                        uint32_t pix;
                        if (sx < 0) sx = 0;
                        if (sx >= w) sx = w - 1;
                        pix = row[sx];
                        r += unpack_channel(pix, g_fb.red_pos, g_fb.red_size);
                        g += unpack_channel(pix, g_fb.green_pos, g_fb.green_size);
                        b += unpack_channel(pix, g_fb.blue_pos, g_fb.blue_size);
                        ++count;
                    }
                    g_blur_scratch[px] = rgb((uint8_t)(r / count), (uint8_t)(g / count), (uint8_t)(b / count));
                }
                for (px = 0; px < w; ++px) row[px] = g_blur_scratch[px];
            }
        }
        return;
    }

    /* Fast path: BGRA sliding window + inv-count multiply. */
    for (pass = 0; pass < passes; ++pass) {
        /* ---- Horizontal pass ---- */
        for (py = 0; py < h; ++py) {
            uint32_t *row = &g_backbuffer[(size_t)(y + py) * FB_MAX_WIDTH + (size_t)x];
            uint32_t sr = 0, sg = 0, sb = 0;
            /* Prime window: indices [-radius .. +radius] clamped. */
            for (k = -radius; k <= radius; ++k) {
                int sx = k; uint32_t pix;
                if (sx < 0) sx = 0;
                if (sx >= w) sx = w - 1;
                pix = row[sx];
                sr += (pix >> 16) & 0xFFu;
                sg += (pix >>  8) & 0xFFu;
                sb += (pix      ) & 0xFFu;
            }
            for (px = 0; px < w; ++px) {
                uint32_t r = (sr * inv_count) >> 16;
                uint32_t g = (sg * inv_count) >> 16;
                uint32_t b = (sb * inv_count) >> 16;
                int out_x, in_x;
                uint32_t pout, pin;
                g_blur_scratch[px] = (r << 16) | (g << 8) | b;
                /* Slide window for next px (out = leftmost, in = one past rightmost). */
                out_x = px - radius;
                in_x  = px + radius + 1;
                if (out_x < 0) out_x = 0;
                if (out_x >= w) out_x = w - 1;
                if (in_x < 0) in_x = 0;
                if (in_x >= w) in_x = w - 1;
                pout = row[out_x];
                pin  = row[in_x];
                sr += ((pin >> 16) & 0xFFu) - ((pout >> 16) & 0xFFu);
                sg += ((pin >>  8) & 0xFFu) - ((pout >>  8) & 0xFFu);
                sb += ((pin      ) & 0xFFu) - ((pout      ) & 0xFFu);
            }
            for (px = 0; px < w; ++px) row[px] = g_blur_scratch[px];
        }
        /* ---- Vertical pass ---- */
        for (px = 0; px < w; ++px) {
            uint32_t *col = &g_backbuffer[(size_t)y * FB_MAX_WIDTH + (size_t)(x + px)];
            uint32_t sr = 0, sg = 0, sb = 0;
            int py2;
            for (k = -radius; k <= radius; ++k) {
                int sy = k; uint32_t pix;
                if (sy < 0) sy = 0;
                if (sy >= h) sy = h - 1;
                pix = col[(size_t)sy * FB_MAX_WIDTH];
                sr += (pix >> 16) & 0xFFu;
                sg += (pix >>  8) & 0xFFu;
                sb += (pix      ) & 0xFFu;
            }
            for (py2 = 0; py2 < h; ++py2) {
                uint32_t r = (sr * inv_count) >> 16;
                uint32_t g = (sg * inv_count) >> 16;
                uint32_t b = (sb * inv_count) >> 16;
                int out_y, in_y;
                uint32_t pout, pin;
                g_blur_scratch[py2] = (r << 16) | (g << 8) | b;
                out_y = py2 - radius;
                in_y  = py2 + radius + 1;
                if (out_y < 0) out_y = 0;
                if (out_y >= h) out_y = h - 1;
                if (in_y < 0) in_y = 0;
                if (in_y >= h) in_y = h - 1;
                pout = col[(size_t)out_y * FB_MAX_WIDTH];
                pin  = col[(size_t)in_y  * FB_MAX_WIDTH];
                sr += ((pin >> 16) & 0xFFu) - ((pout >> 16) & 0xFFu);
                sg += ((pin >>  8) & 0xFFu) - ((pout >>  8) & 0xFFu);
                sb += ((pin      ) & 0xFFu) - ((pout      ) & 0xFFu);
            }
            for (py2 = 0; py2 < h; ++py2) {
                col[(size_t)py2 * FB_MAX_WIDTH] = g_blur_scratch[py2];
            }
        }
    }
}

/* Deterministic noise overlay (for acrylic grain). */
static void draw_noise(int x, int y, int w, int h, uint8_t alpha) {
    int iy, ix;
    uint32_t seed = 0x13579BDFu ^ ((uint32_t)x << 3) ^ ((uint32_t)y << 7);
    if (w <= 0 || h <= 0 || alpha == 0) return;
    for (iy = 0; iy < h; ++iy) {
        for (ix = 0; ix < w; ++ix) {
            seed = seed * 1664525u + 1013904223u;
            uint8_t n = (uint8_t)(seed & 0x7F);
            uint32_t c = rgb(n, n, n);
            fb_blend_pixel(x + ix, y + iy, c, alpha);
        }
    }
}

static void draw_noise_rounded(int x, int y, int w, int h, int r, uint8_t alpha) {
    int iy, ix;
    uint32_t seed = 0x13579BDFu ^ ((uint32_t)x << 3) ^ ((uint32_t)y << 7);
    if (w <= 0 || h <= 0 || alpha == 0) return;
    if (r < 0) r = 0;
    if (r * 2 > w) r = w / 2;
    if (r * 2 > h) r = h / 2;

    for (iy = 0; iy < h; ++iy) {
        for (ix = 0; ix < w; ++ix) {
            uint8_t cov = rounded_pixel_cov(ix, iy, w, h, r);
            seed = seed * 1664525u + 1013904223u;
            if (!cov) continue;
            {
                uint8_t n = (uint8_t)(seed & 0x7F);
                uint32_t c = rgb(n, n, n);
                uint8_t a = (uint8_t)(((uint16_t)alpha * cov) / 255u);
                fb_blend_pixel(x + ix, y + iy, c, a);
            }
        }
    }
}

static void draw_top_glow_rounded(int x, int y, int w, int h, int r,
                                  uint32_t color, uint8_t top_alpha, uint8_t bottom_alpha) {
    int iy, ix;
    int glow_h;
    if (w <= 0 || h <= 0 || top_alpha == 0) return;
    if (r < 0) r = 0;
    if (r * 2 > w) r = w / 2;
    if (r * 2 > h) r = h / 2;

    glow_h = (h * 3) / 5;
    if (glow_h < 10) glow_h = h;
    if (glow_h > h) glow_h = h;

    for (iy = 0; iy < glow_h; ++iy) {
        uint8_t row_a;
        if (glow_h <= 1) row_a = top_alpha;
        else row_a = (uint8_t)(top_alpha + ((int)(bottom_alpha - top_alpha) * iy) / (glow_h - 1));
        if (!row_a) continue;
        for (ix = 0; ix < w; ++ix) {
            uint8_t cov = rounded_pixel_cov(ix, iy, w, h, r);
            if (!cov) continue;
            fb_blend_pixel(x + ix, y + iy, color, (uint8_t)(((uint16_t)row_a * cov) / 255u));
        }
    }
}

static void glass_snapshot_corner(uint32_t *dst, int x, int y, int r) {
    int iy, ix;
    for (iy = 0; iy < r; ++iy) {
        for (ix = 0; ix < r; ++ix) {
            dst[iy * r + ix] = fb_get_pixel(x + ix, y + iy);
        }
    }
}

static void glass_restore_outside_tl(const uint32_t *src, int x, int y, int r) {
    int iy, ix, rr = r * r;
    for (iy = 0; iy < r; ++iy) {
        for (ix = 0; ix < r; ++ix) {
            int dx = r - ix;
            int dy = r - iy;
            if (dx * dx + dy * dy > rr) fb_put_pixel(x + ix, y + iy, src[iy * r + ix]);
        }
    }
}
static void glass_restore_outside_tr(const uint32_t *src, int x, int y, int r) {
    int iy, ix, rr = r * r;
    for (iy = 0; iy < r; ++iy) {
        for (ix = 0; ix < r; ++ix) {
            int dx = ix + 1;
            int dy = r - iy;
            if (dx * dx + dy * dy > rr) fb_put_pixel(x + ix, y + iy, src[iy * r + ix]);
        }
    }
}
static void glass_restore_outside_bl(const uint32_t *src, int x, int y, int r) {
    int iy, ix, rr = r * r;
    for (iy = 0; iy < r; ++iy) {
        for (ix = 0; ix < r; ++ix) {
            int dx = r - ix;
            int dy = iy + 1;
            if (dx * dx + dy * dy > rr) fb_put_pixel(x + ix, y + iy, src[iy * r + ix]);
        }
    }
}
static void glass_restore_outside_br(const uint32_t *src, int x, int y, int r) {
    int iy, ix, rr = r * r;
    for (iy = 0; iy < r; ++iy) {
        for (ix = 0; ix < r; ++ix) {
            int dx = ix + 1;
            int dy = iy + 1;
            if (dx * dx + dy * dy > rr) fb_put_pixel(x + ix, y + iy, src[iy * r + ix]);
        }
    }
}

/* Glass (acrylic/mica): blur background inside rect, tint + noise + stroke. */
static void draw_glass_panel(int x, int y, int w, int h, int radius,
                             uint32_t tint, uint8_t tint_alpha,
                             uint32_t stroke, uint8_t stroke_alpha,
                             int blur_radius, int blur_passes, bool add_noise,
                             bool add_top_glow) {
    int rr;
    /* Clip rectangle to screen to avoid wasted work. */
    int cx = x, cy = y, cw = w, ch = h;
    if (cx < 0) { cw += cx; cx = 0; }
    if (cy < 0) { ch += cy; cy = 0; }
    if ((uint32_t)(cx + cw) > g_fb.width)  cw = (int)g_fb.width  - cx;
    if ((uint32_t)(cy + ch) > g_fb.height) ch = (int)g_fb.height - cy;
    if (cw <= 0 || ch <= 0) return;
    rr = radius;
    if (rr < 0) rr = 0;
    if (rr * 2 > cw) rr = cw / 2;
    if (rr * 2 > ch) rr = ch / 2;
    if (rr > GLASS_CORNER_MAX) rr = GLASS_CORNER_MAX;

    if (g_use_backbuffer && rr > 0) {
        glass_snapshot_corner(g_glass_corner_tl, cx, cy, rr);
        glass_snapshot_corner(g_glass_corner_tr, cx + cw - rr, cy, rr);
        glass_snapshot_corner(g_glass_corner_bl, cx, cy + ch - rr, rr);
        glass_snapshot_corner(g_glass_corner_br, cx + cw - rr, cy + ch - rr, rr);
    }

    blur_rect(cx, cy, cw, ch, blur_radius, blur_passes);
    if (g_use_backbuffer && rr > 0) {
        glass_restore_outside_tl(g_glass_corner_tl, cx, cy, rr);
        glass_restore_outside_tr(g_glass_corner_tr, cx + cw - rr, cy, rr);
        glass_restore_outside_bl(g_glass_corner_bl, cx, cy + ch - rr, rr);
        glass_restore_outside_br(g_glass_corner_br, cx + cw - rr, cy + ch - rr, rr);
    }
    /* Tint overlay as rounded rect so corners disappear nicely. */
    draw_rounded_rect_alpha(x, y, w, h, radius, tint, tint_alpha);
    if (add_top_glow) {
        draw_top_glow_rounded(x, y, w, h, radius, rgb(255, 255, 255),
                              (uint8_t)(12 + tint_alpha / 6), 0);
    }
    if (add_noise) draw_noise_rounded(x, y, w, h, radius, 3);
    if (stroke_alpha) draw_rounded_rect_outline(x, y, w, h, radius, 1, stroke, stroke_alpha);
}

/* ---------------- Font / text ---------------- */

static uint8_t text_cov_curve(uint32_t cov, int scale) {
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
    return (uint8_t)v;
}

static uint8_t sample_glyph_hi_bilinear(const uint8_t glyph[RIDUX_FONT_HI_H][RIDUX_FONT_HI_W],
                                        int fx, int fy) {
    int x0, x1, y0, y1;
    uint32_t wx, wy;
    uint32_t c00, c01, c10, c11;
    uint32_t top, bot, cov;

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
    wx = (uint32_t)(fx & 0xFFFF);
    wy = (uint32_t)(fy & 0xFFFF);

    c00 = glyph[y0][x0];
    c01 = glyph[y0][x1];
    c10 = glyph[y1][x0];
    c11 = glyph[y1][x1];
    top = (c00 * (65536u - wx) + c01 * wx) >> 16;
    bot = (c10 * (65536u - wx) + c11 * wx) >> 16;
    cov = (top * (65536u - wy) + bot * wy) >> 16;
    if (cov > 255u) cov = 255u;
    return (uint8_t)cov;
}

static void draw_char(int x, int y, char c, uint32_t color, uint8_t alpha) {
    const uint8_t(*glyph)[RIDUX_FONT_W] = FONT8X16_AA[(uint8_t)c];
    int row;
    for (row = 0; row < RIDUX_FONT_H; ++row) {
        int col;
        for (col = 0; col < RIDUX_FONT_W; ++col) {
            uint8_t coverage = glyph[row][col];
            uint8_t cov = text_cov_curve(coverage, 1);
            uint8_t a;
            if (cov == 0) continue;
            a = (uint8_t)(((uint16_t)cov * alpha) / 255u);
            fb_blend_pixel(x + col, y + row, color, a);
        }
    }
}

/* High-res glyph sampling for crisp scaled text without bitmap blur. */
static void draw_char_scaled(int x, int y, char c, int scale, uint32_t color, uint8_t alpha) {
    const uint8_t(*glyph)[RIDUX_FONT_HI_W] = FONT32X64_AA[(uint8_t)c];
    int out_w, out_h, ox, oy;
    int fw = RIDUX_FONT_HI_W, fh = RIDUX_FONT_HI_H;
    if (scale <= 1) { draw_char(x, y, c, color, alpha); return; }
    out_w = RIDUX_FONT_W * scale;
    out_h = RIDUX_FONT_H * scale;
    for (oy = 0; oy < out_h; ++oy) {
        int fy = (int)(((((uint64_t)oy << 16) + 32768ull) * (uint64_t)fh) / (uint64_t)out_h) - 32768;
        uint16_t a16;
        for (ox = 0; ox < out_w; ++ox) {
            int fx = (int)(((((uint64_t)ox << 16) + 32768ull) * (uint64_t)fw) / (uint64_t)out_w) - 32768;
            uint32_t cov = sample_glyph_hi_bilinear(glyph, fx, fy);
            cov = text_cov_curve(cov, scale);
            if (cov == 0) continue;
            a16 = (uint16_t)(cov * alpha / 255u);
            fb_blend_pixel(x + ox, y + oy, color, (uint8_t)a16);
        }
    }
}

void draw_text(int x, int y, const char *text, uint32_t color, uint8_t alpha) {
    while (*text) {
        draw_char(x, y, *text, color, alpha);
        x += RIDUX_FONT_W;
        ++text;
    }
}

void draw_text_scaled(int x, int y, const char *text, int scale, uint32_t color, uint8_t alpha) {
    if (scale <= 1) { draw_text(x, y, text, color, alpha); return; }
    while (*text) {
        draw_char_scaled(x, y, *text, scale, color, alpha);
        x += RIDUX_FONT_W * scale;
        ++text;
    }
}

int measure_text(const char *text) { return (int)k_strlen(text) * RIDUX_FONT_W; }

/* ---------------- Flush command queue ---------------- */

static flush_cmd_t *flush_push(flush_cmd_type_t type) {
    flush_cmd_t *cmd;
    if (g_flush_count >= FLUSH_MAX_COMMANDS) return NULL;
    cmd = &g_flush_queue[g_flush_count++];
    k_memset(cmd, 0, sizeof(*cmd));
    cmd->type = type;
    cmd->alpha = 255;
    cmd->scale = 1;
    return cmd;
}
void flush_reset(void) { g_flush_count = 0; g_scissor_top = 0; }

void flush_clear(uint32_t color) {
    flush_cmd_t *c = flush_push(FLUSH_CMD_CLEAR);
    if (c) c->color_a = color;
}
void flush_rect(int x, int y, int w, int h, uint32_t color, uint8_t alpha) {
    flush_cmd_t *c = flush_push(FLUSH_CMD_RECT);
    if (!c) return;
    c->x = x; c->y = y; c->w = w; c->h = h; c->color_a = color; c->alpha = alpha;
}
void flush_round_rect(int x, int y, int w, int h, int r, uint32_t color, uint8_t alpha) {
    flush_cmd_t *c = flush_push(FLUSH_CMD_ROUND_RECT);
    if (!c) return;
    c->x = x; c->y = y; c->w = w; c->h = h; c->radius = r; c->color_a = color; c->alpha = alpha;
}

/* Per-corner-radii rounded rect. The four radii are packed into the
 * existing radius / radius2 fields as 16-bit halves so no struct change
 * is needed. */
void flush_round_rect4(int x, int y, int w, int h,
                      int r_tl, int r_tr, int r_br, int r_bl,
                      uint32_t color, uint8_t alpha) {
    flush_cmd_t *c = flush_push(FLUSH_CMD_ROUND_RECT4);
    if (!c) return;
    if (r_tl < 0) r_tl = 0; if (r_tl > 0xFFFF) r_tl = 0xFFFF;
    if (r_tr < 0) r_tr = 0; if (r_tr > 0xFFFF) r_tr = 0xFFFF;
    if (r_br < 0) r_br = 0; if (r_br > 0xFFFF) r_br = 0xFFFF;
    if (r_bl < 0) r_bl = 0; if (r_bl > 0xFFFF) r_bl = 0xFFFF;
    c->x = x; c->y = y; c->w = w; c->h = h;
    c->radius  = (int)((uint32_t)r_tl | ((uint32_t)r_tr << 16));
    c->radius2 = (int)((uint32_t)r_bl | ((uint32_t)r_br << 16));
    c->color_a = color; c->alpha = alpha;
}
void flush_stroke_rect(int x, int y, int w, int h, int t, uint32_t color, uint8_t alpha) {
    flush_cmd_t *c = flush_push(FLUSH_CMD_STROKE_RECT);
    if (!c) return;
    c->x = x; c->y = y; c->w = w; c->h = h; c->radius2 = t; c->color_a = color; c->alpha = alpha;
}
void flush_stroke_round(int x, int y, int w, int h, int r, int t, uint32_t color, uint8_t alpha) {
    flush_cmd_t *c = flush_push(FLUSH_CMD_STROKE_ROUND);
    if (!c) return;
    c->x = x; c->y = y; c->w = w; c->h = h; c->radius = r; c->radius2 = t;
    c->color_a = color; c->alpha = alpha;
}
void flush_vgradient(int x, int y, int w, int h, uint32_t top, uint32_t bot, uint8_t a) {
    flush_cmd_t *c = flush_push(FLUSH_CMD_VGRADIENT);
    if (!c) return;
    c->x = x; c->y = y; c->w = w; c->h = h; c->color_a = top; c->color_b = bot; c->alpha = a;
}
void flush_hgradient(int x, int y, int w, int h, uint32_t l, uint32_t r, uint8_t a) {
    flush_cmd_t *c = flush_push(FLUSH_CMD_HGRADIENT);
    if (!c) return;
    c->x = x; c->y = y; c->w = w; c->h = h; c->color_a = l; c->color_b = r; c->alpha = a;
}
void flush_radial(int cx, int cy, int r, uint32_t inner, uint32_t outer, uint8_t a) {
    flush_cmd_t *c = flush_push(FLUSH_CMD_RADIAL);
    if (!c) return;
    c->x = cx; c->y = cy; c->radius = r; c->color_a = inner; c->color_b = outer; c->alpha = a;
}
void flush_circle(int cx, int cy, int r, uint32_t color, uint8_t a) {
    flush_cmd_t *c = flush_push(FLUSH_CMD_CIRCLE);
    if (!c) return;
    c->x = cx; c->y = cy; c->radius = r; c->color_a = color; c->alpha = a;
}
void flush_ring(int cx, int cy, int r, int thickness, uint32_t color, uint8_t a) {
    flush_cmd_t *c = flush_push(FLUSH_CMD_RING);
    if (!c) return;
    c->x = cx; c->y = cy; c->radius = r; c->radius2 = thickness; c->color_a = color; c->alpha = a;
}
void flush_line(int x0, int y0, int x1, int y1, int t, uint32_t color, uint8_t a) {
    flush_cmd_t *c = flush_push(FLUSH_CMD_LINE);
    if (!c) return;
    c->x = x0; c->y = y0; c->x2 = x1; c->y2 = y1; c->radius2 = t; c->color_a = color; c->alpha = a;
}
void flush_text(int x, int y, uint32_t color, const char *text) {
    flush_cmd_t *c = flush_push(FLUSH_CMD_TEXT);
    if (!c) return;
    c->x = x; c->y = y; c->color_a = color; c->alpha = 255;
    k_strlcpy(c->text, text, sizeof(c->text));
}
void flush_text_alpha(int x, int y, uint32_t color, uint8_t a, const char *text) {
    flush_cmd_t *c = flush_push(FLUSH_CMD_TEXT);
    if (!c) return;
    c->x = x; c->y = y; c->color_a = color; c->alpha = a;
    k_strlcpy(c->text, text, sizeof(c->text));
}
void flush_text_scaled(int x, int y, int scale, uint32_t color, uint8_t a, const char *text) {
    flush_cmd_t *c = flush_push(FLUSH_CMD_TEXT_SCALED);
    if (!c) return;
    c->x = x; c->y = y; c->color_a = color; c->alpha = a;
    c->scale = (uint8_t)(scale < 1 ? 1 : (scale > 6 ? 6 : scale));
    k_strlcpy(c->text, text, sizeof(c->text));
}
void flush_image(int x, int y, int w, int h, const ridux_image_t *img, uint8_t a) {
    flush_cmd_t *c = flush_push(FLUSH_CMD_IMAGE);
    if (!c) return;
    c->x = x; c->y = y; c->w = w; c->h = h; c->image = img; c->alpha = a;
}
void flush_image_tint(int x, int y, int w, int h, const ridux_image_t *img,
                             uint32_t tint, uint8_t tint_alpha, uint8_t a) {
    flush_cmd_t *c = flush_push(FLUSH_CMD_IMAGE_TINT);
    if (!c) return;
    c->x = x; c->y = y; c->w = w; c->h = h; c->image = img; c->color_a = tint;
    c->tint_alpha = tint_alpha; c->alpha = a;
}
void flush_blur(int x, int y, int w, int h, int radius, int passes) {
    flush_cmd_t *c = flush_push(FLUSH_CMD_BLUR);
    if (!c) return;
    c->x = x; c->y = y; c->w = w; c->h = h; c->radius = radius; c->radius2 = passes;
}
void flush_glass(int x, int y, int w, int h, int radius,
                        uint32_t tint, uint8_t tint_alpha,
                        uint32_t stroke, uint8_t stroke_alpha,
                        int blur_radius, int blur_passes) {
    flush_cmd_t *c = flush_push(FLUSH_CMD_GLASS);
    if (!c) return;
    c->x = x; c->y = y; c->w = w; c->h = h; c->radius = radius;
    c->color_a = tint; c->tint_alpha = tint_alpha;
    c->color_b = stroke; c->alpha = stroke_alpha;
    c->radius2 = blur_radius;
    c->scale = (uint8_t)(blur_passes < 1 ? 1 : blur_passes);
    c->flags = GLASS_FLAG_NOISE | GLASS_FLAG_TOP_GLOW;
}
void flush_glass_plain(int x, int y, int w, int h, int radius,
                              uint32_t tint, uint8_t tint_alpha,
                              uint32_t stroke, uint8_t stroke_alpha,
                              int blur_radius, int blur_passes) {
    flush_cmd_t *c = flush_push(FLUSH_CMD_GLASS);
    if (!c) return;
    c->x = x; c->y = y; c->w = w; c->h = h; c->radius = radius;
    c->color_a = tint; c->tint_alpha = tint_alpha;
    c->color_b = stroke; c->alpha = stroke_alpha;
    c->radius2 = blur_radius;
    c->scale = (uint8_t)(blur_passes < 1 ? 1 : blur_passes);
    c->flags = GLASS_FLAG_NOISE;
}
void flush_shadow(int x, int y, int w, int h, int radius, int spread,
                         uint32_t color, uint8_t a) {
    flush_cmd_t *c = flush_push(FLUSH_CMD_SHADOW);
    if (!c) return;
    c->x = x; c->y = y; c->w = w; c->h = h; c->radius = radius; c->radius2 = spread;
    c->color_a = color; c->alpha = a;
}
void flush_noise(int x, int y, int w, int h, uint8_t a) {
    flush_cmd_t *c = flush_push(FLUSH_CMD_NOISE);
    if (!c) return;
    c->x = x; c->y = y; c->w = w; c->h = h; c->alpha = a;
}
void flush_scissor_push(int x, int y, int w, int h) {
    flush_cmd_t *c = flush_push(FLUSH_CMD_SCISSOR_PUSH);
    if (!c) return;
    c->x = x; c->y = y; c->w = w; c->h = h;
}
void flush_scissor_pop(void) { flush_push(FLUSH_CMD_SCISSOR_POP); }

static void flush_execute_commands(bool present) {
    size_t i;
    for (i = 0; i < g_flush_count; ++i) {
        const flush_cmd_t *c = &g_flush_queue[i];
        switch (c->type) {
        case FLUSH_CMD_CLEAR:
            draw_rect_alpha(0, 0, (int)g_fb.width, (int)g_fb.height, c->color_a, 255);
            break;
        case FLUSH_CMD_RECT:
            draw_rect_alpha(c->x, c->y, c->w, c->h, c->color_a, c->alpha);
            break;
        case FLUSH_CMD_ROUND_RECT:
            draw_rounded_rect_alpha(c->x, c->y, c->w, c->h, c->radius, c->color_a, c->alpha);
            break;
        case FLUSH_CMD_ROUND_RECT4: {
            int r_tl = (int)((uint32_t)c->radius  & 0xFFFFu);
            int r_tr = (int)(((uint32_t)c->radius >> 16) & 0xFFFFu);
            int r_bl = (int)((uint32_t)c->radius2 & 0xFFFFu);
            int r_br = (int)(((uint32_t)c->radius2 >> 16) & 0xFFFFu);
            draw_rounded_rect_alpha4(c->x, c->y, c->w, c->h,
                                     r_tl, r_tr, r_br, r_bl, c->color_a, c->alpha);
            break;
        }
        case FLUSH_CMD_STROKE_RECT:
            draw_stroke_rect(c->x, c->y, c->w, c->h, c->radius2, c->color_a, c->alpha);
            break;
        case FLUSH_CMD_STROKE_ROUND:
            draw_rounded_rect_outline(c->x, c->y, c->w, c->h, c->radius, c->radius2,
                                      c->color_a, c->alpha);
            break;
        case FLUSH_CMD_VGRADIENT:
            draw_vgradient_alpha(c->x, c->y, c->w, c->h, c->color_a, c->color_b, c->alpha);
            break;
        case FLUSH_CMD_HGRADIENT:
            draw_hgradient_alpha(c->x, c->y, c->w, c->h, c->color_a, c->color_b, c->alpha);
            break;
        case FLUSH_CMD_RADIAL:
            draw_radial_alpha(c->x, c->y, c->radius, c->color_a, c->color_b, c->alpha);
            break;
        case FLUSH_CMD_CIRCLE:
            draw_circle_alpha(c->x, c->y, c->radius, c->color_a, c->alpha);
            break;
        case FLUSH_CMD_RING:
            draw_ring_alpha(c->x, c->y, c->radius, c->radius2, c->color_a, c->alpha);
            break;
        case FLUSH_CMD_LINE:
            draw_line_alpha(c->x, c->y, c->x2, c->y2, c->radius2, c->color_a, c->alpha);
            break;
        case FLUSH_CMD_TEXT:
            draw_text(c->x, c->y, c->text, c->color_a, c->alpha);
            break;
        case FLUSH_CMD_TEXT_SCALED:
            draw_text_scaled(c->x, c->y, c->text, c->scale, c->color_a, c->alpha);
            break;
        case FLUSH_CMD_IMAGE:
            draw_image_scaled_alpha(c->x, c->y, c->w, c->h, c->image, c->alpha);
            break;
        case FLUSH_CMD_IMAGE_TINT:
            draw_image_tinted(c->x, c->y, c->w, c->h, c->image, c->color_a, c->tint_alpha, c->alpha);
            break;
        case FLUSH_CMD_BLUR:
            blur_rect(c->x, c->y, c->w, c->h, c->radius, c->radius2);
            break;
        case FLUSH_CMD_GLASS:
            draw_glass_panel(c->x, c->y, c->w, c->h, c->radius,
                             c->color_a, c->tint_alpha,
                             c->color_b, c->alpha,
                             c->radius2, c->scale,
                             (c->flags & GLASS_FLAG_NOISE) != 0,
                             (c->flags & GLASS_FLAG_TOP_GLOW) != 0);
            break;
        case FLUSH_CMD_SHADOW:
            draw_shadow(c->x, c->y, c->w, c->h, c->radius, c->radius2, c->color_a, c->alpha);
            break;
        case FLUSH_CMD_NOISE:
            draw_noise(c->x, c->y, c->w, c->h, c->alpha);
            break;
        case FLUSH_CMD_SCISSOR_PUSH: {
            ui_rect_t r; r.x = c->x; r.y = c->y; r.w = c->w; r.h = c->h;
            scissor_push(r);
            break;
        }
        case FLUSH_CMD_SCISSOR_POP:
            scissor_pop();
            break;
        default:
            break;
        }
    }
    if (present) fb_present();
}

void flush_execute_to_backbuffer(void) {
    flush_execute_commands(false);
}

void flush_execute(void) {
    flush_execute_commands(true);
}


size_t flush_queue_count(void) {
    return g_flush_count;
}
