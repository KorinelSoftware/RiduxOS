#ifndef RIDUX_UI_COMPONENTS_HPP
#define RIDUX_UI_COMPONENTS_HPP

#include "ridux_native_ui.h"

#include <stdint.h>

namespace riduxui {

struct Rect {
    float x;
    float y;
    float w;
    float h;
};

struct Insets {
    float l;
    float t;
    float r;
    float b;
};

struct Theme {
    rui_color_t text;
    rui_color_t text_muted;
    rui_color_t text_dim;
    rui_color_t panel_top;
    rui_color_t panel_bottom;
    rui_color_t panel_border;
    rui_color_t control;
    rui_color_t control_hover;
    rui_color_t shadow;
    rui_color_t accent;
    float panel_radius;
    float control_radius;
};

struct Timeline {
    uint64_t now_ms;
    bool motion;

    float pulse(uint32_t period_ms, uint32_t offset_ms = 0) const {
        uint32_t p;
        uint32_t half;
        if (!motion || !period_ms) return 0.0f;
        p = (uint32_t)((now_ms + offset_ms) % period_ms);
        half = period_ms / 2u;
        if (!half) return 0.0f;
        if (p > half) p = period_ms - p;
        return (float)p / (float)half;
    }
};

struct FlexItem {
    Rect rect;
};

struct FlexRow {
    Rect bounds;
    float gap;
    float cursor;

    explicit FlexRow(Rect r, float spacing = 8.0f)
        : bounds(r), gap(spacing), cursor(r.x) {}

    Rect take(float width) {
        Rect out = { cursor, bounds.y, width, bounds.h };
        cursor += width + gap;
        return out;
    }

    Rect take_fill(float right_reserved = 0.0f) {
        float width = bounds.x + bounds.w - cursor - right_reserved;
        if (width < 0.0f) width = 0.0f;
        Rect out = { cursor, bounds.y, width, bounds.h };
        cursor += width + gap;
        return out;
    }
};

struct Grid {
    Rect bounds;
    int cols;
    float gap_x;
    float gap_y;

    Rect cell(int index, float height) const {
        int col = cols > 0 ? index % cols : 0;
        int row = cols > 0 ? index / cols : index;
        float cw = cols > 0 ? (bounds.w - gap_x * (float)(cols - 1)) / (float)cols : bounds.w;
        return {
            bounds.x + (float)col * (cw + gap_x),
            bounds.y + (float)row * (height + gap_y),
            cw,
            height
        };
    }
};

inline rui_color_t rgba(float r, float g, float b, float a) {
    return rui_rgba(r, g, b, a);
}

inline rui_color_t with_alpha(rui_color_t c, float a) {
    c.a = a;
    return c;
}

inline rui_color_t shade(rui_color_t c, float m, float a) {
    return rui_rgba(c.r * m, c.g * m, c.b * m, a);
}

inline Theme default_theme() {
    Theme t;
    t.text = rui_rgb8(246, 250, 255);
    t.text_muted = rui_rgb8(198, 216, 238);
    t.text_dim = rui_rgb8(150, 168, 190);
    t.panel_top = rui_rgba(0.74f, 0.82f, 0.90f, 0.16f);
    t.panel_bottom = rui_rgba(0.035f, 0.045f, 0.060f, 0.70f);
    t.panel_border = rui_rgba(1.0f, 1.0f, 1.0f, 0.08f);
    t.control = rui_rgba(1.0f, 1.0f, 1.0f, 0.055f);
    t.control_hover = rui_rgba(0.44f, 0.76f, 1.0f, 0.22f);
    t.shadow = rui_rgba(0.0f, 0.0f, 0.0f, 0.26f);
    t.accent = rui_rgb8(82, 184, 255);
    t.panel_radius = 20.0f;
    t.control_radius = 12.0f;
    return t;
}

template <typename Painter>
inline void surface(Painter &p, Rect r, const Theme &theme, float active = 0.0f) {
    p.round(r.x + 12.0f, r.y + 18.0f, r.w, r.h, theme.panel_radius + 4.0f,
            rui_rgba(0.0f, 0.0f, 0.0f, 0.22f + active * 0.04f));
    p.round_gradient(r.x, r.y, r.w, r.h, theme.panel_radius,
                     rui_rgba(theme.panel_top.r, theme.panel_top.g, theme.panel_top.b,
                              theme.panel_top.a + active * 0.05f),
                     rui_rgba(theme.panel_bottom.r, theme.panel_bottom.g, theme.panel_bottom.b,
                              theme.panel_bottom.a + active * 0.06f));
    p.round(r.x + 12.0f, r.y + 12.0f, r.w - 24.0f, 1.0f, 0.5f,
            rui_rgba(1.0f, 1.0f, 1.0f, 0.095f + active * 0.03f));
    p.round(r.x + 14.0f, r.y + r.h - 8.0f, r.w - 28.0f, 1.0f, 0.5f,
            rui_rgba(0.0f, 0.0f, 0.0f, 0.14f));
}

template <typename Painter>
inline void chip(Painter &p, Rect r, const char *label, const Theme &theme,
                 bool active = false, rui_color_t accent = rui_rgb8(82, 184, 255)) {
    p.round_gradient(r.x, r.y, r.w, r.h, theme.control_radius,
                     active ? rui_rgba(accent.r, accent.g, accent.b, 0.46f)
                            : theme.control,
                     active ? rui_rgba(accent.r * 0.26f, accent.g * 0.26f, accent.b * 0.26f, 0.64f)
                            : rui_rgba(1.0f, 1.0f, 1.0f, 0.035f));
    p.text(r.x + 15.0f, r.y + (r.h - 18.0f) * 0.5f, label, 1.00f,
           active ? theme.text : theme.text_muted);
}

template <typename Painter>
inline void icon_button(Painter &p, Rect r, const char *label, const Theme &theme,
                        bool active = false) {
    p.round(r.x, r.y, r.w, r.h, theme.control_radius,
            active ? theme.control_hover : theme.control);
    p.text(r.x + r.w * 0.5f - 8.0f, r.y + r.h * 0.5f - 9.0f, label, 1.05f,
           theme.text);
}

template <typename Painter>
inline void slider(Painter &p, Rect r, float value, rui_color_t accent) {
    if (value < 0.0f) value = 0.0f;
    if (value > 1.0f) value = 1.0f;
    p.round(r.x, r.y + r.h * 0.5f - 2.0f, r.w, 4.0f, 2.0f,
            rui_rgba(1.0f, 1.0f, 1.0f, 0.18f));
    p.round(r.x, r.y + r.h * 0.5f - 2.0f, r.w * value, 4.0f, 2.0f,
            rui_rgba(accent.r, accent.g, accent.b, 0.76f));
    p.round(r.x + r.w * value - 6.0f, r.y + r.h * 0.5f - 7.0f,
            14.0f, 14.0f, 7.0f, rui_rgb8(245, 250, 255));
}

template <typename Painter>
inline void toggle(Painter &p, Rect r, const char *label, bool on,
                   const Theme &theme, rui_color_t accent) {
    p.text(r.x, r.y + 2.0f, label, 1.10f, theme.text_muted);
    p.round(r.x + r.w - 48.0f, r.y, 48.0f, 20.0f, 10.0f,
            on ? rui_rgba(accent.r, accent.g, accent.b, 0.45f)
               : rui_rgba(1.0f, 1.0f, 1.0f, 0.12f));
    p.round(r.x + r.w - 46.0f + (on ? 26.0f : 0.0f), r.y + 3.0f,
            16.0f, 14.0f, 7.0f,
            on ? rui_rgb8(245, 250, 255) : rui_rgb8(140, 152, 168));
}

template <typename Painter>
inline void titlebar(Painter &p, Rect r, const char *title, bool active,
                     const Theme &theme) {
    p.round(r.x + 16.0f, r.y + 18.0f, 12.0f, 12.0f, 6.0f,
            rui_rgba(1.0f, 0.36f, 0.36f, 0.92f));
    p.round(r.x + 38.0f, r.y + 18.0f, 12.0f, 12.0f, 6.0f,
            rui_rgba(1.0f, 0.76f, 0.24f, 0.92f));
    p.round(r.x + 60.0f, r.y + 18.0f, 12.0f, 12.0f, 6.0f,
            rui_rgba(0.34f, 0.84f, 0.44f, 0.92f));
    p.text(r.x + 92.0f, r.y + 15.0f, title, 1.10f,
           active ? theme.text : theme.text_muted);
}

} // namespace riduxui

#endif
