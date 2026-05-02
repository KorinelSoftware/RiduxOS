/*
 * libridux.h - Userspace SDK for the Ridux R3 WM protocol.
 *
 * Freestanding (no libc): everything is implemented in tools/user_libridux.c
 * with inline-asm syscall wrappers. Linked into ELF64 Ring 3 apps that run
 * as CPL=3 tasks under RiduxOS.
 *
 * Wire format constants come from src/ridux_r3wm.h (kept identical between
 * kernel and userspace builds). For convenience, this header re-exposes
 * them so user apps don't need to include the kernel header.
 */
#ifndef RIDUX_LIBRIDUX_H
#define RIDUX_LIBRIDUX_H

typedef unsigned char       rd_u8;
typedef unsigned short      rd_u16;
typedef unsigned int        rd_u32;
typedef unsigned long long  rd_u64;
typedef int                 rd_i32;
typedef long long           rd_i64;
typedef unsigned long       rd_size;

/* Mirror of src/ridux_r3wm.h. Keep in sync. */
#define RIDUX_SYS_WINDOW_OPEN     500
#define RIDUX_SYS_WINDOW_PRESENT  501
#define RIDUX_SYS_WINDOW_POLL     502
#define RIDUX_SYS_WINDOW_CLOSE    503
#define RIDUX_SYS_SHELL_EXEC      504
#define RIDUX_SYS_DESKTOP_STATE   505

#define RIDUX_DESKTOP_APP_MAX 24

#define RIDUX_WIN_FLAG_BORDERLESS  0x00000001u
#define RIDUX_WIN_FLAG_DESKTOP     0x00000002u
#define RIDUX_WIN_FLAG_NO_FOCUS    0x00000004u
#define RIDUX_WIN_FLAG_NO_TASKBAR  0x00000008u

#define RIDUX_EVENT_NONE        0
#define RIDUX_EVENT_MOUSE_MOVE  1
#define RIDUX_EVENT_MOUSE_DOWN  2
#define RIDUX_EVENT_MOUSE_UP    3
#define RIDUX_EVENT_KEY_DOWN    4
#define RIDUX_EVENT_KEY_UP      5
#define RIDUX_EVENT_CLOSE       6
#define RIDUX_EVENT_FOCUS_IN    7
#define RIDUX_EVENT_FOCUS_OUT   8

typedef struct rd_event {
    rd_u32 type;
    rd_i32 x, y;
    rd_u32 button;
    rd_u32 key;
    rd_u32 scancode;
    rd_u32 reserved;
} rd_event_t;

typedef struct rd_window {
    rd_i32  wid;
    rd_u32  width;
    rd_u32  height;
    rd_u32  stride;     /* bytes per row */
    rd_u32 *fb;         /* user-space pointer to the framebuffer */
} rd_window_t;

typedef struct rd_desktop_app_state {
    char     name[24];
    rd_u32   icon;
    rd_u32   running;
    rd_u32   focused;
} rd_desktop_app_state_t;

typedef struct rd_desktop_state {
    rd_u32 width, height;
    rd_i32 mouse_x, mouse_y;
    rd_u32 start_open;
    rd_u32 quick_open;
    rd_u32 hour, minute;
    rd_u32 day, month, year;
    rd_u32 app_count;
    rd_desktop_app_state_t apps[RIDUX_DESKTOP_APP_MAX];
} rd_desktop_state_t;

/* Returns 0 on success, negative errno on failure. Fills *out. */
int  rd_window_open(const char *title, rd_u32 w, rd_u32 h, rd_window_t *out);
int  rd_window_open_flags(const char *title, rd_u32 w, rd_u32 h,
                          rd_u32 flags, rd_window_t *out);
int  rd_window_present(rd_window_t *win, int x, int y, int w, int h);
int  rd_window_poll(rd_window_t *win, rd_event_t *ev, int max_events);
void rd_window_close(rd_window_t *win);
int  rd_shell_exec(const char *command, char *out, rd_u32 out_len);
int  rd_desktop_state(rd_desktop_state_t *out);

/* Drawing primitives (clip-safe, operate on win->fb directly). */
void rd_clear(rd_window_t *win, rd_u32 color);
void rd_fill_rect(rd_window_t *win, int x, int y, int w, int h, rd_u32 color);
void rd_pixel(rd_window_t *win, int x, int y, rd_u32 color);
void rd_text(rd_window_t *win, int x, int y, const char *s, rd_u32 color);
void rd_text_scaled(rd_window_t *win, int x, int y, int scale,
                    const char *s, rd_u32 color);

/* Color helpers. Format is XRGB8888 (R<<16 | G<<8 | B). */
static inline rd_u32 rd_rgb(rd_u8 r, rd_u8 g, rd_u8 b) {
    return ((rd_u32)r << 16) | ((rd_u32)g << 8) | (rd_u32)b;
}

/* Misc syscalls used by apps for logging/sleep. */
rd_i64 rd_write(int fd, const void *buf, rd_size n);
void   rd_exit(int code) __attribute__((noreturn));
void   rd_sleep_ticks(rd_u32 ticks);  /* approx via busy-loop syscalls */

/* ---- Inline syscall trampoline ---- */
static inline rd_i64 rd_syscall(rd_u64 nr, rd_u64 a0, rd_u64 a1, rd_u64 a2,
                                rd_u64 a3, rd_u64 a4, rd_u64 a5) {
    rd_i64 ret;
    register rd_u64 r10 __asm__("r10") = a3;
    register rd_u64 r8  __asm__("r8")  = a4;
    register rd_u64 r9  __asm__("r9")  = a5;
    __asm__ __volatile__(
        "syscall"
        : "=a"(ret)
        : "0"(nr), "D"(a0), "S"(a1), "d"(a2), "r"(r10), "r"(r8), "r"(r9)
        : "rcx", "r11", "memory");
    return ret;
}

#endif /* RIDUX_LIBRIDUX_H */
