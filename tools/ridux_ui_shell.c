#include "ridux_native_ui.h"

#include <errno.h>
#include <fcntl.h>
#ifdef __linux__
#include <linux/input.h>
#endif
#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#ifndef EV_SYN
#define EV_SYN 0x00
#endif
#ifndef EV_KEY
#define EV_KEY 0x01
#endif
#ifndef EV_REL
#define EV_REL 0x02
#endif
#ifndef REL_X
#define REL_X 0x00
#endif
#ifndef REL_Y
#define REL_Y 0x01
#endif
#ifndef BTN_LEFT
#define BTN_LEFT 0x110
#endif
#ifndef BTN_RIGHT
#define BTN_RIGHT 0x111
#endif

static void shell_write_all(const char *text, uint32_t len) {
    while (len > 0) {
        ssize_t wrote = write(2, text, len);
        if (wrote <= 0) return;
        text += (uint32_t)wrote;
        len -= (uint32_t)wrote;
    }
}

static uint32_t shell_len(const char *text) {
    uint32_t len = 0;
    if (!text) return 0;
    while (text[len]) ++len;
    return len;
}

static void shell_log(const char *text) {
    shell_write_all(text, shell_len(text));
}

static int shell_frame_log_enabled(void) {
    static int cached = -1;
    if (cached < 0)
        cached = (access("/etc/ridux-ui-frame-log.enable", F_OK) == 0) ? 1 : 0;
    return cached;
}

static uint32_t shell_frame_delay_us(void) {
    static int cached = -1;
    if (cached < 0) {
        const char *env = getenv("RIDUX_UI_FRAME_DELAY_US");
        unsigned long delay = 0;
        if (env && *env) {
            delay = strtoul(env, NULL, 10);
            if (delay > 1000000UL) delay = 1000000UL;
        }
        cached = (int)delay;
    }
    return (uint32_t)cached;
}

static int shell_stage_log_enabled(void) {
    static int cached = -1;
    if (cached < 0)
        cached = (getenv("RIDUX_UI_STAGE_TRACE") ||
                  access("/etc/ridux-ui-stage-trace.enable", F_OK) == 0) ? 1 : 0;
    return cached;
}

static uint64_t shell_now_ms(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) == 0)
        return (uint64_t)ts.tv_sec * 1000ULL + (uint64_t)ts.tv_nsec / 1000000ULL;
    return 0;
}

static void shell_log_u32(uint32_t value) {
    char buf[16];
    uint32_t pos = sizeof(buf);
    if (value == 0) {
        shell_write_all("0", 1);
        return;
    }
    while (value && pos > 0) {
        buf[--pos] = (char)('0' + (value % 10u));
        value /= 10u;
    }
    shell_write_all(&buf[pos], (uint32_t)sizeof(buf) - pos);
}

static float shell_tri(uint64_t now_ms, uint32_t period_ms, uint32_t offset_ms) {
    uint32_t p;
    uint32_t half;
    if (!period_ms) return 0.0f;
    p = (uint32_t)((now_ms + offset_ms) % period_ms);
    half = period_ms / 2u;
    if (!half) return 0.0f;
    if (p > half) p = period_ms - p;
    return (float)p / (float)half;
}

#define SHELL_MAX_WINDOWS 4
#define SHELL_TITLE_H 34.0f
#define SHELL_TASKBAR_H 58.0f

typedef struct shell_window {
    const char *title;
    const char *tag;
    rui_color_t accent;
    float x;
    float y;
    float w;
    float h;
    int visible;
    int minimized;
} shell_window_t;

typedef struct shell_state {
    int mouse_fd;
    int input_logged;
    float mouse_x;
    float mouse_y;
    float screen_w;
    float screen_h;
    int mouse_left;
    int mouse_pressed;
    int mouse_released;
    int drag_window;
    float drag_dx;
    float drag_dy;
    int focused;
    int z_order[SHELL_MAX_WINDOWS];
    shell_window_t windows[SHELL_MAX_WINDOWS];
} shell_state_t;

static float shell_clampf(float v, float lo, float hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static int shell_hit(float px, float py, float x, float y, float w, float h) {
    return px >= x && py >= y && px < x + w && py < y + h;
}

static void shell_bring_to_front(shell_state_t *s, int idx) {
    int i;
    int pos = -1;
    for (i = 0; i < SHELL_MAX_WINDOWS; ++i) {
        if (s->z_order[i] == idx) {
            pos = i;
            break;
        }
    }
    if (pos < 0) return;
    for (i = pos; i < SHELL_MAX_WINDOWS - 1; ++i)
        s->z_order[i] = s->z_order[i + 1];
    s->z_order[SHELL_MAX_WINDOWS - 1] = idx;
    s->focused = idx;
}

static void shell_layout_defaults(shell_state_t *s) {
    float w = s->screen_w;
    float h = s->screen_h;
    float main_w = shell_clampf(w * 0.46f, 390.0f, 540.0f);
    float side_w = shell_clampf(w * 0.34f, 310.0f, 430.0f);
    float bottom_h = shell_clampf(h * 0.24f, 148.0f, 190.0f);

    s->windows[0] = (shell_window_t){ "Files", "F", rui_rgb8(54, 166, 255),
                                      92.0f, 88.0f, main_w, 314.0f, 1, 0 };
    s->windows[1] = (shell_window_t){ "Terminal", "T", rui_rgb8(88, 224, 160),
                                      w - side_w - 74.0f, 118.0f, side_w, 282.0f, 1, 0 };
    s->windows[2] = (shell_window_t){ "Settings", "S", rui_rgb8(245, 181, 72),
                                      128.0f, h - bottom_h - SHELL_TASKBAR_H - 30.0f,
                                      w - 246.0f, bottom_h, 1, 0 };
    s->windows[3] = (shell_window_t){ "Activity", "A", rui_rgb8(234, 98, 146),
                                      w * 0.42f, 82.0f, shell_clampf(w * 0.26f, 260.0f, 360.0f),
                                      218.0f, 1, 0 };
}

static void shell_state_init(shell_state_t *s, float w, float h) {
    int i;
    memset(s, 0, sizeof(*s));
    s->mouse_fd = -1;
    s->mouse_x = w * 0.50f;
    s->mouse_y = h * 0.55f;
    s->screen_w = w;
    s->screen_h = h;
    s->drag_window = -1;
    s->focused = 1;
    for (i = 0; i < SHELL_MAX_WINDOWS; ++i) s->z_order[i] = i;
    shell_layout_defaults(s);
    shell_bring_to_front(s, 1);
}

static void shell_open_input(shell_state_t *s) {
    if (s->mouse_fd >= 0 || s->input_logged) return;
    s->mouse_fd = open("/dev/input/event1", O_RDONLY | O_NONBLOCK);
    if (s->mouse_fd >= 0)
        shell_log("[ridux-ui-shell] input mouse=/dev/input/event1 status=ok\n");
    else
        shell_log("[ridux-ui-shell] input mouse=/dev/input/event1 status=unavailable\n");
    s->input_logged = 1;
}

static void shell_poll_input(shell_state_t *s) {
    struct input_event events[32];
    s->mouse_pressed = 0;
    s->mouse_released = 0;
    shell_open_input(s);
    if (s->mouse_fd < 0) return;
    for (;;) {
        ssize_t got = read(s->mouse_fd, events, sizeof(events));
        ssize_t i;
        if (got <= 0) {
            if (got < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
                close(s->mouse_fd);
                s->mouse_fd = -1;
                s->input_logged = 0;
            }
            break;
        }
        for (i = 0; i < got / (ssize_t)sizeof(events[0]); ++i) {
            if (events[i].type == EV_REL) {
                if (events[i].code == REL_X) s->mouse_x += (float)events[i].value;
                if (events[i].code == REL_Y) s->mouse_y += (float)events[i].value;
            } else if (events[i].type == EV_KEY && events[i].code == BTN_LEFT) {
                int down = events[i].value != 0;
                if (down && !s->mouse_left) s->mouse_pressed = 1;
                if (!down && s->mouse_left) s->mouse_released = 1;
                s->mouse_left = down;
            }
        }
        s->mouse_x = shell_clampf(s->mouse_x, 0.0f, s->screen_w - 1.0f);
        s->mouse_y = shell_clampf(s->mouse_y, 0.0f, s->screen_h - 1.0f);
    }
}

static void shell_focus_dock_window(shell_state_t *s, int idx) {
    if (idx < 0 || idx >= SHELL_MAX_WINDOWS) return;
    s->windows[idx].visible = 1;
    s->windows[idx].minimized = 0;
    shell_bring_to_front(s, idx);
}

static void shell_update_interaction(shell_state_t *s) {
    int zi;
    if (s->mouse_pressed) {
        float dock_w = 42.0f;
        float gap = 12.0f;
        float total = (dock_w + gap) * (float)SHELL_MAX_WINDOWS - gap;
        float start_x = (s->screen_w - total) * 0.5f;
        float y = s->screen_h - 48.0f;
        int i;
        s->drag_window = -1;
        for (i = 0; i < SHELL_MAX_WINDOWS; ++i) {
            float x = start_x + (dock_w + gap) * (float)i;
            if (shell_hit(s->mouse_x, s->mouse_y, x, y, dock_w, 38.0f)) {
                shell_focus_dock_window(s, i);
                return;
            }
        }
        for (zi = SHELL_MAX_WINDOWS - 1; zi >= 0; --zi) {
            int idx = s->z_order[zi];
            shell_window_t *win = &s->windows[idx];
            float close_x = win->x + win->w - 24.0f;
            float min_x = win->x + win->w - 48.0f;
            if (!win->visible || win->minimized) continue;
            if (!shell_hit(s->mouse_x, s->mouse_y, win->x, win->y, win->w, win->h)) continue;
            shell_bring_to_front(s, idx);
            if (shell_hit(s->mouse_x, s->mouse_y, close_x, win->y + 10.0f, 12.0f, 12.0f)) {
                win->visible = 0;
                return;
            }
            if (shell_hit(s->mouse_x, s->mouse_y, min_x, win->y + 10.0f, 12.0f, 12.0f)) {
                win->minimized = 1;
                return;
            }
            if (shell_hit(s->mouse_x, s->mouse_y, win->x, win->y, win->w, SHELL_TITLE_H)) {
                s->drag_window = idx;
                s->drag_dx = s->mouse_x - win->x;
                s->drag_dy = s->mouse_y - win->y;
            }
            return;
        }
    }
    if (s->mouse_left && s->drag_window >= 0) {
        shell_window_t *win = &s->windows[s->drag_window];
        float max_x = s->screen_w - win->w - 10.0f;
        float max_y = s->screen_h - win->h - SHELL_TASKBAR_H - 8.0f;
        win->x = shell_clampf(s->mouse_x - s->drag_dx, 8.0f, max_x > 8.0f ? max_x : 8.0f);
        win->y = shell_clampf(s->mouse_y - s->drag_dy, 64.0f, max_y > 64.0f ? max_y : 64.0f);
    }
    if (s->mouse_released) s->drag_window = -1;
}

static void gradient_y(rui_context_t *ui, float x, float y, float w, float h,
                       rui_color_t top, rui_color_t bottom, int steps) {
    int i;
    if (steps < 1) steps = 1;
    for (i = 0; i < steps; ++i) {
        float t = (float)i / (float)(steps - 1 ? steps - 1 : 1);
        rui_color_t c = rui_rgba(top.r + (bottom.r - top.r) * t,
                                 top.g + (bottom.g - top.g) * t,
                                 top.b + (bottom.b - top.b) * t,
                                 top.a + (bottom.a - top.a) * t);
        rui_rect(ui, (rui_rect_t){ x, y + h * (float)i / (float)steps,
                                   w, h / (float)steps + 1.0f }, c);
    }
}

static void task_icon(rui_context_t *ui, float x, float y, const char *label,
                      rui_color_t accent, int active, int hover) {
    rui_color_t top = hover ? rui_rgba(accent.r, accent.g, accent.b, 0.98f)
                            : rui_rgba(accent.r * 0.84f, accent.g * 0.84f, accent.b * 0.84f, 0.88f);
    rui_color_t bottom = rui_rgba(accent.r * 0.30f, accent.g * 0.30f, accent.b * 0.30f, 0.92f);
    rui_rect(ui, (rui_rect_t){ x + 2.0f, y + 4.0f, 42.0f, 38.0f }, rui_rgba(0.0f, 0.0f, 0.0f, hover ? 0.32f : 0.18f));
    gradient_y(ui, x, y, 42.0f, 38.0f, top, bottom, 8);
    if (active)
        rui_rect(ui, (rui_rect_t){ x + 10.0f, y + 35.0f, 22.0f, 2.0f }, rui_rgb8(252, 253, 255));
    rui_text(ui, x + 13.0f, y + 8.0f, label, 2.0f, rui_rgb8(252, 253, 255));
}

static void draw_window_frame(rui_context_t *ui, const shell_state_t *s, int idx) {
    const shell_window_t *win = &s->windows[idx];
    int active = idx == s->focused;
    rui_color_t bg = active ? rui_rgba(20.0f / 255.0f, 24.0f / 255.0f, 31.0f / 255.0f, 0.96f)
                            : rui_rgba(18.0f / 255.0f, 21.0f / 255.0f, 28.0f / 255.0f, 0.88f);
    rui_rect(ui, (rui_rect_t){ win->x + 12.0f, win->y + 16.0f, win->w, win->h }, rui_rgba(0.0f, 0.0f, 0.0f, 0.34f));
    rui_rect(ui, (rui_rect_t){ win->x + 5.0f, win->y + 7.0f, win->w, win->h }, rui_rgba(0.0f, 0.0f, 0.0f, 0.18f));
    rui_rect(ui, (rui_rect_t){ win->x, win->y, win->w, win->h }, bg);
    rui_rect(ui, (rui_rect_t){ win->x, win->y, win->w, 1.0f }, rui_rgba(1.0f, 1.0f, 1.0f, active ? 0.22f : 0.10f));
    rui_rect(ui, (rui_rect_t){ win->x, win->y, win->w, SHELL_TITLE_H }, rui_rgba(6.0f / 255.0f, 9.0f / 255.0f, 15.0f / 255.0f, active ? 0.84f : 0.62f));
    rui_rect(ui, (rui_rect_t){ win->x, win->y + SHELL_TITLE_H - 1.0f, win->w, 1.0f }, rui_rgba(1.0f, 1.0f, 1.0f, 0.08f));
    rui_rect(ui, (rui_rect_t){ win->x, win->y, 4.0f, win->h }, win->accent);
    rui_text(ui, win->x + 18.0f, win->y + 8.0f, win->title, 1.7f, rui_rgb8(240, 244, 249));

    rui_rect(ui, (rui_rect_t){ win->x + win->w - 48.0f, win->y + 10.0f, 12.0f, 12.0f },
             rui_rgba(0.95f, 0.74f, 0.27f, 0.92f));
    rui_rect(ui, (rui_rect_t){ win->x + win->w - 24.0f, win->y + 10.0f, 12.0f, 12.0f },
             rui_rgba(0.98f, 0.35f, 0.38f, 0.94f));
}

static void draw_files_content(rui_context_t *ui, const shell_window_t *win) {
    const char *names[5] = { "Desktop", "Projects", "System", "Media", "Downloads" };
    const char *meta[5] = { "4 items", "RiduxOS", "rootfs", "assets", "today" };
    int i;
    rui_text(ui, win->x + 24.0f, win->y + 54.0f, "Home", 2.4f, rui_rgb8(246, 249, 252));
    rui_rect(ui, (rui_rect_t){ win->x + 24.0f, win->y + 88.0f, win->w - 48.0f, 1.0f }, rui_rgba(1.0f, 1.0f, 1.0f, 0.10f));
    for (i = 0; i < 5; ++i) {
        float row_y = win->y + 106.0f + (float)i * 37.0f;
        rui_rect(ui, (rui_rect_t){ win->x + 24.0f, row_y, win->w - 48.0f, 28.0f },
                 i == 1 ? rui_rgba(0.20f, 0.48f, 0.90f, 0.22f) : rui_rgba(1.0f, 1.0f, 1.0f, 0.045f));
        rui_rect(ui, (rui_rect_t){ win->x + 36.0f, row_y + 7.0f, 14.0f, 14.0f },
                 i == 1 ? win->accent : rui_rgb8(96, 108, 124));
        rui_text(ui, win->x + 62.0f, row_y + 5.0f, names[i], 1.5f, rui_rgb8(232, 237, 244));
        rui_text(ui, win->x + win->w - 118.0f, row_y + 6.0f, meta[i], 1.3f, rui_rgb8(148, 160, 176));
    }
}

static void draw_terminal_content(rui_context_t *ui, const shell_window_t *win, uint64_t now_ms) {
    float pulse = shell_tri(now_ms, 900u, 0u);
    rui_rect(ui, (rui_rect_t){ win->x + 18.0f, win->y + 52.0f, win->w - 36.0f, win->h - 70.0f },
             rui_rgba(3.0f / 255.0f, 7.0f / 255.0f, 11.0f / 255.0f, 0.88f));
    rui_text(ui, win->x + 34.0f, win->y + 70.0f, "$ riduxctl status", 1.5f, rui_rgb8(130, 240, 185));
    rui_text(ui, win->x + 34.0f, win->y + 104.0f, "desktop online", 1.5f, rui_rgb8(224, 232, 242));
    rui_text(ui, win->x + 34.0f, win->y + 138.0f, "renderer virgl", 1.5f, rui_rgb8(126, 209, 255));
    rui_text(ui, win->x + 34.0f, win->y + 172.0f, "input evdev", 1.5f, rui_rgb8(246, 203, 120));
    rui_rect(ui, (rui_rect_t){ win->x + 34.0f, win->y + 211.0f, 10.0f + pulse * 8.0f, 18.0f },
             rui_rgba(0.52f, 0.94f, 0.72f, 0.88f));
}

static void draw_settings_content(rui_context_t *ui, const shell_window_t *win, uint64_t now_ms) {
    float pulse = shell_tri(now_ms, 2200u, 200u);
    const char *labels[3] = { "Appearance", "Display", "Input" };
    int i;
    for (i = 0; i < 3; ++i) {
        float x = win->x + 28.0f + (float)i * ((win->w - 64.0f) / 3.0f);
        float col_w = (win->w - 90.0f) / 3.0f;
        rui_rect(ui, (rui_rect_t){ x, win->y + 58.0f, col_w, win->h - 84.0f },
                 rui_rgba(1.0f, 1.0f, 1.0f, 0.055f));
        rui_text(ui, x + 16.0f, win->y + 78.0f, labels[i], 1.7f, rui_rgb8(236, 241, 248));
        rui_rect(ui, (rui_rect_t){ x + 16.0f, win->y + 120.0f, col_w - 32.0f, 8.0f },
                 rui_rgba(1.0f, 1.0f, 1.0f, 0.12f));
        rui_rect(ui, (rui_rect_t){ x + 16.0f, win->y + 120.0f,
                                   (col_w - 32.0f) * (0.42f + pulse * 0.42f), 8.0f },
                 i == 0 ? win->accent : (i == 1 ? rui_rgb8(83, 201, 255) : rui_rgb8(111, 229, 158)));
    }
}

static void draw_activity_content(rui_context_t *ui, const shell_window_t *win,
                                  const rui_frame_info_t *f, uint64_t now_ms) {
    float p0 = shell_tri(now_ms, 1800u, 0u);
    float p1 = shell_tri(now_ms, 2300u, 400u);
    float p2 = shell_tri(now_ms, 3100u, 900u);
    char line[128];
    rui_text(ui, win->x + 22.0f, win->y + 56.0f, "System", 2.2f, rui_rgb8(246, 249, 252));
    rui_text(ui, win->x + 24.0f, win->y + 94.0f, f->renderer ? f->renderer : "renderer", 1.35f, rui_rgb8(155, 226, 255));
    snprintf(line, sizeof(line), "Frame %u", f ? f->frame : 0u);
    rui_text(ui, win->x + 24.0f, win->y + 126.0f, line, 1.45f, rui_rgb8(224, 232, 242));
    rui_rect(ui, (rui_rect_t){ win->x + 24.0f, win->y + 160.0f, win->w - 48.0f, 8.0f }, rui_rgba(1.0f, 1.0f, 1.0f, 0.10f));
    rui_rect(ui, (rui_rect_t){ win->x + 24.0f, win->y + 160.0f, (win->w - 48.0f) * (0.48f + p0 * 0.46f), 8.0f }, win->accent);
    rui_rect(ui, (rui_rect_t){ win->x + 24.0f, win->y + 180.0f, win->w - 48.0f, 8.0f }, rui_rgba(1.0f, 1.0f, 1.0f, 0.10f));
    rui_rect(ui, (rui_rect_t){ win->x + 24.0f, win->y + 180.0f, (win->w - 48.0f) * (0.25f + p1 * 0.52f), 8.0f }, rui_rgb8(93, 218, 172));
    rui_rect(ui, (rui_rect_t){ win->x + 24.0f, win->y + 200.0f, win->w - 48.0f, 8.0f }, rui_rgba(1.0f, 1.0f, 1.0f, 0.10f));
    rui_rect(ui, (rui_rect_t){ win->x + 24.0f, win->y + 200.0f, (win->w - 48.0f) * (0.18f + p2 * 0.60f), 8.0f }, rui_rgb8(247, 196, 92));
}

static void draw_window(rui_context_t *ui, const shell_state_t *s, int idx,
                        const rui_frame_info_t *f, uint64_t now_ms) {
    const shell_window_t *win = &s->windows[idx];
    if (!win->visible || win->minimized) return;
    draw_window_frame(ui, s, idx);
    if (idx == 0) draw_files_content(ui, win);
    else if (idx == 1) draw_terminal_content(ui, win, now_ms);
    else if (idx == 2) draw_settings_content(ui, win, now_ms);
    else draw_activity_content(ui, win, f, now_ms);
}

static void draw_taskbar(rui_context_t *ui, const shell_state_t *s) {
    float w = s->screen_w;
    float h = s->screen_h;
    float dock_w = 42.0f;
    float gap = 12.0f;
    float total = (dock_w + gap) * (float)SHELL_MAX_WINDOWS - gap;
    float start_x = (w - total) * 0.5f;
    int i;
    rui_rect(ui, (rui_rect_t){ 0.0f, h - SHELL_TASKBAR_H, w, SHELL_TASKBAR_H },
             rui_rgba(4.0f / 255.0f, 7.0f / 255.0f, 11.0f / 255.0f, 0.88f));
    rui_rect(ui, (rui_rect_t){ 0.0f, h - SHELL_TASKBAR_H, w, 1.0f }, rui_rgba(1.0f, 1.0f, 1.0f, 0.11f));
    rui_rect(ui, (rui_rect_t){ 18.0f, h - 44.0f, 108.0f, 30.0f }, rui_rgba(1.0f, 1.0f, 1.0f, 0.08f));
    rui_text(ui, 34.0f, h - 36.0f, "Ridux", 1.7f, rui_rgb8(247, 250, 253));
    for (i = 0; i < SHELL_MAX_WINDOWS; ++i) {
        const shell_window_t *win = &s->windows[i];
        float x = start_x + (dock_w + gap) * (float)i;
        int hover = shell_hit(s->mouse_x, s->mouse_y, x, h - 48.0f, dock_w, 38.0f);
        int active = win->visible && !win->minimized && s->focused == i;
        task_icon(ui, x, h - 48.0f, win->tag, win->accent, active, hover);
    }
    rui_rect(ui, (rui_rect_t){ w - 176.0f, h - 44.0f, 148.0f, 30.0f }, rui_rgba(1.0f, 1.0f, 1.0f, 0.07f));
    rui_text(ui, w - 154.0f, h - 36.0f, "Mesa virgl", 1.5f, rui_rgb8(156, 226, 255));
}

static void draw_cursor(rui_context_t *ui, float x, float y) {
    static const uint8_t outline_w[23] = {
        3, 5, 7, 9, 11, 13, 15, 17, 19, 21, 19, 16,
        14, 12, 10, 9, 8, 7, 6, 5, 4, 3, 2
    };
    static const uint8_t fill_w[19] = {
        1, 3, 5, 7, 9, 11, 13, 15, 15, 13, 10, 8, 6, 5, 4, 3, 2, 1, 1
    };
    int i;
    rui_rect(ui, (rui_rect_t){ x + 3.0f, y + 5.0f, 18.0f, 22.0f }, rui_rgba(0.0f, 0.0f, 0.0f, 0.22f));
    for (i = 0; i < 23; ++i)
        rui_rect(ui, (rui_rect_t){ x, y + (float)i, (float)outline_w[i], 1.0f }, rui_rgb8(8, 10, 12));
    for (i = 0; i < 19; ++i)
        rui_rect(ui, (rui_rect_t){ x + 2.0f, y + 2.0f + (float)i, (float)fill_w[i], 1.0f }, rui_rgb8(248, 250, 252));
    rui_rect(ui, (rui_rect_t){ x + 8.0f, y + 17.0f, 7.0f, 2.0f }, rui_rgb8(8, 10, 12));
    rui_rect(ui, (rui_rect_t){ x + 10.0f, y + 19.0f, 7.0f, 8.0f }, rui_rgb8(8, 10, 12));
    rui_rect(ui, (rui_rect_t){ x + 12.0f, y + 20.0f, 3.0f, 6.0f }, rui_rgb8(248, 250, 252));
}

static void draw_shell(rui_context_t *ui, shell_state_t *s) {
    const rui_frame_info_t *f = rui_frame_info(ui);
    float w = (float)f->width;
    float h = (float)f->height;
    uint64_t now_ms = shell_now_ms();
    float pulse = shell_tri(now_ms, 1900u, 0u);
    float slow = shell_tri(now_ms, 5600u, 700u);
    int zi;

    s->screen_w = w;
    s->screen_h = h;
    rui_begin(ui, rui_rgb8(7, 10, 16));
    gradient_y(ui, 0.0f, 0.0f, w, h,
               rui_rgb8(12, 18, 28), rui_rgb8(21, 24, 29), 34);
    rui_rect(ui, (rui_rect_t){ -90.0f + slow * 90.0f, 72.0f, w * 0.56f, 118.0f },
             rui_rgba(0.0f, 0.55f, 0.86f, 0.14f));
    rui_rect(ui, (rui_rect_t){ w * 0.50f - pulse * 70.0f, 188.0f, w * 0.48f, 132.0f },
             rui_rgba(0.72f, 0.22f, 0.58f, 0.11f));
    rui_rect(ui, (rui_rect_t){ 0.0f, 0.0f, w, 48.0f }, rui_rgba(3.0f / 255.0f, 6.0f / 255.0f, 10.0f / 255.0f, 0.82f));
    rui_rect(ui, (rui_rect_t){ 0.0f, 47.0f, w, 1.0f }, rui_rgba(1.0f, 1.0f, 1.0f, 0.10f));
    rui_text(ui, 22.0f, 13.0f, "RiduxOS", 2.1f, rui_rgb8(249, 252, 255));
    rui_text(ui, w - 238.0f, 15.0f, "DRM / GBM / EGL", 1.45f, rui_rgb8(184, 196, 211));

    for (zi = 0; zi < SHELL_MAX_WINDOWS; ++zi)
        draw_window(ui, s, s->z_order[zi], f, now_ms);
    draw_taskbar(ui, s);
    draw_cursor(ui, s->mouse_x, s->mouse_y);
}

int main(int argc, char **argv) {
    rui_context_t *ui = NULL;
    rui_options_t options;
    shell_state_t shell;
    uint64_t fps_last_ms = 0;
    uint32_t fps_last_frame = 0;
    uint32_t startup_fps_logs = 0;
    uint32_t startup_stage_logs = 0;
    uint32_t last_draw_ms = 0;
    uint32_t last_present_ms = 0;
    uint32_t last_total_ms = 0;
    (void)argc;
    (void)argv;
    shell_log("[ridux-ui-shell] start backend=RiduxUI DRM/GBM/EGL/GLES2\n");
    options.backend = RUI_BACKEND_DRM;
    options.immediate_present = false;
    options.require_hardware = true;
    if (!rui_open(&ui, &options)) {
        shell_log("[ridux-ui-shell] RiduxUI init failed\n");
        return 2;
    }
    {
        const rui_frame_info_t *start = rui_frame_info(ui);
        shell_state_init(&shell, start ? (float)start->width : 1024.0f,
                         start ? (float)start->height : 768.0f);
        shell_open_input(&shell);
    }
    while (1) {
        const rui_frame_info_t *f;
        uint32_t delay_us;
        uint64_t frame_begin_ms = shell_now_ms();
        uint64_t draw_done_ms;
        uint64_t present_done_ms;
        if (shell_stage_log_enabled() && startup_stage_logs < 8u) {
            f = rui_frame_info(ui);
            shell_log("[ridux-ui-shell] stage=");
            shell_log_u32(startup_stage_logs);
            shell_log(" draw begin frame=");
            shell_log_u32(f ? f->frame : 0u);
            shell_log("\n");
        }
        f = rui_frame_info(ui);
        if (f) {
            shell.screen_w = (float)f->width;
            shell.screen_h = (float)f->height;
        }
        shell_poll_input(&shell);
        shell_update_interaction(&shell);
        draw_shell(ui, &shell);
        draw_done_ms = shell_now_ms();
        if (shell_stage_log_enabled() && startup_stage_logs < 8u) {
            f = rui_frame_info(ui);
            shell_log("[ridux-ui-shell] stage=");
            shell_log_u32(startup_stage_logs);
            shell_log(" draw done frame=");
            shell_log_u32(f ? f->frame : 0u);
            shell_log("\n");
            shell_log("[ridux-ui-shell] stage=");
            shell_log_u32(startup_stage_logs);
            shell_log(" present begin\n");
        }
        if (!rui_present(ui)) {
            shell_log("[ridux-ui-shell] present failed\n");
            rui_close(ui);
            return 3;
        }
        present_done_ms = shell_now_ms();
        last_draw_ms = (uint32_t)(draw_done_ms - frame_begin_ms);
        last_present_ms = (uint32_t)(present_done_ms - draw_done_ms);
        last_total_ms = (uint32_t)(present_done_ms - frame_begin_ms);
        f = rui_frame_info(ui);
        if (shell_stage_log_enabled() && startup_stage_logs < 8u) {
            shell_log("[ridux-ui-shell] stage=");
            shell_log_u32(startup_stage_logs);
            shell_log(" present done frame=");
            shell_log_u32(f ? f->frame : 0u);
            shell_log("\n");
            ++startup_stage_logs;
        }
        if (shell_frame_log_enabled() || startup_fps_logs < 8u) {
            uint64_t now_ms = shell_now_ms();
            if (!fps_last_ms) {
                fps_last_ms = now_ms;
                fps_last_frame = f->frame;
            }
            if (now_ms > fps_last_ms + 1000ULL) {
                uint32_t frames = f->frame - fps_last_frame;
                uint32_t elapsed = (uint32_t)(now_ms - fps_last_ms);
                uint32_t fps_x10 = elapsed ? (frames * 10000u) / elapsed : 0;
                fps_last_ms = now_ms;
                fps_last_frame = f->frame;
                {
                    char line[256];
                    int n = snprintf(line, sizeof(line),
                                     "[ridux-ui-shell] frame=%u fps=%u.%u draw_ms=%u present_ms=%u total_ms=%u swap_ms=%u lock_ms=%u addfb_ms=%u flip_ms=%u release_ms=%u renderer=%s size=%ux%u\n",
                                     f->frame, fps_x10 / 10u, fps_x10 % 10u,
                                     last_draw_ms, last_present_ms, last_total_ms,
                                     f->present_swap_ms, f->present_lock_ms,
                                     f->present_addfb_ms, f->present_flip_ms,
                                     f->present_release_ms,
                                     f->renderer ? f->renderer : "unknown",
                                     f->width, f->height);
                    if (n > 0) {
                        if ((uint32_t)n > sizeof(line) - 1u) n = (int)sizeof(line) - 1;
                        shell_write_all(line, (uint32_t)n);
                    }
                }
                if (!shell_frame_log_enabled())
                    ++startup_fps_logs;
            }
        } else if ((f->frame % 120u) == 1u) {
            fps_last_ms = 0;
            fps_last_frame = f->frame;
        }
        delay_us = shell_frame_delay_us();
        if (delay_us) usleep(delay_us);
    }
    return 0;
}
