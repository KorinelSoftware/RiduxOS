#include "ridux_native_ui.h"

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <cairo/cairo.h>
#include <errno.h>
#include <fcntl.h>
#include <gbm.h>
#include <pango/pangocairo.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/select.h>
#include <time.h>
#include <unistd.h>
#include <xf86drm.h>
#include <xf86drmMode.h>

#ifndef EGL_PLATFORM_GBM_KHR
#define EGL_PLATFORM_GBM_KHR 0x31D7
#endif

#ifndef DRM_IOCTL_MODE_CREATE_DUMB
#define DRM_IOCTL_MODE_CREATE_DUMB 0xC02064B2
#endif
#ifndef DRM_IOCTL_MODE_MAP_DUMB
#define DRM_IOCTL_MODE_MAP_DUMB 0xC01064B3
#endif
#ifndef DRM_IOCTL_MODE_DESTROY_DUMB
#define DRM_IOCTL_MODE_DESTROY_DUMB 0xC00464B4
#endif

#define RUI_CURSOR_W 64u
#define RUI_CURSOR_H 64u
#define RUI_XCURSOR_MAGIC 0x72756358u
#define RUI_XCURSOR_IMAGE_TYPE 0xfffd0002u

typedef struct rui_drm_mode_create_dumb {
    uint32_t height;
    uint32_t width;
    uint32_t bpp;
    uint32_t flags;
    uint32_t handle;
    uint32_t pitch;
    uint64_t size;
} rui_drm_mode_create_dumb_t;

typedef struct rui_drm_mode_map_dumb {
    uint32_t handle;
    uint32_t pad;
    uint64_t offset;
} rui_drm_mode_map_dumb_t;

typedef struct rui_drm_gem_close {
    uint32_t handle;
    uint32_t pad;
} rui_drm_gem_close_t;

typedef EGLDisplay (*rui_egl_get_platform_display_fn)(EGLenum platform,
                                                       void *native_display,
                                                       const EGLint *attrib_list);

typedef struct rui_drm_output {
    int fd;
    uint32_t connector_id;
    uint32_t crtc_id;
    drmModeModeInfo mode;
    drmModeCrtc *saved_crtc;
} rui_drm_output_t;

typedef struct rui_fb {
    struct gbm_bo *bo;
    uint32_t fb_id;
    bool cached_fb;
} rui_fb_t;

typedef struct rui_gbm_fb {
    int fd;
    uint32_t fb_id;
} rui_gbm_fb_t;

#define RUI_TEXT_CACHE_MAX 256
#define RUI_TEXT_CACHE_TEXT_MAX 160
#define RUI_TEXT_NEW_TEXTURES_PER_FRAME 1u

typedef struct rui_text_cache_entry {
    bool used;
    char text[RUI_TEXT_CACHE_TEXT_MAX];
    int px;
    int width;
    int height;
    uint32_t last_used;
    GLuint texture;
} rui_text_cache_entry_t;

#define RUI_TEXT_BATCH_MAX_QUADS 128u
#define RUI_TEXT_BATCH_MAX_VERTICES (RUI_TEXT_BATCH_MAX_QUADS * 4u)

typedef struct rui_text_batch_vertex {
    GLfloat x;
    GLfloat y;
    GLfloat u;
    GLfloat v;
} rui_text_batch_vertex_t;

typedef struct rui_text_batch_item {
    GLuint texture;
    rui_color_t color;
    GLsizei first;
} rui_text_batch_item_t;

#define RUI_IMAGE_CACHE_MAX 128
#define RUI_IMAGE_NEW_TEXTURES_PER_FRAME 1u
#define RUI_IMAGE_BATCH_MAX_QUADS 256u
#define RUI_IMAGE_BATCH_MAX_VERTICES (RUI_IMAGE_BATCH_MAX_QUADS * 4u)

typedef struct rui_image_cache_entry {
    bool used;
    const uint32_t *pixels;
    uint32_t width;
    uint32_t height;
    uint32_t last_used;
    GLuint texture;
} rui_image_cache_entry_t;

typedef struct rui_image_batch_vertex {
    GLfloat x;
    GLfloat y;
    GLfloat u;
    GLfloat v;
} rui_image_batch_vertex_t;

typedef struct rui_image_batch_item {
    GLuint texture;
    GLsizei first;
} rui_image_batch_item_t;

#define RUI_RECT_BATCH_MAX_VERTICES 8192u

typedef struct rui_rect_batch_vertex {
    GLfloat x;
    GLfloat y;
    GLfloat r;
    GLfloat g;
    GLfloat b;
    GLfloat a;
} rui_rect_batch_vertex_t;

#define RUI_ROUND_BATCH_MAX_VERTICES 4096u

typedef struct rui_round_batch_vertex {
    GLfloat x;
    GLfloat y;
    GLfloat rx;
    GLfloat ry;
    GLfloat rw;
    GLfloat rh;
    GLfloat radius;
    GLfloat tr;
    GLfloat tg;
    GLfloat tb;
    GLfloat ta;
    GLfloat br;
    GLfloat bg;
    GLfloat bb;
    GLfloat ba;
} rui_round_batch_vertex_t;

struct rui_context {
    rui_options_t options;
    rui_drm_output_t out;
    struct gbm_device *gbm;
    struct gbm_surface *surface;
    EGLDisplay display;
    EGLContext egl_context;
    EGLSurface egl_surface;
    rui_fb_t front;
    bool page_flip_ok;
    bool first_frame;
    rui_frame_info_t frame;
    char renderer[160];
    GLuint solid_program;
    GLint solid_pos_loc;
    GLint solid_color_loc;
    GLuint solid_vbo;
    bool solid_pipeline_failed;
    GLuint round_program;
    GLint round_pos_loc;
    GLint round_screen_loc;
    GLint round_rect_loc;
    GLint round_radius_loc;
    GLint round_top_loc;
    GLint round_bottom_loc;
    GLuint round_vbo;
    bool round_pipeline_failed;
    size_t round_batch_count;
    rui_round_batch_vertex_t round_batch[RUI_ROUND_BATCH_MAX_VERTICES];
    GLuint rect_program;
    GLint rect_pos_loc;
    GLint rect_color_loc;
    GLuint rect_vbo;
    bool rect_pipeline_failed;
    size_t rect_batch_count;
    rui_rect_batch_vertex_t rect_batch[RUI_RECT_BATCH_MAX_VERTICES];
    GLuint text_program;
    GLint text_pos_loc;
    GLint text_uv_loc;
    GLint text_color_loc;
    GLint text_tex_loc;
    GLuint text_vbo;
    bool text_pipeline_failed;
    GLuint image_program;
    GLint image_pos_loc;
    GLint image_uv_loc;
    GLint image_tex_loc;
    GLuint image_vbo;
    bool image_pipeline_failed;
    size_t image_batch_count;
    size_t image_vertex_count;
    rui_image_batch_item_t image_batch[RUI_IMAGE_BATCH_MAX_QUADS];
    rui_image_batch_vertex_t image_vertices[RUI_IMAGE_BATCH_MAX_VERTICES];
    size_t text_batch_count;
    size_t text_vertex_count;
    rui_text_batch_item_t text_batch[RUI_TEXT_BATCH_MAX_QUADS];
    rui_text_batch_vertex_t text_vertices[RUI_TEXT_BATCH_MAX_VERTICES];
    uint32_t text_epoch;
    uint32_t text_budget_frame;
    uint32_t text_budget_used;
    bool text_deferred_this_frame;
    rui_text_cache_entry_t text_cache[RUI_TEXT_CACHE_MAX];
    uint32_t image_epoch;
    uint32_t image_budget_frame;
    uint32_t image_budget_used;
    bool image_deferred_this_frame;
    rui_image_cache_entry_t image_cache[RUI_IMAGE_CACHE_MAX];
    bool cursor_active;
    uint32_t cursor_handle;
    uint32_t cursor_pitch;
    uint64_t cursor_size;
    int32_t cursor_hot_x;
    int32_t cursor_hot_y;
    int32_t cursor_x;
    int32_t cursor_y;
    GLuint cursor_texture;
    bool cursor_texture_ready;
    int32_t cursor_gl_hot_x;
    int32_t cursor_gl_hot_y;
};

static void rui_flush_rect_batch(rui_context_t *ctx);
static void rui_flush_round_batch(rui_context_t *ctx);
static void rui_flush_image_batch(rui_context_t *ctx);
static void rui_flush_text_batch(rui_context_t *ctx);

static uint64_t rui_now_ms(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) == 0)
        return (uint64_t)ts.tv_sec * 1000ULL + (uint64_t)ts.tv_nsec / 1000000ULL;
    return 0;
}

static void rui_write_all(const char *text, size_t len) {
    while (len > 0) {
        ssize_t wrote = write(2, text, len);
        if (wrote <= 0) return;
        text += (size_t)wrote;
        len -= (size_t)wrote;
    }
}

static void rui_logf(const char *fmt, ...) {
    char buf[512];
    va_list ap;
    int len;

    va_start(ap, fmt);
    len = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (len < 0) return;
    if ((size_t)len >= sizeof(buf)) len = (int)sizeof(buf) - 1;
    rui_write_all(buf, (size_t)len);
}

static const char *rui_safe_str(const char *s) {
    return s ? s : "(null)";
}

static bool rui_ascii_eq_ci(char a, char b) {
    if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
    if (b >= 'A' && b <= 'Z') b = (char)(b - 'A' + 'a');
    return a == b;
}

static bool rui_contains_ci(const char *text, const char *needle) {
    size_t i;
    size_t n;
    if (!text || !needle || !needle[0]) return false;
    n = strlen(needle);
    for (i = 0; text[i]; ++i) {
        size_t j;
        for (j = 0; j < n && text[i + j] && rui_ascii_eq_ci(text[i + j], needle[j]); ++j) {}
        if (j == n) return true;
    }
    return false;
}

static bool rui_renderer_looks_software(const char *probe) {
    return rui_contains_ci(probe, "llvmpipe") ||
           rui_contains_ci(probe, "softpipe") ||
           rui_contains_ci(probe, "swrast") ||
           rui_contains_ci(probe, "kms_swrast") ||
           rui_contains_ci(probe, "lavapipe") ||
           rui_contains_ci(probe, "software rasterizer");
}

static bool rui_renderer_looks_hardware(const char *probe) {
    return rui_contains_ci(probe, "virgl") ||
           rui_contains_ci(probe, "svga3d") ||
           rui_contains_ci(probe, "vmware") ||
           rui_contains_ci(probe, "vmwgfx") ||
           rui_contains_ci(probe, "iris") ||
           rui_contains_ci(probe, "intel") ||
           rui_contains_ci(probe, "radeon") ||
           rui_contains_ci(probe, "radeonsi") ||
           rui_contains_ci(probe, "amdgpu") ||
           rui_contains_ci(probe, "nouveau") ||
           rui_contains_ci(probe, "nvidia") ||
           rui_contains_ci(probe, "zink");
}

static void rui_log_errno(const char *what) {
    rui_logf("[ridux-ui] %s failed: errno=%d (%s)\n",
             what, errno, strerror(errno));
}

static bool rui_env_enabled(const char *name) {
    const char *value = getenv(name);
    return value && value[0] && strcmp(value, "0") != 0;
}

static bool rui_sync_pageflip_enabled(void) {
    static int cached = -1;
    if (cached < 0) {
        cached = (rui_env_enabled("RIDUX_UI_SYNC_PAGEFLIP") ||
                  access("/etc/ridux-ui-sync-pageflip.enable", F_OK) == 0) ? 1 : 0;
    }
    return cached != 0;
}

static GLuint rui_compile_shader(GLenum type, const char *src) {
    GLuint shader = glCreateShader(type);
    GLint ok = 0;
    if (!shader) return 0;
    glShaderSource(shader, 1, &src, NULL);
    glCompileShader(shader);
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512];
        GLsizei len = 0;
        glGetShaderInfoLog(shader, sizeof(log), &len, log);
        rui_logf("[ridux-ui] shader compile failed: %s\n", len ? log : "(no log)");
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

static GLuint rui_link_program(const char *vs_src, const char *fs_src) {
    GLuint vs = rui_compile_shader(GL_VERTEX_SHADER, vs_src);
    GLuint fs = rui_compile_shader(GL_FRAGMENT_SHADER, fs_src);
    GLuint prog;
    GLint ok = 0;
    if (!vs || !fs) {
        if (vs) glDeleteShader(vs);
        if (fs) glDeleteShader(fs);
        return 0;
    }
    prog = glCreateProgram();
    if (!prog) {
        glDeleteShader(vs);
        glDeleteShader(fs);
        return 0;
    }
    glAttachShader(prog, vs);
    glAttachShader(prog, fs);
    glLinkProgram(prog);
    glDeleteShader(vs);
    glDeleteShader(fs);
    glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[512];
        GLsizei len = 0;
        glGetProgramInfoLog(prog, sizeof(log), &len, log);
        rui_logf("[ridux-ui] shader link failed: %s\n", len ? log : "(no log)");
        glDeleteProgram(prog);
        return 0;
    }
    return prog;
}

static bool rui_init_solid_pipeline(rui_context_t *ctx) {
    static const char *vs =
        "attribute vec2 a_pos;\n"
        "void main(){ gl_Position = vec4(a_pos, 0.0, 1.0); }\n";
    static const char *fs =
        "precision highp float;\n"
        "uniform vec4 u_color;\n"
        "void main(){ gl_FragColor = u_color; }\n";
    if (!ctx || ctx->solid_pipeline_failed) return false;
    if (ctx->solid_program) return true;
    ctx->solid_program = rui_link_program(vs, fs);
    if (!ctx->solid_program) {
        ctx->solid_pipeline_failed = true;
        return false;
    }
    ctx->solid_pos_loc = glGetAttribLocation(ctx->solid_program, "a_pos");
    ctx->solid_color_loc = glGetUniformLocation(ctx->solid_program, "u_color");
    glGenBuffers(1, &ctx->solid_vbo);
    if (ctx->solid_pos_loc < 0 || ctx->solid_color_loc < 0 || !ctx->solid_vbo) {
        ctx->solid_pipeline_failed = true;
        return false;
    }
    return true;
}

static bool rui_init_round_pipeline(rui_context_t *ctx) {
    static const char *vs =
        "attribute vec2 a_pos;\n"
        "attribute vec4 a_rect;\n"
        "attribute float a_radius;\n"
        "attribute vec4 a_top;\n"
        "attribute vec4 a_bottom;\n"
        "uniform vec2 u_screen;\n"
        "varying vec2 v_pos;\n"
        "varying vec4 v_rect;\n"
        "varying float v_radius;\n"
        "varying vec4 v_top;\n"
        "varying vec4 v_bottom;\n"
        "void main(){\n"
        "  v_pos = a_pos;\n"
        "  v_rect = a_rect;\n"
        "  v_radius = a_radius;\n"
        "  v_top = a_top;\n"
        "  v_bottom = a_bottom;\n"
        "  gl_Position = vec4(a_pos.x / u_screen.x * 2.0 - 1.0,\n"
        "                     1.0 - a_pos.y / u_screen.y * 2.0, 0.0, 1.0);\n"
        "}\n";
    static const char *fs =
        "precision highp float;\n"
        "varying vec2 v_pos;\n"
        "varying vec4 v_rect;\n"
        "varying float v_radius;\n"
        "varying vec4 v_top;\n"
        "varying vec4 v_bottom;\n"
        "void main(){\n"
        "  vec2 center = v_rect.xy + v_rect.zw * 0.5;\n"
        "  vec2 q = abs(v_pos - center) - (v_rect.zw * 0.5 - vec2(v_radius));\n"
        "  float d = length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - v_radius;\n"
        "  float edge = 1.0 - smoothstep(-0.75, 0.75, d);\n"
        "  float t = clamp((v_pos.y - v_rect.y) / max(v_rect.w, 1.0), 0.0, 1.0);\n"
        "  vec4 c = mix(v_top, v_bottom, t);\n"
        "  gl_FragColor = vec4(c.rgb, c.a * edge);\n"
        "}\n";
    if (!ctx || ctx->round_pipeline_failed) return false;
    if (ctx->round_program) return true;
    ctx->round_program = rui_link_program(vs, fs);
    if (!ctx->round_program) {
        ctx->round_pipeline_failed = true;
        return false;
    }
    ctx->round_pos_loc = glGetAttribLocation(ctx->round_program, "a_pos");
    ctx->round_screen_loc = glGetUniformLocation(ctx->round_program, "u_screen");
    ctx->round_rect_loc = glGetAttribLocation(ctx->round_program, "a_rect");
    ctx->round_radius_loc = glGetAttribLocation(ctx->round_program, "a_radius");
    ctx->round_top_loc = glGetAttribLocation(ctx->round_program, "a_top");
    ctx->round_bottom_loc = glGetAttribLocation(ctx->round_program, "a_bottom");
    glGenBuffers(1, &ctx->round_vbo);
    if (ctx->round_pos_loc < 0 || ctx->round_screen_loc < 0 ||
        ctx->round_rect_loc < 0 || ctx->round_radius_loc < 0 ||
        ctx->round_top_loc < 0 || ctx->round_bottom_loc < 0 || !ctx->round_vbo) {
        ctx->round_pipeline_failed = true;
        return false;
    }
    return true;
}

static bool rui_init_rect_batch_pipeline(rui_context_t *ctx) {
    static const char *vs =
        "attribute vec2 a_pos;\n"
        "attribute vec4 a_color;\n"
        "varying vec4 v_color;\n"
        "void main(){ v_color = a_color; gl_Position = vec4(a_pos, 0.0, 1.0); }\n";
    static const char *fs =
        "precision highp float;\n"
        "varying vec4 v_color;\n"
        "void main(){ gl_FragColor = v_color; }\n";
    if (!ctx || ctx->rect_pipeline_failed) return false;
    if (ctx->rect_program) return true;
    ctx->rect_program = rui_link_program(vs, fs);
    if (!ctx->rect_program) {
        ctx->rect_pipeline_failed = true;
        return false;
    }
    ctx->rect_pos_loc = glGetAttribLocation(ctx->rect_program, "a_pos");
    ctx->rect_color_loc = glGetAttribLocation(ctx->rect_program, "a_color");
    glGenBuffers(1, &ctx->rect_vbo);
    if (ctx->rect_pos_loc < 0 || ctx->rect_color_loc < 0 || !ctx->rect_vbo) {
        ctx->rect_pipeline_failed = true;
        return false;
    }
    return true;
}

static bool rui_init_text_pipeline(rui_context_t *ctx) {
    static const char *vs =
        "attribute vec2 a_pos;\n"
        "attribute vec2 a_uv;\n"
        "varying vec2 v_uv;\n"
        "void main(){ v_uv = a_uv; gl_Position = vec4(a_pos, 0.0, 1.0); }\n";
    static const char *fs =
        "precision highp float;\n"
        "uniform sampler2D u_tex;\n"
        "uniform vec4 u_color;\n"
        "varying vec2 v_uv;\n"
        "void main(){ float a = texture2D(u_tex, v_uv).a; gl_FragColor = vec4(u_color.rgb, u_color.a * a); }\n";
    if (!ctx || ctx->text_pipeline_failed) return false;
    if (ctx->text_program) return true;
    ctx->text_program = rui_link_program(vs, fs);
    if (!ctx->text_program) {
        ctx->text_pipeline_failed = true;
        return false;
    }
    ctx->text_pos_loc = glGetAttribLocation(ctx->text_program, "a_pos");
    ctx->text_uv_loc = glGetAttribLocation(ctx->text_program, "a_uv");
    ctx->text_color_loc = glGetUniformLocation(ctx->text_program, "u_color");
    ctx->text_tex_loc = glGetUniformLocation(ctx->text_program, "u_tex");
    glGenBuffers(1, &ctx->text_vbo);
    if (ctx->text_pos_loc < 0 || ctx->text_uv_loc < 0 ||
        ctx->text_color_loc < 0 || ctx->text_tex_loc < 0 || !ctx->text_vbo) {
        ctx->text_pipeline_failed = true;
        return false;
    }
    return true;
}

static bool rui_init_image_pipeline(rui_context_t *ctx) {
    static const char *vs =
        "attribute vec2 a_pos;\n"
        "attribute vec2 a_uv;\n"
        "varying vec2 v_uv;\n"
        "void main(){ v_uv = a_uv; gl_Position = vec4(a_pos, 0.0, 1.0); }\n";
    static const char *fs =
        "precision highp float;\n"
        "uniform sampler2D u_tex;\n"
        "varying vec2 v_uv;\n"
        "void main(){ gl_FragColor = texture2D(u_tex, v_uv); }\n";
    if (!ctx || ctx->image_pipeline_failed) return false;
    if (ctx->image_program) return true;
    ctx->image_program = rui_link_program(vs, fs);
    if (!ctx->image_program) {
        ctx->image_pipeline_failed = true;
        return false;
    }
    ctx->image_pos_loc = glGetAttribLocation(ctx->image_program, "a_pos");
    ctx->image_uv_loc = glGetAttribLocation(ctx->image_program, "a_uv");
    ctx->image_tex_loc = glGetUniformLocation(ctx->image_program, "u_tex");
    glGenBuffers(1, &ctx->image_vbo);
    if (ctx->image_pos_loc < 0 || ctx->image_uv_loc < 0 ||
        ctx->image_tex_loc < 0 || !ctx->image_vbo) {
        ctx->image_pipeline_failed = true;
        return false;
    }
    return true;
}

rui_color_t rui_rgba(float r, float g, float b, float a) {
    rui_color_t c;
    c.r = r;
    c.g = g;
    c.b = b;
    c.a = a;
    return c;
}

rui_color_t rui_rgb8(uint8_t r, uint8_t g, uint8_t b) {
    return rui_rgba((float)r / 255.0f, (float)g / 255.0f, (float)b / 255.0f, 1.0f);
}

static bool rui_choose_crtc(int fd, drmModeRes *res, drmModeConnector *conn,
                            uint32_t *crtc_id_out) {
    int e;
    if (!res || !conn || !crtc_id_out) return false;
    if (conn->encoder_id) {
        drmModeEncoder *enc = drmModeGetEncoder(fd, conn->encoder_id);
        if (enc) {
            if (enc->crtc_id) {
                *crtc_id_out = enc->crtc_id;
                drmModeFreeEncoder(enc);
                return true;
            }
            drmModeFreeEncoder(enc);
        }
    }
    for (e = 0; e < conn->count_encoders; ++e) {
        drmModeEncoder *enc = drmModeGetEncoder(fd, conn->encoders[e]);
        int c;
        if (!enc) continue;
        for (c = 0; c < res->count_crtcs; ++c) {
            if (enc->possible_crtcs & (1 << c)) {
                *crtc_id_out = res->crtcs[c];
                drmModeFreeEncoder(enc);
                return true;
            }
        }
        drmModeFreeEncoder(enc);
    }
    return false;
}

static bool rui_open_drm_output(rui_drm_output_t *out) {
    static const char *cards[] = {
        "/dev/dri/card0",
        "/dev/dri/card1",
        "/dev/dri/card2"
    };
    size_t card_index;

    memset(out, 0, sizeof(*out));
    out->fd = -1;
    for (card_index = 0; card_index < sizeof(cards) / sizeof(cards[0]); ++card_index) {
        drmModeRes *res;
        drmModeConnector *best = NULL;
        uint32_t crtc_id = 0;
        int fd = open(cards[card_index], O_RDWR | O_CLOEXEC);
        int pass;
        if (fd < 0) {
            rui_logf("[ridux-ui] open %s failed: errno=%d (%s)\n",
                     cards[card_index], errno, strerror(errno));
            continue;
        }
        res = drmModeGetResources(fd);
        if (!res) {
            rui_log_errno("drmModeGetResources");
            close(fd);
            continue;
        }
        for (pass = 0; pass < 2 && !best; ++pass) {
            int i;
            for (i = 0; i < res->count_connectors; ++i) {
                drmModeConnector *conn = drmModeGetConnector(fd, res->connectors[i]);
                if (!conn) continue;
                if ((pass || conn->connection == DRM_MODE_CONNECTED) &&
                    conn->count_modes > 0 &&
                    rui_choose_crtc(fd, res, conn, &crtc_id)) {
                    best = conn;
                    break;
                }
                drmModeFreeConnector(conn);
            }
        }
        if (!best) {
            rui_logf("[ridux-ui] no usable KMS connector on %s\n", cards[card_index]);
            drmModeFreeResources(res);
            close(fd);
            continue;
        }
        out->fd = fd;
        out->connector_id = best->connector_id;
        out->crtc_id = crtc_id;
        out->mode = best->modes[0];
        out->saved_crtc = drmModeGetCrtc(fd, crtc_id);
        rui_logf("[ridux-ui] drm card=%s connector=%u crtc=%u mode=%ux%u@%u\n",
                 cards[card_index], out->connector_id, out->crtc_id,
                 out->mode.hdisplay, out->mode.vdisplay, out->mode.vrefresh);
        drmModeFreeConnector(best);
        drmModeFreeResources(res);
        return true;
    }
    return false;
}

static bool rui_gbm_format_supported(struct gbm_device *gbm, uint32_t format) {
    uint32_t flags = GBM_BO_USE_SCANOUT | GBM_BO_USE_RENDERING;
    if (!format) return false;
    return gbm_device_is_format_supported(gbm, format, flags) != 0;
}

static bool rui_preferred_scanout_format(uint32_t format) {
    return format == GBM_FORMAT_XRGB8888 || format == GBM_FORMAT_ARGB8888;
}

static bool rui_choose_egl_config(rui_context_t *ctx,
                                  EGLConfig *config_out,
                                  uint32_t *format_out) {
    EGLConfig configs[64];
    EGLint count = 0;
    EGLint i;
    EGLConfig fallback = 0;
    uint32_t fallback_format = GBM_FORMAT_XRGB8888;

    if (!eglGetConfigs(ctx->display, configs, (EGLint)(sizeof(configs) / sizeof(configs[0])), &count) ||
        count <= 0) {
        rui_logf("[ridux-ui] eglGetConfigs failed: egl=0x%x\n", eglGetError());
        return false;
    }

    for (i = 0; i < count; ++i) {
        EGLint surface_type = 0;
        EGLint renderable = 0;
        EGLint red = 0;
        EGLint green = 0;
        EGLint blue = 0;
        EGLint depth = 0;
        EGLint stencil = 0;
        EGLint visual = 0;
        uint32_t format;

        eglGetConfigAttrib(ctx->display, configs[i], EGL_SURFACE_TYPE, &surface_type);
        eglGetConfigAttrib(ctx->display, configs[i], EGL_RENDERABLE_TYPE, &renderable);
        eglGetConfigAttrib(ctx->display, configs[i], EGL_RED_SIZE, &red);
        eglGetConfigAttrib(ctx->display, configs[i], EGL_GREEN_SIZE, &green);
        eglGetConfigAttrib(ctx->display, configs[i], EGL_BLUE_SIZE, &blue);
        eglGetConfigAttrib(ctx->display, configs[i], EGL_DEPTH_SIZE, &depth);
        eglGetConfigAttrib(ctx->display, configs[i], EGL_STENCIL_SIZE, &stencil);
        eglGetConfigAttrib(ctx->display, configs[i], EGL_NATIVE_VISUAL_ID, &visual);

        if (!(surface_type & EGL_WINDOW_BIT)) continue;
        if (!(renderable & EGL_OPENGL_ES2_BIT)) continue;
        if (red < 8 || green < 8 || blue < 8) continue;
        if (depth != 0 || stencil != 0) continue;

        format = visual ? (uint32_t)visual : GBM_FORMAT_XRGB8888;
        if (!rui_gbm_format_supported(ctx->gbm, format)) continue;
        if (rui_preferred_scanout_format(format)) {
            *config_out = configs[i];
            *format_out = format;
            return true;
        }
        if (!fallback) {
            fallback = configs[i];
            fallback_format = format;
        }
    }

    if (fallback) {
        *config_out = fallback;
        *format_out = fallback_format;
        return true;
    }

    return false;
}

static bool rui_init_egl(rui_context_t *ctx) {
    EGLint major = 0;
    EGLint minor = 0;
    uint32_t gbm_format = GBM_FORMAT_XRGB8888;
    EGLConfig config;
    EGLint context_attribs[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
    rui_egl_get_platform_display_fn get_platform_display;

    ctx->gbm = gbm_create_device(ctx->out.fd);
    if (!ctx->gbm) {
        rui_log_errno("gbm_create_device");
        return false;
    }
    get_platform_display =
        (rui_egl_get_platform_display_fn)eglGetProcAddress("eglGetPlatformDisplayEXT");
    ctx->display = get_platform_display
        ? get_platform_display(EGL_PLATFORM_GBM_KHR, ctx->gbm, NULL)
        : EGL_NO_DISPLAY;
    if (ctx->display == EGL_NO_DISPLAY)
        ctx->display = eglGetDisplay((EGLNativeDisplayType)ctx->gbm);
    if (ctx->display == EGL_NO_DISPLAY) {
        rui_logf("[ridux-ui] eglGetDisplay failed\n");
        return false;
    }
    if (!eglInitialize(ctx->display, &major, &minor)) {
        rui_logf("[ridux-ui] eglInitialize failed: egl=0x%x\n", eglGetError());
        return false;
    }
    if (!eglBindAPI(EGL_OPENGL_ES_API)) {
        rui_logf("[ridux-ui] eglBindAPI failed: egl=0x%x\n", eglGetError());
        return false;
    }
    if (!rui_choose_egl_config(ctx, &config, &gbm_format)) {
        rui_logf("[ridux-ui] eglChooseConfig failed: egl=0x%x\n", eglGetError());
        return false;
    }
    rui_logf("[ridux-ui] GBM visual=0x%x\n", gbm_format);
    ctx->surface = gbm_surface_create(ctx->gbm,
                                      ctx->out.mode.hdisplay,
                                      ctx->out.mode.vdisplay,
                                      gbm_format,
                                      GBM_BO_USE_SCANOUT | GBM_BO_USE_RENDERING);
    if (!ctx->surface) {
        rui_log_errno("gbm_surface_create");
        return false;
    }
    ctx->egl_context = eglCreateContext(ctx->display, config, EGL_NO_CONTEXT, context_attribs);
    if (ctx->egl_context == EGL_NO_CONTEXT) {
        rui_logf("[ridux-ui] eglCreateContext failed: egl=0x%x\n", eglGetError());
        return false;
    }
    ctx->egl_surface = eglCreateWindowSurface(ctx->display, config,
                                              (EGLNativeWindowType)ctx->surface, NULL);
    if (ctx->egl_surface == EGL_NO_SURFACE) {
        rui_logf("[ridux-ui] eglCreateWindowSurface failed: egl=0x%x\n", eglGetError());
        return false;
    }
    if (!eglMakeCurrent(ctx->display, ctx->egl_surface, ctx->egl_surface, ctx->egl_context)) {
        rui_logf("[ridux-ui] eglMakeCurrent failed: egl=0x%x\n", eglGetError());
        return false;
    }
    if (!eglSwapInterval(ctx->display, 0))
        rui_logf("[ridux-ui] eglSwapInterval(0) ignored: egl=0x%x\n", eglGetError());

    {
        const char *gl_vendor = (const char *)glGetString(GL_VENDOR);
        const char *gl_renderer = (const char *)glGetString(GL_RENDERER);
        const char *gl_version = (const char *)glGetString(GL_VERSION);
        char probe[384];

        snprintf(ctx->renderer, sizeof(ctx->renderer), "%s",
                 gl_renderer ? gl_renderer : "unknown");
        snprintf(probe, sizeof(probe), "%s %s %s",
                 rui_safe_str(gl_vendor),
                 rui_safe_str(gl_renderer),
                 rui_safe_str(gl_version));
        ctx->frame.renderer = ctx->renderer;
        rui_logf("[ridux-ui] EGL %d.%d vendor=%s client_apis=%s\n",
                 major, minor,
                 rui_safe_str(eglQueryString(ctx->display, EGL_VENDOR)),
                 rui_safe_str(eglQueryString(ctx->display, EGL_CLIENT_APIS)));
        rui_logf("[ridux-ui] GLES vendor=%s renderer=%s version=%s\n",
                 rui_safe_str(gl_vendor),
                 rui_safe_str(gl_renderer),
                 rui_safe_str(gl_version));
        if (ctx->options.require_hardware &&
            (!gl_renderer || rui_renderer_looks_software(probe) ||
             !rui_renderer_looks_hardware(probe))) {
            rui_logf("[ridux-ui] rejected software/unknown GL renderer vendor=%s renderer=%s version=%s\n",
                     rui_safe_str(gl_vendor),
                     rui_safe_str(gl_renderer),
                     rui_safe_str(gl_version));
            return false;
        }
        rui_logf("[ridux-mesa-real] renderer=%s surface=riduxui-drm status=hardware-required accepted\n",
                 ctx->renderer);
    }
    return true;
}

bool rui_open(rui_context_t **out, const rui_options_t *options) {
    rui_context_t *ctx;
    if (!out) return false;
    *out = NULL;
    ctx = (rui_context_t *)calloc(1, sizeof(*ctx));
    if (!ctx) return false;
    ctx->options.backend = RUI_BACKEND_DRM;
    ctx->options.require_hardware = true;
    if (options) ctx->options = *options;
    if (ctx->options.backend != RUI_BACKEND_DRM) {
        rui_logf("[ridux-ui] Wayland backend is reserved; using DRM backend now\n");
        ctx->options.backend = RUI_BACKEND_DRM;
    }
    if (ctx->options.immediate_present ||
        rui_env_enabled("RIDUX_GBM_IMMEDIATE_PRESENT") ||
        access("/etc/ridux-gbm-immediate-present.enable", F_OK) == 0) {
        rui_logf("[ridux-ui] immediate-setcrtc requested but disabled for pipelined present\n");
        ctx->options.immediate_present = false;
    }
    ctx->display = EGL_NO_DISPLAY;
    ctx->egl_context = EGL_NO_CONTEXT;
    ctx->egl_surface = EGL_NO_SURFACE;
    ctx->page_flip_ok = true;
    ctx->first_frame = true;
    rui_logf("[ridux-ui] present mode=%s\n",
             rui_sync_pageflip_enabled() ? "pageflip-sync" : "pageflip-async");
    if (!rui_open_drm_output(&ctx->out) || !rui_init_egl(ctx)) {
        rui_close(ctx);
        return false;
    }
    ctx->frame.width = ctx->out.mode.hdisplay;
    ctx->frame.height = ctx->out.mode.vdisplay;
    *out = ctx;
    return true;
}

static void rui_destroy_cached_fb(struct gbm_bo *bo, void *data) {
    rui_gbm_fb_t *cached = (rui_gbm_fb_t *)data;
    (void)bo;
    if (!cached) return;
    if (cached->fb_id)
        drmModeRmFB(cached->fd, cached->fb_id);
    free(cached);
}

static int rui_add_fb_for_bo(int fd, struct gbm_bo *bo, rui_fb_t *fb) {
    uint32_t width = gbm_bo_get_width(bo);
    uint32_t height = gbm_bo_get_height(bo);
    uint32_t stride = gbm_bo_get_stride(bo);
    uint32_t handle = gbm_bo_get_handle(bo).u32;
    rui_gbm_fb_t *cached = (rui_gbm_fb_t *)gbm_bo_get_user_data(bo);
    int rc;
    memset(fb, 0, sizeof(*fb));
    fb->bo = bo;
    if (cached && cached->fb_id) {
        fb->fb_id = cached->fb_id;
        fb->cached_fb = true;
        return 0;
    }
    rc = drmModeAddFB(fd, width, height, 24, 32, stride, handle, &fb->fb_id);
    if (rc != 0) rui_log_errno("drmModeAddFB");
    if (rc == 0) {
        cached = (rui_gbm_fb_t *)calloc(1, sizeof(*cached));
        if (cached) {
            cached->fd = fd;
            cached->fb_id = fb->fb_id;
            gbm_bo_set_user_data(bo, cached, rui_destroy_cached_fb);
            fb->cached_fb = true;
        }
    }
    return rc;
}

static void rui_release_fb(rui_context_t *ctx, rui_fb_t *fb) {
    if (!ctx || !fb) return;
    if (fb->fb_id && !fb->cached_fb) {
        drmModeRmFB(ctx->out.fd, fb->fb_id);
    }
    fb->fb_id = 0;
    fb->cached_fb = false;
    if (fb->bo) {
        gbm_surface_release_buffer(ctx->surface, fb->bo);
        fb->bo = NULL;
    }
}

static void rui_page_flip_done(int fd, unsigned int frame, unsigned int sec,
                               unsigned int usec, void *data) {
    int *waiting = (int *)data;
    (void)fd;
    (void)frame;
    (void)sec;
    (void)usec;
    *waiting = 0;
}

static bool rui_wait_for_page_flip(int fd, int *waiting) {
    drmEventContext ev;
    memset(&ev, 0, sizeof(ev));
    ev.version = DRM_EVENT_CONTEXT_VERSION;
    ev.page_flip_handler = rui_page_flip_done;
    while (*waiting) {
        fd_set fds;
        int rc;
        FD_ZERO(&fds);
        FD_SET(fd, &fds);
        rc = select(fd + 1, &fds, NULL, NULL, NULL);
        if (rc < 0) {
            if (errno == EINTR) continue;
            rui_log_errno("select pageflip");
            return false;
        }
        if (drmHandleEvent(fd, &ev) != 0) {
            rui_log_errno("drmHandleEvent");
            return false;
        }
    }
    return true;
}

const rui_frame_info_t *rui_frame_info(const rui_context_t *ctx) {
    return ctx ? &ctx->frame : NULL;
}

static uint32_t rui_read_u32_le(const unsigned char *p) {
    return (uint32_t)p[0] |
           ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static bool rui_read_file_limited(const char *path, unsigned char **out, size_t *size_out) {
    FILE *fp;
    long len;
    unsigned char *data;
    if (!path || !out || !size_out) return false;
    *out = NULL;
    *size_out = 0;
    fp = fopen(path, "rb");
    if (!fp) return false;
    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return false;
    }
    len = ftell(fp);
    if (len <= 0 || len > 1024 * 1024) {
        fclose(fp);
        return false;
    }
    if (fseek(fp, 0, SEEK_SET) != 0) {
        fclose(fp);
        return false;
    }
    data = (unsigned char *)malloc((size_t)len);
    if (!data) {
        fclose(fp);
        return false;
    }
    if (fread(data, 1, (size_t)len, fp) != (size_t)len) {
        free(data);
        fclose(fp);
        return false;
    }
    fclose(fp);
    *out = data;
    *size_out = (size_t)len;
    return true;
}

static bool rui_load_xcursor_image(const char *path, uint32_t *dst,
                                   int *hot_x, int *hot_y) {
    unsigned char *data = NULL;
    size_t size = 0;
    uint32_t header_len;
    uint32_t ntoc;
    int best_toc = -1;
    int best_score = -1000000;
    if (!dst || !hot_x || !hot_y) return false;
    if (!rui_read_file_limited(path, &data, &size)) return false;
    if (size < 16 ||
        rui_read_u32_le(data + 0) != RUI_XCURSOR_MAGIC) {
        free(data);
        return false;
    }
    header_len = rui_read_u32_le(data + 4);
    ntoc = rui_read_u32_le(data + 12);
    if (header_len < 16 || header_len > size || ntoc > 128 ||
        header_len + (uint64_t)ntoc * 12ULL > size) {
        free(data);
        return false;
    }
    for (uint32_t i = 0; i < ntoc; ++i) {
        size_t toc = (size_t)header_len + (size_t)i * 12u;
        uint32_t type = rui_read_u32_le(data + toc + 0);
        uint32_t subtype = rui_read_u32_le(data + toc + 4);
        uint32_t pos = rui_read_u32_le(data + toc + 8);
        uint32_t chunk_header_len, width, height;
        int score;
        if (type != RUI_XCURSOR_IMAGE_TYPE) continue;
        if (pos + 36u > size) continue;
        chunk_header_len = rui_read_u32_le(data + pos + 0);
        if (chunk_header_len < 36u || (uint64_t)pos + chunk_header_len > size) continue;
        if (rui_read_u32_le(data + pos + 4) != RUI_XCURSOR_IMAGE_TYPE) continue;
        width = rui_read_u32_le(data + pos + 16);
        height = rui_read_u32_le(data + pos + 20);
        if (width == 0 || height == 0 || width > RUI_CURSOR_W || height > RUI_CURSOR_H) continue;
        if ((uint64_t)pos + chunk_header_len +
            (uint64_t)width * (uint64_t)height * 4ULL > size) continue;
        score = 10000 - (int)((subtype > 32u) ? (subtype - 32u) : (32u - subtype)) * 100 +
                (int)subtype;
        if (score > best_score) {
            best_score = score;
            best_toc = (int)i;
        }
    }
    if (best_toc >= 0) {
        size_t toc = (size_t)header_len + (size_t)best_toc * 12u;
        uint32_t pos = rui_read_u32_le(data + toc + 8);
        uint32_t chunk_header_len = rui_read_u32_le(data + pos + 0);
        uint32_t width = rui_read_u32_le(data + pos + 16);
        uint32_t height = rui_read_u32_le(data + pos + 20);
        const unsigned char *px = data + pos + chunk_header_len;
        memset(dst, 0, RUI_CURSOR_W * RUI_CURSOR_H * sizeof(dst[0]));
        for (uint32_t y = 0; y < height; ++y) {
            for (uint32_t x = 0; x < width; ++x) {
                dst[y * RUI_CURSOR_W + x] =
                    rui_read_u32_le(px + ((size_t)y * width + x) * 4u);
            }
        }
        *hot_x = (int)rui_read_u32_le(data + pos + 24);
        *hot_y = (int)rui_read_u32_le(data + pos + 28);
        free(data);
        return true;
    }
    free(data);
    return false;
}

static void rui_make_adwaita_fallback_cursor(uint32_t *dst, int *hot_x, int *hot_y) {
    static const char *shape[] = {
        "BSS........................",
        "BWSS.......................",
        "BWWSS......................",
        "BWWWSS.....................",
        "BWWWWSS....................",
        "BWWWWWSS...................",
        "BWWWWWWSS..................",
        "BWWWWWWWSS.................",
        "BWWWWWWWWSS................",
        "BWWWWWWWWWSS...............",
        "BWWWWWWWWWWSS..............",
        "BWWWWWWWWWWWSS.............",
        "BWWWWWWWWWWWWSS............",
        "BWWWWWWWWWWWWWSS...........",
        "BWWWWWWWWWWWWWWSS..........",
        "BWWWWWWBBBBBBBBSS..........",
        "BWWWWBWSS..................",
        "BWWB.BWSS..................",
        "BWB..BWWSS.................",
        "BB...BWWSS.................",
        ".....BWWSS.................",
        ".....BWWSS.................",
        "......BBSS.................",
        ".......SS.................."
    };
    memset(dst, 0, RUI_CURSOR_W * RUI_CURSOR_H * sizeof(dst[0]));
    for (uint32_t y = 0; y < sizeof(shape) / sizeof(shape[0]); ++y) {
        const char *s = shape[y];
        for (uint32_t x = 0; s[x]; ++x) {
            if (s[x] == 'B') dst[y * RUI_CURSOR_W + x] = 0xff111318u;
            else if (s[x] == 'W') dst[y * RUI_CURSOR_W + x] = 0xfffbfbfbu;
            else if (s[x] == 'S') dst[y * RUI_CURSOR_W + x] = 0x66000000u;
        }
    }
    *hot_x = 3;
    *hot_y = 1;
}

static bool rui_load_adwaita_cursor_pixels(uint32_t *dst, int *hot_x, int *hot_y,
                                           const char **source_out) {
    static const char *paths[] = {
        "/usr/share/icons/Adwaita/cursors/default",
        "/usr/share/icons/Adwaita/cursors/left_ptr",
        "/usr/share/icons/default/cursors/left_ptr"
    };
    for (size_t i = 0; i < sizeof(paths) / sizeof(paths[0]); ++i) {
        if (rui_load_xcursor_image(paths[i], dst, hot_x, hot_y)) {
            if (source_out) *source_out = paths[i];
            return true;
        }
    }
    rui_make_adwaita_fallback_cursor(dst, hot_x, hot_y);
    if (source_out) *source_out = "builtin-adwaita-fallback";
    return true;
}

bool rui_cursor_enable_adwaita(rui_context_t *ctx, int32_t x, int32_t y) {
    rui_drm_mode_create_dumb_t create;
    rui_drm_mode_map_dumb_t map_req;
    uint32_t pixels[RUI_CURSOR_W * RUI_CURSOR_H];
    const char *source = NULL;
    int hot_x = 0;
    int hot_y = 0;
    void *map = MAP_FAILED;
    if (!ctx || ctx->cursor_active) return ctx && ctx->cursor_active;
    memset(&create, 0, sizeof(create));
    create.width = RUI_CURSOR_W;
    create.height = RUI_CURSOR_H;
    create.bpp = 32;
    if (ioctl(ctx->out.fd, DRM_IOCTL_MODE_CREATE_DUMB, &create) != 0 ||
        !create.handle || !create.pitch || !create.size) {
        rui_logf("[ridux-ui] drm cursor create failed: errno=%d (%s)\n",
                 errno, strerror(errno));
        return false;
    }
    memset(&map_req, 0, sizeof(map_req));
    map_req.handle = create.handle;
    if (ioctl(ctx->out.fd, DRM_IOCTL_MODE_MAP_DUMB, &map_req) != 0) {
        rui_logf("[ridux-ui] drm cursor map-dumb failed: errno=%d (%s)\n",
                 errno, strerror(errno));
        goto fail;
    }
    map = mmap(NULL, (size_t)create.size, PROT_READ | PROT_WRITE,
               MAP_SHARED, ctx->out.fd, (off_t)map_req.offset);
    if (map == MAP_FAILED) {
        rui_logf("[ridux-ui] drm cursor mmap failed: errno=%d (%s)\n",
                 errno, strerror(errno));
        goto fail;
    }
    rui_load_adwaita_cursor_pixels(pixels, &hot_x, &hot_y, &source);
    memset(map, 0, (size_t)create.size);
    for (uint32_t row = 0; row < RUI_CURSOR_H; ++row) {
        uint32_t *dst = (uint32_t *)((unsigned char *)map + (size_t)row * create.pitch);
        memcpy(dst, pixels + row * RUI_CURSOR_W, RUI_CURSOR_W * sizeof(uint32_t));
    }
    munmap(map, (size_t)create.size);
    map = MAP_FAILED;
    if (drmModeSetCursor2(ctx->out.fd, ctx->out.crtc_id, create.handle,
                          RUI_CURSOR_W, RUI_CURSOR_H, hot_x, hot_y) != 0) {
        rui_logf("[ridux-ui] drmModeSetCursor2 failed: errno=%d (%s)\n",
                 errno, strerror(errno));
        goto fail;
    }
    ctx->cursor_active = true;
    ctx->cursor_handle = create.handle;
    ctx->cursor_pitch = create.pitch;
    ctx->cursor_size = create.size;
    ctx->cursor_hot_x = hot_x;
    ctx->cursor_hot_y = hot_y;
    ctx->cursor_x = x;
    ctx->cursor_y = y;
    (void)rui_cursor_move(ctx, x, y);
    rui_logf("[ridux-ui] cursor mode=drm-adwaita source=%s handle=%u size=%ux%u hot=%d,%d\n",
             rui_safe_str(source), create.handle, RUI_CURSOR_W, RUI_CURSOR_H, hot_x, hot_y);
    return true;

fail:
    if (map != MAP_FAILED) munmap(map, (size_t)create.size);
    if (create.handle) {
        rui_drm_gem_close_t close_req;
        memset(&close_req, 0, sizeof(close_req));
        close_req.handle = create.handle;
        (void)ioctl(ctx->out.fd, DRM_IOCTL_MODE_DESTROY_DUMB, &close_req);
    }
    return false;
}

bool rui_cursor_move(rui_context_t *ctx, int32_t x, int32_t y) {
    if (!ctx || !ctx->cursor_active) return false;
    ctx->cursor_x = x;
    ctx->cursor_y = y;
    if (drmModeMoveCursor(ctx->out.fd, ctx->out.crtc_id, x, y) != 0) {
        rui_logf("[ridux-ui] drmModeMoveCursor failed: errno=%d (%s)\n",
                 errno, strerror(errno));
        ctx->cursor_active = false;
        return false;
    }
    return true;
}

bool rui_cursor_hardware_active(const rui_context_t *ctx) {
    return ctx && ctx->cursor_active;
}

bool rui_cursor_disable(rui_context_t *ctx) {
    bool ok = true;
    if (!ctx) return false;
    if (ctx->cursor_active) {
        if (drmModeSetCursor2(ctx->out.fd, ctx->out.crtc_id, 0, 0, 0, 0, 0) != 0) {
            rui_logf("[ridux-ui] drm cursor hide failed: errno=%d (%s)\n",
                     errno, strerror(errno));
            ok = false;
        }
    }
    if (ctx->cursor_handle) {
        rui_drm_gem_close_t close_req;
        memset(&close_req, 0, sizeof(close_req));
        close_req.handle = ctx->cursor_handle;
        (void)ioctl(ctx->out.fd, DRM_IOCTL_MODE_DESTROY_DUMB, &close_req);
    }
    ctx->cursor_active = false;
    ctx->cursor_handle = 0;
    ctx->cursor_pitch = 0;
    ctx->cursor_size = 0;
    ctx->cursor_hot_x = 0;
    ctx->cursor_hot_y = 0;
    return ok;
}

static void rui_rect_vertices(const rui_context_t *ctx, const rui_rect_t *rect,
                              float *v, bool uv) {
    float fw = (float)ctx->frame.width;
    float fh = (float)ctx->frame.height;
    float x0 = rect->x;
    float y0 = rect->y;
    float x1 = rect->x + rect->w;
    float y1 = rect->y + rect->h;
    float nx0 = x0 / fw * 2.0f - 1.0f;
    float nx1 = x1 / fw * 2.0f - 1.0f;
    float ny0 = 1.0f - y0 / fh * 2.0f;
    float ny1 = 1.0f - y1 / fh * 2.0f;
    if (uv) {
        v[0] = nx0; v[1] = ny0; v[2] = 0.0f; v[3] = 0.0f;
        v[4] = nx1; v[5] = ny0; v[6] = 1.0f; v[7] = 0.0f;
        v[8] = nx0; v[9] = ny1; v[10] = 0.0f; v[11] = 1.0f;
        v[12] = nx1; v[13] = ny1; v[14] = 1.0f; v[15] = 1.0f;
    } else {
        v[0] = nx0; v[1] = ny0;
        v[2] = nx1; v[3] = ny0;
        v[4] = nx0; v[5] = ny1;
        v[6] = nx1; v[7] = ny1;
    }
}

static bool rui_cursor_upload_texture(rui_context_t *ctx) {
    uint32_t pixels[RUI_CURSOR_W * RUI_CURSOR_H];
    unsigned char rgba[RUI_CURSOR_W * RUI_CURSOR_H * 4u];
    const char *source = NULL;
    int hot_x = 0;
    int hot_y = 0;
    if (!ctx) return false;
    if (ctx->cursor_texture_ready && ctx->cursor_texture)
        return true;
    rui_load_adwaita_cursor_pixels(pixels, &hot_x, &hot_y, &source);
    for (uint32_t i = 0; i < RUI_CURSOR_W * RUI_CURSOR_H; ++i) {
        uint32_t p = pixels[i];
        rgba[i * 4u + 0u] = (unsigned char)((p >> 16) & 0xffu);
        rgba[i * 4u + 1u] = (unsigned char)((p >> 8) & 0xffu);
        rgba[i * 4u + 2u] = (unsigned char)(p & 0xffu);
        rgba[i * 4u + 3u] = (unsigned char)((p >> 24) & 0xffu);
    }
    if (!ctx->cursor_texture)
        glGenTextures(1, &ctx->cursor_texture);
    if (!ctx->cursor_texture)
        return false;
    glBindTexture(GL_TEXTURE_2D, ctx->cursor_texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA,
                 (GLsizei)RUI_CURSOR_W, (GLsizei)RUI_CURSOR_H, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0,
                    (GLsizei)RUI_CURSOR_W, (GLsizei)RUI_CURSOR_H,
                    GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    glBindTexture(GL_TEXTURE_2D, 0);
    ctx->cursor_texture_ready = true;
    ctx->cursor_gl_hot_x = hot_x;
    ctx->cursor_gl_hot_y = hot_y;
    rui_logf("[ridux-ui] cursor fallback mode=gl-adwaita source=%s texture=%u hot=%d,%d\n",
             rui_safe_str(source), ctx->cursor_texture, hot_x, hot_y);
    return true;
}

bool rui_cursor_draw_adwaita(rui_context_t *ctx, int32_t x, int32_t y) {
    rui_rect_t rect;
    float v[16];
    if (!ctx) return false;
    if (!rui_init_image_pipeline(ctx) || !rui_cursor_upload_texture(ctx))
        return false;
    rui_flush_rect_batch(ctx);
    rui_flush_round_batch(ctx);
    rui_flush_image_batch(ctx);
    rui_flush_text_batch(ctx);
    rect.x = (float)(x - ctx->cursor_gl_hot_x);
    rect.y = (float)(y - ctx->cursor_gl_hot_y);
    rect.w = (float)RUI_CURSOR_W;
    rect.h = (float)RUI_CURSOR_H;
    rui_rect_vertices(ctx, &rect, v, true);
    glDisable(GL_SCISSOR_TEST);
    glUseProgram(ctx->image_program);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, ctx->cursor_texture);
    glUniform1i(ctx->image_tex_loc, 0);
    glBindBuffer(GL_ARRAY_BUFFER, ctx->image_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(v), v, GL_STREAM_DRAW);
    glEnableVertexAttribArray((GLuint)ctx->image_pos_loc);
    glEnableVertexAttribArray((GLuint)ctx->image_uv_loc);
    glVertexAttribPointer((GLuint)ctx->image_pos_loc, 2, GL_FLOAT, GL_FALSE,
                          4 * (GLsizei)sizeof(float), (const void *)0);
    glVertexAttribPointer((GLuint)ctx->image_uv_loc, 2, GL_FLOAT, GL_FALSE,
                          4 * (GLsizei)sizeof(float),
                          (const void *)(2 * sizeof(float)));
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glDisableVertexAttribArray((GLuint)ctx->image_pos_loc);
    glDisableVertexAttribArray((GLuint)ctx->image_uv_loc);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindTexture(GL_TEXTURE_2D, 0);
    glUseProgram(0);
    return true;
}

static GLuint rui_image_texture_get(rui_context_t *ctx, uint32_t width, uint32_t height,
                                    const uint32_t *pixels_argb) {
    rui_image_cache_entry_t *best = NULL;
    uint32_t oldest = 0xffffffffu;
    unsigned char *rgba;
    GLuint tex = 0;
    uint64_t count;
    uint64_t bytes;
    if (!ctx || !pixels_argb || !width || !height) return 0;
    if (width > 4096u || height > 4096u) return 0;
    count = (uint64_t)width * (uint64_t)height;
    bytes = count * 4ULL;
    if (count == 0 || bytes > 64ULL * 1024ULL * 1024ULL) return 0;

    if (ctx->image_budget_frame != ctx->frame.frame) {
        ctx->image_budget_frame = ctx->frame.frame;
        ctx->image_budget_used = 0;
        ctx->image_deferred_this_frame = false;
    }
    ++ctx->image_epoch;
    for (int i = 0; i < RUI_IMAGE_CACHE_MAX; ++i) {
        rui_image_cache_entry_t *e = &ctx->image_cache[i];
        if (e->used && e->pixels == pixels_argb &&
            e->width == width && e->height == height) {
            e->last_used = ctx->image_epoch;
            return e->texture;
        }
        if (!e->used) {
            best = e;
            break;
        }
        if (e->last_used < oldest) {
            oldest = e->last_used;
            best = e;
        }
    }
    if (ctx->image_budget_used >= RUI_IMAGE_NEW_TEXTURES_PER_FRAME) {
        ctx->image_deferred_this_frame = true;
        return 0;
    }
    ++ctx->image_budget_used;
    if (!best || best->used) {
        ctx->image_deferred_this_frame = true;
        return 0;
    }

    rgba = (unsigned char *)malloc((size_t)bytes);
    if (!rgba) return 0;
    for (uint64_t i = 0; i < count; ++i) {
        uint32_t p = pixels_argb[i];
        rgba[i * 4u + 0u] = (unsigned char)((p >> 16) & 0xffu);
        rgba[i * 4u + 1u] = (unsigned char)((p >> 8) & 0xffu);
        rgba[i * 4u + 2u] = (unsigned char)(p & 0xffu);
        rgba[i * 4u + 3u] = (unsigned char)((p >> 24) & 0xffu);
    }

    glGenTextures(1, &tex);
    if (!tex) {
        free(rgba);
        return 0;
    }
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, (GLsizei)width, (GLsizei)height,
                 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0,
                    (GLsizei)width, (GLsizei)height,
                    GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    glBindTexture(GL_TEXTURE_2D, 0);
    free(rgba);

    best->used = true;
    best->pixels = pixels_argb;
    best->width = width;
    best->height = height;
    best->last_used = ctx->image_epoch;
    best->texture = tex;
    return tex;
}

static GLuint rui_image_texture_find(rui_context_t *ctx, uint32_t width, uint32_t height,
                                     const uint32_t *pixels_argb) {
    if (!ctx || !pixels_argb || !width || !height) return 0;
    ++ctx->image_epoch;
    for (int i = 0; i < RUI_IMAGE_CACHE_MAX; ++i) {
        rui_image_cache_entry_t *e = &ctx->image_cache[i];
        if (e->used && e->pixels == pixels_argb &&
            e->width == width && e->height == height) {
            e->last_used = ctx->image_epoch;
            return e->texture;
        }
    }
    return 0;
}

static void rui_image_batch_push_vertex(rui_context_t *ctx, const float *v, int idx) {
    rui_image_batch_vertex_t *out = &ctx->image_vertices[ctx->image_vertex_count++];
    out->x = (GLfloat)v[idx * 4 + 0];
    out->y = (GLfloat)v[idx * 4 + 1];
    out->u = (GLfloat)v[idx * 4 + 2];
    out->v = (GLfloat)v[idx * 4 + 3];
}

static void rui_flush_image_batch(rui_context_t *ctx) {
    size_t i;
    if (!ctx || ctx->image_batch_count == 0 || ctx->image_vertex_count == 0) return;
    if (!rui_init_image_pipeline(ctx)) {
        ctx->image_batch_count = 0;
        ctx->image_vertex_count = 0;
        return;
    }
    glDisable(GL_SCISSOR_TEST);
    glUseProgram(ctx->image_program);
    glActiveTexture(GL_TEXTURE0);
    glUniform1i(ctx->image_tex_loc, 0);
    glBindBuffer(GL_ARRAY_BUFFER, ctx->image_vbo);
    glBufferData(GL_ARRAY_BUFFER,
                 (GLsizeiptr)(ctx->image_vertex_count * sizeof(ctx->image_vertices[0])),
                 ctx->image_vertices,
                 GL_STREAM_DRAW);
    glEnableVertexAttribArray((GLuint)ctx->image_pos_loc);
    glEnableVertexAttribArray((GLuint)ctx->image_uv_loc);
    glVertexAttribPointer((GLuint)ctx->image_pos_loc, 2, GL_FLOAT, GL_FALSE,
                          (GLsizei)sizeof(ctx->image_vertices[0]), (const void *)0);
    glVertexAttribPointer((GLuint)ctx->image_uv_loc, 2, GL_FLOAT, GL_FALSE,
                          (GLsizei)sizeof(ctx->image_vertices[0]),
                          (const void *)(2 * sizeof(GLfloat)));
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    for (i = 0; i < ctx->image_batch_count; ++i) {
        const rui_image_batch_item_t *item = &ctx->image_batch[i];
        glBindTexture(GL_TEXTURE_2D, item->texture);
        glDrawArrays(GL_TRIANGLE_STRIP, item->first, 4);
    }
    glDisableVertexAttribArray((GLuint)ctx->image_pos_loc);
    glDisableVertexAttribArray((GLuint)ctx->image_uv_loc);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindTexture(GL_TEXTURE_2D, 0);
    glUseProgram(0);
    ctx->image_batch_count = 0;
    ctx->image_vertex_count = 0;
}

bool rui_image_argb(rui_context_t *ctx, rui_rect_t rect,
                    uint32_t width, uint32_t height,
                    const uint32_t *pixels_argb) {
    GLuint tex;
    rui_image_batch_item_t *item;
    float v[16];
    if (!ctx || !pixels_argb || rect.w <= 0.0f || rect.h <= 0.0f) return false;
    if (!rui_init_image_pipeline(ctx)) return false;
    tex = rui_image_texture_get(ctx, width, height, pixels_argb);
    if (!tex) return false;
    rui_rect_vertices(ctx, &rect, v, true);
    if (ctx->image_batch_count + 1u > RUI_IMAGE_BATCH_MAX_QUADS ||
        ctx->image_vertex_count + 4u > RUI_IMAGE_BATCH_MAX_VERTICES)
        rui_flush_image_batch(ctx);
    if (ctx->image_batch_count + 1u > RUI_IMAGE_BATCH_MAX_QUADS ||
        ctx->image_vertex_count + 4u > RUI_IMAGE_BATCH_MAX_VERTICES)
        return false;
    item = &ctx->image_batch[ctx->image_batch_count++];
    item->texture = tex;
    item->first = (GLsizei)ctx->image_vertex_count;
    rui_image_batch_push_vertex(ctx, v, 0);
    rui_image_batch_push_vertex(ctx, v, 1);
    rui_image_batch_push_vertex(ctx, v, 2);
    rui_image_batch_push_vertex(ctx, v, 3);
    return true;
}

bool rui_image_argb_cached(rui_context_t *ctx, rui_rect_t rect,
                           uint32_t width, uint32_t height,
                           const uint32_t *pixels_argb) {
    GLuint tex;
    rui_image_batch_item_t *item;
    float v[16];
    if (!ctx || !pixels_argb || rect.w <= 0.0f || rect.h <= 0.0f) return false;
    if (!rui_init_image_pipeline(ctx)) return false;
    tex = rui_image_texture_find(ctx, width, height, pixels_argb);
    if (!tex) return false;
    rui_rect_vertices(ctx, &rect, v, true);
    if (ctx->image_batch_count + 1u > RUI_IMAGE_BATCH_MAX_QUADS ||
        ctx->image_vertex_count + 4u > RUI_IMAGE_BATCH_MAX_VERTICES)
        rui_flush_image_batch(ctx);
    if (ctx->image_batch_count + 1u > RUI_IMAGE_BATCH_MAX_QUADS ||
        ctx->image_vertex_count + 4u > RUI_IMAGE_BATCH_MAX_VERTICES)
        return false;
    item = &ctx->image_batch[ctx->image_batch_count++];
    item->texture = tex;
    item->first = (GLsizei)ctx->image_vertex_count;
    rui_image_batch_push_vertex(ctx, v, 0);
    rui_image_batch_push_vertex(ctx, v, 1);
    rui_image_batch_push_vertex(ctx, v, 2);
    rui_image_batch_push_vertex(ctx, v, 3);
    return true;
}

bool rui_image_preload_argb(rui_context_t *ctx, uint32_t width, uint32_t height,
                            const uint32_t *pixels_argb) {
    if (!ctx || !pixels_argb || !width || !height) return false;
    if (!rui_init_image_pipeline(ctx)) return false;
    return rui_image_texture_get(ctx, width, height, pixels_argb) != 0;
}

static bool rui_blend_rect(rui_context_t *ctx, rui_rect_t rect, rui_color_t color) {
    float v[8];
    if (!rui_init_solid_pipeline(ctx)) return false;
    rui_rect_vertices(ctx, &rect, v, false);
    glDisable(GL_SCISSOR_TEST);
    glUseProgram(ctx->solid_program);
    glBindBuffer(GL_ARRAY_BUFFER, ctx->solid_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(v), v, GL_STREAM_DRAW);
    glEnableVertexAttribArray((GLuint)ctx->solid_pos_loc);
    glVertexAttribPointer((GLuint)ctx->solid_pos_loc, 2, GL_FLOAT, GL_FALSE,
                          2 * (GLsizei)sizeof(float), (const void *)0);
    glUniform4f(ctx->solid_color_loc, color.r, color.g, color.b, color.a);
    if (color.a < 0.999f) {
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    } else {
        glDisable(GL_BLEND);
    }
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glDisableVertexAttribArray((GLuint)ctx->solid_pos_loc);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glUseProgram(0);
    return true;
}

static void rui_rect_batch_push(rui_context_t *ctx, float x, float y, rui_color_t color) {
    rui_rect_batch_vertex_t *v = &ctx->rect_batch[ctx->rect_batch_count++];
    v->x = (GLfloat)x;
    v->y = (GLfloat)y;
    v->r = (GLfloat)color.r;
    v->g = (GLfloat)color.g;
    v->b = (GLfloat)color.b;
    v->a = (GLfloat)color.a;
}

static void rui_flush_rect_batch(rui_context_t *ctx) {
    if (!ctx || ctx->rect_batch_count == 0) return;
    if (!rui_init_rect_batch_pipeline(ctx)) {
        ctx->rect_batch_count = 0;
        return;
    }
    glDisable(GL_SCISSOR_TEST);
    glUseProgram(ctx->rect_program);
    glBindBuffer(GL_ARRAY_BUFFER, ctx->rect_vbo);
    glBufferData(GL_ARRAY_BUFFER,
                 (GLsizeiptr)(ctx->rect_batch_count * sizeof(ctx->rect_batch[0])),
                 ctx->rect_batch,
                 GL_STREAM_DRAW);
    glEnableVertexAttribArray((GLuint)ctx->rect_pos_loc);
    glEnableVertexAttribArray((GLuint)ctx->rect_color_loc);
    glVertexAttribPointer((GLuint)ctx->rect_pos_loc, 2, GL_FLOAT, GL_FALSE,
                          (GLsizei)sizeof(ctx->rect_batch[0]), (const void *)0);
    glVertexAttribPointer((GLuint)ctx->rect_color_loc, 4, GL_FLOAT, GL_FALSE,
                          (GLsizei)sizeof(ctx->rect_batch[0]),
                          (const void *)(2 * sizeof(GLfloat)));
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDrawArrays(GL_TRIANGLES, 0, (GLsizei)ctx->rect_batch_count);
    glDisableVertexAttribArray((GLuint)ctx->rect_pos_loc);
    glDisableVertexAttribArray((GLuint)ctx->rect_color_loc);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glUseProgram(0);
    ctx->rect_batch_count = 0;
}

static bool rui_queue_rect(rui_context_t *ctx, rui_rect_t rect, rui_color_t color) {
    float fw;
    float fh;
    float x0;
    float y0;
    float x1;
    float y1;
    float nx0;
    float nx1;
    float ny0;
    float ny1;
    if (!ctx || !rui_init_rect_batch_pipeline(ctx)) return false;
    if (ctx->rect_batch_count + 6u > RUI_RECT_BATCH_MAX_VERTICES)
        rui_flush_rect_batch(ctx);
    if (ctx->rect_batch_count + 6u > RUI_RECT_BATCH_MAX_VERTICES)
        return false;
    fw = (float)ctx->frame.width;
    fh = (float)ctx->frame.height;
    if (fw <= 0.0f || fh <= 0.0f) return false;
    x0 = rect.x;
    y0 = rect.y;
    x1 = rect.x + rect.w;
    y1 = rect.y + rect.h;
    nx0 = x0 / fw * 2.0f - 1.0f;
    nx1 = x1 / fw * 2.0f - 1.0f;
    ny0 = 1.0f - y0 / fh * 2.0f;
    ny1 = 1.0f - y1 / fh * 2.0f;
    rui_rect_batch_push(ctx, nx0, ny0, color);
    rui_rect_batch_push(ctx, nx1, ny0, color);
    rui_rect_batch_push(ctx, nx0, ny1, color);
    rui_rect_batch_push(ctx, nx1, ny0, color);
    rui_rect_batch_push(ctx, nx1, ny1, color);
    rui_rect_batch_push(ctx, nx0, ny1, color);
    return true;
}

static void rui_round_batch_push_vertex(rui_context_t *ctx, float x, float y,
                                        rui_rect_t rect, float radius,
                                        rui_color_t top, rui_color_t bottom) {
    rui_round_batch_vertex_t *out = &ctx->round_batch[ctx->round_batch_count++];
    out->x = (GLfloat)x;
    out->y = (GLfloat)y;
    out->rx = (GLfloat)rect.x;
    out->ry = (GLfloat)rect.y;
    out->rw = (GLfloat)rect.w;
    out->rh = (GLfloat)rect.h;
    out->radius = (GLfloat)radius;
    out->tr = (GLfloat)top.r;
    out->tg = (GLfloat)top.g;
    out->tb = (GLfloat)top.b;
    out->ta = (GLfloat)top.a;
    out->br = (GLfloat)bottom.r;
    out->bg = (GLfloat)bottom.g;
    out->bb = (GLfloat)bottom.b;
    out->ba = (GLfloat)bottom.a;
}

static bool rui_queue_round_rect(rui_context_t *ctx, rui_rect_t rect,
                                 float radius, rui_color_t top,
                                 rui_color_t bottom) {
    float x0;
    float y0;
    float x1;
    float y1;
    if (!ctx || !rui_init_round_pipeline(ctx)) return false;
    if (ctx->round_batch_count + 6u > RUI_ROUND_BATCH_MAX_VERTICES)
        rui_flush_round_batch(ctx);
    if (ctx->round_batch_count + 6u > RUI_ROUND_BATCH_MAX_VERTICES)
        return false;
    x0 = rect.x;
    y0 = rect.y;
    x1 = rect.x + rect.w;
    y1 = rect.y + rect.h;
    rui_round_batch_push_vertex(ctx, x0, y0, rect, radius, top, bottom);
    rui_round_batch_push_vertex(ctx, x1, y0, rect, radius, top, bottom);
    rui_round_batch_push_vertex(ctx, x0, y1, rect, radius, top, bottom);
    rui_round_batch_push_vertex(ctx, x1, y0, rect, radius, top, bottom);
    rui_round_batch_push_vertex(ctx, x1, y1, rect, radius, top, bottom);
    rui_round_batch_push_vertex(ctx, x0, y1, rect, radius, top, bottom);
    return true;
}

static void rui_flush_round_batch(rui_context_t *ctx) {
    GLsizei stride;
    if (!ctx || ctx->round_batch_count == 0) return;
    if (!rui_init_round_pipeline(ctx)) {
        ctx->round_batch_count = 0;
        return;
    }
    stride = (GLsizei)sizeof(ctx->round_batch[0]);
    glDisable(GL_SCISSOR_TEST);
    glUseProgram(ctx->round_program);
    glBindBuffer(GL_ARRAY_BUFFER, ctx->round_vbo);
    glBufferData(GL_ARRAY_BUFFER,
                 (GLsizeiptr)(ctx->round_batch_count * sizeof(ctx->round_batch[0])),
                 ctx->round_batch,
                 GL_STREAM_DRAW);
    glEnableVertexAttribArray((GLuint)ctx->round_pos_loc);
    glEnableVertexAttribArray((GLuint)ctx->round_rect_loc);
    glEnableVertexAttribArray((GLuint)ctx->round_radius_loc);
    glEnableVertexAttribArray((GLuint)ctx->round_top_loc);
    glEnableVertexAttribArray((GLuint)ctx->round_bottom_loc);
    glVertexAttribPointer((GLuint)ctx->round_pos_loc, 2, GL_FLOAT, GL_FALSE,
                          stride, (const void *)0);
    glVertexAttribPointer((GLuint)ctx->round_rect_loc, 4, GL_FLOAT, GL_FALSE,
                          stride, (const void *)(2 * sizeof(GLfloat)));
    glVertexAttribPointer((GLuint)ctx->round_radius_loc, 1, GL_FLOAT, GL_FALSE,
                          stride, (const void *)(6 * sizeof(GLfloat)));
    glVertexAttribPointer((GLuint)ctx->round_top_loc, 4, GL_FLOAT, GL_FALSE,
                          stride, (const void *)(7 * sizeof(GLfloat)));
    glVertexAttribPointer((GLuint)ctx->round_bottom_loc, 4, GL_FLOAT, GL_FALSE,
                          stride, (const void *)(11 * sizeof(GLfloat)));
    glUniform2f(ctx->round_screen_loc,
                (float)ctx->frame.width, (float)ctx->frame.height);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDrawArrays(GL_TRIANGLES, 0, (GLsizei)ctx->round_batch_count);
    glDisableVertexAttribArray((GLuint)ctx->round_pos_loc);
    glDisableVertexAttribArray((GLuint)ctx->round_rect_loc);
    glDisableVertexAttribArray((GLuint)ctx->round_radius_loc);
    glDisableVertexAttribArray((GLuint)ctx->round_top_loc);
    glDisableVertexAttribArray((GLuint)ctx->round_bottom_loc);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glUseProgram(0);
    ctx->round_batch_count = 0;
}

void rui_begin(rui_context_t *ctx, rui_color_t bg) {
    if (!ctx) return;
    ctx->rect_batch_count = 0;
    ctx->round_batch_count = 0;
    ctx->image_batch_count = 0;
    ctx->image_vertex_count = 0;
    ctx->text_batch_count = 0;
    ctx->text_vertex_count = 0;
    glViewport(0, 0, (GLsizei)ctx->frame.width, (GLsizei)ctx->frame.height);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_DITHER);
    glDisable(GL_BLEND);
    glClearColor(bg.r, bg.g, bg.b, bg.a);
    glClear(GL_COLOR_BUFFER_BIT);
}

void rui_rect(rui_context_t *ctx, rui_rect_t rect, rui_color_t color) {
    GLint sx;
    GLint sy;
    GLsizei sw;
    GLsizei sh;
    if (!ctx || rect.w <= 0.0f || rect.h <= 0.0f) return;
    if (rect.x < 0.0f) {
        rect.w += rect.x;
        rect.x = 0.0f;
    }
    if (rect.y < 0.0f) {
        rect.h += rect.y;
        rect.y = 0.0f;
    }
    if (rect.x + rect.w > (float)ctx->frame.width)
        rect.w = (float)ctx->frame.width - rect.x;
    if (rect.y + rect.h > (float)ctx->frame.height)
        rect.h = (float)ctx->frame.height - rect.y;
    if (rect.w <= 0.0f || rect.h <= 0.0f) return;
    sx = (GLint)(rect.x + 0.5f);
    sy = (GLint)((float)ctx->frame.height - rect.y - rect.h + 0.5f);
    sw = (GLsizei)(rect.w + 0.5f);
    sh = (GLsizei)(rect.h + 0.5f);
    if (sw <= 0 || sh <= 0) return;
    rui_flush_round_batch(ctx);
    rui_flush_image_batch(ctx);
    if (rui_queue_rect(ctx, rect, color)) return;
    rui_flush_rect_batch(ctx);
    if (color.a < 0.999f && rui_blend_rect(ctx, rect, color)) return;
    glEnable(GL_SCISSOR_TEST);
    glScissor(sx, sy, sw, sh);
    glClearColor(color.r, color.g, color.b, color.a);
    glClear(GL_COLOR_BUFFER_BIT);
}

void rui_round_rect_gradient(rui_context_t *ctx, rui_rect_t rect, float radius,
                             rui_color_t top, rui_color_t bottom) {
    float max_radius;
    if (!ctx || rect.w <= 0.0f || rect.h <= 0.0f) return;
    if (rect.x + rect.w < 0.0f || rect.y + rect.h < 0.0f ||
        rect.x > (float)ctx->frame.width || rect.y > (float)ctx->frame.height)
        return;
    max_radius = rect.w < rect.h ? rect.w * 0.5f : rect.h * 0.5f;
    if (radius < 0.0f) radius = 0.0f;
    if (radius > max_radius) radius = max_radius;
    if (!rui_init_round_pipeline(ctx)) {
        rui_rect(ctx, rect, bottom);
        return;
    }
    rui_flush_rect_batch(ctx);
    rui_flush_image_batch(ctx);
    rui_flush_text_batch(ctx);
    if (!rui_queue_round_rect(ctx, rect, radius, top, bottom))
        rui_rect(ctx, rect, bottom);
}

void rui_round_rect(rui_context_t *ctx, rui_rect_t rect, float radius,
                    rui_color_t color) {
    rui_round_rect_gradient(ctx, rect, radius, color, color);
}

static bool rui_render_text_texture(rui_context_t *ctx, const char *text, int px,
                                    rui_text_cache_entry_t *entry) {
    cairo_surface_t *surface = NULL;
    cairo_surface_t *probe_surface = NULL;
    cairo_t *cr = NULL;
    cairo_t *probe_cr = NULL;
    PangoLayout *layout = NULL;
    PangoFontDescription *font = NULL;
    unsigned char *src;
    unsigned char *rgba = NULL;
    int text_w = 0;
    int text_h = 0;
    int width;
    int height;
    int stride;
    int x, y;
    GLuint tex = 0;
    (void)ctx;
    if (!text || !entry || px <= 0) return false;

    probe_surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 1, 1);
    probe_cr = cairo_create(probe_surface);
    layout = pango_cairo_create_layout(probe_cr);
    font = pango_font_description_from_string("Cantarell");
    pango_font_description_set_absolute_size(font, px * PANGO_SCALE);
    pango_layout_set_font_description(layout, font);
    pango_layout_set_text(layout, text, -1);
    pango_layout_get_pixel_size(layout, &text_w, &text_h);
    g_object_unref(layout);
    layout = NULL;
    cairo_destroy(probe_cr);
    probe_cr = NULL;
    cairo_surface_destroy(probe_surface);
    probe_surface = NULL;

    width = text_w + 6;
    height = text_h + 6;
    if (width <= 0 || height <= 0 || width > 4096 || height > 512) goto fail;
    surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, width, height);
    if (cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS) goto fail;
    cr = cairo_create(surface);
    cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
    cairo_paint(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
    cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 1.0);
    layout = pango_cairo_create_layout(cr);
    pango_layout_set_font_description(layout, font);
    pango_layout_set_text(layout, text, -1);
    cairo_move_to(cr, 3.0, 3.0);
    pango_cairo_show_layout(cr, layout);
    cairo_surface_flush(surface);

    stride = cairo_image_surface_get_stride(surface);
    src = cairo_image_surface_get_data(surface);
    rgba = (unsigned char *)malloc((size_t)width * (size_t)height * 4u);
    if (!rgba) goto fail;
    for (y = 0; y < height; ++y) {
        const uint32_t *row = (const uint32_t *)(const void *)(src + (size_t)y * (size_t)stride);
        for (x = 0; x < width; ++x) {
            uint8_t a = (uint8_t)((row[x] >> 24) & 0xFFu);
            size_t o = ((size_t)y * (size_t)width + (size_t)x) * 4u;
            rgba[o + 0] = 255;
            rgba[o + 1] = 255;
            rgba[o + 2] = 255;
            rgba[o + 3] = a;
        }
    }
    glGenTextures(1, &tex);
    if (!tex) goto fail;
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height,
                    GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    glBindTexture(GL_TEXTURE_2D, 0);

    if (entry->texture) glDeleteTextures(1, &entry->texture);
    entry->texture = tex;
    entry->width = width;
    entry->height = height;
    free(rgba);
    g_object_unref(layout);
    pango_font_description_free(font);
    cairo_destroy(cr);
    cairo_surface_destroy(surface);
    return true;

fail:
    if (tex) glDeleteTextures(1, &tex);
    free(rgba);
    if (layout) g_object_unref(layout);
    if (font) pango_font_description_free(font);
    if (cr) cairo_destroy(cr);
    if (surface) cairo_surface_destroy(surface);
    if (probe_cr) cairo_destroy(probe_cr);
    if (probe_surface) cairo_surface_destroy(probe_surface);
    return false;
}

static rui_text_cache_entry_t *rui_text_cache_get(rui_context_t *ctx,
                                                  const char *text, int px) {
    rui_text_cache_entry_t *best = NULL;
    uint32_t oldest = UINT32_MAX;
    bool found_free = false;
    int i;
    if (!ctx || !text || !*text || px <= 0) return NULL;
    if (ctx->text_budget_frame != ctx->frame.frame) {
        ctx->text_budget_frame = ctx->frame.frame;
        ctx->text_budget_used = 0;
        ctx->text_deferred_this_frame = false;
    }
    ++ctx->text_epoch;
    for (i = 0; i < RUI_TEXT_CACHE_MAX; ++i) {
        rui_text_cache_entry_t *e = &ctx->text_cache[i];
        if (!e->used) {
            if (!found_free) {
                best = e;
                found_free = true;
            }
            continue;
        }
        if (e->px == px && strcmp(e->text, text) == 0) {
            e->last_used = ctx->text_epoch;
            return e;
        }
        if (!found_free && e->last_used < oldest) {
            oldest = e->last_used;
            best = e;
        }
    }
    if (ctx->text_budget_used >= RUI_TEXT_NEW_TEXTURES_PER_FRAME) {
        ctx->text_deferred_this_frame = true;
        return NULL;
    }
    ++ctx->text_budget_used;
    if (!best || best->used) {
        ctx->text_deferred_this_frame = true;
        return NULL;
    }
    memset(best, 0, sizeof(*best));
    best->used = true;
    best->px = px;
    best->last_used = ctx->text_epoch;
    snprintf(best->text, sizeof(best->text), "%s", text);
    if (!rui_render_text_texture(ctx, text, px, best)) {
        memset(best, 0, sizeof(*best));
        return NULL;
    }
    return best;
}

static rui_text_cache_entry_t *rui_text_cache_find(rui_context_t *ctx,
                                                   const char *text, int px) {
    if (!ctx || !text || !*text || px <= 0) return NULL;
    ++ctx->text_epoch;
    for (int i = 0; i < RUI_TEXT_CACHE_MAX; ++i) {
        rui_text_cache_entry_t *e = &ctx->text_cache[i];
        if (e->used && e->px == px && strcmp(e->text, text) == 0) {
            e->last_used = ctx->text_epoch;
            return e;
        }
    }
    return NULL;
}

static void rui_text_batch_push_vertex(rui_context_t *ctx, const float *v, int idx) {
    rui_text_batch_vertex_t *out = &ctx->text_vertices[ctx->text_vertex_count++];
    out->x = (GLfloat)v[idx * 4 + 0];
    out->y = (GLfloat)v[idx * 4 + 1];
    out->u = (GLfloat)v[idx * 4 + 2];
    out->v = (GLfloat)v[idx * 4 + 3];
}

static void rui_flush_text_batch(rui_context_t *ctx) {
    size_t i;
    if (!ctx || ctx->text_batch_count == 0 || ctx->text_vertex_count == 0) return;
    if (!rui_init_text_pipeline(ctx)) {
        ctx->text_batch_count = 0;
        ctx->text_vertex_count = 0;
        return;
    }
    glDisable(GL_SCISSOR_TEST);
    glUseProgram(ctx->text_program);
    glActiveTexture(GL_TEXTURE0);
    glUniform1i(ctx->text_tex_loc, 0);
    glBindBuffer(GL_ARRAY_BUFFER, ctx->text_vbo);
    glBufferData(GL_ARRAY_BUFFER,
                 (GLsizeiptr)(ctx->text_vertex_count * sizeof(ctx->text_vertices[0])),
                 ctx->text_vertices,
                 GL_STREAM_DRAW);
    glEnableVertexAttribArray((GLuint)ctx->text_pos_loc);
    glEnableVertexAttribArray((GLuint)ctx->text_uv_loc);
    glVertexAttribPointer((GLuint)ctx->text_pos_loc, 2, GL_FLOAT, GL_FALSE,
                          (GLsizei)sizeof(ctx->text_vertices[0]), (const void *)0);
    glVertexAttribPointer((GLuint)ctx->text_uv_loc, 2, GL_FLOAT, GL_FALSE,
                          (GLsizei)sizeof(ctx->text_vertices[0]),
                          (const void *)(2 * sizeof(GLfloat)));
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    for (i = 0; i < ctx->text_batch_count; ++i) {
        const rui_text_batch_item_t *item = &ctx->text_batch[i];
        glBindTexture(GL_TEXTURE_2D, item->texture);
        glUniform4f(ctx->text_color_loc,
                    item->color.r, item->color.g, item->color.b, item->color.a);
        glDrawArrays(GL_TRIANGLE_STRIP, item->first, 4);
    }
    glDisableVertexAttribArray((GLuint)ctx->text_pos_loc);
    glDisableVertexAttribArray((GLuint)ctx->text_uv_loc);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindTexture(GL_TEXTURE_2D, 0);
    glUseProgram(0);
    ctx->text_batch_count = 0;
    ctx->text_vertex_count = 0;
}

static bool rui_text_pango(rui_context_t *ctx, float x, float y, const char *text,
                           float scale, rui_color_t color) {
    rui_text_cache_entry_t *entry;
    rui_text_batch_item_t *item;
    rui_rect_t rect;
    float v[16];
    int px;
    if (!ctx || !text || !*text) return true;
    if (!rui_init_text_pipeline(ctx)) return false;
    px = (int)(scale * 10.5f + 0.5f);
    if (px < 11) px = 11;
    if (px > 96) px = 96;
    entry = rui_text_cache_get(ctx, text, px);
    if (!entry || !entry->texture) return false;
    rect.x = x;
    rect.y = y;
    rect.w = (float)entry->width;
    rect.h = (float)entry->height;
    rui_flush_rect_batch(ctx);
    rui_flush_round_batch(ctx);
    rui_rect_vertices(ctx, &rect, v, true);
    if (ctx->text_batch_count + 1u > RUI_TEXT_BATCH_MAX_QUADS ||
        ctx->text_vertex_count + 4u > RUI_TEXT_BATCH_MAX_VERTICES)
        rui_flush_text_batch(ctx);
    if (ctx->text_batch_count + 1u > RUI_TEXT_BATCH_MAX_QUADS ||
        ctx->text_vertex_count + 4u > RUI_TEXT_BATCH_MAX_VERTICES)
        return false;
    item = &ctx->text_batch[ctx->text_batch_count++];
    item->texture = entry->texture;
    item->color = color;
    item->first = (GLsizei)ctx->text_vertex_count;
    rui_text_batch_push_vertex(ctx, v, 0);
    rui_text_batch_push_vertex(ctx, v, 1);
    rui_text_batch_push_vertex(ctx, v, 2);
    rui_text_batch_push_vertex(ctx, v, 3);
    return true;
}

bool rui_text_cached(rui_context_t *ctx, float x, float y, const char *text,
                     float scale, rui_color_t color) {
    rui_text_cache_entry_t *entry;
    rui_text_batch_item_t *item;
    rui_rect_t rect;
    float v[16];
    int px;
    if (!ctx || !text || !*text) return true;
    if (!rui_init_text_pipeline(ctx)) return false;
    px = (int)(scale * 10.5f + 0.5f);
    if (px < 11) px = 11;
    if (px > 96) px = 96;
    entry = rui_text_cache_find(ctx, text, px);
    if (!entry || !entry->texture) return false;
    rect.x = x;
    rect.y = y;
    rect.w = (float)entry->width;
    rect.h = (float)entry->height;
    rui_flush_rect_batch(ctx);
    rui_flush_round_batch(ctx);
    rui_rect_vertices(ctx, &rect, v, true);
    if (ctx->text_batch_count + 1u > RUI_TEXT_BATCH_MAX_QUADS ||
        ctx->text_vertex_count + 4u > RUI_TEXT_BATCH_MAX_VERTICES)
        rui_flush_text_batch(ctx);
    if (ctx->text_batch_count + 1u > RUI_TEXT_BATCH_MAX_QUADS ||
        ctx->text_vertex_count + 4u > RUI_TEXT_BATCH_MAX_VERTICES)
        return false;
    item = &ctx->text_batch[ctx->text_batch_count++];
    item->texture = entry->texture;
    item->color = color;
    item->first = (GLsizei)ctx->text_vertex_count;
    rui_text_batch_push_vertex(ctx, v, 0);
    rui_text_batch_push_vertex(ctx, v, 1);
    rui_text_batch_push_vertex(ctx, v, 2);
    rui_text_batch_push_vertex(ctx, v, 3);
    return true;
}

bool rui_text_preload(rui_context_t *ctx, const char *text, float scale) {
    int px;
    rui_text_cache_entry_t *entry;
    if (!ctx || !text || !*text) return false;
    if (!rui_init_text_pipeline(ctx)) return false;
    px = (int)(scale * 10.5f + 0.5f);
    if (px < 11) px = 11;
    if (px > 96) px = 96;
    entry = rui_text_cache_get(ctx, text, px);
    return entry && entry->texture;
}

static uint8_t rui_glyph_row(char ch, int row) {
    static const uint8_t digits[10][7] = {
        {14,17,19,21,25,17,14}, {4,12,4,4,4,4,14},
        {14,17,1,2,4,8,31}, {30,1,1,14,1,1,30},
        {2,6,10,18,31,2,2}, {31,16,30,1,1,17,14},
        {6,8,16,30,17,17,14}, {31,1,2,4,8,8,8},
        {14,17,17,14,17,17,14}, {14,17,17,15,1,2,12}
    };
    static const uint8_t letters[26][7] = {
        {14,17,17,31,17,17,17}, {30,17,17,30,17,17,30},
        {14,17,16,16,16,17,14}, {30,17,17,17,17,17,30},
        {31,16,16,30,16,16,31}, {31,16,16,30,16,16,16},
        {14,17,16,23,17,17,15}, {17,17,17,31,17,17,17},
        {14,4,4,4,4,4,14}, {7,2,2,2,18,18,12},
        {17,18,20,24,20,18,17}, {16,16,16,16,16,16,31},
        {17,27,21,21,17,17,17}, {17,25,21,19,17,17,17},
        {14,17,17,17,17,17,14}, {30,17,17,30,16,16,16},
        {14,17,17,17,21,18,13}, {30,17,17,30,20,18,17},
        {15,16,16,14,1,1,30}, {31,4,4,4,4,4,4},
        {17,17,17,17,17,17,14}, {17,17,17,17,17,10,4},
        {17,17,17,21,21,21,10}, {17,17,10,4,10,17,17},
        {17,17,10,4,4,4,4}, {31,1,2,4,8,16,31}
    };
    if (row < 0 || row >= 7) return 0;
    if (ch >= 'a' && ch <= 'z') ch = (char)(ch - 'a' + 'A');
    if (ch >= 'A' && ch <= 'Z') return letters[ch - 'A'][row];
    if (ch >= '0' && ch <= '9') return digits[ch - '0'][row];
    if (ch == '-') return row == 3 ? 31 : 0;
    if (ch == '.') return row == 6 ? 4 : 0;
    if (ch == ':') return (row == 2 || row == 5) ? 4 : 0;
    if (ch == '/') return (uint8_t)(1u << (6 - row > 4 ? 4 : 6 - row));
    return 0;
}

void rui_text(rui_context_t *ctx, float x, float y, const char *text,
              float scale, rui_color_t color) {
    float pen = x;
    if (!ctx || !text) return;
    if (rui_text_pango(ctx, x, y, text, scale, color)) return;
    if (ctx->text_deferred_this_frame) return;
    if (scale < 1.0f) scale = 1.0f;
    for (; *text; ++text) {
        int row;
        if (*text == ' ') {
            pen += 4.0f * scale;
            continue;
        }
        for (row = 0; row < 7; ++row) {
            uint8_t bits = rui_glyph_row(*text, row);
            int col;
            for (col = 0; col < 5; ++col) {
                if (bits & (1u << (4 - col))) {
                    rui_rect(ctx,
                             (rui_rect_t){ pen + (float)col * scale,
                                           y + (float)row * scale,
                                           scale,
                                           scale },
                             color);
                }
            }
        }
        pen += 6.0f * scale;
    }
}

static bool rui_gl_probe_enabled(void) {
    static int cached = -1;
    if (cached < 0) {
        cached = (getenv("RIDUX_UI_GL_PROBE") ||
                  access("/etc/ridux-ui-gl-probe.enable", F_OK) == 0) ? 1 : 0;
    }
    return cached != 0;
}

static bool rui_present_trace_enabled(void) {
    static int cached = -1;
    if (cached < 0) {
        cached = (getenv("RIDUX_UI_PRESENT_TRACE") ||
                  access("/etc/ridux-ui-present-trace.enable", F_OK) == 0) ? 1 : 0;
    }
    return cached != 0;
}

bool rui_present(rui_context_t *ctx) {
    rui_fb_t next;
    struct gbm_bo *bo;
    static unsigned int gl_probe_count;
    static unsigned int present_trace_count;
    unsigned int trace_id;
    bool trace;
    uint64_t t0;
    uint64_t t1;
    uint64_t t2;
    uint64_t t3;
    uint64_t t4;
    uint64_t t5;
    if (!ctx) return false;
    trace_id = present_trace_count++;
    trace = rui_present_trace_enabled() && trace_id < 8u;
    if (trace)
        rui_logf("[ridux-ui] present[%u] begin frame=%u\n", trace_id, ctx->frame.frame);
    memset(&next, 0, sizeof(next));
    rui_flush_rect_batch(ctx);
    rui_flush_round_batch(ctx);
    rui_flush_image_batch(ctx);
    rui_flush_text_batch(ctx);
    if (rui_gl_probe_enabled() && gl_probe_count < 8) {
        unsigned char top[4] = {0, 0, 0, 0};
        unsigned char mid[4] = {0, 0, 0, 0};
        GLint top_y = (GLint)(ctx->frame.height > 30 ? ctx->frame.height - 30 : 0);
        GLint mid_x = (GLint)(ctx->frame.width / 2);
        GLint mid_y = (GLint)(ctx->frame.height / 2);
        glReadPixels(30, top_y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, top);
        glReadPixels(mid_x, mid_y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, mid);
        rui_logf("[ridux-ui] gl-probe frame=%u top=%u,%u,%u,%u mid=%u,%u,%u,%u err=0x%x\n",
                 ctx->frame.frame,
                 top[0], top[1], top[2], top[3],
                 mid[0], mid[1], mid[2], mid[3],
                 glGetError());
        ++gl_probe_count;
    }
    ctx->frame.present_swap_ms = 0;
    ctx->frame.present_lock_ms = 0;
    ctx->frame.present_addfb_ms = 0;
    ctx->frame.present_flip_ms = 0;
    ctx->frame.present_release_ms = 0;
    if (trace)
        rui_logf("[ridux-ui] present[%u] swap begin\n", trace_id);
    t0 = rui_now_ms();
    if (!eglSwapBuffers(ctx->display, ctx->egl_surface)) {
        rui_logf("[ridux-ui] eglSwapBuffers failed: egl=0x%x\n", eglGetError());
        return false;
    }
    t1 = rui_now_ms();
    ctx->frame.present_swap_ms = (uint32_t)(t1 - t0);
    if (trace)
        rui_logf("[ridux-ui] present[%u] swap done\n", trace_id);
    if (trace)
        rui_logf("[ridux-ui] present[%u] lock-front-buffer begin\n", trace_id);
    bo = gbm_surface_lock_front_buffer(ctx->surface);
    if (!bo) {
        rui_log_errno("gbm_surface_lock_front_buffer");
        return false;
    }
    t2 = rui_now_ms();
    ctx->frame.present_lock_ms = (uint32_t)(t2 - t1);
    if (trace)
        rui_logf("[ridux-ui] present[%u] lock-front-buffer done\n", trace_id);
    if (trace)
        rui_logf("[ridux-ui] present[%u] addfb begin\n", trace_id);
    if (rui_add_fb_for_bo(ctx->out.fd, bo, &next) != 0) {
        gbm_surface_release_buffer(ctx->surface, bo);
        return false;
    }
    t3 = rui_now_ms();
    ctx->frame.present_addfb_ms = (uint32_t)(t3 - t2);
    if (trace)
        rui_logf("[ridux-ui] present[%u] addfb done fb=%u cached=%d\n",
                 trace_id, next.fb_id, next.cached_fb ? 1 : 0);
    if (ctx->first_frame || !ctx->front.fb_id || !ctx->page_flip_ok) {
        if (trace)
            rui_logf("[ridux-ui] present[%u] setcrtc begin\n", trace_id);
        if (drmModeSetCrtc(ctx->out.fd, ctx->out.crtc_id, next.fb_id, 0, 0,
                           &ctx->out.connector_id, 1, &ctx->out.mode) != 0) {
            rui_log_errno("drmModeSetCrtc");
            rui_release_fb(ctx, &next);
            return false;
        }
        if (trace)
            rui_logf("[ridux-ui] present[%u] setcrtc done\n", trace_id);
        rui_release_fb(ctx, &ctx->front);
        ctx->front = next;
    } else {
        bool sync_flip = rui_sync_pageflip_enabled();
        int waiting = sync_flip ? 1 : 0;
        uint32_t flip_flags = sync_flip ? DRM_MODE_PAGE_FLIP_EVENT : DRM_MODE_PAGE_FLIP_ASYNC;
        if (trace)
            rui_logf("[ridux-ui] present[%u] pageflip begin sync=%d\n",
                     trace_id, sync_flip ? 1 : 0);
        if (drmModePageFlip(ctx->out.fd, ctx->out.crtc_id, next.fb_id,
                            flip_flags, sync_flip ? &waiting : NULL) != 0) {
            rui_logf("[ridux-ui] pageflip unavailable, using SetCrtc: errno=%d (%s)\n",
                     errno, strerror(errno));
            ctx->page_flip_ok = false;
            if (trace)
                rui_logf("[ridux-ui] present[%u] fallback-setcrtc begin\n", trace_id);
            if (drmModeSetCrtc(ctx->out.fd, ctx->out.crtc_id, next.fb_id, 0, 0,
                               &ctx->out.connector_id, 1, &ctx->out.mode) != 0) {
                rui_log_errno("drmModeSetCrtc fallback");
                rui_release_fb(ctx, &next);
                return false;
            }
            if (trace)
                rui_logf("[ridux-ui] present[%u] fallback-setcrtc done\n", trace_id);
            rui_release_fb(ctx, &ctx->front);
            ctx->front = next;
        } else if (sync_flip && !rui_wait_for_page_flip(ctx->out.fd, &waiting)) {
            ctx->page_flip_ok = false;
            rui_release_fb(ctx, &next);
            return false;
        } else {
            rui_release_fb(ctx, &ctx->front);
            ctx->front = next;
        }
        if (trace)
            rui_logf("[ridux-ui] present[%u] pageflip done ok=%d\n",
                     trace_id, ctx->page_flip_ok ? 1 : 0);
    }
    t4 = rui_now_ms();
    ctx->frame.present_flip_ms = (uint32_t)(t4 - t3);
    if (trace)
        rui_logf("[ridux-ui] present[%u] release begin\n", trace_id);
    t5 = rui_now_ms();
    ctx->frame.present_release_ms = (uint32_t)(t5 - t4);
    ctx->first_frame = false;
    ++ctx->frame.frame;
    if (trace)
        rui_logf("[ridux-ui] present[%u] done frame=%u\n", trace_id, ctx->frame.frame);
    return true;
}

void rui_close(rui_context_t *ctx) {
    int i;
    if (!ctx) return;
    (void)rui_cursor_disable(ctx);
    rui_release_fb(ctx, &ctx->front);
    for (i = 0; i < RUI_TEXT_CACHE_MAX; ++i) {
        if (ctx->text_cache[i].texture)
            glDeleteTextures(1, &ctx->text_cache[i].texture);
    }
    for (i = 0; i < RUI_IMAGE_CACHE_MAX; ++i) {
        if (ctx->image_cache[i].texture)
            glDeleteTextures(1, &ctx->image_cache[i].texture);
    }
    if (ctx->cursor_texture) glDeleteTextures(1, &ctx->cursor_texture);
    if (ctx->image_vbo) glDeleteBuffers(1, &ctx->image_vbo);
    if (ctx->text_vbo) glDeleteBuffers(1, &ctx->text_vbo);
    if (ctx->rect_vbo) glDeleteBuffers(1, &ctx->rect_vbo);
    if (ctx->round_vbo) glDeleteBuffers(1, &ctx->round_vbo);
    if (ctx->solid_vbo) glDeleteBuffers(1, &ctx->solid_vbo);
    if (ctx->image_program) glDeleteProgram(ctx->image_program);
    if (ctx->text_program) glDeleteProgram(ctx->text_program);
    if (ctx->rect_program) glDeleteProgram(ctx->rect_program);
    if (ctx->round_program) glDeleteProgram(ctx->round_program);
    if (ctx->solid_program) glDeleteProgram(ctx->solid_program);
    if (ctx->display != EGL_NO_DISPLAY) {
        eglMakeCurrent(ctx->display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        if (ctx->egl_surface != EGL_NO_SURFACE) eglDestroySurface(ctx->display, ctx->egl_surface);
        if (ctx->egl_context != EGL_NO_CONTEXT) eglDestroyContext(ctx->display, ctx->egl_context);
        eglTerminate(ctx->display);
    }
    if (ctx->surface) gbm_surface_destroy(ctx->surface);
    if (ctx->gbm) gbm_device_destroy(ctx->gbm);
    if (ctx->out.saved_crtc) drmModeFreeCrtc(ctx->out.saved_crtc);
    if (ctx->out.fd >= 0) close(ctx->out.fd);
    free(ctx);
}
