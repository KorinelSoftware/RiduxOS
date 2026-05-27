#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <errno.h>
#include <fcntl.h>
#include <gbm.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <unistd.h>
#include <xf86drm.h>
#include <xf86drmMode.h>

#ifndef EGL_PLATFORM_GBM_KHR
#define EGL_PLATFORM_GBM_KHR 0x31D7
#endif

typedef EGLDisplay (*PFNEGLGETPLATFORMDISPLAYEXTPROC_LOCAL)(EGLenum platform,
                                                            void *native_display,
                                                            const EGLint *attrib_list);

typedef struct ridux_drm_output {
    int fd;
    uint32_t connector_id;
    uint32_t crtc_id;
    drmModeModeInfo mode;
    drmModeCrtc *saved_crtc;
} ridux_drm_output_t;

typedef struct ridux_fb {
    struct gbm_bo *bo;
    uint32_t fb_id;
} ridux_fb_t;

typedef struct ridux_gl {
    struct gbm_device *gbm;
    struct gbm_surface *surface;
    EGLDisplay display;
    EGLContext context;
    EGLSurface egl_surface;
    GLuint program;
    GLuint vbo;
    GLint pos_loc;
    GLint color_loc;
} ridux_gl_t;

static void log_errno(const char *what) {
    fprintf(stderr, "[ridux-gl-compositor] %s failed: errno=%d (%s)\n",
            what, errno, strerror(errno));
}

static bool env_enabled(const char *name) {
    const char *value = getenv(name);
    return value && value[0] && strcmp(value, "0") != 0;
}

static bool choose_crtc_for_connector(int fd,
                                      drmModeRes *res,
                                      drmModeConnector *conn,
                                      uint32_t *crtc_id_out) {
    int e;

    if (!fd || !res || !conn || !crtc_id_out) return false;

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

static bool open_drm_output(ridux_drm_output_t *out) {
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
        int i;

        if (fd < 0) {
            fprintf(stderr, "[ridux-gl-compositor] open %s failed: errno=%d (%s)\n",
                    cards[card_index], errno, strerror(errno));
            continue;
        }

        res = drmModeGetResources(fd);
        if (!res) {
            log_errno("drmModeGetResources");
            close(fd);
            continue;
        }

        for (i = 0; i < res->count_connectors; ++i) {
            drmModeConnector *conn = drmModeGetConnector(fd, res->connectors[i]);
            if (!conn) continue;
            if (conn->connection == DRM_MODE_CONNECTED &&
                conn->count_modes > 0 &&
                choose_crtc_for_connector(fd, res, conn, &crtc_id)) {
                best = conn;
                break;
            }
            drmModeFreeConnector(conn);
        }

        if (!best) {
            for (i = 0; i < res->count_connectors; ++i) {
                drmModeConnector *conn = drmModeGetConnector(fd, res->connectors[i]);
                if (!conn) continue;
                if (conn->count_modes > 0 &&
                    choose_crtc_for_connector(fd, res, conn, &crtc_id)) {
                    best = conn;
                    break;
                }
                drmModeFreeConnector(conn);
            }
        }

        if (!best) {
            fprintf(stderr, "[ridux-gl-compositor] no usable KMS connector on %s\n",
                    cards[card_index]);
            drmModeFreeResources(res);
            close(fd);
            continue;
        }

        out->fd = fd;
        out->connector_id = best->connector_id;
        out->crtc_id = crtc_id;
        out->mode = best->modes[0];
        out->saved_crtc = drmModeGetCrtc(fd, crtc_id);

        fprintf(stderr,
                "[ridux-gl-compositor] drm card=%s connector=%u crtc=%u mode=%ux%u@%u\n",
                cards[card_index],
                out->connector_id,
                out->crtc_id,
                out->mode.hdisplay,
                out->mode.vdisplay,
                out->mode.vrefresh);

        drmModeFreeConnector(best);
        drmModeFreeResources(res);
        return true;
    }

    return false;
}

static GLuint compile_shader(GLenum type, const char *src) {
    GLuint shader = glCreateShader(type);
    GLint ok = GL_FALSE;
    char log[512];

    glShaderSource(shader, 1, &src, NULL);
    glCompileShader(shader);
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        GLsizei len = 0;
        glGetShaderInfoLog(shader, (GLsizei)sizeof(log), &len, log);
        fprintf(stderr, "[ridux-gl-compositor] shader compile failed: %.*s\n",
                (int)len, log);
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

static GLuint create_program(void) {
    static const char *vs =
        "attribute vec2 a_pos;\n"
        "void main(void) {\n"
        "    gl_Position = vec4(a_pos, 0.0, 1.0);\n"
        "}\n";
    static const char *fs =
        "precision mediump float;\n"
        "uniform vec4 u_color;\n"
        "void main(void) {\n"
        "    gl_FragColor = u_color;\n"
        "}\n";
    GLuint vert = compile_shader(GL_VERTEX_SHADER, vs);
    GLuint frag = compile_shader(GL_FRAGMENT_SHADER, fs);
    GLuint program;
    GLint ok = GL_FALSE;
    char log[512];

    if (!vert || !frag) return 0;

    program = glCreateProgram();
    glAttachShader(program, vert);
    glAttachShader(program, frag);
    glBindAttribLocation(program, 0, "a_pos");
    glLinkProgram(program);
    glDeleteShader(vert);
    glDeleteShader(frag);

    glGetProgramiv(program, GL_LINK_STATUS, &ok);
    if (!ok) {
        GLsizei len = 0;
        glGetProgramInfoLog(program, (GLsizei)sizeof(log), &len, log);
        fprintf(stderr, "[ridux-gl-compositor] program link failed: %.*s\n",
                (int)len, log);
        glDeleteProgram(program);
        return 0;
    }

    return program;
}

static bool init_gl(ridux_gl_t *gl, int drm_fd, uint32_t width, uint32_t height) {
    EGLint major = 0;
    EGLint minor = 0;
    EGLint count = 0;
    EGLConfig config;
    EGLint context_attribs[] = {
        EGL_CONTEXT_CLIENT_VERSION, 2,
        EGL_NONE
    };
    EGLint config_attribs[] = {
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_RED_SIZE, 8,
        EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE, 8,
        EGL_ALPHA_SIZE, 0,
        EGL_DEPTH_SIZE, 0,
        EGL_STENCIL_SIZE, 0,
        EGL_NONE
    };
    PFNEGLGETPLATFORMDISPLAYEXTPROC_LOCAL get_platform_display = NULL;

    memset(gl, 0, sizeof(*gl));
    gl->display = EGL_NO_DISPLAY;
    gl->context = EGL_NO_CONTEXT;
    gl->egl_surface = EGL_NO_SURFACE;

    gl->gbm = gbm_create_device(drm_fd);
    if (!gl->gbm) {
        log_errno("gbm_create_device");
        return false;
    }

    gl->surface = gbm_surface_create(gl->gbm,
                                     width,
                                     height,
                                     GBM_FORMAT_XRGB8888,
                                     GBM_BO_USE_SCANOUT | GBM_BO_USE_RENDERING);
    if (!gl->surface) {
        log_errno("gbm_surface_create");
        return false;
    }

    get_platform_display =
        (PFNEGLGETPLATFORMDISPLAYEXTPROC_LOCAL)eglGetProcAddress("eglGetPlatformDisplayEXT");
    if (get_platform_display) {
        gl->display = get_platform_display(EGL_PLATFORM_GBM_KHR, gl->gbm, NULL);
    }
    if (gl->display == EGL_NO_DISPLAY) {
        gl->display = eglGetDisplay((EGLNativeDisplayType)gl->gbm);
    }
    if (gl->display == EGL_NO_DISPLAY) {
        fprintf(stderr, "[ridux-gl-compositor] eglGetDisplay failed\n");
        return false;
    }

    if (!eglInitialize(gl->display, &major, &minor)) {
        fprintf(stderr, "[ridux-gl-compositor] eglInitialize failed: egl=0x%x\n",
                eglGetError());
        return false;
    }

    if (!eglBindAPI(EGL_OPENGL_ES_API)) {
        fprintf(stderr, "[ridux-gl-compositor] eglBindAPI failed: egl=0x%x\n",
                eglGetError());
        return false;
    }

    if (!eglChooseConfig(gl->display, config_attribs, &config, 1, &count) || count < 1) {
        fprintf(stderr, "[ridux-gl-compositor] eglChooseConfig failed: egl=0x%x\n",
                eglGetError());
        return false;
    }

    gl->context = eglCreateContext(gl->display, config, EGL_NO_CONTEXT, context_attribs);
    if (gl->context == EGL_NO_CONTEXT) {
        fprintf(stderr, "[ridux-gl-compositor] eglCreateContext failed: egl=0x%x\n",
                eglGetError());
        return false;
    }

    gl->egl_surface = eglCreateWindowSurface(gl->display,
                                             config,
                                             (EGLNativeWindowType)gl->surface,
                                             NULL);
    if (gl->egl_surface == EGL_NO_SURFACE) {
        fprintf(stderr, "[ridux-gl-compositor] eglCreateWindowSurface failed: egl=0x%x\n",
                eglGetError());
        return false;
    }

    if (!eglMakeCurrent(gl->display, gl->egl_surface, gl->egl_surface, gl->context)) {
        fprintf(stderr, "[ridux-gl-compositor] eglMakeCurrent failed: egl=0x%x\n",
                eglGetError());
        return false;
    }

    gl->program = create_program();
    if (!gl->program) return false;

    glUseProgram(gl->program);
    gl->pos_loc = glGetAttribLocation(gl->program, "a_pos");
    gl->color_loc = glGetUniformLocation(gl->program, "u_color");
    if (gl->pos_loc < 0 || gl->color_loc < 0) {
        fprintf(stderr, "[ridux-gl-compositor] shader locations missing\n");
        return false;
    }

    glGenBuffers(1, &gl->vbo);
    glBindBuffer(GL_ARRAY_BUFFER, gl->vbo);
    glEnableVertexAttribArray((GLuint)gl->pos_loc);
    glVertexAttribPointer((GLuint)gl->pos_loc, 2, GL_FLOAT, GL_FALSE, 0, (const void *)0);

    fprintf(stderr,
            "[ridux-gl-compositor] EGL %d.%d vendor=%s client_apis=%s\n",
            major,
            minor,
            eglQueryString(gl->display, EGL_VENDOR),
            eglQueryString(gl->display, EGL_CLIENT_APIS));
    fprintf(stderr,
            "[ridux-gl-compositor] GLES vendor=%s renderer=%s version=%s\n",
            glGetString(GL_VENDOR),
            glGetString(GL_RENDERER),
            glGetString(GL_VERSION));

    return true;
}

static int add_fb_for_bo(int fd, struct gbm_bo *bo, ridux_fb_t *fb) {
    uint32_t width = gbm_bo_get_width(bo);
    uint32_t height = gbm_bo_get_height(bo);
    uint32_t stride = gbm_bo_get_stride(bo);
    uint32_t handle = gbm_bo_get_handle(bo).u32;
    int rc;

    memset(fb, 0, sizeof(*fb));
    fb->bo = bo;
    rc = drmModeAddFB(fd, width, height, 24, 32, stride, handle, &fb->fb_id);
    if (rc != 0) log_errno("drmModeAddFB");
    return rc;
}

static void release_fb(int fd, struct gbm_surface *surface, ridux_fb_t *fb) {
    if (!fb) return;
    if (fb->fb_id) {
        drmModeRmFB(fd, fb->fb_id);
        fb->fb_id = 0;
    }
    if (fb->bo) {
        gbm_surface_release_buffer(surface, fb->bo);
        fb->bo = NULL;
    }
}

static uint32_t rgb(unsigned int r, unsigned int g, unsigned int b) {
    return 0xff000000u | ((r & 0xffu) << 16) | ((g & 0xffu) << 8) | (b & 0xffu);
}

static void cpu_rect(uint8_t *base,
                     uint32_t stride,
                     uint32_t screen_w,
                     uint32_t screen_h,
                     int x,
                     int y,
                     int w,
                     int h,
                     uint32_t color) {
    int yy;
    if (!base || !stride || !screen_w || !screen_h || w <= 0 || h <= 0) return;
    if (x < 0) {
        w += x;
        x = 0;
    }
    if (y < 0) {
        h += y;
        y = 0;
    }
    if (x >= (int)screen_w || y >= (int)screen_h) return;
    if (x + w > (int)screen_w) w = (int)screen_w - x;
    if (y + h > (int)screen_h) h = (int)screen_h - y;
    if (w <= 0 || h <= 0) return;

    for (yy = 0; yy < h; ++yy) {
        uint32_t *row = (uint32_t *)(void *)(base + (uint32_t)(y + yy) * stride);
        int xx;
        for (xx = 0; xx < w; ++xx) row[x + xx] = color;
    }
}

static void paint_visible_bo_overlay(struct gbm_bo *bo, uint32_t frame) {
    uint32_t width;
    uint32_t height;
    uint32_t stride = 0;
    void *map_data = NULL;
    uint8_t *map;
    uint32_t y;
    int i;
    uint32_t pulse = frame % 120u;
    if (!bo) return;
    width = gbm_bo_get_width(bo);
    height = gbm_bo_get_height(bo);
    if (!width || !height) return;

    map = (uint8_t *)gbm_bo_map(bo, 0, 0, width, height,
                                GBM_BO_TRANSFER_WRITE, &stride, &map_data);
    if (!map || !stride) {
        static bool warned;
        if (!warned) {
            warned = true;
            fprintf(stderr,
                    "[ridux-gl-compositor] gbm_bo_map overlay unavailable: errno=%d (%s)\n",
                    errno,
                    strerror(errno));
        }
        return;
    }

    for (y = 0; y < height; ++y) {
        uint32_t shade = 24u + (y * 34u) / (height ? height : 1u);
        cpu_rect(map, stride, width, height, 0, (int)y, (int)width, 1,
                 rgb(shade, shade + 10u, shade + 24u));
    }

    cpu_rect(map, stride, width, height, 0, 0, (int)width, 52, rgb(32, 39, 49));
    cpu_rect(map, stride, width, height, 24, 14, 124, 24, rgb(79, 196, 232));
    cpu_rect(map, stride, width, height, (int)width - 268, 14, 72, 24, rgb(48, 176, 132));
    cpu_rect(map, stride, width, height, (int)width - 184, 14, 72, 24, rgb(208, 160, 56));
    cpu_rect(map, stride, width, height, (int)width - 100, 14, 76, 24, rgb(72, 88, 112));

    cpu_rect(map, stride, width, height, 68, 94, (int)(width * 42u / 100u), (int)(height * 52u / 100u), rgb(9, 13, 18));
    cpu_rect(map, stride, width, height, 60, 86, (int)(width * 42u / 100u), (int)(height * 52u / 100u), rgb(35, 44, 58));
    cpu_rect(map, stride, width, height, 60, 86, (int)(width * 42u / 100u), 44, rgb(50, 63, 82));
    cpu_rect(map, stride, width, height, 78, 104, 116, 13, rgb(105, 215, 244));
    cpu_rect(map, stride, width, height, 82, 158, (int)(width * 32u / 100u), 20, rgb(91, 111, 140));
    cpu_rect(map, stride, width, height, 82, 198, (int)(width * 28u / 100u), 20, rgb(69, 86, 113));
    cpu_rect(map, stride, width, height, 82, 238, (int)(width * 35u / 100u), 20, rgb(69, 86, 113));
    cpu_rect(map, stride, width, height, 82, 296, (int)(width * 15u / 100u), 92, rgb(36, 142, 183));
    cpu_rect(map, stride, width, height, 102 + (int)(width * 15u / 100u), 296, (int)(width * 15u / 100u), 92, rgb(166, 96, 196));

    cpu_rect(map, stride, width, height, (int)(width * 54u / 100u) + 8, 118,
             (int)(width * 32u / 100u), (int)(height * 42u / 100u), rgb(8, 12, 16));
    cpu_rect(map, stride, width, height, (int)(width * 54u / 100u), 110,
             (int)(width * 32u / 100u), (int)(height * 42u / 100u), rgb(40, 50, 66));
    cpu_rect(map, stride, width, height, (int)(width * 54u / 100u), 110,
             (int)(width * 32u / 100u), 42, rgb(58, 72, 92));
    cpu_rect(map, stride, width, height, (int)(width * 54u / 100u) + 24, 188,
             (int)(width * 24u / 100u), 58, rgb(58, 85, 113));
    cpu_rect(map, stride, width, height, (int)(width * 54u / 100u) + 24, 270,
             (int)(width * 24u / 100u), 58, rgb(58, 85, 113));
    cpu_rect(map, stride, width, height, (int)(width * 54u / 100u) + 24, 352,
             (int)(width * 24u / 100u), 58, rgb(58, 85, 113));

    cpu_rect(map, stride, width, height, (int)(width * 19u / 100u), (int)height - 100,
             (int)(width * 62u / 100u), 66, rgb(27, 34, 43));
    for (i = 0; i < 9; ++i) {
        uint32_t blue = 150u + (pulse > 60u ? 120u - pulse : pulse);
        cpu_rect(map, stride, width, height,
                 (int)(width * 23u / 100u) + i * 56,
                 (int)height - 84,
                 39,
                 39,
                 rgb(60u + (uint32_t)(i % 3) * 16u,
                     96u + (uint32_t)(i % 4) * 12u,
                     blue > 220u ? 220u : blue));
    }

    gbm_bo_unmap(bo, map_data);
}

static void rect_px(ridux_gl_t *gl,
                    float screen_w,
                    float screen_h,
                    float x,
                    float y,
                    float w,
                    float h,
                    float r,
                    float g,
                    float b,
                    float a) {
    GLint sx;
    GLint sy;
    GLsizei sw;
    GLsizei sh;
    const float bg_r = 0.085f;
    const float bg_g = 0.110f;
    const float bg_b = 0.145f;

    (void)gl;
    if (w <= 0.0f || h <= 0.0f || screen_w <= 0.0f || screen_h <= 0.0f) return;
    if (x < 0.0f) {
        w += x;
        x = 0.0f;
    }
    if (y < 0.0f) {
        h += y;
        y = 0.0f;
    }
    if (x + w > screen_w) w = screen_w - x;
    if (y + h > screen_h) h = screen_h - y;
    if (w <= 0.0f || h <= 0.0f) return;

    sx = (GLint)(x + 0.5f);
    sy = (GLint)(screen_h - y - h + 0.5f);
    sw = (GLsizei)(w + 0.5f);
    sh = (GLsizei)(h + 0.5f);
    if (sw <= 0 || sh <= 0) return;

    if (a < 1.0f) {
        r = r * a + bg_r * (1.0f - a);
        g = g * a + bg_g * (1.0f - a);
        b = b * a + bg_b * (1.0f - a);
    }
    glEnable(GL_SCISSOR_TEST);
    glScissor(sx, sy, sw, sh);
    glClearColor(r, g, b, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
}

static void draw_workspace(ridux_gl_t *gl, uint32_t width, uint32_t height, uint32_t frame) {
    float w = (float)width;
    float h = (float)height;
    float pulse = (float)(frame % 120u);
    int i;

    if (pulse > 60.0f) pulse = 120.0f - pulse;
    pulse /= 60.0f;

    glViewport(0, 0, (GLsizei)width, (GLsizei)height);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_SCISSOR_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glClearColor(0.085f, 0.110f, 0.145f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    for (i = 0; i < 30; ++i) {
        float t = (float)i / 29.0f;
        rect_px(gl, w, h, 0.0f, t * h, w, h / 29.0f + 2.0f,
                0.085f + t * 0.075f,
                0.110f + t * 0.080f,
                0.145f + t * 0.075f,
                1.0f);
    }

    rect_px(gl, w, h, 0.0f, 0.0f, w, 48.0f, 0.055f, 0.066f, 0.084f, 0.96f);
    rect_px(gl, w, h, 24.0f, 13.0f, 116.0f, 22.0f, 0.25f, 0.75f, 0.92f, 0.95f);
    rect_px(gl, w, h, w - 260.0f, 13.0f, 68.0f, 22.0f, 0.12f, 0.65f, 0.46f, 0.92f);
    rect_px(gl, w, h, w - 180.0f, 13.0f, 68.0f, 22.0f, 0.76f, 0.58f, 0.20f, 0.92f);
    rect_px(gl, w, h, w - 100.0f, 13.0f, 76.0f, 22.0f, 0.24f, 0.32f, 0.44f, 0.92f);
    rect_px(gl, w, h, 0.0f, 48.0f, w, 1.0f, 0.65f, 0.78f, 0.86f, 0.25f);

    rect_px(gl, w, h, 68.0f, 96.0f, w * 0.40f, h * 0.52f, 0.0f, 0.0f, 0.0f, 0.27f);
    rect_px(gl, w, h, 60.0f, 88.0f, w * 0.40f, h * 0.52f, 0.08f, 0.10f, 0.13f, 0.98f);
    rect_px(gl, w, h, 60.0f, 88.0f, w * 0.40f, 42.0f, 0.10f, 0.13f, 0.17f, 1.0f);
    rect_px(gl, w, h, 76.0f, 103.0f, 82.0f, 12.0f, 0.35f, 0.80f, 0.96f, 1.0f);
    rect_px(gl, w, h, 80.0f, 156.0f, w * 0.33f, 18.0f, 0.24f, 0.30f, 0.38f, 0.92f);
    rect_px(gl, w, h, 80.0f, 194.0f, w * 0.29f, 18.0f, 0.18f, 0.23f, 0.31f, 0.92f);
    rect_px(gl, w, h, 80.0f, 232.0f, w * 0.35f, 18.0f, 0.18f, 0.23f, 0.31f, 0.92f);
    rect_px(gl, w, h, 80.0f, 288.0f, w * 0.16f, 88.0f, 0.11f, 0.42f, 0.58f, 0.95f);
    rect_px(gl, w, h, 80.0f + w * 0.18f, 288.0f, w * 0.16f, 88.0f, 0.52f, 0.31f, 0.64f, 0.95f);

    rect_px(gl, w, h, w * 0.54f + 8.0f, 116.0f, w * 0.32f, h * 0.42f, 0.0f, 0.0f, 0.0f, 0.22f);
    rect_px(gl, w, h, w * 0.54f, 108.0f, w * 0.32f, h * 0.42f, 0.095f, 0.115f, 0.145f, 0.98f);
    rect_px(gl, w, h, w * 0.54f, 108.0f, w * 0.32f, 40.0f, 0.13f, 0.16f, 0.20f, 1.0f);
    rect_px(gl, w, h, w * 0.54f + 18.0f, 123.0f, 130.0f, 10.0f, 0.80f, 0.86f, 0.90f, 0.95f);
    rect_px(gl, w, h, w * 0.54f + 22.0f, 184.0f, w * 0.25f, 54.0f, 0.14f, 0.20f, 0.27f, 0.95f);
    rect_px(gl, w, h, w * 0.54f + 22.0f, 260.0f, w * 0.25f, 54.0f, 0.14f, 0.20f, 0.27f, 0.95f);
    rect_px(gl, w, h, w * 0.54f + 22.0f, 336.0f, w * 0.25f, 54.0f, 0.14f, 0.20f, 0.27f, 0.95f);

    rect_px(gl, w, h, w * 0.19f, h - 92.0f, w * 0.62f, 68.0f, 0.0f, 0.0f, 0.0f, 0.30f);
    rect_px(gl, w, h, w * 0.19f, h - 98.0f, w * 0.62f, 64.0f, 0.075f, 0.085f, 0.105f, 0.95f);

    for (i = 0; i < 9; ++i) {
        float x = w * 0.23f + (float)i * 56.0f;
        float y = h - 82.0f;
        float blue = 0.42f + 0.10f * pulse;
        rect_px(gl, w, h, x, y, 38.0f, 38.0f,
                0.20f + 0.035f * (float)(i % 3),
                0.36f + 0.030f * (float)(i % 4),
                blue,
                0.96f);
        rect_px(gl, w, h, x + 9.0f, y + 9.0f, 20.0f, 20.0f,
                0.88f, 0.93f, 0.96f, 0.78f);
    }

    rect_px(gl, w, h,
            360.0f + pulse * 140.0f,
            h - 5.0f,
            220.0f,
            2.0f,
            0.25f,
            0.78f,
            1.0f,
            0.75f);
}

static void page_flip_done(int fd,
                           unsigned int frame,
                           unsigned int sec,
                           unsigned int usec,
                           void *data) {
    int *waiting = (int *)data;
    (void)fd;
    (void)frame;
    (void)sec;
    (void)usec;
    *waiting = 0;
}

static bool wait_for_page_flip(int fd, int *waiting) {
    drmEventContext ev;
    memset(&ev, 0, sizeof(ev));
    ev.version = DRM_EVENT_CONTEXT_VERSION;
    ev.page_flip_handler = page_flip_done;

    while (*waiting) {
        fd_set fds;
        int rc;
        FD_ZERO(&fds);
        FD_SET(fd, &fds);
        rc = select(fd + 1, &fds, NULL, NULL, NULL);
        if (rc < 0) {
            if (errno == EINTR) continue;
            log_errno("select pageflip");
            return false;
        }
        if (drmHandleEvent(fd, &ev) != 0) {
            log_errno("drmHandleEvent");
            return false;
        }
    }
    return true;
}

static bool present_frame(ridux_drm_output_t *out,
                          ridux_gl_t *gl,
                          ridux_fb_t *front,
                          bool *page_flip_ok,
                          bool first_frame,
                          bool immediate_present,
                          uint32_t frame) {
    ridux_fb_t next;
    struct gbm_bo *bo;

    if (immediate_present && front->bo) {
        release_fb(out->fd, gl->surface, front);
    }

    if (!eglSwapBuffers(gl->display, gl->egl_surface)) {
        fprintf(stderr, "[ridux-gl-compositor] eglSwapBuffers failed: egl=0x%x\n",
                eglGetError());
        return false;
    }

    bo = gbm_surface_lock_front_buffer(gl->surface);
    if (!bo) {
        log_errno("gbm_surface_lock_front_buffer");
        return false;
    }
    paint_visible_bo_overlay(bo, frame);

    if (add_fb_for_bo(out->fd, bo, &next) != 0) {
        gbm_surface_release_buffer(gl->surface, bo);
        return false;
    }

    if (first_frame || !front->fb_id || !*page_flip_ok) {
        if (drmModeSetCrtc(out->fd,
                           out->crtc_id,
                           next.fb_id,
                           0,
                           0,
                           &out->connector_id,
                           1,
                           &out->mode) != 0) {
            log_errno("drmModeSetCrtc");
            release_fb(out->fd, gl->surface, &next);
            return false;
        }
    } else {
        int waiting = 1;
        if (drmModePageFlip(out->fd,
                            out->crtc_id,
                            next.fb_id,
                            DRM_MODE_PAGE_FLIP_EVENT,
                            &waiting) != 0) {
            fprintf(stderr,
                    "[ridux-gl-compositor] pageflip unavailable, staying on SetCrtc present: errno=%d (%s)\n",
                    errno,
                    strerror(errno));
            *page_flip_ok = false;
            if (drmModeSetCrtc(out->fd,
                               out->crtc_id,
                               next.fb_id,
                               0,
                               0,
                               &out->connector_id,
                               1,
                               &out->mode) != 0) {
                log_errno("drmModeSetCrtc fallback");
                release_fb(out->fd, gl->surface, &next);
                return false;
            }
        } else if (!wait_for_page_flip(out->fd, &waiting)) {
            *page_flip_ok = false;
        }
    }

    if (immediate_present) {
        release_fb(out->fd, gl->surface, &next);
        memset(front, 0, sizeof(*front));
        return true;
    }

    release_fb(out->fd, gl->surface, front);
    *front = next;
    return true;
}

int main(int argc, char **argv) {
    ridux_drm_output_t out;
    ridux_gl_t gl;
    ridux_fb_t front;
    bool page_flip_ok = true;
    uint32_t frame = 0;
    bool first_frame = true;
    bool immediate_present;

    (void)argc;
    (void)argv;

    setvbuf(stderr, NULL, _IONBF, 0);
    fprintf(stderr, "[ridux-gl-compositor] start backend=Mesa EGL/GBM/KMS\n");

    memset(&front, 0, sizeof(front));
    immediate_present = env_enabled("RIDUX_GBM_IMMEDIATE_PRESENT");
    if (immediate_present) page_flip_ok = false;

    if (!open_drm_output(&out)) {
        fprintf(stderr, "[ridux-gl-compositor] no DRM/KMS output available\n");
        return 2;
    }

    if (!init_gl(&gl, out.fd, out.mode.hdisplay, out.mode.vdisplay)) {
        fprintf(stderr, "[ridux-gl-compositor] OpenGL compositor init failed\n");
        return 3;
    }

    while (1) {
        draw_workspace(&gl, out.mode.hdisplay, out.mode.vdisplay, frame);
        if (!present_frame(&out, &gl, &front, &page_flip_ok, first_frame, immediate_present, frame)) {
            fprintf(stderr, "[ridux-gl-compositor] present failed at frame=%u\n", frame);
            return 4;
        }
        first_frame = false;
        if ((frame % 120u) == 0u) {
            fprintf(stderr,
                    "[ridux-gl-compositor] frame=%u compositor=opengl mesa=gbm kms=%ux%u pageflip=%s\n",
                    frame,
                    out.mode.hdisplay,
                    out.mode.vdisplay,
                    page_flip_ok ? "on" : "setcrtc");
        }
        ++frame;
        usleep(16000);
    }

    return 0;
}
