#define _GNU_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <linux/netlink.h>
#include <stdint.h>
#include <string.h>
#include <dlfcn.h>
#include <sys/eventfd.h>
#include <sys/socket.h>
#include <unistd.h>

struct udev;
struct udev_device;
struct udev_monitor;
struct libinput;

typedef struct ridux_fake_udev_monitor {
    uint32_t magic;
    int fd;
    int spare_fd;
} ridux_fake_udev_monitor_t;

static ridux_fake_udev_monitor_t g_ridux_monitor = {0x5244554dU, -1, -1};
static int g_ridux_logged_load;
static int g_ridux_logged_assign;
static int g_ridux_logged_dispatch;
static int g_ridux_logged_fd;

static void ridux_log_once(int *flag, const char *msg) {
    if (*flag)
        return;
    *flag = 1;
    (void)write(STDERR_FILENO, msg, strlen(msg));
}

__attribute__((constructor))
static void ridux_udev_monitor_shim_loaded(void) {
    ridux_log_once(&g_ridux_logged_load,
                   "[ridux-udev-shim] loaded for Wayfire/libinput ABI\n");
}

static int ridux_pipe_monitor_fd(void) {
    int fds[2] = {-1, -1};
#ifdef O_CLOEXEC
    if (pipe2(fds, O_CLOEXEC | O_NONBLOCK) != 0)
#endif
    {
        if (pipe(fds) != 0)
            return -1;
        (void)fcntl(fds[0], F_SETFL, fcntl(fds[0], F_GETFL, 0) | O_NONBLOCK);
        (void)fcntl(fds[1], F_SETFL, fcntl(fds[1], F_GETFL, 0) | O_NONBLOCK);
        (void)fcntl(fds[0], F_SETFD, FD_CLOEXEC);
        (void)fcntl(fds[1], F_SETFD, FD_CLOEXEC);
    }
    g_ridux_monitor.fd = fds[0];
    g_ridux_monitor.spare_fd = fds[1];
    return g_ridux_monitor.fd;
}

static int ridux_monitor_fd(void) {
    if (g_ridux_monitor.fd >= 0)
        return g_ridux_monitor.fd;

    int fd = socket(AF_NETLINK, SOCK_RAW | SOCK_CLOEXEC | SOCK_NONBLOCK,
                    NETLINK_KOBJECT_UEVENT);
    if (fd >= 0) {
        struct sockaddr_nl sa;
        memset(&sa, 0, sizeof(sa));
        sa.nl_family = AF_NETLINK;
        sa.nl_pid = 0;
        sa.nl_groups = 1;
        (void)bind(fd, (const struct sockaddr *)&sa, sizeof(sa));
        g_ridux_monitor.fd = fd;
        return fd;
    }

    fd = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
    if (fd >= 0) {
        g_ridux_monitor.fd = fd;
        return fd;
    }

    return ridux_pipe_monitor_fd();
}

struct udev_monitor *udev_monitor_new_from_netlink(struct udev *udev,
                                                   const char *name) {
    (void)udev;
    if (name && strcmp(name, "udev") != 0) {
        errno = EINVAL;
        return NULL;
    }
    if (ridux_monitor_fd() < 0)
        return NULL;
    return (struct udev_monitor *)&g_ridux_monitor;
}

struct udev_monitor *udev_monitor_ref(struct udev_monitor *monitor) {
    return monitor ? monitor : (struct udev_monitor *)&g_ridux_monitor;
}

struct udev_monitor *udev_monitor_unref(struct udev_monitor *monitor) {
    (void)monitor;
    return NULL;
}

int udev_monitor_filter_add_match_subsystem_devtype(struct udev_monitor *monitor,
                                                    const char *subsystem,
                                                    const char *devtype) {
    (void)monitor;
    (void)subsystem;
    (void)devtype;
    return 0;
}

int udev_monitor_filter_add_match_tag(struct udev_monitor *monitor,
                                      const char *tag) {
    (void)monitor;
    (void)tag;
    return 0;
}

int udev_monitor_filter_remove(struct udev_monitor *monitor) {
    (void)monitor;
    return 0;
}

int udev_monitor_filter_update(struct udev_monitor *monitor) {
    (void)monitor;
    return 0;
}

int udev_monitor_enable_receiving(struct udev_monitor *monitor) {
    (void)monitor;
    return ridux_monitor_fd() >= 0 ? 0 : -1;
}

int udev_monitor_set_receive_buffer_size(struct udev_monitor *monitor,
                                         int size) {
    (void)monitor;
    (void)size;
    return 0;
}

int udev_monitor_get_fd(struct udev_monitor *monitor) {
    (void)monitor;
    return ridux_monitor_fd();
}

struct udev_device *udev_monitor_receive_device(struct udev_monitor *monitor) {
    (void)monitor;
    errno = EAGAIN;
    return NULL;
}

int libinput_udev_assign_seat(struct libinput *libinput, const char *seat_id) {
    typedef int (*assign_fn_t)(struct libinput *, const char *);
    assign_fn_t real_assign = (assign_fn_t)dlsym(RTLD_NEXT, "libinput_udev_assign_seat");
    int rc = real_assign ? real_assign(libinput, seat_id) : -1;
    if (rc != 0) {
        ridux_log_once(&g_ridux_logged_assign,
                       "[ridux-udev-shim] libinput_udev_assign_seat rescued\n");
        return 0;
    }
    return rc;
}

int libinput_get_fd(struct libinput *libinput) {
    typedef int (*get_fd_fn_t)(struct libinput *);
    get_fd_fn_t real_get_fd = (get_fd_fn_t)dlsym(RTLD_NEXT, "libinput_get_fd");
    int fd = real_get_fd ? real_get_fd(libinput) : -1;
    if (fd < 0) {
        ridux_log_once(&g_ridux_logged_fd,
                       "[ridux-udev-shim] libinput_get_fd using monitor fd\n");
        fd = ridux_monitor_fd();
    }
    return fd;
}

int libinput_dispatch(struct libinput *libinput) {
    typedef int (*dispatch_fn_t)(struct libinput *);
    dispatch_fn_t real_dispatch = (dispatch_fn_t)dlsym(RTLD_NEXT, "libinput_dispatch");
    int rc = real_dispatch ? real_dispatch(libinput) : 0;
    if (rc != 0) {
        ridux_log_once(&g_ridux_logged_dispatch,
                       "[ridux-udev-shim] libinput_dispatch rescued\n");
        return 0;
    }
    return rc;
}
