#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>
#include <wayland-client.h>

#include "xdg-shell-client-protocol.h"

#ifndef MFD_CLOEXEC
#define MFD_CLOEXEC 0x0001U
#endif

#define DRM_IOCTL_BASE 'd'
#define DRM_IOWR(n, type) _IOWR(DRM_IOCTL_BASE, (n), type)
#define DRM_IOCTL_VERSION DRM_IOWR(0x00, struct drm_version)
#define DRM_IOCTL_GET_CAP DRM_IOWR(0x0c, struct drm_get_cap)
#define DRM_IOCTL_SET_CLIENT_CAP 0x4010640DUL
#define DRM_IOCTL_MODE_GETRESOURCES DRM_IOWR(0xA0, struct drm_mode_card_res)
#define DRM_IOCTL_MODE_ATOMIC DRM_IOWR(0xBC, struct drm_mode_atomic)
#define DRM_IOCTL_MODE_CREATEPROPBLOB DRM_IOWR(0xBD, struct drm_mode_create_blob)
#define DRM_IOCTL_MODE_DESTROYPROPBLOB DRM_IOWR(0xBE, struct drm_mode_destroy_blob)
#define DRM_IOCTL_SYNCOBJ_CREATE DRM_IOWR(0xBF, struct drm_syncobj_create)
#define DRM_IOCTL_SYNCOBJ_DESTROY DRM_IOWR(0xC0, struct drm_syncobj_destroy)
#define DRM_IOCTL_SYNCOBJ_WAIT DRM_IOWR(0xC3, struct drm_syncobj_wait)
#define DRM_IOCTL_SYNCOBJ_SIGNAL DRM_IOWR(0xC5, struct drm_syncobj_array)
#define DRM_CAP_DUMB_BUFFER 0x1
#define DRM_CAP_PRIME 0x5
#define DRM_CAP_SYNCOBJ 0x13
#define DRM_CAP_SYNCOBJ_TIMELINE 0x14
#define DRM_CAP_ATOMIC_ASYNC_PAGE_FLIP 0x15
#define DRM_CLIENT_CAP_ATOMIC 3
#define DRM_MODE_ATOMIC_TEST_ONLY 0x0100

struct drm_version {
    int version_major;
    int version_minor;
    int version_patchlevel;
    size_t name_len;
    char *name;
    size_t date_len;
    char *date;
    size_t desc_len;
    char *desc;
};

struct drm_get_cap {
    uint64_t capability;
    uint64_t value;
};

struct drm_set_client_cap {
    uint64_t capability;
    uint64_t value;
};

struct drm_mode_card_res {
    uint64_t fb_id_ptr;
    uint64_t crtc_id_ptr;
    uint64_t connector_id_ptr;
    uint64_t encoder_id_ptr;
    uint32_t count_fbs;
    uint32_t count_crtcs;
    uint32_t count_connectors;
    uint32_t count_encoders;
    uint32_t min_width;
    uint32_t max_width;
    uint32_t min_height;
    uint32_t max_height;
};

struct drm_mode_atomic {
    uint32_t flags;
    uint32_t count_objs;
    uint64_t objs_ptr;
    uint64_t count_props_ptr;
    uint64_t props_ptr;
    uint64_t prop_values_ptr;
    uint64_t reserved;
    uint64_t user_data;
};

struct drm_mode_create_blob {
    uint64_t data;
    uint32_t length;
    uint32_t blob_id;
};

struct drm_mode_destroy_blob {
    uint32_t blob_id;
};

struct drm_syncobj_create {
    uint32_t handle;
    uint32_t flags;
};

struct drm_syncobj_destroy {
    uint32_t handle;
    uint32_t pad;
};

struct drm_syncobj_wait {
    uint64_t handles;
    int64_t timeout_nsec;
    uint32_t count_handles;
    uint32_t flags;
    uint32_t first_signaled;
    uint32_t pad;
    uint64_t deadline_nsec;
};

struct drm_syncobj_array {
    uint64_t handles;
    uint32_t count_handles;
    uint32_t pad;
};

struct ladder_state {
    struct wl_display *display;
    struct wl_registry *registry;
    struct wl_compositor *compositor;
    struct wl_shm *shm;
    struct xdg_wm_base *wm_base;
    struct wl_surface *surface;
    struct xdg_surface *xdg_surface;
    struct xdg_toplevel *toplevel;
    struct wl_buffer *buffer;
    int configured;
    int closed;
    int width;
    int height;
};

struct frame_probe {
    struct wl_callback *callback;
    uint32_t frames;
    uint32_t commits;
    int pending;
};

static int fail_count;

static void mark_ok(const char *name) {
    fprintf(stderr, "[ridux-gpu-ladder] %s=ok\n", name);
}

static void mark_fail(const char *name, const char *detail) {
    ++fail_count;
    fprintf(stderr, "[ridux-gpu-ladder] %s=fail detail=%s errno=%d\n",
            name, detail ? detail : "unknown", errno);
}

static int min_int(int a, int b) {
    return a < b ? a : b;
}

static int env_truthy(const char *name) {
    const char *value = name ? getenv(name) : NULL;
    return value && (strcmp(value, "1") == 0 ||
                     strcmp(value, "true") == 0 ||
                     strcmp(value, "yes") == 0 ||
                     strcmp(value, "on") == 0);
}

static int connect_unix_path(const char *path) {
    struct sockaddr_un addr;
    int fd;

    if (!path || !*path) return -1;
    fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", path);
    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(fd);
        return -1;
    }
    return fd;
}

static int connect_unix_path_retry(const char *path, unsigned attempts, useconds_t delay_us) {
    unsigned i;
    for (i = 0; i < attempts; ++i) {
        int fd = connect_unix_path(path);
        if (fd >= 0) return fd;
        usleep(delay_us);
    }
    return -1;
}

static const char *dbus_path_from_env(const char *env_name, const char *fallback) {
    const char *value = getenv(env_name);
    const char *prefix = "unix:path=";
    size_t prefix_len = strlen(prefix);

    if (value && strncmp(value, prefix, prefix_len) == 0) return value + prefix_len;
    return fallback;
}

static void dbus_smoke_one(const char *label, const char *path) {
    int fd = connect_unix_path_retry(path, 20, 100000);
    char name[96];

    snprintf(name, sizeof(name), "dbus_%s_connect", label);
    if (fd >= 0) {
        close(fd);
        fprintf(stderr, "[ridux-gpu-ladder] %s=ok path=%s\n", name, path);
    } else {
        mark_fail(name, path);
    }
}

static void dbus_smoke(void) {
    dbus_smoke_one("session",
                   dbus_path_from_env("DBUS_SESSION_BUS_ADDRESS", "/run/user/1000/bus"));
    dbus_smoke_one("system",
                   dbus_path_from_env("DBUS_SYSTEM_BUS_ADDRESS", "/run/dbus/system_bus_socket"));
}

static void drm_get_cap_log(int fd, uint64_t capability, const char *label) {
    struct drm_get_cap cap;

    memset(&cap, 0, sizeof(cap));
    cap.capability = capability;
    if (ioctl(fd, DRM_IOCTL_GET_CAP, &cap) == 0) {
        fprintf(stderr, "[ridux-gpu-ladder] drm_cap_%s=ok value=%llu\n",
                label, (unsigned long long)cap.value);
    } else {
        fprintf(stderr, "[ridux-gpu-ladder] drm_cap_%s=check errno=%d\n",
                label, errno);
    }
}

static void drm_modern_contract_smoke(int fd);

static void drm_smoke(void) {
    struct drm_version version;
    struct drm_mode_card_res res;
    char name[64];
    char date[64];
    char desc[160];
    int fd;

    fd = open("/dev/dri/card0", O_RDWR | O_CLOEXEC);
    if (fd < 0) {
        mark_fail("drm_open_card0", "/dev/dri/card0");
        return;
    }
    fprintf(stderr, "[ridux-gpu-ladder] drm_open_card0=ok fd=%d\n", fd);

    memset(name, 0, sizeof(name));
    memset(date, 0, sizeof(date));
    memset(desc, 0, sizeof(desc));
    memset(&version, 0, sizeof(version));
    version.name_len = sizeof(name) - 1;
    version.name = name;
    version.date_len = sizeof(date) - 1;
    version.date = date;
    version.desc_len = sizeof(desc) - 1;
    version.desc = desc;
    if (ioctl(fd, DRM_IOCTL_VERSION, &version) == 0) {
        fprintf(stderr,
                "[ridux-gpu-ladder] drm_version=ok name=%s version=%d.%d.%d desc=%s\n",
                name, version.version_major, version.version_minor,
                version.version_patchlevel, desc);
    } else {
        mark_fail("drm_version", "DRM_IOCTL_VERSION");
    }

    memset(&res, 0, sizeof(res));
    if (ioctl(fd, DRM_IOCTL_MODE_GETRESOURCES, &res) == 0) {
        fprintf(stderr,
                "[ridux-gpu-ladder] drm_resources=ok crtcs=%u connectors=%u encoders=%u range=%ux%u-%ux%u\n",
                res.count_crtcs, res.count_connectors, res.count_encoders,
                res.min_width, res.min_height, res.max_width, res.max_height);
    } else {
        mark_fail("drm_resources", "DRM_IOCTL_MODE_GETRESOURCES");
    }

    drm_get_cap_log(fd, DRM_CAP_DUMB_BUFFER, "dumb_buffer");
    drm_get_cap_log(fd, DRM_CAP_PRIME, "prime");
    drm_get_cap_log(fd, DRM_CAP_SYNCOBJ, "syncobj");
    drm_get_cap_log(fd, DRM_CAP_SYNCOBJ_TIMELINE, "syncobj_timeline");
    drm_get_cap_log(fd, DRM_CAP_ATOMIC_ASYNC_PAGE_FLIP, "atomic_async_page_flip");
    drm_modern_contract_smoke(fd);
}

static void drm_modern_contract_smoke(int fd) {
    struct drm_set_client_cap client_cap;
    struct drm_mode_create_blob blob;
    struct drm_mode_destroy_blob destroy_blob;
    struct drm_mode_atomic atomic;
    struct drm_syncobj_create sync_create;
    struct drm_syncobj_wait sync_wait;
    struct drm_syncobj_array sync_array;
    struct drm_syncobj_destroy sync_destroy;
    uint32_t handles[1];
    uint8_t mode_blob[128];

    memset(&client_cap, 0, sizeof(client_cap));
    client_cap.capability = DRM_CLIENT_CAP_ATOMIC;
    client_cap.value = 1;
    if (ioctl(fd, DRM_IOCTL_SET_CLIENT_CAP, &client_cap) == 0)
        mark_ok("drm_client_cap_atomic");
    else
        mark_fail("drm_client_cap_atomic", "DRM_IOCTL_SET_CLIENT_CAP");

    memset(mode_blob, 0, sizeof(mode_blob));
    memset(&blob, 0, sizeof(blob));
    blob.data = (uint64_t)(uintptr_t)mode_blob;
    blob.length = sizeof(mode_blob);
    if (ioctl(fd, DRM_IOCTL_MODE_CREATEPROPBLOB, &blob) == 0 && blob.blob_id) {
        fprintf(stderr, "[ridux-gpu-ladder] drm_prop_blob=ok id=%u\n", blob.blob_id);
        memset(&destroy_blob, 0, sizeof(destroy_blob));
        destroy_blob.blob_id = blob.blob_id;
        (void)ioctl(fd, DRM_IOCTL_MODE_DESTROYPROPBLOB, &destroy_blob);
    } else {
        mark_fail("drm_prop_blob", "DRM_IOCTL_MODE_CREATEPROPBLOB");
    }

    memset(&atomic, 0, sizeof(atomic));
    atomic.flags = DRM_MODE_ATOMIC_TEST_ONLY;
    if (ioctl(fd, DRM_IOCTL_MODE_ATOMIC, &atomic) == 0)
        mark_ok("drm_atomic_test_only");
    else
        mark_fail("drm_atomic_test_only", "DRM_IOCTL_MODE_ATOMIC");

    memset(&sync_create, 0, sizeof(sync_create));
    sync_create.flags = 1;
    if (ioctl(fd, DRM_IOCTL_SYNCOBJ_CREATE, &sync_create) == 0 && sync_create.handle) {
        fprintf(stderr, "[ridux-gpu-ladder] drm_syncobj_create=ok handle=%u\n",
                sync_create.handle);
        handles[0] = sync_create.handle;
        memset(&sync_wait, 0, sizeof(sync_wait));
        sync_wait.handles = (uint64_t)(uintptr_t)handles;
        sync_wait.count_handles = 1;
        sync_wait.timeout_nsec = 0;
        if (ioctl(fd, DRM_IOCTL_SYNCOBJ_WAIT, &sync_wait) == 0)
            mark_ok("drm_syncobj_wait");
        else
            mark_fail("drm_syncobj_wait", "DRM_IOCTL_SYNCOBJ_WAIT");
        memset(&sync_array, 0, sizeof(sync_array));
        sync_array.handles = (uint64_t)(uintptr_t)handles;
        sync_array.count_handles = 1;
        if (ioctl(fd, DRM_IOCTL_SYNCOBJ_SIGNAL, &sync_array) == 0)
            mark_ok("drm_syncobj_signal");
        else
            mark_fail("drm_syncobj_signal", "DRM_IOCTL_SYNCOBJ_SIGNAL");
        memset(&sync_destroy, 0, sizeof(sync_destroy));
        sync_destroy.handle = sync_create.handle;
        (void)ioctl(fd, DRM_IOCTL_SYNCOBJ_DESTROY, &sync_destroy);
    } else {
        mark_fail("drm_syncobj_create", "DRM_IOCTL_SYNCOBJ_CREATE");
    }

    close(fd);
}

static int make_shm_file(size_t size) {
    char path[128];
    int fd;

#ifdef SYS_memfd_create
    fd = (int)syscall(SYS_memfd_create, "ridux-gpu-ladder", MFD_CLOEXEC);
    if (fd >= 0) {
        if (ftruncate(fd, (off_t)size) == 0) return fd;
        close(fd);
    }
#endif

    snprintf(path, sizeof(path), "/tmp/ridux-gpu-ladder-%ld.shm", (long)getpid());
    fd = open(path, O_CREAT | O_RDWR | O_TRUNC | O_CLOEXEC, 0600);
    if (fd < 0) return -1;
    unlink(path);
    if (ftruncate(fd, (off_t)size) < 0) {
        close(fd);
        return -1;
    }
    return fd;
}

static void paint_gradient(uint32_t *pixels, int width, int height) {
    int x;
    int y;

    for (y = 0; y < height; ++y) {
        for (x = 0; x < width; ++x) {
            uint8_t r = (uint8_t)(32 + (x * 180) / width);
            uint8_t g = (uint8_t)(48 + (y * 160) / height);
            uint8_t b = (uint8_t)(180 - (x * 80) / width);
            if ((x / 16 + y / 16) & 1) {
                r = (uint8_t)min_int(255, r + 34);
                g = (uint8_t)min_int(255, g + 34);
            }
            pixels[y * width + x] =
                ((uint32_t)0xff << 24) | ((uint32_t)r << 16) |
                ((uint32_t)g << 8) | (uint32_t)b;
        }
    }
}

static struct wl_buffer *create_shm_buffer(struct ladder_state *state,
                                           int width, int height) {
    struct wl_shm_pool *pool;
    struct wl_buffer *buffer;
    uint32_t *pixels;
    int stride = width * 4;
    size_t size = (size_t)stride * (size_t)height;
    int fd = make_shm_file(size);

    if (fd < 0) {
        mark_fail("wayland_shm_file", "memfd/tmpfile");
        return NULL;
    }
    pixels = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (pixels == MAP_FAILED) {
        mark_fail("wayland_shm_mmap", "mmap");
        close(fd);
        return NULL;
    }
    paint_gradient(pixels, width, height);
    munmap(pixels, size);

    pool = wl_shm_create_pool(state->shm, fd, (int)size);
    buffer = wl_shm_pool_create_buffer(pool, 0, width, height, stride,
                                       WL_SHM_FORMAT_XRGB8888);
    wl_shm_pool_destroy(pool);
    close(fd);
    return buffer;
}

static void xdg_wm_base_ping(void *data, struct xdg_wm_base *wm_base,
                             uint32_t serial) {
    (void)data;
    xdg_wm_base_pong(wm_base, serial);
}

static const struct xdg_wm_base_listener wm_base_listener = {
    .ping = xdg_wm_base_ping,
};

static void xdg_surface_configure(void *data, struct xdg_surface *surface,
                                  uint32_t serial) {
    struct ladder_state *state = (struct ladder_state *)data;
    xdg_surface_ack_configure(surface, serial);
    state->configured = 1;
}

static const struct xdg_surface_listener xdg_surface_listener = {
    .configure = xdg_surface_configure,
};

static void xdg_toplevel_configure(void *data, struct xdg_toplevel *toplevel,
                                   int32_t width, int32_t height,
                                   struct wl_array *states) {
    struct ladder_state *state = (struct ladder_state *)data;
    (void)toplevel;
    (void)states;
    if (width > 0) state->width = width;
    if (height > 0) state->height = height;
}

static void xdg_toplevel_close(void *data, struct xdg_toplevel *toplevel) {
    struct ladder_state *state = (struct ladder_state *)data;
    (void)toplevel;
    state->closed = 1;
}

static const struct xdg_toplevel_listener xdg_toplevel_listener = {
    .configure = xdg_toplevel_configure,
    .close = xdg_toplevel_close,
};

static void registry_global(void *data, struct wl_registry *registry,
                            uint32_t name, const char *interface,
                            uint32_t version) {
    struct ladder_state *state = (struct ladder_state *)data;

    if (strcmp(interface, "wl_compositor") == 0) {
        state->compositor = wl_registry_bind(
            registry, name, &wl_compositor_interface, version < 4 ? version : 4);
    } else if (strcmp(interface, "wl_shm") == 0) {
        state->shm = wl_registry_bind(registry, name, &wl_shm_interface, 1);
    } else if (strcmp(interface, "xdg_wm_base") == 0) {
        state->wm_base = wl_registry_bind(
            registry, name, &xdg_wm_base_interface, version < 2 ? version : 2);
        xdg_wm_base_add_listener(state->wm_base, &wm_base_listener, state);
    }
}

static void registry_remove(void *data, struct wl_registry *registry,
                            uint32_t name) {
    (void)data;
    (void)registry;
    (void)name;
}

static const struct wl_registry_listener registry_listener = {
    .global = registry_global,
    .global_remove = registry_remove,
};

static int64_t monotonic_ms(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) return 0;
    return (int64_t)ts.tv_sec * 1000 + (int64_t)(ts.tv_nsec / 1000000);
}

static void frame_probe_done(void *data, struct wl_callback *callback,
                             uint32_t callback_data) {
    struct frame_probe *probe = (struct frame_probe *)data;
    (void)callback_data;
    if (callback) wl_callback_destroy(callback);
    if (probe) {
        probe->callback = NULL;
        probe->pending = 0;
        ++probe->frames;
    }
}

static const struct wl_callback_listener frame_probe_listener = {
    .done = frame_probe_done,
};

static void wayland_frame_probe(struct ladder_state *state, int duration_ms) {
    struct frame_probe probe;
    int fd;
    int old_flags;
    int64_t start_ms;
    int64_t now_ms;
    int64_t elapsed_ms;

    if (!state || !state->display || !state->surface) return;
    memset(&probe, 0, sizeof(probe));
    fd = wl_display_get_fd(state->display);
    old_flags = fcntl(fd, F_GETFL, 0);
    if (old_flags >= 0) (void)fcntl(fd, F_SETFL, old_flags | O_NONBLOCK);

    start_ms = monotonic_ms();
    now_ms = start_ms;
    while (!state->closed && (now_ms - start_ms) < duration_ms) {
        struct pollfd pfd;
        int rc;

        if (!probe.pending) {
            probe.callback = wl_surface_frame(state->surface);
            if (!probe.callback) break;
            wl_callback_add_listener(probe.callback, &frame_probe_listener, &probe);
            probe.pending = 1;
            ++probe.commits;
            wl_surface_damage_buffer(state->surface, 0, 0, state->width, state->height);
            wl_surface_commit(state->surface);
        }

        while (wl_display_dispatch_pending(state->display) > 0) {
            if (!probe.pending) break;
        }
        (void)wl_display_flush(state->display);

        memset(&pfd, 0, sizeof(pfd));
        pfd.fd = fd;
        pfd.events = POLLIN;
        rc = poll(&pfd, 1, 8);
        if (rc > 0 && (pfd.revents & POLLIN)) {
            rc = wl_display_dispatch(state->display);
            if (rc < 0 && errno != EAGAIN && errno != EWOULDBLOCK) break;
        } else if (rc < 0 && errno != EINTR) {
            break;
        }
        now_ms = monotonic_ms();
    }

    if (probe.callback) {
        wl_callback_destroy(probe.callback);
        probe.callback = NULL;
        probe.pending = 0;
    }
    if (old_flags >= 0) (void)fcntl(fd, F_SETFL, old_flags);

    elapsed_ms = monotonic_ms() - start_ms;
    if (elapsed_ms <= 0) elapsed_ms = 1;
    if (probe.frames > 0) {
        uint64_t fps_x10 = ((uint64_t)probe.frames * 10000ULL +
                            (uint64_t)(elapsed_ms / 2)) / (uint64_t)elapsed_ms;
        uint64_t avg_us = ((uint64_t)elapsed_ms * 1000ULL +
                           (uint64_t)(probe.frames / 2)) / (uint64_t)probe.frames;
        fprintf(stderr,
                "[ridux-fps] wayland_frame_callbacks=ok frames=%u elapsed_ms=%lld fps=%llu.%01llu avg_ms=%llu.%03llu commits=%u\n",
                probe.frames, (long long)elapsed_ms,
                (unsigned long long)(fps_x10 / 10ULL),
                (unsigned long long)(fps_x10 % 10ULL),
                (unsigned long long)(avg_us / 1000ULL),
                (unsigned long long)(avg_us % 1000ULL),
                probe.commits);
    } else {
        fprintf(stderr,
                "[ridux-fps] wayland_frame_callbacks=check frames=0 elapsed_ms=%lld commits=%u\n",
                (long long)elapsed_ms, probe.commits);
    }
}

static int wait_for_xdg_configure(struct ladder_state *state, int timeout_ms) {
    int fd;
    int old_flags;
    int elapsed = 0;

    if (!state || !state->display) return -1;
    fd = wl_display_get_fd(state->display);
    old_flags = fcntl(fd, F_GETFL, 0);
    if (old_flags >= 0) (void)fcntl(fd, F_SETFL, old_flags | O_NONBLOCK);

    while (!state->configured && !state->closed && elapsed < timeout_ms) {
        struct pollfd pfd;
        int rc;

        while (wl_display_dispatch_pending(state->display) > 0) {
            if (state->configured || state->closed) break;
        }
        if (state->configured || state->closed) break;

        (void)wl_display_flush(state->display);
        memset(&pfd, 0, sizeof(pfd));
        pfd.fd = fd;
        pfd.events = POLLIN;
        rc = poll(&pfd, 1, 100);
        if (rc > 0 && (pfd.revents & POLLIN)) {
            rc = wl_display_dispatch(state->display);
            if (rc < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
                if (old_flags >= 0) (void)fcntl(fd, F_SETFL, old_flags);
                return -1;
            }
        } else if (rc < 0 && errno != EINTR) {
            if (old_flags >= 0) (void)fcntl(fd, F_SETFL, old_flags);
            return -1;
        }
        elapsed += 100;
    }

    if (old_flags >= 0) (void)fcntl(fd, F_SETFL, old_flags);
    return state->configured ? 1 : 0;
}

static void destroy_wayland_state(struct ladder_state *state) {
    if (state->buffer) wl_buffer_destroy(state->buffer);
    if (state->toplevel) xdg_toplevel_destroy(state->toplevel);
    if (state->xdg_surface) xdg_surface_destroy(state->xdg_surface);
    if (state->surface) wl_surface_destroy(state->surface);
    if (state->wm_base) xdg_wm_base_destroy(state->wm_base);
    if (state->shm) wl_shm_destroy(state->shm);
    if (state->compositor) wl_compositor_destroy(state->compositor);
    if (state->registry) wl_registry_destroy(state->registry);
    if (state->display) wl_display_disconnect(state->display);
}

static void wayland_smoke(void) {
    struct ladder_state state;
    int visible = env_truthy("RIDUX_GPU_LADDER_VISIBLE");
    int i;

    memset(&state, 0, sizeof(state));
    state.width = visible ? 360 : 1;
    state.height = visible ? 220 : 1;
    for (i = 0; i < 50 && !state.display; ++i) {
        state.display = wl_display_connect(NULL);
        if (!state.display) usleep(100000);
    }
    if (!state.display) {
        mark_fail("wayland_connect", "wl_display_connect");
        return;
    }
    mark_ok("wayland_connect");

    state.registry = wl_display_get_registry(state.display);
    wl_registry_add_listener(state.registry, &registry_listener, &state);
    wl_display_roundtrip(state.display);
    wl_display_roundtrip(state.display);

    if (state.compositor && state.shm && state.wm_base) {
        fprintf(stderr,
                "[ridux-gpu-ladder] wayland_registry=ok compositor=1 shm=1 xdg=1\n");
    } else {
        mark_fail("wayland_registry", "missing compositor/shm/xdg");
        destroy_wayland_state(&state);
        return;
    }

    state.surface = wl_compositor_create_surface(state.compositor);
    state.xdg_surface = xdg_wm_base_get_xdg_surface(state.wm_base, state.surface);
    xdg_surface_add_listener(state.xdg_surface, &xdg_surface_listener, &state);
    state.toplevel = xdg_surface_get_toplevel(state.xdg_surface);
    xdg_toplevel_add_listener(state.toplevel, &xdg_toplevel_listener, &state);
    xdg_toplevel_set_title(state.toplevel,
                           visible ? "Ridux GPU ladder" : "Ridux GPU probe");
    wl_surface_commit(state.surface);
    wl_display_flush(state.display);

    i = wait_for_xdg_configure(&state, 12000);
    if (!state.configured) {
        mark_fail("wayland_xdg_configure", i < 0 ? "dispatch" : "timeout");
        destroy_wayland_state(&state);
        return;
    }
    mark_ok("wayland_xdg_configure");

    state.buffer = create_shm_buffer(&state, state.width, state.height);
    if (!state.buffer) {
        destroy_wayland_state(&state);
        return;
    }
    wl_surface_attach(state.surface, state.buffer, 0, 0);
    wl_surface_damage_buffer(state.surface, 0, 0, state.width, state.height);
    wl_surface_commit(state.surface);
    wl_display_flush(state.display);
    fprintf(stderr,
            "[ridux-gpu-ladder] wayland_shm_window=ok size=%dx%d visible=%d\n",
            state.width, state.height, visible ? 1 : 0);
    wayland_frame_probe(&state, 2200);
    if (!visible) {
        wl_surface_attach(state.surface, NULL, 0, 0);
        wl_surface_commit(state.surface);
        wl_display_flush(state.display);
        wl_display_roundtrip(state.display);
    }
    destroy_wayland_state(&state);
}

int main(void) {
    fprintf(stderr, "[ridux-gpu-ladder] begin pid=%ld\n", (long)getpid());
    drm_smoke();
    dbus_smoke();
    wayland_smoke();
    if (fail_count == 0) {
        fprintf(stderr, "[ridux-gpu-ladder] overall=ok\n");
        return 0;
    }
    fprintf(stderr, "[ridux-gpu-ladder] overall=fail failures=%d\n", fail_count);
    return 1;
}
