#include "user_libridux.h"
#include "assets.h"

#define DESK_W 1024u
#define DESK_H 768u
#define TASKBAR_H 56

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

static int str_len(const char *s) {
    int n = 0;
    if (!s) return 0;
    while (s[n]) ++n;
    return n;
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
    rd_text(w, right - str_len(s) * 8, y, s, color);
}

static void draw_icon_scaled(rd_window_t *w, int icon_id, int x, int y, int sz) {
    const ridux_image_t *img;
    int ox, oy;
    if (icon_id < 0 || icon_id >= RIDUX_ICON_COUNT || sz <= 0) return;
    img = &RIDUX_ICONS[icon_id];
    for (oy = 0; oy < sz; ++oy) {
        rd_u32 sy = (rd_u32)((rd_u64)oy * img->height / (rd_u32)sz);
        for (ox = 0; ox < sz; ++ox) {
            rd_u32 sx = (rd_u32)((rd_u64)ox * img->width / (rd_u32)sz);
            rd_u32 c = img->pixels[(rd_size)sy * img->width + sx];
            rd_u32 a = (c >> 24) & 255u;
            if (a) pixel_alpha(w, x + ox, y + oy, argb_to_xrgb(c), a);
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
            rd_u32 shade;
            if (sx >= img->width) sx = img->width - 1u;
            c = argb_to_xrgb(img->pixels[(rd_size)sy * img->width + sx]);
            shade = (y > w->height * 72u / 100u)
                  ? (rd_u32)(((y - w->height * 72u / 100u) * 50u) /
                             (w->height - w->height * 72u / 100u + 1u))
                  : 0u;
            if (shade) c = mix_rgb(c, rd_rgb(4, 10, 18), shade);
            w->fb[(rd_size)y * pitch + x] = c;
        }
    }
}

static void draw_wallpaper(rd_window_t *w) {
    if (RIDUX_WALLPAPER_COUNT > 0) {
        draw_wallpaper_image(w, &RIDUX_WALLPAPERS[0]);
        return;
    }
    rd_clear(w, rd_rgb(16, 28, 42));
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

static void draw_quick_panel(rd_window_t *w, const rd_desktop_state_t *st) {
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

static void draw_start_menu(rd_window_t *w, const rd_desktop_state_t *st) {
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

static void draw_taskbar(rd_window_t *w, const rd_desktop_state_t *st) {
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

static rd_u32 desktop_sig(const rd_desktop_state_t *st) {
    rd_u32 sig;
    rd_u32 i, j;
    sig = st->start_open * 17u + st->quick_open * 31u + st->hour * 61u + st->minute * 67u;
    sig ^= st->app_count * 97u;
    for (i = 0; i < st->app_count; ++i) {
        sig ^= (st->apps[i].icon + 3u) * (i + 11u);
        sig ^= st->apps[i].running ? (0x100u << (i & 7u)) : 0u;
        sig ^= st->apps[i].focused ? (0x10000u << (i & 7u)) : 0u;
        for (j = 0; j < 24u && st->apps[i].name[j]; ++j) sig = sig * 33u + (rd_u8)st->apps[i].name[j];
    }
    return sig;
}

static void draw_shell(rd_window_t *w, const rd_desktop_state_t *st) {
    draw_wallpaper(w);
    if (st->start_open) draw_start_menu(w, st);
    if (st->quick_open) draw_quick_panel(w, st);
    draw_taskbar(w, st);
}

int main(void) {
    rd_window_t win;
    rd_event_t ev[8];
    rd_desktop_state_t st;
    rd_u32 last_sig = 0xFFFFFFFFu;
    int rc;
    int ticks = 0;

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
            st.app_count = 0;
        }
        {
            rd_u32 sig = desktop_sig(&st);
            if (sig != last_sig || ticks == 0 || (ticks % 900) == 0) {
                draw_shell(&win, &st);
                rd_window_present(&win, 0, 0, (int)win.width, (int)win.height);
                last_sig = sig;
            }
        }
        ++ticks;
        rd_sleep_ticks(4);
    }
}