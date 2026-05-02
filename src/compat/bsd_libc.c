/*
 * Funciones de libc adaptadas de FreeBSD y helpers para procesos Ring 3.
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "base.h"
#include "memory_tasks.h"
#include "linux_syscalls.h"
#include "user_libc.h"
#include "bsd_libc.h"
extern void*ulibc_malloc(size_t);extern void ulibc_free(void*);
extern void*ulibc_memcpy(void*,const void*,size_t);extern void*ulibc_memset(void*,int,size_t);
extern size_t ulibc_strlen(const char*);extern char*ulibc_strcpy(char*,const char*);
extern int ulibc_strcmp(const char*,const char*);extern char*ulibc_strchr(const char*,int);
extern int ulibc_isdigit(int);extern int ulibc_toupper(int);extern int ulibc_tolower(int);extern int ulibc_isspace(int);
extern int ulibc_snprintf(char*,size_t,const char*,...);
extern bool kvfs_read(const char *p, const uint8_t **data, uint32_t *size);
extern int64_t real_sys_userfaultfd(int flags);
extern int64_t real_sys_process_vm_readv(int pid,const iovec_t *local_iov,unsigned long liovcnt,const iovec_t *remote_iov,unsigned long riovcnt,unsigned long flags);
extern int64_t real_sys_process_vm_writev(int pid,const iovec_t *local_iov,unsigned long liovcnt,const iovec_t *remote_iov,unsigned long riovcnt,unsigned long flags);

/* fnmatch (FreeBSD adapted, no wchar) */
int ulibc_fnmatch(const char*P,const char*S,int F){
    const char*btp=0,*bts=0;char pc,sc;
    for(;;){pc=*P++;sc=*S;
    switch(pc){
    case 0:return sc?FNM_NOMATCH:0;
    case '?':if(!sc)return FNM_NOMATCH;if(sc=='/'&&(F&FNM_PATHNAME))goto bt;++S;break;
    case '*':while(*P=='*')++P;if(!*P)return(F&FNM_PATHNAME)&&ulibc_strchr(S,'/')?FNM_NOMATCH:0;
        if(*P=='/'&&(F&FNM_PATHNAME)){S=ulibc_strchr(S,'/');if(!S)return FNM_NOMATCH;break;}
        btp=P;bts=S;break;
    case '[':{if(!sc)return FNM_NOMATCH;if(sc=='/'&&(F&FNM_PATHNAME))goto bt;
        int neg=0,ok=0;const char*op=P;if(*P=='!'||*P=='^'){neg=1;++P;}
        for(;;){char c=*P++;if(c==']'&&P>op+1)break;if(!c)goto bt;char c2=0;
            if(*P=='-'&&P[1]&&P[1]!=']'){c2=P[2];P+=3;if(F&FNM_CASEFOLD){c=ulibc_tolower(c);c2=ulibc_tolower(c2);sc=ulibc_tolower(sc);}
                if(c<=sc&&sc<=c2){ok=1;break;}}else{if(F&FNM_CASEFOLD){c=ulibc_tolower(c);sc=ulibc_tolower(sc);}if(c==sc){ok=1;break;}}}
        while(*P!=']'){if(!*P)goto bt;++P;}++P;++S;if(ok==neg)goto bt;break;}
    case '\\':if(!(F&FNM_NOESCAPE)){pc=*P++;if(!pc)return FNM_NOMATCH;}
    default:++S;if(pc==sc);else if((F&FNM_CASEFOLD)&&ulibc_tolower((unsigned char)pc)==ulibc_tolower((unsigned char)sc));else{
        bt:if(!btp)return FNM_NOMATCH;if(!*bts)return FNM_NOMATCH;++bts;P=btp;S=bts;}break;
    }}
}

/* base64 (FreeBSD adapted) */
static const char B64[]="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
static const char BP='=';
int ulibc_b64_ntop(const uint8_t*s,size_t n,char*t,size_t ts){
    size_t d=0;unsigned i[3],o[4];size_t j;
    while(n>2){i[0]=*s++;i[1]=*s++;i[2]=*s++;n-=3;
        o[0]=i[0]>>2;o[1]=((i[0]&3)<<4)+(i[1]>>4);o[2]=((i[1]&0xf)<<2)+(i[2]>>6);o[3]=i[2]&0x3f;
        if(d+4>ts)return-1;t[d++]=B64[o[0]];t[d++]=B64[o[1]];t[d++]=B64[o[2]];t[d++]=B64[o[3]];}
    if(n){i[0]=i[1]=i[2]=0;for(j=0;j<n;++j)i[j]=*s++;
        o[0]=i[0]>>2;o[1]=((i[0]&3)<<4)+(i[1]>>4);o[2]=((i[1]&0xf)<<2)+(i[2]>>6);
        if(d+4>ts)return-1;t[d++]=B64[o[0]];t[d++]=B64[o[1]];t[d++]=(n==1)?BP:B64[o[2]];t[d++]=BP;}
    if(d>=ts)return-1;t[d]=0;return(int)d;
}
int ulibc_b64_pton(const char*s,uint8_t*t,size_t ts){
    int ti=0,st=0,ch;const char*p;uint8_t nb;
    while((ch=*s++)!=0){if(ulibc_isspace(ch))continue;if(ch==BP)break;p=ulibc_strchr(B64,ch);if(!p)return-1;
        switch(st){
        case 0:if((size_t)ti>=ts)return-1;t[ti]=(uint8_t)((p-B64)<<2);st=1;break;
        case 1:if((size_t)ti>=ts)return-1;t[ti]|=(uint8_t)((p-B64)>>4);nb=(uint8_t)(((p-B64)&0xf)<<4);
            if((size_t)ti+1<ts)t[ti+1]=nb;else if(nb)return-1;++ti;st=2;break;
        case 2:if((size_t)ti>=ts)return-1;t[ti]|=(uint8_t)((p-B64)>>2);nb=(uint8_t)(((p-B64)&3)<<6);
            if((size_t)ti+1<ts)t[ti+1]=nb;else if(nb)return-1;++ti;st=3;break;
        case 3:if((size_t)ti>=ts)return-1;t[ti]|=(uint8_t)(p-B64);++ti;st=0;break;}}
    if(ch==BP){if(st==0||st==1)return-1;}else if(st!=0)return-1;return ti;
}
int ulibc_base64_encode(const void*s,size_t n,char*t,size_t ts){return ulibc_b64_ntop(s,n,t,ts);}
int ulibc_base64_decode(const char*s,void*t,size_t ts){return ulibc_b64_pton(s,t,ts);}

/* arc4random (ChaCha20, OpenBSD/FreeBSD adapted) */
#define CR(v,n)(((v)<<(n))|((v)>>(32-(n))))
#define QR(a,b,c,d)a+=b;d^=a;d=CR(d,16);c+=d;b^=c;b=CR(b,12);a+=b;d^=a;d=CR(d,8);c+=d;b^=c;b=CR(b,7);
static void cc20(uint32_t o[16],const uint32_t s[16]){uint32_t x[16];int i;for(i=0;i<16;++i)x[i]=s[i];
    for(i=0;i<20;++i){QR(x[0],x[4],x[8],x[12]);QR(x[1],x[5],x[9],x[13]);QR(x[2],x[6],x[10],x[14]);QR(x[3],x[7],x[11],x[15]);
        QR(x[0],x[5],x[10],x[15]);QR(x[1],x[6],x[11],x[12]);QR(x[2],x[7],x[8],x[13]);QR(x[3],x[4],x[9],x[14]);}
    for(i=0;i<16;++i)o[i]=x[i]+s[i];}
static uint32_t g_a4s[16];static uint8_t g_a4b[256];static size_t g_a4h=0,g_a4c=0;static bool g_a4k=false;
static const uint8_t g_sig[16]="expand 32-byte k";
static void a4rk(void){uint32_t o[16];int i;cc20(o,g_a4s);for(i=0;i<16;++i)g_a4s[i]=o[i];g_a4h=192;ulibc_memcpy(g_a4b,((uint8_t*)o)+64,192);}
static void a4st(void){uint8_t r[32];int i;uint64_t t;uint32_t lo,hi;__asm__ volatile("rdtsc":"=a"(lo),"=d"(hi));t=((uint64_t)hi<<32)|lo;
    for(i=0;i<32;++i)r[i]=(uint8_t)(t>>(i%8));uint64_t s=t+1;for(i=0;i<32;++i){s^=s<<13;s^=s>>7;s^=s<<17;r[i]^=(uint8_t)(s>>(i%8));}
    ulibc_memcpy(g_a4s,g_sig,16);ulibc_memcpy(((uint8_t*)g_a4s)+16,r,32);ulibc_memset(((uint8_t*)g_a4s)+48,0,16);
    g_a4s[14]=*(uint32_t*)(r+16);g_a4s[15]=*(uint32_t*)(r+24);g_a4k=true;g_a4h=0;g_a4c=1<<20;a4rk();}
uint32_t ulibc_arc4random(void){uint32_t v;if(!g_a4k||g_a4c<=4)a4st();if(g_a4h<4)a4rk();
    v=*(uint32_t*)(g_a4b+256-g_a4h);ulibc_memset(g_a4b+256-g_a4h,0,4);g_a4h-=4;g_a4c-=4;return v;}
void ulibc_arc4random_buf(void*b,size_t n){uint8_t*p=b;while(n){size_t a;if(!g_a4k||g_a4c<=n)a4st();if(!g_a4h)a4rk();
    a=n<g_a4h?n:g_a4h;ulibc_memcpy(p,g_a4b+256-g_a4h,a);ulibc_memset(g_a4b+256-g_a4h,0,a);p+=a;n-=a;g_a4h-=a;g_a4c-=a;}}
uint32_t ulibc_arc4random_uniform(uint32_t u){if(u<2)return 0;uint32_t m=(uint32_t)(-(int32_t)u)%u,r;do{r=ulibc_arc4random();}while(r<m);return r%u;}

/* regex (simplified NFA) */
#define RE_MX 4096
typedef enum{RE_END,RE_DOT,RE_CH,RE_SP,RE_JM,RE_SV,RE_CL} re_op_t;
typedef struct{re_op_t op;uint32_t c,c2;int32_t t;} re_inst_t;
static int re_comp(re_inst_t*co,int mx,const char*p,int cf,size_t*ng){
    int pc=0,g=0;*ng=0;co[pc].op=RE_SV;co[pc].c=0;++pc;
    while(*p&&pc<mx-4){
        if(*p=='('){co[pc].op=RE_SV;co[pc].c=(uint32_t)(++g)*2;++pc;++p;continue;}
        if(*p==')'){co[pc].op=RE_SV;co[pc].c=(uint32_t)g*2+1;++pc;++p;continue;}
        if(*p=='.'){co[pc].op=RE_DOT;++pc;++p;continue;}
        if(*p=='*'&&pc>1){int v=pc-1,s=pc;co[pc].op=RE_SP;co[pc].t=v;co[pc].c2=pc+2;++pc;co[pc].op=RE_JM;co[pc].t=s;++pc;++p;continue;}
        if(*p=='+'&&pc>1){co[pc].op=RE_SP;co[pc].t=pc-1;co[pc].c2=pc+2;++pc;++p;continue;}
        if(*p=='?'&&pc>1){co[pc].op=RE_SP;co[pc].t=pc+2;co[pc].c2=pc-1;++pc;co[pc].op=RE_JM;co[pc].t=pc;++pc;++p;continue;}
        if(*p=='\\'){++p;if(!*p)return REG_EESCAPE;}
        if(*p=='['){int neg=0;++p;if(*p=='^'){neg=1;++p;}
            uint8_t*bm=(uint8_t*)ulibc_malloc(32);ulibc_memset(bm,0,32);
            while(*p&&*p!=']'){char lo=*p++;if(*p=='-'&&p[1]&&p[1]!=']'){char hi=p[2];p+=3;int k;for(k=(int)lo;k<=(int)hi;++k)bm[(uint8_t)k/8]|=1<<((uint8_t)k%8);}else bm[(uint8_t)lo/8]|=1<<((uint8_t)lo%8);}
            if(*p==']')++p;co[pc].op=RE_CL;co[pc].c=(uint32_t)neg;co[pc].t=(int32_t)(uintptr_t)bm;++pc;continue;}
        co[pc].op=RE_CH;co[pc].c=(uint32_t)(unsigned char)*p;++pc;++p;}
    co[pc].op=RE_SV;co[pc].c=1;++pc;co[pc].op=RE_END;*ng=(size_t)g+1;return 0;
}
static int re_mat(const re_inst_t*co,const char*s,regmatch_t*pm,int nm){
    int sp=0,si=0,sv[64],ns=0;if(pm&&nm>0){pm[0].rm_so=-1;pm[0].rm_eo=-1;}
    while(co[sp].op!=RE_END){switch(co[sp].op){
    case RE_CH:if((unsigned char)s[si]!=co[sp].c)return 1;++si;break;
    case RE_DOT:if(!s[si])return 1;++si;break;
    case RE_CL:{uint8_t*bm=(uint8_t*)(uintptr_t)co[sp].t;int c=s[si];if(!c)return 1;int h=bm[(uint8_t)c/8]&(1<<((uint8_t)c%8));if(co[sp].c)h=!h;if(!h)return 1;++si;break;}
    case RE_SP:sp=co[sp].t;break;case RE_JM:sp=co[sp].t;continue;
    case RE_SV:if(ns<62){sv[ns++]=co[sp].c;sv[ns++]=si;}break;default:break;}++sp;}
    if(pm&&nm>0){int i;for(i=0;i<ns;i+=2){int x=sv[i]/2;if(x<nm){if(sv[i]%2==0)pm[x].rm_so=sv[i+1];else pm[x].rm_eo=sv[i+1];}}}return 0;
}
int ulibc_regcomp(ulibc_regex_t*pr,const char*re,int cf){re_inst_t*co=(re_inst_t*)ulibc_malloc(sizeof(re_inst_t)*RE_MX);if(!co)return REG_ESPACE;size_t ng=0;int r=re_comp(co,RE_MX,re,cf,&ng);if(r){ulibc_free(co);return r;}pr->re_compiled=co;pr->re_nsub=ng;pr->re_cflags=cf;return 0;}
int ulibc_regexec(const ulibc_regex_t*pr,const char*s,size_t nm,regmatch_t pm[],int ef){if(!pr||!pr->re_compiled)return REG_NOMATCH;(void)ef;return re_mat((const re_inst_t*)pr->re_compiled,s,pm,(int)nm);}
void ulibc_regfree(ulibc_regex_t*pr){if(pr&&pr->re_compiled){ulibc_free(pr->re_compiled);pr->re_compiled=0;}}
size_t ulibc_regerror(int ec,const ulibc_regex_t*p,char*b,size_t bs){(void)p;(void)ec;const char*m="regex error";size_t l=ulibc_strlen(m);if(bs>0){ulibc_strncpy(b,m,bs-1);b[bs-1]=0;}return l;}

/* glob (simplified) */
int ulibc_glob(const char*pat,int fl,int(*ef)(const char*,int),ulibc_glob_t*pg){
    (void)ef;if(!pg)return-1;pg->gl_pathc=0;pg->gl_pathv=0;pg->gl_offs=0;
    const char*w=pat;int hw=0;while(*w){if(*w=='*'||*w=='?'||*w=='['){hw=1;break;}++w;}
    if(!hw){if(fl&GLOB_NOCHECK){pg->gl_pathv=(char**)ulibc_malloc(2*sizeof(char*));
        pg->gl_pathv[0]=ulibc_malloc(ulibc_strlen(pat)+1);ulibc_strcpy(pg->gl_pathv[0],pat);pg->gl_pathv[1]=0;pg->gl_pathc=1;return 0;}
        return GLOB_NOMATCH;}return GLOB_NOMATCH;
}
void ulibc_globfree(ulibc_glob_t*pg){if(!pg||!pg->gl_pathv)return;size_t i;for(i=0;i<pg->gl_pathc;++i)ulibc_free(pg->gl_pathv[i]);ulibc_free(pg->gl_pathv);pg->gl_pathv=0;pg->gl_pathc=0;}

/* scandir / realpath */
int ulibc_scandir(const char*d,ulibc_dirent_t***nl,int(*f)(const ulibc_dirent_t*),int(*c)(const ulibc_dirent_t**,const ulibc_dirent_t**)){(void)d;(void)f;(void)c;if(nl)*nl=0;return 0;}
char*ulibc_realpath(const char*p,char*r){if(!p)return 0;if(!r)r=(char*)ulibc_malloc(4096);ulibc_strcpy(r,p);return r;}

/* DNS stubs */
uint16_t ulibc_htons(uint16_t v){return(uint16_t)((v<<8)|(v>>8));}
uint16_t ulibc_ntohs(uint16_t v){return ulibc_htons(v);}
uint32_t ulibc_htonl(uint32_t v){return((v&0xFF)<<24)|((v&0xFF00)<<8)|((v>>8)&0xFF00)|((v>>24)&0xFF);}
uint32_t ulibc_ntohl(uint32_t v){return ulibc_htonl(v);}
static int c5_parse_ipv4(const char *cp,uint8_t out[4]){
    int i;
    if(!cp||!out)return 0;
    for(i=0;i<4;++i){
        uint32_t p=0;
        int digs=0;
        while(*cp&&*cp!='.'){
            if(!ulibc_isdigit((unsigned char)*cp))return 0;
            p=p*10u+(uint32_t)(*cp-'0');
            if(p>255u)return 0;
            ++cp;++digs;
        }
        if(!digs)return 0;
        out[i]=(uint8_t)p;
        if(i<3){
            if(*cp!='.')return 0;
            ++cp;
        }
    }
    return *cp==0;
}
static uint16_t c5_service_to_port(const char *svc){
    uint32_t p=0;
    if(!svc||!*svc)return 0;
    if(ulibc_strcmp(svc,"http")==0)return 80;
    if(ulibc_strcmp(svc,"https")==0)return 443;
    if(ulibc_strcmp(svc,"dns")==0)return 53;
    if(ulibc_strcmp(svc,"ws")==0)return 80;
    if(ulibc_strcmp(svc,"wss")==0)return 443;
    while(*svc){
        if(!ulibc_isdigit((unsigned char)*svc))return 0;
        p=p*10u+(uint32_t)(*svc-'0');
        if(p>65535u)return 0;
        ++svc;
    }
    return (uint16_t)p;
}
static char c5ai_ascii_tolower(char ch){
    if(ch>='A'&&ch<='Z')return (char)(ch+('a'-'A'));
    return ch;
}
static const char *c5ai_skip_ws(const char *p){
    while(p&&*p&&ulibc_isspace((unsigned char)*p))++p;
    return p;
}
static void c5ai_strip_comment(char *line){
    size_t i=0;
    if(!line)return;
    while(line[i]){
        if(line[i]=='#'||line[i]==';'){line[i]=0;break;}
        ++i;
    }
}
static bool c5ai_token_next(const char **pp,char *tok,size_t cap){
    const char *p=c5ai_skip_ws(*pp);
    size_t n=0;
    if(!p||!*p||!tok||cap<2){if(tok&&cap)tok[0]=0;*pp=p;return false;}
    while(*p&&!ulibc_isspace((unsigned char)*p)){
        if(n+1<cap)tok[n++]=*p;
        ++p;
    }
    tok[n]=0;
    *pp=p;
    return n>0;
}
static void c5ai_copy_host_norm(const char *src,char *dst,size_t cap,bool strip_dot){
    size_t i=0,n=0;
    if(!dst||cap<2){return;}
    dst[0]=0;
    if(!src)return;
    while(src[i]&&n+1<cap){
        char ch=src[i++];
        if(ulibc_isspace((unsigned char)ch))break;
        dst[n++]=c5ai_ascii_tolower(ch);
    }
    while(strip_dot&&n>0&&dst[n-1]=='.')--n;
    dst[n]=0;
}
static bool c5ai_host_equal(const char *a,const char *b){
    char na[128],nb[128];
    c5ai_copy_host_norm(a,na,sizeof(na),true);
    c5ai_copy_host_norm(b,nb,sizeof(nb),true);
    return ulibc_strcmp(na,nb)==0;
}
static int c5ai_count_dots(const char *s){
    int d=0;
    if(!s)return 0;
    while(*s){if(*s=='.')++d;++s;}
    return d;
}
static bool c5ai_starts_with_ci(const char *s,const char *prefix){
    size_t i=0;
    if(!s||!prefix)return false;
    while(prefix[i]){
        if(!s[i])return false;
        if(c5ai_ascii_tolower(s[i])!=c5ai_ascii_tolower(prefix[i]))return false;
        ++i;
    }
    return true;
}
static bool c5ai_read_line(const uint8_t *data,size_t size,size_t *off,char *line,size_t cap){
    size_t n=0;
    if(!data||!off||!line||cap<2||*off>=size)return false;
    while(*off<size&&data[*off]!='\n'){
        if(n+1<cap)line[n++]=(char)data[*off];
        ++(*off);
    }
    if(*off<size&&data[*off]=='\n')++(*off);
    line[n]=0;
    return true;
}

#define C5AI_RESV_NS_MAX 4
#define C5AI_RESV_SEARCH_MAX 6
typedef struct {
    uint32_t nameservers[C5AI_RESV_NS_MAX];
    int nameserver_count;
    char search[C5AI_RESV_SEARCH_MAX][64];
    int search_count;
    int ndots;
    bool rotate;
} c5ai_resolv_conf_t;
typedef enum {
    C5AI_NSS_FILES = 1,
    C5AI_NSS_DNS   = 2
} c5ai_nss_src_t;
typedef struct {
    c5ai_nss_src_t order[4];
    int count;
} c5ai_nss_hosts_t;

static void c5ai_resolv_defaults(c5ai_resolv_conf_t *rc){
    if(!rc)return;
    ulibc_memset(rc,0,sizeof(*rc));
    rc->ndots=1;
    rc->nameservers[0]=((uint32_t)10u<<24)|((uint32_t)0u<<16)|((uint32_t)2u<<8)|3u; /* 0.2.3 */
    rc->nameserver_count=1;
}
static void c5ai_resolv_add_search(c5ai_resolv_conf_t *rc,const char *domain){
    size_t i;
    if(!rc||!domain||!domain[0])return;
    for(i=0;i<sizeof(rc->search[0]);++i){
        if(!domain[i])break;
        if(ulibc_isspace((unsigned char)domain[i]))break;
    }
    if(i==0||rc->search_count>=C5AI_RESV_SEARCH_MAX)return;
    c5ai_copy_host_norm(domain,rc->search[rc->search_count],sizeof(rc->search[rc->search_count]),true);
    if(rc->search[rc->search_count][0])++rc->search_count;
}
static void c5ai_load_resolv_conf(c5ai_resolv_conf_t *rc){
    const uint8_t *data=0;
    uint32_t sz=0;
    size_t off=0;
    char line[256];
    c5ai_resolv_defaults(rc);
    if(!kvfs_read("/etc/resolv.conf",&data,&sz)){
        if(!kvfs_read("/run/systemd/resolve/resolv.conf",&data,&sz))return;
    }
    while(c5ai_read_line(data,(size_t)sz,&off,line,sizeof(line))){
        const char *p;
        char tok[96];
        c5ai_strip_comment(line);
        p=c5ai_skip_ws(line);
        if(!*p)continue;
        if(c5ai_starts_with_ci(p,"nameserver")){
            uint8_t ip4[4];
            uint32_t ip;
            p+=10;
            if(!c5ai_token_next(&p,tok,sizeof(tok)))continue;
            if(!c5_parse_ipv4(tok,ip4))continue;
            ip=((uint32_t)ip4[0]<<24)|((uint32_t)ip4[1]<<16)|((uint32_t)ip4[2]<<8)|ip4[3];
            if(rc->nameserver_count==1&&rc->nameservers[0]==(((uint32_t)10u<<24)|((uint32_t)0u<<16)|((uint32_t)2u<<8)|3u)){
                rc->nameserver_count=0;
            }
            if(rc->nameserver_count<C5AI_RESV_NS_MAX){
                rc->nameservers[rc->nameserver_count++]=ip;
            }
            continue;
        }
        if(c5ai_starts_with_ci(p,"search")){
            p+=6;
            while(c5ai_token_next(&p,tok,sizeof(tok))){
                c5ai_resolv_add_search(rc,tok);
            }
            continue;
        }
        if(c5ai_starts_with_ci(p,"domain")){
            p+=6;
            if(c5ai_token_next(&p,tok,sizeof(tok))){
                rc->search_count=0;
                c5ai_resolv_add_search(rc,tok);
            }
            continue;
        }
        if(c5ai_starts_with_ci(p,"options")){
            p+=7;
            while(c5ai_token_next(&p,tok,sizeof(tok))){
                if(c5ai_starts_with_ci(tok,"ndots:")){
                    const char *v=tok+6;
                    int nd=0;
                    while(*v&&ulibc_isdigit((unsigned char)*v)){
                        nd=nd*10+(*v-'0');
                        if(nd>15)break;
                        ++v;
                    }
                    if(nd>0&&nd<16)rc->ndots=nd;
                }else if(c5ai_starts_with_ci(tok,"rotate")){
                    rc->rotate=true;
                }
            }
        }
    }
}
static void c5ai_nss_hosts_defaults(c5ai_nss_hosts_t *pol){
    if(!pol)return;
    pol->count=2;
    pol->order[0]=C5AI_NSS_FILES;
    pol->order[1]=C5AI_NSS_DNS;
}
static void c5ai_nss_hosts_add(c5ai_nss_hosts_t *pol,c5ai_nss_src_t src){
    int i;
    if(!pol||pol->count>=4)return;
    for(i=0;i<pol->count;++i)if(pol->order[i]==src)return;
    pol->order[pol->count++]=src;
}
static void c5ai_load_nss_hosts(c5ai_nss_hosts_t *pol){
    const uint8_t *data=0;
    uint32_t sz=0;
    size_t off=0;
    char line[256];
    c5ai_nss_hosts_defaults(pol);
    if(!kvfs_read("/etc/nsswitch.conf",&data,&sz))return;
    while(c5ai_read_line(data,(size_t)sz,&off,line,sizeof(line))){
        const char *p;
        char tok[64];
        c5ai_strip_comment(line);
        p=c5ai_skip_ws(line);
        if(!*p)continue;
        if(!c5ai_starts_with_ci(p,"hosts"))continue;
        p+=5;
        while(*p&&(*p==':'||ulibc_isspace((unsigned char)*p)))++p;
        pol->count=0;
        while(c5ai_token_next(&p,tok,sizeof(tok))){
            if(tok[0]=='[')continue;
            if(c5ai_host_equal(tok,"files")||c5ai_host_equal(tok,"myhostname")){
                c5ai_nss_hosts_add(pol,C5AI_NSS_FILES);
            }else if(c5ai_host_equal(tok,"dns")||c5ai_host_equal(tok,"resolve")){
                c5ai_nss_hosts_add(pol,C5AI_NSS_DNS);
            }
        }
        if(pol->count==0)c5ai_nss_hosts_defaults(pol);
        return;
    }
}
static bool c5ai_hosts_lookup_builtin(const char *node,uint32_t *addr){
    if(!node||!node[0]||!addr)return false;
    if(c5ai_host_equal(node,"localhost")||c5ai_host_equal(node,"localhost.localdomain")){
        *addr=((uint32_t)127u<<24)|1u;
        return true;
    }
    if(c5ai_host_equal(node,"ridux")||c5ai_host_equal(node,"ridux.local")){
        *addr=((uint32_t)10u<<24)|((uint32_t)0u<<16)|((uint32_t)2u<<8)|15u;
        return true;
    }
    return false;
}
static bool c5ai_hosts_lookup_file(const char *node,uint32_t *addr){
    const uint8_t *data=0;
    uint32_t sz=0;
    size_t off=0;
    char line[256];
    if(c5ai_hosts_lookup_builtin(node,addr))return true;
    if(!kvfs_read("/etc/hosts",&data,&sz))return false;
    while(c5ai_read_line(data,(size_t)sz,&off,line,sizeof(line))){
        const char *p;
        char iptok[64];
        uint8_t ip4[4];
        uint32_t ip;
        c5ai_strip_comment(line);
        p=c5ai_skip_ws(line);
        if(!*p)continue;
        if(!c5ai_token_next(&p,iptok,sizeof(iptok)))continue;
        if(!c5_parse_ipv4(iptok,ip4))continue;
        ip=((uint32_t)ip4[0]<<24)|((uint32_t)ip4[1]<<16)|((uint32_t)ip4[2]<<8)|ip4[3];
        while(c5ai_token_next(&p,iptok,sizeof(iptok))){
            if(c5ai_host_equal(node,iptok)){*addr=ip;return true;}
        }
    }
    return false;
}
static bool c5ai_dns_try_name(const char *name,uint32_t *addr){
    char h[128];
    if(!name||!addr)return false;
    c5ai_copy_host_norm(name,h,sizeof(h),true);
    if(!h[0])return false;
    return dns_resolve(h,addr);
}
static bool c5ai_dns_lookup_with_search(const char *node,const c5ai_resolv_conf_t *rc,uint32_t *addr){
    char base[128];
    bool absolute=false,tried_exact=false;
    int dots;
    int i;
    if(!node||!node[0]||!rc||!addr)return false;
    c5ai_copy_host_norm(node,base,sizeof(base),true);
    if(!base[0])return false;
    if(node[ulibc_strlen(node)-1]=='.')absolute=true;
    if(absolute)return c5ai_dns_try_name(base,addr);
    dots=c5ai_count_dots(base);
    if(dots>=rc->ndots){
        if(c5ai_dns_try_name(base,addr))return true;
        tried_exact=true;
    }
    for(i=0;i<rc->search_count;++i){
        char fqdn[192];
        size_t bl=ulibc_strlen(base),sl=ulibc_strlen(rc->search[i]);
        if(bl+1+sl+1>sizeof(fqdn))continue;
        ulibc_memcpy(fqdn,base,bl);
        fqdn[bl]='.';
        ulibc_memcpy(fqdn+bl+1,rc->search[i],sl);
        fqdn[bl+1+sl]=0;
        if(c5ai_dns_try_name(fqdn,addr))return true;
    }
    if(!tried_exact)return c5ai_dns_try_name(base,addr);
    return false;
}
static bool c5ai_resolve_host(const char *node,uint32_t *addr){
    c5ai_nss_hosts_t pol;
    c5ai_resolv_conf_t rc;
    int i;
    if(!node||!node[0]||!addr)return false;
    c5ai_load_nss_hosts(&pol);
    c5ai_load_resolv_conf(&rc);
    for(i=0;i<pol.count;++i){
        if(pol.order[i]==C5AI_NSS_FILES){
            if(c5ai_hosts_lookup_file(node,addr))return true;
        }else if(pol.order[i]==C5AI_NSS_DNS){
            if(c5ai_dns_lookup_with_search(node,&rc,addr))return true;
        }
    }
    return c5ai_dns_try_name(node,addr);
}
uint32_t ulibc_inet_addr(const char*cp){
    uint8_t p[4];
    if(!c5_parse_ipv4(cp,p))return 0;
    return ((uint32_t)p[0]<<24)|((uint32_t)p[1]<<16)|((uint32_t)p[2]<<8)|p[3];
}
int ulibc_inet_pton(int af,const char*src,void*dst){if(af==2){uint32_t a=ulibc_inet_addr(src);if(a==0&&ulibc_strcmp(src,"0.0.0.0")!=0)return 0;*(uint32_t*)dst=a;return 1;}return-1;}
const char*ulibc_inet_ntop(int af,const void*src,char*dst,size_t sz){
    if(af==2){
        uint32_t a=*(const uint32_t*)src;
        ulibc_snprintf(dst,sz,"%u.%u.%u.%u",(unsigned)((a>>24)&0xFF),(unsigned)((a>>16)&0xFF),(unsigned)((a>>8)&0xFF),(unsigned)(a&0xFF));
        return dst;
    }
    return 0;
}
int ulibc_getaddrinfo(const char*node,const char*svc,const ulibc_addrinfo_t*hints,ulibc_addrinfo_t**res){
    int family=0;
    int socktype=1;
    int protocol=6;
    uint16_t port=0;
    uint32_t addr=0;
    if(!res)return EAI_SYSTEM;
    *res=0;
    if(hints&&hints->ai_flags&(uint32_t)~(AI_PASSIVE|AI_CANONNAME|AI_NUMERICHOST|AI_ADDRCONFIG|AI_V4MAPPED))return EAI_BADFLAGS;
    if(hints)family=hints->ai_family;
    if(family!=0&&family!=2)return EAI_FAMILY;
    if(hints&&hints->ai_socktype)socktype=hints->ai_socktype;
    if(socktype!=0&&socktype!=1&&socktype!=2)return EAI_SOCKTYPE;
    if(socktype==0)socktype=1;
    if(socktype==2)protocol=17;
    if(hints&&hints->ai_protocol)protocol=hints->ai_protocol;
    if(svc){
        port=c5_service_to_port(svc);
        if(!port)return EAI_SERVICE;
    }
    if(!node||!*node){
        addr=(hints&&(hints->ai_flags&AI_PASSIVE))?0:0x7F000001u;
    }else if(hints&&(hints->ai_flags&AI_NUMERICHOST)){
        addr=ulibc_inet_addr(node);
        if(addr==0&&ulibc_strcmp(node,"0.0.0.0")!=0)return EAI_NONAME;
    }else{
        if(!c5ai_resolve_host(node,&addr))return EAI_NONAME;
    }
    ulibc_addrinfo_t*ai=(ulibc_addrinfo_t*)ulibc_malloc(sizeof(ulibc_addrinfo_t));
    ulibc_sockaddr_in_t*sa=(ulibc_sockaddr_in_t*)ulibc_malloc(sizeof(ulibc_sockaddr_in_t));
    if(!ai||!sa){ulibc_free(ai);ulibc_free(sa);return EAI_MEMORY;}
    ulibc_memset(ai,0,sizeof(*ai));ulibc_memset(sa,0,sizeof(*sa));sa->sin_family=2;
    sa->sin_addr=addr;
    sa->sin_port=ulibc_htons(port);
    ai->ai_flags=hints?hints->ai_flags:0;
    ai->ai_family=2;ai->ai_socktype=socktype;ai->ai_protocol=protocol;
    ai->ai_addrlen=sizeof(*sa);ai->ai_addr=(ulibc_sockaddr_t*)sa;
    if(hints&&hints->ai_flags&AI_CANONNAME&&node&&*node){
        char canon[128];
        c5ai_copy_host_norm(node,canon,sizeof(canon),true);
        ai->ai_canonname=(char*)ulibc_malloc(ulibc_strlen(canon)+1);
        if(ai->ai_canonname)ulibc_strcpy(ai->ai_canonname,canon);
    }
    *res=ai;
    return 0;
}
void ulibc_freeaddrinfo(ulibc_addrinfo_t*r){
    while(r){
        ulibc_addrinfo_t *n=r->ai_next;
        if(r->ai_addr)ulibc_free(r->ai_addr);
        if(r->ai_canonname)ulibc_free(r->ai_canonname);
        ulibc_free(r);
        r=n;
    }
}
int ulibc_getnameinfo(const ulibc_sockaddr_t*sa,size_t sl,char*h,size_t hl,char*s,size_t sl2,int f){
    const ulibc_sockaddr_in_t *sin;
    (void)f;
    if(!sa||sl<sizeof(ulibc_sockaddr_in_t)||sa->sa_family!=2)return EAI_FAMILY;
    sin=(const ulibc_sockaddr_in_t*)sa;
    if(h&&hl>0){
        if(!ulibc_inet_ntop(2,&sin->sin_addr,h,hl))return EAI_FAIL;
    }
    if(s&&sl2>0){
        uint16_t p=ulibc_ntohs(sin->sin_port);
        ulibc_snprintf(s,sl2,"%u",(unsigned)p);
    }
    return 0;
}
const char*ulibc_gai_strerror(int e){
    switch(e){
    case 0: return "Success";
    case EAI_BADFLAGS: return "Invalid value for ai_flags";
    case EAI_NONAME: return "Name or service not known";
    case EAI_AGAIN: return "Temporary failure in name resolution";
    case EAI_FAIL: return "Non-recoverable failure in name resolution";
    case EAI_FAMILY: return "Address family not supported";
    case EAI_SOCKTYPE: return "Socket type not supported";
    case EAI_SERVICE: return "Service not supported for socket type";
    case EAI_MEMORY: return "Memory allocation failure";
    case EAI_SYSTEM: return "System error";
    default: return "getaddrinfo error";
    }
}

/* X11 / Wayland stubs */
typedef struct {
    uint16_t sun_family;
    char sun_path[108];
} c5_sockaddr_un_t;
static int c5_connect_unix_stream(const char *path){
    c5_sockaddr_un_t sa;
    int fd=(int)real_sys_socket(1,1,0);
    if(fd<0)return fd;
    ulibc_memset(&sa,0,sizeof(sa));
    sa.sun_family=1;
    ulibc_strncpy(sa.sun_path,path,sizeof(sa.sun_path)-1);
    if(real_sys_connect(fd,&sa,(uint32_t)(sizeof(sa.sun_family)+ulibc_strlen(sa.sun_path)+1))<0){
        real_sys_close(fd);
        return -1;
    }
    return fd;
}
static int c5_connect_unix_paths(const char *const *paths,size_t n){
    size_t i;
    for(i=0;i<n;++i){
        int fd;
        if(!paths[i]||!paths[i][0])continue;
        fd=c5_connect_unix_stream(paths[i]);
        if(fd>=0)return fd;
    }
    return -1;
}
int ulibc_x11_connect(int d){
    char path0[128],path1[128];
    const char *const cand[]={path0,path1};
    int display=d;
    if(display<0){
        const char *disp=ulibc_getenv("DISPLAY");
        if(disp&&disp[0]==':'&&ulibc_isdigit((unsigned char)disp[1]))display=(int)(disp[1]-'0');
        else display=0;
    }
    if(display<0)display=0;
    if(display>63)display=63;
    ulibc_snprintf(path0,sizeof(path0),"/tmp/.X11-unix/X%d",display);
    ulibc_snprintf(path1,sizeof(path1),"/tmp/.X11-unix/X0");
    return c5_connect_unix_paths(cand,2);
}
int ulibc_x11_create_window(int f,int x,int y,int w,int h,uint32_t bg){(void)f;(void)x;(void)y;(void)w;(void)h;(void)bg;return 1;}
int ulibc_x11_map_window(int f,uint32_t wid){(void)f;(void)wid;return 0;}
int ulibc_wayland_connect(void){
    const char *rt=ulibc_getenv("XDG_RUNTIME_DIR");
    char path_env[128],path_u1000[128],path_u0[128];
    const char *const cand[]={path_env,path_u1000,path_u0,"/run/wayland/wayland-0"};
    if(rt&&*rt)ulibc_snprintf(path_env,sizeof(path_env),"%s/wayland-0",rt);
    else path_env[0]=0;
    ulibc_snprintf(path_u1000,sizeof(path_u1000),"/run/user/1000/wayland-0");
    ulibc_snprintf(path_u0,sizeof(path_u0),"/run/user/0/wayland-0");
    return c5_connect_unix_paths(cand,4);
}

/* Browser-specific syscall stubs */
int ulibc_seccomp(unsigned int op,unsigned int fl,void*a){(void)op;(void)fl;(void)a;return 0;}
int ulibc_prctl_set_vma(uint64_t a,uint64_t s,const char*n){(void)a;(void)s;(void)n;return 0;}
int ulibc_userfaultfd(int fl){return (int)real_sys_userfaultfd(fl);}
int64_t ulibc_process_vm_readv(int pid,const void*lv,unsigned long lc,const void*rv,unsigned long rc,unsigned long fl){
    return real_sys_process_vm_readv(pid,(const iovec_t*)lv,lc,(const iovec_t*)rv,rc,fl);
}
int64_t ulibc_process_vm_writev(int pid,const void*lv,unsigned long lc,const void*rv,unsigned long rc,unsigned long fl){
    return real_sys_process_vm_writev(pid,(const iovec_t*)lv,lc,(const iovec_t*)rv,rc,fl);
}

/* Shell commands + init */
static void cmd5_bsd(const char*a,char*o,int mx){
    (void)a;
    ulibc_snprintf(o,(size_t)mx,
        "=== RiduxOS compat5 (portable libc extensions) ===\n"
        "fnmatch:  ? * [range] FNM_PATHNAME/NOESCAPE/PERIOD/CASEFOLD\n"
        "base64:   b64_ntop/b64_pton (encode/decode)\n"
        "arc4random: ChaCha20 CSPRNG\n"
        "regex:    regcomp/regexec/regfree (NFA engine)\n"
        "glob:     glob/globfree (simplified)\n"
        "DNS:      getaddrinfo/inet_pton/inet_ntop/inet_addr/htons/htonl\n"
        "X11:      x11_connect/create_window/map_window\n"
        "Wayland:  wayland_connect\n"
        "Browser:  seccomp/prctl_set_vma/userfaultfd/process_vm_readv\n");
}
static void cmd5_b64(const char*a,char*o,int mx){
    (void)a;const char*demo="Hello RiduxOS!";char enc[128];char dec[128];
    int el=ulibc_b64_ntop((const uint8_t*)demo,ulibc_strlen(demo),enc,sizeof(enc));
    int dl=ulibc_b64_pton(enc,(uint8_t*)dec,sizeof(dec));
    if(dl>0)dec[dl]=0;
    ulibc_snprintf(o,(size_t)mx,"base64 demo: '%s' -> '%s' (enc=%d,dec=%d)\n",demo,el>0?enc:"ERR",el,dl);
}
static void cmd5_rng(const char*a,char*o,int mx){
    (void)a;
    uint32_t r1=ulibc_arc4random(),r2=ulibc_arc4random(),r3=ulibc_arc4random_uniform(100);
    ulibc_snprintf(o,(size_t)mx,"arc4random: 0x%08X 0x%08X uniform(100)=%u\n",r1,r2,r3);
}
static const char *c5_skip_ws(const char *p){while(*p&&ulibc_isspace((unsigned char)*p))++p;return p;}
static bool c5_next_tok(const char **pp,char *tok,size_t cap){
    const char *p=c5_skip_ws(*pp);size_t n=0;
    if(!*p){tok[0]=0;*pp=p;return false;}
    while(*p&&!ulibc_isspace((unsigned char)*p)){if(n+1<cap)tok[n++]=*p;++p;}
    tok[n]=0;*pp=p;return true;
}
static bool c5_streq_ci(const char *a,const char *b){
    if(!a||!b)return false;
    while(*a&&*b){
        if(ulibc_tolower((unsigned char)*a)!=ulibc_tolower((unsigned char)*b))return false;
        ++a;++b;
    }
    return *a==0&&*b==0;
}
static bool c5_has_token_ci(const char *s,const char *tok){
    size_t i,j;
    if(!s||!tok||!*tok)return false;
    for(i=0;s[i];++i){
        for(j=0;tok[j];++j){
            unsigned char a=(unsigned char)s[i+j];
            unsigned char b=(unsigned char)tok[j];
            if(!a)break;
            if(ulibc_tolower(a)!=ulibc_tolower(b))break;
        }
        if(!tok[j])return true;
    }
    return false;
}
static bool c5_browser_path_alias(const char *path){
    if(!path||!*path)return false;
    if(c5_streq_ci(path,"chrome")||
       c5_streq_ci(path,"chromium")||
       c5_streq_ci(path,"firefox")||
       c5_streq_ci(path,"google-chrome"))return true;
    if(c5_streq_ci(path,"/bin/chrome.elf")||
       c5_streq_ci(path,"/bin/firefox.elf"))return true;
    if(c5_has_token_ci(path,".elf")&&(c5_has_token_ci(path,"chrome")||c5_has_token_ci(path,"firefox")))return true;
    return false;
}
static bool c5_alias_looks_firefox(const char *path){
    if(!path||!*path)return false;
    return c5_has_token_ci(path,"firefox");
}
static bool c5_path_is_elf64(const char *path){
    const uint8_t *data=0;
    uint32_t sz=0;
    if(!path||!*path)return false;
    if(!kvfs_read(path,&data,&sz))return false;
    return elf64_validate(data,sz);
}
static bool c5_resolve_browser_alias_binary(const char *hint,char *resolved,size_t cap){
    static const char *const firefox_cand[]={
        "/opt/firefox/firefox-bin",
        "/opt/firefox/firefox",
        "/bin/firefox.elf",
        "/usr/lib/firefox/firefox",
        "/usr/lib64/firefox/firefox",
        "/usr/bin/firefox",
        "/bin/firefox",
        0
    };
    static const char *const chrome_cand[]={
        "/bin/chrome.elf",
        "/opt/chromium/chrome",
        "/opt/google/chrome/chrome",
        "/usr/bin/chromium",
        "/usr/bin/google-chrome",
        "/bin/chromium",
        "/bin/chrome",
        0
    };
    const char *const *cand=c5_alias_looks_firefox(hint)?firefox_cand:chrome_cand;
    size_t i;
    if(!resolved||cap==0)return false;
    resolved[0]=0;
    if(hint&&*hint&&c5_path_is_elf64(hint)){
        ulibc_snprintf(resolved,cap,"%s",hint);
        return true;
    }
    for(i=0;cand[i];++i){
        if(c5_path_is_elf64(cand[i])){
            ulibc_snprintf(resolved,cap,"%s",cand[i]);
            return true;
        }
    }
    return false;
}
static const char *c5_default_browser_target(bool force_real){
    if(force_real){
        if(c5_path_is_elf64("/opt/firefox/firefox-bin"))return "/opt/firefox/firefox-bin";
        if(c5_path_is_elf64("/opt/firefox/firefox"))return "/opt/firefox/firefox";
        if(c5_path_is_elf64("/opt/chromium/chrome"))return "/opt/chromium/chrome";
        if(c5_path_is_elf64("/usr/bin/firefox"))return "/usr/bin/firefox";
        return "/opt/chromium/chrome";
    }
    if(c5_path_is_elf64("/opt/firefox/firefox-bin"))return "/opt/firefox/firefox-bin";
    if(c5_path_is_elf64("/opt/firefox/firefox"))return "/opt/firefox/firefox";
    if(c5_path_is_elf64("/bin/firefox.elf"))return "/bin/firefox.elf";
    if(c5_path_is_elf64("/bin/chrome.elf"))return "/bin/chrome.elf";
    return "/bin/chrome.elf";
}
static const char *c5_basename(const char *path){const char *b=path;while(*path){if(*path=='/')b=path+1;++path;}return b;}
static void c5_dirname_copy(const char *path,char *out,size_t cap){
    const char *last=0,*p=path;
    size_t n;
    if(!out||!cap)return;
    out[0]=0;
    if(!path||!*path){ulibc_snprintf(out,cap,"/");return;}
    while(*p){if(*p=='/')last=p;++p;}
    if(!last){ulibc_snprintf(out,cap,".");return;}
    if(last==path){ulibc_snprintf(out,cap,"/");return;}
    n=(size_t)(last-path);
    if(n>=cap)n=cap-1;
    ulibc_memcpy(out,path,n);
    out[n]=0;
}
static void c5_extract_interp(const uint8_t *data,uint32_t sz,bool *is_dyn,char *interp,size_t cap){
    const elf64_ehdr_t *eh=(const elf64_ehdr_t*)data;
    uint16_t i;
    /* A binary needs the dynamic loader whenever PT_INTERP is present,
     * regardless of e_type. Classic glibc binaries (including Firefox)
     * are ET_EXEC with a PT_INTERP pointing at /lib64/ld-linux-x86-64.so.2.
     * Previously we only treated ET_DYN as dynamic, so ET_EXEC binaries
     * were launched without the loader -> instant crash. */
    *is_dyn=false;
    interp[0]=0;
    for(i=0;i<eh->e_phnum;++i){
        uint64_t off=eh->e_phoff+(uint64_t)i*eh->e_phentsize;
        if(off+sizeof(elf64_phdr_t)>sz)break;
        {
            const elf64_phdr_t *ph=(const elf64_phdr_t*)(data+off);
            if(ph->p_type==PT_INTERP&&ph->p_offset+ph->p_filesz<=sz&&ph->p_filesz>1){
                size_t n=(size_t)ph->p_filesz-1;
                if(n>=cap)n=cap-1;
                ulibc_memcpy(interp,data+ph->p_offset,n);
                interp[n]=0;
                *is_dyn=true;   /* PT_INTERP present -> needs loader */
                break;
            }
        }
    }
    /* ET_DYN implies PIE -> also dynamic even if PT_INTERP was missing
     * (rare, but keep the historical behaviour as a safety net). */
    if(eh->e_type==ET_DYN) *is_dyn=true;
}
static int c5_collect_elf_info(const uint8_t *data,uint32_t sz,uint64_t mmap_seed,
                               uint64_t *load_bias,uint64_t *entry,uint64_t *phdr,
                               uint64_t *phent,uint64_t *phnum){
    const elf64_ehdr_t *eh;
    uint64_t min_vaddr=~0ULL;
    uint64_t lb=0;
    uint64_t phdr_va=0;
    uint16_t i;
    if(!data||!sz||!load_bias||!entry||!phdr||!phent||!phnum)return -EINVAL;
    if(!elf64_validate(data,sz))return -ENOEXEC;
    eh=(const elf64_ehdr_t*)data;
    if(eh->e_type==ET_DYN){
        for(i=0;i<eh->e_phnum;++i){
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
    for(i=0;i<eh->e_phnum;++i){
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
static bool c5_find_loader(const char *interp_hint,const uint8_t **data,uint32_t *sz,char *resolved,size_t cap){
    static const char *cand[]={
        "/lib64/ld-linux-x86-64.so.2",
        "/lib/ld-linux-x86-64.so.2",
        "/libexec/ld-elf.so.1"
    };
    size_t i;
    if(interp_hint&&*interp_hint&&kvfs_read(interp_hint,data,sz)){
        ulibc_strcpy(resolved,interp_hint);
        return true;
    }
    for(i=0;i<sizeof(cand)/sizeof(cand[0]);++i){
        if(interp_hint&&*interp_hint&&ulibc_strcmp(interp_hint,cand[i])==0)continue;
        if(kvfs_read(cand[i],data,sz)){
            size_t n=ulibc_strlen(cand[i]);
            if(n>=cap)n=cap-1;
            ulibc_memcpy(resolved,cand[i],n);
            resolved[n]=0;
            return true;
        }
    }
    return false;
}
static task_t *c5_find_task(int pid){
    int i;
    for(i=0;i<TASK_MAX;++i)if(g_tasks[i].used&&g_tasks[i].pid==pid)return &g_tasks[i];
    return 0;
}
static void c5_abort_task(int pid){
    task_t *t=c5_find_task(pid);
    if(t)t->used=false;
}
static bool c5_probe_http_virtual(void){
    int fd;
    ulibc_sockaddr_in_t sa;
    const char req[]="GET /healthz HTTP/1.1\r\nHost: probe.ridux\r\nConnection: close\r\n\r\n";
    char rx[96];
    int64_t wr=0,rd;
    fd=(int)real_sys_socket(2,1,0);
    if(fd<0)return false;
    ulibc_memset(&sa,0,sizeof(sa));
    sa.sin_family=2;
    sa.sin_port=ulibc_htons(80);
    sa.sin_addr=((uint32_t)203u<<24)|((uint32_t)0u<<16)|((uint32_t)113u<<8)|1u;
    if(real_sys_connect(fd,&sa,(uint32_t)sizeof(sa))<0){real_sys_close(fd);return false;}
    wr=real_sys_sendto(fd,req,sizeof(req)-1,0,0,0);
    rd=real_sys_recvfrom(fd,rx,sizeof(rx),0,0,0);
    real_sys_close(fd);
    if(wr<=0||rd<8)return false;
    return rx[0]=='H'&&rx[1]=='T'&&rx[2]=='T'&&rx[3]=='P'&&rx[4]=='/'&&rx[5]=='1'&&rx[6]=='.';
}
static bool c5_probe_wayland_ipc(void){
    int fd=ulibc_wayland_connect();
    if(fd<0)return false;
    real_sys_close(fd);
    return true;
}
static bool c5_probe_x11_ipc(void){
    int fd=ulibc_x11_connect(-1);
    if(fd<0)return false;
    real_sys_close(fd);
    return true;
}
static bool c5_probe_dbus_ipc(void){
    const char *rt=ulibc_getenv("XDG_RUNTIME_DIR");
    const char *probe="AUTH EXTERNAL\r\nNEGOTIATE_UNIX_FD\r\nBEGIN\r\n";
    size_t probe_len=ulibc_strlen(probe);
    size_t sent=0;
    char path_env[128],path_u1000[128],path_u0[128];
    const char *const cand[]={path_env,path_u1000,path_u0,"/run/dbus/system_bus_socket","/tmp/dbus-system_bus_socket"};
    char rx[96];
    size_t n=0;
    int64_t wr=0,rd;
    int fd;
    if(rt&&*rt)ulibc_snprintf(path_env,sizeof(path_env),"%s/bus",rt);
    else path_env[0]=0;
    ulibc_snprintf(path_u1000,sizeof(path_u1000),"/run/user/1000/bus");
    ulibc_snprintf(path_u0,sizeof(path_u0),"/run/user/0/bus");
    fd=c5_connect_unix_paths(cand,5);
    if(fd<0)return false;
    __boot_serial_puts("[probe-dbus] send fd=");
    __boot_serial_putu32((uint32_t)fd);
    __boot_serial_puts("\n");
    while(sent<probe_len){
        wr=real_sys_sendto(fd,probe+sent,probe_len-sent,0,0,0);
        if(wr<=0)break;
        sent+=(size_t)wr;
    }
    __boot_serial_puts("[probe-dbus] send rc=");
    if(sent<probe_len&&wr<0){__boot_serial_puts("-");__boot_serial_putu32((uint32_t)(-wr));}
    else __boot_serial_putu32((uint32_t)sent);
    __boot_serial_puts("\n");
    rd=real_sys_recvfrom(fd,rx,sizeof(rx),0,0,0);
    __boot_serial_puts("[probe-dbus] recv rc=");
    if(rd<0){__boot_serial_puts("-");__boot_serial_putu32((uint32_t)(-rd));}
    else __boot_serial_putu32((uint32_t)rd);
    __boot_serial_puts("\n");
    real_sys_close(fd);
    if(wr<=0||rd<=0)return false;
    n=(size_t)rd;
    if(n>=sizeof(rx))n=sizeof(rx)-1;
    rx[n]=0;
    return c5_has_token_ci(rx,"OK")||c5_has_token_ci(rx,"AGREE")||c5_has_token_ci(rx,"REJECTED");
}

static void c5_spawn_force_stage(const char *stage){
    __boot_serial_force_puts("[spawn!] ");
    __boot_serial_force_puts(stage);
    __boot_serial_force_puts("\n");
}

static void c5_spawn_force_path(const char *stage,const char *path){
    __boot_serial_force_puts("[spawn!] ");
    __boot_serial_force_puts(stage);
    __boot_serial_force_puts(" ");
    __boot_serial_force_puts(path?path:"(null)");
    __boot_serial_force_puts("\n");
}

static void c5_spawn_force_rc(const char *stage,int rc){
    __boot_serial_force_puts("[spawn!] ");
    __boot_serial_force_puts(stage);
    __boot_serial_force_puts(" rc=");
    if(rc<0){
        __boot_serial_force_puts("-");
        __boot_serial_force_putu32((uint32_t)(-rc));
    }else{
        __boot_serial_force_putu32((uint32_t)rc);
    }
    __boot_serial_force_puts("\n");
}

static int c5_spawn_elf_task(const char *path,bool launch_now,char *detail,size_t cap){
    const uint8_t *data=0,*interp_data=0;
    uint32_t sz=0,interp_sz=0;
    const elf64_ehdr_t *eh=0;
    bool is_dyn=false;
    char interp[VFS_PATH_MAX],resolved_interp[VFS_PATH_MAX];
    char dyn_diag[512];
    int map_rc;
    int stack_rc;
    int pid;
    task_t *t,*cur;
    address_space_t *old_as;
    uint64_t main_lb=0,main_entry=0,main_phdr=0,main_phent=0,main_phnum=0;
    uint64_t interp_lb=0,interp_entry=0,interp_phdr=0,interp_phent=0,interp_phnum=0;
    auxv_t auxv[24];
    char *argv_exec[80];
    char *env_exec[128];
    int argc_exec=0;
    int envc=0;
    bool is_chrome=false;
    bool is_firefox=false;
    c5_spawn_force_path("entry",path);
    __boot_serial_puts("[spawn] entry path="); __boot_serial_puts(path?path:"(null)"); __boot_serial_puts("\n");
    if(!path||!*path){ulibc_snprintf(detail,cap,"browser run: falta ruta ELF64.\n");return -EINVAL;}
    if(!kvfs_read(path,&data,&sz)){ulibc_snprintf(detail,cap,"browser run: '%s' no existe en RiduxFS.\n",path);return -ENOENT;}
    c5_spawn_force_rc("vfs_read",(int)sz);
    __boot_serial_puts("[spawn] vfs_read ok sz="); __boot_serial_putu32(sz); __boot_serial_puts("\n");
    if(!elf64_validate(data,sz)){ulibc_snprintf(detail,cap,"browser run: '%s' no es ELF64 valido.\n",path);return -ENOEXEC;}
    c5_spawn_force_stage("elf_validate ok");
    __boot_serial_puts("[spawn] elf_validate ok\n");
    eh=(const elf64_ehdr_t*)data;
    c5_extract_interp(data,sz,&is_dyn,interp,sizeof(interp));
    __boot_serial_puts("[spawn] extract_interp is_dyn="); __boot_serial_puts(is_dyn?"true":"false"); __boot_serial_puts(" interp="); __boot_serial_puts(interp[0]?interp:"(none)"); __boot_serial_puts("\n");
    resolved_interp[0]=0;
    if(is_dyn){
        __boot_serial_puts("[spawn] calling c5_find_loader...\n");
        if(!c5_find_loader(interp,&interp_data,&interp_sz,resolved_interp,sizeof(resolved_interp))){
            c5_spawn_force_stage("find_loader fail");
            __boot_serial_puts("[spawn] find_loader FAIL\n");
            ulibc_snprintf(detail,cap,
                "browser run: ELF dinamico sin loader disponible.\n"
                "interp=%s\n"
                "agrega el loader a initrd (ej: /lib64/ld-linux-x86-64.so.2).\n",
                interp[0]?interp:"(none)");
            return -ENOENT;
        }
        c5_spawn_force_path("find_loader ok",resolved_interp);
        __boot_serial_puts("[spawn] find_loader OK resolved="); __boot_serial_puts(resolved_interp); __boot_serial_puts(" sz="); __boot_serial_putu32(interp_sz); __boot_serial_puts("\n");
        if(!elf64_validate(interp_data,interp_sz)){
            __boot_serial_puts("[spawn] loader elf_validate FAIL\n");
            ulibc_snprintf(detail,cap,"browser run: loader '%s' invalido.\n",resolved_interp);
            return -ENOEXEC;
        }
        __boot_serial_puts("[spawn] loader validated\n");
    }
    __boot_serial_puts("[spawn] task_create...\n");
    pid=task_create(c5_basename(path),eh->e_entry,true);
    c5_spawn_force_rc("task_create",pid);
    __boot_serial_puts("[spawn] task_create returned pid="); __boot_serial_putu32((uint32_t)pid); __boot_serial_puts("\n");
    if(pid<0){ulibc_snprintf(detail,cap,"browser run: task_create fallo (%d).\n",pid);return pid;}
    __boot_serial_puts("[spawn] c5_find_task...\n");
    t=c5_find_task(pid);
    __boot_serial_puts("[spawn] c5_find_task returned t=");
    __boot_serial_puthex64((uint64_t)(uintptr_t)t);
    __boot_serial_puts("\n");
    if(!t){c5_abort_task(pid);ulibc_snprintf(detail,cap,"browser run: no se encontro task pid=%d.\n",pid);return -EAGAIN;}
    __boot_serial_puts("[spawn] t->addr_space=");
    __boot_serial_puthex64((uint64_t)(uintptr_t)t->addr_space);
    __boot_serial_puts("\n");
    if(!t->addr_space)t->addr_space=paging_create_address_space();
    if(!t->addr_space){c5_abort_task(pid);ulibc_snprintf(detail,cap,"browser run: no hay address space.\n");return -ENOMEM;}
    __boot_serial_puts("[spawn] task_current...\n");
    cur=task_current();
    __boot_serial_puts("[spawn] task_current returned cur=");
    __boot_serial_puthex64((uint64_t)(uintptr_t)cur);
    __boot_serial_puts("\n");
    old_as=(cur&&cur->addr_space)?cur->addr_space:paging_get_kernel_space();
    __boot_serial_puts("[spawn] old_as=");
    __boot_serial_puthex64((uint64_t)(uintptr_t)old_as);
    __boot_serial_puts(" new_cr3=");
    __boot_serial_puthex64(t->addr_space ? t->addr_space->cr3_phys : 0);
    __boot_serial_puts("\n");
    __boot_serial_puts("[spawn] paging_switch to task AS...\n");
    paging_switch(t->addr_space);
    __boot_serial_puts("[spawn] paging_switch OK\n");
    __boot_serial_puts("[spawn] c5_collect_elf_info(main)...\n");
    map_rc=c5_collect_elf_info(data,sz,t->mmap_base,&main_lb,&main_entry,&main_phdr,&main_phent,&main_phnum);
    __boot_serial_puts("[spawn] c5_collect_elf_info(main) rc="); __boot_serial_putu32((uint32_t)map_rc); __boot_serial_puts("\n");
    if(map_rc<0){
        paging_switch(old_as);
        c5_abort_task(pid);
        ulibc_snprintf(detail,cap,"browser run: metadata ELF invalida (rc=%d).\n",map_rc);
        return -ENOEXEC;
    }
    __boot_serial_puts("[spawn] compat3_set_next_image_name(main)...\n");
    compat3_set_next_image_name(path);
    __boot_serial_puts("[spawn] elf64_map_into_task(main)...\n");
    c5_spawn_force_stage("map main begin");
    map_rc=elf64_map_into_task(t,data,sz);
    c5_spawn_force_rc("map main",map_rc);
    __boot_serial_puts("[spawn] elf64_map_into_task(main) rc="); __boot_serial_putu32((uint32_t)map_rc); __boot_serial_puts("\n");
    if(map_rc<0){
        paging_switch(old_as);
        c5_abort_task(pid);
        dyn_diag[0]=0;
        compat3_dynlink_stats(dyn_diag,(int)sizeof(dyn_diag));
        if(map_rc==-ETIMEDOUT){
            ulibc_snprintf(detail,cap,"browser run: map ELF timeout (loader watchdog).\n%s",dyn_diag);
        }else{
            ulibc_snprintf(detail,cap,"browser run: map ELF fallo (rc=%d).\n%s",map_rc,dyn_diag);
        }
        return -ENOEXEC;
    }
    if(interp_data&&interp_sz){
        map_rc=c5_collect_elf_info(interp_data,interp_sz,t->mmap_base,&interp_lb,&interp_entry,&interp_phdr,&interp_phent,&interp_phnum);
        if(map_rc<0){
            paging_switch(old_as);
            c5_abort_task(pid);
            ulibc_snprintf(detail,cap,"browser run: metadata loader invalida (rc=%d).\n",map_rc);
            return -ENOEXEC;
        }
        compat3_set_next_image_name(resolved_interp);
        c5_spawn_force_stage("map loader begin");
        map_rc=elf64_map_into_task(t,interp_data,interp_sz);
        c5_spawn_force_rc("map loader",map_rc);
        if(map_rc<0){
            paging_switch(old_as);
            c5_abort_task(pid);
            dyn_diag[0]=0;
            compat3_dynlink_stats(dyn_diag,(int)sizeof(dyn_diag));
            if(map_rc==-ETIMEDOUT){
                ulibc_snprintf(detail,cap,"browser run: map loader timeout (loader watchdog).\n%s",dyn_diag);
            }else{
                ulibc_snprintf(detail,cap,"browser run: map loader fallo (rc=%d).\n%s",map_rc,dyn_diag);
            }
            return -ENOEXEC;
        }
        t->ctx.rip=interp_entry;
        t->entry_point=interp_entry;
    }else{
        t->ctx.rip=main_entry;
        t->entry_point=main_entry;
    }
    (void)interp_phdr;
    (void)interp_phent;
    (void)interp_phnum;

    ulibc_snprintf(t->exec_path,sizeof(t->exec_path),"%s",path);
    t->aux_at_phdr=main_phdr;
    t->aux_at_phent=main_phent;
    t->aux_at_phnum=main_phnum;
    t->aux_at_base=interp_data?interp_lb:0;
    t->aux_at_flags=0;
    t->aux_at_entry=main_entry;
    t->aux_at_uid=(uint64_t)t->uid;
    t->aux_at_euid=(uint64_t)t->euid;
    t->aux_at_gid=(uint64_t)t->gid;
    t->aux_at_egid=(uint64_t)t->egid;
    {
        int auxc=0;
        #define C5_AUX_PUSH(_t,_v) do{if(auxc<(int)(sizeof(auxv)/sizeof(auxv[0]))){auxv[auxc].a_type=(uint64_t)(_t);auxv[auxc].a_val=(uint64_t)(_v);++auxc;}}while(0)
        C5_AUX_PUSH(AT_PHDR,t->aux_at_phdr);
        C5_AUX_PUSH(AT_PHENT,t->aux_at_phent);
        C5_AUX_PUSH(AT_PHNUM,t->aux_at_phnum);
        C5_AUX_PUSH(AT_PAGESZ,PAGE_SIZE);
        C5_AUX_PUSH(AT_BASE,t->aux_at_base);
        C5_AUX_PUSH(AT_FLAGS,t->aux_at_flags);
        C5_AUX_PUSH(AT_ENTRY,t->aux_at_entry);
        C5_AUX_PUSH(AT_UID,t->aux_at_uid);
        C5_AUX_PUSH(AT_EUID,t->aux_at_euid);
        C5_AUX_PUSH(AT_GID,t->aux_at_gid);
        C5_AUX_PUSH(AT_EGID,t->aux_at_egid);
        C5_AUX_PUSH(AT_PLATFORM,(uint64_t)(uintptr_t)"x86_64");
        C5_AUX_PUSH(AT_HWCAP,0);
        C5_AUX_PUSH(AT_CLKTCK,100);
        C5_AUX_PUSH(AT_SECURE,0);
        C5_AUX_PUSH(AT_RANDOM,0);
        C5_AUX_PUSH(AT_HWCAP2,0);
        C5_AUX_PUSH(AT_EXECFN,(uint64_t)(uintptr_t)t->exec_path);
        C5_AUX_PUSH(AT_SYSINFO_EHDR,0);
        #undef C5_AUX_PUSH
        is_chrome=c5_has_token_ci(path,"chrome")||c5_has_token_ci(path,"chromium");
        is_firefox=c5_has_token_ci(path,"firefox");
        if(is_chrome||is_firefox){
            char app_dir[VFS_PATH_MAX];
            /*
             * Firefox still needs the conservative syscall-boundary scheduler
             * while its malloc/thread interaction is being tightened. Chromium
             * is left preemptible so the desktop, cursor and compositor keep
             * running during browser startup.
             */
            t->no_timer_preempt=is_firefox;
            c5_dirname_copy(path,app_dir,sizeof(app_dir));
            if(app_dir[0])ulibc_snprintf(t->cwd,sizeof(t->cwd),"%s",app_dir);
        }
        argv_exec[argc_exec++]=(char*)path;
        if(is_chrome){
            argv_exec[argc_exec++]="--no-sandbox";
            argv_exec[argc_exec++]="--user-data-dir=/tmp/chromium";
            argv_exec[argc_exec++]="--disk-cache-dir=/tmp/chromium-cache";
            argv_exec[argc_exec++]="--no-first-run";
            argv_exec[argc_exec++]="--no-default-browser-check";
            argv_exec[argc_exec++]="--test-type";
            argv_exec[argc_exec++]="--disable-session-crashed-bubble";
            argv_exec[argc_exec++]="--disable-infobars";
            argv_exec[argc_exec++]="--disable-component-update";
            argv_exec[argc_exec++]="--disable-default-apps";
            argv_exec[argc_exec++]="--disable-gpu";
            argv_exec[argc_exec++]="--disable-gpu-compositing";
            argv_exec[argc_exec++]="--use-gl=disabled";
            argv_exec[argc_exec++]="--disable-accelerated-2d-canvas";
            argv_exec[argc_exec++]="--disable-accelerated-video-decode";
            argv_exec[argc_exec++]="--disable-oop-rasterization";
            argv_exec[argc_exec++]="--disable-zero-copy";
            argv_exec[argc_exec++]="--run-all-compositor-stages-before-draw";
            argv_exec[argc_exec++]="--disable-dev-shm-usage";
            argv_exec[argc_exec++]="--no-zygote";
            argv_exec[argc_exec++]="--in-process-gpu";
            argv_exec[argc_exec++]="--winhttp-proxy-resolver";
            argv_exec[argc_exec++]="--renderer-process-limit=1";
            argv_exec[argc_exec++]="--disable-local-storage";
            argv_exec[argc_exec++]="--disable-site-isolation-trials";
            argv_exec[argc_exec++]="--no-proxy-server";
            argv_exec[argc_exec++]="--proxy-server=direct://";
            argv_exec[argc_exec++]="--proxy-bypass-list=*";
            argv_exec[argc_exec++]="--password-store=basic";
            argv_exec[argc_exec++]="--disable-metrics";
            argv_exec[argc_exec++]="--disable-metrics-reporting";
            argv_exec[argc_exec++]="--disable-background-tracing";
            argv_exec[argc_exec++]="--disable-chrome-tracing-computation";
            argv_exec[argc_exec++]="--no-slow-histograms";
            argv_exec[argc_exec++]="--disable-field-trial-config";
            argv_exec[argc_exec++]="--disable-perfetto-system-tracing";
            argv_exec[argc_exec++]="--disable-features=MojoUseEventFd,CalculateNativeWinOcclusion,CanvasOopRasterization,MediaRouter,DialMediaRouteProvider,OptimizationHints,AutofillServerCommunication,CertificateTransparencyComponentUpdater,FirstRunDesktopRevamp,FirstRunDesktopRefresh,SitePerProcess,IsolateOrigins,StrictOriginIsolation,ProcessPerSiteUpToMainFrameThreshold,EnablePerfettoSystemTracing,EnablePerfettoSystemBackgroundTracing,StorageServiceOutOfProcess,AudioServiceOutOfProcess";
            argv_exec[argc_exec++]="--disable-background-networking";
            argv_exec[argc_exec++]="--disable-extensions";
            argv_exec[argc_exec++]="--disable-sync";
            argv_exec[argc_exec++]="--disable-breakpad";
            argv_exec[argc_exec++]="--disable-crash-reporter";
            argv_exec[argc_exec++]="--disable-crashpad";
            argv_exec[argc_exec++]="--disable-crashpad-for-testing";
            argv_exec[argc_exec++]="--disable-hang-monitor";
            argv_exec[argc_exec++]="--noerrdialogs";
            argv_exec[argc_exec++]="--enable-features=UseOzonePlatform,NetworkServiceInProcess2";
            argv_exec[argc_exec++]="--ozone-platform=x11";
            argv_exec[argc_exec++]="file:///home/ridux-firefox-test.html";
        }else if(is_firefox){
            argv_exec[argc_exec++]="--no-remote";
            argv_exec[argc_exec++]="--new-instance";
            argv_exec[argc_exec++]="--profile";
            argv_exec[argc_exec++]="/tmp/firefox-profile";
            argv_exec[argc_exec++]="--new-window";
            argv_exec[argc_exec++]="http://example.com/";
        }
        argv_exec[argc_exec]=0;
        if(is_firefox){
            int ai;
            __boot_serial_puts("[spawn-firefox-argv] argc=");
            __boot_serial_putu32((uint32_t)argc_exec);
            __boot_serial_puts("\n");
            for(ai=0;ai<argc_exec;++ai){
                __boot_serial_puts("[spawn-firefox-argv]   ");
                __boot_serial_putu32((uint32_t)ai);
                __boot_serial_puts(": ");
                __boot_serial_puts(argv_exec[ai]?argv_exec[ai]:"(null)");
                __boot_serial_puts("\n");
            }
        }
        env_exec[envc++]="PATH=/usr/bin:/bin:/opt/chromium:/opt/firefox";
        env_exec[envc++]="HOME=/home";
        env_exec[envc++]="LANG=C";
        env_exec[envc++]="TERM=xterm-256color";
        env_exec[envc++]="DISPLAY=:0";
        env_exec[envc++]="XDG_RUNTIME_DIR=/run/user/0";
        env_exec[envc++]="WAYLAND_DISPLAY=wayland-0";
        if(is_chrome){
            env_exec[envc++]="XDG_SESSION_TYPE=x11";
            env_exec[envc++]="XDG_CURRENT_DESKTOP=Ridux";
            env_exec[envc++]="DESKTOP_SESSION=ridux";
            env_exec[envc++]="XDG_CONFIG_HOME=/tmp";
            env_exec[envc++]="XDG_CACHE_HOME=/tmp";
            env_exec[envc++]="LD_PRELOAD=/opt/ridux/stackchk_trace.so";
            env_exec[envc++]="DBUS_SESSION_BUS_ADDRESS=unix:path=/tmp/ridux-no-dbus";
            env_exec[envc++]="DBUS_SYSTEM_BUS_ADDRESS=unix:path=/tmp/ridux-no-system-dbus";
            env_exec[envc++]="GTK_USE_PORTAL=0";
            env_exec[envc++]="GSETTINGS_BACKEND=memory";
            env_exec[envc++]="GSETTINGS_SCHEMA_DIR=/usr/share/glib-2.0/schemas";
            env_exec[envc++]="GTK_MODULES=";
            env_exec[envc++]="GTK_A11Y=none";
            env_exec[envc++]="GDK_BACKEND=x11";
            env_exec[envc++]="GIO_USE_VFS=local";
            env_exec[envc++]="GIO_USE_VOLUME_MONITOR=unix";
            env_exec[envc++]="NO_AT_BRIDGE=1";
            env_exec[envc++]="AT_SPI_BUS_ADDRESS=unix:path=/tmp/ridux-no-at-spi";
        }else if(is_firefox){
            /* Firefox ya dibujaba, pero la interaccion quedaba floja por X11.
             * Lo empujo primero por Wayland y dejo X11 como red de seguridad. */
            env_exec[envc++]="XDG_SESSION_TYPE=wayland";
            env_exec[envc++]="XDG_CONFIG_HOME=/tmp";
            env_exec[envc++]="XDG_CACHE_HOME=/tmp";
            env_exec[envc++]="MOZILLA_FIVE_HOME=/opt/firefox";
            env_exec[envc++]="MOZ_GRE_HOME=/opt/firefox";
            env_exec[envc++]="MOZ_XRE_DIR=/opt/firefox";
            env_exec[envc++]="MOZ_APP_LAUNCHER=/opt/firefox/firefox";
            env_exec[envc++]="MOZ_LEGACY_PROFILES=1";
            env_exec[envc++]="MOZ_ALLOW_DOWNGRADE=1";
            env_exec[envc++]="MOZ_FORCE_DISABLE_E10S=1";
            env_exec[envc++]="DBUS_SESSION_BUS_ADDRESS=unix:path=/tmp/ridux-no-dbus";
            env_exec[envc++]="DBUS_SYSTEM_BUS_ADDRESS=unix:path=/tmp/ridux-no-system-dbus";
            env_exec[envc++]="GTK_USE_PORTAL=0";
            env_exec[envc++]="GSETTINGS_BACKEND=memory";
            env_exec[envc++]="GSETTINGS_SCHEMA_DIR=/usr/share/glib-2.0/schemas";
            env_exec[envc++]="GTK_MODULES=";
            env_exec[envc++]="GTK_A11Y=none";
            env_exec[envc++]="GIO_USE_VFS=local";
            env_exec[envc++]="GIO_USE_VOLUME_MONITOR=unix";
            env_exec[envc++]="XDG_CURRENT_DESKTOP=Ridux";
            env_exec[envc++]="DESKTOP_SESSION=ridux";
            env_exec[envc++]="AT_SPI_BUS_ADDRESS=unix:path=/tmp/ridux-no-at-spi";
        }else{
            env_exec[envc++]="XDG_SESSION_TYPE=x11";
            env_exec[envc++]="DBUS_SESSION_BUS_ADDRESS=unix:path=/run/user/0/bus";
        }
        if(is_chrome){
            env_exec[envc++]="LD_LIBRARY_PATH=/opt/chromium:/opt/google/chrome:/lib64:/lib:/usr/lib:/lib/x86_64-linux-gnu:/usr/lib/x86_64-linux-gnu";
        }else if(is_firefox){
            env_exec[envc++]="LD_LIBRARY_PATH=/opt/firefox:/opt/chromium:/opt/google/chrome:/lib64:/lib:/usr/lib:/lib/x86_64-linux-gnu:/usr/lib/x86_64-linux-gnu";
        }else{
            env_exec[envc++]="LD_LIBRARY_PATH=/opt/chromium:/opt/google/chrome:/opt/firefox:/lib64:/lib:/usr/lib:/lib/x86_64-linux-gnu:/usr/lib/x86_64-linux-gnu";
        }
        if(is_chrome||is_firefox){
            env_exec[envc++]="FONTCONFIG_FILE=/etc/fonts/fonts-ridux.conf";
            env_exec[envc++]="FONTCONFIG_PATH=/etc/fonts";
            env_exec[envc++]="FONTCONFIG_USE_MMAP=0";
            env_exec[envc++]="FC_DEBUG=0";
            env_exec[envc++]="XDG_DATA_HOME=/tmp";
            env_exec[envc++]="PANGOCAIRO_BACKEND=fontconfig";
            env_exec[envc++]="LIBGL_ALWAYS_SOFTWARE=1";
            env_exec[envc++]="MESA_LOADER_DRIVER_OVERRIDE=llvmpipe";
        }
        if(is_firefox){
            env_exec[envc++]="MOZ_WEBRENDER=0";
            env_exec[envc++]="MOZ_ACCELERATED=0";
            env_exec[envc++]="MOZ_AVOID_OPENGL_ALTOGETHER=1";
            env_exec[envc++]="MOZ_DISABLE_GFX_SANITY_TEST=1";
            env_exec[envc++]="MOZ_DISABLE_GLX_TEST=1";
            env_exec[envc++]="MOZ_X11_EGL=0";
            env_exec[envc++]="MOZ_ENABLE_WAYLAND=1";
            env_exec[envc++]="MOZ_USE_XINPUT2=1";
            env_exec[envc++]="GDK_BACKEND=wayland,x11";
            env_exec[envc++]="GDK_GL=disable";
            env_exec[envc++]="MESA_GLSL_CACHE_DISABLE=1";
            env_exec[envc++]="MESA_SHADER_CACHE_DISABLE=1";
            env_exec[envc++]="MOZ_DISABLE_CONTENT_SANDBOX=1";
            env_exec[envc++]="MOZ_DISABLE_RDD_SANDBOX=1";
            env_exec[envc++]="MOZ_DISABLE_GMP_SANDBOX=1";
            env_exec[envc++]="MOZ_DISABLE_SOCKET_PROCESS_SANDBOX=1";
            env_exec[envc++]="MOZ_DISABLE_UTILITY_SANDBOX=1";
            env_exec[envc++]="MOZ_DBUS_REMOTE=0";
            env_exec[envc++]="MOZ_ENABLE_DBUS=0";
            env_exec[envc++]="MOZ_DISABLE_DBUS=1";
            env_exec[envc++]="NO_AT_BRIDGE=1";
            env_exec[envc++]="MOZ_DISABLE_GPU_SANDBOX=1";
            env_exec[envc++]="MOZ_DISABLE_SOCKET_PROCESS=1";
            env_exec[envc++]="MOZ_DISABLE_GPU_PROCESS=1";
            env_exec[envc++]="MOZ_DISABLE_RDD_PROCESS=1";
            env_exec[envc++]="MOZ_DISABLE_UTILITY_PROCESS=1";
            env_exec[envc++]="MOZ_DISABLE_FORKSERVER=1";
            env_exec[envc++]="MOZ_GMP_DISABLE=1";
            env_exec[envc++]="MOZ_NO_REMOTE=1";
            env_exec[envc++]="GTK_CSD=0";
            env_exec[envc++]="MOZ_CRASHREPORTER_DISABLE=1";
            env_exec[envc++]="MOZ_CRASHREPORTER_NO_REPORT=1";
            env_exec[envc++]="MOZ_DISABLE_CRASHREPORTER=1";
        }
        env_exec[envc]=0;
        c5_spawn_force_stage("setup stack begin");
        stack_rc=elf64_setup_stack(t,argc_exec,argv_exec,env_exec,auxv,auxc);
        c5_spawn_force_rc("setup stack",stack_rc);
        if(stack_rc<0){
            paging_switch(old_as);
            c5_abort_task(pid);
            ulibc_snprintf(detail,cap,"browser run: stack/auxv init fallo (rc=%d).\n",stack_rc);
            return -ENOEXEC;
        }
        if(interp_data&&interp_sz){
            /*
             * Dynamic ELF startup must look like Linux: ld-linux builds the
             * initial glibc TLS block and installs FS with arch_prctl().
             * A synthetic kernel-side TCB here can leak into libc's startup
             * TLS variables and crash before Firefox reaches libxul.
             */
            t->ctx.fs_base=0;
            t->ctx.gs_base=0;
        }else{
            stack_rc=compat3_init_user_tls(t);
            if(stack_rc<0){
                paging_switch(old_as);
                c5_abort_task(pid);
                ulibc_snprintf(detail,cap,"browser run: tls init fallo (rc=%d).\n",stack_rc);
                return -ENOEXEC;
            }
        }
    }
    paging_switch(old_as);
    /* Wire the task's kernel stack so the round-robin scheduler can
     * dispatch it: context_switch_kstack into kernel_rsp_saved must
     * land in clone3_entry_trampoline (isr64.S) which calls
     * clone3_enter_user -> iretq into ring 3. Also flips
     * needs_first_launch=true so task_schedule()'s first-launch
     * priority scan finds this task. Required for the deferred-launch
     * path (e.g. `r3 <elf>`); the immediate-launch path below bypasses
     * this and goes straight through task_launch_to_user. */
    { extern void clone3_setup_kstack(task_t *t);
      clone3_setup_kstack(t); }
    c5_spawn_force_stage("kstack ready");
    t->state=TASK_RUNNABLE;
    if(launch_now){
        ulibc_snprintf(detail,cap,
            "browser run: task creado pid=%d file=%s\n"
            "estado: task creado (launch inmediato; binario real)\n"
            "entry=%u rsp=%u loader=%s\n",
            pid,path,(unsigned)t->entry_point,(unsigned)t->ctx.rsp,
            interp_data?resolved_interp:"(none)");
        __boot_serial_puts("[spawn] launching pid=");
        __boot_serial_putu32((uint32_t)pid);
        __boot_serial_puts(" rip=");
        __boot_serial_puthex64(t->ctx.rip);
        __boot_serial_puts(" rsp=");
        __boot_serial_puthex64(t->ctx.rsp);
        __boot_serial_puts(" (immediate launch)\n");
        c5_spawn_force_stage("launch immediate");
        task_launch_to_user(t);
        return pid;
    }
    ulibc_snprintf(detail,cap,
        "browser run: staged pid=%d file=%s\n"
        "entry=%u rsp=%u loader=%s\n",
        pid,path,(unsigned)t->entry_point,(unsigned)t->ctx.rsp,
        interp_data?resolved_interp:"(none)");
    __boot_serial_puts("[spawn] staged pid=");
    __boot_serial_putu32((uint32_t)pid);
    __boot_serial_puts(" rip=");
    __boot_serial_puthex64(t->ctx.rip);
    __boot_serial_puts(" rsp=");
    __boot_serial_puthex64(t->ctx.rsp);
    __boot_serial_puts(" (deferred launch)\n");
    /* IMPORTANT: enable preemptive scheduling. The timer IRQ in
     * kernel.c only calls task_schedule() while g_user_foreground_active
     * is true, and the flag is otherwise only set by task_launch_to_user
     * (immediate-launch path). Without this, deferred-launch tasks
     * (e.g. spawned via the `r3` shell command for Ring 3 ELFs) stay
     * RUNNABLE forever and never get picked up by the scheduler. */
    g_user_foreground_active = true;
    c5_spawn_force_stage("deferred ready");
    return pid;
}
static void c5_browser_usage(char *o,int mx){
    ulibc_snprintf(o,(size_t)mx,
        "browser help\n"
        "browser check [ruta_elf64]\n"
        "browser run [ruta_elf64]\n"
        "browser realrun [ruta_binario_real] (lanzamiento inmediato)\n"
        "compat: browser <ruta_elf64> sigue funcionando como alias de check.\n"
        "ejemplos: browser check /bin/firefox.elf | browser run firefox | browser realrun /opt/firefox/firefox-bin | chrome\n");
}
static void cmd5_browser(const char*a,char*o,int mx){
    const char *target="firefox/chromium";
    const char *probe_path=0;
    const char *argp=a?a:"";
    char sub[32];
    char run_detail[320];
    const char *ok="ok";
    const char *part="partial";
    const char *miss="missing";
    const char *stub="stub";
    const uint8_t *ld_data=0;
    uint32_t ld_sz=0;
    bool has_dyn_loader=false;
    bool has_execve=(g_syscall_table[59]!=0);
    bool has_mmap=(g_syscall_table[9]!=0&&g_syscall_table[10]!=0&&g_syscall_table[11]!=0);
    bool has_threads=(g_syscall_table[56]!=0&&g_syscall_table[202]!=0);
    bool has_io=(g_syscall_table[0]!=0&&g_syscall_table[1]!=0&&g_syscall_table[257]!=0);
    bool has_path_abi=(g_syscall_table[257]!=0&&g_syscall_table[262]!=0&&g_syscall_table[267]!=0);
    bool has_net=(g_syscall_table[41]!=0&&g_syscall_table[42]!=0&&g_syscall_table[44]!=0&&g_syscall_table[45]!=0&&
                  g_syscall_table[49]!=0&&g_syscall_table[50]!=0);
    bool has_sock_meta=(g_syscall_table[51]!=0&&g_syscall_table[52]!=0&&g_syscall_table[54]!=0&&g_syscall_table[55]!=0);
    bool has_mmsg=(g_syscall_table[299]!=0&&g_syscall_table[307]!=0);
    bool has_msg_ipc=(g_syscall_table[46]!=0&&g_syscall_table[47]!=0&&g_syscall_table[53]!=0);
    bool has_epoll=(g_syscall_table[232]!=0&&g_syscall_table[233]!=0&&g_syscall_table[291]!=0);
    bool has_timerfd=(g_syscall_table[286]!=0&&g_syscall_table[290]!=0);
    bool has_dyn_reloc=compat3_has_dynamic_relocator();
    bool has_userfault=(g_syscall_table[323]!=0);
    bool has_vmread=(g_syscall_table[310]!=0&&g_syscall_table[311]!=0);
    bool has_graphics=(g_drm_mode.width>0&&g_drm_mode.height>0);
    bool probe_http_virtual=false;
    bool probe_x11=false;
    bool probe_wayland=false;
    bool probe_dbus=false;
    char probe[320];
    bool target_dynamic=false;
    bool target_interp_present=false;
    bool target_present=false;

    if(kvfs_read("/lib64/ld-linux-x86-64.so.2",&ld_data,&ld_sz)||
       kvfs_read("/lib/ld-linux-x86-64.so.2",&ld_data,&ld_sz)||
       kvfs_read("/libexec/ld-elf.so.1",&ld_data,&ld_sz)){
        has_dyn_loader=true;
    }

    probe[0]=0;
    if(c5_next_tok(&argp,sub,sizeof(sub))){
        const char *rest=c5_skip_ws(argp);
        if(ulibc_strcmp(sub,"help")==0){c5_browser_usage(o,mx);return;}
        if(ulibc_strcmp(sub,"run")==0||ulibc_strcmp(sub,"realrun")==0){
            bool force_real=(ulibc_strcmp(sub,"realrun")==0);
            const char *run_path=*rest?rest:c5_default_browser_target(force_real);
            const char *exec_path=run_path;
            char resolved_path[VFS_PATH_MAX];
            if(!force_real&&c5_browser_path_alias(run_path)){
                if(c5_resolve_browser_alias_binary(run_path,resolved_path,sizeof(resolved_path))){
                    exec_path=resolved_path;
                }else{
                    compat_ui_open_app("chrome");
                    ulibc_snprintf(o,(size_t)mx,
                        "browser run: launcher Ridux activado (fallback).\n"
                        "target=%s\n"
                        "no se encontro ELF64 real para alias; UI nativa abierta.\n"
                        "hint: usa browser realrun /opt/firefox/firefox-bin cuando el binario exista.\n",
                        run_path);
                    return;
                }
            }
            bool launch_now=force_real;
            int rr=c5_spawn_elf_task(exec_path,launch_now,run_detail,sizeof(run_detail));
            if(rr<0){ulibc_snprintf(o,(size_t)mx,"%s",run_detail);return;}
            if(launch_now){
                ulibc_snprintf(o,(size_t)mx,"%s",run_detail);
                return;
            }
            if(c5_has_token_ci(exec_path,"firefox")||c5_has_token_ci(run_path,"firefox")){
                /* Firefox real crea su propia superficie Wayland. Abrir aqui
                 * el tile falso lanzaba una segunda instancia y parecia cuelgue. */
            }else{
                compat_ui_open_app("Browser");
            }
            ulibc_snprintf(o,(size_t)mx,
                "%s"
                "estado: task staged (launch diferido; kernel/UI siguen activos).\n"
                "ui nativa abierta para render estable.\n",
                run_detail);
            return;
        }
        if(ulibc_strcmp(sub,"check")==0){
            if(*rest){probe_path=rest;target=rest;}
        }else{
            /* backward-compatible alias: browser /ruta/a.bin */
            probe_path=a;
            target=a;
        }
    }

    if(probe_path&&*probe_path){
        const uint8_t *data=0;
        uint32_t sz=0;
        if(!kvfs_read(probe_path,&data,&sz)){
            ulibc_snprintf(probe,sizeof(probe),
                "binary probe: '%s' no existe en RiduxFS/initrd.\n",probe_path);
        }else if(!elf64_validate(data,sz)){
            ulibc_snprintf(probe,sizeof(probe),
                "binary probe: '%s' no es ELF64 valido (size=%u).\n",probe_path,(unsigned)sz);
        }else{
            const elf64_ehdr_t *eh=(const elf64_ehdr_t*)data;
            const char *etype=(eh->e_type==ET_DYN)?"ET_DYN":"ET_EXEC";
            char interp[96];
            uint16_t i;
            target_present=true;
            target_dynamic=(eh->e_type==ET_DYN);
            interp[0]=0;
            for(i=0;i<eh->e_phnum;++i){
                uint64_t off=eh->e_phoff+(uint64_t)i*eh->e_phentsize;
                if(off+sizeof(elf64_phdr_t)>sz)break;
                {
                    const elf64_phdr_t *ph=(const elf64_phdr_t*)(data+off);
                    if(ph->p_type==PT_INTERP&&ph->p_offset+ph->p_filesz<=sz&&ph->p_filesz>1){
                        size_t n=(size_t)ph->p_filesz-1;
                        if(n>=sizeof(interp))n=sizeof(interp)-1;
                        ulibc_memcpy(interp,data+ph->p_offset,n);
                        interp[n]=0;
                        break;
                    }
                }
            }
            if(interp[0]){
                const uint8_t *idata=0;
                uint32_t isz=0;
                if(kvfs_read(interp,&idata,&isz))target_interp_present=true;
                else if(c5_find_loader(interp,&idata,&isz,interp,sizeof(interp)))target_interp_present=true;
            }
            ulibc_snprintf(probe,sizeof(probe),
                "binary probe: '%s' %s phnum=%u interp=%s (%s)\n",
                probe_path,etype,(unsigned)eh->e_phnum,interp[0]?interp:"(none)",
                interp[0]?(target_interp_present?ok:miss):stub);
        }
    }
    probe_http_virtual=c5_probe_http_virtual();
    probe_x11=c5_probe_x11_ipc();
    probe_wayland=c5_probe_wayland_ipc();
    probe_dbus=c5_probe_dbus_ipc();
    ulibc_snprintf(o,(size_t)mx,
        "=== Browser readiness (%s) ===\n"
        "syscalls io/openat/read/write: %s\n"
        "path ABI (newfstatat/readlinkat): %s\n"
        "execve + ELF64 map: %s\n"
        "mmap/mprotect/munmap: %s\n"
        "clone + futex threading: %s\n"
        "socket/connect/send/recv/bind/listen: %s\n"
        "sockname/sockopt ABI: %s\n"
        "sendmmsg/recvmmsg ABI: %s\n"
        "msg IPC (sendmsg/recvmsg/socketpair): %s\n"
        "epoll stack: %s\n"
        "eventfd/timerfd ABI: %s\n"
        "dynamic loader in RiduxFS: %s\n"
        "dynamic reloc engine: %s\n"
        "virtual HTTP probe (/healthz): %s\n"
        "DRM framebuffer mode: %s (%ux%u)\n"
        "X11 IPC connect: %s  Wayland IPC connect: %s  DBus IPC connect: %s\n"
        "sandbox (seccomp/userfaultfd/vm_readv): %s\n"
        "%s"
        "VEREDICTO: staging ELF real para Firefox/Chromium SI; navegador diario totalmente estable TODAVIA NO.\n"
        "Avance: browser run prioriza binario real y Firefox usa perfil software/sandbox reducido para evitar colapsos tempranos.\n"
        "Bloqueos actuales: falta TCP/TLS real end-to-end con certificados, compositor IPC real (X11/Wayland/DBus sin virtual), y ABI userspace/glibc aun parcial.\n"
        "Nota: con binary probe ya validamos ET_DYN/PT_INTERP y presencia del loader objetivo.\n"
        "Lo que si acelera Ridux ahora: libc/regex/fnmatch/base64 y partes POSIX de compat5.\n",
        target,
        has_io?ok:miss,
        has_path_abi?part:miss,
        has_execve?part:miss,
        has_mmap?part:miss,
        has_threads?part:miss,
        has_net?part:miss,
        has_sock_meta?part:miss,
        has_mmsg?part:miss,
        has_msg_ipc?part:stub,
        has_epoll?part:miss,
        has_timerfd?part:stub,
        has_dyn_loader?part:miss,
        has_dyn_reloc?part:miss,
        probe_http_virtual?part:stub,
        has_graphics?ok:miss,
        (unsigned)g_drm_mode.width,(unsigned)g_drm_mode.height,
        probe_x11?part:stub,
        probe_wayland?part:stub,
        probe_dbus?part:stub,
        (has_userfault&&has_vmread)?part:stub,
        probe);
    if(target_present&&target_dynamic&&!target_interp_present){
        size_t used=ulibc_strlen(o);
        if((int)used<mx){
            ulibc_snprintf(o+used,(size_t)(mx-(int)used),
                "Hint: agrega el loader del binario a initrd (ruta PT_INTERP) para avanzar al siguiente bloqueo.\n");
        }
    }
}

int compat5_spawn_user_elf_background(const char *path, char *detail, size_t detail_cap){
    char scratch[512];
    char *out = detail;
    size_t cap = detail_cap;
    int rc;
    if(!out || cap == 0){ out = scratch; cap = sizeof(scratch); }
    out[0] = 0;
    rc = c5_spawn_elf_task(path, false, out, cap);
    return rc;
}

void compat5_register_shell_cmds(void){
    extern compat_shell_cmd_t g_compat_cmds[];
    extern int g_compat_cmd_count;
    #define REG5(n,h,fn) if(g_compat_cmd_count<COMPAT_SHELL_CMD_MAX){g_compat_cmds[g_compat_cmd_count].name=n;g_compat_cmds[g_compat_cmd_count].help=h;g_compat_cmds[g_compat_cmd_count].handler=fn;++g_compat_cmd_count;}
    REG5("bsd","Portable libc extension info",cmd5_bsd)
    REG5("b64","Base64 encode/decode demo",cmd5_b64)
    REG5("rng","arc4random demo",cmd5_rng)
    REG5("browser","browser help | check [elf] | run [elf]",cmd5_browser)
    #undef REG5
}

void compat5_init_all(void){
    /* stir arc4random entropy pool */
    a4st();
    compat5_register_shell_cmds();
}
