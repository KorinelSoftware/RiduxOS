/*
 * ridux_r3wm.h - RiduxOS native Ring 3 window manager protocol.
 *
 * Lets a CPL=3 ELF open a window managed by the in-kernel Ridux WM,
 * write into a shared framebuffer mapped into its address space, and
 * receive routed mouse/keyboard events. This is a Ridux-specific
 * protocol (not Linux ABI compatible) using free syscall slots in
 * the 500-510 range.
 *
 * Wire format is shared between the kernel implementation
 * (src/ridux_r3wm.c) and the userspace SDK
 * (ridux-ui/include/libridux.h, ridux-ui/src/libridux.c). Keep these
 * structs ABI-stable: any change must be mirrored in libridux.h.
 */
#ifndef RIDUX_R3WM_H
#define RIDUX_R3WM_H

#include <stdint.h>

/* --- Syscall numbers (free slots well below SYSCALL_MAX=512) --- */
#define RIDUX_SYS_WINDOW_OPEN     500
#define RIDUX_SYS_WINDOW_PRESENT  501
#define RIDUX_SYS_WINDOW_POLL     502
#define RIDUX_SYS_WINDOW_CLOSE    503
#define RIDUX_SYS_SHELL_EXEC      504
#define RIDUX_SYS_DESKTOP_STATE   505

/* --- Window geometry limits --- */
#define RIDUX_WIN_MIN_W   64
#define RIDUX_WIN_MIN_H   48
#define RIDUX_WIN_MAX_W   1920
#define RIDUX_WIN_MAX_H   1080
#define RIDUX_WIN_MAX     16
#define RIDUX_DESKTOP_APP_MAX 24

/* Window hints. Normal app windows keep the classic Ridux chrome.
 * Desktop/shell surfaces can ask for a quiet borderless layer instead. */
#define RIDUX_WIN_FLAG_BORDERLESS  0x00000001u
#define RIDUX_WIN_FLAG_DESKTOP     0x00000002u
#define RIDUX_WIN_FLAG_NO_FOCUS    0x00000004u
#define RIDUX_WIN_FLAG_NO_TASKBAR  0x00000008u

/* --- Event ring sizing --- */
#define RIDUX_EVENT_RING_SIZE 64   /* power of two */

/* --- Event types --- */
#define RIDUX_EVENT_NONE        0
#define RIDUX_EVENT_MOUSE_MOVE  1
#define RIDUX_EVENT_MOUSE_DOWN  2
#define RIDUX_EVENT_MOUSE_UP    3
#define RIDUX_EVENT_KEY_DOWN    4
#define RIDUX_EVENT_KEY_UP      5
#define RIDUX_EVENT_CLOSE       6
#define RIDUX_EVENT_FOCUS_IN    7
#define RIDUX_EVENT_FOCUS_OUT   8

/* Wire-format event. Coordinates are window-local (origin at the
 * top-left of the window's body, NOT including the title bar). */
typedef struct ridux_event {
    uint32_t type;        /* RIDUX_EVENT_* */
    int32_t  x, y;        /* window-local cursor coords */
    uint32_t button;      /* 0=left, 1=right, 2=middle (for mouse evts) */
    uint32_t key;         /* ASCII char if printable, else 0 (for key evts) */
    uint32_t scancode;    /* PS/2 scancode for key evts */
    uint32_t reserved;
} ridux_event_t;

/* --- Window-open argument layout ---
 *
 * Userspace passes a pointer to this struct as a4 of the syscall.
 * On success the kernel fills .wid and .fb_user_va.
 * .stride is in pixels (4 bytes per pixel, XRGB8888). */
typedef struct ridux_window_open_args {
    const char *title;        /* NUL-terminated, length <=63 */
    uint32_t    width;
    uint32_t    height;
    /* Output fields filled by the kernel */
    int32_t     wid;          /* >=0 on success */
    uint64_t    fb_user_va;   /* user-space pointer to RGBA framebuffer */
    uint32_t    stride;       /* bytes per row (== width*4 today) */
    uint32_t    flags;
} ridux_window_open_args_t;

typedef struct ridux_desktop_app_state {
    char     name[24];
    uint32_t icon;
    uint32_t running;
    uint32_t focused;
} ridux_desktop_app_state_t;

typedef struct ridux_desktop_state {
    uint32_t width, height;
    int32_t  mouse_x, mouse_y;
    uint32_t start_open;
    uint32_t quick_open;
    uint32_t hour, minute;
    uint32_t day, month, year;
    uint32_t app_count;
    ridux_desktop_app_state_t apps[RIDUX_DESKTOP_APP_MAX];
} ridux_desktop_state_t;

#endif /* RIDUX_R3WM_H */
