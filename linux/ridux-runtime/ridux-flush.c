#include "ridux-flush.h"

#include <stdlib.h>
#include <string.h>

#define FLUSH_MAX_COMMANDS 16384
#define FLUSH_TEXT_MAX     192

typedef enum {
    CMD_CLEAR = 0,
    CMD_RECT,
    CMD_ROUND_RECT,
    CMD_STROKE_ROUND,
    CMD_CIRCLE,
    CMD_VGRAD,
    CMD_SHADOW,
    CMD_NOISE,
    CMD_IMAGE,
    CMD_TEXT
} cmd_type_t;

typedef struct {
    cmd_type_t type;
    int x;
    int y;
    int w;
    int h;
    int radius;
    int thickness;
    int scale;
    uint32_t seed;
    flush_color_t c0;
    flush_color_t c1;
    const ridux_image_t *image;
    char text[FLUSH_TEXT_MAX];
} flush_cmd_t;

typedef struct {
    char ch;
    uint8_t row[7];
} glyph_t;

static const glyph_t g_glyphs[] = {
    {'A',{0x0E,0x11,0x11,0x1F,0x11,0x11,0x11}},
    {'B',{0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E}},
    {'C',{0x0E,0x11,0x10,0x10,0x10,0x11,0x0E}},
    {'D',{0x1C,0x12,0x11,0x11,0x11,0x12,0x1C}},
    {'E',{0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F}},
    {'F',{0x1F,0x10,0x10,0x1E,0x10,0x10,0x10}},
    {'G',{0x0E,0x11,0x10,0x17,0x11,0x11,0x0E}},
    {'H',{0x11,0x11,0x11,0x1F,0x11,0x11,0x11}},
    {'I',{0x0E,0x04,0x04,0x04,0x04,0x04,0x0E}},
    {'J',{0x01,0x01,0x01,0x01,0x11,0x11,0x0E}},
    {'K',{0x11,0x12,0x14,0x18,0x14,0x12,0x11}},
    {'L',{0x10,0x10,0x10,0x10,0x10,0x10,0x1F}},
    {'M',{0x11,0x1B,0x15,0x15,0x11,0x11,0x11}},
    {'N',{0x11,0x19,0x15,0x13,0x11,0x11,0x11}},
    {'O',{0x0E,0x11,0x11,0x11,0x11,0x11,0x0E}},
    {'P',{0x1E,0x11,0x11,0x1E,0x10,0x10,0x10}},
    {'Q',{0x0E,0x11,0x11,0x11,0x15,0x12,0x0D}},
    {'R',{0x1E,0x11,0x11,0x1E,0x14,0x12,0x11}},
    {'S',{0x0F,0x10,0x10,0x0E,0x01,0x01,0x1E}},
    {'T',{0x1F,0x04,0x04,0x04,0x04,0x04,0x04}},
    {'U',{0x11,0x11,0x11,0x11,0x11,0x11,0x0E}},
    {'V',{0x11,0x11,0x11,0x11,0x11,0x0A,0x04}},
    {'W',{0x11,0x11,0x11,0x15,0x15,0x15,0x0A}},
    {'X',{0x11,0x11,0x0A,0x04,0x0A,0x11,0x11}},
    {'Y',{0x11,0x11,0x0A,0x04,0x04,0x04,0x04}},
    {'Z',{0x1F,0x01,0x02,0x04,0x08,0x10,0x1F}},
    {'0',{0x0E,0x11,0x13,0x15,0x19,0x11,0x0E}},
    {'1',{0x04,0x0C,0x04,0x04,0x04,0x04,0x0E}},
    {'2',{0x0E,0x11,0x01,0x02,0x04,0x08,0x1F}},
    {'3',{0x1E,0x01,0x01,0x0E,0x01,0x01,0x1E}},
    {'4',{0x02,0x06,0x0A,0x12,0x1F,0x02,0x02}},
    {'5',{0x1F,0x10,0x10,0x1E,0x01,0x01,0x1E}},
    {'6',{0x0E,0x10,0x10,0x1E,0x11,0x11,0x0E}},
    {'7',{0x1F,0x01,0x02,0x04,0x08,0x08,0x08}},
    {'8',{0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E}},
    {'9',{0x0E,0x11,0x11,0x0F,0x01,0x01,0x0E}},
    {':',{0x00,0x04,0x04,0x00,0x04,0x04,0x00}},
    {'-',{0x00,0x00,0x00,0x1F,0x00,0x00,0x00}},
    {'/',{0x01,0x02,0x04,0x08,0x10,0x00,0x00}},
    {'.',{0x00,0x00,0x00,0x00,0x00,0x0C,0x0C}},
    {'+',{0x00,0x04,0x04,0x1F,0x04,0x04,0x00}},
    {'_',{0x00,0x00,0x00,0x00,0x00,0x00,0x1F}},
    {'[',{0x0E,0x08,0x08,0x08,0x08,0x08,0x0E}},
    {']',{0x0E,0x02,0x02,0x02,0x02,0x02,0x0E}},
    {'(',{0x02,0x04,0x08,0x08,0x08,0x04,0x02}},
    {')',{0x08,0x04,0x02,0x02,0x02,0x04,0x08}},
    {' ',{0x00,0x00,0x00,0x00,0x00,0x00,0x00}}
};

static int g_width;
static int g_height;
static uint32_t *g_fb;
static flush_cmd_t g_queue[FLUSH_MAX_COMMANDS];
static size_t g_count;

static int min_i(int a, int b) { return (a < b) ? a : b; }
static int max_i(int a, int b) { return (a > b) ? a : b; }

static int clamp_i(int v, int lo, int hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static uint32_t hash_u32(uint32_t x) {
    x ^= x >> 16;
    x *= 0x7feb352dU;
    x ^= x >> 15;
    x *= 0x846ca68bU;
    x ^= x >> 16;
    return x;
}

static uint32_t pack_rgb(uint8_t r, uint8_t g, uint8_t b) {
    return ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}

static uint32_t blend_over_rgba(uint32_t dst, uint8_t sr, uint8_t sg, uint8_t sb, uint8_t sa) {
    uint32_t dr = (dst >> 16) & 0xFFu;
    uint32_t dg = (dst >> 8) & 0xFFu;
    uint32_t db = dst & 0xFFu;
    uint32_t a = sa;
    uint32_t inv = 255u - a;
    uint8_t rr = (uint8_t)((sr * a + dr * inv) / 255u);
    uint8_t rg = (uint8_t)((sg * a + dg * inv) / 255u);
    uint8_t rb = (uint8_t)((sb * a + db * inv) / 255u);
    return pack_rgb(rr, rg, rb);
}

static uint32_t blend_over(uint32_t dst, flush_color_t src) {
    return blend_over_rgba(dst, src.r, src.g, src.b, src.a);
}

static void put_px(int x, int y, flush_color_t color) {
    uint32_t *p;
    if (x < 0 || y < 0 || x >= g_width || y >= g_height) return;
    p = &g_fb[(size_t)y * (size_t)g_width + (size_t)x];
    if (color.a >= 255u) {
        *p = pack_rgb(color.r, color.g, color.b);
    } else if (color.a > 0u) {
        *p = blend_over(*p, color);
    }
}

static void put_px_rgba(int x, int y, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    uint32_t *p;
    if (x < 0 || y < 0 || x >= g_width || y >= g_height || a == 0u) return;
    p = &g_fb[(size_t)y * (size_t)g_width + (size_t)x];
    if (a >= 255u) {
        *p = pack_rgb(r, g, b);
    } else {
        *p = blend_over_rgba(*p, r, g, b, a);
    }
}

static void fill_rect(int x, int y, int w, int h, flush_color_t color) {
    int px;
    int py;
    int x0;
    int y0;
    int x1;
    int y1;
    if (w <= 0 || h <= 0 || color.a == 0u) return;

    x0 = max_i(0, x);
    y0 = max_i(0, y);
    x1 = min_i(g_width, x + w);
    y1 = min_i(g_height, y + h);
    if (x0 >= x1 || y0 >= y1) return;

    for (py = y0; py < y1; ++py) {
        for (px = x0; px < x1; ++px) {
            put_px(px, py, color);
        }
    }
}

static int point_in_rounded_rect(int px, int py, int w, int h, int radius) {
    if (px < 0 || py < 0 || px >= w || py >= h) return 0;
    if (radius <= 0) return 1;

    if (px >= radius && px < w - radius) return 1;
    if (py >= radius && py < h - radius) return 1;

    {
        int cx = (px < radius) ? (radius - 1) : (w - radius);
        int cy = (py < radius) ? (radius - 1) : (h - radius);
        int dx = px - cx;
        int dy = py - cy;
        return (dx * dx + dy * dy) <= radius * radius;
    }
}

static void fill_round_rect(int x, int y, int w, int h, int radius, flush_color_t color) {
    int px;
    int py;
    int x0;
    int y0;
    int x1;
    int y1;

    if (w <= 0 || h <= 0 || color.a == 0u) return;
    radius = clamp_i(radius, 0, min_i(w, h) / 2);
    if (radius <= 0) {
        fill_rect(x, y, w, h, color);
        return;
    }

    x0 = max_i(0, x);
    y0 = max_i(0, y);
    x1 = min_i(g_width, x + w);
    y1 = min_i(g_height, y + h);
    if (x0 >= x1 || y0 >= y1) return;

    for (py = y0; py < y1; ++py) {
        int ly = py - y;
        for (px = x0; px < x1; ++px) {
            int lx = px - x;
            if (point_in_rounded_rect(lx, ly, w, h, radius)) {
                put_px(px, py, color);
            }
        }
    }
}

static void fill_stroke_round(int x, int y, int w, int h, int radius, int thickness, flush_color_t color) {
    int px;
    int py;
    int x0;
    int y0;
    int x1;
    int y1;
    int iw;
    int ih;
    int ir;

    if (w <= 0 || h <= 0 || color.a == 0u) return;
    thickness = clamp_i(thickness, 1, min_i(w, h));
    radius = clamp_i(radius, 0, min_i(w, h) / 2);

    iw = w - thickness * 2;
    ih = h - thickness * 2;
    if (iw <= 0 || ih <= 0) {
        fill_round_rect(x, y, w, h, radius, color);
        return;
    }
    ir = radius - thickness;
    if (ir < 0) ir = 0;

    x0 = max_i(0, x);
    y0 = max_i(0, y);
    x1 = min_i(g_width, x + w);
    y1 = min_i(g_height, y + h);
    if (x0 >= x1 || y0 >= y1) return;

    for (py = y0; py < y1; ++py) {
        int ly = py - y;
        for (px = x0; px < x1; ++px) {
            int lx = px - x;
            if (!point_in_rounded_rect(lx, ly, w, h, radius)) continue;
            if (point_in_rounded_rect(lx - thickness, ly - thickness, iw, ih, ir)) continue;
            put_px(px, py, color);
        }
    }
}

static void fill_circle(int cx, int cy, int radius, flush_color_t color) {
    int y;
    int rr;
    if (radius <= 0 || color.a == 0u) return;
    rr = radius * radius;
    for (y = -radius; y <= radius; ++y) {
        int yy = y * y;
        int x;
        for (x = -radius; x <= radius; ++x) {
            if (x * x + yy <= rr) {
                put_px(cx + x, cy + y, color);
            }
        }
    }
}

static void fill_vgradient(int x, int y, int w, int h, flush_color_t top, flush_color_t bottom) {
    int py;
    int denom;
    if (w <= 0 || h <= 0) return;
    denom = (h <= 1) ? 1 : (h - 1);

    for (py = 0; py < h; ++py) {
        int t = py;
        flush_color_t c;
        c.r = (uint8_t)(((int)top.r * (denom - t) + (int)bottom.r * t) / denom);
        c.g = (uint8_t)(((int)top.g * (denom - t) + (int)bottom.g * t) / denom);
        c.b = (uint8_t)(((int)top.b * (denom - t) + (int)bottom.b * t) / denom);
        c.a = (uint8_t)(((int)top.a * (denom - t) + (int)bottom.a * t) / denom);
        fill_rect(x, y + py, w, 1, c);
    }
}

static void fill_shadow(int x, int y, int w, int h, int radius, int spread, flush_color_t color) {
    int i;
    if (spread <= 0 || color.a == 0u) return;
    for (i = spread; i >= 1; --i) {
        flush_color_t c = color;
        c.a = (uint8_t)((int)color.a / (i + 1));
        fill_round_rect(x - i, y - i / 2, w + i * 2, h + i * 2, radius + i, c);
    }
}

static void fill_noise(int x, int y, int w, int h, uint32_t seed, uint8_t alpha) {
    int px;
    int py;
    flush_color_t c;
    c.r = 255u;
    c.g = 255u;
    c.b = 255u;
    c.a = alpha;
    if (w <= 0 || h <= 0 || alpha == 0u) return;

    for (py = y; py < y + h; py += 2) {
        for (px = x; px < x + w; px += 2) {
            uint32_t hsh = hash_u32((uint32_t)px * 374761393u ^ (uint32_t)py * 668265263u ^ seed);
            if ((hsh & 0x1Fu) == 0u) {
                put_px(px, py, c);
            }
        }
    }
}

static uint32_t sample_image_bilinear(const ridux_image_t *img, int fx, int fy) {
    int ix0;
    int iy0;
    int ix1;
    int iy1;
    uint32_t wx;
    uint32_t wy;
    uint32_t p00;
    uint32_t p01;
    uint32_t p10;
    uint32_t p11;
    uint32_t a_top;
    uint32_t a_bot;
    uint32_t a;
    uint32_t r_top;
    uint32_t r_bot;
    uint32_t r;
    uint32_t g_top;
    uint32_t g_bot;
    uint32_t g;
    uint32_t b_top;
    uint32_t b_bot;
    uint32_t b;

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

static void draw_image_scaled_alpha(int x, int y, int w, int h, const ridux_image_t *img, uint8_t alpha) {
    int dy;
    int step_x;
    int step_y;
    int minify_hi;
    if (w <= 0 || h <= 0 || !img || !img->pixels || !img->width || !img->height || alpha == 0u) return;

    step_x = (int)(((uint64_t)img->width << 16) / (uint64_t)w);
    step_y = (int)(((uint64_t)img->height << 16) / (uint64_t)h);
    minify_hi = (step_x > (int)(1u << 16)) || (step_y > (int)(1u << 16));

    for (dy = 0; dy < h; ++dy) {
        int fy = (int)(((((uint64_t)dy << 16) + 32768ull) * (uint64_t)img->height) / (uint64_t)h) - 32768;
        int dx;
        for (dx = 0; dx < w; ++dx) {
            int fx = (int)(((((uint64_t)dx << 16) + 32768ull) * (uint64_t)img->width) / (uint64_t)w) - 32768;
            uint32_t packed = minify_hi
                ? sample_image_bilinear_4tap(img, fx, fy, step_x, step_y)
                : sample_image_bilinear(img, fx, fy);
            uint8_t sa = (uint8_t)((packed >> 24) & 0xFFu);
            uint8_t sr;
            uint8_t sg;
            uint8_t sb;
            if (sa == 0u) continue;
            if (alpha != 255u) {
                sa = (uint8_t)(((uint16_t)sa * alpha) / 255u);
                if (sa == 0u) continue;
            }
            sr = (uint8_t)((packed >> 16) & 0xFFu);
            sg = (uint8_t)((packed >> 8) & 0xFFu);
            sb = (uint8_t)(packed & 0xFFu);
            put_px_rgba(x + dx, y + dy, sr, sg, sb, sa);
        }
    }
}

static const uint8_t *glyph_for(char ch) {
    size_t i;
    static const uint8_t blank[7] = {0,0,0,0,0,0,0};
    if (ch >= 'a' && ch <= 'z') ch = (char)(ch - 32);
    for (i = 0; i < sizeof(g_glyphs) / sizeof(g_glyphs[0]); ++i) {
        if (g_glyphs[i].ch == ch) return g_glyphs[i].row;
    }
    return blank;
}

static void draw_text_raw(int x, int y, int scale, flush_color_t color, const char *text) {
    int i;
    if (!text || !*text || scale <= 0 || color.a == 0u) return;

    for (i = 0; text[i]; ++i) {
        const uint8_t *rows = glyph_for(text[i]);
        int row;
        int col;
        int ox = x + i * 6 * scale;
        for (row = 0; row < 7; ++row) {
            uint8_t bits = rows[row];
            for (col = 0; col < 5; ++col) {
                if (bits & (1u << (4 - col))) {
                    fill_rect(ox + col * scale, y + row * scale, scale, scale, color);
                }
            }
        }
    }
}

static flush_cmd_t *push_cmd(cmd_type_t type) {
    flush_cmd_t *cmd;
    if (g_count >= FLUSH_MAX_COMMANDS) return NULL;
    cmd = &g_queue[g_count++];
    (void)memset(cmd, 0, sizeof(*cmd));
    cmd->type = type;
    return cmd;
}

int flush_init(int width, int height) {
    size_t count;
    if (width <= 0 || height <= 0) return 0;
    count = (size_t)width * (size_t)height;
    g_fb = (uint32_t *)malloc(count * sizeof(uint32_t));
    if (!g_fb) return 0;
    g_width = width;
    g_height = height;
    g_count = 0;
    (void)memset(g_fb, 0, count * sizeof(uint32_t));
    return 1;
}

void flush_shutdown(void) {
    free(g_fb);
    g_fb = NULL;
    g_width = 0;
    g_height = 0;
    g_count = 0;
}

int flush_resize(int width, int height) {
    uint32_t *next;
    size_t count;
    if (width <= 0 || height <= 0) return 0;
    count = (size_t)width * (size_t)height;
    next = (uint32_t *)malloc(count * sizeof(uint32_t));
    if (!next) return 0;
    (void)memset(next, 0, count * sizeof(uint32_t));
    free(g_fb);
    g_fb = next;
    g_width = width;
    g_height = height;
    g_count = 0;
    return 1;
}

int flush_width(void) { return g_width; }
int flush_height(void) { return g_height; }
const uint32_t *flush_pixels(void) { return g_fb; }
uint32_t *flush_pixels_mut(void) { return g_fb; }

void flush_reset(void) { g_count = 0; }

void flush_clear(flush_color_t color) {
    flush_cmd_t *cmd = push_cmd(CMD_CLEAR);
    if (!cmd) return;
    cmd->c0 = color;
}

void flush_rect(int x, int y, int w, int h, flush_color_t color) {
    flush_cmd_t *cmd = push_cmd(CMD_RECT);
    if (!cmd) return;
    cmd->x = x; cmd->y = y; cmd->w = w; cmd->h = h;
    cmd->c0 = color;
}

void flush_round_rect(int x, int y, int w, int h, int radius, flush_color_t color) {
    flush_cmd_t *cmd = push_cmd(CMD_ROUND_RECT);
    if (!cmd) return;
    cmd->x = x; cmd->y = y; cmd->w = w; cmd->h = h;
    cmd->radius = radius;
    cmd->c0 = color;
}

void flush_stroke_round(int x, int y, int w, int h, int radius, int thickness, flush_color_t color) {
    flush_cmd_t *cmd = push_cmd(CMD_STROKE_ROUND);
    if (!cmd) return;
    cmd->x = x; cmd->y = y; cmd->w = w; cmd->h = h;
    cmd->radius = radius;
    cmd->thickness = thickness;
    cmd->c0 = color;
}

void flush_circle(int cx, int cy, int radius, flush_color_t color) {
    flush_cmd_t *cmd = push_cmd(CMD_CIRCLE);
    if (!cmd) return;
    cmd->x = cx;
    cmd->y = cy;
    cmd->radius = radius;
    cmd->c0 = color;
}

void flush_vgradient(int x, int y, int w, int h, flush_color_t top, flush_color_t bottom) {
    flush_cmd_t *cmd = push_cmd(CMD_VGRAD);
    if (!cmd) return;
    cmd->x = x; cmd->y = y; cmd->w = w; cmd->h = h;
    cmd->c0 = top;
    cmd->c1 = bottom;
}

void flush_shadow(int x, int y, int w, int h, int radius, int spread, flush_color_t color) {
    flush_cmd_t *cmd = push_cmd(CMD_SHADOW);
    if (!cmd) return;
    cmd->x = x; cmd->y = y; cmd->w = w; cmd->h = h;
    cmd->radius = radius;
    cmd->thickness = spread;
    cmd->c0 = color;
}

void flush_noise(int x, int y, int w, int h, uint32_t seed, uint8_t alpha) {
    flush_cmd_t *cmd = push_cmd(CMD_NOISE);
    if (!cmd) return;
    cmd->x = x; cmd->y = y; cmd->w = w; cmd->h = h;
    cmd->seed = seed;
    cmd->c0 = (flush_color_t){255, 255, 255, alpha};
}

void flush_image(int x, int y, int w, int h, const ridux_image_t *img, uint8_t alpha) {
    flush_cmd_t *cmd = push_cmd(CMD_IMAGE);
    if (!cmd) return;
    cmd->x = x;
    cmd->y = y;
    cmd->w = w;
    cmd->h = h;
    cmd->image = img;
    cmd->c0.a = alpha;
}

void flush_text(int x, int y, int scale, flush_color_t color, const char *text) {
    flush_cmd_t *cmd = push_cmd(CMD_TEXT);
    if (!cmd || !text) return;
    cmd->x = x; cmd->y = y;
    cmd->scale = (scale <= 0) ? 1 : scale;
    cmd->c0 = color;
    (void)strncpy(cmd->text, text, FLUSH_TEXT_MAX - 1);
    cmd->text[FLUSH_TEXT_MAX - 1] = '\0';
}

int flush_text_width(const char *text, int scale) {
    int len;
    if (!text) return 0;
    len = (int)strlen(text);
    if (len <= 0) return 0;
    if (scale <= 0) scale = 1;
    return (len * 6 - 1) * scale;
}

void flush_execute(void) {
    size_t i;
    if (!g_fb || g_width <= 0 || g_height <= 0) return;

    for (i = 0; i < g_count; ++i) {
        flush_cmd_t *cmd = &g_queue[i];
        switch (cmd->type) {
            case CMD_CLEAR: {
                uint32_t pixel = pack_rgb(cmd->c0.r, cmd->c0.g, cmd->c0.b);
                size_t j;
                size_t total = (size_t)g_width * (size_t)g_height;
                for (j = 0; j < total; ++j) g_fb[j] = pixel;
            } break;
            case CMD_RECT:
                fill_rect(cmd->x, cmd->y, cmd->w, cmd->h, cmd->c0);
                break;
            case CMD_ROUND_RECT:
                fill_round_rect(cmd->x, cmd->y, cmd->w, cmd->h, cmd->radius, cmd->c0);
                break;
            case CMD_STROKE_ROUND:
                fill_stroke_round(cmd->x, cmd->y, cmd->w, cmd->h, cmd->radius, cmd->thickness, cmd->c0);
                break;
            case CMD_CIRCLE:
                fill_circle(cmd->x, cmd->y, cmd->radius, cmd->c0);
                break;
            case CMD_VGRAD:
                fill_vgradient(cmd->x, cmd->y, cmd->w, cmd->h, cmd->c0, cmd->c1);
                break;
            case CMD_SHADOW:
                fill_shadow(cmd->x, cmd->y, cmd->w, cmd->h, cmd->radius, cmd->thickness, cmd->c0);
                break;
            case CMD_NOISE:
                fill_noise(cmd->x, cmd->y, cmd->w, cmd->h, cmd->seed, cmd->c0.a);
                break;
            case CMD_IMAGE:
                draw_image_scaled_alpha(cmd->x, cmd->y, cmd->w, cmd->h, cmd->image, cmd->c0.a);
                break;
            case CMD_TEXT:
                draw_text_raw(cmd->x, cmd->y, cmd->scale, cmd->c0, cmd->text);
                break;
            default:
                break;
        }
    }
}
