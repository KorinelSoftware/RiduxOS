#ifndef RIDUX_SURFACE_PROTOCOL_H
#define RIDUX_SURFACE_PROTOCOL_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RIDUX_SURFACE_PROTOCOL_MAGIC 0x52535031u
#define RIDUX_SURFACE_PROTOCOL_VERSION 1u

#define INJURY_PROTOCOL_MAGIC RIDUX_SURFACE_PROTOCOL_MAGIC
#define INJURY_PROTOCOL_VERSION RIDUX_SURFACE_PROTOCOL_VERSION

#define INJURY_CAP_COMPOSITOR_EFFECTS (1u << 0)
#define INJURY_CAP_ACRYLIC_BLUR       (1u << 1)
#define INJURY_CAP_ROUNDED_CORNERS    (1u << 2)
#define INJURY_CAP_DAMAGE_TRACKING    (1u << 3)
#define INJURY_CAP_DMABUF_IMPORT      (1u << 4)
#define INJURY_CAP_WAYLAND_BRIDGE     (1u << 5)
#define INJURY_CAP_XWAYLAND_BRIDGE    (1u << 6)

typedef uint32_t ridux_object_id_t;

typedef enum ridux_protocol_opcode {
    RIDUX_OP_HELLO = 1,
    RIDUX_OP_CREATE_SURFACE = 2,
    RIDUX_OP_DESTROY_SURFACE = 3,
    RIDUX_OP_ATTACH_BUFFER = 4,
    RIDUX_OP_DAMAGE = 5,
    RIDUX_OP_COMMIT = 6,
    RIDUX_OP_SET_TITLE = 7,
    RIDUX_OP_SET_ROLE = 8,
    RIDUX_OP_SET_LAYER = 9,
    RIDUX_OP_SET_EFFECTS = 10,
    RIDUX_OP_POINTER_EVENT = 32,
    RIDUX_OP_KEY_EVENT = 33,
    RIDUX_OP_FRAME_DONE = 64,
    RIDUX_OP_CONFIGURE = 65
} ridux_protocol_opcode_t;

typedef enum ridux_surface_role {
    RIDUX_ROLE_NONE = 0,
    RIDUX_ROLE_TOPLEVEL = 1,
    RIDUX_ROLE_PANEL = 2,
    RIDUX_ROLE_DOCK = 3,
    RIDUX_ROLE_POPUP = 4,
    RIDUX_ROLE_CURSOR = 5,
    RIDUX_ROLE_TASKBAR = 6,
    RIDUX_ROLE_SEARCH = 7,
    RIDUX_ROLE_WAYLAND_BRIDGE_SURFACE = 8
} ridux_surface_role_t;

#define INJURY_ROLE_NONE RIDUX_ROLE_NONE
#define INJURY_ROLE_TOPLEVEL RIDUX_ROLE_TOPLEVEL
#define INJURY_ROLE_PANEL RIDUX_ROLE_PANEL
#define INJURY_ROLE_TASKBAR RIDUX_ROLE_TASKBAR
#define INJURY_ROLE_SEARCH RIDUX_ROLE_SEARCH
#define INJURY_ROLE_POPUP RIDUX_ROLE_POPUP
#define INJURY_ROLE_CURSOR RIDUX_ROLE_CURSOR
#define INJURY_ROLE_WAYLAND_BRIDGE_SURFACE RIDUX_ROLE_WAYLAND_BRIDGE_SURFACE

typedef enum ridux_buffer_kind {
    RIDUX_BUFFER_NONE = 0,
    RIDUX_BUFFER_GBM_HANDLE = 1,
    RIDUX_BUFFER_DMA_BUF = 2,
    RIDUX_BUFFER_SHM = 3
} ridux_buffer_kind_t;

#pragma pack(push, 1)

typedef struct ridux_protocol_header {
    uint32_t magic;
    uint16_t version;
    uint16_t opcode;
    uint32_t object_id;
    uint32_t serial;
    uint32_t size;
} ridux_protocol_header_t;

typedef struct ridux_protocol_hello {
    ridux_protocol_header_t header;
    uint32_t compositor_caps;
    uint32_t preferred_refresh_hz;
} ridux_protocol_hello_t;

typedef struct ridux_protocol_create_surface {
    ridux_protocol_header_t header;
    ridux_object_id_t new_id;
    uint32_t role;
} ridux_protocol_create_surface_t;

typedef struct ridux_protocol_attach_buffer {
    ridux_protocol_header_t header;
    ridux_object_id_t buffer_id;
    uint32_t kind;
    uint32_t width;
    uint32_t height;
    uint32_t stride;
    uint32_t format;
    uint64_t modifier;
    int32_t x;
    int32_t y;
} ridux_protocol_attach_buffer_t;

typedef struct ridux_protocol_damage {
    ridux_protocol_header_t header;
    int32_t x;
    int32_t y;
    int32_t width;
    int32_t height;
} ridux_protocol_damage_t;

typedef struct ridux_protocol_configure {
    ridux_protocol_header_t header;
    uint32_t width;
    uint32_t height;
    uint32_t refresh_hz;
    uint32_t scale;
} ridux_protocol_configure_t;

typedef struct ridux_protocol_surface_effects {
    ridux_protocol_header_t header;
    uint32_t flags;
    float corner_radius;
    float shadow_radius;
    float shadow_opacity;
    float blur_radius;
    float acrylic_opacity;
} ridux_protocol_surface_effects_t;

typedef struct ridux_protocol_pointer_event {
    ridux_protocol_header_t header;
    int32_t x;
    int32_t y;
    uint32_t buttons;
    int32_t wheel_delta;
} ridux_protocol_pointer_event_t;

#pragma pack(pop)

static inline ridux_protocol_header_t ridux_protocol_header_make(
    ridux_protocol_opcode_t opcode, ridux_object_id_t object_id,
    uint32_t serial, uint32_t size) {
    ridux_protocol_header_t h;
    h.magic = RIDUX_SURFACE_PROTOCOL_MAGIC;
    h.version = RIDUX_SURFACE_PROTOCOL_VERSION;
    h.opcode = (uint16_t)opcode;
    h.object_id = object_id;
    h.serial = serial;
    h.size = size;
    return h;
}

#ifdef __cplusplus
}
#endif

#endif
