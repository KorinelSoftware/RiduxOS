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

#define RIDUX_BTN_LEFT 0x110

struct ridux_panel {
    struct wl_display *display;
    struct wl_registry *registry;
    struct wl_compositor *compositor;
    struct wl_shm *shm;
    struct wl_seat *seat;
    struct wl_pointer *pointer;
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
    int menu_open;
    int quick_open;
    int active_workspace;
    int pointer_x;
    int pointer_y;
};

static int create_tmp_file(size_t size) {
    int fd = -1;
#ifdef SYS_memfd_create
    fd = (int)syscall(SYS_memfd_create, "ridux-panel", MFD_CLOEXEC);
#endif
    if (fd < 0) {
        char path[] = "/tmp/ridux-panel-XXXXXX";
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

static uint32_t argb(unsigned a, unsigned r, unsigned g, unsigned b) {
    return ((a & 255u) << 24) | ((r & 255u) << 16) | ((g & 255u) << 8) | (b & 255u);
}

static uint32_t rgb(unsigned r, unsigned g, unsigned b) {
    return argb(255, r, g, b);
}

static int clamp_int(int v, int lo, int hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static uint32_t alpha_over(uint32_t dst, uint32_t src) {
    unsigned sa = (src >> 24) & 255u;
    unsigned sr = (src >> 16) & 255u;
    unsigned sg = (src >> 8) & 255u;
    unsigned sb = src & 255u;
    unsigned dr = (dst >> 16) & 255u;
    unsigned dg = (dst >> 8) & 255u;
    unsigned db = dst & 255u;
    if (sa == 0) return dst;
    if (sa == 255) return src;
    return rgb((sr * sa + dr * (255u - sa)) / 255u,
               (sg * sa + dg * (255u - sa)) / 255u,
               (sb * sa + db * (255u - sa)) / 255u);
}

static uint32_t mix(uint32_t a, uint32_t b, int t, int max) {
    unsigned aa = (a >> 24) & 255u;
    unsigned ar = (a >> 16) & 255u;
    unsigned ag = (a >> 8) & 255u;
    unsigned ab = a & 255u;
    unsigned ba = (b >> 24) & 255u;
    unsigned br = (b >> 16) & 255u;
    unsigned bg = (b >> 8) & 255u;
    unsigned bb = b & 255u;
    if (max <= 0) max = 1;
    if (t < 0) t = 0;
    if (t > max) t = max;
    return argb((aa * (max - t) + ba * t) / max,
                (ar * (max - t) + br * t) / max,
                (ag * (max - t) + bg * t) / max,
                (ab * (max - t) + bb * t) / max);
}

static void put_pixel(uint32_t *p, int w, int h, int x, int y, uint32_t c) {
    if (!p || x < 0 || y < 0 || x >= w || y >= h) return;
    p[y * w + x] = alpha_over(p[y * w + x], c);
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
        for (xx = x; xx < x + rw; ++xx) put_pixel(p, w, h, xx, yy, c);
    }
}

static void stroke_rect(uint32_t *p, int w, int h, int x, int y, int rw, int rh, uint32_t c) {
    fill_rect(p, w, h, x, y, rw, 1, c);
    fill_rect(p, w, h, x, y + rh - 1, rw, 1, c);
    fill_rect(p, w, h, x, y, 1, rh, c);
    fill_rect(p, w, h, x + rw - 1, y, 1, rh, c);
}

static void round_rect(uint32_t *p, int w, int h, int x, int y, int rw, int rh, int r, uint32_t body, uint32_t border) {
    int yy;
    int rr = r * r;
    if (r < 1) {
        fill_rect(p, w, h, x, y, rw, rh, body);
        stroke_rect(p, w, h, x, y, rw, rh, border);
        return;
    }
    for (yy = y; yy < y + rh; ++yy) {
        int xx;
        for (xx = x; xx < x + rw; ++xx) {
            int cx = xx < x + r ? x + r : (xx >= x + rw - r ? x + rw - r - 1 : xx);
            int cy = yy < y + r ? y + r : (yy >= y + rh - r ? y + rh - r - 1 : yy);
            int dx = xx - cx;
            int dy = yy - cy;
            if (dx * dx + dy * dy <= rr) put_pixel(p, w, h, xx, yy, body);
        }
    }
    stroke_rect(p, w, h, x + r, y, rw - 2 * r, rh, border);
    stroke_rect(p, w, h, x, y + r, rw, rh - 2 * r, border);
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
    static const uint8_t pct[7] = {24,25,2,4,8,19,3};
    static const uint8_t slash[7] = {1,1,2,4,8,16,16};
    static const uint8_t plus[7] = {0,4,4,31,4,4,0};
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
        case '-': return dash; case ':': return colon; case '.': return dot; case '%': return pct;
        case '/': return slash; case '+': return plus; default: return blank;
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

static void draw_icon_square(uint32_t *p, int w, int h, int x, int y, uint32_t a, const char *letter) {
    round_rect(p, w, h, x, y, 34, 30, 8, a, argb(255, 115, 144, 255));
    draw_text(p, w, h, x + 11, y + 8, letter, 2, rgb(255,255,255));
}

static void draw_ridux_logo(uint32_t *p, int w, int h, int x, int y, int s) {
    if (s < 1) s = 1;
    fill_rect(p, w, h, x, y, 2 * s, 7 * s, rgb(29, 219, 198));
    fill_rect(p, w, h, x + 2 * s, y, 4 * s, 2 * s, rgb(42, 144, 255));
    fill_rect(p, w, h, x + 5 * s, y + 2 * s, 2 * s, 2 * s, rgb(18, 202, 232));
    fill_rect(p, w, h, x + 2 * s, y + 3 * s, 4 * s, 2 * s, rgb(84, 225, 82));
    fill_rect(p, w, h, x + 4 * s, y + 5 * s, 2 * s, 2 * s, rgb(31, 146, 73));
    fill_rect(p, w, h, x + 1 * s, y + 2 * s, 4 * s, 1 * s, argb(165, 255, 255, 255));
}

static void draw_wallpaper(struct ridux_panel *s, uint32_t *p, int w, int h) {
    int x, y;
    int logo_s = w > 1200 ? 16 : 12;
    int lx = w / 2 - 4 * logo_s;
    int ly = h / 2 - 8 * logo_s;
    (void)s;
    for (y = 0; y < h; ++y) {
        for (x = 0; x < w; ++x) {
            int dx = x - (w * 72 / 100);
            int dy = y - (h * 42 / 100);
            int glow = clamp_int(255 - (dx * dx + dy * dy) / (w > 0 ? w : 1), 0, 255);
            int t = clamp_int((x * 90 + y * 140) / ((w > 0 ? w : 1) + (h > 0 ? h : 1)), 0, 255);
            int curve1 = h * 70 / 100 - x * 18 / 100 + ((x - w / 2) * (x - w / 2)) / (w * 2 + 1);
            int curve2 = h * 58 / 100 + x * 14 / 100 - ((x - w / 3) * (x - w / 3)) / (w * 4 + 1);
            uint32_t c = mix(rgb(2, 11, 45), rgb(0, 112, 226), t, 255);
            c = mix(c, rgb(28, 219, 235), glow, 440);
            if (y > curve1) c = mix(c, rgb(0, 109, 124), clamp_int((y - curve1) * 2, 0, 255), 255);
            if (y > curve1 + 112) c = mix(c, rgb(0, 45, 79), clamp_int((y - curve1 - 112) * 2, 0, 255), 255);
            if (y > curve2 && y < curve2 + 82) c = mix(c, rgb(114, 224, 177), 120 - abs(y - curve2 - 41), 160);
            if (abs(y - curve2) < 3) c = mix(c, rgb(188, 255, 226), 170, 255);
            p[y * w + x] = c;
        }
    }
    round_rect(p, w, h, w / 2 - 135, h / 2 - 65, 270, 150, 28, argb(22, 255,255,255), argb(36,255,255,255));
    draw_ridux_logo(p, w, h, lx, ly, logo_s);
    draw_text(p, w, h, w / 2 - 84, h / 2 + 34, "RIDUXOS", 3, argb(230, 235, 244, 255));
}

static void dock_metrics(int w, int h, int *x, int *y, int *dw, int *dh) {
    int width = w - 96;
    if (width > 520) width = 520;
    if (width < 360) width = w - 32;
    if (width < 220) width = 220;
    *dw = width;
    *dh = 54;
    *x = (w - width) / 2;
    *y = h - 72;
}

static void draw_dock_icon(uint32_t *p, int w, int h, int x, int y, uint32_t color, const char *letter, int active) {
    round_rect(p, w, h, x, y, 42, 38, 10, argb(140, 255,255,255), argb(110, 255,255,255));
    round_rect(p, w, h, x + 5, y + 4, 32, 30, 8, color, argb(210, 190, 220, 255));
    draw_text(p, w, h, x + 16, y + 12, letter, 2, rgb(255,255,255));
    if (active) round_rect(p, w, h, x + 18, y + 43, 6, 4, 2, rgb(77, 235, 222), rgb(77, 235, 222));
}

static void draw_dock(struct ridux_panel *s, uint32_t *p, int w, int h) {
    int x, y, dw, dh;
    int i;
    const char *letters[] = {"A", "F", "T", "W", "R", "S", "B"};
    uint32_t colors[] = {
        rgb(33, 154, 255), rgb(30, 199, 245), rgb(21, 25, 34),
        rgb(31, 142, 255), rgb(36, 220, 181), rgb(92, 119, 255),
        rgb(180, 210, 230)
    };
    (void)s;
    dock_metrics(w, h, &x, &y, &dw, &dh);
    round_rect(p, w, h, x, y, dw, dh, 16, argb(196, 8, 24, 46), argb(145, 135, 226, 255));
    fill_rect(p, w, h, x + 58, y + 10, 1, dh - 20, argb(65, 255,255,255));
    fill_rect(p, w, h, x + dw - 58, y + 10, 1, dh - 20, argb(65, 255,255,255));
    for (i = 0; i < 7; ++i) {
        int ix = x + 18 + i * ((dw - 36) / 7);
        if (ix + 42 > x + dw - 14) ix = x + dw - 56;
        draw_dock_icon(p, w, h, ix, y + 8, colors[i], letters[i], i == 2 || i == 4);
    }
}

static void draw_top_bar(struct ridux_panel *s, uint32_t *p, int w, int h) {
    int i;
    int rx = w - 282;
    time_t now = time(NULL);
    struct tm tmv;
    char timebuf[16] = "14:28";
    (void)h;
    localtime_r(&now, &tmv);
    strftime(timebuf, sizeof(timebuf), "%H:%M", &tmv);
    round_rect(p, w, h, 8, 6, w - 16, 34, 10, argb(218, 5, 32, 80), argb(170, 88, 190, 255));
    round_rect(p, w, h, 16, 10, 76, 26, 8, argb(205, 20, 75, 145), argb(190, 112, 205, 255));
    draw_text(p, w, h, 24, 17, "R RIDUX", 1, rgb(240,248,255));
    for (i = 0; i < 4; ++i) {
        int x = 100 + i * 42;
        if (i == s->active_workspace) round_rect(p, w, h, x, 10, 32, 26, 8, rgb(42, 136, 255), argb(210, 166, 224, 255));
        else round_rect(p, w, h, x, 10, 32, 26, 8, argb(58, 180,220,255), argb(70, 255,255,255));
        draw_text(p, w, h, x + 13, 17, i == 0 ? "1" : (i == 1 ? "2" : (i == 2 ? "3" : "4")), 1, rgb(240,248,255));
    }
    draw_icon_square(p, w, h, 258, 9, argb(160, 30, 36, 64), "T");
    draw_icon_square(p, w, h, 300, 9, argb(255, 180, 68, 26), "F");
    draw_text(p, w, h, w / 2 - 24, 17, timebuf, 1, rgb(245,248,255));
    draw_text(p, w, h, rx, 17, "VOL 65%", 1, rgb(245,248,255));
    draw_text(p, w, h, rx + 74, 17, "BAT 78%", 1, rgb(245,248,255));
    draw_text(p, w, h, rx + 154, 17, "WIFI", 1, rgb(245,248,255));
    draw_text(p, w, h, rx + 204, 17, "BELL", 1, rgb(245,248,255));
    draw_text(p, w, h, w - 38, 17, "PWR", 1, rgb(245,248,255));
}

static void draw_menu(struct ridux_panel *s, uint32_t *p, int w, int h) {
    int x = 10;
    int y = 50;
    int i;
    const char *titles[] = {"NAVEGADOR WEB", "TERMINAL", "ARCHIVOS", "CONFIGURACION", "TIENDA"};
    const char *subs[] = {"FIREFOX", "RIDUX TERMINAL", "GESTOR DE ARCHIVOS", "AJUSTES DEL SISTEMA", "RIDUX STORE"};
    const char *icons[] = {"F", "T", "A", "S", "R"};
    (void)s;
    (void)h;
    round_rect(p, w, h, x, y, 308, 466, 14, argb(210, 4, 38, 86), argb(170, 96, 210, 255));
    round_rect(p, w, h, x + 16, y + 16, 276, 32, 8, argb(118, 39, 93, 152), argb(130, 125, 215, 255));
    draw_text(p, w, h, x + 28, y + 27, "BUSCAR APLICACIONES", 1, argb(255, 158, 169, 196));
    draw_text(p, w, h, x + 18, y + 72, "FAVORITAS", 1, argb(255, 205, 215, 238));
    for (i = 0; i < 5; ++i) {
        int ry = y + 102 + i * 48;
        round_rect(p, w, h, x + 14, ry, 280, 40, 10, argb(78, 150,205,255), argb(80, 220,245,255));
        draw_icon_square(p, w, h, x + 26, ry + 5, mix(rgb(57, 75, 255), rgb(255, 104, 42), i, 5), icons[i]);
        draw_text(p, w, h, x + 72, ry + 8, titles[i], 1, rgb(248,251,255));
        draw_text(p, w, h, x + 72, ry + 24, subs[i], 1, argb(255, 158, 169, 196));
    }
    fill_rect(p, w, h, x + 18, y + 370, 272, 1, argb(90, 255,255,255));
    draw_text(p, w, h, x + 18, y + 392, "TODAS LAS APLICACIONES", 1, rgb(248,251,255));
    draw_text(p, w, h, x + 270, y + 392, "+", 1, rgb(248,251,255));
    draw_icon_square(p, w, h, x + 22, y + 426, argb(135, 30, 99, 160), "L");
    draw_icon_square(p, w, h, x + 80, y + 426, argb(135, 30, 99, 160), "S");
    draw_icon_square(p, w, h, x + 138, y + 426, argb(135, 30, 99, 160), "P");
}

static void draw_quick(struct ridux_panel *s, uint32_t *p, int w, int h) {
    int x = w - 332;
    int y = 50;
    (void)s;
    round_rect(p, w, h, x, y, 322, 474, 14, argb(205, 5, 43, 94), argb(170, 96, 210, 255));
    draw_text(p, w, h, x + 18, y + 24, "MIERCOLES 21 DE MAYO", 1, argb(255, 180, 190, 215));
    draw_text(p, w, h, x + 286, y + 24, "S", 1, argb(255, 180, 190, 215));
    round_rect(p, w, h, x + 16, y + 58, 132, 54, 10, rgb(67, 82, 255), argb(255, 140, 160, 255));
    round_rect(p, w, h, x + 162, y + 58, 142, 54, 10, argb(88, 170,215,255), argb(115,255,255,255));
    round_rect(p, w, h, x + 16, y + 124, 132, 54, 10, argb(88, 170,215,255), argb(115,255,255,255));
    round_rect(p, w, h, x + 162, y + 124, 142, 54, 10, argb(88, 170,215,255), argb(115,255,255,255));
    draw_text(p, w, h, x + 34, y + 72, "WIFI", 1, rgb(255,255,255));
    draw_text(p, w, h, x + 34, y + 88, "RIDUXNET 5G", 1, argb(255, 190, 199, 238));
    draw_text(p, w, h, x + 178, y + 72, "BLUETOOTH", 1, rgb(255,255,255));
    draw_text(p, w, h, x + 178, y + 88, "ACTIVADO", 1, argb(255, 190, 199, 238));
    draw_text(p, w, h, x + 34, y + 138, "MODO OSCURO", 1, rgb(255,255,255));
    draw_text(p, w, h, x + 178, y + 138, "NO MOLESTAR", 1, rgb(255,255,255));
    fill_rect(p, w, h, x + 40, y + 206, 236, 5, argb(150,255,255,255));
    fill_rect(p, w, h, x + 40, y + 206, 174, 5, rgb(78, 98, 255));
    round_rect(p, w, h, x + 210, y + 199, 14, 18, 7, argb(255, 150,160,190), argb(255, 170,180,210));
    fill_rect(p, w, h, x + 40, y + 238, 236, 5, argb(150,255,255,255));
    fill_rect(p, w, h, x + 40, y + 238, 150, 5, rgb(78, 98, 255));
    round_rect(p, w, h, x + 186, y + 231, 14, 18, 7, argb(255, 150,160,190), argb(255, 170,180,210));
    round_rect(p, w, h, x + 16, y + 272, 288, 118, 10, argb(76, 170,215,255), argb(105,255,255,255));
    draw_icon_square(p, w, h, x + 30, y + 286, argb(255, 28, 28, 34), "M");
    draw_text(p, w, h, x + 78, y + 288, "MADE OF MIND", 1, rgb(255,255,255));
    draw_text(p, w, h, x + 78, y + 306, "INFECTED MUSHROOM", 1, argb(255, 180, 190, 215));
    fill_rect(p, w, h, x + 44, y + 340, 230, 4, argb(130,255,255,255));
    fill_rect(p, w, h, x + 44, y + 340, 100, 4, rgb(78, 98, 255));
    draw_text(p, w, h, x + 92, y + 364, "PREV   PLAY   NEXT", 1, rgb(255,255,255));
    round_rect(p, w, h, x + 16, y + 404, 288, 56, 10, argb(76, 170,215,255), argb(105,255,255,255));
    draw_text(p, w, h, x + 34, y + 418, "RIDUX STORE", 1, rgb(255,255,255));
    draw_text(p, w, h, x + 34, y + 436, "ACTUALIZACION DISPONIBLE", 1, argb(255, 190, 199, 238));
}

static void draw_panel(struct ridux_panel *s) {
    uint32_t *p = (uint32_t *)s->pixels;
    int w = s->width > 0 ? s->width : 1024;
    int h = s->height > 0 ? s->height : 768;
    if (!p) return;
    draw_wallpaper(s, p, w, h);
    draw_dock(s, p, w, h);
    draw_top_bar(s, p, w, h);
    if (s->menu_open) draw_menu(s, p, w, h);
    if (s->quick_open && w > 720) draw_quick(s, p, w, h);
}

static void update_input_region(struct ridux_panel *s) {
    struct wl_region *region;
    if (!s->compositor || !s->surface) return;
    region = wl_compositor_create_region(s->compositor);
    wl_region_add(region, 0, 0, s->width > 0 ? s->width : 1024, 48);
    {
        int dx, dy, dw, dh;
        dock_metrics(s->width > 0 ? s->width : 1024, s->height > 0 ? s->height : 768, &dx, &dy, &dw, &dh);
        wl_region_add(region, dx, dy, dw, dh);
    }
    if (s->menu_open) wl_region_add(region, 8, 48, 316, 474);
    if (s->quick_open && s->width > 720) wl_region_add(region, s->width - 340, 48, 336, 484);
    wl_surface_set_input_region(s->surface, region);
    wl_region_destroy(region);
}

static int ensure_buffer(struct ridux_panel *s, int width, int height) {
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
    s->buffer = wl_shm_pool_create_buffer(pool, 0, width, height, stride, WL_SHM_FORMAT_ARGB8888);
    wl_shm_pool_destroy(pool);
    close(fd);
    s->pixels_size = size;
    return s->buffer ? 0 : -1;
}

static void repaint(struct ridux_panel *s) {
    int width = s->width > 0 ? s->width : 1024;
    int height = s->height > 0 ? s->height : 768;
    if (ensure_buffer(s, width, height) < 0) {
        fprintf(stderr, "ridux-panel: buffer failed: %s\n", strerror(errno));
        return;
    }
    draw_panel(s);
    update_input_region(s);
    wl_surface_attach(s->surface, s->buffer, 0, 0);
    wl_surface_damage_buffer(s->surface, 0, 0, s->width, s->height);
    wl_surface_commit(s->surface);
    wl_display_flush(s->display);
    fprintf(stderr, "ridux-panel: painted %dx%d menu=%d quick=%d\n", s->width, s->height, s->menu_open, s->quick_open);
}

static void spawn_path(const char *path) {
    pid_t pid;
    if (!path || access(path, X_OK) != 0) return;
    pid = fork();
    if (pid == 0) {
        execl(path, path, (char *)NULL);
        _exit(127);
    }
}

static void handle_click(struct ridux_panel *s, int x, int y) {
    int i;
    int dx, dy, dw, dh;
    if (y >= 0 && y < 48) {
        if (x >= 8 && x < 96) s->menu_open = !s->menu_open;
        else if (x >= s->width - 320) s->quick_open = !s->quick_open;
        else {
            for (i = 0; i < 4; ++i) {
                int wx = 100 + i * 42;
                if (x >= wx && x < wx + 34) s->active_workspace = i;
            }
            if (x >= 258 && x < 292) spawn_path("/usr/bin/ridux-terminal");
        }
        repaint(s);
        return;
    }
    dock_metrics(s->width > 0 ? s->width : 1024, s->height > 0 ? s->height : 768, &dx, &dy, &dw, &dh);
    if (x >= dx && x < dx + dw && y >= dy && y < dy + dh) {
        int slot = (x - dx - 18) / ((dw - 36) / 7);
        if (slot == 1) spawn_path("/usr/bin/ridux-open-files");
        else if (slot == 2) spawn_path("/usr/bin/ridux-terminal");
        else if (slot == 3) spawn_path("/usr/bin/ridux-open-launcher");
        else if (slot == 5) spawn_path("/usr/bin/ridux-display-settings");
        else spawn_path("/opt/wayfire/bin/ridux-about");
        return;
    }
    if (s->menu_open && x >= 10 && x < 318 && y >= 50 && y < 516) {
        int row = (y - 152) / 48;
        if (y >= 152 && row >= 0 && row < 5) {
            if (row == 1) spawn_path("/usr/bin/ridux-terminal");
            else if (row == 2) spawn_path("/usr/bin/ridux-open-files");
            else if (row == 3) spawn_path("/usr/bin/ridux-display-settings");
            else spawn_path("/opt/wayfire/bin/ridux-about");
        }
        if (y >= 430 && y < 470) spawn_path("/usr/bin/ridux-open-launcher");
    }
}

static void pointer_enter(void *data, struct wl_pointer *pointer, uint32_t serial, struct wl_surface *surface, wl_fixed_t sx, wl_fixed_t sy) {
    struct ridux_panel *s = data;
    (void)pointer; (void)serial; (void)surface;
    s->pointer_x = wl_fixed_to_int(sx);
    s->pointer_y = wl_fixed_to_int(sy);
}

static void pointer_leave(void *data, struct wl_pointer *pointer, uint32_t serial, struct wl_surface *surface) {
    (void)data; (void)pointer; (void)serial; (void)surface;
}

static void pointer_motion(void *data, struct wl_pointer *pointer, uint32_t time, wl_fixed_t sx, wl_fixed_t sy) {
    struct ridux_panel *s = data;
    (void)pointer; (void)time;
    s->pointer_x = wl_fixed_to_int(sx);
    s->pointer_y = wl_fixed_to_int(sy);
}

static void pointer_button(void *data, struct wl_pointer *pointer, uint32_t serial, uint32_t time, uint32_t button, uint32_t state) {
    struct ridux_panel *s = data;
    (void)pointer; (void)serial; (void)time;
    if (button == RIDUX_BTN_LEFT && state == WL_POINTER_BUTTON_STATE_PRESSED) handle_click(s, s->pointer_x, s->pointer_y);
}

static void pointer_axis(void *data, struct wl_pointer *pointer, uint32_t time, uint32_t axis, wl_fixed_t value) {
    (void)data; (void)pointer; (void)time; (void)axis; (void)value;
}

static const struct wl_pointer_listener pointer_listener = {
    .enter = pointer_enter,
    .leave = pointer_leave,
    .motion = pointer_motion,
    .button = pointer_button,
    .axis = pointer_axis
};

static void seat_capabilities(void *data, struct wl_seat *seat, uint32_t caps) {
    struct ridux_panel *s = data;
    if ((caps & WL_SEAT_CAPABILITY_POINTER) && !s->pointer) {
        s->pointer = wl_seat_get_pointer(seat);
        wl_pointer_add_listener(s->pointer, &pointer_listener, s);
    } else if (!(caps & WL_SEAT_CAPABILITY_POINTER) && s->pointer) {
        wl_pointer_destroy(s->pointer);
        s->pointer = NULL;
    }
}

static void seat_name(void *data, struct wl_seat *seat, const char *name) {
    (void)data; (void)seat; (void)name;
}

static const struct wl_seat_listener seat_listener = {
    .capabilities = seat_capabilities,
    .name = seat_name
};

static void wm_base_ping(void *data, struct xdg_wm_base *wm_base, uint32_t serial) {
    (void)data;
    xdg_wm_base_pong(wm_base, serial);
}

static const struct xdg_wm_base_listener wm_base_listener = {
    .ping = wm_base_ping
};

static void xdg_surface_configure(void *data, struct xdg_surface *surface, uint32_t serial) {
    struct ridux_panel *s = data;
    xdg_surface_ack_configure(surface, serial);
    s->configured = 1;
    repaint(s);
}

static const struct xdg_surface_listener xdg_surface_listener = {
    .configure = xdg_surface_configure
};

static void toplevel_configure(void *data, struct xdg_toplevel *toplevel, int32_t width, int32_t height, struct wl_array *states) {
    struct ridux_panel *s = data;
    (void)toplevel;
    (void)states;
    if (width > 0) s->width = width;
    if (height > 0) s->height = height;
}

static void toplevel_close(void *data, struct xdg_toplevel *toplevel) {
    struct ridux_panel *s = data;
    (void)toplevel;
    s->running = 0;
}

static const struct xdg_toplevel_listener toplevel_listener = {
    .configure = toplevel_configure,
    .close = toplevel_close
};

static void registry_global(void *data, struct wl_registry *registry, uint32_t name, const char *interface, uint32_t version) {
    struct ridux_panel *s = data;
    if (strcmp(interface, wl_compositor_interface.name) == 0) {
        s->compositor = wl_registry_bind(registry, name, &wl_compositor_interface, version < 4 ? version : 4);
    } else if (strcmp(interface, wl_shm_interface.name) == 0) {
        s->shm = wl_registry_bind(registry, name, &wl_shm_interface, 1);
    } else if (strcmp(interface, wl_seat_interface.name) == 0) {
        s->seat = wl_registry_bind(registry, name, &wl_seat_interface, version < 5 ? version : 5);
        wl_seat_add_listener(s->seat, &seat_listener, s);
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
    struct ridux_panel s;
    memset(&s, 0, sizeof(s));
    s.width = 1024;
    s.height = 768;
    s.running = 1;
    s.menu_open = 1;
    s.quick_open = 1;
    s.active_workspace = 0;
    s.display = wl_display_connect(NULL);
    if (!s.display) {
        fprintf(stderr, "ridux-panel: cannot connect to Wayland display\n");
        return 1;
    }
    s.registry = wl_display_get_registry(s.display);
    wl_registry_add_listener(s.registry, &registry_listener, &s);
    wl_display_roundtrip(s.display);
    wl_display_roundtrip(s.display);
    if (!s.compositor || !s.shm || !s.wm_base) {
        fprintf(stderr, "ridux-panel: missing compositor=%p shm=%p wm_base=%p\n", (void*)s.compositor, (void*)s.shm, (void*)s.wm_base);
        return 2;
    }
    s.surface = wl_compositor_create_surface(s.compositor);
    s.xdg_surface = xdg_wm_base_get_xdg_surface(s.wm_base, s.surface);
    xdg_surface_add_listener(s.xdg_surface, &xdg_surface_listener, &s);
    s.toplevel = xdg_surface_get_toplevel(s.xdg_surface);
    xdg_toplevel_add_listener(s.toplevel, &toplevel_listener, &s);
    xdg_toplevel_set_title(s.toplevel, "Ridux Panel");
    xdg_toplevel_set_app_id(s.toplevel, "ridux-panel");
    xdg_toplevel_set_maximized(s.toplevel);
    wl_surface_commit(s.surface);
    fprintf(stderr, "ridux-panel: connected\n");
    while (s.running && wl_display_dispatch(s.display) >= 0) {}
    if (s.pointer) wl_pointer_destroy(s.pointer);
    if (s.buffer) wl_buffer_destroy(s.buffer);
    if (s.pixels) munmap(s.pixels, s.pixels_size);
    if (s.toplevel) xdg_toplevel_destroy(s.toplevel);
    if (s.xdg_surface) xdg_surface_destroy(s.xdg_surface);
    if (s.surface) wl_surface_destroy(s.surface);
    if (s.wm_base) xdg_wm_base_destroy(s.wm_base);
    if (s.seat) wl_seat_destroy(s.seat);
    if (s.shm) wl_shm_destroy(s.shm);
    if (s.compositor) wl_compositor_destroy(s.compositor);
    if (s.registry) wl_registry_destroy(s.registry);
    wl_display_disconnect(s.display);
    return 0;
}
