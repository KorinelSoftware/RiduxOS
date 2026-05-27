/* Multiboot parse + kernel_main */

/* Read low 32 bits of the CPU timestamp counter. Monotonic, wraps every
 * ~few seconds on modern CPUs but wrap-safe with unsigned subtract. */
static inline uint32_t rdtsc32(void) {
    uint32_t lo, hi;
    __asm__ __volatile__("rdtsc" : "=a"(lo), "=d"(hi));
    (void)hi;
    return lo;
}

/* Calibrate frame period by sleeping via cpu pause for a short busy loop
 * and measuring TSC delta. We aim for ~60 FPS (16 ms). On a 1 GHz CPU
 * that's 16M cycles; on a 3 GHz CPU, 48M. The loop counts cycles so this
 * auto-adjusts. Done once at boot. */
static void calibrate_frame_period(void) {
    uint32_t t0, t1;
    volatile uint32_t spin;
    /* Spin ~2M iterations as a coarse 'unit'. */
    t0 = rdtsc32();
    for (spin = 0; spin < 2000000u; ++spin) { __asm__ __volatile__("" ::: "memory"); }
    t1 = rdtsc32();
    /* delta ~= cycles for 2M iters. We want 16ms worth. On a 1 GHz CPU
     * 2M iters ~= 2-4ms (roughly). Extrapolate to 16ms = 8x that delta. */
    {
        uint32_t delta = t1 - t0;
        if (delta < 100000u) delta = 100000u; /* sanity floor */
        g_frame_period_tsc = delta * 4u; /* ~60fps target */
        if (g_frame_period_tsc < 5000000u) g_frame_period_tsc = 5000000u;
        if (g_frame_period_tsc > 100000000u) g_frame_period_tsc = 100000000u;
    }
}

static const char *kernel_native_shell_path(void) {
    const uint8_t *data = NULL;
    uint32_t size = 0;

    if (vfs_read("/bin/desktop-shell-r3.elf", &data, &size)) return "/bin/desktop-shell-r3.elf";
    return NULL;
}

static const char *kernel_gl_compositor_path(void) {
    const uint8_t *data = NULL;
    uint32_t size = 0;

    if (!kvfs_exists("/etc/ridux-gl-compositor.enable")) return NULL;
    if (vfs_read("/usr/bin/ridux-gl-compositor", &data, &size)) return "/usr/bin/ridux-gl-compositor";
    return NULL;
}

static const char *kernel_riduxui_shell_path(void) {
    const uint8_t *data = NULL;
    uint32_t size = 0;

    if (!kvfs_exists("/etc/ridux-ui-shell.enable")) return NULL;
    if (vfs_read("/usr/bin/injury-compositor", &data, &size)) return "/usr/bin/injury-compositor";
    if (vfs_read("/usr/bin/ridux-ui-shell", &data, &size)) return "/usr/bin/ridux-ui-shell";
    return NULL;
}

static const char *kernel_wayfire_shell_path(void) {
    const uint8_t *data = NULL;
    uint32_t size = 0;

    if (!kvfs_exists("/etc/ridux-wayfire-primary.enable")) return NULL;
    if (vfs_read("/opt/wayfire/bin/wayfire", &data, &size)) return "/opt/wayfire/bin/wayfire";
    if (vfs_read("/usr/bin/wayfire", &data, &size)) return "/usr/bin/wayfire";
    return NULL;
}

static const char *kernel_hyprland_shell_path(void) {
    const uint8_t *data = NULL;
    uint32_t size = 0;

    if (!kvfs_exists("/etc/ridux-hyprland-primary.enable")) return NULL;
    /*
     * RiduxOS is the session supervisor here. The upstream start-hyprland
     * watchdog expects full Linux process/session semantics and can exit before
     * Hyprland publishes Wayland/IPC sockets on our ABI layer, leaving a black
     * desktop. Launch the compositor directly and keep start-hyprland available
     * for later once the remaining supervisor ABI is complete.
     */
    if (vfs_read("/opt/hyprland/bin/Hyprland", &data, &size)) return "/opt/hyprland/bin/Hyprland";
    if (vfs_read("/opt/hyprland/usr/bin/Hyprland", &data, &size)) return "/opt/hyprland/usr/bin/Hyprland";
    if (vfs_read("/usr/bin/Hyprland", &data, &size)) return "/usr/bin/Hyprland";
    if (vfs_read("/usr/bin/hyprland", &data, &size)) return "/usr/bin/hyprland";
    if (vfs_read("/usr/bin/start-hyprland", &data, &size)) return "/usr/bin/start-hyprland";
    if (vfs_read("/opt/hyprland/bin/start-hyprland", &data, &size)) return "/opt/hyprland/bin/start-hyprland";
    return NULL;
}

static const char *kernel_hyprland_dbus_session_path(void) {
    const uint8_t *data = NULL;
    uint32_t size = 0;

    if (!kvfs_exists("/etc/ridux-hyprland-primary.enable")) return NULL;
    if (vfs_read("/usr/bin/ridux-dbus-session", &data, &size)) return "/usr/bin/ridux-dbus-session";
    return NULL;
}

static const char *kernel_hyprland_dbus_system_path(void) {
    const uint8_t *data = NULL;
    uint32_t size = 0;

    if (!kvfs_exists("/etc/ridux-hyprland-primary.enable")) return NULL;
    if (!kvfs_exists("/etc/ridux-dbus-system-real.enable")) return NULL;
    if (vfs_read("/usr/bin/ridux-dbus-system", &data, &size)) return "/usr/bin/ridux-dbus-system";
    return NULL;
}

static const char *kernel_vulkan_probe_path(void) {
    const uint8_t *data = NULL;
    uint32_t size = 0;

    if (!kvfs_exists("/etc/ridux-vulkan-autoprobe.enable")) return NULL;
    if (vfs_read("/usr/bin/ridux-vulkan-probe", &data, &size)) return "/usr/bin/ridux-vulkan-probe";
    return NULL;
}

static const char *kernel_qt_shell_path(void) {
    const uint8_t *data = NULL;
    uint32_t size = 0;

    if (!kvfs_exists("/etc/ridux-qt-shell.enable")) return NULL;
    if (vfs_read("/usr/bin/ridux-shell-direct", &data, &size)) return "/usr/bin/ridux-shell-direct";
    if (vfs_read("/usr/bin/ridux-shell", &data, &size)) return "/usr/bin/ridux-shell";
    return NULL;
}

static void parse_multiboot_info(uint32_t mbi_addr) {
    uint8_t *ptr;
    uint32_t total_size;

    g_fb.ready = false;
    g_initrd_start = NULL;
    g_initrd_end = NULL;
    g_initrd_overlay_start = NULL;
    g_initrd_overlay_end = NULL;
    g_phys_mem_top = 0;
    pmm_clear_usable_ranges();
    total_size = *((uint32_t *)(uintptr_t)mbi_addr);
    ptr = (uint8_t *)(uintptr_t)(mbi_addr + 8u);

    while (ptr < (uint8_t *)(uintptr_t)(mbi_addr + total_size)) {
        mb2_tag_t *tag = (mb2_tag_t *)ptr;
        if (tag->type == MB2_TAG_END) break;
        if (tag->type == MB2_TAG_FRAMEBUFFER) {
            const mb2_tag_framebuffer_t *f = (const mb2_tag_framebuffer_t *)tag;
            if (f->framebuffer_type == 1 && f->framebuffer_bpp >= 24) {
                g_fb.address = (uint8_t *)(uintptr_t)f->framebuffer_addr;
                g_fb.pitch = f->framebuffer_pitch;
                g_fb.width = f->framebuffer_width;
                g_fb.height = f->framebuffer_height;
                g_fb.bpp = f->framebuffer_bpp;
                g_fb.red_pos = f->red_field_position;
                g_fb.red_size = f->red_mask_size;
                g_fb.green_pos = f->green_field_position;
                g_fb.green_size = f->green_mask_size;
                g_fb.blue_pos = f->blue_field_position;
                g_fb.blue_size = f->blue_mask_size;
                g_fb.ready = true;
            }
        } else if (tag->type == MB2_TAG_MODULE) {
            const mb2_tag_module_t *m = (const mb2_tag_module_t *)tag;
            if (g_initrd_start == NULL) {
                g_initrd_start = (const uint8_t *)(uintptr_t)m->mod_start;
                g_initrd_end   = (const uint8_t *)(uintptr_t)m->mod_end;
            } else if (g_initrd_overlay_start == NULL) {
                g_initrd_overlay_start = (const uint8_t *)(uintptr_t)m->mod_start;
                g_initrd_overlay_end   = (const uint8_t *)(uintptr_t)m->mod_end;
            }
        } else if (tag->type == MB2_TAG_MMAP) {
            const mb2_tag_mmap_t *mm = (const mb2_tag_mmap_t *)tag;
            const uint8_t *ep = (const uint8_t *)mm->entries;
            const uint8_t *end = ((const uint8_t *)tag) + tag->size;
            if (mm->entry_size >= sizeof(mb2_mmap_entry_t)) {
                while (ep + sizeof(mb2_mmap_entry_t) <= end) {
                    const mb2_mmap_entry_t *e = (const mb2_mmap_entry_t *)ep;
                    if (e->type == 1u && e->len && e->addr + e->len > e->addr) {
                        uint64_t top = e->addr + e->len;
                        if (top > g_phys_mem_top) g_phys_mem_top = top;
                        pmm_add_usable_range(e->addr, e->len);
                    }
                    ep += mm->entry_size;
                }
            }
        } else if ((tag->type == MB2_TAG_ACPI_OLD || tag->type == MB2_TAG_ACPI_NEW) &&
                   g_acpi_rsdp == NULL && tag->size > 8u) {
            const mb2_tag_acpi_t *a = (const mb2_tag_acpi_t *)tag;
            g_acpi_rsdp = a->rsdp;
            g_acpi_rsdp_size = tag->size - 8u;
        }
        if (tag->size < 8u) break;
        ptr += (tag->size + 7u) & ~7u;
    }
}

static uint64_t kernel_phys_reserve_end(void){
    uint64_t end=(uint64_t)(uintptr_t)__kernel_end;
    if(g_initrd_end&&(uint64_t)(uintptr_t)g_initrd_end>end){
        end=(uint64_t)(uintptr_t)g_initrd_end;
    }
    if(g_initrd_overlay_end&&(uint64_t)(uintptr_t)g_initrd_overlay_end>end){
        end=(uint64_t)(uintptr_t)g_initrd_overlay_end;
    }
    return(end+PAGE_SIZE-1ULL)&~(PAGE_SIZE-1ULL);
}

/* Hypervisors can expose a linear framebuffer outside the pages that boot64
 * actually identity-mapped. Add missing kernel mappings once compat2 paging is
 * online, regardless of whether the address is below or above 4 GiB. */
static void map_high_framebuffer_if_needed(void){
    address_space_t *kas;
    uint64_t fb_phys,fb_size,start,end,va;
    if(!g_fb.ready)return;
    fb_phys=(uint64_t)(uintptr_t)g_fb.address;
    fb_size=(uint64_t)g_fb.pitch*(uint64_t)g_fb.height;
    if(fb_size==0)fb_size=(uint64_t)g_fb.width*(uint64_t)g_fb.height*4ULL;
    kas=paging_get_kernel_space();
    if(!kas||!kas->pml4)return;
    start=fb_phys&~(PAGE_SIZE-1ULL);
    end=(fb_phys+fb_size+PAGE_SIZE-1ULL)&~(PAGE_SIZE-1ULL);
    for(va=start;va<end;va+=PAGE_SIZE){
        if(paging_get_entry(kas,va)&PAGE_PRESENT)continue;
        if(!paging_map(kas,va,va,PAGE_PRESENT|PAGE_WRITABLE)){
            panic("framebuffer map","no se pudo mapear framebuffer alto");
            return;
        }
    }
}

/* (Definitions of the boot-diag serial helpers. Forward-declared
 * near the top of this translation unit so callers like
 * drivers_bootstrap() earlier in the file can use them.) */
void __boot_serial_init_direct(void) {
    outb(SERIAL_COM1 + 1, 0x00);    /* disable interrupts */
    outb(SERIAL_COM1 + 3, 0x80);    /* DLAB = 1 */
    outb(SERIAL_COM1 + 0, 0x03);    /* divisor low  (38400 baud) */
    outb(SERIAL_COM1 + 1, 0x00);    /* divisor high */
    outb(SERIAL_COM1 + 3, 0x03);    /* 8N1, DLAB = 0 */
    outb(SERIAL_COM1 + 2, 0xC7);    /* FIFO enable, clear */
    outb(SERIAL_COM1 + 4, 0x0B);    /* DTR/RTS/OUT2 (no loopback) */
}
static void boot_serial_putc_raw(char c) {
    uint32_t spin;
    /* Defensive: ensure MCR is NOT in loopback mode. serial_probe()
     * leaves loopback enabled when it fails early, which would silently
     * route our diagnostic writes back into RBR (invisible on host). */
    outb(SERIAL_COM1 + 4, 0x0B);
    if (c == '\n') {
        for (spin = 0; spin < 100000u; ++spin)
            if ((inb(SERIAL_COM1 + 5) & 0x20u)) break;
        outb(SERIAL_COM1, (uint8_t)'\r');
    }
    for (spin = 0; spin < 100000u; ++spin)
        if ((inb(SERIAL_COM1 + 5) & 0x20u)) break;
    outb(SERIAL_COM1, (uint8_t)c);
}
void __boot_serial_putc(char c) {
    if (g_boot_serial_runtime_quiet) return;
    boot_serial_putc_raw(c);
}
void __boot_serial_puts(const char *s) {
    while (*s) { __boot_serial_putc(*s++); }
}
void __boot_serial_puthex64(uint64_t v) {
    static const char *hex = "0123456789abcdef";
    int i;
    __boot_serial_putc('0'); __boot_serial_putc('x');
    for (i = 60; i >= 0; i -= 4) {
        __boot_serial_putc(hex[(v >> i) & 0xF]);
    }
}
void __boot_serial_putu32(uint32_t v) {
    char buf[12];
    int i = 0, j;
    if (v == 0) { __boot_serial_putc('0'); return; }
    while (v) { buf[i++] = '0' + (char)(v % 10); v /= 10; }
    for (j = i - 1; j >= 0; --j) __boot_serial_putc(buf[j]);
}
void __boot_serial_force_putc(char c) {
    boot_serial_putc_raw(c);
}
void __boot_serial_force_puts(const char *s) {
    while (*s) { __boot_serial_force_putc(*s++); }
}
void __boot_serial_force_puthex64(uint64_t v) {
    static const char *hex = "0123456789abcdef";
    int i;
    __boot_serial_force_putc('0'); __boot_serial_force_putc('x');
    for (i = 60; i >= 0; i -= 4) {
        __boot_serial_force_putc(hex[(v >> i) & 0xF]);
    }
}
void __boot_serial_force_putu32(uint32_t v) {
    char buf[12];
    int i = 0, j;
    if (v == 0) { __boot_serial_force_putc('0'); return; }
    while (v) { buf[i++] = '0' + (char)(v % 10); v /= 10; }
    for (j = i - 1; j >= 0; --j) __boot_serial_force_putc(buf[j]);
}

void kernel_main(uint32_t magic, uint32_t mbi_addr) {
    const char *native_shell_path = NULL;
    const char *riduxui_shell_path = NULL;
    const char *hyprland_shell_path = NULL;
    const char *qt_shell_path = NULL;
    const char *gl_compositor_path = NULL;
    const char *vulkan_probe_path = NULL;
    bool native_shell_started = false;
    bool riduxui_shell_started = false;
    bool hyprland_shell_started = false;
    bool qt_shell_started = false;
    bool gl_compositor_started = false;
    bool native_desktop_disabled = false;

    __boot_serial_init_direct();
    /* Force g_serial_ready so klog()/shell_push_line() actually emit
     * bytes. The normal serial_probe does a loopback self-test that
     * fails on VirtualBox file-backed UARTs (VBox doesn't emulate
     * the loopback bit), which would leave the kernel log silent
     * despite the UART being perfectly usable for writing. */
    g_serial_ready = true;
    __boot_serial_puts("[boot] kernel_main entered\n");

    if (magic != MB2_BOOTLOADER_MAGIC) {
        __boot_serial_puts("[boot] FATAL: bad multiboot2 magic, halting\n");
        for (;;) __asm__ volatile("cli; hlt");
    }
    __boot_serial_puts("[boot] multiboot2 magic OK, parsing MBI\n");
    parse_multiboot_info(mbi_addr);
    __boot_serial_puts("[boot] MBI parsed\n");
    if (!g_fb.ready || g_fb.bpp < 24) {
        __boot_serial_puts("[boot] framebuffer not ready; continuing for DRM/virtio-gpu path\n");
        g_fb.address = NULL;
        g_fb.pitch = 4096;
        g_fb.width = 1024;
        g_fb.height = 768;
        g_fb.bpp = 32;
        g_fb.red_pos = 16;
        g_fb.red_size = 8;
        g_fb.green_pos = 8;
        g_fb.green_size = 8;
        g_fb.blue_pos = 0;
        g_fb.blue_size = 8;
        g_fb.ready = false;
    } else {
        __boot_serial_puts("[boot] framebuffer ready\n");
    }

    shell_reset_history();
    g_shell_input[0] = 0; g_shell_input_len = 0;
    g_shift_down = g_ctrl_down = g_extended_scancode = false;
    g_keyboard_accept_after_tick = 0;
    g_wm_drag_mode = false;
    g_arrow_hint_shown = false;
    g_theme_index = 0;
    g_start_open = false;
    g_quick_open = false;
    g_notif_open = false;
    g_needs_redraw = true;

    g_use_backbuffer = g_fb.width <= FB_MAX_WIDTH && g_fb.height <= FB_MAX_HEIGHT;
    if (g_use_backbuffer) k_memset(g_backbuffer, 0, sizeof(g_backbuffer));

    /* Detect the common QEMU/VBox 32bpp BGRA layout so the whole
     * pixel pipeline can take the fast path (no divisions). */
    g_fb_fast_bgra = (g_fb.bpp == 32 &&
                      g_fb.red_pos == 16 && g_fb.red_size == 8 &&
                      g_fb.green_pos == 8 && g_fb.green_size == 8 &&
                      g_fb.blue_pos == 0 && g_fb.blue_size == 8);
    g_gpu_accel_enabled = (g_use_backbuffer && g_fb_fast_bgra);

    /* Calibrate TSC-based frame throttle (60 fps target). */
    calibrate_frame_period();

    g_mouse_initialized = false;
    g_mouse_x = (int)g_fb.width / 2;
    g_mouse_y = (int)g_fb.height / 2;
    g_mouse_packet_index = 0;
    g_mouse_left_down = false;
    g_mouse_right_down = false;
    g_mouse_dragging = false;
    g_mouse_drag_window_id = -1;
    g_mouse_drag_kind = 0;
    g_mouse_resize_edges = 0;

    /* GDT + heap first (no interrupts yet). */
    __boot_serial_puts("[boot] gdt_install...\n");
    gdt_install();
    __boot_serial_puts("[boot] heap_init...\n");
    heap_init();
    platform_probe_cpu_topology();
    klog("gdt + heap ready");
    if (g_cpu_logical_count > 1u) {
        char smp_line[128];
        size_t smp_len = 0;
        smp_line[0] = 0;
        k_append_str(smp_line, &smp_len, sizeof(smp_line), "smp topo: ");
        k_append_u32(smp_line, &smp_len, sizeof(smp_line), g_cpu_logical_count);
        k_append_str(smp_line, &smp_len, sizeof(smp_line), " logical, ");
        k_append_u32(smp_line, &smp_len, sizeof(smp_line), g_cpu_core_count);
        k_append_str(smp_line, &smp_len, sizeof(smp_line), " core(s), online=");
        k_append_u32(smp_line, &smp_len, sizeof(smp_line), g_cpu_online_count);
        k_append_str(smp_line, &smp_len, sizeof(smp_line), g_madt_detected ? " (MADT+BSP)" : " (CPUID+BSP)");
        klog(smp_line);
    }
    __boot_serial_puts("[boot] gdt + heap done\n");

    /* Drivers must initialize BEFORE we enable hardware IRQs, because
     * PS/2 setup uses polling on ports 0x60/0x64 to read the compaq
     * status byte and mouse ACKs. If IRQs were live, our ISR would
     * steal those response bytes and mouse_init would write back a
     * garbage compaq byte (turning off IRQ12). */
    __boot_serial_puts("[boot] drivers_bootstrap...\n");
    drivers_bootstrap();
    gpu_update_status_from_pci();
    klog("drivers ready");
    __boot_serial_puts("[boot] drivers done\n");

    /* Now safe to install IDT, remap PIC and unmask IRQs. */
    __boot_serial_puts("[boot] interrupts_bootstrap...\n");
    interrupts_bootstrap();
    klog("idt + pic ready");
    __boot_serial_puts("[boot] idt + pic done\n");

    /* Filesystem + ELF. */
    __boot_serial_puts("[boot] vfs_init...\n");
    vfs_init();
    if (g_initrd_start && g_initrd_end && g_initrd_end > g_initrd_start) {
        __boot_serial_puts("[boot] vfs_mount_initrd...\n");
        vfs_mount_initrd(g_initrd_start, g_initrd_end);
        if (g_initrd_overlay_start && g_initrd_overlay_end &&
            g_initrd_overlay_end > g_initrd_overlay_start) {
            __boot_serial_puts("[boot] vfs_mount_initrd overlay...\n");
            vfs_mount_initrd(g_initrd_overlay_start, g_initrd_overlay_end);
            klog("initrd overlay mounted");
        }
        if (g_vfs_mount_dropped_slots || g_vfs_mount_dropped_paths) {
            char line[160];
            size_t l = 0;
            line[0] = 0;
            k_append_str(line, &l, sizeof(line), "vfs mount warnings: dropped_slots=");
            k_append_u32(line, &l, sizeof(line), g_vfs_mount_dropped_slots);
            k_append_str(line, &l, sizeof(line), " dropped_paths=");
            k_append_u32(line, &l, sizeof(line), g_vfs_mount_dropped_paths);
            k_append_str(line, &l, sizeof(line), " loaded=");
            k_append_u32(line, &l, sizeof(line), (uint32_t)vfs_count());
            klog(line);
            __boot_serial_puts("[boot] ");
            __boot_serial_puts(line);
            __boot_serial_puts("\n");
        }
    }
    vfs_seed_defaults();
    elf_init();
    klog("vfs mounted");
    __boot_serial_puts("[boot] vfs done\n");

    /* Extended compat layer: ATA, AHCI, NIC, TCP/IP, FAT32, ext2,
     * syscall table, threading, /dev /proc /sys, ELF64 loader. */
    __boot_serial_puts("[boot] compat_init_all...\n");
    compat_init_all();
    klog("compat layer ready");
    __boot_serial_puts("[boot] compat done\n");

    {
        uint64_t reserve_end = kernel_phys_reserve_end();
        __boot_serial_puts("[boot] initrd main=");
        __boot_serial_puthex64((uint64_t)(uintptr_t)g_initrd_start);
        __boot_serial_puts("-");
        __boot_serial_puthex64((uint64_t)(uintptr_t)g_initrd_end);
        __boot_serial_puts(" overlay=");
        __boot_serial_puthex64((uint64_t)(uintptr_t)g_initrd_overlay_start);
        __boot_serial_puts("-");
        __boot_serial_puthex64((uint64_t)(uintptr_t)g_initrd_overlay_end);
        __boot_serial_puts(" reserve_end=");
        __boot_serial_puthex64(reserve_end);
        __boot_serial_puts(" mem_top=");
        __boot_serial_puthex64(g_phys_mem_top);
        __boot_serial_puts("\n");
        if (g_phys_mem_top) pmm_set_memory_limit(g_phys_mem_top);
        pmm_set_alloc_base(reserve_end);
    }

    /* Deep infrastructure: PMM, paging, TSS, tasks, TTY, signals,
     * ext2 full, net packets, shared memory, timers. */
    __boot_serial_puts("[boot] compat2_init_all...\n");
    compat2_init_all();
    klog("compat2 deep infra ready");
    __boot_serial_puts("[boot] compat2 done\n");
    map_high_framebuffer_if_needed();
    __boot_serial_puts("[boot] map_high_framebuffer done\n");

    /* Real syscall wiring, VFS bridge, ELF64 mapper, procfs,
     * Linux ABI (uname 6.1.0), net syscalls, memory syscalls. */
    __boot_serial_puts("[boot] compat3_init_all...\n");
    compat3_init_all();
    klog("compat3 real syscalls ready");
    __boot_serial_puts("[boot] compat3 done\n");

    /* Micro-libc: string/stdlib/stdio/pthread/dlopen/DRM/env. */
    __boot_serial_puts("[boot] compat4_init_all...\n");
    compat4_init_all();
    klog("compat4 micro-libc ready");
    __boot_serial_puts("[boot] compat4 done\n");

    /* Extended libc helpers: fnmatch, base64, arc4random, regex, DNS, X11 stubs. */
    __boot_serial_puts("[boot] compat5_init_all...\n");
    compat5_init_all();
    klog("compat5 extended libc ready");
    __boot_serial_puts("[boot] compat5 done\n");

    /* Modern Linux ABI surface: openat2/close_range/statx/pidfd/clone3/io_uring. */
    __boot_serial_puts("[boot] compat6_init_all...\n");
    compat6_init_all();
    klog("compat6 modern ABI ready");
    __boot_serial_puts("[boot] compat6 done\n");

    /* New compat7 layer */
    __boot_serial_puts("[boot] compat7_init_all...\n");
    compat7_init_all();
    klog("compat7 layer ready");
    __boot_serial_puts("[boot] compat7 done\n");

    /* Full browser runtime: glibc ABI, sandbox, rendering, IPC. */
    __boot_serial_puts("[boot] compat8_init_all...\n");
    compat8_init_all();
    klog("compat8 browser runtime ready");
    __boot_serial_puts("[boot] compat8 done\n");

    /* Ridux native R3 window protocol. Registers RIDUX_SYS_WINDOW_*
     * (500..503) into g_syscall_table[]. Runs LAST so neither compat6
     * nor compat8 can overwrite our slots. */
    __boot_serial_puts("[boot] ridux_r3wm_init...\n");
    ridux_r3wm_init();
    klog("ridux R3 WM protocol ready");
    __boot_serial_puts("[boot] ridux_r3wm done\n");

    /* Processes + scheduler. */
    __boot_serial_puts("[boot] proc_bootstrap...\n");
    proc_bootstrap();
    __boot_serial_puts("[boot] scheduler_tick...\n");
    scheduler_tick();

    const char *wayfire_shell_path = kernel_wayfire_shell_path();
    bool wayfire_shell_started = false;
    hyprland_shell_path = kernel_hyprland_shell_path();
    riduxui_shell_path = kernel_riduxui_shell_path();
    gl_compositor_path = kernel_gl_compositor_path();
    native_shell_path = kernel_native_shell_path();
    qt_shell_path = kernel_qt_shell_path();
    vulkan_probe_path = kernel_vulkan_probe_path();
    g_wayfire_desktop_active = false;
    if (hyprland_shell_path) {
        klog("Hyprland wlroots/Mesa desktop payload found");
        __boot_serial_puts("[boot] Hyprland wlroots/Mesa desktop payload found\n");
    } else if (wayfire_shell_path) {
        klog("Wayfire wlroots/Mesa desktop payload found");
        __boot_serial_puts("[boot] Wayfire wlroots/Mesa desktop payload found\n");
    } else if (riduxui_shell_path) {
        klog("RiduxUI native accelerated shell payload found");
        __boot_serial_puts("[boot] RiduxUI native accelerated shell payload found\n");
    } else if (gl_compositor_path) {
        klog("Ridux Mesa/OpenGL compositor payload found");
        __boot_serial_puts("[boot] Ridux Mesa/OpenGL compositor payload found\n");
    } else if (native_shell_path) {
        klog("Ridux native shell payload found");
        __boot_serial_puts("[boot] Ridux native shell payload found\n");
    } else if (qt_shell_path) {
        klog("Ridux Qt EGLFS/KMS shell payload found as compatibility fallback");
        __boot_serial_puts("[boot] Ridux Qt EGLFS/KMS shell payload found as fallback\n");
    } else {
        klog("Ridux native Ring3 shell payload missing; kernel compositor fallback active");
        __boot_serial_puts("[boot] Ridux native shell missing; kernel compositor fallback active\n");
    }

    __boot_serial_puts("[boot] ui_build_layout...\n");

    /* Keep geometry initialized before the Ring 3 shell opens its desktop
     * surface. The kernel compositor still owns window management and input. */
    ui_build_layout();
    __boot_serial_puts("[boot] native compositor active; launching Ridux shell\n");
    g_kernel_preempt_disable++;
    {
        char detail[384];
        int shell_rc;
        detail[0] = 0;

        if (hyprland_shell_path) {
            const char *dbus_system_path = kernel_hyprland_dbus_system_path();
            const char *dbus_session_path = kernel_hyprland_dbus_session_path();
            if (dbus_system_path) {
                int dbus_sys_rc = compat5_spawn_user_elf_background(dbus_system_path,
                                                                    detail, sizeof(detail));
                if (dbus_sys_rc >= 0) {
                    __boot_serial_puts("[boot] Hyprland DBus system bus launched\n");
                } else {
                    __boot_serial_puts("[boot] Hyprland DBus system bus launch failed\n");
                }
            }
            if (dbus_session_path) {
                int dbus_rc = compat5_spawn_user_elf_background(dbus_session_path,
                                                                detail, sizeof(detail));
                if (dbus_rc >= 0) {
                    __boot_serial_puts("[boot] Hyprland DBus session bus launched\n");
                } else {
                    __boot_serial_puts("[boot] Hyprland DBus session bus launch failed\n");
                }
            }
            shell_rc = compat5_spawn_user_elf_background(hyprland_shell_path,
                                                         detail, sizeof(detail));
            if (shell_rc >= 0) {
                hyprland_shell_started = true;
                native_desktop_disabled = true;
                g_wayfire_desktop_active = true;
                klog("Hyprland wlroots/Mesa desktop launched as primary desktop");
                __boot_serial_puts("[boot] Hyprland wlroots/Mesa desktop launched\n");
            } else {
                hyprland_shell_started = false;
                native_desktop_disabled = false;
                klog("Hyprland primary desktop launch failed");
                __boot_serial_puts("[boot] Hyprland primary desktop launch failed\n");
            }
        }

        if (!hyprland_shell_started && wayfire_shell_path) {
            shell_rc = compat5_spawn_user_elf_background(wayfire_shell_path,
                                                         detail, sizeof(detail));
            if (shell_rc >= 0) {
                wayfire_shell_started = true;
                native_desktop_disabled = true;
                g_wayfire_desktop_active = true;
                klog("Wayfire wlroots/Mesa desktop launched as primary desktop");
                __boot_serial_puts("[boot] Wayfire wlroots/Mesa desktop launched\n");
            } else {
                wayfire_shell_started = false;
                native_desktop_disabled = false;
                klog("Wayfire primary desktop launch failed");
                __boot_serial_puts("[boot] Wayfire primary desktop launch failed\n");
            }
        }

        if (!hyprland_shell_started && !wayfire_shell_started && riduxui_shell_path) {
            shell_rc = compat5_spawn_user_elf_background(riduxui_shell_path,
                                                         detail, sizeof(detail));
            if (shell_rc >= 0) {
                riduxui_shell_started = true;
                native_desktop_disabled = true;
                g_wayfire_desktop_active = true;
                klog("RiduxUI native accelerated shell launched as primary desktop");
                __boot_serial_puts("[boot] RiduxUI native accelerated shell launched\n");
            } else {
                riduxui_shell_started = false;
                native_desktop_disabled = false;
                klog("RiduxUI native accelerated shell launch failed; falling back to GL compositor");
                __boot_serial_puts("[boot] RiduxUI shell launch failed; fallback active\n");
            }
        }

        if (!hyprland_shell_started && !wayfire_shell_started && !riduxui_shell_started && gl_compositor_path) {
            shell_rc = compat5_spawn_user_elf_background(gl_compositor_path,
                                                         detail, sizeof(detail));
            if (shell_rc >= 0) {
                gl_compositor_started = true;
                native_desktop_disabled = true;
                g_wayfire_desktop_active = true;
                klog("Ridux Mesa/OpenGL compositor launched as primary desktop");
                __boot_serial_puts("[boot] Ridux Mesa/OpenGL compositor launched\n");
            } else {
                gl_compositor_started = false;
                native_desktop_disabled = false;
                klog("Ridux Mesa/OpenGL compositor launch failed; falling back to R3 shell");
                __boot_serial_puts("[boot] Ridux Mesa/OpenGL compositor launch failed; fallback active\n");
            }
        }

        if (!hyprland_shell_started && !wayfire_shell_started && !riduxui_shell_started && !gl_compositor_started && native_shell_path) {
            shell_rc = compat5_spawn_user_elf_background(native_shell_path,
                                                         detail, sizeof(detail));
            if (shell_rc >= 0) {
                native_shell_started = true;
                klog("Ridux native shell launched as primary desktop");
                __boot_serial_puts("[boot] Ridux native shell launched\n");
            } else {
                native_shell_started = false;
                klog("Ridux native Ring3 shell launch failed; kernel compositor fallback remains active");
                __boot_serial_puts("[boot] Ridux native shell launch failed; kernel compositor fallback remains active\n");
            }
        }

        if (!hyprland_shell_started && !wayfire_shell_started && !riduxui_shell_started && !gl_compositor_started && !native_shell_started && qt_shell_path) {
            shell_rc = compat5_spawn_user_elf_background_args(qt_shell_path,
                                                              "--direct-kms --mode=session",
                                                              detail, sizeof(detail));
            if (shell_rc >= 0) {
                qt_shell_started = true;
                native_desktop_disabled = true;
                g_wayfire_desktop_active = true;
                klog("Ridux Qt EGLFS/KMS shell launched as primary desktop");
                __boot_serial_puts("[boot] Ridux Qt EGLFS/KMS shell launched\n");
            } else {
                qt_shell_started = false;
                native_desktop_disabled = false;
                klog("Ridux Qt EGLFS/KMS shell launch failed; falling back to GL compositor");
                __boot_serial_puts("[boot] Ridux Qt EGLFS/KMS shell launch failed; fallback active\n");
            }
        }
    }
    /* La terminal vive en el dock. Arrancarla encima del escritorio haria
     * que RiduxUI pareciera una demo de debug en vez de un desktop real. */
    if (g_kernel_preempt_disable) --g_kernel_preempt_disable;
    if (vulkan_probe_path) {
        char vk_detail[384];
        int vk_rc;
        vk_detail[0] = 0;
        vk_rc = compat5_spawn_user_elf_background(vulkan_probe_path,
                                                  vk_detail, sizeof(vk_detail));
        if (vk_rc >= 0) {
            __boot_serial_puts("[boot] Vulkan hardware probe launched\n");
        } else {
            __boot_serial_puts("[boot] Vulkan hardware probe launch failed\n");
        }
    }
    if (hyprland_shell_started || wayfire_shell_started || native_shell_started || riduxui_shell_started || qt_shell_started || gl_compositor_started) {
        __boot_serial_puts("[boot] priming Ridux desktop scheduler\n");
        task_schedule();
        __boot_serial_puts("[boot] Ridux desktop scheduler prime returned\n");
    }
    __boot_serial_puts("[boot] wm_focus done\n");

    shell_boot_message();
    __boot_serial_puts("[boot] shell_boot_message done\n");
    if (!native_desktop_disabled) {
        __boot_serial_puts("[boot] calling initial render_scene()\n");
        render_scene();
        __boot_serial_puts("[boot] initial render_scene() returned\n");
    } else {
        __boot_serial_puts("[boot] initial native render skipped\n");
    }
    /* Keep boot diagnostics, but do not let runtime/browser trace spam turn
     * the UART into a global scheduler lock. Panic/klog still use serial_write. */
    g_boot_serial_runtime_quiet = true;
    shell_run_autoboot_commands();
    g_shell_input_len = 0;
    g_shell_input[0] = 0;
    g_shift_down = g_ctrl_down = g_extended_scancode = false;
    g_keyboard_accept_after_tick = g_pit_ticks + 300u;
    __boot_serial_puts("[boot] shell_run_autoboot done\n");
    if (!native_desktop_disabled) {
        __boot_serial_puts("[boot] calling post-autoboot render_scene()\n");
        render_scene();
        __boot_serial_puts("[boot] post-autoboot render_scene() returned\n");
    } else {
        __boot_serial_puts("[boot] post-autoboot native render skipped\n");
    }
    klog("ready");
    {
        char dbg[96];
        size_t n = 0;
        __boot_serial_puts("[diag] g_fb.ready=");
        __boot_serial_puts(g_fb.ready ? "true" : "false");
        __boot_serial_puts(" g_use_backbuffer=");
        __boot_serial_puts(g_use_backbuffer ? "true" : "false");
        __boot_serial_puts("\n");
        (void)dbg; (void)n;
    }

    g_last_render_tsc = rdtsc32();
    {
    uint64_t last_compat_service_tick = g_pit_ticks;
    for (;;) {
        uint32_t now;
        uint64_t tick_snapshot;
        bool had_input = input_pump(32);

        /* Service user display protocols on real timer cadence, not on a
         * tight spin-loop. The PIT IRQ also services this while a ring-3
         * foreground task is running, so this path is mostly for idle kernel
         * desktop time and deferred launches between user slices. */
        tick_snapshot = g_pit_ticks;
        if (had_input || tick_snapshot != last_compat_service_tick) {
            compat7_tick_all();
            last_compat_service_tick = tick_snapshot;
        }

        /* Frame throttle via TSC (real wall-time, CPU-speed agnostic).
         * Full scene composite is throttled to the configured frame
         * period; between those, the cursor fast-path runs unbounded
         * so mouse motion is fluid even when render_scene is heavy. */
        now = rdtsc32();
        if (native_desktop_disabled) {
            /* External compositor mode is disabled for the Ridux native shell
             * path. Keep this branch as an emergency kill switch. */
            if ((uint32_t)(now - g_last_render_tsc) >= g_frame_period_tsc) {
                compat7_tick_all();
                g_last_render_tsc = now;
                ++g_frame_counter;
            }
        } else if (g_needs_redraw &&
            (uint32_t)(now - g_last_render_tsc) >= g_frame_period_tsc) {
            render_scene();
            compat7_tick_all();
            g_last_render_tsc = now;
            ++g_frame_counter;
        } else if (!g_needs_redraw && g_cursor_moved) {
            render_cursor_only();
        }
        if (g_irq0_preempt_request && !g_kernel_preempt_disable) {
            g_irq0_preempt_request = false;
            task_schedule();
            continue;
        }
        if (!had_input && !g_needs_redraw && !g_cursor_moved) {
            __asm__ volatile("sti; hlt" ::: "memory");
        } else if (!had_input) {
            __asm__ volatile("pause");
        }
    }
    }
}
