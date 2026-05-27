// Shim chico para Firefox en Ridux.
// En la VM glibc abre dependentlibs.list, pero fgets() nos estaba devolviendo una linea vacia.
// Le damos la lista a mano y dejamos el resto tal cual.
#define _GNU_SOURCE
#include <dlfcn.h>
#include <stddef.h>
#include <stdio.h>

static FILE *g_dep_file;
static unsigned g_dep_line;

static const char *const g_dep_lines[] = {
    "libnspr4.so\n",
    "libplc4.so\n",
    "libplds4.so\n",
    "libmozsandbox.so\n",
    "libgkcodecs.so\n",
    "liblgpllibs.so\n",
    "libnssutil3.so\n",
    "libnss3.so\n",
    "libsmime3.so\n",
    "libmozsqlite3.so\n",
    "libssl3.so\n",
    "libmozgtk.so\n",
    "libmozwayland.so\n",
    "libxul.so\n",
    0
};

static int rp_has(const char *s, const char *needle) {
    size_t i, j;
    if (!s || !needle || !needle[0]) return 0;
    for (i = 0; s[i]; ++i) {
        for (j = 0; needle[j] && s[i + j] == needle[j]; ++j) {}
        if (!needle[j]) return 1;
    }
    return 0;
}

static void *rp_next_symbol(const char *name) {
    return dlsym(RTLD_NEXT, name);
}

static char *rp_copy_fgets_line(char *dst, int size, const char *line) {
    int i = 0;
    if (!dst || size <= 0 || !line) return 0;
    while (i + 1 < size && line[i]) {
        dst[i] = line[i];
        ++i;
    }
    dst[i] = 0;
    return dst;
}

FILE *fopen(const char *pathname, const char *mode) {
    typedef FILE *(*real_fopen_t)(const char *, const char *);
    static real_fopen_t real_fopen;
    FILE *ret;

    if (!real_fopen) real_fopen = (real_fopen_t)rp_next_symbol("fopen");
    ret = real_fopen ? real_fopen(pathname, mode) : 0;
    if (ret && pathname && rp_has(pathname, "dependentlibs.list")) {
        g_dep_file = ret;
        g_dep_line = 0;
    }
    return ret;
}

char *fgets(char *s, int size, FILE *stream) {
    typedef char *(*real_fgets_t)(char *, int, FILE *);
    static real_fgets_t real_fgets;

    if (stream == g_dep_file) {
        const char *line = g_dep_lines[g_dep_line];
        if (line) ++g_dep_line;
        return rp_copy_fgets_line(s, size, line);
    }

    if (!real_fgets) real_fgets = (real_fgets_t)rp_next_symbol("fgets");
    return real_fgets ? real_fgets(s, size, stream) : 0;
}

int fclose(FILE *stream) {
    typedef int (*real_fclose_t)(FILE *);
    static real_fclose_t real_fclose;

    if (stream == g_dep_file) {
        g_dep_file = 0;
        g_dep_line = 0;
    }
    if (!real_fclose) real_fclose = (real_fclose_t)rp_next_symbol("fclose");
    return real_fclose ? real_fclose(stream) : -1;
}
