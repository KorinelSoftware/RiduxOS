#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>
#include <wayland-client.h>
#include "xdg-shell-client-protocol.h"

#ifndef MFD_CLOEXEC
#define MFD_CLOEXEC 0x0001U
#endif

struct ridux_shell {
    struct wl_display *display;
    struct wl_registry *registry;
    struct wl_compositor *compositor;
    struct wl_shm *shm;
    struct xdg_wm_base *wm_base;
    struct wl_surface *surface;
    struct xdg_surface *xdg_surface;
    struct xdg_toplevel *toplevel;
    struct wl_buffer *buffer;
    void *pixels;
    size_t pixels_size;
    int width;
    int height;
    int running;
    int configured;
};

static int create_tmp_file(size_t size) {
    int fd = -1;
#ifdef SYS_memfd_create
    fd = (int)syscall(SYS_memfd_create, "ridux-visible-shell", MFD_CLOEXEC);
#endif
    if (fd < 0) {
        char path[] = "/tmp/ridux-visible-shell-XXXXXX";
        fd = mkstemp(path);
        if (fd >= 0) unlink(path);
    }
    if (fd < 0) return -1;
    if (ftruncate(fd, (off_t)size) < 0) {
        close(fd);
        return -1;
    }
    return fd;
}

static uint32_t rgb(unsigned r, unsigned g, unsigned b) {
    return 0xff000000u | ((r & 255u) << 16) | ((g & 255u) << 8) | (b & 255u);
}

static uint32_t mix(uint32_t a, uint32_t b, int t, int max) {
    unsigned ar = (a >> 16) & 255u;
    unsigned ag = (a >> 8) & 255u;
    unsigned ab = a & 255u;
    unsigned br = (b >> 16) & 255u;
    unsigned bg = (b >> 8) & 255u;
    unsigned bb = b & 255u;
    if (max <= 0) max = 1;
    if (t < 0) t = 0;
    if (t > max) t = max;
    return rgb((ar * (max - t) + br * t) / max,
               (ag * (max - t) + bg * t) / max,
               (ab * (max - t) + bb * t) / max);
}

static void fill_rect(uint32_t *p, int w, int h, int x, int y, int rw, int rh, uint32_t c) {
    int yy;
    if (!p || w <= 0 || h <= 0 || rw <= 0 || rh <= 0) return;
    if (x < 0) { rw += x; x = 0; }
    if (y < 0) { rh += y; y = 0; }
    if (x + rw > w) rw = w - x;
    if (y + rh > h) rh = h - y;
    if (rw <= 0 || rh <= 0) return;
    for (yy = y; yy < y + rh; ++yy) {
        int xx;
        uint32_t *row = p + yy * w;
        for (xx = x; xx < x + rw; ++xx) row[xx] = c;
    }
}

static void rect_border(uint32_t *p, int w, int h, int x, int y, int rw, int rh, uint32_t c) {
    fill_rect(p, w, h, x, y, rw, 2, c);
    fill_rect(p, w, h, x, y + rh - 2, rw, 2, c);
    fill_rect(p, w, h, x, y, 2, rh, c);
    fill_rect(p, w, h, x + rw - 2, y, 2, rh, c);
}

static void rounded_box(uint32_t *p, int w, int h, int x, int y, int rw, int rh, uint32_t body, uint32_t border) {
    fill_rect(p, w, h, x + 10, y, rw - 20, rh, body);
    fill_rect(p, w, h, x, y + 10, rw, rh - 20, body);
    fill_rect(p, w, h, x + 4, y + 4, rw - 8, rh - 8, body);
    rect_border(p, w, h, x + 8, y, rw - 16, rh, border);
    rect_border(p, w, h, x, y + 8, rw, rh - 16, border);
}

static const uint8_t *glyph_rows(char c) {
    static const uint8_t blank[7] = {0,0,0,0,0,0,0};
    static const uint8_t A[7] = {14,17,17,31,17,17,17};
    static const uint8_t B[7] = {30,17,17,30,17,17,30};
    static const uint8_t C[7] = {14,17,16,16,16,17,14};
    static const uint8_t D[7] = {30,17,17,17,17,17,30};
    static const uint8_t E[7] = {31,16,16,30,16,16,31};
    static const uint8_t F[7] = {31,16,16,30,16,16,16};
    static const uint8_t G[7] = {14,17,16,23,17,17,14};
    static const uint8_t H[7] = {17,17,17,31,17,17,17};
    static const uint8_t I[7] = {14,4,4,4,4,4,14};
    static const uint8_t J[7] = {7,2,2,2,18,18,12};
    static const uint8_t K[7] = {17,18,20,24,20,18,17};
    static const uint8_t L[7] = {16,16,16,16,16,16,31};
    static const uint8_t M[7] = {17,27,21,21,17,17,17};
    static const uint8_t N[7] = {17,25,21,19,17,17,17};
    static const uint8_t O[7] = {14,17,17,17,17,17,14};
    static const uint8_t P[7] = {30,17,17,30,16,16,16};
    static const uint8_t Q[7] = {14,17,17,17,21,18,13};
    static const uint8_t R[7] = {30,17,17,30,20,18,17};
    static const uint8_t S[7] = {15,16,16,14,1,1,30};
    static const uint8_t T[7] = {31,4,4,4,4,4,4};
    static const uint8_t U[7] = {17,17,17,17,17,17,14};
    static const uint8_t V[7] = {17,17,17,17,17,10,4};
    static const uint8_t W[7] = {17,17,17,21,21,21,10};
    static const uint8_t X[7] = {17,17,10,4,10,17,17};
    static const uint8_t Y[7] = {17,17,10,4,4,4,4};
    static const uint8_t Z[7] = {31,1,2,4,8,16,31};
    static const uint8_t N0[7] = {14,17,19,21,25,17,14};
    static const uint8_t N1[7] = {4,12,4,4,4,4,14};
    static const uint8_t N2[7] = {14,17,1,2,4,8,31};
    static const uint8_t N3[7] = {30,1,1,14,1,1,30};
    static const uint8_t N4[7] = {2,6,10,18,31,2,2};
    static const uint8_t N5[7] = {31,16,30,1,1,17,14};
    static const uint8_t N6[7] = {6,8,16,30,17,17,14};
    static const uint8_t N7[7] = {31,1,2,4,8,8,8};
    static const uint8_t N8[7] = {14,17,17,14,17,17,14};
    static const uint8_t N9[7] = {14,17,17,15,1,2,12};
    static const uint8_t dash[7] = {0,0,0,31,0,0,0};
    static const uint8_t colon[7] = {0,4,4,0,4,4,0};
    static const uint8_t dot[7] = {0,0,0,0,0,12,12};
    if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
    switch (c) {
        case 'A': return A; case 'B': return B; case 'C': return C; case 'D': return D;
        case 'E': return E; case 'F': return F; case 'G': return G; case 'H': return H;
        case 'I': return I; case 'J': return J; case 'K': return K; case 'L': return L;
        case 'M': return M; case 'N': return N; case 'O': return O; case 'P': return P;
        case 'Q': return Q; case 'R': return R; case 'S': return S; case 'T': return T;
        case 'U': return U; case 'V': return V; case 'W': return W; case 'X': return X;
        case 'Y': return Y; case 'Z': return Z; case '0': return N0; case '1': return N1;
        case '2': return N2; case '3': return N3; case '4': return N4; case '5': return N5;
        case '6': return N6; case '7': return N7; case '8': return N8; case '9': return N9;
        case '-': return dash; case ':': return colon; case '.': return dot;
        default: return blank;
    }
}

static void draw_text(uint32_t *p, int w, int h, int x, int y, const char *s, int scale, uint32_t c) {
    int cx = x;
    if (!s || scale <= 0) return;
    while (*s) {
        const uint8_t *g = glyph_rows(*s++);
        int row;
        for (row = 0; row < 7; ++row) {
            int col;
            for (col = 0; col < 5; ++col) {
                if (g[row] & (1u << (4 - col))) {
                    fill_rect(p, w, h, cx + col * scale, y + row * scale, scale, scale, c);
                }
            }
        }
        cx += 6 * scale;
    }
}

static void draw_shell(struct ridux_shell *s) {
    uint32_t *p;
    int x, y;
    int w = s->width > 0 ? s->width : 1024;
    int h = s->height > 0 ? s->height : 768;
    int win_w = w > 900 ? 360 : (w - 96) / 2;
    int win_h = h > 620 ? 230 : 180;
    int left_x = (w - (win_w * 2 + 32)) / 2;
    int right_x = left_x + win_w + 32;
    int win_y = h > 620 ? 154 : 104;
    int dock_w = w > 720 ? 520 : w - 80;
    int dock_x = (w - dock_w) / 2;
    int dock_y = h - 92;
    int i;
    if (!s->pixels) return;
    p = (uint32_t *)s->pixels;
    for (y = 0; y < h; ++y) {
        uint32_t left = mix(rgb(86, 91, 99), rgb(45, 48, 53), y, h);
        uint32_t right = mix(rgb(108, 112, 118), rgb(55, 58, 64), y, h);
        for (x = 0; x < w; ++x) p[y * w + x] = mix(left, right, x, w);
    }
    rounded_box(p, w, h, 18, 14, w - 36, 46, rgb(48, 51, 56), rgb(126, 132, 140));
    draw_text(p, w, h, 42, 28, "RIDUXOS", 2, rgb(246, 247, 248));
    draw_text(p, w, h, w - 244, 28, "WAYFIRE VIRGL", 2, rgb(189, 238, 202));
    rounded_box(p, w, h, left_x, win_y, win_w, win_h, rgb(238, 239, 241), rgb(92, 96, 104));
    fill_rect(p, w, h, left_x + 2, win_y + 2, win_w - 4, 34, rgb(68, 72, 79));
    draw_text(p, w, h, left_x + 18, win_y + 14, "FILES", 2, rgb(255, 255, 255));
    for (i = 0; i < 6; ++i) {
        int tx = left_x + 24 + (i % 3) * 104;
        int ty = win_y + 62 + (i / 3) * 72;
        rounded_box(p, w, h, tx, ty, 72, 48, rgb(211, 214, 219), rgb(130, 136, 145));
    }
    rounded_box(p, w, h, right_x, win_y + 36, win_w, win_h, rgb(246, 247, 248), rgb(92, 96, 104));
    fill_rect(p, w, h, right_x + 2, win_y + 38, win_w - 4, 34, rgb(54, 57, 63));
    draw_text(p, w, h, right_x + 18, win_y + 50, "MONITOR", 2, rgb(255, 255, 255));
    draw_text(p, w, h, right_x + 24, win_y + 100, "GPU 3D", 2, rgb(36, 40, 45));
    draw_text(p, w, h, right_x + 24, win_y + 136, "CURSOR OK", 2, rgb(36, 40, 45));
    draw_text(p, w, h, right_x + 24, win_y + 172, "QT APPS", 2, rgb(36, 40, 45));
    rounded_box(p, w, h, dock_x, dock_y, dock_w, 68, rgb(58, 61, 67), rgb(141, 146, 153));
    for (i = 0; i < 6; ++i) {
        int ix = dock_x + 28 + i * ((dock_w - 56) / 6);
        rounded_box(p, w, h, ix, dock_y + 12, 54, 44,
                    mix(rgb(92, 98, 108), rgb(174, 181, 190), i, 6),
                    rgb(220, 223, 227));
    }
    draw_text(p, w, h, dock_x + 44, dock_y + 28, "R", 2, rgb(255,255,255));
    draw_text(p, w, h, dock_x + 44 + ((dock_w - 56) / 6), dock_y + 28, "F", 2, rgb(255,255,255));
    draw_text(p, w, h, dock_x + 44 + 2 * ((dock_w - 56) / 6), dock_y + 28, "W", 2, rgb(255,255,255));
    draw_text(p, w, h, dock_x + 44 + 3 * ((dock_w - 56) / 6), dock_y + 28, "S", 2, rgb(255,255,255));
    draw_text(p, w, h, dock_x + 44 + 4 * ((dock_w - 56) / 6), dock_y + 28, "T", 2, rgb(255,255,255));
    draw_text(p, w, h, dock_x + 44 + 5 * ((dock_w - 56) / 6), dock_y + 28, "L", 2, rgb(255,255,255));
}

static int ensure_buffer(struct ridux_shell *s, int width, int height) {
    int fd;
    int stride;
    size_t size;
    struct wl_shm_pool *pool;
    if (width <= 0) width = 1024;
    if (height <= 0) height = 768;
    if (s->buffer && s->width == width && s->height == height) return 0;
    if (s->buffer) wl_buffer_destroy(s->buffer);
    if (s->pixels) munmap(s->pixels, s->pixels_size);
    s->buffer = NULL;
    s->pixels = NULL;
    s->pixels_size = 0;
    s->width = width;
    s->height = height;
    stride = width * 4;
    size = (size_t)stride * (size_t)height;
    fd = create_tmp_file(size);
    if (fd < 0) return -1;
    s->pixels = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (s->pixels == MAP_FAILED) {
        s->pixels = NULL;
        close(fd);
        return -1;
    }
    pool = wl_shm_create_pool(s->shm, fd, (int)size);
    s->buffer = wl_shm_pool_create_buffer(pool, 0, width, height, stride, WL_SHM_FORMAT_XRGB8888);
    wl_shm_pool_destroy(pool);
    close(fd);
    s->pixels_size = size;
    return s->buffer ? 0 : -1;
}

static void repaint(struct ridux_shell *s) {
    if (ensure_buffer(s, s->width, s->height) < 0) {
        fprintf(stderr, "ridux-visible-shell: buffer failed: %s\n", strerror(errno));
        return;
    }
    draw_shell(s);
    wl_surface_attach(s->surface, s->buffer, 0, 0);
    wl_surface_damage_buffer(s->surface, 0, 0, s->width, s->height);
    wl_surface_commit(s->surface);
    wl_display_flush(s->display);
    fprintf(stderr, "ridux-visible-shell: painted %dx%d\n", s->width, s->height);
}

static void wm_base_ping(void *data, struct xdg_wm_base *wm_base, uint32_t serial) {
    (void)data;
    xdg_wm_base_pong(wm_base, serial);
}

static const struct xdg_wm_base_listener wm_base_listener = {
    wm_base_ping
};

static void xdg_surface_configure(void *data, struct xdg_surface *surface, uint32_t serial) {
    struct ridux_shell *s = data;
    xdg_surface_ack_configure(surface, serial);
    s->configured = 1;
    repaint(s);
}

static const struct xdg_surface_listener xdg_surface_listener = {
    xdg_surface_configure
};

static void toplevel_configure(void *data, struct xdg_toplevel *toplevel, int32_t width, int32_t height, struct wl_array *states) {
    struct ridux_shell *s = data;
    (void)toplevel;
    (void)states;
    if (width > 0) s->width = width;
    if (height > 0) s->height = height;
}

static void toplevel_close(void *data, struct xdg_toplevel *toplevel) {
    struct ridux_shell *s = data;
    (void)toplevel;
    s->running = 0;
}

static const struct xdg_toplevel_listener toplevel_listener = {
    toplevel_configure,
    toplevel_close
};

static void registry_global(void *data, struct wl_registry *registry, uint32_t name, const char *interface, uint32_t version) {
    struct ridux_shell *s = data;
    if (strcmp(interface, wl_compositor_interface.name) == 0) {
        s->compositor = wl_registry_bind(registry, name, &wl_compositor_interface, version < 4 ? version : 4);
    } else if (strcmp(interface, wl_shm_interface.name) == 0) {
        s->shm = wl_registry_bind(registry, name, &wl_shm_interface, 1);
    } else if (strcmp(interface, xdg_wm_base_interface.name) == 0) {
        s->wm_base = wl_registry_bind(registry, name, &xdg_wm_base_interface, 1);
        xdg_wm_base_add_listener(s->wm_base, &wm_base_listener, s);
    }
}

static void registry_remove(void *data, struct wl_registry *registry, uint32_t name) {
    (void)data;
    (void)registry;
    (void)name;
}

static const struct wl_registry_listener registry_listener = {
    registry_global,
    registry_remove
};

int main(void) {
    struct ridux_shell s;
    memset(&s, 0, sizeof(s));
    s.width = 1024;
    s.height = 768;
    s.running = 1;
    s.display = wl_display_connect(NULL);
    if (!s.display) {
        fprintf(stderr, "ridux-visible-shell: cannot connect to Wayland display\n");
        return 1;
    }
    s.registry = wl_display_get_registry(s.display);
    wl_registry_add_listener(s.registry, &registry_listener, &s);
    wl_display_roundtrip(s.display);
    if (!s.compositor || !s.shm || !s.wm_base) {
        fprintf(stderr, "ridux-visible-shell: missing compositor=%p shm=%p wm_base=%p\n", (void*)s.compositor, (void*)s.shm, (void*)s.wm_base);
        return 2;
    }
    s.surface = wl_compositor_create_surface(s.compositor);
    s.xdg_surface = xdg_wm_base_get_xdg_surface(s.wm_base, s.surface);
    xdg_surface_add_listener(s.xdg_surface, &xdg_surface_listener, &s);
    s.toplevel = xdg_surface_get_toplevel(s.xdg_surface);
    xdg_toplevel_add_listener(s.toplevel, &toplevel_listener, &s);
    xdg_toplevel_set_title(s.toplevel, "RiduxOS Wayfire Desktop");
    xdg_toplevel_set_app_id(s.toplevel, "ridux-visible-shell");
    xdg_toplevel_set_maximized(s.toplevel);
    wl_surface_commit(s.surface);
    fprintf(stderr, "ridux-visible-shell: connected\n");
    while (s.running && wl_display_dispatch(s.display) >= 0) {}
    if (s.buffer) wl_buffer_destroy(s.buffer);
    if (s.pixels) munmap(s.pixels, s.pixels_size);
    if (s.toplevel) xdg_toplevel_destroy(s.toplevel);
    if (s.xdg_surface) xdg_surface_destroy(s.xdg_surface);
    if (s.surface) wl_surface_destroy(s.surface);
    if (s.wm_base) xdg_wm_base_destroy(s.wm_base);
    if (s.shm) wl_shm_destroy(s.shm);
    if (s.compositor) wl_compositor_destroy(s.compositor);
    if (s.registry) wl_registry_destroy(s.registry);
    wl_display_disconnect(s.display);
    return 0;
}
