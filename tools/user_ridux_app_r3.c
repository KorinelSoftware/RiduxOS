/*
 * Pack de apps Ring 3 de Ridux.
 *
 * Es el mismo archivo compilado varias veces con RIDUX_APP_KIND distinto.
 * Asi no tengo que duplicar medio escritorio, pero cada tile del launcher
 * termina corriendo como proceso de usuario de verdad.
 */
#include "user_libridux.h"

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

static const char *app_subtitle(void) {
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
    rd_fill_rect(w, x, y, bw, bh, C_PANEL2);
    rd_fill_rect(w, x, y, bw, 2, color);
    rd_fill_rect(w, x, y + bh - 1, bw, 1, C_LINE);
    rd_fill_rect(w, x, y, 1, bh, C_LINE);
    rd_fill_rect(w, x + bw - 1, y, 1, bh, C_LINE);
    rd_text(w, x + 10, y + bh / 2 - 4, label, C_TEXT);
}

static void chrome(rd_window_t *w) {
    rd_clear(w, C_BG);
    rd_fill_rect(w, 0, 0, (int)w->width, 56, C_PANEL);
    rd_fill_rect(w, 0, 0, (int)w->width, 4, C_ACCENT);
    rd_text_scaled(w, 18, 16, 2, app_title(), C_TEXT);
    rd_text(w, 18, 42, app_subtitle(), C_MUTED);
}

static void card(rd_window_t *w, int x, int y, int ww, int hh,
                 const char *title, const char *body) {
    rd_fill_rect(w, x, y, ww, hh, C_PANEL);
    rd_fill_rect(w, x, y, ww, 2, C_ACCENT);
    rd_text(w, x + 12, y + 12, title, C_TEXT);
    if (body) rd_text(w, x + 12, y + 32, body, C_MUTED);
}

static void draw_generic(rd_window_t *w, int ticks) {
    char n[16];
    chrome(w);
    card(w, 18, 78, (int)w->width - 36, 84, "Ring 3 status",
         "This window is rendered by a user-mode ELF.");
    card(w, 18, 178, (int)w->width - 36, 84, "Backend",
         "Ridux native window protocol, compositor-safe.");
    utoa_dec(ticks, n, sizeof(n));
    rd_text(w, 30, (int)w->height - 28, "frames:", C_MUTED);
    rd_text(w, 82, (int)w->height - 28, n, C_ACCENT);
}

static void draw_terminal(rd_window_t *w, const char *line,
                          char hist[TERM_ROWS][TERM_COLS], int hist_count) {
    int i;
    int y;
    int first = hist_count > 8 ? hist_count - 8 : 0;
    chrome(w);
    rd_fill_rect(w, 18, 78, (int)w->width - 36, (int)w->height - 96, 0x0A101Cu);
    y = 94;
    for (i = first; i < hist_count; ++i) {
        rd_text(w, 34, y, hist[i], i == hist_count - 1 ? C_TEXT : C_MUTED);
        y += 18;
        if (y > (int)w->height - 58) break;
    }
    rd_fill_rect(w, 28, (int)w->height - 42, (int)w->width - 56, 24, 0x08111Fu);
    rd_text(w, 34, (int)w->height - 34, "ridux $", C_ACCENT);
    rd_text(w, 98, (int)w->height - 34, line, C_TEXT);
}

static void draw_files(rd_window_t *w) {
    chrome(w);
    card(w, 18, 76, 180, 210, "Places", "/\n/bin\n/home\n/tmp");
    card(w, 214, 76, (int)w->width - 232, 210, "Files",
         "firefox.elf\nnotes-r3.elf\ncalculator-r3.elf\nsettings-r3.elf");
}

static void draw_settings(rd_window_t *w, int toggle) {
    chrome(w);
    button(w, 30, 86, 190, 42, toggle & 1 ? "Accent: cyan" : "Accent: green", C_ACCENT);
    button(w, 30, 140, 190, 42, toggle & 2 ? "Motion: calm" : "Motion: lively", C_ACCENT2);
    button(w, 30, 194, 190, 42, toggle & 4 ? "Navbar: compact" : "Navbar: full", C_WARN);
    card(w, 250, 86, (int)w->width - 280, 150, "Note",
         "Settings are sandboxed here; kernel theme hook is next.");
}

static void draw_calc(rd_window_t *w, int value, int pending, char op) {
    int i;
    char buf[24];
    const char *labels[16] = {
        "7","8","9","/",
        "4","5","6","*",
        "1","2","3","-",
        "C","0","=","+"
    };
    chrome(w);
    rd_fill_rect(w, 24, 76, (int)w->width - 48, 56, 0x08111Fu);
    utoa_dec(value, buf, sizeof(buf));
    rd_text_scaled(w, 38, 92, 2, buf, C_TEXT);
    if (op) {
        char obuf[2];
        obuf[0] = op; obuf[1] = 0;
        rd_text_scaled(w, (int)w->width - 74, 92, 2, obuf, C_ACCENT);
        utoa_dec(pending, buf, sizeof(buf));
        rd_text(w, (int)w->width - 150, 118, buf, C_MUTED);
    }
    for (i = 0; i < 16; ++i) {
        int col = i % 4;
        int row = i / 4;
        button(w, 24 + col * 72, 150 + row * 54, 62, 44, labels[i],
               (i % 4 == 3 || i == 14) ? C_ACCENT : C_LINE);
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
            rd_text_scaled(w, 34, 100, 2, "RiduxOS Unix", C_TEXT);
            rd_text(w, 38, 136, "Bloom desktop, Linux ABI, Ring 3 apps.", C_MUTED);
            rd_text(w, 38, 158, "This card itself is not kernel UI anymore.", C_ACCENT);
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

    if (RIDUX_APP_KIND == APP_CALC) { ww = 340; wh = 400; }
    else if (RIDUX_APP_KIND == APP_PAINT) { ww = 560; wh = 380; }
    else if (RIDUX_APP_KIND == APP_FILES) { ww = 620; wh = 360; }
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
                    if (in_rect(mx, my, 30, 86, 190, 42)) settings ^= 1;
                    if (in_rect(mx, my, 30, 140, 190, 42)) settings ^= 2;
                    if (in_rect(mx, my, 30, 194, 190, 42)) settings ^= 4;
                    dirty = 1;
                } else if (RIDUX_APP_KIND == APP_CALC) {
                    int col = (mx - 24) / 72;
                    int row = (my - 150) / 54;
                    if (col >= 0 && col < 4 && row >= 0 && row < 4) {
                        const char keys[16] = {'7','8','9','/','4','5','6','*','1','2','3','-','C','0','=','+'};
                        char k = keys[row * 4 + col];
                        if (k >= '0' && k <= '9') calc_value = calc_value * 10 + (k - '0');
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
