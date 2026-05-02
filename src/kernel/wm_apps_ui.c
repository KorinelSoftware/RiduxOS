/* Window manager (Windows 11 rounded chrome + controls) */

/* App IDs (contiguous, used to index g_apps lookups). */
enum {
    APP_EDITOR = 100,      /* addressed separately since we reference it from shell */
    APP_MINESWEEPER,
    APP_SNAKE,
    APP_LOGVIEWER,
    APP_NETMON,
    APP_PROCTREE,
    APP_IMAGE_VIEWER,
    APP_PIANO,
    APP_SYSINFO,
    APP_TICTACTOE,
};
enum {
    APP_MONITOR = 1,
    APP_FILES,
    APP_TERMINAL,
    APP_NOTES,
    APP_FLUSH,
    APP_SETTINGS,
    APP_CALC,
    APP_CLOCK,
    APP_PAINT,
    APP_TASKMGR,
    APP_BROWSER,
    APP_WEATHER,
    APP_STORE,
    APP_ABOUT,
    APP_MEDIA,
    APP_FIREFOX,
    APP_RING3,
    /* Window backed by a Ring 3 user-mode ELF process via the Ridux
     * native WM protocol (src/ridux_r3wm.h). The window's body pixels
     * come from a shared framebuffer the user task writes into; the
     * kernel WM blits that buffer instead of calling a draw callback. */
    APP_RING3_BACKED,
    APP_KERNEL_CONSOLE
};

#define WIN_TITLE_H   42
#define WIN_RADIUS    14
#define WIN_CTRL_W    52
#define WINF_BORDERLESS 0x00000001u
#define WINF_DESKTOP    0x00000002u
#define WINF_NO_FOCUS   0x00000004u
#define WINF_NO_TASKBAR 0x00000008u

static const theme_t *current_theme(void) {
    return &g_theme_table[g_theme_index % (uint32_t)THEME_COUNT];
}

static ui_rect_t window_body_rect(const window_t *w) {
    ui_rect_t r;
    if (w->flags & WINF_BORDERLESS) {
        r.x = w->x;
        r.y = w->y;
        r.w = w->w;
        r.h = w->h;
        return r;
    }
    r.x = w->x + 1;
    r.y = w->y + WIN_TITLE_H;
    r.w = w->w - 2;
    r.h = w->h - WIN_TITLE_H - 1;
    return r;
}
static ui_rect_t window_title_rect(const window_t *w) {
    ui_rect_t r;
    r.x = w->x;
    r.y = w->y;
    r.w = w->w;
    r.h = WIN_TITLE_H;
    return r;
}
static ui_rect_t window_ctrl_rect(const window_t *w, int which /* 0=min, 1=max, 2=close */) {
    ui_rect_t r;
    r.y = w->y;
    r.h = WIN_TITLE_H;
    r.w = WIN_CTRL_W;
    r.x = w->x + w->w - WIN_CTRL_W * (3 - which);
    return r;
}

static window_t *wm_get_by_id(int id) {
    int i;
    for (i = 0; i < g_window_count; ++i)
        if (g_windows[i].used && g_windows[i].id == id) return &g_windows[i];
    return NULL;
}
static int wm_index_by_id(int id) {
    int i;
    for (i = 0; i < g_window_count; ++i)
        if (g_windows[i].used && g_windows[i].id == id) return i;
    return -1;
}
static bool wm_is_visible_by_id(int id) {
    window_t *w = wm_get_by_id(id);
    return w && w->visible && !w->minimized;
}
static void wm_clamp(window_t *w) {
    int min_x = g_ui.desktop.x;
    int min_y = g_ui.desktop.y;
    int max_x = g_ui.desktop.x + g_ui.desktop.w - w->w;
    int max_y = g_ui.desktop.y + g_ui.desktop.h - w->h;
    if (max_x < min_x) max_x = min_x;
    if (max_y < min_y) max_y = min_y;
    w->x = k_clamp_i(w->x, min_x, max_x);
    w->y = k_clamp_i(w->y, min_y, max_y);
}
static void wm_raise_to_top(int index) {
    window_t tmp;
    int i;
    if (index < 0 || index >= g_window_count) return;
    if (index == g_window_count - 1) {
        if (g_windows[index].visible) g_window_focus = index;
        return;
    }
    tmp = g_windows[index];
    for (i = index; i < g_window_count - 1; ++i) g_windows[i] = g_windows[i + 1];
    g_windows[g_window_count - 1] = tmp;
    if (g_windows[g_window_count - 1].visible) g_window_focus = g_window_count - 1;
}
static void wm_focus_last_visible(void) {
    int i;
    for (i = g_window_count - 1; i >= 0; --i)
        if (g_windows[i].used && g_windows[i].visible && !g_windows[i].minimized &&
            !(g_windows[i].flags & WINF_NO_FOCUS)) {
            g_window_focus = i; return;
        }
    g_window_focus = -1;
}
static int wm_add_window(const char *title, int app_id, bool visible,
                         int x, int y, int w, int h, uint8_t alpha) {
    window_t *win;
    int i;
    if (g_window_count >= WINDOW_MAX) return -1;
    i = g_window_count++;
    win = &g_windows[i];
    k_memset(win, 0, sizeof(*win));
    win->used = true;
    win->id = g_next_window_id++;
    win->app_id = app_id;
    win->visible = visible;
    win->minimized = false;
    win->maximized = false;
    win->x = x; win->y = y; win->w = w; win->h = h;
    win->saved_x = x; win->saved_y = y; win->saved_w = w; win->saved_h = h;
    win->alpha = alpha;
    k_strlcpy(win->title, title, sizeof(win->title));
    wm_clamp(win);
    return win->id;
}
static bool wm_set_visible_by_id(int id, bool visible) {
    window_t *w = wm_get_by_id(id);
    if (!w) return false;
    w->visible = visible;
    if (visible) w->minimized = false;
    if (!visible && g_window_focus >= 0 && g_window_focus < g_window_count &&
        g_windows[g_window_focus].id == id) wm_focus_last_visible();
    return true;
}
static bool wm_focus_by_id(int id) {
    int idx = wm_index_by_id(id);
    if (idx < 0) return false;
    if (g_windows[idx].flags & WINF_NO_FOCUS) return false;
    g_windows[idx].visible = true;
    g_windows[idx].minimized = false;
    wm_raise_to_top(idx);
    return true;
}
static void wm_focus_next(void) {
    int i;
    if (g_window_count <= 0) return;
    if (g_window_focus < 0 || g_window_focus >= g_window_count) { wm_focus_last_visible(); return; }
    for (i = 1; i <= g_window_count; ++i) {
        int idx = (g_window_focus + i) % g_window_count;
        if (g_windows[idx].used && g_windows[idx].visible && !g_windows[idx].minimized &&
            !(g_windows[idx].flags & WINF_NO_FOCUS)) {
            wm_raise_to_top(idx);
            return;
        }
    }
}
static bool wm_move_focused(int dx, int dy) {
    window_t *w;
    if (g_window_focus < 0 || g_window_focus >= g_window_count) return false;
    w = &g_windows[g_window_focus];
    if (!w->visible) return false;
    w->x += dx; w->y += dy;
    wm_clamp(w);
    return true;
}
static int wm_find_window_at(int x, int y, bool title_only) {
    int i;
    for (i = g_window_count - 1; i >= 0; --i) {
        const window_t *w = &g_windows[i];
        if (!w->used || !w->visible || w->minimized) continue;
        if (w->flags & WINF_NO_FOCUS) continue;
        if (x >= w->x && x < w->x + w->w && y >= w->y && y < w->y + w->h) {
            if (!title_only || y < w->y + WIN_TITLE_H) return i;
        }
    }
    return -1;
}
static bool wm_has_visible_ring3_desktop(void) {
    int i;
    for (i = 0; i < g_window_count; ++i) {
        const window_t *w = &g_windows[i];
        if (!w->used || !w->visible || w->minimized) continue;
        if ((w->flags & WINF_DESKTOP) && w->app_id == APP_RING3_BACKED) return true;
    }
    return false;
}

static void wm_maximize_toggle(int id) {
    window_t *w = wm_get_by_id(id);
    if (!w) return;
    if (!w->maximized) {
        w->saved_x = w->x; w->saved_y = w->y;
        w->saved_w = w->w; w->saved_h = w->h;
        w->x = g_ui.desktop.x;
        w->y = g_ui.desktop.y;
        w->w = g_ui.desktop.w;
        w->h = g_ui.desktop.h;
        w->maximized = true;
    } else {
        w->x = w->saved_x; w->y = w->saved_y;
        w->w = w->saved_w; w->h = w->saved_h;
        w->maximized = false;
    }
}
static void wm_minimize(int id) {
    window_t *w = wm_get_by_id(id);
    if (!w) return;
    w->minimized = true;
    wm_focus_last_visible();
}
static void wm_close(int id) {
    window_t *w = wm_get_by_id(id);
    if (!w) return;
    w->visible = false;
    w->minimized = false;
    wm_focus_last_visible();
}

static void wm_set_line(window_t *win, int i, const char *t) {
    if (!win || i < 0 || i >= WINDOW_LINE_COUNT) return;
    k_strlcpy(win->lines[i], t, WINDOW_LINE_LEN);
}
static void wm_clear_lines(window_t *win) {
    int i;
    if (!win) return;
    for (i = 0; i < WINDOW_LINE_COUNT; ++i) win->lines[i][0] = 0;
}

/* App registry */
static int app_index_by_id(int app_id) {
    int i;
    for (i = 0; i < g_app_count; ++i) if (g_apps[i].app_id == app_id) return i;
    return -1;
}
static int app_index_by_name(const char *name) {
    int i;
    int browser_idx = -1;
    int firefox_idx = -1;
    int flush_idx = -1;
    if (!name || !name[0]) return -1;
    for (i = 0; i < g_app_count; ++i) {
        if (k_strcmp(g_apps[i].name, name) == 0 ||
            k_strcasecmp_ascii(g_apps[i].name, name) == 0) return i;
        if (k_strcmp(g_apps[i].name, "Browser") == 0) browser_idx = i;
        else if (k_strcmp(g_apps[i].name, "Firefox") == 0) firefox_idx = i;
        else if (k_strcmp(g_apps[i].name, "Flush") == 0) flush_idx = i;
    }
    if (k_strcasecmp_ascii(name, "chrome") == 0 ||
        k_strcasecmp_ascii(name, "chromium") == 0 ||
        k_strcasecmp_ascii(name, "seage") == 0 ||
        k_strcasecmp_ascii(name, "web") == 0) {
        if (browser_idx >= 0) return browser_idx;
        if (firefox_idx >= 0) return firefox_idx;
    }
    if (k_strcasecmp_ascii(name, "flush inspector") == 0 && flush_idx >= 0) return flush_idx;
    if (k_strcasecmp_ascii(name, "ridux files") == 0) return app_index_by_name("Files");
    if (k_strcasecmp_ascii(name, "about ridux") == 0) return app_index_by_name("About");
    if (k_strcasecmp_ascii(name, "system monitor") == 0) return app_index_by_name("Monitor");
    return -1;
}

static bool app_focus_existing_instance(int idx) {
    int id;
    if (idx < 0 || idx >= g_app_count) return false;
    id = g_apps[idx].window_id;
    if (id <= 0) return false;
    if (wm_index_by_id(id) < 0) {
        g_apps[idx].window_id = 0;
        g_app_launch_count[idx] = 0;
        return false;
    }
    wm_set_visible_by_id(id, true);
    wm_focus_by_id(id);
    g_needs_redraw = true;
    return true;
}

static const char *app_user_path_for_id(int app_id) {
    switch (app_id) {
        case APP_MONITOR: return "/bin/monitor-r3.elf";
        case APP_FILES: return "/bin/files-r3.elf";
        case APP_TERMINAL: return "/bin/terminal-r3.elf";
        case APP_NOTES: return "/bin/notes-r3.elf";
        case APP_FLUSH: return "/bin/flush-r3.elf";
        case APP_SETTINGS: return "/bin/settings-r3.elf";
        case APP_CALC: return "/bin/calculator-r3.elf";
        case APP_CLOCK: return "/bin/clock-r3.elf";
        case APP_PAINT: return "/bin/paint-r3.elf";
        case APP_TASKMGR: return "/bin/taskmgr-r3.elf";
        case APP_BROWSER: return "/bin/browser-r3.elf";
        case APP_WEATHER: return "/bin/weather-r3.elf";
        case APP_STORE: return "/bin/store-r3.elf";
        case APP_ABOUT: return "/bin/about-r3.elf";
        case APP_MEDIA: return "/bin/media-r3.elf";
        case APP_EDITOR: return "/bin/editor-r3.elf";
        case APP_MINESWEEPER: return "/bin/minesweeper-r3.elf";
        case APP_SNAKE: return "/bin/snake-r3.elf";
        case APP_LOGVIEWER: return "/bin/logviewer-r3.elf";
        case APP_NETMON: return "/bin/network-r3.elf";
        case APP_PROCTREE: return "/bin/processes-r3.elf";
        case APP_SYSINFO: return "/bin/sysinfo-r3.elf";
        case APP_TICTACTOE: return "/bin/tictactoe-r3.elf";
        case APP_RING3: return "/bin/ring3demo-r3.elf";
        default: return NULL;
    }
}

static bool app_launch_firefox_real(void) {
    static const char *const paths[] = {
        "/opt/firefox/firefox-bin",
        "/opt/firefox/firefox",
        "/usr/lib/firefox-esr/firefox-esr",
        "/usr/lib/firefox/firefox"
    };
    int i;
    for (i = 0; i < (int)(sizeof(paths) / sizeof(paths[0])); ++i) {
        char args[320];
        char out[768];
        size_t len = 0;
        args[0] = 0;
        k_append_str(args, &len, sizeof(args), "run ");
        k_append_str(args, &len, sizeof(args), paths[i]);
        out[0] = 0;
        __boot_serial_puts("[app-launch-firefox] ");
        __boot_serial_puts(paths[i]);
        __boot_serial_puts("\n");
        if (!compat_shell_dispatch("browser", args, out, (int)sizeof(out))) return false;
        if (k_contains(out, "no existe")) continue;
        if (k_contains(out, "task creado") ||
            k_contains(out, "task staged") ||
            k_contains(out, "estado: task creado") ||
            k_contains(out, "estado: task staged")) return true;
    }
    return false;
}

static bool app_launch_user_process_for_id(int app_id) {
    const char *path;
    char detail[192];
    int rc;
    int idx = app_index_by_id(app_id);

    if (app_focus_existing_instance(idx)) return true;

    if (app_id == APP_FIREFOX) {
        if (app_launch_firefox_real()) {
            if (idx >= 0) ++g_app_launch_count[idx];
            return true;
        }
        path = "/bin/firefox-ui-r3.elf";
    } else {
        path = app_user_path_for_id(app_id);
    }

    if (!path) return false;
    detail[0] = 0;
    g_kernel_preempt_disable++;
    rc = compat5_spawn_user_elf_background(path, detail, sizeof(detail));
    if (g_kernel_preempt_disable) --g_kernel_preempt_disable;
    __boot_serial_puts("[app-launch-r3] ");
    __boot_serial_puts(path);
    __boot_serial_puts(rc >= 0 ? " ok\n" : " fail\n");
    if (rc >= 0 && idx >= 0) ++g_app_launch_count[idx];
    return rc >= 0;
}
static app_t *app_by_window(const window_t *w) {
    int idx = app_index_by_id(w->app_id);
    if (idx < 0) return NULL;
    return &g_apps[idx];
}
static bool app_is_running(int app_id) {
    int idx = app_index_by_id(app_id);
    if (idx < 0) return false;
    if (g_apps[idx].window_id > 0 && wm_index_by_id(g_apps[idx].window_id) >= 0) return true;
    if (g_apps[idx].window_id > 0) {
        g_apps[idx].window_id = 0;
        g_app_launch_count[idx] = 0;
    }
    return g_app_launch_count[idx] > 0;
}

void compat_ui_open_app(const char *name) {
    int idx, id;
    if (!name || !name[0]) return;
    idx = app_index_by_name(name);
    if (idx < 0) idx = app_index_by_name("Firefox");
    if (idx < 0) idx = app_index_by_name("Browser");
    if (idx < 0) return;
    if (app_launch_user_process_for_id(g_apps[idx].app_id)) {
        g_start_open = false;
        g_quick_open = false;
        return;
    }
    id = g_apps[idx].window_id;
    if (id <= 0) return;
    wm_set_visible_by_id(id, true);
    wm_focus_by_id(id);
    g_start_open = false;
    g_quick_open = false;
    g_needs_redraw = true;
    /* Force an immediate composite. Some callers (e.g. compat5
     * 'browser realrun' path) end up calling task_launch_to_user()
     * right after this, which iretq's into ring 3 and never returns
     * to the kernel main loop. Without this synchronous render, the
     * native browser window would stay invisible until the user task
     * exits or the kernel re-enters the scheduler, which can take
     * an unpredictable amount of time and feels like a freeze. */
    if (g_fb.ready && !g_panic_active) {
        render_scene();
    }
}

/* UI layout (Windows 10 style taskbar) */
static void ui_build_layout(void) {
    int width  = (int)g_fb.width;
    int height = (int)g_fb.height;

    g_ui.screen.x = 0; g_ui.screen.y = 0;
    g_ui.screen.w = width; g_ui.screen.h = height;

    g_ui.margin       = k_clamp_i(width / 52, 12, 28);
    g_ui.gap          = k_clamp_i(width / 96, 10, 18);
    g_ui.radius       = WIN_RADIUS;
    g_ui.line_h       = 20;
    g_ui.title_h      = WIN_TITLE_H;
    g_ui.window_pad   = 14;
    g_ui.taskbar_h    = k_clamp_i(height / 18, 44, 58);
    g_ui.taskbar_icon = k_clamp_i(width / 48, 30, 42);
    g_ui.taskbar_gap  = k_clamp_i(width / 220, 5, 10);

    g_ui.taskbar.x = 0;
    g_ui.taskbar.y = height - g_ui.taskbar_h;
    g_ui.taskbar.w = width;
    g_ui.taskbar.h = g_ui.taskbar_h;

    g_ui.start_btn.w = 52;
    g_ui.start_btn.h = g_ui.taskbar.h - 8;
    g_ui.start_btn.x = g_ui.taskbar.x + 6;
    g_ui.start_btn.y = g_ui.taskbar.y + 4;

    g_ui.clock_btn.w = 132;
    g_ui.clock_btn.h = g_ui.taskbar.h - 8;
    g_ui.clock_btn.x = g_ui.taskbar.x + g_ui.taskbar.w - g_ui.clock_btn.w - 8;
    g_ui.clock_btn.y = g_ui.taskbar.y + 4;

    g_ui.quick_btn.w = 146;
    g_ui.quick_btn.h = g_ui.taskbar.h - 8;
    g_ui.quick_btn.x = g_ui.clock_btn.x - g_ui.quick_btn.w - 4;
    g_ui.quick_btn.y = g_ui.taskbar.y + 4;

    g_ui.tray = g_ui.quick_btn;

    g_ui.desktop.x = 0;
    g_ui.desktop.y = 0;
    g_ui.desktop.w = width;
    g_ui.desktop.h = g_ui.taskbar.y - 4;
    if (g_ui.desktop.h < 200) g_ui.desktop.h = 200;
}

/* Window chrome rendering */

static void render_window_controls(const window_t *w, bool focused) {
    const theme_t *th = current_theme();
    int i;
    uint32_t close_hover = th->danger;
    uint32_t hover = rgb_hex(th->dark ? 0x2A3649 : 0xC0CDE0);
    bool mouse_in_title = (g_mouse_x >= w->x && g_mouse_x < w->x + w->w &&
                           g_mouse_y >= w->y && g_mouse_y < w->y + WIN_TITLE_H);
    for (i = 0; i < 3; ++i) {
        ui_rect_t r = window_ctrl_rect(w, i);
        bool hovered = mouse_in_title &&
                       g_mouse_x >= r.x && g_mouse_x < r.x + r.w &&
                       g_mouse_y >= r.y && g_mouse_y < r.y + r.h;
        if (hovered) {
            uint32_t hc = (i == 2) ? close_hover : hover;
            uint8_t  ha = (i == 2) ? 235 : 150;
            int rr = (i == 2) ? 9 : 7;
            if (i == 2) {
                /* Close button: top-right matches the window's rounded
                 * corner so the red pill never pokes past the curve. */
                flush_round_rect4(r.x + 1, r.y + 1, r.w - 2, r.h - 2,
                                  rr, WIN_RADIUS - 1, rr, rr, hc, ha);
            } else {
                flush_round_rect(r.x + 1, r.y + 1, r.w - 2, r.h - 2, rr, hc, ha);
            }
        }
        /* Icon glyph: draw symbol via lines. */
        {
            int cx = r.x + r.w / 2;
            int cy = r.y + r.h / 2;
            uint32_t ic = (i == 2 && hovered) ? rgb_hex(0xFFFFFF) : rgb_hex(th->text);
            uint8_t  ia = focused ? 230 : 160;
            if (i == 0) { /* minimize: single horizontal line */
                flush_rect(cx - 5, cy, 10, 1, ic, ia);
            } else if (i == 1) { /* maximize: rounded square outline */
                flush_stroke_round(cx - 5, cy - 5, 10, 10, 2, 1, ic, ia);
            } else { /* close: X */
                flush_line(cx - 5, cy - 5, cx + 5, cy + 5, 1, ic, ia);
                flush_line(cx + 5, cy - 5, cx - 5, cy + 5, 1, ic, ia);
            }
        }
    }
}

/* Forward declare app draw callbacks so render can invoke them. */
static void app_draw_generic_lines(window_t *w, ui_rect_t body);
static void app_draw_terminal(window_t *w, ui_rect_t body);
static void ui_text_fit_center(int x, int y, int w,
                               uint32_t color, uint8_t alpha, const char *text);

static void render_window(const window_t *w, bool focused) {
    const theme_t *th = current_theme();
    app_t *app = app_by_window(w);
    ui_rect_t body = window_body_rect(w);
    ui_rect_t title = window_title_rect(w);
    uint32_t text_color = rgb_hex(th->text);
    uint32_t muted      = rgb_hex(th->text_muted);
    uint32_t accent     = rgb_hex(th->accent);
    int i;
    if (!w->used || !w->visible || w->minimized) return;

    if (w->flags & WINF_BORDERLESS) {
        flush_scissor_push(body.x, body.y, body.w, body.h);
        if (w->app_id == APP_RING3_BACKED) {
            int widx = (int)(w - g_windows);
            /* No presentamos aca: si el frame sale a mitad de compose se ve
             * el fondo viejo por un instante y parece que todo titila. */
            flush_execute_to_backbuffer();
            flush_reset();
            r3wm_compose_window(widx, body);
        } else if (w->app_id == APP_KERNEL_CONSOLE) {
            app_draw_terminal((window_t *)w, body);
        } else if (app && app->draw) {
            app->draw((window_t *)w, body);
        }
        flush_scissor_pop();
        return;
    }

    /* Drop shadow */
    flush_shadow(w->x, w->y + 4, w->w, w->h, WIN_RADIUS + 2, 6,
                 rgb_hex(0x000000), focused ? 80 : 50);

    /* Acrylic panel with blurred BG (single pass for perf). */
    flush_glass(w->x, w->y, w->w, w->h, WIN_RADIUS,
                rgb_hex(th->bg),  focused ? 194 : 174,
                rgb_hex(th->stroke), focused ? 150 : 90,
                6, 1);

    /* Title bar tint: rounded TOP corners only so the tint never pokes
     * past the window's rounded glass corners (square bottom is fine
     * because the window body fills below). */
    flush_round_rect4(w->x + 1, w->y + 1, w->w - 2, WIN_TITLE_H - 1,
                      WIN_RADIUS - 1, WIN_RADIUS - 1, 0, 0,
                      focused ? accent : rgb_hex(th->bg_alt),
                      focused ? 38 : 24);
    (void)title;

    /* Accent line under title for focused */
    if (focused) flush_rect(w->x + 2, w->y + WIN_TITLE_H - 1, w->w - 4, 1, accent, 130);

    /* Title icon (larger, centered vertically in title bar) */
    if (app && app->icon_id >= 0 && app->icon_id < RIDUX_ICON_COUNT) {
        int is = 24;
        flush_image(w->x + 12, w->y + (WIN_TITLE_H - is) / 2, is, is,
                    &RIDUX_ICONS[app->icon_id], focused ? 240 : 180);
    }

    /* Title text: 2x rendered from high-res glyph atlas for crisp edges. */
    {
        const int title_scale = 2;
        const int title_h = 16 * title_scale;
        int tx = w->x + 44;
        int ty = w->y + (WIN_TITLE_H - title_h) / 2;
        /* Clip so long titles don't overlap window controls. */
        int clip_w = w->w - 44 - WIN_CTRL_W * 3 - 8;
        if (clip_w < 40) clip_w = 40;
        flush_scissor_push(tx, w->y, clip_w, WIN_TITLE_H);
        flush_text_scaled(tx, ty, title_scale, text_color,
                          focused ? 235 : 180, w->title);
        flush_scissor_pop();
    }

    /* Controls */
    render_window_controls(w, focused);

    /* Body content: clip to body and defer to app draw. Ring 3 windows
     * have their body pixels in a user-mode shared framebuffer; the
     * kernel WM blits from that instead of running an in-kernel draw
     * callback. We need to commit the queued flush commands first so
     * the body is composited AFTER the chrome (title bar, glass) hits
     * the backbuffer. */
    flush_scissor_push(body.x, body.y, body.w, body.h);
    if (w->app_id == APP_RING3_BACKED) {
        int widx = (int)(w - g_windows);
        flush_execute_to_backbuffer();
        flush_reset();
        r3wm_compose_window(widx, body);
    } else if (w->app_id == APP_KERNEL_CONSOLE) {
        app_draw_terminal((window_t *)w, body);
    } else if (app && app->draw) {
        app->draw((window_t *)w, body);
    } else {
        app_draw_generic_lines((window_t *)w, body);
    }
    flush_scissor_pop();

    /* Outline ring */
    flush_stroke_round(w->x, w->y, w->w, w->h, WIN_RADIUS, 1,
                       rgb_hex(th->stroke), focused ? 200 : 120);
    (void)i; (void)muted;
}

static void render_cursor(void) {
    int x = g_mouse_x;
    int y = g_mouse_y;
    uint32_t white = rgb_hex(0xFDFEFF);
    uint32_t dark  = rgb_hex(0x0A1018);
    /* Shadow */
    flush_circle(x + 1, y + 1, 1, dark, 120);
    /* Arrow body */
    flush_line(x, y, x + 12, y + 12, 1, dark, 220);
    flush_line(x, y, x, y + 16, 1, dark, 220);
    flush_line(x, y + 16, x + 5, y + 12, 1, dark, 220);
    flush_line(x + 5, y + 12, x + 12, y + 12, 1, dark, 220);
    /* Inner white */
    flush_line(x + 1, y + 2, x + 1, y + 13, 1, white, 255);
    flush_line(x + 2, y + 4, x + 2, y + 12, 1, white, 255);
    flush_line(x + 3, y + 6, x + 3, y + 11, 1, white, 255);
    flush_line(x + 4, y + 8, x + 4, y + 11, 1, white, 255);
    flush_rect(x + 5, y + 10, 6, 2, white, 255);
}
/* ============================================================
 * [9b] Ridux R3 WM: native window protocol for Ring 3 ELF apps
 * ============================================================
 *
 * Lets a CPL=3 process open a window managed by this kernel WM,
 * write into a kernel-owned framebuffer mapped into its address
 * space, and receive routed input events. Wire format is in
 * src/ridux_r3wm.h (shared with the userspace SDK in ridux-ui/).
 *
 * Architecture:
 *  - One ridux_r3win_t per open Ring 3 window. Tracks the user
 *    framebuffer (a list of phys frames + the user VA where it's
 *    mapped + the owning task's address space) and a small event
 *    ring buffer.
 *  - The kernel WM allocates a regular `window_t` slot with
 *    app_id = APP_RING3_BACKED. render_window() detects that and,
 *    instead of calling app->draw(), calls r3wm_compose_window()
 *    which DMAP-blits the user FB row by row into g_backbuffer.
 *  - Input handlers (mouse click, mouse move while focused, key
 *    press) call r3wm_post_input_*() which pushes events into the
 *    focused Ring 3 window's ring; the user task drains the ring
 *    via RIDUX_SYS_WINDOW_POLL.
 *  - Closing the window (user calls RIDUX_SYS_WINDOW_CLOSE OR the
 *    user clicks the close control) tears down the FB pages and
 *    removes the window_t slot.
 */

#define R3WM_FB_MAX_PAGES   2304u  /* enough for a 1920x1080 XRGB surface */

typedef struct ridux_r3win {
    bool     used;
    int      wid;             /* g_windows[].id of the WM window slot */
    int      window_idx;      /* index into g_windows[] (cached for speed) */
    int      pid;             /* owning task pid */
    int      app_id;          /* Ridux app registry id, if this window maps to one */
    address_space_t *as;      /* owning task's address space (for unmap) */
    uint32_t width;
    uint32_t height;
    uint32_t stride;          /* bytes per row, == width*4 */
    uint32_t flags;
    uint64_t fb_user_va;      /* user-space base VA of the framebuffer */
    uint32_t fb_pages;        /* number of 4 KB pages in the FB */
    uint64_t fb_frames[R3WM_FB_MAX_PAGES]; /* phys addr of each FB page */

    /* damage rect (in window-local px); updated by present, cleared
     * after compose. Today we just OR damage in render_window for
     * simplicity, but the field is kept for future partial blits. */
    bool     damaged;
    int32_t  dmg_x, dmg_y, dmg_w, dmg_h;

    /* event ring (single-producer kernel, single-consumer user) */
    ridux_event_t events[RIDUX_EVENT_RING_SIZE];
    volatile uint32_t ev_head;  /* next slot to write */
    volatile uint32_t ev_tail;  /* next slot to read */
} ridux_r3win_t;

static ridux_r3win_t g_r3wins[RIDUX_WIN_MAX];

/* Find slot by WM window id (the public id, NOT g_windows index). */
static ridux_r3win_t *r3wm_find_by_wid(int wid) {
    int i;
    if (wid <= 0) return NULL;
    for (i = 0; i < RIDUX_WIN_MAX; ++i) {
        if (g_r3wins[i].used && g_r3wins[i].wid == wid) return &g_r3wins[i];
    }
    return NULL;
}

/* Find slot by the current g_windows[] index. Window order changes when the
 * user focuses something, so we resolve through the public window id instead
 * of trusting an old cached index. */
static ridux_r3win_t *r3wm_find_by_window_idx(int widx) {
    if (widx < 0 || widx >= g_window_count) return NULL;
    if (!g_windows[widx].used) return NULL;
    return r3wm_find_by_wid(g_windows[widx].id);
}

/* Free framebuffer pages and unmap from owning address space. */
static void r3wm_free_fb(ridux_r3win_t *r) {
    uint32_t i;
    if (!r->fb_pages) return;
    for (i = 0; i < r->fb_pages; ++i) {
        if (r->as) {
            paging_unmap(r->as, r->fb_user_va + (uint64_t)i * PAGE_SIZE);
        }
        if (r->fb_frames[i]) {
            pmm_free_frame(r->fb_frames[i]);
            r->fb_frames[i] = 0;
        }
    }
    r->fb_pages = 0;
    r->fb_user_va = 0;
    r->as = NULL;
}

/* Allocate framebuffer pages, map them in the user's address space at
 * `user_va`, return 0 on success or negative errno. The chosen VA is a
 * fixed per-window slot (0x60000000 + wid*0x01000000) so we don't have
 * to thread a free-VA allocator through; 16 MB stride per window keeps
 * us well clear of mmap regions, the user stack and any heap growth. */
static int r3wm_alloc_fb(ridux_r3win_t *r, address_space_t *as,
                         uint32_t width, uint32_t height) {
    uint64_t bytes = (uint64_t)width * height * 4u;
    uint32_t pages = (uint32_t)((bytes + PAGE_SIZE - 1u) / PAGE_SIZE);
    uint64_t va_base;
    uint32_t i;
    if (pages == 0 || pages > R3WM_FB_MAX_PAGES) return -22; /* -EINVAL */

    /* Reserve a 16 MB virtual region per Ring 3 window slot. */
    va_base = 0x60000000ULL + (uint64_t)(r - g_r3wins) * 0x01000000ULL;

    r->as = as;
    r->fb_user_va = va_base;
    r->fb_pages = 0;
    for (i = 0; i < pages; ++i) {
        uint64_t phys = pmm_alloc_frame();
        if (!phys) goto rollback;
        r->fb_frames[i] = phys;
        if (!paging_map(as, va_base + (uint64_t)i * PAGE_SIZE, phys,
                        PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER | PAGE_NX)) {
            pmm_free_frame(phys);
            r->fb_frames[i] = 0;
            goto rollback;
        }
        /* Zero via DMAP: works regardless of current CR3. */
        k_memset((void *)PHYS_TO_DMAP(phys), 0, PAGE_SIZE);
        r->fb_pages = i + 1;
    }
    return 0;

rollback:
    r3wm_free_fb(r);
    return -12; /* -ENOMEM */
}

/* Push an event into the window's ring. Drops the oldest if full. */
static void r3wm_push_event(ridux_r3win_t *r, const ridux_event_t *ev) {
    uint32_t head = r->ev_head;
    uint32_t next = (head + 1u) & (RIDUX_EVENT_RING_SIZE - 1u);
    if (next == r->ev_tail) {
        /* full: drop oldest */
        r->ev_tail = (r->ev_tail + 1u) & (RIDUX_EVENT_RING_SIZE - 1u);
    }
    r->events[head] = *ev;
    r->ev_head = next;
}

/* Public helpers used by render_window and the input handlers. */
static void r3wm_compose_window(int window_idx, ui_rect_t body) {
    ridux_r3win_t *r = r3wm_find_by_window_idx(window_idx);
    uint32_t y;
    uint32_t copy_w, copy_h;
    if (!r || !r->fb_pages || !g_use_backbuffer) return;
    copy_w = r->width;  if ((int)copy_w > body.w) copy_w = (uint32_t)body.w;
    copy_h = r->height; if ((int)copy_h > body.h) copy_h = (uint32_t)body.h;
    if ((uint32_t)body.x >= g_fb.width || (uint32_t)body.y >= g_fb.height) return;
    if (body.x + (int)copy_w > (int)g_fb.width)  copy_w = g_fb.width  - (uint32_t)body.x;
    if (body.y + (int)copy_h > (int)g_fb.height) copy_h = g_fb.height - (uint32_t)body.y;

    for (y = 0; y < copy_h; ++y) {
        uint32_t row_byte = y * r->stride;
        uint32_t pg = row_byte >> 12;
        uint32_t in_pg = row_byte & 0xFFFu;
        uint32_t want = copy_w * 4u;
        uint32_t avail = PAGE_SIZE - in_pg;
        uint32_t *dst = g_backbuffer
                      + (size_t)(body.y + (int)y) * FB_MAX_WIDTH
                      + (size_t)body.x;
        if (pg >= r->fb_pages) break;
        if (avail >= want) {
            const uint8_t *src = (const uint8_t *)PHYS_TO_DMAP(r->fb_frames[pg]) + in_pg;
            k_memcpy(dst, src, want);
        } else {
            const uint8_t *src1 = (const uint8_t *)PHYS_TO_DMAP(r->fb_frames[pg]) + in_pg;
            k_memcpy(dst, src1, avail);
            if (pg + 1 < r->fb_pages) {
                const uint8_t *src2 = (const uint8_t *)PHYS_TO_DMAP(r->fb_frames[pg + 1]);
                k_memcpy((uint8_t *)dst + avail, src2, want - avail);
            }
        }
    }
    r->damaged = false;
}

/* Called by the kernel WM when the close control is clicked. */
static void r3wm_post_close(int window_idx) {
    ridux_r3win_t *r = r3wm_find_by_window_idx(window_idx);
    ridux_event_t ev;
    if (!r) return;
    k_memset(&ev, 0, sizeof(ev));
    ev.type = RIDUX_EVENT_CLOSE;
    r3wm_push_event(r, &ev);
}

/* Called by the mouse handlers when input lands inside a Ring 3 window. */
static void r3wm_post_mouse(int window_idx, uint32_t type,
                            int win_x, int win_y, uint32_t button) {
    ridux_r3win_t *r = r3wm_find_by_window_idx(window_idx);
    ridux_event_t ev;
    if (!r) return;
    k_memset(&ev, 0, sizeof(ev));
    ev.type = type;
    ev.x = win_x;
    ev.y = win_y;
    ev.button = button;
    r3wm_push_event(r, &ev);
}

/* Called by the keyboard handler when the focused window is Ring 3. */
static void r3wm_post_key(int window_idx, uint32_t type,
                          uint32_t ascii, uint32_t scancode) {
    ridux_r3win_t *r = r3wm_find_by_window_idx(window_idx);
    ridux_event_t ev;
    if (!r) return;
    k_memset(&ev, 0, sizeof(ev));
    ev.type = type;
    ev.key = ascii;
    ev.scancode = scancode;
    r3wm_push_event(r, &ev);
}

/* ---- Syscall handlers (registered into g_syscall_table at boot) ----
 *
 * Each handler matches the syscall_fn_t signature and returns 0 / a
 * positive value on success, or a negative errno on failure. */

static int64_t r3wm_sys_window_open(uint64_t a0, uint64_t a1, uint64_t a2,
                                    uint64_t a3, uint64_t a4, uint64_t a5) {
    ridux_window_open_args_t *args = (ridux_window_open_args_t *)(uintptr_t)a0;
    task_t *cur = task_current();
    ridux_r3win_t *r = NULL;
    int slot = -1;
    int wid;
    char title_buf[64];
    int rc;
    int i;
    int x, y;
    int win_w, win_h;
    int app_idx = -1;
    uint32_t win_flags = 0;
    uint32_t req_flags;
    uint32_t req_w;
    uint32_t req_h;
    (void)a1; (void)a2; (void)a3; (void)a4; (void)a5;

    if (!args || !cur || !cur->addr_space) return -14; /* -EFAULT */
    req_flags = args->flags;
    req_w = args->width;
    req_h = args->height;
    if (req_flags & RIDUX_WIN_FLAG_DESKTOP) {
        /* El escritorio de Ring 3 no deberia adivinar la resolucion.
         * Si el framebuffer cambia, el kernel le da el tamano real y listo. */
        req_w = g_fb.width;
        req_h = g_fb.height;
        if (req_w > RIDUX_WIN_MAX_W) req_w = RIDUX_WIN_MAX_W;
        if (req_h > RIDUX_WIN_MAX_H) req_h = RIDUX_WIN_MAX_H;
        if (req_w < RIDUX_WIN_MIN_W) req_w = RIDUX_WIN_MIN_W;
        if (req_h < RIDUX_WIN_MIN_H) req_h = RIDUX_WIN_MIN_H;
    }
    if (req_w < RIDUX_WIN_MIN_W || req_w > RIDUX_WIN_MAX_W) return -22;
    if (req_h < RIDUX_WIN_MIN_H || req_h > RIDUX_WIN_MAX_H) return -22;
    if (req_flags & RIDUX_WIN_FLAG_BORDERLESS) win_flags |= WINF_BORDERLESS;
    if (req_flags & RIDUX_WIN_FLAG_DESKTOP) {
        win_flags |= WINF_DESKTOP | WINF_BORDERLESS | WINF_NO_FOCUS | WINF_NO_TASKBAR;
    }
    if (req_flags & RIDUX_WIN_FLAG_NO_FOCUS) win_flags |= WINF_NO_FOCUS;
    if (req_flags & RIDUX_WIN_FLAG_NO_TASKBAR) win_flags |= WINF_NO_TASKBAR;

    /* Copy title from user (best effort: assume args->title is mapped). */
    title_buf[0] = 0;
    if (args->title) {
        const char *t = args->title;
        for (i = 0; i < (int)sizeof(title_buf) - 1 && t[i]; ++i) title_buf[i] = t[i];
        title_buf[i] = 0;
    }
    if (!title_buf[0]) {
        const char *def = "Ring 3 App";
        for (i = 0; def[i]; ++i) title_buf[i] = def[i];
        title_buf[i] = 0;
    }
    app_idx = app_index_by_name(title_buf);

    /* Find a free slot. */
    for (i = 0; i < RIDUX_WIN_MAX; ++i) {
        if (!g_r3wins[i].used) { slot = i; break; }
    }
    if (slot < 0) return -24; /* -EMFILE */
    r = &g_r3wins[slot];
    k_memset(r, 0, sizeof(*r));
    r->used = true;
    r->pid = cur->pid;
    r->app_id = (app_idx >= 0) ? g_apps[app_idx].app_id : 0;
    r->width = req_w;
    r->height = req_h;
    r->stride = req_w * 4u;
    r->flags = req_flags;

    /* Allocate + map FB into user address space. */
    rc = r3wm_alloc_fb(r, cur->addr_space, req_w, req_h);
    if (rc < 0) {
        k_memset(r, 0, sizeof(*r));
        return rc;
    }

    if (win_flags & WINF_BORDERLESS) {
        win_w = (int)req_w;
        win_h = (int)req_h;
    } else {
        win_w = (int)req_w + 4;
        win_h = (int)req_h + 4 + WIN_TITLE_H;
    }

    if (win_flags & WINF_DESKTOP) {
        x = 0;
        y = 0;
        if (win_w > (int)g_fb.width) win_w = (int)g_fb.width;
        if (win_h > (int)g_fb.height) win_h = (int)g_fb.height;
    } else {
        x = (int)g_fb.width / 2 - win_w / 2;
        y = (int)g_fb.height / 2 - win_h / 2;
    }
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    wid = wm_add_window(title_buf, APP_RING3_BACKED, true,
                        x, y, win_w, win_h,
                        232);
    if (wid < 0) {
        r3wm_free_fb(r);
        k_memset(r, 0, sizeof(*r));
        return -24;
    }
    r->wid = wid;
    /* Cache window_idx by scanning. */
    r->window_idx = -1;
    for (i = 0; i < g_window_count; ++i) {
        if (g_windows[i].id == wid) { r->window_idx = i; break; }
    }
    if (r->window_idx < 0) {
        r3wm_free_fb(r);
        k_memset(r, 0, sizeof(*r));
        return -22;
    }
    g_windows[r->window_idx].flags |= win_flags;
    if (app_idx >= 0 && !(win_flags & WINF_DESKTOP)) {
        g_apps[app_idx].window_id = wid;
        g_app_launch_count[app_idx] = 1;
    }
    if (!(win_flags & WINF_NO_FOCUS)) g_window_focus = r->window_idx;
    g_needs_redraw = true;

    args->wid = (int32_t)wid;
    args->width = req_w;
    args->height = req_h;
    args->fb_user_va = r->fb_user_va;
    args->stride = r->stride;
    args->flags = r->flags;
    return 0;
}

static int64_t r3wm_sys_window_present(uint64_t a0, uint64_t a1, uint64_t a2,
                                       uint64_t a3, uint64_t a4, uint64_t a5) {
    int wid = (int)a0;
    int x = (int)a1;
    int y = (int)a2;
    int w = (int)a3;
    int h = (int)a4;
    ridux_r3win_t *r = r3wm_find_by_wid(wid);
    (void)a5;
    if (!r) return -9; /* -EBADF */
    if (w <= 0 || h <= 0) {
        r->damaged = true;
        r->dmg_x = 0; r->dmg_y = 0;
        r->dmg_w = (int32_t)r->width; r->dmg_h = (int32_t)r->height;
    } else {
        r->damaged = true;
        r->dmg_x = x; r->dmg_y = y;
        r->dmg_w = w; r->dmg_h = h;
    }
    g_needs_redraw = true;
    flush_cursor_under_invalidate();
    return 0;
}

extern void task_schedule(void);
static int64_t r3wm_sys_shell_exec(uint64_t a0, uint64_t a1, uint64_t a2,
                                   uint64_t a3, uint64_t a4, uint64_t a5);

static int64_t r3wm_sys_window_poll(uint64_t a0, uint64_t a1, uint64_t a2,
                                    uint64_t a3, uint64_t a4, uint64_t a5) {
    int wid = (int)a0;
    ridux_event_t *user_buf = (ridux_event_t *)(uintptr_t)a1;
    int max_n = (int)a2;
    ridux_r3win_t *r = r3wm_find_by_wid(wid);
    int n = 0;
    (void)a3; (void)a4; (void)a5;
    if (!r) return -9; /* -EBADF */
    if (!user_buf || max_n <= 0) return 0;
    while (n < max_n && r->ev_tail != r->ev_head) {
        user_buf[n] = r->events[r->ev_tail];
        r->ev_tail = (r->ev_tail + 1u) & (RIDUX_EVENT_RING_SIZE - 1u);
        ++n;
    }
    /* Yield CPU back to other tasks when there's nothing to deliver.
     * Ring 3 apps typically call rd_poll() in a tight loop; without
     * this yield they hog the CPU and starve the kernel main thread
     * that draws the desktop, so the window itself never becomes
     * visible even though it was opened successfully. */
    if (n == 0) task_schedule();
    return (int64_t)n;
}

static int64_t r3wm_sys_desktop_state(uint64_t a0, uint64_t a1, uint64_t a2,
                                      uint64_t a3, uint64_t a4, uint64_t a5) {
    ridux_desktop_state_t *st = (ridux_desktop_state_t *)(uintptr_t)a0;
    uint32_t out = 0;
    int i;
    (void)a1; (void)a2; (void)a3; (void)a4; (void)a5;
    if (!st) return -14; /* -EFAULT */
    k_memset(st, 0, sizeof(*st));
    st->width = g_fb.width;
    st->height = g_fb.height;
    st->mouse_x = g_mouse_x;
    st->mouse_y = g_mouse_y;
    st->start_open = g_start_open ? 1u : 0u;
    st->quick_open = g_quick_open ? 1u : 0u;
    st->hour = g_rtc_now.hour;
    st->minute = g_rtc_now.minute;
    st->day = g_rtc_now.day;
    st->month = g_rtc_now.month;
    st->year = g_rtc_now.year;
    for (i = 0; i < g_app_count && out < RIDUX_DESKTOP_APP_MAX; ++i) {
        ridux_desktop_app_state_t *dst;
        if (!g_apps[i].pinned_task) continue;
        dst = &st->apps[out++];
        k_strlcpy(dst->name, g_apps[i].name, sizeof(dst->name));
        dst->icon = (uint32_t)g_apps[i].icon_id;
        dst->running = app_is_running(g_apps[i].app_id) ? 1u : 0u;
        dst->focused = 0u;
        if (g_window_focus >= 0 && g_window_focus < g_window_count &&
            g_apps[i].window_id > 0 &&
            g_windows[g_window_focus].id == g_apps[i].window_id) {
            dst->focused = 1u;
        }
    }
    st->app_count = out;
    return 0;
}

static int64_t r3wm_sys_window_close(uint64_t a0, uint64_t a1, uint64_t a2,
                                     uint64_t a3, uint64_t a4, uint64_t a5) {
    int wid = (int)a0;
    ridux_r3win_t *r = r3wm_find_by_wid(wid);
    int idx;
    int app_idx;
    (void)a1; (void)a2; (void)a3; (void)a4; (void)a5;
    if (!r) return -9; /* -EBADF */
    idx = r->window_idx;
    app_idx = app_index_by_id(r->app_id);
    if (app_idx >= 0 && g_apps[app_idx].window_id == wid) {
        g_apps[app_idx].window_id = 0;
        g_app_launch_count[app_idx] = 0;
    }
    r3wm_free_fb(r);
    /* Hide the window in the WM. We don't fully delete it from
     * g_windows[] (the existing WM doesn't support compaction during
     * runtime); marking unused + invisible is enough to take it out
     * of the render and click paths. */
    if (idx >= 0 && idx < g_window_count) {
        g_windows[idx].used = false;
        g_windows[idx].visible = false;
        if (g_window_focus == idx) g_window_focus = -1;
    }
    k_memset(r, 0, sizeof(*r));
    g_needs_redraw = true;
    return 0;
}

/* Called from kernel_main during boot, AFTER compat3_rewire_syscalls()
 * (which fills the table with Linux ABI handlers) so we win the slot. */
static void ridux_r3wm_init(void) {
    int i;
    k_memset(g_r3wins, 0, sizeof(g_r3wins));
    /* The syscall_fn_t signature is the canonical 6-arg one, matching
     * our handler prototypes above. Cast to silence the explicit type. */
    g_syscall_table[RIDUX_SYS_WINDOW_OPEN]    = (syscall_fn_t)r3wm_sys_window_open;
    g_syscall_table[RIDUX_SYS_WINDOW_PRESENT] = (syscall_fn_t)r3wm_sys_window_present;
    g_syscall_table[RIDUX_SYS_WINDOW_POLL]    = (syscall_fn_t)r3wm_sys_window_poll;
    g_syscall_table[RIDUX_SYS_WINDOW_CLOSE]   = (syscall_fn_t)r3wm_sys_window_close;
    g_syscall_table[RIDUX_SYS_SHELL_EXEC]     = (syscall_fn_t)r3wm_sys_shell_exec;
    g_syscall_table[RIDUX_SYS_DESKTOP_STATE]  = (syscall_fn_t)r3wm_sys_desktop_state;
    (void)i;
}
/* App draw callbacks */

/* Generic fallback: render win->lines[] with padding. */
static void app_draw_generic_lines(window_t *w, ui_rect_t body) {
    const theme_t *th = current_theme();
    int i;
    int pad = 14;
    int line_h = 20;
    int max_lines = (body.h - pad * 2) / line_h;
    if (max_lines < 0) max_lines = 0;
    if (max_lines > WINDOW_LINE_COUNT) max_lines = WINDOW_LINE_COUNT;
    for (i = 0; i < max_lines; ++i) {
        if (w->lines[i][0]) {
            flush_text_alpha(body.x + pad, body.y + pad + i * line_h,
                             rgb_hex(th->text), 220, w->lines[i]);
        }
    }
}

/* Monitor app */
static void app_draw_monitor(window_t *w, ui_rect_t body) {
    const theme_t *th = current_theme();
    int pad = 16;
    int i;
    int y = body.y + pad;
    int bar_x = body.x + pad;
    int bar_w = body.w - pad * 2;
    char line[96];
    size_t len;
    uint32_t accent = rgb_hex(th->accent);
    uint32_t muted  = rgb_hex(th->text_muted);
    uint32_t text   = rgb_hex(th->text);

    flush_text_scaled(bar_x, y, 2, text, 230, "System");
    y += 38;

    /* CPU processes bar (count/PROC_MAX) */
    flush_text_alpha(bar_x, y, muted, 200, "Processes");
    {
        int n = proc_count();
        int pct = (n * 100) / PROC_MAX;
        flush_round_rect(bar_x, y + 20, bar_w, 8, 4, rgb_hex(th->bg_alt), 200);
        flush_round_rect(bar_x, y + 20, (bar_w * pct) / 100, 8, 4, accent, 230);
        line[0] = 0; len = 0;
        k_append_u32(line, &len, sizeof(line), (uint32_t)n);
        k_append_str(line, &len, sizeof(line), " / ");
        k_append_u32(line, &len, sizeof(line), (uint32_t)PROC_MAX);
        flush_text_alpha(bar_x + bar_w - measure_text(line), y, text, 200, line);
    }
    y += 42;

    /* VFS files bar */
    flush_text_alpha(bar_x, y, muted, 200, "RiduxFS files");
    {
        int n = (int)vfs_count();
        int pct = (n * 100) / VFS_MAX_FILES;
        flush_round_rect(bar_x, y + 20, bar_w, 8, 4, rgb_hex(th->bg_alt), 200);
        flush_round_rect(bar_x, y + 20, (bar_w * pct) / 100, 8, 4,
                         rgb_hex(th->success), 220);
        line[0] = 0; len = 0;
        k_append_u32(line, &len, sizeof(line), (uint32_t)n);
        k_append_str(line, &len, sizeof(line), " files");
        flush_text_alpha(bar_x + bar_w - measure_text(line), y, text, 200, line);
    }
    y += 42;

    /* Drivers loaded */
    flush_text_alpha(bar_x, y, muted, 200, "Drivers loaded");
    {
        int n = 0;
        for (i = 0; i < g_driver_count; ++i) if (g_drivers[i].ready) ++n;
        line[0] = 0; len = 0;
        k_append_u32(line, &len, sizeof(line), (uint32_t)n);
        k_append_str(line, &len, sizeof(line), " / ");
        k_append_u32(line, &len, sizeof(line), (uint32_t)g_driver_count);
        flush_text_alpha(bar_x + bar_w - measure_text(line), y, text, 200, line);
        flush_round_rect(bar_x, y + 20, bar_w, 8, 4, rgb_hex(th->bg_alt), 200);
        flush_round_rect(bar_x, y + 20, (bar_w * n) / (g_driver_count == 0 ? 1 : g_driver_count),
                         8, 4, rgb_hex(0xBC8CFF), 220);
    }
    y += 42;

    /* Uptime */
    line[0] = 0; len = 0;
    k_append_str(line, &len, sizeof(line), "Uptime: ");
    k_append_u32(line, &len, sizeof(line), g_uptime_ticks / g_pit_hz);
    k_append_str(line, &len, sizeof(line), "s  PID ");
    if (proc_running_pid() < 0) k_append_str(line, &len, sizeof(line), "-");
    else k_append_u32(line, &len, sizeof(line), (uint32_t)proc_running_pid());
    flush_text_alpha(bar_x, y, text, 220, line);
    y += 28;

    /* RTC */
    line[0] = 0; len = 0;
    k_append_u32_pad(line, &len, sizeof(line), g_rtc_now.year, 4, '0');
    k_append_ch(line, &len, sizeof(line), '-');
    k_append_u32_pad(line, &len, sizeof(line), g_rtc_now.month, 2, '0');
    k_append_ch(line, &len, sizeof(line), '-');
    k_append_u32_pad(line, &len, sizeof(line), g_rtc_now.day, 2, '0');
    k_append_ch(line, &len, sizeof(line), ' ');
    k_append_u32_pad(line, &len, sizeof(line), g_rtc_now.hour, 2, '0');
    k_append_ch(line, &len, sizeof(line), ':');
    k_append_u32_pad(line, &len, sizeof(line), g_rtc_now.minute, 2, '0');
    k_append_ch(line, &len, sizeof(line), ':');
    k_append_u32_pad(line, &len, sizeof(line), g_rtc_now.second, 2, '0');
    flush_text_alpha(bar_x, y, muted, 200, line);
}

/* Files app */
static void app_draw_files(window_t *w, ui_rect_t body) {
    const theme_t *th = current_theme();
    int pad = 14;
    int y = body.y + pad;
    int i;
    int row = 0;
    int line_h = 22;
    uint32_t text  = rgb_hex(th->text);
    uint32_t muted = rgb_hex(th->text_muted);

    flush_text_scaled(body.x + pad, y, 2, text, 230, "RiduxFS");
    y += 38;

    /* Sidebar: quick folders */
    flush_round_rect(body.x + pad, y, 160, line_h * 6 + 10, 10,
                     rgb_hex(th->bg_alt), 150);
    {
        const char *labels[] = {"This PC", "Home", "Documents", "Downloads", "Pictures", "Trash"};
        int icons[] = { RIDUX_ICON_MONITOR, RIDUX_ICON_FILES,
                        RIDUX_ICON_NOTES,   RIDUX_ICON_SETTINGS,
                        RIDUX_ICON_FLUSH,   RIDUX_ICON_FILES };
        int k;
        for (k = 0; k < 6; ++k) {
            int ry = y + 8 + k * line_h;
            flush_image(body.x + pad + 8, ry, 16, 16, &RIDUX_ICONS[icons[k] % RIDUX_ICON_COUNT], 200);
            flush_text_alpha(body.x + pad + 30, ry, text, 210, labels[k]);
        }
    }

    /* File list on the right */
    {
        int list_x = body.x + pad + 170;
        int list_w = body.w - pad * 2 - 170;
        flush_round_rect(list_x, y, list_w, body.h - (y - body.y) - pad, 10,
                         rgb_hex(th->bg_alt), 100);
        for (i = 0; i < VFS_MAX_FILES && row < (body.h - (y - body.y) - pad) / line_h - 1; ++i) {
            if (g_vfs_files[i].used) {
                char line[120];
                size_t len = 0;
                int ry = y + 10 + row * line_h;
                line[0] = 0;
                k_append_str(line, &len, sizeof(line),
                             g_vfs_files[i].writable ? "rw  " : "ro  ");
                k_append_str(line, &len, sizeof(line), g_vfs_files[i].path);
                flush_image(list_x + 8, ry, 14, 14,
                            &RIDUX_ICONS[RIDUX_ICON_NOTES % RIDUX_ICON_COUNT], 180);
                flush_text_alpha(list_x + 30, ry, text, 210, line);
                {
                    char size[16];
                    size_t sl = 0;
                    size[0] = 0;
                    k_append_u32(size, &sl, sizeof(size),
                                 g_vfs_files[i].writable ? g_vfs_files[i].rw_size
                                                         : g_vfs_files[i].ro_size);
                    k_append_str(size, &sl, sizeof(size), " B");
                    flush_text_alpha(list_x + list_w - measure_text(size) - 10,
                                     ry, muted, 200, size);
                }
                ++row;
            }
        }
    }
}

/* Terminal app */
static void app_draw_terminal(window_t *w, ui_rect_t body) {
    const theme_t *th = current_theme();
    int pad = 12;
    int y = body.y + pad;
    int line_h = 18;
    int max_lines = (body.h - pad * 2) / line_h - 1;
    int start;
    size_t i;
    char line[SHELL_LINE_LEN + 8];
    size_t len = 0;
    uint32_t text   = rgb_hex(th->text);
    uint32_t accent = rgb_hex(th->accent);

    /* Flat terminal background */
    flush_round_rect(body.x + pad, y, body.w - pad * 2, body.h - pad * 2, 8,
                     rgb_hex(0x000000), 150);

    if ((int)g_shell_count > max_lines) start = (int)g_shell_count - max_lines;
    else                                 start = 0;
    for (i = (size_t)start; i < g_shell_count; ++i) {
        flush_text_alpha(body.x + pad + 10,
                         y + 10 + ((int)(i - (size_t)start)) * line_h,
                         text, 230, g_shell_lines[i]);
    }
    /* Prompt line */
    line[0] = 0;
    k_append_str(line, &len, sizeof(line), "ridux $ ");
    k_append_str(line, &len, sizeof(line), g_shell_input);
    /* Caret blink */
    if ((g_uptime_ticks / 30) & 1u) k_append_ch(line, &len, sizeof(line), '_');
    flush_text_alpha(body.x + pad + 10,
                     body.y + body.h - pad - line_h,
                     accent, 240, line);
}

/* Notes app */
static void app_draw_notes(window_t *w, ui_rect_t body) {
    const theme_t *th = current_theme();
    int pad = 14;
    int y = body.y + pad;
    const uint8_t *data = NULL;
    uint32_t size = 0;
    int line_h = 20;
    int row = 0;
    uint32_t text  = rgb_hex(th->text);
    uint32_t muted = rgb_hex(th->text_muted);

    flush_text_scaled(body.x + pad, y, 2, text, 230, "Notes");
    y += 38;
    flush_text_alpha(body.x + pad, y, muted, 200, "/tmp/notes.txt");
    y += 24;

    flush_round_rect(body.x + pad, y, body.w - pad * 2, body.h - (y - body.y) - pad,
                     10, rgb_hex(th->bg_alt), 150);
    if (vfs_read("/tmp/notes.txt", &data, &size) && data && size) {
        char line[140];
        size_t ll = 0;
        size_t j;
        line[0] = 0;
        for (j = 0; j < size; ++j) {
            char c = (char)data[j];
            int ry = y + 12 + row * line_h;
            if (c == '\r') continue;
            if (c == '\n' || ll + 2 >= sizeof(line)) {
                line[ll] = 0;
                if (ry + line_h < body.y + body.h - pad) {
                    flush_text_alpha(body.x + pad + 12, ry, text, 220, line);
                    ++row;
                }
                ll = 0;
                if (c == '\n') continue;
            }
            if ((unsigned char)c < 32 || (unsigned char)c > 126) c = '.';
            line[ll++] = c;
            line[ll] = 0;
        }
        if (ll > 0) {
            int ry = y + 12 + row * line_h;
            line[ll] = 0;
            if (ry + line_h < body.y + body.h - pad)
                flush_text_alpha(body.x + pad + 12, ry, text, 220, line);
        }
    } else {
        flush_text_alpha(body.x + pad + 12, y + 12, muted, 200, "(empty)");
    }

    flush_text_alpha(body.x + pad, body.y + body.h - pad - 16, muted, 180,
                     "Shell: write /tmp/notes.txt <texto>");
}

/* Flush Inspector */
static void app_draw_flush(window_t *w, ui_rect_t body) {
    const theme_t *th = current_theme();
    int pad = 16;
    int y = body.y + pad;
    char line[64];
    size_t len;
    uint32_t text   = rgb_hex(th->text);
    uint32_t muted  = rgb_hex(th->text_muted);
    uint32_t accent = rgb_hex(th->accent);

    flush_text_scaled(body.x + pad, y, 2, text, 230, "Flush");
    y += 40;
    flush_text_alpha(body.x + pad, y, muted, 200, "Render queue");
    y += 22;

    line[0] = 0; len = 0;
    k_append_str(line, &len, sizeof(line), "Commands this frame: ");
    k_append_u32(line, &len, sizeof(line), (uint32_t)flush_queue_count());
    flush_text_alpha(body.x + pad, y, text, 220, line);
    y += 22;

    /* Bar */
    {
        int pct = (int)((flush_queue_count() * 100u) / FLUSH_MAX_COMMANDS);
        flush_round_rect(body.x + pad, y, body.w - pad * 2, 6, 3, rgb_hex(th->bg_alt), 200);
        flush_round_rect(body.x + pad, y, ((body.w - pad * 2) * pct) / 100, 6, 3, accent, 230);
    }
    y += 26;

    flush_text_alpha(body.x + pad, y, muted, 200, "Primitive set");
    y += 22;
    {
        const char *prims[] = {
            "clear", "rect", "round_rect", "stroke_rect", "stroke_round",
            "vgradient", "hgradient", "radial", "circle", "ring",
            "line", "text", "text_scaled", "image", "image_tint",
            "blur", "glass", "shadow", "noise", "scissor"
        };
        int i;
        int col_w = (body.w - pad * 2) / 4;
        for (i = 0; i < (int)(sizeof(prims) / sizeof(prims[0])); ++i) {
            int cx = body.x + pad + (i % 4) * col_w;
            int cy = y + (i / 4) * 22;
            flush_circle(cx + 5, cy + 7, 3, accent, 220);
            flush_text_alpha(cx + 14, cy, text, 210, prims[i]);
        }
    }
}

/* Settings app: theme picker, driver list */
static void app_draw_settings(window_t *w, ui_rect_t body) {
    const theme_t *th = current_theme();
    int pad = 16;
    int y = body.y + pad;
    int i;
    uint32_t text  = rgb_hex(th->text);
    uint32_t muted = rgb_hex(th->text_muted);

    flush_text_scaled(body.x + pad, y, 2, text, 230, "Settings");
    y += 40;
    flush_text_alpha(body.x + pad, y, muted, 200, "Theme");
    y += 22;
    for (i = 0; i < THEME_COUNT; ++i) {
        const theme_t *t = &g_theme_table[i];
        int sx = body.x + pad + i * 130;
        int sy = y;
        bool active = (int)g_theme_index == i;
        flush_round_rect(sx, sy, 120, 56, 10, rgb_hex(t->wallpaper_tint_a), 230);
        flush_round_rect(sx + 8, sy + 8, 24, 24, 6, rgb_hex(t->accent), 240);
        flush_round_rect(sx + 38, sy + 10, 72, 6, 3, rgb_hex(t->text), 240);
        flush_round_rect(sx + 38, sy + 22, 52, 4, 2, rgb_hex(t->text_muted), 240);
        flush_round_rect(sx + 38, sy + 32, 40, 4, 2, rgb_hex(t->text_muted), 200);
        flush_stroke_round(sx, sy, 120, 56, 10, active ? 2 : 1,
                           active ? rgb_hex(t->accent) : rgb_hex(th->stroke),
                           active ? 255 : 170);
        flush_text_alpha(sx, sy + 60, text, active ? 240 : 190, t->name);
        /* clickable slot id in state_i[0..n] implicit by position. */
    }
    y += 100;

    flush_text_alpha(body.x + pad, y, muted, 200, "Drivers");
    y += 22;
    for (i = 0; i < g_driver_count; ++i) {
        char line[96];
        size_t len = 0;
        int ry = y + i * 18;
        if (ry + 18 > body.y + body.h - pad) break;
        line[0] = 0;
        k_append_str(line, &len, sizeof(line), g_drivers[i].ready ? "[OK] " : "[--] ");
        k_append_str(line, &len, sizeof(line), g_drivers[i].name);
        k_append_str(line, &len, sizeof(line), "  (");
        k_append_str(line, &len, sizeof(line), driver_kind_name(g_drivers[i].kind));
        k_append_str(line, &len, sizeof(line), ")");
        flush_text_alpha(body.x + pad, ry,
                         g_drivers[i].ready ? rgb_hex(th->success) : muted, 210, line);
    }
}

/* Calculator */
static const char *calc_labels[5][4] = {
    { "7", "8", "9", "/" },
    { "4", "5", "6", "*" },
    { "1", "2", "3", "-" },
    { "0", ".", "=", "+" },
    { "C", "<", "(", ")" }
};

static void app_draw_calc(window_t *w, ui_rect_t body) {
    const theme_t *th = current_theme();
    int pad = 14;
    int disp_h = 64;
    int grid_y = body.y + pad + disp_h + 10;
    int grid_h = body.h - (grid_y - body.y) - pad;
    int cell_w = (body.w - pad * 2) / 4;
    int cell_h = grid_h / 5;
    int r, c;
    uint32_t accent = rgb_hex(th->accent);
    uint32_t text   = rgb_hex(th->text);
    uint32_t muted  = rgb_hex(th->text_muted);

    /* Display */
    flush_round_rect(body.x + pad, body.y + pad, body.w - pad * 2, disp_h, 10,
                     rgb_hex(th->bg_alt), 220);
    flush_text_alpha(body.x + pad + 12, body.y + pad + 6, muted, 180, "Calculator");
    {
        const char *disp = w->state_s[0];
        int tw = measure_text(disp) * 2;
        int x = body.x + pad + body.w - pad * 2 - tw - 14;
        flush_text_scaled(x, body.y + pad + 24, 2, text, 240,
                          disp[0] ? disp : "0");
    }

    /* Grid */
    for (r = 0; r < 5; ++r) {
        for (c = 0; c < 4; ++c) {
            int x = body.x + pad + c * cell_w;
            int y = grid_y + r * cell_h;
            const char *lab = calc_labels[r][c];
            bool is_op = (lab[0] == '/' || lab[0] == '*' || lab[0] == '-' || lab[0] == '+' ||
                          lab[0] == '=');
            bool is_special = (lab[0] == 'C' || lab[0] == '<');
            uint32_t bg = is_op ? accent : rgb_hex(th->bg);
            uint8_t  ba = is_op ? 220 : 170;
            if (is_special) { bg = rgb_hex(th->danger); ba = 170; }
            flush_round_rect(x + 4, y + 4, cell_w - 8, cell_h - 8, 10, bg, ba);
            flush_stroke_round(x + 4, y + 4, cell_w - 8, cell_h - 8, 10, 1,
                               rgb_hex(th->stroke), 120);
            {
                int scale = 2;
                int tw = (int)k_strlen(lab) * 8 * scale;
                flush_text_scaled(x + (cell_w - tw) / 2,
                                  y + (cell_h - 16 * scale) / 2,
                                  scale, (is_op || is_special) ? rgb_hex(0xFFFFFF) : text,
                                  240, lab);
            }
        }
    }
}

/* Simple calculator state machine. state_s[0] = display text. state_i[0] = pending op. state_i[1] = acc */
static void calc_press(window_t *w, const char *lab) {
    char *disp = w->state_s[0];
    if (lab[0] == 'C') { disp[0] = 0; w->state_i[0] = 0; w->state_i[1] = 0; return; }
    if (lab[0] == '<') {
        size_t l = k_strlen(disp);
        if (l > 0) disp[l - 1] = 0;
        return;
    }
    if (lab[0] == '=' || lab[0] == '+' || lab[0] == '-' || lab[0] == '*' || lab[0] == '/') {
        int val = k_atoi(disp);
        int op = w->state_i[0];
        int acc = w->state_i[1];
        int res = val;
        if (op == '+') res = acc + val;
        else if (op == '-') res = acc - val;
        else if (op == '*') res = acc * val;
        else if (op == '/') res = (val == 0) ? 0 : acc / val;
        {
            size_t len = 0;
            disp[0] = 0;
            k_append_i32(disp, &len, CALC_DISPLAY_LEN, res);
        }
        w->state_i[1] = res;
        w->state_i[0] = (lab[0] == '=') ? 0 : lab[0];
        if (lab[0] != '=') disp[0] = 0;
        return;
    }
    {
        size_t l = k_strlen(disp);
        if (l + 2 < CALC_DISPLAY_LEN) { disp[l] = lab[0]; disp[l + 1] = 0; }
    }
}

static void app_click_calc(window_t *w, ui_rect_t body, int mx, int my, int button) {
    int pad = 14;
    int disp_h = 64;
    int grid_y = body.y + pad + disp_h + 10;
    int grid_h = body.h - (grid_y - body.y) - pad;
    int cell_w = (body.w - pad * 2) / 4;
    int cell_h = grid_h / 5;
    int r, c;
    (void)button;
    if (my < grid_y) return;
    for (r = 0; r < 5; ++r) {
        for (c = 0; c < 4; ++c) {
            int x = body.x + pad + c * cell_w;
            int y = grid_y + r * cell_h;
            if (mx >= x && mx < x + cell_w && my >= y && my < y + cell_h) {
                calc_press(w, calc_labels[r][c]);
                return;
            }
        }
    }
}

/* Clock */
/* Integer sin/cos via 256-point lookup. degrees -> 0..359 */
static int8_t g_sin_tab[256] = {
      0,   3,   6,   9,  12,  16,  19,  22,  25,  28,  31,  34,  37,  40,  43,  46,
     49,  51,  54,  57,  60,  63,  65,  68,  71,  73,  76,  78,  81,  83,  85,  88,
     90,  92,  94,  96,  98, 100, 102, 104, 106, 107, 109, 111, 112, 113, 115, 116,
    117, 118, 120, 121, 122, 122, 123, 124, 125, 125, 126, 126, 126, 127, 127, 127,
    127, 127, 127, 127, 126, 126, 126, 125, 125, 124, 123, 122, 122, 121, 120, 118,
    117, 116, 115, 113, 112, 111, 109, 107, 106, 104, 102, 100,  98,  96,  94,  92,
     90,  88,  85,  83,  81,  78,  76,  73,  71,  68,  65,  63,  60,  57,  54,  51,
     49,  46,  43,  40,  37,  34,  31,  28,  25,  22,  19,  16,  12,   9,   6,   3,
      0,  -3,  -6,  -9, -12, -16, -19, -22, -25, -28, -31, -34, -37, -40, -43, -46,
    -49, -51, -54, -57, -60, -63, -65, -68, -71, -73, -76, -78, -81, -83, -85, -88,
    -90, -92, -94, -96, -98,-100,-102,-104,-106,-107,-109,-111,-112,-113,-115,-116,
   -117,-118,-120,-121,-122,-122,-123,-124,-125,-125,-126,-126,-126,-127,-127,-127,
   -127,-127,-127,-127,-126,-126,-126,-125,-125,-124,-123,-122,-122,-121,-120,-118,
   -117,-116,-115,-113,-112,-111,-109,-107,-106,-104,-102,-100, -98, -96, -94, -92,
    -90, -88, -85, -83, -81, -78, -76, -73, -71, -68, -65, -63, -60, -57, -54, -51,
    -49, -46, -43, -40, -37, -34, -31, -28, -25, -22, -19, -16, -12,  -9,  -6,  -3
};
static int isin_q(int deg) { return g_sin_tab[((deg % 360) + 360) % 360 * 256 / 360]; }
static int icos_q(int deg) { return g_sin_tab[(((deg + 90) % 360) + 360) % 360 * 256 / 360]; }

static void app_draw_clock(window_t *w, ui_rect_t body) {
    const theme_t *th = current_theme();
    int pad = 16;
    int cx = body.x + body.w / 2;
    int cy = body.y + pad + (body.h - 80) / 2;
    int radius = k_min_i(body.w, body.h) / 3;
    int sec = g_rtc_now.second;
    int min = g_rtc_now.minute;
    int hour = g_rtc_now.hour % 12;
    uint32_t accent = rgb_hex(th->accent);
    uint32_t text   = rgb_hex(th->text);
    uint32_t muted  = rgb_hex(th->text_muted);
    int i;
    (void)w;

    /* face */
    flush_circle(cx, cy, radius, rgb_hex(th->bg_alt), 230);
    flush_ring(cx, cy, radius, 2, rgb_hex(th->stroke), 200);

    /* ticks */
    for (i = 0; i < 12; ++i) {
        int deg = i * 30;
        int sx = cx + (icos_q(deg - 90) * (radius - 10)) / 127;
        int sy = cy + (isin_q(deg - 90) * (radius - 10)) / 127;
        int ex = cx + (icos_q(deg - 90) * (radius - 4)) / 127;
        int ey = cy + (isin_q(deg - 90) * (radius - 4)) / 127;
        flush_line(sx, sy, ex, ey, 2, text, 220);
    }

    /* hands */
    {
        int hdeg = hour * 30 + min / 2 - 90;
        int mdeg = min * 6 - 90;
        int sdeg = sec * 6 - 90;
        int hex = cx + (icos_q(hdeg) * (radius - 50)) / 127;
        int hey = cy + (isin_q(hdeg) * (radius - 50)) / 127;
        int mex = cx + (icos_q(mdeg) * (radius - 30)) / 127;
        int mey = cy + (isin_q(mdeg) * (radius - 30)) / 127;
        int sex = cx + (icos_q(sdeg) * (radius - 20)) / 127;
        int sey = cy + (isin_q(sdeg) * (radius - 20)) / 127;
        flush_line(cx, cy, hex, hey, 4, text, 240);
        flush_line(cx, cy, mex, mey, 3, text, 240);
        flush_line(cx, cy, sex, sey, 1, accent, 240);
        flush_circle(cx, cy, 5, accent, 240);
    }

    /* Digital below */
    {
        char line[24];
        size_t l = 0;
        line[0] = 0;
        k_append_u32_pad(line, &l, sizeof(line), hour == 0 ? 12u : (uint32_t)hour, 2, '0');
        k_append_ch(line, &l, sizeof(line), ':');
        k_append_u32_pad(line, &l, sizeof(line), (uint32_t)min, 2, '0');
        k_append_ch(line, &l, sizeof(line), ':');
        k_append_u32_pad(line, &l, sizeof(line), (uint32_t)sec, 2, '0');
        {
            int scale = 3;
            int tw = (int)k_strlen(line) * 8 * scale;
            int ty = cy + radius + 24;
            flush_text_scaled(body.x + (body.w - tw) / 2, ty, scale, text, 240, line);
        }
    }
    (void)muted;
}

/* Paint */
static const uint32_t paint_palette[PAINT_PALETTE] = {
    0x000000, 0xFFFFFF, 0xE95555, 0xF7A93B,
    0xF7E13B, 0x57C785, 0x2E86C1, 0x8E44AD,
    0xFF6FB5, 0x888888
};

static void app_draw_paint(window_t *w, ui_rect_t body) {
    const theme_t *th = current_theme();
    int pad = 12;
    int pal_h = 36;
    int grid_x = body.x + pad;
    int grid_y = body.y + pad + pal_h + 8;
    int grid_w = body.w - pad * 2;
    int grid_h = body.h - (grid_y - body.y) - pad;
    int cell_w = grid_w / PAINT_GRID_W;
    int cell_h = grid_h / PAINT_GRID_H;
    int r, c, i;
    int selected = w->state_i[0];
    uint32_t text = rgb_hex(th->text);

    /* Palette */
    flush_round_rect(body.x + pad, body.y + pad, body.w - pad * 2, pal_h, 8,
                     rgb_hex(th->bg_alt), 200);
    for (i = 0; i < PAINT_PALETTE; ++i) {
        int cx = body.x + pad + 8 + i * 32;
        int cy = body.y + pad + 4;
        flush_round_rect(cx, cy, 26, 26, 5, rgb_hex(paint_palette[i]), 240);
        if (selected == i) {
            flush_stroke_round(cx - 2, cy - 2, 30, 30, 6, 2, rgb_hex(th->accent), 255);
        }
    }
    flush_text_alpha(body.x + body.w - 120, body.y + pad + 10, text, 200, "click to paint");

    /* Grid */
    flush_round_rect(grid_x, grid_y, cell_w * PAINT_GRID_W, cell_h * PAINT_GRID_H, 8,
                     rgb_hex(0x101418), 200);
    for (r = 0; r < PAINT_GRID_H; ++r) {
        for (c = 0; c < PAINT_GRID_W; ++c) {
            int idx = r * PAINT_GRID_W + c;
            uint8_t stored = (uint8_t)((w->state_u[idx / 4] >> ((idx % 4) * 8)) & 0xFFu);
            if (stored != 0xFF) {
                uint32_t col = paint_palette[stored % PAINT_PALETTE];
                flush_rect(grid_x + c * cell_w, grid_y + r * cell_h,
                           cell_w, cell_h, rgb_hex(col), 255);
            }
        }
    }
    /* Grid lines */
    for (r = 0; r <= PAINT_GRID_H; ++r)
        flush_rect(grid_x, grid_y + r * cell_h, cell_w * PAINT_GRID_W, 1,
                   rgb_hex(th->stroke), 60);
    for (c = 0; c <= PAINT_GRID_W; ++c)
        flush_rect(grid_x + c * cell_w, grid_y, 1, cell_h * PAINT_GRID_H,
                   rgb_hex(th->stroke), 60);
}

static void app_click_paint(window_t *w, ui_rect_t body, int mx, int my, int button) {
    int pad = 12;
    int pal_h = 36;
    int grid_x = body.x + pad;
    int grid_y = body.y + pad + pal_h + 8;
    int grid_w = body.w - pad * 2;
    int grid_h = body.h - (grid_y - body.y) - pad;
    int cell_w = grid_w / PAINT_GRID_W;
    int cell_h = grid_h / PAINT_GRID_H;

    /* palette */
    if (my >= body.y + pad && my < body.y + pad + pal_h) {
        int i = (mx - (body.x + pad + 8)) / 32;
        if (i >= 0 && i < PAINT_PALETTE) w->state_i[0] = i;
        return;
    }
    if (mx >= grid_x && my >= grid_y &&
        mx < grid_x + cell_w * PAINT_GRID_W &&
        my < grid_y + cell_h * PAINT_GRID_H) {
        int c = (mx - grid_x) / cell_w;
        int r = (my - grid_y) / cell_h;
        int idx = r * PAINT_GRID_W + c;
        uint32_t mask = ~(0xFFu << ((idx % 4) * 8));
        uint32_t val = button == 2 ? 0xFFu : (uint32_t)w->state_i[0];
        w->state_u[idx / 4] = (w->state_u[idx / 4] & mask) | (val << ((idx % 4) * 8));
    }
}

/* Task Manager */
static void app_draw_taskmgr(window_t *w, ui_rect_t body) {
    const theme_t *th = current_theme();
    int pad = 14;
    int y = body.y + pad;
    int i;
    int line_h = 20;
    uint32_t text  = rgb_hex(th->text);
    uint32_t muted = rgb_hex(th->text_muted);
    uint32_t accent = rgb_hex(th->accent);
    uint32_t max_ticks = 1;
    (void)w;

    for (i = 0; i < PROC_MAX; ++i)
        if (g_processes[i].used && g_processes[i].cpu_ticks > max_ticks)
            max_ticks = g_processes[i].cpu_ticks;

    flush_text_scaled(body.x + pad, y, 2, text, 230, "Task Manager");
    y += 40;
    flush_text_alpha(body.x + pad, y, muted, 200, "Processes");
    y += 20;
    flush_rect(body.x + pad, y, body.w - pad * 2, 1, rgb_hex(th->stroke), 150);
    y += 8;

    for (i = 0; i < PROC_MAX; ++i) {
        if (!g_processes[i].used) continue;
        if (y + line_h > body.y + body.h - pad) break;
        {
            char line[96];
            size_t len = 0;
            int bar_w = body.w / 3;
            int bar_x = body.x + body.w - pad - bar_w;
            int pct = (int)((g_processes[i].cpu_ticks * 100u) / max_ticks);
            line[0] = 0;
            k_append_u32(line, &len, sizeof(line), (uint32_t)g_processes[i].pid);
            k_append_str(line, &len, sizeof(line), "  ");
            k_append_str(line, &len, sizeof(line), g_processes[i].is_user ? "USR" : "KRN");
            k_append_str(line, &len, sizeof(line), "  ");
            k_append_str(line, &len, sizeof(line), g_processes[i].name);
            flush_text_alpha(body.x + pad, y, text, 220, line);

            flush_round_rect(bar_x, y + 4, bar_w, 10, 5, rgb_hex(th->bg_alt), 200);
            flush_round_rect(bar_x, y + 4, (bar_w * pct) / 100, 10, 5, accent, 220);
            y += line_h;
        }
    }
}

/* Native Linux-compat browser shell */
static void app_draw_browser(window_t *w, ui_rect_t body) {
    const theme_t *th = current_theme();
    int pad = 12;
    int addr_h = 34;
    int y = body.y + pad;
    uint32_t text  = rgb_hex(th->text);
    uint32_t muted = rgb_hex(th->text_muted);
    uint32_t accent = rgb_hex(th->accent);

    /* Address bar */
    flush_round_rect(body.x + pad, y, body.w - pad * 2, addr_h, 8,
                     rgb_hex(th->bg_alt), 220);
    flush_circle(body.x + pad + 16, y + addr_h / 2, 4, accent, 240);
    flush_text_alpha(body.x + pad + 32, y + (addr_h - 16) / 2, text, 230,
                     g_browser_vm_requested ? "ridux-vm://browser" : "ridux-linux://browser");
    y += addr_h + 10;
    /* Page */
    flush_round_rect(body.x + pad, y, body.w - pad * 2, body.h - (y - body.y) - pad,
                     10, rgb_hex(th->bg_alt), 120);
    flush_text_scaled(body.x + pad + 18, y + 18, 2, text, 240,
                      g_browser_vm_requested ? "Browser VM" : "Linux Compat Browser");
    flush_text_alpha(body.x + pad + 18, y + 54, muted, 220,
                     g_browser_vm_requested ? g_browser_vm_status : "camino principal: Linux ABI nativo en Ridux");
    if (g_browser_vm_requested) {
        char line[96];
        size_t l = 0;
        line[0] = 0;
        k_append_str(line, &l, sizeof(line), "engine solicitado: ");
        k_append_str(line, &l, sizeof(line), g_browser_vm_engine);
        flush_text_alpha(body.x + pad + 18, y + 84, text, 220, line);
    } else {
        flush_text_alpha(body.x + pad + 18, y + 84, text, 220,
                         "usa: chromium / chrome / firefox");
    }
    flush_text_alpha(body.x + pad + 18, y + 116, text, 220,
                     g_browser_vm_requested ? "arquitectura: Ridux UI + Linux guest aislado" :
                     "arquitectura: syscalls Linux -> kernel Ridux");
    flush_text_alpha(body.x + pad + 18, y + 138, text, 220,
                     g_browser_vm_requested ? "motor objetivo: seL4/CAmkES VMM con Linux minimo" :
                     "objetivo: Firefox/Chromium/Steam sin VM");
    flush_text_alpha(body.x + pad + 18, y + 160, text, 220,
                     g_browser_vm_requested ? "display: framebuffer/input bridge hacia esta ventana" :
                     "fuentes: FreeBSD Linuxulator + Starnix + gVisor");
    flush_text_alpha(body.x + pad + 18, y + 182, text, 220,
                     g_browser_vm_requested ? "atajo host: make sel4-browser-vm-bootstrap" :
                     "fallback: browser-vm chromium");
    flush_text_alpha(body.x + pad + 18, y + 204, muted, 220,
                     "debug nativo: browser realrun /opt/chromium/chrome");
}

/* Firefox (native Ridux app shell over browser renderer) */
static void app_draw_firefox(window_t *w, ui_rect_t body) {
    const theme_t *th = current_theme();
    uint32_t text  = rgb_hex(th->text);
    uint32_t muted = rgb_hex(th->text_muted);
    uint32_t accent = rgb_hex(th->accent);
    int pad = 12;
    int tab = w->state_i[0];
    int i;
    int y;
    if (tab < 0 || tab > 2) tab = 0;
    w->state_i[0] = tab;

    flush_round_rect(body.x + pad, body.y + pad, body.w - pad * 2, 40, 10,
                     rgb_hex(th->bg_alt), 220);
    flush_text_alpha(body.x + pad + 14, body.y + pad + 12, text, 236, "Firefox (Linux Compat)");
    flush_text_alpha(body.x + body.w - 260, body.y + pad + 12, muted, 214,
                     "engine: Ridux Linux ABI");

    y = body.y + pad + 48;
    for (i = 0; i < 3; ++i) {
        int tx = body.x + pad + i * 112;
        int tw = 102;
        int thh = 30;
        const char *label = (i == 0) ? "Home" : (i == 1) ? "Docs" : "Compat";
        bool active = (i == tab);
        flush_round_rect(tx, y, tw, thh, 9,
                         active ? accent : rgb_hex(th->bg_alt),
                         active ? 210 : 170);
        ui_text_fit_center(tx + 6, y + 8, tw - 12,
                           active ? rgb_hex(0xFFFFFF) : text, 236, label);
    }

    y += 42;
    flush_round_rect(body.x + pad, y, body.w - pad * 2, body.h - (y - body.y) - pad, 10,
                     rgb_hex(th->bg_alt), 132);

    if (tab == 0) {
        flush_text_scaled(body.x + pad + 16, y + 14, 2, text, 236, "Firefox real via Linux compat");
        flush_text_alpha(body.x + pad + 16, y + 48, muted, 220,
                         "camino principal: binario Linux ejecutado por Ridux");
        flush_text_alpha(body.x + pad + 16, y + 74, text, 220,
                         "Quick actions:");
        flush_text_alpha(body.x + pad + 16, y + 98, text, 220,
                         "1. firefox");
        flush_text_alpha(body.x + pad + 16, y + 118, text, 220,
                         "2. browser-real firefox");
        flush_text_alpha(body.x + pad + 16, y + 138, text, 220,
                         "3. browser-vm firefox");
    } else if (tab == 1) {
        flush_text_scaled(body.x + pad + 16, y + 14, 2, text, 236, "Firefox App Docs");
        flush_text_alpha(body.x + pad + 16, y + 48, muted, 220,
                         "Esta app prioriza Linux ABI nativo; VM queda como fallback.");
        flush_text_alpha(body.x + pad + 16, y + 74, text, 220,
                         "Comandos utiles:");
        flush_text_alpha(body.x + pad + 16, y + 98, text, 220,
                         "- firefox");
        flush_text_alpha(body.x + pad + 16, y + 118, text, 220,
                         "- browser-real firefox");
        flush_text_alpha(body.x + pad + 16, y + 138, text, 220,
                         "- browser realrun /opt/firefox/firefox-bin");
    } else {
        int syscnt = 0;
        bool io_ok = (g_syscall_table[0] && g_syscall_table[1] && g_syscall_table[257]);
        bool exec_ok = (g_syscall_table[59] != 0);
        bool net_ok = (g_syscall_table[41] && g_syscall_table[42] && g_syscall_table[44] && g_syscall_table[45]);
        for (i = 0; i < SYSCALL_MAX; ++i) if (g_syscall_table[i]) ++syscnt;
        flush_text_scaled(body.x + pad + 16, y + 14, 2, text, 236, "Compat Status");
        flush_text_alpha(body.x + pad + 16, y + 48, text, 220,
                         io_ok ? "io/openat/read/write: ok" : "io/openat/read/write: pending");
        flush_text_alpha(body.x + pad + 16, y + 68, text, 220,
                         exec_ok ? "execve + ELF64 mapper: parcial" : "execve + ELF64 mapper: pending");
        flush_text_alpha(body.x + pad + 16, y + 88, text, 220,
                         net_ok ? "socket/connect/send/recv + HTTP bridge: ok (HTTPS parcial)"
                                : "socket/connect/send/recv: pending");
        {
            char line[96];
            size_t l = 0;
            line[0] = 0;
            k_append_str(line, &l, sizeof(line), "syscalls registrados: ");
            k_append_u32(line, &l, sizeof(line), (uint32_t)syscnt);
            flush_text_alpha(body.x + pad + 16, y + 108, muted, 220, line);
        }
        flush_text_alpha(body.x + pad + 16, y + 132, muted, 220,
                         "Camino principal: Linux ABI nativo; VM solo como fallback/oraculo.");
    }
}

static void app_click_firefox(window_t *w, ui_rect_t body, int mx, int my, int button) {
    int i;
    int pad = 12;
    int y = body.y + pad + 48;
    (void)button;
    for (i = 0; i < 3; ++i) {
        int tx = body.x + pad + i * 112;
        int tw = 102;
        int thh = 30;
        if (mx >= tx && mx < tx + tw && my >= y && my < y + thh) {
            w->state_i[0] = i;
            return;
        }
    }
}

/* Weather (mock using RTC to vary values) */
static void app_draw_weather(window_t *w, ui_rect_t body) {
    const theme_t *th = current_theme();
    int pad = 16;
    int y = body.y + pad;
    int temp = 18 + (int)(g_rtc_now.hour % 15);
    int humid = 40 + (int)(g_rtc_now.minute % 40);
    char line[32];
    size_t len;
    uint32_t text  = rgb_hex(th->text);
    uint32_t muted = rgb_hex(th->text_muted);
    uint32_t accent = rgb_hex(th->accent);
    (void)w;

    flush_text_scaled(body.x + pad, y, 2, text, 230, "Weather");
    y += 40;
    /* Big sun/cloud radial */
    flush_radial(body.x + body.w / 2, y + 70, 50,
                 rgb_hex(0xFFE27A), rgb_hex(th->bg_alt), 220);
    flush_circle(body.x + body.w / 2 + 30, y + 80, 30, rgb_hex(0xE6ECF5), 210);
    flush_circle(body.x + body.w / 2 + 55, y + 90, 22, rgb_hex(0xD5DCE8), 230);
    y += 160;

    line[0] = 0; len = 0;
    k_append_i32(line, &len, sizeof(line), temp);
    k_append_str(line, &len, sizeof(line), " C");
    flush_text_scaled(body.x + pad, y, 4, text, 240, line);
    y += 60;

    line[0] = 0; len = 0;
    k_append_str(line, &len, sizeof(line), "Humidity ");
    k_append_u32(line, &len, sizeof(line), (uint32_t)humid);
    k_append_str(line, &len, sizeof(line), "%");
    flush_text_alpha(body.x + pad, y, muted, 220, line);
    y += 22;
    flush_text_alpha(body.x + pad, y, muted, 220, "Partly cloudy");

    /* hourly bars */
    {
        int i;
        int bx = body.x + pad;
        int by = body.y + body.h - 60;
        int bw = (body.w - pad * 2) / 6;
        for (i = 0; i < 6; ++i) {
            int h = 12 + (int)((g_rtc_now.minute * (i + 1)) % 30);
            flush_round_rect(bx + i * bw + 8, by + (30 - h), bw - 16, h, 4,
                             accent, 210);
        }
    }
}

/* Store (mock) */
static const char *g_store_apps[] = {
    "Calculator", "Paint", "Clock", "Weather",
    "Browser", "Firefox", "Task Manager", "Notes", "Media",
    "Monitor", "Files", "Flush Inspector", "Settings"
};
static void app_draw_store(window_t *w, ui_rect_t body) {
    const theme_t *th = current_theme();
    int pad = 16;
    int y = body.y + pad;
    int i;
    uint32_t text  = rgb_hex(th->text);
    uint32_t muted = rgb_hex(th->text_muted);
    uint32_t accent = rgb_hex(th->accent);
    (void)w;

    flush_text_scaled(body.x + pad, y, 2, text, 230, "Ridux Store");
    y += 40;
    flush_round_rect(body.x + pad, y, body.w - pad * 2, 36, 8,
                     rgb_hex(th->bg_alt), 210);
    flush_text_alpha(body.x + pad + 12, y + 10, muted, 200, "Search apps...");
    y += 50;

    {
        int n = (int)(sizeof(g_store_apps) / sizeof(g_store_apps[0]));
        int cols = 3;
        int card_w = (body.w - pad * 2) / cols;
        int card_h = 96;
        for (i = 0; i < n; ++i) {
            int r = i / cols;
            int c = i % cols;
            int cx = body.x + pad + c * card_w;
            int cy = y + r * (card_h + 12);
            if (cy + card_h > body.y + body.h - pad) break;
            flush_round_rect(cx + 6, cy, card_w - 12, card_h, 10,
                             rgb_hex(th->bg_alt), 180);
            flush_round_rect(cx + 16, cy + 14, 48, 48, 8, accent, 200);
            flush_text_alpha(cx + 16, cy + 68, text, 230, g_store_apps[i]);
            flush_round_rect(cx + card_w - 70, cy + card_h - 26, 54, 18, 9,
                             accent, 220);
            flush_text_alpha(cx + card_w - 60, cy + card_h - 22, rgb_hex(0xFFFFFF),
                             230, "Open");
        }
    }
}

/* About */
static void app_draw_about(window_t *w, ui_rect_t body) {
    const theme_t *th = current_theme();
    int pad = 20;
    int y = body.y + pad;
    uint32_t text   = rgb_hex(th->text);
    uint32_t muted  = rgb_hex(th->text_muted);
    uint32_t accent = rgb_hex(th->accent);
    char line[96];
    size_t len;
    (void)w;

    flush_text_scaled(body.x + pad, y, 3, text, 240, "RiduxOS");
    y += 56;
    flush_text_alpha(body.x + pad, y, accent, 230, "Unix 0.4 Bloom");
    y += 24;
    flush_text_alpha(body.x + pad, y, muted, 220,
                     "Own kernel, own shell, own Flush API, own WM.");
    y += 24;
    flush_text_alpha(body.x + pad, y, muted, 220,
                     "Glassmorphism UI with blur, shadows, acrylic panels.");
    y += 30;

    line[0] = 0; len = 0;
    k_append_str(line, &len, sizeof(line), "Framebuffer: ");
    k_append_u32(line, &len, sizeof(line), g_fb.width);
    k_append_str(line, &len, sizeof(line), " x ");
    k_append_u32(line, &len, sizeof(line), g_fb.height);
    k_append_str(line, &len, sizeof(line), " @ ");
    k_append_u32(line, &len, sizeof(line), g_fb.bpp);
    k_append_str(line, &len, sizeof(line), " bpp");
    flush_text_alpha(body.x + pad, y, text, 220, line);
    y += 22;

    line[0] = 0; len = 0;
    k_append_str(line, &len, sizeof(line), "PCI devices: ");
    k_append_u32(line, &len, sizeof(line), (uint32_t)g_pci_device_count);
    flush_text_alpha(body.x + pad, y, text, 220, line);
    y += 22;

    line[0] = 0; len = 0;
    k_append_str(line, &len, sizeof(line), "Drivers: ");
    k_append_u32(line, &len, sizeof(line), (uint32_t)g_driver_count);
    k_append_str(line, &len, sizeof(line), "  Processes: ");
    k_append_u32(line, &len, sizeof(line), (uint32_t)proc_count());
    k_append_str(line, &len, sizeof(line), "  VFS: ");
    k_append_u32(line, &len, sizeof(line), (uint32_t)vfs_count());
    flush_text_alpha(body.x + pad, y, text, 220, line);
    y += 36;
    flush_text_alpha(body.x + pad, y, muted, 200,
                     "(c) 2025 Ridux Labs - hobby kernel.");
}

/* ---- Ring 3 Demo --------------------------------------------------
 *
 * This is the *only* WM-launchable app that actually runs at CPL=3.
 * Clicking inside its window stages an ELF64 user binary from the
 * rootfs (default `/bin/abi-smoke.elf`) as a runnable Ring 3 task and
 * returns immediately to the WM. The scheduler picks the task up on
 * its next tick and iretq's into ring 3.
 *
 * It demonstrates the *path forward* documented next to `app_t`: the
 * WM stays a single-process Ring 0 shell, while real applications ship
 * as ELF user binaries that talk back to the WM through the Linux ABI
 * the kernel already implements. The other in-tree widgets (Notes,
 * Snake, Paint, ...) remain Ring 0 callbacks by design \u2014 see the
 * comment block above `app_t` for the full architectural rationale. */
static const char *const RING3_DEMO_BINS[] = {
    "/bin/notes-r3.elf",   /* Ridux native WM protocol app (CPL=3 GUI). */
    "/bin/abi-smoke.elf",
    "/bin/x11-smoke.elf",
    "/bin/chrome.elf",
    "/bin/firefox.elf",
};
#define RING3_DEMO_BIN_COUNT ((int)(sizeof(RING3_DEMO_BINS)/sizeof(RING3_DEMO_BINS[0])))

static void app_draw_ring3(window_t *w, ui_rect_t body) {
    const theme_t *th = current_theme();
    uint32_t text   = rgb_hex(th->text);
    uint32_t muted  = rgb_hex(th->text_muted);
    uint32_t accent = rgb_hex(th->accent);
    int pad = 20;
    int y = body.y + pad;
    int sel = w->state_i[0];
    int last_pid = w->state_i[1];
    int last_rc  = w->state_i[2];
    int spawn_count = w->state_i[3];
    char line[96];
    size_t len;
    int i;
    if (sel < 0 || sel >= RING3_DEMO_BIN_COUNT) sel = 0;

    flush_text_scaled(body.x + pad, y, 3, text, 240, "Ring 3 Demo");
    y += 50;
    flush_text_alpha(body.x + pad, y, accent, 230,
                     "Real CPL=3 user-mode ELF launcher");
    y += 24;
    flush_text_alpha(body.x + pad, y, muted, 220,
                     "Click a binary below to stage it as a Ring 3 task.");
    y += 18;
    flush_text_alpha(body.x + pad, y, muted, 200,
                     "The scheduler picks it up on the next tick.");
    y += 28;

    /* Available binaries list (each row is a clickable target). */
    for (i = 0; i < RING3_DEMO_BIN_COUNT; ++i) {
        int rx = body.x + pad;
        int rw = body.w - pad * 2;
        int rh = 36;
        bool hov = (g_mouse_x >= rx && g_mouse_x < rx + rw &&
                    g_mouse_y >= y  && g_mouse_y < y  + rh);
        bool is_sel = (i == sel);
        flush_round_rect(rx, y, rw, rh, 10,
                         is_sel ? accent : rgb_hex(th->bg_alt),
                         is_sel ? 220 : (hov ? 200 : 160));
        if (hov && !is_sel) {
            flush_stroke_round(rx, y, rw, rh, 10, 1, accent, 160);
        }
        flush_text_alpha(rx + 14, y + 11,
                         is_sel ? rgb_hex(0xFFFFFF) : text, 230,
                         RING3_DEMO_BINS[i]);
        y += rh + 6;
    }
    y += 10;

    line[0] = 0; len = 0;
    k_append_str(line, &len, sizeof(line), "Last pid: ");
    if (last_pid > 0) k_append_u32(line, &len, sizeof(line), (uint32_t)last_pid);
    else              k_append_str(line, &len, sizeof(line), "(none)");
    k_append_str(line, &len, sizeof(line), "   spawned: ");
    k_append_u32(line, &len, sizeof(line), (uint32_t)spawn_count);
    flush_text_alpha(body.x + pad, y, text, 220, line);
    y += 22;

    if (last_rc < 0) {
        line[0] = 0; len = 0;
        k_append_str(line, &len, sizeof(line), "Last error rc=");
        k_append_u32(line, &len, sizeof(line), (uint32_t)(-last_rc));
        flush_text_alpha(body.x + pad, y, rgb_hex(th->danger), 220, line);
        y += 22;
    }
    flush_text_alpha(body.x + pad, body.y + body.h - 28, muted, 200,
                     "Click a row to switch + spawn that ELF.");
}

static void app_click_ring3(window_t *w, ui_rect_t body, int mx, int my, int button) {
    int pad = 20;
    int header_h = 50 + 24 + 18 + 28;
    int row_y = body.y + pad + header_h;
    int rh = 36;
    int gap = 6;
    int rx = body.x + pad;
    int rw = body.w - pad * 2;
    int i;
    int chosen = -1;
    char detail[256];
    int rc;
    (void)button;
    for (i = 0; i < RING3_DEMO_BIN_COUNT; ++i) {
        int top = row_y + i * (rh + gap);
        if (mx >= rx && mx < rx + rw && my >= top && my < top + rh) {
            chosen = i;
            break;
        }
    }
    if (chosen < 0) return;
    w->state_i[0] = chosen;
    detail[0] = 0;
    rc = compat5_spawn_user_elf_background(RING3_DEMO_BINS[chosen],
                                            detail, sizeof(detail));
    w->state_i[2] = rc;
    if (rc >= 0) {
        w->state_i[1] = rc;
        w->state_i[3] += 1;
    }
}

/* Media (mock player) */
static void app_draw_media(window_t *w, ui_rect_t body) {
    const theme_t *th = current_theme();
    int pad = 16;
    int y = body.y + pad;
    uint32_t text   = rgb_hex(th->text);
    uint32_t muted  = rgb_hex(th->text_muted);
    uint32_t accent = rgb_hex(th->accent);
    int bar_w = body.w - pad * 2;
    int prog = (int)(g_uptime_ticks % 100);
    (void)w;

    flush_text_scaled(body.x + pad, y, 2, text, 230, "Media");
    y += 40;

    /* Album */
    flush_round_rect(body.x + pad, y, 180, 180, 14, accent, 230);
    flush_round_rect(body.x + pad + 40, y + 40, 100, 100, 50, rgb_hex(th->bg_alt), 255);
    flush_circle(body.x + pad + 90, y + 90, 10, rgb_hex(th->text), 240);
    /* meta */
    flush_text_scaled(body.x + pad + 200, y + 20, 2, text, 240, "Bloom Theme");
    flush_text_alpha(body.x + pad + 200, y + 60, muted, 220, "Ridux Orchestra");
    flush_text_alpha(body.x + pad + 200, y + 86, muted, 200, "Album: Glassmorph");

    /* Progress */
    y += 196;
    flush_round_rect(body.x + pad, y, bar_w, 6, 3, rgb_hex(th->bg_alt), 220);
    flush_round_rect(body.x + pad, y, (bar_w * prog) / 100, 6, 3, accent, 240);
    y += 20;
    /* Controls */
    {
        int bx = body.x + body.w / 2 - 60;
        flush_circle(bx - 30, y + 20, 14, rgb_hex(th->bg_alt), 220);
        flush_circle(bx + 30, y + 20, 14, rgb_hex(th->bg_alt), 220);
        flush_circle(bx, y + 20, 22, accent, 230);
        /* play triangle */
        flush_line(bx - 5, y + 12, bx + 8, y + 20, 2, rgb_hex(0xFFFFFF), 255);
        flush_line(bx + 8, y + 20, bx - 5, y + 28, 2, rgb_hex(0xFFFFFF), 255);
        flush_line(bx - 5, y + 12, bx - 5, y + 28, 2, rgb_hex(0xFFFFFF), 255);
    }
}

/* ============================================================
 * [10.5] Tiny network stack state (loopback-only, no real NIC).
 * Types, global interfaces, ARP table and DNS records live here
 * because the Network Monitor app renders them directly.
 * The command-line handlers (ping/ifconfig/arp/dns) live later
 * in the shell section and re-use these globals.
 * ============================================================ */

typedef struct {
    uint8_t  mac[6];
    uint8_t  ip[4];
    uint8_t  netmask[4];
    uint8_t  gateway[4];
    bool     up;
    uint32_t rx_packets;
    uint32_t tx_packets;
    uint32_t rx_bytes;
    uint32_t tx_bytes;
} netif_t;
typedef struct {
    uint8_t ip[4];
    uint8_t mac[6];
    bool    used;
} arp_entry_t;
typedef struct {
    const char *name;
    uint8_t     ip[4];
} dns_record_t;

static netif_t g_net_lo = {
    {0x00,0x00,0x00,0x00,0x00,0x00}, {127,0,0,1}, {255,0,0,0}, {0,0,0,0},
    true, 0, 0, 0, 0
};
static netif_t g_net_eth0 = {
    {0x52,0x54,0x00,0x12,0x34,0x56}, {10,0,0,2}, {255,255,255,0}, {10,0,0,1},
    true, 0, 0, 0, 0
};
static arp_entry_t g_arp_table[8] = {
    {{10,0,0,1}, {0x52,0x54,0x00,0xAA,0xBB,0xCC}, true},
    {{10,0,0,3}, {0x52,0x54,0x00,0xAA,0xBB,0xCD}, true},
    {{127,0,0,1},{0x00,0x00,0x00,0x00,0x00,0x00}, true},
    {{0},{0},false},{{0},{0},false},{{0},{0},false},{{0},{0},false},{{0},{0},false}
};
static const dns_record_t g_dns[] = {
    { "localhost",   { 127, 0, 0, 1 } },
    { "ridux",       {  10, 0, 0, 2 } },
    { "gateway",     {  10, 0, 0, 1 } },
    { "ridux.local", {  10, 0, 0, 2 } },
    { "labs",        {  10, 0, 0, 3 } },
    { "example.com", { 93,184,216,34 } },
    { "google.com",  {142,250,190,14 } },
    { "github.com",  {140, 82,114,  4 } }
};
#define DNS_COUNT ((int)(sizeof(g_dns)/sizeof(g_dns[0])))

static void net_format_ip(const uint8_t ip[4], char *buf, size_t buf_size) {
    size_t len = 0;
    buf[0] = 0;
    k_append_u32(buf, &len, buf_size, ip[0]);
    k_append_ch(buf, &len, buf_size, '.');
    k_append_u32(buf, &len, buf_size, ip[1]);
    k_append_ch(buf, &len, buf_size, '.');
    k_append_u32(buf, &len, buf_size, ip[2]);
    k_append_ch(buf, &len, buf_size, '.');
    k_append_u32(buf, &len, buf_size, ip[3]);
}
static void net_format_mac(const uint8_t mac[6], char *buf, size_t buf_size) {
    size_t len = 0;
    int i;
    buf[0] = 0;
    for (i = 0; i < 6; ++i) {
        if (i) k_append_ch(buf, &len, buf_size, ':');
        k_append_hex(buf, &len, buf_size, mac[i], 2);
    }
}

/* ============================================================
 * NEW APPS: Editor, Minesweeper, Snake, Log Viewer, Network
 * Monitor, Process Tree, Sysinfo, Tic-Tac-Toe.
 * Every app follows the same contract: draw / click / key / tick.
 * State lives in window_t::state_i/state_u/state_s per-instance.
 * ============================================================ */

/* ---- Editor: full text editor with cursor, insert, delete, Ctrl-S save.
 * state_s[0] = target path (set via `edit <path>` shell command).
 * state_i[0] = caret byte offset into rw_data.
 * state_i[1] = dirty flag (0/1).
 * state_i[2] = scroll offset (first visible line index).  */
static const char *editor_current_path(window_t *w) {
    if (w->state_s[0][0] == 0) k_strlcpy(w->state_s[0], "/tmp/untitled.txt", sizeof(w->state_s[0]));
    return w->state_s[0];
}
static void editor_ensure_loaded(window_t *w) {
    const uint8_t *data; uint32_t size;
    const char *path = editor_current_path(w);
    if (w->state_u[7] == 0xA5A5A5A5u) return;
    if (vfs_read(path, &data, &size)) {
        /* Use w->lines as scrollback; state_s[1] already holds last-saved hash. */
        (void)data; (void)size;
    } else {
        vfs_write(path, "");
    }
    w->state_u[7] = 0xA5A5A5A5u;
}
static void editor_save(window_t *w) {
    const uint8_t *data; uint32_t size;
    const char *path = editor_current_path(w);
    (void)data; (void)size;
    /* Reconstruct buffer from stored text blob state_s[1..3] chained.
     * Keep it simple: flush whatever is currently stored at path via
     * touching it — the actual edits are written via key handler. */
    (void)vfs_touch(path);
    w->state_i[1] = 0;
}
static void app_draw_editor(window_t *w, ui_rect_t body) {
    const theme_t *th = current_theme();
    const char *path;
    const uint8_t *data; uint32_t size;
    int pad = 14;
    int y = body.y + pad;
    int line_h = 18;
    int max_lines;
    int line_no = 0;
    int i;
    uint32_t text = rgb_hex(th->text);
    uint32_t muted = rgb_hex(th->text_muted);
    uint32_t accent = rgb_hex(th->accent);
    char stat[96];
    size_t slen = 0;
    char buf[SHELL_LINE_LEN];
    size_t bl = 0;
    uint32_t caret_line = 0;
    uint32_t j;

    editor_ensure_loaded(w);
    path = editor_current_path(w);

    /* Header */
    flush_text_scaled(body.x + pad, y, 2, text, 235, "Editor");
    y += 36;
    stat[0] = 0;
    k_append_str(stat, &slen, sizeof(stat), path);
    if (w->state_i[1]) k_append_str(stat, &slen, sizeof(stat), "  [*]");
    k_append_str(stat, &slen, sizeof(stat), "   Ctrl-S save  Ctrl-O refresh");
    flush_text_alpha(body.x + pad, y, muted, 200, stat);
    y += 24;

    /* Body area (with scissor) */
    flush_round_rect(body.x + pad, y, body.w - pad * 2,
                     body.h - (y - body.y) - pad, 10,
                     rgb_hex(th->bg_alt), 190);
    flush_scissor_push(body.x + pad + 2, y + 2,
                       body.w - pad * 2 - 4,
                       body.h - (y - body.y) - pad - 4);

    if (!vfs_read(path, &data, &size)) {
        flush_text_alpha(body.x + pad + 10, y + 12, muted, 200, "(new file)");
        flush_scissor_pop();
        return;
    }

    max_lines = ((body.h - (y - body.y)) - pad * 2) / line_h;

    /* Compute caret line first. */
    for (j = 0; j < size && (int)j < w->state_i[0]; ++j) {
        if ((char)data[j] == '\n') ++caret_line;
    }

    line_no = 0;
    bl = 0; buf[0] = 0;
    for (i = 0; i < (int)size; ++i) {
        char c = (char)data[i];
        if (c == '\r') continue;
        if (c == '\n' || bl + 2 >= sizeof(buf)) {
            if (line_no >= w->state_i[2] && line_no - w->state_i[2] < max_lines) {
                int row = line_no - w->state_i[2];
                int ry = y + 12 + row * line_h;
                char num[8]; size_t nl = 0;
                num[0] = 0;
                k_append_u32_pad(num, &nl, sizeof(num), (uint32_t)(line_no + 1), 3, ' ');
                flush_text_alpha(body.x + pad + 6, ry, muted, 160, num);
                buf[bl] = 0;
                flush_text_alpha(body.x + pad + 38, ry, text, 230, buf);
                /* Caret */
                if ((uint32_t)line_no == caret_line) {
                    int col = w->state_i[0];
                    /* compute caret column within this line */
                    int jj;
                    int off = 0;
                    for (jj = 0; jj < i; ++jj) {
                        if ((char)data[jj] == '\n') off = jj + 1;
                    }
                    col = w->state_i[0] - off;
                    if (col < 0) col = 0;
                    if (col > (int)bl) col = (int)bl;
                    if ((g_uptime_ticks / 20u) & 1u) {
                        flush_rect(body.x + pad + 38 + col * 8, ry - 1, 2, 16, accent, 240);
                    }
                }
            }
            ++line_no;
            bl = 0; buf[0] = 0;
            if (c != '\n') buf[bl++] = c;
            continue;
        }
        buf[bl++] = c; buf[bl] = 0;
    }
    if (bl > 0 && line_no >= w->state_i[2] && line_no - w->state_i[2] < max_lines) {
        int row = line_no - w->state_i[2];
        int ry = y + 12 + row * line_h;
        char num[8]; size_t nl = 0;
        num[0] = 0;
        k_append_u32_pad(num, &nl, sizeof(num), (uint32_t)(line_no + 1), 3, ' ');
        flush_text_alpha(body.x + pad + 6, ry, muted, 160, num);
        flush_text_alpha(body.x + pad + 38, ry, text, 230, buf);
    }
    flush_scissor_pop();
}
static void app_key_editor(window_t *w, char ch, uint8_t sc) {
    const uint8_t *data; uint32_t size;
    char scratch[UI_FILE_BUF_MAX + 2];
    size_t len;
    int caret = w->state_i[0];
    const char *path = editor_current_path(w);

    /* Ctrl-S save. */
    if (g_ctrl_down && (ch == 's' || ch == 'S')) { editor_save(w); return; }
    /* PageUp / PageDown scroll (scancodes 0x49 / 0x51 via extended, but
     * those are handled in extended; fall back to j/k as vi-ish keys). */
    if (ch == 0 && sc == 0x49) { if (w->state_i[2] > 10) w->state_i[2] -= 10; else w->state_i[2] = 0; return; }
    if (ch == 0 && sc == 0x51) { w->state_i[2] += 10; return; }

    if (!vfs_read(path, &data, &size)) { vfs_write(path, ""); size = 0; data = NULL; }
    if (size >= UI_FILE_BUF_MAX - 2) return;

    len = size < sizeof(scratch) - 1 ? size : sizeof(scratch) - 1;
    k_memcpy(scratch, data, len);
    scratch[len] = 0;
    if (caret < 0) caret = 0;
    if (caret > (int)len) caret = (int)len;

    if (sc == 0x0E) {                 /* backspace */
        if (caret > 0) {
            k_memcpy(scratch + caret - 1, scratch + caret, len - caret);
            --len; scratch[len] = 0; --caret;
            w->state_i[1] = 1;
        }
    } else if (ch == '\n' || sc == 0x1C) {
        if (len + 1 < sizeof(scratch)) {
            k_memcpy(scratch + caret + 1, scratch + caret, len - caret);
            scratch[caret] = '\n';
            ++len; ++caret;
            scratch[len] = 0;
            w->state_i[1] = 1;
        }
    } else if (ch >= 32 && ch < 127) {
        if (len + 1 < sizeof(scratch)) {
            k_memcpy(scratch + caret + 1, scratch + caret, len - caret);
            scratch[caret] = ch;
            ++len; ++caret;
            scratch[len] = 0;
            w->state_i[1] = 1;
        }
    } else {
        return;
    }
    vfs_write(path, scratch);
    w->state_i[0] = caret;
}

/* ---- Minesweeper ----
 * state_u[0..9] = per-row bitmask of MINE positions (bit i = col i).
 * state_u[10..19] = revealed mask.
 * state_u[20..29] = flagged mask.
 * state_i[0] = seed token, state_i[1] = losses, state_i[2] = wins, state_i[3] = game_over (-1 loss, +1 win). */
#define MINE_GRID 10
static void mine_reset(window_t *w) {
    uint32_t s = 0xC0FFEE11u ^ (uint32_t)(g_pit_ticks * 2654435761u);
    int r, c, placed = 0;
    k_memset(w->state_u, 0, sizeof(w->state_u));
    for (r = 0; r < MINE_GRID; ++r) for (c = 0; c < MINE_GRID && placed < 15; ++c) {
        s = s * 1664525u + 1013904223u;
        if ((s & 0x7) == 0) {
            w->state_u[r] |= (1u << c);
            ++placed;
        }
    }
    w->state_i[3] = 0;
}
static int mine_neighbors(const window_t *w, int r, int c) {
    int dr, dc, n = 0;
    for (dr = -1; dr <= 1; ++dr) for (dc = -1; dc <= 1; ++dc) {
        int rr = r + dr, cc = c + dc;
        if (rr < 0 || rr >= MINE_GRID || cc < 0 || cc >= MINE_GRID) continue;
        if (w->state_u[rr] & (1u << cc)) ++n;
    }
    return n;
}
static void mine_flood(window_t *w, int r, int c) {
    if (r < 0 || r >= MINE_GRID || c < 0 || c >= MINE_GRID) return;
    if (w->state_u[10 + r] & (1u << c)) return;
    if (w->state_u[20 + r] & (1u << c)) return;
    if (w->state_u[r] & (1u << c)) return;
    w->state_u[10 + r] |= (1u << c);
    if (mine_neighbors(w, r, c) == 0) {
        int dr, dc;
        for (dr = -1; dr <= 1; ++dr) for (dc = -1; dc <= 1; ++dc)
            if (dr || dc) mine_flood(w, r + dr, c + dc);
    }
}
static void app_draw_mine(window_t *w, ui_rect_t body) {
    const theme_t *th = current_theme();
    int cell = 28;
    int grid_w = MINE_GRID * cell;
    int gx = body.x + (body.w - grid_w) / 2;
    int gy = body.y + 60;
    int r, c;
    uint32_t text = rgb_hex(th->text);
    uint32_t muted = rgb_hex(th->text_muted);
    uint32_t accent = rgb_hex(th->accent);
    char hdr[64]; size_t hl = 0;

    if (w->state_u[30] != 0xBEEFCAFEu) { mine_reset(w); w->state_u[30] = 0xBEEFCAFEu; }

    flush_text_scaled(body.x + 18, body.y + 12, 2, text, 235, "Minesweeper");
    hdr[0] = 0;
    k_append_str(hdr, &hl, sizeof(hdr), "Left click reveals, right flags. W=");
    k_append_u32(hdr, &hl, sizeof(hdr), (uint32_t)w->state_i[2]);
    k_append_str(hdr, &hl, sizeof(hdr), " L=");
    k_append_u32(hdr, &hl, sizeof(hdr), (uint32_t)w->state_i[1]);
    if (w->state_i[3] > 0) k_append_str(hdr, &hl, sizeof(hdr), "  YOU WON!");
    if (w->state_i[3] < 0) k_append_str(hdr, &hl, sizeof(hdr), "  BOOM!");
    flush_text_alpha(body.x + 18, body.y + 40, muted, 200, hdr);

    for (r = 0; r < MINE_GRID; ++r) {
        for (c = 0; c < MINE_GRID; ++c) {
            int x = gx + c * cell;
            int y = gy + r * cell;
            bool rev   = (w->state_u[10 + r] & (1u << c)) != 0;
            bool flag  = (w->state_u[20 + r] & (1u << c)) != 0;
            bool mine  = (w->state_u[r] & (1u << c)) != 0;
            if (rev) {
                flush_round_rect(x + 1, y + 1, cell - 2, cell - 2, 3,
                                 rgb_hex(th->bg_alt), 200);
                if (mine) {
                    flush_circle(x + cell/2, y + cell/2, 6,
                                 rgb_hex(th->danger), 240);
                } else {
                    int n = mine_neighbors(w, r, c);
                    if (n > 0) {
                        char s[2]; s[0] = '0' + n; s[1] = 0;
                        flush_text_alpha(x + cell/2 - 3, y + cell/2 - 8, text, 230, s);
                    }
                }
            } else {
                flush_round_rect(x + 1, y + 1, cell - 2, cell - 2, 3, accent, 120);
                if (flag) flush_circle(x + cell/2, y + cell/2, 4,
                                       rgb_hex(th->danger), 240);
            }
            flush_stroke_round(x, y, cell, cell, 5, 1, rgb_hex(th->stroke), 80);
        }
    }
}
static void app_click_mine(window_t *w, ui_rect_t body, int mx, int my, int button) {
    int cell = 28;
    int grid_w = MINE_GRID * cell;
    int gx = body.x + (body.w - grid_w) / 2;
    int gy = body.y + 60;
    int r, c;
    int total_rev = 0, safe_cells = MINE_GRID * MINE_GRID;
    if (mx < gx || my < gy || mx >= gx + grid_w || my >= gy + MINE_GRID * cell) {
        /* Click outside grid: reset if game over. */
        if (w->state_i[3] != 0) mine_reset(w);
        return;
    }
    c = (mx - gx) / cell;
    r = (my - gy) / cell;
    if (r < 0 || r >= MINE_GRID || c < 0 || c >= MINE_GRID) return;
    if (w->state_i[3] != 0) return;  /* game done, wait for restart click outside */
    if (button == 2) {
        w->state_u[20 + r] ^= (1u << c);
        return;
    }
    if (w->state_u[r] & (1u << c)) {
        w->state_u[10 + r] |= (1u << c);
        w->state_i[3] = -1;
        w->state_i[1]++;
        return;
    }
    mine_flood(w, r, c);
    /* Check win: all non-mine cells revealed. */
    for (r = 0; r < MINE_GRID; ++r) {
        for (c = 0; c < MINE_GRID; ++c) {
            bool mine = (w->state_u[r] & (1u << c)) != 0;
            bool rev  = (w->state_u[10 + r] & (1u << c)) != 0;
            if (!mine) { --safe_cells; if (rev) ++total_rev; }
        }
    }
    safe_cells = MINE_GRID * MINE_GRID - 15;
    if (total_rev >= safe_cells) { w->state_i[3] = 1; w->state_i[2]++; }
}

/* ---- Snake ----
 * state_i[0] = head x, state_i[1] = head y, state_i[2] = dir (0=R,1=D,2=L,3=U)
 * state_i[3] = length, state_i[4] = food x, state_i[5] = food y, state_i[6] = game over
 * state_u[0..39] = snake segments packed as (y<<8)|x
 * state_i[7] = score. */
#define SNAKE_GRID_W 20
#define SNAKE_GRID_H 12
static void snake_spawn_food(window_t *w) {
    uint32_t s = (uint32_t)(g_pit_ticks * 2654435761u) ^ 0xDEADBEEFu;
    int x, y, tries = 0;
    do {
        s = s * 1664525u + 1013904223u;
        x = (int)(s % SNAKE_GRID_W);
        y = (int)((s / SNAKE_GRID_W) % SNAKE_GRID_H);
        ++tries;
        if (tries > 50) break;
    } while (0);
    w->state_i[4] = x;
    w->state_i[5] = y;
}
static void snake_reset(window_t *w) {
    int i;
    w->state_i[0] = SNAKE_GRID_W / 2;
    w->state_i[1] = SNAKE_GRID_H / 2;
    w->state_i[2] = 0;
    w->state_i[3] = 3;
    w->state_i[6] = 0;
    w->state_i[7] = 0;
    for (i = 0; i < 40; ++i) w->state_u[i] = 0;
    w->state_u[0] = (uint32_t)((w->state_i[1] << 8) | w->state_i[0]);
    snake_spawn_food(w);
}
static void app_tick_snake(window_t *w) {
    int hx, hy, nx, ny, i;
    if (w->state_u[7] != 0xABCD1234u) { snake_reset(w); w->state_u[7] = 0xABCD1234u; }
    if (w->state_i[6]) return;
    /* Only advance every ~8 ticks for playability. */
    if ((g_pit_ticks & 7u) != 0u) return;
    hx = w->state_i[0]; hy = w->state_i[1];
    switch (w->state_i[2]) {
        case 0: nx = hx + 1; ny = hy; break;
        case 1: nx = hx; ny = hy + 1; break;
        case 2: nx = hx - 1; ny = hy; break;
        default: nx = hx; ny = hy - 1; break;
    }
    if (nx < 0 || nx >= SNAKE_GRID_W || ny < 0 || ny >= SNAKE_GRID_H) {
        w->state_i[6] = 1; return;
    }
    /* Self collision */
    for (i = 0; i < w->state_i[3]; ++i) {
        int sx = (int)(w->state_u[i] & 0xFF);
        int sy = (int)((w->state_u[i] >> 8) & 0xFF);
        if (sx == nx && sy == ny) { w->state_i[6] = 1; return; }
    }
    /* Shift body */
    for (i = 39; i > 0; --i) w->state_u[i] = w->state_u[i - 1];
    w->state_u[0] = (uint32_t)((ny << 8) | nx);
    w->state_i[0] = nx; w->state_i[1] = ny;
    if (nx == w->state_i[4] && ny == w->state_i[5]) {
        if (w->state_i[3] < 40) ++w->state_i[3];
        ++w->state_i[7];
        snake_spawn_food(w);
    }
}
static void app_key_snake(window_t *w, char ch, uint8_t sc) {
    (void)sc;
    if (w->state_i[6] && (ch == ' ' || ch == 'r' || ch == 'R')) { snake_reset(w); return; }
    if (ch == 'd' || ch == 'D') { if (w->state_i[2] != 2) w->state_i[2] = 0; }
    else if (ch == 's' || ch == 'S') { if (w->state_i[2] != 3) w->state_i[2] = 1; }
    else if (ch == 'a' || ch == 'A') { if (w->state_i[2] != 0) w->state_i[2] = 2; }
    else if (ch == 'w' || ch == 'W') { if (w->state_i[2] != 1) w->state_i[2] = 3; }
}
static void app_draw_snake(window_t *w, ui_rect_t body) {
    const theme_t *th = current_theme();
    int cell = 22;
    int grid_w = SNAKE_GRID_W * cell;
    int grid_h = SNAKE_GRID_H * cell;
    int gx = body.x + (body.w - grid_w) / 2;
    int gy = body.y + 60;
    int i;
    uint32_t text = rgb_hex(th->text);
    uint32_t muted = rgb_hex(th->text_muted);
    uint32_t accent = rgb_hex(th->accent);
    char hdr[48]; size_t hl = 0;
    if (w->state_u[7] != 0xABCD1234u) { snake_reset(w); w->state_u[7] = 0xABCD1234u; }
    flush_text_scaled(body.x + 18, body.y + 12, 2, text, 235, "Snake");
    hdr[0] = 0;
    k_append_str(hdr, &hl, sizeof(hdr), "WASD move   score=");
    k_append_u32(hdr, &hl, sizeof(hdr), (uint32_t)w->state_i[7]);
    if (w->state_i[6]) k_append_str(hdr, &hl, sizeof(hdr), "  GAME OVER (press R)");
    flush_text_alpha(body.x + 18, body.y + 40, muted, 200, hdr);

    flush_round_rect(gx - 4, gy - 4, grid_w + 8, grid_h + 8, 6,
                     rgb_hex(th->bg_alt), 160);
    /* food */
    flush_circle(gx + w->state_i[4] * cell + cell/2,
                 gy + w->state_i[5] * cell + cell/2, cell/3,
                 rgb_hex(th->danger), 245);
    /* body */
    for (i = 0; i < w->state_i[3]; ++i) {
        int sx = (int)(w->state_u[i] & 0xFF);
        int sy = (int)((w->state_u[i] >> 8) & 0xFF);
        uint8_t a = (uint8_t)(240 - i * 3);
        flush_round_rect(gx + sx * cell + 1, gy + sy * cell + 1,
                         cell - 2, cell - 2, 5, accent, a);
    }
}

/* Log Viewer: scroll through kernel log ring */
static void app_draw_logviewer(window_t *w, ui_rect_t body) {
    const theme_t *th = current_theme();
    int pad = 14, y = body.y + pad, line_h = 18, i;
    uint32_t text = rgb_hex(th->text);
    uint32_t muted = rgb_hex(th->text_muted);
    uint32_t cnt = klog_ring_count();
    int start = w->state_i[0];
    int max_lines = (body.h - (y - body.y) - 60) / line_h;
    char hdr[64]; size_t hl = 0;
    flush_text_scaled(body.x + pad, y, 2, text, 235, "Kernel Log");
    y += 36;
    hdr[0] = 0;
    k_append_u32(hdr, &hl, sizeof(hdr), cnt);
    k_append_str(hdr, &hl, sizeof(hdr), " lines. Click top/bottom half to scroll.");
    flush_text_alpha(body.x + pad, y, muted, 200, hdr);
    y += 22;
    if (start < 0) start = 0;
    if ((uint32_t)start >= cnt) start = (int)cnt - 1;
    if (start < 0) start = 0;
    w->state_i[0] = start;
    flush_round_rect(body.x + pad, y, body.w - pad * 2,
                     body.h - (y - body.y) - pad, 8,
                     rgb_hex(th->bg_alt), 190);
    for (i = 0; i < max_lines && (uint32_t)(start + i) < cnt; ++i) {
        const char *l = klog_ring_get((uint32_t)(start + i));
        if (l) flush_text_alpha(body.x + pad + 8, y + 10 + i * line_h,
                                text, 220, l);
    }
}
static void app_click_logviewer(window_t *w, ui_rect_t body, int mx, int my, int button) {
    (void)button;
    if (mx < body.x + body.w / 2) {
        w->state_i[0] -= 5;
        if (w->state_i[0] < 0) w->state_i[0] = 0;
    } else {
        w->state_i[0] += 5;
    }
}

/* Network Monitor */
static void app_draw_netmon(window_t *w, ui_rect_t body) {
    const theme_t *th = current_theme();
    int pad = 16, y = body.y + pad, line_h = 18, i;
    uint32_t text = rgb_hex(th->text);
    uint32_t muted = rgb_hex(th->text_muted);
    uint32_t accent = rgb_hex(th->accent);
    char buf[96]; size_t l;
    char ip[16], mac[24];
    (void)w;
    flush_text_scaled(body.x + pad, y, 2, text, 235, "Network");
    y += 36;
    flush_text_alpha(body.x + pad, y, muted, 200, "Interfaces");
    y += 22;
    for (i = 0; i < 2; ++i) {
        netif_t *n = (i == 0) ? &g_net_lo : &g_net_eth0;
        const char *name = (i == 0) ? "lo  " : "eth0";
        flush_round_rect(body.x + pad, y, body.w - pad * 2, 58, 8,
                         rgb_hex(th->bg_alt), 180);
        flush_circle(body.x + pad + 14, y + 14, 5,
                     n->up ? rgb_hex(th->success) : rgb_hex(th->danger), 240);
        net_format_ip(n->ip, ip, sizeof(ip));
        net_format_mac(n->mac, mac, sizeof(mac));
        l = 0; buf[0] = 0;
        k_append_str(buf, &l, sizeof(buf), name);
        k_append_str(buf, &l, sizeof(buf), "  ");
        k_append_str(buf, &l, sizeof(buf), ip);
        k_append_str(buf, &l, sizeof(buf), "  ");
        k_append_str(buf, &l, sizeof(buf), mac);
        flush_text_alpha(body.x + pad + 30, y + 6, text, 230, buf);
        l = 0; buf[0] = 0;
        k_append_str(buf, &l, sizeof(buf), "  RX ");
        k_append_u32(buf, &l, sizeof(buf), n->rx_packets);
        k_append_str(buf, &l, sizeof(buf), " pkt   TX ");
        k_append_u32(buf, &l, sizeof(buf), n->tx_packets);
        k_append_str(buf, &l, sizeof(buf), " pkt   RX ");
        k_append_u32(buf, &l, sizeof(buf), n->rx_bytes);
        k_append_str(buf, &l, sizeof(buf), " B  TX ");
        k_append_u32(buf, &l, sizeof(buf), n->tx_bytes);
        k_append_str(buf, &l, sizeof(buf), " B");
        flush_text_alpha(body.x + pad + 30, y + 28, muted, 210, buf);
        y += 72;
    }
    flush_text_alpha(body.x + pad, y, muted, 200, "ARP table");
    y += 22;
    for (i = 0; i < 8; ++i) {
        if (!g_arp_table[i].used) continue;
        net_format_ip(g_arp_table[i].ip, ip, sizeof(ip));
        net_format_mac(g_arp_table[i].mac, mac, sizeof(mac));
        l = 0; buf[0] = 0;
        k_append_str(buf, &l, sizeof(buf), ip);
        while (l < 18 && l + 1 < sizeof(buf)) buf[l++] = ' ';
        buf[l] = 0;
        k_append_str(buf, &l, sizeof(buf), mac);
        flush_text_alpha(body.x + pad + 16, y, text, 220, buf);
        y += line_h;
    }
    (void)accent; (void)line_h;
}

/* Process Tree */
static void app_draw_proctree(window_t *w, ui_rect_t body) {
    const theme_t *th = current_theme();
    int pad = 16, y = body.y + pad, i;
    uint32_t text = rgb_hex(th->text);
    uint32_t muted = rgb_hex(th->text_muted);
    uint32_t accent = rgb_hex(th->accent);
    (void)w;
    flush_text_scaled(body.x + pad, y, 2, text, 235, "Processes");
    y += 36;
    for (i = 0; i < PROC_MAX; ++i) {
        char line[SHELL_LINE_LEN]; size_t l = 0;
        int depth;
        if (!g_processes[i].used) continue;
        depth = (i == 0) ? 0 : 1;
        flush_round_rect(body.x + pad + depth * 20, y, body.w - pad * 2 - depth * 20,
                         26, 6, rgb_hex(th->bg_alt),
                         (g_processes[i].state == PROC_RUNNING) ? 220 : 180);
        flush_circle(body.x + pad + depth * 20 + 14, y + 13, 4,
                     (g_processes[i].state == PROC_RUNNING) ? accent : muted, 240);
        line[0] = 0;
        k_append_str(line, &l, sizeof(line), "pid ");
        k_append_u32_pad(line, &l, sizeof(line), (uint32_t)g_processes[i].pid, 3, ' ');
        k_append_ch(line, &l, sizeof(line), ' ');
        k_append_str(line, &l, sizeof(line), g_processes[i].name);
        while (l < 34 && l + 1 < sizeof(line)) line[l++] = ' ';
        line[l] = 0;
        k_append_str(line, &l, sizeof(line), proc_state_name(g_processes[i].state));
        k_append_str(line, &l, sizeof(line), "  cpu=");
        k_append_u32(line, &l, sizeof(line), g_processes[i].cpu_ticks);
        flush_text_alpha(body.x + pad + depth * 20 + 26, y + 6, text, 220, line);
        y += 30;
        if (y > body.y + body.h - 30) break;
    }
}

/* Sysinfo */
static void app_draw_sysinfo(window_t *w, ui_rect_t body) {
    const theme_t *th = current_theme();
    int pad = 20, y = body.y + pad, line_h = 22;
    uint32_t text = rgb_hex(th->text);
    uint32_t muted = rgb_hex(th->text_muted);
    uint32_t accent = rgb_hex(th->accent);
    char line[SHELL_LINE_LEN]; size_t l;
    uint32_t used, peak, allocs, frees;
    (void)w;
    heap_stats(&used, &peak, &allocs, &frees);
    flush_text_scaled(body.x + pad, y, 3, accent, 240, "RiduxOS");
    y += 54;
    flush_text_scaled(body.x + pad, y, 2, text, 230, "Unix 0.4 Bloom");
    y += 36;
    flush_text_alpha(body.x + pad, y, muted, 210, "Hobby kernel - 32-bit protected mode x86");
    y += line_h + 10;

#define ROW(LBL, VAL_CODE)                                                       \
    do {                                                                         \
        l = 0;                                                                   \
        line[0] = 0;                                                             \
        k_append_str(line, &l, sizeof(line), LBL);                               \
        while (l < 20 && l + 1 < sizeof(line)) {                                 \
            line[l++] = ' ';                                                     \
        }                                                                        \
        line[l] = 0;                                                             \
        VAL_CODE;                                                                \
        flush_text_alpha(body.x + pad, y, text, 230, line);                      \
        y += line_h;                                                             \
    } while (0)

    ROW("CPU",       { k_append_str(line, &l, sizeof(line),
#if defined(__x86_64__)
                                  "x86_64 (long mode, TSC frame throttle)"
#else
                                  "i386 (protected mode, TSC frame throttle)"
#endif
                                  );
                     k_append_str(line, &l, sizeof(line), "  logical=");
                     k_append_u32(line, &l, sizeof(line), g_cpu_logical_count);
                     k_append_str(line, &l, sizeof(line),
                                  g_cpu_logical_count > 1u ? "  SMP topo ok (BSP scheduler)"
                                                           : "  single-core/BSP");
                   });
    ROW("Framebuffer", { k_append_u32(line, &l, sizeof(line), g_fb.width); k_append_ch(line, &l, sizeof(line), 'x'); k_append_u32(line, &l, sizeof(line), g_fb.height); k_append_ch(line, &l, sizeof(line), 'x'); k_append_u32(line, &l, sizeof(line), g_fb.bpp); k_append_str(line, &l, sizeof(line), g_fb_fast_bgra ? "  (BGRA fast path)" : "  (generic)"); });
    ROW("GPU accel",   k_append_str(line, &l, sizeof(line), g_gpu_accel_kind));
    ROW("Heap",        { k_append_u32(line, &l, sizeof(line), used / 1024u); k_append_str(line, &l, sizeof(line), "K / "); k_append_u32(line, &l, sizeof(line), HEAP_SIZE / 1024u); k_append_str(line, &l, sizeof(line), "K (peak "); k_append_u32(line, &l, sizeof(line), peak / 1024u); k_append_str(line, &l, sizeof(line), "K)"); });
    ROW("Allocs",      { k_append_u32(line, &l, sizeof(line), allocs); k_append_str(line, &l, sizeof(line), " allocs / "); k_append_u32(line, &l, sizeof(line), frees); k_append_str(line, &l, sizeof(line), " frees"); });
    ROW("Processes",   k_append_u32(line, &l, sizeof(line), (uint32_t)proc_count()));
    ROW("Drivers",     k_append_u32(line, &l, sizeof(line), (uint32_t)g_driver_count));
    ROW("PCI devices", k_append_u32(line, &l, sizeof(line), (uint32_t)g_pci_device_count));
    ROW("VFS files",   k_append_u32(line, &l, sizeof(line), (uint32_t)vfs_count()));
    ROW("PIT ticks",   k_append_u32(line, &l, sizeof(line), (uint32_t)g_pit_ticks));
    ROW("Frames",      k_append_u32(line, &l, sizeof(line), g_frame_counter));
#undef ROW
}

/* ---- Tic-Tac-Toe ----
 * state_i[0..8] = cells (0 empty, 1 X, 2 O)
 * state_i[9] = whose turn (1 X, 2 O)
 * state_i[10] = winner (0 none, 1 X, 2 O, 3 draw)
 * state_i[11..12] = scores X / O */
static void ttt_reset(window_t *w) {
    int i; for (i = 0; i < 9; ++i) w->state_i[i] = 0;
    w->state_i[9] = 1;
    w->state_i[10] = 0;
}
static int ttt_check(const window_t *w) {
    static const int L[8][3] = {
        {0,1,2},{3,4,5},{6,7,8},
        {0,3,6},{1,4,7},{2,5,8},
        {0,4,8},{2,4,6}
    };
    int i;
    for (i = 0; i < 8; ++i) {
        int a = w->state_i[L[i][0]];
        if (a && a == w->state_i[L[i][1]] && a == w->state_i[L[i][2]]) return a;
    }
    for (i = 0; i < 9; ++i) if (w->state_i[i] == 0) return 0;
    return 3;
}
static void app_draw_ttt(window_t *w, ui_rect_t body) {
    const theme_t *th = current_theme();
    int cell = 80, i;
    int gx = body.x + (body.w - cell * 3) / 2;
    int gy = body.y + 70;
    uint32_t text = rgb_hex(th->text);
    uint32_t muted = rgb_hex(th->text_muted);
    uint32_t accent = rgb_hex(th->accent);
    char hdr[64]; size_t hl = 0;
    if (w->state_u[7] != 0x1337C0DEu) { ttt_reset(w); w->state_u[7] = 0x1337C0DEu; }
    flush_text_scaled(body.x + 18, body.y + 12, 2, text, 235, "Tic-Tac-Toe");
    hdr[0] = 0;
    k_append_str(hdr, &hl, sizeof(hdr), "X=");
    k_append_u32(hdr, &hl, sizeof(hdr), (uint32_t)w->state_i[11]);
    k_append_str(hdr, &hl, sizeof(hdr), "  O=");
    k_append_u32(hdr, &hl, sizeof(hdr), (uint32_t)w->state_i[12]);
    k_append_str(hdr, &hl, sizeof(hdr), "  turn: ");
    k_append_ch(hdr, &hl, sizeof(hdr), w->state_i[9] == 1 ? 'X' : 'O');
    if (w->state_i[10] == 1) k_append_str(hdr, &hl, sizeof(hdr), "  X WINS!");
    else if (w->state_i[10] == 2) k_append_str(hdr, &hl, sizeof(hdr), "  O WINS!");
    else if (w->state_i[10] == 3) k_append_str(hdr, &hl, sizeof(hdr), "  DRAW");
    flush_text_alpha(body.x + 18, body.y + 44, muted, 200, hdr);
    for (i = 0; i < 9; ++i) {
        int r = i / 3, c = i % 3;
        int x = gx + c * cell, y = gy + r * cell;
        flush_round_rect(x + 2, y + 2, cell - 4, cell - 4, 8,
                         rgb_hex(th->bg_alt), 180);
        flush_stroke_round(x, y, cell, cell, 10, 1, rgb_hex(th->stroke), 120);
        if (w->state_i[i] == 1) {
            flush_line(x + 18, y + 18, x + cell - 18, y + cell - 18, 3, accent, 240);
            flush_line(x + cell - 18, y + 18, x + 18, y + cell - 18, 3, accent, 240);
        } else if (w->state_i[i] == 2) {
            flush_ring(x + cell/2, y + cell/2, cell/2 - 18, 3,
                       rgb_hex(th->danger), 240);
        }
    }
    if (w->state_i[10]) flush_text_alpha(body.x + 18, body.y + body.h - 24, muted, 180, "Click board to restart.");
}
static void app_click_ttt(window_t *w, ui_rect_t body, int mx, int my, int button) {
    int cell = 80;
    int gx = body.x + (body.w - cell * 3) / 2;
    int gy = body.y + 70;
    int c, r, idx;
    (void)button;
    if (w->state_i[10]) { ttt_reset(w); return; }
    if (mx < gx || my < gy || mx >= gx + cell * 3 || my >= gy + cell * 3) return;
    c = (mx - gx) / cell;
    r = (my - gy) / cell;
    idx = r * 3 + c;
    if (w->state_i[idx] != 0) return;
    w->state_i[idx] = w->state_i[9];
    w->state_i[9] = (w->state_i[9] == 1) ? 2 : 1;
    w->state_i[10] = ttt_check(w);
    if (w->state_i[10] == 1) w->state_i[11]++;
    else if (w->state_i[10] == 2) w->state_i[12]++;
}

/* Settings click (select theme) */
static void app_click_settings(window_t *w, ui_rect_t body, int mx, int my, int button) {
    int pad = 16;
    int y = body.y + pad + 40 + 22;
    int i;
    (void)w; (void)button;
    if (my < y || my > y + 56) return;
    for (i = 0; i < THEME_COUNT; ++i) {
        int sx = body.x + pad + i * 130;
        if (mx >= sx && mx < sx + 120) {
            g_theme_index = (uint32_t)i;
            return;
        }
    }
}

/* Store click: open apps */
static void app_click_store(window_t *w, ui_rect_t body, int mx, int my, int button) {
    /* Map g_store_apps label to app_id via name match. */
    int pad = 16;
    int y = body.y + pad + 40 + 50;
    int n = (int)(sizeof(g_store_apps) / sizeof(g_store_apps[0]));
    int cols = 3;
    int card_w = (body.w - pad * 2) / cols;
    int card_h = 96;
    int i;
    (void)w; (void)button;
    for (i = 0; i < n; ++i) {
        int r = i / cols;
        int c = i % cols;
        int cx = body.x + pad + c * card_w + 6;
        int cy = y + r * (card_h + 12);
        if (mx >= cx && mx < cx + card_w - 12 && my >= cy && my < cy + card_h) {
            int idx = app_index_by_name(g_store_apps[i]);
            if (idx >= 0) {
                if (app_launch_user_process_for_id(g_apps[idx].app_id)) return;
                wm_set_visible_by_id(g_apps[idx].window_id, true);
                wm_focus_by_id(g_apps[idx].window_id);
            }
            return;
        }
    }
}

/* App registry bootstrap */
static void apps_bootstrap(void) {
    ui_rect_t d = g_ui.desktop;
    int sw = d.w, sh = d.h;
    int i;
    int window_ids[APP_MAX];
    int n;

    /* Windows with reasonable defaults. */
    int term_w = k_clamp_i(sw * 52 / 100, 560, 900);
    int term_h = k_clamp_i(sh * 48 / 100, 300, 520);
    int mon_w  = k_clamp_i(sw * 36 / 100, 360, 520);
    int mon_h  = k_clamp_i(sh * 50 / 100, 300, 540);
    int files_w = k_clamp_i(sw * 46 / 100, 460, 720);
    int files_h = k_clamp_i(sh * 56 / 100, 320, 560);
    int notes_w = k_clamp_i(sw * 30 / 100, 300, 480);
    int notes_h = k_clamp_i(sh * 48 / 100, 260, 460);
    int generic_w = k_clamp_i(sw * 40 / 100, 420, 660);
    int generic_h = k_clamp_i(sh * 52 / 100, 300, 520);
    int calc_w = 340;
    int calc_h = 460;
    int clock_w = 360;
    int clock_h = 420;
    int paint_w = k_clamp_i(sw * 50 / 100, 520, 820);
    int paint_h = k_clamp_i(sh * 62 / 100, 360, 620);

    int cx = d.x + d.w / 2;
    int cy = d.y + d.h / 2;

    for (i = 0; i < APP_MAX; ++i) window_ids[i] = 0;
    for (i = 0; i < APP_MAX; ++i) g_app_launch_count[i] = 0;

    /* La consola vieja queda escondida como salvavidas, pero el escritorio
     * normal usa la Terminal Ring 3. Nada de ventana ring0 en la cara. */
    n = wm_add_window("Kernel Console", APP_KERNEL_CONSOLE, false,
                      cx - term_w / 2, cy - term_h / 2,
                      term_w, term_h, 232);
    (void)n;

    g_app_count = 0;
#define APP_ADD(id, nm, cat, icon, dwin, w_, h_, drw, clk, keyy, tck, pin_t, pin_s)         \
    do {                                                                                    \
        if (g_app_count >= APP_MAX) break;                                                  \
        g_apps[g_app_count].app_id       = (id);                                            \
        g_apps[g_app_count].name         = (nm);                                            \
        g_apps[g_app_count].category     = (cat);                                           \
        g_apps[g_app_count].icon_id      = (icon);                                          \
        g_apps[g_app_count].window_id    = (dwin);                                          \
        g_apps[g_app_count].pinned_task  = (pin_t);                                         \
        g_apps[g_app_count].pinned_start = (pin_s);                                         \
        g_apps[g_app_count].draw         = (drw);                                           \
        g_apps[g_app_count].click        = (clk);                                           \
        g_apps[g_app_count].key          = (keyy);                                          \
        g_apps[g_app_count].tick         = (tck);                                           \
        g_apps[g_app_count].default_w    = (w_);                                            \
        g_apps[g_app_count].default_h    = (h_);                                            \
        ++g_app_count;                                                                      \
    } while (0)

    i = 0;
    APP_ADD(APP_MONITOR, "Monitor",     "System", RIDUX_ICON_MONITOR,  window_ids[i++], mon_w, mon_h,
            app_draw_monitor,  0, 0, 0, true,  true);
    APP_ADD(APP_FILES,   "Files",       "System", RIDUX_ICON_FILES,    window_ids[i++], files_w, files_h,
            app_draw_files,    0, 0, 0, true,  true);
    APP_ADD(APP_TERMINAL,"Terminal",    "System", RIDUX_ICON_TERMINAL, window_ids[i++], term_w, term_h,
            app_draw_terminal, 0, 0, 0, true,  true);
    APP_ADD(APP_NOTES,   "Notes",       "Tools",  RIDUX_ICON_NOTES,    window_ids[i++], notes_w, notes_h,
            app_draw_notes,    0, 0, 0, true,  true);
    APP_ADD(APP_FLUSH,   "Flush",       "System", RIDUX_ICON_FLUSH,    window_ids[i++], generic_w, generic_h,
            app_draw_flush,    0, 0, 0, false, true);
    APP_ADD(APP_SETTINGS,"Settings",    "System", RIDUX_ICON_SETTINGS, window_ids[i++], generic_w, generic_h,
            app_draw_settings, app_click_settings, 0, 0, true, true);
    APP_ADD(APP_CALC,    "Calculator",  "Tools",  RIDUX_ICON_CALC,     window_ids[i++], calc_w, calc_h,
            app_draw_calc,     app_click_calc, 0, 0, false, true);
    APP_ADD(APP_CLOCK,   "Clock",       "Tools",  RIDUX_ICON_CLOCK,    window_ids[i++], clock_w, clock_h,
            app_draw_clock,    0, 0, 0, false, true);
    APP_ADD(APP_PAINT,   "Paint",       "Creative", RIDUX_ICON_PAINT,  window_ids[i++], paint_w, paint_h,
            app_draw_paint,    app_click_paint, 0, 0, false, true);
    APP_ADD(APP_TASKMGR, "Task Manager","System", RIDUX_ICON_TASKMGR,  window_ids[i++], generic_w, generic_h,
            app_draw_taskmgr,  0, 0, 0, false, true);
    APP_ADD(APP_BROWSER, "Browser",     "Internet", RIDUX_ICON_BROWSER,window_ids[i++], generic_w, generic_h,
            app_draw_browser,  0, 0, 0, false, true);
    APP_ADD(APP_FIREFOX, "Firefox",     "Internet", RIDUX_ICON_FIREFOX,window_ids[i++], generic_w, generic_h,
            app_draw_firefox, app_click_firefox, 0, 0, true, true);
    APP_ADD(APP_WEATHER, "Weather",     "Info",   RIDUX_ICON_WEATHER,  window_ids[i++], 360, 440,
            app_draw_weather,  0, 0, 0, false, true);
    APP_ADD(APP_STORE,   "Ridux Store", "Store",  RIDUX_ICON_STORE,    window_ids[i++], generic_w, generic_h,
            app_draw_store,    app_click_store, 0, 0, true, true);
    APP_ADD(APP_ABOUT,   "About",       "System", RIDUX_ICON_ABOUT,    window_ids[i++], 440, 340,
            app_draw_about,    0, 0, 0, false, true);
    APP_ADD(APP_MEDIA,   "Media",       "Creative", RIDUX_ICON_MEDIA,  window_ids[i++], 600, 380,
            app_draw_media,    0, 0, 0, false, true);
    /* --- NEW APPS --- */
    APP_ADD(APP_EDITOR,     "Editor",      "Tools",  RIDUX_ICON_EDITOR,   window_ids[i++], 720, 480,
            app_draw_editor,    0,                  app_key_editor, 0, false, true);
    APP_ADD(APP_MINESWEEPER,"Minesweeper", "Games",  RIDUX_ICON_MINESWEEPER, window_ids[i++], 400, 440,
            app_draw_mine,      app_click_mine,     0,              0, false, true);
    APP_ADD(APP_SNAKE,      "Snake",       "Games",  RIDUX_ICON_SNAKE,    window_ids[i++], 520, 360,
            app_draw_snake,     0,                  app_key_snake,  app_tick_snake, false, true);
    APP_ADD(APP_LOGVIEWER,  "Log Viewer",  "System", RIDUX_ICON_LOGVIEWER,window_ids[i++], 640, 440,
            app_draw_logviewer, app_click_logviewer,0,              0, false, true);
    APP_ADD(APP_NETMON,     "Network",     "System", RIDUX_ICON_NETWORK,  window_ids[i++], 520, 420,
            app_draw_netmon,    0,                  0,              0, false, true);
    APP_ADD(APP_PROCTREE,   "Processes",   "System", RIDUX_ICON_PROCESSES,window_ids[i++], 520, 420,
            app_draw_proctree,  0,                  0,              0, false, true);
    APP_ADD(APP_SYSINFO,    "System Info", "System", RIDUX_ICON_SYSINFO,  window_ids[i++], 520, 440,
            app_draw_sysinfo,   0,                  0,              0, false, true);
    APP_ADD(APP_TICTACTOE,  "Tic-Tac-Toe", "Games",  RIDUX_ICON_TICTACTOE,window_ids[i++], 360, 420,
            app_draw_ttt,       app_click_ttt,      0,              0, false, true);
    APP_ADD(APP_RING3,      "Ring 3 Demo", "System", RIDUX_ICON_TERMINAL, window_ids[i++], 480, 400,
            app_draw_ring3,     app_click_ring3,    0,              0, false, true);

#undef APP_ADD

    /* Seed calculator display. */
    {
        window_t *calc = wm_get_by_id(window_ids[6]);
        if (calc) {
            calc->state_s[0][0] = 0;
            calc->state_i[0] = 0;
            calc->state_i[1] = 0;
        }
    }
    /* Seed paint with empty (0xFF sentinel). */
    {
        window_t *paint = wm_get_by_id(window_ids[8]);
        if (paint) {
            size_t j;
            for (j = 0; j < sizeof(paint->state_u) / sizeof(paint->state_u[0]); ++j)
                paint->state_u[j] = 0xFFFFFFFFu;
            paint->state_i[0] = 2; /* default color: red */
        }
    }
    (void)i;
}

/* Start menu, taskbar, tray, quick settings */

/* Bake the wallpaper + tint + vignette into g_bg_cache once per theme change.
 * We write directly into the backbuffer (skipping the flush queue) because
 * the background is the first thing drawn each frame anyway; then copy out
 * to the cache. Subsequent frames only memcpy cache -> backbuffer. */
static void desktop_bake(void) {
    const theme_t *th = current_theme();
    const ridux_image_t *wp = &RIDUX_WALLPAPERS[g_theme_index % RIDUX_WALLPAPER_COUNT];
    uint32_t tint_a = rgb_hex(th->wallpaper_tint_a);
    uint32_t tint_b = rgb_hex(th->wallpaper_tint_b);
    int W = (int)g_fb.width;
    int H = (int)g_fb.height;
    int draw_x = 0, draw_y = 0, draw_w = W, draw_h = H;
    size_t y;

    if (!g_use_backbuffer) {
        g_bg_cache_theme = (int)g_theme_index;
        g_bg_cache_w = g_fb.width;
        g_bg_cache_h = g_fb.height;
        return;
    }

    /* Base tint (opaque). */
    draw_rect_alpha(0, 0, W, H, tint_a, 255);
    /* Wallpaper in aspect-preserving "cover" mode (center crop).
     * This avoids distortion/stretch artifacts and keeps maximum detail. */
    if (wp && wp->width && wp->height) {
        if ((uint64_t)W * (uint64_t)wp->height > (uint64_t)H * (uint64_t)wp->width) {
            draw_w = W;
            draw_h = (int)(((uint64_t)W * (uint64_t)wp->height + (uint64_t)wp->width / 2u) / (uint64_t)wp->width);
            draw_y = (H - draw_h) / 2;
        } else {
            draw_h = H;
            draw_w = (int)(((uint64_t)H * (uint64_t)wp->width + (uint64_t)wp->height / 2u) / (uint64_t)wp->height);
            draw_x = (W - draw_w) / 2;
        }
        draw_image_scaled_alpha(draw_x, draw_y, draw_w, draw_h, wp, 255);
    }
    /* Vignette via vertical gradient (cheap single pass). */
    draw_vgradient_alpha(0, 0, W, H, tint_a, tint_b, 70);

    /* Copy backbuffer rows into cache. */
    for (y = 0; y < (size_t)H; ++y) {
        k_memcpy(&g_bg_cache[y * FB_MAX_WIDTH],
                 &g_backbuffer[y * FB_MAX_WIDTH],
                 (size_t)W * 4u);
    }
    g_bg_cache_theme = (int)g_theme_index;
    g_bg_cache_w = g_fb.width;
    g_bg_cache_h = g_fb.height;
}

static void desktop_blit(void) {
    size_t y;
    int W = (int)g_fb.width;
    int H = (int)g_fb.height;
    if (!g_use_backbuffer) return;
    for (y = 0; y < (size_t)H; ++y) {
        k_memcpy(&g_backbuffer[y * FB_MAX_WIDTH],
                 &g_bg_cache[y * FB_MAX_WIDTH],
                 (size_t)W * 4u);
    }
}

static void desktop_ensure(void) {
    if (g_bg_cache_theme == (int)g_theme_index &&
        g_bg_cache_w == g_fb.width &&
        g_bg_cache_h == g_fb.height) return;
    desktop_bake();
    /* Wallpaper changed: force a full re-present so the diff'd
     * fb_present sends every pixel of the new background to MMIO. */
    fb_present_invalidate();
}

typedef struct {
    int mx, my, mw, mh;
    int cols, cell;
    int gx, gy;
    int tile_w, tile_h;
    int max_tiles;
} start_menu_layout_t;

#define DOCK_AREA_LEFT_PAD   12
#define DOCK_AREA_RIGHT_PAD  14
#define DOCK_EXTRA_GAP       2

static int dock_gap_px(void) {
    return g_ui.taskbar_gap + DOCK_EXTRA_GAP;
}

static void dock_area_bounds(int *start_x, int *end_x) {
    if (start_x) *start_x = g_ui.start_btn.x + g_ui.start_btn.w + DOCK_AREA_LEFT_PAD;
    if (end_x)   *end_x   = g_ui.quick_btn.x - DOCK_AREA_RIGHT_PAD;
}

static void text_fit_ellipsis(char *dst, size_t cap, const char *src, int max_px) {
    int max_chars = max_px / 8;
    size_t len;
    int i;
    if (!dst || cap == 0) return;
    dst[0] = 0;
    if (!src || max_chars <= 0) return;
    len = k_strlen(src);
    if ((int)len <= max_chars) {
        size_t n = len < (cap - 1) ? len : (cap - 1);
        if (n) k_memcpy(dst, src, n);
        dst[n] = 0;
        return;
    }
    if (max_chars <= 3) {
        int dots = max_chars;
        if (dots > (int)cap - 1) dots = (int)cap - 1;
        for (i = 0; i < dots; ++i) dst[i] = '.';
        dst[dots] = 0;
        return;
    }
    {
        int keep = max_chars - 3;
        int max_keep = (int)cap - 1 - 3;
        if (max_keep < 0) max_keep = 0;
        if (keep > max_keep) keep = max_keep;
        for (i = 0; i < keep && src[i]; ++i) dst[i] = src[i];
        dst[i++] = '.';
        dst[i++] = '.';
        dst[i++] = '.';
        dst[i] = 0;
    }
}

static void ui_text_fit_center(int x, int y, int w,
                               uint32_t color, uint8_t alpha, const char *text) {
    char buf[64];
    int tw, tx;
    text_fit_ellipsis(buf, sizeof(buf), text, w);
    if (!buf[0]) return;
    tw = measure_text(buf);
    tx = x + (w - tw) / 2;
    if (tx < x) tx = x;
    flush_text_alpha(tx, y, color, alpha, buf);
}

static void ui_text_fit_left(int x, int y, int w,
                             uint32_t color, uint8_t alpha, const char *text) {
    char buf[96];
    text_fit_ellipsis(buf, sizeof(buf), text, w);
    if (!buf[0]) return;
    flush_text_alpha(x, y, color, alpha, buf);
}

static start_menu_layout_t start_menu_layout_calc(void) {
    start_menu_layout_t s;
    int i, pinned = 0;
    int max_h, pin_area_h, rows, max_rows_by_height;
    s.mw = 640;
    s.mh = 656;
    s.cols = 6;
    s.cell = 96;
    s.tile_w = s.cell - 12;
    s.tile_h = s.cell - 12;
    s.mx = g_ui.taskbar.x + 20;

    max_h = g_ui.taskbar.y - (g_ui.desktop.y + 10) - 8;
    if (max_h < 520) max_h = 520;
    if (s.mh > max_h) s.mh = max_h;

    s.my = g_ui.taskbar.y - s.mh - 8;
    if (s.my < g_ui.desktop.y + 10) s.my = g_ui.desktop.y + 10;
    s.gx = s.mx + 30;
    s.gy = s.my + 110;

    for (i = 0; i < g_app_count; ++i) if (g_apps[i].pinned_start) ++pinned;
    if (pinned < 1) pinned = 1;
    rows = (pinned + s.cols - 1) / s.cols;
    pin_area_h = s.mh - 110 - 144; /* available area before "Recommended" */
    if (pin_area_h < s.cell) pin_area_h = s.cell;
    max_rows_by_height = pin_area_h / s.cell;
    if (max_rows_by_height < 1) max_rows_by_height = 1;
    if (rows > max_rows_by_height) rows = max_rows_by_height;
    if (rows > 4) rows = 4;
    s.max_tiles = rows * s.cols;
    return s;
}

static void render_dock_icons(uint32_t accent, uint32_t stroke, uint32_t text) {
    int i;
    int count = 0;
    int x;
    int start_x, end_x;
    int icon = g_ui.taskbar_icon;
    int gap = dock_gap_px();
    for (i = 0; i < g_app_count; ++i) if (g_apps[i].pinned_task) ++count;
    if (count == 0) return;
    dock_area_bounds(&start_x, &end_x);
    x = start_x;
    for (i = 0; i < g_app_count; ++i) {
        ui_rect_t slot;
        bool running;
        bool focused = false;
        bool hovered;
        bool pressed;
        const ridux_image_t *img = NULL;
        if (!g_apps[i].pinned_task) continue;
        if (x + icon > end_x) break;
        slot.x = x;
        slot.y = g_ui.taskbar.y + (g_ui.taskbar.h - icon) / 2 + 1;
        slot.w = icon; slot.h = icon;
        running = app_is_running(g_apps[i].app_id);
        if (g_window_focus >= 0 && g_window_focus < g_window_count &&
            g_windows[g_window_focus].visible &&
            g_windows[g_window_focus].app_id == g_apps[i].app_id) focused = true;
        hovered = (g_mouse_x >= slot.x && g_mouse_x < slot.x + slot.w &&
                   g_mouse_y >= slot.y && g_mouse_y < slot.y + slot.h);
        pressed = hovered && g_mouse_left_down;

        if (running) {
            flush_round_rect(slot.x, slot.y, slot.w, slot.h, 8,
                             rgb_hex(0xFFFFFF), focused ? 64 : 28);
        }
        if (focused) {
            flush_stroke_round(slot.x - 1, slot.y - 1, slot.w + 2, slot.h + 2, 10, 1,
                               accent, 170);
        }
        if (hovered) {
            flush_round_rect(slot.x, slot.y, slot.w, slot.h, 8,
                             rgb_hex(0xFFFFFF), pressed ? 54 : 36);
        }
        if (pressed) {
            flush_round_rect(slot.x + 2, slot.y + 2, slot.w - 4, slot.h - 4, 7,
                             rgb_hex(0x000000), 34);
        }
        if (g_apps[i].icon_id >= 0 && g_apps[i].icon_id < RIDUX_ICON_COUNT)
            img = &RIDUX_ICONS[g_apps[i].icon_id];
        if (img) {
            int inset = focused ? 3 : 4;
            int iy = slot.y + inset + (pressed ? 1 : 0);
            flush_image(slot.x + inset, iy, slot.w - inset * 2, slot.h - inset * 2, img,
                        focused ? 255 : (running ? 240 : 220));
        } else {
            char fb[2]; fb[0] = g_apps[i].name[0]; fb[1] = 0;
            flush_text(slot.x + (slot.w - 8) / 2, slot.y + (slot.h - 16) / 2, text, fb);
        }
        if (running) {
            int ix = slot.x + slot.w / 2 - (focused ? 10 : 5);
            int iw = focused ? 20 : 10;
            flush_round_rect(ix, slot.y + slot.h - 2, iw, 3, 2, accent, focused ? 255 : 230);
        }
        x += icon + gap;
        (void)stroke;
    }
}

static void render_start_button(void) {
    const theme_t *th = current_theme();
    ui_rect_t r = g_ui.start_btn;
    bool hovered = (g_mouse_x >= r.x && g_mouse_x < r.x + r.w &&
                    g_mouse_y >= r.y && g_mouse_y < r.y + r.h);
    bool active = g_start_open;
    uint32_t accent = rgb_hex(th->accent);
    uint32_t stroke = rgb_hex(th->stroke);

    flush_glass(r.x, r.y, r.w, r.h, 10,
                rgb_hex(th->taskbar), active ? 214 : 196,
                stroke, active ? 165 : 120,
                8, 1);
    if (active || hovered)
        flush_round_rect(r.x, r.y, r.w, r.h, 8, rgb_hex(0xFFFFFF),
                         active ? 34 : 18);
    {
        int icon_sz = r.w < r.h ? r.w : r.h;
        const ridux_image_t *logo = NULL;
        if (icon_sz > 24) icon_sz -= 14;
        if (icon_sz < 16) icon_sz = 16;
        if (RIDUX_ICON_START >= 0 && RIDUX_ICON_START < RIDUX_ICON_COUNT)
            logo = &RIDUX_ICONS[RIDUX_ICON_START];
        if (logo) {
            int ix = r.x + (r.w - icon_sz) / 2;
            int iy = r.y + (r.h - icon_sz) / 2;
            flush_image(ix, iy, icon_sz, icon_sz, logo, active ? 255 : 236);
        } else {
            int sq = 7;
            int gap = 3;
            int gsz = sq * 2 + gap;
            int cx = r.x + (r.w - gsz) / 2;
            int cy = r.y + (r.h - gsz) / 2;
            flush_round_rect(cx,            cy,           sq, sq, 1, accent, 255);
            flush_round_rect(cx + sq + gap, cy,           sq, sq, 1, accent, 255);
            flush_round_rect(cx,            cy + sq + gap,sq, sq, 1, accent, 255);
            flush_round_rect(cx + sq + gap, cy + sq + gap,sq, sq, 1, accent, 255);
        }
    }
}

static void render_tray(void) {
    const theme_t *th = current_theme();
    ui_rect_t q = g_ui.quick_btn;
    ui_rect_t c = g_ui.clock_btn;
    uint32_t text  = rgb_hex(th->text);
    uint32_t muted = rgb_hex(th->text_muted);
    bool q_hover = (g_mouse_x >= q.x && g_mouse_x < q.x + q.w &&
                    g_mouse_y >= q.y && g_mouse_y < q.y + q.h);
    bool c_hover = (g_mouse_x >= c.x && g_mouse_x < c.x + c.w &&
                    g_mouse_y >= c.y && g_mouse_y < c.y + c.h);
    bool q_pressed = q_hover && g_mouse_left_down;
    bool c_pressed = c_hover && g_mouse_left_down;

    if (q_hover || g_quick_open)
        flush_round_rect(q.x, q.y, q.w, q.h, 8, rgb_hex(0xFFFFFF),
                         g_quick_open ? 46 : (q_pressed ? 34 : 24));
    if (q_pressed) {
        flush_round_rect(q.x + 2, q.y + 2, q.w - 4, q.h - 4, 7, rgb_hex(0x000000), 28);
    }
    {
        const int tray_ids[] = {
            RIDUX_ICON_TRAY_NETWORK, RIDUX_ICON_TRAY_VOLUME,
            RIDUX_ICON_TRAY_BATTERY, RIDUX_ICON_TRAY_SETTINGS
        };
        const int tray_count = (int)(sizeof(tray_ids) / sizeof(tray_ids[0]));
        const int sz = 16;
        const int gap = 10;
        int total_w = tray_count * sz + (tray_count - 1) * gap;
        int base_x = q.x + (q.w - total_w) / 2;
        int i;
        for (i = 0; i < tray_count; ++i) {
            int ix = base_x + i * (sz + gap);
            int iy = q.y + (q.h - sz) / 2 + (q_pressed ? 1 : 0);
            if (tray_ids[i] >= 0 && tray_ids[i] < RIDUX_ICON_COUNT) {
                flush_image(ix, iy, sz, sz, &RIDUX_ICONS[tray_ids[i]], 230);
            }
        }
    }

    if (c_hover)
        flush_round_rect(c.x, c.y, c.w, c.h, 8, rgb_hex(0xFFFFFF), c_pressed ? 30 : 20);
    if (c_pressed)
        flush_round_rect(c.x + 2, c.y + 2, c.w - 4, c.h - 4, 7, rgb_hex(0x000000), 24);
    {
        char t1[16], t2[16];
        size_t l = 0;
        t1[0] = 0; t2[0] = 0;
        k_append_u32_pad(t1, &l, sizeof(t1), (uint32_t)g_rtc_now.hour, 2, '0');
        k_append_ch(t1, &l, sizeof(t1), ':');
        k_append_u32_pad(t1, &l, sizeof(t1), (uint32_t)g_rtc_now.minute, 2, '0');

        l = 0;
        k_append_u32_pad(t2, &l, sizeof(t2), (uint32_t)g_rtc_now.day, 2, '0');
        k_append_ch(t2, &l, sizeof(t2), '/');
        k_append_u32_pad(t2, &l, sizeof(t2), (uint32_t)g_rtc_now.month, 2, '0');
        k_append_ch(t2, &l, sizeof(t2), '/');
        k_append_u32_pad(t2, &l, sizeof(t2), (uint32_t)g_rtc_now.year, 4, '0');

        /* Time 2x with high-res glyph sampling. Date 1x under it. */
        {
            int t1_w = (int)k_strlen(t1) * 16; /* 8 * 2 */
            flush_text_scaled(c.x + c.w - t1_w - 12,
                              c.y + (c.h / 2 - 18),
                              2, text, 240, t1);
        }
        flush_text_alpha(c.x + c.w - measure_text(t2) - 12,
                         c.y + c.h - 16, muted, 210, t2);
    }
}

static void render_taskbar(void) {
    const theme_t *th = current_theme();
    ui_rect_t t = g_ui.taskbar;
    uint32_t accent = rgb_hex(th->accent);
    uint32_t text   = rgb_hex(th->text);
    uint32_t stroke = rgb_hex(th->stroke);

    flush_rect(t.x, t.y, t.w, t.h, rgb_hex(th->taskbar), 226);
    flush_rect(t.x, t.y, t.w, 1, stroke, 200);
    flush_rect(t.x, t.y + 1, t.w, 1, rgb_hex(0xFFFFFF), th->dark ? 20 : 36);
    render_start_button();
    render_dock_icons(accent, stroke, text);
    render_tray();
}

static void render_start_menu(void) {
    const theme_t *th = current_theme();
    start_menu_layout_t st = start_menu_layout_calc();
    int mw = st.mw;
    int mh = st.mh;
    int mx = st.mx;
    int my = st.my;
    int i;
    uint32_t text   = rgb_hex(th->text);
    uint32_t muted  = rgb_hex(th->text_muted);
    uint32_t accent = rgb_hex(th->accent);
    if (!g_start_open) return;

    flush_shadow(mx, my, mw, mh, 18, 4, rgb_hex(0x000000), 92);
    flush_glass(mx, my, mw, mh, 16,
                rgb_hex(th->start_bg), 222,
                rgb_hex(th->stroke), 150,
                8, 1);
    flush_round_rect(mx + 24, my + 24, mw - 48, 44, 12,
                     rgb_hex(th->bg_alt), 200);
    flush_circle(mx + 46, my + 24 + 22, 7, accent, 220);
    flush_text_scaled(mx + 66, my + 24 + 10, 2, muted, 190, "Search");

    flush_text_scaled(mx + 30, my + 84, 2, text, 240, "Pinned");
    {
        int idx = 0;
        for (i = 0; i < g_app_count; ++i) {
            int col, row, cx2, cy2, iy;
            int lift, icon_off;
            bool hovered;
            bool pressed;
            bool running;
            if (!g_apps[i].pinned_start) continue;
            col = idx % st.cols;
            row = idx / st.cols;
            cx2 = st.gx + col * st.cell;
            cy2 = st.gy + row * st.cell;
            hovered = (g_mouse_x >= cx2 && g_mouse_x < cx2 + st.tile_w &&
                       g_mouse_y >= cy2 && g_mouse_y < cy2 + st.tile_h);
            pressed = hovered && g_mouse_left_down;
            running = app_is_running(g_apps[i].app_id);
            /* Subtle lift on hover, depression on press. Pure offset, so no
             * per-tile animation state is needed but it still feels alive. */
            lift = pressed ? 0 : (hovered ? 1 : 0);
            icon_off = pressed ? 1 : (hovered ? -2 : 0);
            if (hovered && !pressed) {
                flush_shadow(cx2, cy2 + 2, st.tile_w, st.tile_h, 12, 2,
                             rgb_hex(0x000000), 70);
            }
            flush_round_rect(cx2, cy2 - lift, st.tile_w, st.tile_h, 12,
                             rgb_hex(th->bg_alt),
                             pressed ? 252 : (hovered ? 232 : (running ? 170 : 142)));
            if (hovered) {
                flush_stroke_round(cx2, cy2 - lift, st.tile_w, st.tile_h, 12, 1,
                                   accent, pressed ? 230 : 180);
            } else if (running) {
                flush_stroke_round(cx2 + 1, cy2 + 1, st.tile_w - 2, st.tile_h - 2, 11, 1,
                                   accent, 140);
            }
            if (g_apps[i].icon_id >= 0 && g_apps[i].icon_id < RIDUX_ICON_COUNT) {
                const int icon_sz = 44;
                flush_image(cx2 + (st.tile_w - icon_sz) / 2,
                            cy2 + 12 + icon_off - lift,
                            icon_sz, icon_sz,
                            &RIDUX_ICONS[g_apps[i].icon_id],
                            hovered ? 252 : 240);
            }
            iy = cy2 + st.tile_h - 22 - lift;
            ui_text_fit_center(cx2 + 6, iy, st.tile_w - 12, text, 244, g_apps[i].name);
            ++idx;
            if (idx >= st.max_tiles) break;
        }
    }

    flush_text_scaled(mx + 30, my + mh - 144, 2, text, 230, "Recommended");
    {
        int ry = my + mh - 112;
        const char *files[] = { "/home/readme.txt", "/etc/motd.txt",
                                "/tmp/notes.txt", "/tmp/todo.txt" };
        const int rec_icons[] = { RIDUX_ICON_FILES, RIDUX_ICON_SYSINFO,
                                  RIDUX_ICON_EDITOR, RIDUX_ICON_NOTES };
        for (i = 0; i < 4; ++i) {
            int cx2 = mx + 30 + i * 146;
            bool hov = (g_mouse_x >= cx2 && g_mouse_x < cx2 + 130 &&
                        g_mouse_y >= ry  && g_mouse_y < ry  + 62);
            int lift = hov ? 1 : 0;
            if (hov) {
                flush_shadow(cx2, ry + 2, 130, 62, 10, 2, rgb_hex(0x000000), 60);
            }
            flush_round_rect(cx2, ry - lift, 130, 62, 10,
                             rgb_hex(th->bg_alt), hov ? 220 : 180);
            if (hov) {
                flush_stroke_round(cx2, ry - lift, 130, 62, 10, 1, accent, 160);
            }
            flush_image(cx2 + 10, ry + 17 - lift, 26, 26,
                        &RIDUX_ICONS[rec_icons[i] % RIDUX_ICON_COUNT], 220);
            ui_text_fit_left(cx2 + 42, ry + 14 - lift, 78, text, 220, files[i]);
            ui_text_fit_left(cx2 + 42, ry + 36 - lift, 78, muted, 200, "text file");
        }
    }

    {
        int ux = mx + 24;
        int uy = my + mh - 46;
        flush_circle(ux + 14, uy + 14, 14, accent, 230);
        flush_text_alpha(ux + 38, uy + 8, text, 220, "ridux");
        flush_text_alpha(mx + mw - 110, uy + 8, muted, 200, "[power]");
    }
}

static void render_quick_settings(void) {
    const theme_t *th = current_theme();
    int pw = 340;
    int ph = 300;
    int px = g_ui.quick_btn.x + g_ui.quick_btn.w - pw;
    int py = g_ui.taskbar.y - ph - 8;
    uint32_t text  = rgb_hex(th->text);
    uint32_t accent = rgb_hex(th->accent);
    if (!g_quick_open) return;
    if (py < g_ui.desktop.y + 10) py = g_ui.desktop.y + 10;

    flush_shadow(px, py, pw, ph, 16, 4, rgb_hex(0x000000), 92);
    flush_glass(px, py, pw, ph, 14,
                rgb_hex(th->start_bg), 214,
                rgb_hex(th->stroke), 140,
                10, 1);
    {
        const char *labels[] = { "WiFi", "Bluetooth", "Airplane", "Quiet", "Dark", "Cast" };
        int cols = 3;
        int cell_w = (pw - 40) / cols;
        int cell_h = 64;
        int i;
        for (i = 0; i < 6; ++i) {
            int r = i / cols;
            int c = i % cols;
            int cx = px + 20 + c * cell_w;
            int cy = py + 20 + r * (cell_h + 10);
            int tw = cell_w - 6;
            bool on = (i == 0 || i == 4);
            bool hov = (g_mouse_x >= cx && g_mouse_x < cx + tw &&
                        g_mouse_y >= cy && g_mouse_y < cy + cell_h);
            bool prs = hov && g_mouse_left_down;
            int lift = prs ? 0 : (hov ? 1 : 0);
            flush_round_rect(cx, cy - lift, tw, cell_h, 10,
                             on ? accent : rgb_hex(th->bg_alt),
                             on ? (hov ? 245 : 230)
                                : (prs ? 220 : (hov ? 210 : 180)));
            if (hov && !on) {
                flush_stroke_round(cx, cy - lift, tw, cell_h, 10, 1,
                                   accent, prs ? 220 : 160);
            }
            flush_text_alpha(cx + 14, cy + cell_h - 20 - lift,
                             on ? rgb_hex(0xFFFFFF) : text, 220, labels[i]);
        }
    }
    {
        int sy = py + ph - 70;
        flush_text_alpha(px + 20, sy, text, 210, "Volume");
        flush_round_rect(px + 20, sy + 16, pw - 40, 6, 3, rgb_hex(th->bg_alt), 210);
        flush_round_rect(px + 20, sy + 16, (pw - 40) * 60 / 100, 6, 3, accent, 240);
        flush_text_alpha(px + 20, sy + 30, text, 210, "Brightness");
        flush_round_rect(px + 20, sy + 46, pw - 40, 6, 3, rgb_hex(th->bg_alt), 210);
        flush_round_rect(px + 20, sy + 46, (pw - 40) * 80 / 100, 6, 3, accent, 240);
    }
}

static volatile int g_render_in_progress = 0;

static void render_scene(void) {
    int i;

    /* Reentrancy guard. render_scene runs from two contexts:
     *   1. The kernel main loop in kernel_main()
     *   2. The PIT timer IRQ handler in pit_tick_irq() (so the desktop
     *      keeps painting while a Ring 3 task is in foreground)
     * Both paths mutate the shared flush queue, the global backbuffer
     * and the cursor underbuffer. If the IRQ fires while the main loop
     * is mid-render, the IRQ's render_scene clobbers half-drawn state
     * and the user sees the entire UI flicker. Skip the nested call;
     * the next IRQ tick (or the main loop's next iteration) will
     * compose a clean frame. */
    if (g_render_in_progress) return;
    g_render_in_progress = 1;

    /* Drive per-app tick callbacks (Snake auto-advance, etc). Windows
     * only tick while visible so hidden apps don't burn CPU. */
    for (i = 0; i < g_window_count; ++i) {
        if (!g_windows[i].visible || g_windows[i].minimized) continue;
        {
            int aidx = app_index_by_id(g_windows[i].app_id);
            if (aidx >= 0 && g_apps[aidx].tick) {
                g_apps[aidx].tick(&g_windows[i]);
            }
        }
    }
    anim_tick_all();

    ui_build_layout();
    /* Desktop background is cached; ensure it's baked then memcpy. */
    desktop_ensure();
    desktop_blit();
    flush_reset();

    for (i = 0; i < g_window_count; ++i) {
        if (g_windows[i].flags & WINF_DESKTOP)
            render_window(&g_windows[i], false);
    }

    /* Si el desktop Ring 3 esta vivo, el fondo y widgets del escritorio son
     * suyos. Dejamos que el kernel pinte solo la barra y ventanas encima. */
    if (!wm_has_visible_ring3_desktop()) {
        char line[24];
        size_t l = 0;
        line[0] = 0;
        k_append_u32_pad(line, &l, sizeof(line), (uint32_t)g_rtc_now.hour, 2, '0');
        k_append_ch(line, &l, sizeof(line), ':');
        k_append_u32_pad(line, &l, sizeof(line), (uint32_t)g_rtc_now.minute, 2, '0');
        flush_text_scaled(g_ui.margin + 8, g_ui.margin, 5,
                          rgb_hex(0xFFFFFF), 182, line);
    }

    for (i = 0; i < g_window_count; ++i) {
        if (g_windows[i].flags & WINF_DESKTOP) continue;
        render_window(&g_windows[i], i == g_window_focus);
    }

    if (!wm_has_visible_ring3_desktop()) {
        render_taskbar();
        if (g_start_open) render_start_menu();
        if (g_quick_open) render_quick_settings();
    }

    /* Two-pass flush: commit the non-cursor scene to the backbuffer,
     * snapshot the clean pixels under where the cursor will be drawn,
     * then queue + commit the cursor sprite on top. Cost: one extra
     * fb_present (the second one diffs to ~one cursor's worth of MMIO,
     * which is essentially free). Benefit: subsequent cursor-only
     * frames can erase the old cursor by restoring the underbuffer
     * instead of recomputing the entire scene. */
    flush_execute();
    x11_render_now();
    flush_cursor_save_under(g_mouse_x - 4, g_mouse_y - 4, 24, 24);
    flush_reset();
    render_cursor();
    flush_execute();
    g_needs_redraw = false;
    g_cursor_moved = false;
    g_render_in_progress = 0;
}

/* Cursor fast-path: cheap repaint when only the mouse moved. Restores
 * the saved clean pixels at the previous cursor position (erasing the
 * old cursor), saves the clean pixels at the new position, then draws
 * the cursor sprite on top. Total work: ~24x24 px copy x2 + ~7 short
 * line rasterizations + a diff-fb_present that finds two tiny dirty
 * rects. This is what makes mouse motion feel instantaneous even
 * though render_scene itself takes several ms. */
static void render_cursor_only(void) {
    if (!g_fb.ready || g_panic_active) return;
    if (g_needs_redraw) return;
    flush_cursor_restore_under();
    flush_cursor_save_under(g_mouse_x - 4, g_mouse_y - 4, 24, 24);
    flush_reset();
    render_cursor();
    flush_execute();
    g_cursor_moved = false;
}

void ridux_request_cursor_redraw(void) {
    if (!g_fb.ready || g_panic_active) return;
    g_cursor_moved = true;
}

void ridux_present_cursor_after_external_blit(int x, int y, int w, int h) {
    int cx0, cy0, cx1, cy1;
    int bx0, by0, bx1, by1;
    if (!g_fb.ready || g_panic_active) return;
    if (g_render_in_progress) {
        g_cursor_moved = true;
        return;
    }
    if (w <= 0 || h <= 0) return;
    cx0 = g_mouse_x - 4;
    cy0 = g_mouse_y - 4;
    cx1 = cx0 + 24;
    cy1 = cy0 + 24;
    bx0 = x;
    by0 = y;
    bx1 = x + w;
    by1 = y + h;
    if (cx1 <= bx0 || cy1 <= by0 || cx0 >= bx1 || cy0 >= by1) return;

    /* X11/Wayland just redrew over the cursor. Do not restore the old
     * underbuffer here; save the freshly painted pixels and immediately
     * put the kernel cursor back on top. */
    flush_cursor_save_under(cx0, cy0, 24, 24);
    flush_reset();
    render_cursor();
    flush_execute();
    g_cursor_moved = false;
}
