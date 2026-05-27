#include "ridux_native_ui.h"
#include "ridux_surface_protocol.h"
#include "ridux_ui_components.hpp"
#include "injury_shell.hpp"
#include "injury_client_runtime.hpp"
#include "ridux_compositor.hpp"
#include "ridux_adwaita_theme.hpp"
#include "assets.h"

#include <errno.h>
#include <fcntl.h>
#ifdef __linux__
#include <linux/input.h>
#endif
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#ifndef EV_KEY
#define EV_KEY 0x01
#endif
#ifndef EV_REL
#define EV_REL 0x02
#endif
#ifndef EV_ABS
#define EV_ABS 0x03
#endif
#ifndef REL_X
#define REL_X 0x00
#endif
#ifndef REL_Y
#define REL_Y 0x01
#endif
#ifndef ABS_X
#define ABS_X 0x00
#endif
#ifndef ABS_Y
#define ABS_Y 0x01
#endif
#ifndef BTN_LEFT
#define BTN_LEFT 0x110
#endif
#ifndef BTN_TOUCH
#define BTN_TOUCH 0x14a
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

static int shell_hw_cursor_only_requested(void) {
    static int cached = -1;
    if (cached < 0)
        cached = (getenv("RIDUX_UI_ASSUME_HW_CURSOR_VISIBLE") ||
                  access("/etc/ridux-ui-hw-cursor-only.enable", F_OK) == 0) ? 1 : 0;
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

static float clampf(float v, float lo, float hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static float approachf(float v, float target, float rate) {
    return v + (target - v) * rate;
}

static float tri(uint64_t now_ms, uint32_t period_ms, uint32_t offset_ms) {
    uint32_t p;
    uint32_t half;
    if (!period_ms) return 0.0f;
    p = (uint32_t)((now_ms + offset_ms) % period_ms);
    half = period_ms / 2u;
    if (!half) return 0.0f;
    if (p > half) p = period_ms - p;
    return (float)p / (float)half;
}

static int hit(float px, float py, float x, float y, float w, float h) {
    return px >= x && py >= y && px < x + w && py < y + h;
}

static rui_color_t alpha(rui_color_t c, float a) {
    c.a = a;
    return c;
}

class Painter {
public:
    explicit Painter(rui_context_t *ctx) : ctx_(ctx) {}

    void rect(float x, float y, float w, float h, rui_color_t c) {
        if (w <= 0.0f || h <= 0.0f) return;
        rui_rect(ctx_, (rui_rect_t){ x, y, w, h }, c);
    }

    void round(float x, float y, float w, float h, float r, rui_color_t c) {
        if (w <= 0.0f || h <= 0.0f) return;
        rui_round_rect(ctx_, (rui_rect_t){ x, y, w, h }, r, c);
    }

    void round_gradient(float x, float y, float w, float h, float r,
                        rui_color_t top, rui_color_t bottom) {
        if (w <= 0.0f || h <= 0.0f) return;
        float max_r = (w < h ? w : h) * 0.5f;
        if (r < 0.0f) r = 0.0f;
        if (r > max_r) r = max_r;
        rui_round_rect_gradient(ctx_, (rui_rect_t){ x, y, w, h }, r, top, bottom);
    }

    void text(float x, float y, const char *value, float scale, rui_color_t color) {
        rui_text(ctx_, x, y, value, scale, color);
    }

    bool text_cached(float x, float y, const char *value, float scale, rui_color_t color) {
        return rui_text_cached(ctx_, x, y, value, scale, color);
    }

    void image(float x, float y, float w, float h, const ridux_image_t *image) {
        if (!image || !image->pixels || !image->width || !image->height) return;
        (void)rui_image_argb(ctx_, (rui_rect_t){ x, y, w, h },
                             image->width, image->height, image->pixels);
    }

    bool image_cached(float x, float y, float w, float h, const ridux_image_t *image) {
        if (!image || !image->pixels || !image->width || !image->height) return false;
        return rui_image_argb_cached(ctx_, (rui_rect_t){ x, y, w, h },
                                     image->width, image->height, image->pixels);
    }

    void preload_image(const ridux_image_t *image) {
        if (!image || !image->pixels || !image->width || !image->height) return;
        (void)rui_image_preload_argb(ctx_, image->width, image->height, image->pixels);
    }

    void preload_text(const char *value, float scale) {
        (void)rui_text_preload(ctx_, value, scale);
    }

    void gradient_y(float x, float y, float w, float h,
                    rui_color_t top, rui_color_t bottom, int steps) {
        if (w <= 0.0f || h <= 0.0f) return;
        if (steps < 1) steps = 1;
        float step_h = h / (float)steps;
        for (int i = 0; i < steps; ++i) {
            float t = steps == 1 ? 0.0f : (float)i / (float)(steps - 1);
            rui_color_t c = rui_rgba(top.r + (bottom.r - top.r) * t,
                                     top.g + (bottom.g - top.g) * t,
                                     top.b + (bottom.b - top.b) * t,
                                     top.a + (bottom.a - top.a) * t);
            rect(x, y + (float)i * step_h, w, step_h + 1.0f, c);
        }
    }

    void hairline(float x, float y, float w, float h, rui_color_t c) {
        rect(x, y, w, h, c);
    }

    void shadow(float x, float y, float w, float h, float r, float active) {
        round(x + 0.0f, y + 14.0f, w, h, r + 4.0f,
              rui_rgba(0.0f, 0.0f, 0.0f, 0.12f + active * 0.035f));
        round(x + 0.0f, y + 5.0f, w, h, r + 2.0f,
              rui_rgba(0.0f, 0.0f, 0.0f, 0.060f));
    }

    void glass(float x, float y, float w, float h, rui_color_t accent, float active) {
        float body = 0.58f + active * 0.08f;
        shadow(x, y, w, h, 18.0f, active);
        round(x - 1.0f, y - 1.0f, w + 2.0f, h + 2.0f, 19.0f,
              rui_rgba(1.0f, 1.0f, 1.0f, 0.055f + active * 0.035f));
        round_gradient(x, y, w, h, 18.0f,
                       rui_rgba(0.78f, 0.86f, 0.96f, 0.17f + active * 0.035f),
                       rui_rgba(0.06f, 0.075f, 0.095f, body));
        round(x + 16.0f, y + 10.0f, w - 32.0f, 1.0f, 0.5f,
              rui_rgba(1.0f, 1.0f, 1.0f, 0.085f));
        round(x + 18.0f, y + 55.0f, 92.0f, 2.0f, 1.0f,
              rui_rgba(accent.r, accent.g, accent.b, 0.32f + active * 0.20f));
        round(x + 18.0f, y + h - 5.0f, w - 36.0f, 1.0f, 0.5f,
              rui_rgba(0.0f, 0.0f, 0.0f, 0.16f));
    }

private:
    rui_context_t *ctx_;
};

struct InputState {
    static constexpr int MAX_DEVS = 16;
    int fds[MAX_DEVS];
    int abs_min_x[MAX_DEVS];
    int abs_max_x[MAX_DEVS];
    int abs_min_y[MAX_DEVS];
    int abs_max_y[MAX_DEVS];
    int fd_count = 0;
    int logged = 0;
    int move_logged = 0;
    float x = 512.0f;
    float y = 384.0f;
    int left = 0;
    int pressed = 0;
    int released = 0;

    InputState() {
        for (int i = 0; i < MAX_DEVS; ++i) {
            fds[i] = -1;
            abs_min_x[i] = 0;
            abs_max_x[i] = 32767;
            abs_min_y[i] = 0;
            abs_max_y[i] = 32767;
        }
    }

    static float map_abs(int value, int lo, int hi, float size) {
        if (hi > lo) {
            float t = ((float)value - (float)lo) / (float)(hi - lo);
            return clampf(t, 0.0f, 1.0f) * (size - 1.0f);
        }
        if ((float)value >= 0.0f && (float)value < size) return (float)value;
        return clampf((float)value / 32767.0f, 0.0f, 1.0f) * (size - 1.0f);
    }

    void probe_abs_ranges(int idx) {
#ifdef EVIOCGABS
        struct input_absinfo ax;
        struct input_absinfo ay;
        if (idx < 0 || idx >= MAX_DEVS) return;
        memset(&ax, 0, sizeof(ax));
        memset(&ay, 0, sizeof(ay));
        if (ioctl(fds[idx], EVIOCGABS(ABS_X), &ax) == 0 && ax.maximum > ax.minimum) {
            abs_min_x[idx] = ax.minimum;
            abs_max_x[idx] = ax.maximum;
        }
        if (ioctl(fds[idx], EVIOCGABS(ABS_Y), &ay) == 0 && ay.maximum > ay.minimum) {
            abs_min_y[idx] = ay.minimum;
            abs_max_y[idx] = ay.maximum;
        }
#else
        (void)idx;
#endif
    }

    void open_once() {
        char line[128];
        if (fd_count > 0 || logged) return;
        for (int i = 0; i < 32 && fd_count < MAX_DEVS; ++i) {
            char path[32];
            int fd;
            snprintf(path, sizeof(path), "/dev/input/event%d", i);
            fd = open(path, O_RDONLY | O_NONBLOCK);
            if (fd < 0) continue;
            fds[fd_count] = fd;
            probe_abs_ranges(fd_count);
            ++fd_count;
        }
        snprintf(line, sizeof(line),
                 "[ridux-ui-shell] input evdev-scan status=%s devices=%d\n",
                 fd_count > 0 ? "ok" : "unavailable", fd_count);
        shell_write_all(line, shell_len(line));
        logged = 1;
    }

    void poll(float screen_w, float screen_h) {
        struct input_event events[32];
        pressed = 0;
        released = 0;
        open_once();
        if (fd_count <= 0) return;
        for (int dev = 0; dev < fd_count;) {
            int keep = 1;
            for (int pass = 0; pass < 8; ++pass) {
                ssize_t got = read(fds[dev], events, sizeof(events));
                if (got <= 0) {
                    if (got < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
                        close(fds[dev]);
                        keep = 0;
                    }
                    break;
                }
                for (ssize_t i = 0; i < got / (ssize_t)sizeof(events[0]); ++i) {
                    if (events[i].type == EV_REL) {
                        if (events[i].code == REL_X && events[i].value) {
                            x += (float)events[i].value;
                            move_logged = move_logged ? move_logged : 1;
                        }
                        if (events[i].code == REL_Y && events[i].value) {
                            y += (float)events[i].value;
                            move_logged = move_logged ? move_logged : 1;
                        }
                    } else if (events[i].type == EV_ABS) {
                        if (events[i].code == ABS_X) {
                            x = map_abs(events[i].value, abs_min_x[dev], abs_max_x[dev], screen_w);
                            move_logged = move_logged ? move_logged : 2;
                        }
                        if (events[i].code == ABS_Y) {
                            y = map_abs(events[i].value, abs_min_y[dev], abs_max_y[dev], screen_h);
                            move_logged = move_logged ? move_logged : 2;
                        }
                    } else if (events[i].type == EV_KEY &&
                               (events[i].code == BTN_LEFT || events[i].code == BTN_TOUCH)) {
                        int down = events[i].value != 0;
                        if (down && !left) pressed = 1;
                        if (!down && left) released = 1;
                        left = down;
                    }
                }
            }
            if (!keep) {
                for (int j = dev; j < fd_count - 1; ++j) {
                    fds[j] = fds[j + 1];
                    abs_min_x[j] = abs_min_x[j + 1];
                    abs_max_x[j] = abs_max_x[j + 1];
                    abs_min_y[j] = abs_min_y[j + 1];
                    abs_max_y[j] = abs_max_y[j + 1];
                }
                --fd_count;
            } else {
                ++dev;
            }
        }
        if (move_logged > 0) {
            shell_log(move_logged == 1
                ? "[ridux-ui-shell] input pointer=relative status=active\n"
                : "[ridux-ui-shell] input pointer=absolute status=active\n");
            move_logged = -1;
        }
        x = clampf(x, 0.0f, screen_w - 1.0f);
        y = clampf(y, 0.0f, screen_h - 1.0f);
    }
};

enum AppId {
    APP_FILES = 0,
    APP_BROWSER,
    APP_TERMINAL,
    APP_SETTINGS,
    APP_STORE,
    APP_MONITOR,
    APP_NOTES,
    APP_COUNT
};

enum RiduxBarPanel {
    BAR_PANEL_NONE = 0,
    BAR_PANEL_LAUNCHER = 1,
    BAR_PANEL_QUICK = 2
};

struct AppWindow {
    const char *title = "";
    const char *glyph = "";
    int icon = RIDUX_ICON_START;
    rui_color_t accent = rui_rgb8(80, 180, 255);
    float x = 0.0f, y = 0.0f, w = 320.0f, h = 220.0f;
    float tx = 0.0f, ty = 0.0f, tw = 320.0f, th = 220.0f;
    float restore_x = 0.0f, restore_y = 0.0f, restore_w = 320.0f, restore_h = 220.0f;
    float show = 1.0f;
    uint32_t resource_frames = 0;
    uint32_t commit_serial = 0;
    bool open = true;
    bool minimized = false;
    bool maximized = false;
};

struct ShellApp {
    const char *label;
    int app;
    int icon;
};

static const ShellApp k_taskbar_apps[] = {
    { "Inicio", -1, RIDUX_ICON_START },
    { "Archivos", APP_FILES, RIDUX_ICON_FILES },
    { "Terminal", APP_TERMINAL, RIDUX_ICON_TERMINAL },
    { "Firefox", APP_BROWSER, RIDUX_ICON_FIREFOX },
    { "Notas", APP_NOTES, RIDUX_ICON_NOTES },
    { "Monitor", APP_MONITOR, RIDUX_ICON_MONITOR },
    { "Ajustes", APP_SETTINGS, RIDUX_ICON_SETTINGS },
};

static const ShellApp k_launcher_apps[] = {
    { "Archivos", APP_FILES, RIDUX_ICON_FILES },
    { "Terminal", APP_TERMINAL, RIDUX_ICON_TERMINAL },
    { "Ajustes", APP_SETTINGS, RIDUX_ICON_SETTINGS },
    { "Calculadora", APP_NOTES, RIDUX_ICON_CALC },
    { "Firefox", APP_BROWSER, RIDUX_ICON_FIREFOX },
    { "Acerca", APP_NOTES, RIDUX_ICON_ABOUT },
    { "Tienda", APP_STORE, RIDUX_ICON_STORE },
    { "Media", APP_NOTES, RIDUX_ICON_MEDIA },
    { "Monitor", APP_MONITOR, RIDUX_ICON_MONITOR },
    { "Pintar", APP_NOTES, RIDUX_ICON_PAINT },
    { "Clima", APP_NOTES, RIDUX_ICON_WEATHER },
    { "Notas", APP_NOTES, RIDUX_ICON_NOTES },
    { "Reloj", APP_NOTES, RIDUX_ICON_CLOCK },
    { "Procesos", APP_MONITOR, RIDUX_ICON_PROCESSES },
    { "Registro", APP_MONITOR, RIDUX_ICON_LOGVIEWER },
    { "Red", APP_SETTINGS, RIDUX_ICON_NETWORK },
    { "Sistema", APP_SETTINGS, RIDUX_ICON_SYSINFO },
    { "Juegos", APP_NOTES, RIDUX_ICON_TICTACTOE },
};

class DesktopShell {
public:
    void init(float w, float h) {
        screen_w_ = w;
        screen_h_ = h;
        input_.x = w * 0.50f;
        input_.y = h * 0.55f;
        setup_apps();
        setup_surfaces();
        for (int i = 0; i < APP_COUNT; ++i) z_[i] = i;
        focus_ = -1;
        input_.open_once();
    }

    void enable_hardware_cursor(rui_context_t *ui) {
        hw_cursor_ = false;
        draw_cursor_fallback_ = true;
        if (shell_hw_cursor_only_requested()) {
            hw_cursor_ = rui_cursor_enable_adwaita(ui, (int32_t)input_.x, (int32_t)input_.y);
            draw_cursor_fallback_ = !hw_cursor_;
            if (hw_cursor_) {
                shell_log("[ridux-ui-shell] cursor=drm-adwaita visible=hardware-only\n");
            } else {
                shell_log("[ridux-ui-shell] cursor=gl-adwaita visible=single fallback=drm-unavailable\n");
            }
            return;
        }
        shell_log("[ridux-ui-shell] cursor=gl-adwaita visible=single reason=qemu-drm-cursor-plane-unverified\n");
    }

    void update(rui_context_t *ui, const rui_frame_info_t *f) {
        if (f) {
            screen_w_ = (float)f->width;
            screen_h_ = (float)f->height;
        }
        input_.poll(screen_w_, screen_h_);
        if (hw_cursor_ && !rui_cursor_move(ui, (int32_t)input_.x, (int32_t)input_.y)) {
            hw_cursor_ = false;
            draw_cursor_fallback_ = true;
            shell_log("[ridux-ui-shell] cursor=drm-adwaita status=lost fallback=gl\n");
        }
        handle_pointer();
        animate_bar();
        for (int i = 0; i < APP_COUNT; ++i) animate_app(apps_[i], i);
        service_pending_open();
        clients_.tick();
        launcher_stager_.tick(bar_panel_render_ == BAR_PANEL_LAUNCHER && bar_panel_show_ > 0.04f);
        sync_surfaces();
    }

    void draw(rui_context_t *ui) {
        const rui_frame_info_t *f = rui_frame_info(ui);
        Painter p(ui);
        uint64_t now = shell_now_ms();
        rui_begin(ui, rui_rgb8(10, 12, 18));
        draw_wallpaper(p, now);
        for (int i = 0; i < scene_.count(); ++i) {
            const riduxcompositor::SceneNode &node = scene_.node(i);
            if (node.surface && node.surface->role == injury::SurfaceRole::Toplevel &&
                node.surface->app >= 0 && node.surface->app < APP_COUNT)
                draw_app(p, node.surface->app, f, now);
        }
        draw_bar_panel(p, f, now);
        draw_taskbar(p, f, now);
        if (draw_cursor_fallback_) {
            (void)rui_cursor_draw_adwaita(ui, (int32_t)input_.x, (int32_t)input_.y);
        }
    }

private:
    static constexpr float title_h_ = 46.0f;
    static constexpr float taskbar_h_ = 56.0f;
    static constexpr int shell_surface_count_ = APP_COUNT + 4;

    InputState input_;
    AppWindow apps_[APP_COUNT];
    injury::Surface surfaces_[shell_surface_count_];
    injury::ClientRuntime clients_;
    injury::ResourceStager launcher_stager_;
    riduxcompositor::SceneGraph scene_;
    riduxcompositor::InputRoute input_route_;
    int z_[APP_COUNT] = {};
    int focus_ = -1;
    int drag_ = -1;
    int resize_ = -1;
    float drag_dx_ = 0.0f;
    float drag_dy_ = 0.0f;
    float resize_origin_x_ = 0.0f;
    float resize_origin_y_ = 0.0f;
    float resize_origin_w_ = 0.0f;
    float resize_origin_h_ = 0.0f;
    float screen_w_ = 1024.0f;
    float screen_h_ = 768.0f;
    bool settings_glass_ = true;
    bool settings_motion_ = true;
    bool settings_gpu_ = true;
    riduxadwaita::Theme adwaita_ = riduxadwaita::default_theme();
    riduxui::Theme theme_ = adwaita_.to_riduxui();
    int workspace_ = 1;
    int bar_panel_ = BAR_PANEL_NONE;
    int bar_panel_render_ = BAR_PANEL_NONE;
    float bar_panel_show_ = 0.0f;
    float bar_pulse_ = 0.0f;
    bool hw_cursor_ = false;
    bool draw_cursor_fallback_ = true;
    int draw_debug_app_ = -1;
    int launcher_trace_left_ = 0;
    int launcher_open_frames_ = 0;
    int launcher_pressed_app_ = -1;
    int taskbar_pressed_index_ = -1;
    int pending_app_open_ = -1;
    uint32_t pending_app_frames_ = 0;
    uint32_t app_commit_serial_ = 1;
    uint32_t surface_generation_ = 1;
    uint32_t resource_warm_index_ = 0;

    void setup_apps() {
        float w = screen_w_;
        float h = screen_h_;
        apps_[APP_FILES] = make_app("Archivos", "F", RIDUX_ICON_FILES, rui_rgb8(72, 170, 255),
                                    78.0f, 86.0f, 390.0f, 330.0f, false);
        apps_[APP_BROWSER] = make_app("Firefox", "B", RIDUX_ICON_FIREFOX, rui_rgb8(110, 208, 255),
                                      330.0f, 74.0f, 470.0f, 344.0f, false);
        apps_[APP_TERMINAL] = make_app("Terminal", "T", RIDUX_ICON_TERMINAL, rui_rgb8(108, 230, 166),
                                       w - 374.0f, 132.0f, 326.0f, 296.0f, false);
        apps_[APP_SETTINGS] = make_app("Ajustes", "S", RIDUX_ICON_SETTINGS, rui_rgb8(255, 196, 86),
                                       104.0f, h - 260.0f, 430.0f, 182.0f, false);
        apps_[APP_STORE] = make_app("Tienda", "P", RIDUX_ICON_STORE, rui_rgb8(232, 112, 170),
                                    178.0f, 116.0f, 388.0f, 296.0f, false);
        apps_[APP_MONITOR] = make_app("Monitor", "M", RIDUX_ICON_MONITOR, rui_rgb8(160, 128, 255),
                                      w - 424.0f, h - 292.0f, 376.0f, 216.0f, false);
        apps_[APP_NOTES] = make_app("Notas", "N", RIDUX_ICON_NOTES, rui_rgb8(246, 212, 96),
                                    w * 0.42f, h - 292.0f, 342.0f, 214.0f, false);
    }

    void setup_surfaces() {
        clients_.clear();
        surfaces_[0].id = injury::SURFACE_TASKBAR;
        surfaces_[0].role = injury::SurfaceRole::Taskbar;
        surfaces_[0].name = "injury-taskbar";
        surfaces_[0].visible = true;
        clients_.create(injury::ClientKind::ShellBar, &surfaces_[0]);

        surfaces_[1].id = injury::SURFACE_LAUNCHER;
        surfaces_[1].role = injury::SurfaceRole::Panel;
        surfaces_[1].name = "injury-launcher";
        clients_.create(injury::ClientKind::Launcher, &surfaces_[1]);

        surfaces_[2].id = injury::SURFACE_QUICK_PANEL;
        surfaces_[2].role = injury::SurfaceRole::Panel;
        surfaces_[2].name = "injury-quick-panel";
        clients_.create(injury::ClientKind::QuickSettings, &surfaces_[2]);

        surfaces_[3].id = injury::SURFACE_SEARCH;
        surfaces_[3].role = injury::SurfaceRole::Search;
        surfaces_[3].name = "injury-search";
        clients_.create(injury::ClientKind::Launcher, &surfaces_[3]);

        for (int i = 0; i < APP_COUNT; ++i) {
            injury::Surface &s = surfaces_[4 + i];
            s.id = injury::SURFACE_FIRST_APP + (uint32_t)i;
            s.role = injury::SurfaceRole::Toplevel;
            s.app = i;
            s.name = apps_[i].title;
            clients_.create(injury::ClientKind::Application, &s);
        }
        launcher_stager_.reset(surface_generation_++);
        sync_surfaces();
        shell_log("[injury-shell] surfaces=taskbar,launcher,quick-panel,search,toplevel-apps protocol=Injury clients=isolated-wayland-ready\n");
    }

    void sync_surfaces() {
        surfaces_[0].rect = { 0.0f, screen_h_ - taskbar_h_, screen_w_, taskbar_h_ };
        surfaces_[0].effects = { 0.0f, 22.0f, 0.16f, 0.76f };
        surfaces_[0].visible = true;

        surfaces_[1].rect = { launcher_x(), launcher_y(), launcher_w(), launcher_h() };
        surfaces_[1].effects = { 24.0f, 28.0f, 0.20f, 0.74f };
        surfaces_[1].visible = (bar_panel_render_ == BAR_PANEL_LAUNCHER && bar_panel_show_ > 0.01f);
        if (surfaces_[1].visible) clients_.map(&surfaces_[1]);
        else clients_.unmap(&surfaces_[1]);

        surfaces_[2].rect = { screen_w_ - 378.0f, screen_h_ - taskbar_h_ - 312.0f, 354.0f, 298.0f };
        surfaces_[2].effects = { 26.0f, 26.0f, 0.18f, 0.72f };
        surfaces_[2].visible = (bar_panel_render_ == BAR_PANEL_QUICK && bar_panel_show_ > 0.01f);
        if (surfaces_[2].visible) clients_.map(&surfaces_[2]);
        else clients_.unmap(&surfaces_[2]);

        surfaces_[3].rect = { launcher_x() + 32.0f, launcher_y() + 26.0f, launcher_w() - 64.0f, 42.0f };
        surfaces_[3].effects = { 14.0f, 18.0f, 0.10f, 0.65f };
        surfaces_[3].visible = surfaces_[1].visible;
        if (surfaces_[3].visible) clients_.map(&surfaces_[3]);
        else clients_.unmap(&surfaces_[3]);

        for (int i = 0; i < APP_COUNT; ++i) {
            injury::Surface &s = surfaces_[4 + i];
            const AppWindow &a = apps_[i];
            s.rect = { a.x, a.y, a.w, a.h };
            s.effects = { 18.0f, 24.0f, (i == focus_) ? 0.18f : 0.12f, settings_glass_ ? 0.72f : 0.0f };
            s.visible = (a.open && !a.minimized && a.show > 0.025f &&
                         a.resource_frames >= 2u);
            if (s.visible) clients_.map(&s);
            else clients_.unmap(&s);
        }
        rebuild_scene();
    }

    void rebuild_scene() {
        scene_.clear();
        for (int z = 0; z < APP_COUNT; ++z) {
            int idx = z_[z];
            const injury::Surface &s = surfaces_[4 + idx];
            scene_.push(&s, apps_[idx].show, (uint32_t)(20 + z));
        }
        scene_.push(&surfaces_[1], bar_panel_show_, 100u);
        scene_.push(&surfaces_[2], bar_panel_show_, 101u);
        scene_.push(&surfaces_[0], 1.0f, 200u);
    }

    AppWindow make_app(const char *title, const char *glyph, int icon,
                       rui_color_t accent, float x, float y, float w, float h,
                       bool open) {
        AppWindow a;
        a.title = title;
        a.glyph = glyph;
        a.icon = icon;
        a.accent = accent;
        a.x = a.tx = a.restore_x = x;
        a.y = a.ty = a.restore_y = y;
        a.w = a.tw = a.restore_w = w;
        a.h = a.th = a.restore_h = h;
        a.open = open;
        a.minimized = !open;
        a.show = open ? 1.0f : 0.0f;
        return a;
    }

    void bring_front(int idx) {
        int pos = -1;
        for (int i = 0; i < APP_COUNT; ++i) {
            if (z_[i] == idx) {
                pos = i;
                break;
            }
        }
        if (pos < 0) return;
        for (int i = pos; i < APP_COUNT - 1; ++i) z_[i] = z_[i + 1];
        z_[APP_COUNT - 1] = idx;
        focus_ = idx;
    }

    void open_app(int idx) {
        if (idx < 0 || idx >= APP_COUNT) return;
        bool cold_start = !apps_[idx].open || apps_[idx].minimized ||
                          apps_[idx].show < 0.05f;
        if (cold_start) {
            apps_[idx].resource_frames = 0;
            apps_[idx].show = 0.0f;
            apps_[idx].commit_serial = app_commit_serial_++;
        }
        apps_[idx].open = true;
        apps_[idx].minimized = false;
        bring_front(idx);
        draw_debug_app_ = idx;
        shell_log("[ridux-ui-shell] app-open committed path=Injury/RiduxSurface\n");
    }

    void request_open_app(int idx, const char *source) {
        if (idx < 0 || idx >= APP_COUNT) return;
        pending_app_open_ = idx;
        pending_app_frames_ = 0;
        drag_ = -1;
        resize_ = -1;
        close_bar_panel();
        shell_log("[injury-shell] app-open queued source=");
        shell_log(source ? source : "unknown");
        shell_log("\n");
    }

    void service_pending_open() {
        if (pending_app_open_ < 0) return;
        if (pending_app_frames_ < 1000000u) ++pending_app_frames_;
        if (bar_panel_ != BAR_PANEL_NONE || bar_panel_render_ != BAR_PANEL_NONE ||
            bar_panel_show_ > 0.025f)
            return;
        int idx = pending_app_open_;
        pending_app_open_ = -1;
        pending_app_frames_ = 0;
        open_app(idx);
    }

    void focus_next_open(void) {
        for (int z = APP_COUNT - 1; z >= 0; --z) {
            int idx = z_[z];
            if (apps_[idx].open && !apps_[idx].minimized) {
                focus_ = idx;
                return;
            }
        }
        focus_ = -1;
    }

    void close_app(int idx) {
        if (idx < 0 || idx >= APP_COUNT) return;
        apps_[idx].open = false;
        apps_[idx].minimized = false;
        apps_[idx].maximized = false;
        apps_[idx].resource_frames = 0;
        drag_ = -1;
        resize_ = -1;
        if (focus_ == idx) focus_next_open();
        shell_log("[ridux-ui-shell] window-control=close action=hide-window\n");
    }

    void toggle_maximize(int idx) {
        AppWindow &a = apps_[idx];
        if (!a.maximized) {
            a.restore_x = a.tx;
            a.restore_y = a.ty;
            a.restore_w = a.tw;
            a.restore_h = a.th;
            a.tx = 14.0f;
            a.ty = 18.0f;
            a.tw = screen_w_ - 28.0f;
            a.th = screen_h_ - taskbar_h_ - 36.0f;
            a.maximized = true;
        } else {
            a.tx = a.restore_x;
            a.ty = a.restore_y;
            a.tw = a.restore_w;
            a.th = a.restore_h;
            a.maximized = false;
        }
    }

    void minimize_to_dock(int idx) {
        apps_[idx].open = true;
        apps_[idx].minimized = true;
        apps_[idx].maximized = false;
        drag_ = -1;
        resize_ = -1;
        if (focus_ == idx) focus_next_open();
        shell_log("[ridux-ui-shell] window-control=minimize action=taskbar\n");
    }

    void handle_pointer() {
        if (input_.released) {
            if (launcher_pressed_app_ >= 0) {
                int pressed = launcher_pressed_app_;
                int released = launcher_app_at(input_.x, input_.y);
                launcher_pressed_app_ = -1;
                if (released == pressed) {
                    request_open_app(pressed, "launcher-release");
                    shell_log("[injury-shell] launcher app-open\n");
                    return;
                }
            }
            if (taskbar_pressed_index_ >= 0) {
                int pressed = taskbar_pressed_index_;
                int released = taskbar_index_at(input_.x, input_.y);
                taskbar_pressed_index_ = -1;
                if (released == pressed) {
                    int app = k_taskbar_apps[pressed].app;
                    if (app >= 0 && app < APP_COUNT) {
                        request_open_app(app, "taskbar-release");
                        return;
                    }
                }
            }
        }
        if (input_.pressed) {
            if (handle_bar_click()) return;
            int taskbar_hit = taskbar_index_at(input_.x, input_.y);
            if (taskbar_hit >= 0) {
                int app = k_taskbar_apps[taskbar_hit].app;
                if (app >= 0 && app < APP_COUNT) {
                    taskbar_pressed_index_ = taskbar_hit;
                } else {
                    toggle_bar_panel(BAR_PANEL_LAUNCHER);
                    shell_log("[injury-shell] taskbar launcher toggle\n");
                }
                return;
            }

            drag_ = -1;
            launcher_pressed_app_ = -1;
            taskbar_pressed_index_ = -1;
            for (int z = APP_COUNT - 1; z >= 0; --z) {
                int idx = z_[z];
                AppWindow &a = apps_[idx];
                if (!a.open || a.show < 0.05f) continue;
                if (!hit(input_.x, input_.y, a.x, a.y, a.w, a.h)) continue;
                bring_front(idx);
                if (hit(input_.x, input_.y, a.x + 16.0f, a.y + 17.0f, 13.0f, 13.0f)) {
                    close_app(idx);
                    return;
                }
                if (hit(input_.x, input_.y, a.x + 38.0f, a.y + 17.0f, 13.0f, 13.0f)) {
                    minimize_to_dock(idx);
                    return;
                }
                if (hit(input_.x, input_.y, a.x + 60.0f, a.y + 17.0f, 13.0f, 13.0f)) {
                    toggle_maximize(idx);
                    shell_log("[ridux-ui-shell] window-control=maximize action=toggle\n");
                    return;
                }
                if (idx == APP_SETTINGS && handle_settings_click(a)) return;
                if (!a.maximized && hit(input_.x, input_.y,
                                        a.x + a.w - 28.0f, a.y + a.h - 28.0f,
                                        24.0f, 24.0f)) {
                    resize_ = idx;
                    resize_origin_x_ = input_.x;
                    resize_origin_y_ = input_.y;
                    resize_origin_w_ = a.tw;
                    resize_origin_h_ = a.th;
                    drag_ = -1;
                    return;
                }
                if (hit(input_.x, input_.y, a.x, a.y, a.w, title_h_)) {
                    drag_ = idx;
                    resize_ = -1;
                    drag_dx_ = input_.x - a.tx;
                    drag_dy_ = input_.y - a.ty;
                    return;
                }
                return;
            }
        }

        if (input_.left && drag_ >= 0) {
            AppWindow &a = apps_[drag_];
            if (!a.maximized) {
                float max_x = screen_w_ - a.tw - 10.0f;
                float max_y = screen_h_ - a.th - taskbar_h_ - 8.0f;
                a.tx = clampf(input_.x - drag_dx_, 8.0f, max_x > 8.0f ? max_x : 8.0f);
                a.ty = clampf(input_.y - drag_dy_, 10.0f, max_y > 10.0f ? max_y : 10.0f);
            }
        }
        if (input_.left && resize_ >= 0) {
            AppWindow &a = apps_[resize_];
            float max_w = screen_w_ - a.tx - 10.0f;
            float max_h = screen_h_ - taskbar_h_ - a.ty - 12.0f;
            a.tw = clampf(resize_origin_w_ + input_.x - resize_origin_x_, 280.0f,
                          max_w > 280.0f ? max_w : 280.0f);
            a.th = clampf(resize_origin_h_ + input_.y - resize_origin_y_, 150.0f,
                          max_h > 150.0f ? max_h : 150.0f);
        }
        if (input_.released) {
            drag_ = -1;
            resize_ = -1;
            launcher_pressed_app_ = -1;
            taskbar_pressed_index_ = -1;
        }
    }

    bool handle_bar_click() {
        float tb_y = screen_h_ - taskbar_h_;
        float start_x = taskbar_start_x();
        if (hit(input_.x, input_.y, start_x, tb_y + 7.0f, 44.0f, 42.0f)) {
            toggle_bar_panel(BAR_PANEL_LAUNCHER);
            shell_log("[injury-shell] taskbar start toggle\n");
            return true;
        }
        if (bar_panel_show_ > 0.18f && bar_panel_ == BAR_PANEL_LAUNCHER) {
            int app = launcher_app_at(input_.x, input_.y);
            if (app >= 0 && app < APP_COUNT) {
                launcher_pressed_app_ = app;
                return true;
            }
            if (hit(input_.x, input_.y, launcher_x(), launcher_y(), launcher_w(), launcher_h()))
                return true;
        }
        if (hit(input_.x, input_.y, screen_w_ - 190.0f, tb_y + 7.0f, 178.0f, 42.0f)) {
            toggle_bar_panel(BAR_PANEL_QUICK);
            shell_log("[injury-shell] taskbar quick toggle\n");
            return true;
        }
        if (bar_panel_show_ > 0.18f && bar_panel_ == BAR_PANEL_QUICK &&
            hit(input_.x, input_.y, screen_w_ - 378.0f,
                screen_h_ - taskbar_h_ - 312.0f, 354.0f, 298.0f)) {
            return true;
        }
        if (bar_panel_ != BAR_PANEL_NONE && input_.y < screen_h_ - taskbar_h_ - 330.0f) {
            close_bar_panel();
            return false;
        }
        return false;
    }

    void close_bar_panel() {
        bar_panel_ = BAR_PANEL_NONE;
    }

    void toggle_bar_panel(int panel) {
        bar_panel_ = (bar_panel_ == panel) ? BAR_PANEL_NONE : panel;
        if (bar_panel_ == BAR_PANEL_LAUNCHER) {
            bar_panel_render_ = BAR_PANEL_LAUNCHER;
            launcher_open_frames_ = 0;
            launcher_trace_left_ = 8;
            launcher_stager_.reset(surface_generation_++);
        } else if (bar_panel_ == BAR_PANEL_QUICK) {
            bar_panel_render_ = BAR_PANEL_QUICK;
        }
    }

    int launcher_visible_app_count() const {
        int launcher_count = (int)(sizeof(k_launcher_apps) / sizeof(k_launcher_apps[0]));
        return launcher_stager_.visible_items(launcher_count);
    }

    int launcher_app_at(float mx, float my) const {
        float panel_x = launcher_x();
        float grid_x = panel_x + 32.0f;
        float grid_y = launcher_y() + 118.0f;
        float grid_w = launcher_w() - 64.0f;
        float cols = 4.0f;
        float gap = 10.0f;
        float cell_w = (grid_w - gap * (cols - 1.0f)) / cols;
        float cell_h = 74.0f;
        int visible_apps = launcher_visible_app_count();
        for (int i = 0; i < visible_apps; ++i) {
            int col = i % 4;
            int row = i / 4;
            float x = grid_x + (float)col * (cell_w + gap);
            float y = grid_y + (float)row * (cell_h + gap);
            if (hit(mx, my, x, y, cell_w, cell_h)) {
                int app = k_launcher_apps[i].app;
                return (app >= 0 && app < APP_COUNT) ? app : -1;
            }
        }
        return -1;
    }

    bool handle_settings_click(const AppWindow &a) {
        float y0 = a.y + 76.0f;
        for (int i = 0; i < 3; ++i) {
            if (hit(input_.x, input_.y, a.x + a.w - 86.0f, y0 + (float)i * 34.0f, 48.0f, 20.0f)) {
                if (i == 0) settings_glass_ = !settings_glass_;
                if (i == 1) settings_motion_ = !settings_motion_;
                if (i == 2) settings_gpu_ = !settings_gpu_;
                return true;
            }
        }
        return false;
    }

    float launcher_w() const { return 612.0f; }
    float launcher_h() const { return 458.0f; }
    float launcher_x() const { return (screen_w_ - launcher_w()) * 0.5f; }
    float launcher_y() const { return screen_h_ - taskbar_h_ - launcher_h() - 14.0f; }

    float taskbar_group_w() const {
        int count = (int)(sizeof(k_taskbar_apps) / sizeof(k_taskbar_apps[0]));
        return 44.0f * (float)count + 8.0f * (float)(count - 1);
    }

    float taskbar_start_x() const {
        return (screen_w_ - taskbar_group_w()) * 0.5f;
    }

    int taskbar_index_at(float mx, float my) const {
        int count = (int)(sizeof(k_taskbar_apps) / sizeof(k_taskbar_apps[0]));
        float item = 44.0f;
        float gap = 8.0f;
        float start = taskbar_start_x();
        float y = screen_h_ - taskbar_h_ + 7.0f;
        for (int i = 0; i < count; ++i) {
            float x = start + (item + gap) * (float)i;
            if (hit(mx, my, x, y, item, 42.0f)) return i;
        }
        return -1;
    }

    void taskbar_rect_for(int idx, float *x, float *y, float *w, float *h) const {
        int count = (int)(sizeof(k_taskbar_apps) / sizeof(k_taskbar_apps[0]));
        float item = 44.0f;
        float gap = 8.0f;
        float start = taskbar_start_x();
        int slot = 0;
        for (int i = 0; i < count; ++i) {
            if (k_taskbar_apps[i].app == idx) {
                slot = i;
                break;
            }
        }
        *x = start + (item + gap) * (float)slot + 5.0f;
        *y = screen_h_ - taskbar_h_ + 13.0f;
        *w = 34.0f;
        *h = 30.0f;
    }

    void animate_app(AppWindow &a, int idx) {
        float dx, dy, dw, dh;
        float geo_rate = settings_motion_ ? 0.22f : 1.0f;
        float show_rate = settings_motion_ ? 0.18f : 1.0f;
        float target_show = (a.open && !a.minimized) ? 1.0f : 0.0f;
        if (a.open && !a.minimized && a.show > 0.04f) {
            if (a.resource_frames < 1000000u) ++a.resource_frames;
        } else {
            a.resource_frames = 0;
        }
        if (a.minimized) {
            taskbar_rect_for(idx, &dx, &dy, &dw, &dh);
        } else {
            dx = a.tx; dy = a.ty; dw = a.tw; dh = a.th;
        }
        a.x = approachf(a.x, dx, geo_rate);
        a.y = approachf(a.y, dy, geo_rate);
        a.w = approachf(a.w, dw, geo_rate);
        a.h = approachf(a.h, dh, geo_rate);
        a.show = approachf(a.show, target_show, show_rate);
    }

    void animate_bar() {
        if (bar_panel_ != BAR_PANEL_NONE)
            bar_panel_render_ = bar_panel_;
        float target = (bar_panel_ == BAR_PANEL_NONE) ? 0.0f : 1.0f;
        float rate = settings_motion_ ? 0.20f : 1.0f;
        bar_panel_show_ = approachf(bar_panel_show_, target, rate);
        if (bar_panel_render_ == BAR_PANEL_LAUNCHER && bar_panel_show_ > 0.01f)
            ++launcher_open_frames_;
        else if (bar_panel_render_ != BAR_PANEL_LAUNCHER)
            launcher_open_frames_ = 0;
        bar_pulse_ = approachf(bar_pulse_, input_.y > screen_h_ - taskbar_h_ - 10.0f ? 1.0f : 0.0f,
                               settings_motion_ ? 0.16f : 1.0f);
        if (bar_panel_show_ < 0.025f && bar_panel_ == BAR_PANEL_NONE) {
            bar_panel_show_ = 0.0f;
            if (bar_panel_render_ == BAR_PANEL_LAUNCHER)
                launcher_stager_.reset(surface_generation_++);
            bar_panel_render_ = BAR_PANEL_NONE;
        }
    }

    bool any_window_visible() const {
        for (int i = 0; i < APP_COUNT; ++i) {
            if (apps_[i].open && !apps_[i].minimized && apps_[i].show > 0.08f)
                return true;
        }
        return false;
    }

    static float text_est(const char *text, float scale) {
        return (float)shell_len(text ? text : "") * 7.4f * scale;
    }

    void text_center(Painter &p, float x, float y, float w, const char *text,
                     float scale, rui_color_t color) {
        p.text(x + (w - text_est(text, scale)) * 0.5f, y, text, scale, color);
    }

    void text_right(Painter &p, float right, float y, const char *text,
                    float scale, rui_color_t color) {
        p.text(right - text_est(text, scale), y, text, scale, color);
    }

    void draw_icon(Painter &p, int icon, float x, float y, float size) {
        if (icon >= 0 && icon < RIDUX_ICON_COUNT)
            p.image(x, y, size, size, &RIDUX_ICONS[icon]);
    }

    void prewarm_shell_resources(Painter &p) {
        struct TextWarm {
            const char *text;
            float scale;
        };
        static const TextWarm texts[] = {
            { "riduxOS", 2.48f },
            { "Inicio", 0.70f },
            { "Archivos", 0.82f },
            { "Terminal", 0.82f },
            { "Firefox", 0.82f },
            { "Notas", 0.82f },
            { "Monitor", 0.82f },
            { "Ajustes", 0.82f },
            { "Buscar en Injury", 0.92f },
            { "Ancladas", 0.98f },
            { "Todas", 0.76f },
            { "En ejecucion", 0.68f },
            { "Aplicacion", 0.68f },
            { "Recomendado", 0.94f },
            { "Injury", 0.88f },
            { "sesion local", 0.74f },
            { "IO", 0.72f },
            { "Controles", 0.98f },
            { "RiduxOS", 0.82f },
            { "Wi-Fi", 0.94f },
            { "Bluetooth", 0.94f },
            { "Audio", 0.94f },
            { "Energia", 0.94f },
            { "Activo", 0.80f },
            { "Listo", 0.80f },
            { "78%", 0.80f },
            { "87%", 0.80f },
            { "Mesa GL/VirGL listo", 0.76f },
            { "Render", 0.72f },
            { "VirGL", 0.70f },
        };
        if (RIDUX_WALLPAPER_COUNT > 0)
            p.preload_image(&RIDUX_WALLPAPERS[0]);
        if (RIDUX_ICON_COUNT > 0) {
            uint32_t icon = resource_warm_index_ % (uint32_t)RIDUX_ICON_COUNT;
            p.preload_image(&RIDUX_ICONS[icon]);
        }
        if (sizeof(texts) / sizeof(texts[0]) > 0) {
            uint32_t text = resource_warm_index_ % (uint32_t)(sizeof(texts) / sizeof(texts[0]));
            p.preload_text(texts[text].text, texts[text].scale);
        }
        ++resource_warm_index_;
    }

    void draw_app_grid_glyph(Painter &p, float x, float y, float size,
                             rui_color_t color, float opacity) {
        float dot = size / 6.0f;
        float step;
        if (dot < 3.0f) dot = 3.0f;
        step = (size - dot) / 2.0f;
        for (int row = 0; row < 3; ++row) {
            for (int col = 0; col < 3; ++col) {
                p.round(x + (float)col * step, y + (float)row * step,
                        dot, dot, dot * 0.5f, alpha(color, opacity));
            }
        }
    }

    void panel_text(Painter &p, float x, float y, const char *text,
                    float scale, rui_color_t color, float fallback_w) {
        if (p.text_cached(x, y, text, scale, color)) return;
        p.round(x, y + 6.0f, fallback_w, 7.0f, 3.5f,
                rui_rgba(1.0f, 1.0f, 1.0f, 0.075f));
    }

    void panel_text_center(Painter &p, float x, float y, float w, const char *text,
                           float scale, rui_color_t color) {
        float tx = x + (w - text_est(text, scale)) * 0.5f;
        if (p.text_cached(tx, y, text, scale, color)) return;
        p.round(x + w * 0.25f, y + 6.0f, w * 0.50f, 7.0f, 3.5f,
                rui_rgba(1.0f, 1.0f, 1.0f, 0.075f));
    }

    void draw_icon_cached_or_placeholder(Painter &p, int icon, float x, float y,
                                         float size, rui_color_t accent) {
        if (icon >= 0 && icon < RIDUX_ICON_COUNT &&
            p.image_cached(x, y, size, size, &RIDUX_ICONS[icon]))
            return;
        p.round(x, y, size, size, size * 0.25f,
                rui_rgba(accent.r, accent.g, accent.b, 0.18f));
        draw_app_grid_glyph(p, x + size * 0.26f, y + size * 0.26f,
                            size * 0.48f, rui_rgb8(230, 242, 255), 0.42f);
    }

    void draw_wallpaper(Painter &p, uint64_t now) {
        float glow = 0.018f;
        (void)now;
        if (RIDUX_WALLPAPER_COUNT > 0)
            p.image(0.0f, 0.0f, screen_w_, screen_h_, &RIDUX_WALLPAPERS[0]);
        else
            p.rect(0.0f, 0.0f, screen_w_, screen_h_, rui_rgb8(9, 11, 15));
        p.rect(0.0f, 0.0f, screen_w_, screen_h_, rui_rgba(3.0f / 255.0f, 8.0f / 255.0f, 18.0f / 255.0f, 0.06f));
        p.round(screen_w_ * 0.50f - 160.0f, screen_h_ * 0.50f - 126.0f,
                320.0f, 220.0f, 62.0f, rui_rgba(0.12f, 0.42f, 0.72f, glow));
    }

    void draw_desktop_brand(Painter &p, uint64_t now) {
        (void)now;
        float cx = screen_w_ * 0.5f;
        float cy = screen_h_ * 0.52f;
        p.round(cx - 74.0f, cy - 92.0f, 148.0f, 148.0f, 34.0f,
                rui_rgba(0.0f, 0.0f, 0.0f, 0.16f));
        p.round_gradient(cx - 60.0f, cy - 108.0f, 120.0f, 120.0f, 28.0f,
                         rui_rgba(0.18f, 0.56f, 1.0f, 0.24f),
                         rui_rgba(0.05f, 0.74f, 0.52f, 0.24f));
        p.round(cx - 56.0f, cy - 104.0f, 36.0f, 36.0f, 3.0f,
                rui_rgba(0.04f, 0.18f, 0.88f, 0.72f));
        p.round(cx - 16.0f, cy - 104.0f, 36.0f, 36.0f, 3.0f,
                rui_rgba(0.06f, 0.42f, 1.0f, 0.76f));
        p.round(cx - 56.0f, cy - 64.0f, 36.0f, 36.0f, 3.0f,
                rui_rgba(0.11f, 0.74f, 0.42f, 0.76f));
        p.round(cx - 16.0f, cy - 64.0f, 36.0f, 36.0f, 3.0f,
                rui_rgba(0.74f, 0.88f, 0.20f, 0.76f));
        p.text(cx - 80.0f, cy + 28.0f, "riduxOS", 2.48f,
               rui_rgba(0.94f, 0.97f, 1.0f, 0.86f));
    }

    void draw_acrylic_texture(Painter &p, float x, float y, float w, float h, float intensity) {
        p.round(x + 12.0f, y + 7.0f, w - 24.0f, 1.0f, 0.5f,
                rui_rgba(1.0f, 1.0f, 1.0f, 0.10f * intensity));
        p.round(x + 16.0f, y + h - 8.0f, w - 32.0f, 1.0f, 0.5f,
                rui_rgba(0.0f, 0.0f, 0.0f, 0.16f * intensity));
    }

    void draw_ridux_bar(Painter &p, const rui_frame_info_t *f, uint64_t now) {
        (void)f;
        float hover = bar_pulse_;
        p.round_gradient(0.0f, 0.0f, screen_w_, 30.0f, 0.0f,
                         rui_rgba(8.0f / 255.0f, 13.0f / 255.0f, 22.0f / 255.0f, 0.92f + hover * 0.02f),
                         rui_rgba(5.0f / 255.0f, 8.0f / 255.0f, 14.0f / 255.0f, 0.94f));
        p.rect(0.0f, 0.0f, screen_w_, 1.0f, rui_rgba(1.0f, 1.0f, 1.0f, 0.055f));
        p.rect(0.0f, 29.0f, screen_w_, 1.0f, rui_rgba(1.0f, 1.0f, 1.0f, 0.070f));
        p.rect(0.0f, 30.0f, screen_w_, 1.0f, rui_rgba(0.0f, 0.0f, 0.0f, 0.28f));
        draw_icon(p, RIDUX_ICON_START, 8.0f, 3.0f, 24.0f);
        p.text(38.0f, 8.0f, "RiduxOS", 0.92f, rui_rgb8(244, 249, 255));
        p.text(112.0f, 8.0f, "Archivo", 0.86f, rui_rgb8(222, 231, 241));
        p.text(176.0f, 8.0f, "Editar", 0.86f, rui_rgb8(222, 231, 241));
        p.text(232.0f, 8.0f, "Ver", 0.86f, rui_rgb8(222, 231, 241));
        p.text(274.0f, 8.0f, "Ventanas", 0.86f, rui_rgb8(222, 231, 241));
        p.text(350.0f, 8.0f, "Ayuda", 0.86f, rui_rgb8(222, 231, 241));
        p.round(414.0f, 5.0f, 62.0f, 20.0f, 10.0f,
                rui_rgba(35.0f / 255.0f, 190.0f / 255.0f, 124.0f / 255.0f, 0.24f));
        text_center(p, 414.0f, 7.0f, 62.0f, "VirGL", 0.78f, rui_rgb8(216, 255, 235));
        draw_bar_clock(p, now, screen_w_ * 0.5f - 24.0f, 8.0f);
        text_right(p, screen_w_ - 214.0f, 8.0f, "22/05/2026", 0.82f, rui_rgb8(202, 214, 226));
        draw_icon(p, RIDUX_ICON_TRAY_VOLUME, screen_w_ - 170.0f, 6.0f, 18.0f);
        draw_icon(p, RIDUX_ICON_TRAY_NETWORK, screen_w_ - 136.0f, 6.0f, 18.0f);
        draw_icon(p, RIDUX_ICON_TRAY_BATTERY, screen_w_ - 100.0f, 6.0f, 18.0f);
        p.text(screen_w_ - 70.0f, 8.0f, "87%", 0.82f, rui_rgb8(232, 242, 251));
        p.text(screen_w_ - 28.0f, 8.0f, "O", 0.82f, rui_rgb8(232, 242, 251));
        draw_bar_panel(p, f, now);
    }

    void draw_workspaces(Painter &p) {
        for (int i = 0; i < 4; ++i) {
            float x = 184.0f + (float)i * 52.0f;
            bool active = workspace_ == i + 1;
            p.round(x, 14.0f, 44.0f, 40.0f, 9.0f,
                    active ? rui_rgba(0.34f, 0.72f, 1.0f, 0.72f) :
                             rui_rgba(1.0f, 1.0f, 1.0f, 0.00f));
            char label[2] = { (char)('1' + i), 0 };
            p.text(x + 19.0f, 26.0f, label, 1.02f,
                   active ? rui_rgb8(255, 255, 255) : rui_rgb8(230, 240, 255));
        }
    }

    void draw_bar_clock(Painter &p, uint64_t now, float x, float y) {
        uint32_t s = (uint32_t)((now / 1000u) % 86400u);
        uint32_t minute = (s / 60u) % 60u;
        uint32_t hour = (s / 3600u + 10u) % 24u;
        char clock[6];
        clock[0] = (char)('0' + (hour / 10u));
        clock[1] = (char)('0' + (hour % 10u));
        clock[2] = ':';
        clock[3] = (char)('0' + (minute / 10u));
        clock[4] = (char)('0' + (minute % 10u));
        clock[5] = 0;
        p.text(x, y, clock, 0.96f, rui_rgb8(232, 240, 250));
    }

    void draw_bar_panel(Painter &p, const rui_frame_info_t *f, uint64_t now) {
        float t = bar_panel_show_;
        if (t <= 0.01f) return;
        if (bar_panel_render_ == BAR_PANEL_LAUNCHER)
            draw_launcher_panel(p, t);
        else if (bar_panel_render_ == BAR_PANEL_QUICK)
            draw_quick_panel(p, f, now, t);
    }

    void draw_launcher_panel(Painter &p, float t) {
        float w = launcher_w();
        float h = launcher_h();
        float x = launcher_x();
        float y = launcher_y() + (1.0f - t) * 28.0f;
        float visible_h = h * t;
        float main_x = x + 32.0f;
        float main_y = y + 26.0f;
        float footer_y = y + h - 54.0f;
        float grid_x = x + 32.0f;
        float grid_y = y + 118.0f;
        float grid_w = w - 64.0f;
        float cell_w = (grid_w - 10.0f * 3.0f) / 4.0f;
        bool trace = launcher_trace_left_ > 0;
        if (trace) shell_log("[injury-shell] launcher draw begin\n");
        if (visible_h < 16.0f) visible_h = 16.0f;
        p.rect(0.0f, 0.0f, screen_w_, screen_h_, rui_rgba(4.0f / 255.0f, 8.0f / 255.0f, 16.0f / 255.0f, 0.055f * t));
        p.round(x + 0.0f, y + 20.0f, w, h, 28.0f, rui_rgba(0.0f, 0.0f, 0.0f, 0.22f * t));
        p.round(x + 0.0f, y + 8.0f, w, h, 28.0f, rui_rgba(0.0f, 0.0f, 0.0f, 0.10f * t));
        p.round_gradient(x, y, w, visible_h, 24.0f,
                         rui_rgba(232.0f / 255.0f, 242.0f / 255.0f, 1.0f, 0.24f * t),
                         rui_rgba(17.0f / 255.0f, 23.0f / 255.0f, 34.0f / 255.0f, 0.88f * t));
        p.round(x + 1.0f, y + 1.0f, w - 2.0f, visible_h - 2.0f, 29.0f,
                rui_rgba(1.0f, 1.0f, 1.0f, 0.085f * t));
        draw_acrylic_texture(p, x, y, w, visible_h, t);
        if (trace) shell_log("[injury-shell] launcher draw shell\n");
        if (t < 0.35f) {
            if (trace) {
                shell_log("[injury-shell] launcher draw early\n");
                --launcher_trace_left_;
            }
            return;
        }

        p.round(main_x, main_y, w - 64.0f, 42.0f, 14.0f,
                rui_rgba(1.0f, 1.0f, 1.0f, 0.095f));
        p.round(main_x + 18.0f, main_y + 17.0f, 9.0f, 9.0f, 5.0f,
                rui_rgb8(200, 218, 238));
        p.round(main_x + 26.0f, main_y + 25.0f, 8.0f, 2.0f, 1.0f,
                rui_rgb8(200, 218, 238));
        if (launcher_stager_.open_frame < 8u) {
            p.round(main_x + 44.0f, main_y + 16.0f, 132.0f, 10.0f, 5.0f,
                    rui_rgba(1.0f, 1.0f, 1.0f, 0.075f));
            p.round(main_x, y + 90.0f, 92.0f, 10.0f, 5.0f,
                    rui_rgba(1.0f, 1.0f, 1.0f, 0.075f));
            if (trace) {
                shell_log("[injury-shell] launcher draw warmup\n");
                --launcher_trace_left_;
            }
            return;
        }
        panel_text(p, main_x + 44.0f, main_y + 12.0f, "Buscar en Injury", 0.92f,
                   rui_rgb8(208, 222, 240), 118.0f);
        panel_text(p, main_x, y + 88.0f, "Ancladas", 0.98f,
                   rui_rgb8(242, 248, 255), 78.0f);
        p.round(x + w - 116.0f, y + 82.0f, 84.0f, 28.0f, 10.0f,
                rui_rgba(1.0f, 1.0f, 1.0f, 0.075f));
        panel_text_center(p, x + w - 116.0f, y + 89.0f, 84.0f, "Todas", 0.76f,
                          rui_rgb8(210, 226, 244));
        if (trace) shell_log("[injury-shell] launcher draw header\n");

        {
            int count = (int)(sizeof(k_launcher_apps) / sizeof(k_launcher_apps[0]));
            int visible_apps = launcher_stager_.visible_items(count);
            for (int i = 0; i < visible_apps; ++i) {
                int col = i % 4;
                int row = i / 4;
                float tx = grid_x + (float)col * (cell_w + 10.0f);
                float ty = grid_y + (float)row * (74.0f + 10.0f);
                bool hover = false;
            int app = k_launcher_apps[i].app;
            bool valid_app = app >= 0 && app < APP_COUNT;
            bool running = valid_app && apps_[app].open && !apps_[app].minimized;
            if (hover || running) {
                    p.rect(tx + 4.0f, ty + 4.0f, cell_w - 8.0f, 64.0f,
                           rui_rgba(1.0f, 1.0f, 1.0f, hover ? 0.12f : 0.065f));
            }
            if (running) {
                    p.round(tx + cell_w * 0.5f - 10.0f, ty + 68.0f,
                            20.0f, 3.0f, 1.5f, rui_rgb8(78, 166, 255));
            }
                draw_icon_cached_or_placeholder(p, k_launcher_apps[i].icon,
                                                tx + 16.0f, ty + 17.0f, 34.0f,
                                                valid_app ? apps_[app].accent : rui_rgb8(78, 166, 255));
                panel_text(p, tx + 60.0f, ty + 18.0f, k_launcher_apps[i].label, 0.82f,
                           hover ? rui_rgb8(248, 252, 255) : rui_rgb8(218, 232, 248),
                           cell_w * 0.38f);
                panel_text(p, tx + 60.0f, ty + 40.0f,
                           valid_app && running ? "En ejecucion" : "Aplicacion",
                           0.68f, rui_rgb8(148, 166, 190), cell_w * 0.34f);
            }
        }
        if (trace) shell_log("[injury-shell] launcher draw grid\n");

        panel_text(p, main_x, y + h - 120.0f, "Recomendado", 0.94f,
                   rui_rgb8(242, 248, 255), 92.0f);
        static const int rec_icons[] = { RIDUX_ICON_FILES, RIDUX_ICON_SETTINGS, RIDUX_ICON_TERMINAL };
        static const char *rec_labels[] = { "Archivos", "Ajustes", "Terminal" };
        for (int i = 0; i < 3; ++i) {
            float rx = main_x + (float)i * 194.0f;
            p.round(rx, y + h - 90.0f, 176.0f, 38.0f, 12.0f,
                    rui_rgba(1.0f, 1.0f, 1.0f, 0.065f));
            (void)rec_icons;
            draw_icon_cached_or_placeholder(p, rec_icons[i], rx + 12.0f, y + h - 82.0f,
                                            22.0f, rui_rgb8(78, 166, 255));
            panel_text(p, rx + 46.0f, y + h - 80.0f, rec_labels[i], 0.80f,
                       rui_rgb8(218, 232, 248), 58.0f);
        }

        p.rect(x + 2.0f, footer_y - 12.0f, w - 4.0f, 1.0f,
               rui_rgba(1.0f, 1.0f, 1.0f, 0.075f));
        draw_icon_cached_or_placeholder(p, RIDUX_ICON_ABOUT, main_x, footer_y + 2.0f,
                                        30.0f, rui_rgb8(78, 166, 255));
        panel_text(p, main_x + 42.0f, footer_y + 1.0f, "Injury", 0.88f,
                   rui_rgb8(242, 248, 255), 48.0f);
        panel_text(p, main_x + 42.0f, footer_y + 19.0f, "sesion local", 0.74f,
                   rui_rgb8(158, 178, 202), 78.0f);
        p.round(x + w - 80.0f, footer_y - 2.0f, 38.0f, 34.0f, 13.0f,
                rui_rgba(1.0f, 1.0f, 1.0f, 0.075f));
        panel_text_center(p, x + w - 80.0f, footer_y + 7.0f, 38.0f, "IO", 0.72f,
                          rui_rgb8(218, 232, 248));
        if (trace) {
            shell_log("[injury-shell] launcher draw done\n");
            --launcher_trace_left_;
        }
    }

    void draw_quick_panel(Painter &p, const rui_frame_info_t *f, uint64_t now, float t) {
        float w = 354.0f;
        float h = 298.0f * t;
        float x = screen_w_ - 378.0f;
        float y = screen_h_ - taskbar_h_ - 312.0f + (1.0f - t) * 28.0f;
        char line[128];
        if (h < 14.0f) h = 14.0f;
        p.round(x + 0.0f, y + 18.0f, w, 298.0f, 26.0f, rui_rgba(0.0f, 0.0f, 0.0f, 0.22f * t));
        p.round_gradient(x, y, w, h, 28.0f,
                         rui_rgba(0.74f, 0.84f, 0.96f, 0.20f * t),
                         rui_rgba(0.05f, 0.07f, 0.10f, 0.88f * t));
        draw_acrylic_texture(p, x, y, w, h, t);
        if (t < 0.35f) return;
        draw_icon(p, RIDUX_ICON_TRAY_SETTINGS, x + 20.0f, y + 16.0f, 28.0f);
        p.text(x + 58.0f, y + 22.0f, "Controles", 0.98f, rui_rgb8(240, 248, 255));
        p.text(x + 250.0f, y + 20.0f, "RiduxOS", 0.82f, rui_rgb8(151, 174, 198));
        draw_quick_tile(p, x + 22.0f, y + 56.0f, 142.0f, "Wi-Fi", "Activo", rui_rgb8(62, 171, 255), true);
        draw_quick_tile(p, x + 188.0f, y + 56.0f, 142.0f, "Bluetooth", "Listo", rui_rgb8(255, 255, 255), false);
        draw_quick_tile(p, x + 22.0f, y + 116.0f, 142.0f, "Audio", "78%", rui_rgb8(39, 216, 185), true);
        draw_quick_tile(p, x + 188.0f, y + 116.0f, 142.0f, "Energia", "87%", rui_rgb8(255, 255, 255), false);
        p.text(x + 24.0f, y + 190.0f, "Brillo", 0.86f, rui_rgb8(218, 232, 244));
        draw_slider(p, x + 92.0f, y + 198.0f, 220.0f, 0.67f, rui_rgb8(62, 171, 255));
        p.text(x + 24.0f, y + 226.0f, "Volumen", 0.86f, rui_rgb8(218, 232, 244));
        draw_slider(p, x + 92.0f, y + 234.0f, 220.0f,
                    0.78f + tri(now, 3600u, 0u) * 0.02f, rui_rgb8(39, 216, 185));
        p.round(x + 22.0f, y + 246.0f, 308.0f, 18.0f, 9.0f,
                rui_rgba(38.0f / 255.0f, 183.0f / 255.0f, 122.0f / 255.0f, 0.20f));
        p.text(x + 34.0f, y + 248.0f, "Render", 0.72f, rui_rgb8(190, 209, 226));
        snprintf(line, sizeof(line), "%s  %u/%u ms",
                 f && f->renderer ? f->renderer : "virgl",
                 f ? f->present_swap_ms : 0u, f ? f->present_flip_ms : 0u);
        p.text(x + 92.0f, y + 248.0f, line, 0.72f, rui_rgb8(210, 255, 230));
        p.text(x + 24.0f, y + 270.0f, "Mesa GL/VirGL listo",
               0.76f, rui_rgb8(188, 245, 215));
    }

    void draw_calendar_panel(Painter &p, const rui_frame_info_t *f, uint64_t now, float t) {
        float w = 342.0f;
        float h = 278.0f * t;
        float x = screen_w_ - w - 24.0f;
        float y = 438.0f - (1.0f - t) * 16.0f;
        char line[64];
        (void)now;
        if (h < 12.0f) h = 12.0f;
        riduxui::surface(p, { x, y, w, h }, theme_, t);
        if (t < 0.35f) return;
        p.text(x + 18.0f, y + 18.0f, "RiduxBar", 1.02f, rui_rgb8(238, 248, 255));
        snprintf(line, sizeof(line), "%u/%u ms", f ? f->present_swap_ms : 0u, f ? f->present_flip_ms : 0u);
        p.text(x + 232.0f, y + 18.0f, line, 0.86f, rui_rgb8(190, 218, 245));
        p.round(x + 18.0f, y + 54.0f, w - 36.0f, 1.0f, 0.5f,
                rui_rgba(1.0f, 1.0f, 1.0f, 0.10f * t));
        p.text(x + 126.0f, y + 76.0f, "Mayo 2026", 1.04f, rui_rgb8(248, 252, 255));
        static const char *days[] = { "Lu", "Ma", "Mi", "Ju", "Vi", "Sa", "Do" };
        for (int i = 0; i < 7; ++i)
            p.text(x + 30.0f + (float)i * 43.0f, y + 112.0f, days[i], 0.86f, rui_rgb8(190, 218, 245));
        for (int i = 0; i < 35; ++i) {
            int n = i - 2;
            float cx = x + 28.0f + (float)(i % 7) * 43.0f;
            float cy = y + 144.0f + (float)(i / 7) * 29.0f;
            char d[3];
            if (n < 1 || n > 31) {
                d[0] = '-'; d[1] = 0;
            } else {
                d[0] = (char)('0' + (n / 10));
                d[1] = (char)('0' + (n % 10));
                d[2] = 0;
                if (d[0] == '0') { d[0] = d[1]; d[1] = 0; }
            }
            if (n == 20)
                p.round(cx - 8.0f, cy - 5.0f, 34.0f, 26.0f, 9.0f, rui_rgba(0.34f, 0.72f, 1.0f, 0.72f));
            p.text(cx, cy, d, 0.88f, n == 20 ? rui_rgb8(255, 255, 255) : rui_rgb8(214, 230, 250));
        }
        p.text(x + 108.0f, y + h - 34.0f, "No hay notificaciones", 0.86f, rui_rgb8(190, 210, 232));
    }

    void draw_slider(Painter &p, float x, float y, float w, float value, rui_color_t accent) {
        riduxui::slider(p, { x, y - 8.0f, w, 20.0f }, value, accent);
    }

    void draw_quick_tile(Painter &p, float x, float y, float w,
                         const char *title, const char *value,
                         rui_color_t accent, bool active) {
        p.round_gradient(x, y, w, 58.0f, 12.0f,
                         active ? rui_rgba(accent.r, accent.g, accent.b, 0.28f) :
                                  theme_.control,
                         rui_rgba(1.0f, 1.0f, 1.0f, 0.055f));
        p.round(x + 14.0f, y + 19.0f, 10.0f, 10.0f, 5.0f, accent);
        p.text(x + 42.0f, y + 13.0f, title, 0.94f, rui_rgb8(244, 250, 255));
        p.text(x + 42.0f, y + 33.0f, value, 0.80f, rui_rgb8(205, 226, 246));
    }

    void draw_traffic(Painter &p, const AppWindow &a, bool active) {
        const char *title = a.resource_frames >= 6u ? a.title : "";
        p.rect(a.x + 16.0f, a.y + 18.0f, 12.0f, 12.0f,
               rui_rgba(1.0f, 0.36f, 0.36f, 0.92f));
        p.rect(a.x + 38.0f, a.y + 18.0f, 12.0f, 12.0f,
               rui_rgba(1.0f, 0.76f, 0.24f, 0.92f));
        p.rect(a.x + 60.0f, a.y + 18.0f, 12.0f, 12.0f,
               rui_rgba(0.34f, 0.84f, 0.44f, 0.92f));
        if (title && *title) {
            panel_text(p, a.x + 92.0f, a.y + 15.0f, title, 1.10f,
                       active ? theme_.text : theme_.text_muted, a.w * 0.24f);
        }
    }

    void draw_stable_window_surface(Painter &p, const AppWindow &a, float active) {
        p.rect(a.x + 0.0f, a.y + 10.0f, a.w, a.h,
               rui_rgba(0.0f, 0.0f, 0.0f, 0.14f + active * 0.04f));
        p.rect(a.x, a.y, a.w, a.h,
               rui_rgba(18.0f / 255.0f, 24.0f / 255.0f, 34.0f / 255.0f,
                        0.82f + active * 0.06f));
        p.rect(a.x + 1.0f, a.y + 1.0f, a.w - 2.0f, 1.0f,
               rui_rgba(1.0f, 1.0f, 1.0f, 0.12f + active * 0.04f));
        p.rect(a.x + 18.0f, a.y + 54.0f, 92.0f, 2.0f,
               rui_rgba(a.accent.r, a.accent.g, a.accent.b, 0.30f + active * 0.18f));
    }

    void draw_resize_handle(Painter &p, const AppWindow &a) {
        if (a.maximized || a.w < 150.0f || a.h < 110.0f) return;
        for (int i = 0; i < 3; ++i) {
            float off = (float)i * 6.0f;
            p.rect(a.x + a.w - 26.0f + off, a.y + a.h - 10.0f,
                   12.0f - off, 1.5f,
                   rui_rgba(1.0f, 1.0f, 1.0f, 0.18f));
            p.rect(a.x + a.w - 10.0f, a.y + a.h - 26.0f + off,
                   1.5f, 12.0f - off,
                   rui_rgba(1.0f, 1.0f, 1.0f, 0.18f));
        }
    }

    void draw_app_commit_slab(Painter &p, const AppWindow &a) {
        p.rect(a.x, a.y, a.w, a.h,
               rui_rgb8(18, 24, 34));
        p.rect(a.x, a.y, a.w, 46.0f,
               rui_rgb8(24, 31, 43));
        p.rect(a.x + 1.0f, a.y + 1.0f, a.w - 2.0f, 1.0f,
               rui_rgb8(60, 74, 94));
    }

    void draw_app(Painter &p, int idx, const rui_frame_info_t *f, uint64_t now) {
        AppWindow &a = apps_[idx];
        if (!a.open || a.show < 0.025f) return;
        float active = (idx == focus_) ? 1.0f : 0.0f;
        bool debug = (draw_debug_app_ == idx);
        if (a.resource_frames < 3u) {
            if (debug) shell_log("[ridux-ui-shell] draw-app stage=commit-wait\n");
            return;
        }
        if (a.resource_frames < 8u) {
            if (debug) shell_log("[ridux-ui-shell] draw-app stage=commit-slab\n");
            draw_app_commit_slab(p, a);
            return;
        }
        if (debug) shell_log("[ridux-ui-shell] draw-app stage=surface\n");
        draw_stable_window_surface(p, a, active);
        if (a.resource_frames < 16u) {
            if (debug) shell_log("[ridux-ui-shell] draw-app stage=surface-only\n");
            return;
        }
        if (debug) shell_log("[ridux-ui-shell] draw-app stage=titlebar\n");
        draw_traffic(p, a, active > 0.0f);
        if (a.show > 0.22f && a.w > 140.0f && a.h > 96.0f) {
            if (a.resource_frames < 28u) {
                if (debug) shell_log("[injury-shell] draw-app stage=resource-warmup\n");
                draw_app_resource_warmup(p, a);
            } else {
                if (debug) shell_log("[ridux-ui-shell] draw-app stage=content-safe\n");
                draw_safe_app_content(p, idx, a, f, now);
            }
        }
        if (a.resource_frames >= 20u)
            draw_resize_handle(p, a);
        if (debug) {
            shell_log("[ridux-ui-shell] draw-app stage=done\n");
            if (a.resource_frames >= 28u)
                draw_debug_app_ = -1;
        }
    }

    void draw_app_resource_warmup(Painter &p, const AppWindow &a) {
        float pulse = (float)(a.resource_frames > 7u ? 7u : a.resource_frames) / 7.0f;
        p.rect(a.x + 24.0f, a.y + 66.0f, a.w - 48.0f, a.h - 92.0f,
               rui_rgba(1.0f, 1.0f, 1.0f, 0.035f));
        p.rect(a.x + 42.0f, a.y + 88.0f, 42.0f, 42.0f,
               rui_rgba(a.accent.r, a.accent.g, a.accent.b, 0.18f + pulse * 0.10f));
        p.rect(a.x + 104.0f, a.y + 92.0f, a.w * 0.42f, 12.0f,
               rui_rgba(1.0f, 1.0f, 1.0f, 0.080f + pulse * 0.025f));
        p.rect(a.x + 104.0f, a.y + 116.0f, a.w * 0.30f, 8.0f,
               rui_rgba(1.0f, 1.0f, 1.0f, 0.055f + pulse * 0.020f));
        p.rect(a.x + 42.0f, a.y + a.h - 58.0f, (a.w - 84.0f) * (0.35f + pulse * 0.65f),
               30.0f, rui_rgba(a.accent.r, a.accent.g, a.accent.b, 0.14f));
    }

    void draw_safe_app_content(Painter &p, int idx, const AppWindow &a,
                               const rui_frame_info_t *f, uint64_t now) {
        if (a.resource_frames < 24u) {
            draw_app_resource_warmup(p, a);
            return;
        }
        draw_app_committed_placeholder(p, idx, a, f, now);
    }

    void draw_app_committed_placeholder(Painter &p, int idx, const AppWindow &a,
                                        const rui_frame_info_t *f, uint64_t now) {
        (void)f;
        float pulse = 0.08f + tri(now, 2400u, (uint32_t)idx * 160u) * 0.05f;
        p.rect(a.x + 28.0f, a.y + 74.0f, a.w - 56.0f, a.h - 104.0f,
               rui_rgba(1.0f, 1.0f, 1.0f, 0.040f));
        draw_icon_cached_or_placeholder(p, a.icon, a.x + 46.0f, a.y + 96.0f,
                                        36.0f, a.accent);
        panel_text(p, a.x + 96.0f, a.y + 98.0f, a.title, 0.92f,
                   rui_rgb8(230, 242, 255), a.w * 0.28f);
        p.rect(a.x + 46.0f, a.y + 150.0f, a.w - 92.0f, 10.0f,
               rui_rgba(1.0f, 1.0f, 1.0f, pulse));
        p.rect(a.x + 46.0f, a.y + 178.0f, (a.w - 92.0f) * 0.62f, 8.0f,
               rui_rgba(1.0f, 1.0f, 1.0f, pulse * 0.72f));
        p.rect(a.x + 46.0f, a.y + a.h - 62.0f, (a.w - 92.0f) * 0.42f,
               28.0f, rui_rgba(a.accent.r, a.accent.g, a.accent.b, 0.16f));
    }

    void draw_files(Painter &p, const AppWindow &a) {
        static const char *names[] = { "Sistema", "Usuarios", "Aplicaciones", "Bibliotecas", "Wallpapers" };
        static const char *meta[] = { "kernel", "home", "7 apps", "Mesa", "4K" };
        p.round(a.x + 20.0f, a.y + 68.0f, 108.0f, a.h - 92.0f, 14.0f,
                rui_rgba(1.0f, 1.0f, 1.0f, 0.065f));
        p.text(a.x + 38.0f, a.y + 86.0f, "Inicio", 1.22f, rui_rgb8(232, 239, 248));
        p.text(a.x + 38.0f, a.y + 116.0f, "Disco", 1.22f, rui_rgb8(166, 178, 194));
        p.text(a.x + 38.0f, a.y + 146.0f, "Red", 1.22f, rui_rgb8(166, 178, 194));
        for (int i = 0; i < 5; ++i) {
            float y = a.y + 70.0f + (float)i * 40.0f;
            p.round(a.x + 146.0f, y, a.w - 170.0f, 30.0f, 9.0f,
                    i == 2 ? rui_rgba(a.accent.r, a.accent.g, a.accent.b, 0.16f) :
                             rui_rgba(1.0f, 1.0f, 1.0f, 0.045f));
            p.round(a.x + 158.0f, y + 8.0f, 14.0f, 14.0f, 5.0f,
                    i == 2 ? a.accent : rui_rgb8(115, 130, 150));
            p.text(a.x + 186.0f, y + 6.0f, names[i], 1.22f, rui_rgb8(233, 238, 246));
            p.text(a.x + a.w - 88.0f, y + 7.0f, meta[i], 1.08f, rui_rgb8(156, 168, 184));
        }
    }

    void draw_browser(Painter &p, const AppWindow &a) {
        p.round(a.x + 22.0f, a.y + 64.0f, a.w - 44.0f, 30.0f, 15.0f,
                rui_rgba(1.0f, 1.0f, 1.0f, 0.095f));
        p.text(a.x + 40.0f, a.y + 72.0f, "ridux://home", 1.14f, rui_rgb8(225, 234, 244));
        p.round_gradient(a.x + 22.0f, a.y + 112.0f, a.w - 44.0f, 82.0f, 18.0f,
                         rui_rgba(0.14f, 0.50f, 0.78f, 0.24f),
                         rui_rgba(0.70f, 0.30f, 0.42f, 0.10f));
        p.text(a.x + 44.0f, a.y + 132.0f, "Ridux", 1.85f, rui_rgb8(248, 252, 255));
        p.text(a.x + 46.0f, a.y + 168.0f, "Mesa VirGL", 1.12f, rui_rgb8(192, 207, 224));
        for (int i = 0; i < 3; ++i) {
            float x = a.x + 24.0f + (float)i * ((a.w - 56.0f) / 3.0f);
            float cw = (a.w - 74.0f) / 3.0f;
            p.round(x, a.y + 214.0f, cw, 74.0f, 14.0f, rui_rgba(1.0f, 1.0f, 1.0f, 0.06f));
            p.round(x + 14.0f, a.y + 232.0f, cw - 28.0f, 9.0f, 4.5f,
                    rui_rgba(1.0f, 1.0f, 1.0f, 0.16f));
            p.round(x + 14.0f, a.y + 256.0f, cw * 0.48f, 8.0f, 4.0f,
                    rui_rgba(a.accent.r, a.accent.g, a.accent.b, 0.40f));
        }
    }

    void draw_terminal(Painter &p, const AppWindow &a, const rui_frame_info_t *f) {
        char line[128];
        p.round(a.x + 18.0f, a.y + 64.0f, a.w - 36.0f, a.h - 84.0f, 16.0f,
                rui_rgba(2.0f / 255.0f, 6.0f / 255.0f, 10.0f / 255.0f, 0.70f));
        p.text(a.x + 34.0f, a.y + 82.0f, "$ riduxctl gpu", 1.17f, rui_rgb8(132, 239, 184));
        p.text(a.x + 34.0f, a.y + 112.0f, "renderer: virgl", 1.17f, rui_rgb8(222, 231, 241));
        snprintf(line, sizeof(line), "swap:%ums flip:%ums",
                 f ? f->present_swap_ms : 0u, f ? f->present_flip_ms : 0u);
        p.text(a.x + 34.0f, a.y + 142.0f, line, 1.17f, rui_rgb8(222, 231, 241));
        p.text(a.x + 34.0f, a.y + 172.0f, "input: evdev mouse", 1.17f, rui_rgb8(250, 210, 128));
        p.round(a.x + 34.0f, a.y + a.h - 46.0f, 12.0f, 18.0f, 4.0f,
                rui_rgba(0.58f, 0.95f, 0.72f, 0.92f));
    }

    void draw_toggle(Painter &p, float x, float y, const char *label, bool on, rui_color_t accent) {
        riduxui::toggle(p, { x, y, 262.0f, 22.0f }, label, on, theme_, accent);
    }

    void draw_settings(Painter &p, const AppWindow &a) {
        draw_toggle(p, a.x + 34.0f, a.y + 76.0f, "Glass", settings_glass_, a.accent);
        draw_toggle(p, a.x + 34.0f, a.y + 110.0f, "Animaciones", settings_motion_, rui_rgb8(90, 208, 255));
        draw_toggle(p, a.x + 34.0f, a.y + 144.0f, "GPU", settings_gpu_, rui_rgb8(104, 232, 162));
        p.round(a.x + a.w - 138.0f, a.y + 78.0f, 86.0f, 64.0f, 16.0f,
                rui_rgba(1.0f, 1.0f, 1.0f, 0.07f));
        p.text(a.x + a.w - 118.0f, a.y + 96.0f, "EGL", 1.55f, rui_rgb8(246, 250, 255));
    }

    void draw_store(Painter &p, const AppWindow &a) {
        static const char *pkgs[] = { "Dolphin", "Konsole", "Firefox", "Wayfire" };
        p.text(a.x + 28.0f, a.y + 68.0f, "Paquetes", 1.55f, rui_rgb8(245, 249, 253));
        for (int i = 0; i < 4; ++i) {
            float y = a.y + 106.0f + (float)i * 42.0f;
            p.round(a.x + 26.0f, y, a.w - 52.0f, 30.0f, 10.0f,
                    rui_rgba(1.0f, 1.0f, 1.0f, 0.055f));
            p.text(a.x + 42.0f, y + 7.0f, pkgs[i], 1.16f, rui_rgb8(231, 238, 247));
            p.round(a.x + a.w - 102.0f, y + 6.0f, 58.0f, 18.0f, 9.0f,
                    rui_rgba(a.accent.r, a.accent.g, a.accent.b, 0.34f));
            p.text(a.x + a.w - 90.0f, y + 8.0f, "Listo", 0.95f, rui_rgb8(248, 252, 255));
        }
    }

    void draw_monitor(Painter &p, const AppWindow &a, const rui_frame_info_t *f, uint64_t now) {
        float p0 = tri(now, 1400u, 0u);
        float p1 = tri(now, 2100u, 500u);
        float p2 = tri(now, 2600u, 900u);
        char line[128];
        snprintf(line, sizeof(line), "swap %ums", f ? f->present_swap_ms : 0u);
        p.text(a.x + 28.0f, a.y + 70.0f, line, 1.18f, rui_rgb8(222, 232, 244));
        snprintf(line, sizeof(line), "flip %ums", f ? f->present_flip_ms : 0u);
        p.text(a.x + 28.0f, a.y + 100.0f, line, 1.18f, rui_rgb8(222, 232, 244));
        draw_meter(p, a.x + 114.0f, a.y + 76.0f, a.w - 144.0f, 8.0f, p0, a.accent);
        draw_meter(p, a.x + 114.0f, a.y + 106.0f, a.w - 144.0f, 8.0f, p1, rui_rgb8(96, 226, 170));
        draw_meter(p, a.x + 28.0f, a.y + 148.0f, a.w - 56.0f, 36.0f, p2, rui_rgb8(255, 205, 98));
    }

    void draw_meter(Painter &p, float x, float y, float w, float h, float value, rui_color_t color) {
        p.round(x, y, w, h, h * 0.5f, rui_rgba(1.0f, 1.0f, 1.0f, 0.11f));
        p.round(x, y, w * clampf(value, 0.08f, 0.96f), h, h * 0.5f, alpha(color, 0.72f));
    }

    void draw_notes(Painter &p, const AppWindow &a) {
        p.text(a.x + 28.0f, a.y + 68.0f, "Roadmap", 1.48f, rui_rgb8(248, 250, 252));
        p.text(a.x + 30.0f, a.y + 104.0f, "Ventanas nativas", 1.12f, rui_rgb8(226, 234, 244));
        p.text(a.x + 30.0f, a.y + 132.0f, "Compositor GPU", 1.12f, rui_rgb8(226, 234, 244));
        p.text(a.x + 30.0f, a.y + 160.0f, "Wayland y Vulkan", 1.12f, rui_rgb8(226, 234, 244));
        p.round(a.x + 26.0f, a.y + 96.0f, a.w - 52.0f, 1.0f, 0.5f,
                rui_rgba(1.0f, 1.0f, 1.0f, 0.10f));
    }

    void draw_taskbar(Painter &p, const rui_frame_info_t *f, uint64_t now) {
        int count = (int)(sizeof(k_taskbar_apps) / sizeof(k_taskbar_apps[0]));
        float item = 44.0f;
        float gap = 8.0f;
        float start = taskbar_start_x();
        float y = screen_h_ - taskbar_h_;
        float pulse = tri(now, 1600u, 0u);
        float hover = bar_pulse_;
        char metric[64];
        p.round(0.0f, y - 10.0f, screen_w_, taskbar_h_ + 10.0f, 0.0f,
                rui_rgba(0.0f, 0.0f, 0.0f, 0.16f));
        p.round_gradient(0.0f, y, screen_w_, taskbar_h_, 0.0f,
                         rui_rgba(236.0f / 255.0f, 246.0f / 255.0f, 1.0f, 0.18f + hover * 0.03f),
                         rui_rgba(13.0f / 255.0f, 18.0f / 255.0f, 27.0f / 255.0f, 0.88f));
        p.rect(0.0f, y, screen_w_, 1.0f, rui_rgba(1.0f, 1.0f, 1.0f, 0.12f));
        p.rect(0.0f, y + 1.0f, screen_w_, 1.0f, rui_rgba(255.0f / 255.0f, 255.0f / 255.0f, 255.0f / 255.0f, 0.045f));
        p.text(18.0f, y + 17.0f, "Injury", 0.90f, rui_rgb8(232, 242, 252));
        p.round(76.0f, y + 15.0f, 78.0f, 26.0f, 10.0f,
                rui_rgba(62.0f / 255.0f, 171.0f / 255.0f, 255.0f / 255.0f, 0.13f));
        text_center(p, 76.0f, y + 22.0f, 78.0f, "VirGL", 0.70f, rui_rgb8(192, 230, 255));
        for (int i = 0; i < count; ++i) {
            int app = k_taskbar_apps[i].app;
            bool running = app >= 0 && apps_[app].open && !apps_[app].minimized;
            bool active = (app >= 0 && focus_ == app) ||
                          (app < 0 && bar_panel_ == BAR_PANEL_LAUNCHER);
            float x = start + (item + gap) * (float)i;
            int over = hit(input_.x, input_.y, x, y + 7.0f, item, 42.0f);
            float lift = over ? 2.0f + pulse * 0.7f : 0.0f;
            p.round(x, y + 7.0f - lift, item, 42.0f, 12.0f,
                    active ? rui_rgba(78.0f / 255.0f, 166.0f / 255.0f, 255.0f / 255.0f, 0.26f) :
                    over ? rui_rgba(1.0f, 1.0f, 1.0f, 0.105f) :
                           rui_rgba(1.0f, 1.0f, 1.0f, 0.032f));
            if (app >= 0) {
                draw_icon(p, k_taskbar_apps[i].icon, x + 9.0f, y + 12.0f - lift, 26.0f);
            } else {
                draw_app_grid_glyph(p, x + 11.0f, y + 13.0f - lift, 23.0f,
                                    rui_rgb8(238, 247, 255), active ? 0.92f : 0.70f);
            }
            if (running || active)
                p.round(x + item * 0.5f - 7.0f, y + 48.0f, 14.0f, 3.0f, 2.0f,
                        rui_rgb8(82, 184, 255));
        }
        snprintf(metric, sizeof(metric), "%s %u/%u ms",
                 f && f->renderer ? f->renderer : "Mesa",
                 f ? f->present_swap_ms : 0u, f ? f->present_flip_ms : 0u);
        text_right(p, screen_w_ - 198.0f, y + 12.0f, metric, 0.68f, rui_rgb8(172, 194, 218));
        draw_icon(p, RIDUX_ICON_TRAY_NETWORK, screen_w_ - 180.0f, y + 17.0f, 18.0f);
        draw_icon(p, RIDUX_ICON_TRAY_VOLUME, screen_w_ - 150.0f, y + 17.0f, 18.0f);
        draw_icon(p, RIDUX_ICON_TRAY_BATTERY, screen_w_ - 120.0f, y + 17.0f, 18.0f);
        draw_bar_clock(p, now, screen_w_ - 80.0f, y + 13.0f);
    }

};

int main(int argc, char **argv) {
    rui_context_t *ui = NULL;
    rui_options_t options;
    DesktopShell desktop;
    uint64_t fps_last_ms = 0;
    uint32_t fps_last_frame = 0;
    uint32_t startup_fps_logs = 0;
    uint32_t startup_stage_logs = 0;
    uint32_t last_draw_ms = 0;
    uint32_t last_present_ms = 0;
    uint32_t last_total_ms = 0;
    (void)argc;
    (void)argv;

    shell_log("[injury-shell] start compositor=Injury backend=RiduxUI++ DRM/GBM/EGL/GLES2\n");
    shell_log("[injury-shell] protocol=Injury/RiduxSurface/1 clients=shell-surfaces wayland-bridge=compat-path\n");
    options.backend = RUI_BACKEND_DRM;
    options.immediate_present = false;
    options.require_hardware = true;
    if (!rui_open(&ui, &options)) {
        shell_log("[ridux-ui-shell] RiduxUI init failed\n");
        return 2;
    }
    {
        const rui_frame_info_t *start = rui_frame_info(ui);
        desktop.init(start ? (float)start->width : 1024.0f,
                     start ? (float)start->height : 768.0f);
        desktop.enable_hardware_cursor(ui);
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
        desktop.update(ui, f);
        desktop.draw(ui);
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
                char line[256];
                int n;
                fps_last_ms = now_ms;
                fps_last_frame = f->frame;
                n = snprintf(line, sizeof(line),
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
                if (!shell_frame_log_enabled()) ++startup_fps_logs;
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
