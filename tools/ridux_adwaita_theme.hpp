#ifndef RIDUX_ADWAITA_THEME_HPP
#define RIDUX_ADWAITA_THEME_HPP

#include "ridux_native_ui.h"
#include "ridux_ui_components.hpp"

namespace riduxadwaita {

struct Metrics {
    float panel_radius = 18.0f;
    float popover_radius = 24.0f;
    float control_radius = 12.0f;
    float taskbar_height = 56.0f;
    float taskbar_item = 44.0f;
    float spacing = 8.0f;
};

struct Palette {
    rui_color_t text = rui_rgb8(246, 248, 250);
    rui_color_t text_muted = rui_rgb8(196, 207, 222);
    rui_color_t text_dim = rui_rgb8(145, 158, 176);
    rui_color_t accent = rui_rgb8(53, 132, 228);
    rui_color_t accent_hover = rui_rgba(53.0f / 255.0f, 132.0f / 255.0f, 228.0f / 255.0f, 0.28f);
    rui_color_t surface = rui_rgba(27.0f / 255.0f, 32.0f / 255.0f, 42.0f / 255.0f, 0.88f);
    rui_color_t surface_high = rui_rgba(46.0f / 255.0f, 54.0f / 255.0f, 68.0f / 255.0f, 0.82f);
    rui_color_t border = rui_rgba(1.0f, 1.0f, 1.0f, 0.12f);
    rui_color_t control = rui_rgba(1.0f, 1.0f, 1.0f, 0.070f);
    rui_color_t control_hover = rui_rgba(1.0f, 1.0f, 1.0f, 0.115f);
    rui_color_t shadow = rui_rgba(0.0f, 0.0f, 0.0f, 0.24f);
};

struct Theme {
    Palette palette;
    Metrics metrics;

    riduxui::Theme to_riduxui() const {
        riduxui::Theme t;
        t.text = palette.text;
        t.text_muted = palette.text_muted;
        t.text_dim = palette.text_dim;
        t.panel_top = rui_rgba(236.0f / 255.0f, 244.0f / 255.0f, 1.0f, 0.18f);
        t.panel_bottom = palette.surface;
        t.panel_border = palette.border;
        t.control = palette.control;
        t.control_hover = palette.control_hover;
        t.shadow = palette.shadow;
        t.accent = palette.accent;
        t.panel_radius = metrics.panel_radius;
        t.control_radius = metrics.control_radius;
        return t;
    }
};

inline Theme default_theme() {
    return Theme();
}

} // namespace riduxadwaita

#endif
