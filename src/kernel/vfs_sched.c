/* VFS (USTAR initrd + rw overlay) */

static void vfs_init(void) {
    k_memset(g_vfs_files, 0, sizeof(g_vfs_files));
    g_vfs_count = 0;
    g_vfs_mount_dropped_slots = 0;
    g_vfs_mount_dropped_paths = 0;
}

static void vfs_normalize_path(const char *in, char *out, size_t out_size) {
    size_t len = 0;
    if (out_size == 0) return;
    out[0] = 0;
    while (*in && k_is_space(*in)) ++in;
    if (*in != '/') {
        if (len + 1 < out_size) { out[len++] = '/'; out[len] = 0; }
    }
    while (*in && len + 1 < out_size) { out[len++] = *in++; out[len] = 0; }
    if (len == 0) { out[0] = '/'; out[1] = 0; }
}
static vfs_file_t *vfs_find(const char *path) {
    char norm[VFS_MAX_PATH];
    size_t i;
    vfs_normalize_path(path, norm, sizeof(norm));
    for (i = 0; i < VFS_MAX_FILES; ++i) {
        if (g_vfs_files[i].used && k_strcmp(g_vfs_files[i].path, norm) == 0) return &g_vfs_files[i];
    }
    return NULL;
}
static vfs_file_t *vfs_alloc(const char *path) {
    char norm[VFS_MAX_PATH];
    size_t i;
    vfs_file_t *existing;
    vfs_normalize_path(path, norm, sizeof(norm));
    existing = vfs_find(norm);
    if (existing) return existing;
    for (i = 0; i < VFS_MAX_FILES; ++i) {
        if (!g_vfs_files[i].used) {
            g_vfs_files[i].used = true;
            g_vfs_files[i].writable = true;
            g_vfs_files[i].ro_data = NULL;
            g_vfs_files[i].ro_size = 0;
            g_vfs_files[i].rw_data = NULL;
            g_vfs_files[i].rw_cap = 0;
            g_vfs_files[i].rw_size = 0;
            k_strlcpy(g_vfs_files[i].path, norm, sizeof(g_vfs_files[i].path));
            ++g_vfs_count;
            return &g_vfs_files[i];
        }
    }
    return NULL;
}
static bool vfs_ensure_rw_capacity(vfs_file_t *f, uint32_t need) {
    char *next;
    uint32_t cap;
    if (!f) return false;
    if (need == 0) need = 1;
    if (need > VFS_RW_MAX) return false;
    if (f->rw_data && f->rw_cap >= need) return true;
    cap = 256u;
    while (cap < need && cap < VFS_RW_MAX) cap <<= 1;
    if (cap > VFS_RW_MAX) cap = VFS_RW_MAX;
    if (cap < need) return false;
    next = (char *)kmalloc(cap);
    if (!next) return false;
    k_memset(next, 0, cap);
    if (f->rw_data && f->rw_size) {
        uint32_t n = f->rw_size;
        if (n >= cap) n = cap - 1u;
        k_memcpy(next, f->rw_data, n);
        next[n] = 0;
    }
    if (f->rw_data) kfree(f->rw_data);
    f->rw_data = next;
    f->rw_cap = cap;
    return true;
}
static bool vfs_promote_writable(vfs_file_t *f) {
    if (!f) return false;
    if (f->writable) return f->rw_data != NULL || vfs_ensure_rw_capacity(f, f->rw_size + 1u);
    if (f->ro_size >= VFS_RW_MAX) return false;
    if (!vfs_ensure_rw_capacity(f, f->ro_size + 1u)) return false;
    if (f->ro_data && f->ro_size) k_memcpy(f->rw_data, f->ro_data, f->ro_size);
    f->rw_size = f->ro_size;
    f->rw_data[f->rw_size] = 0;
    f->writable = true;
    return true;
}
static bool vfs_touch(const char *p) {
    vfs_file_t *f = vfs_alloc(p);
    if (!f) return false;
    if (!f->writable) return vfs_promote_writable(f);
    return true;
}
static bool vfs_write(const char *p, const char *text) {
    size_t len = k_strlen(text);
    vfs_file_t *f = vfs_alloc(p);
    if (!f) return false;
    if (!vfs_promote_writable(f)) return false;
    if (len >= VFS_RW_MAX) len = VFS_RW_MAX - 1;
    if (!vfs_ensure_rw_capacity(f, (uint32_t)len + 1u)) return false;
    k_memcpy(f->rw_data, text, len);
    f->rw_data[len] = 0;
    f->rw_size = (uint32_t)len;
    return true;
}
static bool vfs_write_bytes(const char *p, const uint8_t *data, uint32_t size) {
    uint32_t len = size;
    vfs_file_t *f = vfs_alloc(p);
    if (!f) return false;
    if (!vfs_promote_writable(f)) return false;
    if (len >= VFS_RW_MAX) len = VFS_RW_MAX - 1;
    if (!vfs_ensure_rw_capacity(f, len + 1u)) return false;
    if (len && data) k_memcpy(f->rw_data, data, len);
    else if (len) k_memset(f->rw_data, 0, len);
    f->rw_data[len] = 0;
    f->rw_size = len;
    return true;
}
static bool vfs_remove(const char *p) {
    vfs_file_t *f = vfs_find(p);
    if (!f) return false;
    f->used = false;
    f->writable = false;
    f->path[0] = 0;
    f->ro_data = NULL;
    f->ro_size = 0;
    if (f->rw_data) kfree(f->rw_data);
    f->rw_data = NULL;
    f->rw_cap = 0;
    f->rw_size = 0;
    if (g_vfs_count > 0) --g_vfs_count;
    return true;
}
static bool vfs_rename(const char *src, const char *dst) {
    char src_norm[VFS_MAX_PATH];
    char dst_norm[VFS_MAX_PATH];
    vfs_file_t *from;
    vfs_file_t *to;
    vfs_normalize_path(src, src_norm, sizeof(src_norm));
    vfs_normalize_path(dst, dst_norm, sizeof(dst_norm));
    if (k_strcmp(src_norm, dst_norm) == 0) return true;
    from = vfs_find(src_norm);
    if (!from) return false;
    to = vfs_find(dst_norm);
    if (to && to != from) {
        if (!vfs_remove(dst_norm)) return false;
    }
    k_strlcpy(from->path, dst_norm, sizeof(from->path));
    return true;
}
static bool vfs_read(const char *p, const uint8_t **data, uint32_t *size) {
    vfs_file_t *f = vfs_find(p);
    if (!f) return false;
    if (f->writable) { *data = (const uint8_t *)(f->rw_data ? f->rw_data : ""); *size = f->rw_size; }
    else             { *data = f->ro_data; *size = f->ro_size; }
    return true;
}
static size_t vfs_count(void) { return g_vfs_count; }

/* Non-static wrappers so compat3.c can access VFS */
bool kvfs_read(const char *p, const uint8_t **data, uint32_t *size) { return vfs_read(p, data, size); }
bool kvfs_write(const char *p, const char *text) { return vfs_write(p, text); }
bool kvfs_write_bytes(const char *p, const uint8_t *data, uint32_t size) { return vfs_write_bytes(p, data, size); }
bool kvfs_exists(const char *p) { return vfs_find(p) != 0; }
bool kvfs_remove(const char *p) { return vfs_remove(p); }
bool kvfs_rename(const char *src, const char *dst) { return vfs_rename(src, dst); }
static bool vfs_list_has_name(const char *list, size_t list_len, const char *name, size_t name_len) {
    size_t i = 0;
    while (i < list_len) {
        size_t j = i;
        while (j < list_len && list[j] != '\n') ++j;
        if ((j - i) == name_len) {
            size_t k;
            bool same = true;
            for (k = 0; k < name_len; ++k) {
                if (list[i + k] != name[k]) { same = false; break; }
            }
            if (same) return true;
        }
        i = (j < list_len) ? (j + 1) : j;
    }
    return false;
}
bool kvfs_listdir(const char *dir, char *out, uint32_t out_cap) {
    char norm[VFS_MAX_PATH];
    char prefix[VFS_MAX_PATH];
    size_t used = 0;
    size_t dl;
    size_t i;
    if (!out || out_cap == 0) return false;
    out[0] = 0;
    if (!dir || !dir[0]) dir = "/";
    vfs_normalize_path(dir, norm, sizeof(norm));
    k_strlcpy(prefix, norm, sizeof(prefix));
    dl = k_strlen(prefix);
    if (k_strcmp(prefix, "/") != 0 && dl + 1 < sizeof(prefix) && prefix[dl - 1] != '/') {
        prefix[dl++] = '/';
        prefix[dl] = 0;
    }
    for (i = 0; i < VFS_MAX_FILES; ++i) {
        const char *path;
        const char *rest;
        char name[VFS_MAX_PATH];
        size_t n = 0;
        if (!g_vfs_files[i].used) continue;
        path = g_vfs_files[i].path;
        if (k_strcmp(norm, "/") == 0) {
            if (path[0] != '/') continue;
            rest = path + 1;
        } else {
            if (!k_starts_with(path, prefix)) continue;
            rest = path + dl;
        }
        if (!rest[0]) continue;
        while (rest[n] && rest[n] != '/' && n + 1 < sizeof(name)) {
            name[n] = rest[n];
            ++n;
        }
        if (n == 0) continue;
        name[n] = 0;
        if (vfs_list_has_name(out, used, name, n)) continue;
        if (used + n + 2 > (size_t)out_cap) break;
        if (used) out[used++] = '\n';
        k_memcpy(out + used, name, n);
        used += n;
        out[used] = 0;
    }
    return true;
}

static void vfs_mount_initrd(const uint8_t *start, const uint8_t *end) {
    const uint8_t *ptr = start;
    while (ptr + sizeof(tar_header_t) <= end) {
        const tar_header_t *h = (const tar_header_t *)ptr;
        bool empty = true;
        uint32_t size;
        const uint8_t *fd;
        uint32_t aligned;
        size_t i;
        for (i = 0; i < sizeof(tar_header_t); ++i) if (ptr[i] != 0) { empty = false; break; }
        if (empty) break;
        size = parse_octal(h->size, sizeof(h->size));
        fd = ptr + 512u;
        aligned = (size + 511u) & ~511u;
        if ((h->typeflag == '0' || h->typeflag == 0) && h->name[0]) {
            char path[VFS_MAX_PATH];
            size_t len = 0;
            const char *prefix = h->prefix;
            const char *name = h->name;
            bool truncated = false;
            vfs_file_t *f;
            if (name[0] == '.' && name[1] == '/') name += 2;
            path[0] = '/';
            len = 1;
            if (prefix[0]) {
                while (*prefix && len + 1 < sizeof(path)) path[len++] = *prefix++;
                if (*prefix) truncated = true;
                if (path[len - 1] != '/') {
                    if (len + 1 < sizeof(path)) path[len++] = '/';
                    else truncated = true;
                }
            }
            while (*name && len + 1 < sizeof(path)) path[len++] = *name++;
            if (*name) truncated = true;
            path[len] = 0;
            if (truncated) {
                ++g_vfs_mount_dropped_paths;
            } else {
                f = vfs_alloc(path);
                if (f) {
                    f->writable = false;
                    f->ro_data = (const uint8_t *)PHYS_TO_DMAP((uint64_t)(uintptr_t)fd);
                    f->ro_size = size;
                    if (f->rw_data) kfree(f->rw_data);
                    f->rw_data = NULL;
                    f->rw_cap = 0;
                    f->rw_size = 0;
                } else {
                    ++g_vfs_mount_dropped_slots;
                }
            }
        }
        if (fd + aligned < fd || fd + aligned > end) break;
        ptr = fd + aligned;
    }
}

static void vfs_seed_defaults(void) {
    vfs_write("/home/readme.txt",
              "RiduxOS Unix (Windows 11 edition)\n"
              "Own kernel + own shell + own Flush + own WM.\n"
              "Glassmorphism UI, pluggable drivers, ELF32 userland.\n");
    vfs_write("/tmp/todo.txt",
              "1) expand apps\n2) paint canvas\n3) add NET driver\n");
    vfs_write("/tmp/notes.txt",
              "Ridux Notes\n"
              "- write /tmp/notes.txt <texto>\n"
              "- open notes\n");
    vfs_write("/etc/motd.txt",
              "Welcome to RiduxOS Unix 0.4 Bloom.\n");
    vfs_write("/proc/asound/cards",
              " 0 [Intel          ]: HDA-Intel - HDA Intel\n"
              "                      HDA Intel at 0xf0000000 irq 30\n");
    vfs_write("/proc/asound/version",
              "Advanced Linux Sound Architecture Driver Version k6.1.0.\n");
    vfs_write("/proc/asound/card0/id", "Intel\n");
    vfs_write("/dev/snd/controlC0", "");
    vfs_write("/dev/snd/pcmC0D0p", "");
    vfs_write("/dev/snd/pcmC0D0c", "");
    vfs_write("/run/user/1000/pulse/native", "");
    vfs_write("/etc/pulse/client.conf",
              "autospawn = no\ndaemon-binary = /bin/true\n");
}
/* ELF32 loader + scheduler */

typedef enum {
    ELF_LOAD_OK = 0,
    ELF_LOAD_NOTFOUND,
    ELF_LOAD_BAD_FORMAT,
    ELF_LOAD_TOO_LARGE,
    ELF_LOAD_NO_SLOT
} elf_load_result_t;

static void elf_init(void) { k_memset(g_elf_images, 0, sizeof(g_elf_images)); }

static int elf_find_by_path(const char *path) {
    int i;
    char n[VFS_MAX_PATH];
    vfs_normalize_path(path, n, sizeof(n));
    for (i = 0; i < ELF_MAX_IMAGES; ++i)
        if (g_elf_images[i].used && k_strcmp(g_elf_images[i].path, n) == 0) return i;
    return -1;
}
static int elf_alloc_slot(const char *path) {
    int i;
    int ex = elf_find_by_path(path);
    char n[VFS_MAX_PATH];
    if (ex >= 0) return ex;
    vfs_normalize_path(path, n, sizeof(n));
    for (i = 0; i < ELF_MAX_IMAGES; ++i) {
        if (!g_elf_images[i].used) {
            g_elf_images[i].used = true;
            k_strlcpy(g_elf_images[i].path, n, sizeof(g_elf_images[i].path));
            g_elf_images[i].base_vaddr = 0;
            g_elf_images[i].entry_vaddr = 0;
            g_elf_images[i].mem_size = 0;
            g_elf_images[i].loaded_segments = 0;
            k_memset(g_elf_images[i].memory, 0, sizeof(g_elf_images[i].memory));
            return i;
        }
    }
    return -1;
}
static elf_load_result_t elf_load_from_vfs(const char *path, int *slot_out) {
    const uint8_t *data;
    uint32_t size;
    const elf32_ehdr_t *eh;
    uint32_t minv = 0xFFFFFFFFu, maxv = 0u;
    uint32_t i;
    bool has_load = false;
    uint32_t image_size;
    int slot;

    if (!vfs_read(path, &data, &size)) return ELF_LOAD_NOTFOUND;
    if (size < sizeof(elf32_ehdr_t)) return ELF_LOAD_BAD_FORMAT;
    eh = (const elf32_ehdr_t *)data;
    if (!(eh->e_ident[0] == 0x7F && eh->e_ident[1] == 'E' && eh->e_ident[2] == 'L' &&
          eh->e_ident[3] == 'F' && eh->e_ident[4] == 1 && eh->e_ident[5] == 1))
        return ELF_LOAD_BAD_FORMAT;
    if (eh->e_phentsize != sizeof(elf32_phdr_t) || eh->e_phnum == 0) return ELF_LOAD_BAD_FORMAT;
    if (eh->e_phoff + (uint32_t)eh->e_phnum * sizeof(elf32_phdr_t) > size) return ELF_LOAD_BAD_FORMAT;

    for (i = 0; i < eh->e_phnum; ++i) {
        const elf32_phdr_t *ph =
            (const elf32_phdr_t *)(data + eh->e_phoff + i * (uint32_t)sizeof(elf32_phdr_t));
        if (ph->p_type != 1) continue;
        if (ph->p_filesz > ph->p_memsz) return ELF_LOAD_BAD_FORMAT;
        if (ph->p_offset + ph->p_filesz > size) return ELF_LOAD_BAD_FORMAT;
        if (ph->p_memsz == 0) continue;
        if (ph->p_vaddr < minv) minv = ph->p_vaddr;
        if (ph->p_vaddr + ph->p_memsz > maxv) maxv = ph->p_vaddr + ph->p_memsz;
        has_load = true;
    }
    if (!has_load || minv >= maxv) return ELF_LOAD_BAD_FORMAT;
    image_size = maxv - minv;
    if (image_size == 0 || image_size > ELF_IMAGE_MEM_MAX) return ELF_LOAD_TOO_LARGE;
    if (eh->e_entry < minv || eh->e_entry >= maxv) return ELF_LOAD_BAD_FORMAT;

    slot = elf_alloc_slot(path);
    if (slot < 0) return ELF_LOAD_NO_SLOT;

    g_elf_images[slot].base_vaddr  = minv;
    g_elf_images[slot].entry_vaddr = eh->e_entry;
    g_elf_images[slot].mem_size    = image_size;
    g_elf_images[slot].loaded_segments = 0;
    k_memset(g_elf_images[slot].memory, 0, sizeof(g_elf_images[slot].memory));

    for (i = 0; i < eh->e_phnum; ++i) {
        const elf32_phdr_t *ph =
            (const elf32_phdr_t *)(data + eh->e_phoff + i * (uint32_t)sizeof(elf32_phdr_t));
        if (ph->p_type == 1 && ph->p_memsz > 0) {
            uint32_t off = ph->p_vaddr - minv;
            k_memcpy(g_elf_images[slot].memory + off, data + ph->p_offset, ph->p_filesz);
            g_elf_images[slot].loaded_segments += 1;
        }
    }
    *slot_out = slot;
    return ELF_LOAD_OK;
}

static const char *proc_state_name(proc_state_t s) {
    switch (s) {
    case PROC_READY: return "READY";
    case PROC_RUNNING: return "RUN";
    case PROC_SLEEP: return "SLEEP";
    default: return "UNUSED";
    }
}
static int proc_count(void) {
    int i, n = 0;
    for (i = 0; i < PROC_MAX; ++i) if (g_processes[i].used) ++n;
    return n;
}
static int elf_loaded_count(void) {
    int i, n = 0;
    for (i = 0; i < ELF_MAX_IMAGES; ++i) if (g_elf_images[i].used) ++n;
    return n;
}
static int proc_running_pid(void) {
    if (g_proc_current >= 0 && g_proc_current < PROC_MAX && g_processes[g_proc_current].used)
        return g_processes[g_proc_current].pid;
    return -1;
}
static int proc_alloc(const char *name, bool is_user, int elf_slot, uint32_t pc0) {
    int i;
    for (i = 0; i < PROC_MAX; ++i) {
        if (!g_processes[i].used) {
            g_processes[i].used = true;
            g_processes[i].pid = g_next_pid++;
            g_processes[i].state = PROC_READY;
            g_processes[i].cpu_ticks = 0;
            g_processes[i].work_units = 0;
            g_processes[i].is_user = is_user;
            g_processes[i].elf_slot = elf_slot;
            g_processes[i].user_pc = pc0;
            k_strlcpy(g_processes[i].name, name, sizeof(g_processes[i].name));
            return g_processes[i].pid;
        }
    }
    return -1;
}
static int proc_spawn(const char *name) { return proc_alloc(name, false, -1, 0); }
static int proc_spawn_elf(const char *path, int slot) {
    char name[24];
    size_t len = 0;
    size_t i;
    name[0] = 0;
    k_append_str(name, &len, sizeof(name), "elf:");
    for (i = 0; path[i] && len + 1 < sizeof(name); ++i) {
        char c = path[i];
        if (c == '/') c = '.';
        name[len++] = c;
        name[len] = 0;
    }
    if (slot < 0 || slot >= ELF_MAX_IMAGES || !g_elf_images[slot].used) return -1;
    return proc_alloc(name, true, slot, g_elf_images[slot].entry_vaddr - g_elf_images[slot].base_vaddr);
}
static bool proc_kill(int pid) {
    int i;
    for (i = 0; i < PROC_MAX; ++i) {
        if (g_processes[i].used && g_processes[i].pid == pid) {
            g_processes[i].used = false;
            g_processes[i].state = PROC_UNUSED;
            g_processes[i].is_user = false;
            g_processes[i].elf_slot = -1;
            g_processes[i].user_pc = 0;
            if (g_proc_current == i) g_proc_current = -1;
            return true;
        }
    }
    return false;
}
static void proc_bootstrap(void) {
    k_memset(g_processes, 0, sizeof(g_processes));
    g_proc_current = -1;
    g_next_pid = 1;
    proc_spawn("kernel");
    proc_spawn("compositor");
    proc_spawn("ridux-shell");
    proc_spawn("flushd");
    proc_spawn("ridux-fsd");
    proc_spawn("ps2-inputd");
    proc_spawn("rtc-tickd");
    proc_spawn("pci-bus");
}
static void scheduler_tick(void) {
    int base, i;
    if (g_proc_current >= 0 && g_proc_current < PROC_MAX && g_processes[g_proc_current].used &&
        g_processes[g_proc_current].state == PROC_RUNNING) {
        g_processes[g_proc_current].state = PROC_READY;
    }
    base = (g_proc_current < 0) ? 0 : g_proc_current;
    for (i = 0; i < PROC_MAX; ++i) {
        int idx = (base + 1 + i) % PROC_MAX;
        if (g_processes[idx].used && g_processes[idx].state == PROC_READY) {
            process_t *p = &g_processes[idx];
            g_proc_current = idx;
            p->state = PROC_RUNNING;
            p->cpu_ticks += 1;
            if (p->is_user && p->elf_slot >= 0 && p->elf_slot < ELF_MAX_IMAGES &&
                g_elf_images[p->elf_slot].used && g_elf_images[p->elf_slot].mem_size > 0) {
                elf_image_t *im = &g_elf_images[p->elf_slot];
                uint32_t pc = p->user_pc % im->mem_size;
                uint8_t op = im->memory[pc];
                p->user_pc = (pc + 1) % im->mem_size;
                p->work_units += (uint32_t)(op & 0x0Fu) + 1u;
            } else {
                p->work_units += (uint32_t)(idx + 3);
            }
            return;
        }
    }
    g_proc_current = -1;
}
