#ifndef INJURY_SHELL_HPP
#define INJURY_SHELL_HPP

#include "ridux_surface_protocol.h"

#include <stdint.h>

namespace injury {

enum SurfaceId {
    SURFACE_TASKBAR = 1,
    SURFACE_LAUNCHER = 2,
    SURFACE_QUICK_PANEL = 3,
    SURFACE_SEARCH = 4,
    SURFACE_FIRST_APP = 32
};

enum class SurfaceRole : uint32_t {
    Toplevel = RIDUX_ROLE_TOPLEVEL,
    Taskbar = RIDUX_ROLE_TASKBAR,
    Panel = RIDUX_ROLE_PANEL,
    Search = RIDUX_ROLE_SEARCH,
    Popup = RIDUX_ROLE_POPUP,
    Cursor = RIDUX_ROLE_CURSOR,
    WaylandBridgeSurface = RIDUX_ROLE_WAYLAND_BRIDGE_SURFACE
};

struct Rect {
    float x;
    float y;
    float w;
    float h;
};

struct SurfaceEffects {
    float radius = 18.0f;
    float blur = 22.0f;
    float shadow = 0.18f;
    float acrylic = 0.72f;
};

struct Surface {
    uint32_t id = 0;
    SurfaceRole role = SurfaceRole::Toplevel;
    int app = -1;
    const char *name = "";
    Rect rect = {0.0f, 0.0f, 0.0f, 0.0f};
    SurfaceEffects effects;
    bool visible = false;
    bool interactive = true;
};

struct ResourceStager {
    uint32_t generation = 0;
    uint32_t open_frame = 0;

    void reset(uint32_t gen) {
        generation = gen;
        open_frame = 0;
    }

    void tick(bool active) {
        if (active) {
            if (open_frame < 1000000u) ++open_frame;
        } else {
            open_frame = 0;
        }
    }

    int visible_items(int total) const {
        int cap = 4;
        if (open_frame > 10u) cap = 8;
        if (open_frame > 24u) cap = 12;
        if (open_frame > 48u) cap = total;
        return cap < total ? cap : total;
    }
};

static inline const char *role_name(SurfaceRole role) {
    switch (role) {
    case SurfaceRole::Toplevel: return "toplevel";
    case SurfaceRole::Taskbar: return "taskbar";
    case SurfaceRole::Panel: return "panel";
    case SurfaceRole::Search: return "search";
    case SurfaceRole::Popup: return "popup";
    case SurfaceRole::Cursor: return "cursor";
    case SurfaceRole::WaylandBridgeSurface: return "wayland-bridge-surface";
    }
    return "unknown";
}

static inline ridux_protocol_surface_effects_t effects_message(
    uint32_t object_id, uint32_t serial, const SurfaceEffects &effects) {
    ridux_protocol_surface_effects_t msg = {};
    msg.header = ridux_protocol_header_make(
        RIDUX_OP_SET_EFFECTS, object_id, serial, sizeof(msg));
    msg.flags = INJURY_CAP_COMPOSITOR_EFFECTS |
                INJURY_CAP_ACRYLIC_BLUR |
                INJURY_CAP_ROUNDED_CORNERS;
    msg.corner_radius = effects.radius;
    msg.shadow_radius = 32.0f;
    msg.shadow_opacity = effects.shadow;
    msg.blur_radius = effects.blur;
    msg.acrylic_opacity = effects.acrylic;
    return msg;
}

} // namespace injury

#endif
