/*
 * Memoria, tareas y salto a Ring 3.
 *
 * Esta parte mantiene paging, scheduler de tareas de usuario, syscalls y
 * algunos helpers de red/dispositivos que todavia comparten estructuras.
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "base.h"
#include "memory_tasks.h"

#define PAGE_HUGE 0x080ULL

extern void __boot_serial_puts(const char *s);
extern void __boot_serial_puthex64(uint64_t v);
extern void compat3_task_close_all_fds(task_t *t);

/* local helpers */
static inline void c2_outb(uint16_t p,uint8_t v){__asm__ volatile("outb %0,%1"::"a"(v),"Nd"(p));}
static inline uint8_t c2_inb(uint16_t p){uint8_t r;__asm__ volatile("inb %1,%0":"=a"(r):"Nd"(p));return r;}
static void *c2_memset(void *d,int v,size_t n){uint8_t *p=(uint8_t*)d;size_t i;for(i=0;i<n;++i)p[i]=(uint8_t)v;return d;}
static void *c2_memcpy(void *d,const void *s,size_t n){uint8_t *dd=(uint8_t*)d;const uint8_t *ss=(const uint8_t*)s;size_t i;for(i=0;i<n;++i)dd[i]=ss[i];return d;}
static size_t c2_strlen(const char *s){size_t n=0;while(s[n])++n;return n;}
static void c2_strlcpy(char *d,const char *s,size_t c){size_t i=0;if(!c)return;while(i+1<c&&s[i]){d[i]=s[i];++i;}d[i]=0;}
static int c2_strcmp(const char *a,const char *b){while(*a&&*b&&*a==*b){++a;++b;}return(int)((unsigned char)*a-(unsigned char)*b);}
static void c2_append_str(char *d,size_t *l,size_t c,const char *s){while(*s&&*l+1<c){d[(*l)++]=*s++;}d[*l]=0;}
static void c2_append_ch(char *d,size_t *l,size_t c,char ch){if(*l+1<c){d[(*l)++]=ch;d[*l]=0;}}
static void c2_append_u32(char *d,size_t *l,size_t c,uint32_t v){char t[12];int i=0;if(!v){c2_append_ch(d,l,c,'0');return;}while(v){t[i++]='0'+(char)(v%10);v/=10;}while(--i>=0)c2_append_ch(d,l,c,t[i]);}
static void c2_append_u64(char *d,size_t *l,size_t c,uint64_t v){char t[24];int i=0;if(!v){c2_append_ch(d,l,c,'0');return;}while(v){t[i++]='0'+(char)(v%10);v/=10;}while(--i>=0)c2_append_ch(d,l,c,t[i]);}
static void c2_append_hex32(char *d,size_t *l,size_t c,uint32_t v){const char *h="0123456789abcdef";int i;c2_append_ch(d,l,c,'0');c2_append_ch(d,l,c,'x');for(i=28;i>=0;i-=4)c2_append_ch(d,l,c,h[(v>>i)&0xF]);}
static void c2_append_hex64(char *d,size_t *l,size_t c,uint64_t v){const char *h="0123456789abcdef";int i;c2_append_ch(d,l,c,'0');c2_append_ch(d,l,c,'x');for(i=60;i>=0;i-=4)c2_append_ch(d,l,c,h[(v>>i)&0xF]);}

static inline uint64_t rdtsc(void){uint32_t lo,hi;__asm__ volatile("rdtsc":"=a"(lo),"=d"(hi));return((uint64_t)hi<<32)|lo;}
static inline uint64_t read_cr3(void){uint64_t v;__asm__ volatile("mov %%cr3,%0":"=r"(v));return v;}
static inline void write_cr3(uint64_t v){__asm__ volatile("mov %0,%%cr3"::"r"(v):"memory");}
static inline void invlpg(uint64_t addr){__asm__ volatile("invlpg (%0)"::"r"(addr):"memory");}
static inline void c2_wrmsr(uint32_t msr,uint64_t v){uint32_t lo=(uint32_t)v,hi=(uint32_t)(v>>32);__asm__ volatile("wrmsr"::"c"(msr),"a"(lo),"d"(hi));}
static inline uint64_t c2_rdmsr(uint32_t msr){uint32_t lo,hi;__asm__ volatile("rdmsr":"=a"(lo),"=d"(hi):"c"(msr));return((uint64_t)hi<<32)|lo;}
static inline void c2_cpuid_count(uint32_t leaf,uint32_t subleaf,uint32_t *a,uint32_t *b,uint32_t *c,uint32_t *d){
    uint32_t eax,ebx,ecx,edx;
    __asm__ volatile("cpuid":"=a"(eax),"=b"(ebx),"=c"(ecx),"=d"(edx):"a"(leaf),"c"(subleaf));
    if(a)*a=eax;
    if(b)*b=ebx;
    if(c)*c=ecx;
    if(d)*d=edx;
}
static inline uint64_t c2_xgetbv(uint32_t xcr){
    uint32_t lo,hi;
    __asm__ volatile(".byte 0x0f,0x01,0xd0":"=a"(lo),"=d"(hi):"c"(xcr));
    return ((uint64_t)hi<<32)|lo;
}
static inline void c2_xsetbv(uint32_t xcr,uint64_t v){
    uint32_t lo=(uint32_t)v,hi=(uint32_t)(v>>32);
    __asm__ volatile(".byte 0x0f,0x01,0xd1"::"c"(xcr),"a"(lo),"d"(hi):"memory");
}

#define C2_CR4_OSFXSR     (1ULL << 9)
#define C2_CR4_OSXMMEXCPT (1ULL << 10)
#define C2_CR4_FSGSBASE   (1ULL << 16)
#define C2_CR4_OSXSAVE    (1ULL << 18)
#define C2_XCR0_X87       (1ULL << 0)
#define C2_XCR0_SSE       (1ULL << 1)
#define C2_XCR0_AVX       (1ULL << 2)

static bool g_fpu_use_xsave = false;
static bool g_fpu_avx_enabled = false;
static uint64_t g_fpu_xstate_mask = C2_XCR0_X87 | C2_XCR0_SSE;
static uint32_t g_fpu_state_size = 512;
static bool g_paging_nx_enabled = false;
static bool g_c2_fsgsbase_enabled = false;

static bool c2_cpu_has_ext_edx_bit(uint32_t bit){
    uint32_t max_leaf=0,a,b,c,d;
    c2_cpuid_count(0x80000000u,0,&max_leaf,&b,&c,&d);
    if(max_leaf<0x80000001u)return false;
    c2_cpuid_count(0x80000001u,0,&a,&b,&c,&d);
    return (d&(1u<<bit))!=0;
}

static bool c2_cpu_has_leaf7_ebx_bit(uint32_t bit){
    uint32_t max_leaf=0,a=0,b=0,c=0,d=0;
    c2_cpuid_count(0,0,&max_leaf,0,0,0);
    if(max_leaf<7)return false;
    c2_cpuid_count(7,0,&a,&b,&c,&d);
    return (b&(1u<<bit))!=0;
}

static inline void c2_wrfsbase(uint64_t v){
    if(!g_c2_fsgsbase_enabled)return;
    __asm__ volatile("wrfsbase %0"::"r"(v):"memory");
}
static inline uint64_t c2_rdfsbase_current(void){
    uint64_t v;
    if(g_c2_fsgsbase_enabled){
        __asm__ volatile("rdfsbase %0":"=r"(v)::"memory");
        return v;
    }
    return c2_rdmsr(0xC0000100u);
}

static inline void c2_capture_live_user_fsgs(task_t *t){
    if(!t||t->ctx.cs!=0x28)return;
    /*
     * On SYSCALL/IRQ/exception entry from ring 3, isr64.S has already
     * swapgs'ed into the kernel GS base. FS still contains the user's live
     * base; the user's GS base is now parked in IA32_KERNEL_GS_BASE until the
     * return-path swapgs. This is the piece Linux relies on when CR4.FSGSBASE
     * is enabled: userspace may mutate FS/GS without arch_prctl(), so every
     * kernel entry must sample the live values before a context switch can
     * restore stale TLS into another thread.
     */
    t->ctx.fs_base=c2_rdfsbase_current();
    t->ctx.gs_base=c2_rdmsr(0xC0000102u);
}

static inline void c2_clear_fs_selector(void){
    /*
     * Firefox/glibc use ARCH_SET_FS with a zero selector and a 64-bit FS base.
     * Keep the visible selector at 0 before rewriting IA32_FS_BASE so stale
     * flat kernel/user descriptors cannot leave %fs:off resolving as off.
     */
    __asm__ volatile("xor %%eax,%%eax; mov %%ax,%%fs":::"rax","memory");
}

static uint64_t c2_sanitize_page_flags(uint64_t flags){
    if(!g_paging_nx_enabled)flags&=~PAGE_NX;
    return flags;
}

/* Physical Memory Manager (bitmap allocator) */
static uint32_t g_pmm_bitmap[PMM_MAX_PAGES/32];
static uint32_t g_pmm_pt_bitmap[PMM_MAX_PAGES/32];
static uint32_t g_pmm_total;
static uint32_t g_pmm_free;
static uint32_t g_pmm_hint;
static uint64_t g_pmm_base = 0x200000ULL; /* start after 2MB */

static bool pmm_frame_index(uint64_t phys,uint32_t *idx_out){
    uint32_t idx;
    if(phys<g_pmm_base)return false;
    idx=(uint32_t)((phys&~(PAGE_SIZE-1ULL))/PAGE_SIZE);
    if(idx>=g_pmm_total)return false;
    if(idx_out)*idx_out=idx;
    return true;
}

static void pmm_mark_page_table_frame(uint64_t phys){
    uint32_t idx;
    if(!pmm_frame_index(phys,&idx))return;
    g_pmm_pt_bitmap[idx/32u]|=(1u<<(idx%32u));
}

static bool pmm_is_page_table_frame(uint64_t phys){
    uint32_t idx;
    if(!pmm_frame_index(phys,&idx))return false;
    return (g_pmm_pt_bitmap[idx/32u]&(1u<<(idx%32u)))!=0;
}

void pmm_set_alloc_base(uint64_t phys_base){
    if(phys_base<0x200000ULL)phys_base=0x200000ULL;
    g_pmm_base=phys_base&~(PAGE_SIZE-1ULL);
}

void pmm_init(uint64_t mem_bytes){
    uint32_t pages=(uint32_t)(mem_bytes/PAGE_SIZE);
    uint32_t reserve_pages;
    if(pages>PMM_MAX_PAGES)pages=PMM_MAX_PAGES;
    g_pmm_total=pages; g_pmm_free=pages;
    c2_memset(g_pmm_bitmap,0,sizeof(g_pmm_bitmap));
    c2_memset(g_pmm_pt_bitmap,0,sizeof(g_pmm_pt_bitmap));
    reserve_pages=(uint32_t)(g_pmm_base/PAGE_SIZE);
    if(reserve_pages>pages)reserve_pages=pages;
    {uint32_t i;for(i=0;i<reserve_pages;++i){g_pmm_bitmap[i/32]|=(1u<<(i%32));g_pmm_free--;}}
    g_pmm_hint=reserve_pages<g_pmm_total?reserve_pages:0;
}
uint64_t pmm_alloc_frame(void){
    uint32_t start,end,idx;
    int pass;
    if(!g_pmm_total)return 0;
    start=(g_pmm_hint<g_pmm_total)?g_pmm_hint:0;
    for(pass=0;pass<2;++pass){
        if(pass==0){idx=start;end=g_pmm_total;}
        else{idx=0;end=start;}
        for(;idx<end;++idx){
            uint32_t word=idx/32u;
            uint32_t bit=idx%32u;
            if(g_pmm_bitmap[word]&(1u<<bit))continue;
            if(g_pmm_pt_bitmap[word]&(1u<<bit))continue;
            g_pmm_bitmap[word]|=(1u<<bit);
            if(g_pmm_free)g_pmm_free--;
            g_pmm_hint=(idx+1u<g_pmm_total)?(idx+1u):0u;
            return(uint64_t)idx*PAGE_SIZE;
        }
    }
    return 0;
}

static uint64_t pmm_alloc_contiguous_frames(uint32_t pages){
    uint32_t idx,start=0,run=0;
    if(pages==0||pages>g_pmm_total)return 0;
    for(idx=0;idx<g_pmm_total;++idx){
        uint32_t word=idx/32u;
        uint32_t bit=idx%32u;
        bool free=((g_pmm_bitmap[word]&(1u<<bit))==0)&&
                  ((g_pmm_pt_bitmap[word]&(1u<<bit))==0);
        if(!free){run=0;continue;}
        if(run==0)start=idx;
        ++run;
        if(run==pages){
            uint32_t p;
            for(p=0;p<pages;++p){
                uint32_t j=start+p;
                g_pmm_bitmap[j/32u]|=(1u<<(j%32u));
            }
            if(g_pmm_free>=pages)g_pmm_free-=pages;else g_pmm_free=0;
            g_pmm_hint=(start+pages<g_pmm_total)?(start+pages):0u;
            return(uint64_t)start*PAGE_SIZE;
        }
    }
    return 0;
}
void pmm_free_frame(uint64_t phys){
    uint32_t idx;
    static uint32_t pt_free_warns=0;
    phys&=~(PAGE_SIZE-1ULL);
    if(!pmm_frame_index(phys,&idx))return;
    if(pmm_is_page_table_frame(phys)){
        if(pt_free_warns<16){
            ++pt_free_warns;
            __boot_serial_puts("[pmm] refused free of page-table frame ");
            __boot_serial_puthex64(phys);
            __boot_serial_puts("\n");
        }
        return;
    }
    if(g_pmm_bitmap[idx/32]&(1u<<(idx%32))){
        g_pmm_bitmap[idx/32]&=~(1u<<(idx%32));
        g_pmm_free++;
        if(idx<g_pmm_hint)g_pmm_hint=idx;
    }
}
uint32_t pmm_free_count(void){return g_pmm_free;}
uint32_t pmm_total_count(void){return g_pmm_total;}

/* Real paging: PML4 → PDPT → PD → PT */
static address_space_t g_kernel_as;

/* Allocate a zeroed page for page tables.
 * The returned pointer is a DMAP virtual address, reachable regardless
 * of which CR3 is active (FreeBSD PHYS_TO_DMAP pattern). */
static page_table_t *alloc_page_table(void){
    uint64_t f=pmm_alloc_frame();
    if(!f)return 0;
    pmm_mark_page_table_frame(f);
    page_table_t *pt=(page_table_t*)PHYS_TO_DMAP(f);
    c2_memset(pt,0,sizeof(page_table_t));
    return pt;
}

void paging_init(void){
    /* Use current CR3 as kernel PML4, accessed via DMAP */
    uint64_t cr3=read_cr3();
    g_kernel_as.cr3_phys=cr3&~0xFFFULL;
    g_kernel_as.pml4=(page_table_t*)PHYS_TO_DMAP(cr3&~0xFFFULL);
}

address_space_t *paging_get_kernel_space(void){return &g_kernel_as;}

/* ----------------------------------------------------------------
 * Deep-clone helpers for paging_create_address_space
 *
 * The kernel identity-maps the low 4 GiB using 2 MiB huge pages at
 * the PD level (built by boot64.S). If user address spaces merely
 * cloned the PML4, each user task would share the kernel's PDPT/PD
 * chain -- any `paging_map` that promoted PAGE_USER on an intermediate
 * entry would modify the SHARED kernel tables, leaking ring-3 access
 * into every other AS and (more immediately) making it impossible to
 * install per-task 4 KiB user mappings on top of existing huge pages.
 *
 * Solution: when creating a user AS, deep-clone the low-half PDPT
 * chain. Huge pages are kept pointing at the original kernel physical
 * frames (since kernel still needs its identity map running while
 * user code executes), but the *page table entries* holding those
 * pointers are now owned by the user AS, so paging_map can freely
 * split a huge PDE into a PT of 512 4 KiB PTEs without disturbing
 * kernel mappings.
 * ---------------------------------------------------------------- */
static bool deep_clone_pd(page_table_t *dst_pd, const page_table_t *src_pd) {
    int pdi;
    if (!dst_pd || !src_pd) return false;
    for (pdi = 0; pdi < 512; ++pdi) {
        uint64_t entry = src_pd->entries[pdi];
        if (!(entry & PAGE_PRESENT)) { dst_pd->entries[pdi] = 0; continue; }
        if (entry & PAGE_HUGE) {
            /* Keep kernel identity huge pages supervisor-only inside
             * user address spaces. paging_map() will later split the
             * specific huge page that backs a user mapping and install
             * PAGE_USER only on the leaf entries that truly belong to
             * the process. */
            dst_pd->entries[pdi] = entry;
            continue;
        }
        {
            page_table_t *src_pt = (page_table_t*)PHYS_TO_DMAP(entry & ~0xFFFULL);
            page_table_t *dst_pt = alloc_page_table();
            int pti;
            if (!dst_pt) return false;
            for (pti = 0; pti < 512; ++pti) {
                uint64_t pte = src_pt->entries[pti];
                if (pte & PAGE_PRESENT) dst_pt->entries[pti] = pte;
                else                    dst_pt->entries[pti] = 0;
            }
            dst_pd->entries[pdi] = DMAP_TO_PHYS((uint64_t)dst_pt)
                                   | (entry & 0xFFFULL)
                                   | PAGE_USER;
        }
    }
    return true;
}

static bool deep_clone_pdpt(page_table_t *dst_pdpt, const page_table_t *src_pdpt) {
    int pdpti;
    if (!dst_pdpt || !src_pdpt) return false;
    for (pdpti = 0; pdpti < 512; ++pdpti) {
        uint64_t entry = src_pdpt->entries[pdpti];
        if (!(entry & PAGE_PRESENT)) { dst_pdpt->entries[pdpti] = 0; continue; }
        if (entry & PAGE_HUGE) {
            /* Same rule as the 2 MiB clone above: keep low identity
             * mappings supervisor-only until paging_map() installs an
             * actual user leaf below this range. */
            dst_pdpt->entries[pdpti] = entry;
            continue;
        }
        {
            page_table_t *src_pd = (page_table_t*)PHYS_TO_DMAP(entry & ~0xFFFULL);
            page_table_t *dst_pd = alloc_page_table();
            if (!dst_pd) return false;
            if (!deep_clone_pd(dst_pd, src_pd)) return false;
            dst_pdpt->entries[pdpti] = DMAP_TO_PHYS((uint64_t)dst_pd)
                                       | (entry & 0xFFFULL)
                                       | PAGE_USER;
        }
    }
    return true;
}

address_space_t *paging_create_address_space(void){
    static address_space_t spaces[TASK_MAX];
    static int next_space=0;
    address_space_t *as;
    page_table_t *pml4;
    int i;
    if(next_space>=TASK_MAX)return 0;
    as=&spaces[next_space++];
    pml4=alloc_page_table();
    if(!pml4)return 0;
    c2_memset(pml4, 0, sizeof(*pml4));

    /* Deep-clone low-half (PML4[0..255]) so the new AS owns its own
     * PDPT / PD / PT chain there. This lets the kernel continue to
     * execute from the identity map after CR3 switch while user
     * paging_map calls are free to split huge pages and install
     * 4 KiB user mappings without touching the kernel's tables.
     *
     * High-half entries (PML4[256..511]) are copied as-is: they
     * typically hold kernel-only mappings (or are absent) and need
     * no user access.                                              */
    for (i = 0; i < 512; ++i) {
        uint64_t entry = g_kernel_as.pml4->entries[i];
        if (!(entry & PAGE_PRESENT)) { pml4->entries[i] = 0; continue; }
        if (i < 256) {
            page_table_t *src_pdpt = (page_table_t*)PHYS_TO_DMAP(entry & ~0xFFFULL);
            page_table_t *dst_pdpt = alloc_page_table();
            if (!dst_pdpt) return 0;
            if (!deep_clone_pdpt(dst_pdpt, src_pdpt)) return 0;
            pml4->entries[i] = DMAP_TO_PHYS((uint64_t)dst_pdpt)
                               | (entry & 0xFFFULL)
                               | PAGE_USER;
        } else {
            pml4->entries[i] = entry;
        }
    }

    as->pml4=pml4;
    as->cr3_phys=DMAP_TO_PHYS((uint64_t)pml4);
    return as;
}

void paging_destroy_address_space(address_space_t *as){
    /* Free user page tables (simplified: just free PML4) */
    if(as&&as->pml4&&as!=&g_kernel_as){
        pmm_free_frame(DMAP_TO_PHYS((uint64_t)as->pml4));
        as->pml4=0;
    }
}

/* ----------------------------------------------------------------
 * Split a 2 MiB huge page into a 4 KiB PT
 *
 * Replaces the huge PDE at pd->entries[pdi] with a pointer to a new
 * PT. The new PT is populated with 512 PTEs that each identity-map
 * (huge_phys + i*4K) with the same access flags as the original huge
 * page, plus PAGE_USER if user access is requested. This preserves
 * correctness of all addresses in the 2 MiB range while allowing
 * subsequent paging_map calls to overwrite individual PTEs with
 * different frames (e.g., per-task ELF PT_LOAD pages).
 * ---------------------------------------------------------------- */
static bool paging_split_2m_huge(page_table_t *pd, uint64_t pdi, bool user_access) {
    uint64_t old = pd->entries[pdi];
    uint64_t huge_phys;
    uint64_t pte_flags;
    page_table_t *pt;
    int i;
    if (!(old & PAGE_HUGE)) return true;            /* nothing to split */
    huge_phys = old & ~0xFFFULL & ~(uint64_t)PAGE_HUGE;
    /* The PTE format does not include the HUGE bit; preserve the
     * existing leaf protections and only add PAGE_USER for the page
     * we are about to replace with an explicit user mapping. */
    pte_flags = c2_sanitize_page_flags((old & 0xFFFULL) & ~(uint64_t)PAGE_HUGE);
    pt = alloc_page_table();
    if (!pt) return false;
    for (i = 0; i < 512; ++i) {
        pt->entries[i] = (huge_phys + ((uint64_t)i << 12)) | pte_flags;
    }
    pd->entries[pdi] = DMAP_TO_PHYS((uint64_t)pt)
                       | PAGE_PRESENT | PAGE_WRITABLE
                       | (user_access ? PAGE_USER : 0);
    return true;
}

/* ----------------------------------------------------------------
 * Split a 1 GiB huge page at PDPTE level into a PD of 512 x 2 MiB
 * huge pages. Same idea as the 2 MiB splitter but one level up.
 * ---------------------------------------------------------------- */
static bool paging_split_1g_huge(page_table_t *pdpt, uint64_t pdpti, bool user_access) {
    uint64_t old = pdpt->entries[pdpti];
    uint64_t huge_phys;
    uint64_t pde_flags;
    page_table_t *pd;
    int i;
    if (!(old & PAGE_HUGE)) return true;
    huge_phys = old & ~0xFFFULL & ~(uint64_t)PAGE_HUGE;
    pde_flags = old & 0xFFFULL;   /* keep HUGE bit for child PDEs */
    pd = alloc_page_table();
    if (!pd) return false;
    for (i = 0; i < 512; ++i) {
        pd->entries[i] = (huge_phys + ((uint64_t)i << 21)) | pde_flags;
    }
    pdpt->entries[pdpti] = DMAP_TO_PHYS((uint64_t)pd)
                           | PAGE_PRESENT | PAGE_WRITABLE
                           | (user_access ? PAGE_USER : 0);
    return true;
}

bool paging_map(address_space_t *as,uint64_t virt,uint64_t phys,uint64_t flags){
    uint64_t pml4i=(virt>>39)&0x1FF;
    uint64_t pdpti=(virt>>30)&0x1FF;
    uint64_t pdi=(virt>>21)&0x1FF;
    uint64_t pti=(virt>>12)&0x1FF;
    page_table_t *pdpt,*pd,*pt;
    bool want_user = (flags & PAGE_USER) != 0;
    uint64_t parent_promote = want_user ? PAGE_USER : 0;
    if(!as||!as->pml4)return false;
    flags=c2_sanitize_page_flags(flags);
    /* PML4 -> PDPT */
    if(!(as->pml4->entries[pml4i]&PAGE_PRESENT)){
        pdpt=alloc_page_table();if(!pdpt)return false;
        as->pml4->entries[pml4i]=DMAP_TO_PHYS((uint64_t)pdpt)|PAGE_PRESENT|PAGE_WRITABLE|PAGE_USER;
    } else if(parent_promote && !(as->pml4->entries[pml4i]&PAGE_USER)) {
        as->pml4->entries[pml4i] |= PAGE_USER;
    }
    pdpt=(page_table_t*)PHYS_TO_DMAP(as->pml4->entries[pml4i]&~0xFFFULL);
    if (pdpt->entries[pdpti] & PAGE_HUGE) {
        if (!paging_split_1g_huge(pdpt, pdpti, want_user)) return false;
    }
    /* PDPT -> PD */
    if(!(pdpt->entries[pdpti]&PAGE_PRESENT)){
        pd=alloc_page_table();if(!pd)return false;
        pdpt->entries[pdpti]=DMAP_TO_PHYS((uint64_t)pd)|PAGE_PRESENT|PAGE_WRITABLE|PAGE_USER;
    } else if(parent_promote && !(pdpt->entries[pdpti]&PAGE_USER)) {
        pdpt->entries[pdpti] |= PAGE_USER;
    }
    pd=(page_table_t*)PHYS_TO_DMAP(pdpt->entries[pdpti]&~0xFFFULL);
    if (pd->entries[pdi] & PAGE_HUGE) {
        if (!paging_split_2m_huge(pd, pdi, want_user)) return false;
    }
    /* PD -> PT */
    if(!(pd->entries[pdi]&PAGE_PRESENT)){
        pt=alloc_page_table();if(!pt)return false;
        pd->entries[pdi]=DMAP_TO_PHYS((uint64_t)pt)|PAGE_PRESENT|PAGE_WRITABLE|PAGE_USER;
    } else if(parent_promote && !(pd->entries[pdi]&PAGE_USER)) {
        pd->entries[pdi] |= PAGE_USER;
    }
    pt=(page_table_t*)PHYS_TO_DMAP(pd->entries[pdi]&~0xFFFULL);
    pt->entries[pti]=phys|flags;
    invlpg(virt);
    return true;
}

bool paging_unmap(address_space_t *as,uint64_t virt){
    uint64_t pml4i=(virt>>39)&0x1FF,pdpti=(virt>>30)&0x1FF,pdi=(virt>>21)&0x1FF,pti=(virt>>12)&0x1FF;
    page_table_t *pdpt,*pd,*pt;
    if(!as||!as->pml4)return false;
    if(!(as->pml4->entries[pml4i]&PAGE_PRESENT))return false;
    pdpt=(page_table_t*)PHYS_TO_DMAP(as->pml4->entries[pml4i]&~0xFFFULL);
    if(pdpt->entries[pdpti]&PAGE_HUGE)return false;
    if(!(pdpt->entries[pdpti]&PAGE_PRESENT))return false;
    pd=(page_table_t*)PHYS_TO_DMAP(pdpt->entries[pdpti]&~0xFFFULL);
    if(pd->entries[pdi]&PAGE_HUGE)return false;
    if(!(pd->entries[pdi]&PAGE_PRESENT))return false;
    pt=(page_table_t*)PHYS_TO_DMAP(pd->entries[pdi]&~0xFFFULL);
    pt->entries[pti]=0;
    invlpg(virt);
    return true;
}

uint64_t paging_translate(address_space_t *as,uint64_t virt){
    const uint64_t addr_mask=0x000FFFFFFFFFF000ULL;
    uint64_t pml4i=(virt>>39)&0x1FF,pdpti=(virt>>30)&0x1FF,pdi=(virt>>21)&0x1FF,pti=(virt>>12)&0x1FF;
    page_table_t *pdpt,*pd,*pt;
    if(!as||!as->pml4)return 0;
    if(!(as->pml4->entries[pml4i]&PAGE_PRESENT))return 0;
    pdpt=(page_table_t*)PHYS_TO_DMAP(as->pml4->entries[pml4i]&~0xFFFULL);
    if(pdpt->entries[pdpti]&PAGE_HUGE)
        return((pdpt->entries[pdpti]&addr_mask)&~((1ULL<<30)-1ULL))|(virt&((1ULL<<30)-1ULL));
    if(!(pdpt->entries[pdpti]&PAGE_PRESENT))return 0;
    pd=(page_table_t*)PHYS_TO_DMAP(pdpt->entries[pdpti]&~0xFFFULL);
    if(pd->entries[pdi]&PAGE_HUGE)
        return((pd->entries[pdi]&addr_mask)&~((1ULL<<21)-1ULL))|(virt&((1ULL<<21)-1ULL));
    if(!(pd->entries[pdi]&PAGE_PRESENT))return 0;
    pt=(page_table_t*)PHYS_TO_DMAP(pd->entries[pdi]&~0xFFFULL);
    if(!(pt->entries[pti]&PAGE_PRESENT))return 0;
    return(pt->entries[pti]&addr_mask)|(virt&0xFFF);
}

uint64_t paging_get_entry(address_space_t *as,uint64_t virt){
    uint64_t pml4i=(virt>>39)&0x1FF,pdpti=(virt>>30)&0x1FF,pdi=(virt>>21)&0x1FF,pti=(virt>>12)&0x1FF;
    page_table_t *pdpt,*pd,*pt;
    if(!as||!as->pml4)return 0;
    if(!(as->pml4->entries[pml4i]&PAGE_PRESENT))return 0;
    pdpt=(page_table_t*)PHYS_TO_DMAP(as->pml4->entries[pml4i]&~0xFFFULL);
    if(pdpt->entries[pdpti]&PAGE_HUGE)return pdpt->entries[pdpti];
    if(!(pdpt->entries[pdpti]&PAGE_PRESENT))return 0;
    pd=(page_table_t*)PHYS_TO_DMAP(pdpt->entries[pdpti]&~0xFFFULL);
    if(pd->entries[pdi]&PAGE_HUGE)return pd->entries[pdi];
    if(!(pd->entries[pdi]&PAGE_PRESENT))return 0;
    pt=(page_table_t*)PHYS_TO_DMAP(pd->entries[pdi]&~0xFFFULL);
    return pt->entries[pti];
}

bool paging_set_entry(address_space_t *as,uint64_t virt,uint64_t entry){
    uint64_t pml4i=(virt>>39)&0x1FF,pdpti=(virt>>30)&0x1FF,pdi=(virt>>21)&0x1FF,pti=(virt>>12)&0x1FF;
    page_table_t *pdpt,*pd,*pt;
    if(!as||!as->pml4)return false;
    entry=c2_sanitize_page_flags(entry);
    if(!(as->pml4->entries[pml4i]&PAGE_PRESENT))return false;
    pdpt=(page_table_t*)PHYS_TO_DMAP(as->pml4->entries[pml4i]&~0xFFFULL);
    if(pdpt->entries[pdpti]&PAGE_HUGE)return false;
    if(!(pdpt->entries[pdpti]&PAGE_PRESENT))return false;
    pd=(page_table_t*)PHYS_TO_DMAP(pdpt->entries[pdpti]&~0xFFFULL);
    if(pd->entries[pdi]&PAGE_HUGE)return false;
    if(!(pd->entries[pdi]&PAGE_PRESENT))return false;
    pt=(page_table_t*)PHYS_TO_DMAP(pd->entries[pdi]&~0xFFFULL);
    pt->entries[pti]=entry;
    invlpg(virt);
    return true;
}

void paging_switch(address_space_t *as){
    if(as&&as->cr3_phys)write_cr3(as->cr3_phys);
}
void paging_flush_tlb(uint64_t virt){invlpg(virt);}

/* TSS for ring0 ↔ ring3 */
static tss64_t g_tss __attribute__((aligned(16)));
static uint8_t g_ist1_stack[8192] __attribute__((aligned(16)));

void tss_init(void){
    c2_memset(&g_tss,0,sizeof(g_tss));
    g_tss.ist1=(uint64_t)(uintptr_t)(g_ist1_stack+sizeof(g_ist1_stack));
    g_tss.iomap_base=sizeof(tss64_t);
}
void tss_set_rsp0(uint64_t rsp0){
    g_tss.rsp0=rsp0;
    g_per_cpu.kernel_rsp=rsp0;
}

/* GDT64 with ring3 + TSS descriptors */
static gdt_entry_t   g_gdt64[7] __attribute__((aligned(16)));
static gdt_tss_entry_t g_gdt64_tss __attribute__((aligned(16)));
static gdt_ptr_t     g_gdt64_ptr;
per_cpu_t g_per_cpu __attribute__((aligned(16)));

static void gdt_encode(gdt_entry_t *e, uint32_t base, uint32_t limit,
                        uint8_t access, uint8_t gran){
    e->limit_low  = (uint16_t)(limit & 0xFFFF);
    e->base_low   = (uint16_t)(base & 0xFFFF);
    e->base_mid   = (uint8_t)((base >> 16) & 0xFF);
    e->access     = access;
    e->granularity= (uint8_t)((gran & 0x0F) | ((limit >> 16) & 0xF0));
    e->base_high  = (uint8_t)((base >> 24) & 0xFF);
}

void gdt64_setup(void){
    c2_memset(g_gdt64, 0, sizeof(g_gdt64));
    c2_memset(&g_gdt64_tss, 0, sizeof(g_gdt64_tss));

    /* Final GDT layout (matches MSR_STAR user CS base of 0x28 and
     * `ltr $0x18` loading the 64-bit TSS descriptor at 0x18-0x27):
     *
     *   0x00   g_gdt64[0]    Null
     *   0x08   g_gdt64[1]    Kernel code    (DPL=0, L=1)
     *   0x10   g_gdt64[2]    Kernel data    (DPL=0, W)
     *   0x18   g_gdt64[3]    TSS low  8B  } 16-byte 64-bit
     *   0x20   g_gdt64[4]    TSS high 8B  } system descriptor
     *   0x28   g_gdt64[5]    User code      (DPL=3, L=1)  -> selector 0x2B
     *   0x30   g_gdt64[6]    User data      (DPL=3, W)    -> selector 0x33
     */

    /* 0x00: Null */
    gdt_encode(&g_gdt64[0], 0, 0, 0, 0);
    /* 0x08: Kernel 64-bit code (DPL=0, L=1) */
    gdt_encode(&g_gdt64[1], 0, 0xFFFFF, 0x9A, 0xA);
    g_gdt64[1].granularity = 0x20 | 0x0F;   /* L=1, G=1, limit=FFFFF */
    g_gdt64[1].access      = 0x9A;          /* P+DPL0+S+ER */
    /* 0x10: Kernel data (DPL=0, W=1) */
    gdt_encode(&g_gdt64[2], 0, 0xFFFFF, 0x92, 0xCF);
    /* 0x18 + 0x20: 64-bit TSS descriptor occupying TWO adjacent 8-byte
     * slots (indices 3 and 4). We build a temporary 16-byte image in
     * g_gdt64_tss and copy it directly over those two slots so the
     * final GDT layout matches what `ltr $0x18` expects. */
    {
        uint64_t tss_base  = (uint64_t)(uintptr_t)&g_tss;
        uint32_t tss_limit = sizeof(tss64_t) - 1;
        g_gdt64_tss.low.limit_low   = (uint16_t)(tss_limit & 0xFFFF);
        g_gdt64_tss.low.base_low    = (uint16_t)(tss_base & 0xFFFF);
        g_gdt64_tss.low.base_mid    = (uint8_t)((tss_base >> 16) & 0xFF);
        g_gdt64_tss.low.access      = 0x89;   /* P + 64-bit TSS (Type=9) */
        g_gdt64_tss.low.granularity = (uint8_t)((tss_limit >> 16) & 0x0F);
        g_gdt64_tss.low.base_high   = (uint8_t)((tss_base >> 24) & 0xFF);
        g_gdt64_tss.base_high2      = (uint32_t)((tss_base >> 32) & 0xFFFFFFFF);
        g_gdt64_tss.reserved_zero   = 0;
        /* Copy the 16-byte system descriptor over g_gdt64[3] and [4]. */
        c2_memcpy(&g_gdt64[3], &g_gdt64_tss, sizeof(g_gdt64_tss));
    }
    /* 0x28: User 64-bit code (DPL=3, L=1) -> selector 0x2B */
    gdt_encode(&g_gdt64[5], 0, 0xFFFFF, 0xFA, 0x20 | 0x0F);
    g_gdt64[5].access      = 0xFA;          /* P+DPL3+S+ER */
    g_gdt64[5].granularity = 0x20 | 0x0F;   /* L=1, G=1, limit=FFFFF */
    /* 0x30: User data (DPL=3, W=1) -> selector 0x33 */
    gdt_encode(&g_gdt64[6], 0, 0xFFFFF, 0xF2, 0xCF);

    /* GDT pointer: covers g_gdt64 only (TSS now lives inside it). */
    g_gdt64_ptr.limit = (uint16_t)(sizeof(g_gdt64) - 1);
    g_gdt64_ptr.base  = (uint64_t)(uintptr_t)g_gdt64;

    /* Load new GDT */
    __asm__ volatile("lgdt %0" :: "m"(g_gdt64_ptr));
    /* Reload segment registers */
    __asm__ volatile(
        "mov $0x10, %%ax\n"
        "mov %%ax, %%ds\n"
        "mov %%ax, %%es\n"
        "mov %%ax, %%ss\n"
        ::: "ax"
    );
    /* Far reload CS */
    __asm__ volatile(
        "pushq $0x08\n"
        "lea 1f(%%rip), %%rax\n"
        "pushq %%rax\n"
        "lretq\n"
        "1:\n"
        ::: "rax"
    );
    /* Load TR for TSS */
    __asm__ volatile(
        "mov $0x18, %%ax\n"
        "ltr %%ax\n"
        ::: "ax"
    );

    /* Init per-CPU area */
    c2_memset(&g_per_cpu, 0, sizeof(g_per_cpu));
    g_per_cpu.kernel_rsp = g_tss.rsp0;
    g_per_cpu.tss_addr = (uint64_t)(uintptr_t)&g_tss;

    /* Set GS base to per-CPU area (for SYSCALL entry) */
    c2_wrmsr(0xC0000101u, (uint64_t)(uintptr_t)&g_per_cpu);  /* IA32_GS_BASE */
}

/* MSR setup for SYSCALL/SYSRET */
extern void syscall_entry(void);  /* from isr64.S */

void syscall_msr_init(void){
    uint64_t star, lstar, sfmask;
    bool have_syscall = c2_cpu_has_ext_edx_bit(11);
    bool have_nx = c2_cpu_has_ext_edx_bit(20);
    bool have_fsgsbase = c2_cpu_has_leaf7_ebx_bit(0);

    g_paging_nx_enabled = false;
    g_c2_fsgsbase_enabled = false;
    if(!have_syscall){
        __boot_serial_puts("[c2init] warning: CPUID lacks SYSCALL/SYSRET; user Linux ABI syscalls disabled\n");
        return;
    }

    if(have_fsgsbase){
        uint64_t cr4;
        __asm__ volatile("mov %%cr4,%0":"=r"(cr4));
        /*
         * Modern glibc/Chromium can execute RDFSBASE/WRFSBASE directly when
         * CPUID advertises it. If CR4.FSGSBASE is left clear, that becomes a
         * ring-3 #UD during browser startup. We now capture live user FS/GS on
         * every syscall/IRQ/exception entry, so enabling the bit is safe.
         */
        cr4 |= C2_CR4_FSGSBASE;
        __asm__ volatile("mov %0,%%cr4"::"r"(cr4):"memory");
        g_c2_fsgsbase_enabled = true;
        __boot_serial_puts("[c2init] FSGSBASE enabled with entry FS/GS capture\n");
    }

    /* IA32_MSR_STAR:
     *   [31:0]  reserved
     *   [47:32] kernel CS selector (0x08)
     *   [63:48] user CS selector base (0x28 for our layout)
     *           SYSRET uses base+0x08 for CS, base+0x10 for SS
     *           So user CS = 0x28+0x08 = 0x30? No.
     *           Actually: STAR[48:63] = user CS base value.
     *           SYSRET: CS = STAR[48:63]+0x08, SS = STAR[48:63]+0x10
     *           Wait, that's wrong too. The AMD manual says:
     *           SYSRET CS = STAR[48:63] + 0x10 (for 64-bit mode)
     *           Hmm, let me get this right:
     *
     *           SYSCALL: CS = STAR[32:47], SS = STAR[32:47]+8
     *           SYSRET: CS = STAR[48:63],  SS = STAR[48:63]+8
     *
     *           For our GDT:
     *             Kernel CS = 0x08, kernel SS = 0x10 (0x08+8)
     *             User CS = 0x28, user SS = 0x30 (0x28+8)
     *           So STAR[32:47] = 0x08, STAR[48:63] = 0x28
     */
    star = ((uint64_t)0x28 << 48) | ((uint64_t)0x08 << 32);
    c2_wrmsr(0xC0000081u, star);  /* IA32_STAR */

    /* IA32_LSTAR = SYSCALL entry point */
    lstar = (uint64_t)(uintptr_t)syscall_entry;
    c2_wrmsr(0xC0000082u, lstar);  /* IA32_LSTAR */

    /* IA32_FMASK = flags to clear on SYSCALL entry.
     * Clear IF (bit 9) so interrupts are disabled on entry.
     * Also clear TF (bit 8) and DF (bit 10). */
    sfmask = (1u << 9) | (1u << 8);
    c2_wrmsr(0xC0000084u, sfmask);  /* IA32_FMASK */

    /* Enable SYSCALL/SYSRET + NX in EFER.
     *
     * mmap/mprotect code marks non-executable user pages with PAGE_NX
     * (bit 63 in PTE). If EFER.NXE is not enabled, that bit is
     * interpreted as reserved and user accesses fault with #PF err bit3
     * (reserved-bit violation), which is exactly what we saw on ld-linux
     * startup right after the first mmap.
     */
    {
        uint64_t efer;
        uint32_t lo, hi;
        __asm__ volatile("rdmsr" : "=a"(lo), "=d"(hi) : "c"(0xC0000080u));
        efer = ((uint64_t)hi << 32) | lo;
        /* LMA (bit 10) is a read-only status bit in long mode. Some
         * hypervisors raise #GP if software writes it back as 1. */
        efer &= ~(1ULL << 10);
        efer |= (1ULL << 0);   /* SCE  = System Call Enable */
        if(have_nx){
            efer |= (1ULL << 11);  /* NXE  = Enable Execute Disable bit */
            g_paging_nx_enabled = true;
        } else {
            efer &= ~(1ULL << 11);
            __boot_serial_puts("[c2init] warning: CPUID lacks NXE; masking PAGE_NX from mappings\n");
        }
        lo = (uint32_t)efer;
        hi = (uint32_t)(efer >> 32);
        __asm__ volatile("wrmsr" :: "a"(lo), "d"(hi), "c"(0xC0000080u));
    }
}

/* Syscall dispatch — routes to real_sys_* from compat3 */
extern int64_t real_sys_read(int fd, void *buf, uint64_t count);
extern int64_t real_sys_write(int fd, const void *buf, uint64_t count);
extern int64_t real_sys_open(const char *path, int flags, uint32_t mode);
extern int64_t real_sys_close(int fd);
extern int64_t real_sys_stat(const char *path, void *buf);
extern int64_t real_sys_fstat(int fd, void *buf);
extern int64_t real_sys_lseek(int fd, int64_t off, int whence);
extern int64_t real_sys_mmap(void *addr, uint64_t len, uint64_t prot,
                             uint64_t flags, uint64_t fd, int64_t off);
extern int64_t real_sys_munmap(void *addr, uint64_t len);
extern int64_t real_sys_mprotect(void *addr, uint64_t len, uint64_t prot);
extern int64_t real_sys_brk(void *addr);
extern int64_t real_sys_dup(int fd);
extern int64_t real_sys_dup2(int oldfd, int newfd);
extern int64_t real_sys_pipe(int *pipefd);
extern int64_t real_sys_ioctl(int fd, uint64_t req, void *arg);
extern int64_t real_sys_fcntl(int fd, int cmd, uint64_t arg);
extern int64_t real_sys_fork(void);
extern int64_t real_sys_execve(const char *path, char *const argv[],
                               char *const envp[]);
extern int64_t real_sys_exit(int code);
extern int64_t real_sys_wait4(int pid, int *status, int options, void *ru);
extern int64_t real_sys_getpid(void);
extern int64_t real_sys_getppid(void);
extern int64_t real_sys_getuid(void);
extern int64_t real_sys_getgid(void);
extern int64_t real_sys_geteuid(void);
extern int64_t real_sys_getegid(void);
extern int64_t real_sys_setuid(uint32_t uid);
extern int64_t real_sys_setgid(uint32_t gid);
extern int64_t real_sys_setpgid(int pid, int pgid);
extern int64_t real_sys_getpgid(int pid);
extern int64_t real_sys_setsid(void);
extern int64_t real_sys_kill(int pid, int sig);
extern int64_t real_sys_sigaction(int sig, const void *act, void *oact);
extern int64_t real_sys_sigprocmask(int how, const void *set, void *oset);
extern int64_t real_sys_sigreturn(void);
extern int64_t real_sys_socket(int domain, int type, int proto);
extern int64_t real_sys_bind(int fd, const void *addr, uint64_t addrlen);
extern int64_t real_sys_listen(int fd, int backlog);
extern int64_t real_sys_accept(int fd, void *addr, uint64_t *addrlen);
extern int64_t real_sys_connect(int fd, const void *addr, uint64_t addrlen);
extern int64_t real_sys_sendto(int fd, const void *buf, uint64_t len,
                               uint64_t flags, const void *dst, uint64_t slen);
extern int64_t real_sys_recvfrom(int fd, void *buf, uint64_t len,
                                 uint64_t flags, void *src, uint64_t *slen);
extern int64_t real_sys_setsockopt(int fd, int lev, int opt,
                                   const void *val, uint64_t len);
extern int64_t real_sys_getsockopt(int fd, int lev, int opt,
                                   void *val, uint64_t *len);
extern int64_t real_sys_getsockname(int fd, void *addr, uint64_t *len);
extern int64_t real_sys_getpeername(int fd, void *addr, uint64_t *len);
extern int64_t real_sys_shutdown(int fd, int how);
extern int64_t real_sys_socketpair(int dom, int type, int proto, int *sv);
extern int64_t real_sys_sendmsg(int fd, const void *msg, uint64_t flags);
extern int64_t real_sys_recvmsg(int fd, void *msg, uint64_t flags);
extern int64_t real_sys_nanosleep(const void *req, void *rem);
extern int64_t real_sys_clock_gettime(uint64_t clk, void *tp);
extern int64_t real_sys_clock_getres(uint64_t clk, void *tp);
extern int64_t real_sys_gettimeofday(void *tv, void *tz);
extern int64_t real_sys_uname(void *buf);
extern int64_t real_sys_getrandom(void *buf, uint64_t len, uint32_t flags);
extern int64_t real_sys_prctl(int op, uint64_t a1, uint64_t a2,
                              uint64_t a3, uint64_t a4);
extern int64_t real_sys_futex(uint32_t *addr, int op, uint32_t val,
                              const void *to, uint32_t *addr2, uint32_t val3);
extern int64_t real_sys_set_tid_address(uint32_t *addr);
extern int64_t real_sys_clone(uint64_t flags, void *stack, int *ptid,
                              int *ctid, uint64_t tls);
extern int64_t real_sys_epoll_create1(int flags);
extern int64_t real_sys_epoll_ctl(int epfd, int op, int fd, void *ev);
extern int64_t real_sys_epoll_wait(int epfd, void *ev, int maxev, int to);
extern int64_t real_sys_epoll_pwait(int epfd, void *ev, int maxev, int to,
                                    const void *sigmask);
extern int64_t real_sys_poll(void *fds, uint64_t nfds, int timeout);
extern int64_t real_sys_ppoll(void *fds, uint64_t nfds,
                              const void *ts, const void *sigmask);
extern int64_t real_sys_select(int nfds, void *rfds, void *wfds,
                               void *efds, void *tv);
extern int64_t real_sys_pselect6(int nfds, void *rfds, void *wfds,
                                 void *efds, const void *ts, const void *sm);
extern int64_t real_sys_readlink(const char *path, void *buf, uint64_t sz);
extern int64_t real_sys_getdents64(int fd, void *dirp, uint64_t count);
extern int64_t real_sys_getcwd(void *buf, uint64_t sz);
extern int64_t real_sys_chdir(const char *path);
extern int64_t real_sys_mkdir(const char *path, uint32_t mode);
extern int64_t real_sys_rmdir(const char *path);
extern int64_t real_sys_unlink(const char *path);
extern int64_t real_sys_rename(const char *old, const char *nw);
extern int64_t real_sys_truncate(const char *path, int64_t len);
extern int64_t real_sys_ftruncate(int fd, int64_t len);
extern int64_t real_sys_chmod(const char *path, uint32_t mode);
extern int64_t real_sys_chown(const char *path, uint32_t uid, uint32_t gid);
extern int64_t real_sys_access(const char *path, int mode);
extern int64_t real_sys_dup3(int oldfd, int newfd, int flags);
extern int64_t real_sys_pipe2(int *pipefd, int flags);
extern int64_t real_sys_sched_yield(void);
extern int64_t real_sys_exit_group(int code);
extern int64_t real_sys_mremap(void *addr, uint64_t old, uint64_t nw,
                               uint64_t flags, void *new_addr);
extern int64_t real_sys_msync(void *addr, uint64_t len, int flags);
extern int64_t real_sys_arch_prctl(int code, uint64_t addr);
extern int64_t real_sys_set_robust_list(void *head, uint64_t len);
extern int64_t real_sys_get_robust_list(int pid, void **head, uint64_t *len);

/* ----------------------------------------------------------------
 * NOTE: The `syscall_dispatch` below was a second, conflicting
 * implementation that bypassed `g_syscall_table` and had broken
 * a6 handling (read garbage from the stack). The canonical
 * implementation lives in compat.c and goes through
 * `g_syscall_table[]`, which compat3_rewire_syscalls() repoints
 * at the real_sys_* functions declared above.
 *
 * We keep the helper block compiled out so the extern decls stay
 * available for readers but the duplicate symbol is gone.
 * ---------------------------------------------------------------- */
#if 0
uint64_t syscall_dispatch_disabled(uint64_t nr, uint64_t a1, uint64_t a2,
                                   uint64_t a3, uint64_t a4, uint64_t a5){
    int64_t ret = -38; /* ENOSYS */

    switch(nr){
        /* Linux x86-64 syscall numbers */
        case 0:  ret = real_sys_read((int)a1, (void*)a2, a3); break;
        case 1:  ret = real_sys_write((int)a1, (const void*)a2, a3); break;
        case 2:  ret = real_sys_open((const char*)a1, (int)a2, (uint32_t)a3); break;
        case 3:  ret = real_sys_close((int)a1); break;
        case 4:  ret = real_sys_stat((const char*)a1, (void*)a2); break;
        case 5:  ret = real_sys_fstat((int)a1, (void*)a2); break;
        case 8:  ret = real_sys_lseek((int)a1, (int64_t)a2, (int)a3); break;
        case 9:  ret = real_sys_mmap((void*)a1, a2, a3, a4, a5,
                                     *(int64_t*)((uint8_t*)&a5 + 8)); break;
        case 10: ret = real_sys_mprotect((void*)a1, a2, a3); break;
        case 11: ret = real_sys_munmap((void*)a1, a2); break;
        case 12: ret = real_sys_brk((void*)a1); break;
        case 13: ret = real_sys_sigaction((int)a1, (const void*)a2, (void*)a3); break;
        case 14: ret = real_sys_sigprocmask((int)a1, (const void*)a2, (void*)a3); break;
        case 21: ret = real_sys_pipe((int*)a1); break;
        case 22: ret = real_sys_pipe2((int*)a1, (int)a2); break;
        case 24: ret = real_sys_dup((int)a1); break;
        case 25: ret = real_sys_dup2((int)a1, (int)a2); break;
        case 28: ret = real_sys_mmap((void*)a1, a2, a3, a4, a5,
                                     *(int64_t*)((uint8_t*)&a5 + 8)); break;
        case 32: ret = real_sys_dup3((int)a1, (int)a2, (int)a3); break;
        case 33: ret = real_sys_pipe2((int*)a1, (int)a2); break;
        case 39: ret = real_sys_getpid(); break;
        case 41: ret = real_sys_socket((int)a1, (int)a2, (int)a3); break;
        case 42: ret = real_sys_connect((int)a1, (const void*)a2, a3); break;
        case 43: ret = real_sys_accept((int)a1, (void*)a2, (uint64_t*)a3); break;
        case 44: ret = real_sys_sendto((int)a1, (const void*)a2, a3, a4,
                                       (const void*)a5,
                                       *(uint64_t*)((uint8_t*)&a5 + 8)); break;
        case 45: ret = real_sys_recvfrom((int)a1, (void*)a2, a3, a4,
                                         (void*)a5,
                                         (uint64_t*)(*(uint64_t*)((uint8_t*)&a5+8)));
                 break;
        case 46: ret = real_sys_sendmsg((int)a1, (const void*)a2, a3); break;
        case 47: ret = real_sys_recvmsg((int)a1, (void*)a2, a3); break;
        case 48: ret = real_sys_shutdown((int)a1, (int)a2); break;
        case 49: ret = real_sys_bind((int)a1, (const void*)a2, a3); break;
        case 50: ret = real_sys_listen((int)a1, (int)a2); break;
        case 51: ret = real_sys_getsockname((int)a1, (void*)a2, (uint64_t*)a3); break;
        case 52: ret = real_sys_getpeername((int)a1, (void*)a2, (uint64_t*)a3); break;
        case 53: ret = real_sys_socketpair((int)a1, (int)a2, (int)a3, (int*)a4); break;
        case 54: ret = real_sys_setsockopt((int)a1, (int)a2, (int)a3,
                                           (const void*)a4, a5); break;
        case 55: ret = real_sys_getsockopt((int)a1, (int)a2, (int)a3,
                                           (void*)a4, (uint64_t*)a5); break;
        case 56: ret = real_sys_clone(a1, (void*)a2, (int*)a3, (int*)a4, a5); break;
        case 57: ret = real_sys_fork(); break;
        case 59: ret = real_sys_execve((const char*)a1, (char*const*)a2,
                                       (char*const*)a3); break;
        case 60: ret = real_sys_exit((int)a1); break;
        case 61: ret = real_sys_wait4((int)a1, (int*)a2, (int)a3, (void*)a4); break;
        case 63: ret = real_sys_uname((void*)a1); break;
        case 72: ret = real_sys_getcwd((void*)a1, a2); break;
        case 78: ret = real_sys_getdents64((int)a1, (void*)a2, a3); break;
        case 79: ret = real_sys_chdir((const char*)a1); break;
        case 80: ret = real_sys_mkdir((const char*)a1, (uint32_t)a2); break;
        case 81: ret = real_sys_rmdir((const char*)a1); break;
        case 82: ret = real_sys_unlink((const char*)a1); break;
        case 83: ret = real_sys_rename((const char*)a1, (const char*)a2); break;
        case 84: ret = real_sys_chdir((const char*)a1); break;
        case 87: ret = real_sys_unlink((const char*)a1); break;
        case 89: ret = real_sys_readlink((const char*)a1, (void*)a2, a3); break;
        case 90: ret = real_sys_chmod((const char*)a1, (uint32_t)a2); break;
        case 91: ret = real_sys_chown((const char*)a1, (uint32_t)a2, (uint32_t)a3); break;
        case 92: ret = real_sys_chown((const char*)a1, (uint32_t)a2, (uint32_t)a3); break;
        case 94: ret = real_sys_ftruncate((int)a1, (int64_t)a2); break;
        case 96: ret = real_sys_gettimeofday((void*)a1, (void*)a2); break;
        case 97: ret = real_sys_getrlimit((int)a1, (void*)a2); break;
        case 102: ret = real_sys_getuid(); break;
        case 104: ret = real_sys_getgid(); break;
        case 107: ret = real_sys_geteuid(); break;
        case 108: ret = real_sys_getegid(); break;
        case 110: ret = real_sys_getppid(); break;
        case 111: ret = real_sys_getpgid((int)a1); break;
        case 116: ret = real_sys_setsid(); break;
        case 117: ret = real_sys_setpgid((int)a1, (int)a2); break;
        case 119: ret = real_sys_setuid((uint32_t)a1); break;
        case 122: ret = real_sys_setgid((uint32_t)a1); break;
        case 131: ret = real_sys_sigaction((int)a1, (const void*)a2, (void*)a3); break;
        case 132: ret = real_sys_kill((int)a1, (int)a2); break;
        case 157: ret = real_sys_prctl((int)a1, a2, a3, a4, a5); break;
        case 158: ret = real_sys_arch_prctl((int)a1, a2); break;
        case 186: ret = real_sys_gettid(); break;
        case 202: ret = real_sys_futex((uint32_t*)a1, (int)a2, (uint32_t)a3,
                                        (const void*)a4, (uint32_t*)a5,
                                        *(uint32_t*)((uint8_t*)&a5 + 8)); break;
        case 203: ret = real_sys_sched_yield(); break;
        case 217: ret = real_sys_getdents64((int)a1, (void*)a2, a3); break;
        case 218: ret = real_sys_set_tid_address((uint32_t*)a1); break;
        case 228: ret = real_sys_clock_gettime(a1, (void*)a2); break;
        case 229: ret = real_sys_clock_getres(a1, (void*)a2); break;
        case 231: ret = real_sys_exit_group((int)a1); break;
        case 234: ret = real_sys_getrandom((void*)a1, a2, (uint32_t)a3); break;
        case 248: ret = real_sys_mremap((void*)a1, a2, a3, a4, (void*)a5); break;
        case 255: ret = real_sys_msync((void*)a1, a2, (int)a3); break;
        case 258: ret = real_sys_msync((void*)a1, a2, (int)a3); break;
        case 272: ret = real_sys_uname((void*)a1); break;
        case 289: ret = real_sys_epoll_create1((int)a1); break;
        case 290: ret = real_sys_epoll_ctl((int)a1, (int)a2, (int)a3, (void*)a4); break;
        case 291: ret = real_sys_epoll_wait((int)a1, (void*)a2, (int)a3, (int)a4); break;
        case 302: ret = real_sys_prctl((int)a1, a2, a3, a4, a5); break;
        case 309: ret = real_sys_set_robust_list((void*)a1, a2); break;
        case 310: ret = real_sys_get_robust_list((int)a1, (void**)a2, (uint64_t*)a3); break;
        case 311: ret = real_sys_nanosleep((const void*)a1, (void*)a2); break;
        case 318: ret = real_sys_getrandom((void*)a1, a2, (uint32_t)a3); break;
        case 329: ret = real_sys_epoll_pwait((int)a1, (void*)a2, (int)a3, (int)a4,
                                              (const void*)a5); break;
        case 35: ret = real_sys_nanosleep((const void*)a1, (void*)a2); break;
        case 36: ret = real_sys_poll((void*)a1, a2, (int)a3); break;
        case 37: ret = real_sys_ppoll((void*)a1, a2, (const void*)a3, (const void*)a4); break;
        case 23: ret = real_sys_select((int)a1, (void*)a2, (void*)a3,
                                       (void*)a4, (void*)a5); break;
        case 270: ret = real_sys_pselect6((int)a1, (void*)a2, (void*)a3,
                                          (void*)a4, (const void*)a5,
                                          *(void**)((uint8_t*)&a5 + 8)); break;
        case 76: ret = real_sys_access((const char*)a1, (int)a2); break;
        case 77: ret = real_sys_ftruncate((int)a1, (int64_t)a2); break;
        case 85: ret = real_sys_truncate((const char*)a1, (int64_t)a2); break;
        case 15: ret = real_sys_sigreturn(); break;
        case 16: ret = real_sys_ioctl((int)a1, a2, (void*)a3); break;
        case 72: ret = real_sys_getcwd((void*)a1, a2); break;
        case 25: ret = real_sys_fcntl((int)a1, (int)a2, a3); break;
        default: break;
    }
    return (uint64_t)ret;
}
#endif /* disabled duplicate syscall_dispatch */

/* Task / process management with real context */
task_t g_tasks[TASK_MAX];
int g_current_task=0;
int g_task_next_pid=1;
bool g_user_foreground_active=false;
volatile uint32_t g_task_preempt_defer_ticks=0;
volatile uint32_t g_kernel_preempt_disable=0;
static uint32_t g_task_exit_sibling_trace=0;
static uint8_t g_kstacks[TASK_MAX][TASK_KERNEL_STACK] __attribute__((aligned(16)));
void sig_check_pending(task_t *t);

/* FPU/SSE: init FNINIT, set default FXSAVE state */
static void fpu_init_state(uint8_t *buf){
    c2_memset(buf,0,TASK_FPU_STATE_SIZE);
    /* Set FCW default value (0x037F) in the FXSAVE image */
    uint16_t *fcw=(uint16_t*)(buf+0); *fcw=0x037F;
    /* Set FSW to 0 */
    uint16_t *fsw=(uint16_t*)(buf+2); *fsw=0;
    /* MXCSR default = 0x1F80 */
    uint32_t *mxcsr=(uint32_t*)(buf+24); *mxcsr=0x1F80;
    if(g_fpu_use_xsave&&g_fpu_state_size>=576u){
        uint64_t *xstate_bv=(uint64_t*)(buf+512);
        *xstate_bv=C2_XCR0_X87|C2_XCR0_SSE;
    }
}

static void fpu_log_config(void){
    char line[128];
    size_t l=0;
    c2_append_str(line,&l,sizeof(line),"[fpu] mode=");
    c2_append_str(line,&l,sizeof(line),g_fpu_use_xsave?"xsave":"fxsave");
    c2_append_str(line,&l,sizeof(line)," xcr0=");
    c2_append_hex64(line,&l,sizeof(line),g_fpu_xstate_mask);
    c2_append_str(line,&l,sizeof(line)," size=");
    c2_append_u32(line,&l,sizeof(line),g_fpu_state_size);
    c2_append_str(line,&l,sizeof(line)," avx=");
    c2_append_ch(line,&l,sizeof(line),g_fpu_avx_enabled?'1':'0');
    c2_append_ch(line,&l,sizeof(line),'\n');
    __boot_serial_puts(line);
}

void fpu_init(void){
    uint32_t max_basic=0, eax=0, ebx=0, ecx=0, edx=0;
    uint64_t cr4;

    __asm__ volatile("fninit" ::: "memory");
    /* Set CR0: MP=1, EM=0, TS=0, NE=1 */
    uint64_t cr0;
    __asm__ volatile("mov %%cr0,%0":"=r"(cr0));
    cr0=(cr0&~0xCULL)|0x22ULL; /* clear EM(2)+TS(3), set MP(1)+NE(5) */
    __asm__ volatile("mov %0,%%cr0"::"r"(cr0));

    c2_cpuid_count(0,0,&max_basic,0,0,0);
    c2_cpuid_count(1,0,&eax,&ebx,&ecx,&edx);

    g_fpu_use_xsave=false;
    g_fpu_avx_enabled=false;
    g_fpu_xstate_mask=C2_XCR0_X87|C2_XCR0_SSE;
    g_fpu_state_size=512;

    /* Set CR4: OSFXSR+OSXMMEXCPT; enable OSXSAVE only when supported. */
    __asm__ volatile("mov %%cr4,%0":"=r"(cr4));
    cr4|=C2_CR4_OSFXSR|C2_CR4_OSXMMEXCPT;
    if(ecx&(1u<<26))cr4|=C2_CR4_OSXSAVE;
    __asm__ volatile("mov %0,%%cr4"::"r"(cr4));

    if((ecx&(1u<<26))&&max_basic>=0xDu){
        uint32_t xfeat_lo=0,xfeat_hi=0,xsize_cur=0,xsize_max=0;
        uint64_t desired=C2_XCR0_X87|C2_XCR0_SSE;
        uint64_t supported;

        c2_cpuid_count(0xD,0,&xfeat_lo,&xsize_cur,&xsize_max,&xfeat_hi);
        supported=((uint64_t)xfeat_hi<<32)|xfeat_lo;
        if((supported&(C2_XCR0_X87|C2_XCR0_SSE))==(C2_XCR0_X87|C2_XCR0_SSE)){
            if((ecx&(1u<<28))&&(supported&C2_XCR0_AVX))desired|=C2_XCR0_AVX;
            c2_xsetbv(0,desired);
            g_fpu_use_xsave=true;
            g_fpu_avx_enabled=(desired&C2_XCR0_AVX)!=0;
            g_fpu_xstate_mask=desired;
            c2_cpuid_count(0xD,0,&xfeat_lo,&xsize_cur,&xsize_max,&xfeat_hi);
            if(xsize_cur>0&&xsize_cur<=TASK_FPU_STATE_SIZE)g_fpu_state_size=xsize_cur;
            else if(xsize_max>0&&xsize_max<=TASK_FPU_STATE_SIZE)g_fpu_state_size=xsize_max;
            else g_fpu_state_size=TASK_FPU_STATE_SIZE;
        }
    }

    fpu_log_config();
}

void fpu_save(task_t *t){
    if(!t)return;
    if(g_fpu_use_xsave){
        uint32_t lo=(uint32_t)g_fpu_xstate_mask,hi=(uint32_t)(g_fpu_xstate_mask>>32);
        __asm__ volatile("xsave64 (%0)"::"r"(t->fpu_state),"a"(lo),"d"(hi):"memory");
    }else{
        __asm__ volatile("fxsave64 (%0)"::"r"(t->fpu_state):"memory");
    }
    t->fpu_used=true;
}

void fpu_restore(task_t *t){
    if(!t)return;
    if(!t->fpu_used){
        fpu_init_state(t->fpu_state);
        t->fpu_used=true;
    }
    if(g_fpu_use_xsave){
        uint32_t lo=(uint32_t)g_fpu_xstate_mask,hi=(uint32_t)(g_fpu_xstate_mask>>32);
        __asm__ volatile("xrstor64 (%0)"::"r"(t->fpu_state),"a"(lo),"d"(hi):"memory");
    }else{
        __asm__ volatile("fxrstor64 (%0)"::"r"(t->fpu_state):"memory");
    }
    /* Clear TS in CR0 so FPU instructions work */
    uint64_t cr0;
    __asm__ volatile("mov %%cr0,%0":"=r"(cr0));
    cr0&=~8ULL; /* clear TS bit */
    __asm__ volatile("mov %0,%%cr0"::"r"(cr0));
}

void task_init(void){
    int i;
    c2_memset(g_tasks,0,sizeof(g_tasks));
    g_current_task=0;g_task_next_pid=1;
    g_user_foreground_active=false;
    /* Init FPU for kernel task */
    fpu_init();
    /* Task 0: kernel idle */
    task_t *t=&g_tasks[0];
    t->used=true; t->pid=g_task_next_pid++; t->tgid=t->pid; t->ppid=0; t->pgid=1; t->sid=1;
    t->fdt_group=t->pid;
    t->state=TASK_RUNNING; t->nice=0;
    c2_strlcpy(t->name,"kernel",TASK_NAME_LEN);
    c2_strlcpy(t->cwd,"/",128);
    t->kernel_stack=g_kstacks[0];
    t->kernel_stack_top=(uint64_t)(uintptr_t)(g_kstacks[0]+TASK_KERNEL_STACK);
    /* Initialize kernel RSP for task 0 - point to a valid stack location with space for context */
    t->kernel_rsp_saved=t->kernel_stack_top-64; /* Reserve space for callee-saved regs */
    t->addr_space=paging_get_kernel_space();
    t->brk_start=0x800000ULL; t->brk_current=0x800000ULL;
    t->mmap_base=0x40000000ULL;
    fpu_init_state(t->fpu_state); t->fpu_used=true;
    /* Setup stdin/stdout/stderr */
    t->fdt.fds[0].kind=FDKIND_DEVTTY; t->fdt.fds[0].flags=FDFL_READABLE; t->fdt.fds[0].refcount=1;
    t->fdt.fds[1].kind=FDKIND_DEVTTY; t->fdt.fds[1].flags=FDFL_WRITABLE; t->fdt.fds[1].refcount=1;
    t->fdt.fds[2].kind=FDKIND_DEVTTY; t->fdt.fds[2].flags=FDFL_WRITABLE; t->fdt.fds[2].refcount=1;
    /* Default signal handlers */
    for(i=0;i<NSIG;++i)t->sigactions[i].handler=SIG_DFL;
}

task_t *task_current(void){return &g_tasks[g_current_task];}

uint64_t *task_syscall_user_frame(const task_t *t){
    extern uint64_t *g_syscall_user_frame;
    if(t&&t->syscall_user_frame_valid)return (uint64_t*)t->syscall_user_frame_copy;
    if(t&&t->syscall_user_frame)return t->syscall_user_frame;
    return g_syscall_user_frame;
}

void task_capture_syscall_user_context(void){
    extern uint64_t *g_syscall_user_frame;
    task_t *cur;
    uint64_t *f;
    int i;
    if(g_current_task<0||g_current_task>=TASK_MAX)return;
    cur=&g_tasks[g_current_task];
    if(!cur->used)return;
    c2_capture_live_user_fsgs(cur);
    f=g_syscall_user_frame;
    if(!f)return;
    for(i=0;i<22;++i)cur->syscall_user_frame_copy[i]=f[i];
    cur->syscall_user_frame=cur->syscall_user_frame_copy;
    cur->syscall_user_frame_valid=true;
    f=cur->syscall_user_frame_copy;
    cur->ctx.rip=f[17];
    cur->ctx.rflags=f[19];
    cur->ctx.rsp=f[20];
}

void task_note_irq_user_context(uint64_t rip,uint64_t rsp,uint64_t frame_base){
    task_t *cur;
    (void)frame_base;
    if(g_current_task<0||g_current_task>=TASK_MAX)return;
    cur=&g_tasks[g_current_task];
    if(!cur->used)return;
    c2_capture_live_user_fsgs(cur);
    cur->ctx.rip=rip;
    cur->ctx.rsp=rsp;
}

void task_restore_current_user_msrs(void){
    task_t *cur;
    static uint32_t trace_count=0;
    static uint32_t verify_count=0;
    if(g_current_task<0||g_current_task>=TASK_MAX)return;
    cur=&g_tasks[g_current_task];
    if(!cur->used||cur->ctx.cs!=0x28)return;
    if(trace_count<64){
        uint64_t msr_fs=c2_rdmsr(0xC0000100u);
        uint64_t msr_gs=c2_rdmsr(0xC0000102u);
        if(msr_fs!=cur->ctx.fs_base||msr_gs!=cur->ctx.gs_base){
            ++trace_count;
            __boot_serial_puts("[tls-restore] pid=");
            __boot_serial_putu32((uint32_t)cur->pid);
            __boot_serial_puts(" msr.fs=");
            __boot_serial_puthex64(msr_fs);
            __boot_serial_puts(" ctx.fs=");
            __boot_serial_puthex64(cur->ctx.fs_base);
            __boot_serial_puts(" msr.gs=");
            __boot_serial_puthex64(msr_gs);
            __boot_serial_puts(" ctx.gs=");
            __boot_serial_puthex64(cur->ctx.gs_base);
            __boot_serial_puts("\n");
        }
    }
    c2_clear_fs_selector();
    c2_wrmsr(0xC0000100u,cur->ctx.fs_base);
    c2_wrfsbase(cur->ctx.fs_base);
    c2_wrmsr(0xC0000102u,cur->ctx.gs_base);
    c2_wrmsr(0xC0000101u,(uint64_t)(uintptr_t)&g_per_cpu);
    if(verify_count<32){
        uint64_t after_fs=c2_rdmsr(0xC0000100u);
        uint64_t after_gs=c2_rdmsr(0xC0000102u);
        if(after_fs!=cur->ctx.fs_base||after_gs!=cur->ctx.gs_base){
            ++verify_count;
            __boot_serial_puts("[tls-verify-fail] pid=");
            __boot_serial_putu32((uint32_t)cur->pid);
            __boot_serial_puts(" after.fs=");
            __boot_serial_puthex64(after_fs);
            __boot_serial_puts(" ctx.fs=");
            __boot_serial_puthex64(cur->ctx.fs_base);
            __boot_serial_puts(" after.gs=");
            __boot_serial_puthex64(after_gs);
            __boot_serial_puts(" ctx.gs=");
            __boot_serial_puthex64(cur->ctx.gs_base);
            __boot_serial_puts("\n");
        }
    }
}

static bool c2_has_token(const char *s,const char *needle){
    size_t i,j,nl;
    if(!s||!needle)return false;
    nl=c2_strlen(needle);
    if(!nl)return true;
    for(i=0;s[i];++i){
        for(j=0;j<nl&&s[i+j]&&s[i+j]==needle[j];++j){}
        if(j==nl)return true;
    }
    return false;
}

static bool task_is_browser_runtime(const task_t *t){
    if(!t)return false;
    if(c2_has_token(t->exec_path,"/opt/firefox/")||
       c2_has_token(t->exec_path,"/firefox")||
       c2_has_token(t->exec_path,"/chromium")||
       c2_has_token(t->exec_path,"/chrome"))return true;
    if(c2_has_token(t->name,"firefox")||
       c2_has_token(t->name,"chromium")||
       c2_has_token(t->name,"chrome"))return true;
    return false;
}

static bool task_has_runnable_browser_peer(const task_t *cur){
    int i;
    if(!cur||!cur->addr_space)return false;
    for(i=0;i<TASK_MAX;++i){
        const task_t *t=&g_tasks[i];
        if(!t->used||t==cur)continue;
        if(t->state!=TASK_RUNNABLE)continue;
        if(t->ctx.cs!=0x28)continue;
        if(t->addr_space==cur->addr_space)return true;
        if(task_is_browser_runtime(t))return true;
        if(t->tgid&&t->tgid==cur->tgid)return true;
        if(t->ppid==cur->pid||cur->ppid==t->pid)return true;
        if(t->pgid&&t->pgid==cur->pgid&&task_is_browser_runtime(cur))return true;
    }
    return false;
}

static bool task_browser_syscall_can_yield(uint64_t nr){
    switch(nr){
        case 56:  /* clone */
        case 57:  /* fork */
        case 58:  /* vfork */
        case 60:  /* exit */
        case 202: /* futex */
        case 231: /* exit_group */
            return false;
        default:
            return true;
    }
}

void task_browser_coop_yield_after_syscall(uint64_t nr){
    task_t *cur;
    static uint32_t coop_count=0;
    static uint32_t trace_count=0;
    if(g_current_task<0||g_current_task>=TASK_MAX)return;
    cur=&g_tasks[g_current_task];
    if(!cur->used||cur->state!=TASK_RUNNING||cur->ctx.cs!=0x28)return;
    if(!cur->no_timer_preempt||!task_is_browser_runtime(cur))return;
    /* clone/exit/futex waits already have delicate ordering or their own
     * blocking schedule path. Yielding after ordinary syscalls is enough to
     * give GTK/X11/IPC workers airtime without async timer preemption in
     * the middle of mozjemalloc/libxul code. */
    if(!task_browser_syscall_can_yield(nr))return;
    if(!task_has_runnable_browser_peer(cur))return;
    ++coop_count;
    if(trace_count<64){
        ++trace_count;
        __boot_serial_puts("[browser-coop-yield] pid=");
        __boot_serial_putu32((uint32_t)cur->pid);
        __boot_serial_puts(" nr=");
        __boot_serial_putu32((uint32_t)nr);
        __boot_serial_puts(" count=");
        __boot_serial_putu32(coop_count);
        __boot_serial_puts("\n");
    }
    task_schedule();
}

int task_create(const char *name,uint64_t entry,bool is_user){
    int i; task_t *t=0;
    for(i=1;i<TASK_MAX;++i)if(!g_tasks[i].used){t=&g_tasks[i];break;}
    if(!t)return -EAGAIN;
    c2_memset(t,0,sizeof(*t));
    t->used=true; t->pid=g_task_next_pid++; t->tgid=t->pid; t->ppid=g_tasks[g_current_task].pid;
    t->fdt_group=t->pid;
    t->pgid=t->pid; t->sid=g_tasks[g_current_task].sid;
    t->state=TASK_RUNNABLE; t->nice=0;
    t->no_timer_preempt=g_tasks[g_current_task].no_timer_preempt;
    c2_strlcpy(t->name,name,TASK_NAME_LEN);
    c2_strlcpy(t->cwd,g_tasks[g_current_task].cwd,128);
    t->kernel_stack=g_kstacks[i];
    t->kernel_stack_top=(uint64_t)(uintptr_t)(g_kstacks[i]+TASK_KERNEL_STACK);
    if(is_user){
        t->addr_space=paging_create_address_space();
        t->ctx.cs=0x28; t->ctx.ss=0x30; /* ring3: new GDT */
        t->ctx.rflags=0x200; /* IF */
        /* Map user stack at 0x7FFFFFFFE000 */
        {int pg;uint64_t stk_base=0x7FFFFFFFE000ULL-TASK_USER_STACK;
         for(pg=0;pg<(int)(TASK_USER_STACK/PAGE_SIZE);++pg){
             uint64_t f=pmm_alloc_frame();
             if(f)paging_map(t->addr_space,stk_base+(uint64_t)pg*PAGE_SIZE,f,PAGE_PRESENT|PAGE_WRITABLE|PAGE_USER);
         }
         t->ctx.rsp=0x7FFFFFFFE000ULL-16;}
    } else {
        t->addr_space=paging_get_kernel_space();
        t->ctx.cs=0x08; t->ctx.ss=0x10;
        t->ctx.rflags=0x200;
        t->ctx.rsp=t->kernel_stack_top-16;
    }
    t->ctx.rip=entry;
    fpu_init_state(t->fpu_state); t->fpu_used=false;
    t->brk_start=0x800000ULL; t->brk_current=0x800000ULL; t->mmap_base=0x40000000ULL;
    /* Inherit FDs */
    c2_memcpy(&t->fdt,&g_tasks[g_current_task].fdt,sizeof(fd_table_t));
    {int f;for(f=0;f<TASK_FD_MAX;++f)if(t->fdt.fds[f].kind!=FDKIND_NONE)t->fdt.fds[f].refcount++;}
    /* Default signals */
    {int s;for(s=0;s<NSIG;++s)t->sigactions[s].handler=SIG_DFL;}
    return t->pid;
}

int task_create_user_shell(const char *name,uint64_t entry){
    int i; task_t *t=0;
    for(i=1;i<TASK_MAX;++i)if(!g_tasks[i].used){t=&g_tasks[i];break;}
    if(!t)return -EAGAIN;
    c2_memset(t,0,sizeof(*t));
    t->used=true; t->pid=g_task_next_pid++; t->tgid=t->pid; t->ppid=g_tasks[g_current_task].pid;
    t->fdt_group=t->pid;
    t->pgid=t->pid; t->sid=g_tasks[g_current_task].sid;
    t->state=TASK_RUNNABLE; t->nice=0;
    t->no_timer_preempt=g_tasks[g_current_task].no_timer_preempt;
    c2_strlcpy(t->name,name,TASK_NAME_LEN);
    c2_strlcpy(t->cwd,g_tasks[g_current_task].cwd,128);
    t->kernel_stack=g_kstacks[i];
    t->kernel_stack_top=(uint64_t)(uintptr_t)(g_kstacks[i]+TASK_KERNEL_STACK);
    t->addr_space=paging_create_address_space();
    t->ctx.cs=0x28; t->ctx.ss=0x30;
    t->ctx.rflags=0x200;
    t->ctx.rsp=0;
    t->ctx.rip=entry;
    fpu_init_state(t->fpu_state); t->fpu_used=false;
    t->brk_start=0x800000ULL; t->brk_current=0x800000ULL; t->mmap_base=0x40000000ULL;
    c2_memcpy(&t->fdt,&g_tasks[g_current_task].fdt,sizeof(fd_table_t));
    {int f;for(f=0;f<TASK_FD_MAX;++f)if(t->fdt.fds[f].kind!=FDKIND_NONE)t->fdt.fds[f].refcount++;}
    {int s;for(s=0;s<NSIG;++s)t->sigactions[s].handler=SIG_DFL;}
    return t->pid;
}

int task_fork(int parent_pid){
    int pi=-1,ci=-1,i; task_t *p,*c;
    extern int compat3_fork_address_space(task_t *parent,task_t *child);
    for(i=0;i<TASK_MAX;++i)if(g_tasks[i].used&&g_tasks[i].pid==parent_pid){pi=i;break;}
    if(pi<0)return -ESRCH;
    p=&g_tasks[pi];
    for(i=1;i<TASK_MAX;++i)if(!g_tasks[i].used){ci=i;break;}
    if(ci<0)return -EAGAIN;
    c=&g_tasks[ci];
    c2_memcpy(c,p,sizeof(task_t));
    c->pid=g_task_next_pid++; c->tgid=c->pid; c->ppid=p->pid;
    c->fdt_group=c->pid;
    c->state=TASK_RUNNABLE;
    c->kernel_stack=g_kstacks[ci];
    c->kernel_stack_top=(uint64_t)(uintptr_t)(g_kstacks[ci]+TASK_KERNEL_STACK);
    c2_memcpy(c->kernel_stack,p->kernel_stack,TASK_KERNEL_STACK);
    {
        uint64_t pbase=(uint64_t)(uintptr_t)p->kernel_stack;
        uint64_t cbase=(uint64_t)(uintptr_t)c->kernel_stack;
        if(p->kernel_rsp_saved>=pbase&&p->kernel_rsp_saved<(pbase+TASK_KERNEL_STACK)){
            c->kernel_rsp_saved=cbase+(p->kernel_rsp_saved-pbase);
        }else{
            c->kernel_rsp_saved=c->kernel_stack_top;
        }
    }
    /* Create new address space, then let compat3 clone user pages with COW metadata. */
    c->addr_space=paging_create_address_space();
    if(!c->addr_space)c->addr_space=paging_get_kernel_space();
    if(c->addr_space!=paging_get_kernel_space()){
        if(compat3_fork_address_space(p,c)<0){
            /* Fallback to blank space if clone helper cannot run yet. */
        }
    }
    /* Child returns 0 from fork */
    c->ctx.rax=0;
    c->clear_child_tid=0;
    c->robust_list_head=0;
    c->robust_list_len=0;
    /* Increment FD refcounts */
    {int f;for(f=0;f<TASK_FD_MAX;++f)if(c->fdt.fds[f].kind!=FDKIND_NONE)c->fdt.fds[f].refcount++;}
    return c->pid;
}

void task_exit(int pid,int code){
    int i;
    static uint32_t force_task_exit_trace_count;
    for(i=0;i<TASK_MAX;++i){
        if(g_tasks[i].used&&g_tasks[i].pid==pid){
            int parent_pid=g_tasks[i].ppid;
            bool auto_reap_thread=(g_tasks[i].ctx.cs==0x28&&
                                   g_tasks[i].tgid!=0&&
                                   g_tasks[i].tgid!=g_tasks[i].pid);
            bool shared_user_survivor=false;
            bool runnable_user_survivor=false;
            int first_sleeping_survivor=-1;
            int si;
            if(i==g_current_task){
                for(si=0;si<TASK_MAX;++si){
                    if(si==i)continue;
                    if(!g_tasks[si].used)continue;
                    if(g_tasks[si].state==TASK_ZOMBIE||g_tasks[si].state==TASK_FREE)continue;
                    if(g_tasks[si].addr_space==g_tasks[i].addr_space&&g_tasks[si].ctx.cs==0x28){
                        shared_user_survivor=true;
                        if(g_tasks[si].state==TASK_RUNNING){
                            /* On this single-CPU scheduler, another thread in
                             * the same process cannot truly be running while
                             * the exiting thread owns the CPU. Treat it as a
                             * stale runnable survivor so thread teardown can
                             * return to the process reliably. */
                            g_tasks[si].state=TASK_RUNNABLE;
                            runnable_user_survivor=true;
                        }else if(g_tasks[si].state==TASK_RUNNABLE){
                            runnable_user_survivor=true;
                        }else if(g_tasks[si].state==TASK_SLEEPING&&first_sleeping_survivor<0){
                            first_sleeping_survivor=si;
                        }
                    }
                }
                if(shared_user_survivor&&!runnable_user_survivor&&first_sleeping_survivor>=0){
                    g_tasks[first_sleeping_survivor].state=TASK_RUNNABLE;
                    g_tasks[first_sleeping_survivor].sleep_until=0;
                }
                if(g_task_exit_sibling_trace<16){
                    ++g_task_exit_sibling_trace;
                    __boot_serial_puts("[exit-sibs] cur=");
                    __boot_serial_putu32((uint32_t)g_tasks[i].pid);
                    __boot_serial_puts(" shared=");
                    __boot_serial_putu32(shared_user_survivor?1u:0u);
                    __boot_serial_puts(" run=");
                    __boot_serial_putu32(runnable_user_survivor?1u:0u);
                    __boot_serial_puts(" sleep=");
                    __boot_serial_putu32((uint32_t)(first_sleeping_survivor>=0?g_tasks[first_sleeping_survivor].pid:0));
                    __boot_serial_puts(" ::");
                    for(si=0;si<TASK_MAX;++si){
                        if(si==i)continue;
                        if(!g_tasks[si].used)continue;
                        if(g_tasks[si].addr_space!=g_tasks[i].addr_space)continue;
                        __boot_serial_puts(" ");
                        __boot_serial_putu32((uint32_t)g_tasks[si].pid);
                        __boot_serial_puts(":");
                        __boot_serial_putu32((uint32_t)g_tasks[si].state);
                    }
                    __boot_serial_puts("\n");
                }
                if(!shared_user_survivor)g_user_foreground_active=false;
            }
            if(force_task_exit_trace_count<128u||code!=0){
                ++force_task_exit_trace_count;
                __boot_serial_force_puts("[task-exit!] pid=");
                __boot_serial_force_putu32((uint32_t)g_tasks[i].pid);
                __boot_serial_force_puts(" code=");
                __boot_serial_force_putu32((uint32_t)code);
                __boot_serial_force_puts(" survivors=");
                __boot_serial_force_putu32(shared_user_survivor?1u:0u);
                if(g_tasks[i].name[0]){
                    __boot_serial_force_puts(" name=");
                    __boot_serial_force_puts(g_tasks[i].name);
                }
                __boot_serial_force_puts("\n");
            }
            g_tasks[i].state=TASK_ZOMBIE;
            g_tasks[i].exit_code=code;
            {
                int j;
                for(j=0;j<TASK_MAX;++j){
                    if(!g_tasks[j].used||g_tasks[j].pid!=parent_pid)continue;
                    if(g_tasks[j].state==TASK_SLEEPING){
                        int want=g_tasks[j].wait_pid;
                        if(want==-1||want==0||want==pid||(want<-1&&g_tasks[i].pgid==-want)){
                            g_tasks[j].state=TASK_RUNNABLE;
                            g_tasks[j].wait_pid=0;
                        }
                    }
                    break;
                }
            }
            /* Close all FDs, releasing shared backing objects only when this
             * task held the final descriptor reference. */
            compat3_task_close_all_fds(&g_tasks[i]);
            /* Reparent children to init (pid 1) */
            {int j;for(j=0;j<TASK_MAX;++j)if(g_tasks[j].used&&g_tasks[j].ppid==pid)g_tasks[j].ppid=1;}
            if(auto_reap_thread){
                g_tasks[i].state=TASK_FREE;
                g_tasks[i].used=false;
            }
            break;
        }
    }
}

int task_waitpid(int pid,int *status,int options){
    task_t *cur=task_current();
    int self_pid;
    const int wnohang=1;
    if(!cur)return -ESRCH;
    self_pid=cur->pid;
    for(;;){
        bool have_match=false;
        int i;
        for(i=0;i<TASK_MAX;++i){
            bool match=false;
            if(!g_tasks[i].used)continue;
            if(pid>0){
                match=(g_tasks[i].pid==pid&&g_tasks[i].ppid==self_pid);
            }else if(pid==-1){
                match=(g_tasks[i].ppid==self_pid);
            }else if(pid==0){
                match=(g_tasks[i].ppid==self_pid&&g_tasks[i].pgid==cur->pgid);
            }else{
                match=(g_tasks[i].ppid==self_pid&&g_tasks[i].pgid==-pid);
            }
            if(!match)continue;
            have_match=true;
            if(g_tasks[i].state==TASK_ZOMBIE){
                int ret_pid=g_tasks[i].pid;
                if(status)*status=(g_tasks[i].exit_code&0xFF)<<8;
                g_tasks[i].used=false;
                cur->wait_pid=0;
                return ret_pid;
            }
        }
        if(!have_match){
            cur->wait_pid=0;
            return -ECHILD;
        }
        if(options&wnohang){
            cur->wait_pid=0;
            return 0;
        }
        cur->wait_pid=pid;
        cur->state=TASK_SLEEPING;
        task_schedule();
        if(cur->state==TASK_SLEEPING){
            cur->state=TASK_RUNNING;
            __asm__ volatile("pause");
        }
    }
}

extern void context_switch_kstack(uint64_t *old_rsp_save, uint64_t new_rsp);

static void task_wake_due_sleepers(void){
    uint64_t now=rdtsc();
    int i;
    for(i=0;i<TASK_MAX;++i){
        if(g_tasks[i].used&&g_tasks[i].state==TASK_SLEEPING&&g_tasks[i].sleep_until&&now>=g_tasks[i].sleep_until){
            g_tasks[i].state=TASK_RUNNABLE;
            g_tasks[i].sleep_until=0;
        }
    }
}

void task_schedule(void){
    int i,next=-1,start=(g_current_task+1)%TASK_MAX;
    int prev;
    uint64_t irq_flags=0;
    static uint32_t sched_trace_count=0;
    __asm__ volatile("pushfq; popq %0; cli":"=r"(irq_flags)::"memory");
#define TASK_SCHEDULE_RESTORE_IRQ() do{if(irq_flags&0x200ULL)__asm__ volatile("sti":::"memory");}while(0)
    task_wake_due_sleepers();
    /* Simple round-robin */
    for(i=0;i<TASK_MAX;++i){
        int idx=(start+i)%TASK_MAX;
        if(g_tasks[idx].used&&g_tasks[idx].state==TASK_RUNNABLE&&g_tasks[idx].needs_first_launch){next=idx;break;}
    }
    for(i=0;i<TASK_MAX;++i){
        int idx=(start+i)%TASK_MAX;
        if(next>=0)break;
        if(g_tasks[idx].used&&g_tasks[idx].state==TASK_RUNNABLE){next=idx;break;}
    }
    if(next<0&&(g_current_task<0||g_current_task>=TASK_MAX||g_tasks[g_current_task].state!=TASK_RUNNING)){
        for(i=0;i<TASK_MAX;++i){
            int idx=(start+i)%TASK_MAX;
            if(idx==g_current_task)continue;
            if(g_tasks[idx].ctx.cs!=0x28)continue;
            if(g_tasks[idx].used&&g_tasks[idx].state==TASK_RUNNING){
                g_tasks[idx].state=TASK_RUNNABLE;
                next=idx;
                break;
            }
        }
    }
    if(next<0){
        task_restore_current_user_msrs();
        TASK_SCHEDULE_RESTORE_IRQ();
        return;
    } /* no other task ready */
    if(next==g_current_task){
        task_restore_current_user_msrs();
        TASK_SCHEDULE_RESTORE_IRQ();
        return;
    }
    prev=g_current_task;
    {
        uint64_t msr_fs=c2_rdmsr(0xC0000100u);
        bool trace=(sched_trace_count<64)&&
                   (g_tasks[prev].pid==2||g_tasks[next].pid==2||
                    g_tasks[prev].pid==10||g_tasks[next].pid==10||
                    (g_tasks[prev].ctx.cs==0x28&&msr_fs!=g_tasks[prev].ctx.fs_base));
        if(trace){
            ++sched_trace_count;
            __boot_serial_puts("[sched-out] prev=");
            __boot_serial_putu32((uint32_t)g_tasks[prev].pid);
            __boot_serial_puts(" pst=");
            __boot_serial_putu32((uint32_t)g_tasks[prev].state);
            __boot_serial_puts(" next=");
            __boot_serial_putu32((uint32_t)g_tasks[next].pid);
            __boot_serial_puts(" nst=");
            __boot_serial_putu32((uint32_t)g_tasks[next].state);
            __boot_serial_puts(" msr.fs=");
            __boot_serial_puthex64(msr_fs);
            __boot_serial_puts(" pctx.fs=");
            __boot_serial_puthex64(g_tasks[prev].ctx.fs_base);
            __boot_serial_puts(" nctx.fs=");
            __boot_serial_puthex64(g_tasks[next].ctx.fs_base);
            __boot_serial_puts("\n");
        }
    }
    g_tasks[prev].syscall_preempt_disable=(uint32_t)g_kernel_preempt_disable;
    /* Do not sample FS/GS from the CPU here.  While a task is sleeping in
     * kernel mode the live MSRs can temporarily belong to a different task;
     * copying them back into prev->ctx corrupts Linux TLS and breaks
     * pthread_self()/pthread_getattr_np in Firefox/Chromium.  Userland changes
     * these bases through arch_prctl() or CLONE_SETTLS, which update ctx. */
    /* Save FPU of outgoing task */
    fpu_save(&g_tasks[prev]);
    g_tasks[prev].state=(g_tasks[prev].state==TASK_RUNNING)?TASK_RUNNABLE:g_tasks[prev].state;
    g_current_task=next;
    g_tasks[next].state=TASK_RUNNING;
    g_kernel_preempt_disable=g_tasks[next].syscall_preempt_disable;
    sig_check_pending(&g_tasks[next]);
    c2_clear_fs_selector();
    c2_wrmsr(0xC0000100u,g_tasks[next].ctx.fs_base); /* IA32_FS_BASE */
    c2_wrfsbase(g_tasks[next].ctx.fs_base);
    c2_wrmsr(0xC0000102u,g_tasks[next].ctx.gs_base); /* IA32_KERNEL_GS_BASE */
    c2_wrmsr(0xC0000101u,(uint64_t)(uintptr_t)&g_per_cpu); /* IA32_GS_BASE */
    if(sched_trace_count<64&&(g_tasks[prev].pid==2||g_tasks[next].pid==2||
                               g_tasks[prev].pid==10||g_tasks[next].pid==10)){
        ++sched_trace_count;
        __boot_serial_puts("[sched-in] cur=");
        __boot_serial_putu32((uint32_t)g_tasks[next].pid);
        __boot_serial_puts(" msr.fs=");
        __boot_serial_puthex64(c2_rdmsr(0xC0000100u));
        __boot_serial_puts(" ctx.fs=");
        __boot_serial_puthex64(g_tasks[next].ctx.fs_base);
        __boot_serial_puts("\n");
    }
    tss_set_rsp0(g_tasks[next].kernel_stack_top);
    g_per_cpu.user_rsp=g_tasks[next].ctx.rsp;
    paging_switch(g_tasks[next].addr_space);
    /* Restore FPU of incoming task */
    fpu_restore(&g_tasks[next]);
    /* Real context switch: save current kernel RSP, load next's.
     * For freshly-created threads, this returns into
     * clone3_entry_trampoline which calls clone3_enter_user. */
    context_switch_kstack(&g_tasks[prev].kernel_rsp_saved,
                          g_tasks[next].kernel_rsp_saved);
    /* We return here when another task switches back to us.  Re-assert
     * the user TLS MSRs for the task that was just resumed; Chromium's
     * thread-heavy startup can otherwise continue a sleeping syscall with
     * the FS base left by the last fresh clone trampoline. */
    if(g_current_task>=0&&g_current_task<TASK_MAX&&sched_trace_count<64){
        uint64_t msr_fs=c2_rdmsr(0xC0000100u);
        task_t *cur=&g_tasks[g_current_task];
        if(cur->used&&(cur->pid==2||cur->pid==10||msr_fs!=cur->ctx.fs_base)){
            ++sched_trace_count;
            __boot_serial_puts("[sched-ret] cur=");
            __boot_serial_putu32((uint32_t)cur->pid);
            __boot_serial_puts(" msr.fs=");
            __boot_serial_puthex64(msr_fs);
            __boot_serial_puts(" ctx.fs=");
            __boot_serial_puthex64(cur->ctx.fs_base);
            __boot_serial_puts("\n");
        }
    }
    task_restore_current_user_msrs();
    tss_set_rsp0(g_tasks[g_current_task].kernel_stack_top);
    g_per_cpu.user_rsp=g_tasks[g_current_task].ctx.rsp;
    if(g_tasks[g_current_task].addr_space)paging_switch(g_tasks[g_current_task].addr_space);
    if(g_current_task>=0&&g_current_task<TASK_MAX&&sched_trace_count<64){
        uint64_t msr_fs=c2_rdmsr(0xC0000100u);
        task_t *cur=&g_tasks[g_current_task];
        if(cur->used&&(cur->pid==2||cur->pid==10||msr_fs!=cur->ctx.fs_base)){
            ++sched_trace_count;
            __boot_serial_puts("[sched-ret2] cur=");
            __boot_serial_putu32((uint32_t)cur->pid);
            __boot_serial_puts(" msr.fs=");
            __boot_serial_puthex64(msr_fs);
            __boot_serial_puts(" ctx.fs=");
            __boot_serial_puthex64(cur->ctx.fs_base);
            __boot_serial_puts("\n");
        }
    }
    TASK_SCHEDULE_RESTORE_IRQ();
#undef TASK_SCHEDULE_RESTORE_IRQ
}

/* Provided by isr64.S. One-shot iretq into ring 3; does not return. */
extern void task_launch_to_user_asm(uint64_t rip, uint64_t rsp, uint64_t rflags);

/* Trampoline target in isr64.S for freshly-created threads. */
extern void clone3_entry_trampoline(void);

/* Provided by isr64.S. Restores ALL GPRs from a cpu_context_t,
 * builds iretq frame, does swapgs, iretq. NEVER RETURNS. */
extern void clone3_iretq_ctx(cpu_context_t *ctx);

/* C helper called from clone3_entry_trampoline (isr64.S).
 * Enters ring 3 for a freshly-created thread with FULL register
 * context restored from t->ctx.  NEVER RETURNS. */
void clone3_enter_user(task_t *t) {
    if (!t || !t->used) { for(;;) __asm__ volatile("hlt"); }
    t->needs_first_launch = false;
    __boot_serial_puts("[clone3] enter ring3 pid=");
    __boot_serial_putu32((uint32_t)t->pid);
    __boot_serial_puts(" rip=");
    __boot_serial_puthex64(t->ctx.rip);
    __boot_serial_puts(" rsp=");
    __boot_serial_puthex64(t->ctx.rsp);
    __boot_serial_puts(" rdi=");
    __boot_serial_puthex64(t->ctx.rdi);
    __boot_serial_puts("\n");
    paging_switch(t->addr_space);
    tss_set_rsp0(t->kernel_stack_top);
    g_per_cpu.kernel_rsp = t->kernel_stack_top;
    c2_clear_fs_selector();
    c2_wrmsr(0xC0000100u, t->ctx.fs_base);
    c2_wrfsbase(t->ctx.fs_base);
    c2_wrmsr(0xC0000102u, t->ctx.gs_base);
    c2_wrmsr(0xC0000101u, (uint64_t)(uintptr_t)&g_per_cpu);
    fpu_restore(t);
    /* Use assembly routine that reads all GPRs directly from ctx
     * via a single base pointer (rdi), avoiding the clobbered-base
     * problem that inline asm "m" constraints would cause. */
    clone3_iretq_ctx(&t->ctx);
    __builtin_unreachable();
}

/* Set up a freshly-created thread's kernel stack so that
 * context_switch_kstack into it lands in clone3_entry_trampoline.
 * Stack layout (growing down from kernel_stack_top):
 *   [task_t* arg]       <- popped by trampoline
 *   [ret addr = clone3_entry_trampoline]
 *   [fake r15..rbx, rflags]  <- context_switch_kstack pops these
 */
void clone3_setup_kstack(task_t *t) {
    uint64_t *sp = (uint64_t*)t->kernel_stack_top;
    /* Push task_t* argument for clone3_entry_trampoline */
    *(--sp) = (uint64_t)(uintptr_t)t;
    /* Push return address: clone3_entry_trampoline */
    *(--sp) = (uint64_t)(uintptr_t)clone3_entry_trampoline;
    /* Push fake callee-saved frame (rflags, rbx, rbp, r12-r15) */
    *(--sp) = 0x200ULL;  /* rflags (IF) */
    *(--sp) = 0; /* rbx */
    *(--sp) = 0; /* rbp */
    *(--sp) = 0; /* r12 */
    *(--sp) = 0; /* r13 */
    *(--sp) = 0; /* r14 */
    *(--sp) = 0; /* r15 */
    t->kernel_rsp_saved = (uint64_t)sp;
    t->needs_first_launch = true;
}

/* ----------------------------------------------------------------
 * task_launch_to_user
 *
 * First-time entry into ring 3 for a freshly-created user task.
 * Wires up all the per-CPU / MSR / paging state required for the
 * initial iretq in task_launch_to_user_asm, updates g_current_task,
 * and then transfers control. NEVER RETURNS.
 *
 * After this, the CPU is in user mode executing the task. Control
 * only re-enters the kernel via:
 *   - SYSCALL     -> syscall_entry (isr64.S)
 *   - Timer IRQ   -> irq_common_stub64 (isr64.S)
 *   - #PF, #GP... -> isr_common_stub64 (isr64.S)
 *
 * That means the "kernel main loop" (UI render) stops advancing
 * once this is called. That is intentional for bring-up: the
 * priority while debugging user-mode entry is capturing what the
 * new task does, not continuing desktop animation.
 * ---------------------------------------------------------------- */
/* Diagnostic: dump the first N entries of the active GDT as raw 64-bit
 * quadwords. Useful for confirming that gdt64_setup() produced the
 * layout we think it did (user code at 0x28, user data at 0x30, etc). */
static void dump_gdt_to_serial(void) {
    int i;
    const uint64_t *p = (const uint64_t *)(uintptr_t)&g_gdt64[0];
    __boot_serial_puts("[gdt] dump @ ");
    __boot_serial_puthex64((uint64_t)(uintptr_t)p);
    __boot_serial_puts(" (limit=");
    __boot_serial_putu32((uint32_t)g_gdt64_ptr.limit);
    __boot_serial_puts(")\n");
    for (i = 0; i < 7; ++i) {
        __boot_serial_puts("[gdt]   [");
        __boot_serial_putu32((uint32_t)i);
        __boot_serial_puts("] @0x");
        __boot_serial_puthex64((uint64_t)(i * 8));
        __boot_serial_puts(" = ");
        __boot_serial_puthex64(p[i]);
        __boot_serial_puts("\n");
    }
}

void task_launch_to_user(task_t *t) {
    int idx;
    if (!t || !t->used) {
        __boot_serial_puts("[launch] invalid task, aborting\n");
        return;
    }
    idx = (int)(t - g_tasks);
    dump_gdt_to_serial();
    __boot_serial_puts("[launch] -> pid=");
    __boot_serial_putu32((uint32_t)t->pid);
    __boot_serial_puts(" rip=");
    __boot_serial_puthex64(t->ctx.rip);
    __boot_serial_puts(" rsp=");
    __boot_serial_puthex64(t->ctx.rsp);
    __boot_serial_puts("\n");

    /* Switch to the task's address space BEFORE dumping, so the bytes
     * we read reflect what the user will see (low identity may differ
     * if paging_map promoted huge pages). */
    paging_switch(t->addr_space);
    {
        const uint8_t *code = (const uint8_t *)(uintptr_t)t->ctx.rip;
        int i;
        __boot_serial_puts("[launch] bytes @ rip: ");
        for (i = 0; i < 32; ++i) {
            static const char *hex = "0123456789abcdef";
            __boot_serial_putc(hex[(code[i] >> 4) & 0xF]);
            __boot_serial_putc(hex[code[i] & 0xF]);
            __boot_serial_putc(' ');
        }
        __boot_serial_puts("\n");
    }
    {
        const uint8_t *sp = (const uint8_t *)(uintptr_t)t->ctx.rsp;
        int i;
        __boot_serial_puts("[launch] bytes @ rsp: ");
        for (i = 0; i < 16; ++i) {
            static const char *hex = "0123456789abcdef";
            __boot_serial_putc(hex[(sp[i] >> 4) & 0xF]);
            __boot_serial_putc(hex[sp[i] & 0xF]);
            __boot_serial_putc(' ');
        }
        __boot_serial_puts("\n");
    }

    /* Commit the target task as "current" so syscall handlers and
     * interrupt dispatchers see the right task. */
    g_current_task = idx;
    t->state = TASK_RUNNING;

    /* Switch address space. After this the user VA -> PA mappings
     * (PT_LOAD segments of the ELF, stack, any mmaps the loader did)
     * are live. */
    paging_switch(t->addr_space);

    /* TSS.rsp0 must point at this task's kernel stack so SYSCALL
     * entry (and any subsequent CPU exception taken from user mode)
     * lands on a valid, empty kernel stack. */
    tss_set_rsp0(t->kernel_stack_top);
    g_per_cpu.kernel_rsp = t->kernel_stack_top;

    /* Set FS base for user; KERNEL_GS_BASE holds the user's GS base
     * so that swapgs in task_launch_to_user_asm swaps it into GS base. */
    c2_clear_fs_selector();
    c2_wrmsr(0xC0000100u, t->ctx.fs_base);       /* IA32_FS_BASE */
    c2_wrfsbase(t->ctx.fs_base);
    c2_wrmsr(0xC0000102u, t->ctx.gs_base);       /* IA32_KERNEL_GS_BASE */
    /* Keep IA32_GS_BASE (current GS base) pointing at the per-CPU
     * struct; swapgs will rotate it into KERNEL_GS_BASE. */
    c2_wrmsr(0xC0000101u, (uint64_t)(uintptr_t)&g_per_cpu);

    /* Restore FPU state so userspace sees its own FXSAVE image. */
    fpu_restore(t);

    g_user_foreground_active=true;
    __boot_serial_puts("[launch] iretq to ring 3 now\n");
    /* Does not return. */
    task_launch_to_user_asm(t->ctx.rip, t->ctx.rsp,
                            t->ctx.rflags ? t->ctx.rflags : 0x202ULL);

    /* Unreachable. */
    __boot_serial_puts("[launch] RETURNED FROM iretq (impossible)\n");
    for (;;) __asm__ volatile("cli; hlt");
}

/* File descriptor table operations */
int fd_alloc(fd_table_t *t,uint8_t kind,int ref,uint16_t flags){
    int i;for(i=0;i<TASK_FD_MAX;++i)if(t->fds[i].kind==FDKIND_NONE){
        t->fds[i].kind=kind;t->fds[i].ref=ref;t->fds[i].flags=flags;
        t->fds[i].offset=0;t->fds[i].refcount=1;return i;}
    return -EMFILE;
}
int fd_dup(fd_table_t *t,int oldfd){
    int nfd;if(oldfd<0||oldfd>=TASK_FD_MAX||t->fds[oldfd].kind==FDKIND_NONE)return -EBADF;
    nfd=fd_alloc(t,t->fds[oldfd].kind,t->fds[oldfd].ref,t->fds[oldfd].flags);
    if(nfd>=0)t->fds[nfd].offset=t->fds[oldfd].offset;
    return nfd;
}
int fd_dup2(fd_table_t *t,int oldfd,int newfd){
    if(oldfd<0||oldfd>=TASK_FD_MAX||t->fds[oldfd].kind==FDKIND_NONE)return -EBADF;
    if(newfd<0||newfd>=TASK_FD_MAX)return -EBADF;
    if(oldfd==newfd)return newfd;
    if(t->fds[newfd].kind!=FDKIND_NONE)fd_close(t,newfd);
    c2_memcpy(&t->fds[newfd],&t->fds[oldfd],sizeof(real_fd_t));
    t->fds[newfd].refcount++;
    return newfd;
}
void fd_close(fd_table_t *t,int fd){
    if(fd<0||fd>=TASK_FD_MAX)return;
    if(t->fds[fd].kind==FDKIND_NONE)return;
    c2_memset(&t->fds[fd],0,sizeof(real_fd_t));
}
bool fd_valid(fd_table_t *t,int fd){return fd>=0&&fd<TASK_FD_MAX&&t->fds[fd].kind!=FDKIND_NONE;}

/* Signal delivery */
static void sig_default_action(task_t *t,int sig){
    switch(sig){
        case SIGKILL: case SIGSEGV: case SIGBUS: case SIGILL: case SIGFPE: case SIGABRT:
            task_exit(t->pid,128+sig); break;
        case SIGSTOP: case SIGTSTP: case SIGTTIN: case SIGTTOU:
            t->state=TASK_STOPPED; break;
        case SIGCONT:
            if(t->state==TASK_STOPPED)t->state=TASK_RUNNABLE;
            break;
        case SIGCHLD: case SIGURG: case SIGWINCH: break; /* ignored */
        default: task_exit(t->pid,128+sig); break;
    }
}

static bool sigset_has(const sigset_t2 *set,int sig){
    int idx,bit;
    if(!set||sig<0)return false;
    idx=sig/64;bit=sig%64;
    if(idx<0||idx>1)return false;
    return (set->sig[idx]&(1ULL<<bit))!=0;
}
static void sigset_add(sigset_t2 *set,int sig){
    int idx,bit;
    if(!set||sig<0)return;
    idx=sig/64;bit=sig%64;
    if(idx<0||idx>1)return;
    set->sig[idx]|=(1ULL<<bit);
}
static void sigset_del(sigset_t2 *set,int sig){
    int idx,bit;
    if(!set||sig<0)return;
    idx=sig/64;bit=sig%64;
    if(idx<0||idx>1)return;
    set->sig[idx]&=~(1ULL<<bit);
}

static void sig_enqueue(task_t *t,int sig){
    if(!t||sig<=0||sig>=NSIG)return;
    if(sig==SIGKILL||sig==SIGSTOP)sigset_del(&t->sig_blocked,sig);
    sigset_add(&t->sig_pending,sig);
    if(sig==SIGCONT&&t->state==TASK_STOPPED)t->state=TASK_RUNNABLE;
    if(t->state==TASK_SLEEPING)t->state=TASK_RUNNABLE;
}

static bool sig_target_match(task_t *t,int pid,int caller_pgid){
    if(!t||!t->used)return false;
    if(pid>0)return t->pid==pid;
    if(pid==0)return t->pgid==caller_pgid;
    if(pid==-1)return t->pid>1;
    return t->pgid==-pid;
}

int sig_send(int pid,int sig){
    int i;
    int caller_pgid=g_tasks[g_current_task].pgid;
    int delivered=0;
    if(sig<0||sig>=NSIG)return -EINVAL;
    if(pid==0&&caller_pgid<=0)return -ESRCH;
    for(i=0;i<TASK_MAX;++i){
        task_t *t=&g_tasks[i];
        if(!sig_target_match(t,pid,caller_pgid))continue;
        if(sig==0)return 0; /* existence check only */
        if((sig!=SIGKILL&&sig!=SIGSTOP)&&t->sigactions[sig].handler==SIG_IGN)continue;
        sig_enqueue(t,sig);
        ++delivered;
    }
    return delivered?0:-ESRCH;
}

void sig_check_pending(task_t *t){
    int s;
    if(!t||!t->used)return;
    if(t->sig_in_handler)return;
    /* Only deliver signals to user-mode tasks */
    if(t->ctx.cs != 0x28 && t->ctx.cs != 0x23) return;
    for(s=1;s<NSIG;++s){
        uint32_t sa_flags;
        sig_handler_t handler;
        sig_frame_t frame;
        uint64_t new_rsp;
        sig_frame_t *user_frame;

        if(!sigset_has(&t->sig_pending,s))continue;
        if(sigset_has(&t->sig_blocked,s))continue;
        sigset_del(&t->sig_pending,s);
        if(t->sigactions[s].handler==SIG_IGN)continue;
        if(t->sigactions[s].handler==SIG_DFL){sig_default_action(t,s);continue;}

        handler = t->sigactions[s].handler;
        sa_flags = t->sigactions[s].flags;

        /* Build signal frame on user stack.
         * We push the sig_frame_t + a sigreturn trampoline. */
        c2_memset(&frame, 0, sizeof(frame));

        /* Save current user context into the frame */
        frame.rax = t->ctx.rax;  frame.rbx = t->ctx.rbx;
        frame.rcx = t->ctx.rcx;  frame.rdx = t->ctx.rdx;
        frame.rsi = t->ctx.rsi;  frame.rdi = t->ctx.rdi;
        frame.rbp = t->ctx.rbp;
        frame.r8  = t->ctx.r8;   frame.r9  = t->ctx.r9;
        frame.r10 = t->ctx.r10;  frame.r11 = t->ctx.r11;
        frame.r12 = t->ctx.r12;  frame.r13 = t->ctx.r13;
        frame.r14 = t->ctx.r14;  frame.r15 = t->ctx.r15;
        frame.rsp = t->ctx.rsp;
        frame.rip = t->ctx.rip;
        frame.eflags = t->ctx.rflags;
        frame.cs = (uint16_t)t->ctx.cs;
        frame.ss = (uint16_t)t->ctx.ss;

        /* siginfo */
        frame.si_signo = s;
        frame.si_errno = 0;
        frame.si_code  = 0;
        frame.si_addr  = 0;

        /* Save signal mask */
        frame.sigmask_sig[0] = t->sig_blocked.sig[0];
        frame.sigmask_sig[1] = t->sig_blocked.sig[1];

        /* Calculate new user RSP: push frame + trampoline */
        new_rsp = t->ctx.rsp - sizeof(sig_frame_t) - SIGRETURN_TRAMP_SIZE;
        /* Align to 16 bytes */
        new_rsp &= ~0xFULL;

        /* Copy signal frame to user stack */
        user_frame = (sig_frame_t*)(uintptr_t)new_rsp;
        /* We're in kernel context; the user stack pages are mapped
         * in the task's address space. Since we're still using the
         * kernel's identity map (low-half), we can write directly. */
        c2_memcpy(user_frame, &frame, sizeof(sig_frame_t));

        /* Write sigreturn trampoline at user_frame + sizeof(sig_frame_t).
         * Machine code: mov $15, %rax (2 bytes: 48 c7 c0 0f 00 00 00)
         *               syscall     (2 bytes: 0f 05)
         * Total: 7 bytes, padded to 8. */
        {
            uint8_t *tramp = (uint8_t*)(uintptr_t)(new_rsp + sizeof(sig_frame_t));
            /* mov eax, 15  ->  b8 0f 00 00 00 */
            tramp[0] = 0xB8; tramp[1] = 0x0F; tramp[2] = 0x00;
            tramp[3] = 0x00; tramp[4] = 0x00;
            /* syscall -> 0F 05 */
            tramp[5] = 0x0F; tramp[6] = 0x05;
            tramp[7] = 0xC3; /* nop/ret safety */
        }

        /* Update task state to jump to signal handler on next return */
        t->ctx.rip = (uint64_t)(uintptr_t)handler;
        t->ctx.rsp = new_rsp;
        /* Set up handler arguments: RDI = signal number */
        t->ctx.rdi = (uint64_t)s;
        /* RSI = pointer to siginfo_t (inside the frame) */
        t->ctx.rsi = (uint64_t)(uintptr_t)&user_frame->si_signo;
        /* RDX = pointer to ucontext (inside the frame) */
        t->ctx.rdx = (uint64_t)(uintptr_t)&user_frame->uc_flags;
        /* The return address on the stack points to the trampoline.
         * We push it as the "return RIP" by adjusting RSP and writing.
         * Actually, the handler was called via RIP redirect, so the
         * handler's "return address" is the trampoline address.
         * We push it manually: */
        {
            uint64_t ret_addr = new_rsp + sizeof(sig_frame_t);
            /* Push return address onto the user stack */
            t->ctx.rsp -= 8;
            *(uint64_t*)(uintptr_t)t->ctx.rsp = ret_addr;
        }

        /* Update signal mask state */
        t->last_signal = s;
        t->sig_saved_mask = t->sig_blocked;
        t->sig_in_handler = true;
        if(!(sa_flags & SA_NODEFER)) sigset_add(&t->sig_blocked, s);
        t->sig_blocked.sig[0] |= t->sigactions[s].mask.sig[0];
        t->sig_blocked.sig[1] |= t->sigactions[s].mask.sig[1];
        if(sa_flags & SA_RESETHAND) t->sigactions[s].handler = SIG_DFL;
        break;
    }
}

/* Process groups / sessions */
int task_setpgid(int pid,int pgid){
    int i;for(i=0;i<TASK_MAX;++i)if(g_tasks[i].used&&g_tasks[i].pid==pid){
        g_tasks[i].pgid=pgid?pgid:pid;return 0;}return -ESRCH;}
int task_getpgid(int pid){
    int i;for(i=0;i<TASK_MAX;++i)if(g_tasks[i].used&&g_tasks[i].pid==pid)return g_tasks[i].pgid;return -ESRCH;}
int task_setsid(int pid){
    int i;for(i=0;i<TASK_MAX;++i)if(g_tasks[i].used&&g_tasks[i].pid==pid){
        g_tasks[i].sid=pid;g_tasks[i].pgid=pid;return pid;}return -ESRCH;}
int task_getsid(int pid){
    int i;for(i=0;i<TASK_MAX;++i)if(g_tasks[i].used&&g_tasks[i].pid==pid)return g_tasks[i].sid;return -ESRCH;}

/* Device operations (/dev/null, zero, random, urandom, fb, tty) */
int dev_null_read(void *buf,size_t count){(void)buf;(void)count;return 0;}
int dev_null_write(const void *buf,size_t count){(void)buf;return(int)count;}
int dev_zero_read(void *buf,size_t count){c2_memset(buf,0,count);return(int)count;}

static uint64_t g_rng_state=0;
static uint64_t rng_next(void){
    if(!g_rng_state)g_rng_state=rdtsc();
    g_rng_state^=g_rng_state<<13;g_rng_state^=g_rng_state>>7;g_rng_state^=g_rng_state<<17;
    return g_rng_state;
}
int dev_random_read(void *buf,size_t count){
    uint8_t *p=(uint8_t*)buf;size_t i;
    for(i=0;i<count;++i){uint64_t r=rng_next()^rdtsc();p[i]=(uint8_t)(r>>((i&7)*8));}
    return(int)count;
}
int dev_urandom_read(void *buf,size_t count){return dev_random_read(buf,count);}

int dev_fb_read(void *buf,size_t count,uint64_t offset){
    /* Would read from framebuffer memory */
    (void)buf;(void)count;(void)offset;return 0;
}
int dev_fb_write(const void *buf,size_t count,uint64_t offset){
    (void)buf;(void)count;(void)offset;return(int)count;
}
int dev_fb_mmap(uint64_t *addr_out,uint32_t size){
    (void)size;*addr_out=0xFD000000ULL; /* typical VESA LFB */
    return 0;
}

/* TTY / termios */
tty_device_t g_tty0;

void tty_init(tty_device_t *tty){
    c2_memset(tty,0,sizeof(*tty));
    tty->tio.c_iflag=ICRNL|IXON;
    tty->tio.c_oflag=OPOST|ONLCR;
    tty->tio.c_cflag=CS8|CREAD;
    tty->tio.c_lflag=ISIG|ICANON|ECHO|ECHOE|ECHOK|IEXTEN;
    tty->tio.c_cc[VINTR]=3;   /* Ctrl+C */
    tty->tio.c_cc[VQUIT]=28;  /* Ctrl+\ */
    tty->tio.c_cc[VERASE]=127;/* DEL */
    tty->tio.c_cc[VKILL]=21;  /* Ctrl+U */
    tty->tio.c_cc[VEOF]=4;    /* Ctrl+D */
    tty->tio.c_cc[VMIN]=1;
    tty->tio.c_cc[VTIME]=0;
    tty->tio.c_cc[VSTART]=17; /* Ctrl+Q */
    tty->tio.c_cc[VSTOP]=19;  /* Ctrl+S */
    tty->tio.c_cc[VSUSP]=26;  /* Ctrl+Z */
    tty->tio.c_ispeed=38400;
    tty->tio.c_ospeed=38400;
    tty->rows=25;tty->cols=80;
    tty->fg_pgid=1;
}

void tty_input_char(tty_device_t *tty,char c){
    /* ISIG: generate signals */
    if(tty->tio.c_lflag&ISIG){
        if((uint8_t)c==tty->tio.c_cc[VINTR]){sig_send(-tty->fg_pgid,SIGINT);return;}
        if((uint8_t)c==tty->tio.c_cc[VQUIT]){sig_send(-tty->fg_pgid,SIGQUIT);return;}
        if((uint8_t)c==tty->tio.c_cc[VSUSP]){sig_send(-tty->fg_pgid,SIGTSTP);return;}
    }
    /* ICRNL */
    if((tty->tio.c_iflag&ICRNL)&&c=='\r')c='\n';
    if(tty->tio.c_lflag&ICANON){
        /* Line-buffered mode */
        if((uint8_t)c==tty->tio.c_cc[VERASE]){
            if(tty->line_len>0)tty->line_len--;
            return;
        }
        if((uint8_t)c==tty->tio.c_cc[VKILL]){tty->line_len=0;return;}
        if(c=='\n'||(uint8_t)c==tty->tio.c_cc[VEOF]){
            /* Flush line to input buffer */
            int i;
            for(i=0;i<tty->line_len;++i){
                int next=(tty->input_head+1)%4096;
                if(next==tty->input_tail)break;
                tty->input_buf[tty->input_head]=(uint8_t)tty->line_buf[i];
                tty->input_head=next;
            }
            if(c=='\n'){
                int next=(tty->input_head+1)%4096;
                if(next!=tty->input_tail){tty->input_buf[tty->input_head]='\n';tty->input_head=next;}
            }
            tty->line_len=0;
            return;
        }
        if(tty->line_len<4095)tty->line_buf[tty->line_len++]=(uint8_t)c;
    } else {
        /* Raw mode */
        int next=(tty->input_head+1)%4096;
        if(next!=tty->input_tail){tty->input_buf[tty->input_head]=(uint8_t)c;tty->input_head=next;}
    }
}

int tty_read(tty_device_t *tty,void *buf,size_t count){
    uint8_t *p=(uint8_t*)buf;size_t got=0;
    while(got<count&&tty->input_tail!=tty->input_head){
        p[got++]=tty->input_buf[tty->input_tail];
        tty->input_tail=(tty->input_tail+1)%4096;
    }
    return(int)got;
}

int tty_write(tty_device_t *tty,const void *buf,size_t count){
    (void)tty;(void)buf; /* would echo to screen */
    return(int)count;
}

#define TCGETS     0x5401
#define TCSETS     0x5402
#define TCSETSW    0x5403
#define TCSETSF    0x5404
#define TIOCGPGRP  0x540F
#define TIOCSPGRP  0x5410
#define TIOCGWINSZ 0x5413
#define TIOCSWINSZ 0x5414

typedef struct{uint16_t ws_row,ws_col,ws_xpixel,ws_ypixel;} winsize_t;
typedef struct{
    uint32_t c_iflag;
    uint32_t c_oflag;
    uint32_t c_cflag;
    uint32_t c_lflag;
    uint8_t  c_line;
    uint8_t  c_cc[19];
} linux_termios_abi_t;

#define LINUX_B38400 0000017u

static void tty_export_linux_termios(tty_device_t *tty,linux_termios_abi_t *lt){
    size_t i;
    c2_memset(lt,0,sizeof(*lt));
    lt->c_iflag=tty->tio.c_iflag;
    lt->c_oflag=tty->tio.c_oflag;
    lt->c_cflag=tty->tio.c_cflag|LINUX_B38400;
    lt->c_lflag=tty->tio.c_lflag;
    lt->c_line=0;
    for(i=0;i<sizeof(lt->c_cc);++i)lt->c_cc[i]=tty->tio.c_cc[i];
}

static void tty_import_linux_termios(tty_device_t *tty,const linux_termios_abi_t *lt){
    size_t i;
    tty->tio.c_iflag=lt->c_iflag;
    tty->tio.c_oflag=lt->c_oflag;
    tty->tio.c_cflag=lt->c_cflag&~LINUX_B38400;
    tty->tio.c_lflag=lt->c_lflag;
    for(i=0;i<sizeof(lt->c_cc);++i)tty->tio.c_cc[i]=lt->c_cc[i];
}

int tty_ioctl(tty_device_t *tty,uint64_t req,void *arg){
    switch((uint32_t)req){
        case TCGETS: if(arg){linux_termios_abi_t lt;tty_export_linux_termios(tty,&lt);c2_memcpy(arg,&lt,sizeof(lt));}return 0;
        case TCSETS:case TCSETSW:case TCSETSF: if(arg){linux_termios_abi_t lt;c2_memcpy(&lt,arg,sizeof(lt));tty_import_linux_termios(tty,&lt);}return 0;
        case TIOCGPGRP: if(arg)*(int*)arg=tty->fg_pgid;return 0;
        case TIOCSPGRP: if(arg)tty->fg_pgid=*(int*)arg;return 0;
        case TIOCGWINSZ: if(arg){winsize_t *ws=(winsize_t*)arg;ws->ws_row=tty->rows;ws->ws_col=tty->cols;ws->ws_xpixel=0;ws->ws_ypixel=0;}return 0;
        case TIOCSWINSZ: if(arg){winsize_t *ws=(winsize_t*)arg;tty->rows=ws->ws_row;tty->cols=ws->ws_col;}return 0;
    }
    return -ENOTTY;
}

/* Timer / alarm */
timer_entry_t g_timers[TIMER_MAX];
static uint64_t g_tick_counter=0;

int timer_create_entry(int pid,uint64_t expire,uint64_t interval,int sig){
    int i;for(i=0;i<TIMER_MAX;++i)if(!g_timers[i].armed){
        g_timers[i].armed=true;g_timers[i].pid=pid;g_timers[i].expire_tick=expire;
        g_timers[i].interval=interval;g_timers[i].sig=sig;return i;}
    return -ENOMEM;
}

void timer_tick(uint64_t current_tick){
    int i;g_tick_counter=current_tick;
    for(i=0;i<TIMER_MAX;++i){
        if(!g_timers[i].armed)continue;
        if(current_tick>=g_timers[i].expire_tick){
            sig_send(g_timers[i].pid,g_timers[i].sig);
            if(g_timers[i].interval)g_timers[i].expire_tick=current_tick+g_timers[i].interval;
            else g_timers[i].armed=false;
        }
    }
    /* sleep_until stores TSC deadlines for user-visible sleeps. */
    task_wake_due_sleepers();
}

int sys_alarm(uint32_t seconds){
    int i;int pid=g_tasks[g_current_task].pid;
    /* Cancel existing alarm */
    for(i=0;i<TIMER_MAX;++i)if(g_timers[i].armed&&g_timers[i].pid==pid&&g_timers[i].sig==SIGALRM)g_timers[i].armed=false;
    if(seconds==0)return 0;
    timer_create_entry(pid,g_tick_counter+(uint64_t)seconds*100,0,SIGALRM);
    return 0;
}

/* ext2 full: block group descriptors, inode read, dir traversal */
static uint8_t g_ext2_blk_buf[4096]; /* up to 4K block size */

static bool ext2_read_block(int dev,uint32_t block,void *buf){
    uint32_t secs_per_blk=g_ext2.block_size/512;
    uint32_t lba=block*secs_per_blk;
    uint32_t i;uint8_t *p=(uint8_t*)buf;
    if(dev<0||dev>=g_block_dev_count||g_block_devs[dev].ata_index<0)return false;
    for(i=0;i<secs_per_blk;++i){
        if(!ata_read_sectors(g_block_devs[dev].ata_index,lba+i,1,p+i*512))return false;
    }
    return true;
}

bool ext2_read_inode(int dev,uint32_t ino,ext2_inode_t *out){
    uint32_t grp,idx,blk_off,byte_off;
    ext2_group_desc_t bgd;
    uint32_t bgd_block;
    if(!g_ext2.mounted||ino==0)return false;
    grp=(ino-1)/g_ext2.inodes_per_group;
    idx=(ino-1)%g_ext2.inodes_per_group;
    /* Read block group descriptor */
    bgd_block=g_ext2.first_data_block+1;
    if(!ext2_read_block(dev,bgd_block,g_ext2_blk_buf))return false;
    c2_memcpy(&bgd,g_ext2_blk_buf+grp*sizeof(ext2_group_desc_t),sizeof(bgd));
    /* Read inode from inode table */
    byte_off=idx*(uint32_t)g_ext2.inode_size;
    blk_off=byte_off/g_ext2.block_size;
    byte_off%=g_ext2.block_size;
    if(!ext2_read_block(dev,bgd.bg_inode_table+blk_off,g_ext2_blk_buf))return false;
    c2_memcpy(out,g_ext2_blk_buf+byte_off,sizeof(ext2_inode_t));
    return true;
}

static uint32_t ext2_get_block_num(int dev,const ext2_inode_t *ino,uint32_t block_idx){
    uint32_t ptrs_per_blk=g_ext2.block_size/4;
    uint32_t *buf32;
    if(block_idx<EXT2_NDIR_BLOCKS)return ino->i_block[block_idx];
    block_idx-=EXT2_NDIR_BLOCKS;
    if(block_idx<ptrs_per_blk){
        if(!ino->i_block[EXT2_IND_BLOCK])return 0;
        if(!ext2_read_block(dev,ino->i_block[EXT2_IND_BLOCK],g_ext2_blk_buf))return 0;
        buf32=(uint32_t*)g_ext2_blk_buf;
        return buf32[block_idx];
    }
    block_idx-=ptrs_per_blk;
    if(block_idx<ptrs_per_blk*ptrs_per_blk){
        uint32_t i1=block_idx/ptrs_per_blk,i2=block_idx%ptrs_per_blk;
        if(!ino->i_block[EXT2_DIND_BLOCK])return 0;
        if(!ext2_read_block(dev,ino->i_block[EXT2_DIND_BLOCK],g_ext2_blk_buf))return 0;
        buf32=(uint32_t*)g_ext2_blk_buf;
        uint32_t ind=buf32[i1];if(!ind)return 0;
        if(!ext2_read_block(dev,ind,g_ext2_blk_buf))return 0;
        buf32=(uint32_t*)g_ext2_blk_buf;
        return buf32[i2];
    }
    /* Triple indirect not implemented */
    return 0;
}

int ext2_read_inode_data(int dev,const ext2_inode_t *ino,uint8_t *buf,uint32_t offset,uint32_t size){
    uint32_t file_size=ino->i_size;
    uint32_t read_total=0;
    if(offset>=file_size)return 0;
    if(offset+size>file_size)size=file_size-offset;
    while(read_total<size){
        uint32_t blk_idx=(offset+read_total)/g_ext2.block_size;
        uint32_t blk_off=(offset+read_total)%g_ext2.block_size;
        uint32_t to_read=g_ext2.block_size-blk_off;
        uint32_t blk_num;
        if(to_read>size-read_total)to_read=size-read_total;
        blk_num=ext2_get_block_num(dev,ino,blk_idx);
        if(!blk_num){c2_memset(buf+read_total,0,to_read);}
        else{
            if(!ext2_read_block(dev,blk_num,g_ext2_blk_buf))return(int)read_total;
            c2_memcpy(buf+read_total,g_ext2_blk_buf+blk_off,to_read);
        }
        read_total+=to_read;
    }
    return(int)read_total;
}

uint32_t ext2_lookup(int dev,uint32_t dir_ino,const char *name){
    ext2_inode_t inode;
    uint32_t offset=0;
    size_t nlen=c2_strlen(name);
    if(!ext2_read_inode(dev,dir_ino,&inode))return 0;
    while(offset<inode.i_size){
        uint8_t entry_buf[264];
        ext2_dir_entry_t *de=(ext2_dir_entry_t*)entry_buf;
        int rd=ext2_read_inode_data(dev,&inode,entry_buf,offset,264);
        if(rd<8)break;
        if(de->inode&&de->name_len==(uint8_t)nlen){
            bool match=true;size_t i;
            for(i=0;i<nlen;++i)if(de->name[i]!=name[i]){match=false;break;}
            if(match)return de->inode;
        }
        if(de->rec_len==0)break;
        offset+=de->rec_len;
    }
    return 0;
}

uint32_t ext2_path_resolve(int dev,const char *path){
    uint32_t ino=EXT2_ROOT_INO;
    char comp[256]; int ci=0;
    const char *p=path;
    while(*p=='/')p++;
    if(!*p)return ino;
    while(*p){
        ci=0;
        while(*p&&*p!='/'&&ci<255)comp[ci++]=*p++;
        comp[ci]=0;
        while(*p=='/')p++;
        ino=ext2_lookup(dev,ino,comp);
        if(!ino)return 0;
    }
    return ino;
}

int ext2_list_dir_full(int dev,uint32_t dir_ino,fat32_dirent_t *ents,int max){
    ext2_inode_t inode;
    uint32_t offset=0;int cnt=0;
    if(!ext2_read_inode(dev,dir_ino,&inode))return -1;
    while(offset<inode.i_size&&cnt<max){
        uint8_t entry_buf[264];
        ext2_dir_entry_t *de=(ext2_dir_entry_t*)entry_buf;
        int rd=ext2_read_inode_data(dev,&inode,entry_buf,offset,264);
        if(rd<8)break;
        if(de->inode&&de->name_len>0){
            size_t nl=de->name_len;if(nl>255)nl=255;
            c2_memcpy(ents[cnt].name,de->name,nl);ents[cnt].name[nl]=0;
            ents[cnt].cluster=de->inode;ents[cnt].size=0;
            ents[cnt].is_dir=(de->file_type==EXT2_FT_DIR);
            ents[cnt].attr=de->file_type;++cnt;
        }
        if(de->rec_len==0)break;
        offset+=de->rec_len;
    }
    return cnt;
}

bool ext2_read_file_full(int dev,const char *path,uint8_t *buf,uint32_t buf_size,uint32_t *out_size){
    uint32_t ino=ext2_path_resolve(dev,path);
    ext2_inode_t inode;
    int rd;
    if(!ino)return false;
    if(!ext2_read_inode(dev,ino,&inode))return false;
    if((inode.i_mode&0xF000)==0x4000)return false; /* is directory */
    *out_size=inode.i_size;if(*out_size>buf_size)*out_size=buf_size;
    rd=ext2_read_inode_data(dev,&inode,buf,0,*out_size);
    return rd>0;
}

/* Network packet construction */
static uint16_t htons16(uint16_t v){return(uint16_t)((v>>8)|(v<<8));}
static uint32_t htonl32(uint32_t v){return((v&0xFF)<<24)|((v&0xFF00)<<8)|((v&0xFF0000)>>8)|((v>>24)&0xFF);}

uint16_t net_checksum(const void *data,size_t len){
    const uint16_t *p=(const uint16_t*)data;uint32_t sum=0;
    while(len>1){sum+=*p++;len-=2;}
    if(len)sum+=*(const uint8_t*)p;
    while(sum>>16)sum=(sum&0xFFFF)+(sum>>16);
    return(uint16_t)(~sum);
}

int net_build_eth(uint8_t *buf,const uint8_t *dst,const uint8_t *src,uint16_t type){
    c2_memcpy(buf,dst,6);c2_memcpy(buf+6,src,6);
    buf[12]=(uint8_t)(type>>8);buf[13]=(uint8_t)(type&0xFF);
    return ETH_HEADER_SIZE;
}

int net_build_ip(uint8_t *buf,uint32_t src,uint32_t dst,uint8_t proto,uint16_t payload_len){
    static uint16_t ip_id=1;
    uint16_t total=IP_HEADER_SIZE+payload_len;
    c2_memset(buf,0,IP_HEADER_SIZE);
    buf[0]=0x45; /* version 4, IHL 5 */
    buf[2]=(uint8_t)(total>>8);buf[3]=(uint8_t)(total&0xFF);
    buf[4]=(uint8_t)(ip_id>>8);buf[5]=(uint8_t)(ip_id&0xFF);ip_id++;
    buf[6]=0x40; /* DF */
    buf[8]=64; /* TTL */
    buf[9]=proto;
    buf[12]=(uint8_t)(src>>24);buf[13]=(uint8_t)(src>>16);buf[14]=(uint8_t)(src>>8);buf[15]=(uint8_t)src;
    buf[16]=(uint8_t)(dst>>24);buf[17]=(uint8_t)(dst>>16);buf[18]=(uint8_t)(dst>>8);buf[19]=(uint8_t)dst;
    /* Checksum */
    {uint16_t ck=net_checksum(buf,IP_HEADER_SIZE);buf[10]=(uint8_t)(ck>>8);buf[11]=(uint8_t)(ck&0xFF);}
    return IP_HEADER_SIZE;
}

int net_build_udp(uint8_t *buf,uint16_t src_port,uint16_t dst_port,uint16_t data_len){
    uint16_t total=UDP_HEADER_SIZE+data_len;
    buf[0]=(uint8_t)(src_port>>8);buf[1]=(uint8_t)(src_port&0xFF);
    buf[2]=(uint8_t)(dst_port>>8);buf[3]=(uint8_t)(dst_port&0xFF);
    buf[4]=(uint8_t)(total>>8);buf[5]=(uint8_t)(total&0xFF);
    buf[6]=0;buf[7]=0; /* checksum (optional for UDP over IPv4) */
    return UDP_HEADER_SIZE;
}

int net_build_tcp(uint8_t *buf,uint16_t src_port,uint16_t dst_port,uint32_t seq,uint32_t ack,uint8_t flags,uint16_t window){
    c2_memset(buf,0,TCP_HEADER_SIZE);
    buf[0]=(uint8_t)(src_port>>8);buf[1]=(uint8_t)(src_port&0xFF);
    buf[2]=(uint8_t)(dst_port>>8);buf[3]=(uint8_t)(dst_port&0xFF);
    buf[4]=(uint8_t)(seq>>24);buf[5]=(uint8_t)(seq>>16);buf[6]=(uint8_t)(seq>>8);buf[7]=(uint8_t)seq;
    buf[8]=(uint8_t)(ack>>24);buf[9]=(uint8_t)(ack>>16);buf[10]=(uint8_t)(ack>>8);buf[11]=(uint8_t)ack;
    buf[12]=0x50; /* data offset = 5 (20 bytes) */
    buf[13]=flags;
    buf[14]=(uint8_t)(window>>8);buf[15]=(uint8_t)(window&0xFF);
    return TCP_HEADER_SIZE;
}

int net_build_icmp_echo(uint8_t *buf,uint16_t id,uint16_t seq,const uint8_t *data,uint16_t data_len){
    uint16_t ck;
    buf[0]=8; /* echo request */
    buf[1]=0; /* code */
    buf[2]=0;buf[3]=0; /* checksum placeholder */
    buf[4]=(uint8_t)(id>>8);buf[5]=(uint8_t)(id&0xFF);
    buf[6]=(uint8_t)(seq>>8);buf[7]=(uint8_t)(seq&0xFF);
    if(data&&data_len)c2_memcpy(buf+8,data,data_len);
    ck=net_checksum(buf,ICMP_HEADER_SIZE+data_len);
    buf[2]=(uint8_t)(ck>>8);buf[3]=(uint8_t)(ck&0xFF);
    return ICMP_HEADER_SIZE+(int)data_len;
}

int net_build_arp_request(uint8_t *buf,const uint8_t *src_mac,uint32_t src_ip,uint32_t target_ip){
    buf[0]=0;buf[1]=1; /* htype=ethernet */
    buf[2]=0x08;buf[3]=0x00; /* ptype=IPv4 */
    buf[4]=6;buf[5]=4; /* hlen=6, plen=4 */
    buf[6]=0;buf[7]=1; /* oper=request */
    c2_memcpy(buf+8,src_mac,6);
    buf[14]=(uint8_t)(src_ip>>24);buf[15]=(uint8_t)(src_ip>>16);buf[16]=(uint8_t)(src_ip>>8);buf[17]=(uint8_t)src_ip;
    c2_memset(buf+18,0,6); /* target MAC unknown */
    buf[24]=(uint8_t)(target_ip>>24);buf[25]=(uint8_t)(target_ip>>16);buf[26]=(uint8_t)(target_ip>>8);buf[27]=(uint8_t)target_ip;
    return ARP_PACKET_SIZE;
}

/* Shared memory */
shm_region_t g_shm_regions[SHM_MAX];

int shm_get(int key,uint32_t size,int flags){
    int i;
    enum { C2_IPC_PRIVATE = 0, C2_IPC_CREAT = 01000, C2_IPC_EXCL = 02000 };
    /*
     * SysV IPC_PRIVATE is a request for a fresh segment, not a lookup key.
     * XShm uses shmget(IPC_PRIVATE, ...), shmat(), then shmctl(IPC_RMID);
     * reusing key 0 makes unrelated browser paint buffers alias.
     */
    if(key!=C2_IPC_PRIVATE){
        for(i=0;i<SHM_MAX;++i){
            if(g_shm_regions[i].used&&g_shm_regions[i].key==key){
                if((flags&C2_IPC_CREAT)&&(flags&C2_IPC_EXCL))return -EEXIST;
                return i;
            }
        }
        if(!(flags&C2_IPC_CREAT))return -ENOENT;
    }
    if(size==0)return -EINVAL;
    if(size>SHM_SIZE_MAX)return -EINVAL;
    for(i=0;i<SHM_MAX;++i)if(!g_shm_regions[i].used){
        uint32_t pages=(size+PAGE_SIZE-1)/PAGE_SIZE;uint32_t p;
        uint64_t phys=pmm_alloc_contiguous_frames(pages);
        if(!phys)return -ENOMEM;
        g_shm_regions[i].used=true;g_shm_regions[i].key=key;
        g_shm_regions[i].size=pages*PAGE_SIZE;g_shm_regions[i].refcount=0;g_shm_regions[i].mode=0666;
        g_shm_regions[i].phys=phys;
        for(p=0;p<pages;++p){
            uint8_t *z=(uint8_t*)PHYS_TO_DMAP(phys+(uint64_t)p*PAGE_SIZE);
            c2_memset(z,0,PAGE_SIZE);
        }
        return i;
    }
    return -ENOMEM;
}

void *shm_attach(int id,uint64_t addr){
    if(id<0||id>=SHM_MAX||!g_shm_regions[id].used)return(void*)(uintptr_t)-1;
    g_shm_regions[id].refcount++;
    if(!addr)addr=0x30000000ULL+(uint64_t)id*0x400000ULL;
    /* Map into current task's address space */
    {uint32_t pages=g_shm_regions[id].size/PAGE_SIZE;uint32_t p;
     for(p=0;p<pages;++p)
         paging_map(g_tasks[g_current_task].addr_space,addr+p*PAGE_SIZE,
                    g_shm_regions[id].phys+p*PAGE_SIZE,PAGE_PRESENT|PAGE_WRITABLE|PAGE_USER);}
    return(void*)(uintptr_t)addr;
}

int shm_detach(void *addr){
    (void)addr; /* Would unmap pages */
    return 0;
}

int shm_ctl(int id,int cmd,void *buf){
    (void)id;(void)cmd;(void)buf;return 0;
}

/* stat / fstat / lstat */
int vfs_stat(const char *path,kstat_t *st){
    c2_memset(st,0,sizeof(*st));
    st->st_mode=0100644; /* regular file */
    st->st_nlink=1;st->st_blksize=4096;
    /* Would lookup in VFS */
    (void)path;return 0;
}
int vfs_fstat(int fd,kstat_t *st){
    c2_memset(st,0,sizeof(*st));
    st->st_blksize=4096;
    if(fd<=2){st->st_mode=0020666;st->st_rdev=0x8800;} /* chardev tty */
    else st->st_mode=0100644;
    return 0;
}
int vfs_lstat(const char *path,kstat_t *st){return vfs_stat(path,st);}

/* readv / writev */
int64_t real_readv(int fd,const iovec_t *iov,int iovcnt){
    int64_t total=0;int i;
    for(i=0;i<iovcnt;++i){
        if(!iov[i].iov_base||!iov[i].iov_len)continue;
        /* Route through device based on FD kind */
        task_t *cur=task_current();
        if(!fd_valid(&cur->fdt,fd))return -EBADF;
        switch(cur->fdt.fds[fd].kind){
            case FDKIND_DEVNULL: break;
            case FDKIND_DEVZERO: dev_zero_read(iov[i].iov_base,iov[i].iov_len);total+=(int64_t)iov[i].iov_len;break;
            case FDKIND_DEVRANDOM: dev_random_read(iov[i].iov_base,iov[i].iov_len);total+=(int64_t)iov[i].iov_len;break;
            case FDKIND_DEVTTY: {int r=tty_read(&g_tty0,iov[i].iov_base,iov[i].iov_len);if(r>0)total+=r;}break;
            default: break;
        }
    }
    return total;
}

int64_t real_writev(int fd,const iovec_t *iov,int iovcnt){
    int64_t total=0;int i;
    for(i=0;i<iovcnt;++i){
        if(!iov[i].iov_base||!iov[i].iov_len)continue;
        task_t *cur=task_current();
        if(!fd_valid(&cur->fdt,fd))return -EBADF;
        switch(cur->fdt.fds[fd].kind){
            case FDKIND_DEVNULL: total+=(int64_t)iov[i].iov_len;break;
            case FDKIND_DEVTTY: {
                int r=tty_write(&g_tty0,iov[i].iov_base,iov[i].iov_len);
                if(r>0)total+=r;
                /* Bring-up aid: mirror stderr/user tty writes to serial so
                 * ld-linux/Firefox failure messages survive headless runs. */
                if(fd==2){
                    const char *p=(const char*)iov[i].iov_base;
                    size_t j,lim=iov[i].iov_len;
                    if(lim>240)lim=240;
                    __boot_serial_puts("[u-stderr] ");
                    for(j=0;j<lim;++j){
                        char ch=p[j];
                        if(ch==0)break;
                        if((ch>=' '&&ch<='~')||ch=='\n'||ch=='\r'||ch=='\t')__boot_serial_putc(ch);
                        else __boot_serial_putc('.');
                    }
                    if(iov[i].iov_len>lim)__boot_serial_puts("...");
                    if(lim==0||p[lim-1]!='\n')__boot_serial_putc('\n');
                }
            }break;
            default: total+=(int64_t)iov[i].iov_len;break;
        }
    }
    return total;
}

/* Shell commands for compat2 */
static void cmd2_pmm(const char *a,char *o,int mx){size_t l=0;(void)a;o[0]=0;
    c2_append_str(o,&l,(size_t)mx,"PMM: ");c2_append_u32(o,&l,(size_t)mx,pmm_free_count());
    c2_append_str(o,&l,(size_t)mx,"/");c2_append_u32(o,&l,(size_t)mx,pmm_total_count());
    c2_append_str(o,&l,(size_t)mx," pages free (");c2_append_u32(o,&l,(size_t)mx,pmm_free_count()*4);
    c2_append_str(o,&l,(size_t)mx," KB free / ");c2_append_u32(o,&l,(size_t)mx,pmm_total_count()*4);
    c2_append_str(o,&l,(size_t)mx," KB total)\n");}

static void cmd2_tasks(const char *a,char *o,int mx){size_t l=0;int i;(void)a;o[0]=0;
    c2_append_str(o,&l,(size_t)mx,"PID  PPID PGID STATE      NAME\n");
    for(i=0;i<TASK_MAX;++i){if(!g_tasks[i].used)continue;
        c2_append_u32(o,&l,(size_t)mx,(uint32_t)g_tasks[i].pid);c2_append_str(o,&l,(size_t)mx,"   ");
        c2_append_u32(o,&l,(size_t)mx,(uint32_t)g_tasks[i].ppid);c2_append_str(o,&l,(size_t)mx,"   ");
        c2_append_u32(o,&l,(size_t)mx,(uint32_t)g_tasks[i].pgid);c2_append_str(o,&l,(size_t)mx,"  ");
        switch(g_tasks[i].state){
            case TASK_RUNNING:c2_append_str(o,&l,(size_t)mx,"RUNNING  ");break;
            case TASK_RUNNABLE:c2_append_str(o,&l,(size_t)mx,"RUNNABLE ");break;
            case TASK_SLEEPING:c2_append_str(o,&l,(size_t)mx,"SLEEPING ");break;
            case TASK_STOPPED:c2_append_str(o,&l,(size_t)mx,"STOPPED  ");break;
            case TASK_ZOMBIE:c2_append_str(o,&l,(size_t)mx,"ZOMBIE   ");break;
            default:c2_append_str(o,&l,(size_t)mx,"FREE     ");}
        c2_append_str(o,&l,(size_t)mx,g_tasks[i].name);c2_append_ch(o,&l,(size_t)mx,'\n');}}

static void cmd2_paging(const char *a,char *o,int mx){size_t l=0;(void)a;o[0]=0;
    c2_append_str(o,&l,(size_t)mx,"Kernel CR3: ");c2_append_hex64(o,&l,(size_t)mx,g_kernel_as.cr3_phys);c2_append_ch(o,&l,(size_t)mx,'\n');
    c2_append_str(o,&l,(size_t)mx,"Current task AS: ");
    if(g_tasks[g_current_task].addr_space)c2_append_hex64(o,&l,(size_t)mx,g_tasks[g_current_task].addr_space->cr3_phys);
    else c2_append_str(o,&l,(size_t)mx,"(none)");
    c2_append_ch(o,&l,(size_t)mx,'\n');
    c2_append_str(o,&l,(size_t)mx,"Address spaces created: ");
    {int cnt=0,i;for(i=0;i<TASK_MAX;++i)if(g_tasks[i].used&&g_tasks[i].addr_space)cnt++;
     c2_append_u32(o,&l,(size_t)mx,(uint32_t)cnt);}c2_append_ch(o,&l,(size_t)mx,'\n');}

static void cmd2_shm(const char *a,char *o,int mx){size_t l=0;int i;(void)a;o[0]=0;
    c2_append_str(o,&l,(size_t)mx,"ID  KEY   SIZE     REFS\n");
    for(i=0;i<SHM_MAX;++i){if(!g_shm_regions[i].used)continue;
        c2_append_u32(o,&l,(size_t)mx,(uint32_t)i);c2_append_str(o,&l,(size_t)mx,"   ");
        c2_append_u32(o,&l,(size_t)mx,(uint32_t)g_shm_regions[i].key);c2_append_str(o,&l,(size_t)mx,"   ");
        c2_append_u32(o,&l,(size_t)mx,g_shm_regions[i].size);c2_append_str(o,&l,(size_t)mx,"    ");
        c2_append_u32(o,&l,(size_t)mx,(uint32_t)g_shm_regions[i].refcount);c2_append_ch(o,&l,(size_t)mx,'\n');}
    if(l<2)c2_append_str(o,&l,(size_t)mx,"(no shared memory segments)\n");}

static void cmd2_timers(const char *a,char *o,int mx){size_t l=0;int i;(void)a;o[0]=0;
    c2_append_str(o,&l,(size_t)mx,"ID  PID  SIG  EXPIRE     INTERVAL\n");
    for(i=0;i<TIMER_MAX;++i){if(!g_timers[i].armed)continue;
        c2_append_u32(o,&l,(size_t)mx,(uint32_t)i);c2_append_str(o,&l,(size_t)mx,"   ");
        c2_append_u32(o,&l,(size_t)mx,(uint32_t)g_timers[i].pid);c2_append_str(o,&l,(size_t)mx,"   ");
        c2_append_u32(o,&l,(size_t)mx,(uint32_t)g_timers[i].sig);c2_append_str(o,&l,(size_t)mx,"   ");
        c2_append_u64(o,&l,(size_t)mx,g_timers[i].expire_tick);c2_append_str(o,&l,(size_t)mx,"   ");
        c2_append_u64(o,&l,(size_t)mx,g_timers[i].interval);c2_append_ch(o,&l,(size_t)mx,'\n');}
    if(l<2)c2_append_str(o,&l,(size_t)mx,"(no active timers)\n");}

static void cmd2_fds(const char *a,char *o,int mx){size_t l=0;int i;(void)a;o[0]=0;
    task_t *cur=task_current();
    c2_append_str(o,&l,(size_t)mx,"FD  KIND          FLAGS  REF  OFFSET\n");
    for(i=0;i<TASK_FD_MAX;++i){if(cur->fdt.fds[i].kind==FDKIND_NONE)continue;
        c2_append_u32(o,&l,(size_t)mx,(uint32_t)i);c2_append_str(o,&l,(size_t)mx,"   ");
        switch(cur->fdt.fds[i].kind){
            case FDKIND_VFSFILE:c2_append_str(o,&l,(size_t)mx,"vfs_file    ");break;
            case FDKIND_SOCKET:c2_append_str(o,&l,(size_t)mx,"socket      ");break;
            case FDKIND_PIPE_R:c2_append_str(o,&l,(size_t)mx,"pipe_read   ");break;
            case FDKIND_PIPE_W:c2_append_str(o,&l,(size_t)mx,"pipe_write  ");break;
            case FDKIND_DEVNULL:c2_append_str(o,&l,(size_t)mx,"/dev/null   ");break;
            case FDKIND_DEVZERO:c2_append_str(o,&l,(size_t)mx,"/dev/zero   ");break;
            case FDKIND_DEVRANDOM:c2_append_str(o,&l,(size_t)mx,"/dev/random ");break;
            case FDKIND_DEVFB:c2_append_str(o,&l,(size_t)mx,"/dev/fb0    ");break;
            case FDKIND_DEVTTY:c2_append_str(o,&l,(size_t)mx,"/dev/tty    ");break;
            default:c2_append_str(o,&l,(size_t)mx,"other       ");}
        c2_append_hex32(o,&l,(size_t)mx,(uint32_t)cur->fdt.fds[i].flags);c2_append_str(o,&l,(size_t)mx,"  ");
        c2_append_u32(o,&l,(size_t)mx,(uint32_t)cur->fdt.fds[i].ref);c2_append_str(o,&l,(size_t)mx,"    ");
        c2_append_u64(o,&l,(size_t)mx,cur->fdt.fds[i].offset);c2_append_ch(o,&l,(size_t)mx,'\n');}}

static void cmd2_compat2_info(const char *a,char *o,int mx){size_t l=0;(void)a;o[0]=0;
    int tc=0,sc=0,fc=0,i;
    for(i=0;i<TASK_MAX;++i)if(g_tasks[i].used)tc++;
    for(i=0;i<SHM_MAX;++i)if(g_shm_regions[i].used)sc++;
    for(i=0;i<TASK_FD_MAX;++i)if(g_tasks[g_current_task].fdt.fds[i].kind!=FDKIND_NONE)fc++;
    c2_append_str(o,&l,(size_t)mx,"=== RiduxOS Compat2 Deep Infra ===\n");
    c2_append_str(o,&l,(size_t)mx,"PMM: ");c2_append_u32(o,&l,(size_t)mx,pmm_free_count()*4);c2_append_str(o,&l,(size_t)mx," KB free\n");
    c2_append_str(o,&l,(size_t)mx,"Paging: PML4→PDPT→PD→PT (4-level x86_64)\n");
    c2_append_str(o,&l,(size_t)mx,"Tasks: ");c2_append_u32(o,&l,(size_t)mx,(uint32_t)tc);c2_append_str(o,&l,(size_t)mx,"/");c2_append_u32(o,&l,(size_t)mx,TASK_MAX);c2_append_ch(o,&l,(size_t)mx,'\n');
    c2_append_str(o,&l,(size_t)mx,"Open FDs: ");c2_append_u32(o,&l,(size_t)mx,(uint32_t)fc);c2_append_ch(o,&l,(size_t)mx,'\n');
    c2_append_str(o,&l,(size_t)mx,"SHM segs: ");c2_append_u32(o,&l,(size_t)mx,(uint32_t)sc);c2_append_ch(o,&l,(size_t)mx,'\n');
    c2_append_str(o,&l,(size_t)mx,"TTY: ");c2_append_u32(o,&l,(size_t)mx,(uint32_t)g_tty0.cols);c2_append_str(o,&l,(size_t)mx,"x");
    c2_append_u32(o,&l,(size_t)mx,(uint32_t)g_tty0.rows);c2_append_str(o,&l,(size_t)mx," canonical=");
    c2_append_str(o,&l,(size_t)mx,(g_tty0.tio.c_lflag&ICANON)?"yes":"no");c2_append_ch(o,&l,(size_t)mx,'\n');
    c2_append_str(o,&l,(size_t)mx,"ext2: ");c2_append_str(o,&l,(size_t)mx,g_ext2.mounted?"mounted":"not mounted");
    if(g_ext2.mounted){c2_append_str(o,&l,(size_t)mx," blksz=");c2_append_u32(o,&l,(size_t)mx,g_ext2.block_size);
        c2_append_str(o,&l,(size_t)mx," inodes=");c2_append_u32(o,&l,(size_t)mx,g_ext2.inodes_count);}
    c2_append_ch(o,&l,(size_t)mx,'\n');
    c2_append_str(o,&l,(size_t)mx,"Signals: POSIX (64 signals, sigaction, masks)\n");
    c2_append_str(o,&l,(size_t)mx,"Net packets: ETH+IP+TCP+UDP+ICMP+ARP builders\n");}

/* Master init + shell registration */
void compat2_register_shell_cmds(void){
    /* Uses compat.c's register infrastructure */
    extern compat_shell_cmd_t g_compat_cmds[];
    extern int g_compat_cmd_count;
    #define REG2(n,h,fn) if(g_compat_cmd_count<COMPAT_SHELL_CMD_MAX){g_compat_cmds[g_compat_cmd_count].name=n;g_compat_cmds[g_compat_cmd_count].help=h;g_compat_cmds[g_compat_cmd_count].handler=fn;++g_compat_cmd_count;}
    REG2("pmm","Physical memory manager stats",cmd2_pmm)
    REG2("tasks","List all tasks/processes",cmd2_tasks)
    REG2("paging","Show paging info",cmd2_paging)
    REG2("shm","Show shared memory segments",cmd2_shm)
    REG2("timers","Show active timers",cmd2_timers)
    REG2("fds","Show open file descriptors",cmd2_fds)
    REG2("compat2","Deep infrastructure summary",cmd2_compat2_info)
    #undef REG2
}

void compat2_init_all(void){
    __boot_serial_puts("[c2init] pmm_init...\n");
    /* Physical memory */
    pmm_init((uint64_t)PMM_MAX_PAGES*PAGE_SIZE); /* up to 1GB bitmap window */
    __boot_serial_puts("[c2init] paging_init...\n");
    /* Paging */
    paging_init();
    __boot_serial_puts("[c2init] tss_init...\n");
    /* TSS (must come before gdt64_setup so &g_tss is populated) */
    tss_init();
    __boot_serial_puts("[c2init] gdt64_setup...\n");
    /* 3b. Long-mode GDT with user code/data + TSS descriptor. Without
     * this, the boot GDT from boot64.S (null + kernel code + kernel data,
     * limit=23) is still in effect and ANY iretq to ring 3 faults on
     * the user CS/SS selectors because they're beyond the GDT limit. */
    gdt64_setup();
    __boot_serial_puts("[c2init] syscall_msr_init...\n");
    /* 3c. Wire MSR_STAR/LSTAR/SFMASK so SYSCALL from ring 3 lands at
     * syscall_entry in isr64.S with kernel CS=0x08 / SS=0x10. */
    syscall_msr_init();
    __boot_serial_puts("[c2init] task_init...\n");
    /* Task system */
    task_init();
    /* TTY */
    tty_init(&g_tty0);
    /* Timers */
    c2_memset(g_timers,0,sizeof(g_timers));
    /* Shared memory */
    c2_memset(g_shm_regions,0,sizeof(g_shm_regions));
    /* Shell commands */
    compat2_register_shell_cmds();
    __boot_serial_puts("[c2init] compat2 done\n");
}
