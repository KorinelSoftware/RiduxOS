/* Core utilities */

static inline uint8_t inb(uint16_t port) {
    uint8_t r;
    __asm__ volatile("inb %1, %0" : "=a"(r) : "Nd"(port));
    return r;
}
static inline void outb(uint16_t port, uint8_t v) {
    __asm__ volatile("outb %0, %1" : : "a"(v), "Nd"(port));
}
static inline uint16_t inw(uint16_t port) {
    uint16_t r;
    __asm__ volatile("inw %1, %0" : "=a"(r) : "Nd"(port));
    return r;
}
static inline void outw(uint16_t port, uint16_t v) {
    __asm__ volatile("outw %0, %1" : : "a"(v), "Nd"(port));
}
static inline uint32_t inl(uint16_t port) {
    uint32_t r;
    __asm__ volatile("inl %1, %0" : "=a"(r) : "Nd"(port));
    return r;
}
static inline void outl(uint16_t port, uint32_t v) {
    __asm__ volatile("outl %0, %1" : : "a"(v), "Nd"(port));
}
static inline void io_wait(void) {
    outb(0x80, 0);
}
static inline void cpuid_count(uint32_t leaf, uint32_t subleaf,
                               uint32_t *a, uint32_t *b, uint32_t *c, uint32_t *d) {
    uint32_t ra, rb, rc, rd;
    __asm__ volatile("cpuid"
                     : "=a"(ra), "=b"(rb), "=c"(rc), "=d"(rd)
                     : "a"(leaf), "c"(subleaf));
    if (a) *a = ra;
    if (b) *b = rb;
    if (c) *c = rc;
    if (d) *d = rd;
}
static inline uint64_t rdmsr64(uint32_t msr) {
    uint32_t lo, hi;
    __asm__ volatile("rdmsr" : "=a"(lo), "=d"(hi) : "c"(msr));
    return ((uint64_t)hi << 32) | lo;
}

static void *k_memset(void *dst, int value, size_t size) {
    uint8_t *p = (uint8_t *)dst;
    size_t i;
    for (i = 0; i < size; ++i) p[i] = (uint8_t)value;
    return dst;
}
static int k_memcmp_bytes(const void *a, const void *b, size_t n) {
    const uint8_t *pa = (const uint8_t *)a;
    const uint8_t *pb = (const uint8_t *)b;
    size_t i;
    for (i = 0; i < n; ++i) {
        if (pa[i] != pb[i]) return (int)pa[i] - (int)pb[i];
    }
    return 0;
}

static void *k_memcpy(void *dst, const void *src, size_t size) {
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    size_t i;
    for (i = 0; i < size; ++i) d[i] = s[i];
    return dst;
}
static size_t k_strlen(const char *s) {
    size_t n = 0;
    while (s[n]) ++n;
    return n;
}
static int k_strcmp(const char *a, const char *b) {
    while (*a && *b && *a == *b) { ++a; ++b; }
    return (int)((unsigned char)*a - (unsigned char)*b);
}
static int k_strncmp(const char *a, const char *b, size_t n) {
    size_t i;
    for (i = 0; i < n; ++i) {
        unsigned char ac = (unsigned char)a[i];
        unsigned char bc = (unsigned char)b[i];
        if (ac != bc) return (int)ac - (int)bc;
        if (ac == 0) return 0;
    }
    return 0;
}
static char k_ascii_lower(char c) {
    if (c >= 'A' && c <= 'Z') return (char)(c - 'A' + 'a');
    return c;
}
static int k_strcasecmp_ascii(const char *a, const char *b) {
    while (*a && *b) {
        unsigned char ac = (unsigned char)k_ascii_lower(*a);
        unsigned char bc = (unsigned char)k_ascii_lower(*b);
        if (ac != bc) return (int)ac - (int)bc;
        ++a; ++b;
    }
    return (int)(unsigned char)k_ascii_lower(*a) - (int)(unsigned char)k_ascii_lower(*b);
}
static void k_strlcpy(char *dst, const char *src, size_t dst_size) {
    size_t i = 0;
    if (dst_size == 0) return;
    while (i + 1 < dst_size && src[i]) { dst[i] = src[i]; ++i; }
    dst[i] = 0;
}
static bool k_is_space(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}
static bool k_starts_with(const char *t, const char *p) {
    size_t n = k_strlen(p);
    return k_strncmp(t, p, n) == 0;
}
static bool k_contains(const char *text, const char *needle) {
    size_t i, nlen;
    if (!text || !needle) return false;
    nlen = k_strlen(needle);
    if (nlen == 0) return true;
    for (i = 0; text[i]; ++i) {
        if (k_strncmp(text + i, needle, nlen) == 0) return true;
    }
    return false;
}
extern bool kvfs_exists(const char *path);
static bool k_desktop_debug_trace_enabled(void) {
    static int cached = -1;
    if (cached < 0) {
        cached = (kvfs_exists("/etc/ridux-wayfire-debug.enable") ||
                  kvfs_exists("/etc/ridux-hyprland-debug.enable")) ? 1 : 0;
    }
    return cached != 0;
}
static void k_append_str(char *dst, size_t *len, size_t cap, const char *src) {
    while (*src && *len + 1 < cap) { dst[(*len)++] = *src++; }
    dst[*len] = 0;
}
static void k_append_ch(char *dst, size_t *len, size_t cap, char c) {
    if (*len + 1 < cap) { dst[(*len)++] = c; dst[*len] = 0; }
}
static void k_append_u32(char *dst, size_t *len, size_t cap, uint32_t v) {
    char tmp[12];
    size_t i = 0, j;
    if (v == 0) { k_append_ch(dst, len, cap, '0'); return; }
    while (v && i < sizeof(tmp)) { tmp[i++] = (char)('0' + (v % 10u)); v /= 10u; }
    for (j = i; j > 0; --j) k_append_ch(dst, len, cap, tmp[j - 1]);
}
static void k_append_u32_pad(char *dst, size_t *len, size_t cap, uint32_t v, int width, char fill) {
    char tmp[12];
    size_t i = 0, j;
    if (v == 0) tmp[i++] = '0';
    while (v && i < sizeof(tmp)) { tmp[i++] = (char)('0' + (v % 10u)); v /= 10u; }
    while ((int)i < width) tmp[i++] = fill;
    for (j = i; j > 0; --j) k_append_ch(dst, len, cap, tmp[j - 1]);
}
static void k_append_i32(char *dst, size_t *len, size_t cap, int32_t v) {
    if (v < 0) { k_append_ch(dst, len, cap, '-'); k_append_u32(dst, len, cap, (uint32_t)-v); }
    else       { k_append_u32(dst, len, cap, (uint32_t)v); }
}
static void k_append_hex(char *dst, size_t *len, size_t cap, uint32_t v, int digits) {
    int i;
    static const char *hx = "0123456789ABCDEF";
    for (i = digits - 1; i >= 0; --i) {
        k_append_ch(dst, len, cap, hx[(v >> (i * 4)) & 0xF]);
    }
}
static void k_append_hex64(char *dst, size_t *len, size_t cap, uint64_t v, int digits) {
    int i;
    static const char *hx = "0123456789ABCDEF";
    for (i = digits - 1; i >= 0; --i) {
        k_append_ch(dst, len, cap, hx[(v >> ((uint32_t)i * 4u)) & 0xFULL]);
    }
}
static int k_atoi(const char *s) {
    int sign = 1, v = 0;
    while (k_is_space(*s)) ++s;
    if (*s == '-') { sign = -1; ++s; }
    else if (*s == '+') ++s;
    while (*s >= '0' && *s <= '9') { v = v * 10 + (*s - '0'); ++s; }
    return v * sign;
}
static int k_max_i(int a, int b) { return a > b ? a : b; }
static int k_min_i(int a, int b) { return a < b ? a : b; }
static int k_clamp_i(int v, int lo, int hi) { if (v < lo) return lo; if (v > hi) return hi; return v; }
static int k_abs_i(int v) { return v < 0 ? -v : v; }
static uint32_t k_isqrt(uint32_t n) {
    /* integer sqrt via newton */
    uint32_t x = n, y = 1;
    if (n == 0) return 0;
    while (x > y) { x = (x + y) / 2; y = n / (x == 0 ? 1 : x); }
    return x;
}
static int k_split_tokens(char *buffer, char *argv[], int max_argv) {
    int argc = 0;
    char *p = buffer;
    while (*p && argc < max_argv) {
        while (*p && k_is_space(*p)) ++p;
        if (!*p) break;
        argv[argc++] = p;
        while (*p && !k_is_space(*p)) ++p;
        if (!*p) break;
        *p++ = 0;
    }
    return argc;
}
static uint32_t parse_octal(const char *buf, size_t max_len) {
    uint32_t v = 0;
    size_t i;
    for (i = 0; i < max_len; ++i) {
        char c = buf[i];
        if (c == 0 || c == ' ') break;
        if (c < '0' || c > '7') break;
        v = (v << 3) + (uint32_t)(c - '0');
    }
    return v;
}

/* Pseudo-random (LCG) */
static uint32_t g_rand_state = 0xC0FFEEu;
static uint32_t k_rand(void) {
    g_rand_state = g_rand_state * 1103515245u + 12345u;
    return g_rand_state;
}

static uint32_t smp_detect_logical_cpu_count(uint32_t *threads_per_core, uint32_t *cores_per_pkg) {
    uint32_t max_basic = 0, b = 0, c = 0;
    uint32_t logical = 1;
    uint32_t smt = 1;
    uint32_t core_level = 1;
    uint32_t sub;

    if (threads_per_core) *threads_per_core = 1;
    if (cores_per_pkg) *cores_per_pkg = 1;
    cpuid_count(0, 0, &max_basic, 0, 0, 0);
    if (max_basic >= 0x1Fu) {
        for (sub = 0; sub < 8; ++sub) {
            cpuid_count(0x1Fu, sub, 0, &b, &c, 0);
            if ((b & 0xFFFFu) == 0u) break;
            if (((c >> 8) & 0xFFu) == 1u) smt = b & 0xFFFFu;
            if (((c >> 8) & 0xFFu) != 0u) {
                core_level = b & 0xFFFFu;
                logical = core_level;
            }
        }
    } else if (max_basic >= 0x0Bu) {
        for (sub = 0; sub < 8; ++sub) {
            cpuid_count(0x0Bu, sub, 0, &b, &c, 0);
            if ((b & 0xFFFFu) == 0u) break;
            if (((c >> 8) & 0xFFu) == 1u) smt = b & 0xFFFFu;
            if (((c >> 8) & 0xFFu) != 0u) {
                core_level = b & 0xFFFFu;
                logical = core_level;
            }
        }
    }
    if (logical <= 1u && max_basic >= 1u) {
        cpuid_count(1, 0, 0, &b, 0, 0);
        logical = (b >> 16) & 0xFFu;
    }
    if (logical == 0u) logical = 1u;
    if (smt == 0u || smt > logical) smt = 1u;
    if (threads_per_core) *threads_per_core = smt;
    if (cores_per_pkg) *cores_per_pkg = logical / smt ? logical / smt : 1u;
    return logical;
}

static uint32_t smp_current_apic_id(void) {
    uint32_t max_basic = 0, b = 0, d = 0;
    cpuid_count(0, 0, &max_basic, 0, 0, 0);
    if (max_basic >= 0x1Fu) {
        cpuid_count(0x1Fu, 0, 0, 0, 0, &d);
        return d;
    }
    if (max_basic >= 0x0Bu) {
        cpuid_count(0x0Bu, 0, 0, 0, 0, &d);
        return d;
    }
    cpuid_count(1, 0, 0, &b, 0, 0);
    return (b >> 24) & 0xFFu;
}

static bool acpi_bytes_eq(const char *a, const char *b, size_t n) {
    size_t i;
    for (i = 0; i < n; ++i) if (a[i] != b[i]) return false;
    return true;
}

static bool acpi_checksum_ok(const void *ptr, uint32_t len) {
    const uint8_t *p = (const uint8_t *)ptr;
    uint32_t i;
    uint8_t sum = 0;
    if (!p || len == 0u || len > (1024u * 1024u)) return false;
    for (i = 0; i < len; ++i) sum = (uint8_t)(sum + p[i]);
    return sum == 0u;
}

static const acpi_sdt_header_t *acpi_table_from_phys(uint64_t phys) {
    const acpi_sdt_header_t *h;
    if (!phys) return NULL;
    h = (const acpi_sdt_header_t *)PHYS_TO_DMAP(phys);
    if (h->length < sizeof(acpi_sdt_header_t) || h->length > (1024u * 1024u)) return NULL;
    if (!acpi_checksum_ok(h, h->length)) return NULL;
    return h;
}

static const acpi_sdt_header_t *acpi_find_table(const char sig[4]) {
    const acpi_rsdp_t *rsdp = (const acpi_rsdp_t *)g_acpi_rsdp;
    const acpi_sdt_header_t *root;
    uint32_t i, count;
    if (!g_acpi_rsdp || g_acpi_rsdp_size < 20u) return NULL;
    if (!acpi_bytes_eq(rsdp->signature, "RSD PTR ", 8)) return NULL;
    if (!acpi_checksum_ok(rsdp, 20u)) return NULL;
    if (rsdp->revision >= 2u && g_acpi_rsdp_size >= 36u &&
        rsdp->length >= 36u && rsdp->length <= g_acpi_rsdp_size &&
        acpi_checksum_ok(rsdp, rsdp->length) &&
        rsdp->xsdt_address) {
        root = acpi_table_from_phys(rsdp->xsdt_address);
        if (root && acpi_bytes_eq(root->signature, "XSDT", 4)) {
            const uint64_t *ent = (const uint64_t *)((const uint8_t *)root + sizeof(acpi_sdt_header_t));
            count = (root->length - (uint32_t)sizeof(acpi_sdt_header_t)) / 8u;
            for (i = 0; i < count; ++i) {
                const acpi_sdt_header_t *h = acpi_table_from_phys(ent[i]);
                if (h && acpi_bytes_eq(h->signature, sig, 4)) return h;
            }
        }
    }
    if (!rsdp->rsdt_address) return NULL;
    root = acpi_table_from_phys(rsdp->rsdt_address);
    if (root && acpi_bytes_eq(root->signature, "RSDT", 4)) {
        const uint32_t *ent = (const uint32_t *)((const uint8_t *)root + sizeof(acpi_sdt_header_t));
        count = (root->length - (uint32_t)sizeof(acpi_sdt_header_t)) / 4u;
        for (i = 0; i < count; ++i) {
            const acpi_sdt_header_t *h = acpi_table_from_phys(ent[i]);
            if (h && acpi_bytes_eq(h->signature, sig, 4)) return h;
        }
    }
    return NULL;
}

static void smp_apply_madt_topology(void) {
    const acpi_madt_t *madt = (const acpi_madt_t *)acpi_find_table("APIC");
    const uint8_t *p;
    const uint8_t *end;
    uint32_t ids[SMP_MAX_CPUS];
    uint32_t count = 0;
    uint32_t ioapics = 0;
    uint32_t i;
    if (!madt) return;
    g_madt_detected = true;
    g_madt_lapic_addr = madt->lapic_addr;
    p = (const uint8_t *)madt + sizeof(acpi_madt_t);
    end = (const uint8_t *)madt + madt->h.length;
    while (p + 2 <= end) {
        uint8_t type = p[0];
        uint8_t len = p[1];
        if (len < 2u || p + len > end) break;
        if (type == 0u && len >= 8u) {
            uint8_t apic_id = p[3];
            uint32_t flags = ((uint32_t)p[4]) | ((uint32_t)p[5] << 8) |
                             ((uint32_t)p[6] << 16) | ((uint32_t)p[7] << 24);
            if ((flags & 1u) && count < SMP_MAX_CPUS) ids[count++] = apic_id;
        } else if (type == 9u && len >= 16u) {
            uint32_t apic_id = ((uint32_t)p[4]) | ((uint32_t)p[5] << 8) |
                               ((uint32_t)p[6] << 16) | ((uint32_t)p[7] << 24);
            uint32_t flags = ((uint32_t)p[8]) | ((uint32_t)p[9] << 8) |
                             ((uint32_t)p[10] << 16) | ((uint32_t)p[11] << 24);
            if ((flags & 1u) && count < SMP_MAX_CPUS) ids[count++] = apic_id;
        } else if (type == 1u) {
            ++ioapics;
        }
        p += len;
    }
    if (!count) return;
    g_madt_cpu_count = count;
    g_madt_ioapic_count = ioapics;
    g_cpu_logical_count = count;
    if (g_cpu_threads_per_core == 0u) g_cpu_threads_per_core = 1u;
    g_cpu_core_count = count / g_cpu_threads_per_core;
    if (g_cpu_core_count == 0u) g_cpu_core_count = 1u;
    k_memset(g_cpu_topology, 0, sizeof(g_cpu_topology));
    for (i = 0; i < count && i < SMP_MAX_CPUS; ++i) {
        g_cpu_topology[i].present = true;
        g_cpu_topology[i].logical_id = i;
        g_cpu_topology[i].apic_id = ids[i];
        g_cpu_topology[i].bsp = (ids[i] == g_bsp_apic_id) || (i == 0u && g_bsp_apic_id == 0xFFFFFFFFu);
        g_cpu_topology[i].online = g_cpu_topology[i].bsp;
        g_cpu_topology[i].smt_id = g_cpu_threads_per_core ? (i % g_cpu_threads_per_core) : 0u;
        g_cpu_topology[i].core_id = g_cpu_threads_per_core ? (i / g_cpu_threads_per_core) : i;
        g_cpu_topology[i].package_id = 0;
    }
    g_cpu_online_count = 1;
}

static void platform_probe_cpu_topology(void) {
    uint32_t c = 0, d = 0;
    uint32_t i, visible;

    k_memset(g_cpu_topology, 0, sizeof(g_cpu_topology));
    g_cpu_logical_count = smp_detect_logical_cpu_count(&g_cpu_threads_per_core,
                                                       &g_cpu_core_count);
    g_cpu_online_count = 1;
    g_bsp_apic_id = smp_current_apic_id();

    cpuid_count(1, 0, 0, 0, &c, &d);
    g_lapic_present = (d & (1u << 9)) != 0u;
    g_x2apic_present = (c & (1u << 21)) != 0u;
    g_lapic_base = g_lapic_present ? (rdmsr64(0x1Bu) & 0xFFFFFFFFFFFFF000ull) : 0ull;

    visible = g_cpu_logical_count;
    if (visible == 0u) visible = 1u;
    if (visible > SMP_MAX_CPUS) visible = SMP_MAX_CPUS;
    for (i = 0; i < visible; ++i) {
        g_cpu_topology[i].present = true;
        g_cpu_topology[i].logical_id = i;
        g_cpu_topology[i].apic_id = (i == 0u) ? g_bsp_apic_id : 0xFFFFFFFFu;
        g_cpu_topology[i].bsp = (i == 0u);
        g_cpu_topology[i].online = (i == 0u);
        g_cpu_topology[i].smt_id = g_cpu_threads_per_core ? (i % g_cpu_threads_per_core) : 0u;
        g_cpu_topology[i].core_id = g_cpu_threads_per_core ? (i / g_cpu_threads_per_core) : i;
        g_cpu_topology[i].package_id = 0;
    }
    smp_apply_madt_topology();
    g_smp_topology_detected = true;
}
/* + [5] Graphics moved to src/flush.c */

/* Drivers */

/* Serial COM1 for kernel log */
#define SERIAL_COM1 0x3F8

/* Forward declarations for boot-diagnostic serial helpers defined
 * near the end of this file. They bypass g_serial_ready so callers
 * like drivers_bootstrap() can use them before serial_init runs.
 * Non-static so other TUs (compat.c etc) can emit traces too via
 * the prototypes in compat.h. */
void __boot_serial_init_direct(void);
void __boot_serial_putc(char c);
void __boot_serial_puts(const char *s);
void __boot_serial_puthex64(uint64_t v);
void __boot_serial_putu32(uint32_t v);
void __boot_serial_force_putc(char c);
void __boot_serial_force_puts(const char *s);
void __boot_serial_force_puthex64(uint64_t v);
void __boot_serial_force_putu32(uint32_t v);

static bool serial_probe(void) {
    outb(SERIAL_COM1 + 1, 0x00);
    outb(SERIAL_COM1 + 3, 0x80);
    outb(SERIAL_COM1 + 0, 0x03);
    outb(SERIAL_COM1 + 1, 0x00);
    outb(SERIAL_COM1 + 3, 0x03);
    outb(SERIAL_COM1 + 2, 0xC7);
    outb(SERIAL_COM1 + 4, 0x0B);
    outb(SERIAL_COM1 + 4, 0x1E);
    outb(SERIAL_COM1 + 0, 0xAE);
    if (inb(SERIAL_COM1 + 0) != 0xAE) return false;
    outb(SERIAL_COM1 + 4, 0x0F);
    return true;
}
static void serial_init(void) { g_serial_ready = serial_probe(); }
static void serial_write_byte(uint8_t b) {
    if (!g_serial_ready) return;
    while ((inb(SERIAL_COM1 + 5) & 0x20u) == 0u) { /* wait */ }
    outb(SERIAL_COM1, b);
}
static void serial_write(const char *s) {
    while (*s) {
        if (*s == '\n') serial_write_byte('\r');
        serial_write_byte((uint8_t)*s);
        ++s;
    }
}
static void klog(const char *s) {
    serial_write("[ridux] ");
    serial_write(s);
    serial_write("\n");
}

/* PIT (Programmable Interval Timer) */
#define PIT_CH0  0x40
#define PIT_CMD  0x43

static bool pit_probe(void) { return true; }
static void pit_set_hz(uint32_t hz) {
    uint32_t div;
    if (hz == 0) hz = 100;
    div = 1193182u / hz;
    outb(PIT_CMD, 0x36);
    outb(PIT_CH0, (uint8_t)(div & 0xFFu));
    outb(PIT_CH0, (uint8_t)((div >> 8) & 0xFFu));
    g_pit_hz = hz;
}
static void pit_init(void) { pit_set_hz(100); }

/* RTC / CMOS */
#define CMOS_ADDR 0x70
#define CMOS_DATA 0x71

static uint8_t cmos_read(uint8_t reg) {
    outb(CMOS_ADDR, (uint8_t)(reg | 0x80u));
    return inb(CMOS_DATA);
}
static uint8_t bcd_to_bin(uint8_t v) { return (uint8_t)(((v >> 4) * 10) + (v & 0x0Fu)); }

static bool rtc_probe(void) { return true; }
static void rtc_read(rtc_time_t *t) {
    uint8_t sec, min, hour, day, mon, year, status_b;
    /* wait for update bit clear */
    while (cmos_read(0x0A) & 0x80u) { /* spin */ }
    sec   = cmos_read(0x00);
    min   = cmos_read(0x02);
    hour  = cmos_read(0x04);
    day   = cmos_read(0x07);
    mon   = cmos_read(0x08);
    year  = cmos_read(0x09);
    status_b = cmos_read(0x0B);
    if ((status_b & 0x04u) == 0u) {
        sec  = bcd_to_bin(sec);
        min  = bcd_to_bin(min);
        hour = (uint8_t)(((hour & 0x0Fu) + ((hour & 0x70u) >> 4) * 10u) | (hour & 0x80u));
        day  = bcd_to_bin(day);
        mon  = bcd_to_bin(mon);
        year = bcd_to_bin(year);
    }
    if ((status_b & 0x02u) == 0u && (hour & 0x80u)) {
        hour = (uint8_t)(((hour & 0x7Fu) + 12u) % 24u);
    }
    t->second = sec;
    t->minute = min;
    t->hour   = (uint8_t)(hour & 0x7Fu);
    t->day    = day;
    t->month  = mon;
    t->year   = (uint16_t)(2000u + year);
    t->dow    = 0;
}
static void rtc_init(void) { rtc_read(&g_rtc_now); }
static void rtc_tick(void) { rtc_read(&g_rtc_now); }

/* PC Speaker */
static bool speaker_probe(void) { return true; }
static void speaker_init(void) { /* nothing */ }
static void speaker_beep(uint32_t hz, uint32_t ticks) {
    uint32_t div = 1193182u / (hz == 0 ? 440u : hz);
    uint8_t tmp;
    outb(PIT_CMD, 0xB6);
    outb(0x42, (uint8_t)(div & 0xFFu));
    outb(0x42, (uint8_t)((div >> 8) & 0xFFu));
    tmp = inb(0x61);
    if ((tmp & 3u) != 3u) outb(0x61, tmp | 3u);
    /* crude busy wait using PIT count */
    while (ticks--) { io_wait(); io_wait(); }
    outb(0x61, inb(0x61) & 0xFCu);
}

/* PCI enumeration */
static uint32_t pci_config_read32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t off) {
    uint32_t addr = 0x80000000u |
                    ((uint32_t)bus << 16) |
                    ((uint32_t)slot << 11) |
                    ((uint32_t)func << 8) |
                    ((uint32_t)off & 0xFCu);
    outl(0xCF8, addr);
    return inl(0xCFC);
}
static uint16_t pci_config_read16(uint8_t bus, uint8_t slot, uint8_t func, uint8_t off) {
    uint32_t v = pci_config_read32(bus, slot, func, (uint8_t)(off & 0xFCu));
    return (uint16_t)((v >> ((off & 2u) * 8u)) & 0xFFFFu);
}
static uint8_t pci_config_read8(uint8_t bus, uint8_t slot, uint8_t func, uint8_t off) {
    uint32_t v = pci_config_read32(bus, slot, func, (uint8_t)(off & 0xFCu));
    return (uint8_t)((v >> ((off & 3u) * 8u)) & 0xFFu);
}

static bool pci_probe(void) { return true; }
static void pci_enumerate(void) {
    int b, s, f, bar;
    g_pci_device_count = 0;
    for (b = 0; b < 1; ++b) {
        for (s = 0; s < 32; ++s) {
            for (f = 0; f < 8; ++f) {
                uint16_t vid = pci_config_read16((uint8_t)b, (uint8_t)s, (uint8_t)f, 0);
                uint32_t cls;
                if (vid == 0xFFFF) continue;
                if (g_pci_device_count >= PCI_MAX_DEVICES) break;
                uint16_t did = pci_config_read16((uint8_t)b, (uint8_t)s, (uint8_t)f, 2);
                cls = pci_config_read32((uint8_t)b, (uint8_t)s, (uint8_t)f, 8);
                g_pci_devices[g_pci_device_count].header_type = pci_config_read8((uint8_t)b, (uint8_t)s, (uint8_t)f, 0x0E);
                g_pci_devices[g_pci_device_count].bus = (uint8_t)b;
                g_pci_devices[g_pci_device_count].slot = (uint8_t)s;
                g_pci_devices[g_pci_device_count].func = (uint8_t)f;
                g_pci_devices[g_pci_device_count].vendor_id = vid;
                g_pci_devices[g_pci_device_count].device_id = did;
                for (bar = 0; bar < 6; ++bar) {
                    g_pci_devices[g_pci_device_count].bar[bar] = pci_config_read32((uint8_t)b, (uint8_t)s, (uint8_t)f, (uint8_t)(0x10 + bar * 4));
                }
                g_pci_devices[g_pci_device_count].revision = (uint8_t)(cls & 0xFFu);
                g_pci_devices[g_pci_device_count].prog_if  = (uint8_t)((cls >> 8) & 0xFFu);
                g_pci_devices[g_pci_device_count].subclass = (uint8_t)((cls >> 16) & 0xFFu);
                g_pci_devices[g_pci_device_count].class_code = (uint8_t)((cls >> 24) & 0xFFu);
                __boot_serial_force_puts("[pci] ");
                __boot_serial_force_putu32((uint32_t)b);
                __boot_serial_force_puts(":");
                __boot_serial_force_putu32((uint32_t)s);
                __boot_serial_force_puts(".");
                __boot_serial_force_putu32((uint32_t)f);
                __boot_serial_force_puts(" vid=");
                __boot_serial_force_puthex64((uint64_t)vid);
                __boot_serial_force_puts(" did=");
                __boot_serial_force_puthex64((uint64_t)did);
                __boot_serial_force_puts(" class=");
                __boot_serial_force_puthex64((uint64_t)((cls >> 24) & 0xFFu));
                __boot_serial_force_puts(" sub=");
                __boot_serial_force_puthex64((uint64_t)((cls >> 16) & 0xFFu));
                __boot_serial_force_puts(" if=");
                __boot_serial_force_puthex64((uint64_t)((cls >> 8) & 0xFFu));
                __boot_serial_force_puts(" hdr=");
                __boot_serial_force_puthex64((uint64_t)g_pci_devices[g_pci_device_count].header_type);
                __boot_serial_force_puts(" bar0=");
                __boot_serial_force_puthex64((uint64_t)g_pci_devices[g_pci_device_count].bar[0]);
                __boot_serial_force_puts(" bar1=");
                __boot_serial_force_puthex64((uint64_t)g_pci_devices[g_pci_device_count].bar[1]);
                __boot_serial_force_puts(" bar2=");
                __boot_serial_force_puthex64((uint64_t)g_pci_devices[g_pci_device_count].bar[2]);
                __boot_serial_force_puts("\n");
                ++g_pci_device_count;
                if (f == 0 && (g_pci_devices[g_pci_device_count - 1].header_type & 0x80u) == 0) break;
            }
        }
    }
}
static void pci_init(void) { pci_enumerate(); }

static void gpu_update_status_from_pci(void) {
    int i;
    const char *name = NULL;
    g_gpu_hw_present = false;
    for (i = 0; i < g_pci_device_count; ++i) {
        pci_device_t *d = &g_pci_devices[i];
        if (d->class_code != 0x03u) continue;
        g_gpu_hw_present = true;
        if (d->vendor_id == 0x80EEu) {
            name = "VirtualBox VMSVGA detected";
            break;
        }
        if (d->vendor_id == 0x1234u) {
            name = "Bochs/QEMU VGA detected";
            break;
        }
        if (d->vendor_id == 0x1AF4u) {
            name = "virtio-gpu detected";
            break;
        }
        if (d->vendor_id == 0x8086u) {
            name = "Intel physical GPU detected";
            break;
        }
        if (d->vendor_id == 0x1002u || d->vendor_id == 0x1022u) {
            name = "AMD physical GPU detected";
            break;
        }
        if (d->vendor_id == 0x10DEu) {
            name = "NVIDIA physical GPU detected";
            break;
        }
        name = "PCI display device detected";
    }

    if (g_use_backbuffer && g_fb_fast_bgra) {
        g_gpu_accel_enabled = true;
        g_gpu_accel_kind = name ? name : "linear BGRA framebuffer";
    } else if (g_use_backbuffer) {
        g_gpu_accel_enabled = false;
        g_gpu_accel_kind = name ? "generic framebuffer, slow pixel conversion" :
                                  "software framebuffer, slow pixel conversion";
    } else {
        g_gpu_accel_enabled = false;
        g_gpu_accel_kind = "software fallback (no backbuffer)";
    }
}

/* PS/2 keyboard / mouse */
static bool ps2_probe(void) { return true; }
static int ps2_wait_read_ready(void) {
    int i;
    for (i = 0; i < 100000; ++i) if ((inb(0x64) & 1u) != 0u) return 1;
    return 0;
}
static int ps2_wait_write_ready(void) {
    int i;
    for (i = 0; i < 100000; ++i) if ((inb(0x64) & 2u) == 0u) return 1;
    return 0;
}
static uint8_t mouse_write_cmd(uint8_t v) {
    if (!ps2_wait_write_ready()) return 0;
    outb(0x64, 0xD4);
    if (!ps2_wait_write_ready()) return 0;
    outb(0x60, v);
    if (!ps2_wait_read_ready()) return 0;
    return inb(0x60);
}
static void mouse_init(void) {
    uint8_t config;
    if (!ps2_wait_write_ready()) return;
    outb(0x64, 0xA8);
    if (!ps2_wait_write_ready()) return;
    outb(0x64, 0x20);
    if (!ps2_wait_read_ready()) return;
    config = inb(0x60);
    config |= 0x02u;
    config &= (uint8_t)~0x20u;
    if (!ps2_wait_write_ready()) return;
    outb(0x64, 0x60);
    if (!ps2_wait_write_ready()) return;
    outb(0x60, config);
    mouse_write_cmd(0xF6);
    mouse_write_cmd(0xF4);
    g_mouse_packet_index = 0;
    g_mouse_left_down = false;
    g_mouse_right_down = false;
    g_mouse_dragging = false;
    g_mouse_drag_window_id = -1;
    g_mouse_initialized = true;
}
static void keyboard_init(void) { /* BIOS already sets it up */ }

/* Driver registry */
static void driver_register(const char *name, const char *vendor, driver_kind_t kind,
                            bool (*probe)(void), void (*init)(void)) {
    if (g_driver_count >= DRIVER_MAX) return;
    g_drivers[g_driver_count].name   = name;
    g_drivers[g_driver_count].vendor = vendor;
    g_drivers[g_driver_count].kind   = kind;
    g_drivers[g_driver_count].probe  = probe;
    g_drivers[g_driver_count].init   = init;
    g_drivers[g_driver_count].present = false;
    g_drivers[g_driver_count].ready   = false;
    ++g_driver_count;
}

static void drivers_bootstrap(void) {
    int i;
    g_driver_count = 0;
    driver_register("serial-com1",  "UART 16550",   DRV_KIND_SERIAL,  serial_probe,  serial_init);
    driver_register("pit-8253",     "Intel",        DRV_KIND_TIMER,   pit_probe,     pit_init);
    driver_register("rtc-cmos",     "MC146818",     DRV_KIND_CLOCK,   rtc_probe,     rtc_init);
    driver_register("pc-speaker",   "Generic",      DRV_KIND_SOUND,   speaker_probe, speaker_init);
    driver_register("pci-bus",      "Host Bridge",  DRV_KIND_BUS,     pci_probe,     pci_init);
    driver_register("ps2-keyboard", "i8042",        DRV_KIND_INPUT,   ps2_probe,     keyboard_init);
    driver_register("ps2-mouse",    "i8042",        DRV_KIND_INPUT,   ps2_probe,     mouse_init);
    driver_register("fb-multiboot2","VBE",          DRV_KIND_DISPLAY, 0,             0);
    driver_register("riduxfs",      "Ridux",        DRV_KIND_STORAGE, 0,             0);

    for (i = 0; i < g_driver_count; ++i) {
        __boot_serial_puts("[drv] probing ");
        __boot_serial_puts(g_drivers[i].name ? g_drivers[i].name : "?");
        __boot_serial_puts("\n");
        if (g_drivers[i].probe) {
            g_drivers[i].present = g_drivers[i].probe();
        } else {
            g_drivers[i].present = true;
        }
        __boot_serial_puts(g_drivers[i].present ? "[drv] ...present\n" : "[drv] ...absent\n");
        if (g_drivers[i].present && g_drivers[i].init) {
            __boot_serial_puts("[drv] ...init\n");
            g_drivers[i].init();
            g_drivers[i].ready = true;
            __boot_serial_puts("[drv] ...ready\n");
        }
    }
}

static const char *driver_kind_name(driver_kind_t k) {
    switch (k) {
    case DRV_KIND_INPUT:   return "input";
    case DRV_KIND_DISPLAY: return "display";
    case DRV_KIND_TIMER:   return "timer";
    case DRV_KIND_CLOCK:   return "clock";
    case DRV_KIND_SERIAL:  return "serial";
    case DRV_KIND_BUS:     return "bus";
    case DRV_KIND_STORAGE: return "storage";
    case DRV_KIND_SOUND:   return "sound";
    case DRV_KIND_NET:     return "net";
    default:               return "generic";
    }
}

/* ============================================================
 * [6.5] GDT, IDT, PIC remap, IRQ dispatch, panic screen
 * ------------------------------------------------------------
 * We install our own GDT (flat) and IDT (256 gates). The 32 CPU
 * exceptions point to isr_<n> asm stubs, the 16 IRQs (remapped
 * to vectors 32..47) point to irq_<n> stubs. Both trampolines
 * live in src/isr.S and call isr_dispatch / irq_dispatch.
 * ============================================================ */

/* External asm stubs generated in src/isr.S. */
extern void isr_0(void);  extern void isr_1(void);  extern void isr_2(void);
extern void isr_3(void);  extern void isr_4(void);  extern void isr_5(void);
extern void isr_6(void);  extern void isr_7(void);  extern void isr_8(void);
extern void isr_9(void);  extern void isr_10(void); extern void isr_11(void);
extern void isr_12(void); extern void isr_13(void); extern void isr_14(void);
extern void isr_15(void); extern void isr_16(void); extern void isr_17(void);
extern void isr_18(void); extern void isr_19(void); extern void isr_20(void);
extern void isr_21(void); extern void isr_22(void); extern void isr_23(void);
extern void isr_24(void); extern void isr_25(void); extern void isr_26(void);
extern void isr_27(void); extern void isr_28(void); extern void isr_29(void);
extern void isr_30(void); extern void isr_31(void);

extern void irq_0(void);  extern void irq_1(void);  extern void irq_2(void);
extern void irq_3(void);  extern void irq_4(void);  extern void irq_5(void);
extern void irq_6(void);  extern void irq_7(void);  extern void irq_8(void);
extern void irq_9(void);  extern void irq_10(void); extern void irq_11(void);
extern void irq_12(void); extern void irq_13(void); extern void irq_14(void);
extern void irq_15(void);

static void (* const g_isr_stubs[32])(void) = {
    isr_0,  isr_1,  isr_2,  isr_3,  isr_4,  isr_5,  isr_6,  isr_7,
    isr_8,  isr_9,  isr_10, isr_11, isr_12, isr_13, isr_14, isr_15,
    isr_16, isr_17, isr_18, isr_19, isr_20, isr_21, isr_22, isr_23,
    isr_24, isr_25, isr_26, isr_27, isr_28, isr_29, isr_30, isr_31
};
static void (* const g_irq_stubs[16])(void) = {
    irq_0, irq_1, irq_2,  irq_3,  irq_4,  irq_5,  irq_6,  irq_7,
    irq_8, irq_9, irq_10, irq_11, irq_12, irq_13, irq_14, irq_15
};

static const char *g_exception_names[32] = {
    "Divide-by-zero",             "Debug",                  "NMI",
    "Breakpoint",                 "Overflow",               "Bound range exceeded",
    "Invalid opcode",             "Device not available",   "Double fault",
    "Coprocessor segment overrun","Invalid TSS",            "Segment not present",
    "Stack fault",                "General protection",     "Page fault",
    "(reserved)",                 "x87 floating point",     "Alignment check",
    "Machine check",              "SIMD floating point",    "Virtualization",
    "Control protection",         "(reserved)",             "(reserved)",
    "(reserved)",                 "(reserved)",             "(reserved)",
    "(reserved)",                 "Hypervisor injection",   "VMM communication",
    "Security exception",         "(reserved)"
};

/* GDT */
struct gdt_entry {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t  base_mid;
    uint8_t  access;
    uint8_t  granularity;
    uint8_t  base_high;
} __attribute__((packed));
struct gdt_ptr {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed));

static struct gdt_entry g_gdt[6];
static struct gdt_ptr   g_gdt_ptr;

static void gdt_set_entry(int i, uint32_t base, uint32_t limit, uint8_t access, uint8_t gran) {
    g_gdt[i].base_low    = (uint16_t)(base & 0xFFFF);
    g_gdt[i].base_mid    = (uint8_t)((base >> 16) & 0xFF);
    g_gdt[i].base_high   = (uint8_t)((base >> 24) & 0xFF);
    g_gdt[i].limit_low   = (uint16_t)(limit & 0xFFFF);
    g_gdt[i].granularity = (uint8_t)(((limit >> 16) & 0x0F) | (gran & 0xF0));
    g_gdt[i].access      = access;
}
#if defined(__x86_64__)
static void gdt_install(void) {
    /* Long mode GDT is installed in boot64.S. */
}
#else
static void gdt_install(void) {
    g_gdt_ptr.limit = (uint16_t)(sizeof(g_gdt) - 1);
    g_gdt_ptr.base  = (uint32_t)(uintptr_t)&g_gdt[0];
    gdt_set_entry(0, 0, 0, 0, 0);
    gdt_set_entry(1, 0, 0xFFFFFu, 0x9A, 0xCF); /* kernel code    */
    gdt_set_entry(2, 0, 0xFFFFFu, 0x92, 0xCF); /* kernel data    */
    gdt_set_entry(3, 0, 0xFFFFFu, 0xFA, 0xCF); /* user code (future) */
    gdt_set_entry(4, 0, 0xFFFFFu, 0xF2, 0xCF); /* user data (future) */
    gdt_set_entry(5, 0, 0, 0, 0);              /* TSS slot (future)  */
    __asm__ __volatile__(
        "lgdt %0\n\t"
        "mov $0x10, %%ax\n\t"
        "mov %%ax, %%ds\n\t"
        "mov %%ax, %%es\n\t"
        "mov %%ax, %%fs\n\t"
        "mov %%ax, %%gs\n\t"
        "mov %%ax, %%ss\n\t"
        "ljmp $0x08, $1f\n\t"
        "1:\n\t"
        : : "m"(g_gdt_ptr) : "ax", "memory");
}
#endif

/* IDT */
#if defined(__x86_64__)
struct idt_entry {
    uint16_t offset_low;
    uint16_t sel;
    uint8_t  ist;
    uint8_t  flags;
    uint16_t offset_mid;
    uint32_t offset_high;
    uint32_t zero;
} __attribute__((packed));
struct idt_ptr {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

static struct idt_entry g_idt[256];
static struct idt_ptr   g_idt_ptr;

static void idt_set_gate(uint8_t vec, uint64_t handler, uint16_t sel, uint8_t flags) {
    g_idt[vec].offset_low  = (uint16_t)(handler & 0xFFFFu);
    g_idt[vec].offset_mid  = (uint16_t)((handler >> 16) & 0xFFFFu);
    g_idt[vec].offset_high = (uint32_t)((handler >> 32) & 0xFFFFFFFFu);
    g_idt[vec].sel         = sel;
    g_idt[vec].ist         = 0;
    g_idt[vec].flags       = flags;
    g_idt[vec].zero        = 0;
}
#else
struct idt_entry {
    uint16_t base_low;
    uint16_t sel;
    uint8_t  zero;
    uint8_t  flags;
    uint16_t base_high;
} __attribute__((packed));
struct idt_ptr {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed));

static struct idt_entry g_idt[256];
static struct idt_ptr   g_idt_ptr;

static void idt_set_gate(uint8_t vec, uint32_t handler, uint16_t sel, uint8_t flags) {
    g_idt[vec].base_low  = (uint16_t)(handler & 0xFFFFu);
    g_idt[vec].base_high = (uint16_t)((handler >> 16) & 0xFFFFu);
    g_idt[vec].sel       = sel;
    g_idt[vec].zero      = 0;
    g_idt[vec].flags     = flags;
}
#endif

/* PIC 8259 remap */
static void pic_remap(uint8_t offset_master, uint8_t offset_slave) {
    uint8_t a1 = inb(0x21);
    uint8_t a2 = inb(0xA1);
    outb(0x20, 0x11);                 /* ICW1: init + cascade + edge */
    outb(0xA0, 0x11);
    outb(0x21, offset_master);        /* ICW2: master vector offset */
    outb(0xA1, offset_slave);         /* ICW2: slave vector offset  */
    outb(0x21, 0x04);                 /* ICW3: slave on IRQ2         */
    outb(0xA1, 0x02);                 /* ICW3: slave id 2            */
    outb(0x21, 0x01);                 /* ICW4: 8086 mode             */
    outb(0xA1, 0x01);
    outb(0x21, a1);
    outb(0xA1, a2);
}
static void pic_eoi(uint8_t irq) {
    if (irq >= 8) outb(0xA0, 0x20);
    outb(0x20, 0x20);
}
static void irq_set_mask(uint8_t irq, bool masked) {
    uint16_t port = (irq < 8) ? 0x21 : 0xA1;
    uint8_t shift = (uint8_t)(irq & 7);
    uint8_t val = inb(port);
    if (masked) val |= (uint8_t)(1u << shift);
    else        val &= (uint8_t)~(1u << shift);
    outb(port, val);
}
static void irq_enable(uint8_t irq)  { irq_set_mask(irq, false); }
static void irq_disable_line(uint8_t irq) { irq_set_mask(irq, true); }

/* Input ring buffers (filled by ISRs, drained by main loop) */
#define INPUT_RING_SIZE 256
static volatile uint8_t  g_kbd_ring[INPUT_RING_SIZE];
static volatile uint32_t g_kbd_head, g_kbd_tail;
static volatile uint8_t  g_mouse_ring[INPUT_RING_SIZE];
static volatile uint32_t g_mouse_head, g_mouse_tail;
static volatile uint64_t g_pit_ticks;          /* incremented each PIT IRQ */
static volatile uint32_t g_irq_counts[16];
static volatile bool     g_idt_ready = false;
static bool              g_panic_active = false;

uint64_t ridux_kernel_timer_ticks(void) {
    return g_pit_ticks;
}

uint32_t ridux_kernel_timer_hz(void) {
    return g_pit_hz ? g_pit_hz : 100u;
}

static void kbd_ring_push(uint8_t b) {
    uint32_t next = (g_kbd_head + 1u) & (INPUT_RING_SIZE - 1u);
    if (next == g_kbd_tail) return; /* drop on overflow */
    g_kbd_ring[g_kbd_head] = b;
    g_kbd_head = next;
}
static bool kbd_ring_pop(uint8_t *out) {
    if (g_kbd_tail == g_kbd_head) return false;
    *out = g_kbd_ring[g_kbd_tail];
    g_kbd_tail = (g_kbd_tail + 1u) & (INPUT_RING_SIZE - 1u);
    return true;
}
static void mouse_ring_push(uint8_t b) {
    uint32_t next = (g_mouse_head + 1u) & (INPUT_RING_SIZE - 1u);
    if (next == g_mouse_tail) return;
    g_mouse_ring[g_mouse_head] = b;
    g_mouse_head = next;
}
static bool mouse_ring_pop(uint8_t *out) {
    if (g_mouse_tail == g_mouse_head) return false;
    *out = g_mouse_ring[g_mouse_tail];
    g_mouse_tail = (g_mouse_tail + 1u) & (INPUT_RING_SIZE - 1u);
    return true;
}

/* Forward decls for things we reference inside panic / dispatch. */
static void scheduler_tick(void);
static void rtc_tick(void);
static inline uint32_t rdtsc32(void);
static bool input_pump(int max_events);
static void render_scene(void);
static void render_cursor_only(void);
/* Ridux R3 WM bridge: render_window() (defined earlier) calls into the
 * compose helper which lives in the [9b] section below. */
static void r3wm_compose_window(int window_idx, ui_rect_t body);

/* Minimal panic: paint a big red screen with error + halt. */
static void panic(const char *title, const char *detail) {
    uint32_t red = rgb(160, 24, 40);
    uint32_t ink = rgb(255, 255, 255);
    int cy;
    g_panic_active = true;
    __asm__ __volatile__("cli");
    if (g_fb.ready) {
        flush_reset();
        draw_rect_alpha(0, 0, (int)g_fb.width, (int)g_fb.height, red, 255);
        draw_rect_alpha(20, 20, (int)g_fb.width - 40, 60, rgb(220, 60, 70), 255);
        cy = 40;
        draw_text_scaled(40, cy, ":(  Ridux kernel panic", 2, ink, 255);
        cy = 120;
        if (title)  { draw_text(40, cy, title,  ink, 255); cy += 24; }
        if (detail) { draw_text(40, cy, detail, ink, 230); cy += 24; }
        draw_text(40, cy + 20, "System halted. Press RESET to reboot.", ink, 200);
        fb_present();
    }
    serial_write("PANIC: ");
    if (title)  serial_write(title);
    if (detail) { serial_write(" | "); serial_write(detail); }
    serial_write("\n");
    for (;;) __asm__ __volatile__("cli; hlt");
}

static bool user_qword_ok(task_t *t, uint64_t addr, uint64_t *out) {
    if (!t || !t->addr_space || !out) return false;
    if (addr >= 0x0000800000000000ULL || addr + 7u < addr) return false;
    if (!(paging_get_entry(t->addr_space, addr) & PAGE_PRESENT)) return false;
    if (!(paging_get_entry(t->addr_space, addr + 7u) & PAGE_PRESENT)) return false;
    *out = *(uint64_t *)(uintptr_t)addr;
    return true;
}

static bool user_qword_fault_in_ok(task_t *t, uint64_t addr, uint64_t *out) {
    if (user_qword_ok(t, addr, out)) return true;
    if (!t || t != task_current()) return false;
    (void)compat3_handle_page_fault(addr, 0);
    if (((addr + 7u) & ~(uint64_t)(PAGE_SIZE - 1u)) != (addr & ~(uint64_t)(PAGE_SIZE - 1u))) {
        (void)compat3_handle_page_fault(addr + 7u, 0);
    }
    return user_qword_ok(t, addr, out);
}

static bool user_qword_put_ok(task_t *t, uint64_t addr, uint64_t val) {
    if (!t || !t->addr_space) return false;
    if (addr >= 0x0000800000000000ULL || addr + 7u < addr) return false;
    if (!(paging_get_entry(t->addr_space, addr) & PAGE_PRESENT)) return false;
    if (!(paging_get_entry(t->addr_space, addr + 7u) & PAGE_PRESENT)) return false;
    *(uint64_t *)(uintptr_t)addr = val;
    return true;
}

static bool wayfire_ld_rebuild_linfo(task_t *cur, uint64_t frame_base,
                                     uint32_t vec, uint64_t fault_rip,
                                     uint64_t cr2) {
    enum {
        DT_NULL = 0,
        DT_NUM = 38,
        DT_VALRNGLO = 0x6ffffd00,
        DT_VALRNGHI = 0x6ffffdff,
        DT_VALNUM = 12,
        DT_ADDRRNGLO = 0x6ffffe00,
        DT_ADDRRNGHI = 0x6ffffeff,
        DT_ADDRNUM = 11,
        DT_VERNEEDNUM = 0x6fffffff,
        DT_VERSIONTAGNUM = 16,
        DT_AUXILIARY = 0x7ffffffd,
        DT_FILTER = 0x7fffffff,
        DT_EXTRANUM = 3,
        LINFO_OFF = 0x40
    };
    uint64_t lmap, l_ld, tag, val;
    uint64_t linfo_base;
    uint64_t loader_off;
    uint32_t filled = 0;
    uint32_t i;
    if (vec != 14u || cr2 != 0x8u || !cur || !frame_base) return false;
    if (!cur->name[0] || !k_contains(cur->name, "wayfire")) return false;
    loader_off = (cur->aux_at_base && fault_rip >= cur->aux_at_base) ?
                 (fault_rip - cur->aux_at_base) : fault_rip;
    if (loader_off != 0xeba3u && loader_off != 0xec74u &&
        fault_rip != 0x00000000404d3ba3ULL && fault_rip != 0x00000000404d3c74ULL) {
        return false;
    }
    lmap = *(uint64_t *)(uintptr_t)(frame_base + 0u); /* r15 */
    __boot_serial_force_puts("[wayfire-ld-fix-probe!] rip=");
    __boot_serial_force_puthex64(fault_rip);
    __boot_serial_force_puts(" off=");
    __boot_serial_force_puthex64(loader_off);
    __boot_serial_force_puts(" lmap=");
    __boot_serial_force_puthex64(lmap);
    __boot_serial_force_puts("\n");
    if (!user_qword_fault_in_ok(cur, lmap + 0x10u, &l_ld) || !l_ld) {
        __boot_serial_force_puts("[wayfire-ld-fix-miss!] link_map ld not present lmap=");
        __boot_serial_force_puthex64(lmap);
        __boot_serial_force_puts("\n");
        return false;
    }
    linfo_base = lmap + LINFO_OFF;
    for (i = 0; i < 512u; ++i) {
        uint64_t dyn = l_ld + ((uint64_t)i * 16u);
        uint64_t idx = 0xffffffffffffffffULL;
        if (!user_qword_fault_in_ok(cur, dyn, &tag)) {
            __boot_serial_force_puts("[wayfire-ld-fix-miss!] dynamic tag not present ld=");
            __boot_serial_force_puthex64(l_ld);
            __boot_serial_force_puts(" dyn=");
            __boot_serial_force_puthex64(dyn);
            __boot_serial_force_puts("\n");
            return false;
        }
        if (!user_qword_fault_in_ok(cur, dyn + 8u, &val)) {
            __boot_serial_force_puts("[wayfire-ld-fix-miss!] dynamic val not present ld=");
            __boot_serial_force_puthex64(l_ld);
            __boot_serial_force_puts(" dyn=");
            __boot_serial_force_puthex64(dyn);
            __boot_serial_force_puts("\n");
            return false;
        }
        if (tag == DT_NULL) break;
        if (tag < DT_NUM) {
            idx = tag;
        } else if (tag >= DT_VALRNGLO && tag <= DT_VALRNGHI) {
            idx = (uint64_t)DT_NUM + DT_VERSIONTAGNUM + DT_EXTRANUM +
                  (uint64_t)(DT_VALRNGHI - tag);
        } else if (tag >= DT_ADDRRNGLO && tag <= DT_ADDRRNGHI) {
            idx = (uint64_t)DT_NUM + DT_VERSIONTAGNUM + DT_EXTRANUM +
                  DT_VALNUM + (uint64_t)(DT_ADDRRNGHI - tag);
        } else if (tag >= 0x6ffffff0ULL && tag <= DT_VERNEEDNUM) {
            idx = (uint64_t)DT_NUM + (uint64_t)(DT_VERNEEDNUM - tag);
        } else if (tag == DT_AUXILIARY || tag == DT_FILTER) {
            idx = (uint64_t)DT_NUM + DT_VERSIONTAGNUM +
                  (tag == DT_AUXILIARY ? 1u : 0u);
        }
        if (idx != 0xffffffffffffffffULL && idx < 128u) {
            if (user_qword_put_ok(cur, linfo_base + idx * 8u, dyn)) {
                ++filled;
            }
        }
    }
    if (filled) {
        __boot_serial_force_puts("[wayfire-ld-fix!] rebuilt l_info entries=");
        __boot_serial_force_putu32(filled);
        __boot_serial_force_puts(" lmap=");
        __boot_serial_force_puthex64(lmap);
        __boot_serial_force_puts(" ld=");
        __boot_serial_force_puthex64(l_ld);
        __boot_serial_force_puts(" off=");
        __boot_serial_force_puthex64(loader_off);
        __boot_serial_force_puts("\n");
        return true;
    }
    return false;
}

static bool chrome_task_is_runtime(task_t *cur) {
    if (!cur || !cur->name[0]) return false;
    return k_contains(cur->name, "Chrome_ChildIOThread") ||
           k_contains(cur->name, "Chrome_IOThread") ||
           k_contains(cur->name, "NetworkService") ||
           k_contains(cur->name, "chrome");
}

static bool glibc_secure_getenv_fix(task_t *cur, uint64_t frame_base,
                                    uint64_t fault_rip, uint64_t cr2) {
    static const uint8_t secure_getenv_fault_bytes[] = {
        0x8b, 0x00, 0x85, 0xc0, 0x75, 0x0b,
        0xe9, 0x7e, 0xdd, 0xff, 0xff, 0x66
    };
    const uint8_t *ip;
    int32_t rel;
    uint64_t resume;

    if (!cur || !frame_base || cr2 != 0) return false;
    if (!cur->name[0] || !k_contains(cur->name, "ridux-vulkan-probe")) return false;
    if (!cur->addr_space || fault_rip >= 0x0000800000000000ULL) return false;
    if (!(paging_get_entry(cur->addr_space, fault_rip) & PAGE_PRESENT)) return false;
    if (!(paging_get_entry(cur->addr_space,
                           fault_rip + sizeof(secure_getenv_fault_bytes) - 1u) & PAGE_PRESENT)) {
        return false;
    }

    ip = (const uint8_t *)(uintptr_t)fault_rip;
    if (k_memcmp_bytes(ip, secure_getenv_fault_bytes,
                       sizeof(secure_getenv_fault_bytes)) != 0) {
        return false;
    }

    rel = (int32_t)((uint32_t)ip[7] |
                    ((uint32_t)ip[8] << 8) |
                    ((uint32_t)ip[9] << 16) |
                    ((uint32_t)ip[10] << 24));
    resume = (uint64_t)((int64_t)(fault_rip + 11u) + (int64_t)rel);
    if (resume >= 0x0000800000000000ULL) return false;
    if (!(paging_get_entry(cur->addr_space, resume) & PAGE_PRESENT)) return false;

    *(uint64_t *)(uintptr_t)(frame_base + 136u) = resume;
    __boot_serial_force_puts("[glibc-secure-getenv-fix] pid=");
    __boot_serial_force_putu32((uint32_t)cur->pid);
    __boot_serial_force_puts(" rip=");
    __boot_serial_force_puthex64(fault_rip);
    __boot_serial_force_puts(" resume=");
    __boot_serial_force_puthex64(resume);
    __boot_serial_force_puts("\n");
    return true;
}

static bool chrome_finish_partition_frame(task_t *cur, uint64_t frame_base,
                                          uint64_t outer_rbp,
                                          uint64_t fault_rip,
                                          const char *tag) {
    uint64_t saved_r15, saved_r14, saved_r13, saved_r12, saved_rbx;
    uint64_t saved_rbp, ret;
    if (!frame_base) return false;
    if (!user_qword_ok(cur, outer_rbp - 8u, &saved_r15)) return false;
    if (!user_qword_ok(cur, outer_rbp - 16u, &saved_r14)) return false;
    if (!user_qword_ok(cur, outer_rbp - 24u, &saved_r13)) return false;
    if (!user_qword_ok(cur, outer_rbp - 32u, &saved_r12)) return false;
    if (!user_qword_ok(cur, outer_rbp - 40u, &saved_rbx)) return false;
    if (!user_qword_ok(cur, outer_rbp + 0u, &saved_rbp)) return false;
    if (!user_qword_ok(cur, outer_rbp + 8u, &ret)) return false;
    if (ret < 0x0000000040000000ULL || ret >= 0x0000000060000000ULL) return false;

    *(uint64_t *)(uintptr_t)(frame_base + 0u) = saved_r15;
    *(uint64_t *)(uintptr_t)(frame_base + 8u) = saved_r14;
    *(uint64_t *)(uintptr_t)(frame_base + 16u) = saved_r13;
    *(uint64_t *)(uintptr_t)(frame_base + 24u) = saved_r12;
    *(uint64_t *)(uintptr_t)(frame_base + 64u) = saved_rbp;
    *(uint64_t *)(uintptr_t)(frame_base + 104u) = saved_rbx;
    *(uint64_t *)(uintptr_t)(frame_base + 112u) = 0;
    *(uint64_t *)(uintptr_t)(frame_base + 136u) = ret;
    *(uint64_t *)(uintptr_t)(frame_base + 160u) = outer_rbp + 16u;

    __boot_serial_force_puts("[chrome-pa-skip!] ");
    __boot_serial_force_puts(tag ? tag : "free");
    __boot_serial_force_puts(" pid=");
    __boot_serial_force_putu32((uint32_t)cur->pid);
    __boot_serial_force_puts(" rip=");
    __boot_serial_force_puthex64(fault_rip);
    __boot_serial_force_puts(" ret=");
    __boot_serial_force_puthex64(ret);
    __boot_serial_force_puts("\n");
    return true;
}

static bool chrome_return_current_frame(task_t *cur, uint64_t frame_base,
                                        uint64_t fault_rip,
                                        const char *tag) {
    uint64_t rbp, saved_r15, saved_r14, saved_r13, saved_r12, saved_rbx;
    uint64_t saved_rbp, ret;
    if (!frame_base || !chrome_task_is_runtime(cur)) return false;

    rbp = *(uint64_t *)(uintptr_t)(frame_base + 64u);
    if (!user_qword_ok(cur, rbp - 8u, &saved_r15)) return false;
    if (!user_qword_ok(cur, rbp - 16u, &saved_r14)) return false;
    if (!user_qword_ok(cur, rbp - 24u, &saved_r13)) return false;
    if (!user_qword_ok(cur, rbp - 32u, &saved_r12)) return false;
    if (!user_qword_ok(cur, rbp - 40u, &saved_rbx)) return false;
    if (!user_qword_ok(cur, rbp + 0u, &saved_rbp)) return false;
    if (!user_qword_ok(cur, rbp + 8u, &ret)) return false;
    if (ret < 0x0000000040000000ULL || ret >= 0x0000000060000000ULL) return false;

    *(uint64_t *)(uintptr_t)(frame_base + 0u) = saved_r15;
    *(uint64_t *)(uintptr_t)(frame_base + 8u) = saved_r14;
    *(uint64_t *)(uintptr_t)(frame_base + 16u) = saved_r13;
    *(uint64_t *)(uintptr_t)(frame_base + 24u) = saved_r12;
    *(uint64_t *)(uintptr_t)(frame_base + 64u) = saved_rbp;
    *(uint64_t *)(uintptr_t)(frame_base + 104u) = saved_rbx;
    *(uint64_t *)(uintptr_t)(frame_base + 112u) = 0;
    *(uint64_t *)(uintptr_t)(frame_base + 136u) = ret;
    *(uint64_t *)(uintptr_t)(frame_base + 160u) = rbp + 16u;

    __boot_serial_force_puts("[chrome-pa-helper-skip!] ");
    __boot_serial_force_puts(tag ? tag : "return");
    __boot_serial_force_puts(" pid=");
    __boot_serial_force_putu32((uint32_t)cur->pid);
    __boot_serial_force_puts(" rip=");
    __boot_serial_force_puthex64(fault_rip);
    __boot_serial_force_puts(" ret=");
    __boot_serial_force_puthex64(ret);
    __boot_serial_force_puts("\n");
    return true;
}

static bool chrome_return_tls_frame(task_t *cur, uint64_t frame_base,
                                    uint64_t fault_rip,
                                    const char *tag) {
    uint64_t rbp, saved_r15, saved_r14, saved_rbx, saved_rbp, ret;
    if (!frame_base || !chrome_task_is_runtime(cur)) return false;

    rbp = *(uint64_t *)(uintptr_t)(frame_base + 64u);
    if (!user_qword_ok(cur, rbp - 8u, &saved_r15)) return false;
    if (!user_qword_ok(cur, rbp - 16u, &saved_r14)) return false;
    if (!user_qword_ok(cur, rbp - 24u, &saved_rbx)) return false;
    if (!user_qword_ok(cur, rbp + 0u, &saved_rbp)) return false;
    if (!user_qword_ok(cur, rbp + 8u, &ret)) return false;
    if (ret < 0x0000000040000000ULL || ret >= 0x0000000060000000ULL) return false;

    *(uint64_t *)(uintptr_t)(frame_base + 0u) = saved_r15;
    *(uint64_t *)(uintptr_t)(frame_base + 8u) = saved_r14;
    *(uint64_t *)(uintptr_t)(frame_base + 64u) = saved_rbp;
    *(uint64_t *)(uintptr_t)(frame_base + 104u) = saved_rbx;
    *(uint64_t *)(uintptr_t)(frame_base + 112u) = 0;
    *(uint64_t *)(uintptr_t)(frame_base + 136u) = ret;
    *(uint64_t *)(uintptr_t)(frame_base + 160u) = rbp + 16u;

    __boot_serial_force_puts("[chrome-pa-tls-skip!] ");
    __boot_serial_force_puts(tag ? tag : "tls");
    __boot_serial_force_puts(" pid=");
    __boot_serial_force_putu32((uint32_t)cur->pid);
    __boot_serial_force_puts(" rip=");
    __boot_serial_force_puthex64(fault_rip);
    __boot_serial_force_puts(" ret=");
    __boot_serial_force_puthex64(ret);
    __boot_serial_force_puts("\n");
    return true;
}

static bool chrome_skip_bad_indirect_call(task_t *cur, uint64_t frame_base,
                                          uint64_t fault_rip,
                                          uint64_t cr2,
                                          uint32_t err) {
    uint64_t user_rsp, ret;
    if (!frame_base || !chrome_task_is_runtime(cur)) return false;
    if (fault_rip != cr2 || !(err & 0x10u)) return false;
    if (fault_rip < 0x0000000100000000ULL ||
        fault_rip >= 0x0000800000000000ULL) return false;

    user_rsp = *(uint64_t *)(uintptr_t)(frame_base + 160u);
    if (!user_qword_ok(cur, user_rsp, &ret)) return false;
    if (ret < 0x0000000040000000ULL || ret >= 0x0000000060000000ULL) return false;

    *(uint64_t *)(uintptr_t)(frame_base + 112u) = 0;
    *(uint64_t *)(uintptr_t)(frame_base + 136u) = ret;
    *(uint64_t *)(uintptr_t)(frame_base + 160u) = user_rsp + 8u;

    __boot_serial_force_puts("[chrome-bad-call-skip!] pid=");
    __boot_serial_force_putu32((uint32_t)cur->pid);
    __boot_serial_force_puts(" rip=");
    __boot_serial_force_puthex64(fault_rip);
    __boot_serial_force_puts(" ret=");
    __boot_serial_force_puthex64(ret);
    __boot_serial_force_puts("\n");
    return true;
}

static bool chrome_skip_partitionalloc_trap(uint32_t vec, uint64_t fault_rip,
                                            uint64_t frame_base) {
    task_t *cur;
    uint64_t crash_rbp, outer_rbp, ret_addr;
    if (vec != 13u || !frame_base) return false;
    cur = task_current();
    if (!chrome_task_is_runtime(cur)) return false;

    crash_rbp = *(uint64_t *)(uintptr_t)(frame_base + 64u);
    if (fault_rip == 0x00000000493cf778ULL ||
        fault_rip == 0x00000000493cf74eULL) {
        if (!user_qword_ok(cur, crash_rbp, &outer_rbp)) return false;
        if (!user_qword_ok(cur, crash_rbp + 8u, &ret_addr)) return false;
        if (ret_addr != 0x00000000493cf9c8ULL &&
            ret_addr != 0x00000000493cfa22ULL) return false;
        return chrome_finish_partition_frame(cur, frame_base, outer_rbp,
                                             fault_rip, "nested");
    }
    if (fault_rip == 0x00000000493cfa2bULL) {
        return chrome_finish_partition_frame(cur, frame_base, crash_rbp,
                                             fault_rip, "direct");
    }
    return false;
}

/* Dispatcher called from isr.S for CPU exceptions. */
void isr_dispatch(uint32_t vec, uint32_t err, uint64_t fault_rip, uint64_t frame_base) {
    char buf[96];
    size_t len = 0;
    const char *name = (vec < 32) ? g_exception_names[vec] : "Unknown";
    uint64_t cr2 = 0;
    uint64_t msr_fs = 0;
    uint64_t msr_gs = 0;
    uint64_t msr_kgs = 0;
    uint64_t user_rsp = 0;
    uint64_t saved_cs = frame_base ? *(uint64_t *)(uintptr_t)(frame_base + 144u) : 0;
    bool fault_from_user = ((saved_cs & 3ULL) == 3ULL);
    /* Gate the verbose 3-line kernel-mode trap dump. Every Linux process spawn
       under our compat layer triggers a few recoverable kernel-mode page
       faults (PT_INTERP demand-load, identity heal, etc.) that
       `compat3_handle_page_fault` resolves below. Printing the full dump for
       each one floods the serial port and visibly stalls the desktop. Keep
       the first few dumps for diagnostics, then fall back to a one-line
       summary so unhandled faults are still observable. */
    static uint32_t s_kexc_full_dump_count = 0;
    static uint32_t s_kexc_summary_count = 0;
    bool kexc_full_dump = !fault_from_user && s_kexc_full_dump_count < 24u;
    if (!fault_from_user && !kexc_full_dump && s_kexc_summary_count < 256u) {
        ++s_kexc_summary_count;
        __boot_serial_force_puts("[kexc!] vec=");
        __boot_serial_force_putu32(vec);
        __boot_serial_force_puts(" rip=");
        __boot_serial_force_puthex64(fault_rip);
        {
            task_t *cur = task_current();
            if (cur) {
                __boot_serial_force_puts(" pid=");
                __boot_serial_force_putu32((uint32_t)cur->pid);
                __boot_serial_force_puts(" name=");
                __boot_serial_force_puts(cur->name[0] ? cur->name : "?");
            }
        }
        __boot_serial_force_puts("\n");
    }
    if (kexc_full_dump) {
        task_t *cur = task_current();
        uint64_t fault_rsp = frame_base ? frame_base + 160u : 0;
        uint64_t rax = frame_base ? *(uint64_t *)(uintptr_t)(frame_base + 112u) : 0;
        uint64_t rbx = frame_base ? *(uint64_t *)(uintptr_t)(frame_base + 104u) : 0;
        uint64_t rbp = frame_base ? *(uint64_t *)(uintptr_t)(frame_base + 64u) : 0;
        uint64_t rdi = frame_base ? *(uint64_t *)(uintptr_t)(frame_base + 72u) : 0;
        uint64_t rsi = frame_base ? *(uint64_t *)(uintptr_t)(frame_base + 80u) : 0;
        uint64_t rsp0 = 0;
        uint32_t ki;
        ++s_kexc_full_dump_count;
        __asm__ __volatile__("mov %%rsp,%0" : "=r"(rsp0));
        __boot_serial_force_puts("\n[kexc!] vec=");
        __boot_serial_force_putu32(vec);
        __boot_serial_force_puts(" err=");
        __boot_serial_force_puthex64((uint64_t)err);
        __boot_serial_force_puts(" rip=");
        __boot_serial_force_puthex64(fault_rip);
        __boot_serial_force_puts(" cs=");
        __boot_serial_force_puthex64(saved_cs);
        __boot_serial_force_puts(" frame=");
        __boot_serial_force_puthex64(frame_base);
        __boot_serial_force_puts(" frsp=");
        __boot_serial_force_puthex64(fault_rsp);
        __boot_serial_force_puts(" crsp=");
        __boot_serial_force_puthex64(rsp0);
        if (cur) {
            __boot_serial_force_puts(" pid=");
            __boot_serial_force_putu32((uint32_t)cur->pid);
            __boot_serial_force_puts(" st=");
            __boot_serial_force_putu32((uint32_t)cur->state);
            __boot_serial_force_puts(" name=");
            __boot_serial_force_puts(cur->name[0] ? cur->name : "?");
            __boot_serial_force_puts(" ksp=");
            __boot_serial_force_puthex64(cur->kernel_rsp_saved);
            __boot_serial_force_puts(" kb=");
            __boot_serial_force_puthex64((uint64_t)(uintptr_t)cur->kernel_stack);
            __boot_serial_force_puts(" kt=");
            __boot_serial_force_puthex64(cur->kernel_stack_top);
        }
        __boot_serial_force_puts("\n[kexc!] regs rax=");
        __boot_serial_force_puthex64(rax);
        __boot_serial_force_puts(" rbx=");
        __boot_serial_force_puthex64(rbx);
        __boot_serial_force_puts(" rbp=");
        __boot_serial_force_puthex64(rbp);
        __boot_serial_force_puts(" rdi=");
        __boot_serial_force_puthex64(rdi);
        __boot_serial_force_puts(" rsi=");
        __boot_serial_force_puthex64(rsi);
        __boot_serial_force_puts("\n[kexc!] stack:");
        for (ki = 0; ki < 8u; ++ki) {
            uint64_t *p = (uint64_t *)(uintptr_t)(fault_rsp + (uint64_t)ki * 8u);
            __boot_serial_force_puts(" ");
            __boot_serial_force_puthex64(*p);
        }
        __boot_serial_force_puts("\n");
    }
    if (vec == 14u) {
        __asm__ __volatile__("mov %%cr2,%0" : "=r"(cr2));
        if (!fault_from_user) {
            task_t *cur = task_current();
            uint64_t page = cr2 & ~(uint64_t)(PAGE_SIZE - 1u);
            /*
             * Los procesos Linux corren con su propio CR3, pero el kernel
             * sigue entrando por la identidad baja mientras atiende syscalls.
             * Si alguna división de huge pages deja sin mapear una página del
             * kernel en ese address space, la reponemos como supervisor-only.
             */
            if (cur && cur->addr_space && cr2 < 0x40000000ULL &&
                !(paging_get_entry(cur->addr_space, page) & PAGE_PRESENT)) {
                if (paging_map(cur->addr_space, page, page,
                               PAGE_PRESENT | PAGE_WRITABLE)) {
                    static uint32_t kernel_identity_heal_count = 0;
                    if (kernel_identity_heal_count < 32u) {
                        ++kernel_identity_heal_count;
                        __boot_serial_force_puts("[kernel-identity-heal] pid=");
                        __boot_serial_force_putu32((uint32_t)cur->pid);
                        __boot_serial_force_puts(" rip=");
                        __boot_serial_force_puthex64(fault_rip);
                        __boot_serial_force_puts(" page=");
                        __boot_serial_force_puthex64(page);
                        __boot_serial_force_puts("\n");
                    }
                    return;
                }
            }
        }
        if (compat3_handle_page_fault(cr2, (uint64_t)err)) {
            return;
        }
        if (fault_from_user &&
            compat3_recover_null_plt_call(vec, (uint64_t)err, cr2,
                                          fault_rip, frame_base)) {
            return;
        }
        if (fault_from_user && glibc_secure_getenv_fix(task_current(), frame_base,
                                                       fault_rip, cr2)) {
            return;
        }
        if (fault_rip == 0x0000000049330fc3ULL && frame_base && cr2 == 0x0000000200000017ULL) {
            task_t *cur = task_current();
            if (cur && cur->name[0] == 'P' && k_strcmp(cur->name, "PerfettoTrace") == 0) {
                uint64_t saved_r15 = *(uint64_t *)(uintptr_t)(frame_base + 0u);
                if (cur->addr_space && saved_r15 >= 0x50u) {
                    uint64_t field = saved_r15 - 0x50u;
                    if (field < 0x0000800000000000ULL &&
                        (paging_get_entry(cur->addr_space, field) & PAGE_PRESENT) &&
                        (paging_get_entry(cur->addr_space, field + 7u) & PAGE_PRESENT)) {
                        *(uint64_t *)(uintptr_t)field = 0;
                    }
                }
                *(uint64_t *)(uintptr_t)(frame_base + 112u) = 0; /* AL=false */
                *(uint64_t *)(uintptr_t)(frame_base + 136u) = 0x0000000049330fc6ULL;
                __boot_serial_force_puts("[chrome-perfetto-skip!] pid=");
                __boot_serial_force_putu32((uint32_t)cur->pid);
                __boot_serial_force_puts(" rip=");
                __boot_serial_force_puthex64(fault_rip);
                __boot_serial_force_puts(" cr2=");
                __boot_serial_force_puthex64(cr2);
                __boot_serial_force_puts("\n");
                return;
            }
        }
        if (fault_rip == 0x0000000049310d64ULL && frame_base && cr2 == 0x0000000200000003ULL) {
            task_t *cur = task_current();
            if (cur && cur->name[0] == 'P' && k_strcmp(cur->name, "PerfettoTrace") == 0) {
                uint64_t saved_r15 = *(uint64_t *)(uintptr_t)(frame_base + 0u);
                if (cur->addr_space && saved_r15 >= 0x50u) {
                    uint64_t field = saved_r15 - 0x50u;
                    if (field < 0x0000800000000000ULL &&
                        (paging_get_entry(cur->addr_space, field) & PAGE_PRESENT) &&
                        (paging_get_entry(cur->addr_space, field + 7u) & PAGE_PRESENT)) {
                        *(uint64_t *)(uintptr_t)field = 0;
                    }
                }
                *(uint64_t *)(uintptr_t)(frame_base + 112u) = 0; /* AL=false for following test */
                *(uint64_t *)(uintptr_t)(frame_base + 136u) = 0x0000000049310d68ULL;
                __boot_serial_force_puts("[chrome-perfetto-skip2!] pid=");
                __boot_serial_force_putu32((uint32_t)cur->pid);
                __boot_serial_force_puts(" rip=");
                __boot_serial_force_puthex64(fault_rip);
                __boot_serial_force_puts(" cr2=");
                __boot_serial_force_puthex64(cr2);
                __boot_serial_force_puts("\n");
                return;
            }
        }
        if (fault_rip == 0x0000000049311140ULL && frame_base && cr2 == 0x0000000200000003ULL) {
            task_t *cur = task_current();
            if (cur && cur->name[0] == 'P' && k_strcmp(cur->name, "PerfettoTrace") == 0) {
                uint64_t saved_r15 = *(uint64_t *)(uintptr_t)(frame_base + 0u);
                if (cur->addr_space && saved_r15 >= 0x50u) {
                    uint64_t field = saved_r15 - 0x50u;
                    if (field < 0x0000800000000000ULL &&
                        (paging_get_entry(cur->addr_space, field) & PAGE_PRESENT) &&
                        (paging_get_entry(cur->addr_space, field + 7u) & PAGE_PRESENT)) {
                        *(uint64_t *)(uintptr_t)field = 0;
                    }
                }
                *(uint64_t *)(uintptr_t)(frame_base + 112u) = 0; /* AL=false for following test */
                *(uint64_t *)(uintptr_t)(frame_base + 136u) = 0x0000000049311144ULL;
                __boot_serial_force_puts("[chrome-perfetto-skip3!] pid=");
                __boot_serial_force_putu32((uint32_t)cur->pid);
                __boot_serial_force_puts(" rip=");
                __boot_serial_force_puthex64(fault_rip);
                __boot_serial_force_puts(" cr2=");
                __boot_serial_force_puthex64(cr2);
                __boot_serial_force_puts("\n");
                return;
            }
        }
        if (fault_rip == 0x0000000049308756ULL && frame_base && cr2 == 0x0000000200000007ULL) {
            task_t *cur = task_current();
            if (cur && cur->name[0] == 'P' && k_strcmp(cur->name, "PerfettoTrace") == 0) {
                *(uint64_t *)(uintptr_t)(frame_base + 0u) = 0; /* r15 */
                *(uint64_t *)(uintptr_t)(frame_base + 136u) = 0x0000000049308767ULL;
                __boot_serial_force_puts("[chrome-perfetto-drop-dtor!] pid=");
                __boot_serial_force_putu32((uint32_t)cur->pid);
                __boot_serial_force_puts(" rip=");
                __boot_serial_force_puthex64(fault_rip);
                __boot_serial_force_puts(" cr2=");
                __boot_serial_force_puthex64(cr2);
                __boot_serial_force_puts("\n");
                return;
            }
        }
        if (fault_rip == 0x00000000493112f2ULL && frame_base && cr2 == 0x000000020000001fULL) {
            task_t *cur = task_current();
            if (cur && cur->name[0] == 'P' && k_strcmp(cur->name, "PerfettoTrace") == 0) {
                *(uint64_t *)(uintptr_t)(frame_base + 104u) = 0; /* rbx */
                *(uint64_t *)(uintptr_t)(frame_base + 136u) = 0x00000000493112faULL;
                __boot_serial_force_puts("[chrome-perfetto-skip4!] pid=");
                __boot_serial_force_putu32((uint32_t)cur->pid);
                __boot_serial_force_puts(" rip=");
                __boot_serial_force_puthex64(fault_rip);
                __boot_serial_force_puts(" cr2=");
                __boot_serial_force_puthex64(cr2);
                __boot_serial_force_puts("\n");
                return;
            }
        }
        if (fault_rip == 0x0000000049319209ULL && frame_base && cr2 < PAGE_SIZE) {
            task_t *cur = task_current();
            uint64_t saved_rax = *(uint64_t *)(uintptr_t)(frame_base + 112u);
            uint64_t saved_rbx = *(uint64_t *)(uintptr_t)(frame_base + 104u);
            uint64_t saved_rsi = *(uint64_t *)(uintptr_t)(frame_base + 80u);
            uint64_t saved_rdx = *(uint64_t *)(uintptr_t)(frame_base + 88u);
            uint64_t slot_va = saved_rsi + saved_rdx * 8u;
            uint64_t slot_entry = 0;
            if (cur && cur->addr_space && saved_rax < PAGE_SIZE && saved_rdx < 512u &&
                slot_va >= saved_rsi && slot_va < 0x0000800000000000ULL &&
                (paging_get_entry(cur->addr_space, slot_va) & PAGE_PRESENT) &&
                (paging_get_entry(cur->addr_space, slot_va + 7u) & PAGE_PRESENT)) {
                slot_entry = *(uint64_t *)(uintptr_t)slot_va;
            }
            if (cur && cur->addr_space && slot_entry == 0) {
                static uint32_t chrome_deque_heal_seq = 0;
                uint64_t recovered_block = 0;
                uint64_t recovered_from = 0;
                int scan;
                for (scan = -8; scan <= 8 && !recovered_block; ++scan) {
                    int64_t slot_index = (int64_t)saved_rdx + scan;
                    uint64_t probe_slot;
                    uint64_t q = 0;
                    if (slot_index < 0) continue;
                    probe_slot = saved_rsi + ((uint64_t)slot_index * 8u);
                    if (probe_slot < saved_rsi) continue;
                    if (probe_slot >= 0x0000800000000000ULL) continue;
                    if (!(paging_get_entry(cur->addr_space, probe_slot) & PAGE_PRESENT)) continue;
                    if (!(paging_get_entry(cur->addr_space, probe_slot + 7u) & PAGE_PRESENT)) continue;
                    q = *(uint64_t *)(uintptr_t)probe_slot;
                    if (q < PAGE_SIZE || q >= 0x0000800000000000ULL) continue;
                    if (!(paging_get_entry(cur->addr_space, q + saved_rax) & PAGE_PRESENT)) continue;
                    if (!(paging_get_entry(cur->addr_space, q + saved_rax + 0xA7u) & PAGE_PRESENT)) continue;
                    recovered_block = q;
                    recovered_from = probe_slot;
                }
                for (scan = 0; scan < 16 && !recovered_block; ++scan) {
                    uint64_t probe_slot = saved_rbx + 0x40u + (uint64_t)scan * 8u;
                    uint64_t q = 0;
                    if (probe_slot >= 0x0000800000000000ULL) continue;
                    if (!(paging_get_entry(cur->addr_space, probe_slot) & PAGE_PRESENT)) continue;
                    if (!(paging_get_entry(cur->addr_space, probe_slot + 7u) & PAGE_PRESENT)) continue;
                    q = *(uint64_t *)(uintptr_t)probe_slot;
                    if (q < PAGE_SIZE || q >= 0x0000800000000000ULL) continue;
                    if (!(paging_get_entry(cur->addr_space, q + saved_rax) & PAGE_PRESENT)) continue;
                    if (!(paging_get_entry(cur->addr_space, q + saved_rax + 0xA7u) & PAGE_PRESENT)) continue;
                    recovered_block = q;
                    recovered_from = probe_slot;
                }
                if (recovered_block) {
                    *(uint64_t *)(uintptr_t)slot_va = recovered_block;
                    *(uint64_t *)(uintptr_t)(frame_base + 112u) = recovered_block + saved_rax;
                    __boot_serial_force_puts("[chrome-deque-heal!] pid=");
                    __boot_serial_force_putu32((uint32_t)cur->pid);
                    __boot_serial_force_puts(" slot=");
                    __boot_serial_force_puthex64(slot_va);
                    __boot_serial_force_puts(" block=");
                    __boot_serial_force_puthex64(recovered_block);
                    __boot_serial_force_puts(" rax=");
                    __boot_serial_force_puthex64(saved_rax);
                    __boot_serial_force_puts(" from=");
                    __boot_serial_force_puthex64(recovered_from);
                    __boot_serial_force_puts(" recovered=1\n");
                    return;
                }
                uint64_t phys = pmm_alloc_frame();
                uint64_t heal_va = 0x0000700000000000ULL +
                                   (((uint64_t)((uint32_t)cur->pid & 0xFFFu)) << 24) +
                                   (((uint64_t)(chrome_deque_heal_seq++ & 0xFFFu)) << 12);
                uint32_t guard = 0;
                while (guard++ < 4096u &&
                       (paging_get_entry(cur->addr_space, heal_va) & PAGE_PRESENT)) {
                    heal_va += PAGE_SIZE;
                    if (heal_va >= 0x00007F0000000000ULL) heal_va = 0x0000700000000000ULL;
                }
                if (phys && !(paging_get_entry(cur->addr_space, heal_va) & PAGE_PRESENT)) {
                    k_memset((void *)PHYS_TO_DMAP(phys), 0, PAGE_SIZE);
                    if (paging_map(cur->addr_space, heal_va, phys,
                                   PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER | PAGE_NX)) {
                        *(uint64_t *)(uintptr_t)slot_va = heal_va;
                        if (saved_rbx < 0x0000800000000000ULL &&
                            (paging_get_entry(cur->addr_space, saved_rbx + 0x78u) & PAGE_PRESENT)) {
                            *(uint64_t *)(uintptr_t)(saved_rbx + 0x70u) = 0;
                            *(uint64_t *)(uintptr_t)(saved_rbx + 0x78u) = 0;
                            saved_rax = 0;
                        }
                        *(uint64_t *)(uintptr_t)(frame_base + 112u) = heal_va + saved_rax;
                        __boot_serial_force_puts("[chrome-deque-heal!] pid=");
                        __boot_serial_force_putu32((uint32_t)cur->pid);
                        __boot_serial_force_puts(" slot=");
                        __boot_serial_force_puthex64(slot_va);
                        __boot_serial_force_puts(" block=");
                        __boot_serial_force_puthex64(heal_va);
                        __boot_serial_force_puts(" rax=");
                        __boot_serial_force_puthex64(saved_rax);
                        __boot_serial_force_puts(" recovered=0\n");
                        return;
                    }
                }
            }
        }
    }
    if (fault_from_user && frame_base) {
        task_t *cur = task_current();
        if ((vec == 13u || vec == 14u) && cur && cur->name[0] == 'P' &&
            k_strcmp(cur->name, "PerfettoTrace") == 0) {
            if (fault_rip == 0x0000000049308756ULL) {
                *(uint64_t *)(uintptr_t)(frame_base + 0u) = 0;
                *(uint64_t *)(uintptr_t)(frame_base + 136u) = 0x0000000049308767ULL;
                __boot_serial_force_puts("[chrome-perfetto-drop-dtor2!] pid=");
                __boot_serial_force_putu32((uint32_t)cur->pid);
                __boot_serial_force_puts(" rip=");
                __boot_serial_force_puthex64(fault_rip);
                __boot_serial_force_puts(" vec=");
                __boot_serial_force_putu32(vec);
                __boot_serial_force_puts("\n");
                return;
            }
            if (fault_rip == 0x0000000049310d64ULL) {
                uint64_t saved_r15 = *(uint64_t *)(uintptr_t)(frame_base + 0u);
                if (cur->addr_space && saved_r15 >= 0x50u) {
                    uint64_t field = saved_r15 - 0x50u;
                    if (field < 0x0000800000000000ULL &&
                        (paging_get_entry(cur->addr_space, field) & PAGE_PRESENT) &&
                        (paging_get_entry(cur->addr_space, field + 7u) & PAGE_PRESENT)) {
                        *(uint64_t *)(uintptr_t)field = 0;
                    }
                }
                *(uint64_t *)(uintptr_t)(frame_base + 112u) = 0;
                *(uint64_t *)(uintptr_t)(frame_base + 136u) = 0x0000000049310d68ULL;
                __boot_serial_force_puts("[chrome-perfetto-skip2gp!] pid=");
                __boot_serial_force_putu32((uint32_t)cur->pid);
                __boot_serial_force_puts(" rip=");
                __boot_serial_force_puthex64(fault_rip);
                __boot_serial_force_puts(" vec=");
                __boot_serial_force_putu32(vec);
                __boot_serial_force_puts("\n");
                return;
            }
            if (fault_rip == 0x00000000492b39dbULL) {
                *(uint64_t *)(uintptr_t)(frame_base + 72u) = 0;
                *(uint64_t *)(uintptr_t)(frame_base + 136u) = 0x00000000492b3a01ULL;
                __boot_serial_force_puts("[chrome-perfetto-release-null!] pid=");
                __boot_serial_force_putu32((uint32_t)cur->pid);
                __boot_serial_force_puts(" rip=");
                __boot_serial_force_puthex64(fault_rip);
                __boot_serial_force_puts(" vec=");
                __boot_serial_force_putu32(vec);
                __boot_serial_force_puts("\n");
                return;
            }
            if (fault_rip == 0x0000000049330fc3ULL) {
                uint64_t saved_r15 = *(uint64_t *)(uintptr_t)(frame_base + 0u);
                if (cur->addr_space && saved_r15 >= 0x50u) {
                    uint64_t field = saved_r15 - 0x50u;
                    if (field < 0x0000800000000000ULL &&
                        (paging_get_entry(cur->addr_space, field) & PAGE_PRESENT) &&
                        (paging_get_entry(cur->addr_space, field + 7u) & PAGE_PRESENT)) {
                        *(uint64_t *)(uintptr_t)field = 0;
                    }
                }
                *(uint64_t *)(uintptr_t)(frame_base + 112u) = 0;
                *(uint64_t *)(uintptr_t)(frame_base + 136u) = 0x0000000049330fc6ULL;
                __boot_serial_force_puts("[chrome-perfetto-call-skip!] pid=");
                __boot_serial_force_putu32((uint32_t)cur->pid);
                __boot_serial_force_puts(" rip=");
                __boot_serial_force_puthex64(fault_rip);
                __boot_serial_force_puts(" vec=");
                __boot_serial_force_putu32(vec);
                __boot_serial_force_puts("\n");
                return;
            }
            uint64_t resume = 0;
            if (fault_rip == 0x000000004931209dULL) resume = 0x00000000493120acULL;
            else if (fault_rip == 0x00000000493120b8ULL) resume = 0x00000000493120c4ULL;
            else if (fault_rip == 0x00000000493120cdULL) resume = 0x00000000493120d5ULL;
            if (resume) {
                *(uint64_t *)(uintptr_t)(frame_base + 72u) = 0; /* rdi */
                *(uint64_t *)(uintptr_t)(frame_base + 136u) = resume;
                __boot_serial_force_puts("[chrome-perfetto-release-skip!] pid=");
                __boot_serial_force_putu32((uint32_t)cur->pid);
                __boot_serial_force_puts(" rip=");
                __boot_serial_force_puthex64(fault_rip);
                __boot_serial_force_puts(" resume=");
                __boot_serial_force_puthex64(resume);
                __boot_serial_force_puts("\n");
                return;
            }
        }
        if (vec == 14u && fault_rip == 0x00000000493da402ULL &&
            cr2 == 0x18ULL &&
            chrome_return_current_frame(cur, frame_base, fault_rip,
                                        "partition-helper")) {
            return;
        }
        if (vec == 14u && fault_rip == 0x00000000493da5c6ULL &&
            chrome_return_tls_frame(cur, frame_base, fault_rip,
                                    "tls-invalid")) {
            return;
        }
        if (vec == 14u && fault_rip == 0x00000000493da73fULL &&
            chrome_return_current_frame(cur, frame_base, fault_rip,
                                        "partition-list")) {
            return;
        }
        if (vec == 14u && fault_rip == 0x00000000493e4b22ULL &&
            cr2 < PAGE_SIZE &&
            chrome_return_current_frame(cur, frame_base, fault_rip,
                                        "malloc-size")) {
            return;
        }
        if (vec == 14u &&
            chrome_skip_bad_indirect_call(cur, frame_base, fault_rip,
                                          cr2, err)) {
            return;
        }
        if (chrome_skip_partitionalloc_trap(vec, fault_rip, frame_base)) {
            return;
        }
    }
    /* Loud trace before panic so we capture the exact fault coordinates
     * over serial. Useful when a user-mode task (ld-linux / Firefox)
     * touches an unmapped page -- helps identify the missing mmap /
     * syscall that should have created the mapping. */
    {
        task_t *cur = task_current();
        __boot_serial_puts("\n[!!!] CPU exception vec=");
        __boot_serial_putu32(vec);
        __boot_serial_puts(" (");
        __boot_serial_puts(name);
        __boot_serial_puts(") err=");
        __boot_serial_puthex64((uint64_t)err);
        if (vec == 14u) {
            __boot_serial_puts(" CR2=");
            __boot_serial_puthex64(cr2);
        }
        if (cur) {
            uint32_t lo, hi;
            __asm__ __volatile__("rdmsr" : "=a"(lo), "=d"(hi) : "c"(0xC0000100u));
            msr_fs = ((uint64_t)hi << 32) | lo;
            __asm__ __volatile__("rdmsr" : "=a"(lo), "=d"(hi) : "c"(0xC0000101u));
            msr_gs = ((uint64_t)hi << 32) | lo;
            __asm__ __volatile__("rdmsr" : "=a"(lo), "=d"(hi) : "c"(0xC0000102u));
            msr_kgs = ((uint64_t)hi << 32) | lo;
            if (frame_base) {
                user_rsp = *(uint64_t *)(uintptr_t)(frame_base + 160u);
            }
            __boot_serial_puts(" pid=");
            __boot_serial_putu32((uint32_t)cur->pid);
        }
        __boot_serial_puts(" rip=");
        __boot_serial_puthex64(fault_rip);
        if ((vec == 6u || vec == 13u || vec == 14u) && fault_rip) {
            bool rip_bytes_ok = true;
            if (fault_rip < 0x0000800000000000ULL) {
                rip_bytes_ok = cur && ((paging_get_entry(cur->addr_space, fault_rip) & PAGE_PRESENT) != 0);
            } else if (!(fault_rip >= 0x100000ULL && fault_rip < 0x4000000ULL)) {
                rip_bytes_ok = false;
            }
            if (rip_bytes_ok) {
                const uint8_t *ip = (const uint8_t *)(uintptr_t)fault_rip;
                int i;
                __boot_serial_puts(" bytes=");
                for (i = 0; i < 12; ++i) {
                    static const char *hx = "0123456789abcdef";
                    __boot_serial_putc(hx[(ip[i] >> 4) & 0xF]);
                    __boot_serial_putc(hx[ip[i] & 0xF]);
                    if (i + 1 < 12) __boot_serial_putc(' ');
                }
            } else {
                __boot_serial_puts(" bytes=np");
            }
        }
        __boot_serial_puts("\n");
        if (cur) {
            __boot_serial_puts("[!!!]   ctx.fs=");
            __boot_serial_puthex64(cur->ctx.fs_base);
            __boot_serial_puts(" msr.fs=");
            __boot_serial_puthex64(msr_fs);
            __boot_serial_puts(" ctx.gs=");
            __boot_serial_puthex64(cur->ctx.gs_base);
            __boot_serial_puts(" msr.gs=");
            __boot_serial_puthex64(msr_gs);
            __boot_serial_puts(" msr.kgs=");
            __boot_serial_puthex64(msr_kgs);
            __boot_serial_puts("\n");
            if (false && cur->ctx.fs_base && cur->ctx.fs_base < 0x0000800000000000ULL) {
                static const uint32_t fs_offsets[] = {
                    0x0u, 0x10u, 0x28u, 0x30u, 0x2d0u, 0x510u, 0x518u, 0x520u, 0x528u
                };
                uint32_t fi;
                __boot_serial_puts("[!!!]   fs.q:");
                for (fi = 0; fi < (uint32_t)(sizeof(fs_offsets) / sizeof(fs_offsets[0])); ++fi) {
                    uint64_t va = cur->ctx.fs_base + (uint64_t)fs_offsets[fi];
                    __boot_serial_puts(" +");
                    __boot_serial_puthex64((uint64_t)fs_offsets[fi]);
                    __boot_serial_puts("=");
                    if (va >= cur->ctx.fs_base &&
                        va + 7u >= va &&
                        va + 7u < 0x0000800000000000ULL &&
                        (paging_get_entry(cur->addr_space, va) & PAGE_PRESENT) &&
                        (paging_get_entry(cur->addr_space, va + 7u) & PAGE_PRESENT)) {
                        __boot_serial_puthex64(*(uint64_t *)(uintptr_t)va);
                    } else {
                        __boot_serial_puts("np");
                    }
                }
                __boot_serial_puts("\n");
            }
        }
        if ((vec == 6u || vec == 13u || vec == 14u) && frame_base) {
            uint64_t rax = *(uint64_t *)(uintptr_t)(frame_base + 112u);
            uint64_t rbx = *(uint64_t *)(uintptr_t)(frame_base + 104u);
            uint64_t rdi = *(uint64_t *)(uintptr_t)(frame_base + 72u);
            uint64_t rsi = *(uint64_t *)(uintptr_t)(frame_base + 80u);
            uint64_t rdx = *(uint64_t *)(uintptr_t)(frame_base + 88u);
            uint64_t rcx = *(uint64_t *)(uintptr_t)(frame_base + 96u);
            uint64_t rbp = *(uint64_t *)(uintptr_t)(frame_base + 64u);
            uint64_t r8  = *(uint64_t *)(uintptr_t)(frame_base + 56u);
            uint64_t r9  = *(uint64_t *)(uintptr_t)(frame_base + 48u);
            uint64_t r10 = *(uint64_t *)(uintptr_t)(frame_base + 40u);
            uint64_t r11 = *(uint64_t *)(uintptr_t)(frame_base + 32u);
            uint64_t r12 = *(uint64_t *)(uintptr_t)(frame_base + 24u);
            uint64_t r13 = *(uint64_t *)(uintptr_t)(frame_base + 16u);
            uint64_t r14 = *(uint64_t *)(uintptr_t)(frame_base + 8u);
            uint64_t r15 = *(uint64_t *)(uintptr_t)(frame_base + 0u);
            uint64_t ret = 0;
            uint64_t cr2_entry = 0;
            int si;
            __boot_serial_puts("[!!!]   rax=");
            __boot_serial_puthex64(rax);
            __boot_serial_puts(" rbx=");
            __boot_serial_puthex64(rbx);
            __boot_serial_puts(" rbp=");
            __boot_serial_puthex64(rbp);
            __boot_serial_puts(" rdi=");
            __boot_serial_puthex64(rdi);
            __boot_serial_puts(" rsi=");
            __boot_serial_puthex64(rsi);
            __boot_serial_puts(" rdx=");
            __boot_serial_puthex64(rdx);
            __boot_serial_puts(" rcx=");
            __boot_serial_puthex64(rcx);
            __boot_serial_puts(" ursp=");
            __boot_serial_puthex64(user_rsp);
            if (false && cur && user_rsp && (paging_get_entry(cur->addr_space, user_rsp) & PAGE_PRESENT)) {
                ret = *(uint64_t *)(uintptr_t)user_rsp;
                __boot_serial_puts(" ret=");
                __boot_serial_puthex64(ret);
            }
            __boot_serial_puts("\n");
            __boot_serial_puts("[!!!]   r8=");
            __boot_serial_puthex64(r8);
            __boot_serial_puts(" r9=");
            __boot_serial_puthex64(r9);
            __boot_serial_puts(" r10=");
            __boot_serial_puthex64(r10);
            __boot_serial_puts(" r11=");
            __boot_serial_puthex64(r11);
            __boot_serial_puts(" r12=");
            __boot_serial_puthex64(r12);
            __boot_serial_puts(" r13=");
            __boot_serial_puthex64(r13);
            __boot_serial_puts(" r14=");
            __boot_serial_puthex64(r14);
            __boot_serial_puts(" r15=");
            __boot_serial_puthex64(r15);
            __boot_serial_puts("\n");
            if (false && (vec == 13u || vec == 14u) && cur) {
                const uint8_t *ripb = (const uint8_t *)(uintptr_t)fault_rip;
                const uint8_t *raxb = (const uint8_t *)(uintptr_t)rax;
                const uint8_t *rbxb = (const uint8_t *)(uintptr_t)rbx;
                const uint8_t *rcxb = (const uint8_t *)(uintptr_t)rcx;
                const uint8_t *r13b = (const uint8_t *)(uintptr_t)r13;
                const uint8_t *r14b = (const uint8_t *)(uintptr_t)r14;
                uint64_t rax_entry = 0;
                uint64_t rbx_entry = 0;
                uint64_t rcx_entry = 0;
                uint64_t r13_entry = 0;
                uint64_t r14_entry = 0;
                int bi;
                __boot_serial_puts("[!!!]   ptr.pte");
                if (vec == 14u) {
                    cr2_entry = paging_get_entry(cur->addr_space, cr2);
                    __boot_serial_puts(" cr2=");
                    __boot_serial_puthex64(cr2_entry);
                }
                if (fault_rip && fault_rip < 0x0000800000000000ULL) {
                    __boot_serial_puts(" rip=");
                    __boot_serial_puthex64(paging_get_entry(cur->addr_space, fault_rip));
                }
                if (rax && rax < 0x0000800000000000ULL) {
                    rax_entry = paging_get_entry(cur->addr_space, rax);
                    __boot_serial_puts(" rax=");
                    __boot_serial_puthex64(rax_entry);
                }
                if (rbx && rbx < 0x0000800000000000ULL) {
                    rbx_entry = paging_get_entry(cur->addr_space, rbx);
                    __boot_serial_puts(" rbx=");
                    __boot_serial_puthex64(rbx_entry);
                }
                if (rcx && rcx < 0x0000800000000000ULL) {
                    rcx_entry = paging_get_entry(cur->addr_space, rcx);
                    __boot_serial_puts(" rcx=");
                    __boot_serial_puthex64(rcx_entry);
                }
                if (r13 && r13 < 0x0000800000000000ULL) {
                    r13_entry = paging_get_entry(cur->addr_space, r13);
                    __boot_serial_puts(" r13=");
                    __boot_serial_puthex64(r13_entry);
                }
                if (r14 && r14 < 0x0000800000000000ULL) {
                    r14_entry = paging_get_entry(cur->addr_space, r14);
                    __boot_serial_puts(" r14=");
                    __boot_serial_puthex64(r14_entry);
                }
                __boot_serial_puts("\n");
                if (fault_rip && (paging_get_entry(cur->addr_space, fault_rip) & PAGE_PRESENT)) {
                    __boot_serial_puts("[!!!]   rip.bytes=");
                    for (bi = 0; bi < 12; ++bi) {
                        static const char *hx = "0123456789abcdef";
                        __boot_serial_putc(hx[(ripb[bi] >> 4) & 0xF]);
                        __boot_serial_putc(hx[ripb[bi] & 0xF]);
                        if (bi + 1 < 12) __boot_serial_putc(' ');
                    }
                    __boot_serial_puts("\n");
                }
                if (rax && rax < 0x0000800000000000ULL &&
                    (paging_get_entry(cur->addr_space, rax) & PAGE_PRESENT)) {
                    __boot_serial_puts("[!!!]   rax.bytes=");
                    for (bi = 0; bi < 12; ++bi) {
                        static const char *hx = "0123456789abcdef";
                        __boot_serial_putc(hx[(raxb[bi] >> 4) & 0xF]);
                        __boot_serial_putc(hx[raxb[bi] & 0xF]);
                        if (bi + 1 < 12) __boot_serial_putc(' ');
                    }
                    __boot_serial_puts("\n");
                }
                if (rbx && rbx < 0x0000800000000000ULL &&
                    (paging_get_entry(cur->addr_space, rbx) & PAGE_PRESENT)) {
                    __boot_serial_puts("[!!!]   rbx.bytes=");
                    for (bi = 0; bi < 32; ++bi) {
                        static const char *hx = "0123456789abcdef";
                        __boot_serial_putc(hx[(rbxb[bi] >> 4) & 0xF]);
                        __boot_serial_putc(hx[rbxb[bi] & 0xF]);
                        if (bi + 1 < 32) __boot_serial_putc(' ');
                    }
                    __boot_serial_puts("\n");
                }
                if (rcx && rcx < 0x0000800000000000ULL &&
                    (paging_get_entry(cur->addr_space, rcx) & PAGE_PRESENT)) {
                    __boot_serial_puts("[!!!]   rcx.bytes=");
                    for (bi = 0; bi < 12; ++bi) {
                        static const char *hx = "0123456789abcdef";
                        __boot_serial_putc(hx[(rcxb[bi] >> 4) & 0xF]);
                        __boot_serial_putc(hx[rcxb[bi] & 0xF]);
                        if (bi + 1 < 12) __boot_serial_putc(' ');
                    }
                    __boot_serial_puts("\n");
                }
                if (r13 && r13 < 0x0000800000000000ULL &&
                    (paging_get_entry(cur->addr_space, r13) & PAGE_PRESENT)) {
                    __boot_serial_puts("[!!!]   r13.bytes=");
                    for (bi = 0; bi < 80; ++bi) {
                        static const char *hx = "0123456789abcdef";
                        __boot_serial_putc(hx[(r13b[bi] >> 4) & 0xF]);
                        __boot_serial_putc(hx[r13b[bi] & 0xF]);
                        if (bi + 1 < 80) __boot_serial_putc(' ');
                    }
                    if ((paging_get_entry(cur->addr_space, r13 + 0x18u) & PAGE_PRESENT)) {
                        __boot_serial_puts(" q18=");
                        __boot_serial_puthex64(*(uint64_t *)(uintptr_t)(r13 + 0x18u));
                    }
                    __boot_serial_puts("\n");
                }
                if (r14 && r14 < 0x0000800000000000ULL &&
                    (paging_get_entry(cur->addr_space, r14) & PAGE_PRESENT)) {
                    __boot_serial_puts("[!!!]   r14.bytes=");
                    for (bi = 0; bi < 80; ++bi) {
                        static const char *hx = "0123456789abcdef";
                        __boot_serial_putc(hx[(r14b[bi] >> 4) & 0xF]);
                        __boot_serial_putc(hx[r14b[bi] & 0xF]);
                        if (bi + 1 < 80) __boot_serial_putc(' ');
                    }
                    if ((paging_get_entry(cur->addr_space, r14 + 0x38u) & PAGE_PRESENT)) {
                        __boot_serial_puts(" q38=");
                        __boot_serial_puthex64(*(uint64_t *)(uintptr_t)(r14 + 0x38u));
                    }
                    __boot_serial_puts("\n");
                }
                if (fault_rip >= 0x0000000040000000ULL &&
                    fault_rip <  0x0000000041000000ULL) {
                    static const uint32_t moz_offsets[] = {
                        0xc36b0u, 0xc3700u, 0xc3728u, 0xc3730u,
                        0xc38e8u, 0xc38f0u, 0xc3968u
                    };
                    uint32_t mi;
                    __boot_serial_puts("[!!!]   moz.q:");
                    for (mi = 0; mi < (uint32_t)(sizeof(moz_offsets) / sizeof(moz_offsets[0])); ++mi) {
                        uint64_t va = 0x0000000040000000ULL + (uint64_t)moz_offsets[mi];
                        __boot_serial_puts(" +");
                        __boot_serial_puthex64((uint64_t)moz_offsets[mi]);
                        __boot_serial_puts("=");
                        if (va + 7u >= va &&
                            (paging_get_entry(cur->addr_space, va) & PAGE_PRESENT) &&
                            (paging_get_entry(cur->addr_space, va + 7u) & PAGE_PRESENT)) {
                            __boot_serial_puthex64(*(uint64_t *)(uintptr_t)va);
                        } else {
                            __boot_serial_puts("np");
                        }
                    }
                    __boot_serial_puts("\n");
                }
            }
            if (false && cur && user_rsp) {
                __boot_serial_puts("[!!!]   stack:");
                for (si = 0; si < 24; ++si) {
                    uint64_t sva = user_rsp + (uint64_t)si * 8u;
                    __boot_serial_puts(" ");
                    __boot_serial_puthex64(sva);
                    __boot_serial_puts("=");
                    if (paging_get_entry(cur->addr_space, sva) & PAGE_PRESENT) {
                        __boot_serial_puthex64(*(uint64_t *)(uintptr_t)sva);
                    } else {
                        __boot_serial_puts("np");
                    }
                }
                __boot_serial_puts("\n");
            }
        }
    }
    /* Si explota una app de usuario, se mata esa tarea y listo.  Antes esto
     * miraba solo el RIP, pero el kernel tambien vive en direcciones bajas y
     * eso podia convertir un bug del exit path en un loop de faults infinito. */
    if (fault_from_user) {
        task_t *recover_cur = task_current();
        if (wayfire_ld_rebuild_linfo(recover_cur, frame_base, vec, fault_rip, cr2)) {
            return;
        }
        static uint32_t user_exc_trace_count;
        bool trace_user_exc = (user_exc_trace_count++ < 64u);
        if (!trace_user_exc && user_exc_trace_count == 66u) {
            __boot_serial_force_puts("[exc-user!] mas faults; bajo el log para no matar la VM\n");
        }
        if (trace_user_exc) {
            task_t *cur2 = task_current();
            __boot_serial_force_puts("[exc-user!] vec=");
            __boot_serial_force_putu32(vec);
            __boot_serial_force_puts(" err=");
            __boot_serial_force_puthex64((uint64_t)err);
            if (vec == 14u) {
                __boot_serial_force_puts(" cr2=");
                __boot_serial_force_puthex64(cr2);
            }
            __boot_serial_force_puts(" pid=");
            if (cur2) __boot_serial_force_putu32((uint32_t)cur2->pid);
            else __boot_serial_force_putu32(0);
            if (cur2 && cur2->name[0]) {
                __boot_serial_force_puts(" name=");
                __boot_serial_force_puts(cur2->name);
            }
            __boot_serial_force_puts(" rip=");
            __boot_serial_force_puthex64(fault_rip);
            __boot_serial_force_puts(" cs=");
            __boot_serial_force_puthex64(saved_cs);
            if ((vec == 6u || vec == 12u || vec == 13u || vec == 14u) && fault_rip) {
                bool rip_bytes_ok = false;
                if (fault_rip < 0x0000800000000000ULL) {
                    rip_bytes_ok = cur2 &&
                                   ((paging_get_entry(cur2->addr_space, fault_rip) & PAGE_PRESENT) != 0) &&
                                   ((paging_get_entry(cur2->addr_space, fault_rip + 15u) & PAGE_PRESENT) != 0);
                }
                __boot_serial_force_puts(" bytes=");
                if (rip_bytes_ok) {
                    const uint8_t *ip = (const uint8_t *)(uintptr_t)fault_rip;
                    static const char *hx = "0123456789abcdef";
                    int bi;
                    for (bi = 0; bi < 16; ++bi) {
                        __boot_serial_force_putc(hx[(ip[bi] >> 4) & 0xF]);
                        __boot_serial_force_putc(hx[ip[bi] & 0xF]);
                        if (bi + 1 < 16) __boot_serial_force_putc(' ');
                    }
                } else {
                    __boot_serial_force_puts("np");
                }
            }
            if ((vec == 6u || vec == 12u || vec == 13u || vec == 14u) && frame_base) {
                uint64_t user_sp = *(uint64_t *)(uintptr_t)(frame_base + 160u);
                __boot_serial_force_puts(" rax=");
                __boot_serial_force_puthex64(*(uint64_t *)(uintptr_t)(frame_base + 112u));
                __boot_serial_force_puts(" rbx=");
                __boot_serial_force_puthex64(*(uint64_t *)(uintptr_t)(frame_base + 104u));
                __boot_serial_force_puts(" rcx=");
                __boot_serial_force_puthex64(*(uint64_t *)(uintptr_t)(frame_base + 96u));
                __boot_serial_force_puts(" rdx=");
                __boot_serial_force_puthex64(*(uint64_t *)(uintptr_t)(frame_base + 88u));
                __boot_serial_force_puts(" rdi=");
                __boot_serial_force_puthex64(*(uint64_t *)(uintptr_t)(frame_base + 72u));
                __boot_serial_force_puts(" rsi=");
                __boot_serial_force_puthex64(*(uint64_t *)(uintptr_t)(frame_base + 80u));
                __boot_serial_force_puts(" rbp=");
                __boot_serial_force_puthex64(*(uint64_t *)(uintptr_t)(frame_base + 64u));
                __boot_serial_force_puts(" r8=");
                __boot_serial_force_puthex64(*(uint64_t *)(uintptr_t)(frame_base + 56u));
                __boot_serial_force_puts(" r9=");
                __boot_serial_force_puthex64(*(uint64_t *)(uintptr_t)(frame_base + 48u));
                __boot_serial_force_puts(" r10=");
                __boot_serial_force_puthex64(*(uint64_t *)(uintptr_t)(frame_base + 40u));
                __boot_serial_force_puts(" r11=");
                __boot_serial_force_puthex64(*(uint64_t *)(uintptr_t)(frame_base + 32u));
                __boot_serial_force_puts(" r12=");
                __boot_serial_force_puthex64(*(uint64_t *)(uintptr_t)(frame_base + 24u));
                __boot_serial_force_puts(" r13=");
                __boot_serial_force_puthex64(*(uint64_t *)(uintptr_t)(frame_base + 16u));
                __boot_serial_force_puts(" r14=");
                __boot_serial_force_puthex64(*(uint64_t *)(uintptr_t)(frame_base + 8u));
                __boot_serial_force_puts(" r15=");
                __boot_serial_force_puthex64(*(uint64_t *)(uintptr_t)(frame_base + 0u));
                __boot_serial_force_puts(" ursp=");
                __boot_serial_force_puthex64(user_sp);
                compat3_debug_wayfire_addr_mapping(cur2, fault_rip, "user-exc-rip");
                compat3_debug_wayfire_stack_mapping(cur2, user_sp, "user-exc-stack");
                compat3_debug_wayfire_addr_mapping(cur2, *(uint64_t *)(uintptr_t)(frame_base + 0u), "user-exc-r15");
                compat3_debug_wayfire_addr_mapping(cur2, *(uint64_t *)(uintptr_t)(frame_base + 8u), "user-exc-r14");
                compat3_debug_wayfire_addr_mapping(cur2, *(uint64_t *)(uintptr_t)(frame_base + 16u), "user-exc-r13");
                {
                    static uint32_t wayfire_loader_dump_count;
                    uint64_t r15v = *(uint64_t *)(uintptr_t)(frame_base + 0u);
                    if (cur2 && cur2->name[0] && k_contains(cur2->name, "wayfire") &&
                        r15v && wayfire_loader_dump_count < 24u) {
                        uint64_t qv;
                        uint64_t off;
                        ++wayfire_loader_dump_count;
                        __boot_serial_force_puts("[wayfire-ld-dump!] r15=");
                        __boot_serial_force_puthex64(r15v);
                        for (off = 0; off <= 0x80u; off += 0x8u) {
                            __boot_serial_force_puts(" +");
                            __boot_serial_force_puthex64(off);
                            __boot_serial_force_puts("=");
                            if (user_qword_ok(cur2, r15v + off, &qv)) {
                                __boot_serial_force_puthex64(qv);
                            } else {
                                __boot_serial_force_puts("np");
                            }
                        }
                    }
                }
                if (false && cur2 && user_sp < 0x0000800000000000ULL &&
                    (paging_get_entry(cur2->addr_space, user_sp) & PAGE_PRESENT)) {
                    __boot_serial_force_puts(" stack0=");
                    __boot_serial_force_puthex64(*(uint64_t *)(uintptr_t)user_sp);
                }
            }
            __boot_serial_force_puts("\n");
        }
        if (vec == 1u) {
            /* Debug exception (single-step / TF): clear TF in saved RFLAGS
             * at frame_base+152 and resume. */
            if (frame_base) {
                uint64_t *rflags_ptr = (uint64_t *)(uintptr_t)(frame_base + 152u);
                *rflags_ptr &= ~0x100ULL; /* clear TF */
            }
            __boot_serial_puts("[exc-user] vec=1 cleared TF, resuming pid=");
            {
                task_t *cur2 = task_current();
                if (cur2) __boot_serial_putu32((uint32_t)cur2->pid);
            }
            __boot_serial_puts(" rip=");
            __boot_serial_puthex64(fault_rip);
            __boot_serial_puts("\n");
            return;
        }
        /* Other user-mode exceptions: kill the faulting task. */
        if (trace_user_exc) {
            __boot_serial_puts("[exc-user] killing pid=");
            {
                task_t *cur2 = task_current();
                if (cur2) __boot_serial_putu32((uint32_t)cur2->pid);
            }
            __boot_serial_puts(" vec=");
            __boot_serial_putu32(vec);
            __boot_serial_puts(" rip=");
            __boot_serial_puthex64(fault_rip);
            __boot_serial_puts("\n");
        }
        real_sys_exit(-11); /* SIGSEGV-like exit */
        return; /* unreachable, real_sys_exit never returns */
    }
    buf[0] = 0;
    k_append_str(buf, &len, sizeof(buf), "vector ");
    k_append_u32(buf, &len, sizeof(buf), vec);
    k_append_str(buf, &len, sizeof(buf), " err 0x");
    k_append_hex(buf, &len, sizeof(buf), err, 8);
    k_append_str(buf, &len, sizeof(buf), " rip 0x");
    k_append_hex64(buf, &len, sizeof(buf), fault_rip, 16);
    k_append_str(buf, &len, sizeof(buf), " cs 0x");
    k_append_hex64(buf, &len, sizeof(buf), saved_cs, 4);
    if (vec == 14u) {
        k_append_str(buf, &len, sizeof(buf), " cr2 0x");
        k_append_hex64(buf, &len, sizeof(buf), cr2, 16);
    }
    panic(name, buf);
}

/* Forward decls for driver-side handlers pulled from IRQ context. */
static void pit_tick_irq(void);
static void kbd_irq_handler(void);
static void mouse_irq_handler(void);

/* Dispatcher called from isr.S for hardware IRQs (already remapped). */
void irq_dispatch(uint32_t irq, uint64_t saved_cs) {
    bool do_preempt = false;
    bool irq_from_user = ((saved_cs & 3ULL) == 3ULL);
    if (irq < 16u) g_irq_counts[irq]++;
    switch (irq) {
        case 0:
            pit_tick_irq();
            do_preempt = g_irq0_preempt_request;
            if (do_preempt && irq_from_user) g_irq0_preempt_request = false;
            break;  /* timer */
        case 1:  kbd_irq_handler();    break;  /* keyboard */
        case 12: mouse_irq_handler();  break;  /* PS/2 mouse */
        default:                       break;  /* unhandled: still EOI */
    }
    pic_eoi((uint8_t)irq);
    if (do_preempt && irq_from_user) task_schedule();
}

static void pit_tick_irq(void) {
    g_pit_ticks++;
    g_uptime_ticks++;
    /* Drive UI clock at low rate. The desktop only shows minute-level time;
     * repainting from a 1s tick keeps idle CPU/MMIO cost low. */
    if ((g_pit_ticks % 100u) == 0u) {
        rtc_tick();
        if (!g_wayfire_desktop_active) g_needs_redraw = true;
    }
    scheduler_tick();
    /* Preemptive scheduling for user tasks (threads created via clone3).
     * A 20 ms quantum keeps the desktop responsive while the browser is
     * chewing through startup. Browser tasks that still require cooperative
     * scheduling keep no_timer_preempt set and are skipped here. */
    if (g_user_foreground_active) {
        task_t *cur_task = task_current();
        if (g_task_preempt_defer_ticks) {
            --g_task_preempt_defer_ticks;
        } else if (!g_kernel_preempt_disable &&
                   !(cur_task && cur_task->no_timer_preempt)) {
            g_irq0_preempt_request = true;
        }
    }
    /* When a user task (Firefox realrun) is in foreground iret loop,
     * the normal kernel main loop is paused. Keep the Ridux desktop
     * responsive from timer IRQ context so screen/input do not freeze. */
    if (g_user_foreground_active) {
        static uint64_t last_irq_full_render_tick=0;
        static uint64_t last_wayfire_compat_tick=0;
        static uint64_t last_wayfire_snapshot_tick=0;
        bool had_input = input_pump(32);
        if (g_wayfire_desktop_active) {
            bool desktop_debug=k_desktop_debug_trace_enabled();
            if (desktop_debug && (g_pit_ticks-last_wayfire_snapshot_tick)>=500u) {
                int i;
                task_t *cur_task = task_current();
                last_wayfire_snapshot_tick=g_pit_ticks;
                __boot_serial_force_puts("[desktop-heartbeat!] tick=");
                __boot_serial_force_putu32((uint32_t)g_pit_ticks);
                __boot_serial_force_puts(" cur=");
                __boot_serial_force_putu32(cur_task?(uint32_t)cur_task->pid:0);
                if(cur_task&&cur_task->name[0]){
                    __boot_serial_force_puts(":");
                    __boot_serial_force_puts(cur_task->name);
                }
                __boot_serial_force_puts(" state=");
                __boot_serial_force_putu32(cur_task?(uint32_t)cur_task->state:0);
                __boot_serial_force_puts(" rip=");
                __boot_serial_force_puthex64(cur_task?cur_task->ctx.rip:0);
                __boot_serial_force_puts(" rsp=");
                __boot_serial_force_puthex64(cur_task?cur_task->ctx.rsp:0);
                __boot_serial_force_puts(" ntp=");
                __boot_serial_force_putu32((cur_task&&cur_task->no_timer_preempt)?1u:0u);
                if(cur_task){
                    compat3_debug_wayfire_addr_mapping(cur_task,cur_task->ctx.rip,"desktop-heartbeat-rip");
                    compat3_debug_wayfire_stack_mapping(cur_task,cur_task->ctx.rsp,"desktop-heartbeat-sp");
                }
                for(i=0;i<TASK_MAX;++i){
                    task_t *t=&g_tasks[i];
                    if(!t->used)continue;
                    if(!(k_contains(t->name,"wayfire")||
                         k_contains(t->name,"Wayfire")||
                         k_contains(t->name,"Hyprland")||
                         k_contains(t->name,"hyprland")||
                         k_contains(t->name,"start-hyprland")||
                         k_contains(t->name,"waybar")||
                         k_contains(t->name,"nwg-dock")||
                         k_contains(t->name,"hyprctl")||
                         k_contains(t->name,"xdg-desktop")||
                         k_contains(t->exec_path,"/wayfire")||
                         k_contains(t->exec_path,"/Wayfire")||
                         k_contains(t->exec_path,"/Hyprland")||
                         k_contains(t->exec_path,"/hyprland")||
                         k_contains(t->exec_path,"/start-hyprland")||
                         k_contains(t->exec_path,"/waybar")||
                         k_contains(t->exec_path,"/nwg-dock")||
                         k_contains(t->exec_path,"/hyprctl")||
                         k_contains(t->exec_path,"/xdg-desktop")))continue;
                    __boot_serial_force_puts(" | ");
                    __boot_serial_force_putu32((uint32_t)t->pid);
                    __boot_serial_force_puts(":s");
                    __boot_serial_force_putu32((uint32_t)t->state);
                    __boot_serial_force_puts(":ntp");
                    __boot_serial_force_putu32(t->no_timer_preempt?1u:0u);
                    __boot_serial_force_puts(":rip");
                    __boot_serial_force_puthex64(t->ctx.rip);
                    __boot_serial_force_puts(":rsp");
                    __boot_serial_force_puthex64(t->ctx.rsp);
                    compat3_debug_wayfire_addr_mapping(t,t->ctx.rip,"desktop-heartbeat-task-rip");
                    compat3_debug_wayfire_stack_mapping(t,t->ctx.rsp,"desktop-heartbeat-task-sp");
                    if(t->name[0]){
                        __boot_serial_force_puts(":");
                        __boot_serial_force_puts(t->name);
                    }
                }
                __boot_serial_force_puts("\n");
            }
            if ((g_pit_ticks-last_wayfire_compat_tick)>=10u) {
                last_wayfire_compat_tick=g_pit_ticks;
                compat7_tick_all();
            }
            (void)had_input;
            return;
        }
        if (g_kernel_preempt_disable) {
            if (had_input) g_needs_redraw = true;
            return;
        }
        compat7_tick_all();
        /* Full scene rendering from IRQ context blocks all other IRQs while
         * the framebuffer and Flush queue are being rebuilt. Keep it as a
         * slow fallback for kernel UI changes; browser/X11 damage and cursor
         * motion stay on the cheap paths so the mouse does not disappear
         * under browser startup load. */
        if (g_needs_redraw && (g_pit_ticks-last_irq_full_render_tick)>=25u) {
            render_scene();
            last_irq_full_render_tick=g_pit_ticks;
            compat7_tick_all();
        } else if (!g_needs_redraw && g_cursor_moved) {
            render_cursor_only();
        }
        (void)had_input;
    }
}

static void kbd_irq_handler(void) {
    uint8_t st = inb(0x64);
    if (!(st & 1u)) return;
    /* Only consume keyboard bytes here; mouse bytes (bit 5 set) will also
     * fire IRQ12 separately. */
    if (st & 0x20u) {
        mouse_ring_push(inb(0x60));
    } else {
        kbd_ring_push(inb(0x60));
    }
}

static void mouse_irq_handler(void) {
    uint8_t st = inb(0x64);
    if (!(st & 1u)) return;
    if (st & 0x20u) mouse_ring_push(inb(0x60));
    else            kbd_ring_push(inb(0x60));
}

static void idt_install(void) {
    int i;
    /* Zero all gates first. */
    k_memset(g_idt, 0, sizeof(g_idt));
    /* CPU exceptions: 0..31 */
    for (i = 0; i < 32; ++i) {
        idt_set_gate((uint8_t)i, (uintptr_t)g_isr_stubs[i], 0x08, 0x8E);
    }
    /* Hardware IRQs: vectors 32..47 */
    for (i = 0; i < 16; ++i) {
        idt_set_gate((uint8_t)(32 + i), (uintptr_t)g_irq_stubs[i], 0x08, 0x8E);
    }
    g_idt_ptr.limit = (uint16_t)(sizeof(g_idt) - 1);
    g_idt_ptr.base  = (uintptr_t)&g_idt[0];
    __asm__ __volatile__("lidt %0" : : "m"(g_idt_ptr));
    g_idt_ready = true;
}

#if defined(__x86_64__)
static void interrupts_bootstrap(void) {
    /* Mask every IRQ, remap, install IDT, then selectively unmask. */
    outb(0x21, 0xFF);
    outb(0xA1, 0xFF);
    pic_remap(32, 40);
    idt_install();
    /* Unmask the ones we handle:
     *   IRQ0 = timer, IRQ1 = keyboard, IRQ2 = cascade, IRQ12 = mouse */
    irq_enable(0);
    irq_enable(1);
    irq_enable(2);
    irq_enable(12);
    __asm__ __volatile__("sti");
}
#else
static void interrupts_bootstrap(void) {
    /* Mask every IRQ, remap, install IDT, then selectively unmask. */
    outb(0x21, 0xFF);
    outb(0xA1, 0xFF);
    pic_remap(32, 40);
    idt_install();
    /* Unmask the ones we handle:
     *   IRQ0 = timer, IRQ1 = keyboard, IRQ2 = cascade, IRQ12 = mouse */
    irq_enable(0);
    irq_enable(1);
    irq_enable(2);
    irq_enable(12);
    __asm__ __volatile__("sti");
}
#endif

/* ============================================================
 * [6.6] Kernel heap (first-fit with coalescing)
 * ------------------------------------------------------------
 * 4 MB arena in BSS. Classic block header, O(n) search but plenty
 * fast for our ~few-hundred allocations/second.
 * ============================================================ */

#define HEAP_SIZE (4u * 1024u * 1024u)
static uint8_t g_heap_arena[HEAP_SIZE] __attribute__((aligned(16)));

typedef struct heap_block {
    uint32_t           size;   /* payload bytes, excludes header */
    uint32_t           used;   /* 0/1 */
    struct heap_block *next;
    uint32_t           magic;  /* corruption guard */
} heap_block_t;

#define HEAP_MAGIC 0xB105F00Du

static heap_block_t *g_heap_head;
static uint32_t      g_heap_alloc_count;
static uint32_t      g_heap_free_count;
static uint32_t      g_heap_used_bytes;
static uint32_t      g_heap_peak_bytes;

static void heap_init(void) {
    g_heap_head = (heap_block_t *)(void *)g_heap_arena;
    g_heap_head->size  = HEAP_SIZE - sizeof(heap_block_t);
    g_heap_head->used  = 0;
    g_heap_head->next  = NULL;
    g_heap_head->magic = HEAP_MAGIC;
    g_heap_alloc_count = 0;
    g_heap_free_count  = 0;
    g_heap_used_bytes  = 0;
    g_heap_peak_bytes  = 0;
}

static void *kmalloc(size_t size) {
    heap_block_t *b;
    if (size == 0) return NULL;
    size = (size + 7u) & ~(size_t)7u;
    for (b = g_heap_head; b != NULL; b = b->next) {
        if (b->magic != HEAP_MAGIC) panic("heap corruption", "magic mismatch");
        if (b->used || b->size < size) continue;
        /* Split if room for another header + min payload. */
        if (b->size >= size + sizeof(heap_block_t) + 16u) {
            heap_block_t *nb = (heap_block_t *)((uint8_t *)(b + 1) + size);
            nb->size  = b->size - (uint32_t)size - (uint32_t)sizeof(heap_block_t);
            nb->used  = 0;
            nb->next  = b->next;
            nb->magic = HEAP_MAGIC;
            b->next   = nb;
            b->size   = (uint32_t)size;
        }
        b->used = 1;
        g_heap_alloc_count++;
        g_heap_used_bytes += b->size;
        if (g_heap_used_bytes > g_heap_peak_bytes) g_heap_peak_bytes = g_heap_used_bytes;
        return (void *)(b + 1);
    }
    return NULL;
}

static void kfree(void *p) {
    heap_block_t *cur;
    heap_block_t *b;
    if (!p) return;
    b = (heap_block_t *)p - 1;
    if (b->magic != HEAP_MAGIC) panic("heap free: bad pointer", "magic mismatch");
    if (!b->used)               panic("heap free: double free", "already unused");
    b->used = 0;
    if (g_heap_used_bytes >= b->size) g_heap_used_bytes -= b->size;
    g_heap_free_count++;
    /* Coalesce neighboring free blocks walking from head. */
    for (cur = g_heap_head; cur != NULL; ) {
        if (!cur->used && cur->next && !cur->next->used) {
            cur->size += (uint32_t)sizeof(heap_block_t) + cur->next->size;
            cur->next  = cur->next->next;
            continue;
        }
        cur = cur->next;
    }
}

static void *kcalloc(size_t n, size_t size) {
    void *p;
    size_t total = n * size;
    p = kmalloc(total);
    if (p) k_memset(p, 0, total);
    return p;
}

static void heap_stats(uint32_t *used, uint32_t *peak, uint32_t *allocs, uint32_t *frees) {
    if (used)   *used   = g_heap_used_bytes;
    if (peak)   *peak   = g_heap_peak_bytes;
    if (allocs) *allocs = g_heap_alloc_count;
    if (frees)  *frees  = g_heap_free_count;
}

/* ============================================================
 * [6.7] Kernel log ring (klog() already existed; wrap into ring
 *       so a "Log Viewer" app can inspect last N kernel messages)
 * ============================================================ */

#define KLOG_LINES      256
#define KLOG_LINE_LEN   128
static char     g_klog_buf[KLOG_LINES][KLOG_LINE_LEN];
static uint32_t g_klog_head;      /* next write slot */
static uint32_t g_klog_count;     /* total entries so far */

static void klog_ring_push(const char *line) {
    k_strlcpy(g_klog_buf[g_klog_head], line, KLOG_LINE_LEN);
    g_klog_head = (g_klog_head + 1u) % KLOG_LINES;
    if (g_klog_count < 0xFFFFFFFFu) g_klog_count++;
}
static const char *klog_ring_get(uint32_t age) {
    /* age=0 is the newest line, age=1 previous, ... */
    uint32_t count = g_klog_count < KLOG_LINES ? g_klog_count : KLOG_LINES;
    uint32_t idx;
    if (age >= count) return NULL;
    idx = (g_klog_head + KLOG_LINES - 1u - age) % KLOG_LINES;
    return g_klog_buf[idx];
}
static uint32_t klog_ring_count(void) {
    return g_klog_count < KLOG_LINES ? g_klog_count : KLOG_LINES;
}

/* ============================================================
 * [6.8] ACPI / power control
 * ------------------------------------------------------------
 * Tiny ACPI poke for VBox/QEMU clean poweroff. Real ACPI needs
 * RSDT parsing; we hard-code the ports that QEMU/Bochs expose.
 * Falls back to triple-fault reboot via keyboard controller.
 * ============================================================ */

static void power_reboot(void) {
    /* Keyboard controller 8042: pulse reset line. */
    outb(0x64, 0xFE);
    for (;;) __asm__ __volatile__("hlt");
}

static void power_shutdown(void) {
    /* Known magic writes for QEMU / Bochs / VBox. */
    outw(0xB004, 0x2000);  /* Bochs / old QEMU          */
    outw(0x604,  0x2000);  /* QEMU ACPI PM1a control    */
    outw(0x4004, 0x3400);  /* VirtualBox ACPI           */
    /* Fallback: just halt. */
    for (;;) __asm__ __volatile__("cli; hlt");
}

/* Animation engine (ease + per-window open/close/hover) */

typedef struct {
    uint8_t  active;      /* 0/1                                  */
    uint32_t start_tsc;   /* rdtsc when started                   */
    uint32_t dur_tsc;     /* duration in tsc ticks                */
    int      from, to;    /* integer state, used per-site         */
} anim_t;

/* Global animation state (one per window id, plus a few global slots). */
static anim_t  g_win_open_anim[WINDOW_MAX];
static anim_t  g_win_close_anim[WINDOW_MAX];
static uint8_t g_dock_hover[APP_MAX];      /* hover fade, 0..255 */
static uint32_t g_dock_hover_last;

static uint32_t anim_ms_to_tsc(uint32_t ms) {
    /* g_frame_period_tsc is ~16ms worth of cycles. */
    return (g_frame_period_tsc * ms) / 16u;
}

static void anim_start(anim_t *a, uint32_t dur_ms, int from, int to) {
    a->active    = 1;
    a->start_tsc = rdtsc32();
    a->dur_tsc   = anim_ms_to_tsc(dur_ms);
    a->from      = from;
    a->to        = to;
}

/* Cubic ease-out: 1 - (1-t)^3. Returns 0..256. */
static uint32_t ease_out_cubic_q8(uint32_t t_q8) {
    uint32_t inv = 256u - (t_q8 > 256u ? 256u : t_q8);
    uint32_t c   = (inv * inv * inv) >> 16; /* (256-t)^3/256^2 ~ range 0..256 */
    return 256u - c;
}

/* Returns 0..256 progress (q8). If not active, returns 256 (done). */
static uint32_t anim_progress_q8(const anim_t *a) {
    uint32_t now, elapsed;
    if (!a->active || a->dur_tsc == 0) return 256u;
    now     = rdtsc32();
    elapsed = (uint32_t)(now - a->start_tsc);
    if (elapsed >= a->dur_tsc) return 256u;
    return (elapsed * 256u) / a->dur_tsc;
}

static int anim_value(const anim_t *a) {
    uint32_t p = anim_progress_q8(a);
    uint32_t e = ease_out_cubic_q8(p);
    int diff = a->to - a->from;
    return a->from + (int)((int32_t)diff * (int32_t)e / 256);
}

static void anim_tick_all(void) {
    int i;
    uint32_t now = rdtsc32();
    /* Mark finished open/close anims as inactive. */
    for (i = 0; i < WINDOW_MAX; ++i) {
        if (g_win_open_anim[i].active &&
            (uint32_t)(now - g_win_open_anim[i].start_tsc) >= g_win_open_anim[i].dur_tsc) {
            g_win_open_anim[i].active = 0;
        }
        if (g_win_close_anim[i].active &&
            (uint32_t)(now - g_win_close_anim[i].start_tsc) >= g_win_close_anim[i].dur_tsc) {
            g_win_close_anim[i].active = 0;
        }
    }
}
