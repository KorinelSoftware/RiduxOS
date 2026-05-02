#include "ridux-flush.h"

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define APP_COUNT      8
#define APP_DOCK_MAX   8
#define QUICK_ACTIONS  4
#define CARD_TITLE_H   32
#define RIDUX_SHOW_BRIDGE_CARDS 1

#ifndef ARRAY_LEN
#define ARRAY_LEN(x) ((int)(sizeof(x) / sizeof((x)[0])))
#endif

typedef struct {
    const char *name;
    uint32_t wallpaper_tint_a;
    uint32_t wallpaper_tint_b;
    uint32_t accent;
    uint32_t accent_hot;
    uint32_t text;
    uint32_t text_muted;
    uint32_t bg;
    uint32_t bg_alt;
    uint32_t glass;
    uint32_t stroke;
    uint32_t taskbar;
    uint32_t start_bg;
    uint32_t danger;
    uint32_t success;
    int dark;
} theme_t;

typedef struct {
    const char *name;
    const char *cmd;
    uint32_t icon_color;
    int icon_id;
    KeySym hotkey;
    flush_rect_t dock_rect;
    flush_rect_t tile_rect;
    uint32_t last_launch_ms;
} app_item_t;

typedef struct {
    int used;
    int app_index;
    flush_rect_t rect;
    uint32_t z;
} app_card_t;

typedef struct {
    int w;
    int h;
    int mx;
    int my;
    int show_start;
    int show_quick;
    int running;
    int theme_index;
    uint32_t frame_tick;
    uint32_t last_title_ms;

    int drag_card;
    int drag_off_x;
    int drag_off_y;

    flush_rect_t taskbar;
    flush_rect_t start_btn;
    flush_rect_t quick_btn;
    flush_rect_t clock_btn;
    flush_rect_t start_menu;
    flush_rect_t quick_panel;
    flush_rect_t quick_action_rect[QUICK_ACTIONS];
} ui_state_t;

typedef struct {
    unsigned long mask;
    int shift;
    unsigned long max_value;
} channel_map_t;

typedef struct {
    Display *display;
    int screen;
    Window window;
    GC gc;
    Atom wm_delete;
    Visual *visual;
    int depth;
    XImage *image;
    channel_map_t red;
    channel_map_t green;
    channel_map_t blue;
} x11_ctx_t;

static const theme_t g_theme_table[] = {
    {
        "Bloom Dark",
        0x00102030, 0x00051525,
        0x0060CDFF, 0x0090DFFF,
        0x00F5FAFF, 0x00B7CBE0,
        0x001B2638, 0x00101928,
        0x00FFFFFF, 0x003E5878,
        0x000E1626, 0x001A2540,
        0x00E95555, 0x005BC98F,
        1
    },
    {
        "Glow",
        0x00201A35, 0x00401E55,
        0x00BC8CFF, 0x00D9B0FF,
        0x00F7F3FF, 0x00CFC2DF,
        0x00231A35, 0x00181127,
        0x00FFFFFF, 0x005C4C7F,
        0x00161022, 0x00221A38,
        0x00FF6B6B, 0x009ED89F,
        1
    },
    {
        "Light",
        0x00E4EEFF, 0x00CAD9F5,
        0x000078D4, 0x00106EBE,
        0x00101820, 0x00505C70,
        0x00F3F6FC, 0x00E8ECF5,
        0x00FFFFFF, 0x00A8BBDC,
        0x00D8E1F0, 0x00EEF3FA,
        0x00E81123, 0x00107C10,
        0
    },
    {
        "Ridux",
        0x0012223A, 0x00083060,
        0x0070D6FF, 0x00A8E3FF,
        0x00F4F9FE, 0x00B9CCDF,
        0x00152236, 0x000B1626,
        0x00FFFFFF, 0x004E6B8E,
        0x000A1324, 0x00162340,
        0x00FF6B6B, 0x0062DE95,
        1
    }
};

static app_item_t g_apps[APP_COUNT] = {
    {"Browser",  "/usr/local/bin/ridux-browser chrome --start-maximized",   0x003B82F6, RIDUX_ICON_BROWSER,   XK_F1, {0,0,0,0}, {0,0,0,0}, 0},
    {"Firefox",  "/usr/local/bin/ridux-browser firefox",                    0x00F97316, RIDUX_ICON_FIREFOX,   XK_F2, {0,0,0,0}, {0,0,0,0}, 0},
    {"Chromium", "/usr/local/bin/ridux-browser chromium --start-maximized", 0x0010B981, RIDUX_ICON_BROWSER,   XK_F3, {0,0,0,0}, {0,0,0,0}, 0},
    {"Files",    "xterm -e sh -lc 'clear; ls -la; exec sh'",                0x00F2C94C, RIDUX_ICON_FILES,     XK_F4, {0,0,0,0}, {0,0,0,0}, 0},
    {"Terminal", "xterm",                                                    0x000EA5A6, RIDUX_ICON_TERMINAL,  XK_F5, {0,0,0,0}, {0,0,0,0}, 0},
    {"Monitor",  "xterm -e /usr/local/bin/ridux-app doctor",                0x000256A3, RIDUX_ICON_MONITOR,   XK_F6, {0,0,0,0}, {0,0,0,0}, 0},
    {"Settings", "xterm -e /usr/local/bin/ridux-app list",                  0x0022C55E, RIDUX_ICON_SETTINGS,  XK_F7, {0,0,0,0}, {0,0,0,0}, 0},
    {"Compat",   "xterm -e /usr/local/bin/ridux-compat",                    0x00A855F7, RIDUX_ICON_PROCESSES, XK_F8, {0,0,0,0}, {0,0,0,0}, 0}
};

static app_card_t g_cards[APP_COUNT];
static uint32_t g_next_card_z = 1;
static int g_dock_count = APP_DOCK_MAX;

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

static uint32_t tick_ms(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) return 0u;
    return (uint32_t)(ts.tv_sec * 1000u + (uint32_t)(ts.tv_nsec / 1000000u));
}

static flush_rect_t rect_make(int x, int y, int w, int h) {
    flush_rect_t r;
    r.x = x;
    r.y = y;
    r.w = w;
    r.h = h;
    return r;
}

static int point_in_rect(int x, int y, flush_rect_t r) {
    return x >= r.x && y >= r.y && x < (r.x + r.w) && y < (r.y + r.h);
}

static flush_color_t col(uint32_t hex, uint8_t a) {
    flush_color_t c;
    c.r = (uint8_t)((hex >> 16) & 0xFFu);
    c.g = (uint8_t)((hex >> 8) & 0xFFu);
    c.b = (uint8_t)(hex & 0xFFu);
    c.a = a;
    return c;
}

static flush_color_t mix_color(flush_color_t a, flush_color_t b, int t, int max_t, uint8_t alpha) {
    flush_color_t c;
    if (max_t <= 0) max_t = 1;
    c.r = (uint8_t)((((int)a.r * (max_t - t)) + ((int)b.r * t)) / max_t);
    c.g = (uint8_t)((((int)a.g * (max_t - t)) + ((int)b.g * t)) / max_t);
    c.b = (uint8_t)((((int)a.b * (max_t - t)) + ((int)b.b * t)) / max_t);
    c.a = alpha;
    return c;
}

static const ridux_image_t *app_icon(const app_item_t *app) {
    if (!app) return NULL;
    if (app->icon_id < 0 || app->icon_id >= RIDUX_ICON_COUNT) return NULL;
    return &RIDUX_ICONS[app->icon_id];
}

static void draw_image_cover(int x, int y, int w, int h, const ridux_image_t *img, uint8_t alpha) {
    int draw_x = x;
    int draw_y = y;
    int draw_w = w;
    int draw_h = h;

    if (!img || !img->width || !img->height || w <= 0 || h <= 0) return;

    if ((uint64_t)w * (uint64_t)img->height > (uint64_t)h * (uint64_t)img->width) {
        draw_h = (int)(((uint64_t)w * (uint64_t)img->height + (uint64_t)img->width / 2u) / (uint64_t)img->width);
        draw_y = y + (h - draw_h) / 2;
    } else {
        draw_w = (int)(((uint64_t)h * (uint64_t)img->width + (uint64_t)img->height / 2u) / (uint64_t)img->height);
        draw_x = x + (w - draw_w) / 2;
    }

    flush_image(draw_x, draw_y, draw_w, draw_h, img, alpha);
}

static const theme_t *current_theme(const ui_state_t *ui) {
    return &g_theme_table[ui->theme_index % ARRAY_LEN(g_theme_table)];
}

static void cycle_theme(ui_state_t *ui, int step) {
    int n = ARRAY_LEN(g_theme_table);
    int idx = ui->theme_index + step;
    while (idx < 0) idx += n;
    ui->theme_index = idx % n;
}

static void spawn_cmd(const char *cmd) {
    char line[1024];
    if (!cmd || !*cmd) return;
    (void)snprintf(line, sizeof(line), "%s >/tmp/ridux-launch.log 2>&1 &", cmd);
    (void)system(line);
}

static void clamp_card_to_desktop(const ui_state_t *ui, app_card_t *card) {
    int desktop_h = ui->taskbar.y - 8;
    if (desktop_h < 120) desktop_h = ui->h - 100;

    card->rect.w = clamp_i(card->rect.w, 240, max_i(260, ui->w - 40));
    card->rect.h = clamp_i(card->rect.h, 160, max_i(200, desktop_h - 20));
    card->rect.x = clamp_i(card->rect.x, 8, max_i(8, ui->w - card->rect.w - 8));
    card->rect.y = clamp_i(card->rect.y, 8, max_i(8, desktop_h - card->rect.h - 8));
}

static flush_rect_t card_title_rect(const app_card_t *card) {
    return rect_make(card->rect.x, card->rect.y, card->rect.w, CARD_TITLE_H);
}

static flush_rect_t card_close_rect(const app_card_t *card) {
    return rect_make(card->rect.x + card->rect.w - 26, card->rect.y + 7, 18, 18);
}

static int collect_card_indices(int *out_sorted) {
    int n = 0;
    int i;
    int j;

    for (i = 0; i < APP_COUNT; ++i) {
        if (g_cards[i].used) out_sorted[n++] = i;
    }

    for (i = 0; i < n - 1; ++i) {
        for (j = i + 1; j < n; ++j) {
            if (g_cards[out_sorted[i]].z > g_cards[out_sorted[j]].z) {
                int t = out_sorted[i];
                out_sorted[i] = out_sorted[j];
                out_sorted[j] = t;
            }
        }
    }
    return n;
}

static void launch_app(ui_state_t *ui, int idx) {
    app_card_t *card;
    uint32_t now;
    if (idx < 0 || idx >= APP_COUNT) return;

    spawn_cmd(g_apps[idx].cmd);
    now = tick_ms();
    g_apps[idx].last_launch_ms = now;

    if (!RIDUX_SHOW_BRIDGE_CARDS) return;

    card = &g_cards[idx];
    card->used = 1;
    card->app_index = idx;
    card->z = g_next_card_z++;

    if (card->rect.w <= 0 || card->rect.h <= 0) {
        card->rect.w = clamp_i(ui->w / 3, 320, 520);
        card->rect.h = clamp_i(ui->h / 3, 190, 320);
        card->rect.x = 70 + (idx % 3) * 44;
        card->rect.y = 88 + (idx % 4) * 34;
    }

    clamp_card_to_desktop(ui, card);
}

static void update_layout(ui_state_t *ui) {
    int margin;
    int bar_h;
    int icon_size;
    int gap;
    int avail;
    int total;
    int start_x;
    int quick_w;
    int clock_w;
    int i;
    int tile_pad;
    int tile_w;
    int tile_h;
    int pad;
    int tile_cols;

    margin = clamp_i(ui->w / 80, 12, 28);
    bar_h = clamp_i(ui->h / 11, 58, 80);

    ui->taskbar = rect_make(margin, ui->h - margin - bar_h, ui->w - margin * 2, bar_h);
    ui->start_btn = rect_make(ui->taskbar.x + 8, ui->taskbar.y + 6, bar_h - 12, bar_h - 12);
    quick_w = clamp_i(ui->w / 8, 116, 156);
    clock_w = clamp_i(ui->w / 10, 108, 146);
    ui->quick_btn = rect_make(ui->taskbar.x + ui->taskbar.w - quick_w - 8, ui->taskbar.y + 6, quick_w, bar_h - 12);
    ui->clock_btn = rect_make(
        ui->quick_btn.x - clock_w - 8,
        ui->taskbar.y + 6,
        clock_w,
        bar_h - 12
    );

    icon_size = clamp_i(bar_h - 16, 34, 52);
    gap = clamp_i(ui->w / 160, 8, 14);
    avail = ui->clock_btn.x - (ui->start_btn.x + ui->start_btn.w) - 30;
    g_dock_count = APP_DOCK_MAX;
    while (g_dock_count > 3) {
        int need = g_dock_count * icon_size + (g_dock_count - 1) * gap;
        if (need <= avail) break;
        --g_dock_count;
    }

    total = g_dock_count * icon_size + (g_dock_count - 1) * gap;
    start_x = ui->start_btn.x + ui->start_btn.w + (avail - total) / 2;
    for (i = 0; i < APP_COUNT; ++i) {
        g_apps[i].dock_rect = rect_make(0, 0, 0, 0);
    }
    for (i = 0; i < g_dock_count; ++i) {
        g_apps[i].dock_rect = rect_make(start_x + i * (icon_size + gap), ui->taskbar.y + (ui->taskbar.h - icon_size) / 2, icon_size, icon_size);
    }

    ui->start_menu = rect_make(ui->taskbar.x + 6, 0, clamp_i((ui->w * 58) / 100, 620, 860), clamp_i((ui->h * 58) / 100, 420, 560));
    ui->start_menu.y = ui->taskbar.y - ui->start_menu.h - 12;

    tile_pad = 18;
    tile_cols = 4;
    tile_w = (ui->start_menu.w - tile_pad * (tile_cols + 1)) / tile_cols;
    tile_h = 92;
    for (i = 0; i < APP_COUNT; ++i) {
        int col_i = i % tile_cols;
        int row_i = i / tile_cols;
        g_apps[i].tile_rect = rect_make(
            ui->start_menu.x + tile_pad + col_i * (tile_w + tile_pad),
            ui->start_menu.y + 96 + row_i * (tile_h + 12),
            tile_w,
            tile_h
        );
    }

    ui->quick_panel = rect_make(0, 0, clamp_i(ui->w / 4, 312, 384), 292);
    ui->quick_panel.x = ui->taskbar.x + ui->taskbar.w - ui->quick_panel.w;
    ui->quick_panel.y = ui->taskbar.y - ui->quick_panel.h - 10;

    pad = 14;
    for (i = 0; i < QUICK_ACTIONS; ++i) {
        int col_i = i % 2;
        int row_i = i / 2;
        int cell_w = (ui->quick_panel.w - pad * 3) / 2;
        ui->quick_action_rect[i] = rect_make(
            ui->quick_panel.x + pad + col_i * (cell_w + pad),
            ui->quick_panel.y + 74 + row_i * 56,
            cell_w,
            44
        );
    }

    for (i = 0; i < APP_COUNT; ++i) {
        if (g_cards[i].used) clamp_card_to_desktop(ui, &g_cards[i]);
    }
}

static void seed_startup_cards(ui_state_t *ui) {
    const int startup[] = { 3, 4, 6 };
    int count = ARRAY_LEN(startup);
    int base_w = clamp_i(ui->w / 3, 360, 560);
    int base_h = clamp_i(ui->h / 3, 220, 340);
    int start_x = clamp_i(ui->w / 12, 44, 120);
    int start_y = clamp_i(ui->h / 8, 54, 110);
    int i;

    for (i = 0; i < count; ++i) {
        int idx = startup[i];
        app_card_t *card = &g_cards[idx];
        card->used = 1;
        card->app_index = idx;
        card->z = g_next_card_z++;
        card->rect.w = base_w;
        card->rect.h = base_h;
        card->rect.x = start_x + i * clamp_i(ui->w / 22, 48, 76);
        card->rect.y = start_y + i * clamp_i(ui->h / 18, 34, 56);
        clamp_card_to_desktop(ui, card);
    }
}

static void draw_panel(flush_rect_t r, int radius, flush_color_t tint, flush_color_t stroke, uint32_t tick) {
    flush_shadow(r.x, r.y, r.w, r.h, radius, 6, col(0x000000, 84));
    flush_round_rect(r.x, r.y, r.w, r.h, radius, tint);
    flush_round_rect(r.x + 1, r.y + 1, r.w - 2, r.h / 2, max_i(2, radius - 2), col(0xFFFFFF, max_i(20, tint.a / 6)));
    flush_rect(r.x, r.y, r.w, 1, stroke);
    flush_rect(r.x, r.y + r.h - 1, r.w, 1, stroke);
    flush_rect(r.x, r.y, 1, r.h, stroke);
    flush_rect(r.x + r.w - 1, r.y, 1, r.h, stroke);
    flush_noise(r.x, r.y, r.w, r.h, tick, 14);
}

static void draw_wallpaper(const ui_state_t *ui, const theme_t *th) {
    const ridux_image_t *wallpaper = &RIDUX_WALLPAPERS[ui->theme_index % RIDUX_WALLPAPER_COUNT];

    flush_clear(col(th->wallpaper_tint_a, 255));
    draw_image_cover(0, 0, ui->w, ui->h, wallpaper, 255);
    flush_vgradient(0, 0, ui->w, ui->h, col(th->wallpaper_tint_a, 84), col(th->wallpaper_tint_b, 126));
    flush_rect(0, 0, ui->w, clamp_i(ui->h / 14, 48, 84), col(0x050A14, 80));
    flush_noise(0, 0, ui->w, ui->h, ui->frame_tick ^ 0xA51E, 6);
}

static void draw_clock(const ui_state_t *ui, flush_rect_t r, flush_color_t text_color) {
    time_t now = time(NULL);
    struct tm *tmv = localtime(&now);
    char hhmm[8] = "00:00";
    char date_line[16] = "00/00/0000";
    int scale = (r.h >= 44) ? 2 : 1;
    int tw;
    int tx;
    int ty;
    flush_color_t sub_color = text_color;
    (void)ui;

    if (tmv) {
        (void)snprintf(hhmm, sizeof(hhmm), "%02d:%02d", tmv->tm_hour, tmv->tm_min);
        (void)snprintf(date_line, sizeof(date_line), "%02d/%02d/%04d",
                       tmv->tm_mday, tmv->tm_mon + 1, tmv->tm_year + 1900);
    }

    tw = flush_text_width(hhmm, scale);
    tx = r.x + r.w - tw - 12;
    ty = r.y + (r.h >= 46 ? 6 : (r.h - 7 * scale) / 2);
    flush_text(tx, ty, scale, text_color, hhmm);
    if (sub_color.a > 64) sub_color.a = (uint8_t)(sub_color.a - 64);
    if (r.h >= 42) {
        int dw = flush_text_width(date_line, 1);
        flush_text(r.x + r.w - dw - 12, r.y + r.h - 16, 1, sub_color, date_line);
    }
}

static void draw_desktop_clock(const ui_state_t *ui, const theme_t *th) {
    time_t now = time(NULL);
    struct tm *tmv = localtime(&now);
    char hhmm[8] = "00:00";
    char date_line[24] = "0000-00-00";
    int x = clamp_i(ui->w / 36, 22, 36);
    int y = clamp_i(ui->h / 30, 18, 34);

    if (tmv) {
        (void)snprintf(hhmm, sizeof(hhmm), "%02d:%02d", tmv->tm_hour, tmv->tm_min);
        (void)snprintf(date_line, sizeof(date_line), "%04d-%02d-%02d",
                       tmv->tm_year + 1900, tmv->tm_mon + 1, tmv->tm_mday);
    }

    flush_text(x + 3, y + 3, 4, col(0x000000, 84), hhmm);
    flush_text(x, y, 4, col(th->text, 196), hhmm);
    flush_text(x + 2, y + 34, 1, col(th->text_muted, 204), date_line);
}

static void draw_app_badge(const app_item_t *app, flush_rect_t r, int hover, int active, flush_color_t fg) {
    const ridux_image_t *icon = app_icon(app);
    flush_color_t base = col(0x081224, hover ? 162 : 128);
    flush_color_t glow = col(app->icon_color, hover ? 84 : 46);
    int inset = max_i(5, r.w / 7);
    int icon_size = min_i(r.w, r.h) - inset * 2;

    flush_round_rect(r.x, r.y, r.w, r.h, 10, base);
    flush_round_rect(r.x + 1, r.y + 1, r.w - 2, r.h - 2, 9, glow);
    flush_stroke_round(r.x, r.y, r.w, r.h, 10, 1, col(0xFFFFFF, hover ? 88 : 54));

    if (icon && icon_size >= 16) {
        int ix = r.x + (r.w - icon_size) / 2;
        int iy = r.y + (r.h - icon_size) / 2;
        flush_image(ix, iy, icon_size, icon_size, icon, hover ? 255 : (active ? 245 : 228));
    } else {
        char label[2];
        int scale;
        int tw;
        int tx;
        int ty;
        label[0] = app->name[0];
        label[1] = '\0';
        scale = max_i(1, r.h / 18);
        tw = flush_text_width(label, scale);
        tx = r.x + (r.w - tw) / 2;
        ty = r.y + (r.h - 7 * scale) / 2;
        flush_text(tx, ty, scale, fg, label);
    }

    if (active) {
        int iw = hover ? 20 : 14;
        flush_round_rect(r.x + (r.w - iw) / 2, r.y + r.h - 5, iw, 3, 2, col(0xFFFFFF, 230));
    }
}

static void draw_cards(const ui_state_t *ui, const theme_t *th) {
    if (!RIDUX_SHOW_BRIDGE_CARDS) return;

    int order[APP_COUNT];
    int n = collect_card_indices(order);
    int i;

    for (i = 0; i < n; ++i) {
        const app_card_t *card = &g_cards[order[i]];
        const app_item_t *app = &g_apps[card->app_index];
        const ridux_image_t *icon = app_icon(app);
        flush_rect_t title = card_title_rect(card);
        flush_rect_t close = card_close_rect(card);
        uint32_t now = tick_ms();
        int pulse = (now - app->last_launch_ms) < 550u;
        flush_rect_t body;
        char cmd_line[128];
        size_t src_len;

        draw_panel(card->rect, 14, col(th->bg, 228), col(th->stroke, 120), ui->frame_tick + 19u);
        flush_round_rect(title.x, title.y, title.w, title.h, 14, col(app->icon_color, 194));
        flush_rect(title.x + 2, title.y + title.h - 3, title.w - 4, 2, col(th->glass, 120));
        if (icon) flush_image(title.x + 10, title.y + 6, 18, 18, icon, 238);
        flush_text(title.x + 34, title.y + 9, 2, col(th->text, 238), app->name);
        flush_text(title.x + 34, title.y + 20, 1, col(th->text_muted, 200), "RIDUX APP BRIDGE");

        flush_round_rect(close.x, close.y, close.w, close.h, 6, col(pulse ? th->danger : th->bg_alt, pulse ? 230 : 170));
        flush_text(close.x + 5, close.y + 5, 1, col(th->text, 240), "X");

        body = rect_make(card->rect.x + 14, card->rect.y + CARD_TITLE_H + 12, card->rect.w - 28, card->rect.h - CARD_TITLE_H - 24);
        src_len = strlen(app->cmd);
        if (src_len > 46) {
            (void)snprintf(cmd_line, sizeof(cmd_line), "CMD: %.43s...", app->cmd);
        } else {
            (void)snprintf(cmd_line, sizeof(cmd_line), "CMD: %s", app->cmd);
        }

        flush_text(body.x, body.y, 1, col(th->text, 224), "STATUS: RUNNING");
        flush_text(body.x, body.y + 18, 1, col(th->text_muted, 210), cmd_line);
        flush_text(body.x, body.y + 36, 1, col(th->text_muted, 180), "TIP: DRAG TITLE TO MOVE");
        if (icon) flush_image(body.x + body.w - 64, body.y + 6, 48, 48, icon, 118);

        if (pulse) {
            flush_rect(card->rect.x, card->rect.y, card->rect.w, 1, col(th->accent_hot, 220));
            flush_rect(card->rect.x, card->rect.y + card->rect.h - 1, card->rect.w, 1, col(th->accent_hot, 220));
        }
    }
}

static void draw_start_menu(const ui_state_t *ui, const theme_t *th) {
    int i;
    if (!ui->show_start) return;

    draw_panel(ui->start_menu, 18, col(th->start_bg, 220), col(th->stroke, 140), ui->frame_tick + 71u);
    flush_text(ui->start_menu.x + 18, ui->start_menu.y + 16, 2, col(th->text, 245), "RIDUX START");
    flush_round_rect(ui->start_menu.x + 18, ui->start_menu.y + 42, ui->start_menu.w - 36, 34, 10, col(th->bg_alt, 210));
    flush_circle(ui->start_menu.x + 38, ui->start_menu.y + 59, 6, col(th->accent, 230));
    flush_text(ui->start_menu.x + 54, ui->start_menu.y + 53, 1, col(th->text_muted, 210), "Search apps");
    flush_text(ui->start_menu.x + 18, ui->start_menu.y + 86, 1, col(th->text_muted, 214), "PINNED");

    for (i = 0; i < APP_COUNT; ++i) {
        flush_rect_t tile = g_apps[i].tile_rect;
        int hover = point_in_rect(ui->mx, ui->my, tile);
        const ridux_image_t *icon = app_icon(&g_apps[i]);
        char hk[8];
        flush_round_rect(tile.x, tile.y, tile.w, tile.h, 12, col(th->bg_alt, hover ? 222 : 166));
        flush_stroke_round(tile.x, tile.y, tile.w, tile.h, 12, 1, col(th->stroke, hover ? 170 : 112));
        if (icon) {
            flush_image(tile.x + (tile.w - 42) / 2, tile.y + 12, 42, 42, icon, hover ? 255 : 236);
        }
        flush_text(tile.x + (tile.w - flush_text_width(g_apps[i].name, 1)) / 2, tile.y + 60, 1,
                   col(th->text, hover ? 245 : 225), g_apps[i].name);
        (void)snprintf(hk, sizeof(hk), "F%d", i + 1);
        flush_text(tile.x + (tile.w - flush_text_width(hk, 1)) / 2, tile.y + 74, 1, col(th->text_muted, 200), hk);
    }

    flush_image(ui->start_menu.x + 18, ui->start_menu.y + ui->start_menu.h - 42, 24, 24, &RIDUX_ICONS[RIDUX_ICON_START], 230);
    flush_text(ui->start_menu.x + 50, ui->start_menu.y + ui->start_menu.h - 34, 1, col(th->text, 220), "ridux");
    flush_text(ui->start_menu.x + ui->start_menu.w - 64, ui->start_menu.y + ui->start_menu.h - 34, 1, col(th->text_muted, 200), "[power]");
}

static void draw_quick_panel(const ui_state_t *ui, const theme_t *th) {
    static const char *k_actions[QUICK_ACTIONS] = { "THEME", "TERMINAL", "DOCTOR", "EXIT UI" };
    int i;
    time_t now = time(NULL);
    struct tm *tmv = localtime(&now);
    char date_line[24] = "0000-00-00";

    if (!ui->show_quick) return;
    if (tmv) {
        (void)snprintf(date_line, sizeof(date_line), "%04d-%02d-%02d", tmv->tm_year + 1900, tmv->tm_mon + 1, tmv->tm_mday);
    }

    draw_panel(ui->quick_panel, 16, col(th->taskbar, 224), col(th->stroke, 130), ui->frame_tick + 111u);
    flush_text(ui->quick_panel.x + 14, ui->quick_panel.y + 14, 2, col(th->text, 240), "QUICK SETTINGS");
    flush_text(ui->quick_panel.x + 14, ui->quick_panel.y + 36, 1, col(th->text_muted, 210), date_line);

    for (i = 0; i < QUICK_ACTIONS; ++i) {
        flush_rect_t ar = ui->quick_action_rect[i];
        int hover = point_in_rect(ui->mx, ui->my, ar);
        flush_round_rect(ar.x, ar.y, ar.w, ar.h, 10, col(th->bg_alt, hover ? 224 : 182));
        flush_stroke_round(ar.x, ar.y, ar.w, ar.h, 10, 1, col(th->stroke, hover ? 170 : 120));
        flush_text(ar.x + 14, ar.y + 15, 1, col(th->text, hover ? 245 : 224), k_actions[i]);
    }
}

static void draw_taskbar(const ui_state_t *ui, const theme_t *th) {
    int i;
    draw_panel(ui->taskbar, 16, col(th->taskbar, 214), col(th->stroke, 120), ui->frame_tick + 29u);

    {
        int start_hover = point_in_rect(ui->mx, ui->my, ui->start_btn);
        int icon_sz = min_i(ui->start_btn.w, ui->start_btn.h) - 14;
        flush_color_t start_bg = col(ui->show_start ? th->accent_hot : th->taskbar, start_hover ? 228 : 194);
        flush_round_rect(ui->start_btn.x, ui->start_btn.y, ui->start_btn.w, ui->start_btn.h, 10, start_bg);
        flush_stroke_round(ui->start_btn.x, ui->start_btn.y, ui->start_btn.w, ui->start_btn.h, 10, 1,
                           col(th->stroke, ui->show_start ? 160 : 120));
        flush_image(ui->start_btn.x + (ui->start_btn.w - icon_sz) / 2,
                    ui->start_btn.y + (ui->start_btn.h - icon_sz) / 2,
                    icon_sz, icon_sz, &RIDUX_ICONS[RIDUX_ICON_START],
                    ui->show_start ? 255 : 238);
    }

    for (i = 0; i < g_dock_count; ++i) {
        draw_app_badge(&g_apps[i], g_apps[i].dock_rect, point_in_rect(ui->mx, ui->my, g_apps[i].dock_rect), g_cards[i].used, col(0xFFFFFF, 245));
    }

    {
        int hover_q = point_in_rect(ui->mx, ui->my, ui->quick_btn);
        const int tray_ids[] = {
            RIDUX_ICON_TRAY_NETWORK, RIDUX_ICON_TRAY_VOLUME,
            RIDUX_ICON_TRAY_BATTERY, RIDUX_ICON_TRAY_SETTINGS
        };
        const int tray_count = ARRAY_LEN(tray_ids);
        const int icon_sz = 16;
        int total_w = tray_count * icon_sz + (tray_count - 1) * 10;
        int base_x = ui->quick_btn.x + (ui->quick_btn.w - total_w) / 2;
        flush_round_rect(ui->quick_btn.x, ui->quick_btn.y, ui->quick_btn.w, ui->quick_btn.h, 9, col(th->bg_alt, hover_q ? 236 : 190));
        flush_stroke_round(ui->quick_btn.x, ui->quick_btn.y, ui->quick_btn.w, ui->quick_btn.h, 9, 1, col(th->stroke, hover_q ? 160 : 112));
        for (i = 0; i < tray_count; ++i) {
            flush_image(base_x + i * (icon_sz + 10),
                        ui->quick_btn.y + (ui->quick_btn.h - icon_sz) / 2,
                        icon_sz, icon_sz, &RIDUX_ICONS[tray_ids[i]], 232);
        }
    }

    {
        int hover_c = point_in_rect(ui->mx, ui->my, ui->clock_btn);
        flush_round_rect(ui->clock_btn.x, ui->clock_btn.y, ui->clock_btn.w, ui->clock_btn.h, 9, col(th->bg_alt, hover_c ? 236 : 190));
        flush_stroke_round(ui->clock_btn.x, ui->clock_btn.y, ui->clock_btn.w, ui->clock_btn.h, 9, 1, col(th->stroke, hover_c ? 160 : 112));
        draw_clock(ui, ui->clock_btn, col(th->text, 242));
    }
}

static void render_frame(ui_state_t *ui) {
    const theme_t *th = current_theme(ui);
    draw_wallpaper(ui, th);
    draw_desktop_clock(ui, th);
    draw_cards(ui, th);
    draw_start_menu(ui, th);
    draw_quick_panel(ui, th);
    draw_taskbar(ui, th);
}

static void update_window_title(x11_ctx_t *x11, ui_state_t *ui) {
    uint32_t now = tick_ms();
    if (now - ui->last_title_ms >= 1000u) {
        char title[256];
        ui->last_title_ms = now;
        (void)snprintf(title, sizeof(title),
                       "Ridux UI Flush X11 | Theme: %s | F1-F8 launch | TAB theme | ESC exit",
                       current_theme(ui)->name);
        XStoreName(x11->display, x11->window, title);
    }
}

static void close_menus(ui_state_t *ui) {
    ui->show_start = 0;
    ui->show_quick = 0;
}

static void handle_quick_action(ui_state_t *ui, int action) {
    switch (action) {
        case 0:
            cycle_theme(ui, +1);
            break;
        case 1:
            launch_app(ui, 4);
            break;
        case 2:
            launch_app(ui, 5);
            break;
        case 3:
            ui->running = 0;
            break;
        default:
            break;
    }
}

static void handle_left_button_down(ui_state_t *ui, int x, int y) {
    int order[APP_COUNT];
    int count = collect_card_indices(order);
    int i;

    if (RIDUX_SHOW_BRIDGE_CARDS) {
        for (i = count - 1; i >= 0; --i) {
            app_card_t *card = &g_cards[order[i]];
            flush_rect_t close = card_close_rect(card);
            flush_rect_t title = card_title_rect(card);
            if (point_in_rect(x, y, close)) {
                card->used = 0;
                return;
            }
            if (point_in_rect(x, y, title)) {
                ui->drag_card = order[i];
                ui->drag_off_x = x - card->rect.x;
                ui->drag_off_y = y - card->rect.y;
                card->z = g_next_card_z++;
                return;
            }
        }
    }

    if (point_in_rect(x, y, ui->start_btn)) {
        ui->show_start = !ui->show_start;
        ui->show_quick = 0;
        return;
    }

    if (point_in_rect(x, y, ui->quick_btn)) {
        ui->show_quick = !ui->show_quick;
        ui->show_start = 0;
        return;
    }

    if (point_in_rect(x, y, ui->clock_btn)) {
        cycle_theme(ui, +1);
        return;
    }

    if (ui->show_start && point_in_rect(x, y, ui->start_menu)) {
        for (i = 0; i < APP_COUNT; ++i) {
            if (point_in_rect(x, y, g_apps[i].tile_rect)) {
                launch_app(ui, i);
                close_menus(ui);
                return;
            }
        }
        return;
    }

    if (ui->show_quick && point_in_rect(x, y, ui->quick_panel)) {
        for (i = 0; i < QUICK_ACTIONS; ++i) {
            if (point_in_rect(x, y, ui->quick_action_rect[i])) {
                handle_quick_action(ui, i);
                if (i != 0) close_menus(ui);
                return;
            }
        }
        return;
    }

    for (i = 0; i < g_dock_count; ++i) {
        if (point_in_rect(x, y, g_apps[i].dock_rect)) {
            launch_app(ui, i);
            close_menus(ui);
            return;
        }
    }

    close_menus(ui);
}

static void handle_keydown(ui_state_t *ui, KeySym key) {
    int i;
    switch (key) {
        case XK_Escape:
            if (ui->show_start || ui->show_quick) close_menus(ui);
            else ui->running = 0;
            return;
        case XK_Tab:
            cycle_theme(ui, +1);
            return;
        case XK_Return:
            ui->show_start = !ui->show_start;
            ui->show_quick = 0;
            return;
        case XK_q:
        case XK_Q:
            ui->show_quick = !ui->show_quick;
            ui->show_start = 0;
            return;
        default:
            break;
    }

    for (i = 0; i < APP_COUNT; ++i) {
        if (g_apps[i].hotkey == key) {
            launch_app(ui, i);
            return;
        }
    }
}

static channel_map_t map_from_mask(unsigned long mask) {
    channel_map_t c;
    c.mask = mask;
    c.shift = 0;
    c.max_value = 0;
    if (!mask) return c;

    while (((mask >> c.shift) & 1ul) == 0ul && c.shift < 63) c.shift++;
    c.max_value = mask >> c.shift;
    return c;
}

static unsigned long scale_to_mask(uint8_t v, channel_map_t c) {
    unsigned long scaled;
    if (!c.mask || c.max_value == 0ul) return 0ul;
    scaled = ((unsigned long)v * c.max_value + 127ul) / 255ul;
    return (scaled << c.shift) & c.mask;
}

static int x11_resize_image(x11_ctx_t *x11, int w, int h) {
    size_t image_size;
    if (x11->image) {
        XDestroyImage(x11->image);
        x11->image = NULL;
    }

    x11->image = XCreateImage(
        x11->display,
        x11->visual,
        (unsigned int)x11->depth,
        ZPixmap,
        0,
        NULL,
        (unsigned int)w,
        (unsigned int)h,
        32,
        0
    );
    if (!x11->image) return 0;

    image_size = (size_t)x11->image->bytes_per_line * (size_t)h;
    x11->image->data = (char *)malloc(image_size);
    if (!x11->image->data) {
        XDestroyImage(x11->image);
        x11->image = NULL;
        return 0;
    }
    (void)memset(x11->image->data, 0, image_size);
    return 1;
}

static void x11_shutdown(x11_ctx_t *x11);

static int x11_init(x11_ctx_t *x11, int w, int h, const char *title) {
    XSetWindowAttributes attrs;
    (void)memset(x11, 0, sizeof(*x11));

    x11->display = XOpenDisplay(NULL);
    if (!x11->display) return 0;

    x11->screen = DefaultScreen(x11->display);
    x11->visual = DefaultVisual(x11->display, x11->screen);
    x11->depth = DefaultDepth(x11->display, x11->screen);
    x11->red = map_from_mask(x11->visual->red_mask);
    x11->green = map_from_mask(x11->visual->green_mask);
    x11->blue = map_from_mask(x11->visual->blue_mask);

    attrs.background_pixel = BlackPixel(x11->display, x11->screen);
    attrs.override_redirect = True;
    attrs.event_mask = ExposureMask | StructureNotifyMask | KeyPressMask |
                       ButtonPressMask | ButtonReleaseMask | PointerMotionMask;
    x11->window = XCreateWindow(
        x11->display,
        RootWindow(x11->display, x11->screen),
        0, 0,
        (unsigned int)w,
        (unsigned int)h,
        0,
        x11->depth,
        InputOutput,
        x11->visual,
        CWBackPixel | CWOverrideRedirect | CWEventMask,
        &attrs
    );
    if (!x11->window) {
        x11_shutdown(x11);
        return 0;
    }

    XStoreName(x11->display, x11->window, title);
    x11->gc = XCreateGC(x11->display, x11->window, 0, NULL);
    if (!x11->gc) {
        x11_shutdown(x11);
        return 0;
    }

    x11->wm_delete = XInternAtom(x11->display, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(x11->display, x11->window, &x11->wm_delete, 1);

    XMapWindow(x11->display, x11->window);
    XRaiseWindow(x11->display, x11->window);
    XFlush(x11->display);

    if (!x11_resize_image(x11, w, h)) {
        x11_shutdown(x11);
        return 0;
    }
    return 1;
}

static void x11_shutdown(x11_ctx_t *x11) {
    if (x11->image) {
        XDestroyImage(x11->image);
        x11->image = NULL;
    }
    if (x11->gc) {
        XFreeGC(x11->display, x11->gc);
        x11->gc = 0;
    }
    if (x11->window) {
        XDestroyWindow(x11->display, x11->window);
        x11->window = 0;
    }
    if (x11->display) {
        XCloseDisplay(x11->display);
        x11->display = NULL;
    }
}

static void x11_present(x11_ctx_t *x11) {
    const uint32_t *src = flush_pixels();
    int w = flush_width();
    int h = flush_height();
    int x;
    int y;
    if (!x11->image || !src || w <= 0 || h <= 0) return;

    if (x11->image->bits_per_pixel == 32) {
        for (y = 0; y < h; ++y) {
            uint32_t *dst = (uint32_t *)(x11->image->data + (size_t)y * (size_t)x11->image->bytes_per_line);
            for (x = 0; x < w; ++x) {
                uint32_t s = src[(size_t)y * (size_t)w + (size_t)x];
                uint8_t r = (uint8_t)((s >> 16) & 0xFFu);
                uint8_t g = (uint8_t)((s >> 8) & 0xFFu);
                uint8_t b = (uint8_t)(s & 0xFFu);
                unsigned long p = scale_to_mask(r, x11->red) |
                                  scale_to_mask(g, x11->green) |
                                  scale_to_mask(b, x11->blue);
                dst[x] = (uint32_t)p;
            }
        }
    } else {
        for (y = 0; y < h; ++y) {
            for (x = 0; x < w; ++x) {
                uint32_t s = src[(size_t)y * (size_t)w + (size_t)x];
                uint8_t r = (uint8_t)((s >> 16) & 0xFFu);
                uint8_t g = (uint8_t)((s >> 8) & 0xFFu);
                uint8_t b = (uint8_t)(s & 0xFFu);
                unsigned long p = scale_to_mask(r, x11->red) |
                                  scale_to_mask(g, x11->green) |
                                  scale_to_mask(b, x11->blue);
                XPutPixel(x11->image, x, y, p);
            }
        }
    }

    XPutImage(x11->display, x11->window, x11->gc, x11->image, 0, 0, 0, 0, (unsigned int)w, (unsigned int)h);
    XFlush(x11->display);
}

int main(void) {
    x11_ctx_t x11;
    ui_state_t ui;
    XEvent ev;

    (void)memset(&ui, 0, sizeof(ui));
    ui.w = 1366;
    ui.h = 768;
    ui.running = 1;
    ui.theme_index = 3;
    ui.drag_card = -1;

    if (!x11_init(&x11, ui.w, ui.h, "Ridux UI Flush X11")) return 1;
    {
        int sw = DisplayWidth(x11.display, x11.screen);
        int sh = DisplayHeight(x11.display, x11.screen);
        if (sw >= 640 && sh >= 400) {
            ui.w = sw;
            ui.h = sh;
            XMoveResizeWindow(x11.display, x11.window, 0, 0, (unsigned int)ui.w, (unsigned int)ui.h);
            XRaiseWindow(x11.display, x11.window);
            if (!x11_resize_image(&x11, ui.w, ui.h)) {
                x11_shutdown(&x11);
                return 2;
            }
        }
    }
    if (!flush_init(ui.w, ui.h)) {
        x11_shutdown(&x11);
        return 2;
    }

    update_layout(&ui);
    seed_startup_cards(&ui);

    while (ui.running) {
        while (XPending(x11.display) > 0) {
            XNextEvent(x11.display, &ev);
            switch (ev.type) {
                case ClientMessage:
                    if ((Atom)ev.xclient.data.l[0] == x11.wm_delete) ui.running = 0;
                    break;
                case ConfigureNotify: {
                    int nw = ev.xconfigure.width;
                    int nh = ev.xconfigure.height;
                    if (nw > 100 && nh > 100 && (nw != ui.w || nh != ui.h)) {
                        ui.w = nw;
                        ui.h = nh;
                        if (!flush_resize(nw, nh) || !x11_resize_image(&x11, nw, nh)) {
                            ui.running = 0;
                        } else {
                            update_layout(&ui);
                        }
                    }
                } break;
                case MotionNotify:
                    ui.mx = ev.xmotion.x;
                    ui.my = ev.xmotion.y;
                    if (ui.drag_card >= 0 && ui.drag_card < APP_COUNT && g_cards[ui.drag_card].used) {
                        g_cards[ui.drag_card].rect.x = ui.mx - ui.drag_off_x;
                        g_cards[ui.drag_card].rect.y = ui.my - ui.drag_off_y;
                        clamp_card_to_desktop(&ui, &g_cards[ui.drag_card]);
                    }
                    break;
                case ButtonPress:
                    if (ev.xbutton.button == Button1) {
                        handle_left_button_down(&ui, ev.xbutton.x, ev.xbutton.y);
                    }
                    break;
                case ButtonRelease:
                    if (ev.xbutton.button == Button1) ui.drag_card = -1;
                    break;
                case KeyPress: {
                    KeySym key = XLookupKeysym(&ev.xkey, 0);
                    handle_keydown(&ui, key);
                } break;
                default:
                    break;
            }
        }

        update_layout(&ui);
        update_window_title(&x11, &ui);
        flush_reset();
        render_frame(&ui);
        flush_execute();
        x11_present(&x11);
        ui.frame_tick++;
        usleep(16000);
    }

    flush_shutdown();
    x11_shutdown(&x11);
    return 0;
}
