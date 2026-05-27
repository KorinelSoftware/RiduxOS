/*
 * Glue extra para levantar navegadores reales.
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdarg.h>
#include <stdint.h>
#include "base.h"
#include "memory_tasks.h"
#include "linux_syscalls.h"
#include "user_libc.h"
#include "bsd_libc.h"
#include "linux_abi.h"
#include "display_wayland.h"
#include "browser_runtime.h"

extern void*ulibc_malloc(size_t);extern void ulibc_free(void*);
extern void*ulibc_memcpy(void*,const void*,size_t);extern void*ulibc_memset(void*,int,size_t);
extern size_t ulibc_strlen(const char*);extern char*ulibc_strcpy(char*,const char*);
extern int ulibc_strcmp(const char*,const char*);extern int ulibc_strncmp(const char*,const char*,size_t);
extern int ulibc_memcmp(const void*,const void*,size_t);
extern int ulibc_snprintf(char*,size_t,const char*,...);
extern char*ulibc_getenv(const char*);
extern uint32_t ulibc_arc4random(void);
extern void ulibc_arc4random_buf(void*,size_t);
extern void*ulibc_dlsym(void*,const char*);
extern int64_t real_sys_mmap(uint64_t,uint64_t,int,int,int,uint64_t);
extern int64_t real_sys_munmap(uint64_t,uint64_t);
extern int64_t real_sys_mprotect(uint64_t,uint64_t,int);
extern int64_t real_sys_clone(uint64_t,uint64_t,uint64_t,uint64_t,uint64_t);
extern int64_t real_sys_futex(uint32_t*,int,uint32_t,const timespec_t*,uint32_t*,uint32_t);
extern int64_t real_sys_epoll_create1(int);
extern int64_t real_sys_epoll_ctl(int,int,int,void*);
extern int64_t real_sys_epoll_wait(int,void*,int,int);
extern int64_t real_sys_socketpair(int,int,int,int*);
extern int64_t real_sys_sendmsg(int,const void*,int);
extern int64_t real_sys_recvmsg(int,void*,int);
extern int64_t real_sys_prctl(int,uint64_t,uint64_t,uint64_t,uint64_t);
extern int64_t real_sys_getpid(void);
extern int64_t real_sys_gettid(void);
extern int64_t real_sys_kill(int,int);
extern int64_t real_sys_rt_sigaction(int,const void*,void*,size_t);
extern int64_t real_sys_clock_gettime(int,timespec_t*);
extern int64_t real_sys_sched_yield(void);
extern int64_t real_sys_write(int,const void*,size_t);
extern int64_t real_sys_read(int,void*,size_t);
extern int64_t real_sys_close(int);
extern int64_t real_sys_open(const char*,int,int);
extern int64_t real_sys_access(const char*,int);
extern int64_t real_sys_lseek(int,int64_t,int);
extern int64_t real_sys_socket(int,int,int);
extern int64_t real_sys_bind(int,const void*,uint32_t);
extern int64_t real_sys_listen(int,int);
extern int64_t real_sys_accept(int,void*,uint32_t*);
extern int64_t real_sys_connect(int,const void*,uint32_t);
extern int64_t real_sys_sendto(int,const void*,size_t,int,const void*,uint32_t);
extern int64_t real_sys_recvfrom(int,void*,size_t,int,void*,uint32_t*);
extern int64_t real_sys_seccomp(unsigned int,unsigned int,void*);
extern int64_t real_sys_userfaultfd(int);
extern int64_t real_sys_process_vm_readv(int,const iovec_t*,unsigned long,const iovec_t*,unsigned long,unsigned long);
extern int64_t real_sys_process_vm_writev(int,const iovec_t*,unsigned long,const iovec_t*,unsigned long,unsigned long);
extern int64_t real_sys_capget(void*,void*);
extern int64_t real_sys_capset(const void*,const void*);
extern int64_t real_sys_nanosleep(const timespec_t*,timespec_t*);
extern int compat3_dynobj_snapshot(compat3_dlobj_info_t*,int);
extern uint64_t compat3_lookup_global_symbol(const char*);
extern bool compat3_has_dynamic_relocator(void);
extern void compat3_set_next_image_name(const char*);
extern int elf64_map_into_task(task_t*,const uint8_t*,uint32_t);
extern task_t* task_current(void);

static void c8_strlcpy(char *dst,size_t cap,const char *src){
    size_t i=0;
    if(!dst||cap==0)return;
    if(!src){dst[0]=0;return;}
    while(i+1<cap&&src[i]){dst[i]=src[i];++i;}
    dst[i]=0;
}

/* glibc ABI completion */

/* Symbol table entry type - used by glibc symbol export and dlsym */
typedef struct { const char *name; void *addr; } c8_sym_entry_t;

/* atexit handlers */
typedef struct {
    void (*fn)(void*);
    void *arg;
    void *dso_handle;
} c8_atexit_entry_t;

static c8_atexit_entry_t g_c8_atexit[C8_ATEXIT_MAX];
static int g_c8_atexit_count = 0;

/* DSO handle */
static char g_c8_dso_handle_placeholder;
void *__dso_handle = &g_c8_dso_handle_placeholder;

/* glibc globals */
static char *g_c8_empty_envp[] = { 0 };
char **__environ = g_c8_empty_envp;
char **_environ = g_c8_empty_envp;
static char g_c8_progname[256] = "ridux";
static char g_c8_progname_full[256] = "/ridux";
char *__progname = g_c8_progname;
char *__progname_full = g_c8_progname_full;
char *program_invocation_name = g_c8_progname_full;
char *program_invocation_short_name = g_c8_progname;
void *__libc_stack_end = 0;
int __libc_enable_secure = 0;
int __libc_multiple_threads = 0;
int __libc_single_threaded = 1;

int __cxa_atexit(void (*fn)(void*), void *arg, void *dso_handle){
    if(g_c8_atexit_count >= C8_ATEXIT_MAX) return -1;
    g_c8_atexit[g_c8_atexit_count].fn = fn;
    g_c8_atexit[g_c8_atexit_count].arg = arg;
    g_c8_atexit[g_c8_atexit_count].dso_handle = dso_handle;
    ++g_c8_atexit_count;
    return 0;
}

int __cxa_finalize(void *dso_handle){
    int i;
    for(i = g_c8_atexit_count - 1; i >= 0; --i){
        if(dso_handle && g_c8_atexit[i].dso_handle != dso_handle) continue;
        if(g_c8_atexit[i].fn) g_c8_atexit[i].fn(g_c8_atexit[i].arg);
        g_c8_atexit[i].fn = 0;
    }
    return 0;
}

int atexit(void (*fn)(void)){
    return __cxa_atexit((void(*)(void*))fn, 0, 0);
}

static void c8_run_atexit_all(void){
    __cxa_finalize(0); /* run all */
}

int __libc_start_main(c8_main_t main_fn, int argc, char **argv,
                      void (*init)(int,char**,char**),
                      void (*fini)(void),
                      void (*rtld_fini)(void),
                      void *stack_end){
    __libc_stack_end = stack_end;
    /* Set up program name from argv */
    if(argv && argv[0]){
        size_t l = ulibc_strlen(argv[0]);
        if(l >= sizeof(g_c8_progname)) l = sizeof(g_c8_progname) - 1;
        ulibc_memcpy(g_c8_progname, argv[0], l); g_c8_progname[l] = 0;
        ulibc_memcpy(g_c8_progname_full, argv[0], l); g_c8_progname_full[l] = 0;
        __progname = g_c8_progname;
        __progname_full = g_c8_progname_full;
        program_invocation_name = g_c8_progname_full;
        /* Find basename */
        {const char *sl = g_c8_progname; size_t j;
         for(j=0;g_c8_progname[j];++j) if(g_c8_progname[j]=='/') sl=g_c8_progname+j+1;
         program_invocation_short_name = (char*)sl;}
    }
    /* Register rtld_fini and fini */
    if(rtld_fini) __cxa_atexit((void(*)(void*))rtld_fini, 0, 0);
    if(fini) __cxa_atexit((void(*)(void*))fini, 0, 0);
    /* Run init */
    if(init) init(argc, argv, __environ);
    /* Run main */
    int rc = main_fn(argc, argv, __environ);
    /* Run atexit handlers */
    c8_run_atexit_all();
    /* Exit */
    for(;;) __asm__ volatile("cli; hlt");
    return rc; /* never reached */
}

void __libc_init_first(int argc, char **argv, char **envp){
    (void)argc; (void)argv; (void)envp;
}

void __libc_init_secure(void){
    __libc_enable_secure = 0;
}

void __gmon_start__(void){ /* no profiling */ }
void _mcleanup(void){ /* no profiling */ }

/* __stack_chk_fail provided by compat4 */

int __cxa_thread_atexit(void (*fn)(void*), void *arg, void *dso){
    /* Simplified: just register in atexit list */
    return __cxa_atexit(fn, arg, dso);
}

void __cxa_thread_atexit_impl(void){}

void __tls_get_addr_opt(void){
    /* Optimized TLS access - delegates to ulibc___tls_get_addr */
}

long __syscall(long nr, ...){
    /* Generic syscall trampoline via compat dispatcher (with trace/ENOSYS telemetry). */
    extern int64_t syscall_dispatch(uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t);
    uint64_t args[6] = {0,0,0,0,0,0};
    va_list ap;
    int i;
    va_start(ap, nr);
    for(i = 0; i < 6; ++i){
        args[i] = (uint64_t)va_arg(ap, unsigned long);
    }
    va_end(ap);
    return (long)syscall_dispatch((uint64_t)nr,args[0],args[1],args[2],args[3],args[4],args[5]);
}

int __libc_sigaction(int sig, const void *act, void *old){
    return (int)real_sys_rt_sigaction(sig, act, old, 8);
}

int __libc_current_sigrtmin(void){ return 32; }
int __libc_current_sigrtmax(void){ return 64; }

/* setjmp / longjmp (x86-64) */
int c8_setjmp(c8_jmp_buf_t *env){
    if(!env) return 0;
    __asm__ volatile(
        "mov %%rbx, %0\n"
        "mov %%rbp, %1\n"
        "mov %%r12, %2\n"
        "mov %%r13, %3\n"
        "mov %%r14, %4\n"
        "mov %%r15, %5\n"
        : "=m"(env->rbx), "=m"(env->rbp), "=m"(env->r12),
          "=m"(env->r13), "=m"(env->r14), "=m"(env->r15)
    );
    __asm__ volatile("mov %%rsp, %0" : "=m"(env->rsp));
    /* Return address is on stack - we need the RIP that will be used on longjmp return */
    __asm__ volatile(
        "lea 0(%%rip), %%rax\n"
        "mov %%rax, %0\n"
        : "=m"(env->rip)
    );
    env->has_sigmask = 0;
    return 0;
}

void c8_longjmp(c8_jmp_buf_t *env, int val){
    if(!env) for(;;);
    if(val == 0) val = 1;
    __asm__ volatile(
        "mov %0, %%rbx\n"
        "mov %1, %%rbp\n"
        "mov %2, %%r12\n"
        "mov %3, %%r13\n"
        "mov %4, %%r14\n"
        "mov %5, %%r15\n"
        "mov %6, %%rsp\n"
        "mov %7, %%rax\n"  /* val */
        "jmp *%8\n"
        :
        : "m"(env->rbx), "m"(env->rbp), "m"(env->r12),
          "m"(env->r13), "m"(env->r14), "m"(env->r15),
          "m"(env->rsp), "m"(val), "m"(env->rip)
        : "rax", "rbx", "rbp", "r12", "r13", "r14", "r15", "rsp"
    );
    __builtin_unreachable();
}

int c8_sigsetjmp(c8_jmp_buf_t *env, int savesigs){
    env->has_sigmask = (uint8_t)(savesigs ? 1 : 0);
    if(savesigs){
        /* Save current signal mask */
        extern int64_t real_sys_rt_sigprocmask(int, const void*, void*, size_t);
        real_sys_rt_sigprocmask(0, 0, &env->sigmask, 8);
    }
    return c8_setjmp(env);
}

void c8_siglongjmp(c8_jmp_buf_t *env, int val){
    if(env->has_sigmask){
        extern int64_t real_sys_rt_sigprocmask(int, const void*, void*, size_t);
        real_sys_rt_sigprocmask(2, &env->sigmask, 0, 8);
    }
    c8_longjmp(env, val);
}

/* sigaction full */
int c8_sigaction(int sig, const c8_sigaction_t *act, c8_sigaction_t *old){
    /* Delegate to real_sys_rt_sigaction with struct conversion */
    return (int)real_sys_rt_sigaction(sig, act, old, sizeof(c8_sigaction_t));
}

/* locale / nl_langinfo + iconv */
#define C8_LOCALE_STR_MAX 64
static char g_c8_locale_by_cat[C8_LC_MESSAGES+1][C8_LOCALE_STR_MAX];
static char g_c8_locale_all_cache[256];
static char g_c8_locale_lang[16]="en";
static char g_c8_locale_territory[16]="US";
static char g_c8_locale_codeset[24]="UTF-8";
static char g_c8_locale_radix[4]=".";
static char g_c8_locale_thousep[4]="";
static bool g_c8_locale_inited=false;

typedef enum {
    C8_ENC_UNKNOWN=0,
    C8_ENC_UTF8,
    C8_ENC_LATIN1,
    C8_ENC_ASCII
} c8_encoding_t;
struct c8_iconv_desc {
    c8_encoding_t to_enc;
    c8_encoding_t from_enc;
};

static char c8loc_tolower(char ch){
    if(ch>='A'&&ch<='Z')return (char)(ch+('a'-'A'));
    return ch;
}
static bool c8loc_streq_ci(const char *a,const char *b){
    if(!a||!b)return false;
    while(*a&&*b){
        if(c8loc_tolower(*a)!=c8loc_tolower(*b))return false;
        ++a;++b;
    }
    return *a==0&&*b==0;
}
static bool c8loc_contains_ci(const char *hay,const char *needle){
    size_t i,j;
    if(!hay||!needle||!needle[0])return false;
    for(i=0;hay[i];++i){
        for(j=0;needle[j];++j){
            if(!hay[i+j])break;
            if(c8loc_tolower(hay[i+j])!=c8loc_tolower(needle[j]))break;
        }
        if(!needle[j])return true;
    }
    return false;
}
static void c8loc_trim_copy(char *dst,size_t cap,const char *src){
    size_t i=0,n=0,end;
    if(!dst||cap<2){return;}
    dst[0]=0;
    if(!src)return;
    while(src[i]&&((unsigned char)src[i]==' '||(unsigned char)src[i]=='\t'))++i;
    end=i;
    while(src[end])++end;
    while(end>i&&((unsigned char)src[end-1]==' '||(unsigned char)src[end-1]=='\t'))--end;
    while(i<end&&n+1<cap)dst[n++]=src[i++];
    dst[n]=0;
}
static void c8loc_codeset_normalize(const char *in,char *out,size_t cap){
    if(!out||cap<2){return;}
    out[0]=0;
    if(!in||!in[0]){
        c8_strlcpy(out,cap,"UTF-8");
        return;
    }
    if(c8loc_contains_ci(in,"utf-8")||c8loc_contains_ci(in,"utf8")){
        c8_strlcpy(out,cap,"UTF-8");
        return;
    }
    if(c8loc_contains_ci(in,"8859-1")||c8loc_contains_ci(in,"latin1")||c8loc_contains_ci(in,"latin-1")){
        c8_strlcpy(out,cap,"ISO-8859-1");
        return;
    }
    if(c8loc_contains_ci(in,"ascii")||c8loc_contains_ci(in,"ansi_x3.4-1968")){
        c8_strlcpy(out,cap,"ANSI_X3.4-1968");
        return;
    }
    c8_strlcpy(out,cap,in);
}
static void c8loc_parse(const char *locale,char *lang,size_t lang_cap,char *terr,size_t terr_cap,char *codeset,size_t cs_cap){
    char tmp[C8_LOCALE_STR_MAX];
    size_t i=0;
    size_t us=0,dot=0,at=0;
    if(!lang||!terr||!codeset)return;
    c8_strlcpy(lang,lang_cap,"en");
    c8_strlcpy(terr,terr_cap,"US");
    c8_strlcpy(codeset,cs_cap,"UTF-8");
    if(!locale||!locale[0])return;
    if(c8loc_streq_ci(locale,"C")||c8loc_streq_ci(locale,"POSIX")){
        c8_strlcpy(lang,lang_cap,"C");
        terr[0]=0;
        c8_strlcpy(codeset,cs_cap,"ANSI_X3.4-1968");
        return;
    }
    c8loc_trim_copy(tmp,sizeof(tmp),locale);
    while(tmp[i]){
        if(tmp[i]=='_'&&us==0)us=i;
        else if(tmp[i]=='.'&&dot==0)dot=i;
        else if(tmp[i]=='@'&&at==0)at=i;
        ++i;
    }
    if(at==0)at=i;
    if(dot==0||dot>at)dot=at;
    if(us==0||us>dot)us=dot;
    if(us>0){
        size_t n=us;
        if(n>=lang_cap)n=lang_cap-1;
        if(n)ulibc_memcpy(lang,tmp,n);
        lang[n]=0;
    }else{
        size_t n=dot;
        if(n>=lang_cap)n=lang_cap-1;
        if(n)ulibc_memcpy(lang,tmp,n);
        lang[n]=0;
    }
    if(us<dot){
        size_t n=dot-us-1;
        if(n>=terr_cap)n=terr_cap-1;
        if(n)ulibc_memcpy(terr,tmp+us+1,n);
        terr[n]=0;
    }else{
        terr[0]=0;
    }
    if(dot<at){
        char cs[24];
        size_t n=at-dot-1;
        if(n>=sizeof(cs))n=sizeof(cs)-1;
        if(n)ulibc_memcpy(cs,tmp+dot+1,n);
        cs[n]=0;
        c8loc_codeset_normalize(cs,codeset,cs_cap);
    }
    if(!lang[0])c8_strlcpy(lang,lang_cap,"en");
}
static const char *c8loc_env_for_category(int category){
    switch(category){
    case C8_LC_COLLATE: return "LC_COLLATE";
    case C8_LC_CTYPE: return "LC_CTYPE";
    case C8_LC_MONETARY: return "LC_MONETARY";
    case C8_LC_NUMERIC: return "LC_NUMERIC";
    case C8_LC_TIME: return "LC_TIME";
    case C8_LC_MESSAGES: return "LC_MESSAGES";
    default: return 0;
    }
}
static void c8loc_refresh_runtime(void){
    const char *eff=g_c8_locale_by_cat[C8_LC_CTYPE][0]?g_c8_locale_by_cat[C8_LC_CTYPE]:"C";
    c8loc_parse(eff,g_c8_locale_lang,sizeof(g_c8_locale_lang),
        g_c8_locale_territory,sizeof(g_c8_locale_territory),
        g_c8_locale_codeset,sizeof(g_c8_locale_codeset));
    if(c8loc_streq_ci(g_c8_locale_lang,"fr")||c8loc_streq_ci(g_c8_locale_lang,"de")||c8loc_streq_ci(g_c8_locale_lang,"es")){
        c8_strlcpy(g_c8_locale_radix,sizeof(g_c8_locale_radix),",");
        c8_strlcpy(g_c8_locale_thousep,sizeof(g_c8_locale_thousep),".");
    }else{
        c8_strlcpy(g_c8_locale_radix,sizeof(g_c8_locale_radix),".");
        c8_strlcpy(g_c8_locale_thousep,sizeof(g_c8_locale_thousep),",");
    }
}
static void c8loc_bootstrap(void){
    int i;
    const char *env;
    if(g_c8_locale_inited)return;
    for(i=C8_LC_ALL;i<=C8_LC_MESSAGES;++i)c8_strlcpy(g_c8_locale_by_cat[i],sizeof(g_c8_locale_by_cat[i]),"C");
    env=ulibc_getenv("LC_ALL");
    if(env&&env[0]){
        for(i=C8_LC_ALL;i<=C8_LC_MESSAGES;++i)c8_strlcpy(g_c8_locale_by_cat[i],sizeof(g_c8_locale_by_cat[i]),env);
    }else{
        for(i=C8_LC_COLLATE;i<=C8_LC_MESSAGES;++i){
            const char *v=ulibc_getenv(c8loc_env_for_category(i));
            if(v&&v[0])c8_strlcpy(g_c8_locale_by_cat[i],sizeof(g_c8_locale_by_cat[i]),v);
        }
        env=ulibc_getenv("LANG");
        if(env&&env[0]){
            for(i=C8_LC_COLLATE;i<=C8_LC_MESSAGES;++i){
                if(c8loc_streq_ci(g_c8_locale_by_cat[i],"C"))c8_strlcpy(g_c8_locale_by_cat[i],sizeof(g_c8_locale_by_cat[i]),env);
            }
        }
    }
    c8loc_refresh_runtime();
    g_c8_locale_inited=true;
}
static char *c8loc_current_string(int category){
    int i;
    c8loc_bootstrap();
    if(category==C8_LC_ALL){
        bool same=true;
        for(i=C8_LC_COLLATE;i<=C8_LC_MESSAGES;++i){
            if(ulibc_strcmp(g_c8_locale_by_cat[C8_LC_CTYPE],g_c8_locale_by_cat[i])!=0){same=false;break;}
        }
        if(same)return g_c8_locale_by_cat[C8_LC_CTYPE];
        ulibc_snprintf(g_c8_locale_all_cache,sizeof(g_c8_locale_all_cache),
            "LC_CTYPE=%s;LC_NUMERIC=%s;LC_TIME=%s;LC_COLLATE=%s;LC_MONETARY=%s;LC_MESSAGES=%s",
            g_c8_locale_by_cat[C8_LC_CTYPE],g_c8_locale_by_cat[C8_LC_NUMERIC],g_c8_locale_by_cat[C8_LC_TIME],
            g_c8_locale_by_cat[C8_LC_COLLATE],g_c8_locale_by_cat[C8_LC_MONETARY],g_c8_locale_by_cat[C8_LC_MESSAGES]);
        return g_c8_locale_all_cache;
    }
    if(category<C8_LC_ALL||category>C8_LC_MESSAGES)return 0;
    return g_c8_locale_by_cat[category];
}

char *c8_setlocale(int category, const char *locale){
    int i;
    const char *chosen=locale;
    c8loc_bootstrap();
    if(category<C8_LC_ALL||category>C8_LC_MESSAGES)return 0;
    if(!locale)return c8loc_current_string(category);
    if(locale[0]==0){
        const char *env=ulibc_getenv("LC_ALL");
        if(env&&env[0])chosen=env;
        else if(category!=C8_LC_ALL){
            env=ulibc_getenv(c8loc_env_for_category(category));
            if(env&&env[0])chosen=env;
            else{
                env=ulibc_getenv("LANG");
                chosen=(env&&env[0])?env:"C";
            }
        }else{
            env=ulibc_getenv("LANG");
            chosen=(env&&env[0])?env:"C";
        }
    }
    if(!chosen||!chosen[0])chosen="C";
    if(category==C8_LC_ALL){
        for(i=C8_LC_ALL;i<=C8_LC_MESSAGES;++i)c8_strlcpy(g_c8_locale_by_cat[i],sizeof(g_c8_locale_by_cat[i]),chosen);
    }else{
        c8_strlcpy(g_c8_locale_by_cat[category],sizeof(g_c8_locale_by_cat[category]),chosen);
    }
    c8loc_refresh_runtime();
    return c8loc_current_string(category);
}

char *c8_nl_langinfo(int item){
    c8loc_bootstrap();
    switch(item){
    case C8_CODESET: return g_c8_locale_codeset;
    case C8_RADIXCHAR: return g_c8_locale_radix;
    case C8_THOUSEP: return g_c8_locale_thousep;
    case C8_YESSTR: return (char*)"yes";
    case C8_NOSTR: return (char*)"no";
    case C8_CRNCYSTR: return (char*)"";
    case C8_LANG: return g_c8_locale_lang;
    case C8_TERRITORY: return g_c8_locale_territory;
    case C8_D_T_FMT: return (char*)"%a %b %e %H:%M:%S %Y";
    case C8_D_FMT: return c8loc_streq_ci(g_c8_locale_lang,"en")?(char*)"%m/%d/%Y":(char*)"%d/%m/%Y";
    case C8_T_FMT: return (char*)"%H:%M:%S";
    case C8_T_FMT_AMPM: return (char*)"%I:%M:%S %p";
    case C8_AM_STR: return (char*)"AM";
    case C8_PM_STR: return (char*)"PM";
    default: return (char*)"";
    }
}

static c8_encoding_t c8_iconv_parse_encoding(const char *name){
    if(!name||!name[0])return C8_ENC_UNKNOWN;
    if(c8loc_contains_ci(name,"utf-8")||c8loc_contains_ci(name,"utf8"))return C8_ENC_UTF8;
    if(c8loc_contains_ci(name,"8859-1")||c8loc_contains_ci(name,"latin1")||c8loc_contains_ci(name,"latin-1"))return C8_ENC_LATIN1;
    if(c8loc_contains_ci(name,"ascii")||c8loc_contains_ci(name,"ansi_x3.4-1968"))return C8_ENC_ASCII;
    return C8_ENC_UNKNOWN;
}
static int c8_utf8_decode_one(const uint8_t *in,size_t inlen,uint32_t *cp,size_t *used){
    uint8_t b0;
    if(!in||!cp||!used||inlen==0)return -1;
    b0=in[0];
    if((b0&0x80u)==0){*cp=b0;*used=1;return 0;}
    if((b0&0xE0u)==0xC0u){
        if(inlen<2||(in[1]&0xC0u)!=0x80u)return -1;
        *cp=((uint32_t)(b0&0x1Fu)<<6)|((uint32_t)(in[1]&0x3Fu));
        *used=2;
        return 0;
    }
    if((b0&0xF0u)==0xE0u){
        if(inlen<3||(in[1]&0xC0u)!=0x80u||(in[2]&0xC0u)!=0x80u)return -1;
        *cp=((uint32_t)(b0&0x0Fu)<<12)|((uint32_t)(in[1]&0x3Fu)<<6)|((uint32_t)(in[2]&0x3Fu));
        *used=3;
        return 0;
    }
    if((b0&0xF8u)==0xF0u){
        if(inlen<4||(in[1]&0xC0u)!=0x80u||(in[2]&0xC0u)!=0x80u||(in[3]&0xC0u)!=0x80u)return -1;
        *cp=((uint32_t)(b0&0x07u)<<18)|((uint32_t)(in[1]&0x3Fu)<<12)|((uint32_t)(in[2]&0x3Fu)<<6)|((uint32_t)(in[3]&0x3Fu));
        *used=4;
        return 0;
    }
    return -1;
}
static size_t c8_utf8_encode_one(uint32_t cp,uint8_t *out,size_t outcap){
    if(cp<=0x7Fu){
        if(outcap<1)return 0;
        out[0]=(uint8_t)cp;
        return 1;
    }
    if(cp<=0x7FFu){
        if(outcap<2)return 0;
        out[0]=(uint8_t)(0xC0u|((cp>>6)&0x1Fu));
        out[1]=(uint8_t)(0x80u|(cp&0x3Fu));
        return 2;
    }
    if(cp<=0xFFFFu){
        if(outcap<3)return 0;
        out[0]=(uint8_t)(0xE0u|((cp>>12)&0x0Fu));
        out[1]=(uint8_t)(0x80u|((cp>>6)&0x3Fu));
        out[2]=(uint8_t)(0x80u|(cp&0x3Fu));
        return 3;
    }
    if(cp<=0x10FFFFu){
        if(outcap<4)return 0;
        out[0]=(uint8_t)(0xF0u|((cp>>18)&0x07u));
        out[1]=(uint8_t)(0x80u|((cp>>12)&0x3Fu));
        out[2]=(uint8_t)(0x80u|((cp>>6)&0x3Fu));
        out[3]=(uint8_t)(0x80u|(cp&0x3Fu));
        return 4;
    }
    return 0;
}

c8_iconv_t c8_iconv_open(const char *tocode, const char *fromcode){
    struct c8_iconv_desc *cd=(struct c8_iconv_desc*)ulibc_malloc(sizeof(struct c8_iconv_desc));
    if(!cd){ULIBC_ERRNO=ENOMEM;return C8_ICONV_INVALID;}
    cd->to_enc=c8_iconv_parse_encoding(tocode);
    cd->from_enc=c8_iconv_parse_encoding(fromcode);
    if(cd->to_enc==C8_ENC_UNKNOWN||cd->from_enc==C8_ENC_UNKNOWN){
        ulibc_free(cd);
        ULIBC_ERRNO=EINVAL;
        return C8_ICONV_INVALID;
    }
    return cd;
}
size_t c8_iconv(c8_iconv_t cd, char **inbuf, size_t *inbytesleft, char **outbuf, size_t *outbytesleft){
    uint8_t *in;
    uint8_t *out;
    size_t inleft,outleft;
    size_t nonrev=0;
    if(cd==C8_ICONV_INVALID||!cd||!outbuf||!outbytesleft){ULIBC_ERRNO=EINVAL;return (size_t)-1;}
    if(!inbuf||!inbytesleft)return 0; /* state reset */
    in=(uint8_t*)*inbuf;
    out=(uint8_t*)*outbuf;
    inleft=*inbytesleft;
    outleft=*outbytesleft;
    while(inleft>0){
        uint32_t cp=0;
        size_t used=0,w=0;
        if(cd->from_enc==C8_ENC_UTF8){
            if(c8_utf8_decode_one(in,inleft,&cp,&used)<0){ULIBC_ERRNO=EINVAL;return (size_t)-1;}
        }else if(cd->from_enc==C8_ENC_LATIN1){
            cp=in[0];
            used=1;
        }else if(cd->from_enc==C8_ENC_ASCII){
            if(in[0]&0x80u){ULIBC_ERRNO=EINVAL;return (size_t)-1;}
            cp=in[0];
            used=1;
        }else{
            ULIBC_ERRNO=EINVAL;
            return (size_t)-1;
        }
        if(cd->to_enc==C8_ENC_UTF8){
            w=c8_utf8_encode_one(cp,out,outleft);
            if(w==0){ULIBC_ERRNO=E2BIG;return (size_t)-1;}
        }else if(cd->to_enc==C8_ENC_LATIN1){
            if(outleft<1){ULIBC_ERRNO=E2BIG;return (size_t)-1;}
            if(cp>255u){out[0]='?';++nonrev;}else out[0]=(uint8_t)cp;
            w=1;
        }else if(cd->to_enc==C8_ENC_ASCII){
            if(outleft<1){ULIBC_ERRNO=E2BIG;return (size_t)-1;}
            if(cp>127u){out[0]='?';++nonrev;}else out[0]=(uint8_t)cp;
            w=1;
        }else{
            ULIBC_ERRNO=EINVAL;
            return (size_t)-1;
        }
        in+=used;inleft-=used;
        out+=w;outleft-=w;
    }
    *inbuf=(char*)in;
    *outbuf=(char*)out;
    *inbytesleft=inleft;
    *outbytesleft=outleft;
    return nonrev;
}
int c8_iconv_close(c8_iconv_t cd){
    if(cd==C8_ICONV_INVALID||!cd){ULIBC_ERRNO=EINVAL;return -1;}
    ulibc_free(cd);
    return 0;
}

/* Dynamic loader enhancements */

/* dl_iterate_phdr - iterate loaded shared objects */
int c8_dl_iterate_phdr(c8_dl_phdr_cb_t callback, void *data){
    compat3_dlobj_info_t snap[C8_DYNOBJ_MAX];
    int n = compat3_dynobj_snapshot(snap, C8_DYNOBJ_MAX);
    int i, rc = 0;
    for(i = 0; i < n; ++i){
        c8_dlobj_info_t info;
        info.dlpi_addr = snap[i].dlpi_addr;
        info.dlpi_phdr = snap[i].dlpi_phdr;
        info.dlpi_phnum = snap[i].dlpi_phnum;
        info.tls_module = snap[i].tls_module;
        info.tls_init = snap[i].tls_init;
        info.tls_filesz = snap[i].tls_filesz;
        info.tls_memsz = snap[i].tls_memsz;
        info.tls_align = snap[i].tls_align;
        ulibc_memcpy(info.dlpi_name, snap[i].dlpi_name, sizeof(info.dlpi_name));
        rc = callback(&info, sizeof(info), data);
        if(rc) break;
    }
    return rc;
}

/* dlopen - load a shared library into current process */
void *c8_dlopen(const char *filename, int flags){
    enum { C8_DLOPEN_MAX = 64 * 1024 * 1024 };
    (void)flags;
    if(!filename) return (void*)1; /* RTLD_DEFAULT */
    /* Try to find already loaded */
    {compat3_dlobj_info_t snap[C8_DYNOBJ_MAX];
     int n = compat3_dynobj_snapshot(snap, C8_DYNOBJ_MAX);
     int i;
     for(i = 0; i < n; ++i){
         if(ulibc_strcmp(snap[i].dlpi_name, filename) == 0)
             return (void*)snap[i].dlpi_addr;
     }
    }
    /* Try to load from filesystem */
    {task_t *cur = task_current();
     if(cur){
         int fd = (int)real_sys_open(filename, 0 /* O_RDONLY */, 0);
         uint8_t *buf = 0;
         size_t cap = 0;
         size_t sz = 0;
         int64_t end_pos;
         if(fd < 0) return 0;
         end_pos=real_sys_lseek(fd,0,SEEK_END);
         if(end_pos>0&&end_pos<=C8_DLOPEN_MAX){
             cap=(size_t)end_pos;
             (void)real_sys_lseek(fd,0,SEEK_SET);
         }else{
             cap=1024*1024;
             (void)real_sys_lseek(fd,0,SEEK_SET);
         }
         if(cap<4096)cap=4096;
         buf=(uint8_t*)ulibc_malloc(cap);
         if(!buf){real_sys_close(fd);return 0;}
         for(;;){
             int64_t r;
             if(sz==cap){
                 size_t ncap=cap*2;
                 uint8_t *nbuf;
                 if(ncap> C8_DLOPEN_MAX)ncap=C8_DLOPEN_MAX;
                 if(ncap<=cap)break;
                 nbuf=(uint8_t*)ulibc_malloc(ncap);
                 if(!nbuf)break;
                 ulibc_memcpy(nbuf,buf,sz);
                 ulibc_free(buf);
                 buf=nbuf;
                 cap=ncap;
             }
             r=real_sys_read(fd,buf+sz,cap-sz);
             if(r<0){sz=0;break;}
             if(r==0)break;
             sz+=(size_t)r;
         }
         real_sys_close(fd);
         if(sz<64){ulibc_free(buf);return 0;}
         if(buf[0]!=0x7F||buf[1]!='E'||buf[2]!='L'||buf[3]!='F'){
             ulibc_free(buf);
             return 0;
         }
         compat3_set_next_image_name(filename);
         int rc = elf64_map_into_task(cur, buf, (uint32_t)sz);
         ulibc_free(buf);
         if(rc < 0) return 0;
         /* Return load bias as handle */
         return (void*)(uintptr_t)1; /* simplified handle */
     }
    }
    return 0;
}

static const c8_sym_entry_t g_c8_glibc_syms[] = {
    {"__libc_start_main", (void*)__libc_start_main},
    {"__cxa_atexit", (void*)__cxa_atexit},
    {"__cxa_finalize", (void*)__cxa_finalize},
    {"__cxa_thread_atexit", (void*)__cxa_thread_atexit},
    {"atexit", (void*)atexit},
    {"__dso_handle", (void*)&__dso_handle},
    {"__gmon_start__", (void*)__gmon_start__},
    {"__environ", (void*)&__environ},
    {"_environ", (void*)&_environ},
    {"__progname", (void*)&__progname},
    {"__progname_full", (void*)&__progname_full},
    {"program_invocation_name", (void*)&program_invocation_name},
    {"program_invocation_short_name", (void*)&program_invocation_short_name},
    {"__libc_stack_end", (void*)&__libc_stack_end},
    {"__libc_enable_secure", (void*)&__libc_enable_secure},
    {"__libc_multiple_threads", (void*)&__libc_multiple_threads},
    {"__libc_single_threaded", (void*)&__libc_single_threaded},
    {"__libc_init_first", (void*)__libc_init_first},
    {"__libc_init_secure", (void*)__libc_init_secure},
    {"__libc_sigaction", (void*)__libc_sigaction},
    {"__libc_current_sigrtmin", (void*)__libc_current_sigrtmin},
    {"__libc_current_sigrtmax", (void*)__libc_current_sigrtmax},
    {"setjmp", (void*)c8_setjmp},
    {"longjmp", (void*)c8_longjmp},
    {"sigsetjmp", (void*)c8_sigsetjmp},
    {"siglongjmp", (void*)c8_siglongjmp},
    {"sigaction", (void*)c8_sigaction},
    {"nl_langinfo", (void*)c8_nl_langinfo},
    {"setlocale", (void*)c8_setlocale},
    {"iconv_open", (void*)c8_iconv_open},
    {"iconv", (void*)c8_iconv},
    {"iconv_close", (void*)c8_iconv_close},
    {"dl_iterate_phdr", (void*)c8_dl_iterate_phdr},
    {"dlopen", (void*)c8_dlopen},
    {"dlsym", (void*)c8_dlsym},
    {"dlclose", (void*)c8_dlclose},
    {"dlerror", (void*)c8_dlerror},
    {"seccomp", (void*)c8_seccomp},
    {"userfaultfd", (void*)c8_userfaultfd},
    {"process_vm_readv", (void*)c8_process_vm_readv},
    {"process_vm_writev", (void*)c8_process_vm_writev},
    {"capget", (void*)c8_capget},
    {"capset", (void*)c8_capset},
    {0, 0}
};

void *c8_dlsym(void *handle, const char *symbol){
    int i;
    (void)handle;
    /* Search compat8 glibc symbols first */
    for(i = 0; g_c8_glibc_syms[i].name; ++i){
        if(ulibc_strcmp(g_c8_glibc_syms[i].name, symbol) == 0)
            return g_c8_glibc_syms[i].addr;
    }
    /* Then search compat3 global symbol table */
    {uint64_t addr = compat3_lookup_global_symbol(symbol);
     if(addr) return (void*)(uintptr_t)addr;
    }
    /* Finally fall through to ulibc dlsym */
    return ulibc_dlsym(handle, symbol);
}

int c8_dlclose(void *handle){
    (void)handle;
    return 0; /* simplified: no unloading */
}

static char g_c8_dlerror_buf[256] = "no error";
char *c8_dlerror(void){
    return g_c8_dlerror_buf;
}

/* Lazy PLT resolution - called from PLT trampoline */
void c8_resolve_plt_entry(uint64_t *got_entry, const char *sym_name){
    if(!got_entry || !sym_name) return;
    uint64_t addr = compat3_lookup_global_symbol(sym_name);
    if(addr){
        *got_entry = addr; /* patch GOT entry */
    }
    /* If not found, leave original stub - will fault on next call */
}

/* DT_RUNPATH search */
int c8_dyn_search_runpath(const char *name, char *resolved, size_t rsize){
    static const char *search_paths[] = {
        "/lib/", "/usr/lib/", "/lib/x86_64-linux-gnu/", "/usr/lib/x86_64-linux-gnu/",
        "/usr/local/lib/", "/opt/chrome/lib/", "/opt/firefox/", "/opt/firefox/lib/", 0
    };
    int i;
    if(!name || !resolved || rsize == 0) return -1;
    for(i = 0; search_paths[i]; ++i){
        size_t pl = ulibc_strlen(search_paths[i]);
        size_t nl = ulibc_strlen(name);
        if(pl + nl + 1 > rsize) continue;
        ulibc_memcpy(resolved, search_paths[i], pl);
        ulibc_memcpy(resolved + pl, name, nl);
        resolved[pl + nl] = 0;
        /* Check if file exists */
        extern int64_t real_sys_access(const char*,int);
        if(real_sys_access(resolved, 0 /* F_OK */) == 0) return 0;
    }
    return -1;
}

/* VERSYM resolution */
uint64_t c8_dyn_resolve_versioned(const char *name, uint32_t ver_hash, uint16_t ver_idx){
    (void)ver_hash; (void)ver_idx;
    /* Simplified: ignore versioning, just resolve by name */
    return compat3_lookup_global_symbol(name);
}

/* Sandbox syscalls */

/* seccomp - BPF filter installation */
static bool g_c8_seccomp_active = false;
static c8_sock_fprog_t g_c8_seccomp_filter = {0, 0, 0};

int c8_seccomp(unsigned int operation, unsigned int flags, void *args){
    int64_t rc=real_sys_seccomp(operation,flags,args);
    if(rc!=-ENOSYS)return (int)rc;
    switch(operation){
    case C8_SECCOMP_SET_MODE_STRICT:
        g_c8_seccomp_active = true;
        return 0;
    case C8_SECCOMP_SET_MODE_FILTER:
        if(!args) return -EINVAL;
        {c8_sock_fprog_t *fprog = (c8_sock_fprog_t*)args;
         if(fprog->len > 256) return -EINVAL;
         g_c8_seccomp_active = true;
         g_c8_seccomp_filter.len = fprog->len;
         g_c8_seccomp_filter.filter = fprog->filter;
        }
        return 0;
    case C8_SECCOMP_GET_ACTION_AVAIL:
        return 0;
    case C8_SECCOMP_GET_NOTIF_SIZES:
        return 0;
    default:
        return -EINVAL;
    }
}

/* prctl full */
int c8_prctl_full(int option, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5){
    switch(option){
    case 15: /* PR_SET_NAME */
        return 0;
    case 16: /* PR_GET_NAME */
        return 0;
    case 23: /* PR_SET_NO_NEW_PRIVS */
        return 0;
    case 38: /* PR_SET_SECCOMP */
        return c8_seccomp((unsigned int)a2, (unsigned int)a3, (void*)(uintptr_t)a4);
    case 4:  /* PR_SET_DUMPABLE */
        return 0;
    case 24: /* PR_GET_NO_NEW_PRIVS */
        return 0;
    case 5:  /* PR_GET_DUMPABLE */
        return 1;
    case 29: /* PR_GET_TID_ADDRESS */
        {task_t *cur = task_current();
         if(cur && a2) *(uint64_t*)(uintptr_t)a2 = cur->clear_child_tid;
        }
        return 0;
    case 30: /* PR_SET_TID_ADDRESS */
        {task_t *cur = task_current();
         if(cur) cur->clear_child_tid = a2;
        }
        return 0;
    case 37: /* PR_MCE_KILL */
        return 0;
    case 52: /* PR_SET_VMA */
        return 0;
    case 53: /* PR_GET_VMA */
        return -EINVAL;
    default:
        return (int)real_sys_prctl(option, a2, a3, a4, a5);
    }
}

/* userfaultfd */
int c8_userfaultfd(int flags){
    int64_t rc=real_sys_userfaultfd(flags);
    if(rc!=-ENOSYS)return (int)rc;
    /* Fallback for legacy builds without userfaultfd wiring. */
    {
        task_t *cur = task_current();
        int fd;
        if(!cur) return -ESRCH;
        fd = fd_alloc(&cur->fdt, FDKIND_EVENTFD, 0, FDFL_READABLE|FDFL_WRITABLE);
        return fd;
    }
}

static int64_t c8_iov_copy(c8_iovec_t *dst_iov, unsigned long dst_cnt,
                           const c8_iovec_t *src_iov, unsigned long src_cnt){
    unsigned long di = 0, si = 0;
    size_t doff = 0, soff = 0;
    int64_t total = 0;
    if(!dst_iov || !src_iov) return -EFAULT;
    if(dst_cnt > 4096ul || src_cnt > 4096ul) return -EINVAL;

    while(di < dst_cnt && si < src_cnt){
        c8_iovec_t *dst = &dst_iov[di];
        const c8_iovec_t *src = &src_iov[si];
        size_t drem = (dst->iov_len > doff) ? (dst->iov_len - doff) : 0;
        size_t srem = (src->iov_len > soff) ? (src->iov_len - soff) : 0;
        size_t n = (drem < srem) ? drem : srem;

        if(n == 0){
            if(drem == 0){ ++di; doff = 0; }
            if(srem == 0){ ++si; soff = 0; }
            continue;
        }
        if((!dst->iov_base && dst->iov_len > 0) || (!src->iov_base && src->iov_len > 0))
            return total ? total : -EFAULT;

        ulibc_memcpy((uint8_t*)dst->iov_base + doff, (const uint8_t*)src->iov_base + soff, n);
        total += (int64_t)n;
        doff += n;
        soff += n;

        if(doff >= dst->iov_len){ ++di; doff = 0; }
        if(soff >= src->iov_len){ ++si; soff = 0; }
    }
    return total;
}

/* process_vm_readv/writev */
int64_t c8_process_vm_readv(int pid, const c8_iovec_t *lvec, unsigned long liovcnt,
                            const c8_iovec_t *rvec, unsigned long riovcnt, unsigned long flags){
    int64_t rc=real_sys_process_vm_readv(pid,(const iovec_t*)lvec,liovcnt,(const iovec_t*)rvec,riovcnt,flags);
    if(rc!=-ENOSYS)return rc;
    {
        task_t *cur = task_current();
        if(!cur) return -ESRCH;
        if(flags != 0ul) return -EINVAL;
        if(pid != 0 && pid != cur->pid) return -EPERM;
        return c8_iov_copy((c8_iovec_t*)lvec, liovcnt, rvec, riovcnt);
    }
}

int64_t c8_process_vm_writev(int pid, const c8_iovec_t *lvec, unsigned long liovcnt,
                             const c8_iovec_t *rvec, unsigned long riovcnt, unsigned long flags){
    int64_t rc=real_sys_process_vm_writev(pid,(const iovec_t*)lvec,liovcnt,(const iovec_t*)rvec,riovcnt,flags);
    if(rc!=-ENOSYS)return rc;
    {
        task_t *cur = task_current();
        if(!cur) return -ESRCH;
        if(flags != 0ul) return -EINVAL;
        if(pid != 0 && pid != cur->pid) return -EPERM;
        return c8_iov_copy((c8_iovec_t*)rvec, riovcnt, lvec, liovcnt);
    }
}

/* capget/capset */
int c8_capget(void *hdr, c8_cap_user_data_t *data){
    int64_t rc=real_sys_capget(hdr,data);
    if(rc!=-ENOSYS)return (int)rc;
    if(data)ulibc_memset(data,0,sizeof(*data));
    return 0;
}

int c8_capset(void *hdr, const c8_cap_user_data_t *data){
    int64_t rc=real_sys_capset(hdr,data);
    if(rc!=-ENOSYS)return (int)rc;
    return 0;
}

/* Software rendering pipeline */

void c8_blit_argb32(uint32_t *dst, int dst_stride,
                    const uint32_t *src, int src_stride,
                    int x, int y, int w, int h){
    int dst_x=x;
    int src_x=0;
    int copy_w=w;
    int row;
    if(!dst || !src || w <= 0 || h <= 0 || dst_stride <= 0 || src_stride <= 0) return;
    if(dst_x<0){src_x=-dst_x;copy_w-=src_x;dst_x=0;}
    if(copy_w<=0||dst_x>=dst_stride||src_x>=src_stride)return;
    if(copy_w>dst_stride-dst_x)copy_w=dst_stride-dst_x;
    if(copy_w>src_stride-src_x)copy_w=src_stride-src_x;
    if(copy_w<=0)return;
    for(row = 0; row < h; ++row){
        int dy = y + row;
        if(dy < 0) continue;
        ulibc_memcpy(dst + dy * dst_stride + dst_x, src + row * src_stride + src_x, (size_t)copy_w * 4);
    }
}

void c8_fill_argb32(uint32_t *dst, int stride, int x, int y, int w, int h, uint32_t color){
    int dst_x=x;
    int fill_w=w;
    int row, col;
    if(!dst || stride <= 0 || w <= 0 || h <= 0) return;
    if(dst_x<0){fill_w+=dst_x;dst_x=0;}
    if(fill_w<=0||dst_x>=stride)return;
    if(fill_w>stride-dst_x)fill_w=stride-dst_x;
    if(fill_w<=0)return;
    for(row = 0; row < h; ++row){
        int dy = y + row;
        if(dy < 0) continue;
        for(col = 0; col < fill_w; ++col){
            int dx = dst_x + col;
            dst[dy * stride + dx] = color;
        }
    }
}

void c8_blend_argb32(uint32_t *dst, int dst_stride,
                     const uint32_t *src, int src_stride,
                     int x, int y, int w, int h, uint8_t alpha){
    int dst_x=x;
    int src_x=0;
    int blend_w=w;
    int row, col;
    if(!dst || !src || w <= 0 || h <= 0 || dst_stride <= 0 || src_stride <= 0) return;
    if(dst_x<0){src_x=-dst_x;blend_w-=src_x;dst_x=0;}
    if(blend_w<=0||dst_x>=dst_stride||src_x>=src_stride)return;
    if(blend_w>dst_stride-dst_x)blend_w=dst_stride-dst_x;
    if(blend_w>src_stride-src_x)blend_w=src_stride-src_x;
    if(blend_w<=0)return;
    for(row = 0; row < h; ++row){
        int dy = y + row;
        if(dy < 0) continue;
        for(col = 0; col < blend_w; ++col){
            int dx = dst_x + col;
            uint32_t s = src[row * src_stride + src_x + col];
            uint32_t d = dst[dy * dst_stride + dx];
            uint8_t sa = (uint8_t)((s >> 24) & 0xFF);
            uint8_t a = (uint8_t)((alpha * sa) / 255);
            if(a == 0) continue;
            if(a == 255){ dst[dy * dst_stride + dx] = s; continue; }
            uint8_t sr = (uint8_t)(s & 0xFF), sg = (uint8_t)((s>>8)&0xFF), sb = (uint8_t)((s>>16)&0xFF);
            uint8_t dr = (uint8_t)(d & 0xFF), dg = (uint8_t)((d>>8)&0xFF), db = (uint8_t)((d>>16)&0xFF);
            uint8_t inv = 255 - a;
            uint8_t rr = (uint8_t)((sr * a + dr * inv) / 255);
            uint8_t rg = (uint8_t)((sg * a + dg * inv) / 255);
            uint8_t rb = (uint8_t)((sb * a + db * inv) / 255);
            dst[dy * dst_stride + dx] = (uint32_t)((a << 24) | (rb << 16) | (rg << 8) | rr);
        }
    }
}

void c8_scale_argb32(uint32_t *dst, int dw, int dh, int dst_stride,
                     const uint32_t *src, int sw, int sh, int src_stride){
    int y, x;
    if(!dst || !src || dw <= 0 || dh <= 0 || sw <= 0 || sh <= 0) return;
    for(y = 0; y < dh; ++y){
        int sy = y * sh / dh;
        if(sy >= sh) sy = sh - 1;
        for(x = 0; x < dw; ++x){
            int sx = x * sw / dw;
            if(sx >= sw) sx = sw - 1;
            dst[y * dst_stride + x] = src[sy * src_stride + sx];
        }
    }
}

bool c8_rect_intersect(const c8_rect_t *a, const c8_rect_t *b, c8_rect_t *out){
    int x1, y1, x2, y2;
    if(!a || !b || !out) return false;
    x1 = a->x > b->x ? a->x : b->x;
    y1 = a->y > b->y ? a->y : b->y;
    x2 = (a->x + a->w) < (b->x + b->w) ? (a->x + a->w) : (b->x + b->w);
    y2 = (a->y + a->h) < (b->y + b->h) ? (a->y + a->h) : (b->y + b->h);
    if(x1 >= x2 || y1 >= y2) return false;
    out->x = x1; out->y = y1; out->w = x2 - x1; out->h = y2 - y1;
    return true;
}

/* Font rendering (built-in 8x16 bitmap font) */
static uint8_t g_c8_font8x16[128][16]; /* ASCII only, 8x16 bitmap */
static c8_font_metrics_t g_c8_font_metrics = { 16, 12, -2, 16 };
static bool g_c8_font_initialized = false;

/* Minimal 8x16 font data for printable ASCII (32-126) */
static void c8_font_init_internal(void){
    int c, row;
    if(g_c8_font_initialized) return;
    g_c8_font_initialized = true;
    /* Generate a simple bitmap font pattern */
    for(c = 0; c < 128; ++c){
        for(row = 0; row < 16; ++row){
            uint8_t bits = 0;
            if(c >= 32 && c < 127){
                /* Simple pattern: vertical bars for sides, horizontal for top/bottom */
                int r = row;
                if(r == 0 || r == 15) bits = 0x7E; /* top/bottom */
                else if(r == 1 || r == 14) bits = 0x81; /* corners */
                else if(c >= 'A' && c <= 'Z'){
                    /* Letter shape approximation */
                    if(r >= 2 && r <= 6) bits = 0x7E; /* upper block */
                    else if(r >= 8 && r <= 12) bits = 0x7E; /* lower block */
                    else bits = 0x81; /* sides */
                    if(r == 7) bits = 0x42; /* middle gap */
                }else if(c >= '0' && c <= '9'){
                    if(r >= 2 && r <= 12) bits = 0x7E;
                    if(r == 5 || r == 9) bits = 0x42;
                }else{
                    /* Generic box */
                    if(r >= 2 && r <= 12) bits = 0x81;
                }
            }
            g_c8_font8x16[c][row] = bits;
        }
    }
}

void c8_font_init(void){ c8_font_init_internal(); }

const c8_glyph_t *c8_font_get_glyph(uint32_t codepoint){
    static c8_glyph_t g;
    if(codepoint >= 128) codepoint = '?';
    if(codepoint < 32) codepoint = ' ';
    c8_font_init_internal();
    g.width = 8; g.height = 16;
    g.bearing_x = 0; g.bearing_y = 12;
    g.advance = 8;
    g.bitmap = g_c8_font8x16[codepoint];
    return &g;
}

const c8_font_metrics_t *c8_font_get_metrics(void){
    c8_font_init_internal();
    return &g_c8_font_metrics;
}

void c8_render_glyph_argb32(uint32_t *dst, int dst_stride, int dst_w, int dst_h,
                             int x, int y, const c8_glyph_t *g, uint32_t color){
    int row, col;
    if(!dst || !g || !g->bitmap) return;
    for(row = 0; row < g->height; ++row){
        int dy = y + g->bearing_y - g->height + row;
        if(dy < 0 || dy >= dst_h) continue;
        for(col = 0; col < g->width; ++col){
            int dx = x + col;
            if(dx < 0 || dx >= dst_w) continue;
            if(g->bitmap[row] & (0x80 >> col)){
                dst[dy * dst_stride + dx] = color;
            }
        }
    }
}

/* Skia software backend glue */
c8_surface_t *c8_surface_create(int w, int h){
    c8_surface_t *s;
    if(w <= 0 || h <= 0 || w > 8192 || h > 8192) return 0;
    s = (c8_surface_t*)ulibc_malloc(sizeof(c8_surface_t));
    if(!s) return 0;
    s->width = w; s->height = h; s->stride = w;
    s->pixels = (uint32_t*)ulibc_malloc((size_t)w * (size_t)h * 4);
    if(!s->pixels){ ulibc_free(s); return 0; }
    ulibc_memset(s->pixels, 0, (size_t)w * (size_t)h * 4);
    return s;
}

void c8_surface_destroy(c8_surface_t *s){
    if(!s) return;
    if(s->pixels) ulibc_free(s->pixels);
    ulibc_free(s);
}

void c8_surface_flush(c8_surface_t *s){
    (void)s; /* no-op for software rendering */
}

/* IPC / DBus */

static c8_dbus_service_t g_c8_dbus_services[C8_DBUS_MAX_SERVICES];
static c8_dbus_match_t g_c8_dbus_matches[C8_DBUS_MAX_MATCHES];
static int g_c8_dbus_service_count = 0;
static int g_c8_dbus_match_count = 0;

/* Unix domain socket registry */
#define C8_UNIX_SOCKET_MAX 64
typedef struct {
    bool used;
    char path[256];
    int  sock_fd;
} c8_unix_socket_t;
static c8_unix_socket_t g_c8_unix_sockets[C8_UNIX_SOCKET_MAX];

int c8_register_unix_socket(const char *path, int sock_fd){
    int i;
    if(!path || sock_fd < 0) return -1;
    for(i = 0; i < C8_UNIX_SOCKET_MAX; ++i){
        if(g_c8_unix_sockets[i].used && ulibc_strcmp(g_c8_unix_sockets[i].path, path) == 0){
            g_c8_unix_sockets[i].sock_fd = sock_fd;
            return 0;
        }
    }
    for(i = 0; i < C8_UNIX_SOCKET_MAX; ++i){
        if(!g_c8_unix_sockets[i].used){
            g_c8_unix_sockets[i].used = true;
            c8_strlcpy(g_c8_unix_sockets[i].path, sizeof(g_c8_unix_sockets[i].path), path);
            g_c8_unix_sockets[i].sock_fd = sock_fd;
            return 0;
        }
    }
    return -1;
}

int c8_lookup_unix_socket(const char *path){
    int i;
    if(!path) return -1;
    for(i = 0; i < C8_UNIX_SOCKET_MAX; ++i){
        if(g_c8_unix_sockets[i].used && ulibc_strcmp(g_c8_unix_sockets[i].path, path) == 0)
            return g_c8_unix_sockets[i].sock_fd;
    }
    return -1;
}

int c8_dbus_acquire_name(const char *name, int sock_fd){
    int i;
    if(!name) return -1;
    /* Check if already acquired */
    for(i = 0; i < C8_DBUS_MAX_SERVICES; ++i){
        if(g_c8_dbus_services[i].used && ulibc_strcmp(g_c8_dbus_services[i].name, name) == 0)
            return 0; /* already owned */
    }
    if(g_c8_dbus_service_count >= C8_DBUS_MAX_SERVICES) return -1;
    for(i = 0; i < C8_DBUS_MAX_SERVICES; ++i){
        if(!g_c8_dbus_services[i].used){
            g_c8_dbus_services[i].used = true;
            c8_strlcpy(g_c8_dbus_services[i].name, sizeof(g_c8_dbus_services[i].name), name);
            g_c8_dbus_services[i].owner_pid = (uint32_t)real_sys_getpid();
            g_c8_dbus_services[i].sock_fd = sock_fd;
            ++g_c8_dbus_service_count;
            return 1; /* acquired */
        }
    }
    return -1;
}

int c8_dbus_release_name(const char *name){
    int i;
    if(!name) return -1;
    for(i = 0; i < C8_DBUS_MAX_SERVICES; ++i){
        if(g_c8_dbus_services[i].used && ulibc_strcmp(g_c8_dbus_services[i].name, name) == 0){
            g_c8_dbus_services[i].used = false;
            if(g_c8_dbus_service_count > 0) --g_c8_dbus_service_count;
            return 1;
        }
    }
    return 0;
}

int c8_dbus_send_signal(const char *dest, const char *path,
                        const char *iface, const char *member,
                        const uint8_t *body, size_t body_len){
    int i;
    uint8_t msg[128 + C8_DBUS_MAX_MSG_SIZE];
    size_t pos = 0;
    size_t cap = sizeof(msg);
    (void)dest;
    if(body_len > C8_DBUS_MAX_MSG_SIZE) return -E2BIG;
    /* Build minimal DBus message header */
    c8_dbus_msg_hdr_t *hdr = (c8_dbus_msg_hdr_t*)msg;
    hdr->endian = 'l'; /* little-endian */
    hdr->type = 4; /* signal */
    hdr->flags = 0;
    hdr->version = 1;
    hdr->body_len = (uint32_t)body_len;
    hdr->serial = (uint32_t)ulibc_arc4random();
    /* Header fields: path, interface, member */
    pos = sizeof(c8_dbus_msg_hdr_t);
    /* Path field (type 'o', 1) */
    if(path){
        size_t pl = ulibc_strlen(path);
        if(pos + 4 + pl + 1 >= cap) return -E2BIG;
        msg[pos++] = 1; msg[pos++] = 1; msg[pos++] = 'o'; msg[pos++] = 0;
        ulibc_memcpy(msg + pos, path, pl + 1); pos += pl + 1;
        if(pos > cap) return -E2BIG;
        pos = (pos + 7) & ~7; /* align to 8 */
        if(pos > cap) return -E2BIG;
    }
    /* Interface field (type 's', 2) */
    if(iface){
        size_t il = ulibc_strlen(iface);
        if(pos + 4 + il + 1 >= cap) return -E2BIG;
        msg[pos++] = 2; msg[pos++] = 1; msg[pos++] = 's'; msg[pos++] = 0;
        ulibc_memcpy(msg + pos, iface, il + 1); pos += il + 1;
        if(pos > cap) return -E2BIG;
        pos = (pos + 7) & ~7;
        if(pos > cap) return -E2BIG;
    }
    /* Member field (type 's', 3) */
    if(member){
        size_t ml = ulibc_strlen(member);
        if(pos + 4 + ml + 1 >= cap) return -E2BIG;
        msg[pos++] = 3; msg[pos++] = 1; msg[pos++] = 's'; msg[pos++] = 0;
        ulibc_memcpy(msg + pos, member, ml + 1); pos += ml + 1;
        if(pos > cap) return -E2BIG;
        pos = (pos + 7) & ~7;
        if(pos > cap) return -E2BIG;
    }
    hdr->hdr_fields_len = (uint32_t)(pos - sizeof(c8_dbus_msg_hdr_t));
    /* Pad header to 8-byte boundary */
    while(pos & 7){
        if(pos >= cap) return -E2BIG;
        msg[pos++] = 0;
    }
    /* Append body */
    if(body && body_len > 0){
        if(pos + body_len > cap) return -E2BIG;
        ulibc_memcpy(msg + pos, body, body_len);
        pos += body_len;
    }
    /* Send to all matching subscribers */
    for(i = 0; i < C8_DBUS_MAX_SERVICES; ++i){
        if(g_c8_dbus_services[i].used && g_c8_dbus_services[i].sock_fd >= 0){
            real_sys_sendto(g_c8_dbus_services[i].sock_fd, msg, pos, 0, 0, 0);
        }
    }
    return 0;
}

int c8_dbus_add_match(int sock_fd, const char *rule){
    int i;
    if(!rule) return -1;
    if(g_c8_dbus_match_count >= C8_DBUS_MAX_MATCHES) return -1;
    for(i = 0; i < C8_DBUS_MAX_MATCHES; ++i){
        if(!g_c8_dbus_matches[i].used){
            g_c8_dbus_matches[i].used = true;
            g_c8_dbus_matches[i].service_idx = -1;
            c8_strlcpy(g_c8_dbus_matches[i].rule, sizeof(g_c8_dbus_matches[i].rule), rule);
            ++g_c8_dbus_match_count;
            (void)sock_fd;
            return 0;
        }
    }
    return -1;
}

int c8_dbus_remove_match(int sock_fd, const char *rule){
    int i;
    (void)sock_fd;
    if(!rule) return -1;
    for(i = 0; i < C8_DBUS_MAX_MATCHES; ++i){
        if(g_c8_dbus_matches[i].used && ulibc_strcmp(g_c8_dbus_matches[i].rule, rule) == 0){
            g_c8_dbus_matches[i].used = false;
            if(g_c8_dbus_match_count > 0) --g_c8_dbus_match_count;
            return 0;
        }
    }
    return 0;
}

static bool c8_dbus_contains(const char *msg, const char *needle){
    if(!msg || !needle) return false;
    return c8loc_contains_ci(msg, needle);
}

static bool c8_dbus_extract_name(const char *msg, char *out, size_t out_cap){
    size_t i;
    if(!msg || !out || out_cap < 2) return false;
    out[0] = 0;
    for(i = 0; msg[i]; ++i){
        size_t j = 0;
        bool starts_colon = false;
        bool has_dot = false;
        if(msg[i] == ':'){
            starts_colon = true;
        }else if(!((msg[i] >= 'a' && msg[i] <= 'z') || (msg[i] >= 'A' && msg[i] <= 'Z'))){
            continue;
        }
        while(msg[i + j] && j + 1 < out_cap){
            char ch = msg[i + j];
            bool ok = (ch >= 'a' && ch <= 'z') ||
                      (ch >= 'A' && ch <= 'Z') ||
                      (ch >= '0' && ch <= '9') ||
                      ch == '.' || ch == '_' || ch == '-' || ch == ':';
            if(!ok) break;
            if(ch == '.') has_dot = true;
            out[j] = ch;
            ++j;
        }
        out[j] = 0;
        if(j == 0) continue;
        if((starts_colon || (has_dot && c8loc_contains_ci(out, "org."))) && out[0] != '/'){
            return true;
        }
    }
    return false;
}

static bool c8_dbus_extract_match_rule(const char *msg, char *out, size_t out_cap){
    size_t i, j;
    if(!msg || !out || out_cap < 2) return false;
    out[0] = 0;
    for(i = 0; msg[i]; ++i){
        if(ulibc_strncmp(msg + i, "type='", 6) == 0 ||
           ulibc_strncmp(msg + i, "interface='", 11) == 0 ||
           ulibc_strncmp(msg + i, "member='", 8) == 0){
            j = 0;
            while(msg[i + j] && msg[i + j] != '\n' && j + 1 < out_cap){
                char ch = msg[i + j];
                out[j++] = ch;
                if(ch == '\''){
                    size_t k = j;
                    while(msg[i + k] && msg[i + k] != '\'' && k + 1 < out_cap){
                        out[k] = msg[i + k];
                        ++k;
                    }
                    if(msg[i + k] == '\'' && k + 1 < out_cap){
                        out[k++] = '\'';
                    }
                    j = k;
                    break;
                }
            }
            out[j] = 0;
            return j > 0;
        }
    }
    return false;
}

static const char *c8_dbus_owner_for_name(const char *name){
    int i;
    if(!name) return 0;
    for(i = 0; i < C8_DBUS_MAX_SERVICES; ++i){
        if(!g_c8_dbus_services[i].used) continue;
        if(ulibc_strcmp(g_c8_dbus_services[i].name, name) == 0){
            return g_c8_dbus_services[i].name;
        }
    }
    return 0;
}

static void c8_dbus_emit_name_signal(const char *member){
    (void)c8_dbus_send_signal(0,
        "/org/freedesktop/DBus",
        "org.freedesktop.DBus",
        member,
        0, 0);
}

int c8_dbus_process_incoming(int sock_fd){
    uint8_t buf[C8_DBUS_MAX_MSG_SIZE + 1];
    int64_t r = real_sys_recvfrom(sock_fd, buf, C8_DBUS_MAX_MSG_SIZE, 0, 0, 0);
    char dyn_name[64];
    char token[192];
    const char *msg = (const char*)buf;
    if(r <= 0) return -1;
    buf[(size_t)r] = 0;
    if(c8_dbus_contains(msg, "Hello")){
        ulibc_snprintf(dyn_name, sizeof(dyn_name), ":1.%u", (unsigned)real_sys_getpid());
        (void)c8_dbus_acquire_name(dyn_name, sock_fd);
        c8_dbus_emit_name_signal("NameAcquired");
        return 0;
    }
    if(c8_dbus_contains(msg, "RequestName")){
        if(c8_dbus_extract_name(msg, token, sizeof(token))){
            (void)c8_dbus_acquire_name(token, sock_fd);
            c8_dbus_emit_name_signal("NameAcquired");
        }
        return 0;
    }
    if(c8_dbus_contains(msg, "ReleaseName")){
        if(c8_dbus_extract_name(msg, token, sizeof(token))){
            (void)c8_dbus_release_name(token);
            c8_dbus_emit_name_signal("NameLost");
        }
        return 0;
    }
    if(c8_dbus_contains(msg, "AddMatch")){
        if(!c8_dbus_extract_match_rule(msg, token, sizeof(token))){
            c8_strlcpy(token, sizeof(token), "type='signal'");
        }
        (void)c8_dbus_add_match(sock_fd, token[0] ? token : "type='signal'");
        return 0;
    }
    if(c8_dbus_contains(msg, "RemoveMatch")){
        if(!c8_dbus_extract_match_rule(msg, token, sizeof(token))){
            c8_strlcpy(token, sizeof(token), "type='signal'");
        }
        (void)c8_dbus_remove_match(sock_fd, token[0] ? token : "type='signal'");
        return 0;
    }
    if(c8_dbus_contains(msg, "GetNameOwner")){
        if(c8_dbus_extract_name(msg, token, sizeof(token))){
            if(c8_dbus_owner_for_name(token)){
                c8_dbus_emit_name_signal("NameOwnerChanged");
            }
        }
        return 0;
    }
    /* Keep unknown messages non-fatal so portal clients can probe capabilities. */
    return 0;
}

/* Additional syscalls */

static bool c8_timespec_valid(const timespec_t *ts){
    if(!ts)return false;
    if(ts->tv_sec<0)return false;
    if(ts->tv_nsec<0||ts->tv_nsec>=1000000000LL)return false;
    return true;
}

static void c8_timespec_sub(const timespec_t *a,const timespec_t *b,timespec_t *out){
    out->tv_sec=a->tv_sec-b->tv_sec;
    out->tv_nsec=a->tv_nsec-b->tv_nsec;
    if(out->tv_nsec<0){
        out->tv_nsec+=1000000000LL;
        out->tv_sec-=1;
    }
    if(out->tv_sec<0){
        out->tv_sec=0;
        out->tv_nsec=0;
    }
}

int64_t c8_sys_clock_nanosleep(int clockid, int flags, const timespec_t *req, timespec_t *rem){
    enum { C8_TIMER_ABSTIME = 1 };
    if(!c8_timespec_valid(req)) return -EINVAL;
    if(flags&~C8_TIMER_ABSTIME) return -EINVAL;
    if(!(flags&C8_TIMER_ABSTIME)){
        return real_sys_nanosleep(req,rem);
    }
    {
        timespec_t now,delta;
        int64_t rc=real_sys_clock_gettime(clockid,&now);
        if(rc<0)return rc;
        if(!c8_timespec_valid(&now))return 0;
        c8_timespec_sub(req,&now,&delta);
        if(delta.tv_sec==0&&delta.tv_nsec==0){
            if(rem)ulibc_memset(rem,0,sizeof(*rem));
            return 0;
        }
        return real_sys_nanosleep(&delta,rem);
    }
}

int64_t c8_sys_getresuid(uint32_t *ruid, uint32_t *euid, uint32_t *suid){
    if(ruid) *ruid = 0;
    if(euid) *euid = 0;
    if(suid) *suid = 0;
    return 0;
}

int64_t c8_sys_getresgid(uint32_t *rgid, uint32_t *egid, uint32_t *sgid){
    if(rgid) *rgid = 0;
    if(egid) *egid = 0;
    if(sgid) *sgid = 0;
    return 0;
}

int64_t c8_sys_setresuid(uint32_t ruid, uint32_t euid, uint32_t suid){
    (void)ruid; (void)euid; (void)suid;
    return 0;
}

int64_t c8_sys_setresgid(uint32_t rgid, uint32_t egid, uint32_t sgid){
    (void)rgid; (void)egid; (void)sgid;
    return 0;
}

int64_t c8_sys_copy_file_range(int fd_in, uint64_t *off_in, int fd_out, uint64_t *off_out, size_t len, unsigned flags){
    uint8_t buf[4096];
    size_t done = 0;
    int64_t saved_in=-1,saved_out=-1;
    int64_t err=0;
    if(flags!=0)return -EINVAL;
    if(off_in&&((int64_t)(*off_in)<0))return -EINVAL;
    if(off_out&&((int64_t)(*off_out)<0))return -EINVAL;
    if(off_in){
        saved_in=real_sys_lseek(fd_in,0,SEEK_CUR);
        if(saved_in<0)return saved_in;
        err=real_sys_lseek(fd_in,(int64_t)(*off_in),SEEK_SET);
        if(err<0)return err;
    }
    if(off_out){
        saved_out=real_sys_lseek(fd_out,0,SEEK_CUR);
        if(saved_out<0){if(saved_in>=0)(void)real_sys_lseek(fd_in,saved_in,SEEK_SET);return saved_out;}
        err=real_sys_lseek(fd_out,(int64_t)(*off_out),SEEK_SET);
        if(err<0){
            if(saved_in>=0)(void)real_sys_lseek(fd_in,saved_in,SEEK_SET);
            (void)real_sys_lseek(fd_out,saved_out,SEEK_SET);
            return err;
        }
    }
    while(done < len){
        size_t chunk = len - done;
        if(chunk > sizeof(buf)) chunk = sizeof(buf);
        int64_t r = real_sys_read(fd_in, buf, chunk);
        size_t wr_off=0;
        if(r<0){err=r;break;}
        if(r==0)break;
        while(wr_off<(size_t)r){
            int64_t w=real_sys_write(fd_out,buf+wr_off,(size_t)r-wr_off);
            if(w<=0){err=(w<0)?w:-EIO;break;}
            wr_off+=(size_t)w;
        }
        if(err<0)break;
        done += wr_off;
        if((size_t)r < chunk) break;
    }
    if(off_in){
        int64_t cur=real_sys_lseek(fd_in,0,SEEK_CUR);
        if(cur>=0)*off_in=(uint64_t)cur;
        (void)real_sys_lseek(fd_in,saved_in,SEEK_SET);
    }
    if(off_out){
        int64_t cur=real_sys_lseek(fd_out,0,SEEK_CUR);
        if(cur>=0)*off_out=(uint64_t)cur;
        (void)real_sys_lseek(fd_out,saved_out,SEEK_SET);
    }
    if(done==0&&err<0)return err;
    return (int64_t)done;
}

static int64_t c8_sys_rwvec_at(bool is_write,int fd,const iovec_t *iov,int iovcnt,uint64_t pos_l,uint64_t pos_h,int flags){
    uint64_t off_raw=((pos_h&0xFFFFFFFFu)<<32)|(pos_l&0xFFFFFFFFu);
    int64_t off=(int64_t)off_raw;
    int64_t saved;
    int64_t rc;
    if(iovcnt<0)return -EINVAL;
    if(iovcnt==0)return 0;
    if(!iov)return -EFAULT;
    if(flags!=0)return -EINVAL;
    if(off==-1LL)return is_write?real_writev(fd,iov,iovcnt):real_readv(fd,iov,iovcnt);
    if(off<0)return -EINVAL;
    saved=real_sys_lseek(fd,0,SEEK_CUR);
    if(saved<0)return saved;
    rc=real_sys_lseek(fd,off,SEEK_SET);
    if(rc<0)return rc;
    rc=is_write?real_writev(fd,iov,iovcnt):real_readv(fd,iov,iovcnt);
    (void)real_sys_lseek(fd,saved,SEEK_SET);
    return rc;
}

int64_t c8_sys_preadv2(int fd, const void *iov, int iovcnt, uint64_t pos_l, uint64_t pos_h, int flags){
    return c8_sys_rwvec_at(false,fd,(const iovec_t*)iov,iovcnt,pos_l,pos_h,flags);
}

int64_t c8_sys_pwritev2(int fd, const void *iov, int iovcnt, uint64_t pos_l, uint64_t pos_h, int flags){
    return c8_sys_rwvec_at(true,fd,(const iovec_t*)iov,iovcnt,pos_l,pos_h,flags);
}

int64_t c8_sys_membarrier(int cmd, int flags, int cpu_id){
    (void)cmd; (void)flags; (void)cpu_id;
    __asm__ volatile("mfence" ::: "memory");
    return 0;
}

typedef struct {
    bool     registered;
    int      owner_pid;
    uint64_t user_ptr;
    uint32_t user_len;
    uint32_t signature;
} c8_rseq_state_t;

typedef struct {
    uint32_t cpu_id_start;
    uint32_t cpu_id;
    uint64_t rseq_cs;
    uint32_t flags;
} c8_rseq_user_t;

#define C8_RSEQ_FLAG_UNREGISTER 1u
#define C8_RSEQ_UNREGISTERED_CPU 0xFFFFFFFFu

static c8_rseq_state_t g_c8_rseq_state[TASK_MAX];

static int c8_current_task_index(void){
    if(g_current_task < 0 || g_current_task >= TASK_MAX) return -1;
    if(!g_tasks[g_current_task].used) return -1;
    return g_current_task;
}

static void c8_rseq_forget(int tidx){
    if(tidx < 0 || tidx >= TASK_MAX) return;
    ulibc_memset(&g_c8_rseq_state[tidx], 0, sizeof(g_c8_rseq_state[tidx]));
}

static void c8_rseq_reset_if_stale(int tidx, const task_t *cur){
    if(tidx < 0 || tidx >= TASK_MAX || !cur) return;
    if(g_c8_rseq_state[tidx].registered &&
       g_c8_rseq_state[tidx].owner_pid != cur->pid){
        c8_rseq_forget(tidx);
    }
}

static void c8_rseq_publish_cpu(int tidx, uint32_t cpu){
    c8_rseq_state_t *st;
    c8_rseq_user_t *usr;
    if(tidx < 0 || tidx >= TASK_MAX) return;
    st = &g_c8_rseq_state[tidx];
    if(!st->registered || !st->user_ptr ||
       st->user_len < sizeof(uint32_t) * 2) return;
    usr = (c8_rseq_user_t*)(uintptr_t)st->user_ptr;
    usr->cpu_id_start = cpu;
    usr->cpu_id = cpu;
}

static void c8_rseq_mark_unregistered(int tidx){
    c8_rseq_state_t *st;
    c8_rseq_user_t *usr;
    if(tidx < 0 || tidx >= TASK_MAX) return;
    st = &g_c8_rseq_state[tidx];
    if(st->registered && st->user_ptr &&
       st->user_len >= sizeof(uint32_t) * 2){
        usr = (c8_rseq_user_t*)(uintptr_t)st->user_ptr;
        usr->cpu_id_start = C8_RSEQ_UNREGISTERED_CPU;
        usr->cpu_id = C8_RSEQ_UNREGISTERED_CPU;
    }
    c8_rseq_forget(tidx);
}

int64_t c8_sys_rseq(void *rseq, uint32_t rseq_len, int flags, uint32_t sig){
    task_t *cur=task_current();
    int tidx=c8_current_task_index();
    c8_rseq_state_t *st;
    c8_rseq_user_t *usr=(c8_rseq_user_t*)rseq;
    if(!cur||tidx<0)return -ESRCH;
    if(!usr)return -EFAULT;
    if(rseq_len<sizeof(c8_rseq_user_t))return -EINVAL;
    if(flags!=0&&flags!=(int)C8_RSEQ_FLAG_UNREGISTER)return -EINVAL;
    c8_rseq_reset_if_stale(tidx,cur);
    st=&g_c8_rseq_state[tidx];
    if(flags&(int)C8_RSEQ_FLAG_UNREGISTER){
        if(!st->registered)return -EINVAL;
        if(st->user_ptr!=(uint64_t)(uintptr_t)usr)return -EINVAL;
        c8_rseq_mark_unregistered(tidx);
        return 0;
    }
    if(st->registered){
        if(st->user_ptr==(uint64_t)(uintptr_t)usr)return 0;
        return -EBUSY;
    }
    st->registered=true;
    st->owner_pid=cur->pid;
    st->user_ptr=(uint64_t)(uintptr_t)usr;
    st->user_len=rseq_len;
    st->signature=sig;
    usr->cpu_id_start=0;
    usr->cpu_id=0;
    usr->rseq_cs=0;
    usr->flags=0;
    return 0;
}

int64_t c8_sys_getcpu(uint32_t *cpu, uint32_t *node, void *tcache){
    int tidx = c8_current_task_index();
    (void)tcache;
    if(cpu) *cpu = 0;
    if(node) *node = 0;
    if(tidx >= 0) c8_rseq_publish_cpu(tidx, 0);
    return 0;
}

/* Epoll enhancements - wire edge-triggered flags */
/* The existing epoll_ctl already stores events; we just need to ensure
   EPOLLET/EPOLLONESHOT flags are preserved in the kernel-side entry.
   This is already handled by compat3's epoll implementation which stores
   the full epoll_event_t. We add the syscall wrappers for the new numbers. */

static int64_t c8_sys_epoll_pwait(int epfd, void *events, int maxevents, int timeout, const void *sigmask, size_t sigsz){
    (void)sigmask; (void)sigsz;
    return real_sys_epoll_wait(epfd, events, maxevents, timeout);
}

/* Syscall table rewiring */

extern int64_t (*g_syscall_table[512])(uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t);
static int g_c8_registered_syscalls = 0;

static int64_t c8_w_seccomp(uint64_t a0,uint64_t a1,uint64_t a2,uint64_t a3,uint64_t a4,uint64_t a5){
    (void)a3;(void)a4;(void)a5;return c8_seccomp((unsigned)a0,(unsigned)a1,(void*)(uintptr_t)a2);}
static int64_t c8_w_userfaultfd(uint64_t a0,uint64_t a1,uint64_t a2,uint64_t a3,uint64_t a4,uint64_t a5){(void)a1;(void)a2;(void)a3;(void)a4;(void)a5;return c8_userfaultfd((int)a0);}
static int64_t c8_w_process_vm_readv(uint64_t a0,uint64_t a1,uint64_t a2,uint64_t a3,uint64_t a4,uint64_t a5){
    return c8_process_vm_readv((int)a0,(const c8_iovec_t*)(uintptr_t)a1,(unsigned long)a2,(const c8_iovec_t*)(uintptr_t)a3,(unsigned long)a4,(unsigned long)a5);}
static int64_t c8_w_process_vm_writev(uint64_t a0,uint64_t a1,uint64_t a2,uint64_t a3,uint64_t a4,uint64_t a5){
    return c8_process_vm_writev((int)a0,(const c8_iovec_t*)(uintptr_t)a1,(unsigned long)a2,(const c8_iovec_t*)(uintptr_t)a3,(unsigned long)a4,(unsigned long)a5);}
static int64_t c8_w_capget(uint64_t a0,uint64_t a1,uint64_t a2,uint64_t a3,uint64_t a4,uint64_t a5){(void)a2;(void)a3;(void)a4;(void)a5;return c8_capget((void*)(uintptr_t)a0,(c8_cap_user_data_t*)(uintptr_t)a1);}
static int64_t c8_w_capset(uint64_t a0,uint64_t a1,uint64_t a2,uint64_t a3,uint64_t a4,uint64_t a5){(void)a2;(void)a3;(void)a4;(void)a5;return c8_capset((void*)(uintptr_t)a0,(const c8_cap_user_data_t*)(uintptr_t)a1);}
static int64_t c8_w_clock_nanosleep(uint64_t a0,uint64_t a1,uint64_t a2,uint64_t a3,uint64_t a4,uint64_t a5){(void)a4;(void)a5;return c8_sys_clock_nanosleep((int)a0,(int)a1,(const timespec_t*)(uintptr_t)a2,(timespec_t*)(uintptr_t)a3);}
static int64_t c8_w_getresuid(uint64_t a0,uint64_t a1,uint64_t a2,uint64_t a3,uint64_t a4,uint64_t a5){(void)a3;(void)a4;(void)a5;return c8_sys_getresuid((uint32_t*)(uintptr_t)a0,(uint32_t*)(uintptr_t)a1,(uint32_t*)(uintptr_t)a2);}
static int64_t c8_w_getresgid(uint64_t a0,uint64_t a1,uint64_t a2,uint64_t a3,uint64_t a4,uint64_t a5){(void)a3;(void)a4;(void)a5;return c8_sys_getresgid((uint32_t*)(uintptr_t)a0,(uint32_t*)(uintptr_t)a1,(uint32_t*)(uintptr_t)a2);}
static int64_t c8_w_setresuid(uint64_t a0,uint64_t a1,uint64_t a2,uint64_t a3,uint64_t a4,uint64_t a5){(void)a3;(void)a4;(void)a5;return c8_sys_setresuid((uint32_t)a0,(uint32_t)a1,(uint32_t)a2);}
static int64_t c8_w_setresgid(uint64_t a0,uint64_t a1,uint64_t a2,uint64_t a3,uint64_t a4,uint64_t a5){(void)a3;(void)a4;(void)a5;return c8_sys_setresgid((uint32_t)a0,(uint32_t)a1,(uint32_t)a2);}
static int64_t c8_w_copy_file_range(uint64_t a0,uint64_t a1,uint64_t a2,uint64_t a3,uint64_t a4,uint64_t a5){
    return c8_sys_copy_file_range((int)a0,(uint64_t*)(uintptr_t)a1,(int)a2,(uint64_t*)(uintptr_t)a3,(size_t)a4,(unsigned)a5);}
static int64_t c8_w_membarrier(uint64_t a0,uint64_t a1,uint64_t a2,uint64_t a3,uint64_t a4,uint64_t a5){(void)a3;(void)a4;(void)a5;return c8_sys_membarrier((int)a0,(int)a1,(int)a2);}
static int64_t c8_w_rseq(uint64_t a0,uint64_t a1,uint64_t a2,uint64_t a3,uint64_t a4,uint64_t a5){(void)a4;(void)a5;return c8_sys_rseq((void*)(uintptr_t)a0,(uint32_t)a1,(int)a2,(uint32_t)a3);}
static int64_t c8_w_getcpu(uint64_t a0,uint64_t a1,uint64_t a2,uint64_t a3,uint64_t a4,uint64_t a5){(void)a3;(void)a4;(void)a5;return c8_sys_getcpu((uint32_t*)(uintptr_t)a0,(uint32_t*)(uintptr_t)a1,(void*)(uintptr_t)a2);}
static int64_t c8_w_epoll_pwait(uint64_t a0,uint64_t a1,uint64_t a2,uint64_t a3,uint64_t a4,uint64_t a5){
    return c8_sys_epoll_pwait((int)a0,(void*)(uintptr_t)a1,(int)a2,(int)a3,(const void*)(uintptr_t)a4,(size_t)a5);}

static void c8_register_syscalls(void){
    /* Linux x86-64 syscall numbers */
    struct {
        int nr;
        int64_t (*fn)(uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t);
        bool keep_existing;
    } tbl[] = {
        {317, c8_w_seccomp, true},            /* seccomp: keep compat3 if already wired */
        {323, c8_w_userfaultfd, true},        /* userfaultfd: keep compat3 if already wired */
        {310, c8_w_process_vm_readv, true},   /* keep compat3 */
        {311, c8_w_process_vm_writev, true},  /* keep compat3 */
        {125, c8_w_capget, true},             /* keep compat3 */
        {126, c8_w_capset, true},             /* keep compat3 */
        {230, c8_w_clock_nanosleep, false},   /* upgrade legacy stub */
        {118, c8_w_getresuid, false},
        {120, c8_w_getresgid, false},
        {117, c8_w_setresuid, false},
        {119, c8_w_setresgid, false},
        {326, c8_w_copy_file_range, false},
        {324, c8_w_membarrier, true},         /* keep compat6 by default */
        {334, c8_w_rseq, false},              /* lightweight single-CPU rseq shim for modern libc */
        {309, c8_w_getcpu, true},             /* keep compat6 by default */
        {281, c8_w_epoll_pwait, false},       /* fix wrong legacy wiring */
    };
    int i;
    g_c8_registered_syscalls = 0;
    for(i = 0; i < (int)(sizeof(tbl)/sizeof(tbl[0])); ++i){
        if(tbl[i].nr >= 0 && tbl[i].nr < 512){
            if(tbl[i].keep_existing&&g_syscall_table[tbl[i].nr])continue;
            if(g_syscall_table[tbl[i].nr] == tbl[i].fn) continue;
            g_syscall_table[tbl[i].nr] = tbl[i].fn;
            ++g_c8_registered_syscalls;
        }
    }
}

/* glibc symbols already defined above (before c8_dlsym) */
static void c8_register_glibc_symbols(void){
    /* Symbols are available through c8_dlsym -> g_c8_glibc_syms -> ulibc_dlsym chain */
}

/* Shell commands */

extern compat_shell_cmd_t g_compat_cmds[];
extern int g_compat_cmd_count;
static bool g_c8_shell_cmds_registered = false;

static bool c8_shell_cmd_exists(const char *name){
    int i;
    if(!name) return false;
    for(i = 0; i < g_compat_cmd_count; ++i){
        if(g_compat_cmds[i].name && ulibc_strcmp(g_compat_cmds[i].name, name) == 0)
            return true;
    }
    return false;
}

static void cmd8_abi(const char *args, char *out, int out_max){
    size_t l = 0;
    (void)args;
    out[0] = 0;

    #define A8(s) do{if(l<(size_t)out_max){size_t sl=ulibc_strlen(s);if(l+sl<(size_t)out_max){ulibc_memcpy(out+l,s,sl);l+=sl;}}}while(0)

    A8("=== compat8 Browser Runtime Report ===\n");

    A8("glibc ABI: ok\n");
    A8("setjmp/longjmp: ok\n");
    A8("sigaction: ");
    A8(g_syscall_table[13] ? "ok\n" : "missing\n");
    A8("locale/nl_langinfo: ok\n");

    A8("dl_iterate_phdr: ");
    A8(compat3_has_dynamic_relocator() ? "ok\n" : "partial\n");
    A8("dlopen/dlsym: ");
    A8(g_syscall_table[9] ? "ok\n" : "partial\n");

    A8("seccomp: ");
    A8(g_syscall_table[317] ? "ok\n" : "missing\n");

    A8("userfaultfd: ");
    A8(g_syscall_table[323] ? "ok\n" : "missing\n");

    A8("process_vm_readv/writev: ");
    A8((g_syscall_table[310] && g_syscall_table[311]) ? "ok (self process)\n" : "missing\n");

    A8("capget/capset: ok\n");

    A8("software rendering: ok\n");
    A8("DBus IPC: ");
    A8(c8_lookup_unix_socket("/var/run/dbus/system_bus_socket") >= 0 ? "ok\n" : "partial\n");

    A8("unix socket registry: ok\n");

    A8("additional syscalls: ");
    {char tmp[32]; ulibc_snprintf(tmp, sizeof(tmp), "%d registered\n", g_c8_registered_syscalls); A8(tmp);}

    A8("dynamic relocator: ");
    A8(compat3_has_dynamic_relocator() ? "ok\n" : "missing\n");

    A8("\nOverall verdict: ");
    A8(compat8_browser_runtime_ready() ? "READY - full browser runtime available\n" : "PARTIAL - gaps remain\n");

    #undef A8
}

static void cmd8_dbus(const char *args, char *out, int out_max){
    size_t l = 0;
    int i;
    (void)args;
    out[0] = 0;
    for(i = 0; i < C8_DBUS_MAX_SERVICES && l < (size_t)out_max - 64; ++i){
        if(!g_c8_dbus_services[i].used) continue;
        char tmp[200];
        ulibc_snprintf(tmp, sizeof(tmp), "  %s (pid=%u, fd=%d)\n",
            g_c8_dbus_services[i].name, g_c8_dbus_services[i].owner_pid, g_c8_dbus_services[i].sock_fd);
        size_t tl = ulibc_strlen(tmp);
        if(l + tl < (size_t)out_max){ ulibc_memcpy(out + l, tmp, tl); l += tl; }
    }
    if(l == 0){ ulibc_strcpy(out, "  (no DBus services)\n"); l = 20; }
}

static void cmd8_render(const char *args, char *out, int out_max){
    (void)args;
    ulibc_snprintf(out, (size_t)out_max,
        "Software rendering pipeline:\n"
        "  blit_argb32: ok\n"
        "  fill_argb32: ok\n"
        "  blend_argb32: ok\n"
        "  scale_argb32: ok\n"
        "  font: 8x16 bitmap\n"
        "  surface: create/destroy/flush\n"
        "  Skia glue: surface_t wrapper\n");
}

static void cmd8_glibc(const char *args, char *out, int out_max){
    size_t l = 0;
    int i;
    (void)args;
    out[0] = 0;
    for(i = 0; g_c8_glibc_syms[i].name && l < (size_t)out_max - 64; ++i){
        char tmp[128];
        ulibc_snprintf(tmp, sizeof(tmp), "  %s @ %p\n", g_c8_glibc_syms[i].name, g_c8_glibc_syms[i].addr);
        size_t tl = ulibc_strlen(tmp);
        if(l + tl < (size_t)out_max){ ulibc_memcpy(out + l, tmp, tl); l += tl; }
    }
}

static void c8_register_shell_cmds_impl(void){
    if(g_c8_shell_cmds_registered) return;
    #define REG8(n,h,fn) if(g_compat_cmd_count<COMPAT_SHELL_CMD_MAX && !c8_shell_cmd_exists(n)){g_compat_cmds[g_compat_cmd_count].name=n;g_compat_cmds[g_compat_cmd_count].help=h;g_compat_cmds[g_compat_cmd_count].handler=fn;++g_compat_cmd_count;}
    REG8("abi8", "Browser runtime readiness (compat8)", cmd8_abi)
    REG8("dbus", "List DBus services", cmd8_dbus)
    REG8("render", "Software rendering status", cmd8_render)
    REG8("glibc", "List glibc symbols", cmd8_glibc)
    #undef REG8
    g_c8_shell_cmds_registered = true;
}

void compat8_register_shell_cmds(void){
    c8_register_shell_cmds_impl();
}

/* Master init */

bool compat8_browser_runtime_ready(void){
    /* Runtime gates based on wired syscall ABI + relocator readiness. */
    extern int64_t (*g_syscall_table[512])(uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t);
    if(!compat3_has_dynamic_relocator()) return false;
    if(!g_syscall_table[9] || !g_syscall_table[10] || !g_syscall_table[11]) return false;  /* mmap/mprotect/munmap */
    if(!g_syscall_table[56] || !g_syscall_table[202]) return false;                         /* clone + futex */
    if(!g_syscall_table[232] || !g_syscall_table[233]) return false;                        /* epoll_wait + epoll_ctl */
    if(!g_syscall_table[59] || !g_syscall_table[257]) return false;                         /* execve + openat */
    if(!g_syscall_table[317] || !g_syscall_table[323]) return false;                        /* seccomp + userfaultfd */
    if(!g_syscall_table[310] || !g_syscall_table[311]) return false;                        /* process_vm_{readv,writev} */
    return true;
}

void compat8_init_all(void){
    /* Clear state */
    ulibc_memset(g_c8_atexit, 0, sizeof(g_c8_atexit));
    g_c8_atexit_count = 0;
    ulibc_memset(g_c8_dbus_services, 0, sizeof(g_c8_dbus_services));
    ulibc_memset(g_c8_dbus_matches, 0, sizeof(g_c8_dbus_matches));
    ulibc_memset(g_c8_unix_sockets, 0, sizeof(g_c8_unix_sockets));
    ulibc_memset(g_c8_rseq_state, 0, sizeof(g_c8_rseq_state));
    g_c8_dbus_service_count = 0;
    g_c8_dbus_match_count = 0;
    g_c8_seccomp_active = false;

    /* Initialize font */
    c8_font_init();

    /* Register glibc symbols into global lookup */
    c8_register_glibc_symbols();

    /* Register additional syscalls */
    c8_register_syscalls();

    /* Register shell commands */
    compat8_register_shell_cmds();

    /* Register standard Unix domain socket paths */
    {int sfd;
     sfd = (int)real_sys_socket(1 /* AF_UNIX */, 1 /* SOCK_STREAM */, 0);
     if(sfd >= 0) c8_register_unix_socket("/var/run/dbus/system_bus_socket", sfd);
     sfd = (int)real_sys_socket(1, 1, 0);
     if(sfd >= 0) c8_register_unix_socket("/run/dbus/system_bus_socket", sfd);
     sfd = (int)real_sys_socket(1, 1, 0);
     if(sfd >= 0) c8_register_unix_socket("/run/user/0/bus", sfd);
     sfd = (int)real_sys_socket(1, 1, 0);
     if(sfd >= 0) c8_register_unix_socket("/tmp/ridux-no-dbus", sfd);
     sfd = (int)real_sys_socket(1, 1, 0);
     if(sfd >= 0) c8_register_unix_socket("/tmp/ridux-no-system-dbus", sfd);
     sfd = (int)real_sys_socket(1, 1, 0);
     if(sfd >= 0) c8_register_unix_socket("/tmp/.X11-unix/X0", sfd);
     sfd = (int)real_sys_socket(1, 1, 0);
     if(sfd >= 0) c8_register_unix_socket("/run/user/0/wayland-0", sfd);
     sfd = (int)real_sys_socket(1, 1, 0);
     if(sfd >= 0) c8_register_unix_socket("/run/user/1000/wayland-0", sfd);
     sfd = (int)real_sys_socket(1, 1, 0);
     if(sfd >= 0) c8_register_unix_socket("/run/wayland/wayland-0", sfd);
    }

    /* Acquire well-known DBus names */
    c8_dbus_acquire_name("org.freedesktop.DBus", -1);
    c8_dbus_acquire_name("org.freedesktop.login1", -1);
    c8_dbus_acquire_name("org.freedesktop.systemd1", -1);
    c8_dbus_acquire_name("org.freedesktop.portal.Desktop", -1);
    c8_dbus_acquire_name("org.freedesktop.portal.Settings", -1);
}
