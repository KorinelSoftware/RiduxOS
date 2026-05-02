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

static void parse_multiboot_info(uint32_t mbi_addr) {
    uint8_t *ptr;
    uint32_t total_size;

    g_fb.ready = false;
    g_initrd_start = NULL;
    g_initrd_end = NULL;
    g_initrd_overlay_start = NULL;
    g_initrd_overlay_end = NULL;
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

/* VirtualBox can expose the linear framebuffer above 4 GiB.
 * Boot64 only identity-maps the first 4 GiB, so we add missing
 * kernel mappings once compat2 paging is online. */
static void map_high_framebuffer_if_needed(void){
    address_space_t *kas;
    uint64_t fb_phys,fb_size,start,end,va;
    if(!g_fb.ready)return;
    fb_phys=(uint64_t)(uintptr_t)g_fb.address;
    fb_size=(uint64_t)g_fb.pitch*(uint64_t)g_fb.height;
    if(fb_size==0)fb_size=(uint64_t)g_fb.width*(uint64_t)g_fb.height*4ULL;
    if(fb_phys+fb_size<=0x100000000ULL)return;
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
        __boot_serial_puts("[boot] FATAL: framebuffer not ready (fb.ready or bpp<24), halting\n");
        for (;;) __asm__ volatile("cli; hlt");
    }
    __boot_serial_puts("[boot] framebuffer ready\n");

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

    pmm_set_alloc_base(kernel_phys_reserve_end());

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
    __boot_serial_puts("[boot] ui_build_layout...\n");

    /* UI layout first so window defaults fit. */
    ui_build_layout();
    __boot_serial_puts("[boot] apps_bootstrap...\n");
    apps_bootstrap();
    __boot_serial_puts("[boot] apps_bootstrap done\n");
    g_kernel_preempt_disable++;
    {
        char detail[128];
        detail[0] = 0;
        if (compat5_spawn_user_elf_background("/bin/desktop-shell-r3.elf",
                                              detail, sizeof(detail)) >= 0) {
            klog("desktop shell Ring 3 launched");
            __boot_serial_puts("[boot] desktop-shell-r3 launched\n");
        } else {
            __boot_serial_puts("[boot] desktop-shell-r3 not available\n");
        }
    }
    {
        char detail[128];
        detail[0] = 0;
        if (compat5_spawn_user_elf_background("/bin/terminal-r3.elf",
                                              detail, sizeof(detail)) >= 0) {
            klog("terminal Ring 3 launched");
            __boot_serial_puts("[boot] terminal-r3 launched\n");
        } else {
            __boot_serial_puts("[boot] terminal-r3 not available\n");
        }
    }
    if (g_kernel_preempt_disable) --g_kernel_preempt_disable;
    __boot_serial_puts("[boot] wm_focus done\n");

    shell_boot_message();
    __boot_serial_puts("[boot] shell_boot_message done\n");
    __boot_serial_puts("[boot] calling initial render_scene()\n");
    render_scene();
    __boot_serial_puts("[boot] initial render_scene() returned\n");
    /* Keep boot diagnostics, but do not let runtime/browser trace spam turn
     * the UART into a global scheduler lock. Panic/klog still use serial_write. */
    g_boot_serial_runtime_quiet = true;
    shell_run_autoboot_commands();
    g_shell_input_len = 0;
    g_shell_input[0] = 0;
    g_shift_down = g_ctrl_down = g_extended_scancode = false;
    g_keyboard_accept_after_tick = g_pit_ticks + 300u;
    __boot_serial_puts("[boot] shell_run_autoboot done\n");
    __boot_serial_puts("[boot] calling post-autoboot render_scene()\n");
    render_scene();
    __boot_serial_puts("[boot] post-autoboot render_scene() returned\n");
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
        if (g_needs_redraw &&
            (uint32_t)(now - g_last_render_tsc) >= g_frame_period_tsc) {
            render_scene();
            compat7_tick_all();
            g_last_render_tsc = now;
            ++g_frame_counter;
        } else if (!g_needs_redraw && g_cursor_moved) {
            render_cursor_only();
        }
        if (!had_input && !g_needs_redraw && !g_cursor_moved) {
            __asm__ volatile("sti; hlt" ::: "memory");
        } else if (!had_input) {
            __asm__ volatile("pause");
        }
    }
    }
}
