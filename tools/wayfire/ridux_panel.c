#include <gtk/gtk.h>
#include <gtk-layer-shell/gtk-layer-shell.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

struct ridux_panel {
    GtkWidget *panel;
    GtkWidget *menu;
    GtkWidget *quick;
    GtkWidget *clock;
    GtkWidget *content;
    GtkWidget *workspaces[4];
    int active_workspace;
    guint clock_timer;
};

static GtkWidget *make_menu_window(struct ridux_panel *p);
static GtkWidget *make_quick_window(struct ridux_panel *p);
static void toggle_menu(GtkWidget *widget, gpointer data);
static void toggle_quick(GtkWidget *widget, gpointer data);
static GtkWidget *make_professional_panel_contents(struct ridux_panel *p);

static int panel_debug_enabled(void) {
    const char *value = g_getenv("RIDUX_PANEL_DEBUG");
    return value && *value &&
           g_strcmp0(value, "0") != 0 &&
           g_strcmp0(value, "false") != 0;
}

static void raw_log(const char *message) {
    if (!panel_debug_enabled()) return;
    if (!message) return;
    write(STDERR_FILENO, message, strlen(message));
}

static int layer_shell_enabled(void) {
    const char *value = g_getenv("RIDUX_PANEL_LAYER_SHELL");
    if (!value || !*value) return 1;
    return !(g_strcmp0(value, "0") == 0 || g_strcmp0(value, "false") == 0);
}

static gboolean log_draw(GtkWidget *widget, cairo_t *cr, gpointer data) {
    const char *name = data ? (const char *)data : "widget";
    (void)widget;
    (void)cr;
    if (!panel_debug_enabled()) return FALSE;
    fprintf(stderr, "ridux-panel: draw %s\n", name);
    return FALSE;
}

static void log_widget_event(GtkWidget *widget, gpointer data) {
    const char *name = data ? (const char *)data : "widget";
    (void)widget;
    if (!panel_debug_enabled()) return;
    fprintf(stderr, "ridux-panel: %s\n", name);
}

static void setup_transparent(GtkWidget *win) {
    GdkScreen *screen = gdk_screen_get_default();
    GdkVisual *visual = screen ? gdk_screen_get_rgba_visual(screen) : NULL;
    if (visual) gtk_widget_set_visual(win, visual);
    gtk_widget_set_app_paintable(win, TRUE);
}

static void load_css(void) {
    static const char css[] =
        "* { font-family: 'Inter', 'Segoe UI', 'Cantarell', 'DejaVu Sans', sans-serif; font-size: 12px; }\n"
        "window { background: transparent; }\n"
        ".ridux-panel-frame { background: linear-gradient(135deg, rgba(11, 36, 89, 0.78), rgba(10, 120, 185, 0.52)); border: 1px solid rgba(185, 230, 255, 0.24); border-radius: 18px; box-shadow: 0 18px 54px rgba(0, 15, 40, 0.46); padding: 6px 10px; }\n"
        ".brand-button { color: #f8fbff; background: linear-gradient(135deg, rgba(80, 168, 255, 0.82), rgba(25, 97, 192, 0.72)); border: 1px solid rgba(216, 243, 255, 0.34); border-radius: 13px; padding: 4px 15px; font-weight: 800; }\n"
        ".brand-button:hover { background: linear-gradient(135deg, rgba(97, 188, 255, 0.92), rgba(38, 119, 214, 0.82)); border-color: rgba(236, 251, 255, 0.62); }\n"
        ".workspace { min-width: 33px; min-height: 30px; color: rgba(240,248,255,0.86); background: rgba(255,255,255,0.055); border: 1px solid rgba(255,255,255,0.080); border-radius: 11px; padding: 0 9px; font-weight: 700; }\n"
        ".workspace:hover { color: #ffffff; background: rgba(255,255,255,0.14); border-color: rgba(160,224,255,0.42); }\n"
        ".workspace-active { color: #ffffff; background: linear-gradient(135deg, rgba(74, 160, 255, 0.95), rgba(34, 124, 225, 0.78)); border-color: rgba(220,246,255,0.58); box-shadow: 0 0 22px rgba(54, 156, 255, 0.38); }\n"
        ".panel-icon { min-width: 36px; min-height: 30px; color: #f8fbff; background: rgba(255,255,255,0.070); border: 1px solid rgba(255,255,255,0.095); border-radius: 11px; padding: 0 11px; font-weight: 700; }\n"
        ".panel-icon:hover { background: rgba(54, 157, 255, 0.28); border-color: rgba(159,225,255,0.46); }\n"
        ".status-pill { min-height: 30px; color: rgba(244,250,255,0.94); background: rgba(255,255,255,0.074); border: 1px solid rgba(255,255,255,0.105); border-radius: 12px; padding: 0 13px; font-weight: 700; }\n"
        ".status-pill:hover { background: rgba(54,157,255,0.24); border-color: rgba(160,224,255,0.40); }\n"
        ".interactive-module { padding: 0; }\n"
        ".interactive-module label { color: rgba(244,248,255,0.92); padding: 4px 13px; font-weight: 700; }\n"
        ".clock-label { color: #f8fbff; background: rgba(255,255,255,0.105); border: 1px solid rgba(255,255,255,0.095); border-radius: 12px; font-weight: 800; padding: 5px 20px; }\n"
        ".status-label { color: rgba(239,245,255,0.90); padding: 0 7px; }\n"
        ".glass-card { background: linear-gradient(145deg, rgba(9, 34, 83, 0.76), rgba(8, 112, 166, 0.50)); border: 1px solid rgba(188, 232, 255, 0.23); border-radius: 20px; box-shadow: 0 28px 82px rgba(0, 16, 42, 0.50); color: #f8fbff; }\n"
        ".menu-search { color: #f8fbff; background: rgba(255,255,255,0.10); border: 1px solid rgba(188,232,255,0.18); border-radius: 13px; padding: 9px 12px; }\n"
        ".section-title { color: rgba(238,244,255,0.78); font-size: 11px; font-weight: 700; }\n"
        ".app-row { background: transparent; border: 1px solid transparent; border-radius: 12px; padding: 7px; }\n"
        ".app-row:hover { background: rgba(73, 169, 255, 0.20); border-color: rgba(160, 224, 255, 0.34); }\n"
        ".app-title { color: #ffffff; font-weight: 700; }\n"
        ".app-subtitle { color: rgba(229,238,255,0.62); font-size: 10px; }\n"
        ".quick-tile { color: #f8fbff; background: rgba(255,255,255,0.08); border: 1px solid rgba(255,255,255,0.10); border-radius: 12px; padding: 10px; }\n"
        ".quick-tile:hover { background: rgba(73, 169, 255, 0.26); border-color: rgba(160,224,255,0.45); }\n"
        ".quick-tile-active { background: linear-gradient(135deg, rgba(58, 162, 255, 0.92), rgba(24, 126, 214, 0.80)); border-color: rgba(221,246,255,0.50); }\n"
        ".slider trough { min-height: 5px; border-radius: 6px; background: rgba(255,255,255,0.12); }\n"
        ".slider highlight { border-radius: 6px; background: #46c6ff; }\n"
        ".notification { background: rgba(255,255,255,0.07); border: 1px solid rgba(255,255,255,0.10); border-radius: 12px; padding: 12px; }\n";
    GtkCssProvider *provider = gtk_css_provider_new();
    gtk_css_provider_load_from_data(provider, css, -1, NULL);
    gtk_style_context_add_provider_for_screen(gdk_screen_get_default(), GTK_STYLE_PROVIDER(provider), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(provider);
}

static GtkWidget *styled_label(const char *text, const char *klass) {
    GtkWidget *label = gtk_label_new(text);
    gtk_label_set_xalign(GTK_LABEL(label), 0.0f);
    if (klass) gtk_style_context_add_class(gtk_widget_get_style_context(label), klass);
    return label;
}

static void spawn_cmd(GtkWidget *widget, gpointer data) {
    const char *cmd = data;
    (void)widget;
    if (cmd && *cmd) g_spawn_command_line_async(cmd, NULL);
}

static gboolean update_clock(gpointer data) {
    struct ridux_panel *p = data;
    char buf[32];
    time_t now = time(NULL);
    struct tm tmv;
    if (!p || !p->clock) return G_SOURCE_CONTINUE;
    localtime_r(&now, &tmv);
    strftime(buf, sizeof(buf), "%H:%M", &tmv);
    gtk_label_set_text(GTK_LABEL(p->clock), buf);
    return G_SOURCE_CONTINUE;
}

static void update_workspaces(struct ridux_panel *p) {
    int i;
    for (i = 0; i < 4; ++i) {
        if (!p->workspaces[i]) continue;
        GtkStyleContext *ctx = gtk_widget_get_style_context(p->workspaces[i]);
        if (i == p->active_workspace) gtk_style_context_add_class(ctx, "workspace-active");
        else gtk_style_context_remove_class(ctx, "workspace-active");
    }
}

static void workspace_clicked(GtkWidget *widget, gpointer data) {
    struct ridux_panel *p = g_object_get_data(G_OBJECT(widget), "ridux-panel");
    p->active_workspace = GPOINTER_TO_INT(data);
    update_workspaces(p);
}

static void toggle_window(GtkWidget *window) {
    if (gtk_widget_get_visible(window)) gtk_widget_hide(window);
    else gtk_widget_show_all(window);
}

static void toggle_menu(GtkWidget *widget, gpointer data) {
    struct ridux_panel *p = data;
    (void)widget;
    if (!p->menu) p->menu = make_menu_window(p);
    if (p->quick && gtk_widget_get_visible(p->quick)) gtk_widget_hide(p->quick);
    toggle_window(p->menu);
}

static void toggle_quick(GtkWidget *widget, gpointer data) {
    struct ridux_panel *p = data;
    (void)widget;
    if (!p->quick) p->quick = make_quick_window(p);
    if (p->menu && gtk_widget_get_visible(p->menu)) gtk_widget_hide(p->menu);
    toggle_window(p->quick);
}

static GtkWidget *icon_button(const char *text, const char *klass) {
    GtkWidget *button = gtk_button_new_with_label(text);
    gtk_button_set_relief(GTK_BUTTON(button), GTK_RELIEF_NONE);
    gtk_style_context_add_class(gtk_widget_get_style_context(button), klass ? klass : "panel-icon");
    return button;
}

static GtkWidget *module_label(const char *text, const char *klass) {
    GtkWidget *label = gtk_label_new(text);
    gtk_label_set_xalign(GTK_LABEL(label), 0.5f);
    gtk_style_context_add_class(gtk_widget_get_style_context(label), klass ? klass : "status-pill");
    return label;
}

static gboolean status_module_clicked(GtkWidget *widget, GdkEventButton *event, gpointer data) {
    if (!event || event->button == 1) {
        toggle_quick(widget, data);
        return TRUE;
    }
    return FALSE;
}

static GtkWidget *status_module(const char *text, struct ridux_panel *p) {
    GtkWidget *box = gtk_event_box_new();
    GtkWidget *label = gtk_label_new(text);
    gtk_label_set_xalign(GTK_LABEL(label), 0.5f);
    gtk_widget_add_events(box, GDK_BUTTON_PRESS_MASK | GDK_ENTER_NOTIFY_MASK | GDK_LEAVE_NOTIFY_MASK);
    gtk_widget_set_can_focus(box, TRUE);
    gtk_style_context_add_class(gtk_widget_get_style_context(box), "status-pill");
    gtk_style_context_add_class(gtk_widget_get_style_context(box), "interactive-module");
    gtk_container_add(GTK_CONTAINER(box), label);
    g_signal_connect(box, "button-press-event", G_CALLBACK(status_module_clicked), p);
    return box;
}

static GtkWidget *spacer_widget(void) {
    GtkWidget *spacer = gtk_label_new("");
    gtk_widget_set_hexpand(spacer, TRUE);
    return spacer;
}

static GtkWidget *make_professional_panel_contents(struct ridux_panel *p) {
    GtkWidget *frame = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    GtkWidget *left = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    GtkWidget *center = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    GtkWidget *right = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    GtkWidget *brand = icon_button("RIDUX", "brand-button");
    GtkWidget *terminal = icon_button("TERM", "panel-icon");
    GtkWidget *browser = icon_button("WEB", "panel-icon");
    GtkWidget *lock = icon_button("LOCK", "panel-icon");
    int i;
    gtk_style_context_add_class(gtk_widget_get_style_context(frame), "ridux-panel-frame");
    gtk_widget_set_hexpand(frame, TRUE);
    gtk_widget_set_vexpand(frame, FALSE);
    g_signal_connect(brand, "clicked", G_CALLBACK(toggle_menu), p);
    gtk_box_pack_start(GTK_BOX(left), brand, FALSE, FALSE, 0);
    for (i = 0; i < 4; ++i) {
        char num[2] = {(char)('1' + i), 0};
        GtkWidget *ws = icon_button(num, "workspace");
        p->workspaces[i] = ws;
        g_object_set_data(G_OBJECT(ws), "ridux-panel", p);
        g_signal_connect(ws, "clicked", G_CALLBACK(workspace_clicked), GINT_TO_POINTER(i));
        gtk_box_pack_start(GTK_BOX(left), ws, FALSE, FALSE, 0);
    }
    g_signal_connect(terminal, "clicked", G_CALLBACK(spawn_cmd), "/opt/wayfire/bin/ridux-terminal");
    g_signal_connect(browser, "clicked", G_CALLBACK(spawn_cmd), "/opt/wayfire/bin/ridux-about");
    gtk_box_pack_start(GTK_BOX(left), terminal, FALSE, FALSE, 8);
    gtk_box_pack_start(GTK_BOX(left), browser, FALSE, FALSE, 0);
    p->clock = module_label("--:--", "clock-label");
    gtk_box_pack_start(GTK_BOX(center), p->clock, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(right), status_module("NET", p), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(right), status_module("BT", p), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(right), status_module("VOL 65%", p), FALSE, FALSE, 0);
    g_signal_connect(lock, "clicked", G_CALLBACK(spawn_cmd), "/usr/bin/ridux-lock");
    gtk_box_pack_start(GTK_BOX(right), lock, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(frame), left, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(frame), spacer_widget(), TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(frame), center, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(frame), spacer_widget(), TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(frame), right, FALSE, FALSE, 0);
    update_workspaces(p);
    update_clock(p);
    return frame;
}

static GtkWidget *app_row(const char *icon, const char *title, const char *sub, const char *cmd) {
    GtkWidget *button = gtk_button_new();
    GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    GtkWidget *icon_label = gtk_label_new(icon);
    GtkWidget *text = gtk_box_new(GTK_ORIENTATION_VERTICAL, 1);
    GtkWidget *title_label = styled_label(title, "app-title");
    GtkWidget *sub_label = styled_label(sub, "app-subtitle");
    gtk_button_set_relief(GTK_BUTTON(button), GTK_RELIEF_NONE);
    gtk_style_context_add_class(gtk_widget_get_style_context(button), "app-row");
    gtk_widget_set_size_request(icon_label, 30, 28);
    gtk_box_pack_start(GTK_BOX(text), title_label, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(text), sub_label, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(row), icon_label, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(row), text, TRUE, TRUE, 0);
    gtk_container_add(GTK_CONTAINER(button), row);
    g_signal_connect(button, "clicked", G_CALLBACK(spawn_cmd), (gpointer)cmd);
    return button;
}

static GtkWidget *quick_tile(const char *title, const char *sub, gboolean active) {
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    GtkWidget *button = gtk_button_new();
    GtkWidget *title_label = styled_label(title, "app-title");
    GtkWidget *sub_label = styled_label(sub, "app-subtitle");
    gtk_button_set_relief(GTK_BUTTON(button), GTK_RELIEF_NONE);
    gtk_style_context_add_class(gtk_widget_get_style_context(button), "quick-tile");
    if (active) gtk_style_context_add_class(gtk_widget_get_style_context(button), "quick-tile-active");
    gtk_box_pack_start(GTK_BOX(box), title_label, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), sub_label, FALSE, FALSE, 0);
    gtk_container_add(GTK_CONTAINER(button), box);
    return button;
}

static GtkWidget *make_menu_window(struct ridux_panel *p) {
    GtkWidget *win = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    GtkWidget *card = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    GtkWidget *search = gtk_entry_new();
    GtkWidget *title_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    GtkWidget *title = styled_label("Favoritas", "section-title");
    GtkWidget *spacer = gtk_label_new("");
    GtkWidget *apps = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    GtkWidget *all = icon_button("Todas las aplicaciones  >", "app-row");
    GtkWidget *bottom = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    (void)p;
    setup_transparent(win);
    gtk_window_set_decorated(GTK_WINDOW(win), FALSE);
    gtk_window_set_resizable(GTK_WINDOW(win), FALSE);
    gtk_window_set_default_size(GTK_WINDOW(win), 306, 460);
    gtk_window_set_keep_above(GTK_WINDOW(win), TRUE);
    if (layer_shell_enabled()) {
        gtk_layer_init_for_window(GTK_WINDOW(win));
        gtk_layer_set_namespace(GTK_WINDOW(win), "ridux-panel-menu");
        gtk_layer_set_layer(GTK_WINDOW(win), GTK_LAYER_SHELL_LAYER_OVERLAY);
        gtk_layer_set_anchor(GTK_WINDOW(win), GTK_LAYER_SHELL_EDGE_TOP, TRUE);
        gtk_layer_set_anchor(GTK_WINDOW(win), GTK_LAYER_SHELL_EDGE_LEFT, TRUE);
        gtk_layer_set_keyboard_mode(GTK_WINDOW(win), GTK_LAYER_SHELL_KEYBOARD_MODE_ON_DEMAND);
        gtk_layer_set_margin(GTK_WINDOW(win), GTK_LAYER_SHELL_EDGE_TOP, 76);
        gtk_layer_set_margin(GTK_WINDOW(win), GTK_LAYER_SHELL_EDGE_LEFT, 18);
    }
    gtk_style_context_add_class(gtk_widget_get_style_context(card), "glass-card");
    gtk_container_set_border_width(GTK_CONTAINER(card), 14);
    gtk_entry_set_placeholder_text(GTK_ENTRY(search), "Buscar aplicaciones...");
    gtk_style_context_add_class(gtk_widget_get_style_context(search), "menu-search");
    gtk_box_pack_start(GTK_BOX(title_row), title, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(title_row), spacer, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(title_row), gtk_label_new("CTRL"), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(apps), app_row("WEB", "Navegador Web", "Firefox", "/opt/wayfire/bin/ridux-about"), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(apps), app_row("TERM", "Terminal", "Ridux Terminal", "/opt/wayfire/bin/ridux-terminal"), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(apps), app_row("FILE", "Archivos", "Gestor de archivos", "/opt/wayfire/bin/thunar"), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(apps), app_row("SET", "Configuracion", "Ajustes del sistema", "/opt/wayfire/bin/ridux-settings"), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(apps), app_row("SHOP", "Tienda", "Ridux Store", "/opt/wayfire/bin/ridux-about"), FALSE, FALSE, 0);
    g_signal_connect(all, "clicked", G_CALLBACK(spawn_cmd), "/usr/bin/ridux-open-launcher");
    gtk_box_pack_start(GTK_BOX(bottom), icon_button("LOCK", "panel-icon"), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(bottom), icon_button("SET", "panel-icon"), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(bottom), icon_button("POWER", "panel-icon"), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(card), search, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(card), title_row, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(card), apps, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(card), all, FALSE, FALSE, 4);
    gtk_box_pack_end(GTK_BOX(card), bottom, FALSE, FALSE, 0);
    gtk_container_add(GTK_CONTAINER(win), card);
    g_signal_connect(win, "realize", G_CALLBACK(log_widget_event), "menu realize");
    g_signal_connect(win, "map", G_CALLBACK(log_widget_event), "menu map");
    g_signal_connect(win, "draw", G_CALLBACK(log_draw), "menu");
    return win;
}

static GtkWidget *make_quick_window(struct ridux_panel *p) {
    GtkWidget *win = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    GtkWidget *card = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    GtkWidget *date = styled_label("Ridux Control Center                                      SET", "app-subtitle");
    GtkWidget *grid = gtk_grid_new();
    GtkWidget *volume = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0, 100, 1);
    GtkWidget *brightness = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0, 100, 1);
    GtkWidget *media = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    GtkWidget *notif = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    (void)p;
    setup_transparent(win);
    gtk_window_set_decorated(GTK_WINDOW(win), FALSE);
    gtk_window_set_resizable(GTK_WINDOW(win), FALSE);
    gtk_window_set_default_size(GTK_WINDOW(win), 324, 474);
    gtk_window_set_keep_above(GTK_WINDOW(win), TRUE);
    if (layer_shell_enabled()) {
        gtk_layer_init_for_window(GTK_WINDOW(win));
        gtk_layer_set_namespace(GTK_WINDOW(win), "ridux-control-center");
        gtk_layer_set_layer(GTK_WINDOW(win), GTK_LAYER_SHELL_LAYER_OVERLAY);
        gtk_layer_set_anchor(GTK_WINDOW(win), GTK_LAYER_SHELL_EDGE_TOP, TRUE);
        gtk_layer_set_anchor(GTK_WINDOW(win), GTK_LAYER_SHELL_EDGE_RIGHT, TRUE);
        gtk_layer_set_keyboard_mode(GTK_WINDOW(win), GTK_LAYER_SHELL_KEYBOARD_MODE_ON_DEMAND);
        gtk_layer_set_margin(GTK_WINDOW(win), GTK_LAYER_SHELL_EDGE_TOP, 76);
        gtk_layer_set_margin(GTK_WINDOW(win), GTK_LAYER_SHELL_EDGE_RIGHT, 18);
    }
    gtk_style_context_add_class(gtk_widget_get_style_context(card), "glass-card");
    gtk_container_set_border_width(GTK_CONTAINER(card), 14);
    gtk_grid_set_row_spacing(GTK_GRID(grid), 8);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 8);
    gtk_grid_attach(GTK_GRID(grid), quick_tile("WiFi", "RiduxNet_5G", TRUE), 0, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), quick_tile("Bluetooth", "Activado", FALSE), 1, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), quick_tile("Modo oscuro", "Activado", FALSE), 0, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), quick_tile("No molestar", "Desactivado", FALSE), 1, 1, 1, 1);
    gtk_style_context_add_class(gtk_widget_get_style_context(volume), "slider");
    gtk_style_context_add_class(gtk_widget_get_style_context(brightness), "slider");
    gtk_range_set_value(GTK_RANGE(volume), 74);
    gtk_range_set_value(GTK_RANGE(brightness), 67);
    gtk_style_context_add_class(gtk_widget_get_style_context(media), "notification");
    gtk_box_pack_start(GTK_BOX(media), styled_label("Made of Mind", "app-title"), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(media), styled_label("Infected Mushroom", "app-subtitle"), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(media), gtk_label_new("Prev        Play        Next"), FALSE, FALSE, 6);
    gtk_style_context_add_class(gtk_widget_get_style_context(notif), "notification");
    gtk_box_pack_start(GTK_BOX(notif), styled_label("Ridux Store", "app-title"), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(notif), styled_label("Actualizacion disponible", "app-subtitle"), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(notif), styled_label("Hay una nueva actualizacion lista para instalar.", "app-subtitle"), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(card), date, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(card), grid, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(card), gtk_label_new("Volume"), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(card), volume, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(card), gtk_label_new("Brightness"), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(card), brightness, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(card), media, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(card), notif, FALSE, FALSE, 0);
    gtk_container_add(GTK_CONTAINER(win), card);
    g_signal_connect(win, "realize", G_CALLBACK(log_widget_event), "quick realize");
    g_signal_connect(win, "map", G_CALLBACK(log_widget_event), "quick map");
    g_signal_connect(win, "draw", G_CALLBACK(log_draw), "quick");
    return win;
}

static GtkWidget *make_panel_window(struct ridux_panel *p) {
    GtkWidget *win = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    GdkScreen *screen = gdk_screen_get_default();
    int sw = screen ? gdk_screen_get_width(screen) : 1024;
    setup_transparent(win);
    gtk_window_set_decorated(GTK_WINDOW(win), FALSE);
    gtk_window_set_resizable(GTK_WINDOW(win), FALSE);
    gtk_window_set_default_size(GTK_WINDOW(win), sw > 24 ? sw - 24 : sw, 60);
    gtk_window_set_keep_above(GTK_WINDOW(win), TRUE);
    if (layer_shell_enabled()) {
        gtk_layer_init_for_window(GTK_WINDOW(win));
        gtk_layer_set_namespace(GTK_WINDOW(win), "ridux-panel");
        gtk_layer_set_layer(GTK_WINDOW(win), GTK_LAYER_SHELL_LAYER_TOP);
        gtk_layer_set_anchor(GTK_WINDOW(win), GTK_LAYER_SHELL_EDGE_TOP, TRUE);
        gtk_layer_set_anchor(GTK_WINDOW(win), GTK_LAYER_SHELL_EDGE_LEFT, TRUE);
        gtk_layer_set_anchor(GTK_WINDOW(win), GTK_LAYER_SHELL_EDGE_RIGHT, TRUE);
        gtk_layer_set_margin(GTK_WINDOW(win), GTK_LAYER_SHELL_EDGE_TOP, 8);
        gtk_layer_set_margin(GTK_WINDOW(win), GTK_LAYER_SHELL_EDGE_LEFT, 10);
        gtk_layer_set_margin(GTK_WINDOW(win), GTK_LAYER_SHELL_EDGE_RIGHT, 10);
        gtk_layer_set_exclusive_zone(GTK_WINDOW(win), 70);
    }
    p->active_workspace = 0;
    p->content = make_professional_panel_contents(p);
    gtk_widget_set_size_request(p->content, sw > 24 ? sw - 24 : sw, 56);
    gtk_container_add(GTK_CONTAINER(win), p->content);
    g_signal_connect(win, "realize", G_CALLBACK(log_widget_event), "panel realize");
    g_signal_connect(win, "map", G_CALLBACK(log_widget_event), "panel map");
    g_signal_connect(win, "draw", G_CALLBACK(log_draw), "panel-window");
    return win;
}

int main(int argc, char **argv) {
    struct ridux_panel panel;
    setvbuf(stderr, NULL, _IONBF, 0);
    raw_log("ridux-panel: main enter\n");
    memset(&panel, 0, sizeof(panel));
    g_setenv("GDK_BACKEND", "wayland", TRUE);
    g_setenv("NO_AT_BRIDGE", "1", TRUE);
    g_setenv("GTK_USE_PORTAL", "0", TRUE);
    g_setenv("GIO_USE_VFS", "local", TRUE);
    g_set_prgname("ridux-panel");
    gdk_set_allowed_backends("wayland");
    raw_log("ridux-panel: before gtk_init_check\n");
    if (!gtk_init_check(&argc, &argv)) {
        raw_log("ridux-panel: gtk_init_check failed\n");
        return 1;
    }
    raw_log("ridux-panel: after gtk_init_check\n");
    load_css();
    raw_log("ridux-panel: css loaded\n");
    panel.panel = make_panel_window(&panel);
    raw_log("ridux-panel: panel created\n");
    panel.clock_timer = g_timeout_add_seconds(15, update_clock, &panel);
    raw_log("ridux-panel: before show panel\n");
    gtk_widget_show_all(panel.panel);
    raw_log("ridux-panel: after show panel\n");
    gtk_window_present(GTK_WINDOW(panel.panel));
    raw_log("ridux-panel: after present panel\n");
    fprintf(stderr, "ridux-panel: ready mode=%s\n", layer_shell_enabled() ? "layer-shell" : "gtk-toplevel");
    gtk_main();
    return 0;
}
