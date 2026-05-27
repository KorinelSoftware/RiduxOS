#include "user_libridux.h"
#include "ridux_ui.h"
#include "assets.h"

#define DESK_W 1024u
#define DESK_H 768u
#define TASKBAR_H 56
#define TOPBAR_H 30
#define LAUNCHER_W 650
#define LAUNCHER_H 572
#define LAUNCHER_BASE_Y 68
#define LAUNCHER_SIDE_W 0
#define LAUNCHER_COLS 6
#define LAUNCHER_GAP 8
#define LAUNCHER_TILE_H 84

static rd_u32 argb_to_xrgb(rd_u32 c) { return c & 0x00FFFFFFu; }

static rd_u32 blend_xrgb(rd_u32 dst, rd_u32 src, rd_u32 alpha) {
    rd_u32 inv, sr, sg, sb, dr, dg, db;
    if (alpha == 0u) return dst;
    if (alpha >= 255u) return src & 0x00FFFFFFu;
    inv = 255u - alpha;
    sr = (src >> 16) & 255u; sg = (src >> 8) & 255u; sb = src & 255u;
    dr = (dst >> 16) & 255u; dg = (dst >> 8) & 255u; db = dst & 255u;
    return (((sr * alpha + dr * inv + 127u) / 255u) << 16) |
           (((sg * alpha + dg * inv + 127u) / 255u) << 8) |
            ((sb * alpha + db * inv + 127u) / 255u);
}

static rd_u32 mix_chan(rd_u32 a, rd_u32 b, rd_u32 t) {
    return (a * (255u - t) + b * t) / 255u;
}

static rd_u32 mix_rgb(rd_u32 a, rd_u32 b, rd_u32 t) {
    rd_u32 ar = (a >> 16) & 255u, ag = (a >> 8) & 255u, ab = a & 255u;
    rd_u32 br = (b >> 16) & 255u, bg = (b >> 8) & 255u, bb = b & 255u;
    return (mix_chan(ar, br, t) << 16) |
           (mix_chan(ag, bg, t) << 8) |
            mix_chan(ab, bb, t);
}

static void pixel_alpha(rd_window_t *w, int x, int y, rd_u32 color, rd_u32 alpha) {
    rd_u32 *p;
    if (!w || !w->fb || alpha == 0u) return;
    if (x < 0 || y < 0 || (rd_u32)x >= w->width || (rd_u32)y >= w->height) return;
    p = w->fb + (rd_size)y * (w->stride / 4u) + (rd_size)x;
    *p = blend_xrgb(*p, color, alpha);
}

static void fill_rect_alpha(rd_window_t *w, int x, int y, int ww, int hh, rd_u32 color, rd_u32 alpha) {
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
        for (col = 0; col < ww; ++col) line[col] = blend_xrgb(line[col], color, alpha);
    }
}

static void stroke_rect_alpha(rd_window_t *w, int x, int y, int ww, int hh, rd_u32 color, rd_u32 alpha) {
    fill_rect_alpha(w, x, y, ww, 1, color, alpha);
    fill_rect_alpha(w, x, y + hh - 1, ww, 1, color, alpha);
    fill_rect_alpha(w, x, y, 1, hh, color, alpha);
    fill_rect_alpha(w, x + ww - 1, y, 1, hh, color, alpha);
}

static void glass_rect(rd_window_t *w, int x, int y, int ww, int hh, rd_u32 tint, rd_u32 alpha) {
    int i;
    fill_rect_alpha(w, x + 2, y + 4, ww, hh, rd_rgb(0, 0, 0), 34);
    fill_rect_alpha(w, x, y, ww, hh, tint, alpha);
    fill_rect_alpha(w, x, y, ww, 1, rd_rgb(255, 255, 255), 72);
    fill_rect_alpha(w, x, y + 1, ww, 1, rd_rgb(255, 255, 255), 32);
    stroke_rect_alpha(w, x, y, ww, hh, rd_rgb(255, 255, 255), 42);
    for (i = 0; i < hh / 2; ++i) {
        rd_u32 a = (rd_u32)(18 - (i * 18) / (hh / 2 + 1));
        if (a) fill_rect_alpha(w, x + 1, y + 1 + i, ww - 2, 1, rd_rgb(255, 255, 255), a);
    }
}

static void append_ch(char *out, int *len, int cap, char ch) {
    if (*len + 1 >= cap) return;
    out[(*len)++] = ch;
    out[*len] = 0;
}

static void append_2(char *out, int *len, int cap, rd_u32 v) {
    append_ch(out, len, cap, (char)('0' + ((v / 10u) % 10u)));
    append_ch(out, len, cap, (char)('0' + (v % 10u)));
}

static void append_4(char *out, int *len, int cap, rd_u32 v) {
    append_ch(out, len, cap, (char)('0' + ((v / 1000u) % 10u)));
    append_ch(out, len, cap, (char)('0' + ((v / 100u) % 10u)));
    append_ch(out, len, cap, (char)('0' + ((v / 10u) % 10u)));
    append_ch(out, len, cap, (char)('0' + (v % 10u)));
}

static void draw_text_right(rd_window_t *w, int right, int y, const char *s, rd_u32 color) {
    rd_text(w, right - rd_text_width(s), y, s, color);
}

static int icon_content_bounds(const ridux_image_t *img,
                               int *min_x, int *min_y, int *max_x, int *max_y) {
    int x, y;
    int found = 0;
    int l = 0, t = 0, r = 0, b = 0;
    if (!img || !img->pixels || img->width == 0 || img->height == 0) return 0;
    for (y = 0; y < (int)img->height; ++y) {
        for (x = 0; x < (int)img->width; ++x) {
            rd_u32 c = img->pixels[(rd_size)y * img->width + (rd_size)x];
            rd_u32 a = (c >> 24) & 255u;
            if (a > 8u) {
                if (!found) {
                    l = r = x;
                    t = b = y;
                    found = 1;
                } else {
                    if (x < l) l = x;
                    if (x > r) r = x;
                    if (y < t) t = y;
                    if (y > b) b = y;
                }
            }
        }
    }
    if (!found) return 0;
    if (l > 0) --l;
    if (t > 0) --t;
    if (r + 1 < (int)img->width) ++r;
    if (b + 1 < (int)img->height) ++b;
    *min_x = l;
    *min_y = t;
    *max_x = r;
    *max_y = b;
    return 1;
}

static rd_u32 sample_icon_bilinear(const ridux_image_t *img, rd_u32 sx_fp, rd_u32 sy_fp) {
    rd_u32 x0, y0, x1, y1, wx, wy;
    rd_u32 c00, c10, c01, c11;
    rd_u32 w00, w10, w01, w11;
    rd_u32 a, r, g, b;
    x0 = sx_fp >> 8;
    y0 = sy_fp >> 8;
    wx = sx_fp & 255u;
    wy = sy_fp & 255u;
    if (x0 >= img->width) x0 = img->width - 1u;
    if (y0 >= img->height) y0 = img->height - 1u;
    x1 = x0 + 1u < img->width ? x0 + 1u : x0;
    y1 = y0 + 1u < img->height ? y0 + 1u : y0;
    c00 = img->pixels[(rd_size)y0 * img->width + x0];
    c10 = img->pixels[(rd_size)y0 * img->width + x1];
    c01 = img->pixels[(rd_size)y1 * img->width + x0];
    c11 = img->pixels[(rd_size)y1 * img->width + x1];
    w00 = (256u - wx) * (256u - wy);
    w10 = wx * (256u - wy);
    w01 = (256u - wx) * wy;
    w11 = wx * wy;
    a = (((c00 >> 24) & 255u) * w00 + ((c10 >> 24) & 255u) * w10 +
         ((c01 >> 24) & 255u) * w01 + ((c11 >> 24) & 255u) * w11) >> 16;
    r = (((c00 >> 16) & 255u) * w00 + ((c10 >> 16) & 255u) * w10 +
         ((c01 >> 16) & 255u) * w01 + ((c11 >> 16) & 255u) * w11) >> 16;
    g = (((c00 >> 8) & 255u) * w00 + ((c10 >> 8) & 255u) * w10 +
         ((c01 >> 8) & 255u) * w01 + ((c11 >> 8) & 255u) * w11) >> 16;
    b = ((c00 & 255u) * w00 + (c10 & 255u) * w10 +
         (c01 & 255u) * w01 + (c11 & 255u) * w11) >> 16;
    return (a << 24) | (r << 16) | (g << 8) | b;
}

static void draw_icon_scaled(rd_window_t *w, int icon_id, int x, int y, int sz) {
    const ridux_image_t *img;
    int ox, oy;
    int min_x, min_y, max_x, max_y;
    int src_w, src_h, draw_w, draw_h, dx, dy;
    if (icon_id < 0 || icon_id >= RIDUX_ICON_COUNT || sz <= 0) return;
    img = &RIDUX_ICONS[icon_id];
    if (!icon_content_bounds(img, &min_x, &min_y, &max_x, &max_y)) return;
    src_w = max_x - min_x + 1;
    src_h = max_y - min_y + 1;
    draw_w = sz;
    draw_h = sz;
    if (src_w > src_h) {
        draw_h = (sz * src_h + src_w / 2) / src_w;
    } else if (src_h > src_w) {
        draw_w = (sz * src_w + src_h / 2) / src_h;
    }
    if (draw_w < 1) draw_w = 1;
    if (draw_h < 1) draw_h = 1;
    dx = x + (sz - draw_w) / 2;
    dy = y + (sz - draw_h) / 2;
    for (oy = 0; oy < draw_h; ++oy) {
        rd_u32 sy_fp = ((rd_u32)min_y << 8);
        if (draw_h > 1) {
            sy_fp += (rd_u32)(((rd_u64)oy * (rd_u32)(src_h - 1) * 256u) /
                              (rd_u32)(draw_h - 1));
        }
        for (ox = 0; ox < draw_w; ++ox) {
            rd_u32 sx_fp = ((rd_u32)min_x << 8);
            rd_u32 c;
            if (draw_w > 1) {
                sx_fp += (rd_u32)(((rd_u64)ox * (rd_u32)(src_w - 1) * 256u) /
                                  (rd_u32)(draw_w - 1));
            }
            c = sample_icon_bilinear(img, sx_fp, sy_fp);
            rd_u32 a = (c >> 24) & 255u;
            if (a) pixel_alpha(w, dx + ox, dy + oy, argb_to_xrgb(c), a);
        }
    }
}

static void draw_app_grid_glyph(rd_window_t *w, int x, int y, int sz, rd_u32 color, rd_u32 alpha) {
    int row, col;
    int dot = sz / 6;
    int step;
    if (dot < 3) dot = 3;
    step = (sz - dot) / 2;
    for (row = 0; row < 3; ++row) {
        for (col = 0; col < 3; ++col) {
            int dx = x + col * step;
            int dy = y + row * step;
            rdui_round_alpha(w, dx, dy, dot, dot, dot / 2, color, alpha);
        }
    }
}

static void draw_wallpaper_image(rd_window_t *w, const ridux_image_t *img) {
    rd_u32 pitch;
    rd_u32 view_x = 0, view_y = 0, view_w, view_h;
    rd_u32 x, y;

    if (!w || !w->fb || !img || !img->pixels || !img->width || !img->height) return;
    pitch = w->stride / 4u;
    view_w = img->width;
    view_h = img->height;

    /* Cover como un escritorio normal: llena todo sin deformar la imagen. */
    if ((rd_u64)w->width * img->height > (rd_u64)w->height * img->width) {
        view_h = (rd_u32)(((rd_u64)w->height * img->width) / w->width);
        if (view_h < 1u) view_h = 1u;
        if (view_h > img->height) view_h = img->height;
        view_y = ((rd_u32)img->height - view_h) / 2u;
    } else {
        view_w = (rd_u32)(((rd_u64)w->width * img->height) / w->height);
        if (view_w < 1u) view_w = 1u;
        if (view_w > img->width) view_w = img->width;
        view_x = ((rd_u32)img->width - view_w) / 2u;
    }

    for (y = 0; y < w->height; ++y) {
        rd_u32 sy = view_y + (rd_u32)(((rd_u64)y * view_h) / w->height);
        if (sy >= img->height) sy = img->height - 1u;
        for (x = 0; x < w->width; ++x) {
            rd_u32 sx = view_x + (rd_u32)(((rd_u64)x * view_w) / w->width);
            rd_u32 c;
            if (sx >= img->width) sx = img->width - 1u;
            c = argb_to_xrgb(img->pixels[(rd_size)sy * img->width + sx]);
            /* Sombra suave para que la barra de abajo no "flote" sobre el fondo. */
            {
                rd_u32 shade = (y > w->height * 82u / 100u)
                             ? (rd_u32)(((y - w->height * 82u / 100u) * 18u) /
                                        (w->height - w->height * 82u / 100u + 1u))
                             : 0u;
                if (shade) c = mix_rgb(c, rd_rgb(4, 10, 18), shade);
            }
            w->fb[(rd_size)y * pitch + x] = c;
        }
    }
}

static void draw_wallpaper_bloom(rd_window_t *w) {
    rd_u32 pitch;
    rd_u32 x, y;
    if (!w || !w->fb || !w->width || !w->height) return;
    pitch = w->stride / 4u;
    for (y = 0; y < w->height; ++y) {
        for (x = 0; x < w->width; ++x) {
            rd_u32 hx = (x * 255u) / (w->width ? w->width : 1u);
            rd_u32 vy = (y * 255u) / (w->height ? w->height : 1u);
            int cx = (int)x - (int)w->width * 72 / 100;
            int cy = (int)y - (int)w->height * 38 / 100;
            int glow = 255 - ((cx * cx) / 1200 + (cy * cy) / 700);
            int fold_a = (int)w->height * 36 / 100 + (((int)x - (int)w->width / 2) * ((int)x - (int)w->width / 2)) / 1900;
            int fold_b = (int)w->height * 62 / 100 - ((int)x * 21) / 100;
            rd_u32 c = mix_rgb(rd_rgb(2, 9, 28), rd_rgb(5, 42, 96), hx / 2u + vy / 5u);
            c = mix_rgb(c, rd_rgb(0, 113, 180), hx / 5u);
            if (glow > 0) c = mix_rgb(c, rd_rgb(0, 210, 220), (rd_u32)(glow > 170 ? 170 : glow));
            if ((int)y > fold_a) c = mix_rgb(c, rd_rgb(1, 87, 105), 72u);
            if ((int)y > fold_b) c = mix_rgb(c, rd_rgb(2, 20, 55), 118u);
            if (y > w->height * 76u / 100u) c = mix_rgb(c, rd_rgb(1, 5, 16), 120u);
            w->fb[(rd_size)y * pitch + x] = c;
        }
    }

    rdui_fill_alpha(w, 0, 0, (int)w->width, (int)w->height,
                    rd_rgb(0, 0, 0), 10);
}

static void draw_wallpaper(rd_window_t *w) {
    if (RIDUX_WALLPAPER_COUNT > 0u) {
        draw_wallpaper_image(w, &RIDUX_WALLPAPERS[0]);
        return;
    }
    draw_wallpaper_bloom(w);
}

static void time_strings(const rd_desktop_state_t *st, char *time_out, char *date_out) {
    int n = 0;
    time_out[0] = 0;
    append_2(time_out, &n, 8, st->hour);
    append_ch(time_out, &n, 8, ':');
    append_2(time_out, &n, 8, st->minute);
    n = 0;
    date_out[0] = 0;
    append_2(date_out, &n, 12, st->day ? st->day : 1u);
    append_ch(date_out, &n, 12, '/');
    append_2(date_out, &n, 12, st->month ? st->month : 1u);
    append_ch(date_out, &n, 12, '/');
    append_4(date_out, &n, 12, st->year ? st->year : 2026u);
}

static __attribute__((unused)) void draw_quick_panel(rd_window_t *w, const rd_desktop_state_t *st) {
    int pw = 340;
    int ph = 258;
    int px = (int)w->width - pw - 16;
    int py = (int)w->height - TASKBAR_H - ph - 14;
    int i;
    static const char *labels[4] = { "Wi-Fi", "Bluetooth", "Night light", "Battery" };
    (void)st;
    glass_rect(w, px, py, pw, ph, rd_rgb(18, 29, 47), 178);
    rd_text(w, px + 18, py + 18, "Quick settings", rd_rgb(240, 248, 255));
    for (i = 0; i < 4; ++i) {
        int cx = px + 18 + (i % 2) * 154;
        int cy = py + 52 + (i / 2) * 62;
        fill_rect_alpha(w, cx, cy, 138, 46, i == 0 ? rd_rgb(42, 154, 255) : rd_rgb(255, 255, 255), i == 0 ? 96u : 34u);
        stroke_rect_alpha(w, cx, cy, 138, 46, rd_rgb(255, 255, 255), 32);
        rd_text(w, cx + 12, cy + 15, labels[i], rd_rgb(237, 246, 255));
    }
    rd_text(w, px + 20, py + 186, "Volume", rd_rgb(228, 238, 248));
    fill_rect_alpha(w, px + 92, py + 193, pw - 122, 6, rd_rgb(255, 255, 255), 42);
    fill_rect_alpha(w, px + 92, py + 193, (pw - 122) * 64 / 100, 6, rd_rgb(80, 180, 255), 230);
    rd_text(w, px + 20, py + 218, "Brightness", rd_rgb(228, 238, 248));
    fill_rect_alpha(w, px + 116, py + 225, pw - 146, 6, rd_rgb(255, 255, 255), 42);
    fill_rect_alpha(w, px + 116, py + 225, (pw - 146) * 82 / 100, 6, rd_rgb(120, 210, 255), 230);
}

static __attribute__((unused)) void draw_start_menu(rd_window_t *w, const rd_desktop_state_t *st) {
    int sw = 560;
    int sh = 438;
    int sx = 16;
    int sy = (int)w->height - TASKBAR_H - sh - 14;
    rd_u32 i, shown = 0;
    glass_rect(w, sx, sy, sw, sh, rd_rgb(16, 27, 44), 184);
    rd_text_scaled(w, sx + 24, sy + 20, 2, "Ridux", rd_rgb(245, 250, 255));
    rd_text(w, sx + 24, sy + 62, "Pinned apps running from Ring 3", rd_rgb(176, 198, 220));
    for (i = 0; i < st->app_count && shown < 18u; ++i) {
        int col = (int)(shown % 6u);
        int row = (int)(shown / 6u);
        int tx = sx + 24 + col * 86;
        int ty = sy + 100 + row * 96;
        fill_rect_alpha(w, tx, ty, 74, 78, rd_rgb(255, 255, 255), st->apps[i].running ? 42u : 24u);
        stroke_rect_alpha(w, tx, ty, 74, 78, rd_rgb(255, 255, 255), st->apps[i].focused ? 110u : 28u);
        draw_icon_scaled(w, (int)st->apps[i].icon, tx + 21, ty + 10, 32);
        rd_text(w, tx + 7, ty + 52, st->apps[i].name, rd_rgb(232, 242, 250));
        ++shown;
    }
}

static __attribute__((unused)) void draw_taskbar(rd_window_t *w, const rd_desktop_state_t *st) {
    int y = (int)w->height - TASKBAR_H;
    int app_x = 72;
    int i;
    char time_s[8];
    char date_s[12];
    glass_rect(w, 0, y, (int)w->width, TASKBAR_H, rd_rgb(10, 18, 30), 152);
    fill_rect_alpha(w, 0, y, (int)w->width, 1, rd_rgb(255, 255, 255), 50);

    fill_rect_alpha(w, 12, y + 8, 44, 40, st->start_open ? rd_rgb(70, 150, 230) : rd_rgb(255, 255, 255), st->start_open ? 86u : 30u);
    stroke_rect_alpha(w, 12, y + 8, 44, 40, rd_rgb(255, 255, 255), 42);
    draw_icon_scaled(w, RIDUX_ICON_START, 20, y + 12, 32);

    for (i = 0; i < (int)st->app_count; ++i) {
        int x = app_x + i * 48;
        if (x + 44 > (int)w->width - 250) break;
        if (st->apps[i].running || st->apps[i].focused) {
            fill_rect_alpha(w, x, y + 7, 44, 42,
                            st->apps[i].focused ? rd_rgb(55, 144, 240) : rd_rgb(255, 255, 255),
                            st->apps[i].focused ? 76u : 34u);
            stroke_rect_alpha(w, x, y + 7, 44, 42, rd_rgb(255, 255, 255), st->apps[i].focused ? 92u : 32u);
        }
        draw_icon_scaled(w, (int)st->apps[i].icon, x + 6, y + 11, 32);
        if (st->apps[i].running) fill_rect_alpha(w, x + 14, y + 49, 16, 3, rd_rgb(109, 211, 255), 230);
    }

    time_strings(st, time_s, date_s);
    draw_icon_scaled(w, RIDUX_ICON_TRAY_NETWORK, (int)w->width - 222, y + 18, 20);
    draw_icon_scaled(w, RIDUX_ICON_TRAY_VOLUME,  (int)w->width - 194, y + 18, 20);
    draw_icon_scaled(w, RIDUX_ICON_TRAY_BATTERY, (int)w->width - 166, y + 18, 20);
    fill_rect_alpha(w, (int)w->width - 134, y + 7, 118, 42,
                    st->quick_open ? rd_rgb(70, 150, 230) : rd_rgb(255, 255, 255),
                    st->quick_open ? 78u : 22u);
    stroke_rect_alpha(w, (int)w->width - 134, y + 7, 118, 42, rd_rgb(255, 255, 255), 28);
    draw_text_right(w, (int)w->width - 24, y + 12, time_s, rd_rgb(238, 246, 255));
    draw_text_right(w, (int)w->width - 24, y + 30, date_s, rd_rgb(178, 198, 216));
}

typedef struct shell_app {
    const char *label;
    const char *command;
    int icon;
} shell_app_t;

static const shell_app_t SHELL_DOCK_APPS[] = {
    { "Ridux",    "about",      RIDUX_ICON_START },
    { "Archivos", "Files",      RIDUX_ICON_FILES },
    { "Terminal", "Terminal",   RIDUX_ICON_TERMINAL },
    { "Web",      "Firefox",    RIDUX_ICON_FIREFOX },
    { "Musica",   "Media",      RIDUX_ICON_MEDIA },
    { "Reloj",    "Clock",      RIDUX_ICON_CLOCK },
    { "Ajustes",  "Settings",   RIDUX_ICON_SETTINGS },
    { "Apps",     "",          -1 }
};

static const shell_app_t SHELL_SIDE_APPS[] = {
    { "Home",     "Files",      RIDUX_ICON_FILES },
    { "Terminal", "Terminal",   RIDUX_ICON_TERMINAL },
    { "Browser",  "Firefox",    RIDUX_ICON_FIREFOX },
    { "Settings", "Settings",   RIDUX_ICON_SETTINGS },
    { "Store",    "store",      RIDUX_ICON_STORE },
    { "Music",    "Media",      RIDUX_ICON_MEDIA },
    { "Notes",    "Notes",      RIDUX_ICON_NOTES },
    { "Trash",    "",          RIDUX_ICON_LOGVIEWER }
};

static const shell_app_t SHELL_GRID_APPS[] = {
    { "Archivos",   "Files",      RIDUX_ICON_FILES },
    { "Terminal",   "Terminal",   RIDUX_ICON_TERMINAL },
    { "Ajustes",    "Settings",   RIDUX_ICON_SETTINGS },
    { "Calculadora","Calculator", RIDUX_ICON_CALC },
    { "Firefox",    "Firefox",    RIDUX_ICON_FIREFOX },
    { "Acerca",     "About",      RIDUX_ICON_ABOUT },
    { "Tienda",     "store",      RIDUX_ICON_STORE },
    { "Media",      "Media",      RIDUX_ICON_MEDIA },
    { "Monitor",    "Monitor",    RIDUX_ICON_MONITOR },
    { "Pintar",     "Paint",      RIDUX_ICON_PAINT },
    { "Clima",      "Weather",    RIDUX_ICON_WEATHER },
    { "Notas",      "Notes",      RIDUX_ICON_NOTES },
    { "Reloj",      "Clock",      RIDUX_ICON_CLOCK },
    { "Procesos",   "Processes",  RIDUX_ICON_PROCESSES },
    { "Registro",   "log",        RIDUX_ICON_LOGVIEWER },
    { "Red",        "Network",    RIDUX_ICON_NETWORK },
    { "Sistema",    "sysinfo",    RIDUX_ICON_SYSINFO },
    { "Juegos",     "ttt",        RIDUX_ICON_TICTACTOE }
};

static int hit_box(int mx, int my, int x, int y, int ww, int hh) {
    return mx >= x && my >= y && mx < x + ww && my < y + hh;
}

static int app_running_by_name(const rd_desktop_state_t *st, const char *name) {
    rd_u32 i;
    if (!st || !name || !name[0]) return 0;
    for (i = 0; i < st->app_count; ++i) {
        int a = 0;
        while (name[a] && st->apps[i].name[a] && name[a] == st->apps[i].name[a]) ++a;
        if (!name[a] && !st->apps[i].name[a]) return st->apps[i].running ? 1 : 0;
    }
    return 0;
}

static int launcher_anim_y(int anim) {
    if (anim < 1) anim = 1;
    if (anim > 10) anim = 10;
    return LAUNCHER_BASE_Y + (10 - anim) * 4;
}

static rdui_rect_t launcher_panel_rect(int win_w, int anim) {
    return rdui_rect((win_w - LAUNCHER_W) / 2, launcher_anim_y(anim),
                     LAUNCHER_W, LAUNCHER_H);
}

static rdui_rect_t launcher_grid_area(int win_w, int anim) {
    rdui_rect_t p = launcher_panel_rect(win_w, anim);
    return rdui_rect(p.x + 42, p.y + 150, LAUNCHER_W - 84, 268);
}

static rdui_rect_t launcher_tile_rect(int win_w, int anim, int index) {
    return rdui_grid_rect(launcher_grid_area(win_w, anim), LAUNCHER_COLS,
                          LAUNCHER_GAP, index, LAUNCHER_TILE_H);
}

static void shell_copy(char *out, int *len, int cap, const char *s) {
    int i = 0;
    if (!out || !len || !s) return;
    while (s[i] && *len + 1 < cap) out[(*len)++] = s[i++];
    out[*len] = 0;
}

static void shell_set_text(char *out, int cap, const char *s) {
    int len = 0;
    if (!out || cap <= 0) return;
    out[0] = 0;
    shell_copy(out, &len, cap, s ? s : "");
}

static void shell_open_app(const char *app) {
    char cmd[48];
    char out[160];
    int len = 0;
    if (!app || !app[0]) return;
    cmd[0] = 0;
    shell_copy(cmd, &len, sizeof(cmd), "open ");
    shell_copy(cmd, &len, sizeof(cmd), app);
    out[0] = 0;
    (void)rd_shell_exec(cmd, out, (rd_u32)sizeof(out));
}

static void draw_ridux_topbar(rd_window_t *w, const rd_desktop_state_t *st) {
    char time_s[8];
    char date_s[12];
    int gpu_x = 414;
    const char *gpu_label = (st && st->gpu_real) ? "GPU" : "Mesa";
    rd_u32 gpu_tint = (st && st->gpu_real) ? rd_rgb(35, 190, 124) : rd_rgb(245, 173, 61);
    time_strings(st, time_s, date_s);
    rdui_gradient_v(w, 0, 0, (int)w->width, TOPBAR_H,
                    rd_rgb(8, 13, 22), rd_rgb(5, 8, 14));
    rdui_fill_alpha(w, 0, 0, (int)w->width, TOPBAR_H, rd_rgb(255, 255, 255), 7);
    rdui_fill_alpha(w, 0, TOPBAR_H - 1, (int)w->width, 1, rd_rgb(255, 255, 255), 14);
    draw_icon_scaled(w, RIDUX_ICON_START, 8, 3, 24);
    rd_text(w, 38, 9, "RiduxOS", rd_rgb(244, 249, 255));
    rd_text(w, 112, 9, "Archivo", rd_rgb(222, 231, 241));
    rd_text(w, 176, 9, "Editar", rd_rgb(222, 231, 241));
    rd_text(w, 232, 9, "Ver", rd_rgb(222, 231, 241));
    rd_text(w, 274, 9, "Ventanas", rd_rgb(222, 231, 241));
    rd_text(w, 350, 9, "Ayuda", rd_rgb(222, 231, 241));
    rdui_text_center(w, (int)w->width / 2 - 80, 0, 160, TOPBAR_H, time_s, rd_rgb(242, 248, 255), 1);
    if ((int)w->width >= 940) {
        rdui_round_alpha(w, gpu_x, 5, 54, 20, 10, gpu_tint, 58);
        rdui_round_outline(w, gpu_x, 5, 54, 20, 10, gpu_tint, 72);
        rdui_text_center(w, gpu_x, 5, 54, 20, gpu_label,
                         st && st->gpu_real ? rd_rgb(216, 255, 235) : rd_rgb(255, 236, 204), 1);
    }
    rdui_text_right(w, (int)w->width - 214, 9, date_s, rd_rgb(202, 214, 226));
    draw_icon_scaled(w, RIDUX_ICON_TRAY_VOLUME, (int)w->width - 170, 6, 18);
    draw_icon_scaled(w, RIDUX_ICON_TRAY_NETWORK, (int)w->width - 136, 6, 18);
    draw_icon_scaled(w, RIDUX_ICON_TRAY_BATTERY, (int)w->width - 100, 6, 18);
    rd_text(w, (int)w->width - 70, 9, "87%", rd_rgb(232, 242, 251));
    rd_text(w, (int)w->width - 28, 9, "O", rd_rgb(232, 242, 251));
}

static __attribute__((unused)) void draw_ridux_side_dock(rd_window_t *w, const rd_desktop_state_t *st) {
    int x = 15;
    int y = 116;
    int i;
    int count = (int)(sizeof(SHELL_SIDE_APPS) / sizeof(SHELL_SIDE_APPS[0]));
    int h = 20 + count * 43 + (count - 1) * 10;
    rdui_panel_class(w, x, y, 50, h, "dock.glass");
    for (i = 0; i < count; ++i) {
        int iy = y + 11 + i * 53;
        int active = app_running_by_name(st, SHELL_SIDE_APPS[i].command);
        if (active) rdui_round_alpha(w, x + 41, iy + 14, 3, 16, 2, rd_rgb(80, 196, 255), 220);
        rdui_round_alpha(w, x + 7, iy - 1, 36, 36, 11,
                         active ? rd_rgb(22, 115, 245) : rd_rgb(255, 255, 255),
                         active ? 116u : 12u);
        draw_icon_scaled(w, SHELL_SIDE_APPS[i].icon, x + 11, iy + 3, 28);
    }
}

static void draw_ridux_bottom_dock(rd_window_t *w, const rd_desktop_state_t *st, int launcher_open) {
    int count = (int)(sizeof(SHELL_DOCK_APPS) / sizeof(SHELL_DOCK_APPS[0]));
    int slot = 50;
    int gap = 9;
    int dock_w = count * slot + (count - 1) * gap + 28;
    int x = ((int)w->width - dock_w) / 2;
    int y = (int)w->height - 74;
    int i;
    rdui_material(w, rdui_rect(x, y, dock_w, 60), rd_rgb(13, 19, 29), 218, 28, 4);
    for (i = 0; i < count; ++i) {
        int ix = x + 14 + i * (slot + gap);
        int active = launcher_open && i == count - 1;
        int hover = hit_box((int)st->mouse_x, (int)st->mouse_y, ix, y + 7, slot, 46);
        if (!active) active = app_running_by_name(st, SHELL_DOCK_APPS[i].command);
        rdui_round_alpha(w, ix, y + 7, slot, 46, 16,
                         active ? rd_rgb(83, 139, 255) : rd_rgb(255, 255, 255),
                         active ? 62u : hover ? 24u : 6u);
        if (SHELL_DOCK_APPS[i].icon >= 0) {
            draw_icon_scaled(w, SHELL_DOCK_APPS[i].icon, ix + (slot - 34) / 2, y + 13, 34);
        } else {
            draw_app_grid_glyph(w, ix + (slot - 30) / 2, y + 14, 30,
                                rd_rgb(238, 247, 255), active ? 245u : 210u);
        }
        if (active && i != count - 1) {
            rdui_round_alpha(w, ix + slot / 2 - 8, y + 53, 16, 3, 2, rd_rgb(98, 174, 255), 220);
        }
    }
}

static void draw_ridux_launcher(rd_window_t *w, const rd_desktop_state_t *st, int anim) {
    int i;
    int count = (int)(sizeof(SHELL_GRID_APPS) / sizeof(SHELL_GRID_APPS[0]));
    rdui_rect_t p;
    int sx, sy;
    int main_x, main_y;
    int footer_y;
    rd_u32 ink = rd_rgb(24, 32, 44);
    rd_u32 sub = rd_rgb(85, 101, 121);
    rd_u32 card = rd_rgb(255, 255, 255);
    if (anim < 1) anim = 1;
    if (anim > 10) anim = 10;
    p = launcher_panel_rect((int)w->width, anim);
    sx = p.x;
    sy = p.y;
    main_x = sx + 42;
    main_y = sy + 32;
    footer_y = sy + LAUNCHER_H - 58;

    rdui_fill_alpha(w, 0, 0, (int)w->width, (int)w->height,
                    rd_rgb(3, 7, 14), (rd_u32)(anim * 3));
    rdui_shadow(w, sx, sy, LAUNCHER_W, LAUNCHER_H, 30, 58);
    rdui_round_gradient_v(w, sx, sy, LAUNCHER_W, LAUNCHER_H, 30,
                          rd_rgb(239, 248, 255), rd_rgb(212, 230, 242), 252);
    rdui_round_outline(w, sx, sy, LAUNCHER_W, LAUNCHER_H, 30,
                       rd_rgb(255, 255, 255), 104);
    rdui_round_outline(w, sx + 1, sy + 1, LAUNCHER_W - 2, LAUNCHER_H - 2, 29,
                       rd_rgb(30, 61, 88), 18);

    rdui_round_alpha(w, main_x, main_y, LAUNCHER_W - 84, 42, 21, card, 150);
    rdui_round_outline(w, main_x, main_y, LAUNCHER_W - 84, 42, 21,
                       rd_rgb(38, 73, 102), 26);
    rdui_round_alpha(w, main_x + 18, main_y + 17, 9, 9, 5, rd_rgb(78, 96, 118), 200);
    rdui_round_alpha(w, main_x + 26, main_y + 25, 8, 2, 1, rd_rgb(78, 96, 118), 200);
    rd_text(w, main_x + 44, main_y + 13, "Buscar", sub);

    rd_text(w, main_x, sy + 102, "Fijadas", ink);
    rdui_round_alpha(w, sx + LAUNCHER_W - 126, sy + 94, 84, 30, 15,
                     card, 94);
    rdui_round_outline(w, sx + LAUNCHER_W - 126, sy + 94, 84, 30, 15,
                       rd_rgb(40, 72, 100), 20);
    rdui_text_center(w, sx + LAUNCHER_W - 126, sy + 94, 84, 30,
                     "Todas", sub, 1);

    for (i = 0; i < count; ++i) {
        rdui_rect_t tile = launcher_tile_rect((int)w->width, anim, i);
        int hover = st && hit_box((int)st->mouse_x, (int)st->mouse_y,
                                  tile.x, tile.y, tile.w, tile.h);
        int running = app_running_by_name(st, SHELL_GRID_APPS[i].command);
        if (hover || running) {
            rdui_round_alpha(w, tile.x + 2, tile.y + 1, tile.w - 4,
                             tile.h - 2, 18, card,
                             hover ? 116u : 52u);
            rdui_round_outline(w, tile.x + 2, tile.y + 1, tile.w - 4,
                               tile.h - 2, 18, rd_rgb(44, 94, 134),
                               hover ? 28u : 12u);
        }
        if (running) {
            rdui_round_alpha(w, tile.x + tile.w / 2 - 10, tile.y + tile.h - 7,
                             20, 3, 2, rd_rgb(25, 122, 255), 230);
        }
        draw_icon_scaled(w, SHELL_GRID_APPS[i].icon,
                         tile.x + (tile.w - 46) / 2, tile.y + 10, 46);
        rdui_text_center(w, tile.x - 6, tile.y + 64, tile.w + 12, 18,
                         SHELL_GRID_APPS[i].label,
                         hover ? ink : rd_rgb(38, 51, 66), 1);
    }

    rd_text(w, main_x, sy + 440, "Recomendado", ink);
    rdui_round_alpha(w, main_x, sy + 470, 176, 38, 14, card, 118);
    rdui_round_alpha(w, main_x + 194, sy + 470, 176, 38, 14, card, 118);
    rdui_round_alpha(w, main_x + 388, sy + 470, 176, 38, 14, card, 118);
    rdui_round_outline(w, main_x, sy + 470, 176, 38, 14, rd_rgb(45, 80, 108), 14);
    rdui_round_outline(w, main_x + 194, sy + 470, 176, 38, 14, rd_rgb(45, 80, 108), 14);
    rdui_round_outline(w, main_x + 388, sy + 470, 176, 38, 14, rd_rgb(45, 80, 108), 14);
    draw_icon_scaled(w, RIDUX_ICON_FILES, main_x + 12, sy + 477, 24);
    draw_icon_scaled(w, RIDUX_ICON_SETTINGS, main_x + 206, sy + 477, 24);
    draw_icon_scaled(w, RIDUX_ICON_TERMINAL, main_x + 400, sy + 477, 24);
    rd_text(w, main_x + 46, sy + 478, "Archivos", ink);
    rd_text(w, main_x + 240, sy + 478, "Ajustes", ink);
    rd_text(w, main_x + 434, sy + 478, "Terminal", ink);

    rdui_fill_alpha(w, sx + 2, footer_y - 12, LAUNCHER_W - 4, 1,
                    rd_rgb(40, 72, 100), 20);
    draw_icon_scaled(w, RIDUX_ICON_ABOUT, main_x, footer_y + 2, 30);
    rd_text(w, main_x + 42, footer_y + 1, "ridux", ink);
    rd_text(w, main_x + 42, footer_y + 19, "usuario local", sub);
    rdui_round_alpha(w, sx + LAUNCHER_W - 80, footer_y - 2, 38, 34, 13,
                     card, 118);
    rdui_round_outline(w, sx + LAUNCHER_W - 80, footer_y - 2, 38, 34, 13,
                       rd_rgb(45, 80, 108), 14);
    rdui_text_center(w, sx + LAUNCHER_W - 80, footer_y - 2, 38, 34,
                     "Off", sub, 1);
}

static void draw_ridux_quick_panel(rd_window_t *w, const rd_desktop_state_t *st, int anim) {
    int px = (int)w->width - 378;
    int py = TOPBAR_H + 20 - (10 - anim) * 4;
    int ph = 298;
    const char *gpu = (st && st->gpu_real) ? st->gpu_backend : "GPU present pending";
    if (anim < 1) anim = 1;
    if (anim > 10) anim = 10;
    rdui_material(w, rdui_rect(px, py, 354, ph), rd_rgb(17, 26, 39), 232, 28, 4);
    draw_icon_scaled(w, RIDUX_ICON_TRAY_SETTINGS, px + 20, py + 16, 28);
    rd_text(w, px + 58, py + 22, "Controles", rd_rgb(240, 248, 255));
    rd_text(w, px + 250, py + 20, "RiduxOS", rd_rgb(151, 174, 198));
    rdui_button_class(w, px + 22, py + 56, 142, 46, "Wi-Fi", "button.primary", 1);
    rdui_button_class(w, px + 188, py + 56, 142, 46, "Bluetooth", "button.ghost", 0);
    rdui_button_class(w, px + 22, py + 116, 142, 46, "Audio", "button.primary", 1);
    rdui_button_class(w, px + 188, py + 116, 142, 46, "Energia", "button.ghost", 0);
    rd_text(w, px + 24, py + 190, "Brillo", rd_rgb(218, 232, 244));
    rdui_slider(w, px + 92, py + 194, 220, 67, rd_rgb(62, 171, 255));
    rd_text(w, px + 24, py + 226, "Volumen", rd_rgb(218, 232, 244));
    rdui_slider(w, px + 92, py + 230, 220, 78, rd_rgb(39, 216, 185));
    rdui_round_alpha(w, px + 22, py + 246, 308, 18, 9,
                     st && st->gpu_real ? rd_rgb(38, 183, 122) : rd_rgb(255, 255, 255),
                     st && st->gpu_real ? 50u : 18u);
    rd_text(w, px + 34, py + 248, "Render", rd_rgb(190, 209, 226));
    rd_text(w, px + 92, py + 248, gpu[0] ? gpu : "GPU present pending",
            st && st->gpu_real ? rd_rgb(210, 255, 230) : rd_rgb(206, 219, 232));
    rd_text(w, px + 24, py + 270,
            st && st->mesa_ready && st->opengl_ready && st->vulkan_ready ?
            "Mesa GL/Vulkan listo" : "Mesa GL/Vulkan incompleto",
            st && st->mesa_ready && st->opengl_ready && st->vulkan_ready ?
            rd_rgb(188, 245, 215) : rd_rgb(245, 213, 168));
}

static __attribute__((unused)) int shell_hit_side_app(int mx, int my) {
    int x = 15;
    int y = 116;
    int i;
    int count = (int)(sizeof(SHELL_SIDE_APPS) / sizeof(SHELL_SIDE_APPS[0]));
    for (i = 0; i < count; ++i) {
        int iy = y + 11 + i * 53;
        if (hit_box(mx, my, x + 7, iy, 34, 34)) return i;
    }
    return -1;
}

static int shell_hit_bottom_app(rd_window_t *w, int mx, int my) {
    int count = (int)(sizeof(SHELL_DOCK_APPS) / sizeof(SHELL_DOCK_APPS[0]));
    int slot = 50;
    int gap = 9;
    int dock_w = count * slot + (count - 1) * gap + 28;
    int x = ((int)w->width - dock_w) / 2;
    int y = (int)w->height - 74;
    int i;
    for (i = 0; i < count; ++i) {
        int ix = x + 14 + i * (slot + gap);
        if (hit_box(mx, my, ix, y + 7, slot, 46)) return i;
    }
    return -1;
}

static int shell_hit_grid_app(rd_window_t *w, int mx, int my, int anim) {
    int i;
    int count = (int)(sizeof(SHELL_GRID_APPS) / sizeof(SHELL_GRID_APPS[0]));
    for (i = 0; i < count; ++i) {
        rdui_rect_t tile = launcher_tile_rect((int)w->width, anim, i);
        if (hit_box(mx, my, tile.x, tile.y, tile.w, tile.h)) return i;
    }
    return -1;
}

static rd_u32 desktop_sig(const rd_desktop_state_t *st, int launcher_open, int quick_open) {
    rd_u32 sig;
    rd_u32 i, j;
    sig = (rd_u32)launcher_open * 17u + (rd_u32)quick_open * 31u + st->hour * 61u + st->minute * 67u;
    sig ^= st->gpu_real ? 0xA53Cu : 0x3CA5u;
    sig ^= st->mesa_ready ? 0x1100u : 0x2200u;
    sig ^= st->opengl_ready ? 0x3300u : 0x4400u;
    sig ^= st->vulkan_ready ? 0x5500u : 0x6600u;
    sig ^= st->physical_gpu_preferred ? 0x7700u : 0x8800u;
    for (j = 0; j < RIDUX_GPU_BACKEND_MAX && st->gpu_backend[j]; ++j) sig = sig * 33u + (rd_u8)st->gpu_backend[j];
    sig ^= st->app_count * 97u;
    if (launcher_open || quick_open) {
        sig ^= ((rd_u32)st->mouse_x / 8u) * 257u;
        sig ^= ((rd_u32)st->mouse_y / 8u) * 521u;
    }
    for (i = 0; i < st->app_count; ++i) {
        sig ^= (st->apps[i].icon + 3u) * (i + 11u);
        sig ^= st->apps[i].running ? (0x100u << (i & 7u)) : 0u;
        sig ^= st->apps[i].focused ? (0x10000u << (i & 7u)) : 0u;
        for (j = 0; j < 24u && st->apps[i].name[j]; ++j) sig = sig * 33u + (rd_u8)st->apps[i].name[j];
    }
    return sig;
}

static void draw_shell(rd_window_t *w, const rd_desktop_state_t *st,
                       int launcher_open, int quick_open,
                       int launcher_anim, int quick_anim) {
    draw_wallpaper(w);
    rdui_fill_alpha(w, 0, 0, (int)w->width, (int)w->height, rd_rgb(3, 8, 18), 3);
    draw_ridux_topbar(w, st);
    if (launcher_open || launcher_anim > 0) draw_ridux_launcher(w, st, launcher_anim);
    if (quick_open || quick_anim > 0) draw_ridux_quick_panel(w, st, quick_anim);
    draw_ridux_bottom_dock(w, st, launcher_open);
}

int main(void) {
    rd_window_t win;
    rd_event_t ev[8];
    rd_desktop_state_t st;
    rd_u32 last_sig = 0xFFFFFFFFu;
    int rc;
    int ticks = 0;
    int launcher_open = 0;
    int quick_open = 0;
    int launcher_anim = 0;
    int quick_anim = 0;

    rc = rd_window_open_flags("Ridux Desktop", DESK_W, DESK_H,
                              RIDUX_WIN_FLAG_DESKTOP |
                              RIDUX_WIN_FLAG_BORDERLESS |
                              RIDUX_WIN_FLAG_NO_FOCUS |
                              RIDUX_WIN_FLAG_NO_TASKBAR,
                              &win);
    if (rc < 0) return 1;

    for (;;) {
        int n = rd_window_poll(&win, ev, 8);
        int i;
        for (i = 0; i < n; ++i) {
            if (ev[i].type == RIDUX_EVENT_CLOSE) {
                rd_window_close(&win);
                return 0;
            }
            if (ev[i].type == RIDUX_EVENT_MOUSE_DOWN) {
                int mx = ev[i].x;
                int my = ev[i].y;
                int bottom = shell_hit_bottom_app(&win, mx, my);
                if (hit_box(mx, my, 0, 0, 130, TOPBAR_H)) {
                    launcher_open = !launcher_open;
                    quick_open = 0;
                    last_sig = 0xFFFFFFFFu;
                } else if (hit_box(mx, my, (int)win.width - 210, 0, 210, TOPBAR_H)) {
                    quick_open = !quick_open;
                    launcher_open = 0;
                    last_sig = 0xFFFFFFFFu;
                } else if (launcher_open) {
                    int grid = shell_hit_grid_app(&win, mx, my, launcher_anim);
                    rdui_rect_t launcher_bounds = launcher_panel_rect((int)win.width,
                                                                       launcher_anim);
                    if (grid >= 0) {
                        shell_open_app(SHELL_GRID_APPS[grid].command);
                        launcher_open = 0;
                        quick_open = 0;
                    } else if (!hit_box(mx, my, launcher_bounds.x, launcher_bounds.y,
                                        launcher_bounds.w, launcher_bounds.h)) {
                        launcher_open = 0;
                    }
                    last_sig = 0xFFFFFFFFu;
                } else if (bottom >= 0) {
                    int count = (int)(sizeof(SHELL_DOCK_APPS) / sizeof(SHELL_DOCK_APPS[0]));
                    if (bottom == count - 1) {
                        launcher_open = !launcher_open;
                        quick_open = 0;
                    } else {
                        shell_open_app(SHELL_DOCK_APPS[bottom].command);
                        launcher_open = 0;
                        quick_open = 0;
                    }
                    last_sig = 0xFFFFFFFFu;
                } else if (quick_open && !hit_box(mx, my, (int)win.width - 378, TOPBAR_H + 20, 354, 298)) {
                    quick_open = 0;
                    last_sig = 0xFFFFFFFFu;
                }
            }
            if (ev[i].type == RIDUX_EVENT_KEY_DOWN) {
                rd_u32 sc = ev[i].scancode;
                rd_u32 key = ev[i].key;
                if (sc == 0x39u || sc == 0x3Bu || key == ' ') {
                    launcher_open = !launcher_open;
                    quick_open = 0;
                    last_sig = 0xFFFFFFFFu;
                } else if (key == 'f' || key == 'F') {
                    shell_open_app("Files");
                    launcher_open = 0;
                    quick_open = 0;
                    last_sig = 0xFFFFFFFFu;
                } else if (key == 's' || key == 'S') {
                    shell_open_app("Settings");
                    launcher_open = 0;
                    quick_open = 0;
                    last_sig = 0xFFFFFFFFu;
                } else if (key == 'c' || key == 'C') {
                    shell_open_app("Calculator");
                    launcher_open = 0;
                    quick_open = 0;
                    last_sig = 0xFFFFFFFFu;
                } else if (sc == 0x01u) {
                    launcher_open = 0;
                    quick_open = 0;
                    last_sig = 0xFFFFFFFFu;
                }
            }
        }
        if (rd_desktop_state(&st) < 0) {
            st.width = win.width;
            st.height = win.height;
            st.start_open = 0;
            st.quick_open = 0;
            st.hour = 0;
            st.minute = 0;
            st.day = 1;
            st.month = 1;
            st.year = 2026;
            st.gpu_real = 0;
            st.mesa_ready = 0;
            st.opengl_ready = 0;
            st.vulkan_ready = 0;
            st.physical_gpu_preferred = 0;
            shell_set_text(st.gpu_backend, sizeof(st.gpu_backend), "GPU present pending");
            st.app_count = 0;
        }
        if (launcher_open) {
            if (launcher_anim < 10) ++launcher_anim;
        } else if (launcher_anim > 0) {
            --launcher_anim;
        }
        if (quick_open) {
            if (quick_anim < 10) ++quick_anim;
        } else if (quick_anim > 0) {
            --quick_anim;
        }
        {
            rd_u32 sig = desktop_sig(&st, launcher_open, quick_open);
            sig ^= (rd_u32)launcher_anim * 131u;
            sig ^= (rd_u32)quick_anim * 197u;
            if (sig != last_sig || ticks == 0 || (ticks % 900) == 0) {
                draw_shell(&win, &st, launcher_open, quick_open, launcher_anim, quick_anim);
                rd_window_present(&win, 0, 0, (int)win.width, (int)win.height);
                last_sig = sig;
            }
        }
        ++ticks;
        rd_sleep_ticks((launcher_anim || quick_anim || launcher_open || quick_open) ? 1u : 2u);
    }
}
