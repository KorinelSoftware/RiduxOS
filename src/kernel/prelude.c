/*
 * RiduxOS Unix kernel core.
 *
 * Este bloque tiene tipos/base + estado global del kernel.
 * El renderer Flush queda en src/flush.c para mantenerlo separado.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "assets.h"
#include "flush.h"
#include "compat/base.h"
#include "compat/memory_tasks.h"
#include "compat/linux_syscalls.h"
#include "compat/user_libc.h"
#include "compat/bsd_libc.h"
#include "compat/linux_abi.h"
#include "compat/display_wayland.h"
#include "compat/browser_runtime.h"
#include "ridux_r3wm.h"

/* Compile-time knobs */

#define MB2_BOOTLOADER_MAGIC 0x36d76289u
#define MB2_TAG_END          0u
#define MB2_TAG_MODULE       3u
#define MB2_TAG_FRAMEBUFFER  8u
#define MB2_TAG_ACPI_OLD     14u
#define MB2_TAG_ACPI_NEW     15u

#define FB_MAX_WIDTH  1920
#define FB_MAX_HEIGHT 1080

#define FLUSH_MAX_COMMANDS 16384

#define SHELL_MAX_LINES    48
#define SHELL_LINE_LEN     140
#define SHELL_HISTORY_MAX  16

#define VFS_MAX_FILES 4096
#define VFS_MAX_PATH  256
#define VFS_RW_MAX    65536
#define UI_FILE_BUF_MAX 8192

#define PROC_MAX 32

#define WINDOW_MAX        32
#define WINDOW_LINE_COUNT 32
#define WINDOW_LINE_LEN   120

#define APP_MAX 32

#define ELF_MAX_IMAGES    12
#define ELF_IMAGE_MEM_MAX 131072u
#define ELF_IDENT_NIDENT  16

#define PCI_MAX_DEVICES 32
#define DRIVER_MAX      32

#define CALC_DISPLAY_LEN 24
#define PAINT_GRID_W     28
#define PAINT_GRID_H     18
#define PAINT_PALETTE    10

#define NOTIF_MAX   6
#define NOTIF_TEXT  72

#define START_PINNED_MAX 18
#define SMP_MAX_CPUS     64

/* Window control button ids (bit mask). */
#define WCTL_CLOSE    0x1
#define WCTL_MAXIMIZE 0x2
#define WCTL_MINIMIZE 0x4

/* Types */

typedef struct {
    uint8_t *address;
    uint32_t pitch;
    uint32_t width;
    uint32_t height;
    uint8_t  bpp;
    uint8_t  red_pos, red_size;
    uint8_t  green_pos, green_size;
    uint8_t  blue_pos, blue_size;
    bool     ready;
} framebuffer_t;

typedef struct {
    int x, y, w, h;
} ui_rect_t;

typedef struct {
    uint32_t type;
    uint32_t size;
} mb2_tag_t;

typedef struct {
    uint32_t type;
    uint32_t size;
    uint64_t framebuffer_addr;
    uint32_t framebuffer_pitch;
    uint32_t framebuffer_width;
    uint32_t framebuffer_height;
    uint8_t  framebuffer_bpp;
    uint8_t  framebuffer_type;
    uint16_t reserved;
    uint8_t  red_field_position, red_mask_size;
    uint8_t  green_field_position, green_mask_size;
    uint8_t  blue_field_position, blue_mask_size;
} __attribute__((packed)) mb2_tag_framebuffer_t;

typedef struct {
    uint32_t type;
    uint32_t size;
    uint32_t mod_start;
    uint32_t mod_end;
    char     string[1];
} __attribute__((packed)) mb2_tag_module_t;

typedef struct {
    uint32_t type;
    uint32_t size;
    uint8_t  rsdp[1];
} __attribute__((packed)) mb2_tag_acpi_t;

typedef struct {
    char     signature[8];
    uint8_t  checksum;
    char     oem_id[6];
    uint8_t  revision;
    uint32_t rsdt_address;
    uint32_t length;
    uint64_t xsdt_address;
    uint8_t  extended_checksum;
    uint8_t  reserved[3];
} __attribute__((packed)) acpi_rsdp_t;

typedef struct {
    char     signature[4];
    uint32_t length;
    uint8_t  revision;
    uint8_t  checksum;
    char     oem_id[6];
    char     oem_table_id[8];
    uint32_t oem_revision;
    uint32_t creator_id;
    uint32_t creator_revision;
} __attribute__((packed)) acpi_sdt_header_t;

typedef struct {
    acpi_sdt_header_t h;
    uint32_t lapic_addr;
    uint32_t flags;
} __attribute__((packed)) acpi_madt_t;

typedef struct {
    char name[100];
    char mode[8];
    char uid[8];
    char gid[8];
    char size[12];
    char mtime[12];
    char checksum[8];
    char typeflag;
    char linkname[100];
    char magic[6];
    char version[2];
    char uname[32];
    char gname[32];
    char devmajor[8];
    char devminor[8];
    char prefix[155];
    char padding[12];
} __attribute__((packed)) tar_header_t;

typedef struct {
    bool           used;
    bool           writable;
    char           path[VFS_MAX_PATH];
    const uint8_t *ro_data;
    uint32_t       ro_size;
    char           rw_data[VFS_RW_MAX];
    uint32_t       rw_size;
} vfs_file_t;

typedef enum {
    PROC_UNUSED = 0,
    PROC_READY,
    PROC_RUNNING,
    PROC_SLEEP
} proc_state_t;

typedef struct {
    bool         used;
    int          pid;
    char         name[24];
    proc_state_t state;
    uint32_t     cpu_ticks;
    uint32_t     work_units;
    bool         is_user;
    int          elf_slot;
    uint32_t     user_pc;
} process_t;

typedef struct {
    unsigned char e_ident[ELF_IDENT_NIDENT];
    uint16_t      e_type;
    uint16_t      e_machine;
    uint32_t      e_version;
    uint32_t      e_entry;
    uint32_t      e_phoff;
    uint32_t      e_shoff;
    uint32_t      e_flags;
    uint16_t      e_ehsize;
    uint16_t      e_phentsize;
    uint16_t      e_phnum;
    uint16_t      e_shentsize;
    uint16_t      e_shnum;
    uint16_t      e_shstrndx;
} __attribute__((packed)) elf32_ehdr_t;

typedef struct {
    uint32_t p_type;
    uint32_t p_offset;
    uint32_t p_vaddr;
    uint32_t p_paddr;
    uint32_t p_filesz;
    uint32_t p_memsz;
    uint32_t p_flags;
    uint32_t p_align;
} __attribute__((packed)) elf32_phdr_t;

typedef struct {
    bool     used;
    char     path[VFS_MAX_PATH];
    uint32_t base_vaddr;
    uint32_t entry_vaddr;
    uint32_t mem_size;
    uint32_t loaded_segments;
    uint8_t  memory[ELF_IMAGE_MEM_MAX];
} elf_image_t;

/* El WM todavia conserva widgets internos como respaldo, pero el launcher
 * ahora prefiere ELFs Ring 3. Si algo rompe, la UI vieja queda cerca para
 * debuggear en vez de dejarnos mirando una pantalla negra. */
struct window_s;
struct app_s;

typedef void (*app_draw_fn)(struct window_s *win, ui_rect_t body);
typedef void (*app_click_fn)(struct window_s *win, ui_rect_t body, int mx, int my, int button);
typedef void (*app_key_fn)(struct window_s *win, char ch, uint8_t scancode);
typedef void (*app_tick_fn)(struct window_s *win);

typedef struct window_s {
    bool     used;
    int      id;
    int      app_id;
    bool     visible;
    bool     minimized;
    bool     maximized;
    int      x, y, w, h;
    int      saved_x, saved_y, saved_w, saved_h;
    uint8_t  alpha;
    uint32_t flags;
    char     title[40];
    char     lines[WINDOW_LINE_COUNT][WINDOW_LINE_LEN];
    /* App-state for interactive apps. */
    int      state_i[64];
    uint32_t state_u[64];
    char     state_s[8][96];
} window_t;

typedef struct app_s {
    int           app_id;
    const char   *name;
    const char   *category;
    int           icon_id;
    int           window_id;    /* primary window instance */
    bool          pinned_start;
    bool          pinned_task;
    app_draw_fn   draw;
    app_click_fn  click;
    app_key_fn    key;
    app_tick_fn   tick;
    int           default_w;
    int           default_h;
} app_t;

/* Theme: Windows 11 style light/dark accents. */
typedef struct {
    const char *name;
    uint32_t    wallpaper_tint_a;
    uint32_t    wallpaper_tint_b;
    uint32_t    accent;          /* primary accent */
    uint32_t    accent_hot;      /* hover */
    uint32_t    text;            /* foreground */
    uint32_t    text_muted;
    uint32_t    bg;              /* window body tint */
    uint32_t    bg_alt;          /* window gradient partner */
    uint32_t    glass;           /* white overlay for acrylic sheen */
    uint32_t    stroke;          /* 1px strokes */
    uint32_t    taskbar;         /* taskbar solid tint */
    uint32_t    start_bg;        /* start menu tint */
    uint32_t    danger;          /* close hover */
    uint32_t    success;
    uint8_t     dark;            /* 1 if dark theme */
} theme_t;

typedef struct {
    int      margin;
    int      gap;
    int      radius;
    int      line_h;
    int      title_h;
    int      window_pad;
    int      taskbar_h;
    int      taskbar_icon;
    int      taskbar_gap;
    ui_rect_t screen;
    ui_rect_t desktop;
    ui_rect_t taskbar;
    ui_rect_t start_btn;
    ui_rect_t tray;
    ui_rect_t clock_btn;
    ui_rect_t quick_btn;
} ui_layout_t;

/* Driver registry (pluggable). */
typedef enum {
    DRV_KIND_GENERIC = 0,
    DRV_KIND_INPUT,
    DRV_KIND_DISPLAY,
    DRV_KIND_TIMER,
    DRV_KIND_CLOCK,
    DRV_KIND_SERIAL,
    DRV_KIND_BUS,
    DRV_KIND_STORAGE,
    DRV_KIND_SOUND,
    DRV_KIND_NET
} driver_kind_t;

typedef struct {
    const char  *name;
    const char  *vendor;
    driver_kind_t kind;
    bool       (*probe)(void);
    void       (*init)(void);
    bool         present;
    bool         ready;
    uint32_t     flags;
} driver_t;

/* PCI device record. */
typedef struct {
    uint8_t  bus, slot, func;
    uint16_t vendor_id;
    uint16_t device_id;
    uint8_t  class_code;
    uint8_t  subclass;
    uint8_t  prog_if;
    uint8_t  revision;
} pci_device_t;

/* Real-time clock reading. */
typedef struct {
    uint16_t year;
    uint8_t  month;
    uint8_t  day;
    uint8_t  hour;
    uint8_t  minute;
    uint8_t  second;
    uint8_t  dow;
} rtc_time_t;

/* Notification. */
typedef struct {
    bool     used;
    char     text[NOTIF_TEXT];
    uint32_t when_tick;
    int      icon_id;
} notif_t;

typedef struct {
    bool     present;
    bool     online;
    bool     bsp;
    uint32_t logical_id;
    uint32_t apic_id;
    uint32_t package_id;
    uint32_t core_id;
    uint32_t smt_id;
} cpu_topology_t;

/* Globals */

framebuffer_t g_fb;


bool      g_use_backbuffer;
bool      g_fb_fast_bgra;    /* true iff FB is 32bpp 0x00RRGGBB (QEMU/VBox common) */
uint32_t  g_backbuffer[FB_MAX_WIDTH * FB_MAX_HEIGHT];
static uint32_t  g_bg_cache[FB_MAX_WIDTH * FB_MAX_HEIGHT];
static int       g_bg_cache_theme = -1;
static uint32_t  g_bg_cache_w = 0, g_bg_cache_h = 0;
static uint32_t  g_frame_counter;
static uint32_t  g_last_render_tsc;
static uint32_t  g_frame_period_tsc = 33000000u; /* ~33ms at 1 GHz, clamps fps */
static uint32_t  g_cpu_logical_count = 1;
static uint32_t  g_cpu_online_count = 1;
static uint32_t  g_cpu_core_count = 1;
static uint32_t  g_cpu_threads_per_core = 1;
static uint32_t  g_bsp_apic_id;
static uint64_t  g_lapic_base;
static bool      g_lapic_present;
static bool      g_x2apic_present;
static bool      g_smp_topology_detected;
static cpu_topology_t g_cpu_topology[SMP_MAX_CPUS];
static const uint8_t *g_acpi_rsdp;
static uint32_t  g_acpi_rsdp_size;
static bool      g_madt_detected;
static uint32_t  g_madt_lapic_addr;
static uint32_t  g_madt_cpu_count;
static uint32_t  g_madt_ioapic_count;
static bool      g_gpu_accel_enabled;
static bool      g_gpu_hw_present;
static const char *g_gpu_accel_kind = "software fallback";


static char   g_shell_lines[SHELL_MAX_LINES][SHELL_LINE_LEN];
static size_t g_shell_count;
static char   g_shell_input[SHELL_LINE_LEN];
static size_t g_shell_input_len;
static char   g_shell_history[SHELL_HISTORY_MAX][SHELL_LINE_LEN];
static int    g_shell_history_count;
static int    g_shell_history_cursor;

static bool     g_shift_down;
static bool     g_ctrl_down;
static bool     g_extended_scancode;
static uint64_t g_keyboard_accept_after_tick;
static bool     g_wm_drag_mode;
static bool     g_arrow_hint_shown;
static uint32_t g_theme_index;
static bool     g_needs_redraw;
/* Set by mouse motion handlers to request a cheap cursor-only repaint.
 * Distinct from g_needs_redraw which forces a full scene composite. */
static bool     g_cursor_moved;
static bool     g_start_open;
static bool     g_quick_open;
static bool     g_notif_open;

static const uint8_t *g_initrd_start;
static const uint8_t *g_initrd_end;
static const uint8_t *g_initrd_overlay_start;
static const uint8_t *g_initrd_overlay_end;
extern uint8_t __kernel_end[];

static vfs_file_t g_vfs_files[VFS_MAX_FILES];
static size_t     g_vfs_count;
static uint32_t   g_vfs_mount_dropped_slots;
static uint32_t   g_vfs_mount_dropped_paths;

static process_t g_processes[PROC_MAX];
static int       g_proc_current;
static int       g_next_pid;
static bool      g_irq0_preempt_request;

static window_t g_windows[WINDOW_MAX];
static int      g_window_count;
static int      g_window_focus;
static int      g_next_window_id;

static ui_layout_t g_ui;

static app_t g_apps[APP_MAX];
static int   g_app_count;
static uint32_t g_app_launch_count[APP_MAX];

static elf_image_t g_elf_images[ELF_MAX_IMAGES];

static bool g_browser_vm_requested;
static char g_browser_vm_engine[24] = "chromium";
static char g_browser_vm_status[96] = "backend listo para conectar a Linux VM";

static bool     g_mouse_initialized;
static int      g_mouse_x, g_mouse_y;
static int      g_mouse_prev_x, g_mouse_prev_y;
static uint8_t  g_mouse_packet[3];
static int      g_mouse_packet_index;
static bool     g_mouse_left_down;
static bool     g_mouse_right_down;
static bool     g_mouse_left_edge; /* 1-tick rising edge */
static bool     g_mouse_dragging;
static int      g_mouse_drag_window_id;
static int      g_mouse_drag_offset_x;
static int      g_mouse_drag_offset_y;

static driver_t      g_drivers[DRIVER_MAX];
static int           g_driver_count;

static pci_device_t  g_pci_devices[PCI_MAX_DEVICES];
static int           g_pci_device_count;

static uint32_t g_uptime_ticks;
static uint32_t g_pit_hz = 100;
static bool     g_serial_ready;
static bool     g_boot_serial_runtime_quiet;
static rtc_time_t g_rtc_now;

static notif_t g_notifs[NOTIF_MAX];

static const theme_t g_theme_table[] = {
    /* Windows 11 Bloom Dark */
    {
        "Bloom Dark",
        0x00102030, 0x00051525,
        0x0060CDFF, 0x0090DFFF,
        0x00F5FAFF, 0x00B7CBE0,
        0x001B2638, 0x00101928,
        0x00FFFFFF, 0x003E5878,
        0x000E1626, 0x001A2540,
        0x00E95555, 0x005BC98F,
        1
    },
    /* Windows 11 Glow */
    {
        "Glow",
        0x00201A35, 0x00401E55,
        0x00BC8CFF, 0x00D9B0FF,
        0x00F7F3FF, 0x00CFC2DF,
        0x00231A35, 0x00181127,
        0x00FFFFFF, 0x005C4C7F,
        0x00161022, 0x00221A38,
        0x00FF6B6B, 0x009ED89F,
        1
    },
    /* Windows 11 Light */
    {
        "Light",
        0x00E4EEFF, 0x00CAD9F5,
        0x000078D4, 0x00106EBE,
        0x00101820, 0x00505C70,
        0x00F3F6FC, 0x00E8ECF5,
        0x00FFFFFF, 0x00A8BBDC,
        0x00D8E1F0, 0x00EEF3FA,
        0x00E81123, 0x00107C10,
        0
    },
    /* Ridux custom */
    {
        "Ridux",
        0x0012223A, 0x00083060,
        0x0070D6FF, 0x00A8E3FF,
        0x00F4F9FE, 0x00B9CCDF,
        0x00152236, 0x000B1626,
        0x00FFFFFF, 0x004E6B8E,
        0x000A1324, 0x00162340,
        0x00FF6B6B, 0x0062DE95,
        1
    }
};
#define THEME_COUNT ((int)(sizeof(g_theme_table) / sizeof(g_theme_table[0])))
