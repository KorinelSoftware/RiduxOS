#ifndef RIDUX_UI_H
#define RIDUX_UI_H

#include "user_libridux.h"

typedef struct rdui_rect {
    int x;
    int y;
    int w;
    int h;
} rdui_rect_t;

typedef enum rdui_node_kind {
    RDUI_NODE_PANEL = 1,
    RDUI_NODE_LABEL = 2,
    RDUI_NODE_BUTTON = 3,
    RDUI_NODE_BAR = 4
} rdui_node_kind_t;

typedef struct rdui_node {
    rdui_node_kind_t kind;
    rdui_rect_t rect;
    const char *text;
    rd_u32 bg;
    rd_u32 fg;
    rd_u32 accent;
    rd_u8 alpha;
    rd_u8 radius;
    rd_u8 scale;
    rd_u8 flags;
} rdui_node_t;

typedef struct rdui_style {
    const char *class_name;
    rd_u32 bg;
    rd_u32 bg2;
    rd_u32 fg;
    rd_u32 muted;
    rd_u32 accent;
    rd_u8 alpha;
    rd_u8 radius;
    rd_u8 border_alpha;
} rdui_style_t;

typedef struct rdui_theme {
    rd_u32 bg;
    rd_u32 bg2;
    rd_u32 surface;
    rd_u32 surface2;
    rd_u32 text;
    rd_u32 muted;
    rd_u32 accent;
    rd_u32 accent2;
    rd_u32 border;
    rd_u32 success;
    rd_u32 warning;
    rd_u32 danger;
    int radius_sm;
    int radius_md;
    int radius_lg;
    int gap;
} rdui_theme_t;

typedef struct rdui_nav_item_desc {
    const char *icon;
    const char *label;
    int active;
} rdui_nav_item_desc_t;

typedef struct rdui_folder_desc {
    const char *label;
    const char *mark;
    rd_u32 top;
    rd_u32 bottom;
} rdui_folder_desc_t;

typedef struct rdui_file_desc {
    const char *name;
    const char *kind;
    const char *size;
    const char *date;
    rd_u32 color;
} rdui_file_desc_t;

#define RDUI_F_CENTER_TEXT 0x01u
#define RDUI_F_RIGHT_TEXT  0x02u

#if defined(__GNUC__)
#define RDUI_UNUSED __attribute__((unused))
#else
#define RDUI_UNUSED
#endif

static RDUI_UNUSED void rdui_text_center(rd_window_t *w, int x, int y, int ww, int hh,
                                         const char *s, rd_u32 color, int scale);
static RDUI_UNUSED void rdui_panel_class(rd_window_t *w, int x, int y, int ww, int hh,
                                         const char *class_name);

static RDUI_UNUSED rdui_theme_t rdui_theme_dark(void) {
    rdui_theme_t t;
    t.bg = rd_rgb(10, 14, 22);
    t.bg2 = rd_rgb(6, 9, 15);
    t.surface = rd_rgb(18, 24, 34);
    t.surface2 = rd_rgb(10, 14, 22);
    t.text = rd_rgb(240, 246, 252);
    t.muted = rd_rgb(149, 166, 185);
    t.accent = rd_rgb(82, 137, 255);
    t.accent2 = rd_rgb(50, 203, 155);
    t.border = rd_rgb(255, 255, 255);
    t.success = rd_rgb(62, 205, 108);
    t.warning = rd_rgb(245, 183, 64);
    t.danger = rd_rgb(255, 95, 86);
    t.radius_sm = 10;
    t.radius_md = 16;
    t.radius_lg = 24;
    t.gap = 12;
    return t;
}

static RDUI_UNUSED rdui_rect_t rdui_rect(int x, int y, int w, int h) {
    rdui_rect_t r;
    r.x = x; r.y = y; r.w = w; r.h = h;
    return r;
}

static RDUI_UNUSED rdui_rect_t rdui_inset(rdui_rect_t r, int pad) {
    rdui_rect_t o;
    o.x = r.x + pad;
    o.y = r.y + pad;
    o.w = r.w - pad * 2;
    o.h = r.h - pad * 2;
    if (o.w < 0) o.w = 0;
    if (o.h < 0) o.h = 0;
    return o;
}

static RDUI_UNUSED rdui_rect_t rdui_grid_rect(rdui_rect_t area, int cols,
                                              int gap, int index, int row_h) {
    int col;
    int row;
    int cell_w;
    if (cols < 1) cols = 1;
    if (gap < 0) gap = 0;
    col = index % cols;
    row = index / cols;
    cell_w = (area.w - gap * (cols - 1)) / cols;
    return rdui_rect(area.x + col * (cell_w + gap),
                     area.y + row * (row_h + gap),
                     cell_w, row_h);
}

static RDUI_UNUSED int rdui_streq(const char *a, const char *b) {
    int i = 0;
    if (!a || !b) return 0;
    while (a[i] && b[i] && a[i] == b[i]) ++i;
    return a[i] == 0 && b[i] == 0;
}

static RDUI_UNUSED rdui_style_t rdui_style_for(const char *class_name) {
    rdui_style_t s;
    s.class_name = class_name;
    s.bg = rd_rgb(16, 22, 32);
    s.bg2 = rd_rgb(9, 13, 20);
    s.fg = rd_rgb(238, 247, 255);
    s.muted = rd_rgb(150, 170, 194);
    s.accent = rd_rgb(47, 125, 246);
    s.alpha = 146;
    s.radius = 14;
    s.border_alpha = 42;
    if (rdui_streq(class_name, "surface.window")) {
        s.bg = rd_rgb(17, 24, 32);
        s.bg2 = rd_rgb(7, 10, 15);
        s.alpha = 238;
        s.radius = 20;
    } else if (rdui_streq(class_name, "surface.card")) {
        s.bg = rd_rgb(20, 27, 36);
        s.bg2 = rd_rgb(11, 15, 22);
        s.alpha = 188;
        s.radius = 14;
    } else if (rdui_streq(class_name, "surface.popover")) {
        s.bg = rd_rgb(20, 26, 37);
        s.bg2 = rd_rgb(9, 12, 18);
        s.alpha = 234;
        s.radius = 28;
        s.border_alpha = 34;
    } else if (rdui_streq(class_name, "launcher.tile")) {
        s.bg = rd_rgb(255, 255, 255);
        s.bg2 = rd_rgb(255, 255, 255);
        s.alpha = 10;
        s.radius = 20;
        s.border_alpha = 18;
    } else if (rdui_streq(class_name, "search.field")) {
        s.bg = rd_rgb(255, 255, 255);
        s.bg2 = rd_rgb(255, 255, 255);
        s.alpha = 12;
        s.radius = 17;
        s.border_alpha = 18;
    } else if (rdui_streq(class_name, "status.pill")) {
        s.bg = rd_rgb(255, 255, 255);
        s.bg2 = rd_rgb(255, 255, 255);
        s.alpha = 28;
        s.radius = 12;
        s.border_alpha = 18;
    } else if (rdui_streq(class_name, "button.primary")) {
        s.bg = rd_rgb(47, 125, 246);
        s.bg2 = rd_rgb(35, 104, 214);
        s.accent = rd_rgb(102, 166, 255);
        s.alpha = 214;
        s.radius = 13;
    } else if (rdui_streq(class_name, "button.ghost")) {
        s.bg = rd_rgb(255, 255, 255);
        s.bg2 = rd_rgb(255, 255, 255);
        s.alpha = 30;
        s.radius = 13;
    } else if (rdui_streq(class_name, "dock.glass")) {
        s.bg = rd_rgb(12, 17, 25);
        s.bg2 = rd_rgb(7, 9, 14);
        s.alpha = 218;
        s.radius = 22;
    } else if (rdui_streq(class_name, "calc.display")) {
        s.bg = rd_rgb(3, 19, 37);
        s.bg2 = rd_rgb(7, 44, 73);
        s.alpha = 220;
        s.radius = 14;
    }
    return s;
}

static RDUI_UNUSED rd_u32 rdui_blend(rd_u32 dst, rd_u32 src, rd_u32 alpha) {
    rd_u32 inv;
    rd_u32 sr, sg, sb, dr, dg, db;
    if (alpha == 0u) return dst;
    if (alpha >= 255u) return src & 0x00FFFFFFu;
    inv = 255u - alpha;
    sr = (src >> 16) & 255u;
    sg = (src >> 8) & 255u;
    sb = src & 255u;
    dr = (dst >> 16) & 255u;
    dg = (dst >> 8) & 255u;
    db = dst & 255u;
    return (((sr * alpha + dr * inv + 127u) / 255u) << 16) |
           (((sg * alpha + dg * inv + 127u) / 255u) << 8) |
            ((sb * alpha + db * inv + 127u) / 255u);
}

static RDUI_UNUSED void rdui_pixel_alpha(rd_window_t *w, int x, int y, rd_u32 color, rd_u32 alpha) {
    rd_u32 *p;
    if (!w || !w->fb || alpha == 0u) return;
    if (x < 0 || y < 0 || (rd_u32)x >= w->width || (rd_u32)y >= w->height) return;
    p = w->fb + (rd_size)y * (w->stride / 4u) + (rd_size)x;
    *p = rdui_blend(*p, color, alpha);
}

static RDUI_UNUSED void rdui_fill_alpha(rd_window_t *w, int x, int y, int ww, int hh,
                            rd_u32 color, rd_u32 alpha) {
    int row, col;
    if (!w || !w->fb || ww <= 0 || hh <= 0 || alpha == 0u) return;
    if (x < 0) { ww += x; x = 0; }
    if (y < 0) { hh += y; y = 0; }
    if ((rd_u32)x >= w->width || (rd_u32)y >= w->height) return;
    if ((rd_u32)(x + ww) > w->width) ww = (int)w->width - x;
    if ((rd_u32)(y + hh) > w->height) hh = (int)w->height - y;
    if (ww <= 0 || hh <= 0) return;
    for (row = 0; row < hh; ++row) {
        rd_u32 *line = w->fb + (rd_size)(y + row) * (w->stride / 4u) + (rd_size)x;
        for (col = 0; col < ww; ++col) line[col] = rdui_blend(line[col], color, alpha);
    }
}

static RDUI_UNUSED rd_u32 rdui_mix(rd_u32 a, rd_u32 b, rd_u32 t) {
    rd_u32 ar = (a >> 16) & 255u;
    rd_u32 ag = (a >> 8) & 255u;
    rd_u32 ab = a & 255u;
    rd_u32 br = (b >> 16) & 255u;
    rd_u32 bg = (b >> 8) & 255u;
    rd_u32 bb = b & 255u;
    return (((ar * (255u - t) + br * t) / 255u) << 16) |
           (((ag * (255u - t) + bg * t) / 255u) << 8) |
            ((ab * (255u - t) + bb * t) / 255u);
}

static RDUI_UNUSED void rdui_gradient_v(rd_window_t *w, int x, int y, int ww, int hh,
                            rd_u32 top, rd_u32 bottom) {
    int row;
    if (hh <= 0) return;
    for (row = 0; row < hh; ++row) {
        rd_u32 t = (rd_u32)((row * 255) / (hh > 1 ? hh - 1 : 1));
        rd_fill_rect(w, x, y + row, ww, 1, rdui_mix(top, bottom, t));
    }
}

static RDUI_UNUSED void rdui_gradient_h(rd_window_t *w, int x, int y, int ww, int hh,
                            rd_u32 left, rd_u32 right) {
    int col;
    if (ww <= 0) return;
    for (col = 0; col < ww; ++col) {
        rd_u32 t = (rd_u32)((col * 255) / (ww > 1 ? ww - 1 : 1));
        rd_fill_rect(w, x + col, y, 1, hh, rdui_mix(left, right, t));
    }
}

static RDUI_UNUSED void rdui_stroke(rd_window_t *w, int x, int y, int ww, int hh,
                        rd_u32 color, rd_u32 alpha) {
    rdui_fill_alpha(w, x, y, ww, 1, color, alpha);
    rdui_fill_alpha(w, x, y + hh - 1, ww, 1, color, alpha);
    rdui_fill_alpha(w, x, y, 1, hh, color, alpha);
    rdui_fill_alpha(w, x + ww - 1, y, 1, hh, color, alpha);
}

static RDUI_UNUSED void rdui_round_alpha(rd_window_t *w, int x, int y, int ww, int hh,
                             int r, rd_u32 color, rd_u32 alpha) {
    int ix, iy;
    int outer;
    int inner;
    if (ww <= 0 || hh <= 0) return;
    if (r < 2) {
        rdui_fill_alpha(w, x, y, ww, hh, color, alpha);
        return;
    }
    if (r * 2 > ww) r = ww / 2;
    if (r * 2 > hh) r = hh / 2;
    if (r < 2) {
        rdui_fill_alpha(w, x, y, ww, hh, color, alpha);
        return;
    }
    outer = r * r;
    inner = (r - 1) * (r - 1);
    rdui_fill_alpha(w, x + r, y, ww - r * 2, hh, color, alpha);
    rdui_fill_alpha(w, x, y + r, r, hh - r * 2, color, alpha);
    rdui_fill_alpha(w, x + ww - r, y + r, r, hh - r * 2, color, alpha);
    for (iy = 0; iy < r; ++iy) {
        for (ix = 0; ix < r; ++ix) {
            int dx = r - 1 - ix;
            int dy = r - 1 - iy;
            int d2 = dx * dx + dy * dy;
            if (d2 <= inner) {
                rdui_pixel_alpha(w, x + ix, y + iy, color, alpha);
                rdui_pixel_alpha(w, x + ww - 1 - ix, y + iy, color, alpha);
                rdui_pixel_alpha(w, x + ix, y + hh - 1 - iy, color, alpha);
                rdui_pixel_alpha(w, x + ww - 1 - ix, y + hh - 1 - iy, color, alpha);
            } else if (d2 <= outer) {
                rd_u32 edge_alpha = (alpha * (rd_u32)(outer - d2 + 1)) /
                                    (rd_u32)(outer - inner + 1);
                if (edge_alpha) {
                    rdui_pixel_alpha(w, x + ix, y + iy, color, edge_alpha);
                    rdui_pixel_alpha(w, x + ww - 1 - ix, y + iy, color, edge_alpha);
                    rdui_pixel_alpha(w, x + ix, y + hh - 1 - iy, color, edge_alpha);
                    rdui_pixel_alpha(w, x + ww - 1 - ix, y + hh - 1 - iy, color, edge_alpha);
                }
            }
        }
    }
}

static RDUI_UNUSED rd_u32 rdui_round_clip_alpha(int lx, int ly, int ww, int hh,
                                                int r, rd_u32 alpha) {
    int cx, cy, dx, dy, d2, outer, inner;
    if (alpha == 0u) return 0u;
    if (r < 2) return alpha;
    if (r * 2 > ww) r = ww / 2;
    if (r * 2 > hh) r = hh / 2;
    if (r < 2) return alpha;
    if ((lx >= r && lx < ww - r) || (ly >= r && ly < hh - r)) return alpha;
    cx = lx < r ? r - 1 : ww - r;
    cy = ly < r ? r - 1 : hh - r;
    dx = lx - cx;
    dy = ly - cy;
    d2 = dx * dx + dy * dy;
    outer = r * r;
    if (d2 > outer) return 0u;
    inner = (r - 1) * (r - 1);
    if (d2 <= inner) return alpha;
    return (alpha * (rd_u32)(outer - d2 + 1)) / (rd_u32)(outer - inner + 1);
}

static RDUI_UNUSED void rdui_round_gradient_v(rd_window_t *w, int x, int y,
                                  int ww, int hh, int r,
                                  rd_u32 top, rd_u32 bottom, rd_u32 alpha) {
    int row, col;
    if (!w || !w->fb || ww <= 0 || hh <= 0 || alpha == 0u) return;
    for (row = 0; row < hh; ++row) {
        rd_u32 t = (rd_u32)((row * 255) / (hh > 1 ? hh - 1 : 1));
        rd_u32 c = rdui_mix(top, bottom, t);
        for (col = 0; col < ww; ++col) {
            rd_u32 a = rdui_round_clip_alpha(col, row, ww, hh, r, alpha);
            if (a) rdui_pixel_alpha(w, x + col, y + row, c, a);
        }
    }
}

static RDUI_UNUSED void rdui_round_gradient_h(rd_window_t *w, int x, int y,
                                  int ww, int hh, int r,
                                  rd_u32 left, rd_u32 right, rd_u32 alpha) {
    int row, col;
    if (!w || !w->fb || ww <= 0 || hh <= 0 || alpha == 0u) return;
    for (col = 0; col < ww; ++col) {
        rd_u32 t = (rd_u32)((col * 255) / (ww > 1 ? ww - 1 : 1));
        rd_u32 c = rdui_mix(left, right, t);
        for (row = 0; row < hh; ++row) {
            rd_u32 a = rdui_round_clip_alpha(col, row, ww, hh, r, alpha);
            if (a) rdui_pixel_alpha(w, x + col, y + row, c, a);
        }
    }
}

static RDUI_UNUSED void rdui_round_outline(rd_window_t *w, int x, int y, int ww, int hh,
                               int r, rd_u32 color, rd_u32 alpha) {
    int ix, iy;
    int outer;
    int inner;
    if (ww <= 1 || hh <= 1 || alpha == 0u) return;
    if (r < 3) {
        rdui_stroke(w, x, y, ww, hh, color, alpha);
        return;
    }
    if (r * 2 > ww) r = ww / 2;
    if (r * 2 > hh) r = hh / 2;
    if (r < 3) {
        rdui_stroke(w, x, y, ww, hh, color, alpha);
        return;
    }
    rdui_fill_alpha(w, x + r, y, ww - r * 2, 1, color, alpha);
    rdui_fill_alpha(w, x + r, y + hh - 1, ww - r * 2, 1, color, alpha);
    rdui_fill_alpha(w, x, y + r, 1, hh - r * 2, color, alpha);
    rdui_fill_alpha(w, x + ww - 1, y + r, 1, hh - r * 2, color, alpha);
    outer = r * r;
    inner = (r - 2) * (r - 2);
    for (iy = 0; iy < r; ++iy) {
        for (ix = 0; ix < r; ++ix) {
            int dx = r - 1 - ix;
            int dy = r - 1 - iy;
            int d2 = dx * dx + dy * dy;
            if (d2 <= outer && d2 >= inner) {
                rd_u32 edge_alpha = alpha;
                if (d2 > outer - r) edge_alpha = alpha / 2u + 1u;
                rdui_pixel_alpha(w, x + ix, y + iy, color, edge_alpha);
                rdui_pixel_alpha(w, x + ww - 1 - ix, y + iy, color, edge_alpha);
                rdui_pixel_alpha(w, x + ix, y + hh - 1 - iy, color, edge_alpha);
                rdui_pixel_alpha(w, x + ww - 1 - ix, y + hh - 1 - iy, color, edge_alpha);
            }
        }
    }
}

static RDUI_UNUSED void rdui_shadow(rd_window_t *w, int x, int y, int ww, int hh,
                        int radius, rd_u32 strength) {
    int spread;
    int offset;
    rd_u32 alpha;
    if (strength == 0u) return;
    spread = 5 + (int)(strength / 28u);
    offset = 2 + (int)(strength / 96u);
    alpha = strength / 10u;
    if (alpha > 14u) alpha = 14u;
    rdui_round_alpha(w, x - spread, y + offset - spread,
                     ww + spread * 2, hh + spread * 2,
                     radius + spread, rd_rgb(0, 0, 0), alpha);
}

static RDUI_UNUSED void rdui_glass(rd_window_t *w, int x, int y, int ww, int hh,
                       rd_u32 tint, rd_u32 alpha, int radius) {
    rd_u32 top = rdui_mix(tint, rd_rgb(255, 255, 255), 8u);
    rd_u32 bottom = rdui_mix(tint, rd_rgb(0, 0, 0), 14u);
    rdui_shadow(w, x, y, ww, hh, radius, 42);
    rdui_round_gradient_v(w, x, y, ww, hh, radius, top, bottom, alpha);
    rdui_round_outline(w, x, y, ww, hh, radius, rd_rgb(255, 255, 255), 16);
    rdui_round_alpha(w, x + 2, y + 2, ww - 4, 1, 1,
                     rd_rgb(255, 255, 255), 18);
}

static RDUI_UNUSED void rdui_material(rd_window_t *w, rdui_rect_t r,
                                      rd_u32 tint, rd_u32 alpha,
                                      int radius, int depth) {
    rd_u32 top = rdui_mix(tint, rd_rgb(255, 255, 255), 7u);
    rd_u32 bottom = rdui_mix(tint, rd_rgb(0, 0, 0), 10u);
    if (depth < 0) depth = 0;
    if (depth > 4) depth = 4;
    rdui_shadow(w, r.x, r.y, r.w, r.h, radius,
                (rd_u32)(22 + depth * 8));
    rdui_round_gradient_v(w, r.x, r.y, r.w, r.h, radius,
                          top, bottom, alpha);
    rdui_round_outline(w, r.x, r.y, r.w, r.h, radius,
                       rd_rgb(255, 255, 255), (rd_u32)(12 + depth * 3));
}

static RDUI_UNUSED void rdui_pill(rd_window_t *w, int x, int y, int ww, int hh,
                      const char *label, rd_u32 tint, rd_u32 alpha) {
    int r = hh / 2;
    if (r < 3) r = 3;
    rdui_round_alpha(w, x, y, ww, hh, r, tint, alpha);
    rdui_round_outline(w, x, y, ww, hh, r, rd_rgb(255, 255, 255), 12);
    if (label) rdui_text_center(w, x, y, ww, hh, label, rd_rgb(235, 246, 255), 1);
}

static RDUI_UNUSED void rdui_search_field(rd_window_t *w, int x, int y, int ww, int hh,
                              const char *placeholder) {
    int r = hh / 2;
    if (r < 8) r = 8;
    rdui_round_alpha(w, x, y, ww, hh, r, rd_rgb(255, 255, 255), 12);
    rdui_round_outline(w, x, y, ww, hh, r, rd_rgb(255, 255, 255), 16);
    rdui_round_alpha(w, x + 16, y + hh / 2 - 5, 9, 9, 5, rd_rgb(130, 148, 168), 180);
    rdui_round_alpha(w, x + 24, y + hh / 2 + 3, 7, 2, 1, rd_rgb(130, 148, 168), 180);
    if (placeholder) rd_text(w, x + 42, y + (hh - 16) / 2, placeholder, rd_rgb(145, 162, 182));
}

static RDUI_UNUSED void rdui_slider(rd_window_t *w, int x, int y, int ww, int value,
                        rd_u32 accent) {
    int fill = (ww * value) / 100;
    if (fill < 0) fill = 0;
    if (fill > ww) fill = ww;
    rdui_round_alpha(w, x, y, ww, 8, 4, rd_rgb(255, 255, 255), 36);
    rdui_round_alpha(w, x, y, fill, 8, 4, accent, 222);
    rdui_round_alpha(w, x + fill - 5, y - 4, 16, 16, 8, rd_rgb(230, 240, 250), 200);
    rdui_round_outline(w, x + fill - 5, y - 4, 16, 16, 8, accent, 90);
}

static RDUI_UNUSED void rdui_panel_class(rd_window_t *w, int x, int y, int ww, int hh,
                                         const char *class_name) {
    rdui_style_t s = rdui_style_for(class_name);
    rdui_material(w, rdui_rect(x, y, ww, hh), s.bg, s.alpha, s.radius, 2);
}

static RDUI_UNUSED void rdui_button_class(rd_window_t *w, int x, int y, int ww, int hh,
                                          const char *label, const char *class_name,
                                          int active) {
    rdui_style_t s = rdui_style_for(class_name);
    rd_u32 bg = active ? s.bg : rd_rgb(18, 30, 48);
    rd_u32 bg2 = active ? s.bg2 : rd_rgb(9, 18, 31);
    rdui_shadow(w, x, y, ww, hh, s.radius, active ? 18u : 8u);
    rdui_round_gradient_v(w, x, y, ww, hh, s.radius, bg, bg2,
                          active ? s.alpha : 112u);
    rdui_round_outline(w, x, y, ww, hh, s.radius, rd_rgb(255, 255, 255),
                       active ? 30u : 16u);
    rdui_text_center(w, x, y, ww, hh, label, s.fg, 1);
}

static RDUI_UNUSED int rdui_strlen(const char *s) {
    int n = 0;
    if (!s) return 0;
    while (s[n]) ++n;
    return n;
}

static RDUI_UNUSED void rdui_text_center(rd_window_t *w, int x, int y, int ww, int hh,
                             const char *s, rd_u32 color, int scale) {
    int tw;
    if (scale < 1) scale = 1;
    tw = rd_text_width_scaled(s, scale);
    rd_text_scaled(w, x + (ww - tw) / 2, y + (hh - 16 * scale) / 2,
                   scale, s, color);
}

static RDUI_UNUSED void rdui_text_right(rd_window_t *w, int right, int y,
                            const char *s, rd_u32 color) {
    rd_text(w, right - rd_text_width(s), y, s, color);
}

static RDUI_UNUSED void rdui_button(rd_window_t *w, int x, int y, int ww, int hh,
                        const char *label, rd_u32 tint, int active) {
    rdui_round_gradient_v(w, x, y, ww, hh, 12,
                          active ? tint : rd_rgb(26, 40, 62),
                          active ? rdui_mix(tint, rd_rgb(0, 0, 0), 54u) : rd_rgb(9, 18, 31),
                          active ? 176u : 126u);
    rdui_round_outline(w, x, y, ww, hh, 12, rd_rgb(255, 255, 255), active ? 34u : 16u);
    rdui_text_center(w, x, y, ww, hh, label, rd_rgb(237, 246, 255), 1);
}

static RDUI_UNUSED void rdui_window_controls(rd_window_t *w, int x, int y) {
    rdui_round_alpha(w, x,      y, 12, 12, 6, rd_rgb(255, 95, 86), 225);
    rdui_round_alpha(w, x + 20, y, 12, 12, 6, rd_rgb(255, 189, 46), 225);
    rdui_round_alpha(w, x + 40, y, 12, 12, 6, rd_rgb(39, 201, 63), 225);
    rdui_round_outline(w, x,      y, 12, 12, 6, rd_rgb(255, 255, 255), 28);
    rdui_round_outline(w, x + 20, y, 12, 12, 6, rd_rgb(255, 255, 255), 24);
    rdui_round_outline(w, x + 40, y, 12, 12, 6, rd_rgb(255, 255, 255), 24);
}

static RDUI_UNUSED rdui_rect_t rdui_app_body(rd_window_t *w) {
    int h = w ? (int)w->height - 56 : 0;
    if (h < 0) h = 0;
    return rdui_rect(0, 56, w ? (int)w->width : 0, h);
}

static RDUI_UNUSED void rdui_app_window(rd_window_t *w, const char *title) {
    rdui_style_t win;
    if (!w) return;
    win = rdui_style_for("surface.window");
    rd_clear(w, win.bg2);
    rdui_gradient_v(w, 0, 0, (int)w->width, (int)w->height,
                    rd_rgb(16, 22, 32), win.bg2);
    rdui_fill_alpha(w, 0, 0, (int)w->width, 56, rd_rgb(255, 255, 255), 5);
    rdui_fill_alpha(w, 0, 54, (int)w->width, 1, rd_rgb(255, 255, 255), 14);
    rdui_fill_alpha(w, 0, 55, (int)w->width, 1, rd_rgb(0, 0, 0), 32);
    rdui_window_controls(w, 20, 21);
    rdui_text_center(w, 0, 15, (int)w->width, 24, title, win.fg, 1);
}

static RDUI_UNUSED void rdui_section_title(rd_window_t *w, int x, int y,
                                           const char *title) {
    rd_text(w, x, y, title, rd_rgb(247, 252, 255));
}

static RDUI_UNUSED void rdui_nav_item(rd_window_t *w, rdui_rect_t r,
                                      const char *icon, const char *label,
                                      int active) {
    rdui_theme_t t = rdui_theme_dark();
    rd_u32 fg = active ? t.text : rd_rgb(204, 222, 235);
    rd_u32 icon_fg = active ? rd_rgb(245, 251, 255) : rd_rgb(154, 196, 215);
    if (active) {
        rdui_round_gradient_h(w, r.x, r.y, r.w, r.h, 11,
                              rd_rgb(45, 119, 244), rd_rgb(28, 189, 188), 184);
        rdui_round_outline(w, r.x, r.y, r.w, r.h, 11, rd_rgb(255, 255, 255), 18);
    }
    if (icon && icon[0]) {
        rdui_text_center(w, r.x + 10, r.y, 22, r.h, icon, icon_fg, 1);
    }
    if (label) {
        rd_text(w, r.x + 38, r.y + (r.h - 16) / 2, label, fg);
    }
}

static RDUI_UNUSED void rdui_nav_list(rd_window_t *w, rdui_rect_t area,
                                      const rdui_nav_item_desc_t *items,
                                      int count) {
    int i;
    for (i = 0; i < count; ++i) {
        rdui_rect_t row = rdui_rect(area.x, area.y + i * 30, area.w, 26);
        rdui_nav_item(w, row, items[i].icon, items[i].label, items[i].active);
    }
}

static RDUI_UNUSED void rdui_toolbar_button(rd_window_t *w, rdui_rect_t r,
                                            const char *label, int active) {
    if (active) {
        rdui_round_gradient_h(w, r.x, r.y, r.w, r.h, 11,
                              rd_rgb(47, 116, 235), rd_rgb(38, 99, 205), 178);
        rdui_round_outline(w, r.x, r.y, r.w, r.h, 11, rd_rgb(255, 255, 255), 24);
    } else {
        rdui_round_alpha(w, r.x, r.y, r.w, r.h, 11, rd_rgb(255, 255, 255), 14);
        rdui_round_outline(w, r.x, r.y, r.w, r.h, 11, rd_rgb(255, 255, 255), 14);
    }
    rdui_text_center(w, r.x, r.y, r.w, r.h, label,
                     active ? rd_rgb(244, 250, 255) : rd_rgb(174, 195, 214), 1);
}

static RDUI_UNUSED void rdui_folder_glyph(rd_window_t *w, int x, int y,
                                          rd_u32 top, rd_u32 bottom,
                                          const char *mark) {
    rdui_round_gradient_v(w, x + 7, y, 32, 16, 6, top, bottom, 235);
    rdui_round_gradient_v(w, x, y + 9, 66, 48, 12, top, bottom, 246);
    rdui_round_outline(w, x, y + 9, 66, 48, 12, rd_rgb(255, 255, 255), 18);
    rdui_round_alpha(w, x + 7, y + 14, 52, 1, 1, rd_rgb(255, 255, 255), 28);
    if (mark && mark[0]) {
        rdui_text_center(w, x + 17, y + 23, 32, 24, mark,
                         rd_rgb(230, 250, 255), 2);
    }
}

static RDUI_UNUSED void rdui_folder_tile(rd_window_t *w, rdui_rect_t r,
                                         const rdui_folder_desc_t *folder) {
    rd_u32 top;
    rd_u32 bottom;
    if (!folder) return;
    top = folder->top;
    bottom = folder->bottom;
    rdui_round_alpha(w, r.x, r.y, r.w, r.h, 16, rd_rgb(255, 255, 255), 7);
    rdui_round_outline(w, r.x, r.y, r.w, r.h, 16, rd_rgb(255, 255, 255), 13);
    rdui_folder_glyph(w, r.x + (r.w - 66) / 2, r.y + 19, top, bottom, folder->mark);
    rdui_text_center(w, r.x - 4, r.y + r.h - 28, r.w + 8, 22,
                     folder->label, rd_rgb(246, 252, 255), 1);
}

static RDUI_UNUSED void rdui_file_row(rd_window_t *w, rdui_rect_t r,
                                      const rdui_file_desc_t *file) {
    rd_u32 icon_top;
    rd_u32 icon_bottom;
    if (!file) return;
    icon_top = rdui_mix(file->color, rd_rgb(255, 255, 255), 34u);
    icon_bottom = rdui_mix(file->color, rd_rgb(0, 0, 0), 30u);
    rdui_round_alpha(w, r.x, r.y, r.w, r.h, 12, rd_rgb(255, 255, 255), 8);
    rdui_fill_alpha(w, r.x + 2, r.y + r.h - 1, r.w - 4, 1,
                    rd_rgb(255, 255, 255), 15);
    rdui_round_gradient_v(w, r.x + 14, r.y + 8, 28, 30, 7,
                          icon_top, icon_bottom, 238);
    rdui_round_outline(w, r.x + 14, r.y + 8, 28, 30, 7,
                       rd_rgb(255, 255, 255), 18);
    rd_text(w, r.x + 54, r.y + 7, file->name, rd_rgb(246, 251, 255));
    rd_text(w, r.x + 54, r.y + 25, file->kind, rd_rgb(170, 204, 218));
    rdui_text_right(w, r.x + r.w - 178, r.y + 16, file->size, rd_rgb(204, 227, 237));
    rdui_text_right(w, r.x + r.w - 28, r.y + 16, file->date, rd_rgb(204, 227, 237));
}

static RDUI_UNUSED void rdui_info_row(rd_window_t *w, rdui_rect_t r,
                                      const char *label, const char *value) {
    rdui_round_alpha(w, r.x, r.y, r.w, r.h, 10, rd_rgb(255, 255, 255), 7);
    rdui_fill_alpha(w, r.x + 2, r.y + r.h - 1, r.w - 4, 1,
                    rd_rgb(255, 255, 255), 13);
    rd_text(w, r.x + 14, r.y + (r.h - 16) / 2, label, rd_rgb(218, 230, 240));
    rdui_text_right(w, r.x + r.w - 14, r.y + (r.h - 16) / 2,
                    value, rd_rgb(157, 174, 192));
}

static RDUI_UNUSED void rdui_theme_card(rd_window_t *w, rdui_rect_t r,
                                        const char *label, rd_u32 color,
                                        int active) {
    rdui_round_alpha(w, r.x, r.y, r.w, r.h, 13,
                     active ? rd_rgb(55, 122, 255) : rd_rgb(255, 255, 255),
                     active ? 34u : 7u);
    rdui_round_outline(w, r.x, r.y, r.w, r.h, 13,
                       active ? rd_rgb(78, 144, 255) : rd_rgb(255, 255, 255),
                       active ? 160u : 18u);
    rdui_round_gradient_v(w, r.x + 18, r.y + 18, r.w - 36, 34, 10,
                          rdui_mix(color, rd_rgb(255, 255, 255), 28u),
                          rdui_mix(color, rd_rgb(0, 0, 0), 26u), 224);
    rdui_text_center(w, r.x, r.y + r.h - 27, r.w, 18, label,
                     rd_rgb(214, 229, 242), 1);
    if (active) {
        rdui_round_alpha(w, r.x + r.w - 20, r.y + 8, 14, 14, 7,
                         rd_rgb(55, 126, 255), 245);
        rdui_text_center(w, r.x + r.w - 20, r.y + 8, 14, 14, "v",
                         rd_rgb(255, 255, 255), 1);
    }
}

static RDUI_UNUSED void rdui_calc_key(rd_window_t *w, rdui_rect_t r,
                                      const char *label, rd_u32 top,
                                      rd_u32 bottom, int strong) {
    int radius = strong ? 12 : 4;
    rdui_round_gradient_v(w, r.x + 1, r.y + 1, r.w - 2, r.h - 2,
                          radius, top, bottom, 190);
    rdui_round_outline(w, r.x + 1, r.y + 1, r.w - 2, r.h - 2,
                       radius, rd_rgb(255, 255, 255), 11);
    rdui_text_center(w, r.x, r.y, r.w, r.h, label, rd_rgb(240, 246, 255), 3);
}

static RDUI_UNUSED void rdui_draw_node(rd_window_t *w, const rdui_node_t *n) {
    if (!n) return;
    if (n->kind == RDUI_NODE_PANEL) {
        rdui_glass(w, n->rect.x, n->rect.y, n->rect.w, n->rect.h,
                   n->bg, n->alpha ? n->alpha : 128u, n->radius ? n->radius : 14);
    } else if (n->kind == RDUI_NODE_BAR) {
        rdui_round_alpha(w, n->rect.x, n->rect.y, n->rect.w, n->rect.h,
                         n->radius, n->bg, n->alpha ? n->alpha : 255u);
    } else if (n->kind == RDUI_NODE_BUTTON) {
        rdui_button(w, n->rect.x, n->rect.y, n->rect.w, n->rect.h,
                    n->text, n->accent, n->flags & RDUI_F_CENTER_TEXT);
    } else if (n->kind == RDUI_NODE_LABEL) {
        int scale = n->scale ? n->scale : 1;
        if (n->flags & RDUI_F_CENTER_TEXT) {
            rdui_text_center(w, n->rect.x, n->rect.y, n->rect.w, n->rect.h,
                             n->text, n->fg, scale);
        } else if (n->flags & RDUI_F_RIGHT_TEXT) {
            rdui_text_right(w, n->rect.x + n->rect.w, n->rect.y,
                            n->text, n->fg);
        } else {
            rd_text_scaled(w, n->rect.x, n->rect.y, scale, n->text, n->fg);
        }
    }
}

static RDUI_UNUSED void rdui_draw_tree(rd_window_t *w, const rdui_node_t *nodes, int count) {
    int i;
    for (i = 0; i < count; ++i) rdui_draw_node(w, &nodes[i]);
}

#undef RDUI_UNUSED

#endif

