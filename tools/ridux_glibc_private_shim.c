__attribute__((visibility("default"))) int __libc_enable_secure = 0;

extern char **environ;

static unsigned long ridux_strlen(const char *s) {
    unsigned long n = 0;
    if (!s) return 0;
    while (s[n]) ++n;
    return n;
}

static int ridux_env_name_match(const char *entry, const char *name,
                                unsigned long name_len) {
    unsigned long i;
    if (!entry || !name || !name_len) return 0;
    for (i = 0; i < name_len; ++i) {
        if (entry[i] != name[i]) return 0;
    }
    return entry[name_len] == '=';
}

static char *ridux_getenv_plain(const char *name) {
    unsigned long name_len = ridux_strlen(name);
    char **it = environ;
    if (!name || !*name || !name_len) return 0;
    while (it && *it) {
        if (ridux_env_name_match(*it, name, name_len))
            return *it + name_len + 1;
        ++it;
    }
    return 0;
}

__attribute__((visibility("default"))) char *secure_getenv(const char *name) {
    return ridux_getenv_plain(name);
}

__attribute__((visibility("default"))) char *__secure_getenv(const char *name) {
    return ridux_getenv_plain(name);
}

__attribute__((visibility("default"))) char *__libc_secure_getenv(const char *name) {
    return ridux_getenv_plain(name);
}
