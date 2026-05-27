#include <math.h>
#include <stdlib.h>

#include <gdk/gdk.h>
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <gtk/gtk.h>
#include <gtk-layer-shell.h>

typedef struct {
    GdkPixbuf *background;
    GdkPixbuf *logo;
} RiduxWallpaper;

static void ridux_paint_cover(cairo_t *cr, GdkPixbuf *pixbuf,
                              int width, int height, double alpha) {
    int iw, ih;
    double scale, dw, dh, dx, dy;

    if (!pixbuf || width <= 0 || height <= 0) return;
    iw = gdk_pixbuf_get_width(pixbuf);
    ih = gdk_pixbuf_get_height(pixbuf);
    if (iw <= 0 || ih <= 0) return;

    scale = fmax((double)width / (double)iw, (double)height / (double)ih);
    dw = (double)iw * scale;
    dh = (double)ih * scale;
    dx = ((double)width - dw) * 0.5;
    dy = ((double)height - dh) * 0.5;

    cairo_save(cr);
    cairo_translate(cr, dx, dy);
    cairo_scale(cr, scale, scale);
    gdk_cairo_set_source_pixbuf(cr, pixbuf, 0.0, 0.0);
    cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_BILINEAR);
    cairo_paint_with_alpha(cr, alpha);
    cairo_restore(cr);
}

static void ridux_paint_logo(cairo_t *cr, GdkPixbuf *pixbuf,
                             int width, int height) {
    int iw, ih;
    double target, scale, dw, dh, dx, dy;

    if (!pixbuf || width <= 0 || height <= 0) return;
    iw = gdk_pixbuf_get_width(pixbuf);
    ih = gdk_pixbuf_get_height(pixbuf);
    if (iw <= 0 || ih <= 0) return;

    target = fmin((double)width, (double)height) * 0.22;
    if (target < 104.0) target = 104.0;
    if (target > 184.0) target = 184.0;
    scale = target / (double)((iw > ih) ? iw : ih);
    dw = (double)iw * scale;
    dh = (double)ih * scale;
    dx = ((double)width - dw) * 0.5;
    dy = ((double)height - dh) * 0.5 - (double)height * 0.04;

    cairo_save(cr);
    cairo_translate(cr, dx, dy);
    cairo_scale(cr, scale, scale);
    gdk_cairo_set_source_pixbuf(cr, pixbuf, 0.0, 0.0);
    cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_BILINEAR);
    cairo_paint_with_alpha(cr, 0.82);
    cairo_restore(cr);
}

static gboolean ridux_draw_wallpaper(GtkWidget *widget, cairo_t *cr, gpointer data) {
    RiduxWallpaper *wall = (RiduxWallpaper *)data;
    int width = gtk_widget_get_allocated_width(widget);
    int height = gtk_widget_get_allocated_height(widget);
    cairo_pattern_t *shade;

    cairo_set_source_rgb(cr, 0.015, 0.025, 0.045);
    cairo_paint(cr);
    ridux_paint_cover(cr, wall ? wall->background : NULL, width, height, 1.0);

    shade = cairo_pattern_create_linear(0.0, 0.0, 0.0, (double)height);
    cairo_pattern_add_color_stop_rgba(shade, 0.0, 0.02, 0.05, 0.10, 0.08);
    cairo_pattern_add_color_stop_rgba(shade, 0.68, 0.00, 0.02, 0.05, 0.08);
    cairo_pattern_add_color_stop_rgba(shade, 1.0, 0.00, 0.00, 0.00, 0.20);
    cairo_set_source(cr, shade);
    cairo_paint(cr);
    cairo_pattern_destroy(shade);

    ridux_paint_logo(cr, wall ? wall->logo : NULL, width, height);
    return FALSE;
}

int main(int argc, char **argv) {
    GtkWidget *window;
    RiduxWallpaper wall = {0};
    GError *error = NULL;
    const char *background_path =
        (argc > 1) ? argv[1] : "/usr/share/ridux/wallpapers/WallpaperMain.png";
    const char *logo_path =
        (argc > 2) ? argv[2] : "/usr/share/icons/Ridux/256x256/apps/ridux-logo.png";

    setenv("GDK_BACKEND", "wayland", 0);
    setenv("GTK_USE_PORTAL", "0", 1);
    setenv("DBUS_SESSION_BUS_ADDRESS", "unix:path=/tmp/ridux-wallpaper-no-bus", 1);
    setenv("GSETTINGS_BACKEND", "memory", 1);
    setenv("GIO_USE_VFS", "local", 1);
    setenv("GIO_USE_VOLUME_MONITOR", "unix", 1);
    setenv("NO_AT_BRIDGE", "1", 1);

    gtk_init(&argc, &argv);

    wall.background = gdk_pixbuf_new_from_file(background_path, &error);
    if (error) g_clear_error(&error);
    wall.logo = gdk_pixbuf_new_from_file(logo_path, &error);
    if (error) g_clear_error(&error);

    window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_decorated(GTK_WINDOW(window), FALSE);
    gtk_widget_set_app_paintable(window, TRUE);
    gtk_layer_init_for_window(GTK_WINDOW(window));
    gtk_layer_set_namespace(GTK_WINDOW(window), "ridux-wallpaper");
    gtk_layer_set_layer(GTK_WINDOW(window), GTK_LAYER_SHELL_LAYER_BACKGROUND);
    gtk_layer_set_anchor(GTK_WINDOW(window), GTK_LAYER_SHELL_EDGE_TOP, TRUE);
    gtk_layer_set_anchor(GTK_WINDOW(window), GTK_LAYER_SHELL_EDGE_RIGHT, TRUE);
    gtk_layer_set_anchor(GTK_WINDOW(window), GTK_LAYER_SHELL_EDGE_BOTTOM, TRUE);
    gtk_layer_set_anchor(GTK_WINDOW(window), GTK_LAYER_SHELL_EDGE_LEFT, TRUE);
    gtk_layer_set_exclusive_zone(GTK_WINDOW(window), 0);

    g_signal_connect(window, "draw", G_CALLBACK(ridux_draw_wallpaper), &wall);
    gtk_widget_show_all(window);
    gtk_main();

    if (wall.background) g_object_unref(wall.background);
    if (wall.logo) g_object_unref(wall.logo);
    return 0;
}
