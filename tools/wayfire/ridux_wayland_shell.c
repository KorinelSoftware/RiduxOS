#include <gtk/gtk.h>
#include <gtk-layer-shell/gtk-layer-shell.h>
#include <errno.h>
#include <unistd.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *RIDUX_WALLPAPER = "/opt/wayfire/share/wayfire/ridux-wallpaper.ppm";

static const char *app_mode(const char *argv0) {
    const char *base = strrchr(argv0 ? argv0 : "", '/');
    base = base ? base + 1 : (argv0 ? argv0 : "");
    if (strstr(base, "session")) return "session";
    if (strstr(base, "background")) return "background";
    if (strstr(base, "dock")) return "dock";
    if (strstr(base, "launcher")) return "launcher";
    if (strstr(base, "settings")) return "settings";
    if (strstr(base, "about")) return "about";
    if (strstr(base, "monitor")) return "monitor";
    if (strstr(base, "terminal")) return "terminal";
    if (strstr(base, "thunar") || strstr(base, "files")) return "files";
    return "launcher";
}

static void load_css(void) {
    static const char css[] =
        "* { font-family: 'Cantarell', 'Segoe UI', sans-serif; }\n"
        "window { background: transparent; }\n"
        ".wallpaper { background: #08152b; }\n"
        ".dock { background: rgba(8,14,28,0.62); border: 1px solid rgba(255,255,255,0.18);"
        " border-radius: 20px; padding: 8px 12px; box-shadow: 0 22px 60px rgba(0,0,0,0.38); }\n"
        ".dock button { min-width: 52px; min-height: 46px; padding: 6px 10px; color: #f8fbff;"
        " background: rgba(255,255,255,0.08); border: 1px solid rgba(255,255,255,0.10); border-radius: 14px; }\n"
        ".dock button:hover { background: rgba(49,154,255,0.32); border-color: rgba(105,206,255,0.50); }\n"
        ".window-card { background: rgba(13,22,39,0.86); color: #f7fbff; border-radius: 18px;"
        " border: 1px solid rgba(255,255,255,0.18); box-shadow: 0 28px 80px rgba(0,0,0,0.42); }\n"
        ".title { font-size: 20px; font-weight: 700; color: #ffffff; }\n"
        ".muted { color: rgba(235,244,255,0.68); }\n"
        ".tile { background: rgba(255,255,255,0.08); border: 1px solid rgba(255,255,255,0.12);"
        " border-radius: 16px; padding: 16px; color: #ffffff; }\n"
        ".tile:hover { background: rgba(26,153,255,0.30); border-color: rgba(115,216,255,0.55); }\n"
        ".sidebar { background: rgba(255,255,255,0.07); border-radius: 14px; padding: 10px; }\n"
        "entry { color: #f7fbff; background: rgba(255,255,255,0.10); border: 1px solid rgba(255,255,255,0.16);"
        " border-radius: 12px; padding: 9px 12px; }\n";
    GtkCssProvider *provider = gtk_css_provider_new();
    gtk_css_provider_load_from_data(provider, css, -1, NULL);
    gtk_style_context_add_provider_for_screen(gdk_screen_get_default(),
        GTK_STYLE_PROVIDER(provider), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(provider);
}

static void setup_transparent(GtkWidget *win) {
    GdkScreen *screen = gdk_screen_get_default();
    GdkVisual *visual = screen ? gdk_screen_get_rgba_visual(screen) : NULL;
    if (visual) gtk_widget_set_visual(win, visual);
    gtk_widget_set_app_paintable(win, TRUE);
}

static GtkWidget *label_box(const char *icon, const char *title, const char *sub) {
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    GtkWidget *i = gtk_label_new(icon);
    GtkWidget *t = gtk_label_new(title);
    GtkWidget *s = gtk_label_new(sub ? sub : "");
    gtk_widget_set_halign(i, GTK_ALIGN_CENTER);
    gtk_widget_set_halign(t, GTK_ALIGN_CENTER);
    gtk_widget_set_halign(s, GTK_ALIGN_CENTER);
    gtk_label_set_markup(GTK_LABEL(i), icon);
    gtk_style_context_add_class(gtk_widget_get_style_context(t), "title");
    gtk_style_context_add_class(gtk_widget_get_style_context(s), "muted");
    gtk_box_pack_start(GTK_BOX(box), i, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), t, FALSE, FALSE, 0);
    if (sub && *sub) gtk_box_pack_start(GTK_BOX(box), s, FALSE, FALSE, 0);
    return box;
}

static void spawn_cmd(GtkWidget *w, gpointer data) {
    (void)w;
    const char *cmd = (const char *)data;
    if (cmd && *cmd) g_spawn_command_line_async(cmd, NULL);
}

static int can_exec(const char *path) {
    return path && *path && g_file_test(path, G_FILE_TEST_IS_EXECUTABLE);
}

static int can_exec_raw(const char *path) {
    return path && *path && access(path, X_OK) == 0;
}

static void session_spawn(const char *label, const char *path,
                          char *const argv[]) {
    pid_t pid;
    if (!can_exec_raw(path)) return;
    pid = fork();
    if (pid < 0) {
        fprintf(stderr, "ridux-session: fork failed for %s (%s): %s\n",
                label ? label : path, path, strerror(errno));
        return;
    }
    if (pid == 0) {
        execv(path, argv);
        fprintf(stderr, "ridux-session: exec failed for %s (%s): %s\n",
                label ? label : path, path, strerror(errno));
        _exit(127);
    }
}

static void session_pause(void) {
    usleep(180000);
}

static void spawn_existing(const char *label, const char *path, const char *cmd) {
    GError *err = NULL;
    const char *run = (cmd && *cmd) ? cmd : path;
    if (!can_exec(path) || !run || !*run) return;
    if (!g_spawn_command_line_async(run, &err)) {
        fprintf(stderr, "ridux-session: could not start %s (%s): %s\n",
                label ? label : path, run, err ? err->message : "unknown error");
        if (err) g_error_free(err);
    }
}

static void start_session_services(void) {
    g_setenv("GDK_BACKEND", "wayland", TRUE);
    g_setenv("XDG_SESSION_TYPE", "wayland", TRUE);
    g_setenv("XDG_CURRENT_DESKTOP", "Wayfire", TRUE);
    g_setenv("DESKTOP_SESSION", "wayfire", TRUE);
    g_setenv("HOME", "/home", TRUE);
    g_setenv("XDG_RUNTIME_DIR", "/run/user/1000", TRUE);
    g_setenv("WAYLAND_DISPLAY", "wayland-1", TRUE);
    g_setenv("XDG_CONFIG_HOME", "/tmp/wayfire-home/config", TRUE);
    g_setenv("XDG_CACHE_HOME", "/tmp/wayfire-home/cache", TRUE);
    g_setenv("XDG_DATA_HOME", "/tmp/wayfire-home/share", TRUE);
    g_setenv("XDG_STATE_HOME", "/tmp/wayfire-home/state", TRUE);
    g_setenv("DBUS_SESSION_BUS_ADDRESS", "unix:path=/run/user/1000/bus", TRUE);

    {
        char *const pipewire[] = {"/usr/bin/pipewire", NULL};
        char *const wireplumber[] = {"/usr/bin/wireplumber", NULL};
        char *const pipewire_pulse[] = {"/usr/bin/pipewire-pulse", NULL};
        char *const portal_wlr_libexec[] = {"/usr/libexec/xdg-desktop-portal-wlr", NULL};
        char *const portal_wlr_bin[] = {"/usr/bin/xdg-desktop-portal-wlr", NULL};
        char *const portal_libexec[] = {"/usr/libexec/xdg-desktop-portal", NULL};
        char *const portal_bin[] = {"/usr/bin/xdg-desktop-portal", NULL};
        char *const swaync[] = {"/usr/bin/swaync", NULL};
        char *const wf_background[] = {"/opt/wayfire/bin/wf-background", "-c", "/tmp/wayfire-home/config/wf-shell.ini", NULL};
        char *const ridux_background[] = {"/opt/wayfire/bin/ridux-background", NULL};
        char *const waybar[] = {"/usr/bin/waybar", "-c", "/etc/xdg/waybar/config", "-s", "/etc/xdg/waybar/style.css", NULL};
        char *const wf_panel[] = {"/opt/wayfire/bin/wf-panel", "-c", "/tmp/wayfire-home/config/wf-shell.ini", NULL};
        char *const wf_dock[] = {"/opt/wayfire/bin/wf-dock", "-c", "/tmp/wayfire-home/config/wf-shell.ini", NULL};
        char *const ridux_dock[] = {"/opt/wayfire/bin/ridux-dock", NULL};
        char *const files[] = {"/opt/wayfire/bin/thunar", NULL};

        session_spawn("pipewire", "/usr/bin/pipewire", pipewire);
        session_spawn("wireplumber", "/usr/bin/wireplumber", wireplumber);
        session_spawn("pipewire-pulse", "/usr/bin/pipewire-pulse", pipewire_pulse);
        if (can_exec_raw("/usr/libexec/xdg-desktop-portal-wlr"))
            session_spawn("xdg-desktop-portal-wlr", "/usr/libexec/xdg-desktop-portal-wlr", portal_wlr_libexec);
        else
            session_spawn("xdg-desktop-portal-wlr", "/usr/bin/xdg-desktop-portal-wlr", portal_wlr_bin);
        if (can_exec_raw("/usr/libexec/xdg-desktop-portal"))
            session_spawn("xdg-desktop-portal", "/usr/libexec/xdg-desktop-portal", portal_libexec);
        else
            session_spawn("xdg-desktop-portal", "/usr/bin/xdg-desktop-portal", portal_bin);
        session_spawn("swaync", "/usr/bin/swaync", swaync);

        if (can_exec_raw("/opt/wayfire/bin/wf-background"))
            session_spawn("wf-background", "/opt/wayfire/bin/wf-background", wf_background);
        else
            session_spawn("ridux-background", "/opt/wayfire/bin/ridux-background", ridux_background);
        session_pause();

        if (can_exec_raw("/usr/bin/waybar"))
            session_spawn("waybar", "/usr/bin/waybar", waybar);
        else
            session_spawn("wf-panel", "/opt/wayfire/bin/wf-panel", wf_panel);
        session_pause();

        if (can_exec_raw("/opt/wayfire/bin/wf-dock"))
            session_spawn("wf-dock", "/opt/wayfire/bin/wf-dock", wf_dock);
        else
            session_spawn("ridux-dock", "/opt/wayfire/bin/ridux-dock", ridux_dock);
        session_pause();

        session_spawn("files", "/opt/wayfire/bin/thunar", files);
    }
}

static GtkWidget *dock_button(const char *text, const char *cmd) {
    GtkWidget *b = gtk_button_new_with_label(text);
    gtk_widget_set_tooltip_text(b, cmd);
    g_signal_connect(b, "clicked", G_CALLBACK(spawn_cmd), (gpointer)cmd);
    return b;
}

static int ppm_token(FILE *f, char *out, size_t cap) {
    int c;
    size_t n = 0;
    if (!f || !out || cap < 2) return 0;
    for (;;) {
        c = fgetc(f);
        if (c == EOF) return 0;
        if (c == '#') {
            while ((c = fgetc(f)) != EOF && c != '\n') {}
            continue;
        }
        if (c > ' ') break;
    }
    do {
        if (n + 1 < cap) out[n++] = (char)c;
        c = fgetc(f);
    } while (c != EOF && c > ' ');
    out[n] = 0;
    return n > 0;
}

static cairo_surface_t *load_wallpaper_surface(void) {
    static cairo_surface_t *wallpaper;
    FILE *f;
    char tok[64];
    int w, h, maxv, stride, x, y;
    int ascii_ppm = 0;
    unsigned char *dst;
    if (wallpaper) return wallpaper;
    f = fopen(RIDUX_WALLPAPER, "rb");
    if (!f) return NULL;
    if (!ppm_token(f, tok, sizeof(tok))) goto fail;
    if (strcmp(tok, "P6") == 0) {
        ascii_ppm = 0;
    } else if (strcmp(tok, "P3") == 0) {
        ascii_ppm = 1;
    } else {
        goto fail;
    }
    if (!ppm_token(f, tok, sizeof(tok))) goto fail;
    w = atoi(tok);
    if (!ppm_token(f, tok, sizeof(tok))) goto fail;
    h = atoi(tok);
    if (!ppm_token(f, tok, sizeof(tok))) goto fail;
    maxv = atoi(tok);
    if (w <= 0 || h <= 0 || maxv <= 0 || maxv > 255) goto fail;
    wallpaper = cairo_image_surface_create(CAIRO_FORMAT_RGB24, w, h);
    if (cairo_surface_status(wallpaper) != CAIRO_STATUS_SUCCESS) goto fail_surface;
    dst = cairo_image_surface_get_data(wallpaper);
    stride = cairo_image_surface_get_stride(wallpaper);
    for (y = 0; y < h; ++y) {
        unsigned char *row = dst + y * stride;
        for (x = 0; x < w; ++x) {
            int r, g, b;
            if (ascii_ppm) {
                if (!ppm_token(f, tok, sizeof(tok))) goto fail_surface;
                r = atoi(tok);
                if (!ppm_token(f, tok, sizeof(tok))) goto fail_surface;
                g = atoi(tok);
                if (!ppm_token(f, tok, sizeof(tok))) goto fail_surface;
                b = atoi(tok);
            } else {
                r = fgetc(f);
                g = fgetc(f);
                b = fgetc(f);
                if (r == EOF || g == EOF || b == EOF) goto fail_surface;
            }
            if (maxv != 255) {
                r = (r * 255) / maxv;
                g = (g * 255) / maxv;
                b = (b * 255) / maxv;
            }
            row[x * 4 + 0] = (unsigned char)b;
            row[x * 4 + 1] = (unsigned char)g;
            row[x * 4 + 2] = (unsigned char)r;
            row[x * 4 + 3] = 0;
        }
    }
    cairo_surface_mark_dirty(wallpaper);
    fclose(f);
    return wallpaper;

fail_surface:
    if (wallpaper) cairo_surface_destroy(wallpaper);
    wallpaper = NULL;
fail:
    fclose(f);
    return NULL;
}

static gboolean draw_wallpaper(GtkWidget *area, cairo_t *cr, gpointer data) {
    cairo_surface_t *wallpaper;
    int iw;
    int ih;
    GtkAllocation a;
    double sx, sy, scale, w, h, x, y;
    (void)data;
    gtk_widget_get_allocation(area, &a);
    cairo_set_source_rgb(cr, 0.02, 0.07, 0.16);
    cairo_paint(cr);
    wallpaper = load_wallpaper_surface();
    if (!wallpaper || a.width <= 0 || a.height <= 0) return FALSE;
    iw = cairo_image_surface_get_width(wallpaper);
    ih = cairo_image_surface_get_height(wallpaper);
    if (iw <= 0 || ih <= 0) return FALSE;
    sx = (double)a.width / (double)iw;
    sy = (double)a.height / (double)ih;
    scale = sx > sy ? sx : sy;
    w = (double)iw * scale;
    h = (double)ih * scale;
    x = ((double)a.width - w) * 0.5;
    y = ((double)a.height - h) * 0.5;
    cairo_save(cr);
    cairo_translate(cr, x, y);
    cairo_scale(cr, scale, scale);
    cairo_set_source_surface(cr, wallpaper, 0.0, 0.0);
    cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_GOOD);
    cairo_paint(cr);
    cairo_restore(cr);
    return FALSE;
}

static gboolean redraw_widget(gpointer data) {
    if (data) gtk_widget_queue_draw(GTK_WIDGET(data));
    return TRUE;
}

static void show_background(void) {
    GtkWidget *win = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    GtkWidget *area = gtk_drawing_area_new();
    GdkScreen *screen = gdk_screen_get_default();
    int sw = screen ? gdk_screen_get_width(screen) : 1024;
    int sh = screen ? gdk_screen_get_height(screen) : 768;
    setup_transparent(win);
    gtk_style_context_add_class(gtk_widget_get_style_context(area), "wallpaper");
    gtk_layer_init_for_window(GTK_WINDOW(win));
    gtk_layer_set_layer(GTK_WINDOW(win), GTK_LAYER_SHELL_LAYER_BACKGROUND);
    gtk_layer_set_anchor(GTK_WINDOW(win), GTK_LAYER_SHELL_EDGE_TOP, TRUE);
    gtk_layer_set_anchor(GTK_WINDOW(win), GTK_LAYER_SHELL_EDGE_BOTTOM, TRUE);
    gtk_layer_set_anchor(GTK_WINDOW(win), GTK_LAYER_SHELL_EDGE_LEFT, TRUE);
    gtk_layer_set_anchor(GTK_WINDOW(win), GTK_LAYER_SHELL_EDGE_RIGHT, TRUE);
    gtk_layer_set_exclusive_zone(GTK_WINDOW(win), 0);
    gtk_window_set_decorated(GTK_WINDOW(win), FALSE);
    gtk_window_set_default_size(GTK_WINDOW(win), sw, sh);
    gtk_widget_set_size_request(area, sw, sh);
    g_signal_connect(area, "draw", G_CALLBACK(draw_wallpaper), NULL);
    gtk_container_add(GTK_CONTAINER(win), area);
    gtk_widget_show_all(win);
    g_timeout_add(1000, redraw_widget, area);
}

static void show_dock(void) {
    GtkWidget *win = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    GdkScreen *screen = gdk_screen_get_default();
    int sw = screen ? gdk_screen_get_width(screen) : 1024;
    int dock_w = 390;
    setup_transparent(win);
    gtk_widget_set_name(win, "ridux-dock");
    gtk_style_context_add_class(gtk_widget_get_style_context(box), "dock");
    gtk_widget_set_halign(box, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(box, GTK_ALIGN_END);
    gtk_layer_init_for_window(GTK_WINDOW(win));
    gtk_layer_set_layer(GTK_WINDOW(win), GTK_LAYER_SHELL_LAYER_TOP);
    gtk_layer_set_anchor(GTK_WINDOW(win), GTK_LAYER_SHELL_EDGE_BOTTOM, TRUE);
    gtk_layer_set_anchor(GTK_WINDOW(win), GTK_LAYER_SHELL_EDGE_LEFT, TRUE);
    gtk_layer_set_margin(GTK_WINDOW(win), GTK_LAYER_SHELL_EDGE_BOTTOM, 18);
    gtk_layer_set_margin(GTK_WINDOW(win), GTK_LAYER_SHELL_EDGE_LEFT,
                         sw > dock_w ? (sw - dock_w) / 2 : 0);
    gtk_layer_set_exclusive_zone(GTK_WINDOW(win), 0);
    gtk_window_set_decorated(GTK_WINDOW(win), FALSE);
    gtk_window_set_default_size(GTK_WINDOW(win), 390, 68);
    gtk_container_add(GTK_CONTAINER(win), box);
    gtk_box_pack_start(GTK_BOX(box), dock_button("R", "/usr/bin/ridux-open-launcher"), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), dock_button("Files", "/opt/wayfire/bin/thunar"), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), dock_button("Web", "/opt/firefox/firefox"), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), dock_button("Set", "/opt/wayfire/bin/ridux-settings"), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), dock_button("Term", "/opt/wayfire/bin/ridux-terminal"), FALSE, FALSE, 0);
    gtk_widget_show_all(win);
}

static void show_launcher(void) {
    GtkWidget *win = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    GtkWidget *outer = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);
    GtkWidget *search = gtk_entry_new();
    GtkWidget *grid = gtk_grid_new();
    const char *names[] = {"Files", "Firefox", "Settings", "Terminal", "About", "Monitor"};
    const char *cmds[] = {"/opt/wayfire/bin/thunar", "/opt/firefox/firefox", "/opt/wayfire/bin/ridux-settings",
                          "/opt/wayfire/bin/ridux-terminal", "/opt/wayfire/bin/ridux-about",
                          "/opt/wayfire/bin/ridux-monitor"};
    int i;
    gtk_window_set_title(GTK_WINDOW(win), "Ridux Launcher");
    gtk_window_set_default_size(GTK_WINDOW(win), 560, 420);
    gtk_container_set_border_width(GTK_CONTAINER(outer), 22);
    gtk_style_context_add_class(gtk_widget_get_style_context(outer), "window-card");
    gtk_entry_set_placeholder_text(GTK_ENTRY(search), "Search apps...");
    gtk_grid_set_row_spacing(GTK_GRID(grid), 12);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 12);
    gtk_box_pack_start(GTK_BOX(outer), label_box("<span font='32'>R</span>", "Ridux", "Wayfire session"), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(outer), search, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(outer), grid, TRUE, TRUE, 0);
    for (i = 0; i < 6; ++i) {
        GtkWidget *b = gtk_button_new_with_label(names[i]);
        gtk_style_context_add_class(gtk_widget_get_style_context(b), "tile");
        g_signal_connect(b, "clicked", G_CALLBACK(spawn_cmd), (gpointer)cmds[i]);
        gtk_grid_attach(GTK_GRID(grid), b, i % 3, i / 3, 1, 1);
    }
    gtk_container_add(GTK_CONTAINER(win), outer);
    g_signal_connect(win, "destroy", G_CALLBACK(gtk_main_quit), NULL);
    gtk_widget_show_all(win);
}

static void show_files(void) {
    GtkWidget *win = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    GtkWidget *outer = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 14);
    GtkWidget *side = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    GtkWidget *main = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    GtkWidget *grid = gtk_grid_new();
    const char *folders[] = {"Home", "Desktop", "Documents", "Downloads", "Pictures", "Videos", "Music", "Trash"};
    int i;
    gtk_window_set_title(GTK_WINDOW(win), "Files");
    gtk_window_set_default_size(GTK_WINDOW(win), 820, 520);
    gtk_container_set_border_width(GTK_CONTAINER(outer), 18);
    gtk_style_context_add_class(gtk_widget_get_style_context(outer), "window-card");
    gtk_style_context_add_class(gtk_widget_get_style_context(side), "sidebar");
    gtk_widget_set_size_request(side, 180, -1);
    gtk_grid_set_row_spacing(GTK_GRID(grid), 12);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 12);
    gtk_box_pack_start(GTK_BOX(side), label_box("<span font='34'>R</span>", "Files", "Wayland"), FALSE, FALSE, 6);
    for (i = 0; i < 6; ++i) gtk_box_pack_start(GTK_BOX(side), gtk_label_new(folders[i]), FALSE, FALSE, 4);
    gtk_box_pack_start(GTK_BOX(main), label_box("<span font='22'>/ home / ridux</span>", "Personal Folder", ""), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(main), grid, TRUE, TRUE, 0);
    for (i = 0; i < 8; ++i) {
        GtkWidget *tile = gtk_button_new_with_label(folders[i]);
        gtk_widget_set_size_request(tile, 128, 90);
        gtk_style_context_add_class(gtk_widget_get_style_context(tile), "tile");
        gtk_grid_attach(GTK_GRID(grid), tile, i % 4, i / 4, 1, 1);
    }
    gtk_box_pack_start(GTK_BOX(outer), side, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(outer), main, TRUE, TRUE, 0);
    gtk_container_add(GTK_CONTAINER(win), outer);
    g_signal_connect(win, "destroy", G_CALLBACK(gtk_main_quit), NULL);
    gtk_widget_show_all(win);
}

static void show_settings(void) {
    GtkWidget *win = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    GtkWidget *outer = gtk_box_new(GTK_ORIENTATION_VERTICAL, 14);
    GtkWidget *grid = gtk_grid_new();
    const char *items[] = {"Appearance", "Display", "Network", "Sound", "Input", "About"};
    int i;
    gtk_window_set_title(GTK_WINDOW(win), "Settings");
    gtk_window_set_default_size(GTK_WINDOW(win), 640, 460);
    gtk_container_set_border_width(GTK_CONTAINER(outer), 22);
    gtk_style_context_add_class(gtk_widget_get_style_context(outer), "window-card");
    gtk_grid_set_row_spacing(GTK_GRID(grid), 12);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 12);
    gtk_box_pack_start(GTK_BOX(outer), label_box("<span font='28'>Settings</span>", "Ridux Settings", "Wayland control center"), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(outer), grid, TRUE, TRUE, 0);
    for (i = 0; i < 6; ++i) {
        GtkWidget *tile = gtk_button_new_with_label(items[i]);
        gtk_widget_set_size_request(tile, 170, 88);
        gtk_style_context_add_class(gtk_widget_get_style_context(tile), "tile");
        gtk_grid_attach(GTK_GRID(grid), tile, i % 3, i / 3, 1, 1);
    }
    gtk_container_add(GTK_CONTAINER(win), outer);
    g_signal_connect(win, "destroy", G_CALLBACK(gtk_main_quit), NULL);
    gtk_widget_show_all(win);
}

static void show_about(void) {
    GtkWidget *win = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    GtkWidget *outer = gtk_box_new(GTK_ORIENTATION_VERTICAL, 14);
    GtkWidget *grid = gtk_grid_new();
    const char *left[] = {"Desktop", "Compositor", "Session", "Apps", "Shell"};
    const char *right[] = {"Wayland", "Wayfire", "RiduxOS", "GTK clients", "Ridux Wayfire Shell"};
    int i;
    gtk_window_set_title(GTK_WINDOW(win), "About RiduxOS");
    gtk_window_set_default_size(GTK_WINDOW(win), 560, 420);
    gtk_container_set_border_width(GTK_CONTAINER(outer), 22);
    gtk_style_context_add_class(gtk_widget_get_style_context(outer), "window-card");
    gtk_grid_set_row_spacing(GTK_GRID(grid), 10);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 28);
    gtk_box_pack_start(GTK_BOX(outer), label_box("<span font='42'>R</span>", "RiduxOS", "Wayfire desktop session"), FALSE, FALSE, 0);
    for (i = 0; i < 5; ++i) {
        GtkWidget *l = gtk_label_new(left[i]);
        GtkWidget *r = gtk_label_new(right[i]);
        gtk_widget_set_halign(l, GTK_ALIGN_START);
        gtk_widget_set_halign(r, GTK_ALIGN_END);
        gtk_style_context_add_class(gtk_widget_get_style_context(l), "muted");
        gtk_grid_attach(GTK_GRID(grid), l, 0, i, 1, 1);
        gtk_grid_attach(GTK_GRID(grid), r, 1, i, 1, 1);
    }
    gtk_box_pack_start(GTK_BOX(outer), grid, TRUE, TRUE, 0);
    gtk_container_add(GTK_CONTAINER(win), outer);
    g_signal_connect(win, "destroy", G_CALLBACK(gtk_main_quit), NULL);
    gtk_widget_show_all(win);
}

static void show_monitor(void) {
    GtkWidget *win = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    GtkWidget *outer = gtk_box_new(GTK_ORIENTATION_VERTICAL, 14);
    GtkWidget *grid = gtk_grid_new();
    const char *items[] = {"Compositor: Wayfire", "Renderer: wlroots/pixman", "Display: DRM framebuffer",
                           "Input: Ridux event bridge", "Shell: GTK layer-shell", "Desktop: native Ridux disabled"};
    int i;
    gtk_window_set_title(GTK_WINDOW(win), "System Monitor");
    gtk_window_set_default_size(GTK_WINDOW(win), 640, 420);
    gtk_container_set_border_width(GTK_CONTAINER(outer), 22);
    gtk_style_context_add_class(gtk_widget_get_style_context(outer), "window-card");
    gtk_grid_set_row_spacing(GTK_GRID(grid), 12);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 12);
    gtk_box_pack_start(GTK_BOX(outer), label_box("<span font='28'>Monitor</span>", "Ridux Monitor", "Wayland status"), FALSE, FALSE, 0);
    for (i = 0; i < 6; ++i) {
        GtkWidget *tile = gtk_button_new_with_label(items[i]);
        gtk_widget_set_size_request(tile, 250, 62);
        gtk_style_context_add_class(gtk_widget_get_style_context(tile), "tile");
        gtk_grid_attach(GTK_GRID(grid), tile, i % 2, i / 2, 1, 1);
    }
    gtk_box_pack_start(GTK_BOX(outer), grid, TRUE, TRUE, 0);
    gtk_container_add(GTK_CONTAINER(win), outer);
    g_signal_connect(win, "destroy", G_CALLBACK(gtk_main_quit), NULL);
    gtk_widget_show_all(win);
}

static void show_terminal(void) {
    GtkWidget *win = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    GtkWidget *outer = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    GtkWidget *body = gtk_label_new(NULL);
    gtk_window_set_title(GTK_WINDOW(win), "Terminal");
    gtk_window_set_default_size(GTK_WINDOW(win), 720, 420);
    gtk_container_set_border_width(GTK_CONTAINER(outer), 20);
    gtk_style_context_add_class(gtk_widget_get_style_context(outer), "window-card");
    gtk_label_set_markup(GTK_LABEL(body),
        "<span font='13' foreground='#78ff9f'>ridux@wayfire</span><span font='13'>:~$ compositor --status\n"
        "Wayfire is running as the primary desktop.\n"
        "Native Ridux desktop windows are disabled.\n\n"
        "ridux@wayfire:~$ apps --session\n"
        "launcher, files, settings, about and monitor are GTK/Wayland clients.\n\n"
        "ridux@wayfire:~$ _</span>");
    gtk_widget_set_halign(body, GTK_ALIGN_START);
    gtk_widget_set_valign(body, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(outer), body, TRUE, TRUE, 0);
    gtk_container_add(GTK_CONTAINER(win), outer);
    g_signal_connect(win, "destroy", G_CALLBACK(gtk_main_quit), NULL);
    gtk_widget_show_all(win);
}

int main(int argc, char **argv) {
    const char *mode;
    g_setenv("GDK_BACKEND", "wayland", FALSE);
    mode = app_mode(argv[0]);
    if (strcmp(mode, "session") == 0) {
        start_session_services();
        return 0;
    }
    gtk_init(&argc, &argv);
    load_css();
    if (strcmp(mode, "background") == 0) show_background();
    else if (strcmp(mode, "dock") == 0) show_dock();
    else if (strcmp(mode, "files") == 0) show_files();
    else if (strcmp(mode, "settings") == 0) show_settings();
    else if (strcmp(mode, "about") == 0) show_about();
    else if (strcmp(mode, "monitor") == 0) show_monitor();
    else if (strcmp(mode, "terminal") == 0) show_terminal();
    else show_launcher();
    gtk_main();
    return 0;
}
