/* Shell */

/* Redirect/pipe capture: when active, shell_push_line diverts all
 * command output into g_shell_capture[] instead of the terminal. */
#define SHELL_CAPTURE_LINES 64
static char   g_shell_capture[SHELL_CAPTURE_LINES][SHELL_LINE_LEN];
static int    g_shell_capture_count;
static bool   g_shell_capturing;

static void shell_capture_begin(void) {
    g_shell_capturing     = true;
    g_shell_capture_count = 0;
    g_shell_capture[0][0] = 0;
}
static void shell_capture_end(void)  { g_shell_capturing = false; }
static void shell_capture_push(const char *line) {
    if (g_shell_capture_count < SHELL_CAPTURE_LINES) {
        k_strlcpy(g_shell_capture[g_shell_capture_count++], line, SHELL_LINE_LEN);
    }
}

static void shell_push_line(const char *line) {
    if (g_shell_capturing) {
        shell_capture_push(line);
        serial_write("[cap] "); serial_write(line); serial_write("\n");
        return;
    }
    if (g_shell_count < SHELL_MAX_LINES) {
        k_strlcpy(g_shell_lines[g_shell_count], line, SHELL_LINE_LEN);
        ++g_shell_count;
        serial_write(line); serial_write("\n");
        return;
    }
    {
        size_t i;
        for (i = 1; i < SHELL_MAX_LINES; ++i)
            k_memcpy(g_shell_lines[i - 1], g_shell_lines[i], SHELL_LINE_LEN);
    }
    k_strlcpy(g_shell_lines[SHELL_MAX_LINES - 1], line, SHELL_LINE_LEN);
    serial_write(line); serial_write("\n");
}

static void shell_push_bytes(const uint8_t *data, uint32_t size) {
    char line[SHELL_LINE_LEN];
    size_t line_len = 0;
    uint32_t i;
    line[0] = 0;
    for (i = 0; i < size; ++i) {
        char c = (char)data[i];
        if (c == '\r') continue;
        if (c == '\n') { line[line_len] = 0; shell_push_line(line); line_len = 0; line[0] = 0; continue; }
        if ((unsigned char)c < 32 || (unsigned char)c > 126) c = '.';
        if (line_len + 2 >= SHELL_LINE_LEN) {
            line[line_len] = 0; shell_push_line(line);
            line_len = 0; line[0] = 0;
        }
        line[line_len++] = c; line[line_len] = 0;
    }
    if (line_len > 0) shell_push_line(line);
}

static void shell_reset_history(void) {
    size_t i;
    for (i = 0; i < SHELL_MAX_LINES; ++i) g_shell_lines[i][0] = 0;
    g_shell_count = 0;
}

/* Forward declaration for boot autorun helper. */
static void shell_execute_command(const char *command);
extern void ridux_drm_cursor_move(int32_t x, int32_t y);
extern bool kvfs_exists(const char *path);

static bool shell_input_debug_trace_enabled(void) {
    static int cached = -1;
    if (cached < 0)
        cached = kvfs_exists("/etc/ridux-input-debug.enable") ? 1 : 0;
    return cached != 0;
}

static void shell_boot_message(void) {
    char line[96];
    size_t len = 0;
    shell_push_line("RiduxOS Unix 0.4 Bloom booted.");
    shell_push_line("UI active.");
    shell_push_line("Type `help` for commands.");
    line[0] = 0;
    k_append_str(line, &len, sizeof(line), "FrameBuffer ");
    k_append_u32(line, &len, sizeof(line), g_fb.width);
    k_append_str(line, &len, sizeof(line), "x");
    k_append_u32(line, &len, sizeof(line), g_fb.height);
    k_append_str(line, &len, sizeof(line), "x");
    k_append_u32(line, &len, sizeof(line), g_fb.bpp);
    shell_push_line(line);

    len = 0; line[0] = 0;
    k_append_str(line, &len, sizeof(line), "Drivers: ");
    k_append_u32(line, &len, sizeof(line), (uint32_t)g_driver_count);
    k_append_str(line, &len, sizeof(line), "  PCI: ");
    k_append_u32(line, &len, sizeof(line), (uint32_t)g_pci_device_count);
    k_append_str(line, &len, sizeof(line), "  VFS: ");
    k_append_u32(line, &len, sizeof(line), (uint32_t)vfs_count());
    shell_push_line(line);
}

static const char *shell_autoboot_command_start(char *cmd) {
    unsigned char *p = (unsigned char *)cmd;
    if (p[0] == 0xEFu && p[1] == 0xBBu && p[2] == 0xBFu) p += 3;
    while (*p == ' ' || *p == '\t') ++p;
    if (*p == 0 || *p == '#') return 0;
    return (const char *)p;
}

static void shell_run_autoboot_commands(void) {
    const uint8_t *data = 0;
    uint32_t size = 0;
    char cmd[SHELL_LINE_LEN];
    char kline[96];
    size_t i, len = 0;
    if (!vfs_read("/etc/autoboot.cmd", &data, &size) || !data || size == 0) return;
    shell_push_line("[autorun] /etc/autoboot.cmd");
    klog("[autorun] /etc/autoboot.cmd");
    cmd[0] = 0;
    for (i = 0; i < size; ++i) {
        char c = (char)data[i];
        if (c == '\r') continue;
        if (c == '\n') {
            cmd[len] = 0;
            const char *run = shell_autoboot_command_start(cmd);
            if (run) {
                size_t kl = 0;
                kline[0] = 0;
                k_append_str(kline, &kl, sizeof(kline), "[autorun] ");
                k_append_str(kline, &kl, sizeof(kline), run);
                klog(kline);
                shell_execute_command(run);
            }
            len = 0;
            cmd[0] = 0;
            continue;
        }
        if (len + 1 < sizeof(cmd)) {
            cmd[len++] = c;
            cmd[len] = 0;
        }
    }
    const char *run = shell_autoboot_command_start(cmd);
    if (run) {
        size_t kl = 0;
        kline[0] = 0;
        k_append_str(kline, &kl, sizeof(kline), "[autorun] ");
        k_append_str(kline, &kl, sizeof(kline), run);
        klog(kline);
        shell_execute_command(run);
    }
}

static void shell_cmd_help(void) {
    shell_push_line("-- general --");
    shell_push_line("help clear about uname theme reboot shutdown halt beep time date");
    shell_push_line("uptime free mem df kernlog dmesg sleep whoami hostname pwd env");
    shell_push_line("-- files --");
    shell_push_line("ls [path] | cat <p> | touch <p> | rm <p> | write <p> <text>");
    shell_push_line("head [-n N] <p> | tail [-n N] <p> | wc <p> | grep <pat> <p>");
    shell_push_line("find <pattern> | hexdump <p> | cp <src> <dst> | mv <src> <dst>");
    shell_push_line("mkdir <p> | edit <p>");
    shell_push_line("-- proc/drv --");
    shell_push_line("ps | spawn <n> | kill <pid> | top | pstree");
    shell_push_line("drivers | pci | gpu | smp | irqs | heap | flush");
    shell_push_line("-- wm/apps --");
    shell_push_line("wm list|focus|move|drag|next|max|min|close");
    shell_push_line("elf load|run|list <path>");
    shell_push_line("apps | open <n> | close <n> | firefox | chrome | chromium");
    shell_push_line("wl | x11 | browser-real <firefox|chromium> (Linux ABI nativo)");
    shell_push_line("browser-vm <firefox|chromium> (fallback/oraculo seL4)");
    shell_push_line("-- net --");
    shell_push_line("ifconfig | ping <ip> | arp | dns <host>");
    shell_push_line("-- compat --");
    shell_push_line("lsblk ifconfig route mount lsdev catproc nslookup dhclient");
    shell_push_line("syscalls threads ss elf64 pmm tasks paging shm timers fds");
    shell_push_line("realsys mmaps procfs dynlink libc dlsym heap bsd b64 rng browser [check|run|realrun]");
    shell_push_line("abi6 pidfd io_uring statx sysplus");
    shell_push_line("-- pipes & redirs --");
    shell_push_line("cmd > file    cmd >> file    cmd1 | cmd2 (limited)");
    shell_push_line("Up/Down arrows cycle history. Tab completes when empty or a path prefix.");
}

static void shell_cmd_ls(const char *prefix) {
    char norm[VFS_MAX_PATH];
    size_t i, matched = 0;
    vfs_normalize_path(prefix, norm, sizeof(norm));
    for (i = 0; i < VFS_MAX_FILES; ++i) {
        if (g_vfs_files[i].used && (k_strcmp(norm, "/") == 0 ||
                                    k_starts_with(g_vfs_files[i].path, norm))) {
            char line[SHELL_LINE_LEN];
            size_t len = 0;
            line[0] = 0;
            k_append_str(line, &len, sizeof(line), g_vfs_files[i].writable ? "rw " : "ro ");
            k_append_str(line, &len, sizeof(line), g_vfs_files[i].path);
            k_append_str(line, &len, sizeof(line), " (");
            k_append_u32(line, &len, sizeof(line),
                         g_vfs_files[i].writable ? g_vfs_files[i].rw_size : g_vfs_files[i].ro_size);
            k_append_str(line, &len, sizeof(line), "B)");
            shell_push_line(line);
            ++matched;
        }
    }
    if (!matched) shell_push_line("No files matched.");
}
static void shell_cmd_cat(const char *p) {
    const uint8_t *d; uint32_t s;
    if (!vfs_read(p, &d, &s)) { shell_push_line("cat: not found"); return; }
    if (!s) { shell_push_line("(empty file)"); return; }
    shell_push_bytes(d, s);
}
static void shell_cmd_touch(const char *p) {
    shell_push_line(vfs_touch(p) ? "touch: ok" : "touch: failed");
}
static void shell_cmd_rm(const char *p) {
    shell_push_line(vfs_remove(p) ? "rm: removed" : "rm: not found");
}

/* New Unix-ish commands */
static void shell_cmd_echo(int argc, char *argv[]) {
    char line[SHELL_LINE_LEN];
    size_t len = 0;
    int i;
    line[0] = 0;
    for (i = 1; i < argc; ++i) {
        if (i > 1) k_append_ch(line, &len, sizeof(line), ' ');
        k_append_str(line, &len, sizeof(line), argv[i]);
    }
    shell_push_line(line);
}

static bool shell_line_matches_pattern(const char *line, const char *pat) {
    /* Trivial substring match. */
    size_t pat_len = k_strlen(pat);
    size_t line_len = k_strlen(line);
    size_t i;
    if (pat_len == 0) return true;
    if (pat_len > line_len) return false;
    for (i = 0; i + pat_len <= line_len; ++i) {
        size_t j;
        bool ok = true;
        for (j = 0; j < pat_len; ++j) {
            if (line[i + j] != pat[j]) { ok = false; break; }
        }
        if (ok) return true;
    }
    return false;
}

static void shell_cmd_grep(int argc, char *argv[]) {
    const uint8_t *data; uint32_t size; uint32_t i; size_t ll;
    char line[SHELL_LINE_LEN];
    int matches = 0;
    if (argc < 3) { shell_push_line("usage: grep <pattern> <file>"); return; }
    if (!vfs_read(argv[2], &data, &size)) { shell_push_line("grep: not found"); return; }
    ll = 0; line[0] = 0;
    for (i = 0; i < size; ++i) {
        char c = (char)data[i];
        if (c == '\r') continue;
        if (c == '\n' || ll + 2 >= sizeof(line)) {
            line[ll] = 0;
            if (shell_line_matches_pattern(line, argv[1])) {
                shell_push_line(line); ++matches;
            }
            ll = 0; line[0] = 0;
            continue;
        }
        line[ll++] = c; line[ll] = 0;
    }
    if (ll > 0 && shell_line_matches_pattern(line, argv[1])) {
        shell_push_line(line); ++matches;
    }
    if (matches == 0) shell_push_line("(no matches)");
}

static void shell_cmd_head_tail(int argc, char *argv[], bool is_tail) {
    int n = 10;
    const char *path = NULL;
    int i;
    const uint8_t *data; uint32_t size;
    char lines_buf[64][SHELL_LINE_LEN];
    size_t ll = 0;
    int total = 0, start, end;
    char line[SHELL_LINE_LEN];
    uint32_t idx;

    if (argc < 2) { shell_push_line("usage: head|tail [-n N] <file>"); return; }
    for (i = 1; i < argc; ++i) {
        if (k_strcmp(argv[i], "-n") == 0 && i + 1 < argc) { n = k_atoi(argv[++i]); }
        else path = argv[i];
    }
    if (!path) { shell_push_line("usage: head|tail [-n N] <file>"); return; }
    if (n <= 0) n = 10;
    if (n > 64) n = 64;
    if (!vfs_read(path, &data, &size)) { shell_push_line("not found"); return; }

    line[0] = 0;
    for (idx = 0; idx < size; ++idx) {
        char c = (char)data[idx];
        if (c == '\r') continue;
        if (c == '\n' || ll + 2 >= sizeof(line)) {
            line[ll] = 0;
            if (total < 64) k_strlcpy(lines_buf[total], line, SHELL_LINE_LEN);
            else {
                int kk;
                for (kk = 1; kk < 64; ++kk)
                    k_strlcpy(lines_buf[kk - 1], lines_buf[kk], SHELL_LINE_LEN);
                k_strlcpy(lines_buf[63], line, SHELL_LINE_LEN);
            }
            ++total;
            ll = 0; line[0] = 0;
            continue;
        }
        line[ll++] = c; line[ll] = 0;
    }
    if (ll > 0) {
        if (total < 64) k_strlcpy(lines_buf[total], line, SHELL_LINE_LEN);
        ++total;
    }

    if (total > 64) total = 64;
    if (is_tail) { start = total - n; if (start < 0) start = 0; end = total; }
    else         { start = 0; end = n < total ? n : total; }
    for (i = start; i < end; ++i) shell_push_line(lines_buf[i]);
}

static void shell_cmd_wc(const char *path) {
    const uint8_t *data; uint32_t size;
    uint32_t i;
    uint32_t lines = 0, words = 0, bytes = 0;
    bool in_word = false;
    char out[64];
    size_t len = 0;
    if (!vfs_read(path, &data, &size)) { shell_push_line("wc: not found"); return; }
    bytes = size;
    for (i = 0; i < size; ++i) {
        char c = (char)data[i];
        if (c == '\n') ++lines;
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') in_word = false;
        else if (!in_word) { in_word = true; ++words; }
    }
    out[0] = 0;
    k_append_u32(out, &len, sizeof(out), lines);
    k_append_str(out, &len, sizeof(out), " lines  ");
    k_append_u32(out, &len, sizeof(out), words);
    k_append_str(out, &len, sizeof(out), " words  ");
    k_append_u32(out, &len, sizeof(out), bytes);
    k_append_str(out, &len, sizeof(out), " bytes  ");
    k_append_str(out, &len, sizeof(out), path);
    shell_push_line(out);
}

static void shell_cmd_find(const char *pattern) {
    size_t i; int matches = 0;
    for (i = 0; i < VFS_MAX_FILES; ++i) {
        if (g_vfs_files[i].used) {
            if (shell_line_matches_pattern(g_vfs_files[i].path, pattern)) {
                shell_push_line(g_vfs_files[i].path);
                ++matches;
            }
        }
    }
    if (matches == 0) shell_push_line("find: no matches");
}

static void shell_cmd_hexdump(const char *path) {
    const uint8_t *data; uint32_t size; uint32_t off;
    if (!vfs_read(path, &data, &size)) { shell_push_line("hexdump: not found"); return; }
    if (size == 0) { shell_push_line("(empty)"); return; }
    for (off = 0; off < size; off += 16) {
        char line[SHELL_LINE_LEN];
        size_t len = 0;
        uint32_t j;
        line[0] = 0;
        k_append_hex(line, &len, sizeof(line), off, 6);
        k_append_str(line, &len, sizeof(line), "  ");
        for (j = 0; j < 16; ++j) {
            if (off + j < size) k_append_hex(line, &len, sizeof(line), data[off + j], 2);
            else                k_append_str(line, &len, sizeof(line), "  ");
            k_append_ch(line, &len, sizeof(line), ' ');
        }
        k_append_ch(line, &len, sizeof(line), '|');
        for (j = 0; j < 16 && off + j < size; ++j) {
            char c = (char)data[off + j];
            if (c < 32 || c > 126) c = '.';
            k_append_ch(line, &len, sizeof(line), c);
        }
        k_append_ch(line, &len, sizeof(line), '|');
        shell_push_line(line);
        if (off > 512) { shell_push_line("... (truncated)"); return; }
    }
}

static void shell_cmd_cp(const char *src, const char *dst) {
    const uint8_t *data; uint32_t size;
    char buf[UI_FILE_BUF_MAX + 1];
    uint32_t copy;
    if (!vfs_read(src, &data, &size)) { shell_push_line("cp: src not found"); return; }
        copy = size > UI_FILE_BUF_MAX ? UI_FILE_BUF_MAX : size;
    k_memcpy(buf, data, copy);
    buf[copy] = 0;
    if (!vfs_write(dst, buf)) { shell_push_line("cp: write failed"); return; }
    shell_push_line("cp: ok");
}

static void shell_cmd_mv(const char *src, const char *dst) {
    shell_cmd_cp(src, dst);
    if (!vfs_remove(src)) { shell_push_line("mv: remove src failed"); return; }
    shell_push_line("mv: ok");
}

static void shell_cmd_mkdir(const char *p) {
    /* We don't have dirs as first-class, but placeholder file marks it. */
    char marker[VFS_MAX_PATH + 16];
    size_t len = 0;
    marker[0] = 0;
    k_append_str(marker, &len, sizeof(marker), p);
    k_append_str(marker, &len, sizeof(marker), "/.keep");
    shell_push_line(vfs_touch(marker) ? "mkdir: ok (flat)" : "mkdir: failed");
}

static void shell_cmd_uptime(void) {
    char line[64];
    size_t len = 0;
    uint32_t secs = (uint32_t)(g_pit_ticks / 100u);
    uint32_t mins = secs / 60u;
    uint32_t hours = mins / 60u;
    line[0] = 0;
    k_append_str(line, &len, sizeof(line), "up ");
    k_append_u32(line, &len, sizeof(line), hours);
    k_append_ch(line, &len, sizeof(line), 'h');
    k_append_u32(line, &len, sizeof(line), mins % 60u);
    k_append_ch(line, &len, sizeof(line), 'm');
    k_append_u32(line, &len, sizeof(line), secs % 60u);
    k_append_str(line, &len, sizeof(line), "s   PIT=");
    k_append_u32(line, &len, sizeof(line), (uint32_t)g_pit_ticks);
    k_append_str(line, &len, sizeof(line), "t   procs=");
    k_append_u32(line, &len, sizeof(line), (uint32_t)proc_count());
    shell_push_line(line);
}

static void shell_cmd_free(void) {
    char line[96];
    size_t len = 0;
    uint32_t used, peak, allocs, frees;
    heap_stats(&used, &peak, &allocs, &frees);
    line[0] = 0;
    k_append_str(line, &len, sizeof(line), "heap ");
    k_append_u32(line, &len, sizeof(line), used / 1024u);
    k_append_str(line, &len, sizeof(line), "K used / ");
    k_append_u32(line, &len, sizeof(line), HEAP_SIZE / 1024u);
    k_append_str(line, &len, sizeof(line), "K total  peak=");
    k_append_u32(line, &len, sizeof(line), peak / 1024u);
    k_append_str(line, &len, sizeof(line), "K  allocs=");
    k_append_u32(line, &len, sizeof(line), allocs);
    k_append_str(line, &len, sizeof(line), " frees=");
    k_append_u32(line, &len, sizeof(line), frees);
    shell_push_line(line);
}

static void shell_cmd_df(void) {
    char line[96];
    size_t len = 0;
    uint32_t used_files = (uint32_t)vfs_count();
    line[0] = 0;
    k_append_str(line, &len, sizeof(line), "riduxfs  ");
    k_append_u32(line, &len, sizeof(line), used_files);
    k_append_str(line, &len, sizeof(line), " / ");
    k_append_u32(line, &len, sizeof(line), (uint32_t)VFS_MAX_FILES);
    k_append_str(line, &len, sizeof(line), " inodes  rw_pool=");
    k_append_u32(line, &len, sizeof(line), (uint32_t)VFS_RW_MAX);
    k_append_str(line, &len, sizeof(line), "B/file  dropped=");
    k_append_u32(line, &len, sizeof(line), g_vfs_mount_dropped_slots + g_vfs_mount_dropped_paths);
    shell_push_line(line);
}

static void shell_cmd_mem(void) {
    char line[96];
    size_t len = 0;
    uint32_t used, peak, allocs, frees;
    heap_stats(&used, &peak, &allocs, &frees);
    line[0] = 0;
    k_append_str(line, &len, sizeof(line), "kernel.bin ~");
    k_append_u32(line, &len, sizeof(line), 900u);
    k_append_str(line, &len, sizeof(line), "K  backbuffer=");
    k_append_u32(line, &len, sizeof(line), (uint32_t)(FB_MAX_WIDTH * FB_MAX_HEIGHT * 4u / 1024u));
    k_append_str(line, &len, sizeof(line), "K  bgcache=");
    k_append_u32(line, &len, sizeof(line), (uint32_t)(FB_MAX_WIDTH * FB_MAX_HEIGHT * 4u / 1024u));
    k_append_str(line, &len, sizeof(line), "K  heap=");
    k_append_u32(line, &len, sizeof(line), used / 1024u);
    k_append_str(line, &len, sizeof(line), "K/");
    k_append_u32(line, &len, sizeof(line), HEAP_SIZE / 1024u);
    k_append_str(line, &len, sizeof(line), "K");
    shell_push_line(line);
}

static void shell_cmd_kernlog(void) {
    uint32_t cnt = klog_ring_count();
    uint32_t i;
    if (cnt == 0) { shell_push_line("(no kernel log)"); return; }
    /* Print oldest-first. */
    for (i = cnt; i > 0; --i) {
        const char *l = klog_ring_get(i - 1);
        if (l && l[0]) shell_push_line(l);
    }
}

static void shell_cmd_irqs(void) {
    int i;
    for (i = 0; i < 16; ++i) {
        char line[32];
        size_t len = 0;
        line[0] = 0;
        k_append_str(line, &len, sizeof(line), "IRQ ");
        k_append_u32(line, &len, sizeof(line), (uint32_t)i);
        k_append_str(line, &len, sizeof(line), ": ");
        k_append_u32(line, &len, sizeof(line), g_irq_counts[i]);
        shell_push_line(line);
    }
}

static void shell_cmd_heap(void) {
    heap_block_t *b;
    int i = 0;
    for (b = g_heap_head; b && i < 20; b = b->next, ++i) {
        char line[64];
        size_t len = 0;
        line[0] = 0;
        k_append_str(line, &len, sizeof(line), b->used ? "USED " : "FREE ");
        k_append_u32(line, &len, sizeof(line), b->size);
        k_append_str(line, &len, sizeof(line), "B @0x");
        k_append_hex(line, &len, sizeof(line), (uint32_t)(uintptr_t)b, 8);
        shell_push_line(line);
    }
}

static void shell_cmd_date(void) {
    char line[64];
    size_t len = 0;
    const char *days[] = { "Sun","Mon","Tue","Wed","Thu","Fri","Sat" };
    const char *mons[] = { "Jan","Feb","Mar","Apr","May","Jun",
                           "Jul","Aug","Sep","Oct","Nov","Dec" };
    int mi = (g_rtc_now.month >= 1 && g_rtc_now.month <= 12) ? g_rtc_now.month - 1 : 0;
    int di = g_rtc_now.dow % 7;
    line[0] = 0;
    k_append_str(line, &len, sizeof(line), days[di]);
    k_append_ch(line, &len, sizeof(line), ' ');
    k_append_str(line, &len, sizeof(line), mons[mi]);
    k_append_ch(line, &len, sizeof(line), ' ');
    k_append_u32_pad(line, &len, sizeof(line), (uint32_t)g_rtc_now.day, 2, '0');
    k_append_ch(line, &len, sizeof(line), ' ');
    k_append_u32_pad(line, &len, sizeof(line), (uint32_t)g_rtc_now.hour, 2, '0');
    k_append_ch(line, &len, sizeof(line), ':');
    k_append_u32_pad(line, &len, sizeof(line), (uint32_t)g_rtc_now.minute, 2, '0');
    k_append_ch(line, &len, sizeof(line), ':');
    k_append_u32_pad(line, &len, sizeof(line), (uint32_t)g_rtc_now.second, 2, '0');
    k_append_ch(line, &len, sizeof(line), ' ');
    k_append_u32(line, &len, sizeof(line), g_rtc_now.year);
    shell_push_line(line);
}

static void shell_cmd_pstree(void) {
    int i;
    shell_push_line("PID NAME              STATE    CPU");
    for (i = 0; i < PROC_MAX; ++i) {
        char line[SHELL_LINE_LEN];
        size_t len = 0;
        if (!g_processes[i].used) continue;
        line[0] = 0;
        /* Indent children */
        if (i > 0) k_append_str(line, &len, sizeof(line), " |- ");
        k_append_u32_pad(line, &len, sizeof(line), (uint32_t)g_processes[i].pid, 3, ' ');
        k_append_ch(line, &len, sizeof(line), ' ');
        k_append_str(line, &len, sizeof(line), g_processes[i].name);
        while (len < 32 && len + 1 < sizeof(line)) line[len++] = ' ';
        line[len] = 0;
        k_append_str(line, &len, sizeof(line), proc_state_name(g_processes[i].state));
        k_append_ch(line, &len, sizeof(line), ' ');
        k_append_u32(line, &len, sizeof(line), g_processes[i].cpu_ticks);
        k_append_ch(line, &len, sizeof(line), 't');
        shell_push_line(line);
    }
}

static void shell_cmd_top(void) {
    /* Show top CPU hogs (sorted snapshot). */
    int indices[PROC_MAX];
    int n = 0, i, j;
    for (i = 0; i < PROC_MAX; ++i) if (g_processes[i].used) indices[n++] = i;
    for (i = 0; i < n - 1; ++i) {
        for (j = 0; j < n - 1 - i; ++j) {
            if (g_processes[indices[j]].cpu_ticks < g_processes[indices[j + 1]].cpu_ticks) {
                int tmp = indices[j]; indices[j] = indices[j + 1]; indices[j + 1] = tmp;
            }
        }
    }
    shell_push_line("PID CPU     NAME");
    for (i = 0; i < n && i < 10; ++i) {
        char line[SHELL_LINE_LEN];
        size_t len = 0;
        int k = indices[i];
        line[0] = 0;
        k_append_u32_pad(line, &len, sizeof(line), (uint32_t)g_processes[k].pid, 3, ' ');
        k_append_ch(line, &len, sizeof(line), ' ');
        k_append_u32_pad(line, &len, sizeof(line), g_processes[k].cpu_ticks, 6, ' ');
        k_append_str(line, &len, sizeof(line), "  ");
        k_append_str(line, &len, sizeof(line), g_processes[k].name);
        shell_push_line(line);
    }
}

static void shell_cmd_sleep(int argc, char *argv[]) {
    uint32_t ms = 500;
    uint32_t start;
    if (argc >= 2) { int v = k_atoi(argv[1]); if (v > 0) ms = (uint32_t)v; }
    start = rdtsc32();
    while ((uint32_t)(rdtsc32() - start) < anim_ms_to_tsc(ms)) {
        __asm__ __volatile__("pause");
    }
    shell_push_line("slept");
}

static void shell_cmd_write_raw(const char *cmd) {
    char path[VFS_MAX_PATH];
    size_t path_len = 0;
    const char *p = cmd;
    while (*p && !k_is_space(*p)) ++p;
    while (*p && k_is_space(*p)) ++p;
    if (!*p) { shell_push_line("write: missing path"); return; }
    while (*p && !k_is_space(*p) && path_len + 1 < sizeof(path)) path[path_len++] = *p++;
    path[path_len] = 0;
    while (*p && k_is_space(*p)) ++p;
    if (!*p) { shell_push_line("write: missing content"); return; }
    shell_push_line(vfs_write(path, p) ? "write: ok" : "write: failed");
}
static void shell_cmd_ps(void) {
    int i;
    for (i = 0; i < PROC_MAX; ++i) {
        if (g_processes[i].used) {
            char line[SHELL_LINE_LEN];
            size_t len = 0;
            line[0] = 0;
            k_append_u32(line, &len, sizeof(line), (uint32_t)g_processes[i].pid);
            k_append_str(line, &len, sizeof(line), " ");
            k_append_str(line, &len, sizeof(line), proc_state_name(g_processes[i].state));
            k_append_str(line, &len, sizeof(line), " ");
            k_append_u32(line, &len, sizeof(line), g_processes[i].cpu_ticks);
            k_append_str(line, &len, sizeof(line), "t ");
            k_append_str(line, &len, sizeof(line), g_processes[i].is_user ? "USR" : "KRN");
            k_append_str(line, &len, sizeof(line), " ");
            k_append_str(line, &len, sizeof(line), g_processes[i].name);
            shell_push_line(line);
        }
    }
}
static void shell_cmd_spawn(const char *n) {
    int pid = proc_spawn(n);
    char line[48]; size_t len = 0;
    if (pid < 0) { shell_push_line("spawn: no slots"); return; }
    line[0] = 0;
    k_append_str(line, &len, sizeof(line), "spawned pid ");
    k_append_u32(line, &len, sizeof(line), (uint32_t)pid);
    shell_push_line(line);
}

/* Launch a Ring 3 ELF directly via compat5's background user-mode
 * spawner. Equivalent to picking the same path in the Ring 3 Demo
 * applet, but reachable from the terminal. */
static void shell_cmd_r3(const char *path) {
    char detail[160];
    char head[96];
    int rc;
    size_t hl = 0;
    detail[0] = 0;
    rc = compat5_spawn_user_elf_background(path, detail, sizeof(detail));
    head[0] = 0;
    k_append_str(head, &hl, sizeof(head), "r3 spawn rc=");
    k_append_i32(head, &hl, sizeof(head), rc);
    k_append_str(head, &hl, sizeof(head), " ");
    k_append_str(head, &hl, sizeof(head), path);
    shell_push_line(head);
    if (detail[0]) shell_push_line(detail);
}
static void shell_cmd_kill(const char *pids) {
    int pid = k_atoi(pids);
    if (pid <= 0) { shell_push_line("kill: bad pid"); return; }
    shell_push_line(proc_kill(pid) ? "kill: done" : "kill: not found");
}
static void shell_cmd_drivers(void) {
    int i;
    for (i = 0; i < g_driver_count; ++i) {
        char line[SHELL_LINE_LEN];
        size_t len = 0;
        line[0] = 0;
        k_append_str(line, &len, sizeof(line), g_drivers[i].ready ? "[OK] " : "[--] ");
        k_append_str(line, &len, sizeof(line), g_drivers[i].name);
        k_append_str(line, &len, sizeof(line), " (");
        k_append_str(line, &len, sizeof(line), driver_kind_name(g_drivers[i].kind));
        k_append_str(line, &len, sizeof(line), ")  ");
        if (g_drivers[i].vendor) k_append_str(line, &len, sizeof(line), g_drivers[i].vendor);
        shell_push_line(line);
    }
}
static void shell_cmd_pci(void) {
    int i;
    if (g_pci_device_count == 0) { shell_push_line("pci: no devices enumerated"); return; }
    for (i = 0; i < g_pci_device_count; ++i) {
        char line[SHELL_LINE_LEN];
        size_t len = 0;
        line[0] = 0;
        k_append_u32(line, &len, sizeof(line), g_pci_devices[i].bus);
        k_append_str(line, &len, sizeof(line), ":");
        k_append_u32(line, &len, sizeof(line), g_pci_devices[i].slot);
        k_append_str(line, &len, sizeof(line), ".");
        k_append_u32(line, &len, sizeof(line), g_pci_devices[i].func);
        k_append_str(line, &len, sizeof(line), "  VID:");
        k_append_hex(line, &len, sizeof(line), g_pci_devices[i].vendor_id, 4);
        k_append_str(line, &len, sizeof(line), " DID:");
        k_append_hex(line, &len, sizeof(line), g_pci_devices[i].device_id, 4);
        k_append_str(line, &len, sizeof(line), " class:");
        k_append_hex(line, &len, sizeof(line), g_pci_devices[i].class_code, 2);
        shell_push_line(line);
    }
}
static void shell_cmd_gpu(void) {
    char line[SHELL_LINE_LEN];
    size_t len = 0;
    line[0] = 0;
    k_append_str(line, &len, sizeof(line), "gpu: ");
    k_append_str(line, &len, sizeof(line), g_gpu_accel_kind);
    shell_push_line(line);

    len = 0; line[0] = 0;
    k_append_str(line, &len, sizeof(line), "hw: ");
    k_append_str(line, &len, sizeof(line), g_gpu_hw_present ? "display PCI presente" : "sin PCI display detectado");
    k_append_str(line, &len, sizeof(line), " | accel: ");
    k_append_str(line, &len, sizeof(line), g_gpu_accel_enabled ? "dirty blit BGRA activo" : "fallback software");
    shell_push_line(line);

    len = 0; line[0] = 0;
    k_append_str(line, &len, sizeof(line), "present: ");
    k_append_str(line, &len, sizeof(line), ridux_gpu_present_backend());
    k_append_str(line, &len, sizeof(line), ridux_gpu_real_present_available() ? " | real GPU activo" : " | CPU fallback");
    shell_push_line(line);

    len = 0; line[0] = 0;
    k_append_str(line, &len, sizeof(line), "fb: ");
    k_append_u32(line, &len, sizeof(line), g_fb.width);
    k_append_ch(line, &len, sizeof(line), 'x');
    k_append_u32(line, &len, sizeof(line), g_fb.height);
    k_append_str(line, &len, sizeof(line), " pitch=");
    k_append_u32(line, &len, sizeof(line), g_fb.pitch);
    k_append_str(line, &len, sizeof(line), g_fb_fast_bgra ? " BGRA-fast" : " converted");
    shell_push_line(line);
}
static void shell_cmd_time(void) {
    char line[48];
    size_t len = 0;
    line[0] = 0;
    k_append_u32_pad(line, &len, sizeof(line), g_rtc_now.year, 4, '0');
    k_append_str(line, &len, sizeof(line), "-");
    k_append_u32_pad(line, &len, sizeof(line), g_rtc_now.month, 2, '0');
    k_append_str(line, &len, sizeof(line), "-");
    k_append_u32_pad(line, &len, sizeof(line), g_rtc_now.day, 2, '0');
    k_append_str(line, &len, sizeof(line), " ");
    k_append_u32_pad(line, &len, sizeof(line), g_rtc_now.hour, 2, '0');
    k_append_str(line, &len, sizeof(line), ":");
    k_append_u32_pad(line, &len, sizeof(line), g_rtc_now.minute, 2, '0');
    k_append_str(line, &len, sizeof(line), ":");
    k_append_u32_pad(line, &len, sizeof(line), g_rtc_now.second, 2, '0');
    shell_push_line(line);
}

static void shell_cmd_smp(void) {
    char line[SHELL_LINE_LEN];
    size_t len = 0;
    line[0] = 0;
    k_append_str(line, &len, sizeof(line), "smp topo: ");
    k_append_str(line, &len, sizeof(line), g_smp_topology_detected ? "detectado" : "no detectado");
    k_append_str(line, &len, sizeof(line), " | logical: ");
    k_append_u32(line, &len, sizeof(line), g_cpu_logical_count);
    k_append_str(line, &len, sizeof(line), " | cores: ");
    k_append_u32(line, &len, sizeof(line), g_cpu_core_count);
    k_append_str(line, &len, sizeof(line), " | smt: ");
    k_append_u32(line, &len, sizeof(line), g_cpu_threads_per_core);
    k_append_str(line, &len, sizeof(line), " | online: ");
    k_append_u32(line, &len, sizeof(line), g_cpu_online_count);
    k_append_str(line, &len, sizeof(line), " | src: ");
    k_append_str(line, &len, sizeof(line), g_madt_detected ? "ACPI MADT" : "CPUID");
    shell_push_line(line);

    len = 0; line[0] = 0;
    k_append_str(line, &len, sizeof(line), "bsp apic: ");
    k_append_u32(line, &len, sizeof(line), g_bsp_apic_id);
    k_append_str(line, &len, sizeof(line), " | lapic: ");
    k_append_str(line, &len, sizeof(line), g_lapic_present ? "yes" : "no");
    k_append_str(line, &len, sizeof(line), " | x2apic: ");
    k_append_str(line, &len, sizeof(line), g_x2apic_present ? "yes" : "no");
    k_append_str(line, &len, sizeof(line), " | base low32=0x");
    k_append_hex(line, &len, sizeof(line), (uint32_t)g_lapic_base, 8);
    shell_push_line(line);

    if (g_madt_detected) {
        len = 0; line[0] = 0;
        k_append_str(line, &len, sizeof(line), "madt: lapic=0x");
        k_append_hex(line, &len, sizeof(line), g_madt_lapic_addr, 8);
        k_append_str(line, &len, sizeof(line), " cpus=");
        k_append_u32(line, &len, sizeof(line), g_madt_cpu_count);
        k_append_str(line, &len, sizeof(line), " ioapic=");
        k_append_u32(line, &len, sizeof(line), g_madt_ioapic_count);
        shell_push_line(line);
    }

    len = 0; line[0] = 0;
    k_append_str(line, &len, sizeof(line), "scheduler: BSP-only; siguiente paso real es trampoline AP + INIT/SIPI.");
    shell_push_line(line);
}
static void shell_cmd_elf_list(void) {
    int i; size_t n = 0;
    for (i = 0; i < ELF_MAX_IMAGES; ++i) {
        if (g_elf_images[i].used) {
            char line[SHELL_LINE_LEN];
            size_t len = 0;
            line[0] = 0;
            k_append_str(line, &len, sizeof(line), "[");
            k_append_u32(line, &len, sizeof(line), (uint32_t)i);
            k_append_str(line, &len, sizeof(line), "] ");
            k_append_str(line, &len, sizeof(line), g_elf_images[i].path);
            k_append_str(line, &len, sizeof(line), "  sz:");
            k_append_u32(line, &len, sizeof(line), g_elf_images[i].mem_size);
            shell_push_line(line);
            ++n;
        }
    }
    if (!n) shell_push_line("elf: no images loaded");
}
static void shell_cmd_elf_load(const char *p) {
    int slot = -1;
    elf_load_result_t r = elf_load_from_vfs(p, &slot);
    if (r == ELF_LOAD_OK) {
        char line[64]; size_t len = 0;
        line[0] = 0;
        k_append_str(line, &len, sizeof(line), "elf: loaded slot ");
        k_append_u32(line, &len, sizeof(line), (uint32_t)slot);
        shell_push_line(line);
        return;
    }
    if (r == ELF_LOAD_NOTFOUND)      shell_push_line("elf: file not found");
    else if (r == ELF_LOAD_BAD_FORMAT) shell_push_line("elf: invalid ELF32");
    else if (r == ELF_LOAD_TOO_LARGE)  shell_push_line("elf: image too large");
    else                               shell_push_line("elf: no loader slot");
}
static void shell_cmd_elf_run(const char *p) {
    int slot = -1;
    elf_load_result_t r = elf_load_from_vfs(p, &slot);
    int pid;
    if (r != ELF_LOAD_OK) { shell_cmd_elf_load(p); return; }
    pid = proc_spawn_elf(p, slot);
    if (pid < 0) { shell_push_line("elf: proc table full"); return; }
    {
        char line[64]; size_t len = 0;
        line[0] = 0;
        k_append_str(line, &len, sizeof(line), "elf: running pid ");
        k_append_u32(line, &len, sizeof(line), (uint32_t)pid);
        shell_push_line(line);
    }
}
static void shell_cmd_elf(int argc, char *argv[]) {
    if (argc < 2) { shell_push_line("elf load|run|list"); return; }
    if (k_strcmp(argv[1], "list") == 0) { shell_cmd_elf_list(); return; }
    if (argc < 3) { shell_push_line("elf load|run <path>"); return; }
    if (k_strcmp(argv[1], "load") == 0) shell_cmd_elf_load(argv[2]);
    else if (k_strcmp(argv[1], "run") == 0) shell_cmd_elf_run(argv[2]);
    else shell_push_line("elf: unknown subcmd");
}
static void shell_cmd_wm_list(void) {
    int i;
    for (i = 0; i < g_window_count; ++i) {
        char line[SHELL_LINE_LEN];
        size_t len = 0;
        line[0] = 0;
        k_append_str(line, &len, sizeof(line), i == g_window_focus ? "* " : "  ");
        k_append_u32(line, &len, sizeof(line), (uint32_t)g_windows[i].id);
        k_append_str(line, &len, sizeof(line),
                     g_windows[i].visible ? (g_windows[i].minimized ? " [min] " : " [on] ") : " [off] ");
        k_append_str(line, &len, sizeof(line), g_windows[i].title);
        shell_push_line(line);
    }
}
static void shell_cmd_wm(int argc, char *argv[]) {
    if (argc < 2) { shell_push_line("wm: list|focus|move|drag|next|max|min|close"); return; }
    if (k_strcmp(argv[1], "list") == 0) { shell_cmd_wm_list(); return; }
    if (k_strcmp(argv[1], "next") == 0) { wm_focus_next(); shell_push_line("wm: next"); return; }
    if (k_strcmp(argv[1], "drag") == 0) {
        g_wm_drag_mode = !g_wm_drag_mode;
        shell_push_line(g_wm_drag_mode ? "wm: drag ON" : "wm: drag OFF");
        return;
    }
    if (k_strcmp(argv[1], "focus") == 0) {
        if (argc < 3) { shell_push_line("wm focus <id>"); return; }
        shell_push_line(wm_focus_by_id(k_atoi(argv[2])) ? "wm: focused" : "wm: not found");
        return;
    }
    if (k_strcmp(argv[1], "move") == 0) {
        if (argc < 4) { shell_push_line("wm move <dx> <dy>"); return; }
        shell_push_line(wm_move_focused(k_atoi(argv[2]), k_atoi(argv[3])) ? "wm: moved" : "wm: no focus");
        return;
    }
    if (k_strcmp(argv[1], "max") == 0) {
        if (g_window_focus >= 0) { wm_maximize_toggle(g_windows[g_window_focus].id); shell_push_line("wm: max"); }
        return;
    }
    if (k_strcmp(argv[1], "min") == 0) {
        if (g_window_focus >= 0) { wm_minimize(g_windows[g_window_focus].id); shell_push_line("wm: min"); }
        return;
    }
    if (k_strcmp(argv[1], "close") == 0) {
        if (g_window_focus >= 0) { wm_close(g_windows[g_window_focus].id); shell_push_line("wm: close"); }
        return;
    }
    shell_push_line("wm: unknown subcmd");
}
static void shell_cmd_apps(void) {
    int i;
    for (i = 0; i < g_app_count; ++i) {
        char line[SHELL_LINE_LEN];
        size_t len = 0;
        line[0] = 0;
        k_append_str(line, &len, sizeof(line), app_is_running(g_apps[i].app_id) ? "[on] " : "[off] ");
        k_append_str(line, &len, sizeof(line), g_apps[i].name);
        k_append_str(line, &len, sizeof(line), "  (");
        k_append_str(line, &len, sizeof(line), g_apps[i].category);
        k_append_str(line, &len, sizeof(line), ")");
        shell_push_line(line);
    }
}
static void shell_cmd_app_visible(const char *name, bool visible) {
    int idx = app_index_by_name(name);
    int id;
    if (idx < 0) { shell_push_line("app: unknown"); return; }
    if (visible && app_launch_user_process_for_id(g_apps[idx].app_id)) {
        shell_push_line("app: Ring 3 launched");
        return;
    }
    id = g_apps[idx].window_id;
    if (id <= 0) { shell_push_line("app: no window"); return; }
    wm_set_visible_by_id(id, visible);
    if (visible) wm_focus_by_id(id);
    shell_push_line(visible ? "app: opened" : "app: closed");
}
static void shell_cycle_theme(void) {
    g_theme_index = (g_theme_index + 1u) % (uint32_t)THEME_COUNT;
    shell_push_line("theme: switched");
}

static bool net_parse_ip(const char *s, uint8_t out[4]) {
    int dots = 0, octet = 0;
    int i = 0, out_i = 0;
    while (s[i]) {
        if (s[i] >= '0' && s[i] <= '9') {
            octet = octet * 10 + (s[i] - '0');
            if (octet > 255) return false;
        } else if (s[i] == '.') {
            if (out_i >= 4) return false;
            out[out_i++] = (uint8_t)octet;
            octet = 0;
            ++dots;
        } else return false;
        ++i;
    }
    if (out_i != 3 || dots != 3) return false;
    out[3] = (uint8_t)octet;
    return true;
}
static bool net_arp_lookup(const uint8_t ip[4], uint8_t mac[6]) {
    int i;
    for (i = 0; i < 8; ++i) {
        if (g_arp_table[i].used &&
            k_memcmp_bytes(g_arp_table[i].ip, ip, 4) == 0) {
            k_memcpy(mac, g_arp_table[i].mac, 6);
            return true;
        }
    }
    return false;
}

/* Very fake ping: if the IP is reachable (in ARP table) return success
 * with rand-ish latency derived from RTC. */
static void shell_cmd_ping(const char *target) {
    uint8_t ip[4];
    uint8_t mac[6];
    int i;
    if (!net_parse_ip(target, ip)) {
        /* Try DNS */
        for (i = 0; i < DNS_COUNT; ++i) {
            if (k_strcmp(g_dns[i].name, target) == 0) {
                k_memcpy(ip, g_dns[i].ip, 4);
                break;
            }
        }
        if (i == DNS_COUNT) { shell_push_line("ping: unknown host"); return; }
    }
    if (!net_arp_lookup(ip, mac)) {
        shell_push_line("ping: host unreachable (no route)");
        return;
    }
    for (i = 0; i < 4; ++i) {
        char line[SHELL_LINE_LEN];
        size_t len = 0;
        char ip_buf[16], mac_buf[24];
        uint32_t rtt_us;
        net_format_ip(ip, ip_buf, sizeof(ip_buf));
        net_format_mac(mac, mac_buf, sizeof(mac_buf));
        rtt_us = 120u + (uint32_t)(g_rtc_now.second + i) * 11u;
        line[0] = 0;
        k_append_str(line, &len, sizeof(line), "64 bytes from ");
        k_append_str(line, &len, sizeof(line), ip_buf);
        k_append_str(line, &len, sizeof(line), " (");
        k_append_str(line, &len, sizeof(line), mac_buf);
        k_append_str(line, &len, sizeof(line), "): icmp_seq=");
        k_append_u32(line, &len, sizeof(line), (uint32_t)(i + 1));
        k_append_str(line, &len, sizeof(line), " time=");
        k_append_u32(line, &len, sizeof(line), rtt_us / 1000u);
        k_append_ch(line, &len, sizeof(line), '.');
        k_append_u32_pad(line, &len, sizeof(line), rtt_us % 1000u, 3, '0');
        k_append_str(line, &len, sizeof(line), " ms");
        shell_push_line(line);
        g_net_eth0.tx_packets++;
        g_net_eth0.rx_packets++;
        g_net_eth0.tx_bytes += 64;
        g_net_eth0.rx_bytes += 64;
    }
    shell_push_line("--- ping stats ---  4 packets transmitted, 4 received");
}

static void shell_cmd_ifconfig(void) {
    char buf[64];
    char ip[16], mask[16], gw[16], mac[24];
    net_format_ip(g_net_lo.ip, ip, sizeof(ip));
    net_format_ip(g_net_lo.netmask, mask, sizeof(mask));
    net_format_mac(g_net_lo.mac, mac, sizeof(mac));
    shell_push_line("lo   LOOPBACK,UP  mtu 65536");
    {
        size_t l = 0; buf[0] = 0;
        k_append_str(buf, &l, sizeof(buf), "     inet ");
        k_append_str(buf, &l, sizeof(buf), ip);
        k_append_str(buf, &l, sizeof(buf), "  mask ");
        k_append_str(buf, &l, sizeof(buf), mask);
        shell_push_line(buf);
    }
    {
        size_t l = 0; buf[0] = 0;
        k_append_str(buf, &l, sizeof(buf), "     RX packets ");
        k_append_u32(buf, &l, sizeof(buf), g_net_lo.rx_packets);
        k_append_str(buf, &l, sizeof(buf), "  TX packets ");
        k_append_u32(buf, &l, sizeof(buf), g_net_lo.tx_packets);
        shell_push_line(buf);
    }
    net_format_ip(g_net_eth0.ip, ip, sizeof(ip));
    net_format_ip(g_net_eth0.netmask, mask, sizeof(mask));
    net_format_ip(g_net_eth0.gateway, gw, sizeof(gw));
    net_format_mac(g_net_eth0.mac, mac, sizeof(mac));
    shell_push_line("eth0 BROADCAST,MULTICAST,UP  mtu 1500");
    {
        size_t l = 0; buf[0] = 0;
        k_append_str(buf, &l, sizeof(buf), "     ether ");
        k_append_str(buf, &l, sizeof(buf), mac);
        shell_push_line(buf);
        l = 0; buf[0] = 0;
        k_append_str(buf, &l, sizeof(buf), "     inet ");
        k_append_str(buf, &l, sizeof(buf), ip);
        k_append_str(buf, &l, sizeof(buf), "  mask ");
        k_append_str(buf, &l, sizeof(buf), mask);
        k_append_str(buf, &l, sizeof(buf), "  gw ");
        k_append_str(buf, &l, sizeof(buf), gw);
        shell_push_line(buf);
        l = 0; buf[0] = 0;
        k_append_str(buf, &l, sizeof(buf), "     RX packets ");
        k_append_u32(buf, &l, sizeof(buf), g_net_eth0.rx_packets);
        k_append_str(buf, &l, sizeof(buf), "  TX packets ");
        k_append_u32(buf, &l, sizeof(buf), g_net_eth0.tx_packets);
        k_append_str(buf, &l, sizeof(buf), "  RX bytes ");
        k_append_u32(buf, &l, sizeof(buf), g_net_eth0.rx_bytes);
        shell_push_line(buf);
    }
}

static void shell_cmd_arp(void) {
    int i;
    shell_push_line("Address          HW Address        Flags");
    for (i = 0; i < 8; ++i) {
        char ip[16], mac[24], line[SHELL_LINE_LEN];
        size_t l = 0;
        if (!g_arp_table[i].used) continue;
        net_format_ip(g_arp_table[i].ip, ip, sizeof(ip));
        net_format_mac(g_arp_table[i].mac, mac, sizeof(mac));
        line[0] = 0;
        k_append_str(line, &l, sizeof(line), ip);
        while (l < 17 && l + 1 < sizeof(line)) line[l++] = ' ';
        line[l] = 0;
        k_append_str(line, &l, sizeof(line), mac);
        k_append_str(line, &l, sizeof(line), "   C");
        shell_push_line(line);
    }
}

static void shell_cmd_dns(const char *host) {
    int i;
    for (i = 0; i < DNS_COUNT; ++i) {
        if (k_strcmp(g_dns[i].name, host) == 0) {
            char line[64]; size_t l = 0;
            char ip[16];
            net_format_ip(g_dns[i].ip, ip, sizeof(ip));
            line[0] = 0;
            k_append_str(line, &l, sizeof(line), host);
            k_append_str(line, &l, sizeof(line), " has address ");
            k_append_str(line, &l, sizeof(line), ip);
            shell_push_line(line);
            return;
        }
    }
    shell_push_line("dns: NXDOMAIN");
}

/* shell_cmd_edit opens the Editor app focused on the given path. */
static void shell_cmd_edit(const char *path) {
    int idx = app_index_by_id(APP_EDITOR);
    int id;
    window_t *w;
    if (idx < 0) { shell_push_line("edit: editor app not registered"); return; }
    id = g_apps[idx].window_id;
    if (id <= 0) { shell_push_line("edit: no editor window"); return; }
    w = wm_get_by_id(id);
    if (w) {
        /* Stash target path into first state_s slot for the app to read. */
        k_strlcpy(w->state_s[0], path, sizeof(w->state_s[0]));
        w->state_i[0] = 0;  /* caret offset */
        w->state_i[1] = 0;  /* dirty flag */
    }
    wm_set_visible_by_id(id, true);
    wm_focus_by_id(id);
    shell_push_line("editor: opened");
}

static void shell_push_multiline_text(const char *text) {
    const char *p = text ? text : "";
    while (*p) {
        char line[SHELL_LINE_LEN];
        size_t ll = 0;
        while (*p && *p != '\n' && ll + 1 < sizeof(line)) {
            line[ll++] = *p++;
        }
        line[ll] = 0;
        if (*p == '\n') ++p;
        if (ll > 0) shell_push_line(line);
    }
}

static void shell_join_args_from(int argc, char *argv[], int first, char *out, size_t cap) {
    size_t n = 0;
    int i;
    if (!out || cap == 0) return;
    out[0] = 0;
    for (i = first; i < argc; ++i) {
        if (!argv[i] || !*argv[i]) continue;
        if (n > 0) k_append_str(out, &n, cap, " ");
        k_append_str(out, &n, cap, argv[i]);
    }
}

static bool shell_try_browser_realrun_candidates(const char *const *paths, int count, const char *extra_args, const char *not_found_msg) {
    int i;
    for (i = 0; i < count; ++i) {
        char compat_out[2048];
        char compat_args[320];
        size_t ca_len = 0;
        bool missing_path;
        bool started_task;

        compat_args[0] = 0;
        k_append_str(compat_args, &ca_len, sizeof(compat_args), "run ");
        k_append_str(compat_args, &ca_len, sizeof(compat_args), paths[i]);
        if (extra_args && *extra_args) {
            k_append_str(compat_args, &ca_len, sizeof(compat_args), " ");
            k_append_str(compat_args, &ca_len, sizeof(compat_args), extra_args);
        }

        __boot_serial_puts("[shell-browser-realrun] path=");
        __boot_serial_puts(paths[i]);
        __boot_serial_puts("\n");
        if (!compat_shell_dispatch("browser", compat_args, compat_out, (int)sizeof(compat_out))) {
            __boot_serial_puts("[shell-browser-realrun] browser command missing\n");
            return false;
        }

        missing_path = k_contains(compat_out, "no existe");
        started_task = k_contains(compat_out, "estado: task creado") ||
                       k_contains(compat_out, "task creado pid=") ||
                       k_contains(compat_out, "estado: task staged");

        if (missing_path) continue;

        shell_push_multiline_text(compat_out);
        return started_task;
    }

    if (not_found_msg && *not_found_msg) shell_push_line(not_found_msg);
    return false;
}

static void shell_cmd_browser_vm(const char *engine) {
    const char *target = (engine && *engine) ? engine : "chromium";
    const char *app = "Browser";
    if (k_strcasecmp_ascii(target, "firefox") == 0) {
        target = "firefox";
        app = "Firefox";
    } else if (k_strcasecmp_ascii(target, "chrome") == 0 ||
               k_strcasecmp_ascii(target, "google-chrome") == 0) {
        target = "chromium";
    } else if (k_strcasecmp_ascii(target, "chromium") != 0) {
        shell_push_line("browser-vm <firefox|chromium>");
        return;
    }

    g_browser_vm_requested = true;
    k_strlcpy(g_browser_vm_engine, target, sizeof(g_browser_vm_engine));
    k_strlcpy(g_browser_vm_status,
              "VM backend seleccionado; falta conectar seL4/Linux guest",
              sizeof(g_browser_vm_status));

    shell_cmd_app_visible(app, true);
    render_scene();
    shell_push_line("browser-vm: backend VM seleccionado.");
    shell_push_line("browser-vm: construir host con `make sel4-browser-vm-bootstrap`.");
    shell_push_line("browser-vm: luego `make sel4-browser-vm-build` para Linux guest minimo.");
    shell_push_line("browser-vm: Linux ABI nativo queda disponible con `browser-real <engine>`.");
    __boot_serial_puts("[browser-vm] requested engine=");
    __boot_serial_puts(target);
    __boot_serial_puts("\n");
}

static void shell_cmd_browser_real(const char *engine, const char *extra_args) {
    const char *target = (engine && *engine) ? engine : "chromium";
    bool started;

    g_browser_vm_requested = false;
    k_strlcpy(g_browser_vm_status,
              "VM fallback disponible; camino principal: Linux ABI nativo",
              sizeof(g_browser_vm_status));

    if (k_strcasecmp_ascii(target, "firefox") == 0) {
        const char *const firefox_paths[] = {
            "/opt/firefox/firefox",
            "/opt/firefox/firefox-bin",
            "/usr/lib/firefox-esr/firefox-esr",
            "/usr/lib/firefox/firefox"
        };
        shell_push_line("browser-real: probando Firefox via Linux ABI nativo...");
        started = shell_try_browser_realrun_candidates(
            firefox_paths,
            (int)(sizeof(firefox_paths) / sizeof(firefox_paths[0])),
            extra_args,
            "browser-real: no se encontro Firefox real en RiduxFS.");
    } else {
        const char *const chrome_paths[] = {
            "/opt/chromium/chrome",
            "/opt/google/chrome/chrome",
            "/usr/bin/google-chrome",
            "/usr/lib/chromium/chromium",
            "/bin/chrome.elf"
        };
        shell_push_line("browser-real: probando Chromium via Linux ABI nativo...");
        started = shell_try_browser_realrun_candidates(
            chrome_paths,
            (int)(sizeof(chrome_paths) / sizeof(chrome_paths[0])),
            extra_args,
            "browser-real: no se encontro Chromium real en RiduxFS.");
    }
    if (started) {
        shell_push_line("browser-real: task lanzada (ver serial log).");
        shell_push_line("tip: para el bridge virtual usa URLs http://...");
        wm_minimize_app_windows(APP_TERMINAL);
        wm_minimize_app_windows(APP_KERNEL_CONSOLE);
        g_start_open = false;
        g_quick_open = false;
        g_needs_redraw = true;
    }
}

static void shell_cmd_plasma(int argc, char *argv[]) {
    char compat_out[2048];
    char compat_args[320];
    size_t ca_len = 0;
    int i;
    bool started;

    compat_args[0] = 0;
    if (argc <= 1) {
        k_append_str(compat_args, &ca_len, sizeof(compat_args), "run");
    } else {
        for (i = 1; i < argc; ++i) {
            if (i > 1) k_append_ch(compat_args, &ca_len, sizeof(compat_args), ' ');
            k_append_str(compat_args, &ca_len, sizeof(compat_args), argv[i]);
        }
    }

    if (!compat_shell_dispatch("plasma", compat_args, compat_out, (int)sizeof(compat_out))) {
        shell_push_line("plasma: compat command missing");
        return;
    }

    shell_push_multiline_text(compat_out);
    started = k_contains(compat_out, "estado: Plasma staged") ||
              k_contains(compat_out, "estado: task staged") ||
              k_contains(compat_out, "staged pid=");
    if (started) {
        wm_minimize_app_windows(APP_TERMINAL);
        wm_minimize_app_windows(APP_KERNEL_CONSOLE);
        g_start_open = false;
        g_quick_open = false;
        g_needs_redraw = true;
    }
}

static void shell_cmd_wayfire(int argc, char *argv[]) {
    char compat_out[2048];
    char compat_args[320];
    size_t ca_len = 0;
    int i;
    bool started;

    compat_args[0] = 0;
    if (argc <= 1) {
        k_append_str(compat_args, &ca_len, sizeof(compat_args), "run");
    } else {
        for (i = 1; i < argc; ++i) {
            if (i > 1) k_append_ch(compat_args, &ca_len, sizeof(compat_args), ' ');
            k_append_str(compat_args, &ca_len, sizeof(compat_args), argv[i]);
        }
    }

    if (!compat_shell_dispatch("wayfire", compat_args, compat_out, (int)sizeof(compat_out))) {
        shell_push_line("wayfire: compat command missing");
        return;
    }

    shell_push_multiline_text(compat_out);
    started = k_contains(compat_out, "estado: Wayfire staged") ||
              k_contains(compat_out, "estado: task staged") ||
              k_contains(compat_out, "staged pid=");
    if (started) {
        wm_minimize_app_windows(APP_TERMINAL);
        wm_minimize_app_windows(APP_KERNEL_CONSOLE);
        g_start_open = false;
        g_quick_open = false;
        g_needs_redraw = true;
    }
}

/* Dispatch a single already-tokenized command. Assumes argc > 0.
 * Can be invoked directly by pipe machinery. */
static bool shell_dispatch_argv(int argc, char *argv[]) {
    if      (k_strcmp(argv[0], "help")    == 0) shell_cmd_help();
    else if (k_strcmp(argv[0], "?")       == 0) shell_cmd_help();
    else if (k_strcmp(argv[0], "clear")   == 0) shell_reset_history();
    else if (k_strcmp(argv[0], "about")   == 0) shell_push_line("RiduxOS Unix 0.4 Bloom - glassmorphism edition");
    else if (k_strcmp(argv[0], "uname")   == 0) {
#if defined(__x86_64__)
        shell_push_line("RiduxOS Unix 0.4 Bloom #1 x86_64");
#else
        shell_push_line("RiduxOS Unix 0.4 Bloom #1 i386");
#endif
    }
    else if (k_strcmp(argv[0], "flush")   == 0) shell_push_line("Flush: queue renderer active (20+ primitives).");
    else if (k_strcmp(argv[0], "theme")   == 0) shell_cycle_theme();
    else if (k_strcmp(argv[0], "apps")    == 0) shell_cmd_apps();
    else if (k_strcmp(argv[0], "open")    == 0) { if (argc < 2) shell_push_line("open <name>"); else shell_cmd_app_visible(argv[1], true); }
    else if (k_strcmp(argv[0], "close")   == 0) { if (argc < 2) shell_push_line("close <name>"); else shell_cmd_app_visible(argv[1], false); }
    else if (k_strcmp(argv[0], "browser-vm") == 0) shell_cmd_browser_vm(argc >= 2 ? argv[1] : "chromium");
    else if (k_strcmp(argv[0], "wayfire") == 0 ||
             k_strcmp(argv[0], "wf") == 0) shell_cmd_wayfire(argc, argv);
    else if (k_strcmp(argv[0], "plasma") == 0 ||
             k_strcmp(argv[0], "kde") == 0) shell_cmd_plasma(argc, argv);
    else if (k_strcmp(argv[0], "browser-real") == 0) {
        char extra[256];
        shell_join_args_from(argc, argv, 2, extra, sizeof(extra));
        shell_cmd_browser_real(argc >= 2 ? argv[1] : "chromium", extra);
    }
    else if (k_strcmp(argv[0], "firefox") == 0) {
        char extra[256];
        shell_join_args_from(argc, argv, 1, extra, sizeof(extra));
        shell_cmd_browser_real("firefox", extra);
    }
    else if (k_strcmp(argv[0], "chrome")  == 0 ||
             k_strcmp(argv[0], "chromium")== 0 ||
             k_strcmp(argv[0], "google-chrome")== 0) {
        char extra[256];
        shell_join_args_from(argc, argv, 1, extra, sizeof(extra));
        shell_cmd_browser_real("chromium", extra);
    }
    else if (k_strcmp(argv[0], "start")   == 0) { g_start_open = !g_start_open; shell_push_line(g_start_open ? "start: open" : "start: close"); }
    else if (k_strcmp(argv[0], "quick")   == 0) { g_quick_open = !g_quick_open; shell_push_line(g_quick_open ? "quick: open" : "quick: close"); }
    else if (k_strcmp(argv[0], "ls")      == 0) shell_cmd_ls(argc >= 2 ? argv[1] : "/");
    else if (k_strcmp(argv[0], "cat")     == 0) { if (argc < 2) shell_push_line("cat <p>"); else shell_cmd_cat(argv[1]); }
    else if (k_strcmp(argv[0], "touch")   == 0) { if (argc < 2) shell_push_line("touch <p>"); else shell_cmd_touch(argv[1]); }
    else if (k_strcmp(argv[0], "rm")      == 0) { if (argc < 2) shell_push_line("rm <p>"); else shell_cmd_rm(argv[1]); }
    else if (k_strcmp(argv[0], "echo")    == 0) shell_cmd_echo(argc, argv);
    else if (k_strcmp(argv[0], "grep")    == 0) shell_cmd_grep(argc, argv);
    else if (k_strcmp(argv[0], "head")    == 0) shell_cmd_head_tail(argc, argv, false);
    else if (k_strcmp(argv[0], "tail")    == 0) shell_cmd_head_tail(argc, argv, true);
    else if (k_strcmp(argv[0], "wc")      == 0) { if (argc < 2) shell_push_line("wc <p>"); else shell_cmd_wc(argv[1]); }
    else if (k_strcmp(argv[0], "find")    == 0) { if (argc < 2) shell_push_line("find <pat>"); else shell_cmd_find(argv[1]); }
    else if (k_strcmp(argv[0], "hexdump") == 0) { if (argc < 2) shell_push_line("hexdump <p>"); else shell_cmd_hexdump(argv[1]); }
    else if (k_strcmp(argv[0], "cp")      == 0) { if (argc < 3) shell_push_line("cp <src> <dst>"); else shell_cmd_cp(argv[1], argv[2]); }
    else if (k_strcmp(argv[0], "mv")      == 0) { if (argc < 3) shell_push_line("mv <src> <dst>"); else shell_cmd_mv(argv[1], argv[2]); }
    else if (k_strcmp(argv[0], "mkdir")   == 0) { if (argc < 2) shell_push_line("mkdir <p>"); else shell_cmd_mkdir(argv[1]); }
    else if (k_strcmp(argv[0], "ps")      == 0) shell_cmd_ps();
    else if (k_strcmp(argv[0], "spawn")   == 0) { if (argc < 2) shell_push_line("spawn <n>"); else shell_cmd_spawn(argv[1]); }
    else if (k_strcmp(argv[0], "r3")      == 0) { if (argc < 2) shell_push_line("r3 <elf-path>"); else shell_cmd_r3(argv[1]); }
    else if (k_strcmp(argv[0], "kill")    == 0) { if (argc < 2) shell_push_line("kill <pid>"); else shell_cmd_kill(argv[1]); }
    else if (k_strcmp(argv[0], "pstree")  == 0) shell_cmd_pstree();
    else if (k_strcmp(argv[0], "top")     == 0) shell_cmd_top();
    else if (k_strcmp(argv[0], "uptime")  == 0) shell_cmd_uptime();
    else if (k_strcmp(argv[0], "free")    == 0) shell_cmd_free();
    else if (k_strcmp(argv[0], "df")      == 0) shell_cmd_df();
    else if (k_strcmp(argv[0], "mem")     == 0) shell_cmd_mem();
    else if (k_strcmp(argv[0], "kernlog") == 0) shell_cmd_kernlog();
    else if (k_strcmp(argv[0], "dmesg")   == 0) shell_cmd_kernlog();
    else if (k_strcmp(argv[0], "irqs")    == 0) shell_cmd_irqs();
    else if (k_strcmp(argv[0], "heap")    == 0) shell_cmd_heap();
    else if (k_strcmp(argv[0], "date")    == 0) shell_cmd_date();
    else if (k_strcmp(argv[0], "sleep")   == 0) shell_cmd_sleep(argc, argv);
    else if (k_strcmp(argv[0], "whoami")  == 0) shell_push_line("ridux");
    else if (k_strcmp(argv[0], "hostname")== 0) shell_push_line("ridux-bloom");
    else if (k_strcmp(argv[0], "pwd")     == 0) shell_push_line("/");
    else if (k_strcmp(argv[0], "env")     == 0) {
        shell_push_line("USER=ridux");
        shell_push_line("SHELL=/bin/rsh");
        shell_push_line("TERM=ridux-term");
        shell_push_line("PATH=/bin:/usr/bin:/sbin");
        shell_push_line("HOME=/home");
    }
    else if (k_strcmp(argv[0], "wm")      == 0) shell_cmd_wm(argc, argv);
    else if (k_strcmp(argv[0], "elf")     == 0) shell_cmd_elf(argc, argv);
    else if (k_strcmp(argv[0], "drivers") == 0) shell_cmd_drivers();
    else if (k_strcmp(argv[0], "pci")     == 0) shell_cmd_pci();
    else if (k_strcmp(argv[0], "gpu")     == 0) shell_cmd_gpu();
    else if (k_strcmp(argv[0], "smp")     == 0) shell_cmd_smp();
    else if (k_strcmp(argv[0], "time")    == 0) shell_cmd_time();
    else if (k_strcmp(argv[0], "beep")    == 0) { speaker_beep(880, 120000); shell_push_line("beep!"); }
    else if (k_strcmp(argv[0], "motd")    == 0) shell_cmd_cat("/etc/motd.txt");
    else if (k_strcmp(argv[0], "reboot")  == 0) { shell_push_line("rebooting..."); power_reboot(); }
    else if (k_strcmp(argv[0], "shutdown")== 0 ||
             k_strcmp(argv[0], "poweroff")== 0) { shell_push_line("powering down..."); power_shutdown(); }
    else if (k_strcmp(argv[0], "halt")    == 0) { shell_push_line("cpu halted."); for(;;) __asm__ volatile("cli; hlt"); }
    else if (k_strcmp(argv[0], "ifconfig")== 0) shell_cmd_ifconfig();
    else if (k_strcmp(argv[0], "arp")     == 0) shell_cmd_arp();
    else if (k_strcmp(argv[0], "ping")    == 0) { if (argc < 2) shell_push_line("ping <ip>"); else shell_cmd_ping(argv[1]); }
    else if (k_strcmp(argv[0], "dns")     == 0) { if (argc < 2) shell_push_line("dns <host>"); else shell_cmd_dns(argv[1]); }
    else if (k_strcmp(argv[0], "edit")    == 0) { if (argc < 2) shell_push_line("edit <file>"); else shell_cmd_edit(argv[1]); }
    else {
        /* Try compat layer commands (lsblk, ifconfig, route, mount, lsdev, etc.) */
        char compat_out[2048];
        char compat_args[256];
        size_t ca_len = 0;
        int ai;
        compat_args[0] = 0;
        for (ai = 1; ai < argc; ++ai) {
            if (ai > 1) k_append_ch(compat_args, &ca_len, sizeof(compat_args), ' ');
            k_append_str(compat_args, &ca_len, sizeof(compat_args), argv[ai]);
        }
        if (compat_shell_dispatch(argv[0], compat_args, compat_out, (int)sizeof(compat_out))) {
            shell_push_multiline_text(compat_out);
        } else {
            shell_push_line("Unknown command. Type `help`.");
            return false;
        }
    }
    return true;
}

/* Very small pipe support: "cmd1 | cmd2" runs cmd1 with its output
 * captured into a scratch file /tmp/.pipe, then feeds that file as the
 * last argument of cmd2 (if cmd2 doesn't already have a file arg).
 * Works well for: cmd | head, cmd | tail, cmd | wc, cmd | grep PAT. */
static void shell_run_pipe(char *left, char *right) {
    char *lav[16], *rav[16];
    int lc, rc;
    int i;
    char pipe_path[] = "/tmp/.pipe";
    char pipe_buf[UI_FILE_BUF_MAX + 1];
    size_t blen = 0;

    lc = k_split_tokens(left,  lav, 16);
    rc = k_split_tokens(right, rav, 16);
    if (lc <= 0 || rc <= 0) { shell_push_line("bad pipe"); return; }

    shell_capture_begin();
    shell_dispatch_argv(lc, lav);
    shell_capture_end();

    pipe_buf[0] = 0;
    for (i = 0; i < g_shell_capture_count; ++i) {
        size_t n = k_strlen(g_shell_capture[i]);
        if (blen + n + 2 >= sizeof(pipe_buf)) break;
        k_memcpy(pipe_buf + blen, g_shell_capture[i], n);
        blen += n;
        pipe_buf[blen++] = '\n';
        pipe_buf[blen]   = 0;
    }
    vfs_write(pipe_path, pipe_buf);

    /* Ensure right side has pipe_path as final arg. */
    if (rc + 1 < 16) {
        rav[rc++] = pipe_path;
    }
    shell_dispatch_argv(rc, rav);
}

/* Execute a full command line: handles pipes and '>' / '>>' redirects. */
static void shell_execute_command(const char *command) {
    char prompt[SHELL_LINE_LEN + 12];
    char work[SHELL_LINE_LEN];
    char *left, *right, *redir_op, *redir_path;
    size_t len = 0;
    size_t i;
    char *argv[16];
    int argc;

    prompt[0] = 0;
    k_append_str(prompt, &len, sizeof(prompt), "ridux $ ");
    k_append_str(prompt, &len, sizeof(prompt), command);
    shell_push_line(prompt);

    if (k_starts_with(command, "write ")) { shell_cmd_write_raw(command); return; }

    k_strlcpy(work, command, sizeof(work));

    /* --- Pipe split (only first |). --- */
    left = work;
    right = NULL;
    for (i = 0; work[i]; ++i) {
        if (work[i] == '|') {
            work[i] = 0;
            right = work + i + 1;
            break;
        }
    }
    if (right) { shell_run_pipe(left, right); return; }

    /* --- Redirect split (> or >>). --- */
    redir_op   = NULL;
    redir_path = NULL;
    for (i = 0; work[i]; ++i) {
        if (work[i] == '>') {
            bool append = (work[i + 1] == '>');
            size_t p = i;
            work[i] = 0;
            redir_op = append ? ">>" : ">";
            i += append ? 2u : 1u;
            while (work[i] && k_is_space(work[i])) ++i;
            redir_path = &work[i];
            /* Null-terminate any trailing space before path. */
            (void)p;
            break;
        }
    }

    argc = k_split_tokens(left, argv, 16);
    if (argc <= 0) return;

    if (redir_op && redir_path && redir_path[0]) {
        char out_buf[UI_FILE_BUF_MAX + 1];
        size_t out_len = 0;
        int k;
        shell_capture_begin();
        shell_dispatch_argv(argc, argv);
        shell_capture_end();

        /* If append, start with existing file contents. */
        out_buf[0] = 0;
        if (k_strcmp(redir_op, ">>") == 0) {
            const uint8_t *data; uint32_t size;
            if (vfs_read(redir_path, &data, &size)) {
                size_t copy = size > sizeof(out_buf) - 1 ? sizeof(out_buf) - 1 : size;
                k_memcpy(out_buf, data, copy);
                out_len = copy;
                out_buf[out_len] = 0;
            }
        }
        for (k = 0; k < g_shell_capture_count; ++k) {
            size_t n = k_strlen(g_shell_capture[k]);
            if (out_len + n + 2 >= sizeof(out_buf)) break;
            k_memcpy(out_buf + out_len, g_shell_capture[k], n);
            out_len += n;
            out_buf[out_len++] = '\n';
            out_buf[out_len]   = 0;
        }
        shell_push_line(vfs_write(redir_path, out_buf) ? "redir: ok" : "redir: write failed");
        return;
    }

    shell_dispatch_argv(argc, argv);
}

/* History navigation state used by Up/Down arrows. */
static void shell_history_push(const char *cmd) {
    /* Don't add duplicates of the most recent entry. */
    if (g_shell_history_count > 0 &&
        k_strcmp(g_shell_history[(g_shell_history_count - 1) % SHELL_HISTORY_MAX], cmd) == 0) {
        g_shell_history_cursor = g_shell_history_count;
        return;
    }
    k_strlcpy(g_shell_history[g_shell_history_count % SHELL_HISTORY_MAX], cmd, SHELL_LINE_LEN);
    ++g_shell_history_count;
    g_shell_history_cursor = g_shell_history_count;
}

static int64_t r3wm_sys_shell_exec(uint64_t a0, uint64_t a1, uint64_t a2,
                                   uint64_t a3, uint64_t a4, uint64_t a5) {
    const char *user_cmd = (const char *)(uintptr_t)a0;
    char *user_out = (char *)(uintptr_t)a1;
    uint32_t out_max = (uint32_t)a2;
    char cmd[SHELL_LINE_LEN];
    uint32_t out_len = 0;
    int i, k;
    (void)a3; (void)a4; (void)a5;

    if (!user_cmd || !user_out || out_max == 0) return -14; /* -EFAULT */
    for (i = 0; i < SHELL_LINE_LEN - 1 && user_cmd[i]; ++i) cmd[i] = user_cmd[i];
    cmd[i] = 0;
    user_out[0] = 0;
    if (!cmd[0]) return 0;

    shell_history_push(cmd);
    shell_capture_begin();
    shell_execute_command(cmd);
    shell_capture_end();

    for (k = 0; k < g_shell_capture_count && out_len + 1u < out_max; ++k) {
        const char *line = g_shell_capture[k];
        uint32_t j = 0;
        while (line[j] && out_len + 1u < out_max) user_out[out_len++] = line[j++];
        if (out_len + 1u < out_max) user_out[out_len++] = '\n';
    }
    user_out[out_len < out_max ? out_len : out_max - 1u] = 0;
    g_needs_redraw = true;
    return (int64_t)out_len;
}
static const char *shell_history_at(int cursor) {
    int oldest, idx;
    if (g_shell_history_count <= 0) return "";
    oldest = g_shell_history_count > SHELL_HISTORY_MAX
             ? g_shell_history_count - SHELL_HISTORY_MAX : 0;
    if (cursor < oldest)                          cursor = oldest;
    if (cursor >= g_shell_history_count)          return "";
    idx = cursor % SHELL_HISTORY_MAX;
    return g_shell_history[idx];
}
static void shell_history_set_input(int cursor) {
    const char *h = shell_history_at(cursor);
    k_strlcpy(g_shell_input, h, SHELL_LINE_LEN);
    g_shell_input_len = k_strlen(g_shell_input);
}

/* Tab completion: if the last token is a prefix of a VFS file, replace
 * it with the longest common match. If multiple candidates exist, list
 * them in the terminal. Also completes command names from a built-in
 * list when the cursor is still on the first token. */
static const char *g_cmd_names[] = {
    "help","clear","about","uname","flush","theme","apps","open","close",
    "browser-vm","browser-real","wayfire","wf","plasma","kde","firefox","chrome","chromium","google-chrome","start","quick","ls","cat","touch","rm","echo","grep","head","tail",
    "wc","find","hexdump","cp","mv","mkdir","ps","spawn","kill","pstree",
    "top","uptime","free","df","mem","kernlog","dmesg","irqs","heap",
    "date","sleep","whoami","hostname","pwd","env","wm","elf","drivers",
    "pci","time","beep","motd","reboot","shutdown","poweroff","halt",
    "ifconfig","arp","ping","dns","edit","write",
    "abi6","pidfd","io_uring","statx","sysplus","dynlink",NULL
};

static void shell_try_complete(void) {
    char prefix[SHELL_LINE_LEN];
    size_t plen;
    size_t back;
    int best_idx = -1; int cand = 0;
    size_t i;

    /* Find last token start. */
    back = g_shell_input_len;
    while (back > 0 && !k_is_space(g_shell_input[back - 1])) --back;
    plen = g_shell_input_len - back;
    k_memcpy(prefix, g_shell_input + back, plen);
    prefix[plen] = 0;
    if (plen == 0) return;

    /* If at position 0 (first token), complete commands. */
    if (back == 0) {
        for (i = 0; g_cmd_names[i]; ++i) {
            if (k_starts_with(g_cmd_names[i], prefix)) {
                if (cand == 0) best_idx = (int)i;
                ++cand;
            }
        }
        if (cand == 1 && best_idx >= 0) {
            const char *full = g_cmd_names[best_idx];
            size_t full_len = k_strlen(full);
            if (back + full_len + 2 < SHELL_LINE_LEN) {
                k_strlcpy(g_shell_input + back, full, SHELL_LINE_LEN - back);
                g_shell_input_len = back + full_len;
                g_shell_input[g_shell_input_len++] = ' ';
                g_shell_input[g_shell_input_len] = 0;
            }
            return;
        } else if (cand > 1) {
            char line[SHELL_LINE_LEN]; size_t lp = 0;
            line[0] = 0;
            for (i = 0; g_cmd_names[i]; ++i) {
                if (k_starts_with(g_cmd_names[i], prefix)) {
                    if (lp + k_strlen(g_cmd_names[i]) + 2 < sizeof(line)) {
                        k_append_str(line, &lp, sizeof(line), g_cmd_names[i]);
                        k_append_ch(line, &lp, sizeof(line), ' ');
                    }
                }
            }
            shell_push_line(line);
            return;
        }
    }

    /* Otherwise complete file path in VFS. */
    {
        const char *match = NULL;
        int matches = 0;
        for (i = 0; i < VFS_MAX_FILES; ++i) {
            if (g_vfs_files[i].used && k_starts_with(g_vfs_files[i].path, prefix)) {
                if (matches == 0) match = g_vfs_files[i].path;
                ++matches;
            }
        }
        if (matches == 1 && match) {
            size_t mlen = k_strlen(match);
            if (back + mlen + 1 < SHELL_LINE_LEN) {
                k_strlcpy(g_shell_input + back, match, SHELL_LINE_LEN - back);
                g_shell_input_len = back + mlen;
            }
        } else if (matches > 1) {
            char line[SHELL_LINE_LEN]; size_t lp = 0;
            line[0] = 0;
            for (i = 0; i < VFS_MAX_FILES; ++i) {
                if (g_vfs_files[i].used && k_starts_with(g_vfs_files[i].path, prefix)) {
                    if (lp + k_strlen(g_vfs_files[i].path) + 2 < sizeof(line)) {
                        k_append_str(line, &lp, sizeof(line), g_vfs_files[i].path);
                        k_append_ch(line, &lp, sizeof(line), ' ');
                    }
                }
            }
            shell_push_line(line);
        }
    }
}

static void shell_submit(void) {
    if (g_shell_input_len == 0) return;
    g_shell_input[g_shell_input_len] = 0;
    shell_history_push(g_shell_input);
    shell_execute_command(g_shell_input);
    g_shell_input_len = 0;
    g_shell_input[0] = 0;
}
/* Input handlers (keyboard + mouse high-level) */

static const char keymap_unshift[128] = {
    0,  27, '1','2','3','4','5','6','7','8','9','0','-','=','\b',
    '\t','q','w','e','r','t','y','u','i','o','p','[',']','\n',
    0,  'a','s','d','f','g','h','j','k','l',';','\'','`',0,
    '\\','z','x','c','v','b','n','m',',','.','/',0,'*',0,' ',
};
static const char keymap_shift[128] = {
    0,  27, '!','@','#','$','%','^','&','*','(',')','_','+','\b',
    '\t','Q','W','E','R','T','Y','U','I','O','P','{','}','\n',
    0,  'A','S','D','F','G','H','J','K','L',':','"','~',0,
    '|','Z','X','C','V','B','N','M','<','>','?',0,'*',0,' ',
};

static uint8_t ps2_scancode_to_x11_keycode(uint8_t sc, bool extended) {
    uint8_t base = (uint8_t)(sc & 0x7Fu);
    if (!extended) return (uint8_t)(base + 8u);
    switch (base) {
        case 0x48: return 111; /* Up */
        case 0x4B: return 113; /* Left */
        case 0x4D: return 114; /* Right */
        case 0x50: return 116; /* Down */
        case 0x52: return 118; /* Insert */
        case 0x53: return 119; /* Delete */
        case 0x47: return 110; /* Home */
        case 0x4F: return 115; /* End */
        case 0x49: return 112; /* Page Up */
        case 0x51: return 117; /* Page Down */
        case 0x1C: return 36;  /* Keypad Enter -> Return */
        case 0x1D: return 105; /* Control_R */
        default:   return (uint8_t)(base + 8u);
    }
}

static uint32_t ps2_scancode_to_wl_key(uint8_t sc, bool extended) {
    uint8_t base = (uint8_t)(sc & 0x7Fu);
    if (extended) {
        switch (base) {
            case 0x1C: return 96;  /* KEY_KPENTER */
            case 0x1D: return 97;  /* KEY_RIGHTCTRL */
            case 0x38: return 100; /* KEY_RIGHTALT */
            case 0x47: return 102; /* KEY_HOME */
            case 0x48: return 103; /* KEY_UP */
            case 0x49: return 104; /* KEY_PAGEUP */
            case 0x4B: return 105; /* KEY_LEFT */
            case 0x4D: return 106; /* KEY_RIGHT */
            case 0x4F: return 107; /* KEY_END */
            case 0x50: return 108; /* KEY_DOWN */
            case 0x51: return 109; /* KEY_PAGEDOWN */
            case 0x52: return 110; /* KEY_INSERT */
            case 0x53: return 111; /* KEY_DELETE */
            case 0x5B: return 125; /* KEY_LEFTMETA */
            case 0x5C: return 126; /* KEY_RIGHTMETA */
            default: break;
        }
    }
    /* Wayland wants Linux evdev key numbers. For the set-1 keys we handle
     * here, those line up with the raw PS/2 make code (A=30, Enter=28).
     * Subtracting one made Firefox receive the neighbor key. */
    return (uint32_t)base;
}

/* Is the currently focused window the Terminal? If so, shell keybindings
 * take priority over window navigation. */
static bool terminal_focused(void) {
    if (g_window_focus < 0 || g_window_focus >= g_window_count) return false;
    return g_windows[g_window_focus].app_id == APP_TERMINAL ||
           g_windows[g_window_focus].app_id == APP_KERNEL_CONSOLE;
}

static void keyboard_handle_extended(uint8_t sc) {
    bool release = (sc & 0x80u) != 0u;
    uint8_t base = (uint8_t)(sc & 0x7Fu);
    uint8_t xkc = ps2_scancode_to_x11_keycode(sc, true);
    evdev_push_key((uint16_t)ps2_scancode_to_wl_key(sc, true), release ? 0 : 1);
    if (g_wayfire_desktop_active) return;
    if (release) {
        if (x11_dispatch_key_event(xkc, false)) return;
        (void)wl7_dispatch_keyboard_event(ps2_scancode_to_wl_key(sc, true), 0);
        return;
    }
    /* Super key (left GUI) - 0x5B extended. Toggle start. */
    if (base == 0x5B || base == 0x5C) {
        g_start_open = !g_start_open;
        g_needs_redraw = true;
        return;
    }
    if (x11_dispatch_key_event(xkc, true)) return;
    if (wl7_dispatch_keyboard_event(ps2_scancode_to_wl_key(sc, true), 1)) return;
    /* Up/Down: shell history when terminal focused, else window move in drag mode. */
    if (base == 0x48 || base == 0x50) {
        if (terminal_focused() && !g_wm_drag_mode) {
            if (base == 0x48) {
                if (g_shell_history_cursor > 0) --g_shell_history_cursor;
                shell_history_set_input(g_shell_history_cursor);
            } else {
                if (g_shell_history_cursor < g_shell_history_count) ++g_shell_history_cursor;
                if (g_shell_history_cursor >= g_shell_history_count) {
                    g_shell_input[0] = 0;
                    g_shell_input_len = 0;
                } else {
                    shell_history_set_input(g_shell_history_cursor);
                }
            }
            g_needs_redraw = true;
            return;
        }
    }
    /* Left/Right arrows: only window drag move. */
    if (base == 0x48 || base == 0x50 || base == 0x4B || base == 0x4D) {
        if (!g_wm_drag_mode) {
            if (!g_arrow_hint_shown) {
                shell_push_line("Tip: `wm drag` + arrows to move focused window, or focus Terminal for history.");
                g_arrow_hint_shown = true;
            }
            g_needs_redraw = true;
            return;
        }
        if (base == 0x48)      wm_move_focused(0, -14);
        else if (base == 0x50) wm_move_focused(0, 14);
        else if (base == 0x4B) wm_move_focused(-14, 0);
        else if (base == 0x4D) wm_move_focused(14, 0);
        g_needs_redraw = true;
    }
}

static int ring3_desktop_key_target(void) {
    int i;
    for (i = g_window_count - 1; i >= 0; --i) {
        window_t *w = &g_windows[i];
        if (!w->used || !w->visible || w->minimized) continue;
        if (w->app_id != APP_RING3_BACKED) continue;
        if (w->flags & WINF_DESKTOP) return i;
    }
    return -1;
}

static void keyboard_handle_scancode(uint8_t sc) {
    char ch = 0;
    bool release;
    uint8_t base;
    uint8_t xkc;
    if (g_pit_ticks < g_keyboard_accept_after_tick) {
        g_shift_down = false;
        g_ctrl_down = false;
        g_extended_scancode = false;
        return;
    }
    if (sc == 0xE0) { g_extended_scancode = true; return; }
    if (g_extended_scancode) { g_extended_scancode = false; keyboard_handle_extended(sc); return; }

    release = (sc & 0x80u) != 0u;
    base = (uint8_t)(sc & 0x7Fu);
    xkc = ps2_scancode_to_x11_keycode(sc, false);
    evdev_push_key((uint16_t)base, release ? 0 : 1);
    {
        static uint32_t kbd_trace_count;
        if (shell_input_debug_trace_enabled() && kbd_trace_count < 96u) {
            ++kbd_trace_count;
            __boot_serial_force_puts("[kbd!] sc=");
            __boot_serial_force_putu32((uint32_t)sc);
            __boot_serial_force_puts(" base=");
            __boot_serial_force_putu32((uint32_t)base);
            __boot_serial_force_puts(" xkc=");
            __boot_serial_force_putu32((uint32_t)xkc);
            __boot_serial_force_puts(release ? " up" : " down");
            __boot_serial_force_puts(" focus=");
            __boot_serial_force_putu32((uint32_t)((g_window_focus >= 0) ? g_window_focus : -1));
            __boot_serial_force_puts("\n");
        }
    }

    if (base == 0x2A || base == 0x36) {
        g_shift_down = !release;
        if (g_wayfire_desktop_active) return;
        if (x11_dispatch_key_event(xkc, !release)) return;
        (void)wl7_dispatch_keyboard_event(ps2_scancode_to_wl_key(sc, false), release ? 0u : 1u);
        return;
    }
    if (base == 0x1D) {
        g_ctrl_down = !release;
        if (g_wayfire_desktop_active) return;
        if (x11_dispatch_key_event(xkc, !release)) return;
        (void)wl7_dispatch_keyboard_event(ps2_scancode_to_wl_key(sc, false), release ? 0u : 1u);
        return;
    }
    if (g_wayfire_desktop_active) return;
    if (release) {
        if (x11_dispatch_key_event(xkc, false)) return;
        (void)wl7_dispatch_keyboard_event(ps2_scancode_to_wl_key(sc, false), 0);
        return;
    }

    ch = g_shift_down ? keymap_shift[base] : keymap_unshift[base];

    if (g_ctrl_down && wm_has_visible_ring3_desktop()) {
        int desk = ring3_desktop_key_target();
        if (desk >= 0) {
            r3wm_post_key(desk, RIDUX_EVENT_KEY_DOWN,
                          (uint32_t)(ch ? ch : 0), (uint32_t)base);
            g_needs_redraw = true;
        }
        return;
    }
    if (g_window_focus >= 0 && g_window_focus < g_window_count &&
        g_windows[g_window_focus].app_id == APP_RING3_BACKED) {
        r3wm_post_key(g_window_focus, RIDUX_EVENT_KEY_DOWN,
                      (uint32_t)(ch ? ch : 0), (uint32_t)base);
        g_needs_redraw = true;
        return;
    }

    if (g_window_focus < 0 && wm_has_visible_ring3_desktop()) {
        int desk = ring3_desktop_key_target();
        if (desk >= 0) {
            r3wm_post_key(desk, RIDUX_EVENT_KEY_DOWN,
                          (uint32_t)(ch ? ch : 0), (uint32_t)base);
            g_needs_redraw = true;
        }
        return;
    }

    if (x11_dispatch_key_event(xkc, true)) return;

    if (wm_has_visible_ring3_desktop()) {
        int desk = ring3_desktop_key_target();
        if (desk >= 0) {
            r3wm_post_key(desk, RIDUX_EVENT_KEY_DOWN,
                          (uint32_t)(ch ? ch : 0), (uint32_t)base);
            g_needs_redraw = true;
        }
        return;
    }

    if (wl7_dispatch_keyboard_event(ps2_scancode_to_wl_key(sc, false), 1)) return;

    if (base == 0x0F) {
        /* Tab: complete inside terminal, else switch window. */
        if (terminal_focused()) shell_try_complete();
        else                    wm_focus_next();
        g_needs_redraw = true;
        return;
    }
    if (base == 0x01) { g_start_open = false; g_quick_open = false; g_needs_redraw = true; return; } /* Esc */
    if (base == 0x1C) { shell_submit(); g_needs_redraw = true; return; }
    if (base == 0x0E) {
        if (g_shell_input_len > 0) {
            --g_shell_input_len;
            g_shell_input[g_shell_input_len] = 0;
        }
        g_needs_redraw = true;
        return;
    }

    /* If an interactive non-terminal app is focused and wants keys, route
     * the character there (Editor, Snake, ...). Terminal takes the default
     * shell path below. Ring 3 windows get the event pushed into their
     * own ring so the user task can drain it via SYS_WINDOW_POLL. */
    if (!terminal_focused() && g_window_focus >= 0 && g_window_focus < g_window_count) {
        if (g_windows[g_window_focus].app_id == APP_RING3_BACKED) {
            r3wm_post_key(g_window_focus, RIDUX_EVENT_KEY_DOWN,
                          (uint32_t)(ch ? ch : 0), (uint32_t)base);
            g_needs_redraw = true;
            return;
        } else {
            int aidx = app_index_by_id(g_windows[g_window_focus].app_id);
            if (aidx >= 0 && g_apps[aidx].key) {
                g_apps[aidx].key(&g_windows[g_window_focus], ch ? ch : 0, sc);
                g_needs_redraw = true;
                return;
            }
        }
    }

    if (!ch || ch == '\t' || ch == '\n' || ch == 27) return;
    if (g_shell_input_len + 1 < SHELL_LINE_LEN) {
        g_shell_input[g_shell_input_len++] = ch;
        g_shell_input[g_shell_input_len] = 0;
    }
    g_needs_redraw = true;
}

/* Taskbar / start menu / window control click dispatch. */
static bool hit_in(ui_rect_t r, int x, int y) {
    return x >= r.x && y >= r.y && x < r.x + r.w && y < r.y + r.h;
}

static bool route_ring3_pointer_event(uint32_t type, uint32_t button);

static int taskbar_dock_hit(int mx, int my) {
    int icon = g_ui.taskbar_icon;
    int gap = dock_gap_px();
    int count = 0, i;
    int x, start_x, end_x;
    for (i = 0; i < g_app_count; ++i) if (g_apps[i].pinned_task) ++count;
    if (count == 0) return -1;
    dock_area_bounds(&start_x, &end_x);
    x = start_x;
    for (i = 0; i < g_app_count; ++i) {
        ui_rect_t slot;
        if (!g_apps[i].pinned_task) continue;
        if (x + icon > end_x) break;
        slot.x = x;
        slot.y = g_ui.taskbar.y + (g_ui.taskbar.h - icon) / 2 + 1;
        slot.w = icon; slot.h = icon;
        if (hit_in(slot, mx, my)) return i;
        x += icon + gap;
    }
    return -1;
}

static int start_menu_hit(int mx, int my) {
    start_menu_layout_t st = start_menu_layout_calc();
    int idx = 0, i;
    for (i = 0; i < g_app_count; ++i) {
        int col, row, cx, cy;
        if (!g_apps[i].pinned_start) continue;
        col = idx % st.cols;
        row = idx / st.cols;
        cx = st.gx + col * st.cell;
        cy = st.gy + row * st.cell;
        if (mx >= cx && mx < cx + st.tile_w && my >= cy && my < cy + st.tile_h) return i;
        ++idx;
        if (idx >= st.max_tiles) break;
    }
    return -1;
}

enum {
    WM_DRAG_NONE = 0,
    WM_DRAG_MOVE = 1,
    WM_DRAG_RESIZE = 2,
    WM_EDGE_LEFT = 1,
    WM_EDGE_TOP = 2,
    WM_EDGE_RIGHT = 4,
    WM_EDGE_BOTTOM = 8
};

static int wm_resize_edges_at(const window_t *w, int mx, int my) {
    int margin = 9;
    int edges = 0;
    if (!w || w->maximized) return 0;
    if (w->flags & (WINF_DESKTOP | WINF_BORDERLESS)) return 0;
    if (mx < w->x || my < w->y || mx >= w->x + w->w || my >= w->y + w->h) return 0;
    if (mx < w->x + margin) edges |= WM_EDGE_LEFT;
    if (mx >= w->x + w->w - margin) edges |= WM_EDGE_RIGHT;
    if (my < w->y + margin) edges |= WM_EDGE_TOP;
    if (my >= w->y + w->h - margin) edges |= WM_EDGE_BOTTOM;
    return edges;
}

static void wm_begin_move(window_t *w, int mx, int my) {
    if (!w || w->maximized) return;
    g_mouse_dragging = true;
    g_mouse_drag_kind = WM_DRAG_MOVE;
    g_mouse_drag_window_id = w->id;
    g_mouse_drag_offset_x = mx - w->x;
    g_mouse_drag_offset_y = my - w->y;
    g_mouse_resize_edges = 0;
}

static void wm_begin_resize(window_t *w, int mx, int my, int edges) {
    if (!w || !edges || w->maximized) return;
    g_mouse_dragging = true;
    g_mouse_drag_kind = WM_DRAG_RESIZE;
    g_mouse_drag_window_id = w->id;
    g_mouse_resize_edges = edges;
    g_mouse_drag_start_x = mx;
    g_mouse_drag_start_y = my;
    g_mouse_drag_start_win_x = w->x;
    g_mouse_drag_start_win_y = w->y;
    g_mouse_drag_start_win_w = w->w;
    g_mouse_drag_start_win_h = w->h;
}

static bool handle_window_click(int mx, int my, int button) {
    int widx = wm_find_window_at(mx, my, false);
    int wid;
    window_t *w;
    int which;
    int edges;
    if (widx < 0) return false;
    w = &g_windows[widx];

    for (which = 0; which < 3; ++which) {
        ui_rect_t r = window_ctrl_rect(w, which);
        if (hit_in(r, mx, my)) {
            if      (which == 0) wm_minimize(w->id);
            else if (which == 1) wm_maximize_toggle(w->id);
            else {
                if (w->app_id == APP_RING3_BACKED) r3wm_post_close(widx);
                wm_close(w->id);
            }
            return true;
        }
    }

    wid = w->id;
    edges = wm_resize_edges_at(w, mx, my);
    wm_raise_to_top(widx);
    widx = wm_index_by_id(wid);
    if (widx < 0) return true;
    w = &g_windows[widx];

    if (edges) {
        wm_begin_resize(w, mx, my, edges);
        return true;
    }
    if (my < w->y + WIN_TITLE_H) {
        wm_begin_move(w, mx, my);
        return true;
    }
    if (w->app_id == APP_RING3_BACKED) {
        ui_rect_t body = window_body_rect(w);
        r3wm_post_mouse(widx, RIDUX_EVENT_MOUSE_DOWN,
                        mx - body.x, my - body.y, (uint32_t)button);
        return true;
    }
    {
        app_t *ap = app_by_window(w);
        if (ap && ap->click) {
            ui_rect_t body = window_body_rect(w);
            ap->click(w, body, mx, my, button);
            return true;
        }
    }
    return true;
}

static void handle_ui_click(int mx, int my, int button) {
    int idx;
    bool ring3_desktop_active = wm_has_visible_ring3_desktop();

    if (ring3_desktop_active) {
        /* Con RiduxUI activo, el kernel solo conserva el trabajo de WM:
         * marcos, mover, cerrar y redimensionar. Dock/launcher/fondo son R3. */
        if (handle_window_click(mx, my, button)) return;
        if (x11_dispatch_pointer_event(mx, my, button, true)) return;
        if (wl7_dispatch_pointer_event(mx, my, (uint32_t)button, 1)) return;
        (void)route_ring3_pointer_event(RIDUX_EVENT_MOUSE_DOWN, (uint32_t)button);
        return;
    }
    /* Start menu priority */
    if (g_start_open) {
        idx = start_menu_hit(mx, my);
        if (idx >= 0) {
            if (app_launch_user_process_for_id(g_apps[idx].app_id)) {
                g_start_open = false;
                g_quick_open = false;
                return;
            }
            wm_set_visible_by_id(g_apps[idx].window_id, true);
            wm_focus_by_id(g_apps[idx].window_id);
            g_start_open = false;
            return;
        }
        g_start_open = false;
        return;
    }
    if (g_quick_open) { g_quick_open = false; return; }

    /* Taskbar controls */
    if (hit_in(g_ui.start_btn, mx, my)) { g_start_open = !g_start_open; g_quick_open = false; return; }
    if (hit_in(g_ui.quick_btn, mx, my)) { g_quick_open = !g_quick_open; g_start_open = false; return; }
    if (hit_in(g_ui.clock_btn, mx, my)) { g_quick_open = !g_quick_open; g_start_open = false; return; }

    idx = taskbar_dock_hit(mx, my);
    if (idx >= 0) {
        int id = g_apps[idx].window_id;
        if (app_launch_user_process_for_id(g_apps[idx].app_id)) return;
        if (id > 0) {
            if (wm_is_visible_by_id(id) &&
                g_window_focus >= 0 && g_window_focus < g_window_count &&
                g_windows[g_window_focus].id == id) {
                wm_minimize(id);
            } else {
                wm_set_visible_by_id(id, true);
                wm_focus_by_id(id);
            }
        }
        return;
    }

    /* X11/Wayland clients are composited by compat7 directly rather than
     * represented as kernel WM windows, so give them a chance to receive
     * the click before the in-kernel demo windows below. */
    if (x11_dispatch_pointer_event(mx, my, button, true)) return;
    if (wl7_dispatch_pointer_event(mx, my, (uint32_t)button, 1)) return;

    (void)handle_window_click(mx, my, button);
}

static int ring3_desktop_window_at(int mx, int my) {
    int i;
    for (i = g_window_count - 1; i >= 0; --i) {
        window_t *w = &g_windows[i];
        ui_rect_t body;
        if (!w->used || !w->visible || w->minimized) continue;
        if (w->app_id != APP_RING3_BACKED) continue;
        if (!(w->flags & WINF_DESKTOP)) continue;
        body = window_body_rect(w);
        if (hit_in(body, mx, my)) return i;
    }
    return -1;
}

static bool route_ring3_pointer_event(uint32_t type, uint32_t button) {
    int widx = wm_find_window_at(g_mouse_x, g_mouse_y, false);
    window_t *w;
    ui_rect_t body;
    if (widx < 0 || g_windows[widx].app_id != APP_RING3_BACKED) {
        widx = ring3_desktop_window_at(g_mouse_x, g_mouse_y);
    }
    if (widx < 0) return false;
    w = &g_windows[widx];
    if (w->app_id != APP_RING3_BACKED) return false;
    body = window_body_rect(w);
    if (!hit_in(body, g_mouse_x, g_mouse_y)) return false;
    r3wm_post_mouse(widx, type, g_mouse_x - body.x, g_mouse_y - body.y, button);
    return true;
}

static void mouse_apply_window_resize(window_t *w) {
    int min_w = 180;
    int min_h = WIN_TITLE_H + 110;
    int dx = g_mouse_x - g_mouse_drag_start_x;
    int dy = g_mouse_y - g_mouse_drag_start_y;
    int x = g_mouse_drag_start_win_x;
    int y = g_mouse_drag_start_win_y;
    int ww = g_mouse_drag_start_win_w;
    int hh = g_mouse_drag_start_win_h;
    int right = g_mouse_drag_start_win_x + g_mouse_drag_start_win_w;
    int bottom = g_mouse_drag_start_win_y + g_mouse_drag_start_win_h;
    if (!w) return;
    if (g_mouse_resize_edges & WM_EDGE_LEFT) {
        x = g_mouse_drag_start_win_x + dx;
        ww = right - x;
        if (ww < min_w) { ww = min_w; x = right - min_w; }
    }
    if (g_mouse_resize_edges & WM_EDGE_RIGHT) {
        ww = g_mouse_drag_start_win_w + dx;
        if (ww < min_w) ww = min_w;
    }
    if (g_mouse_resize_edges & WM_EDGE_TOP) {
        y = g_mouse_drag_start_win_y + dy;
        hh = bottom - y;
        if (hh < min_h) { hh = min_h; y = bottom - min_h; }
    }
    if (g_mouse_resize_edges & WM_EDGE_BOTTOM) {
        hh = g_mouse_drag_start_win_h + dy;
        if (hh < min_h) hh = min_h;
    }
    if (x < 0) { ww += x; x = 0; }
    if (y < 0) { hh += y; y = 0; }
    if (x + ww > (int)g_fb.width) ww = (int)g_fb.width - x;
    if (y + hh > (int)g_fb.height) hh = (int)g_fb.height - y;
    if (ww < min_w) ww = min_w;
    if (hh < min_h) hh = min_h;
    w->x = x;
    w->y = y;
    w->w = ww;
    w->h = hh;
    wm_clamp(w);
}

static void mouse_update_drag(int dx, int dy) {
    window_t *w;
    int idx;
    int prev_x = g_mouse_x;
    int prev_y = g_mouse_y;
    g_mouse_x += dx;
    g_mouse_y -= dy;
    if (g_mouse_x < 0) g_mouse_x = 0;
    else if ((uint32_t)g_mouse_x >= g_fb.width) g_mouse_x = (int)g_fb.width - 1;
    if (g_mouse_y < 0) g_mouse_y = 0;
    else if ((uint32_t)g_mouse_y >= g_fb.height) g_mouse_y = (int)g_fb.height - 1;
    if (g_mouse_x != prev_x || g_mouse_y != prev_y) {
        g_cursor_moved = true;
        ridux_drm_cursor_move(g_mouse_x, g_mouse_y);
        if (!g_mouse_dragging) {
            if (!g_wayfire_desktop_active) {
                (void)x11_dispatch_pointer_event(g_mouse_x, g_mouse_y, 0, false);
                (void)wl7_dispatch_pointer_event(g_mouse_x, g_mouse_y, 0, 0);
                (void)route_ring3_pointer_event(RIDUX_EVENT_MOUSE_MOVE, 0);
            }
        }
    }
    if (g_wayfire_desktop_active) return;

    if (!g_mouse_left_down || !g_mouse_dragging) return;
    idx = wm_index_by_id(g_mouse_drag_window_id);
    if (idx < 0) { g_mouse_dragging = false; g_mouse_drag_window_id = -1; return; }
    w = &g_windows[idx];
    if (!w->visible || w->minimized) { g_mouse_dragging = false; return; }
    if (g_mouse_drag_kind == WM_DRAG_RESIZE) {
        mouse_apply_window_resize(w);
    } else {
        w->x = g_mouse_x - g_mouse_drag_offset_x;
        w->y = g_mouse_y - g_mouse_drag_offset_y;
        wm_clamp(w);
    }
    /* When dragging a window, the entire scene needs a real repaint
     * (the window contents are moving, not just the cursor). */
    g_needs_redraw = true;
}

static void mouse_handle_byte(uint8_t data) {
    uint8_t left, right;
    int dx, dy;
    if (!g_mouse_initialized) return;
    if (g_mouse_packet_index == 0 && (data & 0x08u) == 0u) return;
    g_mouse_packet[g_mouse_packet_index++] = data;
    if (g_mouse_packet_index < 3) return;
    g_mouse_packet_index = 0;

    dx = (int)((int8_t)g_mouse_packet[1]);
    dy = (int)((int8_t)g_mouse_packet[2]);
    left  = g_mouse_packet[0] & 0x01u;
    right = g_mouse_packet[0] & 0x02u;

    if (left && !g_mouse_left_down) {
        /* Rising edge: handle UI click */
        g_mouse_left_down = true;
        evdev_push_mouse_key(272, 1);
        if (!g_wayfire_desktop_active) {
            handle_ui_click(g_mouse_x, g_mouse_y, 1);
            g_needs_redraw = true;
        }
    } else if (!left && g_mouse_left_down) {
        bool was_dragging = g_mouse_dragging;
        evdev_push_mouse_key(272, 0);
        if (!g_wayfire_desktop_active && !was_dragging) {
            (void)x11_dispatch_pointer_event(g_mouse_x, g_mouse_y, 1, false);
            (void)wl7_dispatch_pointer_event(g_mouse_x, g_mouse_y, 1, 0);
            (void)route_ring3_pointer_event(RIDUX_EVENT_MOUSE_UP, 1);
        }
        g_mouse_dragging = false;
        g_mouse_drag_window_id = -1;
        g_mouse_drag_kind = WM_DRAG_NONE;
        g_mouse_resize_edges = 0;
        g_mouse_left_down = false;
        if (!g_wayfire_desktop_active) g_needs_redraw = true;
    }

    if (right && !g_mouse_right_down) {
        /* Right click: paint erase on Paint app, else ignore */
        int widx = wm_find_window_at(g_mouse_x, g_mouse_y, false);
        evdev_push_mouse_key(273, 1);
        if (!g_wayfire_desktop_active && widx >= 0) {
            window_t *w = &g_windows[widx];
            app_t *ap = app_by_window(w);
            if (ap && ap->click && w->app_id == APP_PAINT) {
                ui_rect_t body = window_body_rect(w);
                ap->click(w, body, g_mouse_x, g_mouse_y, 2);
                g_needs_redraw = true;
            }
        }
        g_mouse_right_down = true;
    } else if (!right && g_mouse_right_down) {
        evdev_push_mouse_key(273, 0);
        g_mouse_right_down = false;
    }

    if (dx || dy) {
        evdev_push_rel_xy(dx, -dy);
        mouse_update_drag(dx, dy);
    }
}

/* Drain the IRQ-fed ring buffers (preferred path) or fall back to
 * PS/2 port polling if interrupts never became live. */
static bool input_poll_once(void) {
    uint8_t v;
    if (g_idt_ready) {
        if (kbd_ring_pop(&v))   { keyboard_handle_scancode(v); return true; }
        if (mouse_ring_pop(&v)) { mouse_handle_byte(v);        return true; }
        {
            uint8_t status = inb(0x64);
            if ((status & 1u) == 0u) return false;
            v = inb(0x60);
            if (status & 0x20u) mouse_handle_byte(v);
            else                keyboard_handle_scancode(v);
            return true;
        }
    }
    {
        uint8_t status = inb(0x64);
        if ((status & 1u) == 0u) return false;
        v = inb(0x60);
        if (status & 0x20u) mouse_handle_byte(v);
        else                keyboard_handle_scancode(v);
        return true;
    }
}
static bool input_pump(int max_events) {
    int i; bool had = false;
    for (i = 0; i < max_events; ++i) {
        if (!input_poll_once()) break;
        had = true;
    }
    return had;
}
