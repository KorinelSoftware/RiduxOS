#ifndef RIDUX_FLUSH_H
#define RIDUX_FLUSH_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "assets.h"

typedef struct {
    int x;
    int y;
    int w;
    int h;
} flush_rect_t;

typedef struct {
    uint8_t r;
    uint8_t g;
    uint8_t b;
    uint8_t a;
} flush_color_t;

int flush_init(int width, int height);
void flush_shutdown(void);
int flush_resize(int width, int height);

int flush_width(void);
int flush_height(void);
const uint32_t *flush_pixels(void);
uint32_t *flush_pixels_mut(void);

void flush_reset(void);
void flush_clear(flush_color_t color);
void flush_rect(int x, int y, int w, int h, flush_color_t color);
void flush_round_rect(int x, int y, int w, int h, int radius, flush_color_t color);
void flush_stroke_round(int x, int y, int w, int h, int radius, int thickness, flush_color_t color);
void flush_circle(int cx, int cy, int radius, flush_color_t color);
void flush_vgradient(int x, int y, int w, int h, flush_color_t top, flush_color_t bottom);
void flush_shadow(int x, int y, int w, int h, int radius, int spread, flush_color_t color);
void flush_noise(int x, int y, int w, int h, uint32_t seed, uint8_t alpha);
void flush_image(int x, int y, int w, int h, const ridux_image_t *img, uint8_t alpha);
void flush_text(int x, int y, int scale, flush_color_t color, const char *text);
int flush_text_width(const char *text, int scale);

void flush_execute(void);

#endif
