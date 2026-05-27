#ifndef RIDUX_NATIVE_UI_H
#define RIDUX_NATIVE_UI_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct rui_color {
    float r;
    float g;
    float b;
    float a;
} rui_color_t;

typedef struct rui_rect {
    float x;
    float y;
    float w;
    float h;
} rui_rect_t;

typedef struct rui_context rui_context_t;

typedef enum rui_backend {
    RUI_BACKEND_DRM = 1,
    RUI_BACKEND_WAYLAND = 2
} rui_backend_t;

typedef struct rui_options {
    rui_backend_t backend;
    bool immediate_present;
    bool require_hardware;
} rui_options_t;

typedef struct rui_frame_info {
    uint32_t width;
    uint32_t height;
    uint32_t frame;
    const char *renderer;
    uint32_t present_swap_ms;
    uint32_t present_lock_ms;
    uint32_t present_addfb_ms;
    uint32_t present_flip_ms;
    uint32_t present_release_ms;
} rui_frame_info_t;

rui_color_t rui_rgba(float r, float g, float b, float a);
rui_color_t rui_rgb8(uint8_t r, uint8_t g, uint8_t b);

bool rui_open(rui_context_t **out, const rui_options_t *options);
void rui_close(rui_context_t *ctx);
const rui_frame_info_t *rui_frame_info(const rui_context_t *ctx);

void rui_begin(rui_context_t *ctx, rui_color_t bg);
void rui_rect(rui_context_t *ctx, rui_rect_t rect, rui_color_t color);
void rui_round_rect(rui_context_t *ctx, rui_rect_t rect, float radius,
                    rui_color_t color);
void rui_round_rect_gradient(rui_context_t *ctx, rui_rect_t rect, float radius,
                             rui_color_t top, rui_color_t bottom);
void rui_text(rui_context_t *ctx, float x, float y, const char *text,
              float scale, rui_color_t color);
bool rui_text_cached(rui_context_t *ctx, float x, float y, const char *text,
                     float scale, rui_color_t color);
bool rui_text_preload(rui_context_t *ctx, const char *text, float scale);
bool rui_image_argb(rui_context_t *ctx, rui_rect_t rect,
                    uint32_t width, uint32_t height,
                    const uint32_t *pixels_argb);
bool rui_image_argb_cached(rui_context_t *ctx, rui_rect_t rect,
                           uint32_t width, uint32_t height,
                           const uint32_t *pixels_argb);
bool rui_image_preload_argb(rui_context_t *ctx, uint32_t width, uint32_t height,
                            const uint32_t *pixels_argb);
bool rui_present(rui_context_t *ctx);

bool rui_cursor_enable_adwaita(rui_context_t *ctx, int32_t x, int32_t y);
bool rui_cursor_move(rui_context_t *ctx, int32_t x, int32_t y);
bool rui_cursor_hardware_active(const rui_context_t *ctx);
bool rui_cursor_draw_adwaita(rui_context_t *ctx, int32_t x, int32_t y);
bool rui_cursor_disable(rui_context_t *ctx);

#ifdef __cplusplus
}
#endif

#endif
