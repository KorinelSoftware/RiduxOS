/*
 * Pack de apps Ring 3 de Ridux.
 *
 * Es el mismo archivo compilado varias veces con RIDUX_APP_KIND distinto.
 * Asi no tengo que duplicar medio escritorio, pero cada tile del launcher
 * termina corriendo como proceso de usuario de verdad.
 */
#include "user_libridux.h"
#include "ridux_ui.h"
#include "assets.h"

#ifndef RIDUX_APP_KIND
#define RIDUX_APP_KIND 0
#endif

#define APP_TERMINAL  1
#define APP_FILES     2
#define APP_SETTINGS  3
#define APP_CALC      4
#define APP_CLOCK     5
#define APP_PAINT     6
#define APP_TASKMGR   7
#define APP_BROWSER   8
#define APP_FIREFOX   9
#define APP_WEATHER   10
#define APP_STORE     11
#define APP_ABOUT     12
#define APP_MEDIA     13
#define APP_MONITOR   14
#define APP_FLUSH     15
#define APP_EDITOR    16
#define APP_MINE      17
#define APP_SNAKE     18
#define APP_LOG       19
#define APP_NET       20
#define APP_PROC      21
#define APP_SYSINFO   22
#define APP_TTT       23
#define APP_NOTES     24
#define APP_RING3DEMO 25

#define C_BG      0x0F172Au
#define C_PANEL   0x18233Au
#define C_PANEL2  0x22314Fu
#define C_ACCENT  0x38BDF8u
#define C_ACCENT2 0x22C55Eu
#define C_WARN    0xF59E0Bu
#define C_BAD     0xEF4444u
#define C_TEXT    0xEAF6FFu
#define C_MUTED   0x91A4B7u
#define C_LINE    0x31445Fu

#define TERM_ROWS 10
#define TERM_COLS 72

static rd_u32 g_seed = 0x7357BEEF;

static rd_u32 rnd(void) {
    g_seed = g_seed * 1103515245u + 12345u;
    return g_seed;
}

static rd_u32 app_argb_to_xrgb(rd_u32 c) {
    return c & 0x00FFFFFFu;
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
            if (a) rdui_pixel_alpha(w, dx + ox, dy + oy, app_argb_to_xrgb(c), a);
        }
    }
}

static int app_icon(void) {
    switch (RIDUX_APP_KIND) {
        case APP_TERMINAL: return RIDUX_ICON_TERMINAL;
        case APP_FILES: return RIDUX_ICON_FILES;
        case APP_SETTINGS: return RIDUX_ICON_SETTINGS;
        case APP_CALC: return RIDUX_ICON_CALC;
        case APP_CLOCK: return RIDUX_ICON_CLOCK;
        case APP_PAINT: return RIDUX_ICON_PAINT;
        case APP_TASKMGR: return RIDUX_ICON_TASKMGR;
        case APP_BROWSER: return RIDUX_ICON_BROWSER;
        case APP_FIREFOX: return RIDUX_ICON_FIREFOX;
        case APP_WEATHER: return RIDUX_ICON_WEATHER;
        case APP_STORE: return RIDUX_ICON_STORE;
        case APP_ABOUT: return RIDUX_ICON_ABOUT;
        case APP_MEDIA: return RIDUX_ICON_MEDIA;
        case APP_MONITOR: return RIDUX_ICON_MONITOR;
        case APP_FLUSH: return RIDUX_ICON_FLUSH;
        case APP_EDITOR: return RIDUX_ICON_EDITOR;
        case APP_MINE: return RIDUX_ICON_MINESWEEPER;
        case APP_SNAKE: return RIDUX_ICON_SNAKE;
        case APP_LOG: return RIDUX_ICON_LOGVIEWER;
        case APP_NET: return RIDUX_ICON_NETWORK;
        case APP_PROC: return RIDUX_ICON_PROCESSES;
        case APP_SYSINFO: return RIDUX_ICON_SYSINFO;
        case APP_TTT: return RIDUX_ICON_TICTACTOE;
        case APP_NOTES: return RIDUX_ICON_NOTES;
        default: return RIDUX_ICON_START;
    }
}

static const char *app_title(void) {
    switch (RIDUX_APP_KIND) {
        case APP_TERMINAL: return "Terminal";
        case APP_FILES: return "Files";
        case APP_SETTINGS: return "Settings";
        case APP_CALC: return "Calculator";
        case APP_CLOCK: return "Clock";
        case APP_PAINT: return "Paint";
        case APP_TASKMGR: return "Task Manager";
        case APP_BROWSER: return "Browser";
        case APP_FIREFOX: return "Firefox";
        case APP_WEATHER: return "Weather";
        case APP_STORE: return "Ridux Store";
        case APP_ABOUT: return "About";
        case APP_MEDIA: return "Media";
        case APP_MONITOR: return "Monitor";
        case APP_FLUSH: return "Flush";
        case APP_EDITOR: return "Editor";
        case APP_MINE: return "Minesweeper";
        case APP_SNAKE: return "Snake";
        case APP_LOG: return "Log Viewer";
        case APP_NET: return "Network";
        case APP_PROC: return "Processes";
        case APP_SYSINFO: return "System Info";
        case APP_TTT: return "Tic-Tac-Toe";
        case APP_NOTES: return "Notes";
        case APP_RING3DEMO: return "Ring 3 Demo";
        default: return "Ridux App";
    }
}

static __attribute__((unused)) const char *app_subtitle(void) {
    switch (RIDUX_APP_KIND) {
        case APP_TERMINAL: return "Ring 3 terminal shell surface";
        case APP_FILES: return "Files view running outside kernel space";
        case APP_SETTINGS: return "Desktop switches kept in user space";
        case APP_CALC: return "Click numbers and ops";
        case APP_CLOCK: return "Tiny clock face, user-mode repaint";
        case APP_PAINT: return "Click/drag to paint";
        case APP_TASKMGR: return "Process view from the app sandbox";
        case APP_BROWSER: return "Native browser launcher panel";
        case APP_FIREFOX: return "Real Firefox starts via `firefox`";
        case APP_WEATHER: return "Weather card mock, no kernel draw";
        case APP_STORE: return "App tiles from a CPL=3 process";
        case APP_ABOUT: return "RiduxOS identity card";
        case APP_MEDIA: return "Media controls sandboxed";
        case APP_MONITOR: return "CPU, memory and frame stats";
        case APP_FLUSH: return "Renderer queue inspector";
        case APP_EDITOR: return "Small notepad in Ring 3";
        case APP_MINE: return "Minefield toy";
        case APP_SNAKE: return "Snake board toy";
        case APP_LOG: return "Boot log tail";
        case APP_NET: return "Network counters";
        case APP_PROC: return "Process tree";
        case APP_SYSINFO: return "Kernel facts from userspace";
        case APP_TTT: return "Click cells to play";
        case APP_NOTES: return "Notes surface in userspace";
        case APP_RING3DEMO: return "Launcher sanity-check, also outside ring 0";
        default: return "Ridux user app";
    }
}

static void utoa_dec(int v, char *out, int cap) {
    char tmp[16];
    int n = 0, i = 0;
    if (cap <= 0) return;
    if (v < 0) {
        out[i++] = '-';
        v = -v;
    }
    if (v == 0) tmp[n++] = '0';
    while (v > 0 && n < (int)sizeof(tmp)) {
        tmp[n++] = (char)('0' + (v % 10));
        v /= 10;
    }
    while (n > 0 && i + 1 < cap) out[i++] = tmp[--n];
    out[i] = 0;
}

static int in_rect(int x, int y, int rx, int ry, int rw, int rh) {
    return x >= rx && y >= ry && x < rx + rw && y < ry + rh;
}

static void term_push_line(char lines[TERM_ROWS][TERM_COLS], int *count, const char *s) {
    int row, col;
    if (!lines || !count || !s) return;
    if (*count < TERM_ROWS) {
        row = (*count)++;
    } else {
        int r;
        for (r = 1; r < TERM_ROWS; ++r) {
            for (col = 0; col < TERM_COLS; ++col) lines[r - 1][col] = lines[r][col];
        }
        row = TERM_ROWS - 1;
    }
    for (col = 0; col + 1 < TERM_COLS && s[col]; ++col) lines[row][col] = s[col];
    lines[row][col] = 0;
}

static void term_push_multiline(char lines[TERM_ROWS][TERM_COLS], int *count, const char *s) {
    char line[TERM_COLS];
    int n = 0;
    int i;
    if (!s) return;
    for (i = 0; s[i]; ++i) {
        char c = s[i];
        if (c == '\r') continue;
        if (c == '\n') {
            line[n] = 0;
            term_push_line(lines, count, line);
            n = 0;
            continue;
        }
        if (n + 1 < TERM_COLS) line[n++] = c;
    }
    if (n > 0) {
        line[n] = 0;
        term_push_line(lines, count, line);
    }
}

static void button(rd_window_t *w, int x, int y, int bw, int bh,
                   const char *label, rd_u32 color) {
    rdui_button_class(w, x, y, bw, bh, label,
                      (color == C_ACCENT || color == C_ACCENT2) ? "button.primary" : "button.ghost",
                      color == C_ACCENT || color == C_ACCENT2);
}

static void chrome(rd_window_t *w) {
    const char *unused_title = app_title();
    (void)unused_title;
    rd_clear(w, rd_rgb(10, 14, 22));
    rdui_gradient_v(w, 0, 0, (int)w->width, (int)w->height,
                    rd_rgb(16, 21, 30), rd_rgb(8, 11, 17));
    rdui_fill_alpha(w, 0, 0, (int)w->width, 1, rd_rgb(255, 255, 255), 12);
}

static void card(rd_window_t *w, int x, int y, int ww, int hh,
                 const char *title, const char *body) {
    rdui_panel_class(w, x, y, ww, hh, "surface.card");
    rd_text(w, x + 12, y + 12, title, C_TEXT);
    if (body) rd_text(w, x + 12, y + 32, body, C_MUTED);
}

static void premium_card(rd_window_t *w, int x, int y, int ww, int hh,
                         const char *title, const char *body,
                         int icon, rd_u32 accent) {
    rdui_material(w, rdui_rect(x, y, ww, hh), rd_rgb(18, 26, 38), 186, 22, 3);
    rdui_round_alpha(w, x + 14, y + 14, 44, 44, 14, accent, 34);
    rdui_round_outline(w, x + 14, y + 14, 44, 44, 14, rd_rgb(255, 255, 255), 18);
    draw_icon_scaled(w, icon, x + 20, y + 20, 32);
    rd_text(w, x + 72, y + 18, title, C_TEXT);
    if (body) rd_text(w, x + 72, y + 40, body, C_MUTED);
}

static void draw_app_backdrop(rd_window_t *w, rd_u32 a, rd_u32 b) {
    int W = (int)w->width;
    int H = (int)w->height;
    (void)a;
    (void)b;
    rd_clear(w, rd_rgb(7, 10, 17));
    rdui_gradient_v(w, 0, 0, W, H, rd_rgb(14, 19, 28), rd_rgb(7, 10, 16));
    rdui_fill_alpha(w, 0, 0, W, H, rd_rgb(255, 255, 255), 3);
}

static __attribute__((unused)) void draw_large_window_shell(rd_window_t *w, int x, int y, int ww, int hh) {
    rdui_material(w, rdui_rect(x, y, ww, hh), rd_rgb(16, 23, 34), 222, 26, 3);
}

static void draw_generic(rd_window_t *w, int ticks) {
    char n[16];
    draw_app_backdrop(w, rd_rgb(82, 137, 255), rd_rgb(50, 203, 155));
    chrome(w);
    premium_card(w, 20, 82, (int)w->width - 40, 92, "Ring 3 status",
                 "This window is rendered by a user-mode ELF.",
                 app_icon(), C_ACCENT);
    premium_card(w, 20, 194, (int)w->width - 40, 92, "Backend",
                 "Ridux native window protocol, compositor-safe.",
                 RIDUX_ICON_FLUSH, C_ACCENT2);
    utoa_dec(ticks, n, sizeof(n));
    rd_text(w, 30, (int)w->height - 28, "frames:", C_MUTED);
    rd_text(w, 82, (int)w->height - 28, n, C_ACCENT);
}

static void draw_terminal(rd_window_t *w, const char *line,
                          char hist[TERM_ROWS][TERM_COLS], int hist_count) {
    int i;
    int y;
    int first = hist_count > 8 ? hist_count - 8 : 0;
    draw_app_backdrop(w, rd_rgb(42, 132, 255), rd_rgb(40, 210, 158));
    chrome(w);
    rdui_material(w, rdui_rect(18, 78, (int)w->width - 36, (int)w->height - 98),
                  rd_rgb(6, 13, 22), 224, 20, 3);
    y = 94;
    for (i = first; i < hist_count; ++i) {
        rd_text(w, 34, y, hist[i], i == hist_count - 1 ? C_TEXT : C_MUTED);
        y += 18;
        if (y > (int)w->height - 58) break;
    }
    rdui_panel_class(w, 28, (int)w->height - 44, (int)w->width - 56, 28, "search.field");
    rd_text(w, 34, (int)w->height - 34, "ridux $", C_ACCENT);
    rd_text(w, 98, (int)w->height - 34, line, C_TEXT);
}

static __attribute__((unused)) void draw_sidebar_item(rd_window_t *w, int x, int y, const char *icon,
                              const char *label, int active) {
    if (active) {
        rdui_round_gradient_h(w, x - 8, y - 8, 130, 30, 10,
                              rd_rgb(35, 174, 255), rd_rgb(31, 210, 184), 126);
    }
    rd_text(w, x, y, icon, active ? rd_rgb(235, 250, 255) : rd_rgb(170, 213, 222));
    rd_text(w, x + 28, y, label, active ? rd_rgb(246, 252, 255) : rd_rgb(210, 230, 238));
}

static __attribute__((unused)) void draw_folder_icon(rd_window_t *w, int x, int y,
                             rd_u32 top, rd_u32 bottom, const char *mark) {
    rdui_round_gradient_v(w, x + 5, y, 30, 16, 6, top, bottom, 226);
    rdui_round_gradient_v(w, x, y + 9, 64, 48, 12, top, bottom, 244);
    rdui_round_outline(w, x, y + 9, 64, 48, 12, rd_rgb(255, 255, 255), 18);
    if (mark) rdui_text_center(w, x + 16, y + 23, 32, 24, mark, rd_rgb(230, 250, 255), 2);
}

static __attribute__((unused)) void draw_file_row(rd_window_t *w, int x, int y, int ww,
                          const char *name, const char *kind,
                          const char *size, const char *date, rd_u32 color) {
    rdui_round_alpha(w, x, y, ww, 44, 12, rd_rgb(255, 255, 255), 9);
    rdui_fill_alpha(w, x + 2, y + 43, ww - 4, 1, rd_rgb(255, 255, 255), 18);
    rdui_round_gradient_v(w, x + 14, y + 9, 26, 28, 7,
                          rdui_mix(color, rd_rgb(255, 255, 255), 28u),
                          rdui_mix(color, rd_rgb(0, 0, 0), 28u), 235);
    rd_text(w, x + 52, y + 7, name, rd_rgb(246, 251, 255));
    rd_text(w, x + 52, y + 25, kind, rd_rgb(174, 209, 222));
    rdui_text_right(w, x + ww - 178, y + 16, size, rd_rgb(206, 231, 240));
    rdui_text_right(w, x + ww - 28, y + 16, date, rd_rgb(206, 231, 240));
}

static void draw_files(rd_window_t *w) {
    typedef struct file_folder_card {
        const char *label;
        int icon;
        rd_u32 glow;
    } file_folder_card_t;
    typedef struct file_recent_card {
        const char *name;
        const char *kind;
        const char *size;
        const char *date;
        int icon;
        rd_u32 glow;
    } file_recent_card_t;
    static const rdui_nav_item_desc_t nav[] = {
        { "H", "Inicio", 1 },
        { "D", "Escritorio", 0 },
        { "P", "Documentos", 0 },
        { "V", "Descargas", 0 },
        { "M", "Musica", 0 },
        { "I", "Imagenes", 0 },
        { "T", "Papelera", 0 }
    };
    static const file_folder_card_t folders[] = {
        { "Documentos", RIDUX_ICON_FOLDER_DOCUMENTS, 0x3CA2FFu },
        { "Descargas", RIDUX_ICON_FOLDER_DOWNLOADS, 0x31D6B5u },
        { "Musica", RIDUX_ICON_FOLDER_MUSIC, 0xA855F7u },
        { "Imagenes", RIDUX_ICON_FOLDER_PICTURES, 0x22D3EEu },
        { "Videos", RIDUX_ICON_FOLDER_VIDEOS, 0x60A5FAu },
        { "Escritorio", RIDUX_ICON_FOLDER_DESKTOP, 0xFBBF24u }
    };
    static const file_recent_card_t recent[] = {
        { "Project Plan.pdf", "PDF Document", "2.4 MB", "Today, 10:30 AM", RIDUX_ICON_FILE_PDF, 0xF54B4Bu },
        { "riduxos-wallpaper.png", "PNG Image", "3.1 MB", "Today, 9:15 AM", RIDUX_ICON_FILE_IMAGE, 0x55AFFFu },
        { "Budget.xlsx", "Spreadsheet", "24 KB", "Yesterday, 4:22 PM", RIDUX_ICON_FILE_TEXT, 0x42CC67u },
        { "Presentation.pptx", "Presentation", "5.7 MB", "May 10, 2:45 PM", RIDUX_ICON_FILE_TEXT, 0xFF8537u }
    };
    int i;
    int W = (int)w->width;
    int H = (int)w->height;
    int side = 178;
    int panel_x = 10;
    int panel_y = 10;
    int panel_w = W - 20;
    int panel_h = H - 20;
    int cx = panel_x + side + 24;
    int content_w = panel_w - side - 46;
    rd_u32 ink = rd_rgb(24, 34, 48);
    rd_u32 sub = rd_rgb(86, 103, 123);
    rd_u32 line = rd_rgb(48, 84, 112);

    draw_app_backdrop(w, rd_rgb(35, 196, 155), rd_rgb(54, 111, 255));
    rdui_shadow(w, panel_x, panel_y, panel_w, panel_h, 28, 54);
    rdui_round_gradient_v(w, panel_x, panel_y, panel_w, panel_h, 28,
                          rd_rgb(239, 248, 255), rd_rgb(215, 232, 242), 252);
    rdui_round_outline(w, panel_x, panel_y, panel_w, panel_h, 28,
                       rd_rgb(255, 255, 255), 116);
    rdui_round_gradient_v(w, panel_x, panel_y, side, panel_h, 28,
                          rd_rgb(214, 244, 255), rd_rgb(173, 231, 202), 186);
    rdui_fill_alpha(w, panel_x + side, panel_y + 1, 1, panel_h - 2, line, 22);

    draw_icon_scaled(w, RIDUX_ICON_FILES, 28, 24, 40);
    rd_text(w, 76, 36, "Archivos", ink);
    rd_text(w, 30, 88, "Lugares", ink);
    for (i = 0; i < (int)(sizeof(nav) / sizeof(nav[0])); ++i) {
        int row_y = 112 + i * 30;
        if (nav[i].active) {
            rdui_round_gradient_h(w, 30, row_y, 136, 26, 11,
                                  rd_rgb(48, 128, 246), rd_rgb(28, 189, 188), 198);
            rd_text(w, 50, row_y + 5, nav[i].icon, rd_rgb(255, 255, 255));
            rd_text(w, 76, row_y + 5, nav[i].label, rd_rgb(255, 255, 255));
        } else {
            rd_text(w, 50, row_y + 5, nav[i].icon, rd_rgb(70, 108, 132));
            rd_text(w, 76, row_y + 5, nav[i].label, ink);
        }
    }
    rdui_fill_alpha(w, 30, 338, side - 34, 1, line, 24);
    rd_text(w, 30, 360, "Dispositivos", ink);
    rd_text(w, 50, 389, "R", rd_rgb(70, 108, 132));
    rd_text(w, 76, 389, "riduxOS Drive", ink);
    rdui_round_alpha(w, 66, 418, 98, 6, 3, rd_rgb(45, 75, 98), 42);
    rdui_round_alpha(w, 66, 418, 60, 6, 3, rd_rgb(35, 183, 255), 230);

    rdui_toolbar_button(w, rdui_rect(cx, 34, 42, 34), "<", 1);
    rdui_toolbar_button(w, rdui_rect(cx + 48, 34, 42, 34), ">", 0);
    rdui_toolbar_button(w, rdui_rect(cx + 104, 34, 44, 34), "H", 1);
    rdui_toolbar_button(w, rdui_rect(cx + 158, 34, 92, 34), "Home", 1);
    rdui_toolbar_button(w, rdui_rect(cx + content_w - 250, 34, 42, 34), "[]", 1);
    rdui_toolbar_button(w, rdui_rect(cx + content_w - 204, 34, 42, 34), "=", 0);
    rdui_round_alpha(w, cx + content_w - 156, 34, 146, 34, 17,
                     rd_rgb(255, 255, 255), 126);
    rdui_round_outline(w, cx + content_w - 156, 34, 146, 34, 17, line, 22);
    rdui_round_alpha(w, cx + content_w - 140, 47, 8, 8, 4, sub, 190);
    rdui_round_alpha(w, cx + content_w - 132, 55, 7, 2, 1, sub, 190);
    rd_text(w, cx + content_w - 116, 43, "Buscar", sub);

    rd_text(w, cx, 96, "Carpetas", ink);
    for (i = 0; i < (int)(sizeof(folders) / sizeof(folders[0])); ++i) {
        rdui_rect_t tile = rdui_grid_rect(rdui_rect(cx, 126, content_w, 104), 6, 13, i, 104);
        rdui_round_alpha(w, tile.x, tile.y, tile.w, tile.h, 18, rd_rgb(255, 255, 255), 118);
        rdui_round_outline(w, tile.x, tile.y, tile.w, tile.h, 18, line, 18);
        rdui_round_alpha(w, tile.x + 18, tile.y + 14, tile.w - 36, 46, 16,
                         folders[i].glow, 10);
        draw_icon_scaled(w, folders[i].icon, tile.x + (tile.w - 46) / 2, tile.y + 18, 46);
        rdui_text_center(w, tile.x - 8, tile.y + 72, tile.w + 16, 20,
                         folders[i].label, ink, 1);
    }

    rd_text(w, cx, 262, "Recientes", ink);
    rdui_round_alpha(w, cx, 292, content_w, 196, 18, rd_rgb(255, 255, 255), 128);
    rdui_round_outline(w, cx, 292, content_w, 196, 18, line, 16);
    for (i = 0; i < (int)(sizeof(recent) / sizeof(recent[0])); ++i) {
        int row_y = 300 + i * 44;
        rdui_round_alpha(w, cx + 8, row_y, content_w - 16, 40, 13,
                         rd_rgb(255, 255, 255), i == 0 ? 132u : 70u);
        draw_icon_scaled(w, recent[i].icon, cx + 22, row_y + 5, 30);
        rd_text(w, cx + 62, row_y + 5, recent[i].name, ink);
        rd_text(w, cx + 62, row_y + 23, recent[i].kind, sub);
        rdui_text_right(w, cx + content_w - 176, row_y + 15, recent[i].size, sub);
        rdui_text_right(w, cx + content_w - 24, row_y + 15, recent[i].date, sub);
    }
    rdui_fill_alpha(w, cx, H - 42, content_w, 1, line, 20);
    rdui_text_center(w, cx, H - 28, content_w, 18, "6 elementos", sub, 1);
}

static void draw_settings(rd_window_t *w, int toggle) {
    static const rdui_nav_item_desc_t items[] = {
        { "A", "Apariencia", 1 },
        { "E", "Escritorio", 0 },
        { "D", "Dock", 0 },
        { "T", "Tipografias", 0 },
        { "C", "Colores", 0 },
        { "M", "Tema", 0 },
        { "I", "Iconos", 0 },
        { "P", "Cursor", 0 }
    };
    static const rd_u32 accents[7] = {
        0x38BDF8u, 0x8E5BFFu, 0xE84AAAu, 0xF54A4Au,
        0xF5B627u, 0x44C85Au, 0x26CAD5u
    };
    static const char *theme_names[4] = {
        "Dark", "Light", "Blue", "Purple"
    };
    static const rd_u32 theme_colors[4] = {
        0x10223Au, 0xE8EDF3u, 0x1C69E2u, 0x8A23D2u
    };
    int i;
    int W = (int)w->width;
    int H = (int)w->height;
    int side_x = 16;
    int side_y = 72;
    int side_w = 176;
    int content_x = 210;
    int content_y = 72;
    int content_w = W - 230;
    draw_app_backdrop(w, rd_rgb(82, 137, 255), rd_rgb(50, 203, 155));
    chrome(w);
    rdui_material(w, rdui_rect(side_x, side_y, side_w, H - 90),
                  rd_rgb(18, 27, 40), 192, 22, 3);
    draw_icon_scaled(w, RIDUX_ICON_PERSONALIZATION, side_x + 16, side_y + 14, 34);
    rd_text(w, side_x + 58, side_y + 16, "Personalizacion", C_TEXT);
    rd_text(w, side_x + 58, side_y + 34, "RiduxUI", C_MUTED);
    rdui_nav_list(w, rdui_rect(side_x + 10, side_y + 64, side_w - 20, 240),
                  items, (int)(sizeof(items) / sizeof(items[0])));
    rdui_fill_alpha(w, side_x + 16, side_y + 296, side_w - 32, 1,
                    rd_rgb(255, 255, 255), 15);
    rd_text(w, side_x + 14, side_y + 316, "Sistema", C_MUTED);
    rdui_nav_item(w, rdui_rect(side_x + 10, side_y + 340, side_w - 20, 26), "S", "Pantalla", 0);
    rdui_nav_item(w, rdui_rect(side_x + 10, side_y + 370, side_w - 20, 26), "N", "Sonido", 0);

    rdui_material(w, rdui_rect(content_x, content_y, content_w, H - 90),
                  rd_rgb(17, 25, 38), 194, 24, 4);
    draw_icon_scaled(w, RIDUX_ICON_SETTINGS, content_x + 22, content_y + 18, 38);
    rd_text(w, content_x + 70, content_y + 22, "Apariencia", C_TEXT);
    rd_text(w, content_x + 70, content_y + 44, "Tema visual, material y motion del escritorio", C_MUTED);
    for (i = 0; i < 4; ++i) {
        rdui_theme_card(w, rdui_rect(content_x + 22 + i * 92, content_y + 92, 78, 78),
                        theme_names[i], theme_colors[i], i == 0);
    }

    rd_text(w, content_x + 22, content_y + 202, "Acento de color", C_TEXT);
    for (i = 0; i < (int)(sizeof(accents) / sizeof(accents[0])); ++i) {
        rdui_round_alpha(w, content_x + 24 + i * 34, content_y + 230, 22, 22, 11,
                         accents[i], 235);
        if (i == 0) {
            rdui_round_outline(w, content_x + 20 + i * 34, content_y + 226, 30, 30, 15,
                               rd_rgb(255, 255, 255), 90);
        }
    }
    rd_text(w, content_x + 22, content_y + 278, "Modo", C_TEXT);
    rdui_button_class(w, content_x + 22, content_y + 306, 132, 36,
                      toggle & 1 ? "Oscuro" : "Claro", "button.primary", 1);
    rdui_button_class(w, content_x + 172, content_y + 306, 132, 36,
                      toggle & 2 ? "Motion ON" : "Motion OFF", "button.ghost", 0);
    rd_text(w, content_x + 22, content_y + 364, "Transparencia", C_TEXT);
    rdui_slider(w, content_x + 22, content_y + 394, content_w - 70, 40, C_ACCENT);
    rdui_info_row(w, rdui_rect(content_x + 22, content_y + 424, content_w - 44, 34),
                  "Motor UI", "RiduxUI / Flush");
}

static void draw_calc(rd_window_t *w, int value, int pending, char op) {
    int row, col;
    char buf[24];
    const char *labels[20] = {
        "C", "/", "*", "<",
        "7", "8", "9", "-",
        "4", "5", "6", "+",
        "1", "2", "3", "=",
        "0", "0", ".", "="
    };
    int shell_x = 24;
    int shell_y = 14;
    int shell_w = (int)w->width - 48;
    int shell_h = (int)w->height - 28;
    int key_y = shell_y + 144;
    int key_h = (shell_h - 154) / 5;
    int key_w = shell_w / 4;
    rd_u32 grid_top = rd_rgb(43, 174, 192);
    rd_u32 grid_bottom = rd_rgb(121, 198, 78);

    rd_clear(w, rd_rgb(244, 247, 251));
    rdui_round_alpha(w, shell_x + 18, shell_y + 28, shell_w, shell_h, 32,
                     rd_rgb(0, 13, 31), 18);
    rdui_round_alpha(w, shell_x + 8, shell_y + 14, shell_w, shell_h, 28,
                     rd_rgb(0, 13, 31), 18);
    rdui_round_gradient_v(w, shell_x, shell_y, shell_w, shell_h, 24,
                          rd_rgb(29, 101, 218), rd_rgb(20, 184, 196), 255);
    rdui_round_gradient_v(w, shell_x, key_y - 5, shell_w, shell_h - (key_y - shell_y) + 5,
                          22, grid_top, grid_bottom, 218);
    rdui_round_outline(w, shell_x, shell_y, shell_w, shell_h, 24, rd_rgb(255, 255, 255), 22);

    rdui_round_gradient_v(w, shell_x + 16, shell_y + 72, shell_w - 32, 94, 16,
                          rd_rgb(8, 20, 35), rd_rgb(12, 35, 55), 240);
    rdui_round_outline(w, shell_x + 16, shell_y + 72, shell_w - 32, 94, 16,
                       rd_rgb(255, 255, 255), 14);
    utoa_dec(value, buf, sizeof(buf));
    draw_icon_scaled(w, RIDUX_ICON_CALC, shell_x + 28, shell_y + 24, 34);
    rd_text(w, shell_x + 70, shell_y + 34, "Calculator", rd_rgb(230, 244, 255));
    rd_text_scaled(w, shell_x + shell_w - 28 - rd_text_width_scaled(buf, 4), shell_y + 94,
                   4, buf, rd_rgb(248, 252, 255));
    if (op) {
        char obuf[2];
        obuf[0] = op; obuf[1] = 0;
        utoa_dec(pending, buf, sizeof(buf));
        rd_text(w, shell_x + shell_w - 148, shell_y + 142, buf, rd_rgb(151, 178, 199));
        rd_text(w, shell_x + shell_w - 44, shell_y + 142, obuf, rd_rgb(151, 178, 199));
    }

    for (row = 0; row < 5; ++row) {
        for (col = 0; col < 4; ++col) {
            int idx = row * 4 + col;
            int x = shell_x + col * key_w;
            int y = key_y + row * key_h;
            int bw = key_w;
            int bh = key_h;
            rd_u32 cell_top;
            rd_u32 cell_bottom;
            rd_u32 fg = rd_rgb(240, 246, 255);
            if (row == 4 && col == 1) continue;
            if (row == 4 && col == 0) bw = key_w * 2;
            cell_top = rdui_mix(grid_top, grid_bottom, (rd_u32)(row * 46 + col * 8));
            cell_bottom = rdui_mix(grid_top, grid_bottom, (rd_u32)(row * 52 + 44));
            if (labels[idx][0] == '/' || labels[idx][0] == '*' ||
                labels[idx][0] == '-' || labels[idx][0] == '+' ||
                labels[idx][0] == '=' || labels[idx][0] == '<') {
                cell_top = rdui_mix(cell_top, rd_rgb(0, 87, 102), 62u);
                cell_bottom = rdui_mix(cell_bottom, rd_rgb(0, 80, 74), 82u);
            }
            if (labels[idx][0] == 'C') {
                cell_top = rd_rgb(49, 165, 214);
                cell_bottom = rd_rgb(38, 143, 190);
            }
            (void)fg;
            rdui_calc_key(w, rdui_rect(x, y, bw, bh), labels[idx],
                          cell_top, cell_bottom,
                          row == 4 || col == 0 || col == 3);
        }
    }
}

static int calc_apply(int a, int b, char op) {
    if (op == '+') return a + b;
    if (op == '-') return a - b;
    if (op == '*') return a * b;
    if (op == '/' && b != 0) return a / b;
    return b;
}

static void draw_paint_base(rd_window_t *w) {
    chrome(w);
    rd_fill_rect(w, 18, 76, (int)w->width - 36, (int)w->height - 94, 0x08111Fu);
    rd_text(w, 30, 90, "canvas", C_MUTED);
}

static void draw_clock(rd_window_t *w, int ticks) {
    char buf[16];
    chrome(w);
    rd_fill_rect(w, 90, 96, 180, 180, C_PANEL);
    rd_fill_rect(w, 174, 114, 12, 76, C_ACCENT);
    rd_fill_rect(w, 180, 184, 54, 10, C_ACCENT2);
    utoa_dec(ticks, buf, sizeof(buf));
    rd_text_scaled(w, 104, 300, 2, "10:44", C_TEXT);
    rd_text(w, 116, 332, "ring3 ticks:", C_MUTED);
    rd_text(w, 200, 332, buf, C_ACCENT);
}

static void draw_ttt(rd_window_t *w, char cells[9], int turn) {
    int i;
    chrome(w);
    for (i = 0; i < 9; ++i) {
        int x = 70 + (i % 3) * 72;
        int y = 88 + (i / 3) * 72;
        char s[2];
        rd_fill_rect(w, x, y, 62, 62, C_PANEL);
        s[0] = cells[i] ? cells[i] : ' ';
        s[1] = 0;
        rd_text_scaled(w, x + 20, y + 18, 3, s, cells[i] == 'X' ? C_ACCENT : C_ACCENT2);
    }
    rd_text(w, 86, 324, turn ? "turn: O" : "turn: X", C_MUTED);
}

static void draw_kind(rd_window_t *w, int ticks, int mode) {
    char buf[24];
    switch (RIDUX_APP_KIND) {
        case APP_FILES: draw_files(w); break;
        case APP_TASKMGR:
        case APP_PROC:
            chrome(w);
            card(w, 20, 80, (int)w->width - 40, 52, "pid 1", "kernel desktop broker");
            card(w, 20, 144, (int)w->width - 40, 52, "pid 2+", "Ring 3 user apps");
            card(w, 20, 208, (int)w->width - 40, 52, "policy", "apps run outside ring 0");
            break;
        case APP_BROWSER:
        case APP_FIREFOX:
            chrome(w);
            card(w, 22, 86, (int)w->width - 44, 78, "Firefox real binary",
                 "Run `firefox` to launch /opt/firefox/firefox-bin.");
            card(w, 22, 180, (int)w->width - 44, 78, "Display backend",
                 "Wayland preferred, X11 fallback kept for debugging.");
            break;
        case APP_WEATHER:
            chrome(w);
            rd_text_scaled(w, 30, 96, 3, "22 C", C_ACCENT);
            rd_text(w, 34, 140, "Clear sky over Ridux City", C_TEXT);
            rd_text(w, 34, 164, "Wind: 8 km/h    Humidity: 48%", C_MUTED);
            break;
        case APP_STORE:
            chrome(w);
            card(w, 22, 84, 170, 88, "Firefox", "Native Linux ABI");
            card(w, 210, 84, 170, 88, "Notes", "Ring 3 ready");
            card(w, 22, 190, 170, 88, "Paint", "User framebuffer");
            card(w, 210, 190, 170, 88, "Games", "Sandbox toys");
            break;
        case APP_ABOUT:
            chrome(w);
            rdui_glass(w, 24, 78, 222, (int)w->height - 104, rd_rgb(9, 20, 35), 150, 16);
            rdui_round_alpha(w, 96, 114, 62, 62, 18, C_ACCENT, 220);
            rd_text_scaled(w, 112, 128, 3, "R", rd_rgb(244, 250, 255));
            rdui_text_center(w, 24, 190, 222, 34, "RiduxOS", C_TEXT, 2);
            rdui_text_center(w, 24, 226, 222, 18, "1.0 Bloom desktop", C_MUTED, 1);
            rdui_text_center(w, 24, 252, 222, 18, "Una distro unix moderna, ligera", C_TEXT, 1);
            rdui_text_center(w, 24, 272, 222, 18, "y poderosa", C_TEXT, 1);
            rd_text(w, 76, 314, "https://riduxos.dev", C_ACCENT);
            rdui_glass(w, 266, 78, (int)w->width - 290, (int)w->height - 104, rd_rgb(10, 22, 38), 145, 16);
            rd_text(w, 286, 100, "Intro del sistema", C_TEXT);
            rdui_gradient_h(w, 286, 130, (int)w->width - 330, 96, rd_rgb(10, 64, 190), rd_rgb(0, 198, 225));
            rdui_fill_alpha(w, 286, 130, (int)w->width - 330, 96, rd_rgb(0, 0, 0), 42);
            rdui_text_center(w, 286, 130, (int)w->width - 330, 96, "RiduxAboutIntro.mp4", rd_rgb(245, 250, 255), 1);
            rd_text(w, 286, 246, "Nombre del dispositivo", C_MUTED);
            rd_text(w, (int)w->width - 128, 246, "ridux", C_TEXT);
            rd_text(w, 286, 282, "Memoria", C_MUTED);
            rd_text(w, (int)w->width - 128, 282, "15.6 GiB", C_TEXT);
            rd_text(w, 286, 318, "Grafica", C_MUTED);
            rd_text(w, (int)w->width - 128, 318, "Flush GPU path", C_TEXT);
            rd_text(w, 286, 354, "Video", C_MUTED);
            rd_text(w, (int)w->width - 176, 354, "/ridux/videos/about-intro.mp4", C_ACCENT);
            break;
        case APP_MEDIA:
            chrome(w);
            button(w, 54, 110, 80, 48, "<<", C_LINE);
            button(w, 150, 110, 80, 48, "Play", C_ACCENT);
            button(w, 246, 110, 80, 48, ">>", C_LINE);
            rd_fill_rect(w, 54, 190, 272, 8, C_LINE);
            rd_fill_rect(w, 54, 190, 120 + (ticks % 120), 8, C_ACCENT);
            break;
        case APP_MONITOR:
        case APP_SYSINFO:
        case APP_FLUSH:
        case APP_NET:
        case APP_LOG:
            chrome(w);
            utoa_dec(ticks, buf, sizeof(buf));
            card(w, 20, 82, (int)w->width - 40, 64, "SMP", "topology visible to userspace");
            card(w, 20, 158, (int)w->width - 40, 64, "GPU", "backbuffer accelerated compositor path");
            rd_text(w, 34, 248, "sample ticks:", C_MUTED);
            rd_text(w, 118, 248, buf, C_ACCENT);
            break;
        case APP_EDITOR:
        case APP_NOTES:
            chrome(w);
            rd_fill_rect(w, 24, 82, (int)w->width - 48, (int)w->height - 110, 0x08111Fu);
            rd_text(w, 38, 100, "Write ideas here. Keyboard is routed to Ring 3.", C_TEXT);
            break;
        case APP_MINE:
            chrome(w);
            rd_text(w, 34, 82, "safe tiles are blue; mines are amber", C_MUTED);
            for (mode = 0; mode < 25; ++mode) {
                int x = 48 + (mode % 5) * 46;
                int y = 112 + (mode / 5) * 38;
                rd_fill_rect(w, x, y, 34, 28, (mode % 7) ? C_PANEL2 : C_WARN);
            }
            break;
        case APP_SNAKE:
            chrome(w);
            rd_fill_rect(w, 28, 84, (int)w->width - 56, 210, 0x08111Fu);
            rd_fill_rect(w, 70 + (ticks % 180), 160, 18, 18, C_ACCENT2);
            rd_fill_rect(w, 230, 130, 12, 12, C_WARN);
            break;
        default:
            draw_generic(w, ticks);
            break;
    }
}

int main(void) {
    rd_window_t win;
    rd_event_t ev[16];
    int rc, n, i;
    int running = 1;
    int ticks = 0;
    int dirty = 1;
    int settings = 0;
    int calc_value = 0;
    int calc_pending = 0;
    char calc_op = 0;
    char term_line[48];
    int term_len = 0;
    char term_hist[TERM_ROWS][TERM_COLS];
    int term_hist_count = 0;
    char shell_out[768];
    char ttt[9];
    int ttt_turn = 0;
    int ww = 460, wh = 340;

    for (i = 0; i < 48; ++i) term_line[i] = 0;
    for (i = 0; i < TERM_ROWS; ++i) term_hist[i][0] = 0;
    shell_out[0] = 0;
    term_push_line(term_hist, &term_hist_count, "Ring 3 terminal lista. Escribi firefox y Enter.");
    for (i = 0; i < 9; ++i) ttt[i] = 0;

    if (RIDUX_APP_KIND == APP_CALC) { ww = 380; wh = 560; }
    else if (RIDUX_APP_KIND == APP_SETTINGS) { ww = 620; wh = 460; }
    else if (RIDUX_APP_KIND == APP_ABOUT) { ww = 640; wh = 430; }
    else if (RIDUX_APP_KIND == APP_PAINT) { ww = 560; wh = 380; }
    else if (RIDUX_APP_KIND == APP_FILES) { ww = 760; wh = 520; }
    else if (RIDUX_APP_KIND == APP_TERMINAL) { ww = 640; wh = 360; }

    rc = rd_window_open(app_title(), (rd_u32)ww, (rd_u32)wh, &win);
    if (rc < 0) return 1;

    if (RIDUX_APP_KIND == APP_PAINT) {
        draw_paint_base(&win);
        rd_window_present(&win, 0, 0, ww, wh);
        dirty = 0;
    }

    while (running) {
        if (dirty) {
            if (RIDUX_APP_KIND == APP_SETTINGS) draw_settings(&win, settings);
            else if (RIDUX_APP_KIND == APP_CALC) draw_calc(&win, calc_value, calc_pending, calc_op);
            else if (RIDUX_APP_KIND == APP_CLOCK) draw_clock(&win, ticks);
            else if (RIDUX_APP_KIND == APP_TERMINAL) draw_terminal(&win, term_line, term_hist, term_hist_count);
            else if (RIDUX_APP_KIND == APP_TTT) draw_ttt(&win, ttt, ttt_turn);
            else draw_kind(&win, ticks, 0);
            rd_window_present(&win, 0, 0, ww, wh);
            dirty = 0;
        }

        n = rd_window_poll(&win, ev, 16);
        for (i = 0; i < n; ++i) {
            if (ev[i].type == RIDUX_EVENT_CLOSE) running = 0;
            if (ev[i].type == RIDUX_EVENT_KEY_DOWN) {
                if (ev[i].scancode == 1u) { running = 0; continue; }
                if (RIDUX_APP_KIND == APP_TERMINAL) {
                    if (ev[i].scancode == 14u) {
                        if (term_len > 0) term_line[--term_len] = 0;
                        dirty = 1;
                    } else if (ev[i].scancode == 28u) {
                        if (term_len > 0) {
                            int rc2;
                            char prompt[64];
                            int p = 0, q;
                            prompt[p++] = '$'; prompt[p++] = ' ';
                            for (q = 0; term_line[q] && p + 1 < (int)sizeof(prompt); ++q) {
                                prompt[p++] = term_line[q];
                            }
                            prompt[p] = 0;
                            term_push_line(term_hist, &term_hist_count, prompt);
                            shell_out[0] = 0;
                            rc2 = rd_shell_exec(term_line, shell_out, (rd_u32)sizeof(shell_out));
                            if (rc2 < 0) term_push_line(term_hist, &term_hist_count, "shell: syscall fallo");
                            else         term_push_multiline(term_hist, &term_hist_count, shell_out);
                            term_len = 0;
                            term_line[0] = 0;
                        }
                        dirty = 1;
                    } else if (ev[i].key >= 32 && ev[i].key < 127 && term_len < 46) {
                        term_line[term_len++] = (char)ev[i].key;
                        term_line[term_len] = 0;
                        dirty = 1;
                    }
                } else if (ev[i].key >= 32 && ev[i].key < 127) {
                    dirty = 1;
                }
            }
            if (ev[i].type == RIDUX_EVENT_MOUSE_DOWN) {
                int mx = ev[i].x, my = ev[i].y;
                if (RIDUX_APP_KIND == APP_SETTINGS) {
                    if (in_rect(mx, my, 232, 364, 132, 36)) settings ^= 1;
                    if (in_rect(mx, my, 382, 364, 132, 36)) settings ^= 2;
                    dirty = 1;
                } else if (RIDUX_APP_KIND == APP_CALC) {
                    int shell_x = 24;
                    int shell_y = 14;
                    int shell_w = ww - 48;
                    int shell_h = wh - 28;
                    int key_y = shell_y + 144;
                    int key_h = (shell_h - 154) / 5;
                    int key_w = shell_w / 4;
                    int col = (mx - shell_x) / key_w;
                    int row = (my - key_y) / key_h;
                    if (mx >= shell_x && my >= key_y && col >= 0 && col < 4 && row >= 0 && row < 5) {
                        const char keys[20] = {
                            'C','/','*','<',
                            '7','8','9','-',
                            '4','5','6','+',
                            '1','2','3','=',
                            '0','0','.','='
                        };
                        char k;
                        if (row == 4 && col == 1) col = 0;
                        k = keys[row * 4 + col];
                        if (k >= '0' && k <= '9') calc_value = calc_value * 10 + (k - '0');
                        else if (k == '<') calc_value /= 10;
                        else if (k == '.') { }
                        else if (k == 'C') { calc_value = 0; calc_pending = 0; calc_op = 0; }
                        else if (k == '=') { calc_value = calc_apply(calc_pending, calc_value, calc_op); calc_op = 0; }
                        else { calc_pending = calc_value; calc_value = 0; calc_op = k; }
                        dirty = 1;
                    }
                } else if (RIDUX_APP_KIND == APP_PAINT) {
                    rd_u32 c = (rnd() & 1u) ? C_ACCENT : C_ACCENT2;
                    rd_fill_rect(&win, mx - 5, my - 5, 10, 10, c);
                    rd_window_present(&win, mx - 8, my - 8, 16, 16);
                } else if (RIDUX_APP_KIND == APP_TTT) {
                    int col = (mx - 70) / 72;
                    int row = (my - 88) / 72;
                    int idx = row * 3 + col;
                    if (col >= 0 && col < 3 && row >= 0 && row < 3 && idx >= 0 && idx < 9 && !ttt[idx]) {
                        ttt[idx] = ttt_turn ? 'O' : 'X';
                        ttt_turn ^= 1;
                        dirty = 1;
                    }
                }
            }
        }
        ++ticks;
        if ((RIDUX_APP_KIND == APP_CLOCK || RIDUX_APP_KIND == APP_SNAKE) && (ticks % 12) == 0) dirty = 1;
        rd_sleep_ticks(1);
    }

    rd_window_close(&win);
    return 0;
}
