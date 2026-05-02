#ifndef RIDUX_FLUSH_H
#define RIDUX_FLUSH_H

#include <stddef.h>
#include <stdint.h>

#include "assets.h"

uint32_t rgb(uint8_t r, uint8_t g, uint8_t b);
uint32_t rgb_hex(uint32_t hex);
uint32_t blend_color(uint32_t dst, uint32_t src, uint8_t alpha);

void draw_rect_alpha(int x, int y, int w, int h, uint32_t color, uint8_t alpha);
void draw_vgradient_alpha(int x, int y, int w, int h, uint32_t top, uint32_t bottom, uint8_t alpha);
void draw_image_scaled_alpha(int x, int y, int w, int h, const ridux_image_t *img, uint8_t alpha);
void draw_text(int x, int y, const char *text, uint32_t color, uint8_t alpha);
void draw_text_scaled(int x, int y, const char *text, int scale, uint32_t color, uint8_t alpha);
void fb_present(void);
void fb_present_invalidate(void);

/* Cursor fast-path: save the clean composited pixels under the
 * cursor before drawing the sprite, then restore them on the next
 * frame to erase the old cursor without re-running render_scene. */
void flush_cursor_save_under(int x, int y, int w, int h);
void flush_cursor_restore_under(void);
void flush_cursor_under_invalidate(void);

int measure_text(const char *text);
size_t flush_queue_count(void);

void flush_reset(void);
void flush_clear(uint32_t color);
void flush_rect(int x, int y, int w, int h, uint32_t color, uint8_t alpha);
void flush_round_rect(int x, int y, int w, int h, int r, uint32_t color, uint8_t alpha);
void flush_round_rect4(int x, int y, int w, int h,
                      int r_tl, int r_tr, int r_br, int r_bl,
                      uint32_t color, uint8_t alpha);
void flush_stroke_rect(int x, int y, int w, int h, int t, uint32_t color, uint8_t alpha);
void flush_stroke_round(int x, int y, int w, int h, int r, int t, uint32_t color, uint8_t alpha);
void flush_vgradient(int x, int y, int w, int h, uint32_t top, uint32_t bot, uint8_t alpha);
void flush_hgradient(int x, int y, int w, int h, uint32_t left, uint32_t right, uint8_t alpha);
void flush_radial(int cx, int cy, int r, uint32_t inner, uint32_t outer, uint8_t alpha);
void flush_circle(int cx, int cy, int r, uint32_t color, uint8_t alpha);
void flush_ring(int cx, int cy, int r, int thickness, uint32_t color, uint8_t alpha);
void flush_line(int x0, int y0, int x1, int y1, int t, uint32_t color, uint8_t alpha);
void flush_text(int x, int y, uint32_t color, const char *text);
void flush_text_alpha(int x, int y, uint32_t color, uint8_t alpha, const char *text);
void flush_text_scaled(int x, int y, int scale, uint32_t color, uint8_t alpha, const char *text);
void flush_image(int x, int y, int w, int h, const ridux_image_t *img, uint8_t alpha);
void flush_image_tint(int x, int y, int w, int h, const ridux_image_t *img,
                     uint32_t tint, uint8_t tint_alpha, uint8_t alpha);
void flush_blur(int x, int y, int w, int h, int radius, int passes);
void flush_glass(int x, int y, int w, int h, int radius,
                uint32_t tint, uint8_t tint_alpha,
                uint32_t stroke, uint8_t stroke_alpha,
                int blur_radius, int blur_passes);
void flush_glass_plain(int x, int y, int w, int h, int radius,
                       uint32_t tint, uint8_t tint_alpha,
                       uint32_t stroke, uint8_t stroke_alpha,
                       int blur_radius, int blur_passes);
void flush_shadow(int x, int y, int w, int h, int radius, int spread,
                 uint32_t color, uint8_t alpha);
void flush_noise(int x, int y, int w, int h, uint8_t alpha);
void flush_scissor_push(int x, int y, int w, int h);
void flush_scissor_pop(void);
void flush_execute_to_backbuffer(void);
void flush_execute(void);

#endif
