/*
 * Syscalls estilo Linux y loader ELF64.
 *
 * Es grande porque aca viven mmap, exec, VFS bridge, dynlink y bastante glue
 * para procesos reales. La idea es seguir cortandolo cuando sea seguro.
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "base.h"
#include "memory_tasks.h"
#include "linux_syscalls.h"

/* Forward declarations for compat2 functions not in header */
extern int sig_send(int pid, int sig);
extern void sig_check_pending(task_t *t);
extern void *ulibc_dlsym(void *handle, const char *symbol);
extern void compat4_tls_reset_task(int tidx);
extern int drm_ioctl_handler(int fd,uint64_t request,void *arg);
extern int fb_ioctl_handler(int fd,uint64_t request,void *arg);

static const char *c3_vfs_open_slot_path(int ref);
static int c3_vfs_open_slot_alloc(const char *path);
extern bool kvfs_read(const char *path, const uint8_t **data, uint32_t *size);

/* local helpers (no libc) */
#define C3_MSG_SCRATCH_SIZE 16384u

static void *c3_memset(void *d,int v,size_t n){uint8_t *p=(uint8_t*)d;size_t i;for(i=0;i<n;++i)p[i]=(uint8_t)v;return d;}
static void *c3_memcpy(void *d,const void *s,size_t n){uint8_t *dd=(uint8_t*)d;const uint8_t *ss=(const uint8_t*)s;size_t i;for(i=0;i<n;++i)dd[i]=ss[i];return d;}
static size_t c3_strlen(const char *s){size_t n=0;while(s[n])++n;return n;}
static void c3_strlcpy(char *d,const char *s,size_t c){size_t i=0;if(!c)return;while(i+1<c&&s[i]){d[i]=s[i];++i;}d[i]=0;}
static int c3_strcmp(const char *a,const char *b){while(*a&&*b&&*a==*b){++a;++b;}return(int)((unsigned char)*a-(unsigned char)*b);}
static int c3_strncmp(const char *a,const char *b,size_t n){size_t i;for(i=0;i<n&&a[i]&&b[i];++i)if(a[i]!=b[i])return(int)((unsigned char)a[i]-(unsigned char)b[i]);return(i<n)?(int)((unsigned char)a[i]-(unsigned char)b[i]):0;}
static bool c3_starts_with(const char *s,const char *p){while(*p){if(*s!=*p)return false;++s;++p;}return true;}
static bool c3_mem_has_token(const char *s,size_t n,const char *tok){
    size_t m,i,j;
    if(!s||!tok)return false;
    m=c3_strlen(tok);
    if(!m||n<m)return false;
    for(i=0;i+m<=n;++i){
        for(j=0;j<m;++j){
            if(s[i+j]!=tok[j])break;
        }
        if(j==m)return true;
    }
    return false;
}
static bool c3_has_token(const char *s,const char *tok){
    size_t i,j;
    if(!s||!tok||!*tok)return false;
    for(i=0;s[i];++i){
        for(j=0;tok[j];++j){
            if(!s[i+j]||s[i+j]!=tok[j])break;
        }
        if(!tok[j])return true;
    }
    return false;
}
static void c3_append_str(char *d,size_t *l,size_t c,const char *s){while(*s&&*l+1<c){d[(*l)++]=*s++;}d[*l]=0;}
static void c3_append_ch(char *d,size_t *l,size_t c,char ch){if(*l+1<c){d[(*l)++]=ch;d[*l]=0;}}
static void c3_append_u32(char *d,size_t *l,size_t c,uint32_t v){char t[12];int i=0;if(!v){c3_append_ch(d,l,c,'0');return;}while(v){t[i++]='0'+(char)(v%10);v/=10;}while(--i>=0)c3_append_ch(d,l,c,t[i]);}
static void c3_append_u64(char *d,size_t *l,size_t c,uint64_t v){char t[24];int i=0;if(!v){c3_append_ch(d,l,c,'0');return;}while(v){t[i++]='0'+(char)(v%10);v/=10;}while(--i>=0)c3_append_ch(d,l,c,t[i]);}
static void c3_append_i64(char *d,size_t *l,size_t c,int64_t v){uint64_t u;if(v<0){c3_append_ch(d,l,c,'-');u=(uint64_t)(-(v+1))+1ULL;}else u=(uint64_t)v;c3_append_u64(d,l,c,u);}
static void c3_append_hex64(char *d,size_t *l,size_t c,uint64_t v){const char *h="0123456789abcdef";int i;for(i=60;i>=0;i-=4)c3_append_ch(d,l,c,h[(v>>i)&0xF]);}
static void c3_trace_task_name(const task_t *t){
    __boot_serial_puts(" name=");
    if(t&&t->name[0])__boot_serial_puts(t->name);
    else __boot_serial_puts("-");
}
static void c3_force_task_name(const task_t *t){
    __boot_serial_force_puts(" name=");
    if(t&&t->name[0])__boot_serial_force_puts(t->name);
    else __boot_serial_force_puts("-");
}
static void c3_force_rc(int64_t rc){
    if(rc<0){
        __boot_serial_force_puts("-");
        __boot_serial_force_putu32((uint32_t)(-rc));
    }else{
        __boot_serial_force_puthex64((uint64_t)rc);
    }
}
static bool c3_map_trace_wanted(const char *stage,const char *obj,uint64_t a,uint64_t b){
    (void)stage;
    if(obj&&*obj&&
       (c3_has_token(obj,"firefox")||
        c3_has_token(obj,"chrome")||
        c3_has_token(obj,"chromium")||
        c3_has_token(obj,"ld-linux")))return true;
    /* Cuando todavia no sabemos el nombre, solo traceamos imagenes enormes.
     * Para segmentos ya mapeados `a` puede ser una direccion alta, no tamano. */
    if(stage&&c3_has_token(stage,"enter"))return a>(8ULL*1024ULL*1024ULL);
    return false;
}
static void c3_force_map_stage(const char *stage,const char *obj,uint64_t a,uint64_t b){
    if(!c3_map_trace_wanted(stage,obj,a,b))return;
    __boot_serial_force_puts("[elf64_map!] ");
    __boot_serial_force_puts(stage?stage:"?");
    if(obj&&*obj){
        __boot_serial_force_puts(" obj=");
        __boot_serial_force_puts(obj);
    }
    __boot_serial_force_puts(" a=");
    __boot_serial_force_puthex64(a);
    __boot_serial_force_puts(" b=");
    __boot_serial_force_puthex64(b);
    __boot_serial_force_puts("\n");
}
static int c3_fd_group_id(const task_t *t){
    if(!t)return 0;
    return t->fdt_group?t->fdt_group:t->pid;
}
static bool c3_fd_same_group(const task_t *a,const task_t *b){
    int atg,btg;
    if(!a||!b)return false;
    if(c3_fd_group_id(a)==c3_fd_group_id(b))return true;
    atg=a->tgid?a->tgid:a->pid;
    btg=b->tgid?b->tgid:b->pid;
    return atg>0&&atg==btg;
}

static bool c3_fd_group_sync_fd(task_t *owner,int fd){
    int i;
    if(!owner||fd<0||fd>=TASK_FD_MAX)return false;
    if(fd_valid(&owner->fdt,fd))return true;
    for(i=0;i<TASK_MAX;++i){
        task_t *t=&g_tasks[i];
        if(!t->used||t==owner)continue;
        if(t->state==TASK_ZOMBIE||t->state==TASK_FREE)continue;
        if(!c3_fd_same_group(t,owner))continue;
        if(!fd_valid(&t->fdt,fd))continue;
        c3_memcpy(&owner->fdt.fds[fd],&t->fdt.fds[fd],sizeof(real_fd_t));
        {
            static uint32_t sync_trace=0;
            if(sync_trace<96){
                ++sync_trace;
                __boot_serial_puts("[fd-group-sync] owner=");
                __boot_serial_putu32((uint32_t)owner->pid);
                c3_trace_task_name(owner);
                __boot_serial_puts(" from=");
                __boot_serial_putu32((uint32_t)t->pid);
                c3_trace_task_name(t);
                __boot_serial_puts(" fd=");
                __boot_serial_putu32((uint32_t)fd);
                __boot_serial_puts(" kind=");
                __boot_serial_putu32((uint32_t)owner->fdt.fds[fd].kind);
                __boot_serial_puts(" ref=");
                __boot_serial_putu32((uint32_t)owner->fdt.fds[fd].ref);
                __boot_serial_puts(" group=");
                __boot_serial_putu32((uint32_t)c3_fd_group_id(owner));
                __boot_serial_puts("\n");
            }
        }
        return true;
    }
    return false;
}

static bool c3_fd_group_slot_used(task_t *owner,int fd){
    int i;
    if(!owner||fd<0||fd>=TASK_FD_MAX)return false;
    if(fd_valid(&owner->fdt,fd))return true;
    for(i=0;i<TASK_MAX;++i){
        task_t *t=&g_tasks[i];
        if(!t->used||t==owner)continue;
        if(t->state==TASK_ZOMBIE||t->state==TASK_FREE)continue;
        if(!c3_fd_same_group(t,owner))continue;
        if(fd_valid(&t->fdt,fd))return true;
    }
    return false;
}

static void c3_trace_clone_fail(const task_t *cur,int rc){
    int i;
    uint32_t used=0,live=0,zombie=0,same_as=0;
    for(i=0;i<TASK_MAX;++i){
        const task_t *t=&g_tasks[i];
        if(!t->used)continue;
        ++used;
        if(t->state==TASK_ZOMBIE)++zombie;
        else if(t->state!=TASK_FREE)++live;
        if(cur&&t->addr_space==cur->addr_space)++same_as;
    }
    __boot_serial_puts("[clone-fail] rc=");
    if(rc<0){
        __boot_serial_puts("-");
        __boot_serial_putu32((uint32_t)(-rc));
    }else{
        __boot_serial_putu32((uint32_t)rc);
    }
    __boot_serial_puts(" used=");
    __boot_serial_putu32(used);
    __boot_serial_puts("/");
    __boot_serial_putu32((uint32_t)TASK_MAX);
    __boot_serial_puts(" live=");
    __boot_serial_putu32(live);
    __boot_serial_puts(" zombie=");
    __boot_serial_putu32(zombie);
    __boot_serial_puts(" same_as=");
    __boot_serial_putu32(same_as);
    __boot_serial_puts("\n");
}

static void c3_fd_group_copy_fd(task_t *owner,int fd){
    int i;
    if(!owner||fd<0||fd>=TASK_FD_MAX)return;
    for(i=0;i<TASK_MAX;++i){
        task_t *t=&g_tasks[i];
        if(!t->used||t==owner)continue;
        if(t->state==TASK_ZOMBIE||t->state==TASK_FREE)continue;
        if(!c3_fd_same_group(t,owner))continue;
        c3_memcpy(&t->fdt.fds[fd],&owner->fdt.fds[fd],sizeof(real_fd_t));
    }
}
static void c3_fd_group_clear_fd(task_t *owner,int fd){
    int i;
    if(!owner||fd<0||fd>=TASK_FD_MAX)return;
    for(i=0;i<TASK_MAX;++i){
        task_t *t=&g_tasks[i];
        if(!t->used)continue;
        if(!c3_fd_same_group(t,owner))continue;
        fd_close(&t->fdt,fd);
    }
}
static int c3_fd_alloc_for_task(task_t *owner,uint8_t kind,int ref,uint16_t flags){
    int fd;
    if(!owner)return -ESRCH;
    for(fd=0;fd<TASK_FD_MAX;++fd){
        if(c3_fd_group_slot_used(owner,fd))continue;
        owner->fdt.fds[fd].kind=kind;
        owner->fdt.fds[fd].ref=ref;
        owner->fdt.fds[fd].flags=flags;
        owner->fdt.fds[fd].offset=0;
        owner->fdt.fds[fd].refcount=1;
        c3_fd_group_copy_fd(owner,fd);
        return fd;
    }
    return -EMFILE;
}
static uint64_t c3_path_inode(const char *path){
    /* Stable pseudo-inode from normalized path bytes.
     * Avoid pointer-based inode ids: ld-linux relies on st_dev+st_ino
     * to detect duplicate DSOs, and pointer reuse can alias distinct libs. */
    uint64_t h=1469598103934665603ULL;
    if(!path||!*path)return 1;
    while(*path){
        h^=(uint8_t)(*path++);
        h*=1099511628211ULL;
    }
    h&=0x7FFFFFFFFFFFFFFFULL;
    if(h==0)h=1;
    return h;
}
static const char *c3_task_exec_path(const task_t *t){
    int i;
    if(t&&t->exec_path[0])return t->exec_path;
    if(t&&t->tgid>0&&t->tgid!=t->pid){
        for(i=0;i<TASK_MAX;++i){
            if(g_tasks[i].used&&g_tasks[i].pid==t->tgid&&g_tasks[i].exec_path[0])return g_tasks[i].exec_path;
        }
    }
    return 0;
}
static bool c3_task_is_browser_runtime(const task_t *t){
    const char *ep=c3_task_exec_path(t);
    if(ep&&(c3_has_token(ep,"/opt/chromium/")||
            c3_has_token(ep,"/usr/lib/chromium")||
            c3_has_token(ep,"/chromium")||
            c3_has_token(ep,"/chrome")||
            c3_has_token(ep,"/opt/firefox/")||
            c3_has_token(ep,"/firefox")))return true;
    if(t&&t->name[0]&&(c3_has_token(t->name,"Chrome")||
                       c3_has_token(t->name,"chrome")||
                       c3_has_token(t->name,"ThreadPool")||
                       c3_has_token(t->name,"VizCompositor")||
                       c3_has_token(t->name,"CrShutdown")||
                       c3_has_token(t->name,"pango")||
                       c3_has_token(t->name,"gmain")))return true;
    return false;
}
static bool c3_task_is_chromium_runtime(const task_t *t){
    const char *ep=c3_task_exec_path(t);
    if(ep&&(c3_has_token(ep,"/opt/chromium/")||
            c3_has_token(ep,"/opt/google/chrome/")||
            c3_has_token(ep,"/usr/lib/chromium")||
            c3_has_token(ep,"/chromium")||
            c3_has_token(ep,"/chrome")))return true;
    if(t&&t->name[0]&&(c3_has_token(t->name,"Chrome")||
                       c3_has_token(t->name,"chrome")||
                       c3_has_token(t->name,"chromium")||
                       c3_has_token(t->name,"ThreadPool")||
                       c3_has_token(t->name,"VizCompositor")||
                       c3_has_token(t->name,"CrShutdown")))return true;
    return false;
}
static bool c3_task_is_firefox_runtime(const task_t *t){
    const char *ep=c3_task_exec_path(t);
    if(ep&&(c3_has_token(ep,"/opt/firefox/")||
            c3_has_token(ep,"/firefox")))return true;
    if(t&&t->name[0]&&(c3_has_token(t->name,"firefox")||
                       c3_has_token(t->name,"firefox-bin")))return true;
    return false;
}
static bool c3_task_is_firefox_ipc_trace(const task_t *t){
    if(!c3_task_is_firefox_runtime(t))return false;
    if(!t||!t->name[0])return true;
    if(c3_has_token(t->name,"IPC"))return true;
    if(c3_has_token(t->name,"Web Content"))return true;
    if(c3_has_token(t->name,"MainThread"))return true;
    if(c3_has_token(t->name,"Async"))return true;
    if(c3_has_token(t->name,"firefox"))return true;
    if(c3_has_token(t->name,"firefox-bin"))return true;
    return false;
}
static void c3_trace_one_fd(const char *tag,const task_t *t,int fd){
    const real_fd_t *f;
    if(!tag||!t||fd<0||fd>=TASK_FD_MAX)return;
    if(t->fdt.fds[fd].kind==FDKIND_NONE)return;
    f=&t->fdt.fds[fd];
    __boot_serial_puts(tag);
    __boot_serial_puts(" pid=");
    __boot_serial_putu32((uint32_t)t->pid);
    c3_trace_task_name(t);
    __boot_serial_puts(" fd=");
    __boot_serial_putu32((uint32_t)fd);
    __boot_serial_puts(" kind=");
    __boot_serial_putu32((uint32_t)f->kind);
    __boot_serial_puts(" ref=");
    __boot_serial_putu32((uint32_t)f->ref);
    __boot_serial_puts(" flags=");
    __boot_serial_puthex64((uint64_t)f->flags);
    __boot_serial_puts(" off=");
    __boot_serial_puthex64(f->offset);
    if(f->kind==FDKIND_VFSFILE||f->kind==FDKIND_DIR){
        const char *p=c3_vfs_open_slot_path(f->ref);
        if(p){
            __boot_serial_puts(" path=");
            __boot_serial_puts(p);
        }
    }else if((f->kind==FDKIND_PIPE_R||f->kind==FDKIND_PIPE_W)&&
             f->ref>=0&&f->ref<PIPE_MAX&&g_pipes[f->ref].used){
        pipe_t *p=&g_pipes[f->ref];
        uint32_t av=(uint32_t)((p->head-p->tail+PIPE_BUF_SIZE)%PIPE_BUF_SIZE);
        __boot_serial_puts(" pipe_av=");
        __boot_serial_putu32(av);
        __boot_serial_puts(" readers=");
        __boot_serial_putu32((uint32_t)p->readers);
        __boot_serial_puts(" writers=");
        __boot_serial_putu32((uint32_t)p->writers);
    }else if(f->kind==FDKIND_SOCKET&&
             f->ref>=0&&f->ref<SOCK_MAX&&g_sockets[f->ref].used){
        socket_t *s=&g_sockets[f->ref];
        uint32_t rx=s->rx_head-s->rx_tail;
        uint32_t tx=s->tx_head-s->tx_tail;
        uint32_t anc=(uint32_t)(uint8_t)(s->anc_head-s->anc_tail);
        __boot_serial_puts(" rx=");
        __boot_serial_putu32(rx);
        __boot_serial_puts(" tx=");
        __boot_serial_putu32(tx);
        __boot_serial_puts(" anc=");
        __boot_serial_putu32(anc);
        __boot_serial_puts(" peer=");
        __boot_serial_putu32((uint32_t)s->peer);
        __boot_serial_puts(" svc=");
        __boot_serial_putu32((uint32_t)s->virt_service);
        __boot_serial_puts(" st=");
        __boot_serial_putu32((uint32_t)s->tcp_state);
    }
    __boot_serial_puts("\n");
}
static void c3_force_one_fd(const char *tag,const task_t *t,int fd){
    const real_fd_t *f;
    if(!tag||!t||fd<0||fd>=TASK_FD_MAX)return;
    if(t->fdt.fds[fd].kind==FDKIND_NONE)return;
    f=&t->fdt.fds[fd];
    __boot_serial_force_puts(tag);
    __boot_serial_force_puts(" pid=");
    __boot_serial_force_putu32((uint32_t)t->pid);
    c3_force_task_name(t);
    __boot_serial_force_puts(" fd=");
    __boot_serial_force_putu32((uint32_t)fd);
    __boot_serial_force_puts(" kind=");
    __boot_serial_force_putu32((uint32_t)f->kind);
    __boot_serial_force_puts(" ref=");
    __boot_serial_force_putu32((uint32_t)f->ref);
    __boot_serial_force_puts(" flags=");
    __boot_serial_force_puthex64((uint64_t)f->flags);
    __boot_serial_force_puts(" off=");
    __boot_serial_force_puthex64(f->offset);
    if(f->kind==FDKIND_VFSFILE||f->kind==FDKIND_DIR){
        const char *p=c3_vfs_open_slot_path(f->ref);
        if(p){
            __boot_serial_force_puts(" path=");
            __boot_serial_force_puts(p);
        }
    }else if((f->kind==FDKIND_PIPE_R||f->kind==FDKIND_PIPE_W)&&
             f->ref>=0&&f->ref<PIPE_MAX&&g_pipes[f->ref].used){
        pipe_t *p=&g_pipes[f->ref];
        uint32_t av=(uint32_t)((p->head-p->tail+PIPE_BUF_SIZE)%PIPE_BUF_SIZE);
        __boot_serial_force_puts(" pipe_av=");
        __boot_serial_force_putu32(av);
        __boot_serial_force_puts(" readers=");
        __boot_serial_force_putu32((uint32_t)p->readers);
        __boot_serial_force_puts(" writers=");
        __boot_serial_force_putu32((uint32_t)p->writers);
    }else if(f->kind==FDKIND_SOCKET&&
             f->ref>=0&&f->ref<SOCK_MAX&&g_sockets[f->ref].used){
        socket_t *s=&g_sockets[f->ref];
        uint32_t rx=s->rx_head-s->rx_tail;
        uint32_t tx=s->tx_head-s->tx_tail;
        uint32_t anc=(uint32_t)(uint8_t)(s->anc_head-s->anc_tail);
        __boot_serial_force_puts(" rx=");
        __boot_serial_force_putu32(rx);
        __boot_serial_force_puts(" tx=");
        __boot_serial_force_putu32(tx);
        __boot_serial_force_puts(" anc=");
        __boot_serial_force_putu32(anc);
        __boot_serial_force_puts(" peer=");
        __boot_serial_force_putu32((uint32_t)s->peer);
        __boot_serial_force_puts(" svc=");
        __boot_serial_force_putu32((uint32_t)s->virt_service);
        __boot_serial_force_puts(" st=");
        __boot_serial_force_putu32((uint32_t)s->tcp_state);
    }
    __boot_serial_force_puts("\n");
}
static void c3_force_chrome_fd_table(const char *tag,const task_t *t){
    int fd;
    if(!tag||!t)return;
    __boot_serial_force_puts(tag);
    __boot_serial_force_puts(" pid=");
    __boot_serial_force_putu32((uint32_t)t->pid);
    c3_force_task_name(t);
    __boot_serial_force_puts(" fdt_group=");
    __boot_serial_force_putu32((uint32_t)c3_fd_group_id(t));
    __boot_serial_force_puts("\n");
    for(fd=0;fd<16&&fd<TASK_FD_MAX;++fd)c3_force_one_fd("[chrome-fd!]",t,fd);
    if(100<TASK_FD_MAX)c3_force_one_fd("[chrome-fd!]",t,100);
}
static void c3_trace_fd_table(const char *tag,const task_t *t){
    int fd,printed=0;
    if(!tag||!t||!c3_task_is_firefox_runtime(t))return;
    __boot_serial_puts(tag);
    __boot_serial_puts(" pid=");
    __boot_serial_putu32((uint32_t)t->pid);
    c3_trace_task_name(t);
    __boot_serial_puts(" fdt_group=");
    __boot_serial_putu32((uint32_t)c3_fd_group_id(t));
    __boot_serial_puts("\n");
    for(fd=0;fd<TASK_FD_MAX&&printed<40;++fd){
        if(t->fdt.fds[fd].kind==FDKIND_NONE)continue;
        c3_trace_one_fd("[fd]",t,fd);
        ++printed;
    }
}
static bool c3_exec_is_firefox_glxtest(const char *path,char **argv,int argc){
    int i;
    if(path&&(c3_has_token(path,"/opt/firefox/glxtest")||c3_has_token(path,"/glxtest")))return true;
    if(!argv)return false;
    for(i=0;i<argc&&i<4;++i){
        if(argv[i]&&(c3_has_token(argv[i],"/opt/firefox/glxtest")||
                    c3_has_token(argv[i],"/glxtest")||
                    c3_strcmp(argv[i],"glxtest")==0))return true;
    }
    return false;
}
static bool c3_exec_is_chromium_trace(const char *path,char **argv,int argc){
    int i;
    if(path&&(c3_has_token(path,"/opt/chromium/")||
              c3_has_token(path,"/opt/google/chrome/")||
              c3_has_token(path,"/chromium")||
              c3_has_token(path,"/chrome")))return true;
    if(!argv)return false;
    for(i=0;i<argc&&i<32;++i){
        if(argv[i]&&(c3_has_token(argv[i],"--type=")||
                    c3_has_token(argv[i],"--field-trial")||
                    c3_has_token(argv[i],"--shared-files")||
                    c3_has_token(argv[i],"--metrics-shmem-handle")))return true;
    }
    return false;
}
static bool c3_exec_chromium_drop_arg(const char *arg){
    if(!arg)return false;
    if(c3_starts_with(arg,"--metrics-shmem-handle="))return true;
    if(c3_starts_with(arg,"--field-trial-handle="))return true;
    if(c3_starts_with(arg,"--pseudonymization-salt-handle="))return true;
    if(c3_starts_with(arg,"--trace-process-track-uuid="))return true;
    if(c3_starts_with(arg,"--trace-startup"))return true;
    if(c3_starts_with(arg,"--enable-crash-reporter="))return true;
    if(c3_starts_with(arg,"--variations-seed-version"))return true;
    return false;
}
#ifndef C3_EXEC_ARG_MAX
#define C3_EXEC_ARG_MAX 256
#endif
static bool c3_exec_arg_has_prefix(char **argv,int argc,const char *prefix){
    int i;
    size_t n;
    if(!argv||!prefix)return false;
    n=c3_strlen(prefix);
    for(i=0;i<argc&&i<C3_EXEC_ARG_MAX;++i){
        if(argv[i]&&c3_strncmp(argv[i],prefix,n)==0)return true;
    }
    return false;
}
static int c3_exec_append_arg(char **argv,int *argc,const char *arg){
    if(!argv||!argc||!arg)return 0;
    if(*argc<0||*argc>=C3_EXEC_ARG_MAX)return 0;
    argv[*argc]=(char*)arg;
    ++*argc;
    argv[*argc]=0;
    return 1;
}
static int c3_exec_stabilize_chromium_args(char **argv,int *argc){
    int added=0;
    if(!argv||!argc)return 0;
    if(!c3_exec_arg_has_prefix(argv,*argc,"--disable-background-tracing"))
        added+=c3_exec_append_arg(argv,argc,"--disable-background-tracing");
    if(!c3_exec_arg_has_prefix(argv,*argc,"--disable-chrome-tracing-computation"))
        added+=c3_exec_append_arg(argv,argc,"--disable-chrome-tracing-computation");
    if(!c3_exec_arg_has_prefix(argv,*argc,"--no-slow-histograms"))
        added+=c3_exec_append_arg(argv,argc,"--no-slow-histograms");
    return added;
}
static int c3_exec_filter_chromium_args(char **argv,int *argc){
    int r,w=0,removed=0;
    if(!argv||!argc||*argc<=0)return 0;
    for(r=0;r<*argc;++r){
        if(c3_exec_chromium_drop_arg(argv[r])){
            ++removed;
            continue;
        }
        argv[w++]=argv[r];
    }
    argv[w]=0;
    *argc=w;
    return removed;
}
static bool c3_exec_chromium_needs_v8_fd100(char **argv,int argc){
    int i;
    if(!argv)return false;
    for(i=0;i<argc&&i<256;++i){
        const char *a=argv[i];
        if(a&&c3_has_token(a,"--shared-files=")&&
           c3_has_token(a,"v8_context_snapshot_data:100"))return true;
    }
    return false;
}
static int c3_exec_chromium_ensure_v8_fd100(task_t *cur,char **argv,int argc){
    static const char v8_path[]="/opt/chromium/v8_context_snapshot.bin";
    const uint8_t *data=0;
    uint32_t size=0;
    int slot;
    if(!cur||!c3_exec_chromium_needs_v8_fd100(argv,argc))return 0;
    c3_fd_group_sync_fd(cur,100);
    if(fd_valid(&cur->fdt,100))return 0;
    if(c3_fd_group_slot_used(cur,100))return -EBUSY;
    if(!kvfs_read(v8_path,&data,&size)||!data||!size)return -ENOENT;
    slot=c3_vfs_open_slot_alloc(v8_path);
    if(slot<0)return -EMFILE;
    cur->fdt.fds[100].kind=FDKIND_VFSFILE;
    cur->fdt.fds[100].ref=slot;
    cur->fdt.fds[100].flags=FDFL_READABLE;
    cur->fdt.fds[100].offset=0;
    cur->fdt.fds[100].refcount=1;
    c3_fd_group_copy_fd(cur,100);
    __boot_serial_force_puts("[execve-chrome-v8fd!] pid=");
    __boot_serial_force_putu32((uint32_t)cur->pid);
    c3_force_task_name(cur);
    __boot_serial_force_puts(" fd=100 path=");
    __boot_serial_force_puts(v8_path);
    __boot_serial_force_puts(" size=");
    __boot_serial_force_putu32(size);
    __boot_serial_force_puts("\n");
    return 0;
}
static void c3_task_force_browser_stable_mode(task_t *t,const char *path){
    bool firefox=false;
    if(!t)return;
    if(path&&(c3_has_token(path,"/opt/firefox/")||
              c3_has_token(path,"/firefox")))firefox=true;
    if(firefox||c3_task_is_firefox_runtime(t)){
        t->no_timer_preempt=true;
        g_task_preempt_defer_ticks=3;
    }
}
static int c3_task_index_by_pid(int pid){int i;for(i=0;i<TASK_MAX;++i)if(g_tasks[i].used&&g_tasks[i].pid==pid)return i;return -1;}
static task_t *c3_task_by_pid(int pid){int i=c3_task_index_by_pid(pid);return(i>=0)?&g_tasks[i]:0;}
static void c3_clone_prepare_user_tls_stack(task_t *cur,uint64_t child_pid,uint64_t flags,uint64_t child_stack,uint64_t ptid,uint64_t ctid,uint64_t tls);

static inline uint64_t c3_rdtsc(void){uint32_t lo,hi;__asm__ volatile("rdtsc":"=a"(lo),"=d"(hi));return((uint64_t)hi<<32)|lo;}
static uint64_t g_boot_tsc=0;
static uint64_t g_tsc_freq_approx=3000000000ULL; /* ~3GHz estimate */
static uint32_t g_umask_val=022;
static uint8_t  g_c3_seccomp_mode[TASK_MAX];

#define C3_SCM_RIGHTS_MAX 128
#define C3_SCM_RIGHTS_TOKEN_BASE 0x40000000
typedef struct {
    bool used;
    real_fd_t fd;
} c3_scm_right_t;
static c3_scm_right_t g_c3_scm_rights[C3_SCM_RIGHTS_MAX];
static uint32_t g_c3_scm_rights_next=0;
static uint32_t g_c3_seccomp_flags[TASK_MAX];
static uint64_t g_c3_ns_mask[TASK_MAX];
static uint8_t  g_c3_no_new_privs[TASK_MAX];
#define C3_AFFINITY_WORDS ((TASK_MAX+63)/64)
static uint64_t g_c3_sched_affinity[TASK_MAX][C3_AFFINITY_WORDS];
#define C3_RLIMIT_RESOURCE_MAX 16
typedef struct {
    uint64_t cur;
    uint64_t max;
} c3_rlimit_pair_t;
static c3_rlimit_pair_t g_c3_rlimits[TASK_MAX][C3_RLIMIT_RESOURCE_MAX];
static uint8_t g_c3_rlimits_inited[TASK_MAX];
static int g_c3_rlimits_pid[TASK_MAX];
static uint32_t g_c3_dyn_images_mapped=0;
static uint32_t g_c3_dyn_reloc_applied=0;
static uint32_t g_c3_dyn_reloc_failed=0;
static uint32_t g_c3_dyn_reloc_unsupported=0;
static uint32_t g_c3_dyn_needed_loaded=0;
static uint32_t g_c3_dyn_needed_missing=0;
static uint32_t g_c3_dyn_objects=0;
static int g_c3_dyn_load_depth=0;
static uint32_t g_c3_dyn_timeout_ms=4000;
static uint32_t g_c3_dyn_timeout_hits=0;
static uint64_t g_c3_dyn_last_timeout_tsc=0;
static char g_c3_dyn_last_timeout_obj[128];
static char g_c3_dyn_last_timeout_stage[24];
static uint64_t g_c3_dyn_cycle_map=0;
static uint64_t g_c3_dyn_cycle_needed=0;
static uint64_t g_c3_dyn_cycle_reloc=0;
static uint32_t g_c3_cow_faults=0;
static uint32_t g_c3_cow_copies=0;
static uint32_t g_c3_cow_shared_maps=0;
static uint32_t g_c3_cow_fault_fail=0;
static uint32_t g_c3_pf_write_fixups=0;
static uint32_t g_c3_pf_exec_fixups=0;
static uint32_t g_c3_pf_trace_count=0;
static uint32_t g_c3_mmap_trace_count=0;
static uint32_t g_c3_mprotect_trace_count=0;
static uint32_t g_c3_mmap_fail_trace_count=0;
static uint32_t g_c3_munmap_trace_count=0;
static uint32_t g_c3_x11_read_trace_count=0;
static uint16_t g_c3_phys_refcnt[PMM_MAX_PAGES];

#ifndef C3_MMAP_TRACE_MAX
#define C3_MMAP_TRACE_MAX 96
#endif
#ifndef C3_MPROTECT_TRACE_MAX
#define C3_MPROTECT_TRACE_MAX 48
#endif
#ifndef C3_MUNMAP_TRACE_MAX
#define C3_MUNMAP_TRACE_MAX 32
#endif

#define C3_USER_TOP      0x0000800000000000ULL
#define C3_PMM_BASE      0x200000ULL
#define C3_PTE_ADDR_MASK 0x000FFFFFFFFFF000ULL
#define C3_PTE_KEEP_MASK (~C3_PTE_ADDR_MASK)
#define C3_PAGE_SOFT_COW (1ULL<<9)

#define C3_FILEPAGE_MAX  8192
typedef struct {
    bool     used;
    char     path[VFS_PATH_MAX];
    uint64_t page_off;
    uint64_t phys;
} c3_file_page_t;
static c3_file_page_t g_c3_file_pages[C3_FILEPAGE_MAX];

#ifndef C3_EXEC_ARG_MAX
#define C3_EXEC_ARG_MAX   256
#endif
#ifndef C3_EXEC_ENV_MAX
#define C3_EXEC_ENV_MAX   512
#endif
#ifndef C3_EXEC_STR_MAX
#define C3_EXEC_STR_MAX   32768
#endif
#define C3_USER_STACK_TOP 0x7FFFFFFFE000ULL
#define C3_USER_STACK_GROW_MAX (256ULL*1024ULL*1024ULL)
#define C3_USER_TLS_BASE  ((C3_USER_STACK_TOP-C3_USER_STACK_GROW_MAX-PAGE_SIZE)&~(PAGE_SIZE-1ULL))
#define C3_USER_TLS_PREP_BYTES (16ULL*PAGE_SIZE)
#define C3_USER_TLS_DTV   (C3_USER_TLS_BASE+0x200ULL)
#define C3_USER_TLS_DTV_SLOTS 32
static void c3_trace_user_addr_mapping(const task_t *cur,uint64_t addr,const char *tag);
/* Exec scratch area to avoid large allocations on the kernel stack. */
static char  g_c3_exec_argv_pool[C3_EXEC_STR_MAX];
static char  g_c3_exec_env_pool[C3_EXEC_STR_MAX];
static char *g_c3_exec_argv[C3_EXEC_ARG_MAX+1];
static char *g_c3_exec_envp[C3_EXEC_ENV_MAX+1];

typedef struct {
    uint64_t val;
    uint64_t to_free;
} c3_dtv_pointer_t;

typedef union {
    uint64_t counter;
    c3_dtv_pointer_t pointer;
} c3_dtv_t;

typedef struct {
    uint64_t tcb;
    uint64_t dtv;
    uint64_t self;
    uint32_t multiple_threads;
    uint32_t gscope_flag;
    uint64_t sysinfo;
    uint64_t stack_guard;
    uint64_t pointer_guard;
    uint64_t unused_vgetcpu_cache[2];
    uint32_t feature_1;
    uint32_t glibc_unused1;
    uint64_t private_tm[4];
    uint64_t private_ss;
    uint64_t ssp_base;
} c3_glibc_tcb_head_t;

#define C3_DYNOBJ_MAX 256
#define C3_DYN_NEEDED_MAX 128
#define C3_TLS_MODULE_MAX 128
#define C3_DYN_PROFILE_MAX 64
#define C3_DYN_PROFILE_FLAG_TIMEOUT 0x1
typedef struct {
    bool     used;
    char     name[128];
    uint64_t load_bias;
    uint64_t base;
    uint64_t end;
    uint64_t phdr_va;
    uint16_t phnum;
    uint64_t symtab_va;
    uint64_t strtab_va;
    uint64_t syment;
    uint64_t strsz;
    uint32_t symbol_count;
    uint64_t versym_va;
    uint64_t verdef_va;
    uint32_t verdef_num;
    uint16_t tls_module_id;
    uint64_t tls_init_va;
    uint64_t tls_filesz;
    uint64_t tls_memsz;
    uint64_t tls_align;
} c3_dynobj_t;
static c3_dynobj_t g_c3_dynobjs[C3_DYNOBJ_MAX];
static c3_dynobj_t g_c3_exec_dyn_backup[C3_DYNOBJ_MAX];
static char g_c3_next_image_name[128];
static uint16_t g_c3_tls_next_module=1;
typedef struct {
    bool     used;
    uint16_t depth;
    uint16_t flags;
    char     name[128];
    uint32_t needed_total;
    uint32_t needed_loaded;
    uint32_t needed_missing;
    uint32_t relocs_applied;
    uint32_t relocs_failed;
    uint32_t relocs_unsupported;
    uint64_t cycle_map;
    uint64_t cycle_needed;
    uint64_t cycle_reloc;
} c3_dyn_profile_t;
static c3_dyn_profile_t g_c3_dyn_profile[C3_DYN_PROFILE_MAX];
static uint32_t g_c3_dyn_profile_next=0;
static c3_dyn_profile_t g_c3_exec_dyn_profile_backup[C3_DYN_PROFILE_MAX];

static uint64_t c3_tsc_to_us(uint64_t cyc){
    if(!cyc||!g_tsc_freq_approx)return 0;
    return(cyc*1000000ULL)/g_tsc_freq_approx;
}

static uint64_t c3_dyn_deadline_from_ms(uint32_t ms){
    if(!ms||!g_tsc_freq_approx)return 0;
    return c3_rdtsc()+((uint64_t)ms*g_tsc_freq_approx)/1000ULL;
}

static bool c3_dyn_deadline_expired(uint64_t deadline){
    return deadline&&c3_rdtsc()>=deadline;
}

static uint64_t c3_poll_slice_deadline(uint64_t deadline){
    uint64_t now=c3_rdtsc();
    uint64_t slice=now+(g_tsc_freq_approx/1000ULL); /* 1ms */
    if(deadline&&deadline<slice)slice=deadline;
    return slice?slice:now;
}

static void c3_poll_wait_slice(task_t *cur,uint64_t deadline){
    uint64_t slice=c3_poll_slice_deadline(deadline);
    if(!cur){
        task_schedule();
        return;
    }
    if(slice<=c3_rdtsc()){
        __asm__ volatile("pause");
        return;
    }
    cur->sleep_until=slice;
    cur->state=TASK_SLEEPING;
    task_schedule();
    if(cur->state==TASK_SLEEPING){
        cur->state=TASK_RUNNING;
        while(c3_rdtsc()<slice)__asm__ volatile("pause");
    }
    cur->sleep_until=0;
}

static void c3_futex_wait_schedule(task_t *cur,uint64_t deadline){
    if(deadline){
        c3_poll_wait_slice(cur,deadline);
        return;
    }
    if(!cur){
        task_schedule();
        return;
    }
    cur->sleep_until=0;
    cur->state=TASK_SLEEPING;
    task_schedule();
    if(cur->state==TASK_SLEEPING){
        cur->state=TASK_RUNNING;
        __asm__ volatile("pause");
    }
}

static uint32_t c3_timespec_to_ms_ceil(const timespec_t *ts){
    uint64_t ms;
    uint64_t frac;
    if(!ts)return 0;
    if(ts->tv_sec>(int64_t)(0x7FFFFFFFULL/1000ULL))return 0x7FFFFFFFU;
    ms=(uint64_t)ts->tv_sec*1000ULL;
    frac=(uint64_t)(ts->tv_nsec+999999LL)/1000000ULL;
    if(ms>0x7FFFFFFFULL)ms=0x7FFFFFFFULL;
    if(frac>0x7FFFFFFFULL-ms)ms=0x7FFFFFFFULL;
    else ms+=frac;
    return(uint32_t)ms;
}

static uint32_t c3_timeval_to_ms_ceil(const timeval_t *tv){
    uint64_t ms;
    uint64_t frac;
    if(!tv)return 0;
    if(tv->tv_sec>(int64_t)(0x7FFFFFFFULL/1000ULL))return 0x7FFFFFFFU;
    ms=(uint64_t)tv->tv_sec*1000ULL;
    frac=(uint64_t)(tv->tv_usec+999LL)/1000ULL;
    if(ms>0x7FFFFFFFULL)ms=0x7FFFFFFFULL;
    if(frac>0x7FFFFFFFULL-ms)ms=0x7FFFFFFFULL;
    else ms+=frac;
    return(uint32_t)ms;
}

static c3_dyn_profile_t *c3_dyn_profile_begin(const char *name,uint16_t depth,uint32_t needed_total){
    c3_dyn_profile_t *p;
    uint32_t idx;
    idx=g_c3_dyn_profile_next%C3_DYN_PROFILE_MAX;
    ++g_c3_dyn_profile_next;
    p=&g_c3_dyn_profile[idx];
    c3_memset(p,0,sizeof(*p));
    p->used=true;
    p->depth=depth;
    p->needed_total=needed_total;
    if(name&&*name)c3_strlcpy(p->name,name,sizeof(p->name));
    else c3_strlcpy(p->name,"(anon)",sizeof(p->name));
    return p;
}

static void c3_dyn_profile_add_cycle(uint64_t *dst,uint64_t add){
    if(!dst||!add)return;
    if(*dst>~0ULL-add)*dst=~0ULL;
    else *dst+=add;
}

static uint32_t c3_dyn_u32_delta(uint32_t after,uint32_t before){
    if(after>=before)return after-before;
    return after;
}

static void c3_dyn_note_timeout(const char *obj,const char *stage,c3_dyn_profile_t *p){
    ++g_c3_dyn_timeout_hits;
    g_c3_dyn_last_timeout_tsc=c3_rdtsc();
    if(obj&&*obj)c3_strlcpy(g_c3_dyn_last_timeout_obj,obj,sizeof(g_c3_dyn_last_timeout_obj));
    else g_c3_dyn_last_timeout_obj[0]=0;
    if(stage&&*stage)c3_strlcpy(g_c3_dyn_last_timeout_stage,stage,sizeof(g_c3_dyn_last_timeout_stage));
    else g_c3_dyn_last_timeout_stage[0]=0;
    if(p)p->flags|=C3_DYN_PROFILE_FLAG_TIMEOUT;
}

#define C3_FUTEX_WAIT_MAX 256
#define C3_FUTEX_BUCKETS 64
typedef struct {
    bool      used;
    uint32_t *uaddr;
    uintptr_t mm_key;
    uint32_t  bitset;
    int       task_index;
    int       next;
    uint32_t  seq;
} c3_futex_waiter_t;
static c3_futex_waiter_t g_c3_futex_waiters[C3_FUTEX_WAIT_MAX];
static int g_c3_futex_heads[C3_FUTEX_BUCKETS];
static uint32_t g_c3_futex_waiter_seq;

#define C3_EVENTFD_MAX 128
#define C3_EFD_SEMAPHORE 1u
typedef struct {
    bool     used;
    bool     semaphore;
    uint64_t counter;
} c3_eventfd_t;
static c3_eventfd_t g_c3_eventfds[C3_EVENTFD_MAX];

#define C3_TIMERFD_MAX 128
#define C3_TFD_TIMER_ABSTIME 1
typedef struct {
    bool     used;
    int      clockid;
    bool     armed;
    uint64_t interval_tsc;
    uint64_t next_tsc;
} c3_timerfd_t;
static c3_timerfd_t g_c3_timerfds[C3_TIMERFD_MAX];

#define C3_INOTIFY_INST_MAX 64
#define C3_INOTIFY_WATCH_MAX 512
#define C3_INOTIFY_EVENT_MAX 256
#define C3_INOTIFY_NAME_MAX 96

typedef struct {
    int32_t  wd;
    uint32_t mask;
    uint32_t cookie;
    uint16_t name_len;
    char     name[C3_INOTIFY_NAME_MAX];
} c3_inotify_event_rec_t;

typedef struct {
    bool      used;
    uint32_t  next_wd;
    uint16_t  head;
    uint16_t  tail;
    c3_inotify_event_rec_t q[C3_INOTIFY_EVENT_MAX];
} c3_inotify_inst_t;

typedef struct {
    bool     used;
    int      inst_ref;
    int32_t  wd;
    uint32_t mask;
    char     path[VFS_PATH_MAX];
} c3_inotify_watch_t;

static c3_inotify_inst_t g_c3_inotify_inst[C3_INOTIFY_INST_MAX];
static c3_inotify_watch_t g_c3_inotify_watch[C3_INOTIFY_WATCH_MAX];
static uint32_t g_c3_inotify_cookie=1;

#define C3_RENAME_NOREPLACE 1u
#define C3_RENAME_EXCHANGE  2u
#define C3_RENAME_WHITEOUT  4u

#define C3_IN_ACCESS        0x00000001u
#define C3_IN_MODIFY        0x00000002u
#define C3_IN_ATTRIB        0x00000004u
#define C3_IN_CLOSE_WRITE   0x00000008u
#define C3_IN_CLOSE_NOWRITE 0x00000010u
#define C3_IN_OPEN          0x00000020u
#define C3_IN_MOVED_FROM    0x00000040u
#define C3_IN_MOVED_TO      0x00000080u
#define C3_IN_CREATE        0x00000100u
#define C3_IN_DELETE        0x00000200u
#define C3_IN_DELETE_SELF   0x00000400u
#define C3_IN_MOVE_SELF     0x00000800u
#define C3_IN_UNMOUNT       0x00002000u
#define C3_IN_Q_OVERFLOW    0x00004000u
#define C3_IN_IGNORED       0x00008000u
#define C3_IN_ONLYDIR       0x01000000u
#define C3_IN_DONT_FOLLOW   0x02000000u
#define C3_IN_EXCL_UNLINK   0x04000000u
#define C3_IN_MASK_ADD      0x20000000u
#define C3_IN_ISDIR         0x40000000u
#define C3_IN_ONESHOT       0x80000000u
#define C3_IN_ALL_EVENTS    (C3_IN_ACCESS|C3_IN_MODIFY|C3_IN_ATTRIB|C3_IN_CLOSE_WRITE|C3_IN_CLOSE_NOWRITE|C3_IN_OPEN|C3_IN_MOVED_FROM|C3_IN_MOVED_TO|C3_IN_CREATE|C3_IN_DELETE|C3_IN_DELETE_SELF|C3_IN_MOVE_SELF)

#define C3_FALLOC_FL_KEEP_SIZE  0x01
#define C3_FALLOC_FL_PUNCH_HOLE 0x02
#define C3_FALLOC_FL_COLLAPSE_RANGE 0x08
#define C3_FALLOC_FL_ZERO_RANGE 0x10
#define C3_FALLOC_FL_INSERT_RANGE 0x20
#define C3_FALLOC_FL_UNSHARE_RANGE 0x40

static uint32_t c3_fd_ready_mask(task_t *cur,int fd);
static int c3_epoll_ref_from_fd(task_t *cur,int epfd);
static int c3_eventfd_alloc(uint64_t initval,bool semaphore);
static void c3_eventfd_free(int ref);
static int64_t c3_eventfd_read(int ref,void *buf,size_t count,int flags);
static int64_t c3_eventfd_write(int ref,const void *buf,size_t count);
static int c3_timerfd_alloc(int clockid);
static void c3_timerfd_free(int ref);
static bool c3_timerfd_is_ready(int ref,uint64_t now_tsc);
static int64_t c3_timerfd_read_ready(int ref,void *buf,size_t count,int flags);
static int64_t c3_timerfd_settime_ref(int ref,int flags,const void *nv,void *ov);
static bool c3_task_has_unblocked_signal(const task_t *t);
static int c3_inotify_inst_alloc(void);
static void c3_inotify_inst_free(int ref);
static int c3_inotify_watch_add(int ref,const char *path,uint32_t mask);
static int c3_inotify_watch_rm(int ref,int32_t wd);
static int64_t c3_inotify_read(int ref,void *buf,size_t count,int flags);
static void c3_inotify_notify_path(const char *path,uint32_t mask,uint32_t cookie);

static uint32_t c3_futex_wake_n(uint32_t *uaddr,uint32_t want,uintptr_t mm_key,bool exact_key,uint32_t bitset);
static void c3_futex_waiter_remove_task_uaddr(int task_index,uint32_t *uaddr,uintptr_t mm_key,bool exact_key);
static int c3_proc_self_maps(char *buf,int mx,task_t *cur);
static int c3_phys_ref_index(uint64_t phys);
static void c3_phys_ref_set_initial(uint64_t phys);
static void c3_phys_ref_retain(uint64_t phys);
static void c3_phys_ref_release(uint64_t phys);
static void c3_file_page_invalidate_phys(uint64_t phys);
static void c3_file_page_invalidate_path(const char *path);
static int c3_exec_capture_vec(char *const src[],char **outv,int maxc,char *pool,size_t poolsz,int *outc);
static int c3_exec_extract_interp(const uint8_t *data,uint32_t sz,char *out,size_t out_cap,bool *needs_interp);
static int c3_exec_resolve_interp_path(const char *exe_path,const char *interp,char *out,size_t out_cap);
static int c3_exec_collect_elf_info(const uint8_t *data,uint32_t sz,uint64_t mmap_seed,uint64_t *load_bias,uint64_t *entry,uint64_t *phdr,uint64_t *phent,uint64_t *phnum);
static bool c3_exec_map_user_stack(address_space_t *as);
static void c3_exec_fill_random16(uint8_t out[16]);
static void c3_exec_close_cloexec_fds(task_t *cur);
static void c3_exec_reset_signal_state(task_t *cur);
static int c3_user_write_u32_as(address_space_t *as,uint64_t va,uint32_t val,const char *tag);
static void c3_exec_reset_dynamic_state(void);
static void c3_exec_clear_mmap_for_as(address_space_t *as);

typedef struct __attribute__((packed)) {
    uint64_t r_offset;
    uint64_t r_info;
    int64_t  r_addend;
} c3_elf64_rela_t;

typedef struct __attribute__((packed)) {
    uint64_t r_offset;
    uint64_t r_info;
} c3_elf64_rel_t;

typedef struct __attribute__((packed)) {
    uint16_t vd_version;
    uint16_t vd_flags;
    uint16_t vd_ndx;
    uint16_t vd_cnt;
    uint32_t vd_hash;
    uint32_t vd_aux;
    uint32_t vd_next;
} c3_elf64_verdef_t;

typedef struct __attribute__((packed)) {
    uint32_t vda_name;
    uint32_t vda_next;
} c3_elf64_verdaux_t;

typedef struct __attribute__((packed)) {
    uint16_t vn_version;
    uint16_t vn_cnt;
    uint32_t vn_file;
    uint32_t vn_aux;
    uint32_t vn_next;
} c3_elf64_verneed_t;

typedef struct __attribute__((packed)) {
    uint32_t vna_hash;
    uint16_t vna_flags;
    uint16_t vna_other;
    uint32_t vna_name;
    uint32_t vna_next;
} c3_elf64_vernaux_t;

#define C3_DT_NULL      0
#define C3_DT_NEEDED    1
#define C3_DT_HASH      4
#define C3_DT_STRTAB    5
#define C3_DT_SYMTAB    6
#define C3_DT_RELA      7
#define C3_DT_RELASZ    8
#define C3_DT_RELAENT   9
#define C3_DT_STRSZ     10
#define C3_DT_SYMENT    11
#define C3_DT_SONAME    14
#define C3_DT_RPATH     15
#define C3_DT_REL       17
#define C3_DT_RELSZ     18
#define C3_DT_RELENT    19
#define C3_DT_PLTREL    20
#define C3_DT_PLTRELSZ  2
#define C3_DT_JMPREL    23
#define C3_DT_RUNPATH   29
#define C3_DT_RELRSZ    35
#define C3_DT_RELR      36
#define C3_DT_RELRENT   37
#define C3_DT_VERSYM    0x6ffffff0
#define C3_DT_VERDEF    0x6ffffffc
#define C3_DT_VERDEFNUM 0x6ffffffd
#define C3_DT_VERNEED   0x6ffffffe
#define C3_DT_VERNEEDNUM 0x6fffffff
#define C3_DT_GNU_HASH  0x6ffffef5

typedef struct {
    bool     present;
    uint64_t rela_va, rela_sz, rela_ent;
    uint64_t rel_va, rel_sz, rel_ent;
    uint64_t relr_va, relr_sz, relr_ent;
    uint64_t jmprel_va, pltrelsz, pltrel;
    uint64_t symtab_va, strtab_va, syment, strsz;
    uint64_t versym_va;
    uint64_t verdef_va;
    uint64_t verdef_num;
    uint64_t verneed_va;
    uint64_t verneed_num;
    uint64_t hash_va, gnu_hash_va;
    uint32_t needed_count;
    uint64_t needed_offs[C3_DYN_NEEDED_MAX];
    uint64_t soname_off;
    uint64_t rpath_off;
    uint64_t runpath_off;
} c3_dyn_info_t;

#define C3_R_X86_64_NONE      0
#define C3_R_X86_64_64        1
#define C3_R_X86_64_COPY      5
#define C3_R_X86_64_GLOB_DAT  6
#define C3_R_X86_64_JUMP_SLOT 7
#define C3_R_X86_64_RELATIVE  8
#define C3_R_X86_64_32        10
#define C3_R_X86_64_32S       11
#define C3_R_X86_64_DTPMOD64  16
#define C3_R_X86_64_DTPOFF64  17
#define C3_R_X86_64_TPOFF64   18
#define C3_R_X86_64_DTPOFF32  21
#define C3_R_X86_64_GOTTPOFF  22
#define C3_R_X86_64_TPOFF32   23
#define C3_R_X86_64_IRELATIVE 37
#define C3_R_X86_64_GOTPCRELX 41
#define C3_R_X86_64_REX_GOTPCRELX 42
#define C3_DYN_RELOC_MAX_ENTRIES 4096ULL

typedef struct __attribute__((packed)) {
    uint16_t sin_family;
    uint16_t sin_port;
    uint32_t sin_addr;
    uint8_t  sin_zero[8];
} c3_sockaddr_in_t;

typedef struct __attribute__((packed)) {
    uint16_t sun_family;
    char     sun_path[108];
} c3_sockaddr_un_t;

typedef struct __attribute__((packed)) {
    uint16_t nl_family;
    uint16_t nl_pad;
    uint32_t nl_pid;
    uint32_t nl_groups;
} c3_sockaddr_nl_t;

typedef struct {
    void    *msg_name;
    uint32_t msg_namelen;
    iovec_t *msg_iov;
    size_t   msg_iovlen;
    void    *msg_control;
    size_t   msg_controllen;
    int      msg_flags;
} c3_msghdr_t;

typedef struct {
    size_t cmsg_len;
    int    cmsg_level;
    int    cmsg_type;
} c3_cmsghdr_t;

typedef struct {
    c3_msghdr_t msg_hdr;
    uint32_t    msg_len;
} c3_mmsghdr_t;

typedef struct {
    int   fd;
    short events;
    short revents;
} c3_pollfd_t;

#define C3_MSG_IOV_MAX 1024
#define C3_MMSG_MAX 1024
#define C3_MSG_PEEK 0x2
#define C3_MSG_DONTWAIT 0x40
#define C3_MSG_CTRUNC 0x8
#define C3_MSG_WAITFORONE 0x10000
#define C3_MSG_CMSG_CLOEXEC 0x40000000u
#define C3_SOL_SOCKET 1
#define C3_SCM_RIGHTS 1
#define C3_SCM_CREDENTIALS 2
#define C3_SO_ERROR 4
#define C3_SO_TYPE 3
#define C3_SO_RCVBUF 8
#define C3_SO_SNDBUF 7
#define C3_SO_KEEPALIVE 9
#define C3_SO_LINGER 13
#define C3_SO_PASSCRED 16
#define C3_SO_PEERCRED 17
#define C3_SO_ACCEPTCONN 30
#define C3_SO_PROTOCOL 38
#define C3_SO_DOMAIN 39
#define C3_SOCK_VFLAG_PASSCRED 0x0040u
#define C3_POLLIN  0x001
#define C3_POLLOUT 0x004
#define C3_POLLERR 0x008
#define C3_POLLHUP 0x010

typedef struct {
    int32_t pid;
    int32_t uid;
    int32_t gid;
} c3_ucred_t;

static bool c3_socket_ref_closed(int ref){
    socket_t *s;
    if(ref<0||ref>=SOCK_MAX)return true;
    s=&g_sockets[ref];
    if(!s->used)return true;
    return s->type==1&&s->tcp_state==TCP_CLOSED;
}

static uint32_t c3_socket_rx_used(int ref){
    socket_t *s;
    uint32_t used;
    if(ref<0||ref>=SOCK_MAX||!g_sockets[ref].used)return 0;
    s=&g_sockets[ref];
    used=s->rx_head-s->rx_tail;
    if(used>SOCK_BUF_SIZE)used=SOCK_BUF_SIZE;
    return (uint32_t)used;
}

static uint32_t c3_socket_send_space(int ref){
    socket_t *s;
    socket_t *dst;
    uint32_t used;
    if(ref<0||ref>=SOCK_MAX||!g_sockets[ref].used)return 0;
    s=&g_sockets[ref];
    if(s->peer>=0&&s->peer<SOCK_MAX&&g_sockets[s->peer].used){
        dst=&g_sockets[s->peer];
        used=dst->rx_head-dst->rx_tail;
    }else{
        dst=s;
        used=dst->tx_head-dst->tx_tail;
    }
    if(used>SOCK_BUF_SIZE)used=SOCK_BUF_SIZE;
    return (uint32_t)(SOCK_BUF_SIZE-used);
}

static int c3_socket_ref_virtual_service(int ref){
    socket_t *s;
    int svc;
    if(ref<0||ref>=SOCK_MAX||!g_sockets[ref].used)return SOCK_VIRT_NONE;
    s=&g_sockets[ref];
    svc=s->virt_service;
    if(svc!=SOCK_VIRT_NONE)return svc;
    if(s->peer>=0&&s->peer<SOCK_MAX&&g_sockets[s->peer].used)return g_sockets[s->peer].virt_service;
    return SOCK_VIRT_NONE;
}

static bool c3_socket_ref_is_virtual_stream(int ref){
    socket_t *s;
    int svc=SOCK_VIRT_NONE;
    if(ref<0||ref>=SOCK_MAX||!g_sockets[ref].used)return false;
    s=&g_sockets[ref];
    if(s->type!=1)return false;
    svc=s->virt_service;
    if(svc==SOCK_VIRT_X11||svc==SOCK_VIRT_WL||svc==SOCK_VIRT_DBUS)return true;
    if(s->peer>=0&&s->peer<SOCK_MAX&&g_sockets[s->peer].used){
        svc=g_sockets[s->peer].virt_service;
        if(svc==SOCK_VIRT_X11||svc==SOCK_VIRT_WL||svc==SOCK_VIRT_DBUS)return true;
    }
    return false;
}

static int64_t c3_socket_send_iov_coalesced(int sref,const iovec_t *iov,size_t iovcnt,int flags){
    uint8_t pkt[C3_MSG_SCRATCH_SIZE];
    size_t total=0,i;
    int64_t rc;
    for(i=0;i<iovcnt;++i){
        if(!iov[i].iov_len)continue;
        if(!iov[i].iov_base)return -EFAULT;
        if(total+iov[i].iov_len>sizeof(pkt))return -EFBIG;
        c3_memcpy(pkt+total,iov[i].iov_base,iov[i].iov_len);
        total+=iov[i].iov_len;
    }
    if(!total)return 0;
    rc=(int64_t)sock_send(sref,pkt,total,flags);
    return rc;
}

static void c3_trace_x11_read_result(int fd,int ref,size_t count,int flags,int64_t r,const void *buf,uint32_t rx_before){
    uint32_t rx_after;
    uint64_t b0=0,b1=0;
    uint32_t seq=0,len32=0;
    const uint8_t *p=(const uint8_t*)buf;
    size_t lim,i;
    socket_t *s=0;
    task_t *cur=task_current();
    int trace_svc=c3_socket_ref_virtual_service(ref);
    if(trace_svc!=SOCK_VIRT_X11&&trace_svc!=SOCK_VIRT_WL)return;
    if(g_c3_x11_read_trace_count>=256)return;
    if(r<=0)return;
    rx_after=c3_socket_rx_used(ref);
    if(ref>=0&&ref<SOCK_MAX&&g_sockets[ref].used)s=&g_sockets[ref];
    ++g_c3_x11_read_trace_count;
    if(r>0&&p){
        lim=(size_t)r;
        if(lim>8)lim=8;
        for(i=0;i<lim;++i)b0|=((uint64_t)p[i])<<(i*8);
        if((size_t)r>8){
            lim=(size_t)r-8;
            if(lim>8)lim=8;
            for(i=0;i<lim;++i)b1|=((uint64_t)p[i+8])<<(i*8);
        }
        if(r>=4)seq=(uint32_t)p[2]|((uint32_t)p[3]<<8);
        if(r>=8)len32=(uint32_t)p[4]|((uint32_t)p[5]<<8)|((uint32_t)p[6]<<16)|((uint32_t)p[7]<<24);
    }
    __boot_serial_puts(trace_svc==SOCK_VIRT_WL?"[wl-read] #":"[x11-read] #");
    __boot_serial_putu32(g_c3_x11_read_trace_count);
    __boot_serial_puts(" pid=");
    __boot_serial_putu32((uint32_t)(cur?cur->pid:0));
    c3_trace_task_name(cur);
    __boot_serial_puts(" fd=");
    __boot_serial_putu32((uint32_t)fd);
    __boot_serial_puts(" ref=");
    __boot_serial_putu32((uint32_t)ref);
    __boot_serial_puts(" cnt=");
    __boot_serial_puthex64((uint64_t)count);
    __boot_serial_puts(" flags=");
    __boot_serial_puthex64((uint64_t)(uint32_t)flags);
    __boot_serial_puts(" ret=");
    __boot_serial_puthex64((uint64_t)r);
    __boot_serial_puts(" rx_before=");
    __boot_serial_putu32(rx_before);
    __boot_serial_puts(" rx_after=");
    __boot_serial_putu32(rx_after);
    if(s){
        __boot_serial_puts(" peer=");
        __boot_serial_putu32((uint32_t)s->peer);
        __boot_serial_puts(" svc=");
        __boot_serial_putu32((uint32_t)s->virt_service);
    }
    if(r>0&&p){
        __boot_serial_puts(" type=");
        __boot_serial_putu32((uint32_t)p[0]);
        __boot_serial_puts(" detail=");
        __boot_serial_putu32((uint32_t)p[1]);
        __boot_serial_puts(" seqle=");
        __boot_serial_putu32(seq);
        __boot_serial_puts(" lenle=");
        __boot_serial_putu32(len32);
        __boot_serial_puts(" b0=");
        __boot_serial_puthex64(b0);
        __boot_serial_puts(" b1=");
        __boot_serial_puthex64(b1);
    }
    __boot_serial_puts("\n");
}

static int64_t c3_socket_recv_wait(int ref,void *buf,size_t len,int flags){
    task_t *cur=task_current();
    uint32_t waits=0;
    uint32_t rx_before;
    if(len==0)return 0;
    for(;;){
        rx_before=c3_socket_rx_used(ref);
        int64_t r=(int64_t)sock_recv(ref,buf,len,flags);
        if(r!=0){
            c3_trace_x11_read_result(-1,ref,len,flags,r,buf,rx_before);
            return r;
        }
        if(flags&C3_MSG_DONTWAIT)return -EAGAIN;
        if(c3_socket_ref_closed(ref))return 0;
        if(cur&&c3_task_has_unblocked_signal(cur))return -EINTR;
        if(waits<8){
            ++waits;
            __boot_serial_puts("[sock-wait] pid=");
            __boot_serial_putu32(cur?(uint32_t)cur->pid:0);
            c3_trace_task_name(cur);
            __boot_serial_puts(" ref=");
            __boot_serial_putu32((uint32_t)ref);
            __boot_serial_puts(" flags=");
            __boot_serial_puthex64((uint64_t)(uint32_t)flags);
            if(ref>=0&&ref<SOCK_MAX&&g_sockets[ref].used){
                socket_t *s=&g_sockets[ref];
                uint32_t rx=s->rx_head-s->rx_tail;
                __boot_serial_puts(" rx=");
                __boot_serial_putu32(rx);
                __boot_serial_puts(" peer=");
                __boot_serial_putu32((uint32_t)s->peer);
                __boot_serial_puts(" svc=");
                __boot_serial_putu32((uint32_t)s->virt_service);
                __boot_serial_puts(" st=");
                __boot_serial_putu32((uint32_t)s->tcp_state);
                __boot_serial_puts(" dom=");
                __boot_serial_putu32((uint32_t)s->domain);
                __boot_serial_puts(" type=");
                __boot_serial_putu32((uint32_t)s->type);
            }
            __boot_serial_puts("\n");
        }
        c3_poll_wait_slice(cur,0);
    }
}

/* VFS bridge: open/read/write/close over FD table */

/* Kernel VFS from kernel.c is accessed via non-static wrappers */
extern bool kvfs_read(const char *path, const uint8_t **data, uint32_t *size);
extern bool kvfs_write(const char *path, const char *data);
extern bool kvfs_write_bytes(const char *path, const uint8_t *data, uint32_t size);
extern bool kvfs_exists(const char *path);
extern bool kvfs_remove(const char *path);
extern bool kvfs_rename(const char *src,const char *dst);
extern bool kvfs_listdir(const char *dir,char *out,uint32_t out_cap);

static void c3_file_page_invalidate_path(const char *path);
static int c3_file_page_find_slot(const char *path,uint64_t page_off);
static uint64_t c3_file_page_get_frame(const char *path,const uint8_t *file_data,uint32_t file_size,uint64_t page_off);
static void c3_path_mode_remove(const char *path);
static const char *c3_vfs_open_slot_path(int ref);

#define C3_VFS_OPEN_MAX  OPEN_MAX_FILES
#define C3_PATH_SEG_MAX  64
#define C3_VFS_WRITE_MAX 8192
#define C3_VFS_FILE_MAX  65536
#define C3_DIR_MARKER    ".ridux_dir"
#define C3_DELETED_PREFIX "/tmp/.ridux-deleted-"
typedef struct {
    bool used;
    char path[VFS_PATH_MAX];
} c3_vfs_open_t;
static c3_vfs_open_t g_c3_vfs_open[C3_VFS_OPEN_MAX];
static uint8_t g_c3_vfs_file_scratch[C3_VFS_FILE_MAX];
static uint32_t g_c3_deleted_seq=1;

#define C3_MEMFD_TRACK_MAX 256
#define C3_MEMFD_PREFIX "/memfd/"
#define C3_MFD_CLOEXEC 0x0001u
#define C3_MFD_ALLOW_SEALING 0x0002u
#define C3_F_SEAL_SEAL 0x0001u
#define C3_F_SEAL_SHRINK 0x0002u
#define C3_F_SEAL_GROW 0x0004u
#define C3_F_SEAL_WRITE 0x0008u
#define C3_F_SEAL_FUTURE_WRITE 0x0010u
typedef struct {
    bool used;
    char path[VFS_PATH_MAX];
    uint32_t seals;
    uint64_t size;
} c3_memfd_track_t;
static c3_memfd_track_t g_c3_memfds[C3_MEMFD_TRACK_MAX];
static uint32_t g_c3_memfd_seq=1;

static int c3_memfd_track_find(const char *path){
    int i;
    if(!path||!path[0])return -1;
    for(i=0;i<C3_MEMFD_TRACK_MAX;++i){
        if(g_c3_memfds[i].used&&c3_strcmp(g_c3_memfds[i].path,path)==0)return i;
    }
    return -1;
}

static void c3_memfd_track_set(const char *path,uint32_t seals){
    int i,free_slot=-1;
    if(!path||!path[0])return;
    for(i=0;i<C3_MEMFD_TRACK_MAX;++i){
        if(g_c3_memfds[i].used){
            if(c3_strcmp(g_c3_memfds[i].path,path)==0){
                g_c3_memfds[i].seals=seals;
                return;
            }
        }else if(free_slot<0){
            free_slot=i;
        }
    }
    if(free_slot>=0){
        g_c3_memfds[free_slot].used=true;
        c3_strlcpy(g_c3_memfds[free_slot].path,path,sizeof(g_c3_memfds[free_slot].path));
        g_c3_memfds[free_slot].seals=seals;
        g_c3_memfds[free_slot].size=0;
    }
}

static bool c3_memfd_path_get_seals(const char *path,uint32_t *seals){
    int idx=c3_memfd_track_find(path);
    if(idx<0)return false;
    if(seals)*seals=g_c3_memfds[idx].seals;
    return true;
}

static bool c3_memfd_path_set_seals(const char *path,uint32_t seals){
    int idx=c3_memfd_track_find(path);
    if(idx<0)return false;
    g_c3_memfds[idx].seals=seals;
    return true;
}

static bool c3_memfd_path_get_size(const char *path,uint64_t *size){
    int idx=c3_memfd_track_find(path);
    if(idx<0)return false;
    if(size)*size=g_c3_memfds[idx].size;
    return true;
}

static bool c3_memfd_path_set_size(const char *path,uint64_t size){
    int idx=c3_memfd_track_find(path);
    if(idx<0)return false;
    g_c3_memfds[idx].size=size;
    return true;
}

static bool c3_memfd_path_grow_size(const char *path,uint64_t size){
    int idx=c3_memfd_track_find(path);
    if(idx<0)return false;
    if(size>g_c3_memfds[idx].size)g_c3_memfds[idx].size=size;
    return true;
}

static bool c3_memfd_fd_path(task_t *cur,int fd,const char **path){
    const char *p;
    if(path)*path=0;
    if(!cur||!fd_valid(&cur->fdt,fd)||cur->fdt.fds[fd].kind!=FDKIND_VFSFILE)return false;
    p=c3_vfs_open_slot_path(cur->fdt.fds[fd].ref);
    if(!p||!c3_starts_with(p,C3_MEMFD_PREFIX))return false;
    if(path)*path=p;
    return true;
}

static size_t c3_memfd_sparse_read(const char *path,uint64_t off,void *buf,size_t count){
    uint64_t logical=0;
    size_t done=0;
    if(!path||!buf||!c3_memfd_path_get_size(path,&logical))return 0;
    if(off>=logical)return 0;
    if((uint64_t)count>logical-off)count=(size_t)(logical-off);
    while(done<count){
        uint64_t pos=off+(uint64_t)done;
        uint64_t page_off=pos&~(uint64_t)(PAGE_SIZE-1);
        size_t in_page=(size_t)(pos-page_off);
        size_t chunk=PAGE_SIZE-in_page;
        int slot;
        if(chunk>count-done)chunk=count-done;
        slot=c3_file_page_find_slot(path,page_off);
        if(slot>=0&&g_c3_file_pages[slot].phys){
            c3_memcpy((uint8_t*)buf+done,
                      (const void*)(uintptr_t)(PHYS_TO_DMAP(g_c3_file_pages[slot].phys&~0xFFFULL)+in_page),
                      chunk);
        }else{
            c3_memset((uint8_t*)buf+done,0,chunk);
        }
        done+=chunk;
    }
    return done;
}

static int64_t c3_memfd_sparse_write(const char *path,uint64_t off,const void *buf,size_t count){
    size_t done=0;
    uint64_t end;
    if(!path||!buf||!c3_memfd_path_get_size(path,0))return -EINVAL;
    if((uint64_t)count>UINT64_MAX-off)return -EFBIG;
    end=off+(uint64_t)count;
    while(done<count){
        uint64_t pos=off+(uint64_t)done;
        uint64_t page_off=pos&~(uint64_t)(PAGE_SIZE-1);
        size_t in_page=(size_t)(pos-page_off);
        size_t chunk=PAGE_SIZE-in_page;
        uint64_t frame;
        int slot;
        if(chunk>count-done)chunk=count-done;
        frame=c3_file_page_get_frame(path,0,0,page_off);
        if(!frame)return done?(int64_t)done:-ENOMEM;
        slot=c3_file_page_find_slot(path,page_off);
        if(slot<0){
            c3_phys_ref_release(frame);
            return done?(int64_t)done:-ENOMEM;
        }
        c3_memcpy((void*)(uintptr_t)(PHYS_TO_DMAP(frame)+in_page),
                  (const uint8_t*)buf+done,chunk);
        c3_phys_ref_release(frame);
        done+=chunk;
    }
    c3_memfd_path_grow_size(path,end);
    return (int64_t)done;
}

static uint16_t c3_ntoh16(uint16_t v){return(uint16_t)((v>>8)|(v<<8));}

static int c3_vfs_open_slot_alloc(const char *path){
    int i;
    for(i=0;i<C3_VFS_OPEN_MAX;++i)if(!g_c3_vfs_open[i].used){
        g_c3_vfs_open[i].used=true;
        c3_strlcpy(g_c3_vfs_open[i].path,path,sizeof(g_c3_vfs_open[i].path));
        return i;
    }
    return -1;
}
static const char *c3_vfs_open_slot_path(int ref){
    if(ref<0||ref>=C3_VFS_OPEN_MAX||!g_c3_vfs_open[ref].used)return 0;
    return g_c3_vfs_open[ref].path;
}
static bool c3_vfs_open_slot_ref_used_anywhere(int ref){
    int ti,fi;
    if(ref<0)return false;
    for(ti=0;ti<C3_SCM_RIGHTS_MAX;++ti){
        if(!g_c3_scm_rights[ti].used)continue;
        if((g_c3_scm_rights[ti].fd.kind==FDKIND_VFSFILE||
            g_c3_scm_rights[ti].fd.kind==FDKIND_DIR)&&
           g_c3_scm_rights[ti].fd.ref==ref)
            return true;
    }
    for(ti=0;ti<TASK_MAX;++ti){
        if(!g_tasks[ti].used)continue;
        if(g_tasks[ti].state==TASK_ZOMBIE||g_tasks[ti].state==TASK_FREE)continue;
        for(fi=0;fi<TASK_FD_MAX;++fi){
            if((g_tasks[ti].fdt.fds[fi].kind==FDKIND_VFSFILE||g_tasks[ti].fdt.fds[fi].kind==FDKIND_DIR)&&
               g_tasks[ti].fdt.fds[fi].ref==ref)
                return true;
        }
    }
    return false;
}
static void c3_vfs_open_slot_try_free(int ref){
    if(ref<0||ref>=C3_VFS_OPEN_MAX)return;
    if(c3_vfs_open_slot_ref_used_anywhere(ref))return;
    if(c3_starts_with(g_c3_vfs_open[ref].path,C3_DELETED_PREFIX)){
        c3_file_page_invalidate_path(g_c3_vfs_open[ref].path);
        kvfs_remove(g_c3_vfs_open[ref].path);
        c3_path_mode_remove(g_c3_vfs_open[ref].path);
    }
    g_c3_vfs_open[ref].used=false;
    g_c3_vfs_open[ref].path[0]=0;
}
static void c3_vfs_open_slot_rename_path(const char *old_path,const char *new_path){
    int i;
    if(!old_path||!new_path)return;
    for(i=0;i<C3_VFS_OPEN_MAX;++i){
        if(!g_c3_vfs_open[i].used)continue;
        if(c3_strcmp(g_c3_vfs_open[i].path,old_path)!=0)continue;
        c3_strlcpy(g_c3_vfs_open[i].path,new_path,sizeof(g_c3_vfs_open[i].path));
    }
}

static bool c3_vfs_path_open_anywhere(const char *path){
    int i;
    if(!path||!*path)return false;
    for(i=0;i<C3_VFS_OPEN_MAX;++i){
        if(!g_c3_vfs_open[i].used)continue;
        if(c3_strcmp(g_c3_vfs_open[i].path,path)!=0)continue;
        if(c3_vfs_open_slot_ref_used_anywhere(i))return true;
    }
    return false;
}

static void c3_path_dirname(const char *path,char *out,size_t cap){
    size_t len,last=0,i;
    if(!out||cap==0)return;
    out[0]=0;
    if(!path||!path[0]){c3_strlcpy(out,"/",cap);return;}
    len=c3_strlen(path);
    for(i=0;i<len;++i)if(path[i]=='/')last=i;
    if(last==0){c3_strlcpy(out,"/",cap);return;}
    if(last>=cap)last=cap-1;
    c3_memcpy(out,path,last);
    out[last]=0;
}

static void c3_path_build(const char *base,const char *path,char *tmp,size_t cap){
    size_t l=0;
    if(!tmp||cap==0)return;
    tmp[0]=0;
    if(path&&path[0]=='/'){
        c3_strlcpy(tmp,path,cap);
        return;
    }
    if(base&&base[0])c3_append_str(tmp,&l,cap,base);
    else c3_append_ch(tmp,&l,cap,'/');
    if(l==0||tmp[l-1]!='/')c3_append_ch(tmp,&l,cap,'/');
    if(path&&path[0])c3_append_str(tmp,&l,cap,path);
}

static void c3_path_normalize_abs(const char *in,char *out,size_t cap){
    int seg_start[C3_PATH_SEG_MAX];
    int seg_len[C3_PATH_SEG_MAX];
    int segc=0;
    int i=0;
    size_t l=0;
    if(!out||cap==0)return;
    out[0]=0;
    if(!in||!in[0]){c3_strlcpy(out,"/",cap);return;}
    while(in[i]){
        int st,en,len;
        while(in[i]=='/')++i;
        if(!in[i])break;
        st=i;
        while(in[i]&&in[i]!='/')++i;
        en=i; len=en-st;
        if(len==1&&in[st]=='.')continue;
        if(len==2&&in[st]=='.'&&in[st+1]=='.'){if(segc>0)--segc;continue;}
        if(segc<C3_PATH_SEG_MAX){seg_start[segc]=st;seg_len[segc]=len;++segc;}
    }
    c3_append_ch(out,&l,cap,'/');
    for(i=0;i<segc;++i){
        int k;
        if(l>1)c3_append_ch(out,&l,cap,'/');
        for(k=0;k<seg_len[i]&&l+1<cap;++k)out[l++]=in[seg_start[i]+k];
        out[l]=0;
    }
    if(l==0){out[0]='/';out[1]=0;}
}

static void c3_path_dir_marker(const char *path,char *out,size_t cap){
    size_t l=0;
    if(!out||cap==0)return;
    out[0]=0;
    if(!path||!path[0]){c3_strlcpy(out,"/"C3_DIR_MARKER,cap);return;}
    c3_append_str(out,&l,cap,path);
    if(l==0||out[l-1]!='/')c3_append_ch(out,&l,cap,'/');
    c3_append_str(out,&l,cap,C3_DIR_MARKER);
}

static bool c3_path_has_dir_marker(const char *path){
    char marker[VFS_PATH_MAX*2];
    c3_path_dir_marker(path,marker,sizeof(marker));
    return kvfs_exists(marker);
}

static bool c3_parse_proc_pid_path(const char *path,int *pid_out,const char **tail_out){
    int pid=0;
    size_t i=6;
    bool any=false;
    if(pid_out)*pid_out=-1;
    if(tail_out)*tail_out=0;
    if(!path||!c3_starts_with(path,"/proc/"))return false;
    while(path[i]>='0'&&path[i]<='9'){
        any=true;
        pid=pid*10+(int)(path[i]-'0');
        ++i;
    }
    if(!any)return false;
    if(path[i]&&path[i]!='/')return false;
    if(pid_out)*pid_out=pid;
    if(tail_out)*tail_out=&path[i];
    return true;
}

static bool c3_parse_proc_self_task_path(const char *path,int *tid_out,const char **tail_out){
    const char *prefix="/proc/self/task/";
    size_t i=0;
    int tid=0;
    bool any=false;
    if(tid_out)*tid_out=-1;
    if(tail_out)*tail_out=0;
    if(!path)return false;
    while(prefix[i]){
        if(path[i]!=prefix[i])return false;
        ++i;
    }
    while(path[i]>='0'&&path[i]<='9'){
        any=true;
        tid=tid*10+(int)(path[i]-'0');
        ++i;
    }
    if(!any)return false;
    if(path[i]&&path[i]!='/')return false;
    if(tid_out)*tid_out=tid;
    if(tail_out)*tail_out=&path[i];
    return true;
}

static bool c3_sysfs_builtin_dir(const char *path){
    if(!path||!path[0])return false;
    if(c3_strcmp(path,"/sys")==0||c3_strcmp(path,"/sys/kernel")==0||c3_strcmp(path,"/sys/kernel/mm")==0||
       c3_strcmp(path,"/sys/kernel/mm/transparent_hugepage")==0||c3_strcmp(path,"/sys/kernel/security")==0||
       c3_strcmp(path,"/sys/kernel/debug")==0||c3_strcmp(path,"/sys/kernel/debug/tracing")==0)return true;
    if(c3_strcmp(path,"/sys/devices")==0||c3_strcmp(path,"/sys/devices/system")==0||c3_strcmp(path,"/sys/devices/system/cpu")==0||
       c3_strcmp(path,"/sys/devices/system/cpu/cpu0")==0||c3_strcmp(path,"/sys/devices/system/cpu/cpu0/topology")==0||
       c3_strcmp(path,"/sys/devices/system/cpu/cpu0/cpufreq")==0||c3_strcmp(path,"/sys/devices/system/memory")==0||
       c3_strcmp(path,"/sys/devices/system/node")==0||c3_strcmp(path,"/sys/devices/virtual")==0||
       c3_strcmp(path,"/sys/devices/virtual/dmi")==0||c3_strcmp(path,"/sys/devices/virtual/dmi/id")==0)return true;
    if(c3_strcmp(path,"/sys/class")==0||c3_strcmp(path,"/sys/class/drm")==0||c3_strcmp(path,"/sys/class/drm/card0")==0||
       c3_strcmp(path,"/sys/class/drm/card0/device")==0||c3_strcmp(path,"/sys/class/drm/card0/device/driver")==0||
       c3_strcmp(path,"/sys/class/drm/card0/device/driver/module")==0||c3_strcmp(path,"/sys/class/drm/renderD128")==0||
       c3_strcmp(path,"/sys/class/drm/renderD128/device")==0||c3_strcmp(path,"/sys/class/net")==0||
       c3_strcmp(path,"/sys/class/net/lo")==0||c3_strcmp(path,"/sys/class/net/eth0")==0||
       c3_strcmp(path,"/sys/class/graphics")==0||c3_strcmp(path,"/sys/class/graphics/fb0")==0||
       c3_strcmp(path,"/sys/class/dmi")==0||c3_strcmp(path,"/sys/class/dmi/id")==0||
       c3_strcmp(path,"/sys/class/input")==0)return true;
    if(c3_strcmp(path,"/sys/fs")==0||c3_strcmp(path,"/sys/fs/cgroup")==0||c3_strcmp(path,"/sys/fs/selinux")==0||
       c3_strcmp(path,"/sys/module")==0||c3_strcmp(path,"/sys/module/i915")==0||
       c3_strcmp(path,"/sys/module/amdgpu")==0||c3_strcmp(path,"/sys/module/nvidia")==0||
       c3_strcmp(path,"/sys/firmware")==0)return true;
    return false;
}

static bool c3_virtual_path_exists(const char *path){
    int pid=-1;
    const char *tail=0;
    if(!path||!path[0])return false;
    if(c3_starts_with(path,"/dev/"))return true;
    if(c3_starts_with(path,"/sys/"))return true;
    if(c3_starts_with(path,"/proc/")){
        if(c3_parse_proc_pid_path(path,&pid,&tail)){
            task_t *pt=c3_task_by_pid(pid);
            if(!pt)return false;
            return true;
        }
        return true;
    }
    return false;
}

static bool c3_path_is_builtin_dir(const char *path){
    int proc_pid;
    const char *proc_tail=0;
    if(!path||!path[0])return false;
    if(c3_sysfs_builtin_dir(path))return true;
    if(c3_strcmp(path,"/")==0)return true;
    if(c3_strcmp(path,"/proc")==0||c3_strcmp(path,"/proc/self")==0||c3_strcmp(path,"/proc/self/fd")==0||
       c3_strcmp(path,"/proc/net")==0||c3_strcmp(path,"/proc/sys")==0||c3_strcmp(path,"/proc/sys/kernel")==0||
       c3_strcmp(path,"/proc/sys/kernel/random")==0||c3_strcmp(path,"/proc/sys/vm")==0||
       c3_strcmp(path,"/proc/sys/fs")==0||c3_strcmp(path,"/proc/sys/fs/inotify")==0||
       c3_strcmp(path,"/proc/sys/net")==0||c3_strcmp(path,"/proc/sys/net/core")==0||
       c3_strcmp(path,"/proc/sys/net/ipv4")==0)return true;
    if(c3_strcmp(path,"/dev")==0||c3_strcmp(path,"/dev/pts")==0||c3_strcmp(path,"/dev/dri")==0)return true;
    if(c3_strcmp(path,"/tmp")==0||c3_strcmp(path,"/tmp/.X11-unix")==0||
       c3_strcmp(path,"/tmp/chromium")==0||c3_strcmp(path,"/tmp/chromium/Default")==0||
       c3_strcmp(path,"/tmp/chromium/BrowserMetrics")==0||c3_strcmp(path,"/tmp/chromium/Crash Reports")==0||
       c3_strcmp(path,"/tmp/chromium-profile")==0||c3_strcmp(path,"/tmp/chromium-profile/Default")==0||
       c3_strcmp(path,"/tmp/chromium-profile/BrowserMetrics")==0||c3_strcmp(path,"/tmp/chromium-profile/Crash Reports")==0)return true;
    if(c3_strcmp(path,"/run")==0||c3_strcmp(path,"/run/user")==0||c3_strcmp(path,"/run/user/0")==0||
       c3_strcmp(path,"/run/dbus")==0)return true;
    if(c3_strcmp(path,"/home")==0||c3_strcmp(path,"/etc")==0)return true;
    if(c3_strcmp(path,"/usr")==0||c3_strcmp(path,"/usr/bin")==0||c3_strcmp(path,"/usr/lib")==0||
       c3_strcmp(path,"/usr/lib64")==0||c3_strcmp(path,"/usr/local")==0||c3_strcmp(path,"/usr/local/lib")==0)return true;
    if(c3_strcmp(path,"/usr/share")==0||c3_strcmp(path,"/usr/share/X11")==0||
       c3_strcmp(path,"/usr/share/X11/xkb")==0||c3_strcmp(path,"/usr/share/X11/xkb/rules")==0||
       c3_strcmp(path,"/usr/share/X11/xkb/keycodes")==0||c3_strcmp(path,"/usr/share/X11/xkb/symbols")==0||
       c3_strcmp(path,"/usr/share/X11/xkb/types")==0||c3_strcmp(path,"/usr/share/X11/xkb/compat")==0||
       c3_strcmp(path,"/usr/share/X11/xkb/geometry")==0)return true;
    if(c3_strcmp(path,"/lib")==0||c3_strcmp(path,"/lib64")==0||
       c3_strcmp(path,"/lib/x86_64-linux-gnu")==0||c3_strcmp(path,"/usr/lib/x86_64-linux-gnu")==0)return true;
    if(c3_strcmp(path,"/opt")==0||c3_strcmp(path,"/opt/google")==0||c3_strcmp(path,"/opt/google/chrome")==0||
       c3_strcmp(path,"/opt/chromium")==0||c3_strcmp(path,"/memfd")==0)return true;
    if(c3_parse_proc_pid_path(path,&proc_pid,&proc_tail)){
        task_t *pt=c3_task_by_pid(proc_pid);
        if(!pt)return false;
        if(!proc_tail||proc_tail[0]==0)return true;
        if(c3_strcmp(proc_tail,"/fd")==0)return true;
    }
    {
        int task_tid=-1;
        const char *task_tail=0;
        if(c3_strcmp(path,"/proc/self/task")==0)return true;
        if(c3_parse_proc_self_task_path(path,&task_tid,&task_tail)){
            task_t *tt=c3_task_by_pid(task_tid);
            if(!tt)return false;
            if(!task_tail||task_tail[0]==0)return true;
            if(c3_strcmp(task_tail,"/fd")==0)return true;
        }
    }
    return false;
}

static bool c3_path_is_dir(const char *path){
    char list[1024];
    if(!path||!path[0])return false;
    if(c3_path_is_builtin_dir(path))return true;
    if(c3_path_has_dir_marker(path))return true;
    list[0]=0;
    if(kvfs_listdir(path,list,sizeof(list))&&list[0])return true;
    return false;
}

static bool c3_dirent_is_marker(const char *name){
    return name&&c3_strcmp(name,C3_DIR_MARKER)==0;
}

static int c3_resolve_user_path(task_t *cur,int dirfd,const char *path,char *out,size_t cap){
    char base[VFS_PATH_MAX];
    char tmp[VFS_PATH_MAX*2];
    if(!cur||!path||!out||cap==0)return -EFAULT;
    if(path[0]=='/'){
        c3_path_normalize_abs(path,out,cap);
        return 0;
    }
    if(dirfd==AT_FDCWD){
        c3_strlcpy(base,cur->cwd,sizeof(base));
    }else{
        const char *fdp=0;
        uint8_t kind;
        if(!fd_valid(&cur->fdt,dirfd))return -EBADF;
        kind=cur->fdt.fds[dirfd].kind;
        if(kind!=FDKIND_VFSFILE&&kind!=FDKIND_DIR)return -ENOTDIR;
        fdp=c3_vfs_open_slot_path(cur->fdt.fds[dirfd].ref);
        if(!fdp)return -EBADF;
        if(kind==FDKIND_DIR)c3_strlcpy(base,fdp,sizeof(base));
        else c3_path_dirname(fdp,base,sizeof(base));
    }
    c3_path_build(base,path,tmp,sizeof(tmp));
    c3_path_normalize_abs(tmp,out,cap);
    return 0;
}

static int c3_parse_fd_number_tail(const char *s,int *fd_out){
    int fd=0;
    bool any=false;
    if(fd_out)*fd_out=-1;
    if(!s||!*s)return -EINVAL;
    while(*s>='0'&&*s<='9'){
        any=true;
        fd=fd*10+(int)(*s-'0');
        if(fd>=TASK_FD_MAX)return -EBADF;
        ++s;
    }
    if(!any)return -EINVAL;
    if(*s)return -EINVAL;
    if(fd_out)*fd_out=fd;
    return 0;
}

static int c3_proc_fd_target(task_t *cur,const char *path,task_t **owner_out,int *fd_out){
    const char *self_prefix="/proc/self/fd/";
    int proc_pid=-1;
    const char *proc_tail=0;
    task_t *owner=0;
    int fd=-1;
    int rc;
    if(owner_out)*owner_out=0;
    if(fd_out)*fd_out=-1;
    if(!cur||!path)return 0;
    if(c3_starts_with(path,self_prefix)){
        owner=cur;
        rc=c3_parse_fd_number_tail(path+c3_strlen(self_prefix),&fd);
        if(rc<0)return rc;
    }else if(c3_parse_proc_pid_path(path,&proc_pid,&proc_tail)&&proc_tail&&c3_starts_with(proc_tail,"/fd/")){
        owner=c3_task_by_pid(proc_pid);
        if(!owner)return -ESRCH;
        rc=c3_parse_fd_number_tail(proc_tail+4,&fd);
        if(rc<0)return rc;
    }else{
        return 0;
    }
    if(fd<0||fd>=TASK_FD_MAX||!fd_valid(&owner->fdt,fd))return -EBADF;
    if(owner_out)*owner_out=owner;
    if(fd_out)*fd_out=fd;
    return 1;
}

static int c3_sockaddr_ip_port(const void *addr,uint32_t addrlen,uint32_t *ip,uint16_t *port){
    const c3_sockaddr_in_t *sa;
    if(ip)*ip=0;
    if(port)*port=0;
    if(!addr||addrlen<sizeof(c3_sockaddr_in_t))return -EINVAL;
    sa=(const c3_sockaddr_in_t*)addr;
    if(sa->sin_family!=2)return -EINVAL;
    if(ip)*ip=sa->sin_addr;
    if(port)*port=c3_ntoh16(sa->sin_port);
    return 0;
}

static int c3_sockaddr_un_path(const void *addr,uint32_t addrlen,char *out,size_t out_cap){
    const c3_sockaddr_un_t *su;
    uint32_t copy_n;
    uint32_t i,j;
    if(!out||out_cap==0)return -EINVAL;
    out[0]=0;
    if(!addr||addrlen<sizeof(uint16_t))return -EINVAL;
    su=(const c3_sockaddr_un_t*)addr;
    if(su->sun_family!=1)return -EINVAL;
    copy_n=addrlen>sizeof(uint16_t)?(addrlen-sizeof(uint16_t)):0;
    if(copy_n>sizeof(su->sun_path))copy_n=sizeof(su->sun_path);
    if(copy_n>0){
        if(su->sun_path[0]==0){
            out[0]='@';
            j=1;
            for(i=1;i<copy_n&&j+1<out_cap;++i){
                char ch=su->sun_path[i];
                if(ch==0)break;
                out[j++]=ch;
            }
            out[j]=0;
            if(out[1]==0)c3_strlcpy(out,"@abstract",out_cap);
        }else{
            if((size_t)copy_n>=out_cap)copy_n=(uint32_t)(out_cap-1);
            c3_memcpy(out,su->sun_path,copy_n);
            out[copy_n]=0;
        }
        return 0;
    }
    c3_strlcpy(out,"@abstract",out_cap);
    return 0;
}

static void c3_unix_path_to_loopback(const char *upath,uint32_t *ip,uint16_t *port){
    const char *p;
    if(ip)*ip=0x7F000001u;
    if(port)*port=39000;
    if(!upath)return;
    if(c3_starts_with(upath,"@")){
        if(c3_has_token(upath,"X11-unix")){if(port)*port=6000;return;}
        if(c3_starts_with(upath,"@wayland-proxy-")||c3_has_token(upath,"/wayland-proxy-")){if(port)*port=39099;return;}
        if(c3_starts_with(upath,"@wayland-")||c3_has_token(upath,"/wayland-")){if(port)*port=39010;return;}
        if(c3_has_token(upath,"dbus")||c3_has_token(upath,"bus")){if(port)*port=39020;return;}
    }
    if(c3_starts_with(upath,"/tmp/.X11-unix/")){
        if(port)*port=6000;
        return;
    }
    if(c3_starts_with(upath,"/run/user/")){
        p=upath+10;
        while(*p>='0'&&*p<='9')++p;
        if(*p=='/'&&c3_starts_with(p+1,"wayland-proxy-")){
            if(port)*port=39099;
            return;
        }
        if(*p=='/'&&c3_starts_with(p+1,"wayland-")){
            if(port)*port=39010;
            return;
        }
        if(*p=='/'&&(c3_starts_with(p+1,"bus")||c3_starts_with(p+1,"dbus-"))){
            if(port)*port=39020;
            return;
        }
    }
    if(c3_starts_with(upath,"/run/wayland/wayland-")){
        if(port)*port=39010;
        return;
    }
    if(c3_starts_with(upath,"/run/dbus/")||c3_starts_with(upath,"/tmp/dbus-")||c3_has_token(upath,"/bus")){
        if(port)*port=39020;
        return;
    }
    if(port){
        uint32_t h=2166136261u;
        while(*upath){
            h^=(uint8_t)(*upath++);
            h*=16777619u;
        }
        *port=(uint16_t)(20000u+(h%18000u));
    }
}

static bool c3_is_known_ipc_socket_path(const char *path){
    const char *p;
    if(!path||!*path)return false;
    if(c3_starts_with(path,"/tmp/.X11-unix/X"))return true;
    if(c3_starts_with(path,"/run/dbus/")||c3_starts_with(path,"/tmp/dbus-"))return true;
    if(c3_starts_with(path,"/run/user/")){
        p=path+10;
        while(*p>='0'&&*p<='9')++p;
        if(*p=='/'&&c3_starts_with(p+1,"wayland-"))return true;
        if(*p=='/'&&(c3_starts_with(p+1,"bus")||c3_starts_with(p+1,"dbus-")))return true;
    }
    return false;
}

static int c3_sockaddr_target(const void *addr,uint32_t addrlen,uint32_t *ip,uint16_t *port){
    char upath[128];
    if(c3_sockaddr_ip_port(addr,addrlen,ip,port)==0)return 0;
    if(c3_sockaddr_un_path(addr,addrlen,upath,sizeof(upath))==0){
        c3_unix_path_to_loopback(upath,ip,port);
        return 0;
    }
    return -EINVAL;
}

static uint32_t c3_socket_used_count(void){
    uint32_t n=0;
    int i;
    for(i=0;i<SOCK_MAX;++i)if(g_sockets[i].used)++n;
    return n;
}

static void c3_trace_unix_socket_addr(const char *op,int fd,int ref,const char *path,uint16_t port,int64_t rc){
    static uint32_t trace_count=0;
    if(trace_count>=32)return;
    ++trace_count;
    __boot_serial_puts("[unix-");
    __boot_serial_puts(op?op:"?");
    __boot_serial_puts("] fd=");
    __boot_serial_putu32(fd<0?0xFFFFFFFFu:(uint32_t)fd);
    __boot_serial_puts(" pid=");
    __boot_serial_putu32((uint32_t)(task_current()?task_current()->pid:0));
    __boot_serial_puts(" ref=");
    __boot_serial_putu32(ref<0?0xFFFFFFFFu:(uint32_t)ref);
    __boot_serial_puts(" port=");
    __boot_serial_putu32((uint32_t)port);
    __boot_serial_puts(" used=");
    __boot_serial_putu32(c3_socket_used_count());
    __boot_serial_puts(" rc=");
    if(rc<0){
        __boot_serial_puts("-");
        __boot_serial_putu32((uint32_t)(-rc));
    }else{
        __boot_serial_puthex64((uint64_t)rc);
    }
    __boot_serial_puts(" path=");
    __boot_serial_puts(path&&path[0]?path:"(empty)");
    __boot_serial_puts("\n");
}

/* Resolve /dev, /proc, /sys paths to FD kinds */
static uint8_t resolve_dev_kind(const char *path){
    if(c3_strcmp(path,"/dev/null")==0)return FDKIND_DEVNULL;
    if(c3_strcmp(path,"/dev/zero")==0)return FDKIND_DEVZERO;
    if(c3_strcmp(path,"/dev/random")==0||c3_strcmp(path,"/dev/urandom")==0)return FDKIND_DEVRANDOM;
    if(c3_strcmp(path,"/dev/fb0")==0)return FDKIND_DEVFB;
    if(c3_strcmp(path,"/dev/dri/card0")==0||c3_strcmp(path,"/dev/dri/renderD128")==0)return FDKIND_DEVFB;
    if(c3_strcmp(path,"/dev/tty")==0||c3_strcmp(path,"/dev/tty0")==0||c3_strcmp(path,"/dev/console")==0)return FDKIND_DEVTTY;
    if(c3_strcmp(path,"/dev/ptmx")==0||c3_starts_with(path,"/dev/pts/"))return FDKIND_DEVPTMX;
    if(c3_starts_with(path,"/dev/stdin"))return FDKIND_DEVTTY;
    if(c3_starts_with(path,"/dev/stdout"))return FDKIND_DEVTTY;
    if(c3_starts_with(path,"/dev/stderr"))return FDKIND_DEVTTY;
    if(c3_starts_with(path,"/proc/"))return FDKIND_PROC;
    if(c3_starts_with(path,"/sys/"))return FDKIND_PROC;
    return FDKIND_NONE;
}

static char c3_proc_task_state_char(const task_t *t){
    if(!t)return 'R';
    switch(t->state){
        case TASK_SLEEPING: return 'S';
        case TASK_STOPPED:  return 'T';
        case TASK_ZOMBIE:   return 'Z';
        case TASK_FREE:     return 'X';
        default:            return 'R';
    }
}

static uint32_t c3_proc_task_thread_count(const task_t *t){
    int i;
    uint32_t n=0;
    int tgid;
    if(!t)return 1;
    tgid=t->tgid?t->tgid:t->pid;
    for(i=0;i<TASK_MAX;++i){
        if(!g_tasks[i].used)continue;
        if(g_tasks[i].state==TASK_FREE||g_tasks[i].state==TASK_ZOMBIE)continue;
        if((g_tasks[i].tgid?g_tasks[i].tgid:g_tasks[i].pid)==tgid)++n;
    }
    return n?n:1;
}

static int c3_proc_append_task_status(char *buf,int mx,const task_t *t){
    size_t l=0;
    if(!t)return -ESRCH;
    buf[0]=0;
    c3_append_str(buf,&l,(size_t)mx,"Name:\t");c3_append_str(buf,&l,(size_t)mx,t->name);
    c3_append_str(buf,&l,(size_t)mx,"\nState:\t");
    c3_append_ch(buf,&l,(size_t)mx,c3_proc_task_state_char(t));
    c3_append_str(buf,&l,(size_t)mx," (running)\nTgid:\t");c3_append_u32(buf,&l,(size_t)mx,(uint32_t)(t->tgid?t->tgid:t->pid));
    c3_append_str(buf,&l,(size_t)mx,"\nPid:\t");c3_append_u32(buf,&l,(size_t)mx,(uint32_t)t->pid);
    c3_append_str(buf,&l,(size_t)mx,"\nPPid:\t");c3_append_u32(buf,&l,(size_t)mx,(uint32_t)t->ppid);
    c3_append_str(buf,&l,(size_t)mx,"\nUid:\t");c3_append_u32(buf,&l,(size_t)mx,(uint32_t)t->uid);
    c3_append_str(buf,&l,(size_t)mx,"\t");c3_append_u32(buf,&l,(size_t)mx,(uint32_t)t->euid);
    c3_append_str(buf,&l,(size_t)mx,"\t");c3_append_u32(buf,&l,(size_t)mx,(uint32_t)t->uid);
    c3_append_str(buf,&l,(size_t)mx,"\t");c3_append_u32(buf,&l,(size_t)mx,(uint32_t)t->euid);
    c3_append_str(buf,&l,(size_t)mx,"\nGid:\t");c3_append_u32(buf,&l,(size_t)mx,(uint32_t)t->gid);
    c3_append_str(buf,&l,(size_t)mx,"\t");c3_append_u32(buf,&l,(size_t)mx,(uint32_t)t->egid);
    c3_append_str(buf,&l,(size_t)mx,"\t");c3_append_u32(buf,&l,(size_t)mx,(uint32_t)t->gid);
    c3_append_str(buf,&l,(size_t)mx,"\t");c3_append_u32(buf,&l,(size_t)mx,(uint32_t)t->egid);
    c3_append_str(buf,&l,(size_t)mx,"\nVmPeak:\t262144 kB\nVmSize:\t262144 kB\nVmRSS:\t32768 kB\nRssAnon:\t24576 kB\nRssFile:\t8192 kB\n");
    c3_append_str(buf,&l,(size_t)mx,"FDSize:\t512\nThreads:\t");c3_append_u32(buf,&l,(size_t)mx,c3_proc_task_thread_count(t));
    c3_append_str(buf,&l,(size_t)mx,"\nSigQ:\t0/256\n");
    return (int)l;
}

static int c3_proc_append_task_stat(char *buf,int mx,const task_t *t){
    size_t l=0;
    uint64_t startstack=C3_USER_STACK_TOP;
    uint64_t arg_start=C3_USER_STACK_TOP-0x3000ULL;
    uint64_t arg_end=C3_USER_STACK_TOP-0x2800ULL;
    uint64_t env_start=C3_USER_STACK_TOP-0x2800ULL;
    uint64_t env_end=C3_USER_STACK_TOP-0x1800ULL;
    uint64_t startcode=t&&t->entry_point?(t->entry_point&~0x0FFFFFFFULL):0x40000000ULL;
    uint64_t endcode=startcode+0x20000000ULL;
    if(!t)return -ESRCH;
    if(!startcode)startcode=0x40000000ULL;
    buf[0]=0;
#define C3_PROC_STAT_U(_v) do{c3_append_ch(buf,&l,(size_t)mx,' ');c3_append_u64(buf,&l,(size_t)mx,(uint64_t)(_v));}while(0)
#define C3_PROC_STAT_I(_v) do{c3_append_ch(buf,&l,(size_t)mx,' ');c3_append_i64(buf,&l,(size_t)mx,(int64_t)(_v));}while(0)
    c3_append_u32(buf,&l,(size_t)mx,(uint32_t)t->pid);
    c3_append_str(buf,&l,(size_t)mx," (");
    c3_append_str(buf,&l,(size_t)mx,t->name[0]?t->name:"chrome");
    c3_append_str(buf,&l,(size_t)mx,") ");
    c3_append_ch(buf,&l,(size_t)mx,c3_proc_task_state_char(t));
    C3_PROC_STAT_I(t->ppid);
    C3_PROC_STAT_I(t->pgid?t->pgid:t->pid);
    C3_PROC_STAT_I(t->sid?t->sid:t->pid);
    C3_PROC_STAT_I(0);
    C3_PROC_STAT_I(0);
    C3_PROC_STAT_U(4194304ULL);
    C3_PROC_STAT_U(1024);
    C3_PROC_STAT_U(0);
    C3_PROC_STAT_U(0);
    C3_PROC_STAT_U(0);
    C3_PROC_STAT_U(t->cpu_time);
    C3_PROC_STAT_U(0);
    C3_PROC_STAT_I(0);
    C3_PROC_STAT_I(0);
    C3_PROC_STAT_I(20+(t->nice<0?t->nice:0));
    C3_PROC_STAT_I(t->nice);
    C3_PROC_STAT_U(c3_proc_task_thread_count(t));
    C3_PROC_STAT_I(0);
    C3_PROC_STAT_U(1);
    C3_PROC_STAT_U(268435456ULL);
    C3_PROC_STAT_I(8192);
    C3_PROC_STAT_U(0xFFFFFFFFFFFFFFFFULL);
    C3_PROC_STAT_U(startcode);
    C3_PROC_STAT_U(endcode);
    C3_PROC_STAT_U(startstack);
    C3_PROC_STAT_U(0);
    C3_PROC_STAT_U(0);
    C3_PROC_STAT_U(0);
    C3_PROC_STAT_U(0);
    C3_PROC_STAT_U(0);
    C3_PROC_STAT_U(0);
    C3_PROC_STAT_U(0);
    C3_PROC_STAT_U(0);
    C3_PROC_STAT_U(0);
    C3_PROC_STAT_I(17);
    C3_PROC_STAT_I(0);
    C3_PROC_STAT_U(0);
    C3_PROC_STAT_U(0);
    C3_PROC_STAT_U(0);
    C3_PROC_STAT_U(0);
    C3_PROC_STAT_I(0);
    C3_PROC_STAT_U(startcode);
    C3_PROC_STAT_U(t->brk_start?t->brk_start:0x800000ULL);
    C3_PROC_STAT_U(t->brk_start?t->brk_start:0x800000ULL);
    C3_PROC_STAT_U(arg_start);
    C3_PROC_STAT_U(arg_end);
    C3_PROC_STAT_U(env_start);
    C3_PROC_STAT_U(env_end);
    C3_PROC_STAT_I(t->exit_code);
    c3_append_ch(buf,&l,(size_t)mx,'\n');
#undef C3_PROC_STAT_U
#undef C3_PROC_STAT_I
    return (int)l;
}

/* Generate /proc content on-the-fly */
static int proc_generate(const char *path, char *buf, int mx){
    size_t l=0; buf[0]=0;
    task_t *cur=task_current();
    int req_pid=-1;
    const char *pid_tail=0;
    int task_tid=-1;
    const char *task_tail=0;
    if(c3_parse_proc_self_task_path(path,&task_tid,&task_tail)){
        task_t *tt=c3_task_by_pid(task_tid);
        if(!tt)return -ESRCH;
        if(!task_tail||task_tail[0]==0){
            c3_append_str(buf,&l,(size_t)mx,"status\nstat\ncomm\nmaps\nstatm\ncmdline\nfd\n");
            return(int)l;
        }
        if(c3_strcmp(task_tail,"/status")==0)return c3_proc_append_task_status(buf,mx,tt);
        if(c3_strcmp(task_tail,"/stat")==0)return c3_proc_append_task_stat(buf,mx,tt);
        if(c3_strcmp(task_tail,"/comm")==0){
            c3_append_str(buf,&l,(size_t)mx,tt->name);
            c3_append_ch(buf,&l,(size_t)mx,'\n');
            return(int)l;
        }
        if(c3_strcmp(task_tail,"/cmdline")==0){
            c3_append_str(buf,&l,(size_t)mx,tt->name);
            c3_append_ch(buf,&l,(size_t)mx,'\0');
            return(int)l;
        }
        if(c3_strcmp(task_tail,"/maps")==0||c3_strcmp(task_tail,"/smaps")==0){
            return c3_proc_self_maps(buf,mx,tt);
        }
        if(c3_strcmp(task_tail,"/statm")==0){
            c3_append_str(buf,&l,(size_t)mx,"16384 8192 4096 2048 0 2048 0\n");
            return(int)l;
        }
        if(c3_strcmp(task_tail,"/limits")==0){
            c3_append_str(buf,&l,(size_t)mx,
                "Limit                     Soft Limit           Hard Limit           Units\n"
                "Max stack size            8388608              unlimited            bytes\n"
                "Max open files            1024                 1024                 files\n");
            return(int)l;
        }
    }
    if(c3_parse_proc_pid_path(path,&req_pid,&pid_tail)&&pid_tail&&pid_tail[0]=='/'){
        task_t *pt=c3_task_by_pid(req_pid);
        if(!pt)return -ESRCH;
        if(req_pid==cur->pid){
            char self_alias[128];
            size_t al=0;
            self_alias[0]=0;
            c3_append_str(self_alias,&al,sizeof(self_alias),"/proc/self");
            c3_append_str(self_alias,&al,sizeof(self_alias),pid_tail);
            return proc_generate(self_alias,buf,mx);
        }
        if(c3_strcmp(pid_tail,"/status")==0)return c3_proc_append_task_status(buf,mx,pt);
        if(c3_strcmp(pid_tail,"/stat")==0)return c3_proc_append_task_stat(buf,mx,pt);
        if(c3_strcmp(pid_tail,"/comm")==0){
            c3_append_str(buf,&l,(size_t)mx,pt->name);
            c3_append_ch(buf,&l,(size_t)mx,'\n');
            return(int)l;
        }
        if(c3_strcmp(pid_tail,"/cmdline")==0){
            c3_append_str(buf,&l,(size_t)mx,pt->name);c3_append_ch(buf,&l,(size_t)mx,'\0');
            return(int)l;
        }
        if(c3_strcmp(pid_tail,"/maps")==0||c3_strcmp(pid_tail,"/smaps")==0){
            return c3_proc_self_maps(buf,mx,pt);
        }
        if(c3_strcmp(pid_tail,"/statm")==0){
            c3_append_str(buf,&l,(size_t)mx,"16384 8192 4096 2048 0 2048 0\n");
            return(int)l;
        }
        if(c3_strcmp(pid_tail,"/cgroup")==0){
            c3_append_str(buf,&l,(size_t)mx,"0::/\n");
            return(int)l;
        }
        if(c3_strcmp(pid_tail,"/limits")==0){
            c3_append_str(buf,&l,(size_t)mx,
                "Limit                     Soft Limit           Hard Limit           Units\n"
                "Max open files            1024                 1024                 files\n"
                "Max processes             256                  256                  processes\n");
            return(int)l;
        }
        if(c3_strcmp(pid_tail,"/io")==0){
            c3_append_str(buf,&l,(size_t)mx,
                "rchar: 262144\nwchar: 65536\nsyscr: 1024\nsyscw: 256\n"
                "read_bytes: 131072\nwrite_bytes: 32768\ncancelled_write_bytes: 0\n");
            return(int)l;
        }
        if(c3_strcmp(pid_tail,"/environ")==0){
            static const char env_blob[]=
                "USER=root\0HOME=/root\0PATH=/usr/bin:/bin\0LANG=en_US.UTF-8\0";
            size_t n=sizeof(env_blob)-1;
            if((int)n>mx)n=(size_t)mx;
            c3_memcpy(buf,env_blob,n);
            return (int)n;
        }
        if(c3_strcmp(pid_tail,"/exe")==0){
            const char *ep=c3_task_exec_path(pt);
            if(!ep)ep="/usr/bin/browser";
            c3_append_str(buf,&l,(size_t)mx,ep);
            return (int)l;
        }
    }
    if(c3_strcmp(path,"/proc/self/status")==0){
        return c3_proc_append_task_status(buf,mx,cur);
    } else if(c3_strcmp(path,"/proc/self/stat")==0){
        return c3_proc_append_task_stat(buf,mx,cur);
    } else if(c3_strcmp(path,"/proc/self/comm")==0){
        c3_append_str(buf,&l,(size_t)mx,cur->name);
        c3_append_ch(buf,&l,(size_t)mx,'\n');
    } else if(c3_strcmp(path,"/proc/self/maps")==0){
        return c3_proc_self_maps(buf,mx,cur);
    } else if(c3_strcmp(path,"/proc/self/smaps")==0){
        c3_append_str(buf,&l,(size_t)mx,
            "00400000-00452000 r-xp 00000000 00:00 0 /usr/bin/browser\n"
            "Size:                328 kB\nRss:                 220 kB\nPss:                 220 kB\nShared_Clean:          0 kB\nPrivate_Dirty:       220 kB\n"
            "VmFlags: rd ex mr mw me dw\n"
            "00651000-00652000 r--p 00051000 00:00 0 /usr/bin/browser\n"
            "Size:                  4 kB\nRss:                   4 kB\nPss:                   4 kB\n"
            "VmFlags: rd mr mw me ac\n");
    } else if(c3_strcmp(path,"/proc/self/statm")==0){
        c3_append_str(buf,&l,(size_t)mx,"16384 8192 4096 2048 0 2048 0\n");
    } else if(c3_strcmp(path,"/proc/self/cmdline")==0){
        c3_append_str(buf,&l,(size_t)mx,cur->name);c3_append_ch(buf,&l,(size_t)mx,'\0');
    } else if(c3_strcmp(path,"/proc/self/environ")==0){
        static const char env_blob[]=
            "USER=root\0HOME=/root\0PATH=/usr/bin:/bin\0LANG=en_US.UTF-8\0LC_ALL=en_US.UTF-8\0"
            "DISPLAY=:0\0XDG_RUNTIME_DIR=/run/user/0\0DBUS_SESSION_BUS_ADDRESS=unix:path=/run/user/0/bus\0";
        size_t n=sizeof(env_blob)-1;
        if((int)n>mx)n=(size_t)mx;
        c3_memcpy(buf,env_blob,n);
        return (int)n;
    } else if(c3_strcmp(path,"/proc/self/exe")==0){
        const char *ep=c3_task_exec_path(cur);
        if(!ep)ep="/usr/bin/browser";
        c3_append_str(buf,&l,(size_t)mx,ep);
    } else if(c3_strcmp(path,"/proc/self/cgroup")==0){
        c3_append_str(buf,&l,(size_t)mx,"0::/\n");
    } else if(c3_strcmp(path,"/proc/self/limits")==0){
        c3_append_str(buf,&l,(size_t)mx,
            "Limit                     Soft Limit           Hard Limit           Units\n"
            "Max cpu time              unlimited            unlimited            seconds\n"
            "Max file size             unlimited            unlimited            bytes\n"
            "Max data size             unlimited            unlimited            bytes\n"
            "Max stack size            8388608              unlimited            bytes\n"
            "Max core file size        0                    0                    bytes\n"
            "Max resident set          unlimited            unlimited            bytes\n"
            "Max processes             256                  256                  processes\n"
            "Max open files            1024                 1024                 files\n");
    } else if(c3_strcmp(path,"/proc/self/io")==0){
        c3_append_str(buf,&l,(size_t)mx,
            "rchar: 1048576\nwchar: 262144\nsyscr: 4096\nsyscw: 1024\n"
            "read_bytes: 524288\nwrite_bytes: 131072\ncancelled_write_bytes: 0\n");
    } else if(c3_strcmp(path,"/proc/self/auxv")==0){
        uint64_t auxv[48];
        size_t n=0;
        c3_memset(auxv,0,sizeof(auxv));
        auxv[n++]=AT_PHDR; auxv[n++]=cur->aux_at_phdr;
        auxv[n++]=AT_PHENT; auxv[n++]=cur->aux_at_phent;
        auxv[n++]=AT_PHNUM; auxv[n++]=cur->aux_at_phnum;
        auxv[n++]=AT_PAGESZ; auxv[n++]=PAGE_SIZE;
        auxv[n++]=AT_BASE; auxv[n++]=cur->aux_at_base;
        auxv[n++]=AT_FLAGS; auxv[n++]=cur->aux_at_flags;
        auxv[n++]=AT_ENTRY; auxv[n++]=cur->aux_at_entry ? cur->aux_at_entry : cur->entry_point;
        auxv[n++]=AT_UID; auxv[n++]=cur->aux_at_uid;
        auxv[n++]=AT_EUID; auxv[n++]=cur->aux_at_euid;
        auxv[n++]=AT_GID; auxv[n++]=cur->aux_at_gid;
        auxv[n++]=AT_EGID; auxv[n++]=cur->aux_at_egid;
        auxv[n++]=AT_PLATFORM; auxv[n++]=cur->aux_at_platform;
        auxv[n++]=AT_HWCAP; auxv[n++]=0;
        auxv[n++]=AT_CLKTCK; auxv[n++]=100;
        auxv[n++]=AT_SECURE; auxv[n++]=0;
        auxv[n++]=AT_RANDOM; auxv[n++]=cur->aux_at_random;
        auxv[n++]=AT_HWCAP2; auxv[n++]=0;
        auxv[n++]=AT_EXECFN; auxv[n++]=cur->aux_at_execfn;
        auxv[n++]=AT_SYSINFO_EHDR; auxv[n++]=0;
        auxv[n++]=AT_NULL; auxv[n++]=0;
        if((int)(n*sizeof(uint64_t))>mx)return -ENOSPC;
        c3_memcpy(buf,auxv,n*sizeof(uint64_t));
        return (int)(n*sizeof(uint64_t));
    } else if(c3_strcmp(path,"/proc/cpuinfo")==0){
        return vdev_generate_proc_cpuinfo(buf,mx);
    } else if(c3_strcmp(path,"/proc/meminfo")==0){
        return vdev_generate_proc_meminfo(buf,mx);
    } else if(c3_strcmp(path,"/proc/stat")==0){
        return vdev_generate_proc_stat(buf,mx);
    } else if(c3_strcmp(path,"/proc/version")==0){
        return vdev_generate_proc_version(buf,mx);
    } else if(c3_strcmp(path,"/proc/uptime")==0){
        return vdev_generate_proc_uptime(buf,mx);
    } else if(c3_strcmp(path,"/proc/mounts")==0||c3_strcmp(path,"/proc/self/mounts")==0||c3_strcmp(path,"/proc/self/mountinfo")==0){
        return vdev_generate_proc_mounts(buf,mx);
    } else if(c3_strcmp(path,"/proc/net/dev")==0){
        return vdev_generate_proc_net_dev(buf,mx);
    } else if(c3_strcmp(path,"/proc/net/route")==0){
        c3_append_str(buf,&l,(size_t)mx,
            "Iface\tDestination\tGateway\tFlags\tRefCnt\tUse\tMetric\tMask\tMTU\tWindow\tIRTT\n"
            "eth0\t00000000\t0202000A\t0003\t0\t0\t100\t00000000\t0\t0\t0\n"
            "eth0\t0002000A\t00000000\t0001\t0\t0\t100\t00FFFFFF\t0\t0\t0\n");
    } else if(c3_strcmp(path,"/proc/net/unix")==0){
        c3_append_str(buf,&l,(size_t)mx,"Num       RefCount Protocol Flags    Type St Inode Path\n");
        c3_append_str(buf,&l,(size_t)mx,"00000000: 00000002 00000000 00010000 0001 01 1 /tmp/.X11-unix/X0\n");
        c3_append_str(buf,&l,(size_t)mx,"00000001: 00000002 00000000 00010000 0001 01 2 /run/user/0/wayland-0\n");
        c3_append_str(buf,&l,(size_t)mx,"00000002: 00000002 00000000 00010000 0001 01 3 /run/user/1000/wayland-0\n");
        c3_append_str(buf,&l,(size_t)mx,"00000003: 00000002 00000000 00010000 0001 01 4 /run/user/0/bus\n");
        c3_append_str(buf,&l,(size_t)mx,"00000004: 00000002 00000000 00010000 0001 01 5 /run/dbus/system_bus_socket\n");
    } else if(c3_strcmp(path,"/proc/sys/kernel/ostype")==0){
        c3_append_str(buf,&l,(size_t)mx,"Linux\n");
    } else if(c3_strcmp(path,"/proc/sys/kernel/hostname")==0){
        return vdev_generate_sys_kernel_hostname(buf,mx);
    } else if(c3_strcmp(path,"/proc/sys/kernel/osrelease")==0){
        return vdev_generate_sys_kernel_osrelease(buf,mx);
    } else if(c3_strcmp(path,"/proc/sys/kernel/pid_max")==0){
        c3_append_str(buf,&l,(size_t)mx,"4194304\n");
    } else if(c3_strcmp(path,"/proc/sys/kernel/threads-max")==0){
        c3_append_str(buf,&l,(size_t)mx,"131072\n");
    } else if(c3_strcmp(path,"/proc/sys/kernel/random/boot_id")==0){
        c3_append_str(buf,&l,(size_t)mx,"7f32c1d8-3e1e-4a5d-b42b-8c3d0f55a9c1\n");
    } else if(c3_strcmp(path,"/proc/sys/kernel/random/uuid")==0){
        c3_append_str(buf,&l,(size_t)mx,"f6f2d7aa-1a9c-4e5e-a2c3-0a7d9f3b7d11\n");
    } else if(c3_strcmp(path,"/proc/sys/fs/inotify/max_user_watches")==0){
        c3_append_str(buf,&l,(size_t)mx,"524288\n");
    } else if(c3_strcmp(path,"/proc/sys/fs/inotify/max_user_instances")==0){
        c3_append_str(buf,&l,(size_t)mx,"1024\n");
    } else if(c3_strcmp(path,"/proc/sys/fs/inotify/max_queued_events")==0){
        c3_append_str(buf,&l,(size_t)mx,"16384\n");
    } else if(c3_strcmp(path,"/proc/sys/vm/overcommit_memory")==0){
        c3_append_str(buf,&l,(size_t)mx,"0\n");
    } else if(c3_strcmp(path,"/proc/sys/vm/max_map_count")==0){
        c3_append_str(buf,&l,(size_t)mx,"262144\n");
    } else if(c3_strcmp(path,"/proc/sys/vm/swappiness")==0){
        c3_append_str(buf,&l,(size_t)mx,"60\n");
    } else if(c3_strcmp(path,"/proc/sys/net/core/somaxconn")==0){
        c3_append_str(buf,&l,(size_t)mx,"4096\n");
    } else if(c3_strcmp(path,"/proc/sys/net/ipv4/ip_local_port_range")==0){
        c3_append_str(buf,&l,(size_t)mx,"32768 60999\n");
    } else if(c3_strcmp(path,"/sys/kernel/ostype")==0){
        c3_append_str(buf,&l,(size_t)mx,"Linux\n");
    } else if(c3_strcmp(path,"/sys/kernel/hostname")==0){
        return vdev_generate_sys_kernel_hostname(buf,mx);
    } else if(c3_strcmp(path,"/sys/kernel/osrelease")==0){
        return vdev_generate_sys_kernel_osrelease(buf,mx);
    } else if(c3_strcmp(path,"/sys/kernel/mm/transparent_hugepage/enabled")==0){
        c3_append_str(buf,&l,(size_t)mx,"always [madvise] never\n");
    } else if(c3_strcmp(path,"/sys/kernel/mm/transparent_hugepage/defrag")==0){
        c3_append_str(buf,&l,(size_t)mx,"always defer defer+madvise [madvise] never\n");
    } else if(c3_strcmp(path,"/sys/kernel/security/lsm")==0){
        c3_append_str(buf,&l,(size_t)mx,"capability,landlock,yama,integrity\n");
    } else if(c3_strcmp(path,"/sys/kernel/debug/tracing/trace_clock")==0){
        c3_append_str(buf,&l,(size_t)mx,"local global boot [mono]\n");
    } else if(c3_strcmp(path,"/sys/devices/system/cpu/online")==0||
              c3_strcmp(path,"/sys/devices/system/cpu/possible")==0||
              c3_strcmp(path,"/sys/devices/system/cpu/present")==0){
        c3_append_str(buf,&l,(size_t)mx,"0\n");
    } else if(c3_strcmp(path,"/sys/devices/system/cpu/cpu0/topology/core_id")==0){
        c3_append_str(buf,&l,(size_t)mx,"0\n");
    } else if(c3_strcmp(path,"/sys/devices/system/cpu/cpu0/topology/physical_package_id")==0){
        c3_append_str(buf,&l,(size_t)mx,"0\n");
    } else if(c3_strcmp(path,"/sys/devices/system/cpu/cpu0/cpufreq/scaling_cur_freq")==0){
        c3_append_str(buf,&l,(size_t)mx,"3200000\n");
    } else if(c3_strcmp(path,"/sys/devices/system/memory/block_size_bytes")==0){
        c3_append_str(buf,&l,(size_t)mx,"20000000\n");
    } else if(c3_strcmp(path,"/sys/devices/system/node/online")==0){
        c3_append_str(buf,&l,(size_t)mx,"0\n");
    } else if(c3_strcmp(path,"/sys/devices/virtual/dmi/id/product_name")==0||
              c3_strcmp(path,"/sys/class/dmi/id/product_name")==0){
        c3_append_str(buf,&l,(size_t)mx,"Ridux Virtual Machine\n");
    } else if(c3_strcmp(path,"/sys/devices/virtual/dmi/id/sys_vendor")==0||
              c3_strcmp(path,"/sys/class/dmi/id/sys_vendor")==0){
        c3_append_str(buf,&l,(size_t)mx,"RiduxOS Project\n");
    } else if(c3_strcmp(path,"/sys/devices/virtual/dmi/id/board_name")==0||
              c3_strcmp(path,"/sys/class/dmi/id/board_name")==0){
        c3_append_str(buf,&l,(size_t)mx,"RiduxBoard\n");
    } else if(c3_strcmp(path,"/sys/class/drm/card0/device/vendor")==0){
        c3_append_str(buf,&l,(size_t)mx,"0x1234\n");
    } else if(c3_strcmp(path,"/sys/class/drm/card0/device/device")==0){
        c3_append_str(buf,&l,(size_t)mx,"0x1111\n");
    } else if(c3_strcmp(path,"/sys/class/drm/card0/device/subsystem_vendor")==0){
        c3_append_str(buf,&l,(size_t)mx,"0x1af4\n");
    } else if(c3_strcmp(path,"/sys/class/drm/card0/device/subsystem_device")==0){
        c3_append_str(buf,&l,(size_t)mx,"0x1100\n");
    } else if(c3_strcmp(path,"/sys/class/drm/card0/device/driver/module/version")==0){
        c3_append_str(buf,&l,(size_t)mx,"1.0.0-ridux\n");
    } else if(c3_strcmp(path,"/sys/class/drm/card0/device/uevent")==0){
        c3_append_str(buf,&l,(size_t)mx,
            "DRIVER=virtio_gpu\nPCI_CLASS=30000\nPCI_ID=1234:1111\nPCI_SUBSYS_ID=1AF4:1100\n");
    } else if(c3_strcmp(path,"/sys/class/drm/renderD128/device/uevent")==0){
        c3_append_str(buf,&l,(size_t)mx,
            "DEVNAME=dri/renderD128\nSUBSYSTEM=dri\nDEVTYPE=drm_minor\n");
    } else if(c3_strcmp(path,"/sys/class/net/lo/address")==0){
        c3_append_str(buf,&l,(size_t)mx,"00:00:00:00:00:00\n");
    } else if(c3_strcmp(path,"/sys/class/net/lo/operstate")==0){
        c3_append_str(buf,&l,(size_t)mx,"unknown\n");
    } else if(c3_strcmp(path,"/sys/class/net/lo/mtu")==0){
        c3_append_str(buf,&l,(size_t)mx,"65536\n");
    } else if(c3_strcmp(path,"/sys/class/net/eth0/address")==0){
        c3_append_str(buf,&l,(size_t)mx,"52:54:00:12:34:56\n");
    } else if(c3_strcmp(path,"/sys/class/net/eth0/operstate")==0){
        c3_append_str(buf,&l,(size_t)mx,"up\n");
    } else if(c3_strcmp(path,"/sys/class/net/eth0/mtu")==0){
        c3_append_str(buf,&l,(size_t)mx,"1500\n");
    } else if(c3_strcmp(path,"/sys/class/net/eth0/speed")==0){
        c3_append_str(buf,&l,(size_t)mx,"1000\n");
    } else if(c3_strcmp(path,"/sys/class/net/eth0/carrier")==0){
        c3_append_str(buf,&l,(size_t)mx,"1\n");
    } else if(c3_strcmp(path,"/sys/class/net/eth0/duplex")==0){
        c3_append_str(buf,&l,(size_t)mx,"full\n");
    } else if(c3_strcmp(path,"/sys/class/graphics/fb0/name")==0){
        c3_append_str(buf,&l,(size_t)mx,"riduxfb\n");
    } else if(c3_strcmp(path,"/sys/class/graphics/fb0/modes")==0){
        c3_append_str(buf,&l,(size_t)mx,"U:1024x768p-60\n");
    } else if(c3_strcmp(path,"/sys/class/graphics/fb0/virtual_size")==0){
        c3_append_str(buf,&l,(size_t)mx,"1024,768\n");
    } else if(c3_strcmp(path,"/sys/class/graphics/fb0/bits_per_pixel")==0){
        c3_append_str(buf,&l,(size_t)mx,"32\n");
    } else if(c3_strcmp(path,"/sys/fs/cgroup/cgroup.controllers")==0){
        c3_append_str(buf,&l,(size_t)mx,"cpuset cpu io memory pids\n");
    } else if(c3_strcmp(path,"/sys/fs/cgroup/cgroup.subtree_control")==0){
        c3_append_str(buf,&l,(size_t)mx,"\n");
    } else if(c3_strcmp(path,"/sys/fs/cgroup/cgroup.procs")==0){
        c3_append_u32(buf,&l,(size_t)mx,(uint32_t)cur->pid);
        c3_append_ch(buf,&l,(size_t)mx,'\n');
    } else if(c3_strcmp(path,"/sys/fs/selinux/enforce")==0){
        c3_append_str(buf,&l,(size_t)mx,"0\n");
    } else if(c3_strcmp(path,"/sys/module/i915/version")==0||
              c3_strcmp(path,"/sys/module/amdgpu/version")==0||
              c3_strcmp(path,"/sys/module/nvidia/version")==0){
        c3_append_str(buf,&l,(size_t)mx,"0.0-ridux\n");
    } else if(c3_strcmp(path,"/proc/filesystems")==0){
        c3_append_str(buf,&l,(size_t)mx,"\text2\n\text4\n\tfat32\n\tvfat\nnodev\ttmpfs\nnodev\tproc\nnodev\tsysfs\nnodev\tdevtmpfs\n");
    } else if(c3_strcmp(path,"/proc/loadavg")==0){
        c3_append_str(buf,&l,(size_t)mx,"0.10 0.05 0.01 1/32 100\n");
    } else if(c3_strcmp(path,"/proc/self/fd")==0){
        int i;for(i=0;i<TASK_FD_MAX;++i){if(cur->fdt.fds[i].kind==FDKIND_NONE)continue;
            c3_append_u32(buf,&l,(size_t)mx,(uint32_t)i);c3_append_ch(buf,&l,(size_t)mx,'\n');}
    } else {
        c3_append_str(buf,&l,(size_t)mx,"(empty)\n");
    }
    return(int)l;
}

/* Stored proc data for reads after open */
#define PROC_BUF_MAX 16
#define PROC_BUF_BYTES (256u*1024u)
typedef struct { bool used; int fd; char data[PROC_BUF_BYTES]; uint32_t size; } proc_buf_t;
static proc_buf_t g_proc_bufs[PROC_BUF_MAX];
static uint32_t g_c3_openat_trace_count=0;
static uint32_t g_c3_libxi_read_trace_count=0;
static uint32_t g_c3_profile_trace_count=0;
static uint32_t g_c3_firefox_trace_count=0;
static uint32_t g_c3_firefox_read_trace_count=0;
#ifndef C3_OPENAT_TRACE_MAX
#define C3_OPENAT_TRACE_MAX 32
#endif

static bool c3_trace_profile_path(const char *path){
    if(!path||!path[0])return false;
    if(c3_starts_with(path,"/tmp/firefox-profile"))return true;
    if(c3_starts_with(path,"/home/.mozilla"))return true;
    if(c3_strcmp(path,"/home/ridux-firefox-test.html")==0)return true;
    if(c3_starts_with(path,"/opt/firefox/distribution"))return true;
    if(c3_starts_with(path,"/home/.config/chromium"))return true;
    if(c3_starts_with(path,"/home/.cache/chromium"))return true;
    if(c3_starts_with(path,"/tmp/chromium"))return true;
    if(c3_starts_with(path,"/tmp/org.chromium"))return true;
    if(c3_starts_with(path,"/tmp/.org.chromium"))return true;
    if(c3_starts_with(path,C3_DELETED_PREFIX))return true;
    if(c3_strcmp(path,"/proc/sys/fs/inotify/max_user_watches")==0)return true;
    if(c3_has_token(path,"Singleton"))return true;
    if(c3_has_token(path,"BrowserMetrics"))return true;
    if(c3_has_token(path,"Local State"))return true;
    if(c3_has_token(path,"Preferences"))return true;
    if(c3_has_token(path,"Crashpad"))return true;
    if(c3_has_token(path,"First Run"))return true;
    if(c3_has_token(path,"policies.json"))return true;
    if(c3_has_token(path,"distribution.ini"))return true;
    if(c3_has_token(path,"prefs.js"))return true;
    if(c3_has_token(path,"user.js"))return true;
    if(c3_has_token(path,"profiles.ini"))return true;
    if(c3_has_token(path,"installs.ini"))return true;
    if(c3_has_token(path,"compatibility.ini"))return true;
    if(c3_has_token(path,"sessionstore"))return true;
    if(c3_has_token(path,"times.json"))return true;
    return false;
}

static void c3_trace_path_rc(const char *op,const char *path,int64_t rc,uint64_t a,uint64_t b){
    task_t *cur;
    if(!c3_trace_profile_path(path))return;
    if(g_c3_profile_trace_count>=512)return;
    ++g_c3_profile_trace_count;
    cur=task_current();
    __boot_serial_puts("[profile-fs] #");
    __boot_serial_putu32(g_c3_profile_trace_count);
    __boot_serial_puts(" pid=");
    __boot_serial_putu32((uint32_t)(cur?cur->pid:0));
    c3_trace_task_name(cur);
    __boot_serial_puts(" op=");
    __boot_serial_puts(op?op:"?");
    __boot_serial_puts(" a=");
    __boot_serial_puthex64(a);
    __boot_serial_puts(" b=");
    __boot_serial_puthex64(b);
    __boot_serial_puts(" rc=");
    if(rc<0){
        __boot_serial_puts("-");
        __boot_serial_putu32((uint32_t)(-rc));
    }else{
        __boot_serial_puthex64((uint64_t)rc);
    }
    __boot_serial_puts(" path=");
    __boot_serial_puts(path);
    __boot_serial_puts("\n");
}

static bool c3_trace_firefox_path_needed(const char *path){
    if(!path||!path[0])return false;
    if(c3_has_token(path,"/opt/firefox/"))return true;
    if(c3_has_token(path,"/tmp/firefox-profile"))return true;
    if(c3_has_token(path,"/home/.mozilla"))return true;
    if(c3_has_token(path,"/home/ridux-firefox-test.html"))return true;
    if(c3_has_token(path,"dependentlibs"))return true;
    if(c3_has_token(path,"libxul"))return true;
    if(c3_has_token(path,"libmoz"))return true;
    if(c3_has_token(path,"libnspr"))return true;
    if(c3_has_token(path,"libnss"))return true;
    if(c3_has_token(path,".so"))return true;
    if(c3_has_token(path,"/proc/self/exe"))return true;
    if(c3_has_token(path,"/proc/self/cwd"))return true;
    if(c3_has_token(path,"policies.json"))return true;
    if(c3_has_token(path,"distribution.ini"))return true;
    if(c3_has_token(path,"prefs.js"))return true;
    if(c3_has_token(path,"user.js"))return true;
    if(c3_has_token(path,"profiles.ini"))return true;
    if(c3_has_token(path,"installs.ini"))return true;
    if(c3_has_token(path,"compatibility.ini"))return true;
    if(c3_has_token(path,"sessionstore"))return true;
    if(c3_has_token(path,"times.json"))return true;
    return false;
}

static void c3_trace_firefox_path_rc(const char *op,const char *path,int64_t rc,uint64_t a,uint64_t b){
    task_t *cur;
    if(!c3_trace_firefox_path_needed(path))return;
    if(g_c3_firefox_trace_count>=384)return;
    cur=task_current();
    if(!c3_task_is_firefox_runtime(cur))return;
    ++g_c3_firefox_trace_count;
    __boot_serial_puts("[firefox-fs] #");
    __boot_serial_putu32(g_c3_firefox_trace_count);
    __boot_serial_puts(" pid=");
    __boot_serial_putu32((uint32_t)(cur?cur->pid:0));
    c3_trace_task_name(cur);
    __boot_serial_puts(" op=");
    __boot_serial_puts(op?op:"?");
    __boot_serial_puts(" a=");
    __boot_serial_puthex64(a);
    __boot_serial_puts(" b=");
    __boot_serial_puthex64(b);
    __boot_serial_puts(" rc=");
    if(rc<0){
        __boot_serial_puts("-");
        __boot_serial_putu32((uint32_t)(-rc));
    }else{
        __boot_serial_puthex64((uint64_t)rc);
    }
    __boot_serial_puts(" path=");
    __boot_serial_puts(path);
    __boot_serial_puts("\n");
}

static void c3_trace_firefox_read_sample(const char *path,int fd,uint64_t off,size_t count,size_t got,const void *buf){
    const uint8_t *p=(const uint8_t*)buf;
    size_t i,lim;
    task_t *cur;
    if(!path||!c3_has_token(path,"dependentlibs.list"))return;
    if(g_c3_firefox_read_trace_count>=4)return;
    cur=task_current();
    if(!c3_task_is_firefox_runtime(cur))return;
    ++g_c3_firefox_read_trace_count;
    __boot_serial_puts("[firefox-read] #");
    __boot_serial_putu32(g_c3_firefox_read_trace_count);
    __boot_serial_puts(" pid=");
    __boot_serial_putu32((uint32_t)(cur?cur->pid:0));
    __boot_serial_puts(" fd=");
    __boot_serial_putu32((uint32_t)fd);
    __boot_serial_puts(" off=");
    __boot_serial_puthex64(off);
    __boot_serial_puts(" count=");
    __boot_serial_puthex64((uint64_t)count);
    __boot_serial_puts(" got=");
    __boot_serial_puthex64((uint64_t)got);
    __boot_serial_puts(" sample=");
    lim=got<96?got:96;
    for(i=0;i<lim;++i){
        char ch=(char)p[i];
        if(ch=='\n')__boot_serial_putc('|');
        else if(ch=='\r'||ch=='\t')__boot_serial_putc(' ');
        else if(ch>=' '&&ch<='~')__boot_serial_putc(ch);
        else __boot_serial_putc('.');
    }
    __boot_serial_puts(" path=");
    __boot_serial_puts(path);
    __boot_serial_puts("\n");
}

#define C3_SYMLINK_PREFIX "RIDUX_SYMLINK:"
#define C3_PATH_MODE_MAX 2048

typedef struct {
    bool used;
    char path[VFS_PATH_MAX];
    uint32_t mode;
} c3_path_mode_t;

static c3_path_mode_t g_c3_path_modes[C3_PATH_MODE_MAX];

static int c3_path_mode_find(const char *path){
    int i;
    if(!path||!path[0])return -1;
    for(i=0;i<C3_PATH_MODE_MAX;++i){
        if(g_c3_path_modes[i].used&&c3_strcmp(g_c3_path_modes[i].path,path)==0)return i;
    }
    return -1;
}

static uint32_t c3_path_mode_apply_create(int mode,uint32_t fallback){
    uint32_t m=(mode>=0)?((uint32_t)mode&07777u):fallback;
    if(m==0)m=fallback;
    return m&~g_umask_val&07777u;
}

static uint32_t c3_path_mode_get(const char *path,uint32_t fallback){
    int idx=c3_path_mode_find(path);
    if(idx>=0)return g_c3_path_modes[idx].mode&07777u;
    return fallback&07777u;
}

static void c3_path_mode_set(const char *path,uint32_t mode){
    int i,free_slot=-1;
    if(!path||!path[0])return;
    for(i=0;i<C3_PATH_MODE_MAX;++i){
        if(g_c3_path_modes[i].used){
            if(c3_strcmp(g_c3_path_modes[i].path,path)==0){
                g_c3_path_modes[i].mode=mode&07777u;
                return;
            }
        }else if(free_slot<0){
            free_slot=i;
        }
    }
    if(free_slot>=0){
        g_c3_path_modes[free_slot].used=true;
        c3_strlcpy(g_c3_path_modes[free_slot].path,path,sizeof(g_c3_path_modes[free_slot].path));
        g_c3_path_modes[free_slot].mode=mode&07777u;
    }
}

static void c3_path_mode_remove(const char *path){
    int idx=c3_path_mode_find(path);
    if(idx<0)return;
    g_c3_path_modes[idx].used=false;
    g_c3_path_modes[idx].path[0]=0;
    g_c3_path_modes[idx].mode=0;
}

static void c3_path_mode_rename(const char *src,const char *dst){
    int idx=c3_path_mode_find(src);
    if(idx<0)return;
    c3_strlcpy(g_c3_path_modes[idx].path,dst,sizeof(g_c3_path_modes[idx].path));
}

static bool c3_kvfs_symlink_target(const char *path,const char **target,size_t *target_len){
    const uint8_t *data=0;
    uint32_t size=0;
    size_t plen=c3_strlen(C3_SYMLINK_PREFIX);
    if(!path||!target||!target_len)return false;
    if(!kvfs_read(path,&data,&size)||!data||size<plen)return false;
    if(c3_strncmp((const char*)data,C3_SYMLINK_PREFIX,plen)!=0)return false;
    *target=(const char*)data+plen;
    *target_len=(size_t)size-plen;
    return true;
}

int64_t real_sys_open(const char *path, int flags, int mode){
    task_t *cur=task_current();
    char npath[VFS_PATH_MAX];
    uint8_t kind;
    uint16_t fl=0;
    if(!path)return -EFAULT;
    if(c3_resolve_user_path(cur,AT_FDCWD,path,npath,sizeof(npath))<0)return -EINVAL;
    /* Determine FD flags */
    if((flags&O_RDWR)==O_RDWR)fl=FDFL_READABLE|FDFL_WRITABLE;
    else if(flags&O_WRONLY)fl=FDFL_WRITABLE;
    else fl=FDFL_READABLE;
    if(flags&O_APPEND)fl|=FDFL_APPEND;
    if(flags&O_NONBLOCK)fl|=FDFL_NONBLOCK;
    if(flags&O_CLOEXEC)fl|=FDFL_CLOEXEC;

    {
        task_t *fd_owner=0;
        int fdn=-1;
        int pr=c3_proc_fd_target(cur,npath,&fd_owner,&fdn);
        if(pr<0)return pr;
        if(pr>0){
            real_fd_t *src=&fd_owner->fdt.fds[fdn];
            uint16_t nf=src->flags;
            int fd;
            if((fl&FDFL_WRITABLE)&&!(src->flags&FDFL_WRITABLE))return -EACCES;
            if((fl&FDFL_READABLE)&&!(src->flags&FDFL_READABLE))return -EACCES;
            if(flags&O_NONBLOCK)nf|=FDFL_NONBLOCK;
            if(flags&O_CLOEXEC)nf|=FDFL_CLOEXEC;
            else nf&=(uint16_t)~FDFL_CLOEXEC;
            fd=c3_fd_alloc_for_task(cur,src->kind,src->ref,nf);
            if(fd<0)return -EMFILE;
            return fd;
        }
    }

    if((flags&O_TMPFILE)==O_TMPFILE&&c3_path_is_dir(npath)){
        char hidden[VFS_PATH_MAX];
        size_t hl=0;
        uint32_t seq=g_c3_deleted_seq++;
        int slot,fd;
        if(seq==0)seq=g_c3_deleted_seq++;
        hidden[0]=0;
        c3_append_str(hidden,&hl,sizeof(hidden),C3_DELETED_PREFIX);
        c3_append_u32(hidden,&hl,sizeof(hidden),(uint32_t)(cur?cur->pid:0));
        c3_append_ch(hidden,&hl,sizeof(hidden),'-');
        c3_append_u32(hidden,&hl,sizeof(hidden),seq);
        if(!kvfs_write(hidden,""))return -ENOMEM;
        c3_path_mode_set(hidden,c3_path_mode_apply_create(mode,0600));
        slot=c3_vfs_open_slot_alloc(hidden);
        if(slot<0)return -EMFILE;
        fd=c3_fd_alloc_for_task(cur,FDKIND_VFSFILE,slot,fl|FDFL_READABLE|FDFL_WRITABLE);
        if(fd<0)return -EMFILE;
        return fd;
    }

    if(c3_path_is_dir(npath)){
        int slot;
        int fd;
        if((fl&FDFL_WRITABLE)||(flags&O_TRUNC))return -EISDIR;
        slot=c3_vfs_open_slot_alloc(npath);
        if(slot<0)return -EMFILE;
        fd=c3_fd_alloc_for_task(cur,FDKIND_DIR,slot,fl|FDFL_READABLE);
        if(fd<0)return -EMFILE;
        c3_inotify_notify_path(npath,C3_IN_OPEN|C3_IN_ISDIR,0);
        return fd;
    }

    /* Check /dev, /proc, /sys */
    kind=resolve_dev_kind(npath);
    if(kind!=FDKIND_NONE){
        if(flags&O_DIRECTORY)return -ENOTDIR;
        int fd=c3_fd_alloc_for_task(cur,kind,0,fl);
        if(fd<0)return -EMFILE;
        /* For /proc files, generate content now */
        if(kind==FDKIND_PROC){
            int i;
            bool attached=false;
            for(i=0;i<PROC_BUF_MAX;++i)if(!g_proc_bufs[i].used){
                int gen;
                g_proc_bufs[i].used=true;g_proc_bufs[i].fd=fd;
                gen=proc_generate(npath,g_proc_bufs[i].data,(int)PROC_BUF_BYTES);
                if(gen<0){
                    g_proc_bufs[i].used=false;
                    g_proc_bufs[i].fd=-1;
                    g_proc_bufs[i].size=0;
                    c3_fd_group_clear_fd(cur,fd);
                    return gen;
                }
                g_proc_bufs[i].size=(uint32_t)gen;
                cur->fdt.fds[fd].ref=i;
                attached=true;
                break;
            }
            if(!attached){
                c3_fd_group_clear_fd(cur,fd);
                return -EMFILE;
            }
        }
        return fd;
    }
    /* Try kernel VFS */
    {const uint8_t *data;uint32_t size;
     if(kvfs_exists(npath)||kvfs_read(npath,&data,&size)){
         if(flags&O_DIRECTORY)return -ENOTDIR;
         if((flags&O_CREAT)&&(flags&O_EXCL))return -EEXIST;
         int slot=c3_vfs_open_slot_alloc(npath);
         int fd;
         if(slot<0)return -EMFILE;
          fd=c3_fd_alloc_for_task(cur,FDKIND_VFSFILE,slot,fl);
         if(fd<0)return -EMFILE;
        if((flags&O_TRUNC)&&(fl&FDFL_WRITABLE)){
            kvfs_write(npath,"");
            c3_inotify_notify_path(npath,C3_IN_MODIFY,0);
        }
         c3_inotify_notify_path(npath,C3_IN_OPEN,0);
         return fd;
     }
    }
    if(flags&O_DIRECTORY)return -ENOENT;
    /* O_CREAT */
    if(flags&O_CREAT){
        int slot;
        int fd;
        bool existed=kvfs_exists(npath);
        kvfs_write(npath,"");
        if(!existed)c3_path_mode_set(npath,c3_path_mode_apply_create(mode,0644));
        slot=c3_vfs_open_slot_alloc(npath);
        if(slot<0)return -EMFILE;
        fd=c3_fd_alloc_for_task(cur,FDKIND_VFSFILE,slot,fl);
        if(fd<0)return -EMFILE;
        if(!existed)c3_inotify_notify_path(npath,C3_IN_CREATE,0);
        c3_inotify_notify_path(npath,C3_IN_OPEN,0);
        return fd;
    }
    return -ENOENT;
}

int64_t real_sys_openat(int dirfd,const char *path,int flags,int mode){
    task_t *cur=task_current();
    char npath[VFS_PATH_MAX];
    int64_t rc;
    if(!path)return -EFAULT;
    if(c3_resolve_user_path(cur,dirfd,path,npath,sizeof(npath))<0)return -EINVAL;
    rc=real_sys_open(npath,flags,mode);
    c3_trace_path_rc("openat",npath,rc,(uint64_t)(uint32_t)flags,(uint64_t)(uint32_t)mode);
    if(g_c3_openat_trace_count<C3_OPENAT_TRACE_MAX){
        ++g_c3_openat_trace_count;
        __boot_serial_puts("[openat] dirfd=");
        __boot_serial_putu32((uint32_t)dirfd);
        __boot_serial_puts(" path=");
        __boot_serial_puts(npath);
        __boot_serial_puts(" flags=");
        __boot_serial_puthex64((uint64_t)flags);
        __boot_serial_puts(" -> ");
        if(rc<0){
            __boot_serial_puts("-");
            __boot_serial_putu32((uint32_t)(-rc));
        }else{
            __boot_serial_puthex64((uint64_t)rc);
        }
        __boot_serial_puts("\n");
    }
    c3_trace_firefox_path_rc("openat",npath,rc,(uint64_t)(uint32_t)flags,(uint64_t)(uint32_t)mode);
    if(cur&&c3_task_is_browser_runtime(cur)){
        static uint32_t force_open_trace;
        if(force_open_trace<32u){
            ++force_open_trace;
            __boot_serial_force_puts("[bopen!] pid=");
            __boot_serial_force_putu32((uint32_t)cur->pid);
            c3_force_task_name(cur);
            __boot_serial_force_puts(" path=");
            __boot_serial_force_puts(npath);
            __boot_serial_force_puts(" flags=");
            __boot_serial_force_puthex64((uint64_t)(uint32_t)flags);
            __boot_serial_force_puts(" rc=");
            c3_force_rc(rc);
            __boot_serial_force_puts("\n");
        }
    }
    return rc;
}

int64_t real_sys_creat(const char *path,int mode){
    return real_sys_open(path,O_CREAT|O_WRONLY|O_TRUNC,mode);
}

static uint32_t c3_fd_ref_count(uint8_t kind,int ref){
    uint32_t n=0;
    int ti,fi,tj;
    if(kind==FDKIND_NONE)return 0;
    for(ti=0;ti<TASK_MAX;++ti){
        if(!g_tasks[ti].used)continue;
        for(fi=0;fi<TASK_FD_MAX;++fi){
            real_fd_t *f=&g_tasks[ti].fdt.fds[fi];
            bool seen=false;
            if(f->kind!=kind||f->ref!=ref)continue;
            for(tj=0;tj<ti;++tj){
                if(!g_tasks[tj].used)continue;
                if(g_tasks[tj].state==TASK_ZOMBIE||g_tasks[tj].state==TASK_FREE)continue;
                if(!c3_fd_same_group(&g_tasks[tj],&g_tasks[ti]))continue;
                if(g_tasks[tj].fdt.fds[fi].kind==kind&&g_tasks[tj].fdt.fds[fi].ref==ref){
                    seen=true;
                    break;
                }
            }
            if(!seen)++n;
        }
    }
    return n;
}

static bool c3_fd_group_has_live_peer(task_t *owner){
    int i;
    if(!owner)return false;
    for(i=0;i<TASK_MAX;++i){
        task_t *t=&g_tasks[i];
        if(!t->used||t==owner)continue;
        if(t->state==TASK_ZOMBIE||t->state==TASK_FREE)continue;
        if(c3_fd_same_group(t,owner))return true;
    }
    return false;
}

static int64_t c3_close_fd_for_task(task_t *owner,int fd){
    int ref;
    uint8_t kind;
    uint16_t fflags;
    bool last_ref;
    c3_fd_group_sync_fd(owner,fd);
    if(!owner||!fd_valid(&owner->fdt,fd))return -EBADF;
    kind=owner->fdt.fds[fd].kind;
    ref=owner->fdt.fds[fd].ref;
    fflags=owner->fdt.fds[fd].flags;
    last_ref=(c3_fd_ref_count(kind,ref)<=1);
    if(c3_task_is_firefox_ipc_trace(owner)){
        static uint32_t close_trace=0;
        if(close_trace<220){
            ++close_trace;
            c3_trace_one_fd("[close-fd]",owner,fd);
            __boot_serial_puts("[close-fd] last_ref=");
            __boot_serial_putu32(last_ref?1u:0u);
            __boot_serial_puts(" trace=");
            __boot_serial_putu32(close_trace);
            __boot_serial_puts("\n");
        }
        if(owner->pid>=90&&(fd==6||fd==14||fd==17||fd==18||fd==19||
                            (kind==FDKIND_SOCKET&&(ref==8||ref==9)))){
            static uint32_t content_close_trace=0;
            if(content_close_trace<96){
                ++content_close_trace;
                c3_trace_one_fd("[content-close-fd]",owner,fd);
                __boot_serial_puts("[content-close-fd] last_ref=");
                __boot_serial_putu32(last_ref?1u:0u);
                __boot_serial_puts(" trace=");
                __boot_serial_putu32(content_close_trace);
                __boot_serial_puts("\n");
            }
        }
    }
    if(kind==FDKIND_VFSFILE||kind==FDKIND_DIR){
        const char *p=c3_vfs_open_slot_path(ref);
        if(p){
            uint32_t m=(kind==FDKIND_DIR)?(C3_IN_CLOSE_NOWRITE|C3_IN_ISDIR):
                ((fflags&FDFL_WRITABLE)?C3_IN_CLOSE_WRITE:C3_IN_CLOSE_NOWRITE);
            c3_inotify_notify_path(p,m,0);
        }
    }
    if(last_ref){
        if(kind==FDKIND_PROC){
            if(ref>=0&&ref<PROC_BUF_MAX)g_proc_bufs[ref].used=false;
        }else if(kind==FDKIND_SOCKET){
            if(ref>=0&&ref<SOCK_MAX)sock_close(ref);
        }else if(kind==FDKIND_PIPE_R){
            pipe_close_read(ref);
        }else if(kind==FDKIND_PIPE_W){
            pipe_close_write(ref);
        }else if(kind==FDKIND_EPOLL){
            if(ref>=0&&ref<EPOLL_INSTANCES)g_epoll_instances[ref].used=false;
        }else if(kind==FDKIND_EVENTFD){
            c3_eventfd_free(ref);
        }else if(kind==FDKIND_TIMERFD){
            c3_timerfd_free(ref);
        }else if(kind==FDKIND_INOTIFY){
            c3_inotify_inst_free(ref);
        }
    }
    c3_fd_group_clear_fd(owner,fd);
    if(last_ref&&(kind==FDKIND_VFSFILE||kind==FDKIND_DIR))c3_vfs_open_slot_try_free(ref);
    return 0;
}

void compat3_task_close_all_fds(task_t *t){
    int fd;
    if(!t)return;
    if(c3_fd_group_has_live_peer(t)){
        for(fd=0;fd<TASK_FD_MAX;++fd){
            if(t->fdt.fds[fd].kind==FDKIND_NONE)continue;
            fd_close(&t->fdt,fd);
        }
        return;
    }
    for(fd=0;fd<TASK_FD_MAX;++fd){
        if(t->fdt.fds[fd].kind==FDKIND_NONE)continue;
        (void)c3_close_fd_for_task(t,fd);
    }
}

int64_t real_sys_close(int fd){
    task_t *cur=task_current();
    int64_t rc=c3_close_fd_for_task(cur,fd);
    if(rc<0&&c3_task_is_browser_runtime(cur)){
        static uint32_t browser_close_trace=0;
        if(browser_close_trace<160){
            ++browser_close_trace;
            __boot_serial_puts("[close-browser-softfail] pid=");
            __boot_serial_putu32((uint32_t)(cur?cur->pid:0));
            c3_trace_task_name(cur);
            __boot_serial_puts(" fd=");
            __boot_serial_putu32((uint32_t)fd);
            __boot_serial_puts(" rc=-");
            __boot_serial_putu32((uint32_t)(-rc));
            __boot_serial_puts(" trace=");
            __boot_serial_putu32(browser_close_trace);
            __boot_serial_puts("\n");
        }
        return 0;
    }
    return rc;
}

static int64_t c3_pipe_read_wait(int ref,void *buf,size_t count,int flags){
    task_t *cur=task_current();
    uint32_t waits=0;
    if(count==0)return 0;
    for(;;){
        pipe_t *p;
        size_t av,toread,i;
        uint8_t *d;
        if(ref<0||ref>=PIPE_MAX||!g_pipes[ref].used)return -EBADF;
        p=&g_pipes[ref];
        av=(size_t)((p->head-p->tail+PIPE_BUF_SIZE)%PIPE_BUF_SIZE);
        if(av){
            if(count>av)toread=av;
            else toread=count;
            d=(uint8_t*)buf;
            for(i=0;i<toread;++i){
                d[i]=p->buf[p->tail%PIPE_BUF_SIZE];
                p->tail++;
            }
            if(c3_task_is_browser_runtime(cur)){
                static uint32_t pipe_read_trace=0;
                if(pipe_read_trace<64){
                    size_t left=(size_t)((p->head-p->tail+PIPE_BUF_SIZE)%PIPE_BUF_SIZE);
                    ++pipe_read_trace;
                    __boot_serial_puts("[pipe-read] pid=");
                    __boot_serial_putu32(cur?(uint32_t)cur->pid:0);
                    c3_trace_task_name(cur);
                    __boot_serial_puts(" ref=");
                    __boot_serial_putu32((uint32_t)ref);
                    __boot_serial_puts(" ret=");
                    __boot_serial_putu32((uint32_t)toread);
                    __boot_serial_puts(" left=");
                    __boot_serial_putu32((uint32_t)left);
                    __boot_serial_puts(" writers=");
                    __boot_serial_putu32((uint32_t)p->writers);
                    __boot_serial_puts("\n");
                }
            }
            if(c3_task_is_firefox_ipc_trace(cur)){
                static uint32_t ff_pipe_read_trace=0;
                if(ff_pipe_read_trace<220){
                    size_t left=(size_t)((p->head-p->tail+PIPE_BUF_SIZE)%PIPE_BUF_SIZE);
                    uint64_t b0=0;
                    size_t bi,lim=toread<8?toread:8;
                    const uint8_t *rd=(const uint8_t*)buf;
                    for(bi=0;bi<lim;++bi)b0|=((uint64_t)rd[bi])<<(bi*8);
                    ++ff_pipe_read_trace;
                    __boot_serial_puts("[ff-pipe-read] pid=");
                    __boot_serial_putu32(cur?(uint32_t)cur->pid:0);
                    c3_trace_task_name(cur);
                    __boot_serial_puts(" ref=");
                    __boot_serial_putu32((uint32_t)ref);
                    __boot_serial_puts(" count=");
                    __boot_serial_puthex64((uint64_t)count);
                    __boot_serial_puts(" ret=");
                    __boot_serial_putu32((uint32_t)toread);
                    __boot_serial_puts(" left=");
                    __boot_serial_putu32((uint32_t)left);
                    __boot_serial_puts(" b0=");
                    __boot_serial_puthex64(b0);
                    __boot_serial_puts(" trace=");
                    __boot_serial_putu32(ff_pipe_read_trace);
                    __boot_serial_puts("\n");
                }
            }
            if(c3_task_is_firefox_ipc_trace(cur)&&ref>=8){
                static uint32_t content_pipe_read_trace=0;
                if(content_pipe_read_trace<96){
                    size_t left=(size_t)((p->head-p->tail+PIPE_BUF_SIZE)%PIPE_BUF_SIZE);
                    uint64_t b0=0;
                    size_t bi,lim=toread<8?toread:8;
                    const uint8_t *rd=(const uint8_t*)buf;
                    for(bi=0;bi<lim;++bi)b0|=((uint64_t)rd[bi])<<(bi*8);
                    ++content_pipe_read_trace;
                    __boot_serial_puts("[content-pipe-read] pid=");
                    __boot_serial_putu32(cur?(uint32_t)cur->pid:0);
                    c3_trace_task_name(cur);
                    __boot_serial_puts(" ref=");
                    __boot_serial_putu32((uint32_t)ref);
                    __boot_serial_puts(" count=");
                    __boot_serial_puthex64((uint64_t)count);
                    __boot_serial_puts(" ret=");
                    __boot_serial_putu32((uint32_t)toread);
                    __boot_serial_puts(" left=");
                    __boot_serial_putu32((uint32_t)left);
                    __boot_serial_puts(" b0=");
                    __boot_serial_puthex64(b0);
                    __boot_serial_puts(" trace=");
                    __boot_serial_putu32(content_pipe_read_trace);
                    __boot_serial_puts("\n");
                }
            }
            return(int64_t)toread;
        }
        if(p->writers<=0)return 0;
        if(flags&C3_MSG_DONTWAIT)return -EAGAIN;
        if(cur&&c3_task_has_unblocked_signal(cur))return -EINTR;
        if(waits<12){
            ++waits;
            __boot_serial_puts("[pipe-read-wait] pid=");
            __boot_serial_putu32(cur?(uint32_t)cur->pid:0);
            c3_trace_task_name(cur);
            __boot_serial_puts(" ref=");
            __boot_serial_putu32((uint32_t)ref);
            __boot_serial_puts(" writers=");
            __boot_serial_putu32((uint32_t)p->writers);
            __boot_serial_puts("\n");
        }
        c3_poll_wait_slice(cur,0);
    }
}

static int64_t c3_pipe_write_wait(int ref,const void *buf,size_t count,int flags){
    task_t *cur=task_current();
    uint32_t waits=0;
    if(count==0)return 0;
    for(;;){
        pipe_t *p;
        size_t av,towrite,i;
        const uint8_t *s;
        if(ref<0||ref>=PIPE_MAX||!g_pipes[ref].used)return -EBADF;
        p=&g_pipes[ref];
        if(p->readers<=0)return -EPIPE;
        av=(size_t)(PIPE_BUF_SIZE-((p->head-p->tail+PIPE_BUF_SIZE)%PIPE_BUF_SIZE)-1);
        if(av){
            if(count>av)towrite=av;
            else towrite=count;
            s=(const uint8_t*)buf;
            for(i=0;i<towrite;++i){
                p->buf[p->head%PIPE_BUF_SIZE]=s[i];
                p->head++;
            }
            if(c3_task_is_browser_runtime(cur)){
                static uint32_t pipe_write_trace=0;
                if(pipe_write_trace<64){
                    size_t used=(size_t)((p->head-p->tail+PIPE_BUF_SIZE)%PIPE_BUF_SIZE);
                    ++pipe_write_trace;
                    __boot_serial_puts("[pipe-write] pid=");
                    __boot_serial_putu32(cur?(uint32_t)cur->pid:0);
                    c3_trace_task_name(cur);
                    __boot_serial_puts(" ref=");
                    __boot_serial_putu32((uint32_t)ref);
                    __boot_serial_puts(" ret=");
                    __boot_serial_putu32((uint32_t)towrite);
                    __boot_serial_puts(" used=");
                    __boot_serial_putu32((uint32_t)used);
                    __boot_serial_puts(" readers=");
                    __boot_serial_putu32((uint32_t)p->readers);
                    __boot_serial_puts("\n");
                }
            }
            if(c3_task_is_firefox_ipc_trace(cur)){
                static uint32_t ff_pipe_write_trace=0;
                if(ff_pipe_write_trace<220){
                    size_t used=(size_t)((p->head-p->tail+PIPE_BUF_SIZE)%PIPE_BUF_SIZE);
                    uint64_t b0=0;
                    size_t bi,lim=towrite<8?towrite:8;
                    const uint8_t *wr=(const uint8_t*)buf;
                    for(bi=0;bi<lim;++bi)b0|=((uint64_t)wr[bi])<<(bi*8);
                    ++ff_pipe_write_trace;
                    __boot_serial_puts("[ff-pipe-write] pid=");
                    __boot_serial_putu32(cur?(uint32_t)cur->pid:0);
                    c3_trace_task_name(cur);
                    __boot_serial_puts(" ref=");
                    __boot_serial_putu32((uint32_t)ref);
                    __boot_serial_puts(" count=");
                    __boot_serial_puthex64((uint64_t)count);
                    __boot_serial_puts(" ret=");
                    __boot_serial_putu32((uint32_t)towrite);
                    __boot_serial_puts(" used=");
                    __boot_serial_putu32((uint32_t)used);
                    __boot_serial_puts(" b0=");
                    __boot_serial_puthex64(b0);
                    __boot_serial_puts(" trace=");
                    __boot_serial_putu32(ff_pipe_write_trace);
                    __boot_serial_puts("\n");
                }
            }
            if(c3_task_is_firefox_ipc_trace(cur)&&ref>=8){
                static uint32_t content_pipe_write_trace=0;
                if(content_pipe_write_trace<96){
                    size_t used=(size_t)((p->head-p->tail+PIPE_BUF_SIZE)%PIPE_BUF_SIZE);
                    uint64_t b0=0;
                    size_t bi,lim=towrite<8?towrite:8;
                    const uint8_t *wr=(const uint8_t*)buf;
                    for(bi=0;bi<lim;++bi)b0|=((uint64_t)wr[bi])<<(bi*8);
                    ++content_pipe_write_trace;
                    __boot_serial_puts("[content-pipe-write] pid=");
                    __boot_serial_putu32(cur?(uint32_t)cur->pid:0);
                    c3_trace_task_name(cur);
                    __boot_serial_puts(" ref=");
                    __boot_serial_putu32((uint32_t)ref);
                    __boot_serial_puts(" count=");
                    __boot_serial_puthex64((uint64_t)count);
                    __boot_serial_puts(" ret=");
                    __boot_serial_putu32((uint32_t)towrite);
                    __boot_serial_puts(" used=");
                    __boot_serial_putu32((uint32_t)used);
                    __boot_serial_puts(" b0=");
                    __boot_serial_puthex64(b0);
                    __boot_serial_puts(" trace=");
                    __boot_serial_putu32(content_pipe_write_trace);
                    __boot_serial_puts("\n");
                }
            }
            return(int64_t)towrite;
        }
        if(flags&C3_MSG_DONTWAIT)return -EAGAIN;
        if(cur&&c3_task_has_unblocked_signal(cur))return -EINTR;
        if(waits<12){
            ++waits;
            __boot_serial_puts("[pipe-write-wait] pid=");
            __boot_serial_putu32(cur?(uint32_t)cur->pid:0);
            c3_trace_task_name(cur);
            __boot_serial_puts(" ref=");
            __boot_serial_putu32((uint32_t)ref);
            __boot_serial_puts(" readers=");
            __boot_serial_putu32((uint32_t)p->readers);
            __boot_serial_puts("\n");
        }
        c3_poll_wait_slice(cur,0);
    }
}

int64_t real_sys_read(int fd,void *buf,size_t count){
    task_t *cur=task_current();
    if(!fd_valid(&cur->fdt,fd))return -EBADF;
    if(!buf)return -EFAULT;
    switch(cur->fdt.fds[fd].kind){
        case FDKIND_DEVNULL: return 0;
        case FDKIND_DEVZERO: return dev_zero_read(buf,count);
        case FDKIND_DEVRANDOM: return dev_random_read(buf,count);
        case FDKIND_DEVTTY: return tty_read(&g_tty0,buf,count);
        case FDKIND_SOCKET: {
            int fl=(cur->fdt.fds[fd].flags&FDFL_NONBLOCK)?C3_MSG_DONTWAIT:0;
            int ref=cur->fdt.fds[fd].ref;
            uint32_t rx_before=c3_socket_rx_used(ref);
            int64_t r=c3_socket_recv_wait(ref,buf,count,fl);
            if(c3_task_is_firefox_ipc_trace(cur)&&c3_socket_ref_virtual_service(ref)==SOCK_VIRT_NONE){
                static uint32_t ff_sock_read_trace=0;
                if(ff_sock_read_trace<180){
                    socket_t *s=(ref>=0&&ref<SOCK_MAX&&g_sockets[ref].used)?&g_sockets[ref]:0;
                    uint64_t b0=0;
                    size_t bi,lim=(r>0&&(size_t)r<8)?(size_t)r:8;
                    const uint8_t *rb=(const uint8_t*)buf;
                    if(r<=0)lim=0;
                    for(bi=0;bi<lim;++bi)b0|=((uint64_t)rb[bi])<<(bi*8);
                    ++ff_sock_read_trace;
                    __boot_serial_puts("[ff-sock-read] pid=");
                    __boot_serial_putu32((uint32_t)(cur?cur->pid:0));
                    c3_trace_task_name(cur);
                    __boot_serial_puts(" fd=");
                    __boot_serial_putu32((uint32_t)fd);
                    __boot_serial_puts(" ref=");
                    __boot_serial_putu32((uint32_t)ref);
                    __boot_serial_puts(" count=");
                    __boot_serial_puthex64((uint64_t)count);
                    __boot_serial_puts(" ret=");
                    if(r<0){__boot_serial_puts("-");__boot_serial_putu32((uint32_t)(-r));}
                    else __boot_serial_puthex64((uint64_t)r);
                    __boot_serial_puts(" rx_before=");
                    __boot_serial_putu32(rx_before);
                    if(s){
                        __boot_serial_puts(" rx_after=");
                        __boot_serial_putu32(c3_socket_rx_used(ref));
                        __boot_serial_puts(" peer=");
                        __boot_serial_putu32((uint32_t)s->peer);
                        __boot_serial_puts(" st=");
                        __boot_serial_putu32((uint32_t)s->tcp_state);
                    }
                    __boot_serial_puts(" b0=");
                    __boot_serial_puthex64(b0);
                    __boot_serial_puts("\n");
                }
            }
            c3_trace_x11_read_result(fd,ref,count,fl,r,buf,rx_before);
            return r;
        }
        case FDKIND_PROC: {
            int ref=cur->fdt.fds[fd].ref;
            if(ref>=0&&ref<PROC_BUF_MAX&&g_proc_bufs[ref].used){
                uint64_t off=cur->fdt.fds[fd].offset;
                uint32_t avail=g_proc_bufs[ref].size;
                if(off>=avail)return 0;
                size_t toread=count;if(toread>avail-off)toread=avail-(size_t)off;
                c3_memcpy(buf,g_proc_bufs[ref].data+off,toread);
                cur->fdt.fds[fd].offset+=toread;
                c3_fd_group_copy_fd(cur,fd);
                return(int64_t)toread;
            }
            return 0;
        }
        case FDKIND_VFSFILE: {
            const uint8_t *data;uint32_t size;
            const char *vpath=c3_vfs_open_slot_path(cur->fdt.fds[fd].ref);
            uint64_t off;
            size_t toread;
            uint64_t memfd_size=0;
            if(!vpath)return -EBADF;
            if(c3_memfd_path_get_size(vpath,&memfd_size)){
                off=cur->fdt.fds[fd].offset;
                toread=c3_memfd_sparse_read(vpath,off,buf,count);
                cur->fdt.fds[fd].offset+=toread;
                c3_fd_group_copy_fd(cur,fd);
                return(int64_t)toread;
            }
            if(!kvfs_read(vpath,&data,&size))return -ENOENT;
            off=cur->fdt.fds[fd].offset;
            if(off>=size)return 0;
            toread=count;
            if(toread>(size_t)(size-off))toread=(size_t)(size-off);
            c3_memcpy(buf,data+off,toread);
            if(g_c3_libxi_read_trace_count<8&&vpath&&c3_has_token(vpath,"libXi.so.6")){
                uint64_t src8=0,dst8=0;
                size_t bi,lim=toread<8?toread:8;
                for(bi=0;bi<lim;++bi){
                    src8|=((uint64_t)(data+off)[bi])<<(bi*8);
                    dst8|=((uint64_t)((const uint8_t*)buf)[bi])<<(bi*8);
                }
                ++g_c3_libxi_read_trace_count;
                __boot_serial_puts("[read-libXi] fd=");
                __boot_serial_putu32((uint32_t)fd);
                __boot_serial_puts(" off=");
                __boot_serial_puthex64(off);
                __boot_serial_puts(" count=");
                __boot_serial_puthex64((uint64_t)count);
                __boot_serial_puts(" ret=");
                __boot_serial_puthex64((uint64_t)toread);
                __boot_serial_puts(" src8=");
                __boot_serial_puthex64(src8);
                __boot_serial_puts(" dst8=");
                __boot_serial_puthex64(dst8);
                __boot_serial_puts(" path=");
                __boot_serial_puts(vpath);
                __boot_serial_puts("\n");
            }
            c3_trace_firefox_read_sample(vpath,fd,off,count,toread,buf);
            cur->fdt.fds[fd].offset+=toread;
            c3_fd_group_copy_fd(cur,fd);
            c3_inotify_notify_path(vpath,C3_IN_ACCESS,0);
            return(int64_t)toread;
        }
        case FDKIND_PIPE_R: {
            int fl=(cur->fdt.fds[fd].flags&FDFL_NONBLOCK)?C3_MSG_DONTWAIT:0;
            return c3_pipe_read_wait(cur->fdt.fds[fd].ref,buf,count,fl);
        }
        case FDKIND_DEVFB: return dev_fb_read(buf,count,cur->fdt.fds[fd].offset);
        case FDKIND_EVENTFD: {
            int fl=(cur->fdt.fds[fd].flags&FDFL_NONBLOCK)?C3_MSG_DONTWAIT:0;
            return c3_eventfd_read(cur->fdt.fds[fd].ref,buf,count,fl);
        }
        case FDKIND_TIMERFD:
            return c3_timerfd_read_ready(cur->fdt.fds[fd].ref,buf,count,
                (cur->fdt.fds[fd].flags&FDFL_NONBLOCK)?C3_MSG_DONTWAIT:0);
        case FDKIND_SIGNALFD: {
            if(c3_task_has_unblocked_signal(cur)){
                uint8_t info[128];
                size_t n=count<sizeof(info)?count:sizeof(info);
                c3_memset(info,0,sizeof(info));
                c3_memcpy(buf,info,n);
                return (int64_t)n;
            }
            return (cur->fdt.fds[fd].flags&FDFL_NONBLOCK)?-EAGAIN:0;
        }
        case FDKIND_INOTIFY:
            return c3_inotify_read(cur->fdt.fds[fd].ref,buf,count,
                (cur->fdt.fds[fd].flags&FDFL_NONBLOCK)?C3_MSG_DONTWAIT:0);
        case FDKIND_DIR: return -EISDIR;
        default: return -EINVAL;
    }
}

int64_t real_sys_write(int fd,const void *buf,size_t count){
    task_t *cur=task_current();
    if(!fd_valid(&cur->fdt,fd))return -EBADF;
    if(!buf)return -EFAULT;
    switch(cur->fdt.fds[fd].kind){
        case FDKIND_DEVNULL: return(int64_t)count;
        case FDKIND_DEVTTY: {
            int r=tty_write(&g_tty0,buf,count);
            static uint32_t browser_tty_trace_count=0;
            static uint32_t stderr_trace_count=0;
            static uint32_t abort_stack_trace_count=0;
            if(fd==2&&c3_task_is_browser_runtime(cur)&&browser_tty_trace_count<24){
                const char *p=(const char*)buf;
                size_t j,lim=count;
                ++browser_tty_trace_count;
                if(lim>320)lim=320;
                __boot_serial_force_puts("[btty!] pid=");
                __boot_serial_force_putu32(cur?(uint32_t)cur->pid:0);
                c3_force_task_name(cur);
                __boot_serial_force_puts(" fd=");
                __boot_serial_force_putu32((uint32_t)fd);
                __boot_serial_force_puts(" n=");
                __boot_serial_force_putu32((uint32_t)count);
                __boot_serial_force_puts(" text=");
                for(j=0;j<lim;++j){
                    char ch=p[j];
                    if(ch==0)break;
                    if((ch>=' '&&ch<='~')||ch=='\n'||ch=='\r'||ch=='\t')__boot_serial_force_putc(ch);
                    else __boot_serial_force_putc('.');
                }
                if(count>lim)__boot_serial_force_puts("...");
                if(lim==0||p[lim-1]!='\n')__boot_serial_force_putc('\n');
            }
            if(fd==2&&stderr_trace_count<64){
                const char *p=(const char*)buf;
                size_t j,lim=count;
                ++stderr_trace_count;
                if(lim>240)lim=240;
                __boot_serial_puts("[u-stderr] ");
                for(j=0;j<lim;++j){
                    char ch=p[j];
                    if(ch==0)break;
                    if((ch>=' '&&ch<='~')||ch=='\n'||ch=='\r'||ch=='\t')__boot_serial_putc(ch);
                    else __boot_serial_putc('.');
                }
                if(count>lim)__boot_serial_puts("...");
                if(lim==0||p[lim-1]!='\n')__boot_serial_putc('\n');
            }
            if(fd==2&&abort_stack_trace_count<6&&
               (c3_mem_has_token((const char*)buf,count,"Redirecting call to abort()")||
                c3_mem_has_token((const char*)buf,count,"stack smashing detected"))){
                uint64_t *f=task_syscall_user_frame(cur);
                uint64_t user_rip=0,user_rsp=0;
                int si;
                ++abort_stack_trace_count;
                if(f){
                    user_rip=f[17];
                    user_rsp=f[20];
                }
                __boot_serial_puts("[abort-stack] pid=");
                __boot_serial_putu32(cur?(uint32_t)cur->pid:0);
                c3_trace_task_name(cur);
                __boot_serial_puts(" user=");
                __boot_serial_puthex64(user_rip);
                __boot_serial_puts(" ursp=");
                __boot_serial_puthex64(user_rsp);
                __boot_serial_puts(" words=");
                for(si=0;si<96;++si){
                    uint64_t a=user_rsp+(uint64_t)si*8ULL;
                    if(!user_rsp||!cur||!cur->addr_space||
                       !(paging_get_entry(cur->addr_space,a)&PAGE_PRESENT))break;
                    if(si)__boot_serial_puts(",");
                    __boot_serial_puthex64(*(uint64_t*)(uintptr_t)a);
                }
                __boot_serial_puts("\n");
            }
            return r;
        }
        case FDKIND_SOCKET: {
            int ref=cur->fdt.fds[fd].ref;
            int64_t r=(int64_t)sock_send(ref,buf,count,0);
            if(c3_task_is_firefox_ipc_trace(cur)&&c3_socket_ref_virtual_service(ref)==SOCK_VIRT_NONE){
                static uint32_t ff_sock_write_trace=0;
                if(ff_sock_write_trace<180){
                    socket_t *s=(ref>=0&&ref<SOCK_MAX&&g_sockets[ref].used)?&g_sockets[ref]:0;
                    uint64_t b0=0;
                    size_t bi,lim=count<8?count:8;
                    const uint8_t *wb=(const uint8_t*)buf;
                    for(bi=0;bi<lim;++bi)b0|=((uint64_t)wb[bi])<<(bi*8);
                    ++ff_sock_write_trace;
                    __boot_serial_puts("[ff-sock-write] pid=");
                    __boot_serial_putu32((uint32_t)(cur?cur->pid:0));
                    c3_trace_task_name(cur);
                    __boot_serial_puts(" fd=");
                    __boot_serial_putu32((uint32_t)fd);
                    __boot_serial_puts(" ref=");
                    __boot_serial_putu32((uint32_t)ref);
                    __boot_serial_puts(" count=");
                    __boot_serial_puthex64((uint64_t)count);
                    __boot_serial_puts(" ret=");
                    if(r<0){__boot_serial_puts("-");__boot_serial_putu32((uint32_t)(-r));}
                    else __boot_serial_puthex64((uint64_t)r);
                    if(s){
                        __boot_serial_puts(" peer=");
                        __boot_serial_putu32((uint32_t)s->peer);
                        __boot_serial_puts(" st=");
                        __boot_serial_putu32((uint32_t)s->tcp_state);
                    }
                    __boot_serial_puts(" b0=");
                    __boot_serial_puthex64(b0);
                    __boot_serial_puts("\n");
                }
            }
            return r;
        }
        case FDKIND_PIPE_W: {
            int fl=(cur->fdt.fds[fd].flags&FDFL_NONBLOCK)?C3_MSG_DONTWAIT:0;
            return c3_pipe_write_wait(cur->fdt.fds[fd].ref,buf,count,fl);
        }
        case FDKIND_DEVFB: return dev_fb_write(buf,count,cur->fdt.fds[fd].offset);
        case FDKIND_DEVZERO: return(int64_t)count;
        case FDKIND_EVENTFD: {
            static uint32_t eventfd_write_fd_trace=0;
            if(eventfd_write_fd_trace<48){
                ++eventfd_write_fd_trace;
                __boot_serial_puts("[eventfd-write-fd] pid=");
                __boot_serial_putu32(cur?(uint32_t)cur->pid:0);
                c3_trace_task_name(cur);
                __boot_serial_puts(" fd=");
                __boot_serial_putu32((uint32_t)fd);
                __boot_serial_puts(" ref=");
                __boot_serial_putu32((uint32_t)cur->fdt.fds[fd].ref);
                __boot_serial_puts(" count=");
                __boot_serial_puthex64((uint64_t)count);
                __boot_serial_puts("\n");
            }
            return c3_eventfd_write(cur->fdt.fds[fd].ref,buf,count);
        }
        case FDKIND_TIMERFD: return -EINVAL;
        case FDKIND_SIGNALFD: return -EINVAL;
        case FDKIND_INOTIFY: return -EINVAL;
        case FDKIND_DIR: return -EISDIR;
        case FDKIND_VFSFILE: {
            const char *vpath=c3_vfs_open_slot_path(cur->fdt.fds[fd].ref);
            const uint8_t *old_data=0;
            uint32_t old_size=0;
            uint8_t *out=g_c3_vfs_file_scratch;
            size_t base=0,avail,count_in,count_write,i;
            uint64_t memfd_size=0;
            if(!vpath)return -EBADF;
            if(!(cur->fdt.fds[fd].flags&FDFL_WRITABLE))return -EBADF;
            if(c3_memfd_path_get_size(vpath,&memfd_size)){
                uint32_t seals=0;
                int64_t wr;
                if(c3_memfd_path_get_seals(vpath,&seals)&&
                   (seals&(C3_F_SEAL_WRITE|C3_F_SEAL_FUTURE_WRITE)))return -EPERM;
                base=(cur->fdt.fds[fd].flags&FDFL_APPEND)?(size_t)memfd_size:(size_t)cur->fdt.fds[fd].offset;
                wr=c3_memfd_sparse_write(vpath,(uint64_t)base,buf,count);
                if(wr>0){
                    cur->fdt.fds[fd].offset=(uint64_t)base+(uint64_t)wr;
                    c3_fd_group_copy_fd(cur,fd);
                }
                return wr;
            }
            if(kvfs_read(vpath,&old_data,&old_size)&&old_data&&old_size>0){
                if(cur->fdt.fds[fd].flags&FDFL_APPEND)base=(size_t)old_size;
                else base=(size_t)cur->fdt.fds[fd].offset;
                if(base>C3_VFS_FILE_MAX-1)base=C3_VFS_FILE_MAX-1;
                for(i=0;i<base;++i){
                    out[i]=(i<(size_t)old_size)?old_data[i]:0;
                }
            }else{
                base=(size_t)cur->fdt.fds[fd].offset;
                if(base>C3_VFS_FILE_MAX-1)base=C3_VFS_FILE_MAX-1;
                for(i=0;i<base;++i)out[i]=0;
            }
            avail=C3_VFS_FILE_MAX-1-base;
            count_in=count;
            count_write=(count_in<avail)?count_in:avail;
            for(i=0;i<count_write;++i)out[base+i]=((const uint8_t*)buf)[i];
            c3_file_page_invalidate_path(vpath);
            kvfs_write_bytes(vpath,out,(uint32_t)(base+count_write));
            c3_inotify_notify_path(vpath,C3_IN_MODIFY,0);
            cur->fdt.fds[fd].offset=(uint64_t)(base+count_write);
            c3_fd_group_copy_fd(cur,fd);
            return(int64_t)count_write;
        }
        default: return -EINVAL;
    }
}

int64_t real_sys_pread64(int fd,void *buf,size_t count,int64_t offset){
    task_t *cur=task_current();
    uint64_t off;
    if(!fd_valid(&cur->fdt,fd))return -EBADF;
    if(!buf)return -EFAULT;
    if(offset<0)return -EINVAL;
    off=(uint64_t)offset;
    switch(cur->fdt.fds[fd].kind){
        case FDKIND_DEVNULL: return 0;
        case FDKIND_DEVZERO: return dev_zero_read(buf,count);
        case FDKIND_DEVRANDOM: return dev_random_read(buf,count);
        case FDKIND_PROC: {
            int ref=cur->fdt.fds[fd].ref;
            if(ref>=0&&ref<PROC_BUF_MAX&&g_proc_bufs[ref].used){
                uint32_t avail=g_proc_bufs[ref].size;
                size_t toread=count;
                if(off>=avail)return 0;
                if(toread>(size_t)(avail-off))toread=(size_t)(avail-off);
                c3_memcpy(buf,g_proc_bufs[ref].data+off,toread);
                return(int64_t)toread;
            }
            return 0;
        }
        case FDKIND_VFSFILE: {
            const uint8_t *data;
            uint32_t size;
            const char *vpath=c3_vfs_open_slot_path(cur->fdt.fds[fd].ref);
            size_t toread=count;
            uint64_t memfd_size=0;
            if(!vpath)return -EBADF;
            if(c3_memfd_path_get_size(vpath,&memfd_size)){
                toread=c3_memfd_sparse_read(vpath,off,buf,count);
                c3_trace_firefox_read_sample(vpath,fd,off,count,toread,buf);
                return(int64_t)toread;
            }
            if(!kvfs_read(vpath,&data,&size))return -ENOENT;
            if(off>=size)return 0;
            if(toread>(size_t)(size-off))toread=(size_t)(size-off);
            c3_memcpy(buf,data+off,toread);
            c3_trace_firefox_read_sample(vpath,fd,off,count,toread,buf);
            c3_inotify_notify_path(vpath,C3_IN_ACCESS,0);
            return(int64_t)toread;
        }
        case FDKIND_DEVFB: return dev_fb_read(buf,count,off);
        case FDKIND_DIR: return -EISDIR;
        case FDKIND_DEVTTY:
        case FDKIND_SOCKET:
        case FDKIND_PIPE_R:
        case FDKIND_PIPE_W:
        case FDKIND_EVENTFD:
        case FDKIND_TIMERFD:
        case FDKIND_SIGNALFD:
        case FDKIND_INOTIFY:
            return -ESPIPE;
        default:
            return -EINVAL;
    }
}

int64_t real_sys_pwrite64(int fd,const void *buf,size_t count,int64_t offset){
    task_t *cur=task_current();
    uint64_t off;
    if(!fd_valid(&cur->fdt,fd))return -EBADF;
    if(!buf)return -EFAULT;
    if(offset<0)return -EINVAL;
    off=(uint64_t)offset;
    switch(cur->fdt.fds[fd].kind){
        case FDKIND_DEVNULL: return(int64_t)count;
        case FDKIND_DEVZERO: return(int64_t)count;
        case FDKIND_DEVFB: return dev_fb_write(buf,count,off);
        case FDKIND_VFSFILE: {
            const char *vpath=c3_vfs_open_slot_path(cur->fdt.fds[fd].ref);
            const uint8_t *old_data=0;
            uint32_t old_size=0;
            uint8_t *out=g_c3_vfs_file_scratch;
            size_t base=(size_t)off,avail,count_in,count_write,i;
            uint64_t memfd_size=0;
            if(!vpath)return -EBADF;
            if(!(cur->fdt.fds[fd].flags&FDFL_WRITABLE))return -EBADF;
            if(c3_memfd_path_get_size(vpath,&memfd_size)){
                uint32_t seals=0;
                (void)memfd_size;
                if(c3_memfd_path_get_seals(vpath,&seals)&&
                   (seals&(C3_F_SEAL_WRITE|C3_F_SEAL_FUTURE_WRITE)))return -EPERM;
                return c3_memfd_sparse_write(vpath,off,buf,count);
            }
            if(base>=C3_VFS_FILE_MAX-1)return -EFBIG;
            if(kvfs_read(vpath,&old_data,&old_size)&&old_data&&old_size>0){
                for(i=0;i<base;++i){
                    out[i]=(i<(size_t)old_size)?old_data[i]:0;
                }
            }else{
                for(i=0;i<base;++i)out[i]=0;
            }
            avail=C3_VFS_FILE_MAX-1-base;
            count_in=count;
            count_write=(count_in<avail)?count_in:avail;
            for(i=0;i<count_write;++i)out[base+i]=((const uint8_t*)buf)[i];
            c3_file_page_invalidate_path(vpath);
            kvfs_write_bytes(vpath,out,(uint32_t)(base+count_write));
            c3_inotify_notify_path(vpath,C3_IN_MODIFY,0);
            return(int64_t)count_write;
        }
        case FDKIND_DIR: return -EISDIR;
        case FDKIND_DEVTTY:
        case FDKIND_SOCKET:
        case FDKIND_PIPE_R:
        case FDKIND_PIPE_W:
        case FDKIND_EVENTFD:
        case FDKIND_TIMERFD:
        case FDKIND_SIGNALFD:
        case FDKIND_INOTIFY:
            return -ESPIPE;
        default:
            return -EINVAL;
    }
}

int64_t real_sys_readv(int fd,const iovec_t *iov,int iovcnt){
    int64_t total=0;
    int i;
    if(iovcnt<0||iovcnt>1024)return -EINVAL;
    if(iovcnt>0&&!iov)return -EFAULT;
    for(i=0;i<iovcnt;++i){
        int64_t r;
        if(!iov[i].iov_len)continue;
        if(!iov[i].iov_base)return total?total:-EFAULT;
        r=real_sys_read(fd,iov[i].iov_base,iov[i].iov_len);
        if(r<0)return total?total:r;
        total+=r;
        if((size_t)r<iov[i].iov_len)break;
    }
    return total;
}

int64_t real_sys_writev(int fd,const iovec_t *iov,int iovcnt){
    int64_t total=0;
    int i;
    task_t *cur=task_current();
    if(iovcnt<0||iovcnt>1024)return -EINVAL;
    if(iovcnt>0&&!iov)return -EFAULT;
    if(fd_valid(&cur->fdt,fd)&&cur->fdt.fds[fd].kind==FDKIND_SOCKET){
        int ref=cur->fdt.fds[fd].ref;
        if(c3_socket_ref_is_virtual_stream(ref)){
            return c3_socket_send_iov_coalesced(ref,iov,(size_t)iovcnt,0);
        }
    }
    for(i=0;i<iovcnt;++i){
        int64_t r;
        if(!iov[i].iov_len)continue;
        if(!iov[i].iov_base)return total?total:-EFAULT;
        r=real_sys_write(fd,iov[i].iov_base,iov[i].iov_len);
        if(r<0)return total?total:r;
        total+=r;
        if((size_t)r<iov[i].iov_len)break;
    }
    return total;
}

int64_t real_sys_lseek(int fd,int64_t offset,int whence){
    task_t *cur=task_current();
    if(!fd_valid(&cur->fdt,fd))return -EBADF;
    switch(whence){
        case SEEK_SET: cur->fdt.fds[fd].offset=(uint64_t)offset;break;
        case SEEK_CUR: cur->fdt.fds[fd].offset=(uint64_t)((int64_t)cur->fdt.fds[fd].offset+offset);break;
        case SEEK_END:
            if(cur->fdt.fds[fd].kind==FDKIND_VFSFILE){
                const char *vpath=c3_vfs_open_slot_path(cur->fdt.fds[fd].ref);
                const uint8_t *data=0;
                uint32_t size=0;
                uint64_t memfd_size=0;
                if(vpath&&c3_memfd_path_get_size(vpath,&memfd_size))
                    cur->fdt.fds[fd].offset=(uint64_t)((int64_t)memfd_size+offset);
                else if(vpath&&kvfs_read(vpath,&data,&size))
                    cur->fdt.fds[fd].offset=(uint64_t)((int64_t)size+offset);
                else cur->fdt.fds[fd].offset=(uint64_t)offset;
            }else{
                cur->fdt.fds[fd].offset=(uint64_t)offset;
            }
            break;
        default:
            return -EINVAL;
    }
    c3_fd_group_copy_fd(cur,fd);
    return(int64_t)cur->fdt.fds[fd].offset;
}

static int64_t c3_dup_fd_min(task_t *cur,int oldfd,int minfd,bool cloexec){
    int nfd;
    real_fd_t nf;
    if(!cur)return -ESRCH;
    c3_fd_group_sync_fd(cur,oldfd);
    if(!fd_valid(&cur->fdt,oldfd))return -EBADF;
    if(minfd<0)return -EINVAL;
    if(minfd>=TASK_FD_MAX)return -EINVAL;
    for(nfd=minfd;nfd<TASK_FD_MAX;++nfd)
        if(!c3_fd_group_slot_used(cur,nfd))break;
    if(nfd>=TASK_FD_MAX)return -EMFILE;
    c3_memcpy(&nf,&cur->fdt.fds[oldfd],sizeof(nf));
    nf.refcount=1;
    nf.flags=(uint16_t)(nf.flags&~FDFL_CLOEXEC);
    if(cloexec)nf.flags|=FDFL_CLOEXEC;
    cur->fdt.fds[nfd]=nf;
    c3_fd_group_copy_fd(cur,nfd);
    return nfd;
}

int64_t real_sys_dup(int oldfd){
    task_t *cur=task_current();
    int64_t nfd=c3_dup_fd_min(cur,oldfd,0,false);
    if(c3_task_is_firefox_ipc_trace(cur)){
        static uint32_t dup_trace=0;
        if(dup_trace<120){
            ++dup_trace;
            __boot_serial_puts("[dup-fd] pid=");
            __boot_serial_putu32((uint32_t)(cur?cur->pid:0));
            c3_trace_task_name(cur);
            __boot_serial_puts(" old=");
            __boot_serial_putu32((uint32_t)oldfd);
            __boot_serial_puts(" new=");
            if(nfd>=0)__boot_serial_putu32((uint32_t)nfd);
            else __boot_serial_puthex64((uint64_t)nfd);
            __boot_serial_puts("\n");
            if(nfd>=0)c3_trace_one_fd("[dup-fd-new]",cur,(int)nfd);
        }
    }
    return nfd;
}
int64_t real_sys_dup2(int oldfd,int newfd){
    task_t *cur=task_current();
    c3_fd_group_sync_fd(cur,oldfd);
    if(!fd_valid(&cur->fdt,oldfd))return -EBADF;
    if(newfd<0||newfd>=TASK_FD_MAX)return -EBADF;
    if(oldfd==newfd)return newfd;
    c3_fd_group_sync_fd(cur,newfd);
    if(fd_valid(&cur->fdt,newfd)){
        int64_t rc=c3_close_fd_for_task(cur,newfd);
        if(rc<0)return rc;
    }
    c3_memcpy(&cur->fdt.fds[newfd],&cur->fdt.fds[oldfd],sizeof(real_fd_t));
    cur->fdt.fds[newfd].refcount=1;
    cur->fdt.fds[newfd].flags=(uint16_t)(cur->fdt.fds[newfd].flags&~FDFL_CLOEXEC);
    c3_fd_group_copy_fd(cur,newfd);
    if(c3_task_is_firefox_ipc_trace(cur)){
        static uint32_t dup2_trace=0;
        if(dup2_trace<160){
            ++dup2_trace;
            __boot_serial_puts("[dup2-fd] pid=");
            __boot_serial_putu32((uint32_t)(cur?cur->pid:0));
            c3_trace_task_name(cur);
            __boot_serial_puts(" old=");
            __boot_serial_putu32((uint32_t)oldfd);
            __boot_serial_puts(" new=");
            __boot_serial_putu32((uint32_t)newfd);
            __boot_serial_puts("\n");
            c3_trace_one_fd("[dup2-fd-new]",cur,newfd);
        }
    }
    return newfd;
}
int64_t real_sys_dup3(int oldfd,int newfd,int flags){
    int64_t rc;
    if(flags&~O_CLOEXEC)return -EINVAL;
    if(oldfd==newfd)return -EINVAL;
    rc=real_sys_dup2(oldfd,newfd);
    if(rc>=0&&flags&O_CLOEXEC){
        task_t *cur=task_current();
        cur->fdt.fds[newfd].flags|=FDFL_CLOEXEC;
        c3_fd_group_copy_fd(cur,newfd);
    }
    return rc;
}

int64_t real_sys_pipe(int pipefd[2]){
    task_t *cur=task_current();
    int internal_fds[2];
    if(pipe_create(internal_fds)<0)return -ENOMEM;
    pipefd[0]=c3_fd_alloc_for_task(cur,FDKIND_PIPE_R,internal_fds[0]-500,FDFL_READABLE);
    pipefd[1]=c3_fd_alloc_for_task(cur,FDKIND_PIPE_W,internal_fds[1]-600,FDFL_WRITABLE);
    if(pipefd[0]<0||pipefd[1]<0)return -EMFILE;
    if(c3_task_is_firefox_ipc_trace(cur)){
        static uint32_t pipe_trace=0;
        if(pipe_trace<120){
            ++pipe_trace;
            __boot_serial_puts("[pipe-create] pid=");
            __boot_serial_putu32((uint32_t)(cur?cur->pid:0));
            c3_trace_task_name(cur);
            __boot_serial_puts(" rfd=");
            __boot_serial_putu32((uint32_t)pipefd[0]);
            __boot_serial_puts(" wfd=");
            __boot_serial_putu32((uint32_t)pipefd[1]);
            __boot_serial_puts(" ref=");
            __boot_serial_putu32((uint32_t)(internal_fds[0]-500));
            __boot_serial_puts(" trace=");
            __boot_serial_putu32(pipe_trace);
            __boot_serial_puts("\n");
        }
    }
    return 0;
}
int64_t real_sys_pipe2(int pipefd[2],int flags){
    int64_t rc;
    task_t *cur=task_current();
    if(flags&~(O_NONBLOCK|O_CLOEXEC))return -EINVAL;
    rc=real_sys_pipe(pipefd);
    if(rc<0)return rc;
    if(flags&O_NONBLOCK){
        cur->fdt.fds[pipefd[0]].flags|=FDFL_NONBLOCK;
        cur->fdt.fds[pipefd[1]].flags|=FDFL_NONBLOCK;
    }
    if(flags&O_CLOEXEC){
        cur->fdt.fds[pipefd[0]].flags|=FDFL_CLOEXEC;
        cur->fdt.fds[pipefd[1]].flags|=FDFL_CLOEXEC;
    }
    c3_fd_group_copy_fd(cur,pipefd[0]);
    c3_fd_group_copy_fd(cur,pipefd[1]);
    return 0;
}

int64_t real_sys_ioctl(int fd,uint64_t request,uint64_t arg){
    task_t *cur=task_current();
    if(!fd_valid(&cur->fdt,fd))return -EBADF;
    if(cur->fdt.fds[fd].kind==FDKIND_DEVTTY)return tty_ioctl(&g_tty0,request,(void*)(uintptr_t)arg);
    if(cur->fdt.fds[fd].kind==FDKIND_DEVFB){
        int rc=drm_ioctl_handler(fd,request,(void*)(uintptr_t)arg);
        if(rc!=-ENOTTY)return rc;
        return fb_ioctl_handler(fd,request,(void*)(uintptr_t)arg);
    }
    return -ENOTTY;
}

#define F_DUPFD    0
#define F_GETFD    1
#define F_SETFD    2
#define F_GETFL    3
#define F_SETFL    4
#define F_GETLK    5
#define F_SETLK    6
#define F_SETLKW   7
#define F_SETOWN   8
#define F_GETOWN   9
#define F_ADD_SEALS 1033
#define F_GET_SEALS 1034
#define F_DUPFD_CLOEXEC 1030
#define C3_F_OFD_GETLK  36
#define C3_F_OFD_SETLK  37
#define C3_F_OFD_SETLKW 38
#define FD_CLOEXEC 1
#define C3_F_UNLCK 2

typedef struct {
    int16_t l_type;
    int16_t l_whence;
    int64_t l_start;
    int64_t l_len;
    int32_t l_pid;
} c3_flock64_t;

static int c3_fd_linux_status_flags(const real_fd_t *fd){
    int out=0;
    if(!fd)return 0;
    if((fd->flags&FDFL_READABLE)&&(fd->flags&FDFL_WRITABLE))out|=O_RDWR;
    else if(fd->flags&FDFL_WRITABLE)out|=O_WRONLY;
    else out|=O_RDONLY;
    if(fd->flags&FDFL_APPEND)out|=O_APPEND;
    if(fd->flags&FDFL_NONBLOCK)out|=O_NONBLOCK;
    return out;
}

static void c3_trace_fcntl_ret(task_t *cur,int fd,int cmd,int64_t rc){
    static uint32_t ret_trace=0;
    if(!c3_task_is_firefox_ipc_trace(cur)||ret_trace>=220)return;
    ++ret_trace;
    __boot_serial_puts("[fcntl-ret] pid=");
    __boot_serial_putu32((uint32_t)(cur?cur->pid:0));
    c3_trace_task_name(cur);
    __boot_serial_puts(" fd=");
    __boot_serial_putu32((uint32_t)fd);
    __boot_serial_puts(" cmd=");
    __boot_serial_putu32((uint32_t)cmd);
    __boot_serial_puts(" rc=");
    if(rc<0){__boot_serial_puts("-");__boot_serial_putu32((uint32_t)(-rc));}
    else __boot_serial_puthex64((uint64_t)rc);
    __boot_serial_puts("\n");
    if((cmd==F_DUPFD||cmd==F_DUPFD_CLOEXEC)&&rc>=0&&rc<TASK_FD_MAX)
        c3_trace_one_fd("[fcntl-ret-fd]",cur,(int)rc);
}

int64_t real_sys_fcntl(int fd,int cmd,uint64_t arg){
    task_t *cur=task_current();
    c3_fd_group_sync_fd(cur,fd);
    if(!fd_valid(&cur->fdt,fd))return -EBADF;
    if(c3_task_is_firefox_ipc_trace(cur)){
        static uint32_t fcntl_trace=0;
        if(fcntl_trace<180){
            ++fcntl_trace;
            __boot_serial_puts("[fcntl-fd] pid=");
            __boot_serial_putu32((uint32_t)(cur?cur->pid:0));
            c3_trace_task_name(cur);
            __boot_serial_puts(" fd=");
            __boot_serial_putu32((uint32_t)fd);
            __boot_serial_puts(" cmd=");
            __boot_serial_putu32((uint32_t)cmd);
            __boot_serial_puts(" arg=");
            __boot_serial_puthex64(arg);
            __boot_serial_puts(" trace=");
            __boot_serial_putu32(fcntl_trace);
            __boot_serial_puts("\n");
            c3_trace_one_fd("[fcntl-fd-state]",cur,fd);
        }
    }
#define C3_FCNTL_RET(_v) do{ \
        int64_t _rc=(int64_t)(_v); \
        const char *_fp=0; \
        c3_trace_fcntl_ret(cur,fd,cmd,_rc); \
        if(cur->fdt.fds[fd].kind==FDKIND_VFSFILE||cur->fdt.fds[fd].kind==FDKIND_DIR) \
            _fp=c3_vfs_open_slot_path(cur->fdt.fds[fd].ref); \
        if(_fp)c3_trace_path_rc("fcntl",_fp,_rc,(uint64_t)(uint32_t)cmd,arg); \
        return _rc; \
    }while(0)
    switch(cmd){
        case F_DUPFD:
            C3_FCNTL_RET((arg>=(uint64_t)TASK_FD_MAX)?-EINVAL:c3_dup_fd_min(cur,fd,(int)arg,false));
        case F_DUPFD_CLOEXEC: {
            int64_t nfd=(arg>=(uint64_t)TASK_FD_MAX)?-EINVAL:c3_dup_fd_min(cur,fd,(int)arg,true);
            C3_FCNTL_RET(nfd);
        }
        case F_GETFD: C3_FCNTL_RET((cur->fdt.fds[fd].flags&FDFL_CLOEXEC)?FD_CLOEXEC:0);
        case F_SETFD:
            if(arg&FD_CLOEXEC)cur->fdt.fds[fd].flags|=FDFL_CLOEXEC;else cur->fdt.fds[fd].flags&=~FDFL_CLOEXEC;
            c3_fd_group_copy_fd(cur,fd);
            C3_FCNTL_RET(0);
        case F_GETFL: C3_FCNTL_RET((int64_t)c3_fd_linux_status_flags(&cur->fdt.fds[fd]));
        case F_SETFL: {
            uint16_t nb=((arg&FDFL_NONBLOCK)||(arg&O_NONBLOCK))?FDFL_NONBLOCK:0;
            uint16_t ap=((arg&FDFL_APPEND)||(arg&O_APPEND))?FDFL_APPEND:0;
            cur->fdt.fds[fd].flags=(uint16_t)((cur->fdt.fds[fd].flags&~(FDFL_APPEND|FDFL_NONBLOCK))|nb|ap);
            if(cur->fdt.fds[fd].kind==FDKIND_SOCKET&&cur->fdt.fds[fd].ref>=0&&cur->fdt.fds[fd].ref<SOCK_MAX)
                g_sockets[cur->fdt.fds[fd].ref].non_blocking=(nb!=0);
            c3_fd_group_copy_fd(cur,fd);
            C3_FCNTL_RET(0);
        }
        case F_GETLK:
        case C3_F_OFD_GETLK:
            if(arg){
                c3_flock64_t *lk=(c3_flock64_t*)(uintptr_t)arg;
                lk->l_type=C3_F_UNLCK;
                lk->l_pid=0;
            }
            C3_FCNTL_RET(0);
        case F_SETLK:
        case F_SETLKW:
        case C3_F_OFD_SETLK:
        case C3_F_OFD_SETLKW:
            C3_FCNTL_RET(0);
        case F_SETOWN:
            C3_FCNTL_RET(0);
        case F_GETOWN:
            C3_FCNTL_RET(0);
        case F_ADD_SEALS: {
            const char *mp=0;
            uint32_t seals=0;
            uint32_t add=(uint32_t)arg;
            if(add&~(C3_F_SEAL_SEAL|C3_F_SEAL_SHRINK|C3_F_SEAL_GROW|C3_F_SEAL_WRITE|C3_F_SEAL_FUTURE_WRITE))
                C3_FCNTL_RET(-EINVAL);
            if(!c3_memfd_fd_path(cur,fd,&mp)||!c3_memfd_path_get_seals(mp,&seals))
                C3_FCNTL_RET(-EINVAL);
            if(seals&C3_F_SEAL_SEAL)C3_FCNTL_RET(-EPERM);
            if(!c3_memfd_path_set_seals(mp,seals|add))C3_FCNTL_RET(-EINVAL);
            C3_FCNTL_RET(0);
        }
        case F_GET_SEALS: {
            const char *mp=0;
            uint32_t seals=0;
            if(!c3_memfd_fd_path(cur,fd,&mp)||!c3_memfd_path_get_seals(mp,&seals))
                C3_FCNTL_RET(-EINVAL);
            C3_FCNTL_RET((int64_t)seals);
        }
        }
    C3_FCNTL_RET(-EINVAL);
#undef C3_FCNTL_RET
}

static int64_t c3_stat_from_path(const char *path,kstat_t *st){
    const uint8_t *data=0;
    uint32_t size=0;
    uint64_t memfd_size=0;
    if(!st||!path)return -EFAULT;
    c3_memset(st,0,sizeof(*st));
    st->st_uid=(uint32_t)task_current()->uid;
    st->st_gid=(uint32_t)task_current()->gid;
    st->st_nlink=1;
    st->st_blksize=4096;
    st->st_mode=0100644;
    st->st_dev=1;
    st->st_ino=c3_path_inode(path);
    if(c3_path_is_dir(path)){
        st->st_mode=0040000|c3_path_mode_get(path,0755);
        st->st_nlink=2;
        st->st_size=0;
        return 0;
    }
    if(c3_is_known_ipc_socket_path(path)){
        st->st_mode=0140777;
        st->st_nlink=1;
        st->st_size=0;
        return 0;
    }
    if(c3_starts_with(path,"/dev/")){
        st->st_mode=0020666;
        st->st_rdev=0x8800;
        st->st_size=0;
        return 0;
    }
    if(c3_starts_with(path,"/proc/")||c3_starts_with(path,"/sys/")){
        if(!c3_virtual_path_exists(path))return -ENOENT;
        st->st_mode=0100444;
        st->st_size=0;
        return 0;
    }
    if(c3_memfd_path_get_size(path,&memfd_size)){
        st->st_mode=0100000|0600;
        st->st_size=(int64_t)memfd_size;
        st->st_blocks=(st->st_size+511)/512;
        return 0;
    }
    {
        const char *target=0;
        size_t tl=0;
        if(c3_kvfs_symlink_target(path,&target,&tl)){
            (void)target;
            st->st_mode=0120777;
            st->st_size=(int64_t)tl;
            st->st_blocks=(st->st_size+511)/512;
            return 0;
        }
    }
    if(kvfs_read(path,&data,&size)||kvfs_exists(path)){
        st->st_mode=0100000|c3_path_mode_get(path,0644);
        st->st_size=(int64_t)size;
        st->st_blocks=(st->st_size+511)/512;
        return 0;
    }
    return -ENOENT;
}

int64_t real_sys_fstat(int fd,kstat_t *st){
    task_t *cur=task_current();
    const char *vpath;
    if(!fd_valid(&cur->fdt,fd))return -EBADF;
    if(cur->fdt.fds[fd].kind==FDKIND_VFSFILE||cur->fdt.fds[fd].kind==FDKIND_DIR){
        vpath=c3_vfs_open_slot_path(cur->fdt.fds[fd].ref);
        if(!vpath)return -EBADF;
        return c3_stat_from_path(vpath,st);
    }
    return vfs_fstat(fd,st);
}
int64_t real_sys_stat(const char *path,kstat_t *st){
    task_t *cur=task_current();
    char npath[VFS_PATH_MAX];
    if(!path)return -EFAULT;
    if(c3_resolve_user_path(cur,AT_FDCWD,path,npath,sizeof(npath))<0)return -EINVAL;
    return c3_stat_from_path(npath,st);
}
int64_t real_sys_lstat(const char *path,kstat_t *st){return real_sys_stat(path,st);}

int64_t real_sys_access(const char *path,int mode){
    task_t *cur=task_current();
    char npath[VFS_PATH_MAX];
    int64_t rc=-ENOENT;
    (void)mode;
    if(!path)return -EFAULT;
    if(c3_resolve_user_path(cur,AT_FDCWD,path,npath,sizeof(npath))<0)return -EINVAL;
    if(c3_path_is_dir(npath))rc=0;
    else if(c3_is_known_ipc_socket_path(npath))rc=0;
    else if(c3_virtual_path_exists(npath))rc=0;
    else if(kvfs_exists(npath))rc=0;
    c3_trace_path_rc("access",npath,rc,(uint64_t)(uint32_t)mode,0);
    c3_trace_firefox_path_rc("access",npath,rc,(uint64_t)(uint32_t)mode,0);
    return rc;
}
int64_t real_sys_faccessat(int dirfd,const char *path,int mode,int flags){
    task_t *cur=task_current();
    char npath[VFS_PATH_MAX];
    int64_t rc=-ENOENT;
    const int C3_AT_EACCESS_LOCAL=0x200;
    const int C3_AT_SYMLINK_NOFOLLOW_LOCAL=0x100;
    const int C3_AT_EMPTY_PATH_LOCAL=0x1000;
    (void)mode;
    if(!path)return -EFAULT;
    if(flags&~(C3_AT_EACCESS_LOCAL|C3_AT_SYMLINK_NOFOLLOW_LOCAL|C3_AT_EMPTY_PATH_LOCAL))return -EINVAL;
    if(path[0]==0){
        if(!(flags&C3_AT_EMPTY_PATH_LOCAL))return -ENOENT;
        if(dirfd==AT_FDCWD)return 0;
        if(!fd_valid(&cur->fdt,dirfd))return -EBADF;
        return 0;
    }
    if(c3_resolve_user_path(cur,dirfd,path,npath,sizeof(npath))<0)return -EINVAL;
    if(c3_path_is_dir(npath))rc=0;
    else if(c3_is_known_ipc_socket_path(npath))rc=0;
    else if(c3_virtual_path_exists(npath))rc=0;
    else if(kvfs_exists(npath))rc=0;
    c3_trace_path_rc("faccessat",npath,rc,(uint64_t)(uint32_t)mode,(uint64_t)(uint32_t)flags);
    c3_trace_firefox_path_rc("faccessat",npath,rc,(uint64_t)(uint32_t)mode,(uint64_t)(uint32_t)flags);
    return rc;
}

int64_t real_sys_chmod(const char *path,int mode){
    task_t *cur=task_current();
    char npath[VFS_PATH_MAX];
    if(!path)return -EFAULT;
    if(c3_resolve_user_path(cur,AT_FDCWD,path,npath,sizeof(npath))<0)return -EINVAL;
    if(!c3_path_is_dir(npath)&&!c3_is_known_ipc_socket_path(npath)&&
       !c3_virtual_path_exists(npath)&&!kvfs_exists(npath))return -ENOENT;
    c3_path_mode_set(npath,(uint32_t)mode&07777u);
    return 0;
}

static bool c3_chown_path_exists(const char *path){
    return c3_path_is_dir(path)||c3_is_known_ipc_socket_path(path)||
           c3_virtual_path_exists(path)||kvfs_exists(path);
}

int64_t real_sys_chown(const char *path,int uid,int gid){
    task_t *cur=task_current();
    char npath[VFS_PATH_MAX];
    (void)uid;(void)gid;
    if(!path)return -EFAULT;
    if(c3_resolve_user_path(cur,AT_FDCWD,path,npath,sizeof(npath))<0)return -EINVAL;
    if(!c3_chown_path_exists(npath))return -ENOENT;
    return 0;
}

int64_t real_sys_lchown(const char *path,int uid,int gid){
    return real_sys_chown(path,uid,gid);
}

int64_t real_sys_fchown(int fd,int uid,int gid){
    task_t *cur=task_current();
    (void)uid;(void)gid;
    if(!cur||!fd_valid(&cur->fdt,fd))return -EBADF;
    return 0;
}

int64_t real_sys_fchmod(int fd,int mode){
    task_t *cur=task_current();
    const char *vpath;
    if(!cur||!fd_valid(&cur->fdt,fd))return -EBADF;
    if(cur->fdt.fds[fd].kind==FDKIND_VFSFILE||cur->fdt.fds[fd].kind==FDKIND_DIR){
        vpath=c3_vfs_open_slot_path(cur->fdt.fds[fd].ref);
        if(!vpath)return -EBADF;
        c3_path_mode_set(vpath,(uint32_t)mode&07777u);
        return 0;
    }
    return 0;
}

int64_t real_sys_fchownat(int dirfd,const char *path,int uid,int gid,int flags){
    task_t *cur=task_current();
    char npath[VFS_PATH_MAX];
    const int C3_AT_SYMLINK_NOFOLLOW_LOCAL=0x100;
    const int C3_AT_EMPTY_PATH_LOCAL=0x1000;
    (void)uid;(void)gid;
    if(flags&~(C3_AT_SYMLINK_NOFOLLOW_LOCAL|C3_AT_EMPTY_PATH_LOCAL))return -EINVAL;
    if(!path)return -EFAULT;
    if(path[0]==0){
        if(!(flags&C3_AT_EMPTY_PATH_LOCAL))return -ENOENT;
        if(dirfd==AT_FDCWD)return -EINVAL;
        return real_sys_fchown(dirfd,uid,gid);
    }
    if(c3_resolve_user_path(cur,dirfd,path,npath,sizeof(npath))<0)return -EINVAL;
    if(!c3_chown_path_exists(npath))return -ENOENT;
    return 0;
}

int64_t real_sys_fchmodat(int dirfd,const char *path,int mode,int flags){
    task_t *cur=task_current();
    char npath[VFS_PATH_MAX];
    const int C3_AT_SYMLINK_NOFOLLOW_LOCAL=0x100;
    const int C3_AT_EMPTY_PATH_LOCAL=0x1000;
    if(flags&~(C3_AT_SYMLINK_NOFOLLOW_LOCAL|C3_AT_EMPTY_PATH_LOCAL))return -EINVAL;
    if(!path)return -EFAULT;
    if(path[0]==0){
        if(!(flags&C3_AT_EMPTY_PATH_LOCAL))return -ENOENT;
        if(dirfd==AT_FDCWD)return -EINVAL;
        return real_sys_fchmod(dirfd,mode);
    }
    if(c3_resolve_user_path(cur,dirfd,path,npath,sizeof(npath))<0)return -EINVAL;
    return real_sys_chmod(npath,mode);
}

#define C3_DT_UNKNOWN 0
#define C3_DT_DIR     4
#define C3_DT_REG     8
#define C3_DT_LNK     10
#define C3_DT_SOCK    12

static int c3_getdents_emit(void *dirp,size_t count,size_t *written,uint64_t *seq,uint64_t off,const char *name,uint8_t d_type){
    linux_dirent64_t *de;
    if(!written||!seq||!name)return -EFAULT;
    if(*seq<off){++(*seq);return 0;}
    if(*written+sizeof(linux_dirent64_t)>count)return 1;
    de=(linux_dirent64_t*)((uint8_t*)dirp+*written);
    c3_memset(de,0,sizeof(*de));
    de->d_ino=*seq+1;
    de->d_off=(int64_t)(*seq+1);
    de->d_reclen=(uint16_t)sizeof(linux_dirent64_t);
    de->d_type=d_type;
    c3_strlcpy(de->d_name,name,sizeof(de->d_name));
    *written+=sizeof(linux_dirent64_t);
    ++(*seq);
    return 0;
}

int64_t real_sys_getdents64(int fd,void *dirp,size_t count){
    task_t *cur=task_current();
    const char *dpath;
    uint64_t off;
    uint64_t seq=0;
    size_t written=0;
    int rc;
    int proc_pid=-1;
    const char *proc_tail=0;
    if(!fd_valid(&cur->fdt,fd))return -EBADF;
    if(cur->fdt.fds[fd].kind!=FDKIND_DIR)return -ENOTDIR;
    if(!dirp)return -EFAULT;
    if(count<sizeof(linux_dirent64_t))return -EINVAL;
    dpath=c3_vfs_open_slot_path(cur->fdt.fds[fd].ref);
    if(!dpath)return -EBADF;
    off=cur->fdt.fds[fd].offset;

    rc=c3_getdents_emit(dirp,count,&written,&seq,off,".",C3_DT_DIR);
    if(rc<0)return rc;
    if(rc>0)goto dents_done;
    rc=c3_getdents_emit(dirp,count,&written,&seq,off,"..",C3_DT_DIR);
    if(rc<0)return rc;
    if(rc>0)goto dents_done;

    if(c3_strcmp(dpath,"/")==0){
        static const char *root_names[]={"dev","proc","sys","tmp","home","etc","run","usr","lib","lib64","opt","memfd",0};
        int i;
        for(i=0;root_names[i];++i){
            rc=c3_getdents_emit(dirp,count,&written,&seq,off,root_names[i],C3_DT_DIR);
            if(rc<0)return rc;
            if(rc>0)goto dents_done;
        }
    }else if(c3_strcmp(dpath,"/proc")==0){
        static const char *proc_names[]={"self","cpuinfo","meminfo","stat","version","uptime","mounts","filesystems","loadavg","cmdline","net","sys",0};
        int i;
        for(i=0;proc_names[i];++i){
            uint8_t t=(c3_strcmp(proc_names[i],"self")==0||c3_strcmp(proc_names[i],"net")==0||c3_strcmp(proc_names[i],"sys")==0)?C3_DT_DIR:C3_DT_REG;
            rc=c3_getdents_emit(dirp,count,&written,&seq,off,proc_names[i],t);
            if(rc<0)return rc;
            if(rc>0)goto dents_done;
        }
        for(i=0;i<TASK_MAX;++i){
            char pbuf[16];
            size_t pl=0;
            if(!g_tasks[i].used)continue;
            pbuf[0]=0;
            c3_append_u32(pbuf,&pl,sizeof(pbuf),(uint32_t)g_tasks[i].pid);
            rc=c3_getdents_emit(dirp,count,&written,&seq,off,pbuf,C3_DT_DIR);
            if(rc<0)return rc;
            if(rc>0)goto dents_done;
        }
    }else if(c3_parse_proc_pid_path(dpath,&proc_pid,&proc_tail)&&proc_tail&&proc_tail[0]==0){
        task_t *pt=c3_task_by_pid(proc_pid);
        static const char *pid_names[]={"status","stat","comm","maps","smaps","statm","cmdline","environ","auxv","io","limits","cgroup","exe","cwd","fd","mounts","mountinfo",0};
        int i;
        if(!pt)return -ENOENT;
        (void)pt;
        for(i=0;pid_names[i];++i){
            uint8_t t=(c3_strcmp(pid_names[i],"fd")==0)?C3_DT_DIR:((c3_strcmp(pid_names[i],"exe")==0||c3_strcmp(pid_names[i],"cwd")==0)?C3_DT_LNK:C3_DT_REG);
            rc=c3_getdents_emit(dirp,count,&written,&seq,off,pid_names[i],t);
            if(rc<0)return rc;
            if(rc>0)goto dents_done;
        }
    }else if(c3_parse_proc_pid_path(dpath,&proc_pid,&proc_tail)&&proc_tail&&c3_strcmp(proc_tail,"/fd")==0){
        task_t *pt=c3_task_by_pid(proc_pid);
        int i;
        char nbuf[16];
        if(!pt)return -ENOENT;
        for(i=0;i<TASK_FD_MAX;++i){
            size_t nl=0;
            if(pt->fdt.fds[i].kind==FDKIND_NONE)continue;
            nbuf[0]=0;
            c3_append_u32(nbuf,&nl,sizeof(nbuf),(uint32_t)i);
            rc=c3_getdents_emit(dirp,count,&written,&seq,off,nbuf,C3_DT_LNK);
            if(rc<0)return rc;
            if(rc>0)goto dents_done;
        }
    }else if(c3_strcmp(dpath,"/proc/self")==0){
        static const char *self_names[]={"status","stat","comm","maps","smaps","statm","cmdline","environ","auxv","io","limits","cgroup","exe","cwd","fd","task","mounts","mountinfo",0};
        int i;
        for(i=0;self_names[i];++i){
            uint8_t t=(c3_strcmp(self_names[i],"fd")==0||c3_strcmp(self_names[i],"task")==0)?C3_DT_DIR:((c3_strcmp(self_names[i],"exe")==0||c3_strcmp(self_names[i],"cwd")==0)?C3_DT_LNK:C3_DT_REG);
            rc=c3_getdents_emit(dirp,count,&written,&seq,off,self_names[i],t);
            if(rc<0)return rc;
            if(rc>0)goto dents_done;
        }
    }else if(c3_strcmp(dpath,"/proc/self/task")==0){
        int i;
        char nbuf[16];
        int tgid=cur->tgid?cur->tgid:cur->pid;
        for(i=0;i<TASK_MAX;++i){
            size_t nl=0;
            if(!g_tasks[i].used)continue;
            if(g_tasks[i].state==TASK_FREE||g_tasks[i].state==TASK_ZOMBIE)continue;
            if((g_tasks[i].tgid?g_tasks[i].tgid:g_tasks[i].pid)!=tgid)continue;
            nbuf[0]=0;
            c3_append_u32(nbuf,&nl,sizeof(nbuf),(uint32_t)g_tasks[i].pid);
            rc=c3_getdents_emit(dirp,count,&written,&seq,off,nbuf,C3_DT_DIR);
            if(rc<0)return rc;
            if(rc>0)goto dents_done;
        }
    }else{
        int task_tid=-1;
        const char *task_tail=0;
        if(c3_parse_proc_self_task_path(dpath,&task_tid,&task_tail)&&task_tail&&task_tail[0]==0){
            task_t *tt=c3_task_by_pid(task_tid);
            static const char *task_names[]={"status","stat","comm","maps","smaps","statm","cmdline","limits","fd",0};
            int i;
            if(!tt)return -ENOENT;
            (void)tt;
            for(i=0;task_names[i];++i){
                uint8_t t=(c3_strcmp(task_names[i],"fd")==0)?C3_DT_DIR:C3_DT_REG;
                rc=c3_getdents_emit(dirp,count,&written,&seq,off,task_names[i],t);
                if(rc<0)return rc;
                if(rc>0)goto dents_done;
            }
            goto dents_done;
        }
    }
    if(c3_strcmp(dpath,"/proc/self/fd")==0){
        int i;
        char nbuf[16];
        for(i=0;i<TASK_FD_MAX;++i){
            size_t nl=0;
            if(cur->fdt.fds[i].kind==FDKIND_NONE)continue;
            nbuf[0]=0;
            c3_append_u32(nbuf,&nl,sizeof(nbuf),(uint32_t)i);
            rc=c3_getdents_emit(dirp,count,&written,&seq,off,nbuf,C3_DT_LNK);
            if(rc<0)return rc;
            if(rc>0)goto dents_done;
        }
    }else if(c3_strcmp(dpath,"/proc/net")==0){
        rc=c3_getdents_emit(dirp,count,&written,&seq,off,"dev",C3_DT_REG);
        if(rc<0)return rc;
    if(rc>0)goto dents_done;
        rc=c3_getdents_emit(dirp,count,&written,&seq,off,"route",C3_DT_REG);
        if(rc<0)return rc;
    if(rc>0)goto dents_done;
        rc=c3_getdents_emit(dirp,count,&written,&seq,off,"unix",C3_DT_REG);
        if(rc<0)return rc;
    if(rc>0)goto dents_done;
    }else if(c3_strcmp(dpath,"/proc/sys")==0){
        rc=c3_getdents_emit(dirp,count,&written,&seq,off,"kernel",C3_DT_DIR);
        if(rc<0)return rc;
    if(rc>0)goto dents_done;
        rc=c3_getdents_emit(dirp,count,&written,&seq,off,"vm",C3_DT_DIR);
        if(rc<0)return rc;
    if(rc>0)goto dents_done;
        rc=c3_getdents_emit(dirp,count,&written,&seq,off,"net",C3_DT_DIR);
        if(rc<0)return rc;
    if(rc>0)goto dents_done;
    }else if(c3_strcmp(dpath,"/proc/sys/kernel")==0){
        rc=c3_getdents_emit(dirp,count,&written,&seq,off,"ostype",C3_DT_REG);
        if(rc<0)return rc;
    if(rc>0)goto dents_done;
        rc=c3_getdents_emit(dirp,count,&written,&seq,off,"hostname",C3_DT_REG);
        if(rc<0)return rc;
    if(rc>0)goto dents_done;
        rc=c3_getdents_emit(dirp,count,&written,&seq,off,"osrelease",C3_DT_REG);
        if(rc<0)return rc;
    if(rc>0)goto dents_done;
        rc=c3_getdents_emit(dirp,count,&written,&seq,off,"pid_max",C3_DT_REG);
        if(rc<0)return rc;
    if(rc>0)goto dents_done;
        rc=c3_getdents_emit(dirp,count,&written,&seq,off,"threads-max",C3_DT_REG);
        if(rc<0)return rc;
    if(rc>0)goto dents_done;
        rc=c3_getdents_emit(dirp,count,&written,&seq,off,"random",C3_DT_DIR);
        if(rc<0)return rc;
    if(rc>0)goto dents_done;
    }else if(c3_strcmp(dpath,"/proc/sys/kernel/random")==0){
        rc=c3_getdents_emit(dirp,count,&written,&seq,off,"boot_id",C3_DT_REG);
        if(rc<0)return rc;
    if(rc>0)goto dents_done;
        rc=c3_getdents_emit(dirp,count,&written,&seq,off,"uuid",C3_DT_REG);
        if(rc<0)return rc;
    if(rc>0)goto dents_done;
    }else if(c3_strcmp(dpath,"/proc/sys/vm")==0){
        rc=c3_getdents_emit(dirp,count,&written,&seq,off,"overcommit_memory",C3_DT_REG);
        if(rc<0)return rc;
    if(rc>0)goto dents_done;
        rc=c3_getdents_emit(dirp,count,&written,&seq,off,"max_map_count",C3_DT_REG);
        if(rc<0)return rc;
    if(rc>0)goto dents_done;
        rc=c3_getdents_emit(dirp,count,&written,&seq,off,"swappiness",C3_DT_REG);
        if(rc<0)return rc;
    if(rc>0)goto dents_done;
    }else if(c3_strcmp(dpath,"/proc/sys/net")==0){
        rc=c3_getdents_emit(dirp,count,&written,&seq,off,"core",C3_DT_DIR);
        if(rc<0)return rc;
    if(rc>0)goto dents_done;
        rc=c3_getdents_emit(dirp,count,&written,&seq,off,"ipv4",C3_DT_DIR);
        if(rc<0)return rc;
    if(rc>0)goto dents_done;
    }else if(c3_strcmp(dpath,"/proc/sys/net/core")==0){
        rc=c3_getdents_emit(dirp,count,&written,&seq,off,"somaxconn",C3_DT_REG);
        if(rc<0)return rc;
    if(rc>0)goto dents_done;
    }else if(c3_strcmp(dpath,"/proc/sys/net/ipv4")==0){
        rc=c3_getdents_emit(dirp,count,&written,&seq,off,"ip_local_port_range",C3_DT_REG);
        if(rc<0)return rc;
    if(rc>0)goto dents_done;
    }else if(c3_strcmp(dpath,"/sys")==0){
        rc=c3_getdents_emit(dirp,count,&written,&seq,off,"kernel",C3_DT_DIR);
        if(rc<0)return rc;
    if(rc>0)goto dents_done;
        rc=c3_getdents_emit(dirp,count,&written,&seq,off,"devices",C3_DT_DIR);
        if(rc<0)return rc;
    if(rc>0)goto dents_done;
        rc=c3_getdents_emit(dirp,count,&written,&seq,off,"class",C3_DT_DIR);
        if(rc<0)return rc;
    if(rc>0)goto dents_done;
        rc=c3_getdents_emit(dirp,count,&written,&seq,off,"fs",C3_DT_DIR);
        if(rc<0)return rc;
    if(rc>0)goto dents_done;
        rc=c3_getdents_emit(dirp,count,&written,&seq,off,"module",C3_DT_DIR);
        if(rc<0)return rc;
    if(rc>0)goto dents_done;
        rc=c3_getdents_emit(dirp,count,&written,&seq,off,"firmware",C3_DT_DIR);
        if(rc<0)return rc;
    if(rc>0)goto dents_done;
    }else if(c3_strcmp(dpath,"/sys/kernel")==0){
        rc=c3_getdents_emit(dirp,count,&written,&seq,off,"ostype",C3_DT_REG);
        if(rc<0)return rc;
    if(rc>0)goto dents_done;
        rc=c3_getdents_emit(dirp,count,&written,&seq,off,"hostname",C3_DT_REG);
        if(rc<0)return rc;
    if(rc>0)goto dents_done;
        rc=c3_getdents_emit(dirp,count,&written,&seq,off,"osrelease",C3_DT_REG);
        if(rc<0)return rc;
    if(rc>0)goto dents_done;
        rc=c3_getdents_emit(dirp,count,&written,&seq,off,"mm",C3_DT_DIR);
        if(rc<0)return rc;
    if(rc>0)goto dents_done;
        rc=c3_getdents_emit(dirp,count,&written,&seq,off,"security",C3_DT_DIR);
        if(rc<0)return rc;
    if(rc>0)goto dents_done;
        rc=c3_getdents_emit(dirp,count,&written,&seq,off,"debug",C3_DT_DIR);
        if(rc<0)return rc;
    if(rc>0)goto dents_done;
    }else if(c3_strcmp(dpath,"/sys/kernel/mm")==0){
        rc=c3_getdents_emit(dirp,count,&written,&seq,off,"transparent_hugepage",C3_DT_DIR);
        if(rc<0)return rc;
    if(rc>0)goto dents_done;
    }else if(c3_strcmp(dpath,"/sys/kernel/mm/transparent_hugepage")==0){
        rc=c3_getdents_emit(dirp,count,&written,&seq,off,"enabled",C3_DT_REG);
        if(rc<0)return rc;
    if(rc>0)goto dents_done;
        rc=c3_getdents_emit(dirp,count,&written,&seq,off,"defrag",C3_DT_REG);
        if(rc<0)return rc;
    if(rc>0)goto dents_done;
    }else if(c3_strcmp(dpath,"/sys/kernel/security")==0){
        rc=c3_getdents_emit(dirp,count,&written,&seq,off,"lsm",C3_DT_REG);
        if(rc<0)return rc;
    if(rc>0)goto dents_done;
    }else if(c3_strcmp(dpath,"/sys/kernel/debug")==0){
        rc=c3_getdents_emit(dirp,count,&written,&seq,off,"tracing",C3_DT_DIR);
        if(rc<0)return rc;
    if(rc>0)goto dents_done;
    }else if(c3_strcmp(dpath,"/sys/kernel/debug/tracing")==0){
        rc=c3_getdents_emit(dirp,count,&written,&seq,off,"trace_clock",C3_DT_REG);
        if(rc<0)return rc;
    if(rc>0)goto dents_done;
    }else if(c3_strcmp(dpath,"/sys/devices")==0){
        rc=c3_getdents_emit(dirp,count,&written,&seq,off,"system",C3_DT_DIR);
        if(rc<0)return rc;
    if(rc>0)goto dents_done;
        rc=c3_getdents_emit(dirp,count,&written,&seq,off,"virtual",C3_DT_DIR);
        if(rc<0)return rc;
    if(rc>0)goto dents_done;
    }else if(c3_strcmp(dpath,"/sys/devices/system")==0){
        rc=c3_getdents_emit(dirp,count,&written,&seq,off,"cpu",C3_DT_DIR);
        if(rc<0)return rc;
    if(rc>0)goto dents_done;
        rc=c3_getdents_emit(dirp,count,&written,&seq,off,"memory",C3_DT_DIR);
        if(rc<0)return rc;
    if(rc>0)goto dents_done;
        rc=c3_getdents_emit(dirp,count,&written,&seq,off,"node",C3_DT_DIR);
        if(rc<0)return rc;
    if(rc>0)goto dents_done;
    }else if(c3_strcmp(dpath,"/sys/devices/system/cpu")==0){
        rc=c3_getdents_emit(dirp,count,&written,&seq,off,"online",C3_DT_REG);
        if(rc<0)return rc;
    if(rc>0)goto dents_done;
        rc=c3_getdents_emit(dirp,count,&written,&seq,off,"possible",C3_DT_REG);
        if(rc<0)return rc;
    if(rc>0)goto dents_done;
        rc=c3_getdents_emit(dirp,count,&written,&seq,off,"present",C3_DT_REG);
        if(rc<0)return rc;
    if(rc>0)goto dents_done;
        rc=c3_getdents_emit(dirp,count,&written,&seq,off,"cpu0",C3_DT_DIR);
        if(rc<0)return rc;
    if(rc>0)goto dents_done;
    }else if(c3_strcmp(dpath,"/sys/devices/system/cpu/cpu0")==0){
        rc=c3_getdents_emit(dirp,count,&written,&seq,off,"topology",C3_DT_DIR);
        if(rc<0)return rc;
    if(rc>0)goto dents_done;
        rc=c3_getdents_emit(dirp,count,&written,&seq,off,"cpufreq",C3_DT_DIR);
        if(rc<0)return rc;
    if(rc>0)goto dents_done;
    }else if(c3_strcmp(dpath,"/sys/devices/system/cpu/cpu0/topology")==0){
        rc=c3_getdents_emit(dirp,count,&written,&seq,off,"core_id",C3_DT_REG);
        if(rc<0)return rc;
    if(rc>0)goto dents_done;
        rc=c3_getdents_emit(dirp,count,&written,&seq,off,"physical_package_id",C3_DT_REG);
        if(rc<0)return rc;
    if(rc>0)goto dents_done;
    }else if(c3_strcmp(dpath,"/sys/devices/system/cpu/cpu0/cpufreq")==0){
        rc=c3_getdents_emit(dirp,count,&written,&seq,off,"scaling_cur_freq",C3_DT_REG);
        if(rc<0)return rc;
    if(rc>0)goto dents_done;
    }else if(c3_strcmp(dpath,"/sys/devices/system/memory")==0){
        rc=c3_getdents_emit(dirp,count,&written,&seq,off,"block_size_bytes",C3_DT_REG);
        if(rc<0)return rc;
    if(rc>0)goto dents_done;
    }else if(c3_strcmp(dpath,"/sys/devices/system/node")==0){
        rc=c3_getdents_emit(dirp,count,&written,&seq,off,"online",C3_DT_REG);
        if(rc<0)return rc;
    if(rc>0)goto dents_done;
    }else if(c3_strcmp(dpath,"/sys/devices/virtual")==0){
        rc=c3_getdents_emit(dirp,count,&written,&seq,off,"dmi",C3_DT_DIR);
        if(rc<0)return rc;
    if(rc>0)goto dents_done;
    }else if(c3_strcmp(dpath,"/sys/devices/virtual/dmi")==0){
        rc=c3_getdents_emit(dirp,count,&written,&seq,off,"id",C3_DT_DIR);
        if(rc<0)return rc;
    if(rc>0)goto dents_done;
    }else if(c3_strcmp(dpath,"/sys/devices/virtual/dmi/id")==0){
        rc=c3_getdents_emit(dirp,count,&written,&seq,off,"product_name",C3_DT_REG);
        if(rc<0)return rc;
    if(rc>0)goto dents_done;
        rc=c3_getdents_emit(dirp,count,&written,&seq,off,"sys_vendor",C3_DT_REG);
        if(rc<0)return rc;
    if(rc>0)goto dents_done;
        rc=c3_getdents_emit(dirp,count,&written,&seq,off,"board_name",C3_DT_REG);
        if(rc<0)return rc;
    if(rc>0)goto dents_done;
    }else if(c3_strcmp(dpath,"/sys/class")==0){
        rc=c3_getdents_emit(dirp,count,&written,&seq,off,"drm",C3_DT_DIR);
        if(rc<0)return rc;
    if(rc>0)goto dents_done;
        rc=c3_getdents_emit(dirp,count,&written,&seq,off,"net",C3_DT_DIR);
        if(rc<0)return rc;
    if(rc>0)goto dents_done;
        rc=c3_getdents_emit(dirp,count,&written,&seq,off,"graphics",C3_DT_DIR);
        if(rc<0)return rc;
    if(rc>0)goto dents_done;
        rc=c3_getdents_emit(dirp,count,&written,&seq,off,"dmi",C3_DT_DIR);
        if(rc<0)return rc;
    if(rc>0)goto dents_done;
        rc=c3_getdents_emit(dirp,count,&written,&seq,off,"input",C3_DT_DIR);
        if(rc<0)return rc;
    if(rc>0)goto dents_done;
    }else if(c3_strcmp(dpath,"/sys/class/drm")==0){
        rc=c3_getdents_emit(dirp,count,&written,&seq,off,"card0",C3_DT_DIR);
        if(rc<0)return rc;
    if(rc>0)goto dents_done;
        rc=c3_getdents_emit(dirp,count,&written,&seq,off,"renderD128",C3_DT_DIR);
        if(rc<0)return rc;
    if(rc>0)goto dents_done;
    }else if(c3_strcmp(dpath,"/sys/class/drm/card0")==0){
        rc=c3_getdents_emit(dirp,count,&written,&seq,off,"device",C3_DT_DIR);
        if(rc<0)return rc;
    if(rc>0)goto dents_done;
    }else if(c3_strcmp(dpath,"/sys/class/drm/card0/device")==0){
        rc=c3_getdents_emit(dirp,count,&written,&seq,off,"vendor",C3_DT_REG);
        if(rc<0)return rc;
    if(rc>0)goto dents_done;
        rc=c3_getdents_emit(dirp,count,&written,&seq,off,"device",C3_DT_REG);
        if(rc<0)return rc;
    if(rc>0)goto dents_done;
        rc=c3_getdents_emit(dirp,count,&written,&seq,off,"subsystem_vendor",C3_DT_REG);
        if(rc<0)return rc;
    if(rc>0)goto dents_done;
        rc=c3_getdents_emit(dirp,count,&written,&seq,off,"subsystem_device",C3_DT_REG);
        if(rc<0)return rc;
    if(rc>0)goto dents_done;
        rc=c3_getdents_emit(dirp,count,&written,&seq,off,"uevent",C3_DT_REG);
        if(rc<0)return rc;
    if(rc>0)goto dents_done;
        rc=c3_getdents_emit(dirp,count,&written,&seq,off,"driver",C3_DT_DIR);
        if(rc<0)return rc;
    if(rc>0)goto dents_done;
    }else if(c3_strcmp(dpath,"/sys/class/drm/card0/device/driver")==0){
        rc=c3_getdents_emit(dirp,count,&written,&seq,off,"module",C3_DT_DIR);
        if(rc<0)return rc;
    if(rc>0)goto dents_done;
    }else if(c3_strcmp(dpath,"/sys/class/drm/card0/device/driver/module")==0){
        rc=c3_getdents_emit(dirp,count,&written,&seq,off,"version",C3_DT_REG);
        if(rc<0)return rc;
    if(rc>0)goto dents_done;
    }else if(c3_strcmp(dpath,"/sys/class/drm/renderD128")==0){
        rc=c3_getdents_emit(dirp,count,&written,&seq,off,"device",C3_DT_DIR);
        if(rc<0)return rc;
    if(rc>0)goto dents_done;
    }else if(c3_strcmp(dpath,"/sys/class/drm/renderD128/device")==0){
        rc=c3_getdents_emit(dirp,count,&written,&seq,off,"uevent",C3_DT_REG);
        if(rc<0)return rc;
    if(rc>0)goto dents_done;
    }else if(c3_strcmp(dpath,"/sys/class/net")==0){
        rc=c3_getdents_emit(dirp,count,&written,&seq,off,"lo",C3_DT_DIR);
        if(rc<0)return rc;
    if(rc>0)goto dents_done;
        rc=c3_getdents_emit(dirp,count,&written,&seq,off,"eth0",C3_DT_DIR);
        if(rc<0)return rc;
    if(rc>0)goto dents_done;
    }else if(c3_strcmp(dpath,"/sys/class/net/lo")==0){
        rc=c3_getdents_emit(dirp,count,&written,&seq,off,"address",C3_DT_REG);
        if(rc<0)return rc;
    if(rc>0)goto dents_done;
        rc=c3_getdents_emit(dirp,count,&written,&seq,off,"operstate",C3_DT_REG);
        if(rc<0)return rc;
    if(rc>0)goto dents_done;
        rc=c3_getdents_emit(dirp,count,&written,&seq,off,"mtu",C3_DT_REG);
        if(rc<0)return rc;
    if(rc>0)goto dents_done;
    }else if(c3_strcmp(dpath,"/sys/class/net/eth0")==0){
        rc=c3_getdents_emit(dirp,count,&written,&seq,off,"address",C3_DT_REG);
        if(rc<0)return rc;
    if(rc>0)goto dents_done;
        rc=c3_getdents_emit(dirp,count,&written,&seq,off,"operstate",C3_DT_REG);
        if(rc<0)return rc;
    if(rc>0)goto dents_done;
        rc=c3_getdents_emit(dirp,count,&written,&seq,off,"mtu",C3_DT_REG);
        if(rc<0)return rc;
    if(rc>0)goto dents_done;
        rc=c3_getdents_emit(dirp,count,&written,&seq,off,"speed",C3_DT_REG);
        if(rc<0)return rc;
    if(rc>0)goto dents_done;
        rc=c3_getdents_emit(dirp,count,&written,&seq,off,"carrier",C3_DT_REG);
        if(rc<0)return rc;
    if(rc>0)goto dents_done;
        rc=c3_getdents_emit(dirp,count,&written,&seq,off,"duplex",C3_DT_REG);
        if(rc<0)return rc;
    if(rc>0)goto dents_done;
    }else if(c3_strcmp(dpath,"/sys/class/graphics")==0){
        rc=c3_getdents_emit(dirp,count,&written,&seq,off,"fb0",C3_DT_DIR);
        if(rc<0)return rc;
    if(rc>0)goto dents_done;
    }else if(c3_strcmp(dpath,"/sys/class/graphics/fb0")==0){
        rc=c3_getdents_emit(dirp,count,&written,&seq,off,"name",C3_DT_REG);
        if(rc<0)return rc;
    if(rc>0)goto dents_done;
        rc=c3_getdents_emit(dirp,count,&written,&seq,off,"modes",C3_DT_REG);
        if(rc<0)return rc;
    if(rc>0)goto dents_done;
        rc=c3_getdents_emit(dirp,count,&written,&seq,off,"virtual_size",C3_DT_REG);
        if(rc<0)return rc;
    if(rc>0)goto dents_done;
        rc=c3_getdents_emit(dirp,count,&written,&seq,off,"bits_per_pixel",C3_DT_REG);
        if(rc<0)return rc;
    if(rc>0)goto dents_done;
    }else if(c3_strcmp(dpath,"/sys/class/dmi")==0){
        rc=c3_getdents_emit(dirp,count,&written,&seq,off,"id",C3_DT_DIR);
        if(rc<0)return rc;
    if(rc>0)goto dents_done;
    }else if(c3_strcmp(dpath,"/sys/class/dmi/id")==0){
        rc=c3_getdents_emit(dirp,count,&written,&seq,off,"product_name",C3_DT_REG);
        if(rc<0)return rc;
    if(rc>0)goto dents_done;
        rc=c3_getdents_emit(dirp,count,&written,&seq,off,"sys_vendor",C3_DT_REG);
        if(rc<0)return rc;
    if(rc>0)goto dents_done;
        rc=c3_getdents_emit(dirp,count,&written,&seq,off,"board_name",C3_DT_REG);
        if(rc<0)return rc;
    if(rc>0)goto dents_done;
    }else if(c3_strcmp(dpath,"/sys/fs")==0){
        rc=c3_getdents_emit(dirp,count,&written,&seq,off,"cgroup",C3_DT_DIR);
        if(rc<0)return rc;
    if(rc>0)goto dents_done;
        rc=c3_getdents_emit(dirp,count,&written,&seq,off,"selinux",C3_DT_DIR);
        if(rc<0)return rc;
    if(rc>0)goto dents_done;
    }else if(c3_strcmp(dpath,"/sys/fs/cgroup")==0){
        rc=c3_getdents_emit(dirp,count,&written,&seq,off,"cgroup.controllers",C3_DT_REG);
        if(rc<0)return rc;
    if(rc>0)goto dents_done;
        rc=c3_getdents_emit(dirp,count,&written,&seq,off,"cgroup.subtree_control",C3_DT_REG);
        if(rc<0)return rc;
    if(rc>0)goto dents_done;
        rc=c3_getdents_emit(dirp,count,&written,&seq,off,"cgroup.procs",C3_DT_REG);
        if(rc<0)return rc;
    if(rc>0)goto dents_done;
    }else if(c3_strcmp(dpath,"/sys/fs/selinux")==0){
        rc=c3_getdents_emit(dirp,count,&written,&seq,off,"enforce",C3_DT_REG);
        if(rc<0)return rc;
    if(rc>0)goto dents_done;
    }else if(c3_strcmp(dpath,"/sys/module")==0){
        rc=c3_getdents_emit(dirp,count,&written,&seq,off,"i915",C3_DT_DIR);
        if(rc<0)return rc;
    if(rc>0)goto dents_done;
        rc=c3_getdents_emit(dirp,count,&written,&seq,off,"amdgpu",C3_DT_DIR);
        if(rc<0)return rc;
    if(rc>0)goto dents_done;
        rc=c3_getdents_emit(dirp,count,&written,&seq,off,"nvidia",C3_DT_DIR);
        if(rc<0)return rc;
    if(rc>0)goto dents_done;
    }else if(c3_strcmp(dpath,"/sys/module/i915")==0||
              c3_strcmp(dpath,"/sys/module/amdgpu")==0||
              c3_strcmp(dpath,"/sys/module/nvidia")==0){
        rc=c3_getdents_emit(dirp,count,&written,&seq,off,"version",C3_DT_REG);
        if(rc<0)return rc;
    if(rc>0)goto dents_done;
    }else if(c3_strcmp(dpath,"/dev")==0){
        static const char *dev_names[]={"null","zero","random","urandom","tty","tty0","console","ptmx","pts","fb0","dri","stdin","stdout","stderr",0};
        int i;
        for(i=0;dev_names[i];++i){
            uint8_t t=(c3_strcmp(dev_names[i],"pts")==0||c3_strcmp(dev_names[i],"dri")==0)?C3_DT_DIR:C3_DT_REG;
            rc=c3_getdents_emit(dirp,count,&written,&seq,off,dev_names[i],t);
            if(rc<0)return rc;
            if(rc>0)goto dents_done;
        }
    }else if(c3_strcmp(dpath,"/dev/pts")==0){
        rc=c3_getdents_emit(dirp,count,&written,&seq,off,"0",C3_DT_REG);
        if(rc<0)return rc;
    if(rc>0)goto dents_done;
    }else if(c3_strcmp(dpath,"/dev/dri")==0){
        rc=c3_getdents_emit(dirp,count,&written,&seq,off,"card0",C3_DT_REG);
        if(rc<0)return rc;
    if(rc>0)goto dents_done;
        rc=c3_getdents_emit(dirp,count,&written,&seq,off,"renderD128",C3_DT_REG);
        if(rc<0)return rc;
    if(rc>0)goto dents_done;
    }else if(c3_strcmp(dpath,"/run")==0){
        rc=c3_getdents_emit(dirp,count,&written,&seq,off,"user",C3_DT_DIR);
        if(rc<0)return rc;
    if(rc>0)goto dents_done;
        rc=c3_getdents_emit(dirp,count,&written,&seq,off,"dbus",C3_DT_DIR);
        if(rc<0)return rc;
    if(rc>0)goto dents_done;
    }else if(c3_strcmp(dpath,"/run/user")==0){
        rc=c3_getdents_emit(dirp,count,&written,&seq,off,"0",C3_DT_DIR);
        if(rc<0)return rc;
    if(rc>0)goto dents_done;
    }else if(c3_strcmp(dpath,"/run/user/0")==0){
        rc=c3_getdents_emit(dirp,count,&written,&seq,off,"wayland-0",C3_DT_SOCK);
        if(rc<0)return rc;
    if(rc>0)goto dents_done;
        rc=c3_getdents_emit(dirp,count,&written,&seq,off,"bus",C3_DT_SOCK);
        if(rc<0)return rc;
    if(rc>0)goto dents_done;
    }else if(c3_strcmp(dpath,"/tmp")==0){
        rc=c3_getdents_emit(dirp,count,&written,&seq,off,".X11-unix",C3_DT_DIR);
        if(rc<0)return rc;
    if(rc>0)goto dents_done;
    }else if(c3_strcmp(dpath,"/tmp/.X11-unix")==0){
        rc=c3_getdents_emit(dirp,count,&written,&seq,off,"X0",C3_DT_SOCK);
        if(rc<0)return rc;
    if(rc>0)goto dents_done;
    }

    {
        char list[2048];
        size_t p=0;
        list[0]=0;
        if(kvfs_listdir(dpath,list,sizeof(list))){
            while(list[p]){
                char name[256];
                size_t nl=0;
                char child[VFS_PATH_MAX*2];
                char child_norm[VFS_PATH_MAX*2];
                uint8_t dt=C3_DT_REG;
                while(list[p]&&list[p]!='\n'&&nl+1<sizeof(name))name[nl++]=list[p++];
                name[nl]=0;
                while(list[p]=='\n')++p;
                if(!name[0]||c3_dirent_is_marker(name))continue;
                c3_path_build(dpath,name,child,sizeof(child));
                c3_path_normalize_abs(child,child_norm,sizeof(child_norm));
                if(c3_path_is_dir(child_norm))dt=C3_DT_DIR;
                rc=c3_getdents_emit(dirp,count,&written,&seq,off,name,dt);
                if(rc<0)return rc;
                if(rc>0)break;
            }
        }
    }

dents_done:
    cur->fdt.fds[fd].offset=seq;
    c3_fd_group_copy_fd(cur,fd);
    return (int64_t)written;
}

int64_t real_sys_readlink(const char *path,char *buf,size_t bufsiz){
    task_t *cur=task_current();
    char npath[VFS_PATH_MAX];
    int proc_pid=-1;
    const char *proc_tail=0;
    if(!path||!buf||bufsiz==0)return -EFAULT;
    if(c3_resolve_user_path(cur,AT_FDCWD,path,npath,sizeof(npath))<0)return -EINVAL;
    if(c3_parse_proc_pid_path(npath,&proc_pid,&proc_tail)&&proc_tail&&proc_tail[0]=='/'){
        task_t *pt=c3_task_by_pid(proc_pid);
        if(!pt)return -ESRCH;
        if(c3_strcmp(proc_tail,"/exe")==0){
            char tmp[256];size_t l=0;
            const char *ep=c3_task_exec_path(pt);
            if(!ep)ep="/usr/bin/browser";
            c3_append_str(tmp,&l,sizeof(tmp),ep);
            if(l>bufsiz)l=bufsiz;
            c3_memcpy(buf,tmp,l);
            return(int64_t)l;
        }
        if(c3_strcmp(proc_tail,"/cwd")==0){
            size_t l=c3_strlen(pt->cwd);
            if(l>bufsiz)l=bufsiz;
            c3_memcpy(buf,pt->cwd,l);
            return(int64_t)l;
        }
        if(c3_starts_with(proc_tail,"/fd/")){
            int fdn=0;
            size_t i=4;
            const char *target=0;
            while(proc_tail[i]>='0'&&proc_tail[i]<='9'){
                fdn=fdn*10+(int)(proc_tail[i]-'0');
                ++i;
            }
            if(proc_tail[i]!=0)return -EINVAL;
            if(fdn<0||fdn>=TASK_FD_MAX||pt->fdt.fds[fdn].kind==FDKIND_NONE)return -EBADF;
            switch(pt->fdt.fds[fdn].kind){
                case FDKIND_VFSFILE:
                case FDKIND_DIR:
                    target=c3_vfs_open_slot_path(pt->fdt.fds[fdn].ref);
                    break;
                case FDKIND_DEVNULL: target="/dev/null"; break;
                case FDKIND_DEVZERO: target="/dev/zero"; break;
                case FDKIND_DEVRANDOM: target="/dev/urandom"; break;
                case FDKIND_DEVTTY: target="/dev/tty"; break;
                case FDKIND_DEVFB: target="/dev/fb0"; break;
                case FDKIND_EVENTFD: target="anon_inode:[eventfd]"; break;
                case FDKIND_SIGNALFD: target="anon_inode:[signalfd]"; break;
                case FDKIND_TIMERFD: target="anon_inode:[timerfd]"; break;
                case FDKIND_INOTIFY: target="anon_inode:[inotify]"; break;
                case FDKIND_EPOLL: target="anon_inode:[eventpoll]"; break;
                case FDKIND_PIPE_R:
                case FDKIND_PIPE_W: target="pipe:[1]"; break;
                case FDKIND_SOCKET: target="socket:[1]"; break;
                case FDKIND_PROC: target="/proc/self"; break;
                default: target=0; break;
            }
            if(!target)return -EINVAL;
            {
                size_t l=c3_strlen(target);
                if(l>bufsiz)l=bufsiz;
                c3_memcpy(buf,target,l);
                return(int64_t)l;
            }
        }
    }
    if(c3_strcmp(npath,"/proc/self/exe")==0){
        char tmp[256];size_t l=0;
        const char *ep=c3_task_exec_path(cur);
        if(!ep)ep="/usr/bin/browser";
        c3_append_str(tmp,&l,sizeof(tmp),ep);
        if(l>bufsiz)l=bufsiz;
        c3_memcpy(buf,tmp,l);
        return(int64_t)l;
    }
    if(c3_strcmp(npath,"/proc/self/cwd")==0){
        size_t l=c3_strlen(cur->cwd);
        if(l>bufsiz)l=bufsiz;
        c3_memcpy(buf,cur->cwd,l);
        return(int64_t)l;
    }
    if(c3_starts_with(npath,"/proc/self/fd/")){
        char p[64];
        size_t l=0;
        c3_append_str(p,&l,sizeof(p),"/proc/");
        c3_append_u32(p,&l,sizeof(p),(uint32_t)cur->pid);
        c3_append_str(p,&l,sizeof(p),npath+10);
        return real_sys_readlink(p,buf,bufsiz);
    }
    {
        const char *target=0;
        size_t tl=0;
        if(c3_kvfs_symlink_target(npath,&target,&tl)){
            if(tl>bufsiz)tl=bufsiz;
            c3_memcpy(buf,target,tl);
            return(int64_t)tl;
        }
    }
    if(c3_path_is_dir(npath)||c3_is_known_ipc_socket_path(npath)||
       c3_virtual_path_exists(npath)||c3_starts_with(npath,"/dev/")){
        c3_trace_path_rc("readlink",npath,-EINVAL,(uint64_t)bufsiz,0);
        return -EINVAL;
    }
    if(kvfs_exists(npath)){
        c3_trace_path_rc("readlink",npath,-EINVAL,(uint64_t)bufsiz,0);
        return -EINVAL;
    }
    c3_trace_path_rc("readlink",npath,-ENOENT,(uint64_t)bufsiz,0);
    return -ENOENT;
}

static int c3_resolve_proc_exe_exec_path(task_t *cur,const char *path,char *out,size_t cap){
    int proc_pid=-1;
    const char *proc_tail=0;
    task_t *pt=0;
    const char *ep=0;
    if(!path||!out||cap==0)return 0;
    if(c3_strcmp(path,"/proc/self/exe")==0){
        pt=cur;
    }else if(c3_parse_proc_pid_path(path,&proc_pid,&proc_tail)&&proc_tail&&c3_strcmp(proc_tail,"/exe")==0){
        pt=c3_task_by_pid(proc_pid);
        if(!pt)return -ESRCH;
    }else{
        return 0;
    }
    ep=c3_task_exec_path(pt);
    if(!ep||!ep[0])return -ENOENT;
    if(c3_strlen(ep)+1>cap)return -EINVAL;
    c3_strlcpy(out,ep,cap);
    return 1;
}

int64_t real_sys_getcwd(char *buf,size_t size){
    task_t *cur=task_current();
    c3_strlcpy(buf,cur->cwd,size);
    return(int64_t)c3_strlen(buf)+1;
}
int64_t real_sys_chdir(const char *path){
    task_t *cur=task_current();
    char npath[VFS_PATH_MAX];
    if(!path)return -EFAULT;
    if(c3_resolve_user_path(cur,AT_FDCWD,path,npath,sizeof(npath))<0)return -EINVAL;
    if(c3_path_is_dir(npath)){
        c3_strlcpy(cur->cwd,npath,sizeof(cur->cwd));
        return 0;
    }
    if(kvfs_exists(npath)){
        c3_path_dirname(npath,cur->cwd,sizeof(cur->cwd));
        return 0;
    }
    return -ENOENT;
}
int64_t real_sys_fchdir(int fd){
    task_t *cur=task_current();
    const char *p;
    if(!fd_valid(&cur->fdt,fd))return -EBADF;
    if(cur->fdt.fds[fd].kind!=FDKIND_DIR&&cur->fdt.fds[fd].kind!=FDKIND_VFSFILE)return -ENOTDIR;
    p=c3_vfs_open_slot_path(cur->fdt.fds[fd].ref);
    if(!p)return -EBADF;
    if(cur->fdt.fds[fd].kind==FDKIND_DIR)c3_strlcpy(cur->cwd,p,sizeof(cur->cwd));
    else c3_path_dirname(p,cur->cwd,sizeof(cur->cwd));
    return 0;
}
int64_t real_sys_mkdir(const char *path,int mode){
    task_t *cur=task_current();
    char npath[VFS_PATH_MAX];
    char marker[VFS_PATH_MAX*2];
    if(!path)return -EFAULT;
    if(c3_resolve_user_path(cur,AT_FDCWD,path,npath,sizeof(npath))<0)return -EINVAL;
    if(c3_path_is_dir(npath)||kvfs_exists(npath)){
        c3_trace_path_rc("mkdir",npath,-EEXIST,(uint64_t)(uint32_t)mode,0);
        return -EEXIST;
    }
    c3_path_dir_marker(npath,marker,sizeof(marker));
    if(!kvfs_write(marker,"")){
        c3_trace_path_rc("mkdir",npath,-ENOMEM,(uint64_t)(uint32_t)mode,0);
        return -ENOMEM;
    }
    c3_path_mode_set(npath,c3_path_mode_apply_create(mode,0777));
    c3_inotify_notify_path(npath,C3_IN_CREATE|C3_IN_ISDIR,0);
    c3_trace_path_rc("mkdir",npath,0,(uint64_t)(uint32_t)mode,0);
    return 0;
}

static int64_t c3_mkdir_abs(const char *npath,int mode){
    char marker[VFS_PATH_MAX*2];
    if(!npath||npath[0]!='/')return -EINVAL;
    if(c3_path_is_dir(npath))return -EEXIST;
    if(kvfs_exists(npath))return -EEXIST;
    c3_path_dir_marker(npath,marker,sizeof(marker));
    if(!kvfs_write(marker,""))return -ENOMEM;
    c3_path_mode_set(npath,c3_path_mode_apply_create(mode,0777));
    c3_inotify_notify_path(npath,C3_IN_CREATE|C3_IN_ISDIR,0);
    return 0;
}

static void c3_ensure_dir_abs(const char *npath,int mode){
    if(!npath||npath[0]!='/')return;
    if(!c3_path_is_dir(npath)){
        int64_t rc=c3_mkdir_abs(npath,mode);
        (void)rc;
    }
    if(c3_path_is_dir(npath)){
        c3_path_mode_set(npath,c3_path_mode_apply_create(mode,0777));
    }
}

static int64_t real_sys_mkdirat(int dirfd,const char *path,int mode){
    task_t *cur=task_current();
    char npath[VFS_PATH_MAX];
    int64_t rc;
    if(!path)return -EFAULT;
    if(c3_resolve_user_path(cur,dirfd,path,npath,sizeof(npath))<0)return -EINVAL;
    rc=c3_mkdir_abs(npath,mode);
    c3_trace_path_rc("mkdirat",npath,rc,(uint64_t)(uint32_t)mode,(uint64_t)(uint32_t)dirfd);
    return rc;
}

#define C3_AT_REMOVEDIR 0x200
#define C3_AT_SYMLINK_NOFOLLOW 0x100
#define C3_AT_NO_AUTOMOUNT 0x800
#define C3_AT_EMPTY_PATH 0x1000

static int64_t c3_rmdir_path(const char *npath){
    char marker[VFS_PATH_MAX*2];
    char list[1024];
    int i=0;
    if(!npath||!npath[0])return -EINVAL;
    if(!c3_path_is_dir(npath))return -ENOTDIR;
    if(c3_strcmp(npath,"/")==0||
       c3_strcmp(npath,"/proc")==0||
       c3_strcmp(npath,"/sys")==0||
       c3_strcmp(npath,"/dev")==0)return -EBUSY;
    if(kvfs_listdir(npath,list,sizeof(list))&&list[0]){
        while(list[i]){
            char ent[128];
            int j=0;
            while(list[i]&&list[i]!='\n'&&j+1<(int)sizeof(ent))ent[j++]=list[i++];
            ent[j]=0;
            if(list[i]=='\n')++i;
            if(ent[0]&&!c3_dirent_is_marker(ent))return -ENOTEMPTY;
        }
    }
    c3_path_dir_marker(npath,marker,sizeof(marker));
    if(kvfs_exists(marker)&&!kvfs_remove(marker))return -EIO;
    c3_path_mode_remove(npath);
    c3_inotify_notify_path(npath,C3_IN_DELETE|C3_IN_DELETE_SELF|C3_IN_ISDIR,0);
    return 0;
}

int64_t real_sys_unlink(const char *path){
    task_t *cur=task_current();
    char npath[VFS_PATH_MAX];
    if(!path)return -EFAULT;
    if(c3_resolve_user_path(cur,AT_FDCWD,path,npath,sizeof(npath))<0)return -EINVAL;
    if(c3_path_is_dir(npath)){
        c3_trace_path_rc("unlink",npath,-EISDIR,0,0);
        return -EISDIR;
    }
    if(!kvfs_exists(npath)){
        c3_trace_path_rc("unlink",npath,-ENOENT,0,0);
        return -ENOENT;
    }
    c3_file_page_invalidate_path(npath);
    if(c3_vfs_path_open_anywhere(npath)){
        char hidden[VFS_PATH_MAX];
        size_t hl=0;
        uint32_t seq=g_c3_deleted_seq++;
        if(seq==0)seq=g_c3_deleted_seq++;
        hidden[0]=0;
        c3_append_str(hidden,&hl,sizeof(hidden),C3_DELETED_PREFIX);
        c3_append_u32(hidden,&hl,sizeof(hidden),(uint32_t)(cur?cur->pid:0));
        c3_append_ch(hidden,&hl,sizeof(hidden),'-');
        c3_append_u32(hidden,&hl,sizeof(hidden),seq);
        if(!kvfs_rename(npath,hidden)){
            c3_trace_path_rc("unlink",npath,-EIO,0,0);
            return -EIO;
        }
        c3_vfs_open_slot_rename_path(npath,hidden);
        c3_path_mode_rename(npath,hidden);
        c3_inotify_notify_path(npath,C3_IN_DELETE|C3_IN_DELETE_SELF,0);
        c3_trace_path_rc("unlink-open",npath,0,0,0);
        c3_trace_path_rc("unlink-hidden",hidden,0,0,0);
        return 0;
    }
    if(!kvfs_remove(npath)){
        c3_trace_path_rc("unlink",npath,-EIO,0,0);
        return -EIO;
    }
    c3_path_mode_remove(npath);
    c3_inotify_notify_path(npath,C3_IN_DELETE|C3_IN_DELETE_SELF,0);
    c3_trace_path_rc("unlink",npath,0,0,0);
    return 0;
}
int64_t real_sys_rename(const char *o,const char *n){
    task_t *cur=task_current();
    char op[VFS_PATH_MAX],np[VFS_PATH_MAX];
    uint32_t cookie;
    (void)cur;
    if(!o||!n)return -EFAULT;
    if(c3_resolve_user_path(task_current(),AT_FDCWD,o,op,sizeof(op))<0)return -EINVAL;
    if(c3_resolve_user_path(task_current(),AT_FDCWD,n,np,sizeof(np))<0)return -EINVAL;
    if(c3_strcmp(op,np)==0){
        c3_trace_path_rc("rename-src",op,0,0,0);
        c3_trace_path_rc("rename-dst",np,0,0,0);
        return 0;
    }
    if(c3_path_is_dir(op)||c3_path_is_dir(np)){
        c3_trace_path_rc("rename-src",op,-EISDIR,0,0);
        c3_trace_path_rc("rename-dst",np,-EISDIR,0,0);
        return -EISDIR;
    }
    if(!kvfs_exists(op)){
        c3_trace_path_rc("rename-src",op,-ENOENT,0,0);
        c3_trace_path_rc("rename-dst",np,-ENOENT,0,0);
        return -ENOENT;
    }
    cookie=g_c3_inotify_cookie++;
    if(cookie==0)cookie=g_c3_inotify_cookie++;
    if(kvfs_exists(np)){
        c3_file_page_invalidate_path(np);
        if(!kvfs_remove(np))return -EIO;
        c3_path_mode_remove(np);
        c3_inotify_notify_path(np,C3_IN_DELETE|C3_IN_DELETE_SELF,0);
    }
    c3_file_page_invalidate_path(op);
    if(!kvfs_rename(op,np)){
        c3_trace_path_rc("rename-src",op,-EIO,0,0);
        c3_trace_path_rc("rename-dst",np,-EIO,0,0);
        return -EIO;
    }
    c3_path_mode_rename(op,np);
    c3_vfs_open_slot_rename_path(op,np);
    c3_inotify_notify_path(op,C3_IN_MOVED_FROM|C3_IN_MOVE_SELF,cookie);
    c3_inotify_notify_path(np,C3_IN_MOVED_TO,cookie);
    c3_trace_path_rc("rename-src",op,0,0,0);
    c3_trace_path_rc("rename-dst",np,0,0,0);
    return 0;
}
int64_t real_sys_truncate(const char *p,int64_t l){
    task_t *cur=task_current();
    char np[VFS_PATH_MAX];
    const uint8_t *data=0;
    uint32_t size=0;
    uint8_t *out=g_c3_vfs_file_scratch;
    size_t want,i;
    uint64_t memfd_size=0;
    if(!p)return -EFAULT;
    if(c3_resolve_user_path(cur,AT_FDCWD,p,np,sizeof(np))<0)return -EINVAL;
    if(c3_path_is_dir(np))return -EISDIR;
    if(l<0)return -EINVAL;
    if(c3_memfd_path_get_size(np,&memfd_size)){
        uint32_t seals=0;
        if(c3_memfd_path_get_seals(np,&seals)){
            if((uint64_t)l<memfd_size&&(seals&C3_F_SEAL_SHRINK))return -EPERM;
            if((uint64_t)l>memfd_size&&(seals&C3_F_SEAL_GROW))return -EPERM;
        }
        return c3_memfd_path_set_size(np,(uint64_t)l)?0:-EINVAL;
    }
    if(!kvfs_read(np,&data,&size))return -ENOENT;
    want=(size_t)l;
    if(want>C3_VFS_FILE_MAX-1)want=C3_VFS_FILE_MAX-1;
    for(i=0;i<want;++i)out[i]=(i<size)?data[i]:0;
    c3_file_page_invalidate_path(np);
    kvfs_write_bytes(np,out,(uint32_t)want);
    c3_inotify_notify_path(np,C3_IN_MODIFY,0);
    return 0;
}
int64_t real_sys_ftruncate(int fd,int64_t l){
    task_t *cur=task_current();
    const char *vpath;
    uint64_t memfd_size=0;
    uint32_t seals=0;
    if(!fd_valid(&cur->fdt,fd)||cur->fdt.fds[fd].kind!=FDKIND_VFSFILE)return -EBADF;
    if(l<0)return -EINVAL;
    vpath=c3_vfs_open_slot_path(cur->fdt.fds[fd].ref);
    if(!vpath)return -EBADF;
    if(c3_memfd_path_get_size(vpath,&memfd_size)){
        if(c3_memfd_path_get_seals(vpath,&seals)){
            if((uint64_t)l<memfd_size&&(seals&C3_F_SEAL_SHRINK))return -EPERM;
            if((uint64_t)l>memfd_size&&(seals&C3_F_SEAL_GROW))return -EPERM;
        }
        return c3_memfd_path_set_size(vpath,(uint64_t)l)?0:-EINVAL;
    }
    return real_sys_truncate(vpath,l);
}

int64_t real_sys_newfstatat(int dirfd,const char *path,kstat_t *st,int flags){
    task_t *cur=task_current();
    char npath[VFS_PATH_MAX];
    if(!cur)return -ESRCH;
    if(!path||!st)return -EFAULT;
    if(flags&~(C3_AT_SYMLINK_NOFOLLOW|C3_AT_NO_AUTOMOUNT|C3_AT_EMPTY_PATH))return -EINVAL;
    if(path[0]==0){
        if(!(flags&C3_AT_EMPTY_PATH))return -ENOENT;
        if(dirfd==AT_FDCWD){
            return c3_stat_from_path(cur->cwd,st);
        }
        if(!fd_valid(&cur->fdt,dirfd))return -EBADF;
        if(cur->fdt.fds[dirfd].kind==FDKIND_VFSFILE||cur->fdt.fds[dirfd].kind==FDKIND_DIR){
            const char *vpath=c3_vfs_open_slot_path(cur->fdt.fds[dirfd].ref);
            if(!vpath)return -EBADF;
            return c3_stat_from_path(vpath,st);
        }
        return real_sys_fstat(dirfd,st);
    }
    if(c3_resolve_user_path(cur,dirfd,path,npath,sizeof(npath))<0)return -EINVAL;
    {
        int64_t rc=c3_stat_from_path(npath,st);
        c3_trace_path_rc("newfstatat",npath,rc,(uint64_t)(uint32_t)flags,(uint64_t)(uint32_t)dirfd);
        c3_trace_firefox_path_rc("newfstatat",npath,rc,(uint64_t)(uint32_t)flags,(uint64_t)(uint32_t)dirfd);
        return rc;
    }
}

static int64_t real_sys_readlinkat(int dirfd,const char *path,char *buf,size_t bufsiz){
    task_t *cur=task_current();
    char npath[VFS_PATH_MAX];
    int64_t rc;
    if(!path||!buf)return -EFAULT;
    if(c3_resolve_user_path(cur,dirfd,path,npath,sizeof(npath))<0)return -EINVAL;
    rc=real_sys_readlink(npath,buf,bufsiz);
    c3_trace_firefox_path_rc("readlinkat",npath,rc,(uint64_t)bufsiz,(uint64_t)(uint32_t)dirfd);
    return rc;
}

static int64_t real_sys_symlinkat(const char *target,int newdirfd,const char *linkpath){
    task_t *cur=task_current();
    char npath[VFS_PATH_MAX];
    char out[C3_VFS_WRITE_MAX];
    size_t l=0;
    if(!target||!linkpath)return -EFAULT;
    if(c3_resolve_user_path(cur,newdirfd,linkpath,npath,sizeof(npath))<0)return -EINVAL;
    if(c3_path_is_dir(npath)||kvfs_exists(npath)){
        c3_trace_path_rc("symlinkat",npath,-EEXIST,0,(uint64_t)(uint32_t)newdirfd);
        return -EEXIST;
    }
    c3_append_str(out,&l,sizeof(out),C3_SYMLINK_PREFIX);
    c3_append_str(out,&l,sizeof(out),target);
    if(l+1>=sizeof(out)){
        c3_trace_path_rc("symlinkat",npath,-EINVAL,0,(uint64_t)(uint32_t)newdirfd);
        return -EINVAL;
    }
    if(!kvfs_write(npath,out)){
        c3_trace_path_rc("symlinkat",npath,-ENOMEM,0,(uint64_t)(uint32_t)newdirfd);
        return -ENOMEM;
    }
    c3_inotify_notify_path(npath,C3_IN_CREATE,0);
    c3_trace_path_rc("symlinkat",npath,0,0,(uint64_t)(uint32_t)newdirfd);
    return 0;
}

static int64_t real_sys_symlink(const char *target,const char *linkpath){
    return real_sys_symlinkat(target,AT_FDCWD,linkpath);
}

static int64_t real_sys_unlinkat(int dirfd,const char *path,int flags){
    task_t *cur=task_current();
    char npath[VFS_PATH_MAX];
    if(flags&~C3_AT_REMOVEDIR)return -EINVAL;
    if(!path)return -EFAULT;
    if(c3_resolve_user_path(cur,dirfd,path,npath,sizeof(npath))<0)return -EINVAL;
    if(flags&C3_AT_REMOVEDIR)return c3_rmdir_path(npath);
    return real_sys_unlink(npath);
}

static int64_t real_sys_renameat(int olddirfd,const char *oldpath,int newdirfd,const char *newpath){
    task_t *cur=task_current();
    char o[VFS_PATH_MAX],n[VFS_PATH_MAX];
    if(!oldpath||!newpath)return -EFAULT;
    if(c3_resolve_user_path(cur,olddirfd,oldpath,o,sizeof(o))<0)return -EINVAL;
    if(c3_resolve_user_path(cur,newdirfd,newpath,n,sizeof(n))<0)return -EINVAL;
    return real_sys_rename(o,n);
}

static int64_t real_sys_renameat2(int olddirfd,const char *oldpath,int newdirfd,const char *newpath,unsigned int flags){
    task_t *cur=task_current();
    char o[VFS_PATH_MAX],n[VFS_PATH_MAX];
    if(!oldpath||!newpath)return -EFAULT;
    if(flags&~(C3_RENAME_NOREPLACE|C3_RENAME_EXCHANGE|C3_RENAME_WHITEOUT))return -EINVAL;
    if((flags&C3_RENAME_EXCHANGE)&&(flags&C3_RENAME_NOREPLACE))return -EINVAL;
    if(flags&(C3_RENAME_EXCHANGE|C3_RENAME_WHITEOUT))return -EINVAL;
    if(c3_resolve_user_path(cur,olddirfd,oldpath,o,sizeof(o))<0)return -EINVAL;
    if(c3_resolve_user_path(cur,newdirfd,newpath,n,sizeof(n))<0)return -EINVAL;
    if((flags&C3_RENAME_NOREPLACE)&&kvfs_exists(n))return -EEXIST;
    return real_sys_rename(o,n);
}

int64_t real_sys_link(const char *oldpath,const char *newpath){
    task_t *cur=task_current();
    char o[VFS_PATH_MAX],n[VFS_PATH_MAX];
    const uint8_t *data=0;
    uint32_t size=0;
    uint32_t mode;
    if(!oldpath||!newpath)return -EFAULT;
    if(c3_resolve_user_path(cur,AT_FDCWD,oldpath,o,sizeof(o))<0)return -EINVAL;
    if(c3_resolve_user_path(cur,AT_FDCWD,newpath,n,sizeof(n))<0)return -EINVAL;
    if(c3_path_is_dir(o))return -EPERM;
    if(c3_path_is_dir(n)||kvfs_exists(n)){
        c3_trace_path_rc("link-dst",n,-EEXIST,0,0);
        return -EEXIST;
    }
    if(!kvfs_read(o,&data,&size)&&!kvfs_exists(o)){
        c3_trace_path_rc("link-src",o,-ENOENT,0,0);
        return -ENOENT;
    }
    if(!data)size=0;
    if(!kvfs_write_bytes(n,data?data:(const uint8_t*)"",size)){
        c3_trace_path_rc("link-dst",n,-ENOMEM,0,0);
        return -ENOMEM;
    }
    mode=c3_path_mode_get(o,0644);
    c3_path_mode_set(n,mode);
    c3_inotify_notify_path(n,C3_IN_CREATE,0);
    c3_trace_path_rc("link-src",o,0,0,0);
    c3_trace_path_rc("link-dst",n,0,0,0);
    return 0;
}

static int64_t real_sys_linkat(int olddirfd,const char *oldpath,int newdirfd,const char *newpath,int flags){
    task_t *cur=task_current();
    char o[VFS_PATH_MAX],n[VFS_PATH_MAX];
    const int C3_AT_SYMLINK_FOLLOW_LOCAL=0x400;
    if(!oldpath||!newpath)return -EFAULT;
    if(flags&~(C3_AT_SYMLINK_FOLLOW_LOCAL|C3_AT_EMPTY_PATH))return -EINVAL;
    if(oldpath[0]==0){
        if(!(flags&C3_AT_EMPTY_PATH))return -ENOENT;
        if(olddirfd==AT_FDCWD)return -ENOENT;
        if(!fd_valid(&cur->fdt,olddirfd))return -EBADF;
        if(cur->fdt.fds[olddirfd].kind!=FDKIND_VFSFILE)return -EPERM;
        {
            const char *fp=c3_vfs_open_slot_path(cur->fdt.fds[olddirfd].ref);
            if(!fp)return -EBADF;
            c3_strlcpy(o,fp,sizeof(o));
        }
    }else if(c3_resolve_user_path(cur,olddirfd,oldpath,o,sizeof(o))<0)return -EINVAL;
    if(c3_resolve_user_path(cur,newdirfd,newpath,n,sizeof(n))<0)return -EINVAL;
    return real_sys_link(o,n);
}

int64_t real_sys_fsync(int fd){
    task_t *cur=task_current();
    if(!cur||!fd_valid(&cur->fdt,fd))return -EBADF;
    return 0;
}

int64_t real_sys_fdatasync(int fd){
    return real_sys_fsync(fd);
}

int64_t real_sys_flock(int fd,int operation){
    task_t *cur=task_current();
    (void)operation;
    if(!cur||!fd_valid(&cur->fdt,fd))return -EBADF;
    return 0;
}

int64_t real_sys_syncfs(int fd){
    task_t *cur=task_current();
    if(!cur||!fd_valid(&cur->fdt,fd))return -EBADF;
    return 0;
}

int64_t real_sys_fallocate(int fd,int mode,int64_t offset,int64_t len){
    task_t *cur=task_current();
    const char *vpath;
    const uint8_t *data=0;
    uint32_t size=0;
    uint8_t *out=g_c3_vfs_file_scratch;
    size_t cur_size=0;
    size_t end=0;
    size_t i;
    uint64_t memfd_size=0;
    if(!cur||!fd_valid(&cur->fdt,fd))return -EBADF;
    if(cur->fdt.fds[fd].kind!=FDKIND_VFSFILE)return -EBADF;
    if(offset<0||len<=0)return -EINVAL;
    if(mode&~(C3_FALLOC_FL_KEEP_SIZE|C3_FALLOC_FL_ZERO_RANGE))return -EINVAL;
    vpath=c3_vfs_open_slot_path(cur->fdt.fds[fd].ref);
    if(!vpath)return -EBADF;
    if(c3_memfd_path_get_size(vpath,&memfd_size)){
        uint64_t mend=(uint64_t)offset+(uint64_t)len;
        uint32_t seals=0;
        if(mend<(uint64_t)offset)return -EINVAL;
        if(!(mode&C3_FALLOC_FL_KEEP_SIZE)&&mend>memfd_size){
            if(c3_memfd_path_get_seals(vpath,&seals)&&(seals&C3_F_SEAL_GROW))return -EPERM;
            if(!c3_memfd_path_set_size(vpath,mend))return -EINVAL;
        }
        return 0;
    }
    if(mode&C3_FALLOC_FL_KEEP_SIZE)return 0;
    if(kvfs_read(vpath,&data,&size)&&data)cur_size=(size_t)size;
    end=(size_t)offset+(size_t)len;
    if(end<(size_t)offset)return -EINVAL;
    if(end>C3_VFS_FILE_MAX-1)end=C3_VFS_FILE_MAX-1;
    if(cur_size>C3_VFS_FILE_MAX-1)cur_size=C3_VFS_FILE_MAX-1;
    for(i=0;i<end;++i){
        if(i<cur_size)out[i]=data[i];
        else out[i]=0;
    }
    c3_file_page_invalidate_path(vpath);
    kvfs_write_bytes(vpath,out,(uint32_t)end);
    c3_inotify_notify_path(vpath,C3_IN_MODIFY,0);
    return 0;
}

int64_t real_sys_inotify_init1(int flags){
    task_t *cur=task_current();
    uint16_t ff=FDFL_READABLE;
    int ref,fd;
    if(!cur)return -ESRCH;
    if(flags&~(O_NONBLOCK|O_CLOEXEC))return -EINVAL;
    ref=c3_inotify_inst_alloc();
    if(ref<0)return ref;
    if(flags&O_NONBLOCK)ff|=FDFL_NONBLOCK;
    if(flags&O_CLOEXEC)ff|=FDFL_CLOEXEC;
    fd=c3_fd_alloc_for_task(cur,FDKIND_INOTIFY,ref,ff);
    if(fd<0){
        c3_inotify_inst_free(ref);
        return -EMFILE;
    }
    return fd;
}

int64_t real_sys_inotify_init(void){
    return real_sys_inotify_init1(0);
}

int64_t real_sys_inotify_add_watch(int fd,const char *path,uint32_t mask){
    task_t *cur=task_current();
    char npath[VFS_PATH_MAX];
    if(!cur||!path)return -EFAULT;
    if(!fd_valid(&cur->fdt,fd)||cur->fdt.fds[fd].kind!=FDKIND_INOTIFY)return -EBADF;
    if(c3_resolve_user_path(cur,AT_FDCWD,path,npath,sizeof(npath))<0)return -EINVAL;
    return c3_inotify_watch_add(cur->fdt.fds[fd].ref,npath,mask);
}

int64_t real_sys_inotify_rm_watch(int fd,int wd){
    task_t *cur=task_current();
    if(!cur)return -ESRCH;
    if(!fd_valid(&cur->fdt,fd)||cur->fdt.fds[fd].kind!=FDKIND_INOTIFY)return -EBADF;
    return c3_inotify_watch_rm(cur->fdt.fds[fd].ref,(int32_t)wd);
}

static int c3_exec_capture_vec(char *const src[],char **outv,int maxc,char *pool,size_t poolsz,int *outc){
    int c=0;
    size_t used=0;
    if(!outv||maxc<=0||!pool||poolsz==0||!outc)return -EFAULT;
    if(!src){outv[0]=0;*outc=0;return 0;}
    while(c<maxc){
        const char *s=(const char*)(uintptr_t)src[c];
        size_t len=0;
        if(!s){outv[c]=0;*outc=c;return 0;}
        while(s[len])++len;
        if(len+1>poolsz-used)return -E2BIG;
        outv[c]=pool+used;
        c3_memcpy(pool+used,s,len+1);
        used+=len+1;
        ++c;
    }
    return -E2BIG;
}

static int c3_exec_extract_interp(const uint8_t *data,uint32_t sz,char *out,size_t out_cap,bool *needs_interp){
    const elf64_ehdr_t *eh;
    int i;
    if(!data||!sz||!out||out_cap==0||!needs_interp)return -EINVAL;
    out[0]=0;
    *needs_interp=false;
    if(!elf64_validate(data,sz))return -ENOEXEC;
    eh=(const elf64_ehdr_t*)data;
    *needs_interp=(eh->e_type==ET_DYN);
    for(i=0;i<(int)eh->e_phnum;++i){
        uint64_t off=eh->e_phoff+(uint64_t)i*eh->e_phentsize;
        if(off+sizeof(elf64_phdr_t)>sz)break;
        {
            const elf64_phdr_t *ph=(const elf64_phdr_t*)(data+off);
            if(ph->p_type!=PT_INTERP)continue;
            if(ph->p_offset+ph->p_filesz>sz||ph->p_filesz==0)return -ENOEXEC;
            {
                size_t il=(size_t)ph->p_filesz;
                if(il==0)return -ENOEXEC;
                if(il>=out_cap)il=out_cap-1;
                if(il>0)c3_memcpy(out,data+ph->p_offset,il);
                out[il]=0;
            }
            return 0;
        }
    }
    return 0;
}

static int c3_exec_resolve_interp_path(const char *exe_path,const char *interp,char *out,size_t out_cap){
    if(!exe_path||!interp||!interp[0]||!out||out_cap==0)return -EINVAL;
    if(interp[0]=='/'){
        c3_path_normalize_abs(interp,out,out_cap);
        return 0;
    }
    {
        char dir[VFS_PATH_MAX];
        char tmp[VFS_PATH_MAX*2];
        c3_path_dirname(exe_path,dir,sizeof(dir));
        c3_path_build(dir,interp,tmp,sizeof(tmp));
        c3_path_normalize_abs(tmp,out,out_cap);
    }
    return 0;
}

static int c3_exec_collect_elf_info(const uint8_t *data,uint32_t sz,uint64_t mmap_seed,uint64_t *load_bias,uint64_t *entry,uint64_t *phdr,uint64_t *phent,uint64_t *phnum){
    const elf64_ehdr_t *eh;
    uint64_t min_vaddr=~0ULL;
    uint64_t lb=0;
    uint64_t phdr_va=0;
    int i;
    if(!data||!sz||!load_bias||!entry||!phdr||!phent||!phnum)return -EINVAL;
    if(!elf64_validate(data,sz))return -ENOEXEC;
    eh=(const elf64_ehdr_t*)data;
    if(eh->e_type==ET_DYN){
        for(i=0;i<(int)eh->e_phnum;++i){
            uint64_t off=eh->e_phoff+(uint64_t)i*eh->e_phentsize;
            if(off+sizeof(elf64_phdr_t)>sz)break;
            {
                const elf64_phdr_t *ph=(const elf64_phdr_t*)(data+off);
                if(ph->p_type!=PT_LOAD)continue;
                {
                    uint64_t s=ph->p_vaddr&~(PAGE_SIZE-1);
                    if(s<min_vaddr)min_vaddr=s;
                }
            }
        }
        if(min_vaddr==~0ULL)min_vaddr=0;
        if(mmap_seed<0x40000000ULL)mmap_seed=0x40000000ULL;
        lb=(mmap_seed-min_vaddr)&~(PAGE_SIZE-1);
    }
    for(i=0;i<(int)eh->e_phnum;++i){
        uint64_t off=eh->e_phoff+(uint64_t)i*eh->e_phentsize;
        if(off+sizeof(elf64_phdr_t)>sz)break;
        {
            const elf64_phdr_t *ph=(const elf64_phdr_t*)(data+off);
            if(ph->p_type==PT_PHDR){phdr_va=ph->p_vaddr+lb;break;}
        }
    }
    if(!phdr_va)phdr_va=lb+eh->e_phoff;
    *load_bias=lb;
    *entry=eh->e_entry+lb;
    *phdr=phdr_va;
    *phent=eh->e_phentsize?eh->e_phentsize:sizeof(elf64_phdr_t);
    *phnum=eh->e_phnum;
    return 0;
}

static bool c3_exec_map_user_stack(address_space_t *as){
    uint64_t stk_base=C3_USER_STACK_TOP-TASK_USER_STACK;
    int pg;
    if(!as)return false;
    for(pg=0;pg<(int)(TASK_USER_STACK/PAGE_SIZE);++pg){
        uint64_t frame=pmm_alloc_frame();
        uint64_t va=stk_base+(uint64_t)pg*PAGE_SIZE;
        if(!frame)return false;
        c3_phys_ref_set_initial(frame);
        c3_memset((void*)PHYS_TO_DMAP(frame),0,PAGE_SIZE);
        if(!paging_map(as,va,frame,PAGE_PRESENT|PAGE_WRITABLE|PAGE_USER)){
            c3_phys_ref_release(frame);
            break;
        }
    }
    if(pg<(int)(TASK_USER_STACK/PAGE_SIZE)){
        while(--pg>=0){
            uint64_t va=stk_base+(uint64_t)pg*PAGE_SIZE;
            uint64_t ent=paging_get_entry(as,va);
            if(ent&PAGE_PRESENT){
                uint64_t phys=ent&C3_PTE_ADDR_MASK;
                (void)paging_unmap(as,va);
                c3_phys_ref_release(phys);
            }
        }
        return false;
    }
    return true;
}

static void c3_exec_fill_random16(uint8_t out[16]){
    uint64_t x;
    int i;
    if(!out)return;
    x=c3_rdtsc()^(uint64_t)(uintptr_t)task_current()^((uint64_t)g_current_task<<32);
    for(i=0;i<16;++i){
        x^=x<<13;
        x^=x>>7;
        x^=x<<17;
        out[i]=(uint8_t)(x>>((i&7)*8));
    }
}

int compat3_init_user_tls(task_t *t){
    uint64_t ent;
    uint64_t phys=0;
    uint64_t pg;
    uint64_t tls_lo=C3_USER_TLS_BASE-C3_USER_TLS_PREP_BYTES;
    uint64_t tls_hi=C3_USER_TLS_BASE+PAGE_SIZE;
    uint8_t guards[16];
    c3_glibc_tcb_head_t *tcb;
    c3_dtv_t *dtvp;
    int i;
    if(!t||!t->addr_space)return -EINVAL;
    for(pg=tls_lo;pg<tls_hi;pg+=PAGE_SIZE){
        ent=paging_get_entry(t->addr_space,pg);
        if(!(ent&PAGE_PRESENT)){
            phys=pmm_alloc_frame();
            if(!phys)return -ENOMEM;
            c3_phys_ref_set_initial(phys);
            if(!paging_map(t->addr_space,pg,phys,PAGE_PRESENT|PAGE_WRITABLE|PAGE_USER)){
                c3_phys_ref_release(phys);
                return -ENOMEM;
            }
        }
    }
    c3_memset((void*)(uintptr_t)tls_lo,0,(size_t)(tls_hi-tls_lo));
    c3_exec_fill_random16(guards);
    tcb=(c3_glibc_tcb_head_t*)(uintptr_t)C3_USER_TLS_BASE;
    dtvp=(c3_dtv_t*)(uintptr_t)C3_USER_TLS_DTV;
    tcb->tcb=C3_USER_TLS_BASE;
    tcb->dtv=(uint64_t)(uintptr_t)(dtvp+1);
    tcb->self=C3_USER_TLS_BASE;
    tcb->multiple_threads=0;
    tcb->gscope_flag=0;
    tcb->sysinfo=0;
    c3_memcpy(&tcb->stack_guard,guards,sizeof(uint64_t));
    c3_memcpy(&tcb->pointer_guard,guards+sizeof(uint64_t),sizeof(uint64_t));
    dtvp[0].counter=C3_USER_TLS_DTV_SLOTS;
    dtvp[1].counter=1;
    for(i=2;i<C3_USER_TLS_DTV_SLOTS+2;++i){
        dtvp[i].pointer.val=~0ULL;
        dtvp[i].pointer.to_free=0;
    }
    t->ctx.fs_base=C3_USER_TLS_BASE;
    t->ctx.gs_base=0;
    return 0;
}

static void c3_exec_close_cloexec_fds(task_t *cur){
    int fd;
    if(!cur)return;
    for(fd=0;fd<TASK_FD_MAX;++fd){
        if(cur->fdt.fds[fd].kind==FDKIND_NONE)continue;
        if(!(cur->fdt.fds[fd].flags&FDFL_CLOEXEC))continue;
        (void)real_sys_close(fd);
    }
}

static void c3_exec_reset_signal_state(task_t *cur){
    int sig;
    if(!cur)return;
    cur->sig_pending.sig[0]=0;
    cur->sig_pending.sig[1]=0;
    cur->sig_saved_mask.sig[0]=0;
    cur->sig_saved_mask.sig[1]=0;
    cur->sig_in_handler=false;
    cur->last_signal=0;
    for(sig=1;sig<NSIG;++sig){
        if(cur->sigactions[sig].handler!=SIG_IGN)cur->sigactions[sig].handler=SIG_DFL;
        cur->sigactions[sig].mask.sig[0]=0;
        cur->sigactions[sig].mask.sig[1]=0;
        cur->sigactions[sig].flags=0;
    }
}

static bool c3_addr_space_has_other_live_task(address_space_t *as,const task_t *self){
    int i;
    if(!as)return false;
    for(i=0;i<TASK_MAX;++i){
        const task_t *t=&g_tasks[i];
        if(!t->used||t==self)continue;
        if(t->state==TASK_FREE||t->state==TASK_ZOMBIE)continue;
        if(t->addr_space==as)return true;
    }
    return false;
}

static void c3_exec_wake_vfork_parent(const task_t *child){
    int i;
    if(!child)return;
    for(i=0;i<TASK_MAX;++i){
        task_t *p=&g_tasks[i];
        if(!p->used)continue;
        if(p->state!=TASK_SLEEPING)continue;
        if(p->wait_pid!=child->pid)continue;
        p->wait_pid=0;
        p->state=TASK_RUNNABLE;
        __boot_serial_puts("[vfork-exec-wake] child=");
        __boot_serial_putu32((uint32_t)child->pid);
        __boot_serial_puts(" parent=");
        __boot_serial_putu32((uint32_t)p->pid);
        __boot_serial_puts("\n");
    }
}

static void c3_exec_reset_dynamic_state(void){
    c3_memset(g_c3_dynobjs,0,sizeof(g_c3_dynobjs));
    c3_memset(g_c3_dyn_profile,0,sizeof(g_c3_dyn_profile));
    g_c3_dyn_images_mapped=0;
    g_c3_dyn_reloc_applied=0;
    g_c3_dyn_reloc_failed=0;
    g_c3_dyn_reloc_unsupported=0;
    g_c3_dyn_needed_loaded=0;
    g_c3_dyn_needed_missing=0;
    g_c3_dyn_objects=0;
    g_c3_dyn_load_depth=0;
    g_c3_dyn_timeout_hits=0;
    g_c3_dyn_last_timeout_tsc=0;
    g_c3_dyn_last_timeout_obj[0]=0;
    g_c3_dyn_last_timeout_stage[0]=0;
    g_c3_dyn_cycle_map=0;
    g_c3_dyn_cycle_needed=0;
    g_c3_dyn_cycle_reloc=0;
    g_c3_dyn_profile_next=0;
    g_c3_tls_next_module=1;
    g_c3_next_image_name[0]=0;
}

/* Task / process syscalls */
int64_t real_sys_fork(void){
    task_t *cur=task_current();
    int64_t pid=(int64_t)task_fork(cur->pid);
    if(pid>0){
        int pi=g_current_task;
        int ci=c3_task_index_by_pid((int)pid);
        if(pi>=0&&pi<TASK_MAX&&ci>=0&&ci<TASK_MAX){
            g_c3_seccomp_mode[ci]=g_c3_seccomp_mode[pi];
            g_c3_seccomp_flags[ci]=g_c3_seccomp_flags[pi];
            g_c3_ns_mask[ci]=g_c3_ns_mask[pi];
            g_c3_no_new_privs[ci]=g_c3_no_new_privs[pi];
        }
    }
    return pid;
}
int64_t real_sys_vfork(void){return real_sys_fork();}

#define C3_CLONE_VM             0x00000100ULL
#define C3_CLONE_FS             0x00000200ULL
#define C3_CLONE_FILES          0x00000400ULL
#define C3_CLONE_SIGHAND        0x00000800ULL
#define C3_CLONE_VFORK          0x00004000ULL
#define C3_CLONE_THREAD         0x00010000ULL
#define C3_CLONE_NEWNS          0x00020000ULL
#define C3_CLONE_SETTLS         0x00080000ULL
#define C3_CLONE_PARENT_SETTID  0x00100000ULL
#define C3_CLONE_CHILD_CLEARTID 0x00200000ULL
#define C3_CLONE_CHILD_SETTID   0x01000000ULL
#define C3_CLONE_NEWCGROUP      0x02000000ULL
#define C3_CLONE_NEWUTS         0x04000000ULL
#define C3_CLONE_NEWIPC         0x08000000ULL
#define C3_CLONE_NEWUSER        0x10000000ULL
#define C3_CLONE_NEWPID         0x20000000ULL
#define C3_CLONE_NEWNET         0x40000000ULL
#define C3_CLONE_NAMESPACE_MASK (C3_CLONE_NEWNS|C3_CLONE_NEWCGROUP|C3_CLONE_NEWUTS|C3_CLONE_NEWIPC|C3_CLONE_NEWUSER|C3_CLONE_NEWPID|C3_CLONE_NEWNET)

static int c3_make_zombie_child(task_t *parent,const char *name,int exit_code){
    int i;
    task_t *child=0;
    if(!parent)return -ESRCH;
    for(i=1;i<TASK_MAX;++i){
        if(!g_tasks[i].used){
            child=&g_tasks[i];
            break;
        }
    }
    if(!child)return -EAGAIN;
    c3_memset(child,0,sizeof(*child));
    child->used=true;
    child->pid=g_task_next_pid++;
    child->tgid=child->pid;
    child->ppid=parent->pid;
    child->pgid=parent->pgid;
    child->sid=parent->sid;
    child->fdt_group=child->pid;
    child->uid=parent->uid;
    child->gid=parent->gid;
    child->euid=parent->euid;
    child->egid=parent->egid;
    child->state=TASK_ZOMBIE;
    child->exit_code=exit_code;
    c3_strlcpy(child->name,name?name:"linux-probe",TASK_NAME_LEN);
    c3_strlcpy(child->cwd,parent->cwd,sizeof(child->cwd));
    return child->pid;
}

int64_t real_sys_clone(uint64_t flags,uint64_t child_stack,uint64_t ptid,uint64_t ctid,uint64_t tls){
    task_t *cur=task_current();
    int pid;
    task_t *child;
    int ci;
    bool vfork_vm_share;
    if(!cur)return -ESRCH;
    /* Loud trace so we can see which clone variants Firefox/Chrome try
     * and how we serve them. Once Linuxulator parity is good we can
     * gate this behind a verbosity flag. */
    {
        static uint32_t clone_trace_count=0;
        if(clone_trace_count<64){
            ++clone_trace_count;
            __boot_serial_puts("[clone] flags=");
            __boot_serial_puthex64(flags);
            __boot_serial_puts(" parent=");
            __boot_serial_putu32((uint32_t)cur->pid);
            c3_trace_task_name(cur);
            __boot_serial_puts(" stack=");
            __boot_serial_puthex64(child_stack);
            __boot_serial_puts(" ptid=");
            __boot_serial_puthex64(ptid);
            __boot_serial_puts(" ctid=");
            __boot_serial_puthex64(ctid);
            __boot_serial_puts(" tls=");
            __boot_serial_puthex64(tls);
            __boot_serial_puts("\n");
        }
    }
    /* Namespace flags (CLONE_NEW{NS,USER,PID,...}): historically we returned
     * -EPERM here, which causes Firefox/Chrome to log
     * "Sandbox: CanCreateUserNamespace() clone() failure: EPERM" and disable
     * sandboxing. FreeBSD's Linuxulator (sys/compat/linux/linux_fork.c) does
     * not implement Linux namespaces either but accepts the flags silently
     * and runs the child in the same namespace as the parent. We mirror
     * that: just record the requested mask so /proc/self/status can echo it
     * back, and let the clone proceed. */
    if(flags&C3_CLONE_NAMESPACE_MASK){
        int pi=g_current_task;
        if(pi>=0&&pi<TASK_MAX){
            g_c3_ns_mask[pi]|=(uint32_t)((flags&C3_CLONE_NAMESPACE_MASK)>>17);
        }
    }
    /* Firefox/Chromium use clone(CLONE_NEWUSER|SIGCHLD) as a sandbox
     * capability probe. Running the probe child through the current
     * process-fork/COW path can corrupt the parent's user stack on return
     * from wait4; for namespace-only probes, FreeBSD-style Linuxulator
     * behavior can safely report success by creating an already-exited
     * child for wait4() to reap. Real process creation still uses the
     * fork-like path below. */
    if((flags&C3_CLONE_NAMESPACE_MASK) &&
       !(flags&(C3_CLONE_VM|C3_CLONE_THREAD|C3_CLONE_VFORK)) &&
       ((flags&~(C3_CLONE_NAMESPACE_MASK|0xFFULL))==0) &&
       child_stack==0){
        pid=c3_make_zombie_child(cur,"linux-ns-probe",0);
        if(pid<0)return pid;
        if(flags&C3_CLONE_PARENT_SETTID&&ptid){
            *(int*)(uintptr_t)ptid=pid;
        }
        return (int64_t)pid;
    }
    /* CLONE_VFORK | CLONE_VM without CLONE_THREAD is the canonical Linux
     * vfork() / posix_spawn() pattern: child shares parent VM, parent
     * blocks until the child calls execve() or exits. We previously
     * returned -ENOSYS, which broke posix_spawn-based content/GPU
     * process creation in Firefox. Implement it via the thread-clone
     * path (shared VM, runnable child) plus a flag that puts the parent
     * to sleep until the child execs or exits, matching FreeBSD's
     * RFPPWAIT semantics. */
    vfork_vm_share=((flags&C3_CLONE_VFORK)&&(flags&C3_CLONE_VM)&&!(flags&C3_CLONE_THREAD));
    if(vfork_vm_share){
        /* Pretend CLONE_THREAD so the existing thread-clone code path
         * shares VM/sigh and. We will still record CLONE_VFORK below
         * to make the parent block on the child. */
        flags|=C3_CLONE_THREAD;
    }

    /* Thread-like clone path */
    if(flags&C3_CLONE_THREAD){
        /* Linux pthread clones provide their own user stack and normally
         * share the address space (CLONE_VM). Avoid task_create(..., true)
         * here: that allocates an unused 8 MiB user stack plus a fresh page
         * table per thread, which is deadly during Chromium renderer startup. */
        pid=(flags&C3_CLONE_VM) ? task_create_user_shell(cur->name,cur->ctx.rip)
                                : task_create(cur->name,cur->ctx.rip,true);
        if(pid<0){c3_trace_clone_fail(cur,pid);return pid;}
        child=c3_task_by_pid(pid);
        if(!child)return -ESRCH;
        {
            const char *ep=c3_task_exec_path(cur);
            if(ep)c3_strlcpy(child->exec_path,ep,sizeof(child->exec_path));
            c3_task_force_browser_stable_mode(child,ep);
        }

        /* Read the ACTUAL user register state from the syscall entry
         * PUSH_GPRS frame on the kernel stack. cur->ctx is stale
         * (set at task creation), but the real registers are saved
         * by isr64.S's syscall_entry into the kernel stack frame.
         * Layout: [r15 r14 r13 r12 r11 r10 r9 r8 rbp rdi rsi rdx rcx rbx rax
         *          vec err rip cs rflags rsp ss]  */
        {
          uint64_t *f=task_syscall_user_frame(cur);
          if(f){
            child->ctx.r15=f[0]; child->ctx.r14=f[1];
            child->ctx.r13=f[2]; child->ctx.r12=f[3];
            child->ctx.r11=f[4]; child->ctx.r10=f[5];
            child->ctx.r9=f[6];  child->ctx.r8=f[7];
            child->ctx.rbp=f[8]; child->ctx.rdi=f[9];
            child->ctx.rsi=f[10]; child->ctx.rdx=f[11];
            child->ctx.rcx=f[12]; child->ctx.rbx=f[13];
            child->ctx.rip=f[17]; /* user RIP (after syscall instruction) */
            child->ctx.rflags=f[19]; /* user RFLAGS */
            child->ctx.cs=0x28; child->ctx.ss=0x30;
            child->ctx.fs_base=cur->ctx.fs_base;
            child->ctx.gs_base=cur->ctx.gs_base;
          } else {
            child->ctx=cur->ctx;
          }
        }
        child->ctx.rax=0; /* child sees 0 from clone */
        if(child_stack)child->ctx.rsp=child_stack;
        if(flags&C3_CLONE_SETTLS)child->ctx.fs_base=tls;
        c3_clone_prepare_user_tls_stack(cur,(uint64_t)child->pid,flags,child_stack,ptid,ctid,tls);

        if(flags&C3_CLONE_VM){
            child->tgid=vfork_vm_share?child->pid:(cur->tgid?cur->tgid:cur->pid);
            child->ppid=vfork_vm_share?(cur->tgid?cur->tgid:cur->pid):cur->ppid;
            child->addr_space=cur->addr_space;
            child->brk_start=cur->brk_start;
            child->brk_current=cur->brk_current;
            child->mmap_base=cur->mmap_base;
        }
        if(flags&C3_CLONE_SIGHAND){
            c3_memcpy(child->sigactions,cur->sigactions,sizeof(cur->sigactions));
            child->sig_blocked=cur->sig_blocked;
            child->sig_pending.sig[0]=0;
            child->sig_pending.sig[1]=0;
        }
        child->pgid=cur->pgid;
        child->sid=cur->sid;
        if(flags&C3_CLONE_FILES){
            if(!cur->fdt_group)cur->fdt_group=cur->pid;
            child->fdt_group=cur->fdt_group;
        }else{
            child->fdt_group=child->pid;
        }
        child->clear_child_tid=(flags&C3_CLONE_CHILD_CLEARTID)?ctid:0;
        if(flags&C3_CLONE_CHILD_SETTID&&ctid){
            *(int*)(uintptr_t)ctid=child->pid;
        }
        if(flags&C3_CLONE_PARENT_SETTID&&ptid){
            *(int*)(uintptr_t)ptid=child->pid;
        }
        {
            int ci=c3_task_index_by_pid(child->pid);
            int pi=g_current_task;
            if(ci>=0&&ci<TASK_MAX&&pi>=0&&pi<TASK_MAX){
                g_c3_seccomp_mode[ci]=g_c3_seccomp_mode[pi];
                g_c3_seccomp_flags[ci]=g_c3_seccomp_flags[pi];
                g_c3_ns_mask[ci]=g_c3_ns_mask[pi];
                g_c3_no_new_privs[ci]=g_c3_no_new_privs[pi];
            }
        }
        /* Set up kernel stack so context_switch_kstack into this
         * child lands in clone3_entry_trampoline -> ring 3. */
        { extern void clone3_setup_kstack(task_t *t);
          clone3_setup_kstack(child); }
        if(vfork_vm_share){
            cur->wait_pid=child->pid;
            cur->state=TASK_SLEEPING;
            task_schedule();
            if(cur->state==TASK_SLEEPING)cur->state=TASK_RUNNING;
            cur->wait_pid=0;
        }
        /* Let the parent return from clone and reach the common
         * pthread futex wait/join path before timer preemption starts
         * the new thread. */
        if(!vfork_vm_share)g_task_preempt_defer_ticks=2;
        return(int64_t)child->pid;
    }

    /* Process-like clone falls back to fork-like behavior */
    pid=(int)real_sys_fork();
    if(pid<0){c3_trace_clone_fail(cur,pid);return pid;}
    child=c3_task_by_pid(pid);
    if(!child)return pid;
    {
        const char *ep=c3_task_exec_path(cur);
        if(ep)c3_strlcpy(child->exec_path,ep,sizeof(child->exec_path));
        c3_task_force_browser_stable_mode(child,ep);
    }
    child->tgid=child->pid;
    if(flags&C3_CLONE_FILES){
        if(!cur->fdt_group)cur->fdt_group=cur->pid;
        child->fdt_group=cur->fdt_group;
    }else{
        child->fdt_group=child->pid;
    }

    /* Critical: task_fork() copies the parent's kernel stack and sets
     * child->kernel_rsp_saved to the equivalent location, but the
     * parent is currently mid-syscall so its kernel_rsp_saved is STALE
     * (it was last updated when the parent was switched IN by the
     * scheduler, not at the current syscall depth). Letting the child
     * resume via that stale RSP makes context_switch_kstack pop garbage
     * as the next return target, which is exactly what produced the GP
     * fault at user RIP=0x1003d2 (low kernel-load address that happens
     * to be on the kernel stack mid-handler).
     *
     * Fix: discard task_fork()'s stack-copy plan and rebuild the child
     * from the LIVE syscall user frame, exactly like the thread-clone
     * path above does. Then push a fresh clone3 trampoline frame on the
     * child's kernel stack so the next context switch into the child
     * lands in clone3_entry_trampoline -> clone3_enter_user -> iretq to
     * ring 3 at parent's user RIP with rax=0. This mirrors FreeBSD's
     * linux_set_upcall() which builds a fresh trapframe for the child
     * (sys/compat/linux/linux_misc.c). */
    {
      uint64_t *f=task_syscall_user_frame(cur);
      if(f){
        child->ctx.r15=f[0]; child->ctx.r14=f[1];
        child->ctx.r13=f[2]; child->ctx.r12=f[3];
        child->ctx.r11=f[4]; child->ctx.r10=f[5];
        child->ctx.r9=f[6];  child->ctx.r8=f[7];
        child->ctx.rbp=f[8]; child->ctx.rdi=f[9];
        child->ctx.rsi=f[10]; child->ctx.rdx=f[11];
        child->ctx.rcx=f[12]; child->ctx.rbx=f[13];
        child->ctx.rip=f[17]; /* user RIP after the SYSCALL instruction */
        child->ctx.rflags=f[19];
        child->ctx.cs=0x28; child->ctx.ss=0x30;
        child->ctx.fs_base=cur->ctx.fs_base;
        child->ctx.gs_base=cur->ctx.gs_base;
      } else {
        child->ctx=cur->ctx;
      }
    }
    child->ctx.rax=0; /* child sees 0 from clone */
    if(child_stack)child->ctx.rsp=child_stack;
    if(flags&C3_CLONE_SETTLS)child->ctx.fs_base=tls;
    child->clear_child_tid=(flags&C3_CLONE_CHILD_CLEARTID)?ctid:0;
    if(flags&C3_CLONE_CHILD_SETTID&&ctid){
        int wr=c3_user_write_u32_as(child->addr_space,ctid,(uint32_t)child->pid,"clone-child-settid");
        if(wr<0){
            __boot_serial_puts("[clone-child-settid] write-fail child=");
            __boot_serial_putu32((uint32_t)child->pid);
            __boot_serial_puts(" addr=");
            __boot_serial_puthex64(ctid);
            __boot_serial_puts(" rc=");
            if(wr<0){__boot_serial_puts("-");__boot_serial_putu32((uint32_t)(-wr));}
            else __boot_serial_putu32((uint32_t)wr);
            __boot_serial_puts("\n");
        }
    }
    if(flags&C3_CLONE_PARENT_SETTID&&ptid){
        *(int*)(uintptr_t)ptid=child->pid;
    }
    ci=c3_task_index_by_pid(pid);
    if(ci>=0&&ci<TASK_MAX){
        int pi=g_current_task;
        if(pi>=0&&pi<TASK_MAX){
            g_c3_seccomp_mode[ci]=g_c3_seccomp_mode[pi];
            g_c3_seccomp_flags[ci]=g_c3_seccomp_flags[pi];
            g_c3_ns_mask[ci]=g_c3_ns_mask[pi];
            g_c3_no_new_privs[ci]=g_c3_no_new_privs[pi];
        }
    }
    /* Build a fresh trampoline frame on the child's kernel stack so the
     * next context_switch_kstack into the child lands in
     * clone3_entry_trampoline -> clone3_enter_user -> iretq to ring 3
     * with the registers we just populated. */
    { extern void clone3_setup_kstack(task_t *t);
      clone3_setup_kstack(child); }
    /* Same defer-preempt nudge as thread-clone path so the parent gets
     * to record the returned child pid before timer preemption hands
     * control to the child. */
    g_task_preempt_defer_ticks=2;
    return pid;
}

int64_t real_sys_execve(const char *path,char *const argv[],char *const envp[]){
    const uint8_t *data;
    uint32_t sz;
    const uint8_t *interp_data=0;
    uint32_t interp_sz=0;
    char npath[VFS_PATH_MAX];
    char interp[VFS_PATH_MAX];
    char interp_path[VFS_PATH_MAX];
    char old_exec_path[256];
    task_t *cur=task_current();
    address_space_t *old_as,*new_as=0;
    uint64_t old_brk_start,old_brk_current,old_mmap_base,old_rip,old_rsp,old_entry;
    uint64_t old_aux_phdr,old_aux_phent,old_aux_phnum,old_aux_base,old_aux_flags,old_aux_entry;
    uint64_t old_aux_uid,old_aux_euid,old_aux_gid,old_aux_egid,old_aux_random,old_aux_execfn,old_aux_platform;
    uint32_t old_dyn_images_mapped,old_dyn_reloc_applied,old_dyn_reloc_failed,old_dyn_reloc_unsupported;
    uint32_t old_dyn_needed_loaded,old_dyn_needed_missing,old_dyn_objects,old_dyn_timeout_hits;
    uint32_t old_dyn_profile_next;
    int old_dyn_load_depth;
    uint16_t old_tls_next_module;
    uint64_t old_dyn_last_timeout_tsc,old_dyn_cycle_map,old_dyn_cycle_needed,old_dyn_cycle_reloc;
    uint64_t main_lb=0,main_entry=0,main_phdr=0,main_phent=0,main_phnum=0;
    uint64_t interp_lb=0,interp_entry=0,interp_phdr=0,interp_phent=0,interp_phnum=0;
    auxv_t auxv[24];
    char old_next_image[sizeof(g_c3_next_image_name)];
    char old_dyn_last_timeout_obj[sizeof(g_c3_dyn_last_timeout_obj)];
    char old_dyn_last_timeout_stage[sizeof(g_c3_dyn_last_timeout_stage)];
    bool needs_interp=false;
    bool has_interp=false;
    bool switched=false;
    bool is_chrome_exec=false;
    int chrome_filtered_args=0;
    int chrome_added_args=0;
    int chrome_v8_fd_rc=0;
    int argc=0;
    int envc=0;
    int rc;
    if(!cur||!path)return -EFAULT;
    if(c3_resolve_user_path(cur,AT_FDCWD,path,npath,sizeof(npath))<0)return -EINVAL;
    rc=c3_resolve_proc_exe_exec_path(cur,npath,npath,sizeof(npath));
    if(rc<0)return rc;
    if(rc>0){
        __boot_serial_puts("[execve-proc-exe] pid=");
        __boot_serial_putu32((uint32_t)cur->pid);
        c3_trace_task_name(cur);
        __boot_serial_puts(" -> ");
        __boot_serial_puts(npath);
        __boot_serial_puts("\n");
    }
    if(!kvfs_read(npath,&data,&sz))return -ENOENT;
    if(!elf64_validate(data,sz))return -ENOEXEC;

    rc=c3_exec_capture_vec(argv,g_c3_exec_argv,C3_EXEC_ARG_MAX,g_c3_exec_argv_pool,sizeof(g_c3_exec_argv_pool),&argc);
    if(rc<0)return rc;
    rc=c3_exec_capture_vec(envp,g_c3_exec_envp,C3_EXEC_ENV_MAX,g_c3_exec_env_pool,sizeof(g_c3_exec_env_pool),&envc);
    if(rc<0)return rc;
    if(argc==0){
        size_t plen=c3_strlen(npath);
        if(plen+1>sizeof(g_c3_exec_argv_pool))return -E2BIG;
        c3_memcpy(g_c3_exec_argv_pool,npath,plen+1);
        g_c3_exec_argv[0]=g_c3_exec_argv_pool;
        g_c3_exec_argv[1]=0;
        argc=1;
    }
    is_chrome_exec=c3_exec_is_chromium_trace(npath,g_c3_exec_argv,argc);
    if(is_chrome_exec){
        chrome_filtered_args=c3_exec_filter_chromium_args(g_c3_exec_argv,&argc);
        chrome_added_args=c3_exec_stabilize_chromium_args(g_c3_exec_argv,&argc);
        chrome_v8_fd_rc=c3_exec_chromium_ensure_v8_fd100(cur,g_c3_exec_argv,argc);
        if(chrome_v8_fd_rc<0){
            __boot_serial_force_puts("[execve-chrome-v8fd-fail!] pid=");
            __boot_serial_force_putu32((uint32_t)cur->pid);
            c3_force_task_name(cur);
            __boot_serial_force_puts(" rc=");
            c3_force_rc((int64_t)chrome_v8_fd_rc);
            __boot_serial_force_puts("\n");
        }
        if(chrome_filtered_args>0){
            __boot_serial_force_puts("[execve-chrome-filter!] pid=");
            __boot_serial_force_putu32((uint32_t)cur->pid);
            c3_force_task_name(cur);
            __boot_serial_force_puts(" removed=");
            __boot_serial_force_putu32((uint32_t)chrome_filtered_args);
            __boot_serial_force_puts("\n");
        }
        if(chrome_added_args>0){
            __boot_serial_force_puts("[execve-chrome-stable!] pid=");
            __boot_serial_force_putu32((uint32_t)cur->pid);
            c3_force_task_name(cur);
            __boot_serial_force_puts(" added=");
            __boot_serial_force_putu32((uint32_t)chrome_added_args);
            __boot_serial_force_puts("\n");
        }
    }
    if(c3_trace_firefox_path_needed(npath)){
        int ai;
        __boot_serial_puts("[execve-firefox] pid=");
        __boot_serial_putu32((uint32_t)cur->pid);
        c3_trace_task_name(cur);
        __boot_serial_puts(" path=");
        __boot_serial_puts(npath);
        __boot_serial_puts(" argc=");
        __boot_serial_putu32((uint32_t)argc);
        __boot_serial_puts("\n");
        for(ai=0;ai<argc&&ai<32;++ai){
            __boot_serial_puts("[execve-firefox]   ");
            __boot_serial_putu32((uint32_t)ai);
            __boot_serial_puts(": ");
            __boot_serial_puts(g_c3_exec_argv[ai]?g_c3_exec_argv[ai]:"(null)");
            __boot_serial_puts("\n");
        }
        for(ai=0;ai<envc&&ai<128;++ai){
            const char *ev=g_c3_exec_envp[ai];
            if(!ev)continue;
            if(c3_has_token(ev,"MOZ_FORCE_DISABLE")||
               c3_has_token(ev,"MOZ_DISABLE_E10S")||
               c3_has_token(ev,"MOZ_DISABLE_FORKSERVER")||
               c3_has_token(ev,"FISSION")||
               c3_has_token(ev,"LD_PRELOAD")){
                __boot_serial_puts("[execve-firefox-env]   ");
                __boot_serial_puts(ev);
                __boot_serial_puts("\n");
            }
        }
        c3_trace_fd_table("[execve-firefox-fds-before]",cur);
    }
    if(is_chrome_exec){
        static uint32_t chrome_exec_trace_count=0;
        if(chrome_exec_trace_count<10){
            int ai;
            ++chrome_exec_trace_count;
            __boot_serial_force_puts("[execve-chrome!] pid=");
            __boot_serial_force_putu32((uint32_t)cur->pid);
            c3_force_task_name(cur);
            __boot_serial_force_puts(" path=");
            __boot_serial_force_puts(npath);
            __boot_serial_force_puts(" argc=");
            __boot_serial_force_putu32((uint32_t)argc);
            __boot_serial_force_puts("\n");
            for(ai=0;ai<argc&&ai<24;++ai){
                __boot_serial_force_puts("[execve-chrome!]   ");
                __boot_serial_force_putu32((uint32_t)ai);
                __boot_serial_force_puts(": ");
                __boot_serial_force_puts(g_c3_exec_argv[ai]?g_c3_exec_argv[ai]:"(null)");
                __boot_serial_force_puts("\n");
            }
            c3_force_chrome_fd_table("[execve-chrome-fds-before!]",cur);
        }
    }
    if(c3_exec_is_firefox_glxtest(npath,g_c3_exec_argv,argc)){
        __boot_serial_puts("[execve-firefox] suppressing glxtest helper pid=");
        __boot_serial_putu32((uint32_t)cur->pid);
        __boot_serial_puts("\n");
        return real_sys_exit(0);
    }
    (void)envc;

    rc=c3_exec_extract_interp(data,sz,interp,sizeof(interp),&needs_interp);
    if(rc<0)return rc;
    if(needs_interp&&!interp[0])return -ENOEXEC;
    interp_path[0]=0;
    if(interp[0]){
        if(c3_exec_resolve_interp_path(npath,interp,interp_path,sizeof(interp_path))<0)return -ENOEXEC;
        if(!kvfs_read(interp_path,&interp_data,&interp_sz))return -ENOENT;
        if(!elf64_validate(interp_data,interp_sz))return -ENOEXEC;
        has_interp=true;
    }

    old_as=cur->addr_space;
    old_brk_start=cur->brk_start;
    old_brk_current=cur->brk_current;
    old_mmap_base=cur->mmap_base;
    old_rip=cur->ctx.rip;
    old_rsp=cur->ctx.rsp;
    old_entry=cur->entry_point;
    old_aux_phdr=cur->aux_at_phdr;
    old_aux_phent=cur->aux_at_phent;
    old_aux_phnum=cur->aux_at_phnum;
    old_aux_base=cur->aux_at_base;
    old_aux_flags=cur->aux_at_flags;
    old_aux_entry=cur->aux_at_entry;
    old_aux_uid=cur->aux_at_uid;
    old_aux_euid=cur->aux_at_euid;
    old_aux_gid=cur->aux_at_gid;
    old_aux_egid=cur->aux_at_egid;
    old_aux_random=cur->aux_at_random;
    old_aux_execfn=cur->aux_at_execfn;
    old_aux_platform=cur->aux_at_platform;
    c3_strlcpy(old_exec_path,cur->exec_path,sizeof(old_exec_path));
    old_dyn_images_mapped=g_c3_dyn_images_mapped;
    old_dyn_reloc_applied=g_c3_dyn_reloc_applied;
    old_dyn_reloc_failed=g_c3_dyn_reloc_failed;
    old_dyn_reloc_unsupported=g_c3_dyn_reloc_unsupported;
    old_dyn_needed_loaded=g_c3_dyn_needed_loaded;
    old_dyn_needed_missing=g_c3_dyn_needed_missing;
    old_dyn_objects=g_c3_dyn_objects;
    old_dyn_load_depth=g_c3_dyn_load_depth;
    old_dyn_timeout_hits=g_c3_dyn_timeout_hits;
    old_dyn_last_timeout_tsc=g_c3_dyn_last_timeout_tsc;
    old_dyn_cycle_map=g_c3_dyn_cycle_map;
    old_dyn_cycle_needed=g_c3_dyn_cycle_needed;
    old_dyn_cycle_reloc=g_c3_dyn_cycle_reloc;
    old_dyn_profile_next=g_c3_dyn_profile_next;
    old_tls_next_module=g_c3_tls_next_module;
    c3_memcpy(g_c3_exec_dyn_backup,g_c3_dynobjs,sizeof(g_c3_dynobjs));
    c3_memcpy(g_c3_exec_dyn_profile_backup,g_c3_dyn_profile,sizeof(g_c3_dyn_profile));
    c3_strlcpy(old_next_image,g_c3_next_image_name,sizeof(old_next_image));
    c3_strlcpy(old_dyn_last_timeout_obj,g_c3_dyn_last_timeout_obj,sizeof(old_dyn_last_timeout_obj));
    c3_strlcpy(old_dyn_last_timeout_stage,g_c3_dyn_last_timeout_stage,sizeof(old_dyn_last_timeout_stage));

    new_as=paging_create_address_space();
    if(!new_as)return -ENOMEM;
    if(!c3_exec_map_user_stack(new_as)){
        paging_destroy_address_space(new_as);
        return -ENOMEM;
    }

    cur->addr_space=new_as;
    cur->brk_start=0x800000ULL;
    cur->brk_current=0x800000ULL;
    cur->mmap_base=0x40000000ULL;
    cur->ctx.rsp=C3_USER_STACK_TOP-16;
    cur->entry_point=0;
    cur->exec_path[0]=0;
    cur->aux_at_phdr=0;
    cur->aux_at_phent=0;
    cur->aux_at_phnum=0;
    cur->aux_at_base=0;
    cur->aux_at_flags=0;
    cur->aux_at_entry=0;
    cur->aux_at_uid=(uint64_t)cur->uid;
    cur->aux_at_euid=(uint64_t)cur->euid;
    cur->aux_at_gid=(uint64_t)cur->gid;
    cur->aux_at_egid=(uint64_t)cur->egid;
    cur->aux_at_random=0;
    cur->aux_at_execfn=0;
    cur->aux_at_platform=0;

    paging_switch(new_as);
    switched=true;
    c3_exec_reset_dynamic_state();
    c3_exec_clear_mmap_for_as(new_as);

    rc=c3_exec_collect_elf_info(data,sz,cur->mmap_base,&main_lb,&main_entry,&main_phdr,&main_phent,&main_phnum);
    if(rc<0)goto exec_fail;
    compat3_set_next_image_name(npath);
    rc=elf64_map_into_task(cur,data,sz);
    if(rc<0)goto exec_fail;

    if(has_interp){
        rc=c3_exec_collect_elf_info(interp_data,interp_sz,cur->mmap_base,&interp_lb,&interp_entry,&interp_phdr,&interp_phent,&interp_phnum);
        if(rc<0)goto exec_fail;
        compat3_set_next_image_name(interp_path);
        rc=elf64_map_into_task(cur,interp_data,interp_sz);
        if(rc<0)goto exec_fail;
        cur->ctx.rip=interp_entry;
        cur->entry_point=interp_entry;
    }else{
        cur->ctx.rip=main_entry;
        cur->entry_point=main_entry;
    }
    (void)interp_phdr;
    (void)interp_phent;
    (void)interp_phnum;

    c3_strlcpy(cur->exec_path,npath,sizeof(cur->exec_path));
    c3_task_force_browser_stable_mode(cur,npath);
    cur->aux_at_phdr=main_phdr;
    cur->aux_at_phent=main_phent;
    cur->aux_at_phnum=main_phnum;
    cur->aux_at_base=has_interp?interp_lb:0;
    cur->aux_at_flags=0;
    cur->aux_at_entry=main_entry;
    cur->aux_at_uid=(uint64_t)cur->uid;
    cur->aux_at_euid=(uint64_t)cur->euid;
    cur->aux_at_gid=(uint64_t)cur->gid;
    cur->aux_at_egid=(uint64_t)cur->egid;

    {
        int auxc=0;
        #define C3_AUX_PUSH(_t,_v) do{if(auxc<(int)(sizeof(auxv)/sizeof(auxv[0]))){auxv[auxc].a_type=(uint64_t)(_t);auxv[auxc].a_val=(uint64_t)(_v);++auxc;}}while(0)
        C3_AUX_PUSH(AT_PHDR,cur->aux_at_phdr);
        C3_AUX_PUSH(AT_PHENT,cur->aux_at_phent);
        C3_AUX_PUSH(AT_PHNUM,cur->aux_at_phnum);
        C3_AUX_PUSH(AT_PAGESZ,PAGE_SIZE);
        C3_AUX_PUSH(AT_BASE,cur->aux_at_base);
        C3_AUX_PUSH(AT_FLAGS,cur->aux_at_flags);
        C3_AUX_PUSH(AT_ENTRY,cur->aux_at_entry);
        C3_AUX_PUSH(AT_UID,cur->aux_at_uid);
        C3_AUX_PUSH(AT_EUID,cur->aux_at_euid);
        C3_AUX_PUSH(AT_GID,cur->aux_at_gid);
        C3_AUX_PUSH(AT_EGID,cur->aux_at_egid);
        C3_AUX_PUSH(AT_PLATFORM,(uint64_t)(uintptr_t)"x86_64");
        C3_AUX_PUSH(AT_HWCAP,0);
        C3_AUX_PUSH(AT_CLKTCK,100);
        C3_AUX_PUSH(AT_SECURE,0);
        C3_AUX_PUSH(AT_RANDOM,0);
        C3_AUX_PUSH(AT_HWCAP2,0);
        C3_AUX_PUSH(AT_EXECFN,(uint64_t)(uintptr_t)cur->exec_path);
        C3_AUX_PUSH(AT_SYSINFO_EHDR,0);
        #undef C3_AUX_PUSH
        rc=elf64_setup_stack(cur,argc,g_c3_exec_argv,g_c3_exec_envp,auxv,auxc);
        if(rc<0)goto exec_fail;
    }

    c3_exec_close_cloexec_fds(cur);
    if(c3_trace_firefox_path_needed(npath))c3_trace_fd_table("[execve-firefox-fds-after-cloexec]",cur);
    if(is_chrome_exec){
        static uint32_t chrome_fd_after_trace_count=0;
        if(chrome_fd_after_trace_count<10){
            ++chrome_fd_after_trace_count;
            c3_force_chrome_fd_table("[execve-chrome-fds-after-cloexec!]",cur);
        }
    }
    c3_exec_reset_signal_state(cur);
    compat4_tls_reset_task(g_current_task);
    if(has_interp){
        /*
         * A Linux kernel does not prebuild a glibc TCB for dynamically
         * linked programs.  ld-linux owns initial TLS setup and will install
         * the real thread pointer with arch_prctl(ARCH_SET_FS).  Starting it
         * on our synthetic TLS block can make glibc keep pointers into the
         * seed TCB and later crash in libc TLS users such as __ctype_init.
         */
        cur->ctx.fs_base=0;
        cur->ctx.gs_base=0;
    }else{
        rc=compat3_init_user_tls(cur);
        if(rc<0)goto exec_fail;
    }
    if(!c3_addr_space_has_other_live_task(old_as,cur))c3_exec_clear_mmap_for_as(old_as);
    {
        const char *n=npath;
        const char *s=npath;
        while(*s){if(*s=='/')n=s+1;++s;}
        c3_strlcpy(cur->name,n,TASK_NAME_LEN);
    }
    c3_exec_wake_vfork_parent(cur);
    /* execve success never returns to the pre-exec syscall site.
     * Jump straight into the new image so we do not iretq back to
     * the old user RIP with a replaced address space underneath. */
    task_launch_to_user(cur);
    return 0;

exec_fail:
    cur->addr_space=old_as;
    cur->brk_start=old_brk_start;
    cur->brk_current=old_brk_current;
    cur->mmap_base=old_mmap_base;
    cur->ctx.rip=old_rip;
    cur->ctx.rsp=old_rsp;
    cur->entry_point=old_entry;
    cur->aux_at_phdr=old_aux_phdr;
    cur->aux_at_phent=old_aux_phent;
    cur->aux_at_phnum=old_aux_phnum;
    cur->aux_at_base=old_aux_base;
    cur->aux_at_flags=old_aux_flags;
    cur->aux_at_entry=old_aux_entry;
    cur->aux_at_uid=old_aux_uid;
    cur->aux_at_euid=old_aux_euid;
    cur->aux_at_gid=old_aux_gid;
    cur->aux_at_egid=old_aux_egid;
    cur->aux_at_random=old_aux_random;
    cur->aux_at_execfn=old_aux_execfn;
    cur->aux_at_platform=old_aux_platform;
    g_c3_dyn_images_mapped=old_dyn_images_mapped;
    g_c3_dyn_reloc_applied=old_dyn_reloc_applied;
    g_c3_dyn_reloc_failed=old_dyn_reloc_failed;
    g_c3_dyn_reloc_unsupported=old_dyn_reloc_unsupported;
    g_c3_dyn_needed_loaded=old_dyn_needed_loaded;
    g_c3_dyn_needed_missing=old_dyn_needed_missing;
    g_c3_dyn_objects=old_dyn_objects;
    g_c3_dyn_load_depth=old_dyn_load_depth;
    g_c3_dyn_timeout_hits=old_dyn_timeout_hits;
    g_c3_dyn_last_timeout_tsc=old_dyn_last_timeout_tsc;
    g_c3_dyn_cycle_map=old_dyn_cycle_map;
    g_c3_dyn_cycle_needed=old_dyn_cycle_needed;
    g_c3_dyn_cycle_reloc=old_dyn_cycle_reloc;
    g_c3_dyn_profile_next=old_dyn_profile_next;
    g_c3_tls_next_module=old_tls_next_module;
    c3_memcpy(g_c3_dynobjs,g_c3_exec_dyn_backup,sizeof(g_c3_dynobjs));
    c3_memcpy(g_c3_dyn_profile,g_c3_exec_dyn_profile_backup,sizeof(g_c3_dyn_profile));
    c3_strlcpy(g_c3_next_image_name,old_next_image,sizeof(g_c3_next_image_name));
    c3_strlcpy(g_c3_dyn_last_timeout_obj,old_dyn_last_timeout_obj,sizeof(g_c3_dyn_last_timeout_obj));
    c3_strlcpy(g_c3_dyn_last_timeout_stage,old_dyn_last_timeout_stage,sizeof(g_c3_dyn_last_timeout_stage));
    c3_strlcpy(cur->exec_path,old_exec_path,sizeof(cur->exec_path));
    if(switched&&old_as)paging_switch(old_as);
    if(new_as&&new_as!=old_as)paging_destroy_address_space(new_as);
    return rc<0?rc:-ENOEXEC;
}

#define C3_FUTEX_WAITERS_BIT    0x80000000u
#define C3_FUTEX_OWNER_DIED_BIT 0x40000000u
#define C3_FUTEX_TID_MASK       0x3fffffffu
#define C3_FUTEX_BITSET_ANY     0xFFFFFFFFu

typedef struct {
    uint64_t next;
} c3_robust_list64_t;

typedef struct {
    uint64_t list_next;
    int64_t  futex_offset;
    uint64_t list_op_pending;
} c3_robust_list_head64_t;

static void c3_robust_wake_one(task_t *cur,uint64_t node,int64_t futex_offset){
    uint64_t faddr;
    uint32_t *uaddr;
    uint32_t oldv,newv,woken;
    if(!cur||!node)return;
    faddr=(uint64_t)((int64_t)node+futex_offset);
    if(!faddr||((uintptr_t)faddr&3U)!=0)return;
    uaddr=(uint32_t*)(uintptr_t)faddr;
    oldv=*uaddr;
    if((oldv&C3_FUTEX_TID_MASK)!=(uint32_t)cur->pid)return;
    newv=(oldv&C3_FUTEX_WAITERS_BIT)|C3_FUTEX_OWNER_DIED_BIT;
    *uaddr=newv;
    woken=c3_futex_wake_n(uaddr,1,(uintptr_t)cur->addr_space,true,C3_FUTEX_BITSET_ANY);
    if(!woken)(void)c3_futex_wake_n(uaddr,(uint32_t)-1,0,false,C3_FUTEX_BITSET_ANY);
}

static void c3_robust_list_exit(task_t *cur){
    c3_robust_list_head64_t *head;
    uint64_t head_addr,next,pending;
    int64_t futex_offset;
    uint32_t guard=0;
    if(!cur||!cur->robust_list_head)return;
    if(cur->robust_list_len&&cur->robust_list_len<sizeof(c3_robust_list_head64_t))return;
    head_addr=cur->robust_list_head;
    head=(c3_robust_list_head64_t*)(uintptr_t)head_addr;
    next=head->list_next;
    futex_offset=head->futex_offset;
    pending=head->list_op_pending;
    while(next&&next!=head_addr&&guard++<2048u){
        c3_robust_list64_t *node=(c3_robust_list64_t*)(uintptr_t)next;
        uint64_t cur_node=next;
        next=node->next;
        c3_robust_wake_one(cur,cur_node,futex_offset);
    }
    if(pending&&pending!=head_addr)c3_robust_wake_one(cur,pending,futex_offset);
}

int64_t real_sys_exit(int code){
    task_t *cur=task_current();
    int ti=g_current_task;
    static uint32_t force_exit_trace_count;
    if(!cur)return -ESRCH;
    if(force_exit_trace_count<128u||code!=0){
        ++force_exit_trace_count;
        __boot_serial_force_puts("[exit!] pid=");
        __boot_serial_force_putu32((uint32_t)cur->pid);
        __boot_serial_force_puts(" code=");
        __boot_serial_force_putu32((uint32_t)code);
        c3_force_task_name(cur);
        __boot_serial_force_puts("\n");
    }
    __boot_serial_puts("[exit] pid=");
    __boot_serial_putu32((uint32_t)cur->pid);
    __boot_serial_puts(" code=");
    __boot_serial_putu32((uint32_t)code);
    __boot_serial_puts("\n");
    c3_robust_list_exit(cur);
    if(cur&&cur->clear_child_tid){
        uint32_t *ctid=(uint32_t*)(uintptr_t)cur->clear_child_tid;
        __boot_serial_puts("[exit] clear_child_tid=");
        __boot_serial_puthex64(cur->clear_child_tid);
        __boot_serial_puts("\n");
        *ctid=0;
        { uint32_t woken=c3_futex_wake_n(ctid,1,(uintptr_t)cur->addr_space,true,0xFFFFFFFFu);
          __boot_serial_puts("[exit] futex_wake woken=");
          __boot_serial_putu32(woken);
          __boot_serial_puts("\n");
          if(!woken){
              /* Try broader wake without exact key match */
              woken=c3_futex_wake_n(ctid,(uint32_t)-1,0,false,0xFFFFFFFFu);
              __boot_serial_puts("[exit] futex_wake(broad) woken=");
              __boot_serial_putu32(woken);
              __boot_serial_puts("\n");
          }
        }
    }
    if(cur){
        c3_futex_waiter_remove_task_uaddr(g_current_task,0,0,false);
    }
    if(ti>=0&&ti<TASK_MAX){
        g_c3_seccomp_mode[ti]=0;
        g_c3_seccomp_flags[ti]=0;
        g_c3_ns_mask[ti]=0;
        g_c3_no_new_privs[ti]=0;
    }
    task_exit(cur->pid,code);
    task_schedule();
    /* No more runnable user tasks. Do NOT halt the CPU: the original
     * kernel main loop iteration that spawned this task is parked in
     * the syscall path and can never return on its own, but the timer
     * IRQ handler in kernel.c (irq_pit) can keep the WM alive from
     * interrupt context as long as `g_user_foreground_active` stays
     * true. We sti+hlt forever so timer ticks continue, mouse +
     * keyboard remain responsive, and the user can launch another
     * Ring 3 task (the scheduler picks it up automatically). */
    __boot_serial_force_puts("[exit!] no runnable task; keeping WM alive via IRQ\n");
    __boot_serial_puts("[exit] no runnable task; keeping WM alive via IRQ\n");
    g_kernel_preempt_disable = 0;
    g_task_preempt_defer_ticks = 0;
    g_user_foreground_active = true;
    for (;;) __asm__ volatile("sti; hlt");
    return 0; /* unreachable */
}
static bool c3_same_thread_group(const task_t *a,const task_t *b){
    int ag,bg;
    if(!a||!b)return false;
    ag=a->tgid?a->tgid:a->pid;
    bg=b->tgid?b->tgid:b->pid;
    return ag==bg;
}

static void c3_trace_exit_group_stack(task_t *cur){
    static uint32_t trace_count=0;
    uint64_t *f;
    uint64_t rip=0,rsp=0;
    int i;
    if(!cur||trace_count>=8)return;
    ++trace_count;
    f=task_syscall_user_frame(cur);
    if(f){
        rip=f[17];
        rsp=f[20];
    }
    __boot_serial_puts("[exit_group-stack] pid=");
    __boot_serial_putu32((uint32_t)cur->pid);
    c3_trace_task_name(cur);
    __boot_serial_puts(" rip=");
    __boot_serial_puthex64(rip);
    __boot_serial_puts(" rsp=");
    __boot_serial_puthex64(rsp);
    __boot_serial_puts("\n");
    c3_trace_user_addr_mapping(cur,rip,"exitgrp-rip");
    if(!rsp||rsp<0x10000ULL||rsp>=0x0000800000000000ULL||!cur->addr_space)return;
    for(i=0;i<48;++i){
        uint64_t a=rsp+(uint64_t)i*8ULL;
        uint64_t sv;
        if(!(paging_get_entry(cur->addr_space,a)&PAGE_PRESENT))break;
        sv=*(uint64_t*)(uintptr_t)a;
        if((i&3)==0){
            __boot_serial_puts("[exit_group-stack] +");
            __boot_serial_putu32((uint32_t)(i*8));
            __boot_serial_puts(":");
        }
        __boot_serial_puts(" ");
        __boot_serial_puthex64(sv);
        if((i&3)==3)__boot_serial_puts("\n");
        c3_trace_user_addr_mapping(cur,sv,"exitgrp-sp");
    }
    if((i&3)!=0)__boot_serial_puts("\n");
}

int64_t real_sys_exit_group(int code){
    task_t *cur=task_current();
    int i;
    if(!cur)return -ESRCH;
    c3_trace_exit_group_stack(cur);
    __boot_serial_puts("[exit_group] tgid=");
    __boot_serial_putu32((uint32_t)(cur->tgid?cur->tgid:cur->pid));
    __boot_serial_puts(" code=");
    __boot_serial_putu32((uint32_t)code);
    __boot_serial_puts("\n");
    for(i=0;i<TASK_MAX;++i){
        task_t *t=&g_tasks[i];
        if(!t->used||t==cur)continue;
        if(t->state==TASK_ZOMBIE||t->state==TASK_FREE)continue;
        if(!c3_same_thread_group(cur,t))continue;
        c3_robust_list_exit(t);
        if(t->clear_child_tid){
            uint32_t *ctid=(uint32_t*)(uintptr_t)t->clear_child_tid;
            *ctid=0;
            (void)c3_futex_wake_n(ctid,(uint32_t)-1,(uintptr_t)t->addr_space,true,0xFFFFFFFFu);
            (void)c3_futex_wake_n(ctid,(uint32_t)-1,0,false,0xFFFFFFFFu);
        }
        c3_futex_waiter_remove_task_uaddr(i,0,0,false);
        task_exit(t->pid,code);
    }
    return real_sys_exit(code);
}

int64_t real_sys_wait4(int pid,int *status,int options,void *rusage){
    int64_t r;
    /* M1 diagnostic: log parent's user RSP at wait4 ENTRY (read from the
     * iretq frame on parent's own kernel stack, which is per-task and
     * therefore not affected by per-CPU gs:8 races) and at EXIT (after
     * task_waitpid blocks/wakes, possibly running other tasks in between).
     * If both prints show the SAME rsp value, then the iretq frame RSP
     * slot is intact and any post-wait4 user-space crash is due to
     * parent's user MEMORY being corrupted (likely the child's COW page
     * separation failing for the shared stack page). If they differ,
     * something is overwriting the iretq frame's rsp slot during the
     * syscall handler. */
    {
      uint64_t *f=task_syscall_user_frame(task_current());
      __boot_serial_puts("[wait4 enter] pid_arg=");
      __boot_serial_putu32((uint32_t)pid);
      __boot_serial_puts(" iretq.rsp=");
      __boot_serial_puthex64(f?f[20]:0);
      __boot_serial_puts(" iretq.rip=");
      __boot_serial_puthex64(f?f[17]:0);
      __boot_serial_puts("\n");
    }
    (void)rusage;
    r=(int64_t)task_waitpid(pid,status,options);
    {
      uint64_t *f=task_syscall_user_frame(task_current());
      __boot_serial_puts("[wait4 exit ] ret=");
      __boot_serial_puthex64((uint64_t)r);
      __boot_serial_puts(" iretq.rsp=");
      __boot_serial_puthex64(f?f[20]:0);
      __boot_serial_puts(" iretq.rip=");
      __boot_serial_puthex64(f?f[17]:0);
      __boot_serial_puts("\n");
    }
    return r;
}

typedef struct {
    int si_signo;
    int si_errno;
    int si_code;
    int __pad0;
    int si_pid;
    unsigned int si_uid;
    int si_status;
    int __pad1;
    int64_t si_utime;
    int64_t si_stime;
    uint8_t __pad[80];
} c3_linux_siginfo_chld_t;

int64_t real_sys_waitid(int idtype,int id,void *infop,int options,void *rusage){
    enum { C3_P_ALL=0, C3_P_PID=1, C3_P_PGID=2 };
    enum { C3_WNOHANG=1, C3_WEXITED=4, C3_WNOWAIT=0x01000000 };
    enum { C3_SIGCHLD=17, C3_CLD_EXITED=1 };
    task_t *cur=task_current();
    c3_linux_siginfo_chld_t *si=(c3_linux_siginfo_chld_t*)infop;
    int wait_pid=-1;
    int status=0;
    int wait_options=options&~C3_WNOWAIT;
    int64_t r;
    (void)rusage;
    if(!cur)return -ESRCH;
    if(!si)return -EFAULT;
    if(idtype==C3_P_ALL){
        wait_pid=-1;
    }else if(idtype==C3_P_PID){
        if(id<=0)return -EINVAL;
        wait_pid=id;
    }else if(idtype==C3_P_PGID){
        wait_pid=id? -id : 0;
    }else{
        return -EINVAL;
    }
    if(!(options&C3_WEXITED))wait_options|=C3_WNOHANG;
    c3_memset(si,0,sizeof(*si));
    {
        static uint32_t waitid_trace_count=0;
        if(waitid_trace_count<32){
            ++waitid_trace_count;
            __boot_serial_puts("[waitid] idtype=");
            __boot_serial_putu32((uint32_t)idtype);
            __boot_serial_puts(" id=");
            __boot_serial_putu32((uint32_t)id);
            __boot_serial_puts(" options=");
            __boot_serial_puthex64((uint64_t)(uint32_t)options);
            __boot_serial_puts("\n");
        }
    }
    if(options&C3_WNOWAIT){
        int i;
        bool have_match=false;
        for(i=0;i<TASK_MAX;++i){
            bool match=false;
            if(!g_tasks[i].used)continue;
            if(wait_pid>0)match=(g_tasks[i].pid==wait_pid&&g_tasks[i].ppid==cur->pid);
            else if(wait_pid==-1)match=(g_tasks[i].ppid==cur->pid);
            else if(wait_pid==0)match=(g_tasks[i].ppid==cur->pid&&g_tasks[i].pgid==cur->pgid);
            else match=(g_tasks[i].ppid==cur->pid&&g_tasks[i].pgid==-wait_pid);
            if(!match)continue;
            have_match=true;
            if(g_tasks[i].state!=TASK_ZOMBIE)continue;
            si->si_signo=C3_SIGCHLD;
            si->si_errno=0;
            si->si_code=C3_CLD_EXITED;
            si->si_pid=g_tasks[i].pid;
            si->si_uid=(unsigned int)g_tasks[i].uid;
            si->si_status=g_tasks[i].exit_code&0xFF;
            return 0;
        }
        if(!have_match)return -ECHILD;
        return 0;
    }
    r=(int64_t)task_waitpid(wait_pid,((options&C3_WNOWAIT)?0:&status),wait_options);
    if(r<0)return r;
    if(r==0)return 0;
    si->si_signo=C3_SIGCHLD;
    si->si_errno=0;
    si->si_code=C3_CLD_EXITED;
    si->si_pid=(int)r;
    si->si_uid=(unsigned int)cur->uid;
    si->si_status=(status>>8)&0xFF;
    return 0;
}

int64_t real_sys_getpid(void){
    task_t *cur=task_current();
    if(!cur)return -ESRCH;
    return(int64_t)(cur->tgid?cur->tgid:cur->pid);
}
int64_t real_sys_getppid(void){return(int64_t)task_current()->ppid;}
int64_t real_sys_gettid(void){return(int64_t)task_current()->pid;} /* 1:1 threading */
int64_t real_sys_getuid(void){return(int64_t)task_current()->uid;}
int64_t real_sys_getgid(void){return(int64_t)task_current()->gid;}
int64_t real_sys_geteuid(void){return(int64_t)task_current()->euid;}
int64_t real_sys_getegid(void){return(int64_t)task_current()->egid;}
int64_t real_sys_setuid(int uid){task_current()->uid=uid;task_current()->euid=uid;return 0;}
int64_t real_sys_setgid(int gid){task_current()->gid=gid;task_current()->egid=gid;return 0;}
int64_t real_sys_setpgid(int pid,int pgid){
    if(!pid)pid=task_current()->pid;
    return(int64_t)task_setpgid(pid,pgid);
}
int64_t real_sys_getpgid(int pid){
    if(!pid)pid=task_current()->pid;
    return(int64_t)task_getpgid(pid);
}
int64_t real_sys_setsid(void){return(int64_t)task_setsid(task_current()->pid);}
int64_t real_sys_getsid(int pid){
    if(!pid)pid=task_current()->pid;
    return(int64_t)task_getsid(pid);
}

/* Signal syscalls */
static bool c3_signal_default_fatal(int sig){
    switch(sig){
        case SIGKILL: case SIGSEGV: case SIGBUS: case SIGILL:
        case SIGFPE: case SIGABRT: case SIGTERM:
            return true;
        default:
            return false;
    }
}

static bool c3_signal_targets_current_pid(int pid,const task_t *cur){
    if(!cur)return false;
    if(pid>0)return pid==cur->pid;
    if(pid==0)return cur->pgid==g_tasks[g_current_task].pgid;
    if(pid==-1)return cur->pid>1;
    return cur->pgid==-pid;
}

static int64_t c3_signal_after_send(const char *op,int pid,int sig,int rc){
    task_t *cur=task_current();
    static uint32_t trace_count=0;
    static uint32_t abort_stack_dumps=0;
    if(trace_count<96){
        ++trace_count;
        __boot_serial_puts("[signal] ");
        __boot_serial_puts(op?op:"?");
        __boot_serial_puts(" pid=");
        __boot_serial_putu32((uint32_t)pid);
        __boot_serial_puts(" sig=");
        __boot_serial_putu32((uint32_t)sig);
        __boot_serial_puts(" rc=");
        __boot_serial_putu32((uint32_t)rc);
        if(cur){
            __boot_serial_puts(" cur=");
            __boot_serial_putu32((uint32_t)cur->pid);
            __boot_serial_puts(" h=");
            __boot_serial_puthex64((sig>0&&sig<NSIG)?(uint64_t)(uintptr_t)cur->sigactions[sig].handler:0);
            __boot_serial_puts(" blk=");
            __boot_serial_putu32((sig>0&&sig<64&&(cur->sig_blocked.sig[sig/64]&(1ULL<<(sig%64))))?1u:0u);
        }
        __boot_serial_puts("\n");
    }
    if(rc==0&&cur&&sig==SIGABRT&&abort_stack_dumps<4){
        uint64_t *f=task_syscall_user_frame(cur);
        ++abort_stack_dumps;
        __boot_serial_puts("[signal-abort-stack] pid=");
        __boot_serial_putu32((uint32_t)cur->pid);
        __boot_serial_puts(" rip=");
        __boot_serial_puthex64(f?f[17]:0);
        __boot_serial_puts(" rsp=");
        __boot_serial_puthex64(f?f[20]:0);
        {
            uint32_t lo=0,hi=0;
            uint64_t fs=0;
            __asm__ volatile("rdmsr":"=a"(lo),"=d"(hi):"c"(0xC0000100u));
            fs=((uint64_t)hi<<32)|lo;
            __boot_serial_puts(" ctx.fs=");
            __boot_serial_puthex64(cur->ctx.fs_base);
            __boot_serial_puts(" msr.fs=");
            __boot_serial_puthex64(fs);
            if(fs){
                __boot_serial_puts(" fs28=");
                __boot_serial_puthex64(*(const uint64_t*)(uintptr_t)(fs+0x28));
            }
        }
        __boot_serial_puts("\n");
        if(f&&f[20]){
            const uint64_t *sp=(const uint64_t*)(uintptr_t)f[20];
            c3_trace_user_addr_mapping(cur,f[17],"abort-rip");
            if(sig>0&&sig<NSIG)c3_trace_user_addr_mapping(cur,(uint64_t)(uintptr_t)cur->sigactions[sig].handler,"abort-handler");
            for(int i=0;i<48;++i){
                uint64_t sv=sp[i];
                if((i&3)==0){
                    __boot_serial_puts("[signal-abort-stack] +");
                    __boot_serial_putu32((uint32_t)(i*8));
                    __boot_serial_puts(":");
                }
                __boot_serial_puts(" ");
                __boot_serial_puthex64(sv);
                if((i&3)==3)__boot_serial_puts("\n");
                c3_trace_user_addr_mapping(cur,sv,"abort-sp");
            }
            if((48&3)!=0)__boot_serial_puts("\n");
        }
    }
    if(rc==0&&cur&&sig>0&&sig<NSIG&&c3_signal_targets_current_pid(pid,cur)){
        if(cur->sigactions[sig].handler==SIG_DFL&&c3_signal_default_fatal(sig)){
            __boot_serial_puts("[signal-fatal] self/default sig=");
            __boot_serial_putu32((uint32_t)sig);
            __boot_serial_puts("\n");
            return real_sys_exit_group(128+sig);
        }
        sig_check_pending(cur);
    }
    return(int64_t)rc;
}

int64_t real_sys_kill(int pid,int sig){
    int rc=sig_send(pid,sig);
    return c3_signal_after_send("kill",pid,sig,rc);
}
int64_t real_sys_tkill(int tid,int sig){
    int rc=sig_send(tid,sig);
    return c3_signal_after_send("tkill",tid,sig,rc);
}
int64_t real_sys_tgkill(int tgid,int tid,int sig){
    task_t *t=c3_task_by_pid(tid);
    if(tgid>0&&(!t||(t->tgid?t->tgid:t->pid)!=tgid))return -ESRCH;
    return c3_signal_after_send("tgkill",tid,sig,sig_send(tid,sig));
}

typedef struct {
    uint64_t sa_handler;
    uint64_t sa_flags;
    uint64_t sa_restorer;
    sigset_t2 sa_mask;
} c3_rt_sigaction_t;

static void c3_sigset_drop_unblockable(sigset_t2 *s){
    if(!s)return;
    s->sig[SIGKILL/64]&=~(1ULL<<(SIGKILL%64));
    s->sig[SIGSTOP/64]&=~(1ULL<<(SIGSTOP%64));
}

static bool c3_task_has_unblocked_signal(const task_t *t){
    uint64_t p0,p1;
    if(!t)return false;
    p0=t->sig_pending.sig[0]&~t->sig_blocked.sig[0];
    p1=t->sig_pending.sig[1]&~t->sig_blocked.sig[1];
    return (p0|p1)!=0;
}

int64_t real_sys_rt_sigaction(int sig,const void *act,void *oldact,size_t sigsetsize){
    task_t *cur=task_current();
    size_t nbytes=sigsetsize?sigsetsize:sizeof(uint64_t);
    if(!cur)return -ESRCH;
    if(sigsetsize&&(sigsetsize<sizeof(uint64_t)||sigsetsize>sizeof(sigset_t2)))return -EINVAL;
    if(sig<1||sig>=NSIG)return -EINVAL;
    if(sig==SIGKILL||sig==SIGSTOP)return -EINVAL;
    if(oldact){
        uint8_t oldsa[24+sizeof(sigset_t2)];
        c3_memset(oldsa,0,sizeof(oldsa));
        *(uint64_t*)(oldsa+0)=(uint64_t)(uintptr_t)cur->sigactions[sig].handler;
        *(uint64_t*)(oldsa+8)=cur->sigactions[sig].flags;
        *(uint64_t*)(oldsa+16)=(uint64_t)(uintptr_t)cur->sigactions[sig].restorer;
        c3_memcpy(oldsa+24,&cur->sigactions[sig].mask,nbytes);
        c3_memcpy(oldact,oldsa,24+nbytes);
    }
    if(act){
        const uint8_t *sa=(const uint8_t*)act;
        sigset_t2 mask;
        c3_memset(&mask,0,sizeof(mask));
        c3_memcpy(&mask,sa+24,nbytes);
        cur->sigactions[sig].handler=(sig_handler_t)(uintptr_t)(*(const uint64_t*)(sa+0));
        cur->sigactions[sig].flags=(uint32_t)(*(const uint64_t*)(sa+8));
        cur->sigactions[sig].restorer=(void*)(uintptr_t)(*(const uint64_t*)(sa+16));
        cur->sigactions[sig].mask=mask;
        c3_sigset_drop_unblockable(&cur->sigactions[sig].mask);
    }
    return 0;
}

int64_t real_sys_rt_sigprocmask(int how,const void *set,void *oldset,size_t sigsetsize){
    task_t *cur=task_current();
    size_t nbytes=sigsetsize?sigsetsize:sizeof(sigset_t2);
    if(!cur)return -ESRCH;
    if(sigsetsize&&(sigsetsize<sizeof(uint64_t)||sigsetsize>sizeof(sigset_t2)))return -EINVAL;
    if(nbytes>sizeof(sigset_t2))nbytes=sizeof(sigset_t2);
    if(oldset)c3_memcpy(oldset,&cur->sig_blocked,nbytes);
    if(set){
        sigset_t2 s;
        c3_memset(&s,0,sizeof(s));
        c3_memcpy(&s,set,nbytes);
        c3_sigset_drop_unblockable(&s);
        switch(how){
            case 0:/*SIG_BLOCK*/ cur->sig_blocked.sig[0]|=s.sig[0];cur->sig_blocked.sig[1]|=s.sig[1];break;
            case 1:/*SIG_UNBLOCK*/ cur->sig_blocked.sig[0]&=~s.sig[0];cur->sig_blocked.sig[1]&=~s.sig[1];break;
            case 2:/*SIG_SETMASK*/ cur->sig_blocked.sig[0]=s.sig[0];cur->sig_blocked.sig[1]=s.sig[1];break;
            default: return -EINVAL;
        }
    }
    sig_check_pending(cur);
    return 0;
}

int64_t real_sys_rt_sigreturn(void){
    task_t *cur=task_current();
    if(!cur)return -ESRCH;
    if(cur->sig_in_handler){
        cur->sig_blocked=cur->sig_saved_mask;
        cur->sig_in_handler=false;
    }
    sig_check_pending(cur);
    return 0;
}
int64_t real_sys_sigaltstack(const void *ss,void *oss){(void)ss;(void)oss;return 0;}

static int c3_phys_ref_index(uint64_t phys){
    uint64_t idx;
    if(!phys)return -1;
    phys&=~0xFFFULL;
    if(phys<C3_PMM_BASE)return -1;
    idx=(phys-C3_PMM_BASE)/PAGE_SIZE;
    if(idx>=(uint64_t)PMM_MAX_PAGES)return -1;
    return (int)idx;
}

static void c3_phys_ref_set_initial(uint64_t phys){
    int idx=c3_phys_ref_index(phys);
    if(idx<0)return;
    if(g_c3_phys_refcnt[idx]==0)g_c3_phys_refcnt[idx]=1;
}

static void c3_phys_ref_retain(uint64_t phys){
    int idx=c3_phys_ref_index(phys);
    if(idx<0)return;
    if(g_c3_phys_refcnt[idx]==0)g_c3_phys_refcnt[idx]=1;
    else if(g_c3_phys_refcnt[idx]<0xFFFFu)++g_c3_phys_refcnt[idx];
}

static void c3_phys_ref_release(uint64_t phys){
    int idx=c3_phys_ref_index(phys);
    static uint32_t untracked_release_warns=0;
    static uint32_t quarantine_warns=0;
    phys&=~0xFFFULL;
    if(!phys)return;
    if(idx<0){
        return;
    }
    if(g_c3_phys_refcnt[idx]==0){
        if(untracked_release_warns<32){
            ++untracked_release_warns;
            __boot_serial_puts("[phys-ref] ignored untracked release phys=");
            __boot_serial_puthex64(phys);
            __boot_serial_puts("\n");
        }
        return;
    }
    if(g_c3_phys_refcnt[idx]>1){
        --g_c3_phys_refcnt[idx];
        return;
    }
    g_c3_phys_refcnt[idx]=0;
    c3_file_page_invalidate_phys(phys);
    /* Browser bring-up safety: do not recycle user frames immediately.
     * A few Linux-ABI paths still touch inherited identity/ELF mappings
     * through coarse metadata; reusing a mistakenly released executable
     * page turns it into a zero-filled anonymous page and crashes inside
     * valid code. Quarantine until mmap ownership is exact. */
    if(quarantine_warns<16){
        ++quarantine_warns;
        __boot_serial_puts("[phys-ref] quarantined released frame phys=");
        __boot_serial_puthex64(phys);
        __boot_serial_puts("\n");
    }
}

/* Memory syscalls (real paging) */
#define MMAP_REGION_MAX 4096
typedef struct {
    bool      used;
    address_space_t *as;
    uint64_t  virt;
    uint64_t  size;
    int       prot;
    int       flags;
    int       fd;
    uint64_t  offset;
    uint8_t   backing_kind;
    char      backing_path[VFS_PATH_MAX];
} mmap_entry_t;
static mmap_entry_t g_mmap_table[MMAP_REGION_MAX];
static uint32_t g_c3_shared_anon_seq=1;

static void c3_shared_anon_make_path(char *out,size_t cap){
    size_t l=0;
    uint32_t seq=g_c3_shared_anon_seq++;
    if(!out||!cap)return;
    if(seq==0)seq=g_c3_shared_anon_seq++;
    out[0]=0;
    c3_append_str(out,&l,cap,C3_MEMFD_PREFIX);
    c3_append_str(out,&l,cap,".shanon-");
    c3_append_u32(out,&l,cap,seq);
}

static void c3_exec_clear_mmap_for_as(address_space_t *as){
    int i;
    if(!as)return;
    if(as==paging_get_kernel_space())return;
    for(i=0;i<MMAP_REGION_MAX;++i){
        if(!g_mmap_table[i].used)continue;
        if(g_mmap_table[i].as!=as)continue;
        g_mmap_table[i].used=false;
    }
}

/* Linux-ish mmap/prot flags used by userland runtimes */
#define C3_PROT_READ       0x1
#define C3_PROT_WRITE      0x2
#define C3_PROT_EXEC       0x4
#define C3_MAP_SHARED      0x01
#define C3_MAP_PRIVATE     0x02
#define C3_MAP_FIXED       0x10
#define C3_MAP_ANONYMOUS   0x20
#define C3_MAP_FIXED_NOREPLACE 0x100000
#define C3_MAP_ANON        C3_MAP_ANONYMOUS
#define C3_MREMAP_MAYMOVE  0x1
#define C3_MREMAP_FIXED    0x2
#define C3_MREMAP_DONTUNMAP 0x4
#define C3_MADV_NORMAL     0
#define C3_MADV_RANDOM     1
#define C3_MADV_SEQUENTIAL 2
#define C3_MADV_WILLNEED   3
#define C3_MADV_DONTNEED   4
#define C3_MADV_FREE       8
#define C3_MADV_REMOVE     9
#define C3_MADV_DONTFORK   10
#define C3_MADV_DOFORK     11
#define C3_MADV_MERGEABLE  12
#define C3_MADV_UNMERGEABLE 13
#define C3_MADV_HUGEPAGE   14
#define C3_MADV_NOHUGEPAGE 15
#define C3_MADV_DONTDUMP   16
#define C3_MADV_DODUMP     17
#define C3_MADV_WIPEONFORK 18
#define C3_MADV_KEEPONFORK 19
#define C3_MADV_COLD       20
#define C3_MADV_PAGEOUT    21

static bool c3_mmap_is_anon(int flags){return (flags&C3_MAP_ANONYMOUS)!=0;}
static bool c3_mmap_is_private(int flags){return (flags&C3_MAP_PRIVATE)!=0;}
static bool c3_mmap_is_shared(int flags){return (flags&C3_MAP_SHARED)!=0;}
static bool c3_mmap_is_lazy_anon(const mmap_entry_t *m){
    return m&&m->used&&m->fd<0&&c3_mmap_is_anon(m->flags);
}

static bool c3_mmap_fd_perms_ok(task_t *cur,int fd,int prot,int flags){
    uint16_t f;
    (void)prot;
    if(!cur||fd<0||!fd_valid(&cur->fdt,fd))return false;
    f=cur->fdt.fds[fd].flags;
    if(!(f&FDFL_READABLE))return false;
    if((prot&C3_PROT_WRITE)&&c3_mmap_is_shared(flags)&&!(f&FDFL_WRITABLE))return false;
    return true;
}

static void c3_mmap_perm_text(int prot,int flags,char out[5]){
    out[0]=(prot&C3_PROT_READ)?'r':'-';
    out[1]=(prot&C3_PROT_WRITE)?'w':'-';
    out[2]=(prot&C3_PROT_EXEC)?'x':'-';
    out[3]=c3_mmap_is_private(flags)?'p':'s';
    out[4]=0;
}

static int c3_mmap_alloc_slot(void){
    int i;
    for(i=0;i<MMAP_REGION_MAX;++i)if(!g_mmap_table[i].used)return i;
    return -1;
}
static int c3_mmap_alloc_slot_except(int skip){
    int i;
    for(i=0;i<MMAP_REGION_MAX;++i)if(i!=skip&&!g_mmap_table[i].used)return i;
    return -1;
}

static uint64_t c3_mmap_pte_flags_from_prot(int prot){
    uint64_t pflags=PAGE_PRESENT;
    if(prot&(C3_PROT_READ|C3_PROT_WRITE|C3_PROT_EXEC))pflags|=PAGE_USER;
    if(prot&C3_PROT_WRITE)pflags|=PAGE_WRITABLE;
    if(!(prot&C3_PROT_EXEC))pflags|=PAGE_NX;
    return pflags;
}

static bool c3_mmap_overlap_as(address_space_t *as,uint64_t s,uint64_t e,int skip_idx){
    int i;
    for(i=0;i<MMAP_REGION_MAX;++i){
        uint64_t rs,re;
        if(i==skip_idx||!g_mmap_table[i].used)continue;
        if(g_mmap_table[i].as!=as)continue;
        rs=g_mmap_table[i].virt;
        re=rs+g_mmap_table[i].size;
        if(!(e<=rs||s>=re))return true;
    }
    return false;
}

static bool c3_mmap_pages_present(address_space_t *as,uint64_t s,uint64_t e){
    uint64_t pg;
    if(!as||e<s)return true;
    s&=~(PAGE_SIZE-1);
    e=(e+PAGE_SIZE-1)&~(PAGE_SIZE-1);
    for(pg=s;pg<e;pg+=PAGE_SIZE){
        uint64_t entry=paging_get_entry(as,pg);
        if((entry&PAGE_PRESENT)&&(entry&PAGE_USER))return true;
    }
    return false;
}

static bool c3_mmap_next_gap_after_overlap(address_space_t *as,uint64_t s,uint64_t e,int skip_idx,uint64_t *next){
    int i;
    bool hit=false;
    uint64_t n=s+PAGE_SIZE;
    for(i=0;i<MMAP_REGION_MAX;++i){
        uint64_t rs,re,candidate;
        if(i==skip_idx||!g_mmap_table[i].used)continue;
        if(g_mmap_table[i].as!=as)continue;
        rs=g_mmap_table[i].virt;
        re=rs+g_mmap_table[i].size;
        if(e<=rs||s>=re)continue;
        candidate=(re+PAGE_SIZE-1)&~(PAGE_SIZE-1);
        if(candidate>n)n=candidate;
        hit=true;
    }
    if(next)*next=n;
    return hit;
}

static int c3_mmap_region_find(address_space_t *as,uint64_t addr){
    int i;
    int best=-1;
    uint64_t best_size=~0ULL;
    for(i=0;i<MMAP_REGION_MAX;++i){
        uint64_t s,e;
        if(!g_mmap_table[i].used||g_mmap_table[i].as!=as)continue;
        s=g_mmap_table[i].virt;
        e=s+g_mmap_table[i].size;
        if(addr>=s&&addr<e){
            if(g_mmap_table[i].size<best_size||
               (g_mmap_table[i].size==best_size&&i>best)){
                best=i;
                best_size=g_mmap_table[i].size;
            }
        }
    }
    return best;
}

static void c3_trace_user_addr_mapping(const task_t *cur,uint64_t addr,const char *tag){
    static uint32_t trace_count=0;
    int idx;
    uint64_t off;
    char perms[5];
    if(trace_count>=160)return;
    if(!cur||!cur->addr_space)return;
    if(addr<0x40000000ULL||addr>=C3_USER_TOP)return;
    idx=c3_mmap_region_find(cur->addr_space,addr);
    if(idx<0)return;
    ++trace_count;
    off=g_mmap_table[idx].offset+(addr-g_mmap_table[idx].virt);
    c3_mmap_perm_text(g_mmap_table[idx].prot,g_mmap_table[idx].flags,perms);
    __boot_serial_puts("[addr-map] ");
    __boot_serial_puts(tag?tag:"addr");
    __boot_serial_puts(" pid=");
    __boot_serial_putu32((uint32_t)cur->pid);
    c3_trace_task_name(cur);
    __boot_serial_puts(" addr=");
    __boot_serial_puthex64(addr);
    __boot_serial_puts(" base=");
    __boot_serial_puthex64(g_mmap_table[idx].virt);
    __boot_serial_puts(" off=");
    __boot_serial_puthex64(off);
    __boot_serial_puts(" prot=");
    __boot_serial_puts(perms);
    __boot_serial_puts(" path=");
    if(g_mmap_table[idx].backing_path[0])__boot_serial_puts(g_mmap_table[idx].backing_path);
    else if(g_mmap_table[idx].fd>=0)__boot_serial_puts("[fd]");
    else __boot_serial_puts("[anon]");
    __boot_serial_puts("\n");
}

static int c3_mmap_pick_addr(task_t *cur,uint64_t hint,uint64_t len,int flags,uint64_t *out){
    uint64_t base,try_addr;
    int guard=0;
    if(!cur||!out||!len)return -EINVAL;
    if(len>=C3_USER_TOP)return -ENOMEM;
    if(flags&(C3_MAP_FIXED|C3_MAP_FIXED_NOREPLACE)){
        uint64_t fixed=hint&~(PAGE_SIZE-1);
        if(fixed>=C3_USER_TOP||len>C3_USER_TOP-fixed)return -ENOMEM;
        *out=fixed;
        return 0;
    }
    base=hint? (hint&~(PAGE_SIZE-1)) : ((cur->mmap_base+PAGE_SIZE-1)&~(PAGE_SIZE-1));
    if(base<0x40000000ULL||base>=C3_USER_TOP||len>C3_USER_TOP-base)base=0x40000000ULL;
    try_addr=base;
retry_search:
    while(guard<8192){
        uint64_t next=try_addr+PAGE_SIZE;
        uint64_t end;
        if(try_addr>=C3_USER_TOP||len>C3_USER_TOP-try_addr)break;
        end=try_addr+len;
        if(!c3_mmap_overlap_as(cur->addr_space,try_addr,end,-1)&&
           !c3_mmap_pages_present(cur->addr_space,try_addr,end)){
            *out=try_addr;
            if(end+PAGE_SIZE>end&&end+PAGE_SIZE<C3_USER_TOP)end+=PAGE_SIZE;
            if(end>cur->mmap_base&&end<C3_USER_TOP)cur->mmap_base=end;
            return 0;
        }
        if(c3_mmap_next_gap_after_overlap(cur->addr_space,try_addr,end,-1,&next)&&next>try_addr&&next<C3_USER_TOP)
            try_addr=next;
        else
            try_addr+=PAGE_SIZE;
        ++guard;
    }
    if(hint){
        hint=0;
        guard=0;
        try_addr=(cur->mmap_base+PAGE_SIZE-1)&~(PAGE_SIZE-1);
        if(try_addr<0x40000000ULL||try_addr>=C3_USER_TOP||len>C3_USER_TOP-try_addr)try_addr=0x40000000ULL;
        goto retry_search;
    }
    return -ENOMEM;
}

static int c3_mmap_map_pages(address_space_t *as,uint64_t va,uint64_t len,uint64_t pflags,bool zero_pages){
    uint64_t pg;
    for(pg=0;pg<len;pg+=PAGE_SIZE){
        uint64_t frame;
        uint64_t entry=paging_get_entry(as,va+pg);
        if((entry&PAGE_PRESENT)&&(entry&PAGE_USER)){
            uint64_t rb;
            for(rb=0;rb<pg;rb+=PAGE_SIZE){
                uint64_t phys=paging_translate(as,va+rb);
                paging_unmap(as,va+rb);
                if(phys)c3_phys_ref_release(phys&~0xFFFULL);
            }
            return -EEXIST;
        }
        frame=pmm_alloc_frame();
        if(!frame){
            uint64_t rb;
            for(rb=0;rb<pg;rb+=PAGE_SIZE){
                uint64_t phys=paging_translate(as,va+rb);
                paging_unmap(as,va+rb);
                if(phys)c3_phys_ref_release(phys&~0xFFFULL);
            }
            return -ENOMEM;
        }
        c3_phys_ref_set_initial(frame);
        if(zero_pages){
            /* Zero the physical frame before insertion in user VA space.
             * This avoids touching freshly-mapped user pages one-by-one,
             * which becomes very expensive for large browser reservations. */
            c3_memset((void*)PHYS_TO_DMAP(frame),0,PAGE_SIZE);
        }
        if(!paging_map(as,va+pg,frame,pflags)){
            uint64_t rb;
            c3_phys_ref_release(frame);
            for(rb=0;rb<pg;rb+=PAGE_SIZE){
                uint64_t phys=paging_translate(as,va+rb);
                paging_unmap(as,va+rb);
                if(phys)c3_phys_ref_release(phys&~0xFFFULL);
            }
            return -ENOMEM;
        }
    }
    return 0;
}

static void c3_mmap_unmap_pages(address_space_t *as,uint64_t va,uint64_t len,bool free_phys){
    uint64_t pg;
    for(pg=0;pg<len;pg+=PAGE_SIZE){
        uint64_t entry=paging_get_entry(as,va+pg);
        uint64_t phys;
        if(!(entry&PAGE_PRESENT))continue;
        if(!(entry&PAGE_USER))continue;
        if(entry&PAGE_PS)continue;
        phys=entry&C3_PTE_ADDR_MASK;
        if(!paging_unmap(as,va+pg))continue;
        if(free_phys&&phys)c3_phys_ref_release(phys);
    }
}

static int c3_mmap_file_copy(task_t *cur,uint64_t va,uint64_t len,int fd,uint64_t off){
    const uint8_t *data=0;
    uint32_t size=0;
    uint64_t to_copy=0;
    uint8_t kind;
    if(!cur||fd<0||!fd_valid(&cur->fdt,fd))return -EBADF;
    kind=cur->fdt.fds[fd].kind;
    if(kind==FDKIND_VFSFILE){
        const char *vpath=c3_vfs_open_slot_path(cur->fdt.fds[fd].ref);
        uint64_t memfd_size=0;
        if(vpath&&c3_memfd_path_get_size(vpath,&memfd_size)){
            static uint32_t trace_count=0;
            if(off>=memfd_size)return 0;
            to_copy=len;
            if(to_copy>memfd_size-off)to_copy=memfd_size-off;
            c3_memfd_sparse_read(vpath,off,(void*)(uintptr_t)va,(size_t)to_copy);
            if(c3_task_is_firefox_runtime(cur)&&trace_count<16){
                ++trace_count;
                __boot_serial_puts("[mmap-memfd-copy] pid=");
                __boot_serial_putu32((uint32_t)cur->pid);
                c3_trace_task_name(cur);
                __boot_serial_puts(" fd=");
                __boot_serial_putu32((uint32_t)fd);
                __boot_serial_puts(" va=");
                __boot_serial_puthex64(va);
                __boot_serial_puts(" len=");
                __boot_serial_puthex64(len);
                __boot_serial_puts(" off=");
                __boot_serial_puthex64(off);
                __boot_serial_puts(" copied=");
                __boot_serial_puthex64(to_copy);
                __boot_serial_puts(" size=");
                __boot_serial_puthex64(memfd_size);
                __boot_serial_puts(" path=");
                __boot_serial_puts(vpath);
                __boot_serial_puts("\n");
            }
            return 0;
        }
        if(!vpath||!kvfs_read(vpath,&data,&size)){
            if(vpath&&(c3_starts_with(vpath,"/tmp/.ridux-deleted-")||c3_starts_with(vpath,C3_MEMFD_PREFIX))){
                return 0;
            }
            return -ENOENT;
        }
        if(off>=size)return 0;
        to_copy=len;
        if(to_copy>(uint64_t)(size-off))to_copy=(uint64_t)(size-off);
        c3_memcpy((void*)(uintptr_t)va,data+off,(size_t)to_copy);
        return 0;
    }
    if(kind==FDKIND_PROC){
        int ref=cur->fdt.fds[fd].ref;
        if(ref>=0&&ref<PROC_BUF_MAX&&g_proc_bufs[ref].used){
            size=g_proc_bufs[ref].size;
            if(off>=size)return 0;
            to_copy=len;
            if(to_copy>(uint64_t)(size-off))to_copy=(uint64_t)(size-off);
            c3_memcpy((void*)(uintptr_t)va,g_proc_bufs[ref].data+off,(size_t)to_copy);
            return 0;
        }
    }
    if(kind==FDKIND_DEVZERO||kind==FDKIND_DEVNULL)return 0;
    if(kind==FDKIND_DEVFB)return 0;
    if(kind==FDKIND_SOCKET||kind==FDKIND_PIPE_R||kind==FDKIND_PIPE_W||
       kind==FDKIND_EVENTFD||kind==FDKIND_TIMERFD||kind==FDKIND_SIGNALFD||
       kind==FDKIND_INOTIFY||kind==FDKIND_EPOLL||kind==FDKIND_DIR||
       kind==FDKIND_DEVTTY||kind==FDKIND_DEVRANDOM){
        static uint32_t nonfile_trace=0;
        if(nonfile_trace<32){
            ++nonfile_trace;
            __boot_serial_puts("[mmap-nonfile-zero] pid=");
            __boot_serial_putu32((uint32_t)cur->pid);
            c3_trace_task_name(cur);
            __boot_serial_puts(" fd=");
            __boot_serial_putu32((uint32_t)fd);
            __boot_serial_puts(" kind=");
            __boot_serial_putu32((uint32_t)kind);
            __boot_serial_puts(" ref=");
            __boot_serial_putu32((uint32_t)cur->fdt.fds[fd].ref);
            __boot_serial_puts(" len=");
            __boot_serial_puthex64(len);
            __boot_serial_puts("\n");
        }
        return 0;
    }
    return -ENODEV;
}

static void c3_file_page_invalidate_phys(uint64_t phys){
    int i;
    phys&=~0xFFFULL;
    for(i=0;i<C3_FILEPAGE_MAX;++i){
        if(!g_c3_file_pages[i].used)continue;
        if((g_c3_file_pages[i].phys&~0xFFFULL)!=phys)continue;
        {
            int ridx=c3_phys_ref_index(g_c3_file_pages[i].phys);
            if(ridx>=0&&g_c3_phys_refcnt[ridx]>0)
                c3_phys_ref_release(g_c3_file_pages[i].phys&~0xFFFULL);
        }
        g_c3_file_pages[i].used=false;
        g_c3_file_pages[i].path[0]=0;
        g_c3_file_pages[i].page_off=0;
        g_c3_file_pages[i].phys=0;
    }
}

static void c3_file_page_invalidate_path(const char *path){
    int i;
    if(!path||!*path)return;
    for(i=0;i<C3_FILEPAGE_MAX;++i){
        if(!g_c3_file_pages[i].used)continue;
        if(c3_strcmp(g_c3_file_pages[i].path,path)!=0)continue;
        {
            int ridx=c3_phys_ref_index(g_c3_file_pages[i].phys);
            if(ridx>=0&&g_c3_phys_refcnt[ridx]>0)
                c3_phys_ref_release(g_c3_file_pages[i].phys&~0xFFFULL);
        }
        g_c3_file_pages[i].used=false;
        g_c3_file_pages[i].path[0]=0;
        g_c3_file_pages[i].page_off=0;
        g_c3_file_pages[i].phys=0;
    }
}

static int c3_file_page_find_slot(const char *path,uint64_t page_off){
    int i;
    if(!path||!*path)return -1;
    for(i=0;i<C3_FILEPAGE_MAX;++i){
        if(!g_c3_file_pages[i].used)continue;
        if(g_c3_file_pages[i].page_off!=page_off)continue;
        if(c3_strcmp(g_c3_file_pages[i].path,path)!=0)continue;
        return i;
    }
    return -1;
}

static int c3_file_page_alloc_slot(void){
    int i;
    for(i=0;i<C3_FILEPAGE_MAX;++i){
        if(!g_c3_file_pages[i].used)return i;
    }
    return -1;
}

static uint64_t c3_file_page_get_frame(const char *path,const uint8_t *file_data,uint32_t file_size,uint64_t page_off){
    int slot=c3_file_page_find_slot(path,page_off);
    uint64_t frame;
    if(slot>=0){
        frame=g_c3_file_pages[slot].phys&~0xFFFULL;
        if(frame){
            c3_phys_ref_retain(frame);
            ++g_c3_cow_shared_maps;
            return frame;
        }
        g_c3_file_pages[slot].used=false;
    }
    frame=pmm_alloc_frame();
    if(!frame)return 0;
    c3_phys_ref_set_initial(frame);
    c3_memset((void*)PHYS_TO_DMAP(frame),0,PAGE_SIZE);
    if(file_data&&page_off<(uint64_t)file_size){
        uint64_t remain=(uint64_t)file_size-page_off;
        uint64_t to_copy=(remain<PAGE_SIZE)?remain:PAGE_SIZE;
        c3_memcpy((void*)PHYS_TO_DMAP(frame),file_data+page_off,(size_t)to_copy);
    }
    slot=c3_file_page_alloc_slot();
    if(slot>=0){
        g_c3_file_pages[slot].used=true;
        g_c3_file_pages[slot].page_off=page_off;
        g_c3_file_pages[slot].phys=frame;
        c3_strlcpy(g_c3_file_pages[slot].path,path,sizeof(g_c3_file_pages[slot].path));
        c3_phys_ref_retain(frame);
    }
    return frame;
}

static int c3_mmap_map_private_file(task_t *cur,uint64_t va,uint64_t len,int prot,int fd,uint64_t off){
    const uint8_t *data=0;
    uint32_t size=0;
    const char *memfd_path=0;
    uint64_t memfd_size=0;
    uint8_t kind;
    uint64_t pflags;
    uint64_t pg;
    if(!cur||fd<0||!fd_valid(&cur->fdt,fd))return -EBADF;
    kind=cur->fdt.fds[fd].kind;
    if(kind==FDKIND_VFSFILE){
        const char *vpath=c3_vfs_open_slot_path(cur->fdt.fds[fd].ref);
        if(vpath&&c3_memfd_path_get_size(vpath,&memfd_size)){
            memfd_path=vpath;
        }else if(!vpath||!kvfs_read(vpath,&data,&size)){
            if(!(vpath&&(c3_starts_with(vpath,"/tmp/.ridux-deleted-")||
                         c3_starts_with(vpath,C3_MEMFD_PREFIX))))return -ENOENT;
            data=0;
            size=0;
        }
    }else if(kind==FDKIND_PROC){
        int ref=cur->fdt.fds[fd].ref;
        if(ref>=0&&ref<PROC_BUF_MAX&&g_proc_bufs[ref].used){
            data=g_proc_bufs[ref].data;
            size=g_proc_bufs[ref].size;
        }
    }else if(kind==FDKIND_DEVZERO||kind==FDKIND_DEVNULL||kind==FDKIND_DEVFB||
             kind==FDKIND_SOCKET||kind==FDKIND_PIPE_R||kind==FDKIND_PIPE_W||
             kind==FDKIND_EVENTFD||kind==FDKIND_TIMERFD||kind==FDKIND_SIGNALFD||
             kind==FDKIND_INOTIFY||kind==FDKIND_EPOLL||kind==FDKIND_DIR||
             kind==FDKIND_DEVTTY||kind==FDKIND_DEVRANDOM){
        data=0;
        size=0;
    }else{
        return -ENODEV;
    }

    /* Keep MAP_PRIVATE mappings physically private and populate each frame
     * through the kernel DMAP before installing it with final RX/RO flags.
     * Writing bytes through the final user VA is fragile for browser-sized
     * DSOs: a later CR0.WP or permission repair path can leave executable
     * pages mapped but still zero-filled, which crashes inside libxul text. */
    pflags=c3_mmap_pte_flags_from_prot(prot);
    for(pg=0;pg<len;pg+=PAGE_SIZE){
        uint64_t entry=paging_get_entry(cur->addr_space,va+pg);
        uint64_t frame;
        if((entry&PAGE_PRESENT)&&(entry&PAGE_USER)){
            c3_mmap_unmap_pages(cur->addr_space,va,pg,true);
            return -EEXIST;
        }
        frame=pmm_alloc_frame();
        if(!frame){
            c3_mmap_unmap_pages(cur->addr_space,va,pg,true);
            return -ENOMEM;
        }
        c3_phys_ref_set_initial(frame);
        c3_memset((void*)PHYS_TO_DMAP(frame),0,PAGE_SIZE);
        if(memfd_path&&off<=UINT64_MAX-pg&&off+pg<memfd_size){
            uint64_t remain=memfd_size-(off+pg);
            uint64_t to_copy=(remain<PAGE_SIZE)?remain:PAGE_SIZE;
            c3_memfd_sparse_read(memfd_path,off+pg,(void*)PHYS_TO_DMAP(frame),(size_t)to_copy);
        }else if(data&&off+pg<(uint64_t)size){
            uint64_t remain=(uint64_t)size-(off+pg);
            uint64_t to_copy=(remain<PAGE_SIZE)?remain:PAGE_SIZE;
            c3_memcpy((void*)PHYS_TO_DMAP(frame),data+off+pg,(size_t)to_copy);
        }
        if(!paging_map(cur->addr_space,va+pg,frame,pflags)){
            c3_phys_ref_release(frame);
            c3_mmap_unmap_pages(cur->addr_space,va,pg,true);
            return -ENOMEM;
        }
    }
    if(memfd_path&&c3_task_is_firefox_runtime(cur)){
        static uint32_t trace_count=0;
        if(trace_count<32){
            ++trace_count;
            __boot_serial_puts("[mmap-private-memfd] pid=");
            __boot_serial_putu32((uint32_t)cur->pid);
            c3_trace_task_name(cur);
            __boot_serial_puts(" fd=");
            __boot_serial_putu32((uint32_t)fd);
            __boot_serial_puts(" va=");
            __boot_serial_puthex64(va);
            __boot_serial_puts(" len=");
            __boot_serial_puthex64(len);
            __boot_serial_puts(" off=");
            __boot_serial_puthex64(off);
            __boot_serial_puts(" size=");
            __boot_serial_puthex64(memfd_size);
            __boot_serial_puts(" path=");
            __boot_serial_puts(memfd_path);
            __boot_serial_puts("\n");
        }
    }
    return 0;
}

static int c3_mmap_map_shared_vfs_file(task_t *cur,uint64_t va,uint64_t len,int prot,int fd,uint64_t off){
    const uint8_t *data=0;
    uint32_t size=0;
    const char *path;
    uint64_t pg;
    uint64_t pflags;
    if(!cur||fd<0||!fd_valid(&cur->fdt,fd))return -EBADF;
    if(cur->fdt.fds[fd].kind!=FDKIND_VFSFILE)return -ENODEV;
    path=c3_vfs_open_slot_path(cur->fdt.fds[fd].ref);
    if(!path||!*path)return -ENOENT;
    if(!kvfs_read(path,&data,&size)){
        if(c3_starts_with(path,"/tmp/.ridux-deleted-")||c3_starts_with(path,C3_MEMFD_PREFIX)){
            data=0;
            size=0;
        }else{
            return -ENOENT;
        }
    }
    pflags=c3_mmap_pte_flags_from_prot(prot);
    for(pg=0;pg<len;pg+=PAGE_SIZE){
        uint64_t entry=paging_get_entry(cur->addr_space,va+pg);
        uint64_t frame;
        if((entry&PAGE_PRESENT)&&(entry&PAGE_USER)){
            static uint32_t collision_trace=0;
            c3_mmap_unmap_pages(cur->addr_space,va,pg,true);
            if(collision_trace<32){
                ++collision_trace;
                __boot_serial_puts("[mmap-shared-vfs-collide] pid=");
                __boot_serial_putu32((uint32_t)cur->pid);
                c3_trace_task_name(cur);
                __boot_serial_puts(" va=");
                __boot_serial_puthex64(va+pg);
                __boot_serial_puts(" entry=");
                __boot_serial_puthex64(entry);
                __boot_serial_puts(" fd=");
                __boot_serial_putu32((uint32_t)fd);
                __boot_serial_puts(" path=");
                __boot_serial_puts(path);
                __boot_serial_puts("\n");
            }
            return -EEXIST;
        }
        frame=c3_file_page_get_frame(path,data,size,off+pg);
        if(!frame){
            c3_mmap_unmap_pages(cur->addr_space,va,pg,true);
            return -ENOMEM;
        }
        if(!paging_map(cur->addr_space,va+pg,frame,pflags)){
            c3_phys_ref_release(frame);
            c3_mmap_unmap_pages(cur->addr_space,va,pg,true);
            return -ENOMEM;
        }
    }
    {
        static uint32_t trace_count=0;
        if(trace_count<8){
            ++trace_count;
            __boot_serial_puts("[mmap-shared-vfs] pid=");
            __boot_serial_putu32((uint32_t)cur->pid);
            c3_trace_task_name(cur);
            __boot_serial_puts(" fd=");
            __boot_serial_putu32((uint32_t)fd);
            __boot_serial_puts(" va=");
            __boot_serial_puthex64(va);
            __boot_serial_puts(" len=");
            __boot_serial_puthex64(len);
            __boot_serial_puts(" off=");
            __boot_serial_puthex64(off);
            __boot_serial_puts(" path=");
            __boot_serial_puts(path);
            __boot_serial_puts("\n");
        }
    }
    return 0;
}

static int c3_mmap_writeback_vfs_range(task_t *cur,const mmap_entry_t *m,uint64_t va,uint64_t len){
    const uint8_t *old_data=0;
    uint32_t old_size=0;
    uint8_t *out=g_c3_vfs_file_scratch;
    uint64_t rel,file_start,file_end;
    uint32_t want;
    uint32_t i;
    if(!cur||!m||!m->used||!m->backing_path[0])return 0;
    if(!c3_mmap_is_shared(m->flags)||!(m->prot&C3_PROT_WRITE))return 0;
    if(m->backing_kind!=FDKIND_VFSFILE)return 0;
    if(c3_starts_with(m->backing_path,C3_MEMFD_PREFIX))return 0;
    if(va<m->virt)return -EINVAL;
    rel=va-m->virt;
    file_start=m->offset+rel;
    file_end=file_start+len;
    if(file_end<file_start)return -EINVAL;
    if(file_start>=C3_VFS_FILE_MAX-1)return 0;
    if(file_end>C3_VFS_FILE_MAX-1)file_end=C3_VFS_FILE_MAX-1;
    want=(uint32_t)file_end;
    if(kvfs_read(m->backing_path,&old_data,&old_size)&&old_data){
        uint32_t copy=old_size;
        if(copy>want)copy=want;
        for(i=0;i<copy;++i)out[i]=old_data[i];
        for(;i<want;++i)out[i]=0;
    }else{
        for(i=0;i<want;++i)out[i]=0;
    }
    while(va<m->virt+m->size&&file_start<file_end&&len>0){
        uint64_t pg=va&~(PAGE_SIZE-1);
        uint64_t in_pg=va-pg;
        uint64_t chunk=PAGE_SIZE-in_pg;
        if(chunk>len)chunk=len;
        if(file_start+chunk>file_end)chunk=file_end-file_start;
        if((paging_get_entry(cur->addr_space,pg)&PAGE_PRESENT)!=0){
            c3_memcpy(out+(uint32_t)file_start,(const void*)(uintptr_t)va,(size_t)chunk);
        }
        va+=chunk;
        len-=chunk;
        file_start+=chunk;
    }
    c3_file_page_invalidate_path(m->backing_path);
    if(!kvfs_write_bytes(m->backing_path,out,want))return -EIO;
    return 0;
}

int64_t real_sys_mmap(uint64_t addr,uint64_t len,int prot,int flags,int fd,uint64_t off){
    task_t *cur=task_current();
    uint64_t aligned_len;
    uint64_t vaddr=0;
    bool has_overlap;
    bool is_fixed;
    bool is_anon;
    int slot;
    int r;
    int retry_nohint=0;
    uint64_t pick_hint;
    const int known_prot=C3_PROT_READ|C3_PROT_WRITE|C3_PROT_EXEC;
#define C3_MMAP_FAIL(_stage) do{ \
        if(g_c3_mmap_fail_trace_count<64){ \
            ++g_c3_mmap_fail_trace_count; \
            __boot_serial_puts("[mmap-fail] stage="); \
            __boot_serial_puts(_stage); \
            __boot_serial_puts(" len="); \
            __boot_serial_puthex64(aligned_len); \
            __boot_serial_puts(" prot="); \
            __boot_serial_putu32((uint32_t)prot); \
            __boot_serial_puts(" flags="); \
            __boot_serial_puthex64((uint64_t)(uint32_t)flags); \
            __boot_serial_puts(" fd="); \
            __boot_serial_putu32((uint32_t)(fd>=0?fd:0xFFFFFFFFu)); \
            __boot_serial_puts(" off="); \
            __boot_serial_puthex64(off); \
            __boot_serial_puts(" base="); \
            __boot_serial_puthex64(cur?cur->mmap_base:0); \
            __boot_serial_puts(" free="); \
            __boot_serial_putu32(pmm_free_count()); \
            __boot_serial_puts("\n"); \
        } \
    }while(0)
    if(!cur||!cur->addr_space)return -ESRCH;
    if(!len)return -EINVAL;
    if(prot&~known_prot)return -EINVAL;
    if(len>C3_USER_TOP)return -ENOMEM;
    aligned_len=(len+PAGE_SIZE-1)&~(PAGE_SIZE-1);
    if(aligned_len<len||aligned_len>=C3_USER_TOP)return -ENOMEM;
    is_fixed=(flags&(C3_MAP_FIXED|C3_MAP_FIXED_NOREPLACE))!=0;
    is_anon=c3_mmap_is_anon(flags);
    if(!is_anon&&(off&(PAGE_SIZE-1)))return -EINVAL;
    if((flags&(C3_MAP_PRIVATE|C3_MAP_SHARED))==0)return -EINVAL;
    if(c3_mmap_is_private(flags)&&c3_mmap_is_shared(flags))return -EINVAL;
    if(!is_anon){
        if(fd<0||!fd_valid(&cur->fdt,fd))return -EBADF;
        if(!c3_mmap_fd_perms_ok(cur,fd,prot,flags))return -EACCES;
    }else{
        fd=-1;
    }
    if(addr&&(addr&(PAGE_SIZE-1))&&is_fixed)return -EINVAL;
retry_pick:
    pick_hint=retry_nohint?0:addr;
    if(c3_mmap_pick_addr(cur,pick_hint,aligned_len,flags,&vaddr)<0){
        C3_MMAP_FAIL("pick-addr");
        return -ENOMEM;
    }
    if(vaddr>=C3_USER_TOP||aligned_len>C3_USER_TOP-vaddr){
        C3_MMAP_FAIL("noncanonical");
        return -ENOMEM;
    }
    has_overlap=c3_mmap_overlap_as(cur->addr_space,vaddr,vaddr+aligned_len,-1)||
        c3_mmap_pages_present(cur->addr_space,vaddr,vaddr+aligned_len);
    if((flags&C3_MAP_FIXED_NOREPLACE)&&has_overlap)return -EEXIST;
    if((flags&C3_MAP_FIXED)&&has_overlap){
        /* MAP_FIXED replaces overlapping regions */
        real_sys_munmap(vaddr,aligned_len);
        c3_mmap_unmap_pages(cur->addr_space,vaddr,aligned_len,true);
    }else if(has_overlap){
        C3_MMAP_FAIL("overlap");
        return -ENOMEM;
    }
    if(is_anon){
        /* Linux reserves anonymous mmap ranges lazily. Browsers routinely
         * reserve very large PROT_NONE / no-reserve arenas; eagerly backing
         * those with physical pages stalls the kernel before userland can
         * render a window. compat3_handle_page_fault backs pages on demand. */
        r=0;
    }else if(c3_mmap_is_private(flags)){
        r=c3_mmap_map_private_file(cur,vaddr,aligned_len,prot,fd,off);
        if(r<0){
            C3_MMAP_FAIL("private-map");
            return r;
        }
    }else if(!is_anon&&cur->fdt.fds[fd].kind==FDKIND_VFSFILE){
        r=c3_mmap_map_shared_vfs_file(cur,vaddr,aligned_len,prot,fd,off);
        if(r<0){
            if(r==-EEXIST&&!is_fixed&&!retry_nohint){
                retry_nohint=1;
                goto retry_pick;
            }
            C3_MMAP_FAIL("shared-vfs-map");
            return r;
        }
    }else{
        r=c3_mmap_map_pages(cur->addr_space,vaddr,aligned_len,c3_mmap_pte_flags_from_prot(prot),true);
        if(r<0){
            C3_MMAP_FAIL("anon-map");
            return r;
        }
        if(!is_anon){
            r=c3_mmap_file_copy(cur,vaddr,aligned_len,fd,off);
            if(r<0){
                c3_mmap_unmap_pages(cur->addr_space,vaddr,aligned_len,true);
                C3_MMAP_FAIL("file-copy");
                return r;
            }
        }
    }
    slot=c3_mmap_alloc_slot();
    if(slot<0){
        c3_mmap_unmap_pages(cur->addr_space,vaddr,aligned_len,true);
        C3_MMAP_FAIL("alloc-slot");
        return -ENOMEM;
    }
    g_mmap_table[slot].used=true;
    g_mmap_table[slot].as=cur->addr_space;
    g_mmap_table[slot].virt=vaddr;
    g_mmap_table[slot].size=aligned_len;
    g_mmap_table[slot].prot=prot;
    g_mmap_table[slot].flags=flags;
    g_mmap_table[slot].fd=fd;
    g_mmap_table[slot].offset=off;
    g_mmap_table[slot].backing_kind=0;
    g_mmap_table[slot].backing_path[0]=0;
    if(is_anon&&c3_mmap_is_shared(flags)){
        c3_shared_anon_make_path(g_mmap_table[slot].backing_path,
                                 sizeof(g_mmap_table[slot].backing_path));
        {
            static uint32_t shanon_trace=0;
            if(shanon_trace<16){
                ++shanon_trace;
                __boot_serial_force_puts("[mmap-shanon!] pid=");
                __boot_serial_force_putu32((uint32_t)cur->pid);
                c3_force_task_name(cur);
                __boot_serial_force_puts(" va=");
                __boot_serial_force_puthex64(vaddr);
                __boot_serial_force_puts(" len=");
                __boot_serial_force_puthex64(aligned_len);
                __boot_serial_force_puts(" path=");
                __boot_serial_force_puts(g_mmap_table[slot].backing_path);
                __boot_serial_force_puts("\n");
            }
        }
    }else if(fd>=0&&fd_valid(&cur->fdt,fd)){
        g_mmap_table[slot].backing_kind=cur->fdt.fds[fd].kind;
        if(cur->fdt.fds[fd].kind==FDKIND_VFSFILE){
            const char *p=c3_vfs_open_slot_path(cur->fdt.fds[fd].ref);
            if(p)c3_strlcpy(g_mmap_table[slot].backing_path,p,sizeof(g_mmap_table[slot].backing_path));
        }else if(cur->fdt.fds[fd].kind==FDKIND_PROC){
            c3_strlcpy(g_mmap_table[slot].backing_path,"[proc]",sizeof(g_mmap_table[slot].backing_path));
        }else if(cur->fdt.fds[fd].kind==FDKIND_DEVZERO){
            c3_strlcpy(g_mmap_table[slot].backing_path,"/dev/zero",sizeof(g_mmap_table[slot].backing_path));
        }else if(cur->fdt.fds[fd].kind==FDKIND_DEVNULL){
            c3_strlcpy(g_mmap_table[slot].backing_path,"/dev/null",sizeof(g_mmap_table[slot].backing_path));
        }else if(cur->fdt.fds[fd].kind==FDKIND_DEVFB){
            c3_strlcpy(g_mmap_table[slot].backing_path,"/dev/dri/card0",sizeof(g_mmap_table[slot].backing_path));
        }
    }
    if(c3_task_is_chromium_runtime(cur)){
        static uint32_t chrome_mmap_trace=0;
        const char *bp=g_mmap_table[slot].backing_path;
        bool interesting_fd=(fd>=0&&fd<16)||fd==100;
        bool interesting_path=bp[0]&&(c3_has_token(bp,"/memfd/")||
                                      c3_has_token(bp,"snapshot")||
                                      c3_has_token(bp,"chromium")||
                                      c3_has_token(bp,"chrome"));
        if(chrome_mmap_trace<96&&(interesting_fd||interesting_path||c3_mmap_is_shared(flags))){
            ++chrome_mmap_trace;
            __boot_serial_force_puts("[mmap-chrome!] pid=");
            __boot_serial_force_putu32((uint32_t)cur->pid);
            c3_force_task_name(cur);
            __boot_serial_force_puts(" slot=");
            __boot_serial_force_putu32((uint32_t)slot);
            __boot_serial_force_puts(" va=");
            __boot_serial_force_puthex64(vaddr);
            __boot_serial_force_puts(" len=");
            __boot_serial_force_puthex64(aligned_len);
            __boot_serial_force_puts(" prot=");
            __boot_serial_force_putu32((uint32_t)prot);
            __boot_serial_force_puts(" flags=");
            __boot_serial_force_puthex64((uint64_t)(uint32_t)flags);
            __boot_serial_force_puts(" fd=");
            __boot_serial_force_putu32((uint32_t)(fd>=0?fd:0xFFFFFFFFu));
            __boot_serial_force_puts(" off=");
            __boot_serial_force_puthex64(off);
            if(bp[0]){
                __boot_serial_force_puts(" path=");
                __boot_serial_force_puts(bp);
            }
            __boot_serial_force_puts("\n");
        }
    }
    if(g_c3_mmap_trace_count<C3_MMAP_TRACE_MAX){
        ++g_c3_mmap_trace_count;
        __boot_serial_puts("[mmap] pid=");
        __boot_serial_putu32((uint32_t)cur->pid);
        c3_trace_task_name(cur);
        __boot_serial_puts(" slot=");
        __boot_serial_putu32((uint32_t)slot);
        __boot_serial_puts(" hint=");
        __boot_serial_puthex64(addr);
        __boot_serial_puts(" va=");
        __boot_serial_puthex64(vaddr);
        __boot_serial_puts(" len=");
        __boot_serial_puthex64(aligned_len);
        __boot_serial_puts(" prot=");
        __boot_serial_putu32((uint32_t)prot);
        __boot_serial_puts(" flags=");
        __boot_serial_puthex64((uint64_t)(uint32_t)flags);
        __boot_serial_puts(" fd=");
        __boot_serial_putu32((uint32_t)(fd>=0?fd:0xFFFFFFFFu));
        __boot_serial_puts(" off=");
        __boot_serial_puthex64(off);
        __boot_serial_puts(" as=");
        __boot_serial_puthex64((uint64_t)(uintptr_t)cur->addr_space);
        if(retry_nohint)__boot_serial_puts(" retry=1");
        if(g_mmap_table[slot].backing_path[0]){
            __boot_serial_puts(" path=");
            __boot_serial_puts(g_mmap_table[slot].backing_path);
        }
        if(is_anon)__boot_serial_puts(" lazy=1");
        __boot_serial_puts("\n");
    }
    return(int64_t)vaddr;
#undef C3_MMAP_FAIL
}

static bool c3_mmap_range_fully_covered(address_space_t *as,uint64_t us,uint64_t ue){
    uint64_t cur=us;
    while(cur<ue){
        int idx=c3_mmap_region_find(as,cur);
        uint64_t end;
        if(idx<0)return false;
        end=g_mmap_table[idx].virt+g_mmap_table[idx].size;
        if(end<=cur)return false;
        cur=(end<ue)?end:ue;
    }
    return true;
}

static bool c3_mmap_can_merge(const mmap_entry_t *a,const mmap_entry_t *b){
    if(!a||!b||!a->used||!b->used)return false;
    if(a->as!=b->as)return false;
    if(a->prot!=b->prot||a->flags!=b->flags)return false;
    if(a->fd!=b->fd||a->backing_kind!=b->backing_kind)return false;
    if(c3_strcmp(a->backing_path,b->backing_path)!=0)return false;
    return (a->virt+a->size)==b->virt&&(a->offset+a->size)==b->offset;
}

static void c3_mmap_merge_adjacent(address_space_t *as){
    int i,j;
    for(i=0;i<MMAP_REGION_MAX;++i){
        if(!g_mmap_table[i].used||g_mmap_table[i].as!=as)continue;
        for(j=0;j<MMAP_REGION_MAX;++j){
            if(i==j||!g_mmap_table[j].used||g_mmap_table[j].as!=as)continue;
            if(c3_mmap_can_merge(&g_mmap_table[i],&g_mmap_table[j])){
                g_mmap_table[i].size+=g_mmap_table[j].size;
                g_mmap_table[j].used=false;
            }
        }
    }
}

static int c3_mprotect_apply_page(address_space_t *as,uint64_t pg,uint64_t pf){
    uint64_t entry=paging_get_entry(as,pg);
    uint64_t phys;
    uint64_t keep;
    if(!(entry&PAGE_PRESENT))return -ENOENT;
    phys=entry&C3_PTE_ADDR_MASK;
    keep=entry&C3_PTE_KEEP_MASK;
    if((pf&PAGE_WRITABLE)&&(keep&C3_PAGE_SOFT_COW)){
        int ridx=c3_phys_ref_index(phys);
        if(ridx>=0&&g_c3_phys_refcnt[ridx]>1){
            uint64_t new_frame=pmm_alloc_frame();
            if(!new_frame)return -ENOMEM;
            c3_phys_ref_set_initial(new_frame);
            c3_memcpy(PHYS_TO_DMAP(new_frame),PHYS_TO_DMAP(phys),PAGE_SIZE);
            c3_phys_ref_release(phys);
            phys=new_frame;
            ++g_c3_cow_copies;
        }
        keep&=~C3_PAGE_SOFT_COW;
    }
    keep&=~(PAGE_WRITABLE|PAGE_NX|PAGE_USER);
    keep|=(pf&(PAGE_WRITABLE|PAGE_NX|PAGE_USER));
    keep|=PAGE_PRESENT;
    if(keep&PAGE_WRITABLE)keep&=~C3_PAGE_SOFT_COW;
    paging_set_entry(as,pg,phys|keep);
    return 0;
}

static void c3_pf_promote_user_parents(address_space_t *as,uint64_t virt);

static uint64_t c3_mmap_alloc_lazy_frame_for_page(const mmap_entry_t *m,uint64_t pg){
    uint64_t frame;
    if(m&&c3_mmap_is_shared(m->flags)&&m->backing_path[0]&&pg>=m->virt){
        uint64_t rel=pg-m->virt;
        return c3_file_page_get_frame(m->backing_path,0,0,m->offset+rel);
    }
    frame=pmm_alloc_frame();
    if(!frame)return 0;
    c3_phys_ref_set_initial(frame);
    c3_memset((void*)PHYS_TO_DMAP(frame),0,PAGE_SIZE);
    return frame;
}

static int c3_mmap_materialize_zero_page(address_space_t *as,uint64_t pg,int prot){
    uint64_t frame;
    uint64_t entry;
    int midx;
    mmap_entry_t *m=0;
    if(!as)return -EINVAL;
    entry=paging_get_entry(as,pg);
    if((entry&PAGE_PRESENT)&&(entry&PAGE_USER)&&!(entry&0x080ULL))
        return c3_mprotect_apply_page(as,pg,c3_mmap_pte_flags_from_prot(prot));
    midx=c3_mmap_region_find(as,pg);
    if(midx>=0&&midx<MMAP_REGION_MAX&&c3_mmap_is_lazy_anon(&g_mmap_table[midx]))
        m=&g_mmap_table[midx];
    frame=c3_mmap_alloc_lazy_frame_for_page(m,pg);
    if(!frame)return -ENOMEM;
    if(!paging_map(as,pg,frame,c3_mmap_pte_flags_from_prot(prot))){
        c3_phys_ref_release(frame);
        return -ENOMEM;
    }
    c3_pf_promote_user_parents(as,pg);
    {
        static uint32_t trace_count=0;
        if(trace_count<32){
            ++trace_count;
            __boot_serial_puts("[mprotect-lazy-map] page=");
            __boot_serial_puthex64(pg);
            __boot_serial_puts(" prot=");
            __boot_serial_putu32((uint32_t)prot);
            __boot_serial_puts(" free=");
            __boot_serial_putu32(pmm_free_count());
            __boot_serial_puts("\n");
        }
    }
    return 0;
}

static bool c3_user_low_va(uint64_t va){
    return va<0x0000800000000000ULL;
}

static bool c3_user_qword_read_present(address_space_t *as,uint64_t va,uint64_t *out){
    uint64_t e0,e1;
    if(!as||!out||!c3_user_low_va(va)||!c3_user_low_va(va+7)||va+7<va)return false;
    e0=paging_get_entry(as,va);
    e1=paging_get_entry(as,va+7);
    if(!(e0&PAGE_PRESENT)||!(e0&PAGE_USER)||(e0&0x080ULL))return false;
    if(!(e1&PAGE_PRESENT)||!(e1&PAGE_USER)||(e1&0x080ULL))return false;
    *out=*(uint64_t*)(uintptr_t)va;
    return true;
}

static bool c3_user_ptr_plausible(address_space_t *as,uint64_t va){
    uint64_t entry;
    if(!va)return true;
    if(!c3_user_low_va(va))return false;
    entry=paging_get_entry(as,va);
    return (entry&PAGE_PRESENT)&&(entry&PAGE_USER)&&!(entry&0x080ULL);
}

static int c3_clone_materialize_user_page(task_t *cur,uint64_t va,const char *tag){
    uint64_t pg;
    int midx;
    uint64_t entry;
    uint64_t frame;
    if(!cur||!cur->addr_space||!va||!c3_user_low_va(va))return 0;
    pg=va&~(PAGE_SIZE-1ULL);
    entry=paging_get_entry(cur->addr_space,pg);
    if((entry&PAGE_PRESENT)&&(entry&PAGE_USER)&&!(entry&0x080ULL))return 0;
    midx=c3_mmap_region_find(cur->addr_space,pg);
    if(midx<0)return 0;
    if(!c3_mmap_is_lazy_anon(&g_mmap_table[midx]))return 0;
    if(!(g_mmap_table[midx].prot&C3_PROT_WRITE))return 0;
    frame=c3_mmap_alloc_lazy_frame_for_page(&g_mmap_table[midx],pg);
    if(!frame){
        __boot_serial_puts("[clone-page] materialize-fail tag=");
        __boot_serial_puts(tag?tag:"?");
        __boot_serial_puts(" page=");
        __boot_serial_puthex64(pg);
        __boot_serial_puts("\n");
        return -ENOMEM;
    }
    if(!paging_map(cur->addr_space,pg,frame,c3_mmap_pte_flags_from_prot(g_mmap_table[midx].prot))){
        c3_phys_ref_release(frame);
        __boot_serial_puts("[clone-page] map-fail tag=");
        __boot_serial_puts(tag?tag:"?");
        __boot_serial_puts(" page=");
        __boot_serial_puthex64(pg);
        __boot_serial_puts("\n");
        return -ENOMEM;
    }
    {
        static uint32_t trace_count=0;
        if(trace_count<8){
            ++trace_count;
            __boot_serial_puts("[clone-page] materialized tag=");
            __boot_serial_puts(tag?tag:"?");
            __boot_serial_puts(" page=");
            __boot_serial_puthex64(pg);
            __boot_serial_puts(" old=");
            __boot_serial_puthex64(entry);
            __boot_serial_puts("\n");
        }
    }
    return 1;
}

static void c3_clone_prepare_user_tls_stack(task_t *cur,uint64_t child_pid,uint64_t flags,uint64_t child_stack,uint64_t ptid,uint64_t ctid,uint64_t tls){
    static uint32_t trace_count=0;
    uint64_t q510=0,q518=0,q520=0,q528=0;
    uint32_t fixed_tsd=0;
    uint64_t tls_page=0,stack_page=0;
    uint64_t tls_entry=0,stack_entry=0;
    int tls_midx=-1,stack_midx=-1;
    uint64_t off;
    if(!cur||!cur->addr_space)return;
    if(!(flags&C3_CLONE_THREAD))return;
    if(flags&C3_CLONE_SETTLS){
        c3_clone_materialize_user_page(cur,tls,"tls");
        if(tls)c3_clone_materialize_user_page(cur,tls+0x5ffULL,"tls-tsd");
    }
    if(child_stack>=8ULL)c3_clone_materialize_user_page(cur,child_stack-8ULL,"stack");
    if(ptid)c3_clone_materialize_user_page(cur,ptid,"ptid");
    if(ctid)c3_clone_materialize_user_page(cur,ctid,"ctid");

    if((flags&C3_CLONE_SETTLS)&&tls&&c3_user_low_va(tls+0x610ULL)){
        for(off=0x518ULL;off<0x610ULL;off+=8ULL){
            uint64_t va=tls+off;
            uint64_t q=0;
            if(!c3_user_qword_read_present(cur->addr_space,va,&q))continue;
            if(!c3_user_ptr_plausible(cur->addr_space,q)){
                *(uint64_t*)(uintptr_t)va=0;
                ++fixed_tsd;
            }
        }
        (void)c3_user_qword_read_present(cur->addr_space,tls+0x510ULL,&q510);
        (void)c3_user_qword_read_present(cur->addr_space,tls+0x518ULL,&q518);
        (void)c3_user_qword_read_present(cur->addr_space,tls+0x520ULL,&q520);
        (void)c3_user_qword_read_present(cur->addr_space,tls+0x528ULL,&q528);
    }

    if(trace_count<96){
        ++trace_count;
        if(tls){
            tls_page=tls&~(PAGE_SIZE-1ULL);
            tls_entry=paging_get_entry(cur->addr_space,tls_page);
            tls_midx=c3_mmap_region_find(cur->addr_space,tls_page);
        }
        if(child_stack>=8ULL){
            stack_page=(child_stack-8ULL)&~(PAGE_SIZE-1ULL);
            stack_entry=paging_get_entry(cur->addr_space,stack_page);
            stack_midx=c3_mmap_region_find(cur->addr_space,stack_page);
        }
        __boot_serial_puts("[clone-tls] child=");
        __boot_serial_putu32((uint32_t)child_pid);
        __boot_serial_puts(" parent=");
        __boot_serial_putu32((uint32_t)cur->pid);
        c3_trace_task_name(cur);
        __boot_serial_puts(" flags=");
        __boot_serial_puthex64(flags);
        __boot_serial_puts(" tls=");
        __boot_serial_puthex64(tls);
        __boot_serial_puts(" tls.pte=");
        __boot_serial_puthex64(tls_entry);
        __boot_serial_puts(" tls.midx=");
        __boot_serial_putu32(tls_midx<0?0xFFFFFFFFu:(uint32_t)tls_midx);
        __boot_serial_puts(" stack=");
        __boot_serial_puthex64(child_stack);
        __boot_serial_puts(" stack.pte=");
        __boot_serial_puthex64(stack_entry);
        __boot_serial_puts(" stack.midx=");
        __boot_serial_putu32(stack_midx<0?0xFFFFFFFFu:(uint32_t)stack_midx);
        __boot_serial_puts(" tsd=");
        __boot_serial_puthex64(q510);
        __boot_serial_puts(",");
        __boot_serial_puthex64(q518);
        __boot_serial_puts(",");
        __boot_serial_puthex64(q520);
        __boot_serial_puts(",");
        __boot_serial_puthex64(q528);
        __boot_serial_puts(" fixed=");
        __boot_serial_putu32(fixed_tsd);
        __boot_serial_puts("\n");
    }
}

int64_t real_sys_munmap(uint64_t addr,uint64_t len){
    task_t *cur=task_current();
    uint64_t us,ue,raw_end;
    int i;
    bool touched=false;
    bool trace=false;
    if(!cur||!cur->addr_space)return -ESRCH;
    if(!len|| (addr&(PAGE_SIZE-1)))return -EINVAL;
    if(addr>=C3_USER_TOP||len>C3_USER_TOP-addr)return -EINVAL;
    us=addr;
    raw_end=addr+len;
    ue=(raw_end+PAGE_SIZE-1)&~(PAGE_SIZE-1);
    if(ue<raw_end||ue>C3_USER_TOP)return -EINVAL;
    if(g_c3_munmap_trace_count<C3_MUNMAP_TRACE_MAX){
        ++g_c3_munmap_trace_count;
        trace=true;
        __boot_serial_puts("[munmap] pid=");
        __boot_serial_putu32((uint32_t)cur->pid);
        c3_trace_task_name(cur);
        __boot_serial_puts(" addr=");
        __boot_serial_puthex64(addr);
        __boot_serial_puts(" len=");
        __boot_serial_puthex64(len);
        __boot_serial_puts(" as=");
        __boot_serial_puthex64((uint64_t)(uintptr_t)cur->addr_space);
        __boot_serial_puts("\n");
    }
    for(i=0;i<MMAP_REGION_MAX;++i){
        uint64_t rs,re,os,oe;
        if(!g_mmap_table[i].used||g_mmap_table[i].as!=cur->addr_space)continue;
        rs=g_mmap_table[i].virt;
        re=rs+g_mmap_table[i].size;
        if(ue<=rs||us>=re)continue;
        os=(us>rs)?us:rs;
        oe=(ue<re)?ue:re;
        if(oe<=os)continue;
        if(trace){
            __boot_serial_puts("[munmap-hit] pid=");
            __boot_serial_putu32((uint32_t)cur->pid);
            c3_trace_task_name(cur);
            __boot_serial_puts(" slot=");
            __boot_serial_putu32((uint32_t)i);
            __boot_serial_puts(" map=");
            __boot_serial_puthex64(rs);
            __boot_serial_puts("+");
            __boot_serial_puthex64(re-rs);
            __boot_serial_puts(" cut=");
            __boot_serial_puthex64(os);
            __boot_serial_puts("+");
            __boot_serial_puthex64(oe-os);
            if(g_mmap_table[i].backing_path[0]){
                __boot_serial_puts(" path=");
                __boot_serial_puts(g_mmap_table[i].backing_path);
            }
            __boot_serial_puts("\n");
        }
        c3_mmap_writeback_vfs_range(cur,&g_mmap_table[i],os,oe-os);
        c3_mmap_unmap_pages(cur->addr_space,os,oe-os,true);
        touched=true;
        if(os==rs&&oe==re){
            g_mmap_table[i].used=false;
            continue;
        }
        if(os==rs){
            g_mmap_table[i].virt=oe;
            g_mmap_table[i].size=re-oe;
            g_mmap_table[i].offset+=(oe-rs);
            continue;
        }
        if(oe==re){
            g_mmap_table[i].size=os-rs;
            continue;
        }
        {
            int ns=c3_mmap_alloc_slot();
            if(ns>=0){
                g_mmap_table[ns]=g_mmap_table[i];
                g_mmap_table[ns].virt=oe;
                g_mmap_table[ns].size=re-oe;
                g_mmap_table[ns].offset+=(oe-rs);
                g_mmap_table[i].size=os-rs;
            }else{
                /* Out of metadata slots: keep left half only */
                g_mmap_table[i].size=os-rs;
            }
        }
    }
    if(touched)c3_mmap_merge_adjacent(cur->addr_space);
    return 0;
}

int64_t real_sys_mprotect(uint64_t addr,uint64_t len,int prot){
    task_t *cur=task_current();
    uint64_t us,ue,pf,pg;
    const int known_prot=C3_PROT_READ|C3_PROT_WRITE|C3_PROT_EXEC;
    int i;
    bool touched=false;
    if(!cur||!cur->addr_space)return -ESRCH;
    if(prot&~known_prot)return -EINVAL;
    if(!len||(addr&(PAGE_SIZE-1)))return -EINVAL;
    us=addr;
    ue=(addr+len+PAGE_SIZE-1)&~(PAGE_SIZE-1);
    if(g_c3_mprotect_trace_count<C3_MPROTECT_TRACE_MAX){
        int midx=c3_mmap_region_find(cur->addr_space,us);
        ++g_c3_mprotect_trace_count;
        __boot_serial_puts("[mprotect] addr=");
        __boot_serial_puthex64(addr);
        __boot_serial_puts(" len=");
        __boot_serial_puthex64(len);
        __boot_serial_puts(" prot=");
        __boot_serial_putu32((uint32_t)prot);
        if(midx>=0&&midx<MMAP_REGION_MAX&&g_mmap_table[midx].backing_path[0]){
            __boot_serial_puts(" path=");
            __boot_serial_puts(g_mmap_table[midx].backing_path);
            __boot_serial_puts(" base=");
            __boot_serial_puthex64(g_mmap_table[midx].virt);
            __boot_serial_puts(" off=");
            __boot_serial_puthex64(g_mmap_table[midx].offset);
        }
        __boot_serial_puts("\n");
    }
    pf=c3_mmap_pte_flags_from_prot(prot);
    if(!c3_mmap_range_fully_covered(cur->addr_space,us,ue)){
        /* exec/ELF segments can be mapped outside mmap metadata; allow
         * mprotect as long as every page in range is currently mapped. */
        for(pg=us;pg<ue;pg+=PAGE_SIZE){
            if(!(paging_get_entry(cur->addr_space,pg)&PAGE_PRESENT))return -ENOMEM;
        }
        for(pg=us;pg<ue;pg+=PAGE_SIZE){
            int pr=c3_mprotect_apply_page(cur->addr_space,pg,pf);
            if(pr<0)return pr;
        }
        return 0;
    }
    for(i=0;i<MMAP_REGION_MAX;++i){
        uint64_t rs,re,os,oe,pg;
        bool lazy_anon;
        bool materialize_small_lazy;
        if(!g_mmap_table[i].used||g_mmap_table[i].as!=cur->addr_space)continue;
        rs=g_mmap_table[i].virt;
        re=rs+g_mmap_table[i].size;
        if(ue<=rs||us>=re)continue;
        lazy_anon=c3_mmap_is_lazy_anon(&g_mmap_table[i]);
        if((prot&C3_PROT_WRITE)&&c3_mmap_is_shared(g_mmap_table[i].flags)){
            if(g_mmap_table[i].fd>=0&&!c3_mmap_fd_perms_ok(cur,g_mmap_table[i].fd,prot,g_mmap_table[i].flags))
                return -EACCES;
        }
        os=(us>rs)?us:rs;
        oe=(ue<re)?ue:re;
        if(oe<=os)continue;
        materialize_small_lazy=lazy_anon&&(prot&C3_PROT_WRITE)&&
            ((oe-os)<=(2ULL*1024ULL*1024ULL));
        if(lazy_anon&&!materialize_small_lazy&&
           (oe-os)>(64ULL*1024ULL*1024ULL)){
            /* Large browser reservations are usually entirely lazy. Updating
             * metadata is enough; future faults will materialize pages with
             * the new protection. */
        }else{
            for(pg=os;pg<oe;pg+=PAGE_SIZE){
                int pr;
                if(lazy_anon){
                    uint64_t entry=paging_get_entry(cur->addr_space,pg);
                    if(!(entry&PAGE_PRESENT)||!(entry&PAGE_USER)||(entry&0x080ULL)){
                        if(materialize_small_lazy){
                            pr=c3_mmap_materialize_zero_page(cur->addr_space,pg,prot);
                            if(pr<0)return pr;
                        }
                        continue;
                    }
                }
                pr=c3_mprotect_apply_page(cur->addr_space,pg,pf);
                if(pr==-ENOENT&&lazy_anon){
                    if(materialize_small_lazy){
                        pr=c3_mmap_materialize_zero_page(cur->addr_space,pg,prot);
                        if(pr<0)return pr;
                    }
                    continue;
                }
                if(pr<0)return pr;
            }
        }
        touched=true;
        if(os==rs&&oe==re){
            g_mmap_table[i].prot=prot;
            continue;
        }
        if(os==rs){
            int ns=c3_mmap_alloc_slot();
            if(ns>=0){
                g_mmap_table[ns]=g_mmap_table[i];
                g_mmap_table[ns].virt=oe;
                g_mmap_table[ns].size=re-oe;
                g_mmap_table[ns].offset+=(oe-rs);
                g_mmap_table[i].size=oe-rs;
                g_mmap_table[i].prot=prot;
            }else{
                g_mmap_table[i].prot=prot;
            }
            continue;
        }
        if(oe==re){
            int ns=c3_mmap_alloc_slot();
            if(ns>=0){
                g_mmap_table[ns]=g_mmap_table[i];
                g_mmap_table[ns].virt=os;
                g_mmap_table[ns].size=oe-os;
                g_mmap_table[ns].offset+=(os-rs);
                g_mmap_table[ns].prot=prot;
                g_mmap_table[i].size=os-rs;
            }else{
                g_mmap_table[i].prot=prot;
            }
            continue;
        }
        {
            int mid=c3_mmap_alloc_slot();
            int right=(mid>=0)?c3_mmap_alloc_slot_except(mid):-1;
            if(mid>=0&&right>=0){
                g_mmap_table[mid]=g_mmap_table[i];
                g_mmap_table[mid].virt=os;
                g_mmap_table[mid].size=oe-os;
                g_mmap_table[mid].offset+=(os-rs);
                g_mmap_table[mid].prot=prot;
                g_mmap_table[right]=g_mmap_table[i];
                g_mmap_table[right].virt=oe;
                g_mmap_table[right].size=re-oe;
                g_mmap_table[right].offset+=(oe-rs);
                g_mmap_table[i].size=os-rs;
            }else{
                g_mmap_table[i].prot=prot;
            }
        }
    }
    if(touched)c3_mmap_merge_adjacent(cur->addr_space);
    return touched?0:-ENOMEM;
}

int64_t real_sys_brk(uint64_t addr){
    task_t *cur=task_current();
    uint64_t old_end,new_end,pg;
    if(!cur||!cur->addr_space)return -ESRCH;
    if(!addr)return(int64_t)cur->brk_current;
    if(addr<cur->brk_start)return(int64_t)cur->brk_current;
    old_end=(cur->brk_current+PAGE_SIZE-1)&~(PAGE_SIZE-1);
    new_end=(addr+PAGE_SIZE-1)&~(PAGE_SIZE-1);
    if(new_end>old_end){
        for(pg=old_end;pg<new_end;pg+=PAGE_SIZE){
            uint64_t frame=pmm_alloc_frame();
            if(!frame)return(int64_t)cur->brk_current;
            c3_phys_ref_set_initial(frame);
            paging_map(cur->addr_space,pg,frame,c3_mmap_pte_flags_from_prot(C3_PROT_READ|C3_PROT_WRITE));
            c3_memset((void*)(uintptr_t)pg,0,PAGE_SIZE);
        }
    }else if(new_end<old_end){
        for(pg=new_end;pg<old_end;pg+=PAGE_SIZE){
            uint64_t phys=paging_translate(cur->addr_space,pg);
            paging_unmap(cur->addr_space,pg);
            if(phys)c3_phys_ref_release(phys&~0xFFFULL);
        }
    }
    cur->brk_current=addr;
    return(int64_t)cur->brk_current;
}

int64_t real_sys_mremap(uint64_t old_addr,uint64_t old_size,uint64_t new_size,int flags,uint64_t new_addr){
    task_t *cur=task_current();
    const int known_flags=C3_MREMAP_MAYMOVE|C3_MREMAP_FIXED|C3_MREMAP_DONTUNMAP;
    int idx;
    uint64_t old_len,new_len;
    if(!cur||!cur->addr_space)return -ESRCH;
    if(flags&~known_flags)return -EINVAL;
    if((flags&C3_MREMAP_FIXED)&&(!(flags&C3_MREMAP_MAYMOVE)||!new_addr))return -EINVAL;
    if(!old_size||!new_size||(old_addr&(PAGE_SIZE-1)))return -EINVAL;
    old_len=(old_size+PAGE_SIZE-1)&~(PAGE_SIZE-1);
    new_len=(new_size+PAGE_SIZE-1)&~(PAGE_SIZE-1);
    idx=c3_mmap_region_find(cur->addr_space,old_addr);
    if(idx<0||g_mmap_table[idx].virt!=old_addr)return -EINVAL;
    if(new_len==old_len)return (int64_t)old_addr;
    if(new_len<old_len){
        real_sys_munmap(old_addr+new_len,old_len-new_len);
        if(g_mmap_table[idx].used)g_mmap_table[idx].size=new_len;
        return (int64_t)old_addr;
    }
    /* Try in-place growth first */
    if(!c3_mmap_overlap_as(cur->addr_space,old_addr+old_len,old_addr+new_len,idx)){
        uint64_t add=new_len-old_len;
        if(c3_mmap_is_lazy_anon(&g_mmap_table[idx])){
            g_mmap_table[idx].size=new_len;
            return (int64_t)old_addr;
        }
        if(c3_mmap_map_pages(cur->addr_space,old_addr+old_len,add,c3_mmap_pte_flags_from_prot(g_mmap_table[idx].prot),true)==0){
            if(!c3_mmap_is_anon(g_mmap_table[idx].flags)){
                c3_mmap_file_copy(cur,old_addr+old_len,add,g_mmap_table[idx].fd,g_mmap_table[idx].offset+old_len);
            }
            g_mmap_table[idx].size=new_len;
            return (int64_t)old_addr;
        }
    }
    if(!(flags&C3_MREMAP_MAYMOVE))return -ENOMEM;
    if(flags&C3_MREMAP_FIXED){
        if(new_addr&(PAGE_SIZE-1))return -EINVAL;
        if(!(flags&C3_MREMAP_DONTUNMAP)){
            uint64_t ns=new_addr,ne=new_addr+new_len;
            uint64_t os=old_addr,oe=old_addr+old_len;
            if(!(ne<=os||ns>=oe))return -EINVAL;
        }
    }else{
        new_addr=0;
    }
    {
        int mapf=g_mmap_table[idx].flags;
        int64_t nva;
        mapf&=~(C3_MAP_FIXED|C3_MAP_FIXED_NOREPLACE);
        if(new_addr)mapf|=C3_MAP_FIXED;
        nva=real_sys_mmap(new_addr,new_len,g_mmap_table[idx].prot,mapf,g_mmap_table[idx].fd,g_mmap_table[idx].offset);
        if(nva<0)return nva;
        c3_memcpy((void*)(uintptr_t)nva,(const void*)(uintptr_t)old_addr,(size_t)old_len);
        if(!(flags&C3_MREMAP_DONTUNMAP))real_sys_munmap(old_addr,old_len);
        return nva;
    }
}

static bool c3_va_is_shared_mapping(address_space_t *as,uint64_t va){
    int idx=c3_mmap_region_find(as,va);
    if(idx<0)return false;
    return c3_mmap_is_shared(g_mmap_table[idx].flags);
}

int compat3_fork_address_space(task_t *parent,task_t *child){
    int pml4i,pdpti,pdi,pti;
    address_space_t *pas,*cas;
    if(!parent||!child)return -EINVAL;
    pas=parent->addr_space;
    cas=child->addr_space;
    if(!pas||!cas||pas==paging_get_kernel_space()||cas==paging_get_kernel_space())return -EINVAL;
    for(pml4i=0;pml4i<256;++pml4i){
        if(!(pas->pml4->entries[pml4i]&PAGE_PRESENT))continue;
        {
            page_table_t *pdpt=(page_table_t*)PHYS_TO_DMAP(pas->pml4->entries[pml4i]&~0xFFFULL);
            for(pdpti=0;pdpti<512;++pdpti){
                if(!(pdpt->entries[pdpti]&PAGE_PRESENT))continue;
                if(pdpt->entries[pdpti]&0x080ULL)continue; /* 1 GiB huge identity mapping */
                {
                    page_table_t *pd=(page_table_t*)PHYS_TO_DMAP(pdpt->entries[pdpti]&~0xFFFULL);
                    for(pdi=0;pdi<512;++pdi){
                        if(!(pd->entries[pdi]&PAGE_PRESENT))continue;
                        if(pd->entries[pdi]&0x080ULL)continue; /* 2 MiB huge identity mapping */
                        {
                            page_table_t *pt=(page_table_t*)PHYS_TO_DMAP(pd->entries[pdi]&~0xFFFULL);
                            for(pti=0;pti<512;++pti){
                                uint64_t entry=pt->entries[pti];
                                uint64_t phys;
                                uint64_t flags;
                                uint64_t va;
                                bool user;
                                bool shared;
                                if(!(entry&PAGE_PRESENT))continue;
                                phys=entry&C3_PTE_ADDR_MASK;
                                flags=entry&C3_PTE_KEEP_MASK;
                                user=(flags&PAGE_USER)!=0;
                                if(!user||!phys)continue;
                                va=((uint64_t)pml4i<<39)|((uint64_t)pdpti<<30)|((uint64_t)pdi<<21)|((uint64_t)pti<<12);
                                shared=c3_va_is_shared_mapping(pas,va);
                                if(!shared&&((flags&PAGE_WRITABLE)||(flags&C3_PAGE_SOFT_COW))){
                                    flags&=~PAGE_WRITABLE;
                                    flags|=C3_PAGE_SOFT_COW;
                                    pt->entries[pti]=phys|flags;
                                    if(task_current()&&task_current()->addr_space==pas)paging_flush_tlb(va);
                                }
                                c3_phys_ref_set_initial(phys);
                                c3_phys_ref_retain(phys);
                                if(!paging_map(cas,va,phys,flags)){
                                    c3_phys_ref_release(phys);
                                    return -ENOMEM;
                                }
                            }
                        }
                    }
                }
            }
        }
    }
    /* Mirror mmap metadata for the child. */
    for(pml4i=0;pml4i<MMAP_REGION_MAX;++pml4i){
        if(!g_mmap_table[pml4i].used||g_mmap_table[pml4i].as!=pas)continue;
        for(pdpti=0;pdpti<MMAP_REGION_MAX;++pdpti){
            if(!g_mmap_table[pdpti].used){
                g_mmap_table[pdpti]=g_mmap_table[pml4i];
                g_mmap_table[pdpti].as=cas;
                g_mmap_table[pdpti].used=true;
                break;
            }
        }
    }
    return 0;
}

#define C3_PFERR_WRITE 0x2
#define C3_PFERR_USER  0x4
#define C3_PFERR_FETCH 0x10

/* PF loop detector: if the same page faults repeatedly the fix
 * isn't sticking and we're in an infinite fault loop. */
static uint64_t g_c3_pf_loop_addr = 0;
static uint32_t g_c3_pf_loop_count = 0;
static uint32_t g_c3_pf_total_handled = 0;
#define C3_PF_LOOP_THRESHOLD 6

static bool c3_pf_rewrite_entry(address_space_t *as,uint64_t page,uint64_t phys,uint64_t keep){
    uint64_t entry=(phys&C3_PTE_ADDR_MASK)|(keep&C3_PTE_KEEP_MASK)|PAGE_PRESENT;
    if(paging_set_entry(as,page,entry))return true;
    return paging_map(as,page,phys,entry&C3_PTE_KEEP_MASK);
}

/* Promote PAGE_USER on parent entries (PML4E, PDPTE, PDE) for a given
 * virtual address.  When the deep-clone copies kernel identity-map entries
 * as supervisor-only, a later user mapping under that range will have the
 * leaf PTE marked PAGE_USER but the parent entries still lack it — the CPU
 * faults with err=0x2 (supervisor violation) before it even reaches the
 * leaf.  Calling this after any successful user PF fixup ensures the walk
 * succeeds on the next attempt. */
static void c3_pf_promote_user_parents(address_space_t *as,uint64_t virt){
    uint64_t pml4i=(virt>>39)&0x1FF,pdpti=(virt>>30)&0x1FF,pdi=(virt>>21)&0x1FF;
    page_table_t *pdpt,*pd;
    if(!as||!as->pml4)return;
    /* All page-table frames are reached via DMAP — no CR3 switch needed. */
    if(!(as->pml4->entries[pml4i]&PAGE_PRESENT))goto done;
    if(!(as->pml4->entries[pml4i]&PAGE_USER))
        as->pml4->entries[pml4i]|=PAGE_USER;
    pdpt=(page_table_t*)PHYS_TO_DMAP(as->pml4->entries[pml4i]&~0xFFFULL);
    if(pdpt->entries[pdpti]&0x080ULL)goto done;  /* PAGE_HUGE */
    if(!(pdpt->entries[pdpti]&PAGE_PRESENT))goto done;
    if(!(pdpt->entries[pdpti]&PAGE_USER))
        pdpt->entries[pdpti]|=PAGE_USER;
    pd=(page_table_t*)PHYS_TO_DMAP(pdpt->entries[pdpti]&~0xFFFULL);
    if(pd->entries[pdi]&0x080ULL)goto done;  /* PAGE_HUGE */
    if(!(pd->entries[pdi]&PAGE_PRESENT))goto done;
    if(!(pd->entries[pdi]&PAGE_USER))
        pd->entries[pdi]|=PAGE_USER;
done:
    __asm__ volatile("invlpg (%0)" :: "r"(virt) : "memory");
}

static int c3_user_prepare_private_write_page(address_space_t *as,uint64_t page,const char *tag){
    uint64_t entry,phys,keep,new_frame;
    if(!as||!c3_user_low_va(page))return -EFAULT;
    entry=paging_get_entry(as,page);
    if(!(entry&PAGE_PRESENT)||!(entry&PAGE_USER)||(entry&0x080ULL))return -EFAULT;
    phys=entry&C3_PTE_ADDR_MASK;
    keep=entry&C3_PTE_KEEP_MASK;
    if((keep&PAGE_WRITABLE)&&!(keep&C3_PAGE_SOFT_COW))return 0;
    if(!(keep&C3_PAGE_SOFT_COW))return -EFAULT;
    new_frame=pmm_alloc_frame();
    if(!new_frame)return -ENOMEM;
    c3_phys_ref_set_initial(new_frame);
    c3_memcpy(PHYS_TO_DMAP(new_frame),PHYS_TO_DMAP(phys),PAGE_SIZE);
    keep|=PAGE_WRITABLE|PAGE_USER;
    keep&=~C3_PAGE_SOFT_COW;
    if(!c3_pf_rewrite_entry(as,page,new_frame,keep)){
        c3_phys_ref_release(new_frame);
        return -ENOMEM;
    }
    c3_phys_ref_release(phys);
    c3_pf_promote_user_parents(as,page);
    {
        static uint32_t trace_count=0;
        if(trace_count<16){
            ++trace_count;
            __boot_serial_puts("[user-write-cow] tag=");
            __boot_serial_puts(tag?tag:"?");
            __boot_serial_puts(" page=");
            __boot_serial_puthex64(page);
            __boot_serial_puts(" old=");
            __boot_serial_puthex64(phys);
            __boot_serial_puts(" new=");
            __boot_serial_puthex64(new_frame);
            __boot_serial_puts("\n");
        }
    }
    return 0;
}

static int c3_user_write_u32_as(address_space_t *as,uint64_t va,uint32_t val,const char *tag){
    size_t done=0;
    if(!as||!c3_user_low_va(va)||!c3_user_low_va(va+3ULL)||va+3ULL<va)return -EFAULT;
    while(done<4){
        uint64_t cur=va+(uint64_t)done;
        uint64_t page=cur&~(PAGE_SIZE-1ULL);
        size_t in_page=(size_t)(cur-page);
        size_t n=PAGE_SIZE-in_page;
        uint64_t phys;
        size_t i;
        int rc;
        if(n>4-done)n=4-done;
        rc=c3_user_prepare_private_write_page(as,page,tag);
        if(rc<0)return rc;
        phys=paging_translate(as,cur);
        if(!phys)return -EFAULT;
        for(i=0;i<n;++i){
            size_t bi=done+i;
            uint8_t b=(uint8_t)((val>>(bi*8))&0xFFu);
            volatile uint8_t *dst=(volatile uint8_t*)PHYS_TO_DMAP(phys+(uint64_t)i);
            *dst=b;
        }
        done+=n;
    }
    return 0;
}

static bool c3_pf_mmap_access_allowed(int prot,uint64_t err_code){
    if(err_code&C3_PFERR_FETCH)return (prot&C3_PROT_EXEC)!=0;
    if(err_code&C3_PFERR_WRITE)return (prot&C3_PROT_WRITE)!=0;
    return (prot&(C3_PROT_READ|C3_PROT_WRITE|C3_PROT_EXEC))!=0;
}

static bool c3_pf_materialize_lazy_anon(task_t *cur,int midx,uint64_t page,uint64_t err_code){
    uint64_t frame,pflags;
    mmap_entry_t *m;
    if(!cur||midx<0||midx>=MMAP_REGION_MAX)return false;
    m=&g_mmap_table[midx];
    if(!c3_mmap_is_lazy_anon(m))return false;
    if(!c3_pf_mmap_access_allowed(m->prot,err_code))return false;
    frame=c3_mmap_alloc_lazy_frame_for_page(m,page);
    if(!frame)return false;
    pflags=c3_mmap_pte_flags_from_prot(m->prot);
    if(!paging_map(cur->addr_space,page,frame,pflags)){
        c3_phys_ref_release(frame);
        return false;
    }
    c3_pf_promote_user_parents(cur->addr_space,page);
    {
        static uint32_t trace_count=0;
        if(trace_count<8){
            ++trace_count;
            __boot_serial_puts("[pf-lazy-anon] page=");
            __boot_serial_puthex64(page);
            __boot_serial_puts(" prot=");
            __boot_serial_putu32((uint32_t)m->prot);
            __boot_serial_puts(" size=");
            __boot_serial_puthex64(m->size);
            __boot_serial_puts(" base=");
            __boot_serial_puthex64(m->virt);
            __boot_serial_puts("\n");
        }
    }
    return true;
}

static void c3_pf_trace_denied(task_t *cur,uint64_t page,uint64_t err_code,uint64_t entry,int midx,const char *why){
    static uint32_t deny_trace=0;
    if(deny_trace>=64)return;
    ++deny_trace;
    __boot_serial_puts("[pf-deny] why=");
    __boot_serial_puts(why?why:"?");
    __boot_serial_puts(" pid=");
    __boot_serial_putu32((uint32_t)(cur?cur->pid:0));
    if(cur)c3_trace_task_name(cur);
    __boot_serial_puts(" page=");
    __boot_serial_puthex64(page);
    __boot_serial_puts(" err=");
    __boot_serial_puthex64(err_code);
    __boot_serial_puts(" pte=");
    __boot_serial_puthex64(entry);
    __boot_serial_puts(" midx=");
    __boot_serial_putu32((midx>=0)?(uint32_t)midx:0xFFFFFFFFu);
    __boot_serial_puts("\n");
}

bool compat3_handle_page_fault(uint64_t fault_addr,uint64_t err_code){
    task_t *cur=task_current();
    uint64_t page=fault_addr&~(PAGE_SIZE-1);
    uint64_t entry,phys,keep;
    int midx;
    int ridx;
    if(!cur||!cur->addr_space)return false;
    /*
     * Linux-style user processes must not gain access to the NULL/low
     * guard area.  The kernel has a supervisor identity map there, and the
     * generic "repair PAGE_USER/PAGE_WRITABLE" fallback below used to turn a
     * deliberate NULL crash into a writable mapping.  Firefox relies on that
     * NULL write to terminate mozalloc_abort instead of recursing forever.
     */
    if(page<0x10000ULL)return false;
    /* PF loop detection */
    if(page==g_c3_pf_loop_addr){
        ++g_c3_pf_loop_count;
    }else{
        g_c3_pf_loop_addr=page;
        g_c3_pf_loop_count=1;
    }
    if(g_c3_pf_loop_count>=C3_PF_LOOP_THRESHOLD){
        __boot_serial_puts("[pf-LOOP] page=");
        __boot_serial_puthex64(page);
        __boot_serial_puts(" err=");
        __boot_serial_puthex64(err_code);
        __boot_serial_puts(" count=");
        __boot_serial_putu32(g_c3_pf_loop_count);
        __boot_serial_puts(" pid=");
        __boot_serial_putu32((uint32_t)cur->pid);
        entry=paging_get_entry(cur->addr_space,page);
        __boot_serial_puts(" pte=");
        __boot_serial_puthex64(entry);
        __boot_serial_puts("\n");
        /* Don't fix it anymore — let the exception path print the full
         * register dump so we can see what instruction is looping. */
        return false;
    }
    ++g_c3_pf_total_handled;
    entry=paging_get_entry(cur->addr_space,page);
    midx=c3_mmap_region_find(cur->addr_space,page);
    if(midx>=0&&g_mmap_table[midx].prot==0){
        static uint32_t protnone_trace=0;
        if(protnone_trace<32){
            ++protnone_trace;
            __boot_serial_puts("[pf-protnone] page=");
            __boot_serial_puthex64(page);
            __boot_serial_puts(" err=");
            __boot_serial_puthex64(err_code);
            __boot_serial_puts(" base=");
            __boot_serial_puthex64(g_mmap_table[midx].virt);
            __boot_serial_puts(" size=");
            __boot_serial_puthex64(g_mmap_table[midx].size);
            __boot_serial_puts(" flags=");
            __boot_serial_puthex64((uint64_t)(uint32_t)g_mmap_table[midx].flags);
            __boot_serial_puts("\n");
        }
        return false;
    }
    if(midx>=0&&c3_mmap_is_lazy_anon(&g_mmap_table[midx])&&
       (!(entry&PAGE_PRESENT)||!(entry&PAGE_USER)||(entry&0x080ULL))){
        if(c3_pf_materialize_lazy_anon(cur,midx,page,err_code))return true;
    }
    if(!(entry&PAGE_PRESENT)){
        /* Stack growth: if any code (user OR kernel on behalf of user)
         * touches a non-present page in the user stack region, map one
         * more anonymous page.  We zero via the physical frame address
         * (identity-mapped) to avoid a nested PF on the virtual address
         * whose paging-structure caches may still be stale. */
        if(page<C3_USER_STACK_TOP&&
           page>=(C3_USER_STACK_TOP-C3_USER_STACK_GROW_MAX)){
            uint64_t frame=pmm_alloc_frame();
            if(!frame)return false;
            c3_phys_ref_set_initial(frame);
            /* Zero the frame via DMAP — always reachable. */
            c3_memset((void*)PHYS_TO_DMAP(frame),0,PAGE_SIZE);
            if(!paging_map(cur->addr_space,page,frame,PAGE_PRESENT|PAGE_WRITABLE|PAGE_USER|PAGE_NX)){
                c3_phys_ref_release(frame);
                return false;
            }
            c3_pf_promote_user_parents(cur->addr_space,page);
            return true;
        }
        return false;
    }
    keep=entry&C3_PTE_KEEP_MASK;
    phys=entry&C3_PTE_ADDR_MASK;

    /* Recover instruction-fetch protection faults on user-space pages.
     * When IRET to ring 3 faults because the target page has NX set,
     * the CPU attributes the fault to ring 0 (USER bit = 0) because
     * the privilege switch didn't complete.  Handle both USER and
     * non-USER exec faults for addresses in the user canonical half. */
    if((err_code&C3_PFERR_FETCH)&&(page<0x0000800000000000ULL)){
        bool patched=false;
        if(midx>=0&&(g_mmap_table[midx].prot&C3_PROT_EXEC)){
            if(keep&PAGE_NX){
                keep&=~PAGE_NX;
                patched=true;
            }
            if(!(keep&PAGE_USER)){
                keep|=PAGE_USER;
                patched=true;
            }
            patched=true;
        }else if((keep&PAGE_USER)&&!(keep&PAGE_NX)&&!(entry&0x080ULL)){
            c3_pf_promote_user_parents(cur->addr_space,page);
            ++g_c3_pf_exec_fixups;
            return true;
        }else if(midx<0){
            c3_pf_trace_denied(cur,page,err_code,entry,midx,"exec-outside-mmap");
        }
        if(patched){
            if(!c3_pf_rewrite_entry(cur->addr_space,page,phys,keep))return false;
            c3_pf_promote_user_parents(cur->addr_space,page);
            ++g_c3_pf_exec_fixups;
            return true;
        }
    }

    /* Recover read-side user faults on already-present pages by rewriting
     * the same mapping with user permissions, which also repairs upper-level
     * PAGE_USER propagation when needed. */
    if((err_code&C3_PFERR_USER)&&
       !(err_code&C3_PFERR_WRITE)&&
       !(err_code&C3_PFERR_FETCH)){
        if(page<0x0000800000000000ULL){
            bool patched=false;
            if(midx>=0&&(g_mmap_table[midx].prot&(C3_PROT_READ|C3_PROT_EXEC))){
                patched=true;
                if(!(keep&PAGE_USER))keep|=PAGE_USER;
                if(g_mmap_table[midx].prot&C3_PROT_EXEC)keep&=~PAGE_NX;
            }else if((keep&PAGE_USER)&&!(entry&0x080ULL)){
                c3_pf_promote_user_parents(cur->addr_space,page);
                ++g_c3_pf_exec_fixups;
                return true;
            }else if(midx<0){
                c3_pf_trace_denied(cur,page,err_code,entry,midx,"read-outside-mmap");
            }
            if(patched){
                if(!c3_pf_rewrite_entry(cur->addr_space,page,phys,keep))return false;
                c3_pf_promote_user_parents(cur->addr_space,page);
                ++g_c3_pf_exec_fixups;
                return true;
            }
        }
    }

    if((err_code&C3_PFERR_WRITE)==0)return false;

    if(keep&C3_PAGE_SOFT_COW){
        ++g_c3_cow_faults;
        ridx=c3_phys_ref_index(phys);
        if(ridx>=0&&g_c3_phys_refcnt[ridx]>1){
            uint64_t new_frame=pmm_alloc_frame();
            if(!new_frame){
                ++g_c3_cow_fault_fail;
                return false;
            }
            c3_phys_ref_set_initial(new_frame);
            c3_memcpy(PHYS_TO_DMAP(new_frame),PHYS_TO_DMAP(phys),PAGE_SIZE);
            c3_phys_ref_release(phys);
            phys=new_frame;
            ++g_c3_cow_copies;
        }
        keep&=~C3_PAGE_SOFT_COW;
        keep|=PAGE_WRITABLE;
        if(!c3_pf_rewrite_entry(cur->addr_space,page,phys,keep)){
            ++g_c3_cow_fault_fail;
            return false;
        }
        c3_pf_promote_user_parents(cur->addr_space,page);
        return true;
    }

    /* Some loaders transiently expect writable private mappings even if the
     * PTE reached RO state (e.g. after split/merge/mprotect sequencing). If
     * mmap metadata still marks the region writable, honor that intent. */
    if(midx>=0&&(g_mmap_table[midx].prot&C3_PROT_WRITE)){
        ridx=c3_phys_ref_index(phys);
        if(c3_mmap_is_private(g_mmap_table[midx].flags)&&ridx>=0&&g_c3_phys_refcnt[ridx]>1){
            uint64_t new_frame=pmm_alloc_frame();
            if(!new_frame)return false;
            c3_phys_ref_set_initial(new_frame);
            c3_memcpy(PHYS_TO_DMAP(new_frame),PHYS_TO_DMAP(phys),PAGE_SIZE);
            c3_phys_ref_release(phys);
            phys=new_frame;
            ++g_c3_cow_copies;
        }
        keep|=PAGE_WRITABLE;
        if(!c3_pf_rewrite_entry(cur->addr_space,page,phys,keep))return false;
        c3_pf_promote_user_parents(cur->addr_space,page);
        ++g_c3_pf_write_fixups;
        return true;
    }

    /* Do not turn non-writable private mappings into writable memory.  That
     * hid real invalid writes and, with Firefox's thread/process churn, could
     * corrupt executable pages (the crash signature was a libstdc++ text RIP
     * whose bytes had become ASCII data).  Writable private mappings are
     * handled by the metadata-authorized block above. */
    if(midx>=0&&c3_mmap_is_private(g_mmap_table[midx].flags)){
        static uint32_t ro_private_trace=0;
        if(ro_private_trace<32){
            ++ro_private_trace;
            __boot_serial_puts("[pf-deny-ro-private] pid=");
            __boot_serial_putu32((uint32_t)cur->pid);
            c3_trace_task_name(cur);
            __boot_serial_puts(" page=");
            __boot_serial_puthex64(page);
            __boot_serial_puts(" prot=");
            __boot_serial_putu32((uint32_t)g_mmap_table[midx].prot);
            __boot_serial_puts(" flags=");
            __boot_serial_puthex64((uint64_t)(uint32_t)g_mmap_table[midx].flags);
            if(g_mmap_table[midx].backing_path[0]){
                __boot_serial_puts(" path=");
                __boot_serial_puts(g_mmap_table[midx].backing_path);
            }
            __boot_serial_puts("\n");
        }
        return false;
    }

    /* Last-resort recovery only repairs upper-level PAGE_USER propagation
     * for pages that are already user-writable.  Do not convert supervisor
     * identity mappings or true RO pages into writable user memory: doing so
     * hides invalid accesses and can corrupt browser allocator metadata. */
    if(page<0x0000800000000000ULL&&
       (keep&PAGE_USER)&&(keep&PAGE_WRITABLE)&&!(entry&0x080ULL)){
        c3_pf_promote_user_parents(cur->addr_space,page);
        ++g_c3_pf_write_fixups;
        return true;
    }
    if(midx<0)c3_pf_trace_denied(cur,page,err_code,entry,midx,"write-outside-mmap");

    if(g_c3_pf_trace_count<32){
        ++g_c3_pf_trace_count;
        __boot_serial_puts("[pf] unhandled write fault page=");
        __boot_serial_puthex64(page);
        __boot_serial_puts(" keep=");
        __boot_serial_puthex64(keep);
        __boot_serial_puts(" midx=");
        __boot_serial_putu32((midx>=0)?(uint32_t)midx:0xFFFFFFFFu);
        if(midx>=0){
            __boot_serial_puts(" prot=");
            __boot_serial_putu32((uint32_t)g_mmap_table[midx].prot);
            __boot_serial_puts(" flags=");
            __boot_serial_puthex64((uint64_t)(uint32_t)g_mmap_table[midx].flags);
        }
        __boot_serial_puts("\n");
    }
    return false;
}

static int c3_madvise_discard_anon(task_t *cur,uint64_t us,uint64_t ue){
    uint64_t dropped=0;
    int i;
    if(!cur||!cur->addr_space)return -ESRCH;
    for(i=0;i<MMAP_REGION_MAX;++i){
        uint64_t rs,re,os,oe,pg;
        if(!g_mmap_table[i].used||g_mmap_table[i].as!=cur->addr_space)continue;
        if(!c3_mmap_is_lazy_anon(&g_mmap_table[i]))continue;
        if(!c3_mmap_is_private(g_mmap_table[i].flags))continue;
        rs=g_mmap_table[i].virt;
        re=rs+g_mmap_table[i].size;
        if(ue<=rs||us>=re)continue;
        os=(us>rs)?us:rs;
        oe=(ue<re)?ue:re;
        for(pg=os;pg<oe;pg+=PAGE_SIZE){
            uint64_t entry=paging_get_entry(cur->addr_space,pg);
            if(!(entry&PAGE_PRESENT))continue;
            if(!(entry&PAGE_USER))continue;
            if(entry&0x080ULL)continue;
            c3_mmap_unmap_pages(cur->addr_space,pg,PAGE_SIZE,true);
            ++dropped;
        }
    }
    if(dropped){
        static uint32_t trace_count=0;
        if(trace_count<64){
            ++trace_count;
            __boot_serial_puts("[madvise-discard] pid=");
            __boot_serial_putu32((uint32_t)cur->pid);
            c3_trace_task_name(cur);
            __boot_serial_puts(" range=");
            __boot_serial_puthex64(us);
            __boot_serial_puts("-");
            __boot_serial_puthex64(ue);
            __boot_serial_puts(" pages=");
            __boot_serial_putu32((uint32_t)dropped);
            __boot_serial_puts("\n");
        }
    }
    return 0;
}

int64_t real_sys_madvise(uint64_t addr,uint64_t len,int advice){
    task_t *cur=task_current();
    uint64_t us,ue;
    if(!cur||!cur->addr_space)return -ESRCH;
    if(!len)return 0;
    if(addr&(PAGE_SIZE-1))return -EINVAL;
    if(addr>=0x0000800000000000ULL)return -ENOMEM;
    if(len>0x0000800000000000ULL-addr)return -ENOMEM;
    us=addr;
    ue=(addr+len+PAGE_SIZE-1)&~(PAGE_SIZE-1ULL);
    switch(advice){
        case C3_MADV_DONTNEED:
            return c3_madvise_discard_anon(cur,us,ue);
        case C3_MADV_FREE:
            /* MADV_FREE is lazy on Linux: contents remain usable until real
             * memory pressure reclaims them. Keep pages intact during browser
             * bring-up; eager discard can invalidate allocator metadata. */
            return 0;
        case C3_MADV_NORMAL:
        case C3_MADV_RANDOM:
        case C3_MADV_SEQUENTIAL:
        case C3_MADV_WILLNEED:
        case C3_MADV_DONTFORK:
        case C3_MADV_DOFORK:
        case C3_MADV_MERGEABLE:
        case C3_MADV_UNMERGEABLE:
        case C3_MADV_HUGEPAGE:
        case C3_MADV_NOHUGEPAGE:
        case C3_MADV_DONTDUMP:
        case C3_MADV_DODUMP:
        case C3_MADV_WIPEONFORK:
        case C3_MADV_KEEPONFORK:
        case C3_MADV_COLD:
        case C3_MADV_PAGEOUT:
            return 0;
        case C3_MADV_REMOVE:
        default:
            return -EINVAL;
    }
}
int64_t real_sys_msync(uint64_t a,uint64_t l,int f){
    task_t *cur=task_current();
    uint64_t us,ue;
    bool touched=false;
    int i;
    (void)f;
    if(!cur||!cur->addr_space)return -ESRCH;
    if(!l||(a&(PAGE_SIZE-1)))return -EINVAL;
    us=a;
    ue=(a+l+PAGE_SIZE-1)&~(PAGE_SIZE-1);
    for(i=0;i<MMAP_REGION_MAX;++i){
        uint64_t rs,re,os,oe,pg;
        if(!g_mmap_table[i].used||g_mmap_table[i].as!=cur->addr_space)continue;
        rs=g_mmap_table[i].virt;
        re=rs+g_mmap_table[i].size;
        if(ue<=rs||us>=re)continue;
        os=(us>rs)?us:rs;
        oe=(ue<re)?ue:re;
        if(oe<=os)continue;
        for(pg=os;pg<oe;pg+=PAGE_SIZE)paging_flush_tlb(pg);
        c3_mmap_writeback_vfs_range(cur,&g_mmap_table[i],os,oe-os);
        touched=true;
    }
    return touched?0:-ENOMEM;
}

static int c3_proc_self_maps(char *buf,int mx,task_t *cur){
    size_t l=0;
    int i,entries=0;
    if(!buf||mx<=0)return 0;
    buf[0]=0;
    if(!cur)return 0;
    for(i=0;i<MMAP_REGION_MAX;++i){
        uint64_t s,e;
        char perms[5];
        if(!g_mmap_table[i].used||g_mmap_table[i].as!=cur->addr_space)continue;
        s=g_mmap_table[i].virt;
        e=s+g_mmap_table[i].size;
        c3_mmap_perm_text(g_mmap_table[i].prot,g_mmap_table[i].flags,perms);
        c3_append_hex64(buf,&l,(size_t)mx,s);c3_append_ch(buf,&l,(size_t)mx,'-');c3_append_hex64(buf,&l,(size_t)mx,e);
        c3_append_str(buf,&l,(size_t)mx," ");c3_append_str(buf,&l,(size_t)mx,perms);
        c3_append_str(buf,&l,(size_t)mx," ");c3_append_hex64(buf,&l,(size_t)mx,g_mmap_table[i].offset);
        c3_append_str(buf,&l,(size_t)mx," 00:00 0 ");
        if(g_mmap_table[i].backing_path[0])c3_append_str(buf,&l,(size_t)mx,g_mmap_table[i].backing_path);
        else if(g_mmap_table[i].fd>=0)c3_append_str(buf,&l,(size_t)mx,"[fd]");
        else c3_append_str(buf,&l,(size_t)mx,"[anon]");
        c3_append_ch(buf,&l,(size_t)mx,'\n');
        ++entries;
    }
    if(cur->brk_start&&cur->brk_current>cur->brk_start){
        c3_append_hex64(buf,&l,(size_t)mx,cur->brk_start);c3_append_ch(buf,&l,(size_t)mx,'-');c3_append_hex64(buf,&l,(size_t)mx,cur->brk_current);
        c3_append_str(buf,&l,(size_t)mx," rw-p 0000000000000000 00:00 0 [heap]\n");
        ++entries;
    }
    {
        uint64_t ss=C3_USER_STACK_TOP-TASK_USER_STACK;
        c3_append_hex64(buf,&l,(size_t)mx,ss);
        c3_append_ch(buf,&l,(size_t)mx,'-');
        c3_append_hex64(buf,&l,(size_t)mx,C3_USER_STACK_TOP);
        c3_append_str(buf,&l,(size_t)mx," rw-p 0000000000000000 00:00 0 [stack]\n");
        ++entries;
    }
    if(!entries)c3_append_str(buf,&l,(size_t)mx,"(no mappings)\n");
    return(int)l;
}

/* ELF64 real mapper */
static uint32_t c3_elf64_r_type(uint64_t info){return(uint32_t)(info&0xFFFFFFFFu);}
static uint32_t c3_elf64_r_sym(uint64_t info){return(uint32_t)(info>>32);}
static uint8_t c3_elf64_st_bind(uint8_t info){return(uint8_t)(info>>4);}
#define C3_STB_GLOBAL 1
#define C3_STB_WEAK 2
#define C3_STB_GNU_UNIQUE 10

static const char *c3_basename(const char *p){
    const char *n=p,*s=p;
    if(!p)return "";
    while(*s){if(*s=='/')n=s+1;++s;}
    return n;
}

void compat3_set_next_image_name(const char *path){
    if(path&&*path)c3_strlcpy(g_c3_next_image_name,path,sizeof(g_c3_next_image_name));
    else g_c3_next_image_name[0]=0;
}

static bool c3_dyn_name_match(const char *a,const char *b){
    if(!a||!b||!*a||!*b)return false;
    if(c3_strcmp(a,b)==0)return true;
    return c3_strcmp(c3_basename(a),c3_basename(b))==0;
}

static c3_dynobj_t *c3_dynobj_find_by_name(const char *name){
    int i;
    if(!name||!*name)return 0;
    for(i=0;i<C3_DYNOBJ_MAX;++i){
        if(!g_c3_dynobjs[i].used)continue;
        if(c3_dyn_name_match(g_c3_dynobjs[i].name,name))return &g_c3_dynobjs[i];
    }
    return 0;
}

static bool c3_dynobj_exists(const char *name){
    return c3_dynobj_find_by_name(name)!=0;
}

static c3_dynobj_t *c3_dynobj_alloc(void){
    int i;
    for(i=0;i<C3_DYNOBJ_MAX;++i){
        if(!g_c3_dynobjs[i].used){
            c3_memset(&g_c3_dynobjs[i],0,sizeof(g_c3_dynobjs[i]));
            g_c3_dynobjs[i].used=true;
            return &g_c3_dynobjs[i];
        }
    }
    return 0;
}

static bool c3_dyn_parse_info(uint64_t load_bias,uint64_t dyn_vaddr,uint64_t dyn_memsz,c3_dyn_info_t *di){
    const elf64_dyn_t *dyn;
    uint64_t i,dn;
    if(!di||!dyn_vaddr||!dyn_memsz)return false;
    c3_memset(di,0,sizeof(*di));
    di->present=true;
    di->syment=sizeof(elf64_sym_t);
    dyn=(const elf64_dyn_t*)(uintptr_t)dyn_vaddr;
    dn=dyn_memsz/sizeof(elf64_dyn_t);
    for(i=0;i<dn;++i){
        int64_t tag=dyn[i].d_tag;
        uint64_t val=dyn[i].d_val;
        if(tag==C3_DT_NULL)break;
        switch(tag){
            case C3_DT_RELA: di->rela_va=load_bias+val; break;
            case C3_DT_RELASZ: di->rela_sz=val; break;
            case C3_DT_RELAENT: di->rela_ent=val; break;
            case C3_DT_REL: di->rel_va=load_bias+val; break;
            case C3_DT_RELSZ: di->rel_sz=val; break;
            case C3_DT_RELENT: di->rel_ent=val; break;
            case C3_DT_RELR: di->relr_va=load_bias+val; break;
            case C3_DT_RELRSZ: di->relr_sz=val; break;
            case C3_DT_RELRENT: di->relr_ent=val; break;
            case C3_DT_JMPREL: di->jmprel_va=load_bias+val; break;
            case C3_DT_PLTRELSZ: di->pltrelsz=val; break;
            case C3_DT_PLTREL: di->pltrel=val; break;
            case C3_DT_SYMTAB: di->symtab_va=load_bias+val; break;
            case C3_DT_STRTAB: di->strtab_va=load_bias+val; break;
            case C3_DT_SYMENT: di->syment=val; break;
            case C3_DT_STRSZ: di->strsz=val; break;
            case C3_DT_VERSYM: di->versym_va=load_bias+val; break;
            case C3_DT_VERDEF: di->verdef_va=load_bias+val; break;
            case C3_DT_VERDEFNUM: di->verdef_num=val; break;
            case C3_DT_VERNEED: di->verneed_va=load_bias+val; break;
            case C3_DT_VERNEEDNUM: di->verneed_num=val; break;
            case C3_DT_HASH: di->hash_va=load_bias+val; break;
            case C3_DT_GNU_HASH: di->gnu_hash_va=load_bias+val; break;
            case C3_DT_SONAME: di->soname_off=val; break;
            case C3_DT_RPATH: di->rpath_off=val; break;
            case C3_DT_RUNPATH: di->runpath_off=val; break;
            case C3_DT_NEEDED:
                if(di->needed_count<C3_DYN_NEEDED_MAX){
                    di->needed_offs[di->needed_count++]=val;
                }
                break;
            default: break;
        }
    }
    return true;
}

static uint32_t c3_dyn_gnu_symbol_count(const c3_dyn_info_t *di){
    const uint32_t *gh;
    const uint32_t *buckets;
    const uint32_t *chains;
    uint32_t nbuckets,symoffset,bloom_size;
    uint32_t i,maxidx=0;
    if(!di||!di->gnu_hash_va)return 0;
    gh=(const uint32_t*)(uintptr_t)di->gnu_hash_va;
    nbuckets=gh[0];
    symoffset=gh[1];
    bloom_size=gh[2];
    if(!nbuckets||nbuckets>65536||bloom_size>65536)return 0;
    buckets=gh+4+bloom_size*2; /* ELF64 bloom is uint64_t[] */
    chains=buckets+nbuckets;
    maxidx=symoffset;
    for(i=0;i<nbuckets;++i){
        uint32_t sym=buckets[i];
        uint32_t guard=0;
        if(sym<symoffset)continue;
        if(sym>maxidx)maxidx=sym;
        while(guard<131072){
            uint32_t chain=chains[sym-symoffset];
            if(sym>maxidx)maxidx=sym;
            ++guard;
            if(chain&1U)break;
            ++sym;
        }
    }
    if(maxidx>0&&maxidx<131072)return maxidx+1;
    return 0;
}

static uint32_t c3_dyn_symbol_count(const c3_dyn_info_t *di){
    uint32_t n=0;
    if(!di||!di->symtab_va||!di->strtab_va)return 0;
    if(di->gnu_hash_va){
        uint32_t g=c3_dyn_gnu_symbol_count(di);
        if(g)n=g;
    }
    if(di->hash_va){
        const uint32_t *h=(const uint32_t*)(uintptr_t)di->hash_va;
        uint32_t nchain=h[1];
        if(nchain>0&&nchain<131072&&nchain>n)n=nchain;
    }
    if(!n)n=4096;
    return n;
}

static bool c3_dyn_lookup_tls_symbol_global(const char *name,uint32_t *modid,uint64_t *st_value){
    int i;
    if(!name||!*name)return false;
    for(i=0;i<C3_DYNOBJ_MAX;++i){
        c3_dynobj_t *o=&g_c3_dynobjs[i];
        uint32_t s,max;
        if(!o->used||!o->symtab_va||!o->strtab_va)continue;
        max=o->symbol_count?o->symbol_count:4096;
        for(s=1;s<max;++s){
            const elf64_sym_t *sym=(const elf64_sym_t*)(uintptr_t)(o->symtab_va+(uint64_t)s*o->syment);
            const char *sname;
            if(sym->st_shndx==0)continue;
            if(o->strsz&&sym->st_name>=o->strsz)continue;
            sname=(const char*)(uintptr_t)(o->strtab_va+sym->st_name);
            if(!sname||!*sname)continue;
            if(c3_strcmp(sname,name)==0){
                if(modid)*modid=o->tls_module_id?o->tls_module_id:1;
                if(st_value)*st_value=sym->st_value;
                return true;
            }
        }
    }
    return false;
}

static uint16_t c3_dyn_register_object(const char *name,uint64_t load_bias,uint64_t base,uint64_t end,uint64_t phdr_va,uint16_t phnum,const c3_dyn_info_t *di,uint64_t tls_init_va,uint64_t tls_filesz,uint64_t tls_memsz,uint64_t tls_align){
    c3_dynobj_t *o;
    if(!di||!di->symtab_va||!di->strtab_va)return 0;
    if(name&&*name){
        c3_dynobj_t *e=c3_dynobj_find_by_name(name);
        if(e)return e->tls_module_id;
    }
    o=c3_dynobj_alloc();
    if(!o)return 0;
    if(name&&*name)c3_strlcpy(o->name,name,sizeof(o->name));
    else c3_strlcpy(o->name,"(anon)",sizeof(o->name));
    o->load_bias=load_bias;
    o->base=base;
    o->end=end;
    o->phdr_va=phdr_va;
    o->phnum=phnum;
    o->symtab_va=di->symtab_va;
    o->strtab_va=di->strtab_va;
    o->syment=di->syment?di->syment:sizeof(elf64_sym_t);
    o->strsz=di->strsz;
    o->symbol_count=c3_dyn_symbol_count(di);
    o->versym_va=di->versym_va;
    o->verdef_va=di->verdef_va;
    o->verdef_num=(uint32_t)di->verdef_num;
    o->tls_init_va=tls_init_va;
    o->tls_filesz=tls_filesz;
    o->tls_memsz=tls_memsz;
    o->tls_align=tls_align?tls_align:16;
    if(tls_memsz&&g_c3_tls_next_module<C3_TLS_MODULE_MAX){
        o->tls_module_id=g_c3_tls_next_module++;
    }else if(tls_memsz){
        o->tls_module_id=1;
    }else{
        o->tls_module_id=0;
    }
    ++g_c3_dyn_objects;
    return o->tls_module_id;
}

static uint16_t c3_dyn_sym_ver_index(const c3_dyn_info_t *di,uint32_t symidx){
    const uint16_t *vs;
    if(!di||!di->versym_va||!symidx)return 0;
    vs=(const uint16_t*)(uintptr_t)(di->versym_va+(uint64_t)symidx*sizeof(uint16_t));
    return (uint16_t)(*vs&0x7FFFu);
}

static const char *c3_dyn_verneed_name_by_index(const c3_dyn_info_t *di,uint16_t veridx){
    const uint8_t *vn_ptr;
    uint32_t vn_i=0;
    if(!di||veridx<=1||!di->verneed_va||!di->verneed_num||!di->strtab_va)return 0;
    vn_ptr=(const uint8_t*)(uintptr_t)di->verneed_va;
    while(vn_i<(uint32_t)di->verneed_num&&vn_i<4096u){
        const c3_elf64_verneed_t *vn=(const c3_elf64_verneed_t*)(const void*)vn_ptr;
        const uint8_t *aux_ptr=vn_ptr+vn->vn_aux;
        uint32_t aux_i=0;
        while(aux_i<(uint32_t)vn->vn_cnt&&aux_i<4096u){
            const c3_elf64_vernaux_t *vna=(const c3_elf64_vernaux_t*)(const void*)aux_ptr;
            uint16_t idx=(uint16_t)(vna->vna_other&0x7FFFu);
            if(idx==veridx){
                if(di->strsz&&vna->vna_name>=di->strsz)return 0;
                return (const char*)(uintptr_t)(di->strtab_va+vna->vna_name);
            }
            if(!vna->vna_next)break;
            aux_ptr+=vna->vna_next;
            ++aux_i;
        }
        if(!vn->vn_next)break;
        vn_ptr+=vn->vn_next;
        ++vn_i;
    }
    return 0;
}

static const char *c3_dyn_verdef_name_by_index(const c3_dynobj_t *o,uint16_t veridx){
    const uint8_t *vd_ptr;
    uint32_t vd_i=0;
    if(!o||veridx<=1||!o->verdef_va||!o->verdef_num||!o->strtab_va)return 0;
    vd_ptr=(const uint8_t*)(uintptr_t)o->verdef_va;
    while(vd_i<o->verdef_num&&vd_i<4096u){
        const c3_elf64_verdef_t *vd=(const c3_elf64_verdef_t*)(const void*)vd_ptr;
        uint16_t idx=(uint16_t)(vd->vd_ndx&0x7FFFu);
        if(idx==veridx){
            const c3_elf64_verdaux_t *aux=(const c3_elf64_verdaux_t*)(const void*)(vd_ptr+vd->vd_aux);
            if(o->strsz&&aux->vda_name>=o->strsz)return 0;
            return (const char*)(uintptr_t)(o->strtab_va+aux->vda_name);
        }
        if(!vd->vd_next)break;
        vd_ptr+=vd->vd_next;
        ++vd_i;
    }
    return 0;
}

static uint64_t c3_dyn_lookup_in_obj(const c3_dynobj_t *o,const char *name,const char *req_ver,bool *is_weak){
    uint32_t i,max;
    uint64_t weak_candidate=0;
    if(is_weak)*is_weak=false;
    if(!o||!name||!*name||!o->symtab_va||!o->strtab_va)return 0;
    max=o->symbol_count?o->symbol_count:4096;
    for(i=1;i<max;++i){
        const elf64_sym_t *sym=(const elf64_sym_t*)(uintptr_t)(o->symtab_va+(uint64_t)i*o->syment);
        const char *sname,*prov_ver=0;
        uint8_t bind;
        uint16_t raw_ver=0;
        uint16_t ver_idx=0;
        bool hidden=false;
        if(sym->st_shndx==0)continue;
        if(o->strsz&&sym->st_name>=o->strsz)continue;
        sname=(const char*)(uintptr_t)(o->strtab_va+sym->st_name);
        if(!sname||!*sname)continue;
        if(c3_strcmp(sname,name)!=0)continue;
        if(o->versym_va){
            raw_ver=*(const uint16_t*)(uintptr_t)(o->versym_va+(uint64_t)i*sizeof(uint16_t));
            ver_idx=(uint16_t)(raw_ver&0x7FFFu);
            hidden=(raw_ver&0x8000u)!=0;
            prov_ver=c3_dyn_verdef_name_by_index(o,ver_idx);
        }
        if(req_ver&&*req_ver){
            if(ver_idx<=1||!prov_ver||c3_strcmp(prov_ver,req_ver)!=0)continue;
        }else if(hidden){
            continue;
        }
        bind=c3_elf64_st_bind(sym->st_info);
        if(bind==C3_STB_WEAK){
            if(!weak_candidate)weak_candidate=o->load_bias+sym->st_value;
            continue;
        }
        if(bind==C3_STB_GLOBAL||bind==C3_STB_GNU_UNIQUE||bind==0){
            if(is_weak)*is_weak=false;
            return o->load_bias+sym->st_value;
        }
    }
    if(weak_candidate){
        if(is_weak)*is_weak=true;
        return weak_candidate;
    }
    return 0;
}

static uint64_t c3_dyn_lookup_global(const char *name){
    int i;
    uint64_t weak_candidate=0;
    for(i=0;i<C3_DYNOBJ_MAX;++i){
        uint64_t v;
        bool is_weak=false;
        if(!g_c3_dynobjs[i].used)continue;
        v=c3_dyn_lookup_in_obj(&g_c3_dynobjs[i],name,0,&is_weak);
        if(!v)continue;
        if(!is_weak)return v;
        if(!weak_candidate)weak_candidate=v;
    }
    return weak_candidate;
}

static uint64_t c3_dyn_lookup_global_versioned(const char *name,const char *req_ver){
    int i;
    uint64_t weak_candidate=0;
    for(i=0;i<C3_DYNOBJ_MAX;++i){
        uint64_t v;
        bool is_weak=false;
        if(!g_c3_dynobjs[i].used)continue;
        v=c3_dyn_lookup_in_obj(&g_c3_dynobjs[i],name,req_ver,&is_weak);
        if(!v)continue;
        if(!is_weak)return v;
        if(!weak_candidate)weak_candidate=v;
    }
    return weak_candidate;
}

uint64_t compat3_lookup_global_symbol(const char *name){
    return c3_dyn_lookup_global(name);
}

static bool c3_find_shared_object(const char *name,const char *origin_dir,const char *runpath,const uint8_t **data,uint32_t *sz,char *resolved,size_t cap){
    static const char *dirs[]={
        "/opt/firefox/","/opt/firefox/browser/",
        "/opt/chromium/","/opt/google/chrome/",
        "/lib64/","/lib/","/usr/lib64/","/usr/lib/",
        "/lib/x86_64-linux-gnu/","/usr/lib/x86_64-linux-gnu/",
        "/usr/local/lib/"
    };
    int i;
    char p[VFS_PATH_MAX];
    size_t l;
    if(!name||!*name)return false;
    if(name[0]=='/'&&kvfs_read(name,data,sz)){if(resolved)c3_strlcpy(resolved,name,cap);return true;}
    if(runpath&&*runpath){
        size_t pos=0;
        while(runpath[pos]){
            char token[VFS_PATH_MAX];
            char base[VFS_PATH_MAX];
            size_t tlen=0,bl=0,j=pos;
            token[0]=0;
            base[0]=0;
            while(runpath[j]&&runpath[j]!=':'&&tlen+1<sizeof(token))token[tlen++]=runpath[j++];
            token[tlen]=0;
            if(runpath[j]==':')++j;
            pos=j;
            if(!token[0])continue;
            if(c3_strncmp(token,"$ORIGIN",7)==0&&origin_dir&&*origin_dir){
                c3_append_str(base,&bl,sizeof(base),origin_dir);
                c3_append_str(base,&bl,sizeof(base),token+7);
            }else if(token[0]=='/'){
                c3_strlcpy(base,token,sizeof(base));
            }else if(origin_dir&&*origin_dir){
                c3_append_str(base,&bl,sizeof(base),origin_dir);
                if(bl&&base[bl-1]!='/')c3_append_ch(base,&bl,sizeof(base),'/');
                c3_append_str(base,&bl,sizeof(base),token);
            }else{
                c3_strlcpy(base,token,sizeof(base));
            }
            l=0;p[0]=0;
            c3_append_str(p,&l,sizeof(p),base);
            if(l&&p[l-1]!='/')c3_append_ch(p,&l,sizeof(p),'/');
            c3_append_str(p,&l,sizeof(p),name);
            if(kvfs_read(p,data,sz)){
                if(resolved)c3_strlcpy(resolved,p,cap);
                return true;
            }
        }
    }
    if(origin_dir&&*origin_dir){
        l=0;p[0]=0;
        c3_append_str(p,&l,sizeof(p),origin_dir);
        if(l&&p[l-1]!='/')c3_append_ch(p,&l,sizeof(p),'/');
        c3_append_str(p,&l,sizeof(p),name);
        if(kvfs_read(p,data,sz)){
            if(resolved)c3_strlcpy(resolved,p,cap);
            return true;
        }
    }
    for(i=0;i<(int)(sizeof(dirs)/sizeof(dirs[0]));++i){
        l=0;p[0]=0;
        c3_append_str(p,&l,sizeof(p),dirs[i]);
        c3_append_str(p,&l,sizeof(p),name);
        if(kvfs_read(p,data,sz)){
            if(resolved)c3_strlcpy(resolved,p,cap);
            return true;
        }
    }
    return false;
}

static int c3_load_needed_objects(task_t *t,const c3_dyn_info_t *di,const char *origin_name,uint64_t deadline,c3_dyn_profile_t *prof){
    uint32_t i;
    int rc=0;
    const char *runpath=0;
    uint64_t t0=c3_rdtsc();
    char origin_dir[VFS_PATH_MAX];
    origin_dir[0]=0;
    if(!di||!di->needed_count||!di->strtab_va)return 0;
    if(origin_name&&*origin_name)c3_path_dirname(origin_name,origin_dir,sizeof(origin_dir));
    if(di->runpath_off&&(!di->strsz||di->runpath_off<di->strsz))
        runpath=(const char*)(uintptr_t)(di->strtab_va+di->runpath_off);
    else if(di->rpath_off&&(!di->strsz||di->rpath_off<di->strsz))
        runpath=(const char*)(uintptr_t)(di->strtab_va+di->rpath_off);
    if(g_c3_dyn_load_depth>=32)return -EAGAIN;
    ++g_c3_dyn_load_depth;
    for(i=0;i<di->needed_count;++i){
        const char *nm;
        const uint8_t *lib_data=0;
        uint32_t lib_sz=0;
        int lrc;
        char resolved[VFS_PATH_MAX];
        uint64_t off=di->needed_offs[i];
        if(c3_dyn_deadline_expired(deadline)){
            c3_dyn_note_timeout(origin_name,"needed",prof);
            rc=-ETIMEDOUT;
            break;
        }
        if(di->strsz&&off>=di->strsz)continue;
        nm=(const char*)(uintptr_t)(di->strtab_va+off);
        if(!nm||!*nm)continue;
        if(c3_dynobj_exists(nm))continue;
        if(!c3_find_shared_object(nm,origin_dir,runpath,&lib_data,&lib_sz,resolved,sizeof(resolved))){
            ++g_c3_dyn_needed_missing;
            if(prof)++prof->needed_missing;
            continue;
        }
        if(c3_dynobj_exists(resolved)||c3_dynobj_exists(c3_basename(resolved)))continue;
        if(!elf64_validate(lib_data,lib_sz)){
            ++g_c3_dyn_needed_missing;
            if(prof)++prof->needed_missing;
            continue;
        }
        compat3_set_next_image_name(resolved);
        lrc=elf64_map_into_task(t,lib_data,lib_sz);
        if(lrc<0){
            if(lrc==-ETIMEDOUT){
                c3_dyn_note_timeout(resolved,"needed-map",prof);
                rc=-ETIMEDOUT;
                break;
            }
            ++g_c3_dyn_needed_missing;
            if(prof)++prof->needed_missing;
            continue;
        }
        ++g_c3_dyn_needed_loaded;
        if(prof)++prof->needed_loaded;
    }
    --g_c3_dyn_load_depth;
    {
        uint64_t dt=c3_rdtsc()-t0;
        c3_dyn_profile_add_cycle(&g_c3_dyn_cycle_needed,dt);
        if(prof)c3_dyn_profile_add_cycle(&prof->cycle_needed,dt);
    }
    return rc;
}

static uint64_t c3_dyn_resolve_symbol(uint64_t load_bias,const c3_dyn_info_t *di,uint32_t symidx){
    const elf64_sym_t *sym;
    const char *name;
    const char *req_ver=0;
    void *resolved;
    uint64_t globalv;
    uint64_t syment;
    if(!di||!symidx||!di->symtab_va||!di->strtab_va)return 0;
    syment=di->syment?di->syment:sizeof(elf64_sym_t);
    sym=(const elf64_sym_t*)(uintptr_t)(di->symtab_va+(uint64_t)symidx*syment);
    if(sym->st_shndx!=0)return load_bias+sym->st_value;
    if(di->strsz&&sym->st_name>=di->strsz)return 0;
    name=(const char*)(uintptr_t)(di->strtab_va+sym->st_name);
    if(!name||!*name)return 0;
    req_ver=c3_dyn_verneed_name_by_index(di,c3_dyn_sym_ver_index(di,symidx));
    globalv=c3_dyn_lookup_global_versioned(name,req_ver);
    if(!globalv&&req_ver&&*req_ver)globalv=c3_dyn_lookup_global(name);
    if(globalv)return globalv;
    resolved=ulibc_dlsym((void*)1,name);
    return resolved?(uint64_t)(uintptr_t)resolved:0;
}

static const elf64_sym_t *c3_dyn_sym_ptr(const c3_dyn_info_t *di,uint32_t symidx){
    uint64_t syment;
    if(!di||!di->symtab_va||!symidx)return 0;
    syment=di->syment?di->syment:sizeof(elf64_sym_t);
    return (const elf64_sym_t*)(uintptr_t)(di->symtab_va+(uint64_t)symidx*syment);
}

typedef uint64_t(*c3_ifunc_resolver_t)(void);

static uint64_t c3_dyn_resolve_ifunc(uint64_t resolver_va){
    task_t *cur=task_current();
    uint64_t entry;
    if(!resolver_va)return 0;
    if(!cur||!cur->addr_space)return resolver_va;
    entry=paging_get_entry(cur->addr_space,resolver_va);
    if(!(entry&PAGE_PRESENT)||(entry&PAGE_NX))return resolver_va;
    return ((c3_ifunc_resolver_t)(uintptr_t)resolver_va)();
}

static void c3_dyn_reloc_note_skipped(uint64_t skipped){
    uint32_t add;
    if(!skipped)return;
    add=(skipped>0xFFFFFFFFULL)?0xFFFFFFFFu:(uint32_t)skipped;
    if(g_c3_dyn_reloc_unsupported>0xFFFFFFFFu-add)g_c3_dyn_reloc_unsupported=0xFFFFFFFFu;
    else g_c3_dyn_reloc_unsupported+=add;
}

static int c3_apply_relr_table(uint64_t load_bias,uint64_t relr_va,uint64_t relr_sz,uint64_t relr_ent,uint64_t deadline,const char *obj_name,c3_dyn_profile_t *prof){
    const uint64_t *tbl;
    uint64_t n,i;
    uint64_t *cursor=0;
    if(!relr_va||!relr_sz)return 0;
    if(!relr_ent)relr_ent=sizeof(uint64_t);
    if(relr_ent!=sizeof(uint64_t)){++g_c3_dyn_reloc_unsupported;return 0;}
    tbl=(const uint64_t*)(uintptr_t)relr_va;
    n=relr_sz/relr_ent;
    if(n>C3_DYN_RELOC_MAX_ENTRIES){
        c3_dyn_reloc_note_skipped(n-C3_DYN_RELOC_MAX_ENTRIES);
        n=C3_DYN_RELOC_MAX_ENTRIES;
    }
    for(i=0;i<n;++i){
        uint64_t e=tbl[i];
        if((i&63ULL)==0ULL&&c3_dyn_deadline_expired(deadline)){
            c3_dyn_note_timeout(obj_name,"relr",prof);
            return -ETIMEDOUT;
        }
        if((e&1ULL)==0){
            uint64_t *where=(uint64_t*)(uintptr_t)(load_bias+e);
            *where+=load_bias;
            ++g_c3_dyn_reloc_applied;
            cursor=where+1;
            continue;
        }
        if(cursor){
            uint64_t bits=e>>1;
            uint64_t b;
            for(b=0;b<63;++b){
                if(bits&(1ULL<<b)){
                    cursor[b]+=load_bias;
                    ++g_c3_dyn_reloc_applied;
                }
            }
            cursor+=63;
        }else{
            ++g_c3_dyn_reloc_unsupported;
        }
    }
    return 0;
}

static int c3_apply_rela_table(uint64_t load_bias,uint64_t rela_va,uint64_t rela_sz,uint64_t rela_ent,const c3_dyn_info_t *di,uint16_t tls_module_id,uint64_t deadline,const char *obj_name,c3_dyn_profile_t *prof){
    uint64_t i,n;
    if(!rela_va||!rela_sz)return 0;
    if(!rela_ent)rela_ent=sizeof(c3_elf64_rela_t);
    n=rela_sz/rela_ent;
    if(n>C3_DYN_RELOC_MAX_ENTRIES){
        c3_dyn_reloc_note_skipped(n-C3_DYN_RELOC_MAX_ENTRIES);
        n=C3_DYN_RELOC_MAX_ENTRIES;
    }
    for(i=0;i<n;++i){
        const c3_elf64_rela_t *r=(const c3_elf64_rela_t*)(uintptr_t)(rela_va+i*rela_ent);
        uint64_t *where=(uint64_t*)(uintptr_t)(load_bias+r->r_offset);
        uint32_t type=c3_elf64_r_type(r->r_info);
        uint32_t sym=c3_elf64_r_sym(r->r_info);
        const elf64_sym_t *se=c3_dyn_sym_ptr(di,sym);
        const char *tls_name=0;
        uint64_t symv=0;
        uint64_t tls_st_value=0;
        uint32_t tls_mod=(tls_module_id?tls_module_id:1);
        if((i&63ULL)==0ULL&&c3_dyn_deadline_expired(deadline)){
            c3_dyn_note_timeout(obj_name,"rela",prof);
            return -ETIMEDOUT;
        }
        if(sym&&se&&di&&di->strtab_va){
            if(!di->strsz||se->st_name<di->strsz)tls_name=(const char*)(uintptr_t)(di->strtab_va+se->st_name);
        }
        switch(type){
            case C3_R_X86_64_NONE: break;
            case C3_R_X86_64_RELATIVE:
                *where=load_bias+(uint64_t)r->r_addend;
                ++g_c3_dyn_reloc_applied;
                break;
            case C3_R_X86_64_GLOB_DAT:
            case C3_R_X86_64_JUMP_SLOT:
            case C3_R_X86_64_64:
                symv=c3_dyn_resolve_symbol(load_bias,di,sym);
                if(symv){
                    *where=symv+(uint64_t)r->r_addend;
                    ++g_c3_dyn_reloc_applied;
                }else if(se&&c3_elf64_st_bind(se->st_info)==C3_STB_WEAK){
                    *where=(uint64_t)r->r_addend;
                    ++g_c3_dyn_reloc_applied;
                }else{
                    ++g_c3_dyn_reloc_failed;
                }
                break;
            case C3_R_X86_64_COPY:
                symv=c3_dyn_resolve_symbol(load_bias,di,sym);
                if(symv&&se&&se->st_size){
                    c3_memcpy(where,(const void*)(uintptr_t)symv,(size_t)se->st_size);
                    ++g_c3_dyn_reloc_applied;
                }else if(se&&c3_elf64_st_bind(se->st_info)==C3_STB_WEAK){
                    ++g_c3_dyn_reloc_applied;
                }else{
                    ++g_c3_dyn_reloc_failed;
                }
                break;
            case C3_R_X86_64_32:
                symv=c3_dyn_resolve_symbol(load_bias,di,sym);
                if(symv){
                    *(uint32_t*)(void*)where=(uint32_t)(symv+(uint64_t)r->r_addend);
                    ++g_c3_dyn_reloc_applied;
                }else if(se&&c3_elf64_st_bind(se->st_info)==C3_STB_WEAK){
                    *(uint32_t*)(void*)where=(uint32_t)r->r_addend;
                    ++g_c3_dyn_reloc_applied;
                }else{
                    ++g_c3_dyn_reloc_failed;
                }
                break;
            case C3_R_X86_64_32S:
                symv=c3_dyn_resolve_symbol(load_bias,di,sym);
                if(symv){
                    *(int32_t*)(void*)where=(int32_t)(symv+(uint64_t)r->r_addend);
                    ++g_c3_dyn_reloc_applied;
                }else if(se&&c3_elf64_st_bind(se->st_info)==C3_STB_WEAK){
                    *(int32_t*)(void*)where=(int32_t)r->r_addend;
                    ++g_c3_dyn_reloc_applied;
                }else{
                    ++g_c3_dyn_reloc_failed;
                }
                break;
            case C3_R_X86_64_DTPMOD64:
                if(!(se&&se->st_shndx!=0)&&tls_name&&*tls_name){
                    uint32_t found_mod=0;
                    if(c3_dyn_lookup_tls_symbol_global(tls_name,&found_mod,0)&&found_mod)tls_mod=found_mod;
                }
                *where=(uint64_t)tls_mod;
                ++g_c3_dyn_reloc_applied;
                break;
            case C3_R_X86_64_DTPOFF64:
                if(se&&se->st_shndx!=0){
                    *where=se->st_value+(uint64_t)r->r_addend;
                    ++g_c3_dyn_reloc_applied;
                }else if(tls_name&&*tls_name&&c3_dyn_lookup_tls_symbol_global(tls_name,0,&tls_st_value)){
                    *where=tls_st_value+(uint64_t)r->r_addend;
                    ++g_c3_dyn_reloc_applied;
                }else{
                    symv=c3_dyn_resolve_symbol(load_bias,di,sym);
                    if(symv){
                        *where=symv+(uint64_t)r->r_addend;
                        ++g_c3_dyn_reloc_applied;
                    }else{
                        ++g_c3_dyn_reloc_failed;
                    }
                }
                break;
            case C3_R_X86_64_TPOFF64:
                if(se&&se->st_shndx!=0){
                    *where=(uint64_t)(-(int64_t)(se->st_value+(uint64_t)r->r_addend));
                    ++g_c3_dyn_reloc_applied;
                }else if(tls_name&&*tls_name&&c3_dyn_lookup_tls_symbol_global(tls_name,0,&tls_st_value)){
                    *where=(uint64_t)(-(int64_t)(tls_st_value+(uint64_t)r->r_addend));
                    ++g_c3_dyn_reloc_applied;
                }else{
                    symv=c3_dyn_resolve_symbol(load_bias,di,sym);
                    if(symv){
                        *where=symv+(uint64_t)r->r_addend;
                        ++g_c3_dyn_reloc_applied;
                    }else{
                        ++g_c3_dyn_reloc_failed;
                    }
                }
                break;
            case C3_R_X86_64_DTPOFF32:
                if(se&&se->st_shndx!=0){
                    *(uint32_t*)(void*)where=(uint32_t)(se->st_value+(uint64_t)r->r_addend);
                    ++g_c3_dyn_reloc_applied;
                }else if(tls_name&&*tls_name&&c3_dyn_lookup_tls_symbol_global(tls_name,0,&tls_st_value)){
                    *(uint32_t*)(void*)where=(uint32_t)(tls_st_value+(uint64_t)r->r_addend);
                    ++g_c3_dyn_reloc_applied;
                }else{
                    ++g_c3_dyn_reloc_failed;
                }
                break;
            case C3_R_X86_64_TPOFF32:
            case C3_R_X86_64_GOTTPOFF:
                if(se&&se->st_shndx!=0){
                    *(int32_t*)(void*)where=(int32_t)(-(int64_t)(se->st_value+(uint64_t)r->r_addend));
                    ++g_c3_dyn_reloc_applied;
                }else if(tls_name&&*tls_name&&c3_dyn_lookup_tls_symbol_global(tls_name,0,&tls_st_value)){
                    *(int32_t*)(void*)where=(int32_t)(-(int64_t)(tls_st_value+(uint64_t)r->r_addend));
                    ++g_c3_dyn_reloc_applied;
                }else{
                    ++g_c3_dyn_reloc_failed;
                }
                break;
            case C3_R_X86_64_IRELATIVE:
                *where=c3_dyn_resolve_ifunc(load_bias+(uint64_t)r->r_addend);
                ++g_c3_dyn_reloc_applied;
                break;
            case C3_R_X86_64_GOTPCRELX:
            case C3_R_X86_64_REX_GOTPCRELX:
                symv=c3_dyn_resolve_symbol(load_bias,di,sym);
                if(symv){
                    *(int32_t*)(void*)where=(int32_t)(symv+(uint64_t)r->r_addend-(uint64_t)(uintptr_t)where);
                    ++g_c3_dyn_reloc_applied;
                }else if(se&&c3_elf64_st_bind(se->st_info)==C3_STB_WEAK){
                    *(int32_t*)(void*)where=(int32_t)((uint64_t)r->r_addend-(uint64_t)(uintptr_t)where);
                    ++g_c3_dyn_reloc_applied;
                }else{
                    ++g_c3_dyn_reloc_failed;
                }
                break;
            default:
                ++g_c3_dyn_reloc_unsupported;
                break;
        }
    }
    return 0;
}

static int c3_apply_rel_table(uint64_t load_bias,uint64_t rel_va,uint64_t rel_sz,uint64_t rel_ent,const c3_dyn_info_t *di,uint16_t tls_module_id,uint64_t deadline,const char *obj_name,c3_dyn_profile_t *prof){
    uint64_t i,n;
    if(!rel_va||!rel_sz)return 0;
    if(!rel_ent)rel_ent=sizeof(c3_elf64_rel_t);
    n=rel_sz/rel_ent;
    if(n>C3_DYN_RELOC_MAX_ENTRIES){
        c3_dyn_reloc_note_skipped(n-C3_DYN_RELOC_MAX_ENTRIES);
        n=C3_DYN_RELOC_MAX_ENTRIES;
    }
    for(i=0;i<n;++i){
        const c3_elf64_rel_t *r=(const c3_elf64_rel_t*)(uintptr_t)(rel_va+i*rel_ent);
        uint64_t *where=(uint64_t*)(uintptr_t)(load_bias+r->r_offset);
        uint64_t addend=*where;
        uint32_t type=c3_elf64_r_type(r->r_info);
        uint32_t sym=c3_elf64_r_sym(r->r_info);
        const elf64_sym_t *se=c3_dyn_sym_ptr(di,sym);
        const char *tls_name=0;
        uint64_t symv=0;
        uint64_t tls_st_value=0;
        uint32_t tls_mod=(tls_module_id?tls_module_id:1);
        if((i&63ULL)==0ULL&&c3_dyn_deadline_expired(deadline)){
            c3_dyn_note_timeout(obj_name,"rel",prof);
            return -ETIMEDOUT;
        }
        if(sym&&se&&di&&di->strtab_va){
            if(!di->strsz||se->st_name<di->strsz)tls_name=(const char*)(uintptr_t)(di->strtab_va+se->st_name);
        }
        switch(type){
            case C3_R_X86_64_NONE: break;
            case C3_R_X86_64_RELATIVE:
                *where=load_bias+addend;
                ++g_c3_dyn_reloc_applied;
                break;
            case C3_R_X86_64_GLOB_DAT:
            case C3_R_X86_64_JUMP_SLOT:
            case C3_R_X86_64_64:
                symv=c3_dyn_resolve_symbol(load_bias,di,sym);
                if(symv){
                    *where=symv+addend;
                    ++g_c3_dyn_reloc_applied;
                }else if(se&&c3_elf64_st_bind(se->st_info)==C3_STB_WEAK){
                    *where=addend;
                    ++g_c3_dyn_reloc_applied;
                }else{
                    ++g_c3_dyn_reloc_failed;
                }
                break;
            case C3_R_X86_64_COPY:
                symv=c3_dyn_resolve_symbol(load_bias,di,sym);
                if(symv&&se&&se->st_size){
                    c3_memcpy(where,(const void*)(uintptr_t)symv,(size_t)se->st_size);
                    ++g_c3_dyn_reloc_applied;
                }else if(se&&c3_elf64_st_bind(se->st_info)==C3_STB_WEAK){
                    ++g_c3_dyn_reloc_applied;
                }else{
                    ++g_c3_dyn_reloc_failed;
                }
                break;
            case C3_R_X86_64_32:
                symv=c3_dyn_resolve_symbol(load_bias,di,sym);
                if(symv){
                    *(uint32_t*)(void*)where=(uint32_t)(symv+addend);
                    ++g_c3_dyn_reloc_applied;
                }else if(se&&c3_elf64_st_bind(se->st_info)==C3_STB_WEAK){
                    *(uint32_t*)(void*)where=(uint32_t)addend;
                    ++g_c3_dyn_reloc_applied;
                }else{
                    ++g_c3_dyn_reloc_failed;
                }
                break;
            case C3_R_X86_64_32S:
                symv=c3_dyn_resolve_symbol(load_bias,di,sym);
                if(symv){
                    *(int32_t*)(void*)where=(int32_t)(symv+addend);
                    ++g_c3_dyn_reloc_applied;
                }else if(se&&c3_elf64_st_bind(se->st_info)==C3_STB_WEAK){
                    *(int32_t*)(void*)where=(int32_t)addend;
                    ++g_c3_dyn_reloc_applied;
                }else{
                    ++g_c3_dyn_reloc_failed;
                }
                break;
            case C3_R_X86_64_DTPMOD64:
                if(!(se&&se->st_shndx!=0)&&tls_name&&*tls_name){
                    uint32_t found_mod=0;
                    if(c3_dyn_lookup_tls_symbol_global(tls_name,&found_mod,0)&&found_mod)tls_mod=found_mod;
                }
                *where=(uint64_t)tls_mod;
                ++g_c3_dyn_reloc_applied;
                break;
            case C3_R_X86_64_DTPOFF64:
                if(se&&se->st_shndx!=0){
                    *where=se->st_value+addend;
                    ++g_c3_dyn_reloc_applied;
                }else if(tls_name&&*tls_name&&c3_dyn_lookup_tls_symbol_global(tls_name,0,&tls_st_value)){
                    *where=tls_st_value+addend;
                    ++g_c3_dyn_reloc_applied;
                }else{
                    symv=c3_dyn_resolve_symbol(load_bias,di,sym);
                    if(symv){
                        *where=symv+addend;
                        ++g_c3_dyn_reloc_applied;
                    }else{
                        ++g_c3_dyn_reloc_failed;
                    }
                }
                break;
            case C3_R_X86_64_TPOFF64:
                if(se&&se->st_shndx!=0){
                    *where=(uint64_t)(-(int64_t)(se->st_value+addend));
                    ++g_c3_dyn_reloc_applied;
                }else if(tls_name&&*tls_name&&c3_dyn_lookup_tls_symbol_global(tls_name,0,&tls_st_value)){
                    *where=(uint64_t)(-(int64_t)(tls_st_value+addend));
                    ++g_c3_dyn_reloc_applied;
                }else{
                    symv=c3_dyn_resolve_symbol(load_bias,di,sym);
                    if(symv){
                        *where=symv+addend;
                        ++g_c3_dyn_reloc_applied;
                    }else{
                        ++g_c3_dyn_reloc_failed;
                    }
                }
                break;
            case C3_R_X86_64_DTPOFF32:
                if(se&&se->st_shndx!=0){
                    *(uint32_t*)(void*)where=(uint32_t)(se->st_value+addend);
                    ++g_c3_dyn_reloc_applied;
                }else if(tls_name&&*tls_name&&c3_dyn_lookup_tls_symbol_global(tls_name,0,&tls_st_value)){
                    *(uint32_t*)(void*)where=(uint32_t)(tls_st_value+addend);
                    ++g_c3_dyn_reloc_applied;
                }else{
                    ++g_c3_dyn_reloc_failed;
                }
                break;
            case C3_R_X86_64_TPOFF32:
            case C3_R_X86_64_GOTTPOFF:
                if(se&&se->st_shndx!=0){
                    *(int32_t*)(void*)where=(int32_t)(-(int64_t)(se->st_value+addend));
                    ++g_c3_dyn_reloc_applied;
                }else if(tls_name&&*tls_name&&c3_dyn_lookup_tls_symbol_global(tls_name,0,&tls_st_value)){
                    *(int32_t*)(void*)where=(int32_t)(-(int64_t)(tls_st_value+addend));
                    ++g_c3_dyn_reloc_applied;
                }else{
                    ++g_c3_dyn_reloc_failed;
                }
                break;
            case C3_R_X86_64_IRELATIVE:
                *where=c3_dyn_resolve_ifunc(load_bias+addend);
                ++g_c3_dyn_reloc_applied;
                break;
            case C3_R_X86_64_GOTPCRELX:
            case C3_R_X86_64_REX_GOTPCRELX:
                symv=c3_dyn_resolve_symbol(load_bias,di,sym);
                if(symv){
                    *(int32_t*)(void*)where=(int32_t)(symv+addend-(uint64_t)(uintptr_t)where);
                    ++g_c3_dyn_reloc_applied;
                }else if(se&&c3_elf64_st_bind(se->st_info)==C3_STB_WEAK){
                    *(int32_t*)(void*)where=(int32_t)(addend-(uint64_t)(uintptr_t)where);
                    ++g_c3_dyn_reloc_applied;
                }else{
                    ++g_c3_dyn_reloc_failed;
                }
                break;
            default:
                ++g_c3_dyn_reloc_unsupported;
                break;
        }
    }
    return 0;
}

static int c3_apply_dynamic_relocs(uint64_t load_bias,const c3_dyn_info_t *di,uint16_t tls_module_id,uint64_t deadline,const char *obj_name,c3_dyn_profile_t *prof){
    int rc=0;
    uint64_t t0=c3_rdtsc();
    uint32_t a0=g_c3_dyn_reloc_applied;
    uint32_t f0=g_c3_dyn_reloc_failed;
    uint32_t u0=g_c3_dyn_reloc_unsupported;
    if(!di||!di->present)return 0;
    rc=c3_apply_relr_table(load_bias,di->relr_va,di->relr_sz,di->relr_ent,deadline,obj_name,prof);
    if(rc<0)goto done;
    rc=c3_apply_rela_table(load_bias,di->rela_va,di->rela_sz,di->rela_ent,di,tls_module_id,deadline,obj_name,prof);
    if(rc<0)goto done;
    rc=c3_apply_rel_table(load_bias,di->rel_va,di->rel_sz,di->rel_ent,di,tls_module_id,deadline,obj_name,prof);
    if(rc<0)goto done;
    if(di->jmprel_va&&di->pltrelsz){
        if(di->pltrel==C3_DT_REL)
            rc=c3_apply_rel_table(load_bias,di->jmprel_va,di->pltrelsz,di->rel_ent,di,tls_module_id,deadline,obj_name,prof);
        else
            rc=c3_apply_rela_table(load_bias,di->jmprel_va,di->pltrelsz,di->rela_ent,di,tls_module_id,deadline,obj_name,prof);
        if(rc<0)goto done;
    }
done:
    if(prof){
        prof->relocs_applied+=c3_dyn_u32_delta(g_c3_dyn_reloc_applied,a0);
        prof->relocs_failed+=c3_dyn_u32_delta(g_c3_dyn_reloc_failed,f0);
        prof->relocs_unsupported+=c3_dyn_u32_delta(g_c3_dyn_reloc_unsupported,u0);
    }
    {
        uint64_t dt=c3_rdtsc()-t0;
        c3_dyn_profile_add_cycle(&g_c3_dyn_cycle_reloc,dt);
        if(prof)c3_dyn_profile_add_cycle(&prof->cycle_reloc,dt);
    }
    return rc;
}

int elf64_map_into_task(task_t *t,const uint8_t *data,uint32_t sz){
    const elf64_ehdr_t *eh=(const elf64_ehdr_t*)data;
    int i;
    int rc=0;
    uint64_t load_bias=0;
    uint64_t min_vaddr=~0ULL;
    uint64_t max_end=0;
    uint64_t dyn_vaddr=0,dyn_memsz=0;
    uint64_t image_base=~0ULL;
    uint64_t image_phdr_va=0;
    uint16_t image_phnum=0;
    uint64_t tls_init_va=0;
    uint64_t tls_filesz=0;
    uint64_t tls_memsz=0;
    uint64_t tls_align=0;
    uint16_t tls_module_id=0;
    uint64_t dyn_deadline=0;
    uint64_t dyn_t0=0;
    bool set_brk_for_image;
    bool defer_dyn_to_user_loader=false;
    c3_dyn_info_t di;
    c3_dyn_profile_t *prof=0;
    char obj_name[128];
    c3_force_map_stage("enter","",sz,eh?eh->e_entry:0);
    __boot_serial_puts("[elf64_map] enter sz=");
    __boot_serial_putu32(sz);
    __boot_serial_puts(" e_type=");
    __boot_serial_putu32(eh->e_type);
    __boot_serial_puts(" e_phnum=");
    __boot_serial_putu32(eh->e_phnum);
    __boot_serial_puts(" e_entry=");
    __boot_serial_puthex64(eh->e_entry);
    __boot_serial_puts("\n");
    obj_name[0]=0;
    if(!elf64_validate(data,sz))return -1;
    __boot_serial_puts("[elf64_map] validate OK\n");
    if(!t->addr_space)t->addr_space=paging_create_address_space();
    if(!t->addr_space)return -1;
    if(g_c3_next_image_name[0]){
        c3_strlcpy(obj_name,g_c3_next_image_name,sizeof(obj_name));
        g_c3_next_image_name[0]=0;
    }
    c3_force_map_stage("image",obj_name[0]?obj_name:"(unset)",eh->e_type,eh->e_phnum);

    if(eh->e_type==ET_DYN){
        for(i=0;i<(int)eh->e_phnum;++i){
            uint64_t off=eh->e_phoff+(uint64_t)i*eh->e_phentsize;
            if(off+sizeof(elf64_phdr_t)>sz)break;
            {
                const elf64_phdr_t *ph=(const elf64_phdr_t*)(data+off);
                if(ph->p_type!=PT_LOAD)continue;
                uint64_t s=ph->p_vaddr&~(PAGE_SIZE-1);
                if(s<min_vaddr)min_vaddr=s;
            }
        }
        if(min_vaddr==~0ULL)min_vaddr=0;
        if(t->mmap_base<0x40000000ULL)t->mmap_base=0x40000000ULL;
        load_bias=(t->mmap_base-min_vaddr)&~(PAGE_SIZE-1);
    }
    c3_force_map_stage("bias",obj_name[0]?obj_name:"(unset)",load_bias,t?t->mmap_base:0);

    t->entry_point=eh->e_entry+load_bias;
    t->ctx.rip=t->entry_point;
    set_brk_for_image=(eh->e_type!=ET_DYN)||(t->brk_start==0);

    image_phnum=(uint16_t)eh->e_phnum;
    __boot_serial_puts("[elf64_map] PT loop, load_bias=");
    __boot_serial_puthex64(load_bias);
    __boot_serial_puts("\n");
    for(i=0;i<(int)eh->e_phnum;++i){
        uint64_t off=eh->e_phoff+(uint64_t)i*eh->e_phentsize;
        if(off+sizeof(elf64_phdr_t)>sz)break;
        {
            const elf64_phdr_t *ph=(const elf64_phdr_t*)(data+off);
            if(ph->p_type==PT_PHDR)image_phdr_va=ph->p_vaddr+load_bias;
            if(ph->p_type==PT_DYNAMIC){
                dyn_vaddr=ph->p_vaddr+load_bias;
                dyn_memsz=ph->p_memsz;
            }
            if(ph->p_type==PT_TLS){
                tls_init_va=ph->p_vaddr+load_bias;
                tls_filesz=ph->p_filesz;
                tls_memsz=ph->p_memsz;
                tls_align=ph->p_align;
            }
            if(ph->p_type!=PT_LOAD)continue;
            {
                uint64_t seg_start=(ph->p_vaddr+load_bias)&~(PAGE_SIZE-1);
                uint64_t seg_end=((ph->p_vaddr+load_bias+ph->p_memsz)+PAGE_SIZE-1)&~(PAGE_SIZE-1);
                uint64_t pg;
                uint64_t pflags=PAGE_PRESENT|PAGE_USER;
                c3_force_map_stage("load-begin",obj_name[0]?obj_name:"(unset)",ph->p_vaddr+load_bias,ph->p_memsz);
                if(ph->p_flags&0x2)pflags|=PAGE_WRITABLE;
                __boot_serial_puts("[elf64_map] PT_LOAD #");
                __boot_serial_putu32((uint32_t)i);
                __boot_serial_puts(" vaddr=");
                __boot_serial_puthex64(ph->p_vaddr+load_bias);
                __boot_serial_puts(" memsz=");
                __boot_serial_puthex64(ph->p_memsz);
                __boot_serial_puts(" filesz=");
                __boot_serial_puthex64(ph->p_filesz);
                __boot_serial_puts(" flags=");
                __boot_serial_puthex64(ph->p_flags);
                __boot_serial_puts("\n");
                for(pg=seg_start;pg<seg_end;pg+=PAGE_SIZE){
                    uint64_t frame=pmm_alloc_frame();
                    if(!frame)return -1;
                    c3_phys_ref_set_initial(frame);
                    paging_map(t->addr_space,pg,frame,pflags);
                    c3_memset((void*)(uintptr_t)pg,0,PAGE_SIZE);
                }
                __boot_serial_puts("[elf64_map]   pages mapped, about to memcpy file data\n");
                if(ph->p_filesz>0&&ph->p_offset+ph->p_filesz<=sz){
                    c3_memcpy((void*)(uintptr_t)(ph->p_vaddr+load_bias),data+ph->p_offset,(size_t)ph->p_filesz);
                }
                __boot_serial_puts("[elf64_map]   PT_LOAD #");
                __boot_serial_putu32((uint32_t)i);
                __boot_serial_puts(" done\n");
                c3_force_map_stage("load-done",obj_name[0]?obj_name:"(unset)",seg_start,seg_end);
                if(seg_start<image_base)image_base=seg_start;
                if(seg_end>max_end)max_end=seg_end;
                if(set_brk_for_image&&ph->p_vaddr+load_bias+ph->p_memsz>t->brk_start){
                    t->brk_start=((ph->p_vaddr+load_bias+ph->p_memsz)+PAGE_SIZE-1)&~(PAGE_SIZE-1);
                    t->brk_current=t->brk_start;
                }
            }
        }
    }
    __boot_serial_puts("[elf64_map] PT loop done obj=");
    __boot_serial_puts(obj_name[0]?obj_name:"(unset)");
    __boot_serial_puts(" dyn_vaddr=");
    __boot_serial_puthex64(dyn_vaddr);
    __boot_serial_puts(" dyn_memsz=");
    __boot_serial_puthex64(dyn_memsz);
    __boot_serial_puts(" max_end=");
    __boot_serial_puthex64(max_end);
    __boot_serial_puts("\n");

    /* CRITICAL: bump t->mmap_base past this image's max_end BEFORE
     * we recurse into c3_load_needed_objects. Otherwise every
     * recursively-loaded shared library (libc, ld-linux, libpthread...)
     * computes its own load_bias from the ORIGINAL t->mmap_base and
     * lands on top of us, clobbering the PT_LOAD pages we just wrote.
     * This was the root cause of a #GP during libc's reloc phase:
     * ld-linux was being loaded at the same 0x40000000 as libc. */
    if (max_end) {
        uint64_t next = (max_end + 0x200000ULL + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
        if (next > t->mmap_base) t->mmap_base = next;
    }
    c3_force_map_stage("pt-done",obj_name[0]?obj_name:"(unset)",max_end,t?t->mmap_base:0);

    if(!image_phdr_va)image_phdr_va=load_bias+eh->e_phoff;
    if(dyn_vaddr&&dyn_memsz&&c3_dyn_parse_info(load_bias,dyn_vaddr,dyn_memsz,&di)){
        c3_force_map_stage("dyn",obj_name[0]?obj_name:"(unset)",dyn_vaddr,di.needed_count);
        __boot_serial_puts("[elf64_map] dyn_parse_info OK, needed_count=");
        __boot_serial_putu32(di.needed_count);
        __boot_serial_puts("\n");
        if(!obj_name[0]&&di.soname_off&&(!di.strsz||di.soname_off<di.strsz)){
            const char *sn=(const char*)(uintptr_t)(di.strtab_va+di.soname_off);
            if(sn&&*sn)c3_strlcpy(obj_name,sn,sizeof(obj_name));
        }
        if(!obj_name[0]){
            size_t l=0;
            c3_append_str(obj_name,&l,sizeof(obj_name),"img");
            c3_append_u32(obj_name,&l,sizeof(obj_name),g_c3_dyn_images_mapped+1);
        }
        defer_dyn_to_user_loader=
            c3_has_token(obj_name,"firefox")||
            c3_has_token(obj_name,"chromium")||
            c3_has_token(obj_name,"chrome")||
            c3_has_token(obj_name,"ld-linux-x86-64.so.2");
        if(defer_dyn_to_user_loader){
            c3_force_map_stage("defer",obj_name,dyn_vaddr,dyn_memsz);
            __boot_serial_puts("[elf64_map] defer dynlink to user ld-linux obj=");
            __boot_serial_puts(obj_name);
            __boot_serial_puts("\n");
            ++g_c3_dyn_images_mapped;
        }else{
            prof=c3_dyn_profile_begin(obj_name,(uint16_t)g_c3_dyn_load_depth,di.needed_count);
            dyn_t0=c3_rdtsc();
            dyn_deadline=c3_dyn_deadline_from_ms(g_c3_dyn_timeout_ms);
            tls_module_id=c3_dyn_register_object(obj_name,load_bias,image_base,max_end,image_phdr_va,image_phnum,&di,tls_init_va,tls_filesz,tls_memsz,tls_align);
            __boot_serial_puts("[elf64_map] calling c3_load_needed_objects obj=");
            __boot_serial_puts(obj_name);
            __boot_serial_puts("\n");
            rc=c3_load_needed_objects(t,&di,obj_name,dyn_deadline,prof);
            __boot_serial_puts("[elf64_map] c3_load_needed_objects rc=");
            __boot_serial_putu32((uint32_t)rc);
            __boot_serial_puts(" obj=");
            __boot_serial_puts(obj_name);
            __boot_serial_puts("\n");
            if(rc<0){
                uint64_t dt=c3_rdtsc()-dyn_t0;
                c3_dyn_profile_add_cycle(&g_c3_dyn_cycle_map,dt);
                if(prof)c3_dyn_profile_add_cycle(&prof->cycle_map,dt);
                return rc;
            }
            __boot_serial_puts("[elf64_map] calling c3_apply_dynamic_relocs obj=");
            __boot_serial_puts(obj_name);
            __boot_serial_puts("\n");
            rc=c3_apply_dynamic_relocs(load_bias,&di,tls_module_id,dyn_deadline,obj_name,prof);
            __boot_serial_puts("[elf64_map] c3_apply_dynamic_relocs rc=");
            __boot_serial_putu32((uint32_t)rc);
            __boot_serial_puts(" obj=");
            __boot_serial_puts(obj_name);
            __boot_serial_puts("\n");
            if(rc<0){
                uint64_t dt=c3_rdtsc()-dyn_t0;
                c3_dyn_profile_add_cycle(&g_c3_dyn_cycle_map,dt);
                if(prof)c3_dyn_profile_add_cycle(&prof->cycle_map,dt);
                return rc;
            }
            ++g_c3_dyn_images_mapped;
            {
                uint64_t dt=c3_rdtsc()-dyn_t0;
                c3_dyn_profile_add_cycle(&g_c3_dyn_cycle_map,dt);
                if(prof)c3_dyn_profile_add_cycle(&prof->cycle_map,dt);
            }
        }
    } else {
        __boot_serial_puts("[elf64_map] no PT_DYNAMIC or parse failed, skipping relocs\n");
    }
    c3_force_map_stage("return",obj_name[0]?obj_name:"(none)",max_end,t?t->mmap_base:0);
    __boot_serial_puts("[elf64_map] returning 0 obj=");
    __boot_serial_puts(obj_name[0]?obj_name:"(none)");
    __boot_serial_puts("\n");
    if(max_end){
        uint64_t next=(max_end+0x200000ULL+PAGE_SIZE-1)&~(PAGE_SIZE-1);
        if(next>t->mmap_base)t->mmap_base=next;
    }
    if(!t->mmap_base)t->mmap_base=(t->brk_start+0x200000ULL)&~(PAGE_SIZE-1);
    return 0;
}

int elf64_setup_stack(task_t *t,int argc,char **argv,char **envp,auxv_t *auxv,int auxc){
    uint64_t sp=C3_USER_STACK_TOP;
    uint64_t stack_base=C3_USER_STACK_TOP-TASK_USER_STACK;
    uint64_t arg_ptrs[C3_EXEC_ARG_MAX];
    uint64_t env_ptrs[C3_EXEC_ENV_MAX];
    uint8_t random_bytes[16];
    uint64_t random_ptr=0;
    uint64_t execfn_ptr=0;
    uint64_t platform_ptr=0;
    int envc=0;
    int i;
    if(!t||argc<0||argc>C3_EXEC_ARG_MAX||auxc<0)return -EINVAL;
    if(envp){
        while(envp[envc]){
            if(envc>=C3_EXEC_ENV_MAX)return -E2BIG;
            ++envc;
        }
    }
    if(argc>0&&!argv)return -EFAULT;

    for(i=envc-1;i>=0;--i){
        size_t len=c3_strlen(envp[i])+1;
        if(sp<stack_base+len)return -E2BIG;
        sp-=len;
        c3_memcpy((void*)(uintptr_t)sp,envp[i],len);
        env_ptrs[i]=sp;
    }
    for(i=argc-1;i>=0;--i){
        size_t len;
        if(!argv[i])return -EFAULT;
        len=c3_strlen(argv[i])+1;
        if(sp<stack_base+len)return -E2BIG;
        sp-=len;
        c3_memcpy((void*)(uintptr_t)sp,argv[i],len);
        arg_ptrs[i]=sp;
    }

    c3_exec_fill_random16(random_bytes);
    if(sp<stack_base+sizeof(random_bytes))return -E2BIG;
    sp-=sizeof(random_bytes);
    c3_memcpy((void*)(uintptr_t)sp,random_bytes,sizeof(random_bytes));
    random_ptr=sp;

    for(i=0;i<auxc;++i){
        if((auxv[i].a_type==AT_EXECFN||auxv[i].a_type==AT_PLATFORM)&&auxv[i].a_val){
            const char *aux_str=(const char*)(uintptr_t)auxv[i].a_val;
            size_t len=c3_strlen(aux_str)+1;
            if(sp<stack_base+len)return -E2BIG;
            sp-=len;
            c3_memcpy((void*)(uintptr_t)sp,aux_str,len);
            auxv[i].a_val=sp;
            if(auxv[i].a_type==AT_EXECFN)execfn_ptr=sp;
            else platform_ptr=sp;
        }
    }
    for(i=0;i<auxc;++i){
        if(auxv[i].a_type==AT_RANDOM)auxv[i].a_val=random_ptr;
    }

    {
        uint64_t post_bytes=(uint64_t)(auxc+1)*sizeof(auxv_t)+(uint64_t)(envc+argc+3)*sizeof(uint64_t);
        sp&=~0xFULL;
        if((post_bytes&0xFULL)!=0){
            if(sp<stack_base+8)return -E2BIG;
            sp-=8;
        }
    }

    if(sp<stack_base+sizeof(auxv_t))return -E2BIG;
    sp-=sizeof(auxv_t);
    ((auxv_t*)(uintptr_t)sp)->a_type=AT_NULL;
    ((auxv_t*)(uintptr_t)sp)->a_val=0;
    for(i=auxc-1;i>=0;--i){
        if(sp<stack_base+sizeof(auxv_t))return -E2BIG;
        sp-=sizeof(auxv_t);
        ((auxv_t*)(uintptr_t)sp)->a_type=auxv[i].a_type;
        ((auxv_t*)(uintptr_t)sp)->a_val=auxv[i].a_val;
    }

    if(sp<stack_base+sizeof(uint64_t))return -E2BIG;
    sp-=sizeof(uint64_t);
    *(uint64_t*)(uintptr_t)sp=0;
    for(i=envc-1;i>=0;--i){
        if(sp<stack_base+sizeof(uint64_t))return -E2BIG;
        sp-=sizeof(uint64_t);
        *(uint64_t*)(uintptr_t)sp=env_ptrs[i];
    }

    if(sp<stack_base+sizeof(uint64_t))return -E2BIG;
    sp-=sizeof(uint64_t);
    *(uint64_t*)(uintptr_t)sp=0;
    for(i=argc-1;i>=0;--i){
        if(sp<stack_base+sizeof(uint64_t))return -E2BIG;
        sp-=sizeof(uint64_t);
        *(uint64_t*)(uintptr_t)sp=arg_ptrs[i];
    }

    if(sp<stack_base+sizeof(uint64_t))return -E2BIG;
    sp-=sizeof(uint64_t);
    *(uint64_t*)(uintptr_t)sp=(uint64_t)argc;

    t->ctx.rsp=sp;
    t->aux_at_random=random_ptr;
    t->aux_at_execfn=execfn_ptr;
    t->aux_at_platform=platform_ptr;
    return 0;
}

/* Net syscalls */
int64_t real_sys_socket(int domain,int type,int protocol){
    task_t *cur=task_current();
    int base_type=type&0xF;
    int sock_type=0;
    uint16_t ff=FDFL_READABLE|FDFL_WRITABLE;
    int sfd;
    if(base_type==1||base_type==5)sock_type=1;
    else if(base_type==2)sock_type=2;
    else if(base_type==3&&domain==16)sock_type=2;
    else return -EINVAL;
    sfd=sock_create(domain,sock_type,protocol);
    if(sfd<0)return -ENOMEM;
    if(type&O_NONBLOCK){ff|=FDFL_NONBLOCK;g_sockets[sfd].non_blocking=true;}
    if(type&O_CLOEXEC)ff|=FDFL_CLOEXEC;
    int fd=c3_fd_alloc_for_task(cur,FDKIND_SOCKET,sfd,ff);
    if(fd<0){sock_close(sfd);return -EMFILE;}
    if(cur&&c3_task_is_browser_runtime(cur)){
        static uint32_t force_socket_trace;
        if(force_socket_trace<12u){
            ++force_socket_trace;
            __boot_serial_force_puts("[bsock!] pid=");
            __boot_serial_force_putu32((uint32_t)cur->pid);
            c3_force_task_name(cur);
            __boot_serial_force_puts(" fd=");
            __boot_serial_force_putu32((uint32_t)fd);
            __boot_serial_force_puts(" ref=");
            __boot_serial_force_putu32((uint32_t)sfd);
            __boot_serial_force_puts(" dom=");
            __boot_serial_force_putu32((uint32_t)domain);
            __boot_serial_force_puts(" type=");
            __boot_serial_force_puthex64((uint64_t)(uint32_t)type);
            __boot_serial_force_puts(" proto=");
            __boot_serial_force_putu32((uint32_t)protocol);
            __boot_serial_force_puts("\n");
        }
    }
    return fd;
}

int64_t real_sys_bind(int fd,const void *addr,uint32_t addrlen){
    task_t *cur=task_current();
    uint32_t ip=0;
    uint16_t port=0;
    char upath[128];
    bool is_unix=false;
    int ref;
    int64_t rc;
    if(!fd_valid(&cur->fdt,fd)||cur->fdt.fds[fd].kind!=FDKIND_SOCKET)return -ENOTSOCK;
    ref=cur->fdt.fds[fd].ref;
    if(g_sockets[ref].domain==16){
        (void)addr;
        (void)addrlen;
        g_sockets[ref].local_port=(uint16_t)(cur?cur->pid:0);
        return 0;
    }
    upath[0]=0;
    is_unix=(c3_sockaddr_un_path(addr,addrlen,upath,sizeof(upath))==0);
    if(c3_sockaddr_target(addr,addrlen,&ip,&port)<0)return -EINVAL;
    rc=(int64_t)sock_bind(ref,ip,port);
    if(is_unix)c3_trace_unix_socket_addr("bind",fd,ref,upath,port,rc);
    return rc;
}

int64_t real_sys_listen(int fd,int backlog){
    task_t *cur=task_current();
    if(!fd_valid(&cur->fdt,fd)||cur->fdt.fds[fd].kind!=FDKIND_SOCKET)return -ENOTSOCK;
    return(int64_t)sock_listen(cur->fdt.fds[fd].ref,backlog);
}

int64_t real_sys_accept(int fd,void *addr,uint32_t *addrlen){
    task_t *cur=task_current();
    uint32_t rip=0;
    uint16_t rport=0;
    if(!fd_valid(&cur->fdt,fd)||cur->fdt.fds[fd].kind!=FDKIND_SOCKET)return -ENOTSOCK;
    int nsfd=sock_accept(cur->fdt.fds[fd].ref,&rip,&rport);
    if(nsfd<0)return nsfd;
    int nfd=c3_fd_alloc_for_task(cur,FDKIND_SOCKET,nsfd,FDFL_READABLE|FDFL_WRITABLE);
    if(addr&&addrlen&&*addrlen>=sizeof(c3_sockaddr_in_t)){
        c3_sockaddr_in_t *sa=(c3_sockaddr_in_t*)addr;
        c3_memset(sa,0,sizeof(*sa));
        sa->sin_family=2;
        sa->sin_port=c3_ntoh16(rport);
        sa->sin_addr=rip;
        *addrlen=sizeof(*sa);
    }
    return nfd;
}
int64_t real_sys_accept4(int fd,void *addr,uint32_t *addrlen,int flags){
    task_t *cur=task_current();
    int64_t nfd=real_sys_accept(fd,addr,addrlen);
    if(nfd<0)return nfd;
    if(flags&~(O_NONBLOCK|O_CLOEXEC)){
        real_sys_close((int)nfd);
        return -EINVAL;
    }
    if(flags&O_NONBLOCK){
        cur->fdt.fds[(int)nfd].flags|=FDFL_NONBLOCK;
        if(cur->fdt.fds[(int)nfd].kind==FDKIND_SOCKET&&cur->fdt.fds[(int)nfd].ref>=0&&cur->fdt.fds[(int)nfd].ref<SOCK_MAX){
            g_sockets[cur->fdt.fds[(int)nfd].ref].non_blocking=true;
        }
    }
    if(flags&O_CLOEXEC)cur->fdt.fds[(int)nfd].flags|=FDFL_CLOEXEC;
    return nfd;
}

int64_t real_sys_connect(int fd,const void *addr,uint32_t addrlen){
    task_t *cur=task_current();
    uint32_t ip=0;
    uint16_t port=0;
    char upath[128];
    bool is_unix=false;
    int ref;
    int64_t rc;
    if(!fd_valid(&cur->fdt,fd)||cur->fdt.fds[fd].kind!=FDKIND_SOCKET)return -ENOTSOCK;
    ref=cur->fdt.fds[fd].ref;
    if(g_sockets[ref].domain==16){
        (void)addr;
        (void)addrlen;
        g_sockets[ref].remote_port=0;
        return 0;
    }
    upath[0]=0;
    is_unix=(c3_sockaddr_un_path(addr,addrlen,upath,sizeof(upath))==0);
    if(c3_sockaddr_target(addr,addrlen,&ip,&port)<0)return -EINVAL;
    rc=(int64_t)sock_connect(ref,ip,port);
    if(is_unix)c3_trace_unix_socket_addr("conn",fd,ref,upath,port,rc);
    if(cur&&c3_task_is_browser_runtime(cur)){
        static uint32_t force_connect_trace;
        if(force_connect_trace<20u){
            ++force_connect_trace;
            __boot_serial_force_puts("[bconn!] pid=");
            __boot_serial_force_putu32((uint32_t)cur->pid);
            c3_force_task_name(cur);
            __boot_serial_force_puts(" fd=");
            __boot_serial_force_putu32((uint32_t)fd);
            __boot_serial_force_puts(" ref=");
            __boot_serial_force_putu32((uint32_t)ref);
            __boot_serial_force_puts(" port=");
            __boot_serial_force_putu32((uint32_t)port);
            __boot_serial_force_puts(" svc=");
            __boot_serial_force_putu32((uint32_t)g_sockets[ref].virt_service);
            __boot_serial_force_puts(" rc=");
            c3_force_rc(rc);
            if(is_unix){
                __boot_serial_force_puts(" path=");
                __boot_serial_force_puts(upath[0]?upath:"(empty)");
            }
            __boot_serial_force_puts("\n");
        }
    }
    return rc;
}

int64_t real_sys_sendto(int fd,const void *buf,size_t len,int flags,const void *addr,uint32_t addrlen){
    task_t *cur=task_current();
    uint32_t ip=0;
    uint16_t port=0;
    int ref;
    if(!fd_valid(&cur->fdt,fd)||cur->fdt.fds[fd].kind!=FDKIND_SOCKET)return -ENOTSOCK;
    ref=cur->fdt.fds[fd].ref;
    if(ref>=0&&ref<SOCK_MAX&&g_sockets[ref].used&&g_sockets[ref].domain==16){
        (void)buf;
        (void)flags;
        (void)addr;
        (void)addrlen;
        return (int64_t)len;
    }
    if(addr&&addrlen>=sizeof(uint16_t)){
        if(c3_sockaddr_target(addr,addrlen,&ip,&port)<0)return -EINVAL;
        sock_connect(ref,ip,port);
    }
    return(int64_t)sock_send(ref,buf,len,flags);
}

int64_t real_sys_recvfrom(int fd,void *buf,size_t len,int flags,void *addr,uint32_t *addrlen){
    task_t *cur=task_current();
    if(!fd_valid(&cur->fdt,fd)||cur->fdt.fds[fd].kind!=FDKIND_SOCKET)return -ENOTSOCK;
    {
        int rflags=flags;
        int64_t r;
        int ref=cur->fdt.fds[fd].ref;
        uint32_t rx_before=c3_socket_rx_used(ref);
        if(ref>=0&&ref<SOCK_MAX&&g_sockets[ref].used&&g_sockets[ref].domain==16){
            (void)buf;
            (void)len;
            if(addr&&addrlen&&*addrlen>=sizeof(c3_sockaddr_nl_t)){
                c3_sockaddr_nl_t *nl=(c3_sockaddr_nl_t*)addr;
                c3_memset(nl,0,sizeof(*nl));
                nl->nl_family=16;
                nl->nl_pid=(uint32_t)(cur?cur->pid:0);
                *addrlen=sizeof(*nl);
            }
            return -EAGAIN;
        }
        if(cur->fdt.fds[fd].flags&FDFL_NONBLOCK)rflags|=C3_MSG_DONTWAIT;
        r=c3_socket_recv_wait(ref,buf,len,rflags);
        c3_trace_x11_read_result(fd,ref,len,rflags,r,buf,rx_before);
        if(r>=0&&addr&&addrlen&&*addrlen>=sizeof(c3_sockaddr_in_t)){
            c3_sockaddr_in_t *sa=(c3_sockaddr_in_t*)addr;
            socket_t *s=&g_sockets[ref];
            c3_memset(sa,0,sizeof(*sa));
            sa->sin_family=2;
            sa->sin_port=c3_ntoh16(s->remote_port);
            sa->sin_addr=s->remote_ip;
            *addrlen=sizeof(*sa);
        }
        return r;
    }
}

int64_t real_sys_setsockopt(int fd,int level,int optname,const void *optval,uint32_t optlen){
    task_t *cur=task_current();
    socket_t *s;
    int v;
    if(!fd_valid(&cur->fdt,fd)||cur->fdt.fds[fd].kind!=FDKIND_SOCKET)return -ENOTSOCK;
    s=&g_sockets[cur->fdt.fds[fd].ref];
    if(level==C3_SOL_SOCKET&&optname==C3_SO_PASSCRED){
        if(!optval||optlen<sizeof(int))return -EFAULT;
        v=*(const int*)optval;
        if(v)s->virt_flags|=C3_SOCK_VFLAG_PASSCRED;
        else s->virt_flags&=(uint16_t)~C3_SOCK_VFLAG_PASSCRED;
        return 0;
    }
    return(int64_t)sock_setsockopt(cur->fdt.fds[fd].ref,level,optname,optval,(size_t)optlen);
}
int64_t real_sys_getsockopt(int fd,int level,int optname,void *optval,uint32_t *optlen){
    task_t *cur=task_current();
    socket_t *s;
    int v;
    if(!fd_valid(&cur->fdt,fd)||cur->fdt.fds[fd].kind!=FDKIND_SOCKET)return -ENOTSOCK;
    if(!optlen||!optval||*optlen<sizeof(int))return -EFAULT;
    s=&g_sockets[cur->fdt.fds[fd].ref];
    if(level!=C3_SOL_SOCKET)return -EINVAL;
    switch(optname){
        case C3_SO_ERROR:
            v=s->error;
            s->error=0;
            break;
        case C3_SO_TYPE:
            v=s->type;
            break;
        case C3_SO_DOMAIN:
            v=s->domain;
            break;
        case C3_SO_PROTOCOL:
            v=s->protocol;
            break;
        case C3_SO_ACCEPTCONN:
            v=(s->tcp_state==TCP_LISTEN)?1:0;
            break;
        case C3_SO_RCVBUF:
        case C3_SO_SNDBUF:
            v=SOCK_BUF_SIZE;
            break;
        case C3_SO_KEEPALIVE:
            v=0;
            break;
        case C3_SO_PASSCRED:
            v=(s->virt_flags&C3_SOCK_VFLAG_PASSCRED)?1:0;
            break;
        case C3_SO_LINGER:
            if(*optlen<8)return -EINVAL;
            ((int*)optval)[0]=0;
            ((int*)optval)[1]=0;
            *optlen=8;
            return 0;
        case C3_SO_PEERCRED:
            if(*optlen<12)return -EINVAL;
            ((int*)optval)[0]=cur->ppid?cur->ppid:cur->pid;
            ((uint32_t*)optval)[1]=(uint32_t)cur->uid;
            ((uint32_t*)optval)[2]=(uint32_t)cur->gid;
            *optlen=12;
            return 0;
        default:
            {
                static uint32_t trace_count=0;
                if(trace_count<16){
                    ++trace_count;
                    __boot_serial_puts("[getsockopt-unsupported] fd=");
                    __boot_serial_putu32((uint32_t)fd);
                    __boot_serial_puts(" level=");
                    __boot_serial_putu32((uint32_t)level);
                    __boot_serial_puts(" opt=");
                    __boot_serial_putu32((uint32_t)optname);
                    __boot_serial_puts(" len=");
                    __boot_serial_putu32(optlen?*optlen:0);
                    __boot_serial_puts("\n");
                }
            }
            return -EINVAL;
    }
    *(int*)optval=v;
    *optlen=sizeof(int);
    return 0;
}
int64_t real_sys_getsockname(int fd,void *addr,uint32_t *addrlen){
    task_t *cur=task_current();
    socket_t *s;
    c3_sockaddr_in_t *sa;
    c3_sockaddr_un_t *su;
    if(!fd_valid(&cur->fdt,fd)||cur->fdt.fds[fd].kind!=FDKIND_SOCKET)return -ENOTSOCK;
    if(!addr||!addrlen||*addrlen<sizeof(uint16_t))return -EFAULT;
    s=&g_sockets[cur->fdt.fds[fd].ref];
    if(s->domain==16){
        c3_sockaddr_nl_t *nl;
        if(*addrlen<sizeof(c3_sockaddr_nl_t))return -EINVAL;
        nl=(c3_sockaddr_nl_t*)addr;
        c3_memset(nl,0,sizeof(*nl));
        nl->nl_family=16;
        nl->nl_pid=(uint32_t)cur->pid;
        *addrlen=sizeof(*nl);
        return 0;
    }
    if(s->domain==1){
        if(*addrlen<sizeof(c3_sockaddr_un_t))return -EINVAL;
        su=(c3_sockaddr_un_t*)addr;
        c3_memset(su,0,sizeof(*su));
        su->sun_family=1;
        c3_strlcpy(su->sun_path,"@ridux-local",sizeof(su->sun_path));
        *addrlen=sizeof(c3_sockaddr_un_t);
        return 0;
    }
    if(*addrlen<sizeof(c3_sockaddr_in_t))return -EINVAL;
    sa=(c3_sockaddr_in_t*)addr;
    c3_memset(sa,0,sizeof(*sa));
    sa->sin_family=2;
    sa->sin_addr=s->local_ip;
    sa->sin_port=c3_ntoh16(s->local_port);
    *addrlen=sizeof(*sa);
    return 0;
}
int64_t real_sys_getpeername(int fd,void *addr,uint32_t *addrlen){
    task_t *cur=task_current();
    socket_t *s;
    c3_sockaddr_in_t *sa;
    c3_sockaddr_un_t *su;
    if(!fd_valid(&cur->fdt,fd)||cur->fdt.fds[fd].kind!=FDKIND_SOCKET)return -ENOTSOCK;
    if(!addr||!addrlen||*addrlen<sizeof(uint16_t))return -EFAULT;
    s=&g_sockets[cur->fdt.fds[fd].ref];
    if(s->domain==16)return -ENOTCONN;
    if(s->domain==1){
        if(*addrlen<sizeof(c3_sockaddr_un_t))return -EINVAL;
        su=(c3_sockaddr_un_t*)addr;
        c3_memset(su,0,sizeof(*su));
        su->sun_family=1;
        if(s->remote_port>=6000&&s->remote_port<6064)c3_strlcpy(su->sun_path,"/tmp/.X11-unix/X0",sizeof(su->sun_path));
        else if(s->remote_port>=39010&&s->remote_port<=39019)c3_strlcpy(su->sun_path,"/run/user/0/wayland-0",sizeof(su->sun_path));
        else if(s->remote_port==39020||s->remote_port==39021)c3_strlcpy(su->sun_path,"/run/user/0/bus",sizeof(su->sun_path));
        else c3_strlcpy(su->sun_path,"@ridux-peer",sizeof(su->sun_path));
        *addrlen=sizeof(c3_sockaddr_un_t);
        return 0;
    }
    if(*addrlen<sizeof(c3_sockaddr_in_t))return -EINVAL;
    sa=(c3_sockaddr_in_t*)addr;
    c3_memset(sa,0,sizeof(*sa));
    sa->sin_family=2;
    sa->sin_addr=s->remote_ip;
    sa->sin_port=c3_ntoh16(s->remote_port);
    *addrlen=sizeof(*sa);
    return 0;
}
int64_t real_sys_shutdown(int fd,int how){
    task_t *cur=task_current();
    (void)how;
    if(!fd_valid(&cur->fdt,fd)||cur->fdt.fds[fd].kind!=FDKIND_SOCKET)return -ENOTSOCK;
    sock_close(cur->fdt.fds[fd].ref);return 0;
}
int64_t real_sys_socketpair(int domain,int type,int protocol,int sv[2]){
    task_t *cur=task_current();
    int refs[2];
    int base_type=type&0xF;
    int sock_type=0;
    uint16_t ff=FDFL_READABLE|FDFL_WRITABLE;
    int rc;
    int fd0,fd1;
    if(!sv)return -EFAULT;
    if(domain!=1&&domain!=2)return -EINVAL;
    if(base_type==1||base_type==5)sock_type=1;
    else if(base_type==2)sock_type=2;
    else return -EINVAL;
    if(type&O_NONBLOCK)ff|=FDFL_NONBLOCK;
    if(type&O_CLOEXEC)ff|=FDFL_CLOEXEC;
    rc=sock_pair_create(domain,sock_type,protocol,refs);
    if(rc<0)return rc;
    if(type&O_NONBLOCK){
        g_sockets[refs[0]].non_blocking=true;
        g_sockets[refs[1]].non_blocking=true;
    }
    fd0=c3_fd_alloc_for_task(cur,FDKIND_SOCKET,refs[0],ff);
    if(fd0<0){sock_close(refs[0]);sock_close(refs[1]);return -EMFILE;}
    fd1=c3_fd_alloc_for_task(cur,FDKIND_SOCKET,refs[1],ff);
    if(fd1<0){
        c3_fd_group_clear_fd(cur,fd0);
        sock_close(refs[0]);sock_close(refs[1]);
        return -EMFILE;
    }
    sv[0]=fd0;sv[1]=fd1;
    if(c3_task_is_firefox_ipc_trace(cur)){
        static uint32_t socketpair_trace=0;
        if(socketpair_trace<120){
            ++socketpair_trace;
            __boot_serial_puts("[socketpair] pid=");
            __boot_serial_putu32((uint32_t)(cur?cur->pid:0));
            c3_trace_task_name(cur);
            __boot_serial_puts(" domain=");
            __boot_serial_putu32((uint32_t)domain);
            __boot_serial_puts(" type=");
            __boot_serial_puthex64((uint64_t)(uint32_t)type);
            __boot_serial_puts(" fd0=");
            __boot_serial_putu32((uint32_t)fd0);
            __boot_serial_puts(" ref0=");
            __boot_serial_putu32((uint32_t)refs[0]);
            __boot_serial_puts(" fd1=");
            __boot_serial_putu32((uint32_t)fd1);
            __boot_serial_puts(" ref1=");
            __boot_serial_putu32((uint32_t)refs[1]);
            __boot_serial_puts(" trace=");
            __boot_serial_putu32(socketpair_trace);
            __boot_serial_puts("\n");
        }
    }
    return 0;
}

static int64_t c3_sendmsg_iov(int sref,const iovec_t *iov,size_t iovcnt,int flags){
    if(c3_socket_ref_is_virtual_stream(sref)){
        return c3_socket_send_iov_coalesced(sref,iov,iovcnt,flags);
    }
    if(sref>=0&&sref<SOCK_MAX&&g_sockets[sref].used&&g_sockets[sref].type==2){
        uint8_t pkt[C3_MSG_SCRATCH_SIZE];
        size_t total=0,i;
        for(i=0;i<iovcnt;++i){
            if(!iov[i].iov_len)continue;
            if(!iov[i].iov_base)return -EFAULT;
            if(total+iov[i].iov_len>sizeof(pkt))return -EFBIG;
            c3_memcpy(pkt+total,iov[i].iov_base,iov[i].iov_len);
            total+=iov[i].iov_len;
        }
        if(!total)return 0;
        return (int64_t)sock_send(sref,pkt,total,flags);
    }
    int64_t total=0;
    size_t i;
    for(i=0;i<iovcnt;++i){
        int64_t rc;
        if(!iov[i].iov_len)continue;
        if(!iov[i].iov_base)return total?total:-EFAULT;
        rc=(int64_t)sock_send(sref,iov[i].iov_base,iov[i].iov_len,flags);
        if(rc<0)return total?total:rc;
        total+=rc;
        if((size_t)rc<iov[i].iov_len)break;
    }
    return total;
}

static int64_t c3_recvmsg_iov(int sref,const iovec_t *iov,size_t iovcnt,int flags){
    if(sref>=0&&sref<SOCK_MAX&&g_sockets[sref].used&&g_sockets[sref].type==2){
        uint8_t pkt[C3_MSG_SCRATCH_SIZE];
        size_t cap=0,i,off=0;
        int64_t rc;
        for(i=0;i<iovcnt&&cap<sizeof(pkt);++i){
            if(!iov[i].iov_len)continue;
            if(!iov[i].iov_base)return -EFAULT;
            cap+=iov[i].iov_len;
            if(cap>sizeof(pkt))cap=sizeof(pkt);
        }
        if(!cap)return 0;
        rc=c3_socket_recv_wait(sref,pkt,cap,flags);
        if(rc<=0)return rc;
        for(i=0;i<iovcnt&&off<(size_t)rc;++i){
            size_t cp;
            if(!iov[i].iov_len)continue;
            cp=iov[i].iov_len;
            if(cp>(size_t)rc-off)cp=(size_t)rc-off;
            c3_memcpy(iov[i].iov_base,pkt+off,cp);
            off+=cp;
        }
        return rc;
    }
    int64_t total=0;
    size_t i,cap=0,limit,remain;
    for(i=0;i<iovcnt;++i){
        if(!iov[i].iov_len)continue;
        if(!iov[i].iov_base)return -EFAULT;
        if(cap+iov[i].iov_len<cap)cap=(size_t)-1;
        else cap+=iov[i].iov_len;
    }
    if(!cap)return 0;
    limit=sock_recv_limit_before_right(sref,cap);
    if(limit>cap)limit=cap;
    if(!limit)return (flags&C3_MSG_DONTWAIT)?-EAGAIN:0;
    remain=limit;
    for(i=0;i<iovcnt;++i){
        int64_t rc;
        size_t want;
        if(!remain)break;
        if(!iov[i].iov_len)continue;
        want=iov[i].iov_len;
        if(want>remain)want=remain;
        rc=c3_socket_recv_wait(sref,iov[i].iov_base,want,flags);
        if(rc<0)return total?total:rc;
        if(rc==0)return total;
        total+=rc;
        remain-=(size_t)rc;
        if((size_t)rc<want)break;
    }
    return total;
}

static void c3_trace_firefox_ipc_recv_payload(task_t *cur,int fd,int64_t rc,c3_msghdr_t *mh){
    const uint8_t *p;
    size_t avail;
    size_t qi;
    static uint32_t dump_count=0;
    if(dump_count>=64)return;
    if(!cur||!mh||rc<=0)return;
    if(!c3_task_is_firefox_ipc_trace(cur))return;
    if(!c3_has_token(cur->name,"IPC I/O Child"))return;
    if(!mh->msg_iov||!mh->msg_iovlen)return;
    if(!mh->msg_iov[0].iov_base||!mh->msg_iov[0].iov_len)return;
    avail=mh->msg_iov[0].iov_len;
    if(avail>(size_t)rc)avail=(size_t)rc;
    if(avail>64)avail=64;
    p=(const uint8_t*)mh->msg_iov[0].iov_base;
    ++dump_count;
    __boot_serial_puts("[ipc-recv-dump] pid=");
    __boot_serial_putu32((uint32_t)cur->pid);
    c3_trace_task_name(cur);
    __boot_serial_puts(" fd=");
    __boot_serial_putu32((uint32_t)fd);
    __boot_serial_puts(" rc=");
    __boot_serial_puthex64((uint64_t)rc);
    __boot_serial_puts(" ctrl=");
    __boot_serial_puthex64((uint64_t)mh->msg_controllen);
    __boot_serial_puts(" bytes=");
    __boot_serial_putu32((uint32_t)avail);
    for(qi=0;qi<8&&qi*8<avail;++qi){
        uint64_t q=0;
        size_t bi;
        for(bi=0;bi<8&&qi*8+bi<avail;++bi)
            q|=((uint64_t)p[qi*8+bi])<<(bi*8);
        __boot_serial_puts(" q");
        __boot_serial_putu32((uint32_t)qi);
        __boot_serial_puts("=");
        __boot_serial_puthex64(q);
    }
    __boot_serial_puts("\n");
}

static size_t c3_cmsg_align(size_t n){
    size_t a=sizeof(size_t)-1;
    return (n+a)&~a;
}

static size_t c3_cmsg_len(size_t payload_len){
    return c3_cmsg_align(sizeof(c3_cmsghdr_t))+payload_len;
}

static size_t c3_cmsg_space(size_t payload_len){
    return c3_cmsg_align(sizeof(c3_cmsghdr_t))+c3_cmsg_align(payload_len);
}

static bool c3_recvmsg_put_cmsg(void *control,size_t cap,size_t *used,
                                int level,int type,const void *payload,size_t payload_len,
                                int *msg_flags){
    size_t off,len,space;
    c3_cmsghdr_t *ch;
    if(!control||!used)return false;
    off=c3_cmsg_align(*used);
    len=c3_cmsg_len(payload_len);
    space=c3_cmsg_space(payload_len);
    if(off+len>cap){
        if(msg_flags)*msg_flags|=C3_MSG_CTRUNC;
        return false;
    }
    ch=(c3_cmsghdr_t*)((uint8_t*)control+off);
    ch->cmsg_len=len;
    ch->cmsg_level=level;
    ch->cmsg_type=type;
    if(payload_len&&payload)c3_memcpy((uint8_t*)ch+c3_cmsg_align(sizeof(c3_cmsghdr_t)),payload,payload_len);
    *used=off+space;
    if(*used>cap)*used=off+len;
    return true;
}

static bool c3_scm_right_is_token(int token){
    return token>=C3_SCM_RIGHTS_TOKEN_BASE&&
           token<C3_SCM_RIGHTS_TOKEN_BASE+C3_SCM_RIGHTS_MAX;
}

static int c3_scm_right_store(task_t *sender,int pass_fd){
    uint32_t n;
    if(!sender||!fd_valid(&sender->fdt,pass_fd))return -EBADF;
    for(n=0;n<C3_SCM_RIGHTS_MAX;++n){
        uint32_t idx=(g_c3_scm_rights_next+n)%C3_SCM_RIGHTS_MAX;
        if(!g_c3_scm_rights[idx].used){
            real_fd_t *src=&sender->fdt.fds[pass_fd];
            g_c3_scm_rights[idx].used=true;
            c3_memcpy(&g_c3_scm_rights[idx].fd,src,sizeof(real_fd_t));
            g_c3_scm_rights[idx].fd.flags=(uint16_t)(g_c3_scm_rights[idx].fd.flags&~FDFL_CLOEXEC);
            g_c3_scm_rights_next=(idx+1)%C3_SCM_RIGHTS_MAX;
            {
                static uint32_t trace_count=0;
                if(trace_count<32){
                    ++trace_count;
                    __boot_serial_puts("[scm-rights-store] pid=");
                    __boot_serial_putu32((uint32_t)sender->pid);
                    __boot_serial_puts(" oldfd=");
                    __boot_serial_putu32((uint32_t)pass_fd);
                    __boot_serial_puts(" token=");
                    __boot_serial_putu32((uint32_t)(C3_SCM_RIGHTS_TOKEN_BASE+(int)idx));
                    __boot_serial_puts(" kind=");
                    __boot_serial_putu32((uint32_t)src->kind);
                    __boot_serial_puts(" ref=");
                    __boot_serial_putu32((uint32_t)src->ref);
                    if(src->kind==FDKIND_VFSFILE||src->kind==FDKIND_DIR){
                        const char *p=c3_vfs_open_slot_path(src->ref);
                        if(p){
                            __boot_serial_puts(" path=");
                            __boot_serial_puts(p);
                        }
                    }
                    __boot_serial_puts("\n");
                }
            }
            return C3_SCM_RIGHTS_TOKEN_BASE+(int)idx;
        }
    }
    return -ENFILE;
}

static void c3_scm_right_discard(int token){
    int idx=token-C3_SCM_RIGHTS_TOKEN_BASE;
    if(idx<0||idx>=C3_SCM_RIGHTS_MAX)return;
    c3_memset(&g_c3_scm_rights[idx],0,sizeof(g_c3_scm_rights[idx]));
}

static int c3_scm_right_take(task_t *receiver,int token,int recv_flags,int *out_fd){
    int idx=token-C3_SCM_RIGHTS_TOKEN_BASE;
    real_fd_t *src;
    uint16_t fl;
    int nfd;
    if(!receiver||!out_fd)return -EFAULT;
    if(idx<0||idx>=C3_SCM_RIGHTS_MAX||!g_c3_scm_rights[idx].used)return -EBADF;
    src=&g_c3_scm_rights[idx].fd;
    fl=(uint16_t)(src->flags&~FDFL_CLOEXEC);
    if(((uint32_t)recv_flags)&C3_MSG_CMSG_CLOEXEC)fl|=FDFL_CLOEXEC;
    nfd=c3_fd_alloc_for_task(receiver,src->kind,src->ref,fl);
    if(nfd<0)return nfd;
    if(nfd>=TASK_FD_MAX)return -EMFILE;
    receiver->fdt.fds[nfd].offset=src->offset;
    c3_fd_group_copy_fd(receiver,nfd);
    *out_fd=nfd;
    {
        static uint32_t trace_count=0;
        if(trace_count<64){
            ++trace_count;
            __boot_serial_puts("[scm-rights-recv] pid=");
            __boot_serial_putu32((uint32_t)receiver->pid);
            c3_trace_task_name(receiver);
            __boot_serial_puts(" token=");
            __boot_serial_putu32((uint32_t)token);
            __boot_serial_puts(" newfd=");
            __boot_serial_putu32((uint32_t)nfd);
            __boot_serial_puts(" kind=");
            __boot_serial_putu32((uint32_t)src->kind);
            __boot_serial_puts(" ref=");
            __boot_serial_putu32((uint32_t)src->ref);
            if(src->kind==FDKIND_VFSFILE||src->kind==FDKIND_DIR){
                const char *p=c3_vfs_open_slot_path(src->ref);
                if(p){
                    __boot_serial_puts(" path=");
                    __boot_serial_puts(p);
                }
            }
            __boot_serial_puts("\n");
        }
    }
    c3_scm_right_discard(token);
    return 0;
}

static int c3_recvmsg_drain_rights(task_t *cur,int sref,void *control,size_t ctrl_cap,
                                   size_t *ctrl_used,int flags,int *msg_flags){
    int delivered=0;
    if(!control||!ctrl_used||ctrl_cap<sizeof(c3_cmsghdr_t))return 0;
    for(;;){
        int pass_fd;
        int recv_fd;
        bool allocated=false;
        int crc=sock_recv_right(sref,&pass_fd);
        if(crc!=0)break;
        recv_fd=pass_fd;
        if(c3_scm_right_is_token(pass_fd)){
            int trc=c3_scm_right_take(cur,pass_fd,flags,&recv_fd);
            if(trc<0)return delivered?delivered:trc;
            allocated=true;
        }else if(!cur||!fd_valid(&cur->fdt,pass_fd)){
            return delivered?delivered:-EBADF;
        }
        if(!c3_recvmsg_put_cmsg(control,ctrl_cap,ctrl_used,
                                C3_SOL_SOCKET,C3_SCM_RIGHTS,
                                &recv_fd,sizeof(recv_fd),msg_flags)){
            if(allocated)(void)c3_close_fd_for_task(cur,recv_fd);
            break;
        }
        ++delivered;
        if(delivered>=8)break;
    }
    return delivered;
}

static task_t *c3_socket_owner_task(int sref,const task_t *skip){
    int i,fd;
    if(sref<0||sref>=SOCK_MAX)return 0;
    for(i=0;i<TASK_MAX;++i){
        task_t *t=&g_tasks[i];
        if(!t->used||t==skip)continue;
        if(t->state==TASK_ZOMBIE||t->state==TASK_FREE)continue;
        for(fd=0;fd<TASK_FD_MAX;++fd){
            if(t->fdt.fds[fd].kind==FDKIND_SOCKET&&t->fdt.fds[fd].ref==sref)return t;
        }
    }
    for(i=0;i<TASK_MAX;++i){
        task_t *t=&g_tasks[i];
        if(!t->used)continue;
        if(t->state==TASK_ZOMBIE||t->state==TASK_FREE)continue;
        for(fd=0;fd<TASK_FD_MAX;++fd){
            if(t->fdt.fds[fd].kind==FDKIND_SOCKET&&t->fdt.fds[fd].ref==sref)return t;
        }
    }
    return 0;
}

static void c3_recvmsg_make_ucred(task_t *cur,int sref,c3_ucred_t *uc){
    socket_t *s;
    task_t *owner=0;
    if(!uc)return;
    uc->pid=cur?cur->pid:1;
    uc->uid=cur?cur->euid:0;
    uc->gid=cur?cur->egid:0;
    if(sref<0||sref>=SOCK_MAX||!g_sockets[sref].used)return;
    s=&g_sockets[sref];
    if(s->peer>=0&&s->peer<SOCK_MAX)owner=c3_socket_owner_task(s->peer,cur);
    if(!owner&&cur&&cur->ppid>0)owner=c3_task_by_pid(cur->ppid);
    if(owner){
        uc->pid=owner->tgid?owner->tgid:owner->pid;
        uc->uid=owner->euid;
        uc->gid=owner->egid;
    }
}

int64_t real_sys_sendmsg(int fd,const void *msg,int flags){
    task_t *cur=task_current();
    const c3_msghdr_t *mh=(const c3_msghdr_t*)msg;
    const c3_cmsghdr_t *ch;
    size_t cneed=sizeof(c3_cmsghdr_t)+sizeof(int);
    uint32_t ip=0;
    uint16_t port=0;
    int sref;
    int rc;
    int64_t out_rc;
    if(!fd_valid(&cur->fdt,fd)||cur->fdt.fds[fd].kind!=FDKIND_SOCKET)return -ENOTSOCK;
    if(!mh)return -EFAULT;
    if(mh->msg_iovlen>C3_MSG_IOV_MAX)return -EINVAL;
    if(mh->msg_iovlen&&(!mh->msg_iov))return -EFAULT;
    sref=cur->fdt.fds[fd].ref;
    if(c3_task_is_firefox_ipc_trace(cur)&&c3_socket_ref_virtual_service(sref)==SOCK_VIRT_NONE){
        static uint32_t sendmsg_entry_trace=0;
        if(sendmsg_entry_trace<140){
            socket_t *s=(sref>=0&&sref<SOCK_MAX&&g_sockets[sref].used)?&g_sockets[sref]:0;
            ++sendmsg_entry_trace;
            __boot_serial_puts("[sendmsg] pid=");
            __boot_serial_putu32((uint32_t)(cur?cur->pid:0));
            c3_trace_task_name(cur);
            __boot_serial_puts(" fd=");
            __boot_serial_putu32((uint32_t)fd);
            __boot_serial_puts(" ref=");
            __boot_serial_putu32((uint32_t)sref);
            __boot_serial_puts(" flags=");
            __boot_serial_puthex64((uint64_t)(uint32_t)flags);
            __boot_serial_puts(" iovlen=");
            __boot_serial_putu32((uint32_t)mh->msg_iovlen);
            __boot_serial_puts(" controllen=");
            __boot_serial_puthex64((uint64_t)mh->msg_controllen);
            if(s){
                __boot_serial_puts(" peer=");
                __boot_serial_putu32((uint32_t)s->peer);
                __boot_serial_puts(" rx=");
                __boot_serial_putu32(c3_socket_rx_used(sref));
                __boot_serial_puts(" peer_rx=");
                __boot_serial_putu32(c3_socket_rx_used(s->peer));
                __boot_serial_puts(" space=");
                __boot_serial_putu32(c3_socket_send_space(sref));
                __boot_serial_puts(" anc=");
                __boot_serial_putu32((uint32_t)(uint8_t)(s->anc_head-s->anc_tail));
            }
            __boot_serial_puts("\n");
        }
    }
    if(sref>=0&&sref<SOCK_MAX&&g_sockets[sref].used&&g_sockets[sref].domain==16){
        size_t i,total=0;
        if(mh->msg_name&&mh->msg_namelen>=sizeof(uint16_t)){
            const c3_sockaddr_nl_t *nl=(const c3_sockaddr_nl_t*)mh->msg_name;
            if(nl->nl_family!=16)return -EINVAL;
        }
        for(i=0;i<mh->msg_iovlen;++i){
            if(mh->msg_iov[i].iov_len&&!mh->msg_iov[i].iov_base)return -EFAULT;
            total+=mh->msg_iov[i].iov_len;
        }
        (void)flags;
        return (int64_t)total;
    }
    if(mh->msg_name){
        if(mh->msg_namelen<sizeof(uint16_t))return -EINVAL;
        if(c3_sockaddr_target(mh->msg_name,mh->msg_namelen,&ip,&port)<0)return -EINVAL;
        sock_connect(sref,ip,port);
    }
    if(mh->msg_control&&mh->msg_controllen>=sizeof(c3_cmsghdr_t)){
        ch=(const c3_cmsghdr_t*)mh->msg_control;
        if(ch->cmsg_level==C3_SOL_SOCKET&&ch->cmsg_type==C3_SCM_RIGHTS){
            int pass_fd;
            int queued_fd;
            if(ch->cmsg_len<cneed||mh->msg_controllen<cneed)return -EINVAL;
            pass_fd=*(const int*)((const uint8_t*)mh->msg_control+sizeof(c3_cmsghdr_t));
            queued_fd=c3_scm_right_store(cur,pass_fd);
            if(queued_fd<0)return queued_fd;
            rc=sock_send_right(sref,queued_fd);
            if(rc<0){
                c3_scm_right_discard(queued_fd);
                return rc;
            }
        }
    }
    if(!mh->msg_iovlen){
        out_rc=0;
    }else{
        out_rc=c3_sendmsg_iov(sref,mh->msg_iov,mh->msg_iovlen,flags);
    }
    if(c3_task_is_firefox_ipc_trace(cur)&&c3_socket_ref_virtual_service(sref)==SOCK_VIRT_NONE){
        static uint32_t sendmsg_ret_trace=0;
        if(sendmsg_ret_trace<140){
            ++sendmsg_ret_trace;
            __boot_serial_puts("[sendmsg-ret] pid=");
            __boot_serial_putu32((uint32_t)(cur?cur->pid:0));
            c3_trace_task_name(cur);
            __boot_serial_puts(" fd=");
            __boot_serial_putu32((uint32_t)fd);
            __boot_serial_puts(" ref=");
            __boot_serial_putu32((uint32_t)sref);
            __boot_serial_puts(" rc=");
            if(out_rc<0){__boot_serial_puts("-");__boot_serial_putu32((uint32_t)(-out_rc));}
            else __boot_serial_puthex64((uint64_t)out_rc);
            __boot_serial_puts(" rx=");
            __boot_serial_putu32(c3_socket_rx_used(sref));
            if(sref>=0&&sref<SOCK_MAX&&g_sockets[sref].used){
                socket_t *s=&g_sockets[sref];
                __boot_serial_puts(" peer_rx=");
                __boot_serial_putu32(c3_socket_rx_used(s->peer));
                __boot_serial_puts(" space=");
                __boot_serial_putu32(c3_socket_send_space(sref));
            }
            __boot_serial_puts("\n");
        }
    }
    return out_rc;
}

int64_t real_sys_recvmsg(int fd,void *msg,int flags){
    task_t *cur=task_current();
    c3_msghdr_t *mh=(c3_msghdr_t*)msg;
    int sref;
    size_t ctrl_cap;
    size_t ctrl_used=0;
    int64_t rc;
    if(!fd_valid(&cur->fdt,fd)||cur->fdt.fds[fd].kind!=FDKIND_SOCKET)return -ENOTSOCK;
    if(!mh)return -EFAULT;
    if(mh->msg_iovlen>C3_MSG_IOV_MAX)return -EINVAL;
    if(mh->msg_iovlen&&(!mh->msg_iov))return -EFAULT;
    sref=cur->fdt.fds[fd].ref;
    if(c3_task_is_firefox_ipc_trace(cur)&&c3_socket_ref_virtual_service(sref)==SOCK_VIRT_NONE){
        static uint32_t recvmsg_entry_trace=0;
        if(recvmsg_entry_trace<140){
            socket_t *s=(sref>=0&&sref<SOCK_MAX&&g_sockets[sref].used)?&g_sockets[sref]:0;
            ++recvmsg_entry_trace;
            __boot_serial_puts("[recvmsg] pid=");
            __boot_serial_putu32((uint32_t)(cur?cur->pid:0));
            c3_trace_task_name(cur);
            __boot_serial_puts(" fd=");
            __boot_serial_putu32((uint32_t)fd);
            __boot_serial_puts(" ref=");
            __boot_serial_putu32((uint32_t)sref);
            __boot_serial_puts(" flags=");
            __boot_serial_puthex64((uint64_t)(uint32_t)flags);
            __boot_serial_puts(" iovlen=");
            __boot_serial_putu32((uint32_t)mh->msg_iovlen);
            __boot_serial_puts(" controllen=");
            __boot_serial_puthex64((uint64_t)mh->msg_controllen);
            if(s){
                __boot_serial_puts(" peer=");
                __boot_serial_putu32((uint32_t)s->peer);
                __boot_serial_puts(" rx=");
                __boot_serial_putu32(c3_socket_rx_used(sref));
                __boot_serial_puts(" anc=");
                __boot_serial_putu32((uint32_t)(uint8_t)(s->anc_head-s->anc_tail));
                __boot_serial_puts(" st=");
                __boot_serial_putu32((uint32_t)s->tcp_state);
            }
            __boot_serial_puts("\n");
        }
    }
    if(sref>=0&&sref<SOCK_MAX&&g_sockets[sref].used&&g_sockets[sref].domain==16){
        if(mh->msg_name){
            c3_sockaddr_nl_t *nl;
            if(mh->msg_namelen<sizeof(c3_sockaddr_nl_t))return -EINVAL;
            nl=(c3_sockaddr_nl_t*)mh->msg_name;
            c3_memset(nl,0,sizeof(*nl));
            nl->nl_family=16;
            nl->nl_pid=(uint32_t)(cur?cur->pid:0);
            mh->msg_namelen=sizeof(*nl);
        }else{
            mh->msg_namelen=0;
        }
        mh->msg_flags=0;
        mh->msg_controllen=0;
        (void)flags;
        return -EAGAIN;
    }
    if(mh->msg_name){
        uint32_t nlen=mh->msg_namelen;
        int64_t nrc=real_sys_getpeername(fd,mh->msg_name,&nlen);
        if(nrc<0)return nrc;
        mh->msg_namelen=nlen;
    }else{
        mh->msg_namelen=0;
    }
    mh->msg_flags=0;
    ctrl_cap=mh->msg_control?mh->msg_controllen:0;
    if(mh->msg_control&&ctrl_cap>=sizeof(c3_cmsghdr_t)){
        int drc=c3_recvmsg_drain_rights(cur,sref,mh->msg_control,ctrl_cap,
                                        &ctrl_used,flags,&mh->msg_flags);
        if(drc<0)return drc;
        if(sref>=0&&sref<SOCK_MAX&&g_sockets[sref].used&&
           (g_sockets[sref].virt_flags&C3_SOCK_VFLAG_PASSCRED)){
            c3_ucred_t uc;
            c3_recvmsg_make_ucred(cur,sref,&uc);
            (void)c3_recvmsg_put_cmsg(mh->msg_control,ctrl_cap,&ctrl_used,
                                      C3_SOL_SOCKET,C3_SCM_CREDENTIALS,
                                      &uc,sizeof(uc),&mh->msg_flags);
            {
                static uint32_t trace_count=0;
                if(trace_count<32){
                    ++trace_count;
                    __boot_serial_puts("[recvmsg-cred] pid=");
                    __boot_serial_putu32(cur?(uint32_t)cur->pid:0);
                    c3_trace_task_name(cur);
                    __boot_serial_puts(" fd=");
                    __boot_serial_putu32((uint32_t)fd);
                    __boot_serial_puts(" sref=");
                    __boot_serial_putu32((uint32_t)sref);
                    __boot_serial_puts(" cred.pid=");
                    __boot_serial_putu32((uint32_t)uc.pid);
                    __boot_serial_puts(" used=");
                    __boot_serial_putu32((uint32_t)ctrl_used);
                    __boot_serial_puts(" flags=");
                    __boot_serial_puthex64((uint64_t)(uint32_t)mh->msg_flags);
                    __boot_serial_puts("\n");
                }
            }
        }
    }else{
        mh->msg_controllen=0;
    }
    if(!mh->msg_iovlen){
        if(mh->msg_control&&ctrl_cap>=sizeof(c3_cmsghdr_t))mh->msg_controllen=ctrl_used;
        if(c3_task_is_firefox_ipc_trace(cur)&&c3_socket_ref_virtual_service(sref)==SOCK_VIRT_NONE){
            static uint32_t recvmsg_zero_trace=0;
            if(recvmsg_zero_trace<80){
                ++recvmsg_zero_trace;
                __boot_serial_puts("[recvmsg-ret] pid=");
                __boot_serial_putu32((uint32_t)(cur?cur->pid:0));
                c3_trace_task_name(cur);
                __boot_serial_puts(" fd=");
                __boot_serial_putu32((uint32_t)fd);
                __boot_serial_puts(" ref=");
                __boot_serial_putu32((uint32_t)sref);
                __boot_serial_puts(" rc=0 ctrl=");
                __boot_serial_puthex64((uint64_t)mh->msg_controllen);
                __boot_serial_puts("\n");
            }
        }
        return 0;
    }
    if(cur->fdt.fds[fd].flags&FDFL_NONBLOCK)flags|=C3_MSG_DONTWAIT;
    rc=c3_recvmsg_iov(sref,mh->msg_iov,mh->msg_iovlen,flags);
    if(mh->msg_control&&ctrl_cap>=sizeof(c3_cmsghdr_t))mh->msg_controllen=ctrl_used;
    else mh->msg_controllen=0;
    c3_trace_firefox_ipc_recv_payload(cur,fd,rc,mh);
    if(c3_task_is_firefox_ipc_trace(cur)&&c3_socket_ref_virtual_service(sref)==SOCK_VIRT_NONE){
        static uint32_t recvmsg_ret_trace=0;
        if(recvmsg_ret_trace<140){
            socket_t *s=(sref>=0&&sref<SOCK_MAX&&g_sockets[sref].used)?&g_sockets[sref]:0;
            ++recvmsg_ret_trace;
            __boot_serial_puts("[recvmsg-ret] pid=");
            __boot_serial_putu32((uint32_t)(cur?cur->pid:0));
            c3_trace_task_name(cur);
            __boot_serial_puts(" fd=");
            __boot_serial_putu32((uint32_t)fd);
            __boot_serial_puts(" ref=");
            __boot_serial_putu32((uint32_t)sref);
            __boot_serial_puts(" rc=");
            if(rc<0){__boot_serial_puts("-");__boot_serial_putu32((uint32_t)(-rc));}
            else __boot_serial_puthex64((uint64_t)rc);
            __boot_serial_puts(" ctrl=");
            __boot_serial_puthex64((uint64_t)mh->msg_controllen);
            if(s){
                __boot_serial_puts(" rx_after=");
                __boot_serial_putu32(c3_socket_rx_used(sref));
                __boot_serial_puts(" anc_after=");
                __boot_serial_putu32((uint32_t)(uint8_t)(s->anc_head-s->anc_tail));
            }
            __boot_serial_puts("\n");
        }
    }
    return rc;
}

int64_t real_sys_sendmmsg(int fd,void *vmessages,unsigned int vlen,unsigned int flags){
    task_t *cur=task_current();
    c3_mmsghdr_t *vec=(c3_mmsghdr_t*)vmessages;
    unsigned int i,count=0;
    if(!fd_valid(&cur->fdt,fd)||cur->fdt.fds[fd].kind!=FDKIND_SOCKET)return -ENOTSOCK;
    if(!vec)return -EFAULT;
    if(vlen==0)return 0;
    if(vlen>C3_MMSG_MAX)vlen=C3_MMSG_MAX;
    for(i=0;i<vlen;++i){
        int64_t rc=real_sys_sendmsg(fd,&vec[i].msg_hdr,(int)flags);
        if(rc<0)return count?(int64_t)count:rc;
        vec[i].msg_len=(uint32_t)rc;
        ++count;
    }
    return (int64_t)count;
}

int64_t real_sys_recvmmsg(int fd,void *vmessages,unsigned int vlen,unsigned int flags,const timespec_t *timeout){
    task_t *cur=task_current();
    c3_mmsghdr_t *vec=(c3_mmsghdr_t*)vmessages;
    unsigned int count=0;
    uint64_t deadline=0;
    int rflags=(int)flags;
    if(!fd_valid(&cur->fdt,fd)||cur->fdt.fds[fd].kind!=FDKIND_SOCKET)return -ENOTSOCK;
    if(!vec)return -EFAULT;
    if(vlen==0)return 0;
    if(vlen>C3_MMSG_MAX)vlen=C3_MMSG_MAX;
    if(timeout){
        if(timeout->tv_sec<0||timeout->tv_nsec<0||timeout->tv_nsec>=1000000000LL)return -EINVAL;
        if(timeout->tv_sec||timeout->tv_nsec){
            uint64_t ns=(uint64_t)timeout->tv_sec*1000000000ULL+(uint64_t)timeout->tv_nsec;
            deadline=c3_rdtsc()+(ns*g_tsc_freq_approx)/1000000000ULL;
        }else{
            rflags|=C3_MSG_DONTWAIT;
        }
    }
    while(count<vlen){
        int64_t rc=real_sys_recvmsg(fd,&vec[count].msg_hdr,rflags);
        if(rc<0){
            if(rc==-EAGAIN){
                if(count)return (int64_t)count;
                if(rflags&C3_MSG_DONTWAIT)return -EAGAIN;
                if(deadline&&c3_rdtsc()>=deadline)return 0;
                if(deadline){
                    cur->sleep_until=deadline;
                    cur->state=TASK_SLEEPING;
                    task_schedule();
                    if(cur->state==TASK_SLEEPING){
                        cur->state=TASK_RUNNING;
                        __asm__ volatile("pause");
                    }
                    continue;
                }
            }
            return count?(int64_t)count:rc;
        }
        vec[count].msg_len=(uint32_t)rc;
        ++count;
        if((flags&C3_MSG_WAITFORONE)&&count==1)rflags|=C3_MSG_DONTWAIT;
        if(rc==0)break;
    }
    return (int64_t)count;
}

/* Time / clock syscalls */
#define C3_CLOCK_REALTIME            0
#define C3_CLOCK_MONOTONIC           1
#define C3_CLOCK_PROCESS_CPUTIME_ID  2
#define C3_CLOCK_THREAD_CPUTIME_ID   3
#define C3_CLOCK_MONOTONIC_RAW       4
#define C3_CLOCK_REALTIME_COARSE     5
#define C3_CLOCK_MONOTONIC_COARSE    6
#define C3_CLOCK_BOOTTIME            7
#define C3_REALTIME_BOOT_EPOCH_SEC   1777248000LL /* 2026-04-27 00:00:00 UTC */

static uint64_t tsc_to_ns(uint64_t tsc){return tsc*1000000000ULL/g_tsc_freq_approx;}

int64_t real_sys_clock_gettime(int clockid,timespec_t *tp){
    uint64_t elapsed_ns;
    if(!tp)return -EFAULT;
    if(!g_boot_tsc)g_boot_tsc=c3_rdtsc();
    elapsed_ns=tsc_to_ns(c3_rdtsc()-g_boot_tsc);
    switch(clockid){
        case C3_CLOCK_REALTIME:
        case C3_CLOCK_REALTIME_COARSE:
            tp->tv_sec=(int64_t)(elapsed_ns/1000000000ULL)+C3_REALTIME_BOOT_EPOCH_SEC;
            tp->tv_nsec=(int64_t)(elapsed_ns%1000000000ULL);
            return 0;
        case C3_CLOCK_MONOTONIC:
        case C3_CLOCK_MONOTONIC_RAW:
        case C3_CLOCK_MONOTONIC_COARSE:
        case C3_CLOCK_BOOTTIME:
        case C3_CLOCK_PROCESS_CPUTIME_ID:
        case C3_CLOCK_THREAD_CPUTIME_ID:
            tp->tv_sec=(int64_t)(elapsed_ns/1000000000ULL);
            tp->tv_nsec=(int64_t)(elapsed_ns%1000000000ULL);
            return 0;
        default:
            return -EINVAL;
    }
}

static bool c3_clockid_supported(int clockid){
    switch(clockid){
        case C3_CLOCK_REALTIME:
        case C3_CLOCK_MONOTONIC:
        case C3_CLOCK_PROCESS_CPUTIME_ID:
        case C3_CLOCK_THREAD_CPUTIME_ID:
        case C3_CLOCK_MONOTONIC_RAW:
        case C3_CLOCK_REALTIME_COARSE:
        case C3_CLOCK_MONOTONIC_COARSE:
        case C3_CLOCK_BOOTTIME:
            return true;
        default:
            return false;
    }
}

int64_t real_sys_clock_getres(int clockid,timespec_t *tp){
    if(!c3_clockid_supported(clockid))return -EINVAL;
    if(tp){tp->tv_sec=0;tp->tv_nsec=1000;}
    return 0;
}

int64_t real_sys_gettimeofday(timeval_t *tv,timezone_t *tz){
    if(tv){
        timespec_t ts;real_sys_clock_gettime(0,&ts);
        tv->tv_sec=ts.tv_sec;tv->tv_usec=ts.tv_nsec/1000;
    }
    if(tz){tz->tz_minuteswest=0;tz->tz_dsttime=0;}
    return 0;
}

int64_t real_sys_time(int64_t *tloc){
    timespec_t ts;
    int64_t rc=real_sys_clock_gettime(0,&ts);
    if(rc<0)return rc;
    if(tloc)*tloc=ts.tv_sec;
    return ts.tv_sec;
}

int64_t real_sys_nanosleep(const timespec_t *req,timespec_t *rem){
    task_t *cur=task_current();
    uint64_t ns,ticks,deadline,now;
    if(!req)return -EFAULT;
    if(req->tv_sec<0||req->tv_nsec<0||req->tv_nsec>=1000000000LL)return -EINVAL;
    if(c3_task_has_unblocked_signal(cur)){
        if(rem)c3_memcpy(rem,req,sizeof(*rem));
        return -EINTR;
    }
    ns=(uint64_t)req->tv_sec*1000000000ULL+(uint64_t)req->tv_nsec;
    ticks=ns/10000000ULL; /* ~10ms per tick */
    if(ticks<1&&ns)ticks=1;
    deadline=c3_rdtsc()+ticks*g_tsc_freq_approx/100;
    for(;;){
        now=c3_rdtsc();
        if(now>=deadline)break;
        if(c3_task_has_unblocked_signal(cur)){
            if(rem){
                uint64_t left_ns=(deadline>now)?((deadline-now)*1000000000ULL/g_tsc_freq_approx):0;
                rem->tv_sec=(int64_t)(left_ns/1000000000ULL);
                rem->tv_nsec=(int64_t)(left_ns%1000000000ULL);
            }
            return -EINTR;
        }
        cur->sleep_until=deadline;
        cur->state=TASK_SLEEPING;
        task_schedule();
        if(cur->state==TASK_SLEEPING){
            cur->state=TASK_RUNNING;
            __asm__ volatile("pause");
        }
    }
    if(rem){rem->tv_sec=0;rem->tv_nsec=0;}
    return 0;
}

int64_t real_sys_alarm(uint32_t seconds){return(int64_t)sys_alarm(seconds);}

int64_t real_sys_timer_create(int clockid,void *sevp,int *timerid){
    (void)clockid;(void)sevp;
    int id=timer_create_entry(task_current()->pid,0,0,SIGALRM);
    if(timerid)*timerid=id;
    return id>=0?0:-ENOMEM;
}
int64_t real_sys_timer_settime(int id,int flags,const void *nv,void *ov){
    (void)id;(void)flags;(void)nv;(void)ov;return 0;
}

int64_t real_sys_sysinfo(sysinfo_t *info){
    if(!info)return -EFAULT;
    c3_memset(info,0,sizeof(sysinfo_t));
    info->uptime=tsc_to_ns(c3_rdtsc()-g_boot_tsc)/1000000000ULL;
    info->totalram=(uint64_t)pmm_total_count()*PAGE_SIZE;
    info->freeram=(uint64_t)pmm_free_count()*PAGE_SIZE;
    info->procs=1;{int i;for(i=0;i<TASK_MAX;++i)if(g_tasks[i].used)info->procs++;}
    info->mem_unit=1;
    info->loads[0]=100;info->loads[1]=50;info->loads[2]=25;
    return 0;
}

/* Linux ABI syscalls */
int64_t real_sys_uname(utsname_t *buf){
    if(!buf)return -EFAULT;
    c3_memset(buf,0,sizeof(utsname_t));
    c3_strlcpy(buf->sysname,"Linux",65);
    c3_strlcpy(buf->nodename,"ridux",65);
    c3_strlcpy(buf->release,"6.1.0-ridux",65);
    c3_strlcpy(buf->version,"#1 SMP PREEMPT_DYNAMIC RiduxOS 1.0",65);
    c3_strlcpy(buf->machine,"x86_64",65);
    c3_strlcpy(buf->domainname,"(none)",65);
    return 0;
}

int64_t real_sys_getrandom(void *buf,size_t buflen,unsigned int flags){
    #define C3_GRND_NONBLOCK 0x0001u
    #define C3_GRND_RANDOM   0x0002u
    #define C3_GRND_INSECURE 0x0004u
    if(flags&~(C3_GRND_NONBLOCK|C3_GRND_RANDOM|C3_GRND_INSECURE))return -EINVAL;
    if(!buf&&buflen)return -EFAULT;
    if(!buflen)return 0;
    return(int64_t)dev_random_read(buf,buflen);
}

#define C3_RLIMIT_CPU        0
#define C3_RLIMIT_FSIZE      1
#define C3_RLIMIT_DATA       2
#define C3_RLIMIT_STACK      3
#define C3_RLIMIT_CORE       4
#define C3_RLIMIT_RSS        5
#define C3_RLIMIT_NPROC      6
#define C3_RLIMIT_NOFILE     7
#define C3_RLIMIT_MEMLOCK    8
#define C3_RLIMIT_AS         9
#define C3_RLIMIT_LOCKS      10
#define C3_RLIMIT_SIGPENDING 11
#define C3_RLIMIT_MSGQUEUE   12
#define C3_RLIMIT_NICE       13
#define C3_RLIMIT_RTPRIO     14
#define C3_RLIMIT_RTTIME     15
#define C3_RLIM_INFINITY (~0ULL)

typedef struct __attribute__((packed)){
    uint64_t rlim_cur;
    uint64_t rlim_max;
} c3_rlimit64_t;

static bool c3_rlimit_resource_valid(int resource){
    return resource>=0&&resource<C3_RLIMIT_RESOURCE_MAX;
}

static void c3_rlimit_default_pair(int resource,c3_rlimit_pair_t *p){
    if(!p)return;
    p->cur=C3_RLIM_INFINITY;
    p->max=C3_RLIM_INFINITY;
    switch(resource){
        case C3_RLIMIT_STACK: p->cur=8ULL*1024ULL*1024ULL; break;
        case C3_RLIMIT_CORE: p->cur=0; p->max=0; break;
        case C3_RLIMIT_NPROC: p->cur=256; p->max=256; break;
        case C3_RLIMIT_NOFILE: p->cur=1024; p->max=1024; break;
        case C3_RLIMIT_SIGPENDING: p->cur=256; p->max=256; break;
        case C3_RLIMIT_MSGQUEUE: p->cur=8ULL*1024ULL*1024ULL; p->max=8ULL*1024ULL*1024ULL; break;
        case C3_RLIMIT_NICE: p->cur=0; p->max=0; break;
        case C3_RLIMIT_RTPRIO: p->cur=0; p->max=0; break;
        case C3_RLIMIT_RTTIME: p->cur=C3_RLIM_INFINITY; p->max=C3_RLIM_INFINITY; break;
    }
}

static int c3_rlimit_task_index(int pid){
    if(pid==0)return g_current_task;
    return c3_task_index_by_pid(pid);
}

static void c3_rlimit_ensure_task_defaults(int tidx){
    int r;
    if(tidx<0||tidx>=TASK_MAX)return;
    if(g_c3_rlimits_inited[tidx]&&g_c3_rlimits_pid[tidx]==g_tasks[tidx].pid)return;
    for(r=0;r<C3_RLIMIT_RESOURCE_MAX;++r)c3_rlimit_default_pair(r,&g_c3_rlimits[tidx][r]);
    g_c3_rlimits_inited[tidx]=1;
    g_c3_rlimits_pid[tidx]=g_tasks[tidx].pid;
}

int64_t real_sys_getrlimit(int resource,void *rlim){
    int tidx=g_current_task;
    c3_rlimit64_t *out=(c3_rlimit64_t*)rlim;
    if(!rlim)return -EFAULT;
    if(!c3_rlimit_resource_valid(resource))return -EINVAL;
    if(tidx<0||tidx>=TASK_MAX)return -ESRCH;
    c3_rlimit_ensure_task_defaults(tidx);
    out->rlim_cur=g_c3_rlimits[tidx][resource].cur;
    out->rlim_max=g_c3_rlimits[tidx][resource].max;
    return 0;
}

int64_t real_sys_setrlimit(int resource,const void *rlim){
    int tidx=g_current_task;
    const c3_rlimit64_t *in=(const c3_rlimit64_t*)rlim;
    if(!in)return -EFAULT;
    if(!c3_rlimit_resource_valid(resource))return -EINVAL;
    if(tidx<0||tidx>=TASK_MAX)return -ESRCH;
    if(in->rlim_cur>in->rlim_max)return -EINVAL;
    c3_rlimit_ensure_task_defaults(tidx);
    g_c3_rlimits[tidx][resource].cur=in->rlim_cur;
    g_c3_rlimits[tidx][resource].max=in->rlim_max;
    return 0;
}

int64_t real_sys_prlimit64(int pid,int resource,const void *new_rlim,void *old_rlim){
    int tidx=c3_rlimit_task_index(pid);
    task_t *cur=task_current();
    c3_rlimit64_t *oldp=(c3_rlimit64_t*)old_rlim;
    const c3_rlimit64_t *newp=(const c3_rlimit64_t*)new_rlim;
    if(!c3_rlimit_resource_valid(resource))return -EINVAL;
    if(!cur)return -ESRCH;
    if(tidx<0||tidx>=TASK_MAX||!g_tasks[tidx].used)return -ESRCH;
    if(pid!=0&&g_tasks[tidx].pid!=cur->pid&&cur->uid!=0&&cur->uid!=g_tasks[tidx].uid)return -EPERM;
    c3_rlimit_ensure_task_defaults(tidx);
    if(oldp){
        oldp->rlim_cur=g_c3_rlimits[tidx][resource].cur;
        oldp->rlim_max=g_c3_rlimits[tidx][resource].max;
    }
    if(newp){
        if(newp->rlim_cur>newp->rlim_max)return -EINVAL;
        g_c3_rlimits[tidx][resource].cur=newp->rlim_cur;
        g_c3_rlimits[tidx][resource].max=newp->rlim_max;
    }
    return 0;
}

typedef struct __attribute__((packed)){
    int64_t  f_type;
    int64_t  f_bsize;
    uint64_t f_blocks;
    uint64_t f_bfree;
    uint64_t f_bavail;
    uint64_t f_files;
    uint64_t f_ffree;
    int32_t  f_fsid[2];
    int64_t  f_namelen;
    int64_t  f_frsize;
    int64_t  f_flags;
    int64_t  f_spare[4];
} c3_statfs_t;

#define C3_TMPFS_MAGIC 0x01021994LL

static void c3_fill_statfs(c3_statfs_t *st){
    uint64_t free_pages=pmm_free_count();
    uint64_t total_pages=pmm_total_count();
    if(!st)return;
    c3_memset(st,0,sizeof(*st));
    st->f_type=C3_TMPFS_MAGIC;
    st->f_bsize=PAGE_SIZE;
    st->f_frsize=PAGE_SIZE;
    st->f_blocks=total_pages?total_pages:1;
    st->f_bfree=free_pages;
    st->f_bavail=free_pages;
    st->f_files=OPEN_MAX_FILES+TASK_FD_MAX;
    st->f_ffree=OPEN_MAX_FILES;
    st->f_fsid[0]=0x52494455; /* RIDU */
    st->f_fsid[1]=0x58465300; /* XFS\0-ish */
    st->f_namelen=255;
    st->f_flags=0;
}

int64_t real_sys_statfs(const char *path,void *buf){
    if(!path||!buf)return -EFAULT;
    c3_fill_statfs((c3_statfs_t*)buf);
    return 0;
}

int64_t real_sys_fstatfs(int fd,void *buf){
    task_t *cur=task_current();
    if(!cur)return -ESRCH;
    if(!buf)return -EFAULT;
    if(fd<0||!fd_valid(&cur->fdt,fd))return -EBADF;
    c3_fill_statfs((c3_statfs_t*)buf);
    return 0;
}

int64_t real_sys_readahead(int fd,uint64_t offset,size_t count){
    task_t *cur=task_current();
    (void)offset;
    (void)count;
    if(!cur)return -ESRCH;
    if(fd<0||!fd_valid(&cur->fdt,fd))return -EBADF;
    return 0;
}

#define PR_GET_DUMPABLE 3
#define PR_SET_DUMPABLE 4
#define PR_SET_NAME 15
#define PR_GET_NAME 16
#define PR_GET_SECCOMP 21
#define PR_SET_SECCOMP 22
#define PR_CAPBSET_READ 23
#define PR_CAPBSET_DROP 24
#define PR_SET_NO_NEW_PRIVS 38
#define PR_GET_NO_NEW_PRIVS 39
#define PR_SET_PTRACER 0x59616d61

#define C3_SECCOMP_SET_MODE_STRICT 0u
#define C3_SECCOMP_SET_MODE_FILTER 1u
#define C3_SECCOMP_GET_ACTION_AVAIL 2u
#define C3_SECCOMP_GET_NOTIF_SIZES 3u

#define C3_SECCOMP_MODE_DISABLED 0u
#define C3_SECCOMP_MODE_STRICT   1u
#define C3_SECCOMP_MODE_FILTER   2u

#define C3_SECCOMP_FILTER_FLAG_TSYNC       (1u<<0)
#define C3_SECCOMP_FILTER_FLAG_LOG         (1u<<1)
#define C3_SECCOMP_FILTER_FLAG_SPEC_ALLOW  (1u<<2)
#define C3_SECCOMP_FILTER_FLAG_NEW_LISTENER (1u<<3)
#define C3_SECCOMP_FILTER_FLAG_TSYNC_ESRCH (1u<<4)

#define C3_SECCOMP_RET_KILL_PROCESS 0x80000000u
#define C3_SECCOMP_RET_KILL_THREAD  0x00000000u
#define C3_SECCOMP_RET_TRAP         0x00030000u
#define C3_SECCOMP_RET_ERRNO        0x00050000u
#define C3_SECCOMP_RET_TRACE        0x7ff00000u
#define C3_SECCOMP_RET_LOG          0x7ffc0000u
#define C3_SECCOMP_RET_ALLOW        0x7fff0000u

#define C3_CLONE_SYSVSEM   0x00040000ULL
#define C3_CLONE_NEWNS     0x00020000ULL
#define C3_CLONE_NEWCGROUP 0x02000000ULL
#define C3_CLONE_NEWUTS    0x04000000ULL
#define C3_CLONE_NEWIPC    0x08000000ULL
#define C3_CLONE_NEWUSER   0x10000000ULL
#define C3_CLONE_NEWPID    0x20000000ULL
#define C3_CLONE_NEWNET    0x40000000ULL
#define C3_CLONE_NEWTIME   0x00000080ULL
#define C3_NS_CLONE_MASK (C3_CLONE_NEWNS|C3_CLONE_NEWCGROUP|C3_CLONE_NEWUTS|C3_CLONE_NEWIPC|C3_CLONE_NEWUSER|C3_CLONE_NEWPID|C3_CLONE_NEWNET|C3_CLONE_NEWTIME)

#define C3_LINUX_CAP_VERSION_3 0x20080522u
#define C3_LINUX_CAPABILITY_U32S_3 2u

typedef struct __attribute__((packed)){
    uint16_t seccomp_notif;
    uint16_t seccomp_notif_resp;
    uint16_t seccomp_data;
} c3_seccomp_notif_sizes_t;

typedef struct __attribute__((packed)){
    uint32_t version;
    int32_t  pid;
} c3_cap_user_header_t;

typedef struct __attribute__((packed)){
    uint32_t effective;
    uint32_t permitted;
    uint32_t inheritable;
} c3_cap_user_data_t;

#define C3_UFFD_USER_MODE_ONLY 0x1

static int64_t c3_process_vm_copy_iov(const iovec_t *dst_iov,unsigned long dst_cnt,const iovec_t *src_iov,unsigned long src_cnt){
    unsigned long di=0,si=0;
    size_t doff=0,soff=0;
    uint64_t copied=0;
    if(!dst_iov||!src_iov)return -EFAULT;
    if(dst_cnt>1024||src_cnt>1024)return -EINVAL;
    while(di<dst_cnt&&si<src_cnt){
        const iovec_t *dv=&dst_iov[di];
        const iovec_t *sv=&src_iov[si];
        uint8_t *db=(uint8_t*)dv->iov_base;
        const uint8_t *sb=(const uint8_t*)sv->iov_base;
        size_t dleft,sleft,n;
        if(doff>=dv->iov_len){++di;doff=0;continue;}
        if(soff>=sv->iov_len){++si;soff=0;continue;}
        if(!db||!sb)return copied?(int64_t)copied:-EFAULT;
        dleft=dv->iov_len-doff;
        sleft=sv->iov_len-soff;
        n=(dleft<sleft)?dleft:sleft;
        c3_memcpy(db+doff,sb+soff,n);
        copied+=n;
        doff+=n;
        soff+=n;
    }
    return (int64_t)copied;
}

static int64_t c3_seccomp_apply(unsigned int op,unsigned int flags,void *args){
    task_t *cur=task_current();
    int ti=g_current_task;
    if(!cur||ti<0||ti>=TASK_MAX)return -ESRCH;
    switch(op){
        case C3_SECCOMP_SET_MODE_STRICT:
            if(flags!=0)return -EINVAL;
            if(g_c3_seccomp_mode[ti]==C3_SECCOMP_MODE_FILTER)return -EACCES;
            g_c3_seccomp_mode[ti]=C3_SECCOMP_MODE_STRICT;
            g_c3_seccomp_flags[ti]=0;
            return 0;
        case C3_SECCOMP_SET_MODE_FILTER:
            if(flags&~(C3_SECCOMP_FILTER_FLAG_TSYNC|
                       C3_SECCOMP_FILTER_FLAG_LOG|
                       C3_SECCOMP_FILTER_FLAG_SPEC_ALLOW|
                       C3_SECCOMP_FILTER_FLAG_NEW_LISTENER|
                       C3_SECCOMP_FILTER_FLAG_TSYNC_ESRCH))return -EINVAL;
            if(!g_c3_no_new_privs[ti])return -EACCES;
            g_c3_seccomp_mode[ti]=C3_SECCOMP_MODE_FILTER;
            g_c3_seccomp_flags[ti]=flags;
            (void)args; /* BPF program ignored for now. */
            return 0;
        case C3_SECCOMP_GET_ACTION_AVAIL:
            if(!args)return -EFAULT;
            {
                uint32_t action=*(uint32_t*)args;
                switch(action){
                    case C3_SECCOMP_RET_KILL_PROCESS:
                    case C3_SECCOMP_RET_KILL_THREAD:
                    case C3_SECCOMP_RET_TRAP:
                    case C3_SECCOMP_RET_ERRNO:
                    case C3_SECCOMP_RET_TRACE:
                    case C3_SECCOMP_RET_LOG:
                    case C3_SECCOMP_RET_ALLOW:
                        return 0;
                    default:
                        return -EINVAL;
                }
            }
        case C3_SECCOMP_GET_NOTIF_SIZES:
            if(!args)return -EFAULT;
            {
                c3_seccomp_notif_sizes_t *ns=(c3_seccomp_notif_sizes_t*)args;
                ns->seccomp_notif=80;
                ns->seccomp_notif_resp=24;
                ns->seccomp_data=64;
            }
            return 0;
        default:
            return -EINVAL;
    }
}

int64_t real_sys_seccomp(unsigned int operation,unsigned int flags,void *args){
    return c3_seccomp_apply(operation,flags,args);
}

int64_t real_sys_unshare(uint64_t flags){
    int ti=g_current_task;
    uint64_t allowed=C3_CLONE_FS|C3_CLONE_FILES|C3_CLONE_SYSVSEM|C3_NS_CLONE_MASK;
    if(ti<0||ti>=TASK_MAX)return -ESRCH;
    if(flags&~allowed)return -EINVAL;
    if(flags&C3_NS_CLONE_MASK)g_c3_ns_mask[ti]|=(flags&C3_NS_CLONE_MASK);
    return 0;
}

int64_t real_sys_setns(int fd,int nstype){
    task_t *cur=task_current();
    task_t *target=0;
    int ti=g_current_task;
    int tti;
    uint64_t mask;
    if(!cur||ti<0||ti>=TASK_MAX)return -ESRCH;
    if(!fd_valid(&cur->fdt,fd))return -EBADF;
    if(cur->fdt.fds[fd].kind!=FDKIND_PROC)return -EINVAL;
    target=c3_task_by_pid(cur->fdt.fds[fd].ref);
    if(!target)return -ESRCH;
    tti=c3_task_index_by_pid(target->pid);
    if(tti<0||tti>=TASK_MAX)return -ESRCH;
    mask=(nstype==0)?C3_NS_CLONE_MASK:(uint64_t)(uint32_t)nstype;
    if(mask&~C3_NS_CLONE_MASK)return -EINVAL;
    g_c3_ns_mask[ti]=(g_c3_ns_mask[ti]&~mask)|(g_c3_ns_mask[tti]&mask);
    return 0;
}

int64_t real_sys_capget(void *hdrp,void *datap){
    task_t *cur=task_current();
    c3_cap_user_header_t *h=(c3_cap_user_header_t*)hdrp;
    c3_cap_user_data_t *d=(c3_cap_user_data_t*)datap;
    if(!cur||!h||!d)return -EFAULT;
    if(h->version!=C3_LINUX_CAP_VERSION_3){
        h->version=C3_LINUX_CAP_VERSION_3;
        return -EINVAL;
    }
    if(h->pid!=0&&h->pid!=cur->pid)return -EPERM;
    c3_memset(d,0,sizeof(c3_cap_user_data_t)*C3_LINUX_CAPABILITY_U32S_3);
    return 0;
}

int64_t real_sys_capset(const void *hdrp,const void *datap){
    task_t *cur=task_current();
    const c3_cap_user_header_t *h=(const c3_cap_user_header_t*)hdrp;
    const c3_cap_user_data_t *d=(const c3_cap_user_data_t*)datap;
    if(!cur||!h||!d)return -EFAULT;
    if(h->version!=C3_LINUX_CAP_VERSION_3)return -EINVAL;
    if(h->pid!=0&&h->pid!=cur->pid)return -EPERM;
    if(d[0].effective||d[0].permitted||d[0].inheritable||
       d[1].effective||d[1].permitted||d[1].inheritable)return -EPERM;
    return 0;
}

int64_t real_sys_userfaultfd(int flags){
    task_t *cur=task_current();
    uint16_t f=FDFL_READABLE|FDFL_WRITABLE;
    int fd;
    if(!cur)return -ESRCH;
    if(flags&~(O_CLOEXEC|O_NONBLOCK|C3_UFFD_USER_MODE_ONLY))return -EINVAL;
    if(flags&O_NONBLOCK)f|=FDFL_NONBLOCK;
    if(flags&O_CLOEXEC)f|=FDFL_CLOEXEC;
    fd=c3_fd_alloc_for_task(cur,FDKIND_EVENTFD,0,f);
    if(fd<0)return -EMFILE;
    return fd;
}

int64_t real_sys_process_vm_readv(int pid,const iovec_t *local_iov,unsigned long liovcnt,const iovec_t *remote_iov,unsigned long riovcnt,unsigned long flags){
    task_t *cur=task_current();
    if(!cur)return -ESRCH;
    if(flags!=0)return -EINVAL;
    if(pid!=0&&pid!=cur->pid)return -EPERM;
    return c3_process_vm_copy_iov(local_iov,liovcnt,remote_iov,riovcnt);
}

int64_t real_sys_process_vm_writev(int pid,const iovec_t *local_iov,unsigned long liovcnt,const iovec_t *remote_iov,unsigned long riovcnt,unsigned long flags){
    task_t *cur=task_current();
    if(!cur)return -ESRCH;
    if(flags!=0)return -EINVAL;
    if(pid!=0&&pid!=cur->pid)return -EPERM;
    return c3_process_vm_copy_iov((iovec_t*)remote_iov,riovcnt,local_iov,liovcnt);
}

int64_t real_sys_prctl(int option,uint64_t a2,uint64_t a3,uint64_t a4,uint64_t a5){
    task_t *cur=task_current();
    int ti=g_current_task;
    if(!cur||ti<0||ti>=TASK_MAX)return -ESRCH;
    switch(option){
        case PR_SET_NAME:
            c3_strlcpy(cur->name,(const char*)(uintptr_t)a2,TASK_NAME_LEN);
            if(c3_task_is_browser_runtime(cur)&&
               (c3_has_token(cur->name,"IPC I/O")||
                c3_has_token(cur->name,"Web Content")||
                c3_has_token(cur->name,"Renderer")||
                c3_has_token(cur->name,"Compositor")||
                c3_has_token(cur->name,"Socket Thread"))){
                static uint32_t preempt_trace=0;
                cur->no_timer_preempt=false;
                if(preempt_trace<32){
                    ++preempt_trace;
                    __boot_serial_puts("[browser-preempt-enable] pid=");
                    __boot_serial_putu32((uint32_t)cur->pid);
                    c3_trace_task_name(cur);
                    __boot_serial_puts("\n");
                }
            }
            {
                static uint32_t trace_count=0;
                if(trace_count<64){
                    ++trace_count;
                    __boot_serial_puts("[prctl-name] pid=");
                    __boot_serial_putu32((uint32_t)cur->pid);
                    c3_trace_task_name(cur);
                    __boot_serial_puts("\n");
                }
            }
            return 0;
        case PR_GET_NAME: c3_strlcpy((char*)(uintptr_t)a2,cur->name,16);return 0;
        case PR_GET_DUMPABLE: return 1;
        case PR_SET_DUMPABLE: return 0;
        case PR_GET_SECCOMP: return (int64_t)g_c3_seccomp_mode[ti];
        case PR_SET_SECCOMP:
            if(a2==C3_SECCOMP_MODE_STRICT)return c3_seccomp_apply(C3_SECCOMP_SET_MODE_STRICT,0,(void*)(uintptr_t)a3);
            if(a2==C3_SECCOMP_MODE_FILTER)return c3_seccomp_apply(C3_SECCOMP_SET_MODE_FILTER,0,(void*)(uintptr_t)a3);
            return -EINVAL;
        case PR_SET_NO_NEW_PRIVS:
            if(a2!=1||a3||a4||a5)return -EINVAL;
            g_c3_no_new_privs[ti]=1;
            return 0;
        case PR_GET_NO_NEW_PRIVS:
            return g_c3_no_new_privs[ti]?1:0;
        case PR_CAPBSET_READ:
            return 0;
        case PR_CAPBSET_DROP:
            return 0;
        case PR_SET_PTRACER:
            return 0;
    }
    return -EINVAL;
}

#define ARCH_SET_GS 0x1001
#define ARCH_SET_FS 0x1002
#define ARCH_GET_FS 0x1003
#define ARCH_GET_GS 0x1004

int64_t real_sys_arch_prctl(int code,uint64_t addr){
    task_t *cur=task_current();
    static uint32_t trace_count=0;
    switch(code){
        case ARCH_SET_FS: cur->ctx.fs_base=addr;
            if(trace_count<32){
                ++trace_count;
                __boot_serial_puts("[arch_prctl] pid=");
                __boot_serial_putu32((uint32_t)(cur?cur->pid:0));
                c3_trace_task_name(cur);
                __boot_serial_puts(" SET_FS ");
                __boot_serial_puthex64(addr);
                __boot_serial_puts("\n");
            }
            {uint32_t lo=(uint32_t)addr,hi=(uint32_t)(addr>>32);
             __asm__ volatile("wrmsr"::"c"(0xC0000100u),"a"(lo),"d"(hi));}return 0;
        case ARCH_SET_GS: cur->ctx.gs_base=addr;
            if(trace_count<32){
                ++trace_count;
                __boot_serial_puts("[arch_prctl] pid=");
                __boot_serial_putu32((uint32_t)(cur?cur->pid:0));
                c3_trace_task_name(cur);
                __boot_serial_puts(" SET_GS ");
                __boot_serial_puthex64(addr);
                __boot_serial_puts("\n");
            }
            {uint32_t lo=(uint32_t)addr,hi=(uint32_t)(addr>>32);
             __asm__ volatile("wrmsr"::"c"(0xC0000102u),"a"(lo),"d"(hi));}return 0;
        case ARCH_GET_FS: *(uint64_t*)(uintptr_t)addr=cur->ctx.fs_base;return 0;
        case ARCH_GET_GS: *(uint64_t*)(uintptr_t)addr=cur->ctx.gs_base;return 0;
    }
    return -EINVAL;
}

static uint64_t g_tid_addr=0;
int64_t real_sys_set_tid_address(uint64_t tidptr){
    task_t *cur=task_current();
    g_tid_addr=tidptr;
    if(cur)cur->clear_child_tid=tidptr;
    return real_sys_gettid();
}
int64_t real_sys_set_robust_list(uint64_t head,size_t len){
    task_t *cur=task_current();
    if(!cur)return -ESRCH;
    cur->robust_list_head=head;
    cur->robust_list_len=len;
    {
        static uint32_t trace_count=0;
        if(trace_count<32){
            ++trace_count;
            __boot_serial_puts("[robust-set] pid=");
            __boot_serial_putu32((uint32_t)cur->pid);
            __boot_serial_puts(" head=");
            __boot_serial_puthex64(head);
            __boot_serial_puts(" len=");
            __boot_serial_puthex64((uint64_t)len);
            __boot_serial_puts("\n");
        }
    }
    return 0;
}
int64_t real_sys_get_robust_list(int pid,uint64_t *head,size_t *len){
    task_t *t;
    if(!head||!len)return -EFAULT;
    if(pid==0||pid==task_current()->pid)t=task_current();
    else t=c3_task_by_pid(pid);
    if(!t)return -ESRCH;
    *head=t->robust_list_head;
    *len=t->robust_list_len;
    return 0;
}

#define C3_FUTEX_CMD_MASK     0x7F
#define C3_FUTEX_PRIVATE_FLAG 0x80
#define C3_FUTEX_CLOCK_REALTIME 0x100
#define C3_FUTEX_WAIT_BITSET  9
#define C3_FUTEX_WAKE_BITSET  10
#define C3_FUTEX_REQUEUE      3
#define C3_FUTEX_CMP_REQUEUE  4
#define C3_FUTEX_WAKE_OP      5
#define C3_FUTEX_LOCK_PI      6
#define C3_FUTEX_UNLOCK_PI    7
#define C3_FUTEX_TRYLOCK_PI   8
#define C3_FUTEX_BITSET_MATCH_ANY 0xFFFFFFFFu
#define C3_FUTEX_OP_OPARG_SHIFT 8u
#define C3_FUTEX_SPURIOUS_STALLS 0xFFFFFFFFu

static uint32_t c3_futex_hash(uint32_t *uaddr,uintptr_t mm_key){
    uintptr_t x=(uintptr_t)uaddr;
    x^=(x>>7)^(x>>17)^mm_key;
    return (uint32_t)(x&(C3_FUTEX_BUCKETS-1));
}

static int c3_futex_waiter_alloc(void){
    int i;
    for(i=0;i<C3_FUTEX_WAIT_MAX;++i)if(!g_c3_futex_waiters[i].used)return i;
    return -1;
}

static int c3_futex_waiter_add(uint32_t *uaddr,int task_index,uintptr_t mm_key,uint32_t bitset){
    int i=c3_futex_waiter_alloc();
    uint32_t b;
    if(i<0)return -1;
    if(++g_c3_futex_waiter_seq==0)g_c3_futex_waiter_seq=1;
    b=c3_futex_hash(uaddr,mm_key);
    g_c3_futex_waiters[i].used=true;
    g_c3_futex_waiters[i].uaddr=uaddr;
    g_c3_futex_waiters[i].mm_key=mm_key;
    g_c3_futex_waiters[i].bitset=bitset;
    g_c3_futex_waiters[i].task_index=task_index;
    g_c3_futex_waiters[i].next=g_c3_futex_heads[b];
    g_c3_futex_waiters[i].seq=g_c3_futex_waiter_seq;
    g_c3_futex_heads[b]=i;
    return i;
}

static bool c3_futex_waiter_active(int idx,uint32_t seq){
    return idx>=0&&idx<C3_FUTEX_WAIT_MAX&&
           g_c3_futex_waiters[idx].used&&
           g_c3_futex_waiters[idx].seq==seq;
}

static void c3_futex_waiter_remove_idx(int idx){
    uint32_t b;
    int prev=-1,it;
    if(idx<0||idx>=C3_FUTEX_WAIT_MAX||!g_c3_futex_waiters[idx].used)return;
    b=c3_futex_hash(g_c3_futex_waiters[idx].uaddr,g_c3_futex_waiters[idx].mm_key);
    it=g_c3_futex_heads[b];
    while(it>=0){
        if(it==idx){
            if(prev<0)g_c3_futex_heads[b]=g_c3_futex_waiters[it].next;
            else g_c3_futex_waiters[prev].next=g_c3_futex_waiters[it].next;
            g_c3_futex_waiters[it].used=false;
            g_c3_futex_waiters[it].uaddr=0;
            g_c3_futex_waiters[it].mm_key=0;
            g_c3_futex_waiters[it].bitset=0;
            g_c3_futex_waiters[it].task_index=-1;
            g_c3_futex_waiters[it].next=-1;
            g_c3_futex_waiters[it].seq=0;
            return;
        }
        prev=it;
        it=g_c3_futex_waiters[it].next;
    }
}

static void c3_futex_waiter_remove_task_uaddr(int task_index,uint32_t *uaddr,uintptr_t mm_key,bool exact_key){
    int i;
    for(i=0;i<C3_FUTEX_WAIT_MAX;++i){
        if(!g_c3_futex_waiters[i].used)continue;
        if(g_c3_futex_waiters[i].task_index!=task_index)continue;
        if(uaddr&&g_c3_futex_waiters[i].uaddr!=uaddr)continue;
        if(exact_key&&g_c3_futex_waiters[i].mm_key!=mm_key)continue;
        c3_futex_waiter_remove_idx(i);
    }
}

static uint32_t c3_futex_wake_n(uint32_t *uaddr,uint32_t want,uintptr_t mm_key,bool exact_key,uint32_t bitset){
    uint32_t woken=0;
    uint32_t b;
    int prev=-1,it;
    if(!want)return 0;
    if(!exact_key){
        int i;
        for(i=0;i<C3_FUTEX_WAIT_MAX&&woken<want;++i){
            int ti;
            if(!g_c3_futex_waiters[i].used)continue;
            if(g_c3_futex_waiters[i].uaddr!=uaddr)continue;
            if((g_c3_futex_waiters[i].bitset&bitset)==0)continue;
            ti=g_c3_futex_waiters[i].task_index;
            c3_futex_waiter_remove_idx(i);
            if(ti>=0&&ti<TASK_MAX&&g_tasks[ti].used&&g_tasks[ti].state==TASK_SLEEPING){
                g_tasks[ti].sleep_until=0;
                g_tasks[ti].state=TASK_RUNNABLE;
            }
            ++woken;
        }
        return woken;
    }
    b=c3_futex_hash(uaddr,mm_key);
    it=g_c3_futex_heads[b];
    while(it>=0&&woken<want){
        int next=g_c3_futex_waiters[it].next;
        bool match=(g_c3_futex_waiters[it].uaddr==uaddr)&&(g_c3_futex_waiters[it].mm_key==mm_key);
        if(match&&((g_c3_futex_waiters[it].bitset&bitset)!=0)){
            int ti=g_c3_futex_waiters[it].task_index;
            if(prev<0)g_c3_futex_heads[b]=next;
            else g_c3_futex_waiters[prev].next=next;
            g_c3_futex_waiters[it].used=false;
            g_c3_futex_waiters[it].uaddr=0;
            g_c3_futex_waiters[it].mm_key=0;
            g_c3_futex_waiters[it].bitset=0;
            g_c3_futex_waiters[it].task_index=-1;
            g_c3_futex_waiters[it].next=-1;
            g_c3_futex_waiters[it].seq=0;
            if(ti>=0&&ti<TASK_MAX&&g_tasks[ti].used&&g_tasks[ti].state==TASK_SLEEPING){
                g_tasks[ti].sleep_until=0;
                g_tasks[ti].state=TASK_RUNNABLE;
            }
            ++woken;
        }else{
            prev=it;
        }
        it=next;
    }
    return woken;
}

static uint32_t c3_futex_requeue(uint32_t *from,uint32_t *to,uint32_t max_move,uintptr_t old_key,uintptr_t new_key,bool exact_key){
    uint32_t moved=0;
    uint32_t oldb,newb;
    int prev=-1,it;
    if(!max_move)return 0;
    oldb=c3_futex_hash(from,old_key);
    it=g_c3_futex_heads[oldb];
    while(it>=0&&moved<max_move){
        int next=g_c3_futex_waiters[it].next;
        bool match=(g_c3_futex_waiters[it].uaddr==from)&&(!exact_key||g_c3_futex_waiters[it].mm_key==old_key);
        if(match){
            if(prev<0)g_c3_futex_heads[oldb]=next;
            else g_c3_futex_waiters[prev].next=next;
            g_c3_futex_waiters[it].uaddr=to;
            g_c3_futex_waiters[it].mm_key=new_key;
            newb=c3_futex_hash(to,new_key);
            g_c3_futex_waiters[it].next=g_c3_futex_heads[newb];
            g_c3_futex_heads[newb]=it;
            ++moved;
        }else{
            prev=it;
        }
        it=next;
    }
    return moved;
}

static bool c3_futex_wake_op_apply(uint32_t *uaddr,uint32_t encoded){
    uint32_t op=(encoded>>28)&0xFu;
    uint32_t cmp=(encoded>>24)&0xFu;
    int32_t oparg=(int32_t)((encoded>>12)&0xFFFu);
    int32_t cmparg=(int32_t)(encoded&0xFFFu);
    int32_t oldv,newv;
    if(!uaddr)return false;
    if(oparg&0x800)oparg|=~0xFFF;
    if(cmparg&0x800)cmparg|=~0xFFF;
    if(op&C3_FUTEX_OP_OPARG_SHIFT)oparg=(oparg<0||oparg>=31)?0:(1<<oparg);
    oldv=(int32_t)(*uaddr);
    switch(op&7u){
        case 0: newv=oparg; break;              /* SET */
        case 1: newv=oldv+oparg; break;         /* ADD */
        case 2: newv=oldv|oparg; break;         /* OR */
        case 3: newv=oldv&~oparg; break;        /* ANDN */
        case 4: newv=oldv^oparg; break;         /* XOR */
        default: return false;
    }
    *uaddr=(uint32_t)newv;
    switch(cmp){
        case 0: return oldv==cmparg;
        case 1: return oldv!=cmparg;
        case 2: return oldv<cmparg;
        case 3: return oldv<=cmparg;
        case 4: return oldv>cmparg;
        case 5: return oldv>=cmparg;
        default: return false;
    }
}

static bool c3_timespec_valid(const timespec_t *timeout){
    if(!timeout)return true;
    return timeout->tv_sec>=0&&timeout->tv_nsec>=0&&timeout->tv_nsec<1000000000LL;
}

static uint64_t c3_futex_timeout_deadline(const timespec_t *timeout,bool absolute,int clockid){
    uint64_t add_ns,now_ns;
    if(!timeout)return 0;
    if(!c3_timespec_valid(timeout))return 0;
    if(absolute){
        timespec_t now;
        if(real_sys_clock_gettime(clockid,&now)<0)return c3_rdtsc();
        now_ns=(uint64_t)now.tv_sec*1000000000ULL+(uint64_t)now.tv_nsec;
        add_ns=(uint64_t)timeout->tv_sec*1000000000ULL+(uint64_t)timeout->tv_nsec;
        if(add_ns<=now_ns)return c3_rdtsc();
        add_ns-=now_ns;
    }else{
        add_ns=(uint64_t)timeout->tv_sec*1000000000ULL+(uint64_t)timeout->tv_nsec;
    }
    if(!add_ns)return c3_rdtsc();
    return c3_rdtsc()+(add_ns*g_tsc_freq_approx)/1000000000ULL;
}

static uint32_t c3_futex_waiter_count(uint32_t *uaddr,uintptr_t mm_key,bool exact_key,uint32_t bitset){
    uint32_t n=0;
    int i;
    for(i=0;i<C3_FUTEX_WAIT_MAX;++i){
        if(!g_c3_futex_waiters[i].used)continue;
        if(g_c3_futex_waiters[i].uaddr!=uaddr)continue;
        if(exact_key&&g_c3_futex_waiters[i].mm_key!=mm_key)continue;
        if((g_c3_futex_waiters[i].bitset&bitset)==0)continue;
        ++n;
    }
    return n;
}

static void c3_futex_trace_wake(const char *tag,const task_t *cur,int op,uint32_t *uaddr,uint32_t want,
                                uint32_t before,uint32_t exact_woken,uint32_t broad_woken,uint32_t *uaddr2,uint32_t aux){
    static uint32_t trace_count=0;
    uint64_t ret0=0;
    if(trace_count>=8)return;
    if(cur&&cur->ctx.rsp>=0x10000ULL)ret0=*(uint64_t*)(uintptr_t)cur->ctx.rsp;
    ++trace_count;
    __boot_serial_puts("[futex-");
    __boot_serial_puts(tag?tag:"wake");
    __boot_serial_puts("] pid=");
    __boot_serial_putu32(cur?(uint32_t)cur->pid:0);
    c3_trace_task_name(cur);
    __boot_serial_puts(" rip=");
    __boot_serial_puthex64(cur?cur->ctx.rip:0);
    __boot_serial_puts(" ret=");
    __boot_serial_puthex64(ret0);
    __boot_serial_puts(" op=");
    __boot_serial_puthex64((uint64_t)(uint32_t)op);
    __boot_serial_puts(" uaddr=");
    __boot_serial_puthex64((uint64_t)(uintptr_t)uaddr);
    __boot_serial_puts(" want=");
    __boot_serial_puthex64((uint64_t)want);
    __boot_serial_puts(" waiters=");
    __boot_serial_putu32(before);
    __boot_serial_puts(" exact=");
    __boot_serial_putu32(exact_woken);
    __boot_serial_puts(" broad=");
    __boot_serial_putu32(broad_woken);
    if(uaddr2){
        __boot_serial_puts(" uaddr2=");
        __boot_serial_puthex64((uint64_t)(uintptr_t)uaddr2);
        __boot_serial_puts(" aux=");
        __boot_serial_puthex64((uint64_t)aux);
    }
    __boot_serial_puts("\n");
}

static void c3_futex_trace_stall(const task_t *cur,uint32_t *uaddr,int op,uint32_t val,uint32_t seen,const timespec_t *timeout){
    static uint32_t trace_count=0;
    uint64_t ret0=0;
    int i;
    if(trace_count>=4)return;
    if(cur&&cur->ctx.rsp>=0x10000ULL)ret0=*(uint64_t*)(uintptr_t)cur->ctx.rsp;
    ++trace_count;
    __boot_serial_puts("[futex-stall] pid=");
    __boot_serial_putu32(cur?(uint32_t)cur->pid:0);
    c3_trace_task_name(cur);
    __boot_serial_puts(" rip=");
    __boot_serial_puthex64(cur?cur->ctx.rip:0);
    __boot_serial_puts(" ret=");
    __boot_serial_puthex64(ret0);
    __boot_serial_puts(" op=");
    __boot_serial_puthex64((uint64_t)(uint32_t)op);
    __boot_serial_puts(" uaddr=");
    __boot_serial_puthex64((uint64_t)(uintptr_t)uaddr);
    __boot_serial_puts(" val=");
    __boot_serial_puthex64((uint64_t)val);
    __boot_serial_puts(" seen=");
    __boot_serial_puthex64((uint64_t)seen);
    __boot_serial_puts(" timeout=");
    __boot_serial_puthex64((uint64_t)(uintptr_t)timeout);
    __boot_serial_puts(" tasks=");
    for(i=0;i<TASK_MAX;++i){
        if(!g_tasks[i].used)continue;
        if(cur&&g_tasks[i].addr_space!=cur->addr_space)continue;
        __boot_serial_puts(" ");
        __boot_serial_putu32((uint32_t)g_tasks[i].pid);
        c3_trace_task_name(&g_tasks[i]);
        __boot_serial_puts(":");
        __boot_serial_putu32((uint32_t)g_tasks[i].state);
    }
    __boot_serial_puts("\n");
}

static void c3_futex_trace_wait_event(const char *stage,const task_t *cur,int op,uint32_t *uaddr,
                                      uint32_t val,uint32_t seen,const timespec_t *timeout,
                                      uint64_t deadline,uint32_t bitset,const char *reason){
    static uint32_t trace_count=0;
    uint64_t user_rip=0;
    uint64_t user_rsp=0;
    uint64_t stk[28];
    int stk_n=0;
    int i;
    if(!cur)return;
    if(cur->pid!=2&&cur->pid!=6&&cur->pid!=7&&cur->pid!=8)return;
    if(trace_count>=8)return;
    for(i=0;i<(int)(sizeof(stk)/sizeof(stk[0]));++i)stk[i]=0;
    {
        uint64_t *f=task_syscall_user_frame(cur);
        if(f){
            user_rip=f[17];
            user_rsp=f[20];
            if(user_rsp>=0x10000ULL&&user_rsp<0x0000800000000000ULL&&cur->addr_space){
                for(i=0;i<(int)(sizeof(stk)/sizeof(stk[0]));++i){
                    uint64_t a=user_rsp+(uint64_t)i*8ULL;
                    if(!(paging_get_entry(cur->addr_space,a)&PAGE_PRESENT))break;
                    stk[i]=*(uint64_t*)(uintptr_t)a;
                    stk_n=i+1;
                }
            }
        }
    }
    ++trace_count;
    __boot_serial_puts("[futex-wait-");
    __boot_serial_puts(stage?stage:"?");
    __boot_serial_puts("] pid=");
    __boot_serial_putu32((uint32_t)cur->pid);
    c3_trace_task_name(cur);
    __boot_serial_puts(" user=");
    __boot_serial_puthex64(user_rip);
    __boot_serial_puts(" ursp=");
    __boot_serial_puthex64(user_rsp);
    __boot_serial_puts(" stack=");
    for(i=0;i<stk_n;++i){
        if(i)__boot_serial_puts(",");
        __boot_serial_puthex64(stk[i]);
    }
    __boot_serial_puts(" op=");
    __boot_serial_puthex64((uint64_t)(uint32_t)op);
    __boot_serial_puts(" uaddr=");
    __boot_serial_puthex64((uint64_t)(uintptr_t)uaddr);
    __boot_serial_puts(" val=");
    __boot_serial_puthex64((uint64_t)val);
    __boot_serial_puts(" seen=");
    __boot_serial_puthex64((uint64_t)seen);
    __boot_serial_puts(" bitset=");
    __boot_serial_puthex64((uint64_t)bitset);
    __boot_serial_puts(" timeout=");
    __boot_serial_puthex64((uint64_t)(uintptr_t)timeout);
    if(timeout){
        __boot_serial_puts(" ts=");
        __boot_serial_puthex64((uint64_t)timeout->tv_sec);
        __boot_serial_puts(".");
        __boot_serial_puthex64((uint64_t)timeout->tv_nsec);
    }
    __boot_serial_puts(" deadline=");
    __boot_serial_puthex64(deadline);
    if(reason){
        __boot_serial_puts(" reason=");
        __boot_serial_puts(reason);
    }
    __boot_serial_puts("\n");
}

int64_t real_sys_futex(uint32_t *uaddr,int op,uint32_t val,const timespec_t *timeout,uint32_t *uaddr2,uint32_t val3){
    int cmd=op&C3_FUTEX_CMD_MASK;
    bool is_private=(op&C3_FUTEX_PRIVATE_FLAG)!=0;
    bool use_realtime=((op&C3_FUTEX_CLOCK_REALTIME)!=0);
    task_t *cur=task_current();
    uintptr_t mm_key=(is_private&&cur)?(uintptr_t)cur->addr_space:0;
    bool exact_key=is_private;
    uint32_t val2=(uint32_t)(uintptr_t)timeout;
    uint32_t bitset=(cmd==C3_FUTEX_WAIT_BITSET||cmd==C3_FUTEX_WAKE_BITSET)?val3:C3_FUTEX_BITSET_MATCH_ANY;
    if(!uaddr)return -EFAULT;
    if(((uintptr_t)uaddr&3U)!=0)return -EINVAL;
    if(use_realtime&&cmd!=FUTEX_WAIT&&cmd!=C3_FUTEX_WAIT_BITSET)return -EINVAL;
    if((cmd==C3_FUTEX_WAIT_BITSET||cmd==C3_FUTEX_WAKE_BITSET)&&bitset==0)return -EINVAL;
    if(cmd==FUTEX_WAIT||cmd==C3_FUTEX_WAIT_BITSET){
        uint64_t deadline=0;
        int waiter_idx;
        uint32_t waiter_seq;
        uint32_t wait_spins=0;
        uint32_t stall_rounds=0;
        uint32_t seen_now;
        const char *exit_reason=0;
        if(!cur)return -ESRCH;
        seen_now=*uaddr;
        if(seen_now!=val){
            c3_futex_trace_wait_event("eagain",cur,op,uaddr,val,seen_now,timeout,0,bitset,"mismatch");
            return -EAGAIN;
        }
        (void)seen_now;
        if(!timeout){
            static uint32_t coop_wait_trace=0;
            if(coop_wait_trace<4){
                int i;
                ++coop_wait_trace;
                __boot_serial_puts("[futex-coopwait] pid=");
                __boot_serial_putu32((uint32_t)cur->pid);
                c3_trace_task_name(cur);
                __boot_serial_puts(" op=");
                __boot_serial_puthex64((uint64_t)(uint32_t)op);
                __boot_serial_puts(" uaddr=");
                __boot_serial_puthex64((uint64_t)(uintptr_t)uaddr);
                __boot_serial_puts(" val=");
                __boot_serial_puthex64((uint64_t)val);
                __boot_serial_puts(" tasks=");
                for(i=0;i<TASK_MAX;++i){
                    if(!g_tasks[i].used)continue;
                    if(g_tasks[i].addr_space!=cur->addr_space)continue;
                    __boot_serial_puts(" ");
                    __boot_serial_putu32((uint32_t)g_tasks[i].pid);
                    c3_trace_task_name(&g_tasks[i]);
                    __boot_serial_puts(":");
                    __boot_serial_putu32((uint32_t)g_tasks[i].state);
                }
                __boot_serial_puts("\n");
            }
        }
        if(timeout){
            bool absolute_timeout=(cmd==C3_FUTEX_WAIT_BITSET);
            int timeout_clock=use_realtime?C3_CLOCK_REALTIME:C3_CLOCK_MONOTONIC;
            if(!c3_timespec_valid(timeout))return -EINVAL;
            deadline=c3_futex_timeout_deadline(timeout,absolute_timeout,timeout_clock);
        }
        waiter_idx=c3_futex_waiter_add(uaddr,g_current_task,mm_key,bitset);
        if(waiter_idx<0)return -EAGAIN;
        waiter_seq=g_c3_futex_waiters[waiter_idx].seq;
        c3_futex_trace_wait_event("enter",cur,op,uaddr,val,*uaddr,timeout,deadline,bitset,0);
        for(;;){
            if(!c3_futex_waiter_active(waiter_idx,waiter_seq)){exit_reason="woken";break;}
            if(*uaddr!=val){exit_reason="changed";break;}
            ++wait_spins;
            if(!deadline&&wait_spins>=1024u){
                c3_futex_trace_stall(cur,uaddr,op,val,*uaddr,timeout);
                wait_spins=0;
                ++stall_rounds;
                if(stall_rounds>=C3_FUTEX_SPURIOUS_STALLS){
                    c3_futex_waiter_remove_idx(waiter_idx);
                    c3_futex_trace_wait_event("exit",cur,op,uaddr,val,*uaddr,timeout,deadline,bitset,"spurious");
                    return 0;
                }
            }
            if(c3_task_has_unblocked_signal(cur)){
                c3_futex_waiter_remove_task_uaddr(g_current_task,uaddr,mm_key,exact_key);
                c3_futex_trace_wait_event("exit",cur,op,uaddr,val,*uaddr,timeout,deadline,bitset,"signal");
                return -EINTR;
            }
            if(deadline&&c3_rdtsc()>=deadline){
                c3_futex_waiter_remove_task_uaddr(g_current_task,uaddr,mm_key,exact_key);
                c3_futex_trace_wait_event("exit",cur,op,uaddr,val,*uaddr,timeout,deadline,bitset,"timeout");
                return -ETIMEDOUT;
            }
            c3_futex_wait_schedule(cur,deadline);
            if(!c3_futex_waiter_active(waiter_idx,waiter_seq)){exit_reason="woken";break;}
            if(cur->state==TASK_SLEEPING){
                cur->state=TASK_RUNNING;
            }
        }
        c3_futex_waiter_remove_task_uaddr(g_current_task,uaddr,mm_key,exact_key);
        c3_futex_trace_wait_event("exit",cur,op,uaddr,val,*uaddr,timeout,deadline,bitset,exit_reason?exit_reason:"spurious");
        return 0;
    }
    if(cmd==FUTEX_WAKE||cmd==C3_FUTEX_WAKE_BITSET){
        uint32_t before=c3_futex_waiter_count(uaddr,mm_key,exact_key,bitset);
        uint32_t exact_woken=c3_futex_wake_n(uaddr,val,mm_key,exact_key,bitset);
        uint32_t broad_woken=0;
        uint32_t woken=exact_woken;
        if(exact_key&&woken<val){
            broad_woken=c3_futex_wake_n(uaddr,val-woken,0,false,bitset);
            woken+=broad_woken;
        }
        c3_futex_trace_wake(cmd==C3_FUTEX_WAKE_BITSET?"wakebit":"wake",cur,op,uaddr,val,before,exact_woken,broad_woken,0,bitset);
        return(int64_t)woken;
    }
    if(cmd==C3_FUTEX_WAKE_OP){
        uint32_t woken,before1,before2,exact1,broad1=0,exact2=0,broad2=0;
        uint32_t val2w=(uint32_t)(uintptr_t)timeout;
        bool wake2;
        if(!uaddr2)return -EFAULT;
        if(((uintptr_t)uaddr2&3U)!=0)return -EINVAL;
        before1=c3_futex_waiter_count(uaddr,mm_key,exact_key,C3_FUTEX_BITSET_MATCH_ANY);
        before2=c3_futex_waiter_count(uaddr2,mm_key,exact_key,C3_FUTEX_BITSET_MATCH_ANY);
        wake2=c3_futex_wake_op_apply(uaddr2,val3);
        exact1=c3_futex_wake_n(uaddr,val,mm_key,exact_key,C3_FUTEX_BITSET_MATCH_ANY);
        woken=exact1;
        if(exact_key&&woken<val){
            broad1=c3_futex_wake_n(uaddr,val-woken,0,false,C3_FUTEX_BITSET_MATCH_ANY);
            woken+=broad1;
        }
        if(wake2){
            exact2=c3_futex_wake_n(uaddr2,val2w,mm_key,exact_key,C3_FUTEX_BITSET_MATCH_ANY);
            if(exact_key&&exact2<val2w){
                broad2=c3_futex_wake_n(uaddr2,val2w-exact2,0,false,C3_FUTEX_BITSET_MATCH_ANY);
            }
            woken+=exact2+broad2;
        }
        c3_futex_trace_wake("wakeop1",cur,op,uaddr,val,before1,exact1,broad1,uaddr2,val3);
        c3_futex_trace_wake("wakeop2",cur,op,uaddr2,val2w,before2,exact2,broad2,uaddr,wake2?1u:0u);
        return(int64_t)woken;
    }
    if(cmd==C3_FUTEX_LOCK_PI||cmd==C3_FUTEX_TRYLOCK_PI){
        uint32_t tid;
        uint32_t seen;
        uint32_t spins=0;
        if(!cur)return -ESRCH;
        tid=(uint32_t)cur->pid&C3_FUTEX_TID_MASK;
        for(;;){
            seen=*uaddr;
            if((seen&C3_FUTEX_TID_MASK)==0){
                *uaddr=tid;
                return 0;
            }
            if((seen&C3_FUTEX_TID_MASK)==tid)return -EAGAIN;
            *uaddr=seen|C3_FUTEX_WAITERS_BIT;
            if(cmd==C3_FUTEX_TRYLOCK_PI)return -EAGAIN;
            if(++spins>256)return -EAGAIN;
            c3_poll_wait_slice(cur,0);
        }
    }
    if(cmd==C3_FUTEX_UNLOCK_PI){
        uint32_t before;
        uint32_t exact_woken,broad_woken=0,woken;
        if(!cur)return -ESRCH;
        before=c3_futex_waiter_count(uaddr,mm_key,exact_key,C3_FUTEX_BITSET_MATCH_ANY);
        *uaddr=0;
        exact_woken=c3_futex_wake_n(uaddr,1,mm_key,exact_key,C3_FUTEX_BITSET_MATCH_ANY);
        woken=exact_woken;
        if(exact_key&&woken<1){
            broad_woken=c3_futex_wake_n(uaddr,1-woken,0,false,C3_FUTEX_BITSET_MATCH_ANY);
            woken+=broad_woken;
        }
        c3_futex_trace_wake("unlockpi",cur,op,uaddr,1,before,exact_woken,broad_woken,0,0);
        return(int64_t)woken;
    }
    if(cmd==C3_FUTEX_REQUEUE){
        uint32_t woken,moved,before;
        if(!uaddr2)return -EFAULT;
        if(((uintptr_t)uaddr2&3U)!=0)return -EINVAL;
        before=c3_futex_waiter_count(uaddr,mm_key,exact_key,C3_FUTEX_BITSET_MATCH_ANY);
        woken=c3_futex_wake_n(uaddr,val,mm_key,exact_key,C3_FUTEX_BITSET_MATCH_ANY);
        moved=c3_futex_requeue(uaddr,uaddr2,val2,mm_key,mm_key,exact_key);
        c3_futex_trace_wake("requeue",cur,op,uaddr,val,before,woken,moved,uaddr2,val2);
        return(int64_t)(woken+moved);
    }
    if(cmd==C3_FUTEX_CMP_REQUEUE){
        uint32_t woken,moved,before;
        if(!uaddr2)return -EFAULT;
        if(((uintptr_t)uaddr2&3U)!=0)return -EINVAL;
        if(*uaddr!=val3)return -EAGAIN;
        before=c3_futex_waiter_count(uaddr,mm_key,exact_key,C3_FUTEX_BITSET_MATCH_ANY);
        woken=c3_futex_wake_n(uaddr,val,mm_key,exact_key,C3_FUTEX_BITSET_MATCH_ANY);
        moved=c3_futex_requeue(uaddr,uaddr2,val2,mm_key,mm_key,exact_key);
        c3_futex_trace_wake("cmpreq",cur,op,uaddr,val,before,woken,moved,uaddr2,val2);
        return(int64_t)(woken+moved);
    }
    c3_futex_trace_wake("unsupported",cur,op,uaddr,val,0,0,0,uaddr2,val3);
    return -ENOSYS;
}

typedef struct __attribute__((packed)) {
    int32_t  wd;
    uint32_t mask;
    uint32_t cookie;
    uint32_t len;
} c3_inotify_event_hdr_t;

static uint32_t c3_inotify_name_padded(uint32_t n){
    uint32_t want=n? (n+1u):0u;
    if(!want)return 0;
    return (want+3u)&~3u;
}

static void c3_inotify_basename(const char *path,char *out,size_t cap){
    const char *p=path;
    const char *last=path;
    if(!out||cap==0)return;
    out[0]=0;
    if(!path||!*path)return;
    while(*p){if(*p=='/')last=p+1;++p;}
    c3_strlcpy(out,last,cap);
}

static bool c3_inotify_q_empty(const c3_inotify_inst_t *in){
    return !in||in->head==in->tail;
}

static uint16_t c3_inotify_q_next(uint16_t i){
    return (uint16_t)((i+1u)%C3_INOTIFY_EVENT_MAX);
}

static void c3_inotify_enqueue(int ref,int32_t wd,uint32_t mask,uint32_t cookie,const char *name){
    c3_inotify_inst_t *in;
    uint16_t head,next;
    if(ref<0||ref>=C3_INOTIFY_INST_MAX||!g_c3_inotify_inst[ref].used)return;
    in=&g_c3_inotify_inst[ref];
    head=in->head;
    next=c3_inotify_q_next(head);
    if(next==in->tail)in->tail=c3_inotify_q_next(in->tail);
    in->q[head].wd=wd;
    in->q[head].mask=mask;
    in->q[head].cookie=cookie;
    in->q[head].name_len=0;
    in->q[head].name[0]=0;
    if(name&&*name){
        size_t nl=c3_strlen(name);
        if(nl>=sizeof(in->q[head].name))nl=sizeof(in->q[head].name)-1;
        c3_memcpy(in->q[head].name,name,nl);
        in->q[head].name[nl]=0;
        in->q[head].name_len=(uint16_t)nl;
    }
    in->head=next;
}

static int c3_inotify_inst_alloc(void){
    int i;
    for(i=0;i<C3_INOTIFY_INST_MAX;++i){
        if(g_c3_inotify_inst[i].used)continue;
        c3_memset(&g_c3_inotify_inst[i],0,sizeof(g_c3_inotify_inst[i]));
        g_c3_inotify_inst[i].used=true;
        g_c3_inotify_inst[i].next_wd=1;
        return i;
    }
    return -ENOMEM;
}

static void c3_inotify_inst_free(int ref){
    int i;
    if(ref<0||ref>=C3_INOTIFY_INST_MAX||!g_c3_inotify_inst[ref].used)return;
    for(i=0;i<C3_INOTIFY_WATCH_MAX;++i){
        if(g_c3_inotify_watch[i].used&&g_c3_inotify_watch[i].inst_ref==ref)
            g_c3_inotify_watch[i].used=false;
    }
    c3_memset(&g_c3_inotify_inst[ref],0,sizeof(g_c3_inotify_inst[ref]));
}

static int c3_inotify_watch_add(int ref,const char *path,uint32_t mask){
    c3_inotify_inst_t *in;
    uint32_t emask=mask&(C3_IN_ALL_EVENTS|C3_IN_ONLYDIR|C3_IN_DONT_FOLLOW|C3_IN_EXCL_UNLINK|C3_IN_ONESHOT);
    int i,free_idx=-1;
    if(ref<0||ref>=C3_INOTIFY_INST_MAX||!g_c3_inotify_inst[ref].used||!path||!*path)return -EINVAL;
    if((emask&C3_IN_ALL_EVENTS)==0)emask|=C3_IN_ALL_EVENTS;
    if((emask&C3_IN_ONLYDIR)&&!c3_path_is_dir(path))return -ENOTDIR;
    in=&g_c3_inotify_inst[ref];
    for(i=0;i<C3_INOTIFY_WATCH_MAX;++i){
        if(!g_c3_inotify_watch[i].used){
            if(free_idx<0)free_idx=i;
            continue;
        }
        if(g_c3_inotify_watch[i].inst_ref!=ref)continue;
        if(c3_strcmp(g_c3_inotify_watch[i].path,path)!=0)continue;
        if(mask&C3_IN_MASK_ADD)g_c3_inotify_watch[i].mask|=emask;
        else g_c3_inotify_watch[i].mask=emask;
        return g_c3_inotify_watch[i].wd;
    }
    if(free_idx<0)return -ENOMEM;
    if(in->next_wd<=0)in->next_wd=1;
    g_c3_inotify_watch[free_idx].used=true;
    g_c3_inotify_watch[free_idx].inst_ref=ref;
    g_c3_inotify_watch[free_idx].wd=(int32_t)in->next_wd++;
    g_c3_inotify_watch[free_idx].mask=emask;
    c3_strlcpy(g_c3_inotify_watch[free_idx].path,path,sizeof(g_c3_inotify_watch[free_idx].path));
    return g_c3_inotify_watch[free_idx].wd;
}

static int c3_inotify_watch_rm(int ref,int32_t wd){
    int i;
    if(ref<0||ref>=C3_INOTIFY_INST_MAX||!g_c3_inotify_inst[ref].used)return -EINVAL;
    for(i=0;i<C3_INOTIFY_WATCH_MAX;++i){
        if(!g_c3_inotify_watch[i].used)continue;
        if(g_c3_inotify_watch[i].inst_ref!=ref||g_c3_inotify_watch[i].wd!=wd)continue;
        g_c3_inotify_watch[i].used=false;
        c3_inotify_enqueue(ref,wd,C3_IN_IGNORED,0,0);
        return 0;
    }
    return -EINVAL;
}

static void c3_inotify_notify_path(const char *path,uint32_t mask,uint32_t cookie){
    char parent[VFS_PATH_MAX];
    char base[C3_INOTIFY_NAME_MAX];
    int i;
    if(!path||!*path||!mask)return;
    c3_path_dirname(path,parent,sizeof(parent));
    c3_inotify_basename(path,base,sizeof(base));
    for(i=0;i<C3_INOTIFY_WATCH_MAX;++i){
        c3_inotify_watch_t *w=&g_c3_inotify_watch[i];
        uint32_t ev;
        bool oneshot;
        if(!w->used)continue;
        if(w->inst_ref<0||w->inst_ref>=C3_INOTIFY_INST_MAX||!g_c3_inotify_inst[w->inst_ref].used)continue;
        if(c3_strcmp(w->path,path)==0){
            ev=mask&w->mask;
            if(ev)c3_inotify_enqueue(w->inst_ref,w->wd,ev,cookie,0);
            if((w->mask&C3_IN_ONESHOT)&&ev){
                int32_t wd=w->wd;
                w->used=false;
                c3_inotify_enqueue(w->inst_ref,wd,C3_IN_IGNORED,0,0);
            }
            continue;
        }
        if(c3_strcmp(w->path,parent)==0){
            ev=mask&w->mask;
            if(ev)c3_inotify_enqueue(w->inst_ref,w->wd,ev,cookie,base);
            oneshot=(w->mask&C3_IN_ONESHOT)&&ev;
            if(oneshot){
                int32_t wd=w->wd;
                w->used=false;
                c3_inotify_enqueue(w->inst_ref,wd,C3_IN_IGNORED,0,0);
            }
        }
    }
}

static int64_t c3_inotify_read(int ref,void *buf,size_t count,int flags){
    c3_inotify_inst_t *in;
    task_t *cur=task_current();
    uint8_t *dst=(uint8_t*)buf;
    size_t written=0;
    if(!buf)return -EFAULT;
    if(ref<0||ref>=C3_INOTIFY_INST_MAX||!g_c3_inotify_inst[ref].used)return -EBADF;
    if(count<sizeof(c3_inotify_event_hdr_t))return -EINVAL;
    in=&g_c3_inotify_inst[ref];
    for(;;){
        while(!c3_inotify_q_empty(in)){
            c3_inotify_event_rec_t rec=in->q[in->tail];
            c3_inotify_event_hdr_t h;
            uint32_t nlen=c3_inotify_name_padded(rec.name_len);
            size_t need=sizeof(h)+(size_t)nlen;
            if(written&&written+need>count)return (int64_t)written;
            if(need>count)return -EINVAL;
            h.wd=rec.wd;
            h.mask=rec.mask;
            h.cookie=rec.cookie;
            h.len=nlen;
            c3_memcpy(dst+written,&h,sizeof(h));
            written+=sizeof(h);
            if(nlen){
                c3_memset(dst+written,0,nlen);
                if(rec.name_len)c3_memcpy(dst+written,rec.name,(size_t)rec.name_len);
                written+=nlen;
            }
            in->tail=c3_inotify_q_next(in->tail);
        }
        if(written)return (int64_t)written;
        if(flags&C3_MSG_DONTWAIT)return -EAGAIN;
        if(cur&&c3_task_has_unblocked_signal(cur))return -EINTR;
        if(cur){
            cur->state=TASK_SLEEPING;
            task_schedule();
            if(cur->state==TASK_SLEEPING)cur->state=TASK_RUNNING;
        }else{
            return -EAGAIN;
        }
    }
}

/* epoll / poll / select / eventfd / timerfd / signalfd */
static int c3_eventfd_alloc(uint64_t initval,bool semaphore){
    int i;
    for(i=0;i<C3_EVENTFD_MAX;++i){
        if(g_c3_eventfds[i].used)continue;
        g_c3_eventfds[i].used=true;
        g_c3_eventfds[i].semaphore=semaphore;
        g_c3_eventfds[i].counter=initval;
        {
            static uint32_t trace_count=0;
            task_t *cur=task_current();
            if(trace_count<32){
                ++trace_count;
                __boot_serial_puts("[eventfd-alloc] pid=");
                __boot_serial_putu32(cur?(uint32_t)cur->pid:0);
                c3_trace_task_name(cur);
                __boot_serial_puts(" ref=");
                __boot_serial_putu32((uint32_t)i);
                __boot_serial_puts(" init=");
                __boot_serial_puthex64(initval);
                __boot_serial_puts(" sem=");
                __boot_serial_putu32(semaphore?1u:0u);
                __boot_serial_puts("\n");
            }
        }
        return i;
    }
    return -EMFILE;
}

static void c3_eventfd_free(int ref){
    if(ref<0||ref>=C3_EVENTFD_MAX)return;
    g_c3_eventfds[ref].used=false;
    g_c3_eventfds[ref].semaphore=false;
    g_c3_eventfds[ref].counter=0;
}

static int64_t c3_eventfd_read(int ref,void *buf,size_t count,int flags){
    c3_eventfd_t *e;
    uint64_t v=0;
    task_t *cur;
    uint32_t waits=0;
    if(ref<0||ref>=C3_EVENTFD_MAX||!g_c3_eventfds[ref].used)return -EBADF;
    if(!buf)return -EFAULT;
    if(count<sizeof(uint64_t))return -EINVAL;
    e=&g_c3_eventfds[ref];
    cur=task_current();
    while(!e->counter){
        if(flags&C3_MSG_DONTWAIT)return -EAGAIN;
        if(cur&&c3_task_has_unblocked_signal(cur))return -EINTR;
        if(waits<8){
            ++waits;
            __boot_serial_puts("[eventfd-read-wait] pid=");
            __boot_serial_putu32(cur?(uint32_t)cur->pid:0);
            c3_trace_task_name(cur);
            __boot_serial_puts(" ref=");
            __boot_serial_putu32((uint32_t)ref);
            __boot_serial_puts("\n");
        }
        c3_poll_wait_slice(cur,0);
        if(ref<0||ref>=C3_EVENTFD_MAX||!g_c3_eventfds[ref].used)return -EBADF;
        e=&g_c3_eventfds[ref];
    }
    if(e->semaphore){
        v=1;
        e->counter-=1;
    }else{
        v=e->counter;
        e->counter=0;
    }
    c3_memcpy(buf,&v,sizeof(v));
    return (int64_t)sizeof(v);
}

static int64_t c3_eventfd_write(int ref,const void *buf,size_t count){
    c3_eventfd_t *e;
    uint64_t v;
    task_t *cur=task_current();
    int i;
    if(ref<0||ref>=C3_EVENTFD_MAX||!g_c3_eventfds[ref].used)return -EBADF;
    if(!buf)return -EFAULT;
    if(count<sizeof(uint64_t))return -EINVAL;
    c3_memcpy(&v,buf,sizeof(v));
    if(v==~0ULL)return -EINVAL;
    e=&g_c3_eventfds[ref];
    if(e->counter>~0ULL-v)return -EAGAIN;
    e->counter+=v;
    if(cur){
        for(i=0;i<TASK_MAX;++i){
            if(!g_tasks[i].used||g_tasks[i].state!=TASK_SLEEPING)continue;
            if(g_tasks[i].addr_space!=cur->addr_space)continue;
            g_tasks[i].sleep_until=0;
            g_tasks[i].state=TASK_RUNNABLE;
        }
    }
    {
        static uint32_t trace_count=0;
        if(trace_count<32){
            ++trace_count;
            __boot_serial_puts("[eventfd-write] pid=");
            __boot_serial_putu32(cur?(uint32_t)cur->pid:0);
            c3_trace_task_name(cur);
            __boot_serial_puts(" ref=");
            __boot_serial_putu32((uint32_t)ref);
            __boot_serial_puts(" val=");
            __boot_serial_puthex64(v);
            __boot_serial_puts(" counter=");
            __boot_serial_puthex64(e->counter);
            __boot_serial_puts("\n");
        }
    }
    return (int64_t)sizeof(v);
}

typedef struct {
    timespec_t it_interval;
    timespec_t it_value;
} c3_itimerspec_t;

static uint64_t c3_ns_to_tsc(uint64_t ns){
    if(!ns||!g_tsc_freq_approx)return 0;
    return (ns*g_tsc_freq_approx)/1000000000ULL;
}

static uint64_t c3_timespec_to_ns(const timespec_t *ts){
    if(!ts||ts->tv_sec<0||ts->tv_nsec<0||ts->tv_nsec>=1000000000LL)return ~0ULL;
    return (uint64_t)ts->tv_sec*1000000000ULL+(uint64_t)ts->tv_nsec;
}

static void c3_ns_to_timespec(uint64_t ns,timespec_t *ts){
    if(!ts)return;
    ts->tv_sec=(int64_t)(ns/1000000000ULL);
    ts->tv_nsec=(int64_t)(ns%1000000000ULL);
}

static int c3_timerfd_alloc(int clockid){
    int i;
    for(i=0;i<C3_TIMERFD_MAX;++i){
        if(g_c3_timerfds[i].used)continue;
        c3_memset(&g_c3_timerfds[i],0,sizeof(g_c3_timerfds[i]));
        g_c3_timerfds[i].used=true;
        g_c3_timerfds[i].clockid=clockid;
        return i;
    }
    return -EMFILE;
}

static void c3_timerfd_free(int ref){
    if(ref<0||ref>=C3_TIMERFD_MAX)return;
    c3_memset(&g_c3_timerfds[ref],0,sizeof(g_c3_timerfds[ref]));
}

static uint64_t c3_timerfd_consume_expired(c3_timerfd_t *t,uint64_t now_tsc){
    uint64_t n=0;
    if(!t||!t->armed||!t->next_tsc||now_tsc<t->next_tsc)return 0;
    if(!t->interval_tsc){
        t->armed=false;
        return 1;
    }
    n=1+((now_tsc-t->next_tsc)/t->interval_tsc);
    if(n&&t->interval_tsc&&n>(~0ULL/t->interval_tsc)){
        t->next_tsc=~0ULL;
    }else if(n&&t->next_tsc>~0ULL-(n*t->interval_tsc)){
        t->next_tsc=~0ULL;
    }else{
        t->next_tsc+=n*t->interval_tsc;
    }
    return n;
}

static bool c3_timerfd_is_ready(int ref,uint64_t now_tsc){
    c3_timerfd_t *t;
    if(ref<0||ref>=C3_TIMERFD_MAX||!g_c3_timerfds[ref].used)return false;
    t=&g_c3_timerfds[ref];
    return t->armed&&t->next_tsc&&now_tsc>=t->next_tsc;
}

static int64_t c3_timerfd_read_ready(int ref,void *buf,size_t count,int flags){
    c3_timerfd_t *t;
    uint64_t now;
    uint64_t expirations;
    task_t *cur=task_current();
    if(ref<0||ref>=C3_TIMERFD_MAX||!g_c3_timerfds[ref].used)return -EBADF;
    if(!buf)return -EFAULT;
    if(count<sizeof(uint64_t))return -EINVAL;
    t=&g_c3_timerfds[ref];
    now=c3_rdtsc();
    expirations=c3_timerfd_consume_expired(t,now);
    if(!expirations){
        if(flags&C3_MSG_DONTWAIT)return -EAGAIN;
        if(!t->armed)return 0;
        if(cur&&t->next_tsc>now){
            cur->sleep_until=t->next_tsc;
            cur->state=TASK_SLEEPING;
            task_schedule();
            if(cur->state==TASK_SLEEPING){
                cur->state=TASK_RUNNING;
                __asm__ volatile("pause");
            }
        }
        now=c3_rdtsc();
        expirations=c3_timerfd_consume_expired(t,now);
        if(!expirations)return 0;
    }
    c3_memcpy(buf,&expirations,sizeof(expirations));
    return (int64_t)sizeof(expirations);
}

static int64_t c3_timerfd_settime_ref(int ref,int flags,const void *nv,void *ov){
    c3_timerfd_t *t;
    const c3_itimerspec_t *newv=(const c3_itimerspec_t*)nv;
    c3_itimerspec_t *oldv=(c3_itimerspec_t*)ov;
    uint64_t now_tsc;
    uint64_t old_interval_ns=0;
    uint64_t old_rem_ns=0;
    uint64_t val_ns;
    uint64_t int_ns;
    if(ref<0||ref>=C3_TIMERFD_MAX||!g_c3_timerfds[ref].used)return -EBADF;
    if(flags&~C3_TFD_TIMER_ABSTIME)return -EINVAL;
    if(!newv)return -EFAULT;
    if(!c3_timespec_valid(&newv->it_value)||!c3_timespec_valid(&newv->it_interval))return -EINVAL;
    t=&g_c3_timerfds[ref];
    now_tsc=c3_rdtsc();

    if(oldv){
        c3_memset(oldv,0,sizeof(*oldv));
        if(t->interval_tsc)old_interval_ns=tsc_to_ns(t->interval_tsc);
        if(t->armed&&t->next_tsc>now_tsc)old_rem_ns=tsc_to_ns(t->next_tsc-now_tsc);
        c3_ns_to_timespec(old_interval_ns,&oldv->it_interval);
        c3_ns_to_timespec(old_rem_ns,&oldv->it_value);
    }

    val_ns=c3_timespec_to_ns(&newv->it_value);
    int_ns=c3_timespec_to_ns(&newv->it_interval);
    if(val_ns==~0ULL||int_ns==~0ULL)return -EINVAL;
    if(val_ns==0){
        t->armed=false;
        t->next_tsc=0;
        t->interval_tsc=0;
        return 0;
    }

    if(flags&C3_TFD_TIMER_ABSTIME){
        timespec_t nowts;
        uint64_t now_abs_ns;
        if(real_sys_clock_gettime(t->clockid,&nowts)<0)return -EINVAL;
        now_abs_ns=c3_timespec_to_ns(&nowts);
        if(now_abs_ns==~0ULL)now_abs_ns=0;
        if(val_ns<=now_abs_ns)val_ns=1;
        else val_ns-=now_abs_ns;
    }

    t->interval_tsc=c3_ns_to_tsc(int_ns);
    if(int_ns&&t->interval_tsc==0)t->interval_tsc=1;
    t->next_tsc=now_tsc+c3_ns_to_tsc(val_ns);
    if(t->next_tsc<=now_tsc)t->next_tsc=now_tsc+1;
    t->armed=true;
    return 0;
}

static int c3_epoll_ref_from_fd(task_t *cur,int epfd){
    if(!cur||!fd_valid(&cur->fdt,epfd))return -EBADF;
    if(cur->fdt.fds[epfd].kind!=FDKIND_EPOLL)return -EBADF;
    if(cur->fdt.fds[epfd].ref<0||cur->fdt.fds[epfd].ref>=EPOLL_INSTANCES)return -EBADF;
    return cur->fdt.fds[epfd].ref;
}

static uint32_t c3_fd_ready_mask(task_t *cur,int fd){
    real_fd_t *fe;
    if(!cur||!fd_valid(&cur->fdt,fd))return EPOLLERR|EPOLLHUP;
    fe=&cur->fdt.fds[fd];
    switch(fe->kind){
        case FDKIND_SOCKET: {
            socket_t *s;
            uint32_t m=0;
            size_t rx;
            int aq;
            bool unix_peerless_idle;
            if(fe->ref<0||fe->ref>=SOCK_MAX)return EPOLLERR|EPOLLHUP;
            s=&g_sockets[fe->ref];
            if(!s->used)return EPOLLERR|EPOLLHUP;
            rx=(size_t)(s->rx_head-s->rx_tail);
            if(rx>SOCK_BUF_SIZE)rx=SOCK_BUF_SIZE;
            aq=(int)((s->accept_head+SOCK_ACCEPTQ-s->accept_tail)%SOCK_ACCEPTQ);
            unix_peerless_idle=(s->domain==1&&s->peer<0&&s->virt_service==SOCK_VIRT_NONE&&
                                !rx&&s->anc_head==s->anc_tail);
            if(rx||s->anc_head!=s->anc_tail||(s->tcp_state==TCP_LISTEN&&aq>0))m|=EPOLLIN;
            if(s->type==1){
                if((s->tcp_state==TCP_ESTABLISHED||s->tcp_state==TCP_SYN_SENT||s->tcp_state==TCP_LISTEN)&&
                   c3_socket_send_space(fe->ref)>0)m|=EPOLLOUT;
                /*
                 * The tiny socket layer uses TCP_CLOSED both for real EOF and
                 * for never-connected AF_UNIX descriptors.  Firefox passes IPC
                 * sockets through SCM_RIGHTS; briefly peerless empty streams
                 * must sleep in epoll instead of spinning on a synthetic HUP.
                 */
                if(s->tcp_state==TCP_CLOSED&&!unix_peerless_idle)m|=EPOLLIN|EPOLLHUP;
            }else{
                m|=EPOLLOUT;
            }
            return m;
        }
        case FDKIND_PIPE_R: {
            pipe_t *p;
            size_t av;
            uint32_t m=0;
            if(fe->ref<0||fe->ref>=PIPE_MAX||!g_pipes[fe->ref].used)return EPOLLERR|EPOLLHUP;
            p=&g_pipes[fe->ref];
            av=(size_t)((p->head-p->tail+PIPE_BUF_SIZE)%PIPE_BUF_SIZE);
            if(av)m|=EPOLLIN;
            if(p->writers<=0)m|=EPOLLIN|EPOLLHUP;
            return m;
        }
        case FDKIND_PIPE_W: {
            pipe_t *p;
            size_t av;
            uint32_t m=0;
            if(fe->ref<0||fe->ref>=PIPE_MAX||!g_pipes[fe->ref].used)return EPOLLERR|EPOLLHUP;
            p=&g_pipes[fe->ref];
            av=(size_t)(PIPE_BUF_SIZE-((p->head-p->tail+PIPE_BUF_SIZE)%PIPE_BUF_SIZE)-1);
            if(av)m|=EPOLLOUT;
            if(p->readers<=0)m|=EPOLLERR|EPOLLHUP;
            return m;
        }
        case FDKIND_EVENTFD:
            if(fe->ref<0||fe->ref>=C3_EVENTFD_MAX||!g_c3_eventfds[fe->ref].used)return EPOLLERR|EPOLLHUP;
            return (g_c3_eventfds[fe->ref].counter?EPOLLIN:0)|EPOLLOUT;
        case FDKIND_TIMERFD:
            if(fe->ref<0||fe->ref>=C3_TIMERFD_MAX||!g_c3_timerfds[fe->ref].used)return EPOLLERR|EPOLLHUP;
            return (c3_timerfd_is_ready(fe->ref,c3_rdtsc())?EPOLLIN:0)|EPOLLOUT;
        case FDKIND_SIGNALFD: return c3_task_has_unblocked_signal(cur)?EPOLLIN:EPOLLOUT;
        case FDKIND_INOTIFY:
            if(fe->ref<0||fe->ref>=C3_INOTIFY_INST_MAX||!g_c3_inotify_inst[fe->ref].used)return EPOLLERR|EPOLLHUP;
            return c3_inotify_q_empty(&g_c3_inotify_inst[fe->ref])?0:EPOLLIN;
        case FDKIND_VFSFILE:
        case FDKIND_DEVNULL:
        case FDKIND_DEVZERO:
        case FDKIND_DEVRANDOM:
        case FDKIND_DEVFB:
        case FDKIND_DEVTTY:
        case FDKIND_PROC:
        case FDKIND_DIR:
            return EPOLLIN|EPOLLOUT;
        default: return EPOLLERR|EPOLLHUP;
    }
}

int64_t real_sys_epoll_create1(int flags){
    task_t *cur=task_current();
    int i,fd;
    uint16_t ff=FDFL_READABLE|FDFL_WRITABLE;
    if(flags&~O_CLOEXEC)return -EINVAL;
    for(i=0;i<EPOLL_INSTANCES;++i){
        if(g_epoll_instances[i].used)continue;
        c3_memset(&g_epoll_instances[i],0,sizeof(g_epoll_instances[i]));
        g_epoll_instances[i].used=true;
        if(flags&O_CLOEXEC)ff|=FDFL_CLOEXEC;
        fd=c3_fd_alloc_for_task(cur,FDKIND_EPOLL,i,ff);
        if(fd<0){g_epoll_instances[i].used=false;return -EMFILE;}
        {
            static uint32_t trace_count=0;
            if(trace_count<24){
                ++trace_count;
                __boot_serial_puts("[epoll-create] pid=");
                __boot_serial_putu32(cur?(uint32_t)cur->pid:0);
                c3_trace_task_name(cur);
                __boot_serial_puts(" fd=");
                __boot_serial_putu32((uint32_t)fd);
                __boot_serial_puts(" ref=");
                __boot_serial_putu32((uint32_t)i);
                __boot_serial_puts("\n");
            }
        }
        return fd;
    }
    return -ENOMEM;
}

int64_t real_sys_epoll_ctl(int epfd,int op,int fd,void *event){
    task_t *cur=task_current();
    int ref=c3_epoll_ref_from_fd(cur,epfd);
    epoll_instance_t *ep;
    epoll_event_t *ev=(epoll_event_t*)event;
    int i,found=-1,free_slot=-1;
    if(ref<0)return ref;
    ep=&g_epoll_instances[ref];
    if((op==EPOLL_CTL_ADD||op==EPOLL_CTL_MOD)&&!fd_valid(&cur->fdt,fd))return -EBADF;
    {
        static uint32_t trace_count=0;
        if(trace_count<96){
            uint8_t kind=0;
            int item_ref=-1;
            uint32_t events=ev?ev->events:0;
            ++trace_count;
            if(cur&&fd_valid(&cur->fdt,fd)){
                kind=cur->fdt.fds[fd].kind;
                item_ref=cur->fdt.fds[fd].ref;
            }
            __boot_serial_puts("[epoll-ctl] pid=");
            __boot_serial_putu32(cur?(uint32_t)cur->pid:0);
            c3_trace_task_name(cur);
            __boot_serial_puts(" epfd=");
            __boot_serial_putu32((uint32_t)epfd);
            __boot_serial_puts(" op=");
            __boot_serial_putu32((uint32_t)op);
            __boot_serial_puts(" fd=");
            __boot_serial_putu32((uint32_t)fd);
            __boot_serial_puts(" kind=");
            __boot_serial_putu32((uint32_t)kind);
            __boot_serial_puts(" ref=");
            __boot_serial_putu32((uint32_t)item_ref);
            __boot_serial_puts(" ev=");
            __boot_serial_puthex64((uint64_t)events);
            if(ev){
                __boot_serial_puts(" data=");
                __boot_serial_puthex64(ev->data);
            }
            __boot_serial_puts("\n");
        }
    }
    for(i=0;i<EPOLL_MAX_EVENTS;++i){
        if(ep->items[i].used&&ep->items[i].fd==fd){found=i;break;}
        if(!ep->items[i].used&&free_slot<0)free_slot=i;
    }
    if(op==EPOLL_CTL_ADD){
        int slot;
        if(found>=0)return -EEXIST;
        if(free_slot<0)return -ENOMEM;
        slot=free_slot;
        ep->items[slot].used=true;
        ep->items[slot].fd=fd;
        ep->items[slot].events=ev?ev->events:(EPOLLIN|EPOLLOUT);
        ep->items[slot].data=ev?ev->data:(uint64_t)(uint32_t)fd;
        if(slot>=ep->count)ep->count=slot+1;
        return 0;
    }
    if(op==EPOLL_CTL_MOD){
        if(found<0)return -ENOENT;
        ep->items[found].events=ev?ev->events:ep->items[found].events;
        if(ev)ep->items[found].data=ev->data;
        return 0;
    }
    if(op==EPOLL_CTL_DEL){
        if(found<0)return -ENOENT;
        ep->items[found].used=false;
        while(ep->count>0&&!ep->items[ep->count-1].used)--ep->count;
        return 0;
    }
    return -EINVAL;
}

int64_t real_sys_epoll_wait(int epfd,void *events,int maxevents,int timeout){
    task_t *cur=task_current();
    epoll_event_t *out=(epoll_event_t*)events;
    int ref=c3_epoll_ref_from_fd(cur,epfd);
    epoll_instance_t *ep;
    uint64_t deadline=0;
    if(ref<0)return ref;
    if(maxevents<=0)return -EINVAL;
    if(!out)return -EFAULT;
    ep=&g_epoll_instances[ref];
    if(timeout>0)deadline=c3_dyn_deadline_from_ms((uint32_t)timeout);
    {
        static uint32_t trace_count=0;
        if(trace_count<48){
            ++trace_count;
            __boot_serial_puts("[epoll-wait] pid=");
            __boot_serial_putu32(cur?(uint32_t)cur->pid:0);
            c3_trace_task_name(cur);
            __boot_serial_puts(" epfd=");
            __boot_serial_putu32((uint32_t)epfd);
            __boot_serial_puts(" ref=");
            __boot_serial_putu32((uint32_t)ref);
            __boot_serial_puts(" count=");
            __boot_serial_putu32((uint32_t)ep->count);
            __boot_serial_puts(" timeout=");
            __boot_serial_putu32((uint32_t)timeout);
            if(ep->count>0&&ep->items[0].used){
                __boot_serial_puts(" fd0=");
                __boot_serial_putu32((uint32_t)ep->items[0].fd);
                __boot_serial_puts(" ev0=");
                __boot_serial_puthex64((uint64_t)ep->items[0].events);
                __boot_serial_puts(" data0=");
                __boot_serial_puthex64(ep->items[0].data);
            }
            __boot_serial_puts("\n");
        }
    }
    for(;;){
        int i,n=0;
        for(i=0;i<ep->count&&n<maxevents;++i){
            uint32_t want,mask,ready;
            if(!ep->items[i].used)continue;
            want=ep->items[i].events?ep->items[i].events:(EPOLLIN|EPOLLOUT);
            mask=c3_fd_ready_mask(cur,ep->items[i].fd);
            ready=mask&(want|EPOLLERR|EPOLLHUP);
            if(!ready)continue;
            {
                static uint32_t ready_trace_count=0;
                if(ready_trace_count<96){
                    real_fd_t *fe=0;
                    uint32_t rx=0;
                    int peer=-1;
                    uint8_t svc=0,st=0;
                    uint16_t anc=0;
                    uint32_t pipe_av=0,pipe_readers=0,pipe_writers=0;
                    ++ready_trace_count;
                    if(cur&&fd_valid(&cur->fdt,ep->items[i].fd)){
                        fe=&cur->fdt.fds[ep->items[i].fd];
                        if(fe->kind==FDKIND_SOCKET&&fe->ref>=0&&fe->ref<SOCK_MAX&&g_sockets[fe->ref].used){
                            socket_t *s=&g_sockets[fe->ref];
                            rx=s->rx_head-s->rx_tail;
                            peer=s->peer;
                            svc=s->virt_service;
                            st=(uint8_t)s->tcp_state;
                            anc=(uint16_t)(uint8_t)(s->anc_head-s->anc_tail);
                        }else if((fe->kind==FDKIND_PIPE_R||fe->kind==FDKIND_PIPE_W)&&
                                 fe->ref>=0&&fe->ref<PIPE_MAX&&g_pipes[fe->ref].used){
                            pipe_t *p=&g_pipes[fe->ref];
                            pipe_av=(uint32_t)((p->head-p->tail+PIPE_BUF_SIZE)%PIPE_BUF_SIZE);
                            pipe_readers=(uint32_t)p->readers;
                            pipe_writers=(uint32_t)p->writers;
                        }
                    }
                    __boot_serial_puts("[epoll-ready] pid=");
                    __boot_serial_putu32(cur?(uint32_t)cur->pid:0);
                    c3_trace_task_name(cur);
                    __boot_serial_puts(" epfd=");
                    __boot_serial_putu32((uint32_t)epfd);
                    __boot_serial_puts(" fd=");
                    __boot_serial_putu32((uint32_t)ep->items[i].fd);
                    __boot_serial_puts(" kind=");
                    __boot_serial_putu32(fe?(uint32_t)fe->kind:0);
                    __boot_serial_puts(" ref=");
                    __boot_serial_putu32(fe?(uint32_t)fe->ref:0xFFFFFFFFu);
                    __boot_serial_puts(" want=");
                    __boot_serial_puthex64((uint64_t)want);
                    __boot_serial_puts(" mask=");
                    __boot_serial_puthex64((uint64_t)mask);
                    __boot_serial_puts(" ready=");
                    __boot_serial_puthex64((uint64_t)ready);
                    __boot_serial_puts(" data=");
                    __boot_serial_puthex64(ep->items[i].data);
                    if(fe&&fe->kind==FDKIND_SOCKET){
                        __boot_serial_puts(" rx=");
                        __boot_serial_putu32(rx);
                        __boot_serial_puts(" anc=");
                        __boot_serial_putu32((uint32_t)anc);
                        __boot_serial_puts(" peer=");
                        __boot_serial_putu32((uint32_t)peer);
                        __boot_serial_puts(" svc=");
                        __boot_serial_putu32((uint32_t)svc);
                        __boot_serial_puts(" st=");
                        __boot_serial_putu32((uint32_t)st);
                    }else if(fe&&(fe->kind==FDKIND_PIPE_R||fe->kind==FDKIND_PIPE_W)){
                        __boot_serial_puts(" pipe_av=");
                        __boot_serial_putu32(pipe_av);
                        __boot_serial_puts(" readers=");
                        __boot_serial_putu32(pipe_readers);
                        __boot_serial_puts(" writers=");
                        __boot_serial_putu32(pipe_writers);
                    }
                    __boot_serial_puts("\n");
                }
            }
            out[n].events=ready;
            out[n].data=ep->items[i].data?ep->items[i].data:(uint64_t)(uint32_t)ep->items[i].fd;
            ++n;
        }
        if(n>0)return n;
        if(timeout==0)return 0;
        if(c3_task_has_unblocked_signal(cur))return -EINTR;
        if(deadline&&c3_dyn_deadline_expired(deadline))return 0;
        c3_poll_wait_slice(cur,deadline);
    }
}

int64_t real_sys_poll(void *fds,uint64_t nfds,int timeout){
    task_t *cur=task_current();
    c3_pollfd_t *pf=(c3_pollfd_t*)fds;
    uint64_t deadline=0;
    bool trace_this=false;
    uint32_t trace_no=0;
    uint32_t trace_waits=0;
    static uint32_t poll_trace_count=0;
    if(nfds&&(!pf))return -EFAULT;
    if(nfds>4096)nfds=4096;
    if(timeout>0)deadline=c3_dyn_deadline_from_ms((uint32_t)timeout);
    if(poll_trace_count<160){
        trace_this=true;
        trace_no=++poll_trace_count;
        __boot_serial_puts("[poll] #");
        __boot_serial_putu32(trace_no);
        __boot_serial_puts(" pid=");
        __boot_serial_putu32(cur?(uint32_t)cur->pid:0);
        c3_trace_task_name(cur);
        __boot_serial_puts(" nfds=");
        __boot_serial_putu32((uint32_t)nfds);
        __boot_serial_puts(" timeout=");
        __boot_serial_putu32((uint32_t)timeout);
        if(pf&&nfds){
            __boot_serial_puts(" fd0=");
            __boot_serial_putu32((uint32_t)pf[0].fd);
            __boot_serial_puts(" ev0=");
            __boot_serial_puthex64((uint64_t)(uint16_t)pf[0].events);
            if(cur&&fd_valid(&cur->fdt,pf[0].fd)){
                real_fd_t *fe=&cur->fdt.fds[pf[0].fd];
                __boot_serial_puts(" kind0=");
                __boot_serial_putu32((uint32_t)fe->kind);
                __boot_serial_puts(" ref0=");
                __boot_serial_putu32((uint32_t)fe->ref);
                if(fe->kind==FDKIND_SOCKET&&fe->ref>=0&&fe->ref<SOCK_MAX&&g_sockets[fe->ref].used){
                    socket_t *s=&g_sockets[fe->ref];
                    uint32_t rx=s->rx_head-s->rx_tail;
                    __boot_serial_puts(" rx0=");
                    __boot_serial_putu32(rx);
                    __boot_serial_puts(" peer0=");
                    __boot_serial_putu32((uint32_t)s->peer);
                    __boot_serial_puts(" svc0=");
                    __boot_serial_putu32((uint32_t)s->virt_service);
                    __boot_serial_puts(" st0=");
                    __boot_serial_putu32((uint32_t)s->tcp_state);
                }
            }
        }
        __boot_serial_puts("\n");
    }
    for(;;){
        uint64_t i,ready=0;
        uint32_t mask0=0,want0=0,rev0=0;
        for(i=0;i<nfds;++i){
            uint32_t mask;
            uint32_t want;
            if(pf[i].fd<0){pf[i].revents=0;continue;}
            mask=c3_fd_ready_mask(cur,pf[i].fd);
            want=(uint16_t)pf[i].events;
            pf[i].revents=(short)(mask&(want|C3_POLLERR|C3_POLLHUP));
            if(i==0){
                mask0=mask;
                want0=want;
                rev0=(uint32_t)(uint16_t)pf[i].revents;
            }
            if(pf[i].revents)++ready;
        }
        if(ready){
            if(trace_this){
                __boot_serial_puts("[poll-ret] #");
                __boot_serial_putu32(trace_no);
                __boot_serial_puts(" ready=");
                __boot_serial_putu32((uint32_t)ready);
                __boot_serial_puts(" mask0=");
                __boot_serial_puthex64(mask0);
                __boot_serial_puts(" want0=");
                __boot_serial_puthex64(want0);
                __boot_serial_puts(" rev0=");
                __boot_serial_puthex64(rev0);
                __boot_serial_puts("\n");
            }
            return (int64_t)ready;
        }
        if(timeout==0)return 0;
        if(c3_task_has_unblocked_signal(cur))return -EINTR;
        if(deadline&&c3_dyn_deadline_expired(deadline))return 0;
        if(trace_this&&trace_waits<6){
            __boot_serial_puts("[poll-wait] #");
            __boot_serial_putu32(trace_no);
            __boot_serial_puts(" wait=");
            __boot_serial_putu32(trace_waits);
            __boot_serial_puts(" mask0=");
            __boot_serial_puthex64(mask0);
            __boot_serial_puts(" want0=");
            __boot_serial_puthex64(want0);
            __boot_serial_puts(" rev0=");
            __boot_serial_puthex64(rev0);
            __boot_serial_puts("\n");
        }
        ++trace_waits;
        c3_poll_wait_slice(cur,deadline);
    }
}

int64_t real_sys_select(int nfds,void *rfds,void *wfds,void *efds,timeval_t *timeout){
    task_t *cur=task_current();
    uint64_t in_r[(TASK_FD_MAX+63)/64],in_w[(TASK_FD_MAX+63)/64],in_e[(TASK_FD_MAX+63)/64];
    uint64_t out_r[(TASK_FD_MAX+63)/64],out_w[(TASK_FD_MAX+63)/64],out_e[(TASK_FD_MAX+63)/64];
    int lim=nfds;
    size_t words;
    size_t nbytes;
    uint64_t deadline=0;
    int has_timeout=0;
    if(lim<0)return -EINVAL;
    if(lim>TASK_FD_MAX)lim=TASK_FD_MAX;
    words=(size_t)((lim+63)/64);
    nbytes=words*sizeof(uint64_t);
    c3_memset(in_r,0,sizeof(in_r));
    c3_memset(in_w,0,sizeof(in_w));
    c3_memset(in_e,0,sizeof(in_e));
    if(rfds&&nbytes)c3_memcpy(in_r,rfds,nbytes);
    if(wfds&&nbytes)c3_memcpy(in_w,wfds,nbytes);
    if(efds&&nbytes)c3_memcpy(in_e,efds,nbytes);
    if(timeout){
        uint32_t ms;
        if(timeout->tv_sec<0||timeout->tv_usec<0||timeout->tv_usec>=1000000LL)return -EINVAL;
        has_timeout=1;
        ms=c3_timeval_to_ms_ceil(timeout);
        deadline=ms?c3_dyn_deadline_from_ms(ms):c3_rdtsc();
    }
    for(;;){
        int fd,ready=0;
        c3_memset(out_r,0,sizeof(out_r));
        c3_memset(out_w,0,sizeof(out_w));
        c3_memset(out_e,0,sizeof(out_e));
        for(fd=0;fd<lim;++fd){
            bool tr=false,tw=false,te=false;
            bool any=false;
            uint32_t mask;
            uint64_t bit=(1ULL<<(fd&63));
            size_t wi=(size_t)(fd>>6);
            if((!rfds||((in_r[wi]&bit)==0))&&(!wfds||((in_w[wi]&bit)==0))&&(!efds||((in_e[wi]&bit)==0)))continue;
            mask=c3_fd_ready_mask(cur,fd);
            if(rfds&&(in_r[wi]&bit)&& (mask&(EPOLLIN|EPOLLHUP|EPOLLERR))){out_r[wi]|=bit;tr=true;any=true;}
            if(wfds&&(in_w[wi]&bit)&& (mask&(EPOLLOUT|EPOLLERR))){out_w[wi]|=bit;tw=true;any=true;}
            if(efds&&(in_e[wi]&bit)&& (mask&EPOLLERR)){out_e[wi]|=bit;te=true;any=true;}
            if(any){(void)tr;(void)tw;(void)te;++ready;}
        }
        if(ready){
            if(rfds&&nbytes)c3_memcpy(rfds,out_r,nbytes);
            if(wfds&&nbytes)c3_memcpy(wfds,out_w,nbytes);
            if(efds&&nbytes)c3_memcpy(efds,out_e,nbytes);
            return ready;
        }
        if(has_timeout&&deadline&&c3_dyn_deadline_expired(deadline)){
            if(rfds&&nbytes)c3_memset(rfds,0,nbytes);
            if(wfds&&nbytes)c3_memset(wfds,0,nbytes);
            if(efds&&nbytes)c3_memset(efds,0,nbytes);
            return 0;
        }
        if(c3_task_has_unblocked_signal(cur))return -EINTR;
        if(has_timeout&&deadline==c3_rdtsc()){
            if(rfds&&nbytes)c3_memset(rfds,0,nbytes);
            if(wfds&&nbytes)c3_memset(wfds,0,nbytes);
            if(efds&&nbytes)c3_memset(efds,0,nbytes);
            return 0;
        }
        c3_poll_wait_slice(cur,has_timeout?deadline:0);
    }
}

typedef struct {
    uint64_t sigmask;
    uint64_t sigsetsize;
} c3_pselect6_sigarg_t;

static int c3_apply_temp_sigmask(task_t *cur,const void *set,size_t setsize,sigset_t2 *saved){
    sigset_t2 tmp;
    size_t nbytes;
    if(!cur||!saved)return -EFAULT;
    *saved=cur->sig_blocked;
    if(!set)return 0;
    if(setsize&&(setsize<sizeof(uint64_t)||setsize>sizeof(sigset_t2)))return -EINVAL;
    nbytes=setsize?setsize:sizeof(sigset_t2);
    if(nbytes>sizeof(sigset_t2))nbytes=sizeof(sigset_t2);
    c3_memset(&tmp,0,sizeof(tmp));
    c3_memcpy(&tmp,set,nbytes);
    c3_sigset_drop_unblockable(&tmp);
    cur->sig_blocked=tmp;
    return 0;
}

static void c3_restore_temp_sigmask(task_t *cur,const sigset_t2 *saved){
    if(!cur||!saved)return;
    cur->sig_blocked=*saved;
    sig_check_pending(cur);
}

int64_t real_sys_ppoll(void *fds,uint64_t nfds,const timespec_t *timeout,const void *sigmask,size_t sigsetsize){
    task_t *cur=task_current();
    sigset_t2 saved;
    int rc_mask;
    int timeout_ms=-1;
    int64_t rc;
    if(!cur)return -ESRCH;
    if(timeout){
        if(timeout->tv_sec<0||timeout->tv_nsec<0||timeout->tv_nsec>=1000000000LL)return -EINVAL;
        timeout_ms=(int)c3_timespec_to_ms_ceil(timeout);
    }
    {
        static uint32_t ppoll_trace_count=0;
        if(ppoll_trace_count<48){
            c3_pollfd_t *pf=(c3_pollfd_t*)fds;
            ++ppoll_trace_count;
            __boot_serial_puts("[ppoll] pid=");
            __boot_serial_putu32((uint32_t)cur->pid);
            __boot_serial_puts(" nfds=");
            __boot_serial_putu32((uint32_t)nfds);
            __boot_serial_puts(" timeout=");
            __boot_serial_puthex64((uint64_t)(uintptr_t)timeout);
            __boot_serial_puts(" ms=");
            __boot_serial_putu32((uint32_t)(timeout_ms<0?0xFFFFFFFFu:(uint32_t)timeout_ms));
            if(pf&&nfds){
                __boot_serial_puts(" fd0=");
                __boot_serial_putu32((uint32_t)pf[0].fd);
                __boot_serial_puts(" ev0=");
                __boot_serial_puthex64((uint64_t)(uint16_t)pf[0].events);
            }
            __boot_serial_puts("\n");
        }
    }
    rc_mask=c3_apply_temp_sigmask(cur,sigmask,sigsetsize,&saved);
    if(rc_mask<0)return rc_mask;
    rc=real_sys_poll(fds,nfds,timeout_ms);
    c3_restore_temp_sigmask(cur,&saved);
    return rc;
}

int64_t real_sys_pselect6(int nfds,void *rfds,void *wfds,void *efds,const timespec_t *timeout,const void *sigarg){
    task_t *cur=task_current();
    sigset_t2 saved;
    const void *set=0;
    size_t setsize=0;
    timeval_t tv;
    timeval_t *tvp=0;
    int rc_mask;
    int64_t rc;
    if(!cur)return -ESRCH;
    if(timeout){
        int64_t usec;
        if(timeout->tv_sec<0||timeout->tv_nsec<0||timeout->tv_nsec>=1000000000LL)return -EINVAL;
        usec=(timeout->tv_nsec+999LL)/1000LL;
        tv.tv_sec=timeout->tv_sec+(usec/1000000LL);
        tv.tv_usec=usec%1000000LL;
        tvp=&tv;
    }
    if(sigarg){
        const c3_pselect6_sigarg_t *a=(const c3_pselect6_sigarg_t*)sigarg;
        if(a->sigmask&&a->sigsetsize>=sizeof(uint64_t)&&a->sigsetsize<=sizeof(sigset_t2)){
            set=(const void*)(uintptr_t)a->sigmask;
            setsize=(size_t)a->sigsetsize;
        }else{
            set=sigarg;
            setsize=sizeof(sigset_t2);
        }
    }
    rc_mask=c3_apply_temp_sigmask(cur,set,setsize,&saved);
    if(rc_mask<0)return rc_mask;
    rc=real_sys_select(nfds,rfds,wfds,efds,tvp);
    c3_restore_temp_sigmask(cur,&saved);
    return rc;
}

int64_t real_sys_eventfd2(unsigned int initval,int flags){
    task_t *cur=task_current();
    int ref;
    uint16_t ff=FDFL_READABLE|FDFL_WRITABLE;
    if(flags&~(C3_EFD_SEMAPHORE|O_NONBLOCK|O_CLOEXEC))return -EINVAL;
    ref=c3_eventfd_alloc((uint64_t)initval,(flags&C3_EFD_SEMAPHORE)!=0);
    if(ref<0)return ref;
    if(flags&O_NONBLOCK)ff|=FDFL_NONBLOCK;
    if(flags&O_CLOEXEC)ff|=FDFL_CLOEXEC;
    {
        int fd=c3_fd_alloc_for_task(cur,FDKIND_EVENTFD,ref,ff);
        if(fd<0){c3_eventfd_free(ref);return -EMFILE;}
        {
            static uint32_t trace_count=0;
            if(trace_count<32){
                ++trace_count;
                __boot_serial_puts("[eventfd-fd] pid=");
                __boot_serial_putu32(cur?(uint32_t)cur->pid:0);
                c3_trace_task_name(cur);
                __boot_serial_puts(" fd=");
                __boot_serial_putu32((uint32_t)fd);
                __boot_serial_puts(" ref=");
                __boot_serial_putu32((uint32_t)ref);
                __boot_serial_puts(" flags=");
                __boot_serial_puthex64((uint64_t)(uint32_t)flags);
                __boot_serial_puts("\n");
            }
        }
        return fd;
    }
}

int64_t real_sys_timerfd_create(int clockid,int flags){
    task_t *cur=task_current();
    int ref;
    uint16_t ff=FDFL_READABLE;
    if(flags&~(O_NONBLOCK|O_CLOEXEC))return -EINVAL;
    ref=c3_timerfd_alloc(clockid);
    if(ref<0)return ref;
    if(flags&O_NONBLOCK)ff|=FDFL_NONBLOCK;
    if(flags&O_CLOEXEC)ff|=FDFL_CLOEXEC;
    {
        int fd=c3_fd_alloc_for_task(cur,FDKIND_TIMERFD,ref,ff);
        if(fd<0){c3_timerfd_free(ref);return fd;}
        return fd;
    }
}
int64_t real_sys_timerfd_settime(int fd,int flags,const void *nv,void *ov){
    task_t *cur=task_current();
    if(!fd_valid(&cur->fdt,fd)||cur->fdt.fds[fd].kind!=FDKIND_TIMERFD)return -EBADF;
    return c3_timerfd_settime_ref(cur->fdt.fds[fd].ref,flags,nv,ov);
}

int64_t real_sys_timerfd_gettime(int fd,void *cur_value){
    task_t *cur=task_current();
    c3_itimerspec_t *cv=(c3_itimerspec_t*)cur_value;
    c3_timerfd_t *t;
    uint64_t now_tsc;
    uint64_t rem_ns=0;
    if(!cur||!cur_value)return -EFAULT;
    if(!fd_valid(&cur->fdt,fd)||cur->fdt.fds[fd].kind!=FDKIND_TIMERFD)return -EBADF;
    t=&g_c3_timerfds[cur->fdt.fds[fd].ref];
    now_tsc=c3_rdtsc();
    c3_memset(cv,0,sizeof(*cv));
    if(t->interval_tsc)c3_ns_to_timespec(tsc_to_ns(t->interval_tsc),&cv->it_interval);
    if(t->armed&&t->next_tsc>now_tsc)rem_ns=tsc_to_ns(t->next_tsc-now_tsc);
    c3_ns_to_timespec(rem_ns,&cv->it_value);
    return 0;
}

int64_t real_sys_signalfd4(int fd,const void *mask,size_t sizemask,int flags){
    task_t *cur=task_current();
    uint16_t ff=FDFL_READABLE;
    (void)fd;(void)mask;(void)sizemask;
    if(flags&O_NONBLOCK)ff|=FDFL_NONBLOCK;
    if(flags&O_CLOEXEC)ff|=FDFL_CLOEXEC;
    return(int64_t)c3_fd_alloc_for_task(cur,FDKIND_SIGNALFD,0,ff);
}

/* Misc syscalls */
int64_t real_sys_shmget(int key,size_t size,int shmflg){return(int64_t)shm_get(key,(uint32_t)size,shmflg);}
int64_t real_sys_shmat(int shmid,uint64_t shmaddr,int shmflg){(void)shmflg;return(int64_t)(uintptr_t)shm_attach(shmid,shmaddr);}
int64_t real_sys_shmdt(uint64_t shmaddr){return(int64_t)shm_detach((void*)(uintptr_t)shmaddr);}
int64_t real_sys_shmctl(int shmid,int cmd,void *buf){return(int64_t)shm_ctl(shmid,cmd,buf);}
int64_t real_sys_umask(int mask){int old=(int)g_umask_val;g_umask_val=(uint32_t)mask&0777;return old;}
int64_t real_sys_getrusage(int who,void *usage){(void)who;if(usage)c3_memset(usage,0,144);return 0;}
int64_t real_sys_times(void *buf){if(buf)c3_memset(buf,0,32);return 0;}

#define C3_PRIO_PROCESS 0
#define C3_PRIO_PGRP    1
#define C3_PRIO_USER    2
static int c3_priority_target(task_t *cur,int which,int who){
    int idx;
    if(!cur)return -ESRCH;
    switch(which){
        case C3_PRIO_PROCESS:
            if(who==0||who==cur->pid)return g_current_task;
            idx=c3_task_index_by_pid(who);
            if(idx<0)return -ESRCH;
            return idx;
        case C3_PRIO_PGRP:
        case C3_PRIO_USER:
            return g_current_task;
        default:
            return -EINVAL;
    }
}

int64_t real_sys_getpriority(int which,int who){
    task_t *cur=task_current();
    int tidx=c3_priority_target(cur,which,who);
    if(tidx<0)return tidx;
    return 20-g_tasks[tidx].nice;
}

int64_t real_sys_setpriority(int which,int who,int prio){
    task_t *cur=task_current();
    int tidx=c3_priority_target(cur,which,who);
    if(tidx<0)return tidx;
    if(prio<-20)prio=-20;
    if(prio>19)prio=19;
    g_tasks[tidx].nice=prio;
    return 0;
}

int64_t real_sys_sched_yield(void){task_schedule();return 0;}
static int c3_sched_affinity_target(task_t *cur,int pid){
    int idx;
    if(!cur)return -ESRCH;
    if(pid==0||pid==cur->pid)return g_current_task;
    idx=c3_task_index_by_pid(pid);
    if(idx<0)return -ESRCH;
    if(cur->uid!=0&&cur->uid!=g_tasks[idx].uid)return -EPERM;
    return idx;
}

static bool c3_affinity_has_any(const uint64_t *m,size_t words){
    size_t i;
    if(!m)return false;
    for(i=0;i<words;++i)if(m[i])return true;
    return false;
}

int64_t real_sys_sched_getaffinity(int pid,size_t len,void *mask){
    task_t *cur=task_current();
    int tidx=c3_sched_affinity_target(cur,pid);
    size_t need=sizeof(uint64_t)*C3_AFFINITY_WORDS;
    size_t ncopy;
    if(tidx<0)return tidx;
    if(!mask)return -EFAULT;
    if(len<sizeof(uint64_t))return -EINVAL;
    if(!c3_affinity_has_any(g_c3_sched_affinity[tidx],C3_AFFINITY_WORDS))g_c3_sched_affinity[tidx][0]=1ULL;
    ncopy=len<need?len:need;
    c3_memset(mask,0,len);
    c3_memcpy(mask,g_c3_sched_affinity[tidx],ncopy);
    return (int64_t)need;
}

int64_t real_sys_sched_setaffinity(int pid,size_t len,const void *mask){
    task_t *cur=task_current();
    int tidx=c3_sched_affinity_target(cur,pid);
    uint64_t tmp[C3_AFFINITY_WORDS];
    size_t need=sizeof(uint64_t)*C3_AFFINITY_WORDS;
    size_t ncopy;
    if(tidx<0)return tidx;
    if(!mask)return -EFAULT;
    if(len==0)return -EINVAL;
    c3_memset(tmp,0,sizeof(tmp));
    ncopy=len<need?len:need;
    c3_memcpy(tmp,mask,ncopy);
    /* Ridux currently has one online CPU (cpu0), so reject empty masks. */
    if(!c3_affinity_has_any(tmp,C3_AFFINITY_WORDS))return -EINVAL;
    g_c3_sched_affinity[tidx][0]=tmp[0]|1ULL;
    return 0;
}

int64_t real_sys_sched_get_priority_max(int policy){
    switch(policy){
        case 1: /* SCHED_FIFO */
        case 2: /* SCHED_RR */
            return 99;
        case 0: /* SCHED_OTHER */
        case 3: /* SCHED_BATCH */
        case 5: /* SCHED_IDLE */
            return 0;
        default:
            return -EINVAL;
    }
}

int64_t real_sys_sched_get_priority_min(int policy){
    switch(policy){
        case 1: /* SCHED_FIFO */
        case 2: /* SCHED_RR */
            return 1;
        case 0: /* SCHED_OTHER */
        case 3: /* SCHED_BATCH */
        case 5: /* SCHED_IDLE */
            return 0;
        default:
            return -EINVAL;
    }
}

int64_t real_sys_memfd_create(const char *name,unsigned int flags){
    task_t *cur=task_current();
    char path[VFS_PATH_MAX];
    size_t l=0;
    int slot;
    int fd;
    uint32_t seq;
    if(flags&~(C3_MFD_CLOEXEC|C3_MFD_ALLOW_SEALING))return -EINVAL;
    seq=g_c3_memfd_seq++;
    if(seq==0)seq=g_c3_memfd_seq++;
    path[0]=0;
    c3_append_str(path,&l,sizeof(path),C3_MEMFD_PREFIX);
    if(name&&name[0])c3_append_str(path,&l,sizeof(path),name);
    else c3_append_str(path,&l,sizeof(path),"anon");
    c3_append_ch(path,&l,sizeof(path),'-');
    c3_append_u32(path,&l,sizeof(path),seq);
    kvfs_write(path,"");
    c3_memfd_track_set(path,(flags&C3_MFD_ALLOW_SEALING)?0u:C3_F_SEAL_SEAL);
    slot=c3_vfs_open_slot_alloc(path);
    if(slot<0)return -EMFILE;
    fd=c3_fd_alloc_for_task(cur,FDKIND_VFSFILE,slot,
                            FDFL_READABLE|FDFL_WRITABLE|
                            ((flags&C3_MFD_CLOEXEC)?FDFL_CLOEXEC:0));
    return fd<0?-EMFILE:(int64_t)fd;
}
int64_t real_sys_mlock(uint64_t a,size_t l){(void)a;(void)l;return 0;}
int64_t real_sys_munlock(uint64_t a,size_t l){(void)a;(void)l;return 0;}

/* Rewire syscall table to real implementations */
/* Wrapper: adapt real_sys_* to the 6-arg syscall_fn_t signature */
#define WRAP0(name,fn) static int64_t name(uint64_t a,uint64_t b,uint64_t c,uint64_t d,uint64_t e,uint64_t f){(void)a;(void)b;(void)c;(void)d;(void)e;(void)f;return fn();}
#define WRAP1(name,fn,T0) static int64_t name(uint64_t a,uint64_t b,uint64_t c,uint64_t d,uint64_t e,uint64_t f){(void)b;(void)c;(void)d;(void)e;(void)f;return fn((T0)a);}
#define WRAP2(name,fn,T0,T1) static int64_t name(uint64_t a,uint64_t b,uint64_t c,uint64_t d,uint64_t e,uint64_t f){(void)c;(void)d;(void)e;(void)f;return fn((T0)a,(T1)b);}
#define WRAP3(name,fn,T0,T1,T2) static int64_t name(uint64_t a,uint64_t b,uint64_t c,uint64_t d,uint64_t e,uint64_t f){(void)d;(void)e;(void)f;return fn((T0)a,(T1)b,(T2)c);}
#define WRAP4(name,fn,T0,T1,T2,T3) static int64_t name(uint64_t a,uint64_t b,uint64_t c,uint64_t d,uint64_t e,uint64_t f){(void)e;(void)f;return fn((T0)a,(T1)b,(T2)c,(T3)d);}
#define WRAP5(name,fn,T0,T1,T2,T3,T4) static int64_t name(uint64_t a,uint64_t b,uint64_t c,uint64_t d,uint64_t e,uint64_t f){(void)f;return fn((T0)a,(T1)b,(T2)c,(T3)d,(T4)e);}
#define WRAP6(name,fn,T0,T1,T2,T3,T4,T5) static int64_t name(uint64_t a,uint64_t b,uint64_t c,uint64_t d,uint64_t e,uint64_t f){return fn((T0)a,(T1)b,(T2)c,(T3)d,(T4)e,(T5)f);}

/* File ops */
WRAP3(w_read,real_sys_read,int,void*,size_t)
WRAP3(w_write,real_sys_write,int,const void*,size_t)
WRAP4(w_pread64,real_sys_pread64,int,void*,size_t,int64_t)
WRAP4(w_pwrite64,real_sys_pwrite64,int,const void*,size_t,int64_t)
WRAP3(w_open,real_sys_open,const char*,int,int)
WRAP2(w_creat,real_sys_creat,const char*,int)
WRAP1(w_close,real_sys_close,int)
WRAP2(w_stat,real_sys_stat,const char*,kstat_t*)
WRAP2(w_fstat,real_sys_fstat,int,kstat_t*)
WRAP2(w_lstat,real_sys_lstat,const char*,kstat_t*)
WRAP3(w_lseek,real_sys_lseek,int,int64_t,int)
WRAP3(w_readv,real_sys_readv,int,const iovec_t*,int)
WRAP3(w_writev,real_sys_writev,int,const iovec_t*,int)
WRAP6(w_mmap,real_sys_mmap,uint64_t,uint64_t,int,int,int,uint64_t)
WRAP3(w_mprotect,real_sys_mprotect,uint64_t,uint64_t,int)
WRAP2(w_munmap,real_sys_munmap,uint64_t,uint64_t)
WRAP1(w_brk,real_sys_brk,uint64_t)
WRAP5(w_mremap,real_sys_mremap,uint64_t,uint64_t,uint64_t,int,uint64_t)
WRAP3(w_msync,real_sys_msync,uint64_t,uint64_t,int)
WRAP3(w_ioctl,real_sys_ioctl,int,uint64_t,uint64_t)
WRAP2(w_access,real_sys_access,const char*,int)
WRAP1(w_pipe,real_sys_pipe,int*)
WRAP1(w_dup,real_sys_dup,int)
WRAP2(w_dup2,real_sys_dup2,int,int)
WRAP0(w_fork,real_sys_fork)
WRAP3(w_execve,real_sys_execve,const char*,char*const*,char*const*)
WRAP1(w_exit,real_sys_exit,int)
WRAP4(w_wait4,real_sys_wait4,int,int*,int,void*)
WRAP5(w_waitid,real_sys_waitid,int,int,void*,int,void*)
WRAP2(w_kill,real_sys_kill,int,int)
WRAP1(w_uname,real_sys_uname,utsname_t*)
WRAP3(w_fcntl,real_sys_fcntl,int,int,uint64_t)
WRAP2(w_flock,real_sys_flock,int,int)
WRAP1(w_fsync,real_sys_fsync,int)
WRAP1(w_fdatasync,real_sys_fdatasync,int)
WRAP3(w_getdents64,real_sys_getdents64,int,void*,size_t)
WRAP2(w_getcwd,real_sys_getcwd,char*,size_t)
WRAP1(w_chdir,real_sys_chdir,const char*)
WRAP2(w_mkdir,real_sys_mkdir,const char*,int)
WRAP3(w_mkdirat,real_sys_mkdirat,int,const char*,int)
WRAP1(w_unlink,real_sys_unlink,const char*)
WRAP2(w_rename,real_sys_rename,const char*,const char*)
WRAP2(w_link,real_sys_link,const char*,const char*)
WRAP3(w_readlink,real_sys_readlink,const char*,char*,size_t)
WRAP2(w_symlink,real_sys_symlink,const char*,const char*)
WRAP2(w_chmod,real_sys_chmod,const char*,int)
WRAP2(w_fchmod,real_sys_fchmod,int,int)
WRAP3(w_chown,real_sys_chown,const char*,int,int)
WRAP3(w_fchown,real_sys_fchown,int,int,int)
WRAP3(w_lchown,real_sys_lchown,const char*,int,int)
WRAP0(w_getpid,real_sys_getpid)
WRAP0(w_getppid,real_sys_getppid)
WRAP0(w_gettid,real_sys_gettid)
WRAP0(w_getuid,real_sys_getuid)
WRAP0(w_getgid,real_sys_getgid)
WRAP0(w_geteuid,real_sys_geteuid)
WRAP0(w_getegid,real_sys_getegid)
WRAP1(w_setuid,real_sys_setuid,int)
WRAP1(w_setgid,real_sys_setgid,int)
WRAP2(w_setpgid,real_sys_setpgid,int,int)
WRAP1(w_getpgid,real_sys_getpgid,int)
WRAP0(w_setsid,real_sys_setsid)
WRAP1(w_getsid,real_sys_getsid,int)
WRAP4(w_rt_sigaction,real_sys_rt_sigaction,int,const void*,void*,size_t)
WRAP4(w_rt_sigprocmask,real_sys_rt_sigprocmask,int,const void*,void*,size_t)
WRAP0(w_rt_sigreturn,real_sys_rt_sigreturn)
WRAP2(w_sigaltstack,real_sys_sigaltstack,const void*,void*)
WRAP2(w_clock_gettime,real_sys_clock_gettime,int,timespec_t*)
WRAP2(w_clock_getres,real_sys_clock_getres,int,timespec_t*)
WRAP2(w_gettimeofday,real_sys_gettimeofday,timeval_t*,timezone_t*)
WRAP1(w_time,real_sys_time,int64_t*)
WRAP2(w_nanosleep,real_sys_nanosleep,const timespec_t*,timespec_t*)
WRAP1(w_alarm,real_sys_alarm,uint32_t)
WRAP1(w_sysinfo,real_sys_sysinfo,sysinfo_t*)
WRAP3(w_getrandom,real_sys_getrandom,void*,size_t,unsigned int)
WRAP2(w_getrlimit,real_sys_getrlimit,int,void*)
WRAP4(w_prlimit64,real_sys_prlimit64,int,int,const void*,void*)
WRAP2(w_statfs,real_sys_statfs,const char*,void*)
WRAP2(w_fstatfs,real_sys_fstatfs,int,void*)
WRAP3(w_readahead,real_sys_readahead,int,uint64_t,size_t)
WRAP5(w_prctl,real_sys_prctl,int,uint64_t,uint64_t,uint64_t,uint64_t)
WRAP2(w_arch_prctl,real_sys_arch_prctl,int,uint64_t)
WRAP1(w_set_tid_address,real_sys_set_tid_address,uint64_t)
WRAP2(w_set_robust_list,real_sys_set_robust_list,uint64_t,size_t)
WRAP6(w_futex,real_sys_futex,uint32_t*,int,uint32_t,const timespec_t*,uint32_t*,uint32_t)
WRAP3(w_socket,real_sys_socket,int,int,int)
WRAP3(w_bind,real_sys_bind,int,const void*,uint32_t)
WRAP2(w_listen,real_sys_listen,int,int)
WRAP3(w_accept,real_sys_accept,int,void*,uint32_t*)
WRAP3(w_connect,real_sys_connect,int,const void*,uint32_t)
WRAP6(w_sendto,real_sys_sendto,int,const void*,size_t,int,const void*,uint32_t)
WRAP6(w_recvfrom,real_sys_recvfrom,int,void*,size_t,int,void*,uint32_t*)
WRAP3(w_sendmsg,real_sys_sendmsg,int,const void*,int)
WRAP3(w_recvmsg,real_sys_recvmsg,int,void*,int)
WRAP4(w_sendmmsg,real_sys_sendmmsg,int,void*,unsigned int,unsigned int)
WRAP5(w_recvmmsg,real_sys_recvmmsg,int,void*,unsigned int,unsigned int,const timespec_t*)
WRAP5(w_setsockopt,real_sys_setsockopt,int,int,int,const void*,uint32_t)
WRAP5(w_getsockopt,real_sys_getsockopt,int,int,int,void*,uint32_t*)
WRAP3(w_getsockname,real_sys_getsockname,int,void*,uint32_t*)
WRAP3(w_getpeername,real_sys_getpeername,int,void*,uint32_t*)
WRAP1(w_epoll_create1,real_sys_epoll_create1,int)
WRAP4(w_epoll_ctl,real_sys_epoll_ctl,int,int,int,void*)
WRAP4(w_epoll_wait,real_sys_epoll_wait,int,void*,int,int)
WRAP3(w_poll,real_sys_poll,void*,uint64_t,int)
WRAP6(w_pselect6,real_sys_pselect6,int,void*,void*,void*,const timespec_t*,const void*)
WRAP5(w_ppoll,real_sys_ppoll,void*,uint64_t,const timespec_t*,const void*,size_t)
WRAP2(w_eventfd2,real_sys_eventfd2,unsigned int,int)
WRAP2(w_timerfd_create,real_sys_timerfd_create,int,int)
WRAP4(w_timerfd_settime,real_sys_timerfd_settime,int,int,const void*,void*)
WRAP2(w_timerfd_gettime,real_sys_timerfd_gettime,int,void*)
WRAP4(w_signalfd4,real_sys_signalfd4,int,const void*,size_t,int)
WRAP4(w_openat,real_sys_openat,int,const char*,int,int)
WRAP1(w_exit_group,real_sys_exit_group,int)
WRAP5(w_clone,real_sys_clone,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t)
WRAP0(w_vfork,real_sys_vfork)
WRAP0(w_sched_yield,real_sys_sched_yield)
WRAP1(w_umask,real_sys_umask,int)
WRAP2(w_getrusage,real_sys_getrusage,int,void*)
WRAP2(w_getpriority,real_sys_getpriority,int,int)
WRAP3(w_setpriority,real_sys_setpriority,int,int,int)
WRAP2(w_capget,real_sys_capget,void*,void*)
WRAP2(w_capset,real_sys_capset,const void*,const void*)
WRAP3(w_shmget,real_sys_shmget,int,size_t,int)
WRAP3(w_shmat,real_sys_shmat,int,uint64_t,int)
WRAP3(w_shmctl,real_sys_shmctl,int,int,void*)
WRAP1(w_shmdt,real_sys_shmdt,uint64_t)
WRAP2(w_memfd_create,real_sys_memfd_create,const char*,unsigned int)
WRAP2(w_pipe2,real_sys_pipe2,int*,int)
WRAP3(w_dup3,real_sys_dup3,int,int,int)
WRAP4(w_faccessat,real_sys_faccessat,int,const char*,int,int)
WRAP4(w_newfstatat,real_sys_newfstatat,int,const char*,kstat_t*,int)
WRAP4(w_readlinkat,real_sys_readlinkat,int,const char*,char*,size_t)
WRAP3(w_symlinkat,real_sys_symlinkat,const char*,int,const char*)
WRAP3(w_unlinkat,real_sys_unlinkat,int,const char*,int)
WRAP4(w_renameat,real_sys_renameat,int,const char*,int,const char*)
WRAP5(w_linkat,real_sys_linkat,int,const char*,int,const char*,int)
WRAP5(w_fchownat,real_sys_fchownat,int,const char*,int,int,int)
WRAP4(w_fchmodat,real_sys_fchmodat,int,const char*,int,int)
WRAP5(w_renameat2,real_sys_renameat2,int,const char*,int,const char*,unsigned int)
WRAP3(w_tgkill,real_sys_tgkill,int,int,int)
WRAP2(w_tkill,real_sys_tkill,int,int)
WRAP2(w_ftruncate,real_sys_ftruncate,int,int64_t)
WRAP2(w_truncate,real_sys_truncate,const char*,int64_t)
WRAP4(w_fallocate,real_sys_fallocate,int,int,int64_t,int64_t)
WRAP4(w_accept4,real_sys_accept4,int,void*,uint32_t*,int)
WRAP2(w_shutdown,real_sys_shutdown,int,int)
WRAP2(w_mlock,real_sys_mlock,uint64_t,size_t)
WRAP2(w_munlock,real_sys_munlock,uint64_t,size_t)
WRAP3(w_madvise,real_sys_madvise,uint64_t,uint64_t,int)
WRAP3(w_sched_setaffinity,real_sys_sched_setaffinity,int,size_t,const void*)
WRAP3(w_sched_getaffinity,real_sys_sched_getaffinity,int,size_t,void*)
WRAP1(w_sched_get_priority_max,real_sys_sched_get_priority_max,int)
WRAP1(w_sched_get_priority_min,real_sys_sched_get_priority_min,int)
WRAP1(w_syncfs,real_sys_syncfs,int)
WRAP3(w_get_robust_list,real_sys_get_robust_list,int,uint64_t*,size_t*)
WRAP3(w_seccomp,real_sys_seccomp,unsigned int,unsigned int,void*)
WRAP1(w_unshare,real_sys_unshare,uint64_t)
WRAP2(w_setns,real_sys_setns,int,int)
WRAP1(w_userfaultfd,real_sys_userfaultfd,int)
WRAP0(w_inotify_init,real_sys_inotify_init)
WRAP1(w_inotify_init1,real_sys_inotify_init1,int)
WRAP3(w_inotify_add_watch,real_sys_inotify_add_watch,int,const char*,uint32_t)
WRAP2(w_inotify_rm_watch,real_sys_inotify_rm_watch,int,int)
WRAP6(w_process_vm_readv,real_sys_process_vm_readv,int,const iovec_t*,unsigned long,const iovec_t*,unsigned long,unsigned long)
WRAP6(w_process_vm_writev,real_sys_process_vm_writev,int,const iovec_t*,unsigned long,const iovec_t*,unsigned long,unsigned long)

static int64_t w_socketpair(uint64_t a,uint64_t b,uint64_t c,uint64_t d,uint64_t e,uint64_t f){
    (void)e;(void)f;
    return real_sys_socketpair((int)a,(int)b,(int)c,(int*)(uintptr_t)d);
}

void compat3_rewire_syscalls(void){
    /* Overwrite compat.c stubs with real implementations */
    g_syscall_table[0]=w_read;
    g_syscall_table[1]=w_write;
    g_syscall_table[2]=w_open;
    g_syscall_table[3]=w_close;
    g_syscall_table[4]=w_stat;
    g_syscall_table[5]=w_fstat;
    g_syscall_table[6]=w_lstat;
    g_syscall_table[7]=w_poll;
    g_syscall_table[8]=w_lseek;
    g_syscall_table[9]=w_mmap;
    g_syscall_table[10]=w_mprotect;
    g_syscall_table[11]=w_munmap;
    g_syscall_table[12]=w_brk;
    g_syscall_table[13]=w_rt_sigaction;
    g_syscall_table[14]=w_rt_sigprocmask;
    g_syscall_table[15]=w_rt_sigreturn;
    g_syscall_table[16]=w_ioctl;
    g_syscall_table[17]=w_pread64;
    g_syscall_table[18]=w_pwrite64;
    g_syscall_table[19]=w_readv;
    g_syscall_table[20]=w_writev;
    g_syscall_table[21]=w_access;
    g_syscall_table[22]=w_pipe;
    g_syscall_table[24]=w_sched_yield;
    g_syscall_table[25]=w_mremap;
    g_syscall_table[26]=w_msync;
    g_syscall_table[29]=w_shmget;
    g_syscall_table[30]=w_shmat;
    g_syscall_table[31]=w_shmctl;
    g_syscall_table[28]=w_madvise;
    g_syscall_table[32]=w_dup;
    g_syscall_table[33]=w_dup2;
    g_syscall_table[35]=w_nanosleep;
    g_syscall_table[37]=w_alarm;
    g_syscall_table[39]=w_getpid;
    g_syscall_table[41]=w_socket;
    g_syscall_table[42]=w_connect;
    g_syscall_table[43]=w_accept;
    g_syscall_table[44]=w_sendto;
    g_syscall_table[45]=w_recvfrom;
    g_syscall_table[46]=w_sendmsg;
    g_syscall_table[47]=w_recvmsg;
    g_syscall_table[48]=w_shutdown;
    g_syscall_table[49]=w_bind;
    g_syscall_table[50]=w_listen;
    g_syscall_table[51]=w_getsockname;
    g_syscall_table[52]=w_getpeername;
    g_syscall_table[53]=w_socketpair;
    g_syscall_table[54]=w_setsockopt;
    g_syscall_table[55]=w_getsockopt;
    g_syscall_table[56]=w_clone;
    g_syscall_table[57]=w_fork;
    g_syscall_table[58]=w_vfork;
    g_syscall_table[59]=w_execve;
    g_syscall_table[60]=w_exit;
    g_syscall_table[61]=w_wait4;
    g_syscall_table[62]=w_kill;
    g_syscall_table[63]=w_uname;
    g_syscall_table[67]=w_shmdt;
    g_syscall_table[72]=w_fcntl;
    g_syscall_table[73]=w_flock;
    g_syscall_table[74]=w_fsync;
    g_syscall_table[75]=w_fdatasync;
    g_syscall_table[76]=w_truncate;
    g_syscall_table[77]=w_ftruncate;
    g_syscall_table[79]=w_getcwd;
    g_syscall_table[80]=w_chdir;
    g_syscall_table[83]=w_mkdir;
    g_syscall_table[85]=w_creat;
    g_syscall_table[86]=w_link;
    g_syscall_table[87]=w_unlink;
    g_syscall_table[88]=w_symlink;
    g_syscall_table[82]=w_rename;
    g_syscall_table[89]=w_readlink;
    g_syscall_table[90]=w_chmod;
    g_syscall_table[91]=w_fchmod;
    g_syscall_table[92]=w_chown;
    g_syscall_table[93]=w_fchown;
    g_syscall_table[94]=w_lchown;
    g_syscall_table[95]=w_umask;
    g_syscall_table[96]=w_gettimeofday;
    g_syscall_table[97]=w_getrlimit;
    g_syscall_table[98]=w_getrusage;
    g_syscall_table[99]=w_sysinfo;
    g_syscall_table[102]=w_getuid;
    g_syscall_table[104]=w_getgid;
    g_syscall_table[105]=w_setuid;
    g_syscall_table[106]=w_setgid;
    g_syscall_table[107]=w_geteuid;
    g_syscall_table[108]=w_getegid;
    g_syscall_table[109]=w_setpgid;
    g_syscall_table[110]=w_getppid;
    g_syscall_table[112]=w_setsid;
    g_syscall_table[121]=w_getpgid;
    g_syscall_table[124]=w_getsid;
    g_syscall_table[125]=w_capget;
    g_syscall_table[126]=w_capset;
    g_syscall_table[131]=w_sigaltstack;
    g_syscall_table[137]=w_statfs;
    g_syscall_table[138]=w_fstatfs;
    g_syscall_table[140]=w_getpriority;
    g_syscall_table[141]=w_setpriority;
    g_syscall_table[146]=w_sched_get_priority_max;
    g_syscall_table[147]=w_sched_get_priority_min;
    g_syscall_table[149]=w_mlock;
    g_syscall_table[150]=w_munlock;
    g_syscall_table[158]=w_arch_prctl;
    g_syscall_table[186]=w_gettid;
    g_syscall_table[187]=w_readahead;
    g_syscall_table[200]=w_tkill;
    g_syscall_table[201]=w_time;
    g_syscall_table[202]=w_futex;
    g_syscall_table[203]=w_sched_setaffinity;
    g_syscall_table[204]=w_sched_getaffinity;
    g_syscall_table[217]=w_getdents64;
    g_syscall_table[218]=w_set_tid_address;
    g_syscall_table[228]=w_clock_gettime;
    g_syscall_table[229]=w_clock_getres;
    g_syscall_table[231]=w_exit_group;
    g_syscall_table[232]=w_epoll_wait;
    g_syscall_table[233]=w_epoll_ctl;
    g_syscall_table[234]=w_tgkill;
    g_syscall_table[247]=w_waitid;
    g_syscall_table[253]=w_inotify_init;
    g_syscall_table[254]=w_inotify_add_watch;
    g_syscall_table[255]=w_inotify_rm_watch;
    g_syscall_table[257]=w_openat;
    g_syscall_table[258]=w_mkdirat;
    g_syscall_table[260]=w_fchownat;
    g_syscall_table[262]=w_newfstatat;
    g_syscall_table[263]=w_unlinkat;
    g_syscall_table[264]=w_renameat;
    g_syscall_table[265]=w_linkat;
    g_syscall_table[266]=w_symlinkat;
    g_syscall_table[267]=w_readlinkat;
    g_syscall_table[268]=w_fchmodat;
    g_syscall_table[269]=w_faccessat;
    g_syscall_table[270]=w_pselect6;
    g_syscall_table[271]=w_ppoll;
    g_syscall_table[272]=w_unshare;
    g_syscall_table[288]=w_accept4;
    g_syscall_table[289]=w_signalfd4;
    g_syscall_table[290]=w_eventfd2;
    g_syscall_table[291]=w_epoll_create1;
    g_syscall_table[292]=w_dup3;
    g_syscall_table[293]=w_pipe2;
    g_syscall_table[294]=w_inotify_init1;
    g_syscall_table[283]=w_timerfd_create;
    g_syscall_table[285]=w_fallocate;
    g_syscall_table[286]=w_timerfd_settime;
    g_syscall_table[287]=w_timerfd_gettime;
    g_syscall_table[299]=w_recvmmsg;
    g_syscall_table[302]=w_prlimit64;
    g_syscall_table[303]=w_prctl; /* actually name_to_handle_at, reuse */
    g_syscall_table[306]=w_syncfs;
    g_syscall_table[307]=w_sendmmsg;
    g_syscall_table[308]=w_setns;
    g_syscall_table[310]=w_process_vm_readv;
    g_syscall_table[311]=w_process_vm_writev;
    g_syscall_table[316]=w_renameat2;
    g_syscall_table[317]=w_seccomp;
    g_syscall_table[318]=w_getrandom;
    g_syscall_table[319]=w_memfd_create;
    g_syscall_table[323]=w_userfaultfd;
    g_syscall_table[273]=w_set_robust_list;
    g_syscall_table[274]=w_get_robust_list;
    g_syscall_table[157]=w_prctl;
}

/* Shell commands */
static void cmd3_syscalls_real(const char *a,char *o,int mx){
    size_t l=0;int i,cnt=0;(void)a;o[0]=0;
    for(i=0;i<SYSCALL_MAX;++i)if(g_syscall_table[i])cnt++;
    c3_append_str(o,&l,(size_t)mx,"Syscalls wired: ");c3_append_u32(o,&l,(size_t)mx,(uint32_t)cnt);
    c3_append_str(o,&l,(size_t)mx,"/");c3_append_u32(o,&l,(size_t)mx,SYSCALL_MAX);
    c3_append_str(o,&l,(size_t)mx," (real implementations via compat3)\n");
    c3_append_str(o,&l,(size_t)mx,"File: open/read/write/close/lseek/dup/pipe/ioctl/fcntl/stat/access\n");
    c3_append_str(o,&l,(size_t)mx,"Task: fork/vfork/clone/execve/exit/wait4/getpid/kill\n");
    c3_append_str(o,&l,(size_t)mx,"Signal: rt_sigaction/rt_sigprocmask/rt_sigreturn/sigaltstack/tgkill\n");
    c3_append_str(o,&l,(size_t)mx,"Mem: mmap/munmap/mprotect/brk/mremap/msync/madvise (+ COW fork/MAP_PRIVATE file-backed)\n");
    c3_append_str(o,&l,(size_t)mx,"Net: socket/bind/listen/accept/connect/send/recv/sendmmsg/recvmmsg/sockname/sockopt\n");
    c3_append_str(o,&l,(size_t)mx,"Time: clock_gettime/gettimeofday/nanosleep/alarm\n");
    c3_append_str(o,&l,(size_t)mx,"ABI: uname(Linux 6.1.0)/sysinfo/getrandom/prctl/arch_prctl\n");
    c3_append_str(o,&l,(size_t)mx,"IPC: epoll/poll/eventfd/timerfd/signalfd/inotify/futex(bitset+requeue queues)/shmget\n");
}

static void cmd3_mmap_regions(const char *a,char *o,int mx){
    size_t l=0;int i;task_t *cur=task_current();(void)a;o[0]=0;
    c3_append_str(o,&l,(size_t)mx,"START-END         SIZE      PERM OFFS              BACKING\n");
    for(i=0;i<MMAP_REGION_MAX;++i){if(!g_mmap_table[i].used)continue;
        char perms[5];
        if(cur&&g_mmap_table[i].as!=cur->addr_space)continue;
        c3_mmap_perm_text(g_mmap_table[i].prot,g_mmap_table[i].flags,perms);
        c3_append_hex64(o,&l,(size_t)mx,g_mmap_table[i].virt);c3_append_ch(o,&l,(size_t)mx,'-');
        c3_append_hex64(o,&l,(size_t)mx,g_mmap_table[i].virt+g_mmap_table[i].size);c3_append_str(o,&l,(size_t)mx," ");
        c3_append_u64(o,&l,(size_t)mx,g_mmap_table[i].size);c3_append_str(o,&l,(size_t)mx,"  ");
        c3_append_str(o,&l,(size_t)mx,perms);c3_append_str(o,&l,(size_t)mx," ");
        c3_append_hex64(o,&l,(size_t)mx,g_mmap_table[i].offset);c3_append_str(o,&l,(size_t)mx,"  ");
        if(g_mmap_table[i].backing_path[0])c3_append_str(o,&l,(size_t)mx,g_mmap_table[i].backing_path);
        else if(g_mmap_table[i].fd>=0)c3_append_str(o,&l,(size_t)mx,"[fd]");
        else c3_append_str(o,&l,(size_t)mx,"[anon]");
        c3_append_ch(o,&l,(size_t)mx,'\n');}
    if(l<2)c3_append_str(o,&l,(size_t)mx,"(no mmap regions)\n");
}

static void cmd3_procfs(const char *a,char *o,int mx){
    if(!a||!*a){
        size_t l=0;o[0]=0;
        c3_append_str(o,&l,(size_t)mx,"usage: procfs <path>\n  e.g. procfs /proc/self/status\n");
        c3_append_str(o,&l,(size_t)mx,"  /proc/self/status  /proc/self/maps  /proc/self/cmdline\n");
        c3_append_str(o,&l,(size_t)mx,"  /proc/cpuinfo  /proc/meminfo  /proc/version\n");
        c3_append_str(o,&l,(size_t)mx,"  /proc/mounts  /proc/filesystems  /proc/loadavg\n");
        return;
    }
    proc_generate(a,o,mx);
}

bool compat3_has_dynamic_relocator(void){return true;}

int compat3_dynlink_stats(char *buf,int mx){
    size_t l=0;
    uint64_t since_boot_us=0;
    uint64_t timeout_since_boot_us=0;
    uint32_t total_recent;
    int i,shown=0;
    buf[0]=0;
    c3_append_str(buf,&l,(size_t)mx,"dynamic relocator: enabled\n");
    c3_append_str(buf,&l,(size_t)mx,"shared objects registered: ");
    c3_append_u32(buf,&l,(size_t)mx,g_c3_dyn_objects);
    c3_append_ch(buf,&l,(size_t)mx,'\n');
    c3_append_str(buf,&l,(size_t)mx,"next TLS module id: ");
    c3_append_u32(buf,&l,(size_t)mx,g_c3_tls_next_module);
    c3_append_ch(buf,&l,(size_t)mx,'\n');
    c3_append_str(buf,&l,(size_t)mx,"dynamic images mapped: ");
    c3_append_u32(buf,&l,(size_t)mx,g_c3_dyn_images_mapped);
    c3_append_ch(buf,&l,(size_t)mx,'\n');
    c3_append_str(buf,&l,(size_t)mx,"DT_NEEDED loaded: ");
    c3_append_u32(buf,&l,(size_t)mx,g_c3_dyn_needed_loaded);
    c3_append_str(buf,&l,(size_t)mx,"  missing: ");
    c3_append_u32(buf,&l,(size_t)mx,g_c3_dyn_needed_missing);
    c3_append_ch(buf,&l,(size_t)mx,'\n');
    c3_append_str(buf,&l,(size_t)mx,"relocs applied: ");
    c3_append_u32(buf,&l,(size_t)mx,g_c3_dyn_reloc_applied);
    c3_append_str(buf,&l,(size_t)mx,"  failed: ");
    c3_append_u32(buf,&l,(size_t)mx,g_c3_dyn_reloc_failed);
    c3_append_str(buf,&l,(size_t)mx,"  unsupported: ");
    c3_append_u32(buf,&l,(size_t)mx,g_c3_dyn_reloc_unsupported);
    c3_append_ch(buf,&l,(size_t)mx,'\n');
    c3_append_str(buf,&l,(size_t)mx,"loader timeout budget: ");
    c3_append_u32(buf,&l,(size_t)mx,g_c3_dyn_timeout_ms);
    c3_append_str(buf,&l,(size_t)mx," ms  hits: ");
    c3_append_u32(buf,&l,(size_t)mx,g_c3_dyn_timeout_hits);
    c3_append_ch(buf,&l,(size_t)mx,'\n');
    if(g_boot_tsc&&g_c3_dyn_last_timeout_tsc>=g_boot_tsc){
        timeout_since_boot_us=c3_tsc_to_us(g_c3_dyn_last_timeout_tsc-g_boot_tsc);
    }
    if(g_c3_dyn_timeout_hits){
        c3_append_str(buf,&l,(size_t)mx,"last timeout: ");
        c3_append_str(buf,&l,(size_t)mx,g_c3_dyn_last_timeout_stage[0]?g_c3_dyn_last_timeout_stage:"(unknown)");
        c3_append_str(buf,&l,(size_t)mx," obj=");
        c3_append_str(buf,&l,(size_t)mx,g_c3_dyn_last_timeout_obj[0]?g_c3_dyn_last_timeout_obj:"(unknown)");
        c3_append_str(buf,&l,(size_t)mx," at ");
        c3_append_u64(buf,&l,(size_t)mx,timeout_since_boot_us);
        c3_append_str(buf,&l,(size_t)mx," us since boot\n");
    }
    c3_append_str(buf,&l,(size_t)mx,"dyn cycles us: map=");
    c3_append_u64(buf,&l,(size_t)mx,c3_tsc_to_us(g_c3_dyn_cycle_map));
    c3_append_str(buf,&l,(size_t)mx," needed=");
    c3_append_u64(buf,&l,(size_t)mx,c3_tsc_to_us(g_c3_dyn_cycle_needed));
    c3_append_str(buf,&l,(size_t)mx," reloc=");
    c3_append_u64(buf,&l,(size_t)mx,c3_tsc_to_us(g_c3_dyn_cycle_reloc));
    c3_append_ch(buf,&l,(size_t)mx,'\n');
    c3_append_str(buf,&l,(size_t)mx,"recent loader objects:\n");
    total_recent=g_c3_dyn_profile_next;
    if(total_recent>C3_DYN_PROFILE_MAX)total_recent=C3_DYN_PROFILE_MAX;
    for(i=0;i<(int)total_recent&&shown<8;++i){
        uint32_t idx=(g_c3_dyn_profile_next-1u-(uint32_t)i)%C3_DYN_PROFILE_MAX;
        c3_dyn_profile_t *p=&g_c3_dyn_profile[idx];
        if(!p->used)continue;
        c3_append_str(buf,&l,(size_t)mx,"  ");
        c3_append_str(buf,&l,(size_t)mx,p->name);
        c3_append_str(buf,&l,(size_t)mx," depth=");
        c3_append_u32(buf,&l,(size_t)mx,p->depth);
        c3_append_str(buf,&l,(size_t)mx," need=");
        c3_append_u32(buf,&l,(size_t)mx,p->needed_loaded);
        c3_append_ch(buf,&l,(size_t)mx,'/');
        c3_append_u32(buf,&l,(size_t)mx,p->needed_missing);
        c3_append_ch(buf,&l,(size_t)mx,'/');
        c3_append_u32(buf,&l,(size_t)mx,p->needed_total);
        c3_append_str(buf,&l,(size_t)mx," rel=");
        c3_append_u32(buf,&l,(size_t)mx,p->relocs_applied);
        c3_append_ch(buf,&l,(size_t)mx,'/');
        c3_append_u32(buf,&l,(size_t)mx,p->relocs_failed);
        c3_append_ch(buf,&l,(size_t)mx,'/');
        c3_append_u32(buf,&l,(size_t)mx,p->relocs_unsupported);
        c3_append_str(buf,&l,(size_t)mx," us=");
        c3_append_u64(buf,&l,(size_t)mx,c3_tsc_to_us(p->cycle_map));
        c3_append_ch(buf,&l,(size_t)mx,'/');
        c3_append_u64(buf,&l,(size_t)mx,c3_tsc_to_us(p->cycle_needed));
        c3_append_ch(buf,&l,(size_t)mx,'/');
        c3_append_u64(buf,&l,(size_t)mx,c3_tsc_to_us(p->cycle_reloc));
        if(p->flags&C3_DYN_PROFILE_FLAG_TIMEOUT)c3_append_str(buf,&l,(size_t)mx," timeout");
        c3_append_ch(buf,&l,(size_t)mx,'\n');
        ++shown;
    }
    if(!shown)c3_append_str(buf,&l,(size_t)mx,"  (none)\n");
    if(g_boot_tsc){
        since_boot_us=c3_tsc_to_us(c3_rdtsc()-g_boot_tsc);
        c3_append_str(buf,&l,(size_t)mx,"uptime(us): ");
        c3_append_u64(buf,&l,(size_t)mx,since_boot_us);
        c3_append_ch(buf,&l,(size_t)mx,'\n');
    }
    c3_append_str(buf,&l,(size_t)mx,"COW faults: ");
    c3_append_u32(buf,&l,(size_t)mx,g_c3_cow_faults);
    c3_append_str(buf,&l,(size_t)mx,"  copies: ");
    c3_append_u32(buf,&l,(size_t)mx,g_c3_cow_copies);
    c3_append_str(buf,&l,(size_t)mx,"  shared file maps: ");
    c3_append_u32(buf,&l,(size_t)mx,g_c3_cow_shared_maps);
    c3_append_str(buf,&l,(size_t)mx,"  fault-fail: ");
    c3_append_u32(buf,&l,(size_t)mx,g_c3_cow_fault_fail);
    c3_append_ch(buf,&l,(size_t)mx,'\n');
    return(int)l;
}

int compat3_dynobj_snapshot(compat3_dlobj_info_t *out,int max){
    int i,n=0;
    if(!out||max<=0)return 0;
    for(i=0;i<C3_DYNOBJ_MAX&&n<max;++i){
        if(!g_c3_dynobjs[i].used)continue;
        out[n].dlpi_addr=g_c3_dynobjs[i].load_bias;
        out[n].dlpi_phdr=g_c3_dynobjs[i].phdr_va;
        out[n].dlpi_phnum=g_c3_dynobjs[i].phnum;
        out[n].tls_module=g_c3_dynobjs[i].tls_module_id;
        out[n].tls_init=g_c3_dynobjs[i].tls_init_va;
        out[n].tls_filesz=g_c3_dynobjs[i].tls_filesz;
        out[n].tls_memsz=g_c3_dynobjs[i].tls_memsz;
        out[n].tls_align=g_c3_dynobjs[i].tls_align;
        c3_strlcpy(out[n].dlpi_name,g_c3_dynobjs[i].name,sizeof(out[n].dlpi_name));
        ++n;
    }
    return n;
}

static void cmd3_dynlink(const char *a,char *o,int mx){
    (void)a;
    compat3_dynlink_stats(o,mx);
}

static void cmd3_compat3_info(const char *a,char *o,int mx){
    size_t l=0;int i,sc=0,mm=0;task_t *cur=task_current();(void)a;o[0]=0;
    for(i=0;i<SYSCALL_MAX;++i)if(g_syscall_table[i])sc++;
    for(i=0;i<MMAP_REGION_MAX;++i)if(g_mmap_table[i].used&&(!cur||g_mmap_table[i].as==cur->addr_space))mm++;
    c3_append_str(o,&l,(size_t)mx,"=== RiduxOS Compat3 Real Syscalls ===\n");
    c3_append_str(o,&l,(size_t)mx,"Syscalls wired: ");c3_append_u32(o,&l,(size_t)mx,(uint32_t)sc);c3_append_ch(o,&l,(size_t)mx,'\n');
    c3_append_str(o,&l,(size_t)mx,"mmap regions: ");c3_append_u32(o,&l,(size_t)mx,(uint32_t)mm);c3_append_ch(o,&l,(size_t)mx,'\n');
    c3_append_str(o,&l,(size_t)mx,"uname: Linux 6.1.0-ridux x86_64\n");
    c3_append_str(o,&l,(size_t)mx,"PMM free: ");c3_append_u32(o,&l,(size_t)mx,pmm_free_count()*4);c3_append_str(o,&l,(size_t)mx," KB\n");
    c3_append_str(o,&l,(size_t)mx,"ELF64 mapper: PT_LOAD segment mapping (real paging)\n");
    c3_append_str(o,&l,(size_t)mx,"ELF64 dynamic relocs: REL/RELA/RELR + GNU_HASH + RELATIVE/GLOB_DAT/JUMP_SLOT/64/32/COPY/IRELATIVE + TLS(DTPMOD/DTPOFF/TPOFF)\n");
    c3_append_str(o,&l,(size_t)mx,"ELF64 symbol versioning: VERSYM + VERNEED + VERDEF resolver\n");
    c3_append_str(o,&l,(size_t)mx,"COW: fork private pages + MAP_PRIVATE file page sharing + write-fault split\n");
    c3_append_str(o,&l,(size_t)mx,"Threads ABI: clone TLS/TID + robust_list + futex timeout/requeue/bitset/EINTR\n");
    c3_append_str(o,&l,(size_t)mx,"Socket ABI+: sendmmsg/recvmmsg + getsockname/getpeername/getsockopt + accept4\n");
    c3_append_str(o,&l,(size_t)mx,"FS ABI+: fsync/fdatasync/flock/fallocate/syncfs + renameat2 + inotify\n");
    c3_append_str(o,&l,(size_t)mx,"Sandbox ABI: seccomp + no_new_privs + unshare/setns + capget/capset\n");
    c3_append_str(o,&l,(size_t)mx,"dyn images: ");c3_append_u32(o,&l,(size_t)mx,g_c3_dyn_images_mapped);
    c3_append_str(o,&l,(size_t)mx,"  relocs ok: ");c3_append_u32(o,&l,(size_t)mx,g_c3_dyn_reloc_applied);
    c3_append_str(o,&l,(size_t)mx,"  failed: ");c3_append_u32(o,&l,(size_t)mx,g_c3_dyn_reloc_failed);
    c3_append_ch(o,&l,(size_t)mx,'\n');
    c3_append_str(o,&l,(size_t)mx,"arch_prctl: FS/GS base via WRMSR\n");
    c3_append_str(o,&l,(size_t)mx,"procfs: /proc/self/{status,maps,cmdline,exe,fd}\n");
    c3_append_str(o,&l,(size_t)mx,"        /proc/{cpuinfo,meminfo,version,mounts,filesystems,loadavg}\n");
}

void compat3_register_shell_cmds(void){
    extern compat_shell_cmd_t g_compat_cmds[];
    extern int g_compat_cmd_count;
    #define REG3(n,h,fn) if(g_compat_cmd_count<COMPAT_SHELL_CMD_MAX){g_compat_cmds[g_compat_cmd_count].name=n;g_compat_cmds[g_compat_cmd_count].help=h;g_compat_cmds[g_compat_cmd_count].handler=fn;++g_compat_cmd_count;}
    REG3("realsys","Show real syscall info",cmd3_syscalls_real)
    REG3("mmaps","Show mmap regions",cmd3_mmap_regions)
    REG3("procfs","Read /proc files",cmd3_procfs)
    REG3("dynlink","Show dynamic linker/reloc stats",cmd3_dynlink)
    REG3("compat3","Compat3 real syscalls summary",cmd3_compat3_info)
    #undef REG3
}

/* Master init */
void compat3_init_all(void){
    int i;
    g_boot_tsc=c3_rdtsc();
    c3_memset(g_mmap_table,0,sizeof(g_mmap_table));
    c3_memset(g_proc_bufs,0,sizeof(g_proc_bufs));
    c3_memset(g_c3_path_modes,0,sizeof(g_c3_path_modes));
    c3_ensure_dir_abs("/tmp",01777);
    c3_ensure_dir_abs("/tmp/.X11-unix",01777);
    c3_ensure_dir_abs("/tmp/firefox-profile",0700);
    c3_ensure_dir_abs("/tmp/firefox-profile/cache2",0700);
    c3_ensure_dir_abs("/tmp/firefox-profile/startupCache",0700);
    c3_ensure_dir_abs("/tmp/firefox-profile/storage",0700);
    c3_ensure_dir_abs("/home",0755);
    c3_ensure_dir_abs("/home/.mozilla",0700);
    c3_ensure_dir_abs("/home/.mozilla/firefox",0700);
    kvfs_write("/home/ridux-firefox-test.html",
        "<!doctype html>\n"
        "<html><head><meta charset=\"utf-8\"><title>Ridux Firefox Render Test</title>\n"
        "<style>body{margin:40px;font:20px sans-serif;background:#f7fafc;color:#101828}"
        ".card{max-width:760px;border:1px solid #cbd5e1;border-radius:8px;padding:24px;background:#fff}"
        "h1{font-size:34px;margin:0 0 14px}p{line-height:1.45}.row{display:flex;gap:12px;align-items:center;flex-wrap:wrap}"
        "button,input{font:inherit;padding:8px 12px;border:1px solid #94a3b8;border-radius:6px;background:white}"
        "button{background:#0f766e;color:white;border-color:#0f766e}</style></head>\n"
        "<body><main class=\"card\"><h1>Ridux Firefox Render Test</h1>\n"
        "<p>HTML, CSS, text shaping, layout, input and click handling are alive.</p>\n"
        "<div class=\"row\"><input value=\"type here\"><button onclick=\"document.body.style.background='#ecfdf5'\">Click test</button></div>\n"
        "</main></body></html>\n");
    kvfs_write("/tmp/firefox-profile/prefs.js",
        "user_pref(\"app.normandy.enabled\", false);\n"
        "user_pref(\"app.shield.optoutstudies.enabled\", false);\n"
        "user_pref(\"app.update.auto\", false);\n"
        "user_pref(\"app.update.enabled\", false);\n"
        "user_pref(\"browser.aboutwelcome.enabled\", false);\n"
        "user_pref(\"browser.cache.disk.enable\", false);\n"
        "user_pref(\"browser.startup.blankWindow\", false);\n"
        "user_pref(\"browser.newtabpage.enabled\", false);\n"
        "user_pref(\"browser.newtabpage.activity-stream.enabled\", false);\n"
        "user_pref(\"browser.safebrowsing.downloads.enabled\", false);\n"
        "user_pref(\"browser.safebrowsing.malware.enabled\", false);\n"
        "user_pref(\"browser.safebrowsing.phishing.enabled\", false);\n"
        "user_pref(\"browser.search.suggest.enabled\", false);\n"
        "user_pref(\"browser.search.update\", false);\n"
        "user_pref(\"browser.sessionstore.resume_from_crash\", false);\n"
        "user_pref(\"browser.sessionstore.resume_session_once\", false);\n"
        "user_pref(\"browser.sessionstore.restore_on_demand\", false);\n"
        "user_pref(\"browser.sessionstore.restore_pinned_tabs_on_demand\", false);\n"
        "user_pref(\"browser.shell.checkDefaultBrowser\", false);\n"
        "user_pref(\"browser.startup.homepage\", \"http://example.com/\");\n"
        "user_pref(\"browser.startup.homepage_override.mstone\", \"ignore\");\n"
        "user_pref(\"browser.startup.page\", 1);\n"
        "user_pref(\"browser.tabs.remote.autostart\", false);\n"
        "user_pref(\"browser.tabs.remote.autostart.2\", false);\n"
        "user_pref(\"browser.tabs.remote.force-disable\", true);\n"
        "user_pref(\"browser.tabs.remote.force-enable\", false);\n"
        "user_pref(\"browser.tabs.remote.separateFileUriProcess\", false);\n"
        "user_pref(\"browser.tabs.remote.separatePrivilegedContentProcess\", false);\n"
        "user_pref(\"browser.tabs.remote.separatePrivilegedMozillaWebContentProcess\", false);\n"
        "user_pref(\"datareporting.healthreport.uploadEnabled\", false);\n"
        "user_pref(\"datareporting.policy.dataSubmissionEnabled\", false);\n"
        "user_pref(\"dom.ipc.keepProcessesAlive.web\", 0);\n"
        "user_pref(\"dom.ipc.forkserver.enable\", false);\n"
        "user_pref(\"dom.ipc.processPrelaunch.enabled\", false);\n"
        "user_pref(\"dom.ipc.processPrelaunch.fission.number\", 0);\n"
        "user_pref(\"dom.ipc.processCount\", 1);\n"
        "user_pref(\"dom.ipc.processCount.extension\", 1);\n"
        "user_pref(\"dom.ipc.processCount.file\", 1);\n"
        "user_pref(\"dom.ipc.processCount.privilegedabout\", 1);\n"
        "user_pref(\"dom.ipc.processCount.privilegedmozilla\", 1);\n"
        "user_pref(\"dom.ipc.processCount.web\", 1);\n"
        "user_pref(\"dom.ipc.processCount.webIsolated\", 1);\n"
        "user_pref(\"extensions.getAddons.cache.enabled\", false);\n"
        "user_pref(\"extensions.systemAddon.update.enabled\", false);\n"
        "user_pref(\"extensions.update.enabled\", false);\n"
        "user_pref(\"extensions.webextensions.remote\", false);\n"
        "user_pref(\"fission.autostart\", false);\n"
        "user_pref(\"fission.autostart.session\", false);\n"
        "user_pref(\"gfx.canvas.azure.accelerated\", false);\n"
        "user_pref(\"gfx.x11-egl.force-disabled\", true);\n"
        "user_pref(\"gfx.webrender.force-disabled\", true);\n"
        "user_pref(\"layers.acceleration.disabled\", true);\n"
        "user_pref(\"layers.gpu-process.enabled\", false);\n"
        "user_pref(\"layers.omtp.enabled\", false);\n"
        "user_pref(\"media.cubeb.sandbox\", false);\n"
        "user_pref(\"media.hardware-video-decoding.enabled\", false);\n"
        "user_pref(\"media.rdd-process.enabled\", false);\n"
        "user_pref(\"network.captive-portal-service.enabled\", false);\n"
        "user_pref(\"network.connectivity-service.enabled\", false);\n"
        "user_pref(\"network.dns.disablePrefetch\", true);\n"
        "user_pref(\"network.http.speculative-parallel-limit\", 0);\n"
        "user_pref(\"network.predictor.enabled\", false);\n"
        "user_pref(\"network.prefetch-next\", false);\n"
        "user_pref(\"network.proxy.type\", 0);\n"
        "user_pref(\"network.stricttransportsecurity.preloadlist\", false);\n"
        "user_pref(\"services.settings.server\", \"\");\n"
        "user_pref(\"dom.security.https_first\", false);\n"
        "user_pref(\"dom.security.https_only_mode\", false);\n"
        "user_pref(\"dom.security.https_only_mode_ever_enabled\", false);\n"
        "user_pref(\"dom.security.https_only_mode_pbm\", false);\n"
        "user_pref(\"security.sandbox.content.level\", 0);\n"
        "user_pref(\"security.sandbox.rdd.level\", 0);\n"
        "user_pref(\"startup.homepage_welcome_url\", \"\");\n"
        "user_pref(\"startup.homepage_welcome_url.additional\", \"\");\n"
        "user_pref(\"toolkit.startup.max_resumed_crashes\", -1);\n"
        "user_pref(\"toolkit.telemetry.enabled\", false);\n"
        "user_pref(\"toolkit.telemetry.unified\", false);\n"
        "user_pref(\"trailhead.firstrun.didSeeAboutWelcome\", true);\n"
        "user_pref(\"webgl.disabled\", true);\n");
    kvfs_write("/tmp/firefox-profile/user.js",
        "user_pref(\"browser.aboutwelcome.enabled\", false);\n"
        "user_pref(\"browser.startup.blankWindow\", false);\n"
        "user_pref(\"browser.newtabpage.enabled\", false);\n"
        "user_pref(\"browser.newtabpage.activity-stream.enabled\", false);\n"
        "user_pref(\"browser.sessionstore.resume_from_crash\", false);\n"
        "user_pref(\"browser.sessionstore.resume_session_once\", false);\n"
        "user_pref(\"browser.sessionstore.restore_on_demand\", false);\n"
        "user_pref(\"browser.sessionstore.restore_pinned_tabs_on_demand\", false);\n"
        "user_pref(\"browser.shell.checkDefaultBrowser\", false);\n"
        "user_pref(\"browser.startup.homepage\", \"http://example.com/\");\n"
        "user_pref(\"browser.startup.homepage_override.mstone\", \"ignore\");\n"
        "user_pref(\"browser.startup.page\", 1);\n"
        "user_pref(\"browser.tabs.remote.autostart\", false);\n"
        "user_pref(\"browser.tabs.remote.autostart.2\", false);\n"
        "user_pref(\"browser.tabs.remote.force-disable\", true);\n"
        "user_pref(\"browser.tabs.remote.force-enable\", false);\n"
        "user_pref(\"browser.tabs.remote.separateFileUriProcess\", false);\n"
        "user_pref(\"browser.tabs.remote.separatePrivilegedContentProcess\", false);\n"
        "user_pref(\"browser.tabs.remote.separatePrivilegedMozillaWebContentProcess\", false);\n"
        "user_pref(\"dom.ipc.keepProcessesAlive.web\", 0);\n"
        "user_pref(\"dom.ipc.forkserver.enable\", false);\n"
        "user_pref(\"dom.ipc.processPrelaunch.enabled\", false);\n"
        "user_pref(\"dom.ipc.processPrelaunch.fission.number\", 0);\n"
        "user_pref(\"dom.ipc.processCount\", 1);\n"
        "user_pref(\"dom.ipc.processCount.extension\", 1);\n"
        "user_pref(\"dom.ipc.processCount.file\", 1);\n"
        "user_pref(\"dom.ipc.processCount.privilegedabout\", 1);\n"
        "user_pref(\"dom.ipc.processCount.privilegedmozilla\", 1);\n"
        "user_pref(\"dom.ipc.processCount.web\", 1);\n"
        "user_pref(\"dom.ipc.processCount.webIsolated\", 1);\n"
        "user_pref(\"fission.autostart\", false);\n"
        "user_pref(\"fission.autostart.session\", false);\n"
        "user_pref(\"gfx.webrender.force-disabled\", true);\n"
        "user_pref(\"layers.acceleration.disabled\", true);\n"
        "user_pref(\"network.captive-portal-service.enabled\", false);\n"
        "user_pref(\"network.connectivity-service.enabled\", false);\n"
        "user_pref(\"network.stricttransportsecurity.preloadlist\", false);\n"
        "user_pref(\"dom.security.https_first\", false);\n"
        "user_pref(\"dom.security.https_only_mode\", false);\n"
        "user_pref(\"dom.security.https_only_mode_ever_enabled\", false);\n"
        "user_pref(\"dom.security.https_only_mode_pbm\", false);\n"
        "user_pref(\"security.sandbox.content.level\", 0);\n"
        "user_pref(\"toolkit.telemetry.enabled\", false);\n"
        "user_pref(\"webgl.disabled\", true);\n");
    kvfs_write("/home/.mozilla/firefox/profiles.ini",
        "[General]\n"
        "StartWithLastProfile=1\n"
        "Version=2\n"
        "\n"
        "[Profile0]\n"
        "Name=ridux\n"
        "IsRelative=0\n"
        "Path=/tmp/firefox-profile\n"
        "Default=1\n");
    kvfs_write("/home/.mozilla/firefox/installs.ini",
        "[Ridux]\n"
        "Default=/tmp/firefox-profile\n"
        "Locked=1\n");
    c3_ensure_dir_abs("/tmp/chromium-profile",0700);
    c3_ensure_dir_abs("/tmp/chromium-profile/Default",0700);
    c3_ensure_dir_abs("/tmp/chromium-profile/BrowserMetrics",0700);
    c3_ensure_dir_abs("/tmp/chromium-profile/Crash Reports",0700);
    c3_ensure_dir_abs("/tmp/chromium-profile/Crash Reports/completed",0700);
    c3_ensure_dir_abs("/tmp/chromium-profile/NativeMessagingHosts",0700);
    c3_ensure_dir_abs("/tmp/chromium-profile/component_crx_cache",0700);
    c3_ensure_dir_abs("/tmp/chromium-profile/ShaderCache",0700);
    c3_ensure_dir_abs("/tmp/chromium-profile/GrShaderCache",0700);
    c3_ensure_dir_abs("/tmp/chromium-profile/WidevineCdm",0700);
    c3_ensure_dir_abs("/tmp/chromium-profile/Default/GPUCache",0700);
    c3_ensure_dir_abs("/tmp/chromium-profile/Default/DawnWebGPUCache",0700);
    c3_ensure_dir_abs("/tmp/chromium-profile/Default/DawnGraphiteCache",0700);
    c3_ensure_dir_abs("/tmp/chromium-profile/Default/Cache",0700);
    c3_ensure_dir_abs("/tmp/chromium-profile/Default/Code Cache",0700);
    c3_ensure_dir_abs("/tmp/chromium-profile/Default/Code Cache/js",0700);
    c3_ensure_dir_abs("/tmp/chromium-profile/Default/Code Cache/wasm",0700);
    c3_ensure_dir_abs("/tmp/chromium-profile/Default/Local Storage",0700);
    c3_ensure_dir_abs("/tmp/chromium-profile/Default/Local Storage/leveldb",0700);
    c3_ensure_dir_abs("/tmp/chromium-profile/Default/Service Worker",0700);
    c3_ensure_dir_abs("/tmp/chromium-profile/Default/Sync Data",0700);
    c3_ensure_dir_abs("/tmp/chromium-profile/Default/Sync Data/LevelDB",0700);
    kvfs_write("/tmp/chromium-profile/First Run","");
    kvfs_write("/tmp/chromium-profile/Consent To Send Stats","");
    kvfs_write("/tmp/chromium-profile/Variations","");
    kvfs_write("/tmp/chromium-profile/Last Version","");
    kvfs_write("/tmp/chromium-profile/Local State","{\"browser\":{\"check_default_browser\":false},\"profile\":{\"info_cache\":{}},\"session\":{\"restore_on_startup\":5}}");
    kvfs_write("/tmp/chromium-profile/Default/Preferences","{\"profile\":{\"exit_type\":\"Normal\",\"exited_cleanly\":true},\"browser\":{\"has_seen_welcome_page\":true}}");
    kvfs_write("/tmp/chromium-profile/Default/Secure Preferences","{}");
    kvfs_write("/tmp/chromium-profile/Default/README","");
    kvfs_write("/tmp/chromium-profile/Default/Managed Mode Settings","{}");
    kvfs_write("/tmp/chromium-profile/WidevineCdm/latest-component-updated-widevine-cdm","");
    c3_ensure_dir_abs("/tmp/chromium",0700);
    c3_ensure_dir_abs("/tmp/chromium/Default",0700);
    c3_ensure_dir_abs("/tmp/chromium/BrowserMetrics",0700);
    c3_ensure_dir_abs("/tmp/chromium/Crash Reports",0700);
    c3_ensure_dir_abs("/tmp/chromium/Crash Reports/completed",0700);
    c3_ensure_dir_abs("/tmp/chromium/NativeMessagingHosts",0700);
    c3_ensure_dir_abs("/tmp/chromium/component_crx_cache",0700);
    c3_ensure_dir_abs("/tmp/chromium/ShaderCache",0700);
    c3_ensure_dir_abs("/tmp/chromium/GrShaderCache",0700);
    c3_ensure_dir_abs("/tmp/chromium/WidevineCdm",0700);
    c3_ensure_dir_abs("/tmp/chromium/Default/GPUCache",0700);
    c3_ensure_dir_abs("/tmp/chromium/Default/DawnWebGPUCache",0700);
    c3_ensure_dir_abs("/tmp/chromium/Default/DawnGraphiteCache",0700);
    c3_ensure_dir_abs("/tmp/chromium/Default/Cache",0700);
    c3_ensure_dir_abs("/tmp/chromium/Default/Code Cache",0700);
    c3_ensure_dir_abs("/tmp/chromium/Default/Code Cache/js",0700);
    c3_ensure_dir_abs("/tmp/chromium/Default/Code Cache/wasm",0700);
    c3_ensure_dir_abs("/tmp/chromium/Default/Local Storage",0700);
    c3_ensure_dir_abs("/tmp/chromium/Default/Local Storage/leveldb",0700);
    c3_ensure_dir_abs("/tmp/chromium/Default/Service Worker",0700);
    c3_ensure_dir_abs("/tmp/chromium/Default/Sync Data",0700);
    c3_ensure_dir_abs("/tmp/chromium/Default/Sync Data/LevelDB",0700);
    c3_ensure_dir_abs("/tmp/chromium/Default/Policy",0700);
    c3_ensure_dir_abs("/tmp/chromium-cache",0700);
    c3_ensure_dir_abs("/tmp/chromium-cache/Default",0700);
    c3_ensure_dir_abs("/tmp/chromium-cache/Default/Cache",0700);
    c3_ensure_dir_abs("/tmp/chromium-cache/Default/Cache/Cache_Data",0700);
    c3_ensure_dir_abs("/tmp/chromium-cache/Default/Code Cache",0700);
    c3_ensure_dir_abs("/tmp/chromium-cache/Default/Code Cache/js",0700);
    c3_ensure_dir_abs("/tmp/chromium-cache/Default/Code Cache/wasm",0700);
    kvfs_write("/tmp/chromium/First Run","");
    kvfs_write("/tmp/chromium/Consent To Send Stats","");
    kvfs_write("/tmp/chromium/Variations","");
    kvfs_write("/tmp/chromium/Last Version","");
    kvfs_write("/tmp/chromium/Local State","{\"browser\":{\"check_default_browser\":false},\"profile\":{\"info_cache\":{}},\"session\":{\"restore_on_startup\":5}}");
    kvfs_write("/tmp/chromium/Default/Preferences","{\"profile\":{\"exit_type\":\"Normal\",\"exited_cleanly\":true},\"browser\":{\"has_seen_welcome_page\":true}}");
    kvfs_write("/tmp/chromium/Default/Secure Preferences","{}");
    kvfs_write("/tmp/chromium/Default/README","");
    kvfs_write("/tmp/chromium/Default/Managed Mode Settings","{}");
    kvfs_write("/tmp/chromium/Default/Policy/User Policy","");
    kvfs_write("/tmp/chromium/WidevineCdm/latest-component-updated-widevine-cdm","");
    c3_ensure_dir_abs("/home/.config",0700);
    c3_ensure_dir_abs("/home/.config/chromium",0700);
    c3_ensure_dir_abs("/home/.config/chromium/Default",0700);
    c3_ensure_dir_abs("/home/.config/chromium/BrowserMetrics",0700);
    c3_ensure_dir_abs("/home/.config/chromium/Crash Reports",0700);
    c3_ensure_dir_abs("/home/.config/chromium/Crash Reports/completed",0700);
    c3_ensure_dir_abs("/home/.config/chromium/NativeMessagingHosts",0700);
    c3_ensure_dir_abs("/home/.config/chromium/component_crx_cache",0700);
    c3_ensure_dir_abs("/home/.config/chromium/ShaderCache",0700);
    c3_ensure_dir_abs("/home/.config/chromium/GrShaderCache",0700);
    c3_ensure_dir_abs("/home/.config/chromium/WidevineCdm",0700);
    c3_ensure_dir_abs("/home/.config/chromium/Default/GPUCache",0700);
    c3_ensure_dir_abs("/home/.config/chromium/Default/DawnWebGPUCache",0700);
    c3_ensure_dir_abs("/home/.config/chromium/Default/DawnGraphiteCache",0700);
    c3_ensure_dir_abs("/home/.config/chromium/Default/Cache",0700);
    c3_ensure_dir_abs("/home/.config/chromium/Default/Code Cache",0700);
    c3_ensure_dir_abs("/home/.config/chromium/Default/Code Cache/js",0700);
    c3_ensure_dir_abs("/home/.config/chromium/Default/Code Cache/wasm",0700);
    c3_ensure_dir_abs("/home/.config/chromium/Default/Local Storage",0700);
    c3_ensure_dir_abs("/home/.config/chromium/Default/Local Storage/leveldb",0700);
    c3_ensure_dir_abs("/home/.config/chromium/Default/Service Worker",0700);
    c3_ensure_dir_abs("/home/.config/chromium/Default/Sync Data",0700);
    c3_ensure_dir_abs("/home/.config/chromium/Default/Sync Data/LevelDB",0700);
    c3_ensure_dir_abs("/home/.cache",0700);
    c3_ensure_dir_abs("/home/.cache/chromium",0700);
    c3_ensure_dir_abs("/home/.cache/chromium/Default",0700);
    c3_ensure_dir_abs("/home/.cache/chromium/Default/Cache",0700);
    c3_ensure_dir_abs("/home/.cache/chromium/Default/Cache/Cache_Data",0700);
    c3_ensure_dir_abs("/home/.cache/chromium/Default/Code Cache",0700);
    c3_ensure_dir_abs("/home/.cache/chromium/Default/Code Cache/js",0700);
    c3_ensure_dir_abs("/home/.cache/chromium/Default/Code Cache/wasm",0700);
    kvfs_write("/home/.config/chromium/First Run","");
    kvfs_write("/home/.config/chromium/Consent To Send Stats","");
    kvfs_write("/home/.config/chromium/Variations","");
    kvfs_write("/home/.config/chromium/Last Version","");
    kvfs_write("/home/.config/chromium/Local State","{\"browser\":{\"check_default_browser\":false},\"profile\":{\"info_cache\":{}},\"session\":{\"restore_on_startup\":5}}");
    kvfs_write("/home/.config/chromium/Default/Preferences","{\"profile\":{\"exit_type\":\"Normal\",\"exited_cleanly\":true},\"browser\":{\"has_seen_welcome_page\":true}}");
    kvfs_write("/home/.config/chromium/Default/Secure Preferences","{}");
    kvfs_write("/home/.config/chromium/Default/README","");
    kvfs_write("/home/.config/chromium/Default/Managed Mode Settings","{}");
    kvfs_write("/home/.config/chromium/WidevineCdm/latest-component-updated-widevine-cdm","");
    c3_memset(g_c3_futex_waiters,0,sizeof(g_c3_futex_waiters));
    c3_memset(g_c3_eventfds,0,sizeof(g_c3_eventfds));
    c3_memset(g_c3_timerfds,0,sizeof(g_c3_timerfds));
    c3_memset(g_c3_inotify_inst,0,sizeof(g_c3_inotify_inst));
    c3_memset(g_c3_inotify_watch,0,sizeof(g_c3_inotify_watch));
    g_c3_inotify_cookie=1;
    for(i=0;i<C3_FUTEX_BUCKETS;++i)g_c3_futex_heads[i]=-1;
    g_c3_dyn_images_mapped=0;
    g_c3_dyn_reloc_applied=0;
    g_c3_dyn_reloc_failed=0;
    g_c3_dyn_reloc_unsupported=0;
    g_c3_dyn_needed_loaded=0;
    g_c3_dyn_needed_missing=0;
    g_c3_dyn_objects=0;
    g_c3_dyn_load_depth=0;
    g_c3_dyn_timeout_ms=4000;
    g_c3_dyn_timeout_hits=0;
    g_c3_dyn_last_timeout_tsc=0;
    g_c3_dyn_last_timeout_obj[0]=0;
    g_c3_dyn_last_timeout_stage[0]=0;
    g_c3_dyn_cycle_map=0;
    g_c3_dyn_cycle_needed=0;
    g_c3_dyn_cycle_reloc=0;
    g_c3_dyn_profile_next=0;
    g_c3_cow_faults=0;
    g_c3_cow_copies=0;
    g_c3_cow_shared_maps=0;
    g_c3_cow_fault_fail=0;
    g_c3_tls_next_module=1;
    g_c3_next_image_name[0]=0;
    c3_memset(g_c3_dynobjs,0,sizeof(g_c3_dynobjs));
    c3_memset(g_c3_dyn_profile,0,sizeof(g_c3_dyn_profile));
    c3_memset(g_c3_seccomp_mode,0,sizeof(g_c3_seccomp_mode));
    c3_memset(g_c3_seccomp_flags,0,sizeof(g_c3_seccomp_flags));
    c3_memset(g_c3_ns_mask,0,sizeof(g_c3_ns_mask));
    c3_memset(g_c3_no_new_privs,0,sizeof(g_c3_no_new_privs));
    c3_memset(g_c3_sched_affinity,0,sizeof(g_c3_sched_affinity));
    c3_memset(g_c3_rlimits,0,sizeof(g_c3_rlimits));
    c3_memset(g_c3_rlimits_inited,0,sizeof(g_c3_rlimits_inited));
    for(i=0;i<TASK_MAX;++i)g_c3_rlimits_pid[i]=-1;
    for(i=0;i<TASK_MAX;++i){
        g_c3_sched_affinity[i][0]=1ULL;
        c3_rlimit_ensure_task_defaults(i);
    }
    c3_memset(g_c3_phys_refcnt,0,sizeof(g_c3_phys_refcnt));
    c3_memset(g_c3_file_pages,0,sizeof(g_c3_file_pages));
    /* Rewire syscall table from stubs to real implementations */
    compat3_rewire_syscalls();
    /* Register shell commands */
    compat3_register_shell_cmds();

    /* M1A pilot smoke test: invoke FreeBSD's bsd_to_linux_errno()
     * from the imported sys/compat/linux/linux_errno.c to confirm the
     * Linuxulator import + freebsd_compat shim layer works at runtime,
     * not just at link time.
     *
     * Inputs: BSD EAGAIN(35), EINVAL(22), ENOSYS(78), EPIPE(32)
     * Expected Linux mapping (negated): -11, -22, -38, -32
     * If we see those exact values in serial, the import is healthy. */
    {
        extern int bsd_to_linux_errno(int error);
        /* The mapping returns NEGATIVE Linux errnos (e.g. -11 for
         * EAGAIN). We print as 32-bit unsigned for readability — a
         * "good" run shows 0xFFFFFFF5 (-11), 0xFFFFFFEA (-22),
         * 0xFFFFFFDA (-38), 0xFFFFFFE0 (-32). */
        __boot_serial_puts("[linuxulator] M1A bsd_to_linux_errno smoke test:\n");
        __boot_serial_puts("[linuxulator]   bsd EAGAIN(35) -> linux 0x");
        __boot_serial_putu32((uint32_t)bsd_to_linux_errno(35));
        __boot_serial_puts(" (expect 0xFFFFFFF5 = -11)\n");
        __boot_serial_puts("[linuxulator]   bsd EINVAL(22) -> linux 0x");
        __boot_serial_putu32((uint32_t)bsd_to_linux_errno(22));
        __boot_serial_puts(" (expect 0xFFFFFFEA = -22)\n");
        __boot_serial_puts("[linuxulator]   bsd ENOSYS(78) -> linux 0x");
        __boot_serial_putu32((uint32_t)bsd_to_linux_errno(78));
        __boot_serial_puts(" (expect 0xFFFFFFDA = -38)\n");
        __boot_serial_puts("[linuxulator]   bsd EPIPE(32)  -> linux 0x");
        __boot_serial_putu32((uint32_t)bsd_to_linux_errno(32));
        __boot_serial_puts(" (expect 0xFFFFFFE0 = -32)\n");
    }
}
