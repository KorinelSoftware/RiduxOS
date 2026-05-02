/*
 * Red real, TLS basico y bridge grafico X11/Wayland.
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "base.h"
#include "memory_tasks.h"
#include "linux_syscalls.h"
#include "user_libc.h"
#include "bsd_libc.h"
#include "linux_abi.h"
#include "display_wayland.h"

extern void*ulibc_malloc(size_t);extern void ulibc_free(void*);
extern void*ulibc_memcpy(void*,const void*,size_t);extern void*ulibc_memset(void*,int,size_t);
extern size_t ulibc_strlen(const char*);extern char*ulibc_strcpy(char*,const char*);
extern int ulibc_strcmp(const char*,const char*);extern int ulibc_strncmp(const char*,const char*,size_t);
extern int ulibc_memcmp(const void*,const void*,size_t);
extern int ulibc_snprintf(char*,size_t,const char*,...);
extern uint32_t ulibc_arc4random(void);
extern void ulibc_arc4random_buf(void*,size_t);
extern int64_t real_sys_lseek(int fd,int64_t offset,int whence);
extern int64_t real_sys_read(int fd,void *buf,size_t count);
extern bool g_use_backbuffer;
extern uint32_t g_backbuffer[];
extern void fb_present(void);

#define C7_FB_MAX_WIDTH 1920u

static char c7_ascii_tolower(char ch){
    if(ch>='A'&&ch<='Z')return (char)(ch-'A'+'a');
    return ch;
}

static int c7_strcasecmp(const char *a,const char *b){
    while(*a&&*b){
        char ca=c7_ascii_tolower(*a);
        char cb=c7_ascii_tolower(*b);
        if(ca!=cb)return (int)((unsigned char)ca-(unsigned char)cb);
        ++a;
        ++b;
    }
    return (int)((unsigned char)c7_ascii_tolower(*a)-(unsigned char)c7_ascii_tolower(*b));
}

static size_t c7_strnlen_u8(const uint8_t *s,size_t maxn){
    size_t n=0;
    while(n<maxn&&s[n])++n;
    return n;
}

/* DRM/Wayland pixel formats (fourcc values) commonly used by Firefox. */
#define WL7_FMT_ARGB8888   0u
#define WL7_FMT_XRGB8888   1u
#define WL7_FMT_DRM_ARGB32 0x34325241u /* AR24 */
#define WL7_FMT_DRM_XRGB32 0x34325258u /* XR24 */
#define WL7_FMT_DRM_ABGR32 0x34324241u /* AB24 */
#define WL7_FMT_DRM_XBGR32 0x34324258u /* XB24 */

/* Framebuffer type (mirrored from kernel.c for extern access) */
typedef struct {
    uint8_t *address;
    uint32_t pitch;
    uint32_t width;
    uint32_t height;
    uint8_t  bpp;
    uint8_t  red_pos, red_size;
    uint8_t  green_pos, green_size;
    uint8_t  blue_pos, blue_size;
    bool     ready;
} c7_framebuffer_t;

/* CRYPTO: AES-128/256 (software, no SSE) */
static const uint8_t g_aes_sbox[256]={
0x63,0x7c,0x77,0x7b,0xf2,0x6b,0x6f,0xc5,0x30,0x01,0x67,0x2b,0xfe,0xd7,0xab,0x76,
0xca,0x82,0xc9,0x7d,0xfa,0x59,0x47,0xf0,0xad,0xd4,0xa2,0xaf,0x9c,0xa4,0x72,0xc0,
0xb7,0xfd,0x93,0x26,0x36,0x3f,0xf7,0xcc,0x34,0xa5,0xe5,0xf1,0x71,0xd8,0x31,0x15,
0x04,0xc7,0x23,0xc3,0x18,0x96,0x05,0x9a,0x07,0x12,0x80,0xe2,0xeb,0x27,0xb2,0x75,
0x09,0x83,0x2c,0x1a,0x1b,0x6e,0x5a,0xa0,0x52,0x3b,0xd6,0xb3,0x29,0xe3,0x2f,0x84,
0x53,0xd1,0x00,0xed,0x20,0xfc,0xb1,0x5b,0x6a,0xcb,0xbe,0x39,0x4a,0x4c,0x58,0xcf,
0xd0,0xef,0xaa,0xfb,0x43,0x4d,0x33,0x85,0x45,0xf9,0x02,0x7f,0x50,0x3c,0x9f,0xa8,
0x51,0xa3,0x40,0x8f,0x92,0x9d,0x38,0xf5,0xbc,0xb6,0xda,0x21,0x10,0xff,0xf3,0xd2,
0xcd,0x0c,0x13,0xec,0x5f,0x97,0x44,0x17,0xc4,0xa7,0x7e,0x3d,0x64,0x5d,0x19,0x73,
0x60,0x81,0x4f,0xdc,0x22,0x2a,0x90,0x88,0x46,0xee,0xb8,0x14,0xde,0x5e,0x0b,0xdb,
0xe0,0x32,0x3a,0x0a,0x49,0x06,0x24,0x5c,0xc2,0xd3,0xac,0x62,0x91,0x95,0xe4,0x79,
0xe7,0xc8,0x37,0x6d,0x8d,0xd5,0x4e,0xa9,0x6c,0x56,0xf4,0xea,0x65,0x7a,0xae,0x08,
0xba,0x78,0x25,0x2e,0x1c,0xa6,0xb4,0xc6,0xe8,0xdd,0x74,0x1f,0x4b,0xbd,0x8b,0x8a,
0x70,0x3e,0xb5,0x66,0x48,0x03,0xf6,0x0e,0x61,0x35,0x57,0xb9,0x86,0xc1,0x1d,0x9e,
0xe1,0xf8,0x98,0x11,0x69,0xd9,0x8e,0x94,0x9b,0x1e,0x87,0xe9,0xce,0x55,0x28,0xdf,
0x8c,0xa1,0x89,0x0d,0xbf,0xe6,0x42,0x68,0x41,0x99,0x2d,0x0f,0xb0,0x54,0xbb,0x16};
static const uint8_t g_aes_inv_sbox[256]={
0x52,0x09,0x6a,0xd5,0x30,0x36,0xa5,0x38,0xbf,0x40,0xa3,0x9e,0x81,0xf3,0xd7,0xfb,
0x7c,0xe3,0x39,0x82,0x9b,0x2f,0xff,0x87,0x34,0x8e,0x43,0x44,0xc4,0xde,0xe9,0xcb,
0x54,0x7b,0x94,0x32,0xa6,0xc2,0x23,0x3d,0xee,0x4c,0x95,0x0b,0x42,0xfa,0xc3,0x4e,
0x08,0x2e,0xa1,0x66,0x28,0xd9,0x24,0xb2,0x76,0x5b,0xa2,0x49,0x6d,0x8b,0xd1,0x25,
0x72,0xf8,0xf6,0x64,0x86,0x68,0x98,0x16,0xd4,0xa4,0x5c,0xcc,0x5d,0x65,0xb6,0x92,
0x6c,0x70,0x48,0x50,0xfd,0xed,0xb9,0xda,0x5e,0x15,0x46,0x57,0xa7,0x8d,0x9d,0x84,
0x90,0xd8,0xab,0x00,0x8c,0xbc,0xd3,0x0a,0xf7,0xe4,0x58,0x05,0xb8,0xb3,0x45,0x06,
0xd0,0x2c,0x1e,0x8f,0xca,0x3f,0x0f,0x02,0xc1,0xaf,0xbd,0x03,0x01,0x13,0x8a,0x6b,
0x3a,0x91,0x11,0x41,0x4f,0x67,0xdc,0xea,0x97,0xf2,0xcf,0xce,0xf0,0xb4,0xe6,0x73,
0x96,0xac,0x74,0x22,0xe7,0xad,0x35,0x85,0xe2,0xf9,0x37,0xe8,0x1c,0x75,0xdf,0x6e,
0x47,0xf1,0x1a,0x71,0x1d,0x29,0xc5,0x89,0x6f,0xb7,0x62,0x0e,0xaa,0x18,0xbe,0x1b,
0xfc,0x56,0x3e,0x4b,0xc6,0xd2,0x79,0x20,0x9a,0xdb,0xc0,0xfe,0x78,0xcd,0x5a,0xf4,
0x1f,0xdd,0xa8,0x33,0x88,0x07,0xc7,0x31,0xb1,0x12,0x10,0x59,0x27,0x80,0xec,0x5f,
0x60,0x51,0x7f,0xa9,0x19,0xb5,0x4a,0x0d,0x2d,0xe5,0x7a,0x9f,0x93,0xc9,0x9c,0xef,
0xa0,0xe0,0x3b,0x4d,0xae,0x2a,0xf5,0xb0,0xc8,0xeb,0xbb,0x3c,0x83,0x53,0x99,0x61,
0x17,0x2b,0x04,0x7e,0xba,0x77,0xd6,0x26,0xe1,0x69,0x14,0x63,0x55,0x21,0x0c,0x7d};
static const uint8_t g_aes_rcon[11]={0x00,0x01,0x02,0x04,0x08,0x10,0x20,0x40,0x80,0x1b,0x36};

#define AES_ROTL(x,n)(((x)<<(n))|((x)>>(32-(n))))
static uint32_t aes_sub_word(uint32_t w){return((uint32_t)g_aes_sbox[(w>>24)&0xFF]<<24)|((uint32_t)g_aes_sbox[(w>>16)&0xFF]<<16)|((uint32_t)g_aes_sbox[(w>>8)&0xFF]<<8)|(uint32_t)g_aes_sbox[w&0xFF];}
static uint32_t aes_rot_word(uint32_t w){return(w<<8)|(w>>24);}

void aes7_key_setup(aes7_ctx_t*ctx,const uint8_t*key,int bits){
    int i,nk=bits/32,nr;
    uint32_t*w=ctx->rk;
    if(bits==128)nr=10;else if(bits==192)nr=12;else nr=14;
    ctx->nr=nr;
    for(i=0;i<nk;++i)w[i]=((uint32_t)key[4*i]<<24)|((uint32_t)key[4*i+1]<<16)|((uint32_t)key[4*i+2]<<8)|key[4*i+3];
    for(i=nk;i<4*(nr+1);++i){uint32_t t=w[i-1];if(i%nk==0)t=aes_sub_word(aes_rot_word(t))^((uint32_t)g_aes_rcon[i/nk]<<24);else if(nk>6&&i%nk==4)t=aes_sub_word(t);w[i]=w[i-nk]^t;}
}

static void aes_add_round_key(uint32_t*rk,uint8_t*s){int i;for(i=0;i<16;++i)s[i]^=(uint8_t)(rk[i/4]>>((3-(i%4))*8));}
static void aes_sub_bytes(uint8_t*s){int i;for(i=0;i<16;++i)s[i]=g_aes_sbox[s[i]];}
static void aes_inv_sub_bytes(uint8_t*s){int i;for(i=0;i<16;++i)s[i]=g_aes_inv_sbox[s[i]];}
static void aes_shift_rows(uint8_t*s){
    uint8_t t;t=s[1];s[1]=s[5];s[5]=s[9];s[9]=s[13];s[13]=t;
    t=s[2];s[2]=s[10];s[10]=t;t=s[6];s[6]=s[14];s[14]=t;
    t=s[3];s[3]=s[15];s[15]=s[11];s[11]=s[7];s[7]=t;}
static void aes_inv_shift_rows(uint8_t*s){
    uint8_t t;t=s[13];s[13]=s[9];s[9]=s[5];s[5]=s[1];s[1]=t;
    t=s[10];s[10]=s[2];s[2]=t;t=s[14];s[14]=s[6];s[6]=t;
    t=s[7];s[7]=s[11];s[11]=s[15];s[15]=s[3];s[3]=t;}
static uint8_t aes_mul(uint8_t a,uint8_t b){uint8_t p=0;int i;for(i=0;i<8;++i){if(b&1)p^=a;b>>=1;uint8_t h=a&0x80;a<<=1;if(h)a^=0x1b;}return p;}
static void aes_mix_columns(uint8_t*s){int c;for(c=0;c<4;++c){int i=c*4;uint8_t a0=s[i],a1=s[i+1],a2=s[i+2],a3=s[i+3];s[i]=aes_mul(2,a0)^aes_mul(3,a1)^a2^a3;s[i+1]=a0^aes_mul(2,a1)^aes_mul(3,a2)^a3;s[i+2]=a0^a1^aes_mul(2,a2)^aes_mul(3,a3);s[i+3]=aes_mul(3,a0)^a1^a2^aes_mul(2,a3);}}
static void aes_inv_mix_columns(uint8_t*s){int c;for(c=0;c<4;++c){int i=c*4;uint8_t a0=s[i],a1=s[i+1],a2=s[i+2],a3=s[i+3];s[i]=aes_mul(0x0e,a0)^aes_mul(0x0b,a1)^aes_mul(0x0d,a2)^aes_mul(0x09,a3);s[i+1]=aes_mul(0x09,a0)^aes_mul(0x0e,a1)^aes_mul(0x0b,a2)^aes_mul(0x0d,a3);s[i+2]=aes_mul(0x0d,a0)^aes_mul(0x09,a1)^aes_mul(0x0e,a2)^aes_mul(0x0b,a3);s[i+3]=aes_mul(0x0b,a0)^aes_mul(0x0d,a1)^aes_mul(0x09,a2)^aes_mul(0x0e,a3);}}

void aes7_encrypt(aes7_ctx_t*ctx,const uint8_t in[16],uint8_t out[16]){
    uint8_t s[16];int i,r;ulibc_memcpy(s,in,16);
    aes_add_round_key(ctx->rk,s);
    for(r=1;r<ctx->nr;++r){aes_sub_bytes(s);aes_shift_rows(s);aes_mix_columns(s);aes_add_round_key(ctx->rk+r*4,s);}
    aes_sub_bytes(s);aes_shift_rows(s);aes_add_round_key(ctx->rk+ctx->nr*4,s);
    ulibc_memcpy(out,s,16);
}
void aes7_decrypt(aes7_ctx_t*ctx,const uint8_t in[16],uint8_t out[16]){
    uint8_t s[16];int i,r;ulibc_memcpy(s,in,16);
    aes_add_round_key(ctx->rk+ctx->nr*4,s);
    for(r=ctx->nr-1;r>0;--r){aes_inv_shift_rows(s);aes_inv_sub_bytes(s);aes_add_round_key(ctx->rk+r*4,s);aes_inv_mix_columns(s);}
    aes_inv_shift_rows(s);aes_inv_sub_bytes(s);aes_add_round_key(ctx->rk,s);
    ulibc_memcpy(out,s,16);
}

void aes7_cbc_encrypt(aes7_ctx_t*ctx,const uint8_t*iv,const uint8_t*in,uint8_t*out,size_t len){
    uint8_t prev[16];ulibc_memcpy(prev,iv,16);size_t i;
    for(i=0;i<len;i+=16){int j;uint8_t blk[16];for(j=0;j<16;++j)blk[j]=in[i+j]^prev[j];
        aes7_encrypt(ctx,blk,out+i);ulibc_memcpy(prev,out+i,16);}}
void aes7_cbc_decrypt(aes7_ctx_t*ctx,const uint8_t*iv,const uint8_t*in,uint8_t*out,size_t len){
    uint8_t prev[16];ulibc_memcpy(prev,iv,16);size_t i;
    for(i=0;i<len;i+=16){uint8_t tmp[16];aes7_decrypt(ctx,in+i,tmp);int j;for(j=0;j<16;++j)out[i+j]=tmp[j]^prev[j];ulibc_memcpy(prev,in+i,16);}}

/* CRYPTO: SHA-256 */
static const uint32_t g_sha256_k[64]={
0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2};

#define SHA_ROTR(x,n)(((x)>>(n))|((x)<<(32-(n))))
#define SHA_CH(x,y,z)((x)&(y)^(~(x)&(z)))
#define SHA_MAJ(x,y,z)((x)&(y)^(x)&(z)^(y)&(z))
#define SHA_EP0(x)(SHA_ROTR(x,2)^SHA_ROTR(x,13)^SHA_ROTR(x,22))
#define SHA_EP1(x)(SHA_ROTR(x,6)^SHA_ROTR(x,11)^SHA_ROTR(x,25))
#define SHA_SIG0(x)(SHA_ROTR(x,7)^SHA_ROTR(x,18)^((x)>>3))
#define SHA_SIG1(x)(SHA_ROTR(x,17)^SHA_ROTR(x,19)^((x)>>10))

void sha256_init(sha256_ctx_t*ctx){ctx->state[0]=0x6a09e667;ctx->state[1]=0xbb67ae85;ctx->state[2]=0x3c6ef372;ctx->state[3]=0xa54ff53a;ctx->state[4]=0x510e527f;ctx->state[5]=0x9b05688c;ctx->state[6]=0x1f83d9ab;ctx->state[7]=0x5be0cd19;ctx->count=0;ulibc_memset(ctx->buf,0,64);}

static void sha256_transform(sha256_ctx_t*ctx,const uint8_t blk[64]){
    uint32_t w[64];int i;for(i=0;i<16;++i)w[i]=((uint32_t)blk[i*4]<<24)|((uint32_t)blk[i*4+1]<<16)|((uint32_t)blk[i*4+2]<<8)|blk[i*4+3];
    for(i=16;i<64;++i)w[i]=SHA_SIG1(w[i-2])+w[i-7]+SHA_SIG0(w[i-15])+w[i-16];
    uint32_t a=ctx->state[0],b=ctx->state[1],c=ctx->state[2],d=ctx->state[3],e=ctx->state[4],f=ctx->state[5],g=ctx->state[6],h=ctx->state[7];
    for(i=0;i<64;++i){uint32_t t1=h+SHA_EP1(e)+SHA_CH(e,f,g)+g_sha256_k[i]+w[i];uint32_t t2=SHA_EP0(a)+SHA_MAJ(a,b,c);h=g;g=f;f=e;e=d+t1;d=c;c=b;b=a;a=t1+t2;}
    ctx->state[0]+=a;ctx->state[1]+=b;ctx->state[2]+=c;ctx->state[3]+=d;ctx->state[4]+=e;ctx->state[5]+=f;ctx->state[6]+=g;ctx->state[7]+=h;
}

void sha256_update(sha256_ctx_t*ctx,const void*data,size_t len){
    const uint8_t*p=data;size_t i;uint32_t used=(uint32_t)(ctx->count%64);
    ctx->count+=len;
    if(used){size_t avail=64-used;if(len<avail){ulibc_memcpy(ctx->buf+used,p,len);return;}
        ulibc_memcpy(ctx->buf+used,p,avail);sha256_transform(ctx,ctx->buf);p+=avail;len-=avail;}
    while(len>=64){sha256_transform(ctx,p);p+=64;len-=64;}
    if(len)ulibc_memcpy(ctx->buf,p,len);
}

void sha256_final(sha256_ctx_t*ctx,uint8_t out[32]){
    uint64_t bits=ctx->count*8;uint32_t used=(uint32_t)(ctx->count%64);
    ctx->buf[used++]=0x80;
    if(used>56){while(used<64)ctx->buf[used++]=0;sha256_transform(ctx,ctx->buf);used=0;}
    while(used<56)ctx->buf[used++]=0;
    ctx->buf[56]=(uint8_t)(bits>>56);ctx->buf[57]=(uint8_t)(bits>>48);ctx->buf[58]=(uint8_t)(bits>>40);ctx->buf[59]=(uint8_t)(bits>>32);
    ctx->buf[60]=(uint8_t)(bits>>24);ctx->buf[61]=(uint8_t)(bits>>16);ctx->buf[62]=(uint8_t)(bits>>8);ctx->buf[63]=(uint8_t)(bits);
    sha256_transform(ctx,ctx->buf);
    int i;for(i=0;i<8;++i){out[i*4]=(uint8_t)(ctx->state[i]>>24);out[i*4+1]=(uint8_t)(ctx->state[i]>>16);out[i*4+2]=(uint8_t)(ctx->state[i]>>8);out[i*4+3]=(uint8_t)(ctx->state[i]);}
}

void sha256(const void*data,size_t len,uint8_t out[32]){sha256_ctx_t c;sha256_init(&c);sha256_update(&c,data,len);sha256_final(&c,out);}

/* HMAC-SHA256 */
void hmac_sha256_init(hmac_sha256_ctx_t*ctx,const uint8_t*key,size_t keylen){
    uint8_t k[64];ulibc_memset(k,0,64);
    if(keylen>64){sha256(key,keylen,k);}else{ulibc_memcpy(k,key,keylen);}
    uint8_t ipad[64],opad[64];int i;
    for(i=0;i<64;++i){ipad[i]=k[i]^0x36;opad[i]=k[i]^0x5c;}
    sha256_init(&ctx->inner);sha256_update(&ctx->inner,ipad,64);
    sha256_init(&ctx->outer);sha256_update(&ctx->outer,opad,64);
}
void hmac_sha256_update(hmac_sha256_ctx_t*ctx,const void*data,size_t len){sha256_update(&ctx->inner,data,len);}
void hmac_sha256_final(hmac_sha256_ctx_t*ctx,uint8_t out[32]){
    uint8_t inner_hash[32];sha256_final(&ctx->inner,inner_hash);
    sha256_update(&ctx->outer,inner_hash,32);sha256_final(&ctx->outer,out);
}

/* TLS 1.2 PRF (RFC 5246) - P_SHA256 */
void tls7_prf(const uint8_t*secret,size_t slen,const char*label,const uint8_t*seed,size_t seedlen,uint8_t*out,size_t outlen){
    size_t llen=ulibc_strlen(label);
    size_t tlen=llen+seedlen;
    uint8_t*seed_merged=(uint8_t*)ulibc_malloc(tlen);
    ulibc_memcpy(seed_merged,label,llen);ulibc_memcpy(seed_merged+llen,seed,seedlen);
    /* A(0)=seed, A(i)=HMAC(secret,A(i-1)) */
    uint8_t a[32];hmac_sha256_ctx_t hctx;
    hmac_sha256_init(&hctx,secret,slen);hmac_sha256_update(&hctx,seed_merged,tlen);hmac_sha256_final(&hctx,a);
    size_t done=0;
    while(done<outlen){
        hmac_sha256_init(&hctx,secret,slen);hmac_sha256_update(&hctx,a,32);hmac_sha256_update(&hctx,seed_merged,tlen);
        uint8_t tmp[32];hmac_sha256_final(&hctx,tmp);
        size_t cp=outlen-done;if(cp>32)cp=32;ulibc_memcpy(out+done,tmp,cp);done+=cp;
        /* next A */
        hmac_sha256_init(&hctx,secret,slen);hmac_sha256_update(&hctx,a,32);hmac_sha256_final(&hctx,a);
    }
    ulibc_free(seed_merged);
}

/* TCP REAL: state machine + congestion control + retransmission */
tcp7_tcb_t g_tcp7_tcbs[TCP7_MAX_CONNECTIONS];

static tcp7_tcb_t*tcp7_alloc(void){
    int i;
    for(i=0;i<TCP7_MAX_CONNECTIONS;++i){
        if(g_tcp7_tcbs[i].sock_fd>=0)continue;
        ulibc_memset(&g_tcp7_tcbs[i],0,sizeof(tcp7_tcb_t));
        g_tcp7_tcbs[i].sock_fd=-1;
        g_tcp7_tcbs[i].state=TCP7_CLOSED;
        return &g_tcp7_tcbs[i];
    }
    return 0;
}
static tcp7_tcb_t*tcp7_find(int fd){int i;for(i=0;i<TCP7_MAX_CONNECTIONS;++i)if(g_tcp7_tcbs[i].sock_fd==fd)return&g_tcp7_tcbs[i];return 0;}

static uint32_t tcp7_isn(void){return ulibc_arc4random();}

static void tcp7_release_tcb(tcp7_tcb_t*tcb){
    if(!tcb)return;
    tcb->sock_fd=-1;
    tcb->state=TCP7_CLOSED;
    tcb->delack_pending=false;
    tcb->time_wait_expire=0;
    tcb->delack_expire=0;
}

static void tcp7_rtt_update(tcp7_tcb_t*tcb,uint32_t rtt_ms){
    if(!tcb->srtt){tcb->srtt=rtt_ms;tcb->rttvar=rtt_ms/2;}
    else{tcb->rttvar=(3*tcb->rttvar+((tcb->srtt>rtt_ms)?tcb->srtt-rtt_ms:rtt_ms-tcb->srtt))/4;
         tcb->srtt=(7*tcb->srtt+rtt_ms)/8;}
    tcb->rto=tcb->srtt+4*tcb->rttvar;
    if(tcb->rto<TCP7_RTO_MIN)tcb->rto=TCP7_RTO_MIN;
    if(tcb->rto>TCP7_RTO_MAX)tcb->rto=TCP7_RTO_MAX;
}

static void tcp7_send_syn(int fd,tcp7_tcb_t*tcb){
    socket_t*s=&g_sockets[fd];
    uint8_t pkt[60];int h=net_build_tcp(pkt,s->local_port,s->remote_port,tcb->iss,0,TCP_SYN,tcb->rcv_wnd);
    sock_send(fd,pkt,(size_t)h,0);
    tcb->snd_nxt=tcb->iss+1;tcb->snd_una=tcb->iss;
}

static void tcp7_send_synack(int fd,tcp7_tcb_t*tcb){
    socket_t*s=&g_sockets[fd];
    uint8_t pkt[60];int h=net_build_tcp(pkt,s->local_port,s->remote_port,tcb->iss,tcb->irs+1,TCP_SYN|TCP_ACK,tcb->rcv_wnd);
    sock_send(fd,pkt,(size_t)h,0);
    tcb->snd_nxt=tcb->iss+1;tcb->snd_una=tcb->iss;
}

static void tcp7_send_ack(int fd,tcp7_tcb_t*tcb){
    socket_t*s=&g_sockets[fd];
    uint8_t pkt[60];int h=net_build_tcp(pkt,s->local_port,s->remote_port,tcb->snd_nxt,tcb->rcv_nxt,TCP_ACK,tcb->rcv_wnd);
    sock_send(fd,pkt,(size_t)h,0);
    tcb->delack_pending=false;
}

static void tcp7_send_fin(int fd,tcp7_tcb_t*tcb){
    socket_t*s=&g_sockets[fd];
    uint8_t pkt[60];int h=net_build_tcp(pkt,s->local_port,s->remote_port,tcb->snd_nxt,tcb->rcv_nxt,TCP_FIN|TCP_ACK,tcb->rcv_wnd);
    sock_send(fd,pkt,(size_t)h,0);
    tcb->snd_nxt++;
}

int tcp7_connect(int sock_fd,uint32_t dst_ip,uint16_t dst_port){
    socket_t*s=&g_sockets[sock_fd];
    tcp7_tcb_t*tcb=tcp7_alloc();if(!tcb)return-ENOMEM;
    tcb->sock_fd=sock_fd;
    tcb->state=TCP7_SYN_SENT;
    tcb->iss=tcp7_isn();tcb->irs=0;
    tcb->snd_nxt=tcb->iss;tcb->snd_una=tcb->iss;
    tcb->rcv_nxt=0;tcb->rcv_wnd=65535;
    tcb->snd_wnd=65535;
    tcb->cwnd=TCP7_INITIAL_CWND*TCP7_MSS_DEFAULT;
    tcb->ssthresh=TCP7_INITIAL_SSTHRESH;
    tcb->cc_state=CC_SLOW_START;
    tcb->dup_acks=0;
    tcb->rto=TCP7_RTO_INITIAL;tcb->srtt=0;tcb->rttvar=0;
    tcb->rto_count=0;tcb->rto_max=TCP7_MAX_RETRANSMITS;
    tcb->mss=TCP7_MSS_DEFAULT;
    tcb->delack_pending=false;
    tcb->delack_expire=2;
    tcb->time_wait_expire=0;
    s->remote_ip=dst_ip;s->remote_port=dst_port;
    s->tcp_state=TCP_SYN_SENT;
    s->tcp_seq=tcb->iss;s->tcp_ack=0;
    tcp7_send_syn(sock_fd,tcb);
    return 0;
}

int tcp7_accept(int listen_fd,uint32_t*src_ip,uint16_t*src_port){
    int nsfd=sock_accept(listen_fd,src_ip,src_port);
    if(nsfd<0)return nsfd;
    tcp7_tcb_t*tcb=tcp7_alloc();if(!tcb){sock_close(nsfd);return-ENOMEM;}
    tcb->sock_fd=nsfd;
    tcb->state=TCP7_ESTABLISHED;
    tcb->iss=tcp7_isn();
    tcb->irs=g_sockets[nsfd].tcp_ack;
    tcb->snd_nxt=tcb->iss+1;tcb->snd_una=tcb->iss;
    tcb->rcv_nxt=tcb->irs+1;tcb->rcv_wnd=65535;
    tcb->snd_wnd=65535;
    tcb->cwnd=TCP7_INITIAL_CWND*TCP7_MSS_DEFAULT;
    tcb->ssthresh=TCP7_INITIAL_SSTHRESH;
    tcb->cc_state=CC_SLOW_START;tcb->dup_acks=0;
    tcb->rto=TCP7_RTO_INITIAL;tcb->mss=TCP7_MSS_DEFAULT;
    tcb->delack_expire=2;
    tcb->time_wait_expire=0;
    g_sockets[nsfd].tcp_state=TCP_ESTABLISHED;
    g_sockets[nsfd].tcp_seq=tcb->iss+1;g_sockets[nsfd].tcp_ack=tcb->irs+1;
    tcp7_send_ack(nsfd,tcb);
    return nsfd;
}

int tcp7_send(int sock_fd,const void*buf,size_t len){
    socket_t*s=&g_sockets[sock_fd];
    tcp7_tcb_t*tcb=tcp7_find(sock_fd);if(!tcb)return-ENOTCONN;
    if(tcb->state!=TCP7_ESTABLISHED&&tcb->state!=TCP7_CLOSE_WAIT)return-ENOTCONN;
    /* Congestion control: limit by min(cwnd, snd_wnd) */
    uint32_t win=tcb->cwnd;if(tcb->snd_wnd<win)win=tcb->snd_wnd;
    size_t avail=(size_t)(win>(tcb->snd_nxt-tcb->snd_una)?win-(tcb->snd_nxt-tcb->snd_una):0);
    if(len>avail)len=avail;
    if(len==0)return-EAGAIN;
    /* Send data segments */
    const uint8_t*p=buf;size_t sent=0;
    while(sent<len){
        size_t seg=len-sent;if(seg>tcb->mss)seg=tcb->mss;
        uint8_t pkt[1500];
        int h=net_build_tcp(pkt,s->local_port,s->remote_port,tcb->snd_nxt,tcb->rcv_nxt,TCP_ACK|TCP_PSH,tcb->rcv_wnd);
        ulibc_memcpy(pkt+h,p+sent,seg);
        sock_send(sock_fd,pkt,(size_t)(h+seg),0);
        tcb->snd_nxt+=(uint32_t)seg;
        sent+=seg;
    }
    /* CC: slow start or avoidance */
    if(tcb->cc_state==CC_SLOW_START){tcb->cwnd+=(uint32_t)sent;}
    else{tcb->cwnd+=(uint32_t)sent*((uint32_t)TCP7_MSS_DEFAULT)/tcb->cwnd;}
    return(int)sent;
}

int tcp7_recv(int sock_fd,void*buf,size_t len){
    tcp7_tcb_t*tcb=tcp7_find(sock_fd);if(!tcb)return-ENOTCONN;
    if(tcb->state!=TCP7_ESTABLISHED&&tcb->state!=TCP7_FIN_WAIT_1&&tcb->state!=TCP7_FIN_WAIT_2&&tcb->state!=TCP7_CLOSE_WAIT){
        if(tcb->state==TCP7_TIME_WAIT||tcb->state==TCP7_CLOSED)return 0;return-ENOTCONN;}
    /* Use existing socket rx buffer */
    int rc=sock_recv(sock_fd,buf,len,0);
    if(rc>0){
        tcb->rcv_nxt+=(uint32_t)rc;
        tcb->delack_pending=true;
        tcb->delack_expire=2;
    }
    return rc;
}

int tcp7_close(int sock_fd){
    tcp7_tcb_t*tcb=tcp7_find(sock_fd);if(!tcb)return-ENOTCONN;
    switch(tcb->state){
    case TCP7_ESTABLISHED:tcb->state=TCP7_FIN_WAIT_1;tcp7_send_fin(sock_fd,tcb);break;
    case TCP7_CLOSE_WAIT:tcb->state=TCP7_LAST_ACK;tcp7_send_fin(sock_fd,tcb);break;
    case TCP7_SYN_SENT:case TCP7_SYN_RCVD:tcb->state=TCP7_CLOSED;break;
    default:tcb->state=TCP7_CLOSED;break;
    }
    g_sockets[sock_fd].tcp_state=TCP_CLOSED;
    if(tcb->state==TCP7_CLOSED)tcp7_release_tcb(tcb);
    return 0;
}

void tcp7_incoming_segment(int sock_fd,const uint8_t*ip_pkt,size_t len){
    (void)ip_pkt;(void)len;
    tcp7_tcb_t*tcb=tcp7_find(sock_fd);if(!tcb)return;
    /* Parse TCP flags from packet */
    uint8_t flags=0;if(len>33)flags=ip_pkt[33];
    uint32_t seq=0,ack=0;
    uint16_t wnd=0;
    if(len>24){seq=((uint32_t)ip_pkt[24]<<24)|((uint32_t)ip_pkt[25]<<16)|((uint32_t)ip_pkt[26]<<8)|ip_pkt[27];}
    if(len>28){ack=((uint32_t)ip_pkt[28]<<24)|((uint32_t)ip_pkt[29]<<16)|((uint32_t)ip_pkt[30]<<8)|ip_pkt[31];}
    if(len>35){wnd=(uint16_t)(((uint16_t)ip_pkt[34]<<8)|ip_pkt[35]);}
    if(wnd)tcb->snd_wnd=wnd;

    switch(tcb->state){
    case TCP7_SYN_SENT:
        if(flags&TCP_SYN&&flags&TCP_ACK){
            tcb->irs=seq;tcb->rcv_nxt=seq+1;
            tcb->snd_una=ack;tcb->state=TCP7_ESTABLISHED;
            tcp7_rtt_update(tcb,100);/* estimate */
            tcb->cc_state=CC_SLOW_START;
            g_sockets[sock_fd].tcp_state=TCP_ESTABLISHED;
            g_sockets[sock_fd].tcp_seq=tcb->snd_nxt;g_sockets[sock_fd].tcp_ack=tcb->rcv_nxt;
            tcp7_send_ack(sock_fd,tcb);
        }
        break;
    case TCP7_SYN_RCVD:
        if(flags&TCP_ACK){tcb->snd_una=ack;tcb->state=TCP7_ESTABLISHED;g_sockets[sock_fd].tcp_state=TCP_ESTABLISHED;}
        break;
    case TCP7_ESTABLISHED:
        if(flags&TCP_FIN){tcb->rcv_nxt++;tcb->state=TCP7_CLOSE_WAIT;g_sockets[sock_fd].tcp_state=TCP_CLOSE_WAIT;tcp7_send_ack(sock_fd,tcb);}
        else if(flags&TCP_ACK){
            if(ack>tcb->snd_una){uint32_t nacked=ack-tcb->snd_una;tcb->snd_una=ack;tcb->dup_acks=0;
                /* CC: new ACK, grow window */
                if(tcb->cc_state==CC_SLOW_START){tcb->cwnd+=nacked;if(tcb->cwnd>=tcb->ssthresh)tcb->cc_state=CC_AVOIDANCE;}
                else{tcb->cwnd+=TCP7_MSS_DEFAULT*((uint32_t)TCP7_MSS_DEFAULT)/tcb->cwnd;}
                tcb->rto_count=0;
            }else if(ack==tcb->snd_una){tcb->dup_acks++;
                if(tcb->dup_acks>=3){/* fast retransmit */
                    tcb->ssthresh=tcb->cwnd/2;if(tcb->ssthresh<2*TCP7_MSS_DEFAULT)tcb->ssthresh=2*TCP7_MSS_DEFAULT;
                    tcb->cwnd=tcb->ssthresh+3*TCP7_MSS_DEFAULT;tcb->cc_state=CC_FAST_RECOVER;}
            }
        }
        break;
    case TCP7_FIN_WAIT_1:
        if(flags&TCP_ACK&&flags&TCP_FIN){tcb->rcv_nxt++;tcb->state=TCP7_TIME_WAIT;tcb->time_wait_expire=200;tcp7_send_ack(sock_fd,tcb);}
        else if(flags&TCP_ACK){tcb->state=TCP7_FIN_WAIT_2;}
        else if(flags&TCP_FIN){tcb->rcv_nxt++;tcb->state=TCP7_CLOSING;tcp7_send_ack(sock_fd,tcb);}
        break;
    case TCP7_FIN_WAIT_2:
        if(flags&TCP_FIN){tcb->rcv_nxt++;tcb->state=TCP7_TIME_WAIT;tcb->time_wait_expire=200;tcp7_send_ack(sock_fd,tcb);}
        break;
    case TCP7_CLOSING:
        if(flags&TCP_ACK){tcb->state=TCP7_TIME_WAIT;tcb->time_wait_expire=200;}
        break;
    case TCP7_LAST_ACK:
        if(flags&TCP_ACK){tcb->state=TCP7_CLOSED;g_sockets[sock_fd].tcp_state=TCP_CLOSED;tcp7_release_tcb(tcb);}
        break;
    default:break;
    }
}

void tcp7_tick(void){
    int i;for(i=0;i<TCP7_MAX_CONNECTIONS;++i){
        tcp7_tcb_t*tcb=&g_tcp7_tcbs[i];if(tcb->sock_fd<0)continue;
        /* RTO check for SYN_SENT/SYN_RCVD */
        if(tcb->state==TCP7_SYN_SENT||tcb->state==TCP7_SYN_RCVD){
            tcb->rto_count++;if(tcb->rto_count>=tcb->rto_max){tcb->state=TCP7_CLOSED;g_sockets[tcb->sock_fd].tcp_state=TCP_CLOSED;g_sockets[tcb->sock_fd].error=ETIMEDOUT;tcp7_release_tcb(tcb);continue;}
            if(tcb->state==TCP7_SYN_SENT)tcp7_send_syn(tcb->sock_fd,tcb);
            else tcp7_send_synack(tcb->sock_fd,tcb);
            tcb->rto*=2;if(tcb->rto>TCP7_RTO_MAX)tcb->rto=TCP7_RTO_MAX;
        }
        /* TIME_WAIT expiry */
        if(tcb->state==TCP7_TIME_WAIT){
            if(!tcb->time_wait_expire)tcb->time_wait_expire=200;
            else --tcb->time_wait_expire;
            if(!tcb->time_wait_expire)tcp7_release_tcb(tcb);
        }
        /* Delayed ACK */
        if(tcb->delack_pending&&tcb->state==TCP7_ESTABLISHED){
            if(!tcb->delack_expire)tcb->delack_expire=2;
            else --tcb->delack_expire;
            if(!tcb->delack_expire)tcp7_send_ack(tcb->sock_fd,tcb);
        }
    }
}

int tcp7_get_state(int sock_fd){tcp7_tcb_t*tcb=tcp7_find(sock_fd);return tcb?tcb->state:-1;}
uint32_t tcp7_get_rtt(int sock_fd){tcp7_tcb_t*tcb=tcp7_find(sock_fd);return tcb?tcb->srtt:0;}

/* TLS 1.2: record layer, handshake, key derivation, X.509 */
tls7_session_t g_tls7_sessions[TLS7_MAX_SESSIONS];
ca7_entry_t g_ca7_store[TLS7_MAX_CA];
int g_ca7_count=0;

static tls7_session_t*tls7_alloc(void){int i;for(i=0;i<TLS7_MAX_SESSIONS;++i)if(!g_tls7_sessions[i].used){ulibc_memset(&g_tls7_sessions[i],0,sizeof(tls7_session_t));g_tls7_sessions[i].used=true;return&g_tls7_sessions[i];}return 0;}
static tls7_session_t*tls7_find(int fd){int i;for(i=0;i<TLS7_MAX_SESSIONS;++i)if(g_tls7_sessions[i].used&&g_tls7_sessions[i].sock_fd==fd)return&g_tls7_sessions[i];return 0;}

/* TLS record header: 1 byte type, 2 bytes version, 2 bytes length */
static int tls7_send_record(tls7_session_t*s,uint8_t type,const uint8_t*payload,uint16_t len){
    uint8_t hdr[5];hdr[0]=type;hdr[1]=0x03;hdr[2]=0x03;hdr[3]=(uint8_t)(len>>8);hdr[4]=(uint8_t)len;
    sock_send(s->sock_fd,hdr,5,0);
    sock_send(s->sock_fd,payload,len,0);
    return 0;
}

/* TLS record read: returns record type, payload in s->recv_buf */
static int tls7_recv_record(tls7_session_t*s){
    uint8_t hdr[5];int rc=sock_recv(s->sock_fd,hdr,5,0);
    if(rc<5)return-1;
    uint16_t len=((uint16_t)hdr[3]<<8)|hdr[4];
    if(len>TLS7_MAX_FRAG)return-1;
    rc=sock_recv(s->sock_fd,s->recv_buf,len,0);
    if(rc<0)return-1;
    s->recv_len=(uint32_t)rc;
    return hdr[0];
}

/* Build ClientHello */
static int tls7_build_client_hello(tls7_session_t*s,uint8_t*out,size_t outmax){
    size_t pos=0;
    #define TLS7_NEED(_n) do{if(pos+(_n)>outmax)return-1;}while(0)
    /* Handshake header: 1 type + 3 length */
    TLS7_NEED(4);
    out[pos++]=TLS7_HS_CLIENT_HELLO;
    /* length placeholder */
    uint8_t*lenp=out+pos;pos+=3;
    /* ClientVersion */
    TLS7_NEED(2);
    out[pos++]=0x03;out[pos++]=0x03;
    /* Random (32 bytes) */
    TLS7_NEED(32);
    ulibc_arc4random_buf(s->client_random,32);
    ulibc_memcpy(out+pos,s->client_random,32);pos+=32;
    /* SessionID length=0 */
    TLS7_NEED(1);
    out[pos++]=0;
    /* CipherSuites: RSA AES-CBC variants (ECDHE not implemented yet) */
    TLS7_NEED(8);
    out[pos++]=0;out[pos++]=4; /* length */
    out[pos++]=0x00;out[pos++]=0x3C; /* RSA_WITH_AES_128_CBC_SHA256 */
    out[pos++]=0x00;out[pos++]=0x3D; /* RSA_WITH_AES_256_CBC_SHA256 */
    /* CompressionMethods: null */
    out[pos++]=1;out[pos++]=0;
    /* Extensions length */
    TLS7_NEED(2);
    uint8_t*ext_len_p=out+pos;pos+=2;
    size_t ext_start=pos;
    /* SNI */
    if(s->sni_hostname[0]){
        size_t hlen=ulibc_strlen(s->sni_hostname);
        TLS7_NEED(hlen+9);
        out[pos++]=0x00;out[pos++]=0x00; /* SNI */
        uint16_t ext_len=(uint16_t)(hlen+5);out[pos++]=(uint8_t)(ext_len>>8);out[pos++]=(uint8_t)ext_len;
        uint16_t list_len=(uint16_t)(hlen+3);out[pos++]=(uint8_t)(list_len>>8);out[pos++]=(uint8_t)list_len;
        out[pos++]=0x00; /* host name type */
        out[pos++]=(uint8_t)(hlen>>8);out[pos++]=(uint8_t)hlen;
        ulibc_memcpy(out+pos,s->sni_hostname,hlen);pos+=hlen;
    }
    /* ALPN */
    if(s->alpn_protocol[0]){
        size_t plen=ulibc_strlen(s->alpn_protocol);
        TLS7_NEED(plen+8);
        out[pos++]=0x00;out[pos++]=0x10; /* ALPN */
        uint16_t ext_len=(uint16_t)(plen+3);out[pos++]=(uint8_t)(ext_len>>8);out[pos++]=(uint8_t)ext_len;
        uint16_t list_len=(uint16_t)(plen+1);out[pos++]=(uint8_t)(list_len>>8);out[pos++]=(uint8_t)list_len;
        out[pos++]=(uint8_t)plen;ulibc_memcpy(out+pos,s->alpn_protocol,plen);pos+=plen;
    }
    /* Signature algorithms */
    TLS7_NEED(12);
    out[pos++]=0x00;out[pos++]=0x0D; /* sig_algs */
    uint16_t sig_len=8;out[pos++]=(uint8_t)(sig_len>>8);out[pos++]=(uint8_t)sig_len;
    {
        uint16_t list_len=(uint16_t)(sig_len-2);
        out[pos++]=(uint8_t)(list_len>>8);
        out[pos++]=(uint8_t)list_len;
    }
    out[pos++]=0x04;out[pos++]=0x01; /* SHA256+RSA */
    out[pos++]=0x05;out[pos++]=0x01; /* SHA384+RSA */
    /* Fill extension total length */
    uint16_t etot=(uint16_t)(pos-ext_start);ext_len_p[0]=(uint8_t)(etot>>8);ext_len_p[1]=(uint8_t)etot;
    /* Fill handshake length */
    uint32_t hlen3=(uint32_t)(pos-4); /* bytes after handshake header */
    lenp[0]=(uint8_t)(hlen3>>16);lenp[1]=(uint8_t)(hlen3>>8);lenp[2]=(uint8_t)hlen3;
    #undef TLS7_NEED
    return(int)pos;
}

/* Derive key material from master secret */
static void tls7_derive_keys(tls7_session_t*s){
    uint8_t seed[64];ulibc_memcpy(seed,s->client_random,32);ulibc_memcpy(seed+32,s->server_random,32);
    /* key_block = PRF(master_secret, "key expansion", seed) */
    uint8_t key_block[160];
    tls7_prf(s->master_secret,48,"key expansion",seed,64,key_block,sizeof(key_block));
    /* Split key_block: client_write_MAC_key[32], server_write_MAC_key[32],
       client_write_key[32], server_write_key[32], client_write_IV[16], server_write_IV[16] */
    ulibc_memcpy(s->client_write_mac_key,key_block,32);
    ulibc_memcpy(s->server_write_mac_key,key_block+32,32);
    ulibc_memcpy(s->client_write_key,key_block+64,32);
    ulibc_memcpy(s->server_write_key,key_block+96,32);
    ulibc_memcpy(s->client_write_iv,key_block+128,16);
    ulibc_memcpy(s->server_write_iv,key_block+144,16);
    /* Setup AES contexts */
    aes7_key_setup(&s->client_enc,s->client_write_key,128);
    aes7_key_setup(&s->client_dec,s->server_write_key,128);
    aes7_key_setup(&s->server_enc,s->server_write_key,128);
    aes7_key_setup(&s->server_dec,s->client_write_key,128);
}

/* TLS encrypt+send application data */
static int tls7_send_appdata(tls7_session_t*s,const void*buf,size_t len){
    const uint8_t*data=buf;
    while(len>0){
        size_t frag=len;if(frag>TLS7_MAX_FRAG)frag=TLS7_MAX_FRAG;
        /* CBC: pad to 16-byte boundary + 1 (padding length byte) */
        size_t padded=frag+1; /* +1 for MAC placeholder simplified */
        size_t iv_len=16;
        size_t enc_len=((padded+iv_len+15)/16)*16;
        uint8_t*rec=(uint8_t*)ulibc_malloc(enc_len+16+32);
        if(!rec)return-ENOMEM;
        ulibc_arc4random_buf(rec,iv_len); /* IV */
        ulibc_memcpy(rec+iv_len,data,frag);
        /* Simplified: no real MAC for now, just encrypt the payload */
        size_t total=iv_len+frag;
        /* Pad to 16-byte boundary */
        size_t pad=16-(total%16);if(pad==0)pad=16;
        ulibc_memset(rec+total,(uint8_t)(pad-1),pad);
        total+=pad;
        aes7_cbc_encrypt(&s->client_enc,rec,rec+iv_len,rec+iv_len,total-iv_len);
        tls7_send_record(s,TLS7_APPLICATION_DATA,rec,(uint16_t)total);
        ulibc_free(rec);
        data+=frag;len-=frag;
        s->client_seq++;
    }
    return 0;
}

static bool tls7_field_equals(const uint8_t *a,size_t alen,const uint8_t *b,size_t blen){
    char sa[128];
    char sb[128];
    size_t i;
    if(!a||!b)return false;
    if(alen>=sizeof(sa))alen=sizeof(sa)-1;
    if(blen>=sizeof(sb))blen=sizeof(sb)-1;
    for(i=0;i<alen;++i)sa[i]=(char)a[i];
    sa[alen]=0;
    for(i=0;i<blen;++i)sb[i]=(char)b[i];
    sb[blen]=0;
    return c7_strcasecmp(sa,sb)==0;
}

static bool tls7_hostname_match(const x509_cert_t *cert,const char *hostname){
    const uint8_t *subj;
    size_t slen;
    char subj_s[128];
    char host_s[128];
    const char *dot;
    size_t i;
    if(!cert||!hostname||!hostname[0])return false;
    subj=cert->subject;
    slen=c7_strnlen_u8(subj,sizeof(cert->subject));
    if(!slen)return false;
    if(slen>=sizeof(subj_s))slen=sizeof(subj_s)-1;
    for(i=0;i<slen;++i)subj_s[i]=(char)subj[i];
    subj_s[slen]=0;
    for(i=0;i+1<sizeof(host_s)&&hostname[i];++i)host_s[i]=hostname[i];
    host_s[i]=0;
    if(c7_strcasecmp(subj_s,host_s)==0)return true;
    if(subj_s[0]=='*'&&subj_s[1]=='.'){
        dot=host_s;
        while(*dot&&*dot!='.')++dot;
        if(*dot=='.'&&dot[1]&&c7_strcasecmp(subj_s+1,dot)==0)return true;
    }
    return false;
}

typedef struct {
    size_t n;
    uint8_t v[TLS7_RSA_MAX_BYTES];
} c7_bn_t;

static bool c7_asn1_read_len(const uint8_t *p,size_t left,size_t *len,size_t *len_len){
    uint8_t b;
    size_t n;
    size_t v=0;
    size_t i;
    if(!p||left<1||!len||!len_len)return false;
    b=p[0];
    if((b&0x80)==0){*len=b;*len_len=1;return true;}
    n=(size_t)(b&0x7F);
    if(n==0||n>4||left<1+n)return false;
    for(i=0;i<n;++i)v=(v<<8)|p[1+i];
    *len=v;
    *len_len=1+n;
    return true;
}

static bool c7_asn1_read_tlv(const uint8_t *p,size_t left,uint8_t tag,const uint8_t **val,size_t *vlen,size_t *consumed){
    size_t len,len_len,total;
    if(!p||left<2||p[0]!=tag||!val||!vlen||!consumed)return false;
    if(!c7_asn1_read_len(p+1,left-1,&len,&len_len))return false;
    total=1+len_len+len;
    if(total>left)return false;
    *val=p+1+len_len;
    *vlen=len;
    *consumed=total;
    return true;
}

static int c7_find_oid(const uint8_t *p,size_t len,const uint8_t *oid,size_t oid_len){
    size_t i,j;
    if(!p||!oid||oid_len==0||len<oid_len)return-1;
    for(i=0;i+oid_len<=len;++i){
        int ok=1;
        for(j=0;j<oid_len;++j)if(p[i+j]!=oid[j]){ok=0;break;}
        if(ok)return(int)i;
    }
    return-1;
}

static void c7_bn_zero(c7_bn_t *a){if(!a)return;ulibc_memset(a,0,sizeof(*a));}

static void c7_bn_trim(c7_bn_t *a){
    if(!a)return;
    while(a->n>0&&a->v[a->n-1]==0)--a->n;
}

static bool c7_bn_from_be(c7_bn_t *a,const uint8_t *be,size_t len){
    size_t i;
    if(!a||!be||len==0||len>TLS7_RSA_MAX_BYTES)return false;
    c7_bn_zero(a);
    for(i=0;i<len;++i)a->v[i]=be[len-1-i];
    a->n=len;
    c7_bn_trim(a);
    return true;
}

static bool c7_bn_to_be_fixed(const c7_bn_t *a,uint8_t *out,size_t out_len){
    size_t i;
    if(!a||!out||out_len==0)return false;
    ulibc_memset(out,0,out_len);
    if(a->n>out_len)return false;
    for(i=0;i<a->n;++i)out[out_len-1-i]=a->v[i];
    return true;
}

static int c7_bn_cmp(const c7_bn_t *a,const c7_bn_t *b){
    size_t i;
    if(a->n>b->n)return 1;
    if(a->n<b->n)return-1;
    for(i=a->n;i>0;--i){
        uint8_t av=a->v[i-1],bv=b->v[i-1];
        if(av>bv)return 1;
        if(av<bv)return-1;
    }
    return 0;
}

static void c7_bn_sub_inplace(c7_bn_t *a,const c7_bn_t *b){
    size_t i;
    int borrow=0;
    for(i=0;i<a->n;++i){
        int av=a->v[i];
        int bv=(i<b->n)?b->v[i]:0;
        int t=av-bv-borrow;
        if(t<0){t+=256;borrow=1;}else borrow=0;
        a->v[i]=(uint8_t)t;
    }
    c7_bn_trim(a);
}

static bool c7_bn_add_mod(c7_bn_t *out,const c7_bn_t *a,const c7_bn_t *b,const c7_bn_t *m){
    size_t i,n;
    uint16_t carry=0;
    c7_bn_t t;
    if(!out||!a||!b||!m)return false;
    c7_bn_zero(&t);
    n=(a->n>b->n)?a->n:b->n;
    if(n+1>TLS7_RSA_MAX_BYTES)return false;
    for(i=0;i<n;++i){
        uint16_t av=(i<a->n)?a->v[i]:0;
        uint16_t bv=(i<b->n)?b->v[i]:0;
        uint16_t s=av+bv+carry;
        t.v[i]=(uint8_t)(s&0xFFu);
        carry=(uint16_t)(s>>8);
    }
    t.n=n;
    if(carry){
        if(t.n>=TLS7_RSA_MAX_BYTES)return false;
        t.v[t.n++]=(uint8_t)carry;
    }
    while(c7_bn_cmp(&t,m)>=0)c7_bn_sub_inplace(&t,m);
    *out=t;
    return true;
}

static bool c7_bn_is_zero(const c7_bn_t *a){return !a||a->n==0;}
static bool c7_bn_is_odd(const c7_bn_t *a){return a&&a->n>0&&((a->v[0]&1u)!=0);}

static void c7_bn_shr1(c7_bn_t *a){
    size_t i;
    uint8_t carry=0;
    if(!a||a->n==0)return;
    for(i=a->n;i>0;--i){
        uint8_t cur=a->v[i-1];
        a->v[i-1]=(uint8_t)((cur>>1)|(carry<<7));
        carry=(uint8_t)(cur&1u);
    }
    c7_bn_trim(a);
}

static bool c7_bn_mod_mul(c7_bn_t *out,const c7_bn_t *a,const c7_bn_t *b,const c7_bn_t *m){
    c7_bn_t x,y,res,tmp;
    if(!out||!a||!b||!m||c7_bn_is_zero(m))return false;
    x=*a;
    while(c7_bn_cmp(&x,m)>=0)c7_bn_sub_inplace(&x,m);
    y=*b;
    c7_bn_zero(&res);
    while(!c7_bn_is_zero(&y)){
        if(c7_bn_is_odd(&y)){
            if(!c7_bn_add_mod(&tmp,&res,&x,m))return false;
            res=tmp;
        }
        c7_bn_shr1(&y);
        if(!c7_bn_add_mod(&tmp,&x,&x,m))return false;
        x=tmp;
    }
    *out=res;
    return true;
}

static bool c7_bn_mod_exp(c7_bn_t *out,const c7_bn_t *base,const c7_bn_t *exp,const c7_bn_t *mod){
    c7_bn_t b,e,res,tmp;
    if(!out||!base||!exp||!mod||c7_bn_is_zero(mod))return false;
    b=*base;
    while(c7_bn_cmp(&b,mod)>=0)c7_bn_sub_inplace(&b,mod);
    e=*exp;
    c7_bn_zero(&res);
    res.n=1;
    res.v[0]=1;
    while(!c7_bn_is_zero(&e)){
        if(c7_bn_is_odd(&e)){
            if(!c7_bn_mod_mul(&tmp,&res,&b,mod))return false;
            res=tmp;
        }
        c7_bn_shr1(&e);
        if(!c7_bn_is_zero(&e)){
            if(!c7_bn_mod_mul(&tmp,&b,&b,mod))return false;
            b=tmp;
        }
    }
    *out=res;
    return true;
}

static bool c7_pubkey_unpack_rsa(const uint8_t *blob,uint32_t blob_len,const uint8_t **mod,size_t *mod_len,const uint8_t **exp,size_t *exp_len){
    size_t ml,el;
    if(!blob||blob_len<6||!mod||!mod_len||!exp||!exp_len)return false;
    ml=((size_t)blob[0]<<8)|blob[1];
    el=((size_t)blob[2]<<8)|blob[3];
    if(ml==0||el==0||ml>TLS7_RSA_MAX_BYTES||el>8)return false;
    if(4+ml+el>blob_len)return false;
    *mod=blob+4;
    *mod_len=ml;
    *exp=blob+4+ml;
    *exp_len=el;
    return true;
}

static bool c7_rsa_public_op(const uint8_t *in,size_t in_len,const uint8_t *key_blob,uint32_t key_blob_len,uint8_t *out,size_t out_len){
    const uint8_t *mod_be,*exp_be;
    size_t mod_len,exp_len;
    c7_bn_t n,e,m,c;
    if(!in||!key_blob||!out)return false;
    if(!c7_pubkey_unpack_rsa(key_blob,key_blob_len,&mod_be,&mod_len,&exp_be,&exp_len))return false;
    if(out_len<mod_len)return false;
    if(in_len>mod_len)return false;
    {
        uint8_t padded[TLS7_RSA_MAX_BYTES];
        size_t pad=mod_len-in_len;
        ulibc_memset(padded,0,sizeof(padded));
        if(pad)ulibc_memset(padded,0,pad);
        ulibc_memcpy(padded+pad,in,in_len);
        if(!c7_bn_from_be(&m,padded,mod_len))return false;
    }
    if(!c7_bn_from_be(&n,mod_be,mod_len))return false;
    if(!c7_bn_from_be(&e,exp_be,exp_len))return false;
    if(c7_bn_cmp(&m,&n)>=0)return false;
    if(!c7_bn_mod_exp(&c,&m,&e,&n))return false;
    return c7_bn_to_be_fixed(&c,out,mod_len);
}

static bool c7_rsa_pkcs1_encrypt_pms(const x509_cert_t *cert,const uint8_t pms[48],uint8_t *out,size_t *out_len){
    const uint8_t *mod_be,*exp_be;
    size_t mod_len,exp_len,ps_len;
    uint8_t em[TLS7_RSA_MAX_BYTES];
    size_t i;
    c7_bn_t n,e,m,c;
    if(!cert||!pms||!out||!out_len)return false;
    if(cert->pubkey_type!=0)return false;
    if(!c7_pubkey_unpack_rsa(cert->pubkey_data,cert->pubkey_len,&mod_be,&mod_len,&exp_be,&exp_len))return false;
    if(mod_len<64||mod_len>TLS7_RSA_MAX_BYTES)return false;
    if(mod_len<48+11)return false;
    ps_len=mod_len-48-3;
    em[0]=0x00;
    em[1]=0x02;
    for(i=0;i<ps_len;++i){
        uint8_t r=0;
        while(r==0)r=(uint8_t)(ulibc_arc4random()&0xFFu);
        em[2+i]=r;
    }
    em[2+ps_len]=0x00;
    ulibc_memcpy(em+3+ps_len,pms,48);
    if(!c7_bn_from_be(&m,em,mod_len))return false;
    if(!c7_bn_from_be(&n,mod_be,mod_len))return false;
    if(!c7_bn_from_be(&e,exp_be,exp_len))return false;
    if(c7_bn_cmp(&m,&n)>=0)return false;
    if(!c7_bn_mod_exp(&c,&m,&e,&n))return false;
    if(!c7_bn_to_be_fixed(&c,out,mod_len))return false;
    *out_len=mod_len;
    return true;
}

static bool c7_pkcs1_verify_sha256(const uint8_t *sig,size_t sig_len,const uint8_t hash[32],const uint8_t *issuer_key,uint32_t issuer_key_len){
    static const uint8_t di_prefix[]={
        0x30,0x31,0x30,0x0D,0x06,0x09,0x60,0x86,0x48,0x01,0x65,0x03,0x04,0x02,0x01,0x05,0x00,0x04,0x20
    };
    uint8_t em[TLS7_RSA_MAX_BYTES];
    const uint8_t *mod_be,*exp_be;
    size_t mod_len,exp_len,pos;
    (void)exp_be;(void)exp_len;
    if(!sig||!hash||!issuer_key)return false;
    if(!c7_pubkey_unpack_rsa(issuer_key,issuer_key_len,&mod_be,&mod_len,&exp_be,&exp_len))return false;
    if(mod_len>TLS7_RSA_MAX_BYTES)return false;
    if(sig_len>mod_len)return false;
    if(!c7_rsa_public_op(sig,sig_len,issuer_key,issuer_key_len,em,sizeof(em)))return false;
    if(em[0]!=0x00||em[1]!=0x01)return false;
    pos=2;
    while(pos<mod_len&&em[pos]==0xFF)++pos;
    if(pos<10||pos>=mod_len||em[pos]!=0x00)return false;
    ++pos;
    if(pos+sizeof(di_prefix)+32>mod_len)return false;
    if(ulibc_memcmp(em+pos,di_prefix,sizeof(di_prefix))!=0)return false;
    pos+=sizeof(di_prefix);
    return ulibc_memcmp(em+pos,hash,32)==0;
}

static bool c7_extract_rsa_pubkey_blob(const uint8_t *der,size_t len,uint8_t *out,uint32_t *out_len){
    static const uint8_t rsa_oid[]={0x2A,0x86,0x48,0x86,0xF7,0x0D,0x01,0x01,0x01};
    int oid_pos=c7_find_oid(der,len,rsa_oid,sizeof(rsa_oid));
    size_t i;
    if(oid_pos<0||!out||!out_len)return false;
    for(i=(size_t)oid_pos+sizeof(rsa_oid);i+4<len;++i){
        const uint8_t *bit_v,*rsa_v,*mod_v,*exp_v,*p;
        size_t bit_l,rsa_l,mod_l,exp_l,c,rc,mc,ec;
        uint16_t ml,el;
        if(der[i]!=0x03)continue;
        if(!c7_asn1_read_tlv(der+i,len-i,0x03,&bit_v,&bit_l,&c))continue;
        if(bit_l<2||bit_v[0]!=0x00)continue;
        p=bit_v+1;
        if(!c7_asn1_read_tlv(p,bit_l-1,0x30,&rsa_v,&rsa_l,&rc))continue;
        if(!c7_asn1_read_tlv(rsa_v,rsa_l,0x02,&mod_v,&mod_l,&mc))continue;
        if(!c7_asn1_read_tlv(rsa_v+mc,rsa_l-mc,0x02,&exp_v,&exp_l,&ec))continue;
        while(mod_l>1&&mod_v[0]==0){++mod_v;--mod_l;}
        while(exp_l>1&&exp_v[0]==0){++exp_v;--exp_l;}
        if(mod_l==0||mod_l>TLS7_RSA_MAX_BYTES||exp_l==0||exp_l>8)continue;
        if(4+mod_l+exp_l>512)continue;
        ml=(uint16_t)mod_l;
        el=(uint16_t)exp_l;
        out[0]=(uint8_t)(ml>>8);out[1]=(uint8_t)ml;
        out[2]=(uint8_t)(el>>8);out[3]=(uint8_t)el;
        ulibc_memcpy(out+4,mod_v,mod_l);
        ulibc_memcpy(out+4+mod_l,exp_v,exp_l);
        *out_len=(uint32_t)(4+mod_l+exp_l);
        return true;
    }
    return false;
}

/* X.509 DER parser (minimal but cryptographic): extracts tbs hash, RSA key and signature */
int x509_parse_der(const uint8_t*der,size_t len,x509_cert_t*out){
    static const uint8_t sig_oid_sha256_rsa[]={0x2A,0x86,0x48,0x86,0xF7,0x0D,0x01,0x01,0x0B};
    const uint8_t *cert_v,*tbs_v,*sigalg_v,*sigbit_v;
    size_t cert_l,tbs_l,sigalg_l,sigbit_l;
    size_t cert_c,tbs_c,sigalg_c,sigbit_c;
    size_t i;
    int cn_seen=0;
    if(!der||len<16||!out)return-1;
    ulibc_memset(out,0,sizeof(*out));
    if(!c7_asn1_read_tlv(der,len,0x30,&cert_v,&cert_l,&cert_c))return-1;
    if(!c7_asn1_read_tlv(cert_v,cert_l,0x30,&tbs_v,&tbs_l,&tbs_c))return-1;
    sha256(cert_v,tbs_c,out->tbs_hash);
    if(!c7_asn1_read_tlv(cert_v+tbs_c,cert_l-tbs_c,0x30,&sigalg_v,&sigalg_l,&sigalg_c))return-1;
    if(!c7_asn1_read_tlv(cert_v+tbs_c+sigalg_c,cert_l-tbs_c-sigalg_c,0x03,&sigbit_v,&sigbit_l,&sigbit_c))return-1;
    (void)cert_c;
    (void)sigbit_c;
    if(sigbit_l<1||sigbit_v[0]!=0x00)return-1;
    out->sig_len=(uint32_t)(sigbit_l-1);
    if(out->sig_len>sizeof(out->sig_data))out->sig_len=sizeof(out->sig_data);
    if(out->sig_len)ulibc_memcpy(out->sig_data,sigbit_v+1,out->sig_len);
    if(c7_find_oid(sigalg_v,sigalg_l,sig_oid_sha256_rsa,sizeof(sig_oid_sha256_rsa))>=0)out->sig_type=1;
    for(i=0;i+5<tbs_l;++i){
        if(tbs_v[i]!=0x55||tbs_v[i+1]!=0x04||tbs_v[i+2]!=0x03)continue;
        i+=3;
        if(i+1>=tbs_l)break;
        if(tbs_v[i]!=0x0C&&tbs_v[i]!=0x13&&tbs_v[i]!=0x16)continue;
        ++i;
        {
            uint8_t slen=tbs_v[i++];
            if(i+slen>tbs_l)break;
            if(slen>127)slen=127;
            if(cn_seen==0)ulibc_memcpy(out->issuer,tbs_v+i,slen);
            else if(cn_seen==1)ulibc_memcpy(out->subject,tbs_v+i,slen);
            ++cn_seen;
            if(cn_seen>=2)break;
        }
    }
    if(!out->subject[0]&&out->issuer[0])ulibc_memcpy(out->subject,out->issuer,sizeof(out->subject));
    if(!c7_extract_rsa_pubkey_blob(tbs_v,tbs_l,out->pubkey_data,&out->pubkey_len))return-1;
    out->pubkey_type=0;
    out->valid=(out->sig_type==1);
    return out->valid?0:-1;
}

int ca7_load(const uint8_t*der,size_t len){
    x509_cert_t cert;
    ca7_entry_t*e;
    int rc;
    if(g_ca7_count>=TLS7_MAX_CA)return-1;
    rc=x509_parse_der(der,len,&cert);
    if(rc<0||!cert.valid||cert.pubkey_type!=0||cert.pubkey_len==0)return-1;
    e=&g_ca7_store[g_ca7_count];
    ulibc_memset(e,0,sizeof(*e));
    ulibc_memcpy(e->subject,cert.subject,sizeof(e->subject));
    ulibc_memcpy(e->pubkey_data,cert.pubkey_data,sizeof(e->pubkey_data));
    e->pubkey_len=cert.pubkey_len;
    e->pubkey_type=cert.pubkey_type;
    e->trusted=true;
    ++g_ca7_count;
    return 0;
}

static bool c7_verify_cert_signature_with_key(const x509_cert_t *cert,const uint8_t *issuer_key,uint32_t issuer_key_len){
    if(!cert||!issuer_key||cert->sig_type!=1||cert->sig_len==0)return false;
    return c7_pkcs1_verify_sha256(cert->sig_data,cert->sig_len,cert->tbs_hash,issuer_key,issuer_key_len);
}

bool ca7_verify_cert_chain(const x509_cert_t*chain,int count){
    int i;
    size_t chain_subject_len,chain_issuer_len;
    if(!chain||count<=0||g_ca7_count<=0)return false;
    for(i=0;i<count;++i){
        if(!chain[i].valid||chain[i].pubkey_type!=0||chain[i].pubkey_len==0)return false;
    }
    for(i=0;i<count-1;++i){
        size_t ilen=c7_strnlen_u8(chain[i].issuer,sizeof(chain[i].issuer));
        size_t slen=c7_strnlen_u8(chain[i+1].subject,sizeof(chain[i+1].subject));
        if(!ilen||!slen)return false;
        if(!tls7_field_equals(chain[i].issuer,ilen,chain[i+1].subject,slen))return false;
        if(!c7_verify_cert_signature_with_key(&chain[i],chain[i+1].pubkey_data,chain[i+1].pubkey_len))return false;
    }
    chain_subject_len=c7_strnlen_u8(chain[count-1].subject,sizeof(chain[count-1].subject));
    chain_issuer_len=c7_strnlen_u8(chain[count-1].issuer,sizeof(chain[count-1].issuer));
    for(i=0;i<g_ca7_count;++i){
        size_t ca_len;
        if(!g_ca7_store[i].trusted||g_ca7_store[i].pubkey_type!=0||g_ca7_store[i].pubkey_len==0)continue;
        ca_len=c7_strnlen_u8(g_ca7_store[i].subject,sizeof(g_ca7_store[i].subject));
        if(!ca_len)continue;
        if(chain_issuer_len&&tls7_field_equals(chain[count-1].issuer,chain_issuer_len,g_ca7_store[i].subject,ca_len)){
            if(c7_verify_cert_signature_with_key(&chain[count-1],g_ca7_store[i].pubkey_data,g_ca7_store[i].pubkey_len))return true;
        }
        if(chain_subject_len&&tls7_field_equals(chain[count-1].subject,chain_subject_len,g_ca7_store[i].subject,ca_len)){
            if(c7_verify_cert_signature_with_key(&chain[count-1],g_ca7_store[i].pubkey_data,g_ca7_store[i].pubkey_len))return true;
        }
    }
    return false;
}

/* TLS connect: send ClientHello, process ServerHello/Cert/Finished */
int tls7_connect(int sock_fd,const char*hostname,const char*alpn){
    tls7_session_t*s=tls7_alloc();if(!s)return-ENOMEM;
    s->sock_fd=sock_fd;s->version=TLS7_VERSION_12;
    s->cipher_suite=TLS7_RSA_WITH_AES_128_CBC_SHA256;
    s->verify_mode=(g_ca7_count>0)?TLS7_VERIFY_STRICT:TLS7_VERIFY_HOSTNAME;
    s->cert_chain_ok=false;
    s->hostname_ok=(hostname==0||hostname[0]==0);
    if(hostname){size_t hl=ulibc_strlen(hostname);if(hl>127)hl=127;ulibc_memcpy(s->sni_hostname,hostname,hl);}
    if(alpn){size_t al=ulibc_strlen(alpn);if(al>31)al=31;ulibc_memcpy(s->alpn_protocol,alpn,al);}
    sha256_init(&s->handshake_hash);

    /* Build and send ClientHello */
    uint8_t hello[1024];int hlen=tls7_build_client_hello(s,hello,sizeof(hello));
    if(hlen<0){s->used=false;return-1;}
    sha256_update(&s->handshake_hash,hello,(size_t)hlen);
    tls7_send_record(s,TLS7_HANDSHAKE,hello,(uint16_t)hlen);
    s->state=TLS7_CLIENT_HELLO_SENT;

    /* Process server response: simplified - expect ServerHello, Certificate, ServerHelloDone */
    int rec_type=tls7_recv_record(s);
    if(rec_type!=TLS7_HANDSHAKE){s->used=false;return-1;}
    /* Parse ServerHello */
    if(s->recv_len>6&&s->recv_buf[0]==TLS7_HS_SERVER_HELLO){
        sha256_update(&s->handshake_hash,s->recv_buf,s->recv_len);
        uint16_t srv_ver=((uint16_t)s->recv_buf[4]<<8)|s->recv_buf[5];
        if(srv_ver!=TLS7_VERSION_12){s->used=false;return-1;}
        ulibc_memcpy(s->server_random,s->recv_buf+6,32);
        s->cipher_suite=((uint16_t)s->recv_buf[38]<<8)|s->recv_buf[39];
        if(s->cipher_suite!=TLS7_RSA_WITH_AES_128_CBC_SHA256&&s->cipher_suite!=TLS7_RSA_WITH_AES_256_CBC_SHA256){
            s->used=false;
            return -EINVAL;
        }
        s->state=TLS7_SERVER_HELLO_RECV;
    }

    /* Certificate */
    rec_type=tls7_recv_record(s);
    if(rec_type==TLS7_HANDSHAKE&&s->recv_len>4&&s->recv_buf[0]==TLS7_HS_CERTIFICATE){
        sha256_update(&s->handshake_hash,s->recv_buf,s->recv_len);
        /* Parse certificate list. */
        if(s->recv_len>=7){
            size_t hs_len=((size_t)s->recv_buf[1]<<16)|((size_t)s->recv_buf[2]<<8)|(size_t)s->recv_buf[3];
            size_t hs_end=4+hs_len;
            size_t list_len=((size_t)s->recv_buf[4]<<16)|((size_t)s->recv_buf[5]<<8)|(size_t)s->recv_buf[6];
            size_t pos=7;
            size_t list_end;
            int cert_idx=0;
            if(hs_end>s->recv_len)hs_end=s->recv_len;
            list_end=pos+list_len;
            if(list_end>hs_end)list_end=hs_end;
            while(pos+3<=list_end&&cert_idx<TLS7_MAX_CERTS){
                size_t cert_len=((size_t)s->recv_buf[pos]<<16)|((size_t)s->recv_buf[pos+1]<<8)|(size_t)s->recv_buf[pos+2];
                pos+=3;
                if(!cert_len||pos+cert_len>list_end)break;
                if(x509_parse_der(s->recv_buf+pos,cert_len,&s->certs[cert_idx])==0)++cert_idx;
                pos+=cert_len;
            }
            s->cert_count=cert_idx;
        }
        if(s->cert_count>0){
            s->cert_chain_ok=ca7_verify_cert_chain(s->certs,s->cert_count);
            s->hostname_ok=tls7_hostname_match(&s->certs[0],hostname);
        }
        if((s->verify_mode&TLS7_VERIFY_CHAIN)&&!s->cert_chain_ok){s->used=false;return -EACCES;}
        if((s->verify_mode&TLS7_VERIFY_HOSTNAME)&&!s->hostname_ok){s->used=false;return -EACCES;}
        s->state=TLS7_CERTIFICATE_RECV;
    }

    /* ServerHelloDone */
    rec_type=tls7_recv_record(s);
    if(rec_type==TLS7_HANDSHAKE&&s->recv_len>=4&&s->recv_buf[0]==TLS7_HS_SERVER_HELLO_DONE){
        sha256_update(&s->handshake_hash,s->recv_buf,s->recv_len);
        s->state=TLS7_HELLO_DONE_RECV;
    }

    /* ClientKeyExchange: RSA PKCS#1 v1.5 encrypted pre-master secret */
    ulibc_arc4random_buf(s->pre_master_secret,48);
    s->pre_master_secret[0]=0x03;s->pre_master_secret[1]=0x03;/* TLS 1.2 */
    if(s->cert_count<=0||s->certs[0].pubkey_type!=0){s->used=false;return-EINVAL;}
    {
        uint8_t enc[TLS7_RSA_MAX_BYTES];
        size_t enc_len=0;
        uint8_t *kex;
        size_t msg_len;
        uint32_t hs_len;
        if(!c7_rsa_pkcs1_encrypt_pms(&s->certs[0],s->pre_master_secret,enc,&enc_len)){s->used=false;return-EINVAL;}
        msg_len=6+enc_len;
        if(msg_len>0xFFFFu){s->used=false;return-EINVAL;}
        kex=(uint8_t*)ulibc_malloc(msg_len);
        if(!kex){s->used=false;return-ENOMEM;}
        kex[0]=TLS7_HS_CLIENT_KEY_EXCH;
        hs_len=(uint32_t)(2+enc_len);
        kex[1]=(uint8_t)(hs_len>>16);
        kex[2]=(uint8_t)(hs_len>>8);
        kex[3]=(uint8_t)hs_len;
        kex[4]=(uint8_t)(enc_len>>8);
        kex[5]=(uint8_t)enc_len;
        ulibc_memcpy(kex+6,enc,enc_len);
        sha256_update(&s->handshake_hash,kex,msg_len);
        tls7_send_record(s,TLS7_HANDSHAKE,kex,(uint16_t)msg_len);
        ulibc_free(kex);
    }
    s->state=TLS7_CLIENT_KEY_EXCH_SENT;

    /* Derive master secret */
    uint8_t seed[64];ulibc_memcpy(seed,s->client_random,32);ulibc_memcpy(seed+32,s->server_random,32);
    tls7_prf(s->pre_master_secret,48,"master secret",seed,64,s->master_secret,48);
    tls7_derive_keys(s);

    /* ChangeCipherSpec */
    uint8_t ccs=1;tls7_send_record(s,TLS7_CHANGE_CIPHER_SPEC,&ccs,1);
    s->state=TLS7_CHANGE_CIPHER_SENT;

    /* Finished (encrypted) - compute verify_data */
    uint8_t hash[32];sha256_final(&s->handshake_hash,hash);
    uint8_t verify[12];
    tls7_prf(s->master_secret,48,"client finished",hash,32,verify,12);
    /* Send encrypted Finished */
    tls7_send_appdata(s,verify,12);/* simplified: send as app data */
    s->state=TLS7_FINISHED_SENT;

    /* Read server ChangeCipherSpec + Finished */
    rec_type=tls7_recv_record(s);
    if(rec_type==TLS7_CHANGE_CIPHER_SPEC){s->state=TLS7_CHANGE_CIPHER_RECV;}
    rec_type=tls7_recv_record(s);
    if(rec_type==TLS7_HANDSHAKE||rec_type==TLS7_APPLICATION_DATA){
        s->state=TLS7_FINISHED_RECV;
    }

    s->state=TLS7_ESTABLISHED;
    return 0;
}

int tls7_send(int sock_fd,const void*buf,size_t len){
    tls7_session_t*s=tls7_find(sock_fd);if(!s||s->state!=TLS7_ESTABLISHED)return-ENOTCONN;
    return tls7_send_appdata(s,buf,len);
}

int tls7_recv(int sock_fd,void*buf,size_t len){
    tls7_session_t*s=tls7_find(sock_fd);if(!s||s->state!=TLS7_ESTABLISHED)return-ENOTCONN;
    /* If we have buffered app data, return it */
    if(s->app_len>0){size_t n=s->app_len;if(n>len)n=len;ulibc_memcpy(buf,s->app_buf,n);s->app_len=0;return(int)n;}
    /* Read a record */
    int rec_type=tls7_recv_record(s);
    if(rec_type==TLS7_APPLICATION_DATA&&s->recv_len>0){
        /* Decrypt CBC */
        size_t dlen=s->recv_len;
        if(dlen>=32){
            size_t enc_len=dlen-16;
            uint8_t*dec=(uint8_t*)ulibc_malloc(enc_len);
            if(!dec)return -ENOMEM;
            aes7_cbc_decrypt(&s->server_dec,s->recv_buf,s->recv_buf+16,dec,enc_len);
            /* Remove padding */
            {
                size_t plain_len=enc_len;
                uint8_t pad=dec[enc_len-1];
                if((size_t)pad<enc_len)plain_len-=((size_t)pad+1);
                if(plain_len>len)plain_len=len;
                ulibc_memcpy(buf,dec,plain_len);
                ulibc_free(dec);
                s->server_seq++;
                return (int)plain_len;
            }
        }
    }
    if(rec_type==TLS7_ALERT)return 0;/* connection closed */
    return-EAGAIN;
}

void tls7_close(int sock_fd){tls7_session_t*s=tls7_find(sock_fd);if(s){s->state=TLS7_CLOSED;s->used=false;}}
int tls7_get_state(int sock_fd){tls7_session_t*s=tls7_find(sock_fd);return s?(int)s->state:-1;}

/* X11 PROTOCOL: connection setup, windows, GCs, events, PutImage */
x11_connection_t g_x11_conns[X11_MAX_CONN];
static bool g_x11_render_dirty;
static uint32_t g_x11_trace_req_count;
static uint32_t g_x11_trace_ext_count;
static uint32_t g_x11_trace_win_count;
static uint32_t g_x11_trace_event_count;
static uint32_t g_x11_trace_atom_count;
static uint32_t g_x11_trace_render_count;
static uint32_t g_x11_server_time;
static uint32_t g_wl7_trace_count;
static uint32_t g_wl7_req_trace_count;
#define WL7_TRACE_ATTACH_LIMIT 16u
#define WL7_TRACE_REQ_LIMIT    64u
#define WL7_TRACE_SEND_LIMIT   24u
static int g_x11_focus_conn=-1;
static uint32_t g_x11_focus_window=0;
static uint16_t g_x11_key_state_mask;

extern void ridux_request_cursor_redraw(void);
extern void ridux_present_cursor_after_external_blit(int x,int y,int w,int h);

#define X11_MAX_REQUEST_BYTES (32u*1024u*1024u)
#define X11_EVENT_MASK_PROPERTY_CHANGE (1u<<22)

typedef struct {
    uint32_t id;
    const char *name;
} x11_builtin_atom_t;

static const x11_builtin_atom_t g_x11_builtin_atoms[]={
    {1,"PRIMARY"},{2,"SECONDARY"},{3,"ARC"},{4,"ATOM"},{5,"BITMAP"},
    {6,"CARDINAL"},{7,"COLORMAP"},{8,"CURSOR"},
    {9,"CUT_BUFFER0"},{10,"CUT_BUFFER1"},{11,"CUT_BUFFER2"},{12,"CUT_BUFFER3"},
    {13,"CUT_BUFFER4"},{14,"CUT_BUFFER5"},{15,"CUT_BUFFER6"},{16,"CUT_BUFFER7"},
    {17,"DRAWABLE"},{18,"FONT"},{19,"INTEGER"},{20,"PIXMAP"},
    {21,"POINT"},{22,"RECTANGLE"},{23,"RESOURCE_MANAGER"},
    {24,"RGB_COLOR_MAP"},{25,"RGB_BEST_MAP"},{26,"RGB_BLUE_MAP"},
    {27,"RGB_DEFAULT_MAP"},{28,"RGB_GRAY_MAP"},{29,"RGB_GREEN_MAP"},
    {30,"RGB_RED_MAP"},{31,"STRING"},{32,"VISUALID"},{33,"WINDOW"},
    {34,"WM_COMMAND"},{35,"WM_HINTS"},{36,"WM_CLIENT_MACHINE"},
    {37,"WM_ICON_NAME"},{38,"WM_ICON_SIZE"},{39,"WM_NAME"},
    {40,"WM_NORMAL_HINTS"},{41,"WM_SIZE_HINTS"},{42,"WM_ZOOM_HINTS"},
    {43,"MIN_SPACE"},{44,"NORM_SPACE"},{45,"MAX_SPACE"},{46,"END_SPACE"},
    {47,"SUPERSCRIPT_X"},{48,"SUPERSCRIPT_Y"},{49,"SUBSCRIPT_X"},
    {50,"SUBSCRIPT_Y"},{51,"UNDERLINE_POSITION"},{52,"UNDERLINE_THICKNESS"},
    {53,"STRIKEOUT_ASCENT"},{54,"STRIKEOUT_DESCENT"},{55,"ITALIC_ANGLE"},
    {56,"X_HEIGHT"},{57,"QUAD_WIDTH"},{58,"WEIGHT"},{59,"POINT_SIZE"},
    {60,"RESOLUTION"},{61,"COPYRIGHT"},{62,"NOTICE"},{63,"FONT_NAME"},
    {64,"FAMILY_NAME"},{65,"FULL_NAME"},{66,"CAP_HEIGHT"},
    {67,"WM_CLASS"},{68,"WM_TRANSIENT_FOR"}
};

static const char *x11_atom_name_by_id(x11_connection_t *c,uint32_t id){
    int i;
    if(!c||!id)return "";
    for(i=0;i<X11_MAX_ATOM;++i){
        if(c->atoms[i].used&&c->atoms[i].id==id)return c->atoms[i].name;
    }
    return "";
}

static uint32_t x11_atom_id_by_name(x11_connection_t *c,const char *name){
    int i;
    if(!c||!name||!*name)return 0;
    for(i=0;i<X11_MAX_ATOM;++i){
        if(c->atoms[i].used&&ulibc_strcmp(c->atoms[i].name,name)==0)return c->atoms[i].id;
    }
    return 0;
}

static uint32_t x11_register_atom(x11_connection_t *c,uint32_t id,const char *name){
    int i,free_i=-1;
    if(!c||!id||!name||!*name)return 0;
    for(i=0;i<X11_MAX_ATOM;++i){
        if(c->atoms[i].used){
            if(c->atoms[i].id==id||ulibc_strcmp(c->atoms[i].name,name)==0){
                c->atoms[i].id=id;
                ulibc_strcpy(c->atoms[i].name,name);
                return id;
            }
        }else if(free_i<0){
            free_i=i;
        }
    }
    if(free_i<0)return 0;
    c->atoms[free_i].used=true;
    c->atoms[free_i].id=id;
    ulibc_strcpy(c->atoms[free_i].name,name);
    c->atom_count++;
    return id;
}

static uint32_t x11_intern_atom(x11_connection_t *c,const char *name,bool create){
    uint32_t id,max_id=0;
    int i,free_i=-1;
    if(!c||!name||!*name)return 0;
    id=x11_atom_id_by_name(c,name);
    if(id||!create)return id;
    for(i=0;i<X11_MAX_ATOM;++i){
        if(c->atoms[i].used){
            if(c->atoms[i].id>max_id)max_id=c->atoms[i].id;
        }else if(free_i<0){
            free_i=i;
        }
    }
    if(free_i<0)return 0;
    id=max_id+1u;
    c->atoms[free_i].used=true;
    c->atoms[free_i].id=id;
    ulibc_strcpy(c->atoms[free_i].name,name);
    c->atom_count++;
    return id;
}

static void x11_init_atoms(x11_connection_t *c){
    size_t i;
    if(!c)return;
    for(i=0;i<sizeof(g_x11_builtin_atoms)/sizeof(g_x11_builtin_atoms[0]);++i){
        x11_register_atom(c,g_x11_builtin_atoms[i].id,g_x11_builtin_atoms[i].name);
    }
    x11_intern_atom(c,"WM_PROTOCOLS",true);
    x11_intern_atom(c,"WM_DELETE_WINDOW",true);
    x11_intern_atom(c,"UTF8_STRING",true);
    x11_intern_atom(c,"_NET_SUPPORTED",true);
    x11_intern_atom(c,"_NET_SUPPORTING_WM_CHECK",true);
    x11_intern_atom(c,"_NET_WM_NAME",true);
    x11_intern_atom(c,"_NET_WM_PID",true);
    x11_intern_atom(c,"_NET_WM_WINDOW_TYPE",true);
    x11_intern_atom(c,"_NET_WM_WINDOW_TYPE_NORMAL",true);
    x11_intern_atom(c,"_NET_ACTIVE_WINDOW",true);
    x11_intern_atom(c,"_NET_WM_STATE",true);
    x11_intern_atom(c,"_NET_WM_STATE_FULLSCREEN",true);
    x11_intern_atom(c,"_NET_WM_STATE_MAXIMIZED_VERT",true);
    x11_intern_atom(c,"_NET_WM_STATE_MAXIMIZED_HORZ",true);
    x11_intern_atom(c,"_NET_WM_DESKTOP",true);
    x11_intern_atom(c,"_NET_WM_ALLOWED_ACTIONS",true);
    x11_intern_atom(c,"_NET_WM_ACTION_CLOSE",true);
    x11_intern_atom(c,"_NET_WM_ACTION_MINIMIZE",true);
    x11_intern_atom(c,"_NET_WM_ACTION_MAXIMIZE_VERT",true);
    x11_intern_atom(c,"_NET_WM_ACTION_MAXIMIZE_HORZ",true);
    x11_intern_atom(c,"_NET_FRAME_EXTENTS",true);
    x11_intern_atom(c,"_NET_WORKAREA",true);
    x11_intern_atom(c,"_SCREENSAVER_STATUS",true);
    x11_intern_atom(c,"_MOTIF_WM_HINTS",true);
    x11_intern_atom(c,"WM_S0",true);
    x11_intern_atom(c,"_NET_WM_CM_S0",true);
    x11_intern_atom(c,"_XSETTINGS_S0",true);
    x11_intern_atom(c,"_XSETTINGS_SETTINGS",true);
    x11_intern_atom(c,"MANAGER",true);
}

static const char *x11_opcode_name(uint8_t opcode){
    switch(opcode){
        case X11_CreateWindow:return "CreateWindow";
        case X11_ChangeWindowAttributes:return "ChangeWindowAttributes";
        case X11_GetWindowAttributes:return "GetWindowAttributes";
        case X11_DestroyWindow:return "DestroyWindow";
        case X11_ReparentWindow:return "ReparentWindow";
        case X11_MapWindow:return "MapWindow";
        case X11_MapSubwindows:return "MapSubwindows";
        case X11_UnmapWindow:return "UnmapWindow";
        case X11_ConfigureWindow:return "ConfigureWindow";
        case X11_GetGeometry:return "GetGeometry";
        case X11_QueryTree:return "QueryTree";
        case X11_InternAtom:return "InternAtom";
        case X11_GetAtomName:return "GetAtomName";
        case X11_ChangeProperty:return "ChangeProperty";
        case X11_DeleteProperty:return "DeleteProperty";
        case X11_GetProperty:return "GetProperty";
        case X11_SetSelectionOwner:return "SetSelectionOwner";
        case X11_GetSelectionOwner:return "GetSelectionOwner";
        case X11_ConvertSelection:return "ConvertSelection";
        case X11_SendEvent:return "SendEvent";
        case X11_GrabServer:return "GrabServer";
        case X11_UngrabServer:return "UngrabServer";
        case X11_GrabPointer:return "GrabPointer";
        case X11_GrabKeyboard:return "GrabKeyboard";
        case X11_QueryPointer:return "QueryPointer";
        case X11_TranslateCoordinates:return "TranslateCoordinates";
        case X11_WarpPointer:return "WarpPointer";
        case X11_SetInputFocus:return "SetInputFocus";
        case X11_GetInputFocus:return "GetInputFocus";
        case X11_OpenFont:return "OpenFont";
        case X11_CreatePixmap:return "CreatePixmap";
        case X11_FreePixmap:return "FreePixmap";
        case X11_CreateGC:return "CreateGC";
        case X11_ChangeGC:return "ChangeGC";
        case X11_FreeGC:return "FreeGC";
        case X11_CopyArea:return "CopyArea";
        case X11_PolyFillRectangle:return "PolyFillRectangle";
        case X11_Sync:return "Sync";
        case X11_PutImage:return "PutImage";
        case X11_GetImage:return "GetImage";
        case X11_Flush:return "Flush";
        case X11_QueryExtension:return "QueryExtension";
        case X11_ListExtensions:return "ListExtensions";
        case X11_GetKeyboardMapping:return "GetKeyboardMapping";
        case X11_SetPointerMapping:return "SetPointerMapping";
        case X11_GetPointerMapping:return "GetPointerMapping";
        case X11_SetModifierMapping:return "SetModifierMapping";
        case X11_GetModifierMapping:return "GetModifierMapping";
        default:return "?";
    }
}

static const char *x11_ext_opcode_name(x11_connection_t *c,uint8_t opcode){
    if(!c)return "?";
    if(opcode==c->ext_opcode_shm)return "MIT-SHM";
    if(opcode==c->ext_opcode_render)return "RENDER";
    if(opcode==c->ext_opcode_xfixes)return "XFIXES";
    if(opcode==c->ext_opcode_randr)return "RANDR";
    if(opcode==c->ext_opcode_xinput)return "XInputExtension";
    if(opcode==c->ext_opcode_glx)return "GLX";
    if(opcode==c->ext_opcode_bigreq)return "BIG-REQUESTS";
    if(opcode==c->ext_opcode_xge)return "Generic Event Extension";
    if(opcode==c->ext_opcode_composite)return "Composite";
    if(opcode==c->ext_opcode_damage)return "DAMAGE";
    if(opcode==c->ext_opcode_shape)return "SHAPE";
    if(opcode==c->ext_opcode_xkeyboard)return "XKEYBOARD";
    if(opcode==c->ext_opcode_sync)return "SYNC";
    return 0;
}

static void x11_trace_setup(x11_connection_t *c){
    if(!c)return;
    __boot_serial_force_puts("[x11-setup!] fd=");
    __boot_serial_force_putu32((uint32_t)c->sock_fd);
    __boot_serial_force_puts(" proto=");
    __boot_serial_force_putu32((uint32_t)c->proto_major);
    __boot_serial_force_puts(".");
    __boot_serial_force_putu32((uint32_t)c->proto_minor);
    __boot_serial_force_puts(" screen=");
    __boot_serial_force_putu32((uint32_t)c->screen_width);
    __boot_serial_force_puts("x");
    __boot_serial_force_putu32((uint32_t)c->screen_height);
    __boot_serial_force_puts(" root=");
    __boot_serial_force_puthex64((uint64_t)c->root_window);
    __boot_serial_force_puts(" visual=");
    __boot_serial_force_puthex64((uint64_t)c->visual);
    __boot_serial_force_puts("\n");
}

static void x11_trace_request(x11_connection_t *c,uint8_t opcode,uint8_t extra,uint16_t req_len,size_t body_len){
    const char *ext_name;
    if(g_x11_trace_req_count>=1400)return;
    ++g_x11_trace_req_count;
    ext_name=x11_ext_opcode_name(c,opcode);
    if(opcode==X11_GetKeyboardMapping||opcode==X11_GetModifierMapping||
       opcode==X11_CreateWindow||opcode==X11_MapWindow){
        __boot_serial_force_puts("[x11-req!] #");
        __boot_serial_force_putu32(g_x11_trace_req_count);
        __boot_serial_force_puts(" fd=");
        __boot_serial_force_putu32(c?(uint32_t)c->sock_fd:0);
        __boot_serial_force_puts(" seq=");
        __boot_serial_force_putu32(c?(uint32_t)c->sequence:0);
        __boot_serial_force_puts(" op=");
        __boot_serial_force_putu32((uint32_t)opcode);
        __boot_serial_force_puts(" ");
        __boot_serial_force_puts(ext_name?ext_name:x11_opcode_name(opcode));
        __boot_serial_force_puts(" extra=");
        __boot_serial_force_putu32((uint32_t)extra);
        __boot_serial_force_puts(" len4=");
        __boot_serial_force_putu32((uint32_t)req_len);
        __boot_serial_force_puts(" body=");
        __boot_serial_force_putu32((uint32_t)body_len);
        __boot_serial_force_puts(" pending=");
        __boot_serial_force_putu32(c?(uint32_t)c->req_len:0);
        __boot_serial_force_puts("\n");
    }
}

static void x11_trace_extension(x11_connection_t *c,const char *name,uint8_t minor,size_t body_len,const char *action){
    if(g_x11_trace_ext_count>=40)return;
    ++g_x11_trace_ext_count;
    __boot_serial_force_puts("[x11-ext!] #");
    __boot_serial_force_putu32(g_x11_trace_ext_count);
    __boot_serial_force_puts(" fd=");
    __boot_serial_force_putu32(c?(uint32_t)c->sock_fd:0);
    __boot_serial_force_puts(" ");
    __boot_serial_force_puts(name?name:"?");
    __boot_serial_force_puts(" minor=");
    __boot_serial_force_putu32((uint32_t)minor);
    __boot_serial_force_puts(" body=");
    __boot_serial_force_putu32((uint32_t)body_len);
    __boot_serial_force_puts(" ");
    __boot_serial_force_puts(action?action:"");
    __boot_serial_force_puts("\n");
}

static void x11_trace_window_op(x11_connection_t *c,const char *op,uint32_t id,int w,int h){
    if(g_x11_trace_win_count>=320)return;
    ++g_x11_trace_win_count;
    __boot_serial_force_puts("[x11-win!] ");
    __boot_serial_force_puts(op?op:"?");
    __boot_serial_force_puts(" fd=");
    __boot_serial_force_putu32(c?(uint32_t)c->sock_fd:0);
    __boot_serial_force_puts(" id=");
    __boot_serial_force_puthex64((uint64_t)id);
    if(w>=0&&h>=0){
        __boot_serial_force_puts(" size=");
        __boot_serial_force_putu32((uint32_t)w);
        __boot_serial_force_puts("x");
        __boot_serial_force_putu32((uint32_t)h);
    }
    __boot_serial_force_puts("\n");
}

static void x11_trace_atom(x11_connection_t *c,const char *op,uint32_t id,const char *name,uint32_t aux){
    if(g_x11_trace_atom_count>=320)return;
    ++g_x11_trace_atom_count;
    __boot_serial_puts("[x11-atom] #");
    __boot_serial_putu32(g_x11_trace_atom_count);
    __boot_serial_puts(" fd=");
    __boot_serial_putu32(c?(uint32_t)c->sock_fd:0);
    __boot_serial_puts(" ");
    __boot_serial_puts(op?op:"?");
    __boot_serial_puts(" id=");
    __boot_serial_putu32(id);
    __boot_serial_puts(" name=");
    __boot_serial_puts((name&&*name)?name:"?");
    if(aux){
        __boot_serial_puts(" aux=");
        __boot_serial_puthex64((uint64_t)aux);
    }
    __boot_serial_puts("\n");
}

static uint32_t x11_selection_owner_for_name(x11_connection_t *c,const char *name){
    if(!c||!name)return 0;
    /*
     * Firefox/GTK repeatedly probes EWMH before showing its browser chrome.
     * We advertise a tiny non-compositing WM so clients can leave the
     * unmanaged 1x1/10x10 setup path, while root SendEvent is still dropped
     * below so client messages do not echo back into the app.
     */
    if(ulibc_strcmp(name,"WM_S0")==0)return c->root_window;
    if(ulibc_strcmp(name,"_XSETTINGS_S0")==0)return 0;
    /* Do not claim a full compositing manager yet. Chromium switches into a
     * heavier XComposite path when this selection has an owner; our fast path
     * is the simpler X11 backing-store + MIT-SHM/software paint route. */
    if(ulibc_strcmp(name,"_NET_WM_CM_S0")==0)return 0;
    return 0;
}

static bool x11_advertise_wm(const x11_connection_t *c){
    return c&&c->root_window;
}

static void x11_wr16(const x11_connection_t *c,uint8_t *p,uint16_t v);
static void x11_wr32(const x11_connection_t *c,uint8_t *p,uint32_t v);
static void x11_push_event(x11_connection_t*c,const x11_event_t*ev);
static bool x11_window_content_origin(x11_connection_t *c,x11_window_t *w,int *x,int *y);

static void x11_push_xfixes_selection_event(x11_connection_t *c,uint32_t window,uint32_t selection,uint32_t owner){
    uint8_t raw[32];
    x11_event_t ev;
    if(!c||!window||!selection)return;
    ulibc_memset(raw,0,sizeof(raw));
    raw[0]=68; /* XFIXES first_event + XFixesSelectionNotify */
    raw[1]=0;  /* XFixesSetSelectionOwnerNotify */
    x11_wr16(c,raw+2,c->sequence);
    x11_wr32(c,raw+4,window);
    x11_wr32(c,raw+8,owner);
    x11_wr32(c,raw+12,selection);
    x11_wr32(c,raw+16,1); /* timestamp */
    x11_wr32(c,raw+20,1); /* selection timestamp */
    ulibc_memcpy(&ev,raw,sizeof(ev));
    x11_push_event(c,&ev);
}

static void x11_push_raw32_event(x11_connection_t *c,const uint8_t raw[32]){
    x11_event_t ev;
    if(!c||!raw)return;
    ulibc_memcpy(&ev,raw,sizeof(ev));
    x11_push_event(c,&ev);
}

static void x11_push_configure_notify(x11_connection_t *c,uint32_t wid,int x,int y,int w,int h,int border){
    uint8_t raw[32];
    if(!c||!wid)return;
    ulibc_memset(raw,0,sizeof(raw));
    raw[0]=X11_ConfigureNotify;
    raw[1]=0;
    x11_wr16(c,raw+2,c->sequence);
    x11_wr32(c,raw+4,wid);       /* event */
    x11_wr32(c,raw+8,wid);       /* window */
    x11_wr32(c,raw+12,0);        /* above-sibling */
    x11_wr16(c,raw+16,(uint16_t)(int16_t)x);
    x11_wr16(c,raw+18,(uint16_t)(int16_t)y);
    x11_wr16(c,raw+20,(uint16_t)w);
    x11_wr16(c,raw+22,(uint16_t)h);
    x11_wr16(c,raw+24,(uint16_t)border);
    raw[26]=0;                   /* override-redirect */
    x11_push_raw32_event(c,raw);
}

static void x11_push_map_notify_event(x11_connection_t *c,uint32_t wid){
    uint8_t raw[32];
    if(!c||!wid)return;
    ulibc_memset(raw,0,sizeof(raw));
    raw[0]=X11_MapNotify;
    raw[1]=0;
    x11_wr16(c,raw+2,c->sequence);
    x11_wr32(c,raw+4,wid);       /* event */
    x11_wr32(c,raw+8,wid);       /* window */
    raw[12]=0;                   /* override-redirect */
    x11_push_raw32_event(c,raw);
}

static void x11_push_expose_event(x11_connection_t *c,uint32_t wid,int x,int y,int w,int h){
    uint8_t raw[32];
    if(!c||!wid)return;
    ulibc_memset(raw,0,sizeof(raw));
    raw[0]=X11_Expose;
    raw[1]=0;
    x11_wr16(c,raw+2,c->sequence);
    x11_wr32(c,raw+4,wid);
    x11_wr16(c,raw+8,(uint16_t)(int16_t)x);
    x11_wr16(c,raw+10,(uint16_t)(int16_t)y);
    x11_wr16(c,raw+12,(uint16_t)w);
    x11_wr16(c,raw+14,(uint16_t)h);
    x11_wr16(c,raw+16,0);        /* count */
    x11_push_raw32_event(c,raw);
}

static x11_connection_t*x11_alloc(void){int i;for(i=0;i<X11_MAX_CONN;++i)if(!g_x11_conns[i].used){ulibc_memset(&g_x11_conns[i],0,sizeof(x11_connection_t));g_x11_conns[i].used=true;return&g_x11_conns[i];}return 0;}

static x11_window_t*x11_find_window(x11_connection_t*c,uint32_t id){
    int i;for(i=0;i<X11_MAX_WINDOWS;++i)if(c->windows[i].used&&c->windows[i].id==id)return&c->windows[i];return 0;}
static x11_pixmap_t*x11_find_pixmap(x11_connection_t*c,uint32_t id){
    int i;for(i=0;i<X11_MAX_PIXMAP;++i)if(c->pixmaps[i].used&&c->pixmaps[i].id==id)return&c->pixmaps[i];return 0;}
static x11_gc_t*x11_find_gc(x11_connection_t*c,uint32_t id){
    int i;for(i=0;i<X11_MAX_GC;++i)if(c->gcs[i].used&&c->gcs[i].id==id)return&c->gcs[i];return 0;}
static x11_shmseg_t*x11_find_shmseg(x11_connection_t*c,uint32_t id){
    int i;for(i=0;i<X11_MAX_SHMSEG;++i)if(c->shmsegs[i].used&&c->shmsegs[i].id==id)return&c->shmsegs[i];return 0;}
static x11_picture_t*x11_find_picture(x11_connection_t*c,uint32_t id){
    int i;for(i=0;i<X11_MAX_PICTURE;++i)if(c->pictures[i].used&&c->pictures[i].id==id)return&c->pictures[i];return 0;}

static uint32_t x11_next_server_time(void){
    ++g_x11_server_time;
    if(!g_x11_server_time)g_x11_server_time=1;
    return g_x11_server_time;
}

static void x11_push_property_notify_event(x11_connection_t *c,uint32_t wid,uint32_t atom,uint8_t state){
    uint8_t raw[32];
    x11_window_t *w;
    const char *name;
    if(!c||!wid||!atom)return;
    w=x11_find_window(c,wid);
    name=x11_atom_name_by_id(c,atom);
    /*
     * GTK obtains server time by changing GDK_TIMESTAMP_PROP and waiting for
     * PropertyNotify. Without this event Firefox stalls before its real
     * browser window is mapped.
     */
    if(w&&!(w->event_mask&X11_EVENT_MASK_PROPERTY_CHANGE)&&
       (!name||ulibc_strcmp(name,"GDK_TIMESTAMP_PROP")!=0))return;
    ulibc_memset(raw,0,sizeof(raw));
    raw[0]=X11_PropertyNotify;
    raw[1]=state; /* 0=NewValue, 1=Deleted */
    x11_wr16(c,raw+2,c->sequence);
    x11_wr32(c,raw+4,wid);
    x11_wr32(c,raw+8,atom);
    x11_wr32(c,raw+12,x11_next_server_time());
    x11_push_raw32_event(c,raw);
}

enum {
    X11_PROP_MODE_REPLACE = 0,
    X11_PROP_MODE_PREPEND = 1,
    X11_PROP_MODE_APPEND  = 2
};

static bool x11_is_lsb(const x11_connection_t *c){
    return c&&c->endian==0x0001;
}

static uint16_t x11_rd16(const x11_connection_t *c,const uint8_t *p){
    if(x11_is_lsb(c))return (uint16_t)p[0]|((uint16_t)p[1]<<8);
    return (uint16_t)(((uint16_t)p[0]<<8)|p[1]);
}

static uint32_t x11_rd32(const x11_connection_t *c,const uint8_t *p){
    if(x11_is_lsb(c))return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);
    return ((uint32_t)p[0]<<24)|((uint32_t)p[1]<<16)|((uint32_t)p[2]<<8)|p[3];
}

static void x11_wr16(const x11_connection_t *c,uint8_t *p,uint16_t v){
    if(x11_is_lsb(c)){p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);return;}
    p[0]=(uint8_t)(v>>8);p[1]=(uint8_t)v;
}

static void x11_wr32(const x11_connection_t *c,uint8_t *p,uint32_t v){
    if(x11_is_lsb(c)){p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);p[2]=(uint8_t)(v>>16);p[3]=(uint8_t)(v>>24);return;}
    p[0]=(uint8_t)(v>>24);p[1]=(uint8_t)(v>>16);p[2]=(uint8_t)(v>>8);p[3]=(uint8_t)v;
}

static void x11_reply_init(x11_connection_t *c,uint8_t *reply,size_t sz,uint8_t detail){
    if(!c||!reply||sz<32)return;
    ulibc_memset(reply,0,sz);
    reply[0]=1;
    reply[1]=detail;
    x11_wr16(c,reply+2,c->sequence);
}

static void x11_reply_set_length(x11_connection_t *c,uint8_t *reply,uint32_t words){
    if(!c||!reply)return;
    x11_wr32(c,reply+4,words);
}

static void x11_send_glx_string_reply(x11_connection_t *c,const char *s){
    uint8_t reply[32];
    uint8_t pad[4]={0,0,0,0};
    uint32_t n=0;
    uint32_t words;
    uint32_t pad_len;
    if(!c)return;
    if(s)n=(uint32_t)ulibc_strlen(s);
    words=(n+3u)/4u;
    pad_len=(4u-(n&3u))&3u;
    x11_reply_init(c,reply,sizeof(reply),0);
    x11_reply_set_length(c,reply,words);
    x11_wr32(c,reply+12,n);
    sock_send(c->sock_fd,reply,sizeof(reply),0);
    if(n&&s)sock_send(c->sock_fd,s,n,0);
    if(pad_len)sock_send(c->sock_fd,pad,pad_len,0);
}

static void x11_write_render_format(x11_connection_t *c,uint8_t *p,uint32_t id,uint8_t depth,bool alpha){
    x11_wr32(c,p+0,id);
    p[4]=1;               /* PictTypeDirect */
    p[5]=depth;
    x11_wr16(c,p+8,16);   /* red shift */
    x11_wr16(c,p+10,0xFF);
    x11_wr16(c,p+12,8);   /* green shift */
    x11_wr16(c,p+14,0xFF);
    x11_wr16(c,p+16,0);   /* blue shift */
    x11_wr16(c,p+18,0xFF);
    x11_wr16(c,p+20,alpha?24:0);
    x11_wr16(c,p+22,alpha?0xFF:0);
    x11_wr32(c,p+24,0);   /* colormap */
}

static void x11_send_render_pict_formats(x11_connection_t *c){
    uint8_t reply[32];
    uint8_t data[80];
    uint32_t fmt_rgb24=0x42u;
    uint32_t fmt_argb32=0x43u;
    if(!c)return;
    x11_reply_init(c,reply,sizeof(reply),0);
    x11_wr32(c,reply+4,(uint32_t)(sizeof(data)/4u));
    x11_wr32(c,reply+8,2);  /* num formats */
    x11_wr32(c,reply+12,1); /* num screens */
    x11_wr32(c,reply+16,1); /* num depths */
    x11_wr32(c,reply+20,1); /* num visuals */
    x11_wr32(c,reply+24,0); /* num subpixel types */
    ulibc_memset(data,0,sizeof(data));
    x11_write_render_format(c,data+0,fmt_rgb24,24,false);
    x11_write_render_format(c,data+28,fmt_argb32,32,true);
    x11_wr32(c,data+56,1);         /* one depth in this screen */
    x11_wr32(c,data+60,fmt_rgb24); /* fallback */
    data[64]=24;
    x11_wr16(c,data+66,1);         /* one visual */
    x11_wr32(c,data+72,c->visual);
    x11_wr32(c,data+76,fmt_rgb24);
    sock_send(c->sock_fd,reply,sizeof(reply),0);
    sock_send(c->sock_fd,data,sizeof(data),0);
}

static x11_property_t *x11_find_prop(x11_connection_t *c,uint32_t window,uint32_t atom){
    int i;
    if(!c||!window||!atom)return 0;
    for(i=0;i<X11_MAX_PROPS;++i){
        if(!c->props[i].used)continue;
        if(c->props[i].window!=window||c->props[i].atom!=atom)continue;
        return &c->props[i];
    }
    return 0;
}

static x11_property_t *x11_alloc_prop(x11_connection_t *c){
    int i;
    if(!c)return 0;
    for(i=0;i<X11_MAX_PROPS;++i){
        if(c->props[i].used)continue;
        ulibc_memset(&c->props[i],0,sizeof(c->props[i]));
        c->props[i].used=true;
        ++c->prop_count;
        return &c->props[i];
    }
    return 0;
}

static void x11_free_prop(x11_connection_t *c,x11_property_t *p){
    if(!c||!p||!p->used)return;
    if(p->data)ulibc_free(p->data);
    ulibc_memset(p,0,sizeof(*p));
    if(c->prop_count>0)--c->prop_count;
}

static void x11_free_window_props(x11_connection_t *c,uint32_t window){
    int i;
    if(!c||!window)return;
    for(i=0;i<X11_MAX_PROPS;++i){
        if(!c->props[i].used||c->props[i].window!=window)continue;
        x11_free_prop(c,&c->props[i]);
    }
}

static bool x11_drawable_surface(x11_connection_t*c,uint32_t drawable,uint8_t **backing,uint32_t *backing_size,int *width,int *height){
    x11_window_t *w;
    x11_pixmap_t *p;
    if(!c||!backing||!backing_size||!width||!height)return false;
    w=x11_find_window(c,drawable);
    if(w&&w->backing&&w->backing_size){
        *backing=w->backing;
        *backing_size=w->backing_size;
        *width=w->width;
        *height=w->height;
        return true;
    }
    p=x11_find_pixmap(c,drawable);
    if(p&&p->backing&&p->backing_size){
        *backing=p->backing;
        *backing_size=p->backing_size;
        *width=p->width;
        *height=p->height;
        return true;
    }
    return false;
}

static void x11_resize_window_backing(x11_window_t *w,int old_w,int old_h){
    uint8_t *old;
    uint32_t old_size;
    uint8_t *nb;
    uint32_t new_size;
    uint32_t rows,cols,row;
    uint64_t pixels;
    if(!w)return;
    old=w->backing;
    old_size=w->backing_size;
    if(w->width<=0||w->height<=0){
        if(old)ulibc_free(old);
        w->backing=0;
        w->backing_size=0;
        return;
    }
    pixels=(uint64_t)(uint32_t)w->width*(uint64_t)(uint32_t)w->height;
    if(pixels>0x1000000ULL){
        if(old)ulibc_free(old);
        w->backing=0;
        w->backing_size=0;
        return;
    }
    new_size=(uint32_t)pixels*4u;
    if(old&&old_size==new_size)return;
    nb=(uint8_t*)ulibc_malloc(new_size);
    if(!nb){
        if(old)ulibc_free(old);
        w->backing=0;
        w->backing_size=0;
        return;
    }
    ulibc_memset(nb,0,new_size);
    if(old&&old_w>0&&old_h>0){
        rows=(old_h<w->height)?(uint32_t)old_h:(uint32_t)w->height;
        cols=(old_w<w->width)?(uint32_t)old_w:(uint32_t)w->width;
        for(row=0;row<rows;++row){
            size_t old_off=(size_t)row*(size_t)old_w*4u;
            size_t new_off=(size_t)row*(size_t)w->width*4u;
            size_t bytes=(size_t)cols*4u;
            if(old_off+bytes<=old_size&&new_off+bytes<=new_size)
                ulibc_memcpy(nb+new_off,old+old_off,bytes);
        }
    }
    if(old)ulibc_free(old);
    w->backing=nb;
    w->backing_size=new_size;
}

static void x11_blit_argb32_buf(uint8_t *dst,size_t dst_size,int dst_w,int dst_h,int dst_x,int dst_y,const uint8_t*src,size_t src_stride,int src_x,int src_y,int w,int h){
    int row;
    if(!dst||!src||w<=0||h<=0||src_stride<4||dst_w<=0||dst_h<=0)return;
    for(row=0;row<h;++row){
        int sy=src_y+row;
        int dy=dst_y+row;
        int col;
        if(dy<0||dy>=dst_h||sy<0)continue;
        for(col=0;col<w;++col){
            int sx=src_x+col;
            int dx=dst_x+col;
            size_t src_off,dst_off;
            if(dx<0||dx>=dst_w||sx<0)continue;
            src_off=(size_t)sy*src_stride+(size_t)sx*4;
            dst_off=(size_t)(dy*dst_w+dx)*4;
            if(dst_off+4>dst_size)continue;
            ulibc_memcpy(dst+dst_off,src+src_off,4);
        }
    }
    g_x11_render_dirty=true;
}

static void x11_fill_rect_argb32(uint8_t *dst,size_t dst_size,int dst_w,int dst_h,int x,int y,int w,int h,uint32_t argb){
    int row;
    if(!dst||dst_w<=0||dst_h<=0||w<=0||h<=0)return;
    for(row=0;row<h;++row){
        int dy=y+row;
        int col;
        if(dy<0||dy>=dst_h)continue;
        for(col=0;col<w;++col){
            int dx=x+col;
            size_t off;
            if(dx<0||dx>=dst_w)continue;
            off=(size_t)(dy*dst_w+dx)*4;
            if(off+4>dst_size)continue;
            dst[off+0]=(uint8_t)(argb&0xFFu);
            dst[off+1]=(uint8_t)((argb>>8)&0xFFu);
            dst[off+2]=(uint8_t)((argb>>16)&0xFFu);
            dst[off+3]=(uint8_t)((argb>>24)&0xFFu);
        }
    }
    g_x11_render_dirty=true;
}

static void x11_render_fill_rectangles(x11_connection_t *c,const uint8_t *body,size_t body_len){
    uint32_t dst_pic;
    uint16_t red,green,blue,alpha;
    uint32_t argb;
    x11_picture_t *dp;
    uint8_t *dst_backing=0;
    uint32_t dst_size=0;
    int dst_w=0,dst_h=0;
    size_t off;
    if(!c||!body||body_len<16)return;
    dst_pic=x11_rd32(c,body+4);
    red=x11_rd16(c,body+8);
    green=x11_rd16(c,body+10);
    blue=x11_rd16(c,body+12);
    alpha=x11_rd16(c,body+14);
    argb=((uint32_t)(alpha>>8)<<24)|((uint32_t)(red>>8)<<16)|((uint32_t)(green>>8)<<8)|(uint32_t)(blue>>8);
    dp=x11_find_picture(c,dst_pic);
    if(!dp)return;
    if(!x11_drawable_surface(c,dp->drawable,&dst_backing,&dst_size,&dst_w,&dst_h))return;
    for(off=16;off+8<=body_len;off+=8){
        int16_t x=(int16_t)x11_rd16(c,body+off+0);
        int16_t y=(int16_t)x11_rd16(c,body+off+2);
        uint16_t w=x11_rd16(c,body+off+4);
        uint16_t h=x11_rd16(c,body+off+6);
        x11_fill_rect_argb32(dst_backing,dst_size,dst_w,dst_h,(int)x,(int)y,(int)w,(int)h,argb);
    }
    x11_trace_window_op(c,"render-fillrect",dp->drawable,dst_w,dst_h);
    g_x11_render_dirty=true;
}

static void x11_push_event(x11_connection_t*c,const x11_event_t*ev){
    if(!c||!ev)return;
    if(c->sock_fd>=0){
        int sent=sock_send(c->sock_fd,ev,32,0);
        if(sent==32){
            if(g_x11_trace_event_count<120){
                ++g_x11_trace_event_count;
                __boot_serial_puts("[x11-event] fd=");
                __boot_serial_putu32((uint32_t)c->sock_fd);
                __boot_serial_puts(" type=");
                __boot_serial_putu32((uint32_t)ev->type);
                __boot_serial_puts(" detail=");
                __boot_serial_putu32((uint32_t)ev->detail);
                __boot_serial_puts(" immediate\n");
            }
            return;
        }
    }
    if(c->event_count>=X11_MAX_EVENTS)return;
    c->events[c->event_tail]=*ev;
    c->event_tail=(c->event_tail+1)%X11_MAX_EVENTS;
    c->event_count++;
}

static void x11_force_map_top_levels(x11_connection_t *c){
    int i;
    if(!c)return;
    for(i=0;i<X11_MAX_WINDOWS;++i){
        x11_window_t *w=&c->windows[i];
        if(!w->used||w->id==c->root_window)continue;
        if(w->width<64||w->height<64)continue;
        if(w->mapped&&w->visible)continue;
        w->mapped=true;
        w->visible=true;
        g_x11_render_dirty=true;
        x11_trace_window_op(c,"auto-map",w->id,w->width,w->height);
        x11_push_configure_notify(c,w->id,w->x,w->y,w->width,w->height,w->border_width);
        x11_push_map_notify_event(c,w->id);
        x11_push_expose_event(c,w->id,0,0,w->width,w->height);
    }
}

static void x11_auto_map_if_client_window(x11_connection_t *c,x11_window_t *w){
    if(!c||!w||!w->used||w->id==c->root_window)return;
    if(w->width<64||w->height<64)return;
    if(w->mapped&&w->visible)return;
    w->mapped=true;
    w->visible=true;
    g_x11_render_dirty=true;
    x11_trace_window_op(c,"auto-map",w->id,w->width,w->height);
    x11_push_configure_notify(c,w->id,w->x,w->y,w->width,w->height,w->border_width);
    x11_push_map_notify_event(c,w->id);
    x11_push_expose_event(c,w->id,0,0,w->width,w->height);
}
int x11_process_request(int conn_idx);

static int x11_conn_idx_by_sock(int sock_fd){
    int i;
    for(i=0;i<X11_MAX_CONN;++i){
        if(g_x11_conns[i].used&&g_x11_conns[i].sock_fd==sock_fd)return i;
    }
    return -1;
}

static int x11_init_stream_connection(x11_connection_t *c,int cfd){
    c->sock_fd=cfd;
    c->proto_major=11;c->proto_minor=0;
    c->endian=0x0100;/* MSB first (X11 standard) */
    c->next_id=0x00400000;/* XID base */
    c->resource_base=c->next_id;
    c->sequence=0;
    c->pointer_x=0;
    c->pointer_y=0;
    c->ext_opcode_shm=128;
    c->ext_opcode_render=129;
    c->ext_opcode_xfixes=130;
    c->ext_opcode_randr=131;
    c->ext_opcode_xinput=132;
    c->ext_opcode_glx=133;
    c->ext_opcode_bigreq=134;
    c->ext_opcode_xge=135;
    c->ext_opcode_composite=136;
    c->ext_opcode_damage=137;
    c->ext_opcode_shape=138;
    c->ext_opcode_xkeyboard=139;
    c->ext_opcode_sync=140;
    /* Read client connection setup */
    uint8_t setup_hdr[12];int rc=sock_recv(cfd,setup_hdr,12,0);
    if(rc<12){c->used=false;sock_close(cfd);return-1;}
    uint8_t byte_order=setup_hdr[0];
    if(byte_order==0x42)c->endian=0x0100;/* MSB */
    else if(byte_order==0x6C)c->endian=0x0001;/* LSB */
    c->proto_major=x11_rd16(c,setup_hdr+2);
    c->proto_minor=x11_rd16(c,setup_hdr+4);
    {
        uint16_t auth_name_len=x11_rd16(c,setup_hdr+6);
        uint16_t auth_data_len=x11_rd16(c,setup_hdr+8);
        /* Read auth data */
        if(auth_name_len>0){
            char sink[64];
            size_t want=(size_t)((auth_name_len+3u)&~3u);
            if(auth_name_len<(uint16_t)sizeof(c->auth_name)){
                sock_recv(cfd,c->auth_name,want,0);
            }else{
                size_t left=want;
                while(left){
                    size_t chunk=(left<sizeof(sink))?left:sizeof(sink);
                    sock_recv(cfd,sink,chunk,0);
                    left-=chunk;
                }
                auth_name_len=(uint16_t)(sizeof(c->auth_name)-1);
            }
            c->auth_name_len=auth_name_len;
            c->auth_name[c->auth_name_len]=0;
        }
        if(auth_data_len>0){
            char sink[128];
            size_t want=(size_t)((auth_data_len+3u)&~3u);
            if(auth_data_len<(uint16_t)sizeof(c->auth_data)){
                sock_recv(cfd,c->auth_data,want,0);
            }else{
                size_t left=want;
                while(left){
                    size_t chunk=(left<sizeof(sink))?left:sizeof(sink);
                    sock_recv(cfd,sink,chunk,0);
                    left-=chunk;
                }
                auth_data_len=(uint16_t)(sizeof(c->auth_data)-1);
            }
            c->auth_data_len=auth_data_len;
            c->auth_data[c->auth_data_len]=0;
        }
    }
    /* Send a standards-shaped setup reply. Real libX11/GTK are picky
     * about the fixed setup, pixmap format, screen, depth, and visual
     * records being at their protocol offsets. */
    {
        uint8_t resp[8];
        uint8_t info[112];
        uint8_t *fmt;
        uint8_t *scr;
        uint8_t *dep;
        uint8_t *vis;
        uint32_t root=0x00000024u;
        uint32_t cmap=0x00000020u;
        uint32_t visual=0x00000021u;
        extern c7_framebuffer_t g_fb;
        c->resource_base=0x00400000u;
        c->next_id=c->resource_base;
        c->root_window=root;
        c->visual=visual;
        c->depth=24;
        c->white_pixel=0x00FFFFFFu;
        c->black_pixel=0x00000000u;
        c->screen_width=(uint16_t)(g_fb.width?g_fb.width:1024u);
        c->screen_height=(uint16_t)(g_fb.height?g_fb.height:768u);

        ulibc_memset(resp,0,sizeof(resp));
        resp[0]=1;/* success */
        x11_wr16(c,resp+2,c->proto_major);
        x11_wr16(c,resp+4,c->proto_minor);
        x11_wr16(c,resp+6,(uint16_t)(sizeof(info)/4u));
        sock_send(cfd,resp,sizeof(resp),0);

        ulibc_memset(info,0,sizeof(info));
        x11_wr32(c,info+0,12000000u);          /* release number */
        x11_wr32(c,info+4,c->resource_base);
        x11_wr32(c,info+8,0x001FFFFFu);        /* resource-id-mask */
        x11_wr32(c,info+12,0);                 /* motion buffer */
        x11_wr16(c,info+16,0);                 /* vendor length */
        x11_wr16(c,info+18,65535);             /* max request len */
        info[20]=1;                            /* screens */
        info[21]=1;                            /* pixmap formats */
        info[22]=x11_is_lsb(c)?0:1;            /* image byte order */
        info[23]=x11_is_lsb(c)?0:1;            /* bitmap bit order */
        info[24]=32;                           /* bitmap scanline unit */
        info[25]=32;                           /* bitmap scanline pad */
        info[26]=8;                            /* min keycode */
        info[27]=255;                          /* max keycode */

        fmt=info+32;
        fmt[0]=24;                             /* depth */
        fmt[1]=32;                             /* bits per pixel */
        fmt[2]=32;                             /* scanline pad */

        scr=info+40;
        x11_wr32(c,scr+0,root);
        x11_wr32(c,scr+4,cmap);
        x11_wr32(c,scr+8,c->white_pixel);
        x11_wr32(c,scr+12,c->black_pixel);
        x11_wr32(c,scr+16,0x00FFFFFFu);        /* current input masks */
        x11_wr16(c,scr+20,c->screen_width);
        x11_wr16(c,scr+22,c->screen_height);
        x11_wr16(c,scr+24,(uint16_t)((uint32_t)c->screen_width*25u/96u));
        x11_wr16(c,scr+26,(uint16_t)((uint32_t)c->screen_height*25u/96u));
        x11_wr16(c,scr+28,1);
        x11_wr16(c,scr+30,1);
        x11_wr32(c,scr+32,visual);
        scr[36]=0;                             /* backing stores */
        scr[37]=0;                             /* save unders */
        scr[38]=24;                            /* root depth */
        scr[39]=1;                             /* allowed depths */

        dep=info+80;
        dep[0]=24;
        x11_wr16(c,dep+2,1);                   /* visuals */

        vis=info+88;
        x11_wr32(c,vis+0,visual);
        vis[4]=4;                              /* TrueColor */
        vis[5]=8;                              /* bits per RGB */
        x11_wr16(c,vis+6,256);
        x11_wr32(c,vis+8,0x00FF0000u);
        x11_wr32(c,vis+12,0x0000FF00u);
        x11_wr32(c,vis+16,0x000000FFu);

        sock_send(cfd,info,sizeof(info),0);
    }
    /* Create root window */
    x11_window_t*rw=&c->windows[0];rw->used=true;rw->id=c->root_window;rw->parent=0;
    rw->width=c->screen_width;rw->height=c->screen_height;rw->depth=c->depth;
    rw->klass=1;rw->visual=c->visual;rw->mapped=true;rw->visible=true;
    c->window_count=1;
    /* Register the real X11 predefined atom IDs before dynamic atoms.
     * GTK/Xlib mix XInternAtom results with XA_* constants, so these
     * IDs must match Xatom.h instead of being allocated sequentially. */
    x11_init_atoms(c);
    x11_trace_setup(c);
    return 0;
}

/* X11 connection setup: send server hello, read client hello */
int x11_accept_connection(int listen_sock_fd){
    uint32_t src_ip;uint16_t src_port;
    int cfd=sock_accept(listen_sock_fd,&src_ip,&src_port);
    x11_connection_t*c;
    if(cfd<0)return-1;
    c=x11_alloc();if(!c){sock_close(cfd);return-1;}
    if(x11_init_stream_connection(c,cfd)<0){
        c->used=false;
        sock_close(cfd);
        return -1;
    }
    return(int)(c-g_x11_conns);
}

int x11_attach_socket(int sock_fd){
    x11_connection_t *c;
    if(sock_fd<0)return -1;
    if(x11_conn_idx_by_sock(sock_fd)>=0)return 0;
    c=x11_alloc();
    if(!c)return -1;
    if(x11_init_stream_connection(c,sock_fd)<0){
        c->used=false;
        return -1;
    }
    return 0;
}

int x11_detach_socket(int sock_fd){
    int idx=x11_conn_idx_by_sock(sock_fd);
    int i;
    x11_connection_t *c;
    if(idx<0)return -1;
    c=&g_x11_conns[idx];
    for(i=0;i<X11_MAX_WINDOWS;++i){
        if(c->windows[i].backing)ulibc_free(c->windows[i].backing);
    }
    for(i=0;i<X11_MAX_PIXMAP;++i){
        if(c->pixmaps[i].backing)ulibc_free(c->pixmaps[i].backing);
    }
    for(i=0;i<X11_MAX_PROPS;++i){
        if(c->props[i].data)ulibc_free(c->props[i].data);
    }
    if(c->req_buf)ulibc_free(c->req_buf);
    ulibc_memset(c,0,sizeof(*c));
    return 0;
}

int x11_process_socket(int sock_fd){
    int idx=x11_conn_idx_by_sock(sock_fd);
    if(idx<0)return -1;
    return x11_process_request(idx);
}

static uint8_t x11_extension_opcode(x11_connection_t *c,const char *name){
    if(!c||!name||!name[0])return 0;
    if(ulibc_strcmp(name,"MIT-SHM")==0)return c->ext_opcode_shm;
    if(ulibc_strcmp(name,"RENDER")==0)return c->ext_opcode_render;
    if(ulibc_strcmp(name,"XFIXES")==0)return c->ext_opcode_xfixes;
    if(ulibc_strcmp(name,"RANDR")==0)return c->ext_opcode_randr;
    /* Keep GTK/Firefox on core X11 input until the XI2 event model is complete. */
    if(ulibc_strcmp(name,"XInputExtension")==0)return 0;
    if(ulibc_strcmp(name,"GLX")==0)return 0;
    if(ulibc_strcmp(name,"BIG-REQUESTS")==0)return c->ext_opcode_bigreq;
    if(ulibc_strcmp(name,"Generic Event Extension")==0)return c->ext_opcode_xge;
    /* Hide Composite/DAMAGE until those extensions are semantically complete.
     * Advertising them made Chromium probe compositor-manager behavior before
     * it ever produced a plain software paint. */
    if(ulibc_strcmp(name,"Composite")==0)return 0;
    if(ulibc_strcmp(name,"DAMAGE")==0)return 0;
    if(ulibc_strcmp(name,"SHAPE")==0)return c->ext_opcode_shape;
    /*
     * Chromium can fall back to core X11 keyboard handling. Keep XKEYBOARD
     * hidden until our XKB GetMap replies are complete enough for xcb's parser.
     */
    if(ulibc_strcmp(name,"XKEYBOARD")==0)return 0;
    if(ulibc_strcmp(name,"SYNC")==0)return c->ext_opcode_sync;
    return 0;
}

static int x11_handle_extension_request(x11_connection_t *c,uint8_t opcode,uint8_t minor,const uint8_t *body,size_t body_len){
    uint8_t reply[32];
    if(!c)return -1;
    x11_reply_init(c,reply,sizeof(reply),0);
    if(opcode==c->ext_opcode_shm){
        x11_trace_extension(c,"MIT-SHM",minor,body_len,"handle");
        if(minor==0){ /* QueryVersion */
            reply[1]=1;   /* shared pixmaps */
            x11_wr16(c,reply+8,1);   /* major version */
            x11_wr16(c,reply+10,2);  /* minor version */
            sock_send(c->sock_fd,reply,sizeof(reply),0);
        }else if(minor==1&&body_len>=12){ /* Attach */
            uint32_t shmseg=x11_rd32(c,body+0);
            int shmid=(int)x11_rd32(c,body+4);
            bool ro=(body[8]!=0);
            x11_shmseg_t*seg=x11_find_shmseg(c,shmseg);
            if(!seg){
                int i;for(i=0;i<X11_MAX_SHMSEG;++i)if(!c->shmsegs[i].used){seg=&c->shmsegs[i];break;}
            }
            if(seg){
                seg->used=true;seg->id=shmseg;seg->shmid=shmid;seg->read_only=ro;
                ++c->shmseg_count;
            }
        }else if(minor==2&&body_len>=4){ /* Detach */
            uint32_t shmseg=x11_rd32(c,body+0);
            x11_shmseg_t*seg=x11_find_shmseg(c,shmseg);
            if(seg){seg->used=false;seg->id=0;seg->shmid=-1;seg->read_only=false;}
        }else if(minor==3&&body_len>=36){ /* PutImage */
            uint32_t drawable=x11_rd32(c,body+0);
            uint16_t total_w=x11_rd16(c,body+8);
            uint16_t total_h=x11_rd16(c,body+10);
            uint16_t src_x=x11_rd16(c,body+12);
            uint16_t src_y=x11_rd16(c,body+14);
            uint16_t src_w=x11_rd16(c,body+16);
            uint16_t src_h=x11_rd16(c,body+18);
            int16_t dst_x=(int16_t)x11_rd16(c,body+20);
            int16_t dst_y=(int16_t)x11_rd16(c,body+22);
            uint8_t send_event=body[26];
            uint32_t shmseg=x11_rd32(c,body+28);
            uint32_t offset=x11_rd32(c,body+32);
            x11_shmseg_t*seg=x11_find_shmseg(c,shmseg);
            uint8_t *dst_backing=0;
            uint32_t dst_size=0;
            int dst_w=0,dst_h=0;
            x11_trace_window_op(c,"shm-putimage",drawable,(int)src_w,(int)src_h);
            if(seg&&seg->shmid>=0&&seg->shmid<SHM_MAX&&g_shm_regions[seg->shmid].used){
                size_t stride=(size_t)(total_w?total_w:src_w)*4;
                size_t seg_size=(size_t)g_shm_regions[seg->shmid].size;
                size_t src_start=(size_t)offset;
                size_t last_row;
                size_t needed;
                const uint8_t*src=0;
                (void)total_h;
                last_row=(size_t)src_y+(src_h?((size_t)src_h-1u):0u);
                needed=last_row*stride+((size_t)src_x+(size_t)src_w)*4u;
                if(stride&&src_start<seg_size&&needed<=seg_size-src_start)
                    src=(const uint8_t*)PHYS_TO_DMAP(g_shm_regions[seg->shmid].phys+src_start);
                if(x11_drawable_surface(c,drawable,&dst_backing,&dst_size,&dst_w,&dst_h)){
                    if(src){
                        x11_blit_argb32_buf(dst_backing,dst_size,dst_w,dst_h,dst_x,dst_y,src,stride,src_x,src_y,src_w,src_h);
                        g_x11_render_dirty=true;
                    }
                }
                if(send_event){
                    x11_event_t ev;ulibc_memset(&ev,0,sizeof(ev));
                    ev.type=64; /* MIT-SHM completion event base */
                    ((uint32_t*)ev.pad)[0]=drawable;
                    x11_push_event(c,&ev);
                }
            }
        }else{
            x11_trace_extension(c,"MIT-SHM",minor,body_len,"missing");
        }
        return 0;
    }
    if(opcode==c->ext_opcode_render){
        x11_trace_extension(c,"RENDER",minor,body_len,"handle");
        if(minor==0){ /* QueryVersion */
            x11_wr32(c,reply+8,0);   /* major */
            x11_wr32(c,reply+12,11); /* minor */
            sock_send(c->sock_fd,reply,sizeof(reply),0);
        }else if(minor==1){ /* QueryPictFormats */
            x11_send_render_pict_formats(c);
        }else if(minor==2||minor==29){ /* QueryPictIndexValues / QueryFilters */
            sock_send(c->sock_fd,reply,sizeof(reply),0);
        }else if(minor==17||minor==18||minor==19||minor==20||minor==22){ /* GlyphSet bookkeeping: accepted as void. */
        }else if(minor==23||minor==24||minor==25){ /* CompositeGlyphs: text can be repainted by client surfaces. */
        }else if(minor==26){ /* FillRectangles */
            x11_render_fill_rectangles(c,body,body_len);
        }else if(minor==4&&body_len>=12){ /* CreatePicture */
            uint32_t pid=x11_rd32(c,body+0);
            uint32_t drawable=x11_rd32(c,body+4);
            uint32_t format=x11_rd32(c,body+8);
            x11_picture_t*p=x11_find_picture(c,pid);
            if(!p){int i;for(i=0;i<X11_MAX_PICTURE;++i)if(!c->pictures[i].used){p=&c->pictures[i];break;}}
            if(p){p->used=true;p->id=pid;p->drawable=drawable;p->format=format;++c->picture_count;}
        }else if(minor==7&&body_len>=4){ /* FreePicture */
            uint32_t pid=x11_rd32(c,body+0);
            x11_picture_t*p=x11_find_picture(c,pid);
            if(p){p->used=false;p->id=0;p->drawable=0;p->format=0;}
        }else if(minor==8&&body_len>=28){ /* Composite */
            uint32_t src_pic=x11_rd32(c,body+0);
            uint32_t dst_pic=x11_rd32(c,body+8);
            int16_t src_x=(int16_t)x11_rd16(c,body+12);
            int16_t src_y=(int16_t)x11_rd16(c,body+14);
            int16_t dst_x=(int16_t)x11_rd16(c,body+20);
            int16_t dst_y=(int16_t)x11_rd16(c,body+22);
            uint16_t w=x11_rd16(c,body+24);
            uint16_t h=x11_rd16(c,body+26);
            x11_picture_t*sp=x11_find_picture(c,src_pic);
            x11_picture_t*dp=x11_find_picture(c,dst_pic);
            if(dp)x11_trace_window_op(c,"render-composite",dp->drawable,(int)w,(int)h);
            if(sp&&dp){
                uint8_t *src_backing=0,*dst_backing=0;
                uint32_t src_size=0,dst_size=0;
                int src_wi=0,src_hi=0,dst_wi=0,dst_hi=0;
                if(x11_drawable_surface(c,sp->drawable,&src_backing,&src_size,&src_wi,&src_hi)&&
                   x11_drawable_surface(c,dp->drawable,&dst_backing,&dst_size,&dst_wi,&dst_hi)){
                    (void)src_size;
                    (void)src_hi;
                    x11_blit_argb32_buf(dst_backing,dst_size,dst_wi,dst_hi,dst_x,dst_y,src_backing,(size_t)src_wi*4,src_x,src_y,w,h);
                    g_x11_render_dirty=true;
                }
            }
        }else{
            x11_trace_extension(c,"RENDER",minor,body_len,"missing");
        }
        return 0;
    }
    if(opcode==c->ext_opcode_xfixes){
        x11_trace_extension(c,"XFIXES",minor,body_len,"handle");
        if(minor==0){
            x11_wr32(c,reply+8,5); /* major */
            x11_wr32(c,reply+12,0); /* minor */
            sock_send(c->sock_fd,reply,sizeof(reply),0);
        }else if(minor==2){
            if(body_len>=12){
                uint32_t window=x11_rd32(c,body+0);
                uint32_t selection=x11_rd32(c,body+4);
                uint32_t event_mask=x11_rd32(c,body+8);
                const char *sel_name=x11_atom_name_by_id(c,selection);
                uint32_t owner=x11_selection_owner_for_name(c,sel_name);
                x11_trace_atom(c,"xfixes-select",selection,sel_name,window);
                (void)event_mask;
                if(ulibc_strcmp(sel_name,"_NET_WM_CM_S0")==0){
                    c->xfixes_cm_window=window?window:c->root_window;
                    c->xfixes_cm_selection=selection;
                    c->xfixes_cm_notified=false;
                }
                (void)owner;
            }
        }else if(minor==3){
            /* SelectCursorInput is a void request. */
        }else if(minor==4){ /* GetCursorImage */
            sock_send(c->sock_fd,reply,sizeof(reply),0);
        }else{
            x11_trace_extension(c,"XFIXES",minor,body_len,"missing");
        }
        return 0;
    }
    if(opcode==c->ext_opcode_composite){
        x11_trace_extension(c,"Composite",minor,body_len,"handle");
        if(minor==0){
            x11_wr32(c,reply+8,0); /* major */
            x11_wr32(c,reply+12,4); /* minor */
            sock_send(c->sock_fd,reply,sizeof(reply),0);
        }else{
            /* Redirect/Unredirect/overlay requests can be treated as no-ops
             * by this in-kernel compositor because every window is already
             * backed by our framebuffer-side surface. */
        }
        return 0;
    }
    if(opcode==c->ext_opcode_damage){
        x11_trace_extension(c,"DAMAGE",minor,body_len,"handle");
        if(minor==0){ /* QueryVersion */
            x11_wr32(c,reply+8,1);
            x11_wr32(c,reply+12,1);
            sock_send(c->sock_fd,reply,sizeof(reply),0);
        }else if(minor==1||minor==2||minor==3||minor==4){
            /* Create/Destroy/Subtract/Add: damage tracking is implicit in our
             * backing-store compositor, so these are accepted as no-ops. */
        }else{
            x11_trace_extension(c,"DAMAGE",minor,body_len,"missing");
        }
        return 0;
    }
    if(opcode==c->ext_opcode_shape){
        x11_trace_extension(c,"SHAPE",minor,body_len,"handle");
        if(minor==0){ /* QueryVersion */
            x11_wr16(c,reply+8,1);
            x11_wr16(c,reply+10,1);
            sock_send(c->sock_fd,reply,sizeof(reply),0);
        }else if(minor==5){ /* QueryExtents */
            reply[1]=0; /* not shaped */
            sock_send(c->sock_fd,reply,sizeof(reply),0);
        }else if(minor==7){ /* InputSelected */
            reply[1]=0;
            sock_send(c->sock_fd,reply,sizeof(reply),0);
        }else if(minor==8){ /* GetRectangles */
            x11_reply_set_length(c,reply,0);
            x11_wr32(c,reply+8,0);
            sock_send(c->sock_fd,reply,sizeof(reply),0);
        }else if(minor==1||minor==2||minor==3||minor==4||minor==6){
            /* Rectangles/Mask/Combine/Offset/SelectInput are void requests. */
        }else{
            x11_trace_extension(c,"SHAPE",minor,body_len,"missing");
        }
        return 0;
    }
    if(opcode==c->ext_opcode_xkeyboard){
        x11_trace_extension(c,"XKEYBOARD",minor,body_len,"handle");
        if(minor==0){ /* UseExtension */
            reply[1]=1; /* supported */
            x11_wr16(c,reply+8,1);
            x11_wr16(c,reply+10,0);
            sock_send(c->sock_fd,reply,sizeof(reply),0);
        }else if(minor==3){ /* GetState */
            reply[1]=0; /* group */
            sock_send(c->sock_fd,reply,sizeof(reply),0);
        }else if(minor==5||minor==7||minor==8||minor==9||minor==11||minor==12||
                 minor==14||minor==16||minor==18||minor==19||minor==20||minor==21){
            /* Query-style XKB requests: empty-but-valid replies keep libX11
             * moving until full keyboard maps are wired. */
            if(minor==8){ /* GetMap: no components, no variable-length body. */
                reply[1]=0; /* device id */
                x11_wr16(c,reply+8,0); /* present */
            }
            sock_send(c->sock_fd,reply,sizeof(reply),0);
        }else if(minor==1||minor==2||minor==4||minor==6||minor==10||
                 minor==13||minor==15||minor==17||minor==22){
            /* Void XKB requests. */
        }else{
            x11_trace_extension(c,"XKEYBOARD",minor,body_len,"missing");
        }
        return 0;
    }
    if(opcode==c->ext_opcode_sync){
        x11_trace_extension(c,"SYNC",minor,body_len,"handle");
        if(minor==0){ /* Initialize */
            x11_wr16(c,reply+8,3);
            x11_wr16(c,reply+10,1);
            sock_send(c->sock_fd,reply,sizeof(reply),0);
        }else if(minor==1){ /* ListSystemCounters */
            x11_reply_set_length(c,reply,0);
            x11_wr32(c,reply+8,0);
            sock_send(c->sock_fd,reply,sizeof(reply),0);
        }else if(minor==5||minor==10||minor==13){
            sock_send(c->sock_fd,reply,sizeof(reply),0);
        }else if(minor==2||minor==3||minor==4||minor==6||minor==7||minor==8||
                 minor==9||minor==11||minor==12){
            /* Counter/alarm mutating requests are accepted as no-ops. */
        }else{
            x11_trace_extension(c,"SYNC",minor,body_len,"missing");
        }
        return 0;
    }
    if(opcode==c->ext_opcode_randr){
        x11_trace_extension(c,"RANDR",minor,body_len,"handle");
        if(minor==0){
            x11_wr32(c,reply+8,1); /* major */
            x11_wr32(c,reply+12,5); /* minor */
            sock_send(c->sock_fd,reply,sizeof(reply),0);
        }else if(minor==6){ /* GetScreenSizeRange */
            x11_wr16(c,reply+8,64);
            x11_wr16(c,reply+10,64);
            x11_wr16(c,reply+12,c->screen_width);
            x11_wr16(c,reply+14,c->screen_height);
            sock_send(c->sock_fd,reply,sizeof(reply),0);
        }else if(minor==42){ /* GetMonitors */
            uint8_t mon[24];
            uint32_t mmw=(uint32_t)(((uint32_t)c->screen_width*254u)/960u);
            uint32_t mmh=(uint32_t)(((uint32_t)c->screen_height*254u)/960u);
            if(mmw<1u)mmw=1u;
            if(mmh<1u)mmh=1u;
            x11_wr32(c,reply+4,6);  /* one xRRMonitorInfo */
            x11_wr32(c,reply+8,1);  /* timestamp */
            x11_wr32(c,reply+12,1); /* nmonitors */
            x11_wr32(c,reply+16,0); /* noutputs */
            ulibc_memset(mon,0,sizeof(mon));
            x11_wr32(c,mon+0,0); /* atom name */
            mon[4]=1;            /* primary */
            mon[5]=1;            /* automatic */
            x11_wr16(c,mon+6,0); /* noutput */
            x11_wr16(c,mon+8,0);
            x11_wr16(c,mon+10,0);
            x11_wr16(c,mon+12,c->screen_width);
            x11_wr16(c,mon+14,c->screen_height);
            x11_wr32(c,mon+16,mmw);
            x11_wr32(c,mon+20,mmh);
            sock_send(c->sock_fd,reply,sizeof(reply),0);
            sock_send(c->sock_fd,mon,sizeof(mon),0);
        }else if(minor==1||minor==2||minor==5||minor==8||minor==9||minor==10||
                 minor==11||minor==15||minor==16||minor==20||minor==21||minor==22||
                 minor==23||minor==25||minor==27||minor==28||minor==29||minor==31||
                 minor==32||minor==33||minor==36||minor==37||minor==41||minor==45){
            /* Common RR queries: return empty-but-valid reply payload. */
            sock_send(c->sock_fd,reply,sizeof(reply),0);
        }else if(minor==3||minor==4||minor==7||minor==12||minor==13||minor==14||
                 minor==17||minor==18||minor==19||minor==24||minor==26||minor==30||
                 minor==34||minor==35||minor==38||minor==39||minor==40||minor==43||
                 minor==44||minor==46){
            /* Void RR requests: accepted without reply. */
        }else{
            x11_trace_extension(c,"RANDR",minor,body_len,"missing");
        }
        return 0;
    }
    if(opcode==c->ext_opcode_xinput){
        x11_trace_extension(c,"XInputExtension",minor,body_len,"handle");
        if(minor==47){ /* XIQueryVersion */
            x11_wr16(c,reply+8,2); /* XI2 major */
            x11_wr16(c,reply+10,3); /* XI2 minor */
            sock_send(c->sock_fd,reply,sizeof(reply),0);
        }else if(minor==48){ /* XIQueryDevice */
            sock_send(c->sock_fd,reply,sizeof(reply),0);
        }else if(minor==46){ /* XISelectEvents */
            /* Stored implicitly by core event queue wiring. */
        }else{
            x11_trace_extension(c,"XInputExtension",minor,body_len,"missing");
        }
        return 0;
    }
    if(opcode==c->ext_opcode_glx){
        x11_trace_extension(c,"GLX",minor,body_len,"handle");
        if(minor==7){ /* QueryVersion */
            x11_wr32(c,reply+8,1); /* GLX major */
            x11_wr32(c,reply+12,4); /* GLX minor */
            sock_send(c->sock_fd,reply,sizeof(reply),0);
        }else if(minor==5){ /* MakeCurrent */
            x11_wr32(c,reply+8,1); /* context tag */
            sock_send(c->sock_fd,reply,sizeof(reply),0);
        }else if(minor==18){ /* QueryExtensionsString */
            x11_send_glx_string_reply(c,"GLX_EXT_visual_info GLX_EXT_visual_rating GLX_ARB_create_context GLX_ARB_create_context_profile");
        }else if(minor==19){ /* QueryServerString */
            uint32_t name=body_len>=8?x11_rd32(c,body+4):0;
            if(name==1) x11_send_glx_string_reply(c,"Ridux GLX");
            else if(name==2) x11_send_glx_string_reply(c,"1.4");
            else if(name==3) x11_send_glx_string_reply(c,"GLX_EXT_visual_info GLX_EXT_visual_rating GLX_ARB_create_context GLX_ARB_create_context_profile");
            else x11_send_glx_string_reply(c,"");
        }else if(minor==17){ /* VendorPrivateWithReply */
            sock_send(c->sock_fd,reply,sizeof(reply),0);
        }else if(minor==3||minor==11||minor==21||minor==24){
            /* CreateContext / SwapBuffers / CreateNewContext / DestroyContext: no reply */
        }else{
            x11_trace_extension(c,"GLX",minor,body_len,"missing");
        }
        return 0;
    }
    if(opcode==c->ext_opcode_bigreq||(opcode==0&&minor==0&&body_len==0)){
        x11_trace_extension(c,"BIG-REQUESTS",minor,body_len,"handle");
        if(minor==0){ /* Enable */
            x11_wr32(c,reply+8,0x00100000u);
            sock_send(c->sock_fd,reply,sizeof(reply),0);
        }else{
            x11_trace_extension(c,"BIG-REQUESTS",minor,body_len,"missing");
        }
        return 0;
    }
    if(opcode==c->ext_opcode_xge||(opcode==0&&minor==0&&(body_len==4||body_len==8))){
        x11_trace_extension(c,"Generic Event Extension",minor,body_len,"handle");
        if(minor==0){ /* QueryVersion */
            x11_wr16(c,reply+8,1);
            x11_wr16(c,reply+10,0);
            sock_send(c->sock_fd,reply,sizeof(reply),0);
        }else{
            x11_trace_extension(c,"Generic Event Extension",minor,body_len,"missing");
        }
        return 0;
    }
    return -1;
}

static int x11_reqbuf_reserve(x11_connection_t *c,uint32_t need){
    uint8_t *nb;
    uint32_t nc;
    if(!c)return -EINVAL;
    if(need<=c->req_cap)return 0;
    if(need>X11_MAX_REQUEST_BYTES)return -E2BIG;
    nc=c->req_cap?c->req_cap:4096u;
    while(nc<need){
        if(nc>X11_MAX_REQUEST_BYTES/2u){
            nc=X11_MAX_REQUEST_BYTES;
            break;
        }
        nc*=2u;
    }
    if(nc<need)return -E2BIG;
    nb=(uint8_t*)ulibc_malloc(nc);
    if(!nb)return -ENOMEM;
    if(c->req_buf&&c->req_len)ulibc_memcpy(nb,c->req_buf,c->req_len);
    if(c->req_buf)ulibc_free(c->req_buf);
    c->req_buf=nb;
    c->req_cap=nc;
    return 0;
}

static void x11_reqbuf_drop(x11_connection_t *c,uint32_t n){
    uint32_t i,left;
    if(!c||!c->req_buf||n==0)return;
    if(n>=c->req_len){c->req_len=0;return;}
    left=c->req_len-n;
    for(i=0;i<left;++i)c->req_buf[i]=c->req_buf[n+i];
    c->req_len=left;
}

static int x11_pull_socket_rx(x11_connection_t *c){
    socket_t *s;
    uint16_t used;
    int rc;
    if(!c||c->sock_fd<0||c->sock_fd>=SOCK_MAX)return -EINVAL;
    s=&g_sockets[c->sock_fd];
    if(!s->used)return -ENOTCONN;
    used=(uint16_t)(s->rx_head-s->rx_tail);
    if(!used)return 0;
    if((uint32_t)c->req_len+(uint32_t)used>2u*1024u*1024u)return -E2BIG;
    rc=x11_reqbuf_reserve(c,c->req_len+(uint32_t)used);
    if(rc<0)return rc;
    while(used){
        uint16_t chunk=used;
        rc=sock_recv(c->sock_fd,c->req_buf+c->req_len,chunk,0);
        if(rc<=0)break;
        c->req_len+=(uint32_t)rc;
        used=(uint16_t)(used-(uint16_t)rc);
    }
    return 0;
}

/* Process one X11 request from client */
int x11_process_request(int conn_idx){
    if(conn_idx<0||conn_idx>=X11_MAX_CONN)return-1;
    x11_connection_t*c=&g_x11_conns[conn_idx];if(!c->used)return-1;
    uint8_t req_hdr[4];
    uint8_t stack_body[4096];
    uint8_t *body=stack_body;
    bool body_heap=false;
    int rc=x11_pull_socket_rx(c);
    size_t total_len;
    size_t body_offset;
    if(rc<0)return rc;
    if(c->req_len<4)return-1;
    ulibc_memcpy(req_hdr,c->req_buf,4);
    uint8_t opcode=req_hdr[0];
    uint8_t extra=req_hdr[1];/* often unused or param */
    uint16_t req_len=x11_rd16(c,req_hdr+2);/* in 4-byte units */
    uint32_t req_words=req_len;
    body_offset=4u;
    if(req_len==0){
        if(c->req_len<8)return -1;
        req_words=x11_rd32(c,c->req_buf+4);
        body_offset=8u;
        if(req_words<2u){
            x11_reqbuf_drop(c,4);
            return -EINVAL;
        }
    }
    if(req_words==0)req_words=1;
    total_len=(size_t)req_words*4u;
    if(total_len<body_offset){
        x11_reqbuf_drop(c,4);
        return -EINVAL;
    }
    size_t body_len=total_len-body_offset;
    if(total_len>X11_MAX_REQUEST_BYTES||body_len>X11_MAX_REQUEST_BYTES-body_offset){
        x11_reqbuf_drop(c,4);
        return -E2BIG;
    }
    if(c->req_len<total_len)return-1;
    if(body_len>0){
        if(body_len>sizeof(stack_body)){
            body=(uint8_t*)ulibc_malloc(body_len);
            if(!body)return -ENOMEM;
            body_heap=true;
        }
        ulibc_memcpy(body,c->req_buf+body_offset,body_len);
    }
    x11_reqbuf_drop(c,(uint32_t)total_len);
    c->sequence++;
    #define X11_U16(_p) x11_rd16(c,(_p))
    #define X11_U32(_p) x11_rd32(c,(_p))
    #define X11_W16(_p,_v) x11_wr16(c,(_p),(_v))
    #define X11_W32(_p,_v) x11_wr32(c,(_p),(_v))
    #define X11_RET(_v) do{if(body_heap)ulibc_free(body);return (_v);}while(0)
    x11_trace_request(c,opcode,extra,req_len,body_len);
    if(x11_handle_extension_request(c,opcode,extra,body,body_len)==0)X11_RET(0);
    switch(opcode){
    case X11_CreateWindow:{
        if(body_len<28)break;
        uint32_t wid=X11_U32(body+0);
        uint32_t parent=X11_U32(body+4);
        int16_t x=(int16_t)X11_U16(body+8);
        int16_t y=(int16_t)X11_U16(body+10);
        uint16_t w=X11_U16(body+12);
        uint16_t h=X11_U16(body+14);
        uint16_t border=X11_U16(body+16);
        uint16_t klass=X11_U16(body+18);
        uint32_t visual=X11_U32(body+20);
        x11_window_t *parent_w=x11_find_window(c,parent);
        x11_trace_window_op(c,"create",wid,(int)w,(int)h);
        x11_window_t*wn=x11_find_window(c,wid);if(!wn){
            int i;for(i=0;i<X11_MAX_WINDOWS;++i)if(!c->windows[i].used){wn=&c->windows[i];break;}
            if(!wn)break;}
        if(wn->backing){ulibc_free(wn->backing);wn->backing=0;wn->backing_size=0;}
        wn->used=true;wn->id=wid;wn->parent=parent;wn->x=x;wn->y=y;
        wn->width=w;wn->height=h;wn->border_width=border;
        wn->depth=extra?extra:(parent_w?parent_w->depth:c->depth);
        wn->klass=klass?klass:(parent_w?parent_w->klass:1);
        wn->visual=visual?visual:(parent_w?parent_w->visual:c->visual);
        wn->background_pixel=0xFF333366;/* default bg */
        wn->mapped=false;wn->visible=false;wn->event_mask=0;
        if(w&&h&&((uint32_t)w*(uint32_t)h)<=0x1000000u){
            wn->backing_size=(uint32_t)w*(uint32_t)h*4u;
            wn->backing=(uint8_t*)ulibc_malloc(wn->backing_size);
        }else{
            wn->backing_size=0;
            wn->backing=0;
        }
        if(wn->backing)ulibc_memset(wn->backing,0,wn->backing_size);
        c->window_count++;
        g_x11_render_dirty=true;
        x11_auto_map_if_client_window(c,wn);
        break;}
    case X11_ChangeWindowAttributes:{
        if(body_len<8)break;
        {
            uint32_t wid=X11_U32(body+0);
            uint32_t mask=X11_U32(body+4);
            x11_window_t *wn=x11_find_window(c,wid);
            size_t pos=8;
            int bit;
            if(!wn)break;
            for(bit=0;bit<32&&pos+4<=body_len;++bit){
                uint32_t v;
                if((mask&(1u<<bit))==0)continue;
                v=X11_U32(body+pos);
                if(bit==1)wn->background_pixel=v;
                else if(bit==11)wn->event_mask=v;
                g_x11_render_dirty=true;
                pos+=4;
            }
        }
        break;}
    case X11_DestroyWindow:{
        if(body_len<4)break;
        {
            uint32_t wid=X11_U32(body+0);
            x11_window_t*wn=x11_find_window(c,wid);
            if(wn){
                if(wn->backing)ulibc_free(wn->backing);
                x11_free_window_props(c,wid);
                ulibc_memset(wn,0,sizeof(*wn));
                if(c->window_count>0)--c->window_count;
                g_x11_render_dirty=true;
            }
        }
        break;}
    case X11_MapWindow:{
        uint32_t wid=0;
        if(body_len>=4)wid=X11_U32(body+0);
        x11_trace_window_op(c,"map",wid,-1,-1);
        x11_window_t*wn=x11_find_window(c,wid);
        if(wn){wn->mapped=true;wn->visible=true;g_x11_render_dirty=true;
            /* Push MapNotify + Expose so simple X11 clients draw without
             * waiting for a full window manager round trip. */
            x11_push_map_notify_event(c,wid);
            x11_push_expose((int)(c-g_x11_conns),wid,0,0,wn->width,wn->height);}
        break;}
    case X11_UnmapWindow:{
        if(body_len<4)break;
        {
            uint32_t wid=X11_U32(body+0);
            x11_window_t*wn=x11_find_window(c,wid);
            if(wn){wn->mapped=false;wn->visible=false;g_x11_render_dirty=true;}
        }
        break;}
    case X11_MapSubwindows:{
        if(body_len<4)break;
        {
            uint32_t parent=X11_U32(body+0);
            int i;
            for(i=0;i<X11_MAX_WINDOWS;++i){
                if(!c->windows[i].used||c->windows[i].parent!=parent)continue;
                c->windows[i].mapped=true;
                c->windows[i].visible=true;
                g_x11_render_dirty=true;
            }
        }
        break;}
    case X11_ReparentWindow:{
        if(body_len<12)break;
        {
            uint32_t wid=X11_U32(body+0);
            uint32_t parent=X11_U32(body+4);
            int16_t x=(int16_t)X11_U16(body+8);
            int16_t y=(int16_t)X11_U16(body+10);
            x11_window_t *wn=x11_find_window(c,wid);
            if(wn){
                wn->parent=parent;
                wn->x=x;
                wn->y=y;
                g_x11_render_dirty=true;
            }
        }
        break;}
    case X11_ConfigureWindow:{
        if(body_len<8)break;
        {
            uint32_t wid=X11_U32(body+0);
            uint16_t mask=X11_U16(body+4);
            x11_window_t *wn=x11_find_window(c,wid);
            size_t pos=8;
            int bit;
            int old_w,old_h;
            if(!wn)break;
            old_w=wn->width;
            old_h=wn->height;
            for(bit=0;bit<16&&pos+4<=body_len;++bit){
                if((mask&(1u<<bit))==0)continue;
                {
                    uint32_t v=X11_U32(body+pos);
                    if(bit==0)wn->x=(int32_t)v;
                    else if(bit==1)wn->y=(int32_t)v;
                    else if(bit==2)wn->width=(int32_t)v;
                    else if(bit==3)wn->height=(int32_t)v;
                    else if(bit==4)wn->border_width=(int32_t)v;
                    g_x11_render_dirty=true;
                    pos+=4;
                }
            }
            if(wn->width!=old_w||wn->height!=old_h)
                x11_resize_window_backing(wn,old_w,old_h);
            x11_auto_map_if_client_window(c,wn);
        }
        break;}
    case X11_CreatePixmap:{
        if(body_len<12)break;
        {
            uint32_t pid=X11_U32(body+0);
            uint32_t drawable=X11_U32(body+4);
            uint16_t w=X11_U16(body+8);
            uint16_t h=X11_U16(body+10);
            x11_pixmap_t *pm=x11_find_pixmap(c,pid);
            if(!pm){
                int i;
                for(i=0;i<X11_MAX_PIXMAP;++i)if(!c->pixmaps[i].used){pm=&c->pixmaps[i];break;}
            }
            if(pm){
                if(pm->backing){ulibc_free(pm->backing);pm->backing=0;pm->backing_size=0;}
                pm->used=true;
                pm->id=pid;
                pm->drawable=drawable;
                pm->depth=extra;
                pm->width=w;
                pm->height=h;
                if(w&&h&&((uint32_t)w*(uint32_t)h)<=0x1000000u){
                    pm->backing_size=(uint32_t)w*(uint32_t)h*4u;
                    pm->backing=(uint8_t*)ulibc_malloc(pm->backing_size);
                    if(pm->backing)ulibc_memset(pm->backing,0,pm->backing_size);
                }
                ++c->pixmap_count;
            }
        }
        break;}
    case X11_FreePixmap:{
        if(body_len<4)break;
        {
            uint32_t pid=X11_U32(body+0);
            x11_pixmap_t*pm=x11_find_pixmap(c,pid);
            if(pm){
                if(pm->backing)ulibc_free(pm->backing);
                ulibc_memset(pm,0,sizeof(*pm));
                if(c->pixmap_count>0)--c->pixmap_count;
            }
        }
        break;}
    case X11_CreateGC:{
        if(body_len<12)break;
        uint32_t cid=X11_U32(body+0);
        uint32_t drawable=X11_U32(body+4);
        x11_gc_t*gc=x11_find_gc(c,cid);if(!gc){
            int i;for(i=0;i<X11_MAX_GC;++i)if(!c->gcs[i].used){gc=&c->gcs[i];break;}
            if(!gc)break;}
        gc->used=true;gc->id=cid;gc->drawable=drawable;
        gc->fg_pixel=0xFFFFFFFF;gc->bg_pixel=0;
        gc->function=3;/* GXCopy */gc->line_width=0;
        c->gc_count++;
        break;}
    case X11_ChangeGC:{
        if(body_len<8)break;
        {
            uint32_t cid=X11_U32(body+0);
            uint32_t mask=X11_U32(body+4);
            x11_gc_t *gc=x11_find_gc(c,cid);
            size_t pos=8;
            int bit;
            if(!gc)break;
            for(bit=0;bit<24&&pos+4<=body_len;++bit){
                uint32_t v;
                if((mask&(1u<<bit))==0)continue;
                v=X11_U32(body+pos);
                if(bit==0)gc->function=(int)v;
                else if(bit==2)gc->fg_pixel=v;
                else if(bit==3)gc->bg_pixel=v;
                else if(bit==4)gc->line_width=(int)v;
                pos+=4;
            }
        }
        break;}
    case X11_SetClipRectangles:
        break; /* clipping is ignored by the minimal compositor */
    case X11_FreeGC:{
        if(body_len<4)break;
        {
            uint32_t cid=X11_U32(body+0);
            x11_gc_t *gc=x11_find_gc(c,cid);
            if(gc){
                ulibc_memset(gc,0,sizeof(*gc));
                if(c->gc_count>0)--c->gc_count;
            }
        }
        break;}
    case X11_CopyArea:{
        if(body_len<24)break;
        {
            uint32_t src_drawable=X11_U32(body+0);
            uint32_t dst_drawable=X11_U32(body+4);
            int16_t src_x=(int16_t)X11_U16(body+12);
            int16_t src_y=(int16_t)X11_U16(body+14);
            int16_t dst_x=(int16_t)X11_U16(body+16);
            int16_t dst_y=(int16_t)X11_U16(body+18);
            uint16_t w=X11_U16(body+20);
            uint16_t h=X11_U16(body+22);
            uint8_t *src_backing=0,*dst_backing=0;
            uint32_t src_size=0,dst_size=0;
            int src_w=0,src_h=0,dst_w=0,dst_h=0;
            if(x11_drawable_surface(c,src_drawable,&src_backing,&src_size,&src_w,&src_h)&&
               x11_drawable_surface(c,dst_drawable,&dst_backing,&dst_size,&dst_w,&dst_h)){
                (void)src_size;
                (void)src_h;
                x11_blit_argb32_buf(dst_backing,dst_size,dst_w,dst_h,dst_x,dst_y,src_backing,(size_t)src_w*4,src_x,src_y,w,h);
                g_x11_render_dirty=true;
            }
        }
        break;}
    case X11_PolyFillRectangle:{
        if(body_len<8)break;
        {
            uint32_t drawable=X11_U32(body+0);
            uint32_t gcid=X11_U32(body+4);
            x11_gc_t *gc=x11_find_gc(c,gcid);
            uint32_t color=gc?gc->fg_pixel:0xFFFFFFFFu;
            uint8_t *dst_backing=0;
            uint32_t dst_size=0;
            int dst_w=0,dst_h=0;
            size_t pos=8;
            if(!x11_drawable_surface(c,drawable,&dst_backing,&dst_size,&dst_w,&dst_h))break;
            while(pos+8<=body_len){
                int16_t rx=(int16_t)X11_U16(body+pos);
                int16_t ry=(int16_t)X11_U16(body+pos+2);
                uint16_t rw=X11_U16(body+pos+4);
                uint16_t rh=X11_U16(body+pos+6);
                x11_fill_rect_argb32(dst_backing,dst_size,dst_w,dst_h,rx,ry,rw,rh,color);
                pos+=8;
            }
            g_x11_render_dirty=true;
        }
        break;}
    case X11_PutImage:{
        if(body_len<24)break;
        uint32_t drawable=X11_U32(body+0);
        uint32_t gcid=X11_U32(body+4);
        uint16_t w=X11_U16(body+8);
        uint16_t h=X11_U16(body+10);
        int16_t dst_x=(int16_t)X11_U16(body+12);
        int16_t dst_y=(int16_t)X11_U16(body+14);
        uint8_t format=body[16];
        x11_trace_window_op(c,"putimage",drawable,(int)w,(int)h);
        /* Image data starts at byte 24 */
        uint8_t*img_data=body+24;
        size_t img_len=body_len-24;
        /* Find window and blit to backing store */
        uint8_t *dst_backing=0;
        uint32_t dst_size=0;
        int dst_w=0,dst_h=0;
        if(img_len>0&&x11_drawable_surface(c,drawable,&dst_backing,&dst_size,&dst_w,&dst_h)){
            size_t stride=(size_t)w*4;
            (void)gcid;
            (void)format;
            if(stride>0&&img_len>=stride*(size_t)h){
                x11_blit_argb32_buf(dst_backing,dst_size,dst_w,dst_h,dst_x,dst_y,img_data,stride,0,0,(int)w,(int)h);
                g_x11_render_dirty=true;
            }
        }
        break;}
    case X11_GetImage:{
        if(body_len<12)break;
        {
            uint32_t drawable=X11_U32(body+0);
            int16_t src_x=(int16_t)X11_U16(body+4);
            int16_t src_y=(int16_t)X11_U16(body+6);
            uint16_t w=X11_U16(body+8);
            uint16_t h=X11_U16(body+10);
            uint8_t *src_backing=0;
            uint32_t src_size=0;
            int src_w=0,src_h=0;
            uint8_t reply[32];
            uint8_t *img=0;
            size_t data_len,pad_len;
            size_t row,col;
            if(!w||!h)break;
            if(!x11_drawable_surface(c,drawable,&src_backing,&src_size,&src_w,&src_h))break;
            data_len=(size_t)w*(size_t)h*4u;
            if(data_len>4u*1024u*1024u)break;
            img=(uint8_t*)ulibc_malloc(data_len);
            if(!img)break;
            ulibc_memset(img,0,data_len);
            for(row=0;row<h;++row){
                int sy=src_y+(int)row;
                for(col=0;col<w;++col){
                    int sx=src_x+(int)col;
                    size_t doff=((size_t)row*(size_t)w+col)*4u;
                    size_t soff;
                    if(sx<0||sy<0||sx>=src_w||sy>=src_h)continue;
                    soff=((size_t)sy*(size_t)src_w+(size_t)sx)*4u;
                    if(soff+4>src_size||doff+4>data_len)continue;
                    ulibc_memcpy(img+doff,src_backing+soff,4);
                }
            }
            x11_reply_init(c,reply,sizeof(reply),24); /* depth */
            {
                uint32_t words=(uint32_t)((data_len+3u)/4u);
                X11_W32(reply+4,words);
            }
            X11_W32(reply+8,c->visual);
            sock_send(c->sock_fd,reply,sizeof(reply),0);
            sock_send(c->sock_fd,img,data_len,0);
            pad_len=((4u-(data_len&3u))&3u);
            if(pad_len){
                uint8_t pad[4]={0,0,0,0};
                sock_send(c->sock_fd,pad,pad_len,0);
            }
            ulibc_free(img);
            X11_RET(0);
        }
        break;}
    case X11_InternAtom:{
        if(body_len<4)break;
        uint8_t only_if_exists=extra;
        uint16_t name_len=X11_U16(body+0);
        char name[64]={0};
        if(name_len>(uint16_t)(body_len-4))name_len=(uint16_t)(body_len-4);
        if(name_len>63)name_len=63;
        ulibc_memcpy(name,body+4,name_len);
        uint32_t atom_id=x11_intern_atom(c,name,only_if_exists?false:true);
        x11_trace_atom(c,"intern",atom_id,name,(uint32_t)only_if_exists);
        /* Reply: 32 bytes */
        uint8_t reply[32];
        x11_reply_init(c,reply,sizeof(reply),0);
        X11_W32(reply+8,atom_id);
        sock_send(c->sock_fd,reply,32,0);
        X11_RET(0);/* reply sent, don't read more */}
    case X11_GetAtomName:{
        if(body_len<4)break;
        {
            uint32_t atom=X11_U32(body+0);
            const char *name=x11_atom_name_by_id(c,atom);
            size_t name_len=0;
            uint8_t reply[32];
            name_len=ulibc_strlen(name);
            if(name_len>255u)name_len=255u;
            x11_reply_init(c,reply,sizeof(reply),0);
            {
                uint32_t words=(uint32_t)((name_len+3u)/4u);
                X11_W32(reply+4,words);
            }
            X11_W16(reply+8,(uint16_t)name_len);
            sock_send(c->sock_fd,reply,sizeof(reply),0);
            if(name_len)sock_send(c->sock_fd,name,name_len,0);
            if((name_len&3u)!=0u){
                uint8_t pad[4]={0,0,0,0};
                sock_send(c->sock_fd,pad,4u-(name_len&3u),0);
            }
            X11_RET(0);
        }
    }
    case X11_QueryExtension:{
        char ext_name[64];
        uint16_t name_len;
        uint8_t major_opcode=0;
        uint8_t first_event=0;
        uint8_t first_error=0;
        uint8_t present=0;
        uint8_t reply[32];
        size_t ncopy;
        if(body_len<4)break;
        name_len=X11_U16(body+0);
        if(name_len>(uint16_t)(body_len-4))name_len=(uint16_t)(body_len-4);
        ncopy=name_len;
        if(ncopy>=sizeof(ext_name))ncopy=sizeof(ext_name)-1;
        ulibc_memset(ext_name,0,sizeof(ext_name));
        if(ncopy)ulibc_memcpy(ext_name,body+4,ncopy);
        major_opcode=x11_extension_opcode(c,ext_name);
        if(major_opcode){
            present=1;
            if(major_opcode==c->ext_opcode_shm)first_event=64;
            else if(major_opcode==c->ext_opcode_xfixes)first_event=68;
            else if(major_opcode==c->ext_opcode_xinput)first_event=72;
            else if(major_opcode==c->ext_opcode_damage)first_event=80;
            else if(major_opcode==c->ext_opcode_shape)first_event=84;
            else if(major_opcode==c->ext_opcode_xkeyboard)first_event=88;
            else if(major_opcode==c->ext_opcode_sync)first_event=96;
        }
        x11_trace_extension(c,ext_name,0,body_len,present?"present":"absent");
        x11_reply_init(c,reply,sizeof(reply),0);
        reply[8]=present;
        reply[9]=major_opcode;
        reply[10]=first_event;
        reply[11]=first_error;
        sock_send(c->sock_fd,reply,sizeof(reply),0);
        X11_RET(0);
    }
    case X11_GetWindowAttributes:{
        if(body_len<4)break;
        {
            uint32_t wid=X11_U32(body+0);
            x11_window_t *wn=x11_find_window(c,wid);
            uint8_t reply[44];
            uint32_t visual=wn&&wn->visual?wn->visual:c->visual;
            uint16_t klass=wn&&wn->klass?wn->klass:1; /* InputOutput */
            uint8_t map_state=wn&&wn->mapped?2:0;     /* IsViewable */
            x11_reply_init(c,reply,sizeof(reply),0);   /* backing-store hint */
            X11_W32(reply+4,3); /* length=3 (12 bytes after 32-byte header) */
            X11_W32(reply+8,visual);
            X11_W16(reply+12,klass);
            reply[14]=0; /* ForgetGravity */
            reply[15]=1; /* NorthWestGravity */
            X11_W32(reply+16,0x00FFFFFFu);
            X11_W32(reply+20,0);
            reply[24]=0; /* save-under */
            reply[25]=1; /* map-installed */
            reply[26]=map_state;
            reply[27]=0; /* override-redirect */
            X11_W32(reply+28,0x00000020u); /* default colormap */
            X11_W32(reply+32,wn?wn->event_mask:0);
            X11_W32(reply+36,wn?wn->event_mask:0);
            X11_W16(reply+40,0);
            sock_send(c->sock_fd,reply,sizeof(reply),0);
            X11_RET(0);
        }
    }
    case X11_ChangeProperty:{
        if(body_len<20)break;
        {
            uint8_t mode=extra;
            uint32_t win=X11_U32(body+0);
            uint32_t atom=X11_U32(body+4);
            uint32_t type=X11_U32(body+8);
            uint8_t format=body[12];
            uint32_t nitems=X11_U32(body+16);
            size_t unit=(format==8)?1u:((format==16)?2u:((format==32)?4u:0u));
            size_t bytes=0;
            size_t padded=0;
            const uint8_t *src=body+20;
            x11_property_t *prop;
            const char *prop_name=x11_atom_name_by_id(c,atom);
            bool created=false;
            uint8_t *ndata=0;
            size_t nbytes=0;
            (void)win;
            if(!atom||!unit)break;
            x11_trace_atom(c,"change-prop",atom,prop_name,win);
            bytes=(size_t)nitems*unit;
            padded=(bytes+3u)&~3u;
            if(bytes>8u*1024u*1024u)break;
            if(body_len<20u+padded)break;
            prop=x11_find_prop(c,win,atom);
            if(!prop){prop=x11_alloc_prop(c);created=true;}
            if(!prop)break;
            if(mode==X11_PROP_MODE_REPLACE||!prop->data){
                if(bytes){
                    ndata=(uint8_t*)ulibc_malloc(bytes);
                    if(!ndata){
                        if(created)x11_free_prop(c,prop);
                        break;
                    }
                    ulibc_memcpy(ndata,src,bytes);
                }
                if(prop->data)ulibc_free(prop->data);
                prop->data=ndata;
                prop->data_bytes=(uint32_t)bytes;
            }else if(mode==X11_PROP_MODE_APPEND){
                nbytes=(size_t)prop->data_bytes+bytes;
                if(nbytes>8u*1024u*1024u){
                    if(created)x11_free_prop(c,prop);
                    break;
                }
                ndata=(uint8_t*)ulibc_malloc(nbytes);
                if(!ndata){
                    if(created)x11_free_prop(c,prop);
                    break;
                }
                if(prop->data_bytes)ulibc_memcpy(ndata,prop->data,prop->data_bytes);
                if(bytes)ulibc_memcpy(ndata+prop->data_bytes,src,bytes);
                if(prop->data)ulibc_free(prop->data);
                prop->data=ndata;
                prop->data_bytes=(uint32_t)nbytes;
            }else if(mode==X11_PROP_MODE_PREPEND){
                nbytes=(size_t)prop->data_bytes+bytes;
                if(nbytes>8u*1024u*1024u){
                    if(created)x11_free_prop(c,prop);
                    break;
                }
                ndata=(uint8_t*)ulibc_malloc(nbytes);
                if(!ndata){
                    if(created)x11_free_prop(c,prop);
                    break;
                }
                if(bytes)ulibc_memcpy(ndata,src,bytes);
                if(prop->data_bytes)ulibc_memcpy(ndata+bytes,prop->data,prop->data_bytes);
                if(prop->data)ulibc_free(prop->data);
                prop->data=ndata;
                prop->data_bytes=(uint32_t)nbytes;
            }else{
                if(created)x11_free_prop(c,prop);
                break;
            }
            prop->used=true;
            prop->window=win;
            prop->atom=atom;
            prop->type=type;
            prop->format=format;
            x11_push_property_notify_event(c,win,atom,0);
        }
        break;}
    case X11_DeleteProperty:{
        if(body_len<8)break;
        {
            uint32_t win=X11_U32(body+0);
            uint32_t atom=X11_U32(body+4);
            x11_property_t *prop=x11_find_prop(c,win,atom);
            x11_trace_atom(c,"delete-prop",atom,x11_atom_name_by_id(c,atom),win);
            if(prop)x11_free_prop(c,prop);
            x11_push_property_notify_event(c,win,atom,1);
        }
        break;}
    case X11_GetProperty:{
        if(body_len<20)break;
        {
            uint8_t do_delete=extra;
            uint32_t win=X11_U32(body+0);
            uint32_t atom=X11_U32(body+4);
            uint32_t req_type=X11_U32(body+8);
            uint32_t long_off=X11_U32(body+12);
            uint32_t long_len=X11_U32(body+16);
            x11_property_t *prop=x11_find_prop(c,win,atom);
            uint8_t reply[32];
            size_t unit=0;
            size_t start=0;
            size_t available=0;
            size_t send_bytes=0;
            size_t words=0;
            uint32_t bytes_after=0;
            uint32_t nitems=0;
            const char *prop_name=x11_atom_name_by_id(c,atom);
            x11_trace_atom(c,"get-prop",atom,prop_name,win);
            x11_reply_init(c,reply,sizeof(reply),0);
            if((!prop||!prop->used)){
                uint32_t type_atom=x11_atom_id_by_name(c,"ATOM");
                uint32_t type_window=x11_atom_id_by_name(c,"WINDOW");
                uint32_t type_cardinal=x11_atom_id_by_name(c,"CARDINAL");
                uint32_t type_utf8=x11_atom_id_by_name(c,"UTF8_STRING");
                uint32_t type_string=x11_atom_id_by_name(c,"STRING");
                if(prop_name&&ulibc_strcmp(prop_name,"WM_STATE")==0&&
                   (req_type==0||req_type==atom)){
                    x11_window_t *sw=x11_find_window(c,win);
                    uint8_t data[8];
                    ulibc_memset(data,0,sizeof(data));
                    X11_W32(data+0,(sw&&sw->mapped)?1u:0u); /* NormalState / WithdrawnState */
                    X11_W32(data+4,0);                      /* icon window */
                    start=(size_t)long_off*4u;
                    if(start<sizeof(data)){
                        available=sizeof(data)-start;
                        send_bytes=(size_t)long_len*4u;
                        if(send_bytes>available)send_bytes=available;
                        bytes_after=(uint32_t)(available-send_bytes);
                        nitems=(uint32_t)(send_bytes/4u);
                    }
                    reply[1]=32;
                    X11_W32(reply+4,(uint32_t)((send_bytes+3u)/4u));
                    X11_W32(reply+8,atom);
                    X11_W32(reply+12,bytes_after);
                    X11_W32(reply+16,nitems);
                    sock_send(c->sock_fd,reply,sizeof(reply),0);
                    if(send_bytes)sock_send(c->sock_fd,data+start,send_bytes,0);
                    X11_RET(0);
                }
                if(x11_advertise_wm(c)&&prop_name&&ulibc_strcmp(prop_name,"_NET_SUPPORTING_WM_CHECK")==0&&
                   (req_type==0||req_type==type_window)){
                    uint8_t data[4];
                    if(long_off!=0)send_bytes=0;
                    else send_bytes=(long_len?4u:0u);
                    reply[1]=32;
                    X11_W32(reply+4,send_bytes?1u:0u);
                    X11_W32(reply+8,type_window);
                    X11_W32(reply+12,send_bytes?0u:4u);
                    X11_W32(reply+16,send_bytes?1u:0u);
                    X11_W32(data,c->root_window);
                    sock_send(c->sock_fd,reply,sizeof(reply),0);
                    if(send_bytes)sock_send(c->sock_fd,data,send_bytes,0);
                    X11_RET(0);
                }
                if(x11_advertise_wm(c)&&prop_name&&ulibc_strcmp(prop_name,"_NET_SUPPORTED")==0&&
                   (req_type==0||req_type==type_atom)){
                    uint32_t vals[32];
                    uint8_t data[128];
                    uint32_t count=0,bi;
                    size_t total,start2;
                    vals[count++]=x11_intern_atom(c,"_NET_SUPPORTED",true);
                    vals[count++]=x11_intern_atom(c,"_NET_SUPPORTING_WM_CHECK",true);
                    vals[count++]=x11_intern_atom(c,"_NET_WM_NAME",true);
                    vals[count++]=x11_intern_atom(c,"_NET_WM_PID",true);
                    vals[count++]=x11_intern_atom(c,"_NET_ACTIVE_WINDOW",true);
                    vals[count++]=x11_intern_atom(c,"_NET_WM_WINDOW_TYPE",true);
                    vals[count++]=x11_intern_atom(c,"_NET_WM_WINDOW_TYPE_NORMAL",true);
                    vals[count++]=x11_intern_atom(c,"_NET_WM_STATE",true);
                    vals[count++]=x11_intern_atom(c,"_NET_WM_STATE_FULLSCREEN",true);
                    vals[count++]=x11_intern_atom(c,"_NET_WM_STATE_MAXIMIZED_VERT",true);
                    vals[count++]=x11_intern_atom(c,"_NET_WM_STATE_MAXIMIZED_HORZ",true);
                    vals[count++]=x11_intern_atom(c,"_NET_WM_DESKTOP",true);
                    vals[count++]=x11_intern_atom(c,"_NET_WM_ALLOWED_ACTIONS",true);
                    vals[count++]=x11_intern_atom(c,"_NET_WM_ACTION_CLOSE",true);
                    vals[count++]=x11_intern_atom(c,"_NET_WM_ACTION_MINIMIZE",true);
                    vals[count++]=x11_intern_atom(c,"_NET_WM_ACTION_MAXIMIZE_VERT",true);
                    vals[count++]=x11_intern_atom(c,"_NET_WM_ACTION_MAXIMIZE_HORZ",true);
                    vals[count++]=x11_intern_atom(c,"_NET_FRAME_EXTENTS",true);
                    vals[count++]=x11_intern_atom(c,"WM_PROTOCOLS",true);
                    for(bi=0;bi<count;++bi)X11_W32(data+bi*4u,vals[bi]);
                    total=(size_t)count*4u;
                    start2=(size_t)long_off*4u;
                    if(start2<total){
                        available=total-start2;
                        send_bytes=(size_t)long_len*4u;
                        if(send_bytes>available)send_bytes=available;
                        bytes_after=(uint32_t)(available-send_bytes);
                        nitems=(uint32_t)(send_bytes/4u);
                    }
                    reply[1]=32;
                    X11_W32(reply+4,(uint32_t)((send_bytes+3u)/4u));
                    X11_W32(reply+8,type_atom);
                    X11_W32(reply+12,bytes_after);
                    X11_W32(reply+16,nitems);
                    sock_send(c->sock_fd,reply,sizeof(reply),0);
                    if(send_bytes)sock_send(c->sock_fd,data+start2,send_bytes,0);
                    X11_RET(0);
                }
                if(x11_advertise_wm(c)&&prop_name&&ulibc_strcmp(prop_name,"_NET_WM_NAME")==0&&
                   (req_type==0||req_type==type_utf8||req_type==type_string)){
                    const char *name8="Ridux WM";
                    uint32_t n=(uint32_t)ulibc_strlen(name8);
                    uint8_t pad[4]={0,0,0,0};
                    start=(size_t)long_off*4u;
                    if(start<n){
                        available=(size_t)n-start;
                        send_bytes=(size_t)long_len*4u;
                        if(send_bytes>available)send_bytes=available;
                        bytes_after=(uint32_t)(available-send_bytes);
                        nitems=(uint32_t)send_bytes;
                    }
                    reply[1]=8;
                    X11_W32(reply+4,(uint32_t)((send_bytes+3u)/4u));
                    X11_W32(reply+8,type_utf8?type_utf8:type_string);
                    X11_W32(reply+12,bytes_after);
                    X11_W32(reply+16,nitems);
                    sock_send(c->sock_fd,reply,sizeof(reply),0);
                    if(send_bytes)sock_send(c->sock_fd,name8+start,send_bytes,0);
                    if(send_bytes&&(send_bytes&3u))sock_send(c->sock_fd,pad,4u-(send_bytes&3u),0);
                    X11_RET(0);
                }
                if(x11_advertise_wm(c)&&prop_name&&ulibc_strcmp(prop_name,"_NET_FRAME_EXTENTS")==0&&
                   (req_type==0||req_type==type_cardinal)){
                    uint8_t data[16];
                    ulibc_memset(data,0,sizeof(data));
                    if(long_off!=0)send_bytes=0;
                    else send_bytes=(long_len>=4u)?16u:(size_t)long_len*4u;
                    reply[1]=32;
                    X11_W32(reply+4,(uint32_t)((send_bytes+3u)/4u));
                    X11_W32(reply+8,type_cardinal);
                    X11_W32(reply+12,(uint32_t)(16u-send_bytes));
                    X11_W32(reply+16,(uint32_t)(send_bytes/4u));
                    sock_send(c->sock_fd,reply,sizeof(reply),0);
                    if(send_bytes)sock_send(c->sock_fd,data,send_bytes,0);
                    X11_RET(0);
                }
                if(x11_advertise_wm(c)&&prop_name&&ulibc_strcmp(prop_name,"_NET_WORKAREA")==0&&
                   (req_type==0||req_type==type_cardinal)){
                    uint8_t data[16];
                    ulibc_memset(data,0,sizeof(data));
                    X11_W32(data+0,0);
                    X11_W32(data+4,0);
                    X11_W32(data+8,(uint32_t)c->screen_width);
                    X11_W32(data+12,(uint32_t)c->screen_height);
                    start=(size_t)long_off*4u;
                    if(start<sizeof(data)){
                        available=sizeof(data)-start;
                        send_bytes=(size_t)long_len*4u;
                        if(send_bytes>available)send_bytes=available;
                        bytes_after=(uint32_t)(available-send_bytes);
                        nitems=(uint32_t)(send_bytes/4u);
                    }
                    reply[1]=32;
                    X11_W32(reply+4,(uint32_t)((send_bytes+3u)/4u));
                    X11_W32(reply+8,type_cardinal);
                    X11_W32(reply+12,bytes_after);
                    X11_W32(reply+16,nitems);
                    sock_send(c->sock_fd,reply,sizeof(reply),0);
                    if(send_bytes)sock_send(c->sock_fd,data+start,send_bytes,0);
                    X11_RET(0);
                }
                if(prop_name&&ulibc_strcmp(prop_name,"_XSETTINGS_SETTINGS")==0&&
                   (req_type==0||req_type==atom)){
                    uint8_t data[12];
                    uint8_t pad[4]={0,0,0,0};
                    ulibc_memset(data,0,sizeof(data));
                    data[0]=x11_is_lsb(c)?0:1; /* LSBFirst / MSBFirst */
                    X11_W32(data+4,1); /* serial */
                    X11_W32(data+8,0); /* no settings */
                    start=(size_t)long_off*4u;
                    if(start<sizeof(data)){
                        available=sizeof(data)-start;
                        send_bytes=(size_t)long_len*4u;
                        if(send_bytes>available)send_bytes=available;
                        bytes_after=(uint32_t)(available-send_bytes);
                        nitems=(uint32_t)send_bytes;
                    }
                    reply[1]=8;
                    X11_W32(reply+4,(uint32_t)((send_bytes+3u)/4u));
                    X11_W32(reply+8,atom);
                    X11_W32(reply+12,bytes_after);
                    X11_W32(reply+16,nitems);
                    sock_send(c->sock_fd,reply,sizeof(reply),0);
                    if(send_bytes)sock_send(c->sock_fd,data+start,send_bytes,0);
                    if(send_bytes&&(send_bytes&3u))sock_send(c->sock_fd,pad,4u-(send_bytes&3u),0);
                    X11_RET(0);
                }
            }
            if(prop&&prop->used&&prop->format&&(req_type==0||req_type==prop->type)){
                unit=(prop->format==8)?1u:((prop->format==16)?2u:4u);
                start=(size_t)long_off*4u;
                if(start<prop->data_bytes){
                    available=(size_t)prop->data_bytes-start;
                    send_bytes=(size_t)long_len*4u;
                    if(send_bytes>available)send_bytes=available;
                    bytes_after=(uint32_t)(available-send_bytes);
                    if(unit)nitems=(uint32_t)(send_bytes/unit);
                }
                reply[1]=prop->format;
                X11_W32(reply+8,prop->type);
            }
            words=(send_bytes+3u)/4u;
            X11_W32(reply+4,(uint32_t)words);
            X11_W32(reply+12,bytes_after);
            X11_W32(reply+16,nitems);
            sock_send(c->sock_fd,reply,sizeof(reply),0);
            if(send_bytes&&prop&&prop->data){
                sock_send(c->sock_fd,prop->data+start,send_bytes,0);
                if((send_bytes&3u)!=0u){
                    uint8_t pad[4]={0,0,0,0};
                    sock_send(c->sock_fd,pad,4u-(send_bytes&3u),0);
                }
            }
            if(do_delete&&prop&&prop->used&&bytes_after==0)x11_free_prop(c,prop);
            X11_RET(0);
        }
    }
    case X11_GetGeometry:{
        if(body_len<4)break;
        {
            uint32_t drawable=X11_U32(body+0);
            x11_window_t *w=x11_find_window(c,drawable);
            x11_pixmap_t *p=x11_find_pixmap(c,drawable);
            uint8_t reply[32];
            int x=0,y=0,width=0,height=0,border=0;
            uint8_t depth=c->depth;
            if(w){
                x=w->x;y=w->y;width=w->width;height=w->height;border=w->border_width;depth=w->depth?w->depth:c->depth;
            }else if(p){
                width=p->width;height=p->height;depth=p->depth?p->depth:c->depth;
            }
            x11_reply_init(c,reply,sizeof(reply),depth);
            X11_W32(reply+8,c->root_window);
            X11_W16(reply+12,(uint16_t)x);
            X11_W16(reply+14,(uint16_t)y);
            X11_W16(reply+16,(uint16_t)width);
            X11_W16(reply+18,(uint16_t)height);
            X11_W16(reply+20,(uint16_t)border);
            sock_send(c->sock_fd,reply,sizeof(reply),0);
            X11_RET(0);
        }
    }
    case X11_QueryTree:{
        if(body_len<4)break;
        {
            uint32_t wid=X11_U32(body+0);
            x11_window_t *w=x11_find_window(c,wid);
            uint8_t reply[32];
            uint32_t children[X11_MAX_WINDOWS];
            uint16_t nchild=0;
            int i;
            for(i=0;i<X11_MAX_WINDOWS;++i){
                if(!c->windows[i].used||c->windows[i].parent!=wid)continue;
                children[nchild++]=c->windows[i].id;
            }
            x11_reply_init(c,reply,sizeof(reply),0);
            X11_W32(reply+4,nchild); /* 4-byte child IDs follow */
            X11_W32(reply+8,c->root_window);
            X11_W32(reply+12,w?w->parent:0);
            X11_W16(reply+16,nchild);
            sock_send(c->sock_fd,reply,sizeof(reply),0);
            for(i=0;i<nchild;++i){
                uint8_t idb[4];
                X11_W32(idb,children[i]);
                sock_send(c->sock_fd,idb,sizeof(idb),0);
            }
            X11_RET(0);
        }
    }
    case X11_TranslateCoordinates:{
        if(body_len<12)break;
        {
            uint32_t src_wid=X11_U32(body+0);
            uint32_t dst_wid=X11_U32(body+4);
            int16_t src_x=(int16_t)X11_U16(body+8);
            int16_t src_y=(int16_t)X11_U16(body+10);
            x11_window_t *src=x11_find_window(c,src_wid);
            x11_window_t *dst=x11_find_window(c,dst_wid);
            int16_t dst_x=src_x;
            int16_t dst_y=src_y;
            uint8_t reply[32];
            if(src&&dst){
                dst_x=(int16_t)(src_x+src->x-dst->x);
                dst_y=(int16_t)(src_y+src->y-dst->y);
            }
            x11_reply_init(c,reply,sizeof(reply),1);
            X11_W32(reply+8,0); /* child */
            X11_W16(reply+12,(uint16_t)dst_x);
            X11_W16(reply+14,(uint16_t)dst_y);
            sock_send(c->sock_fd,reply,sizeof(reply),0);
            X11_RET(0);
        }
    }
    case X11_QueryPointer:{
        uint8_t reply[32];
        x11_reply_init(c,reply,sizeof(reply),1); /* same-screen=true */
        X11_W32(reply+8,c->root_window);  /* root */
        X11_W32(reply+12,c->root_window); /* child */
        X11_W16(reply+16,(uint16_t)c->pointer_x); /* root x */
        X11_W16(reply+18,(uint16_t)c->pointer_y); /* root y */
        X11_W16(reply+20,(uint16_t)c->pointer_x); /* win x */
        X11_W16(reply+22,(uint16_t)c->pointer_y); /* win y */
        sock_send(c->sock_fd,reply,32,0);X11_RET(0);}
    case X11_GetInputFocus:{
        uint8_t reply[32];
        x11_reply_init(c,reply,sizeof(reply),0);
        reply[8]=1;/* focus=PointerRoot */reply[9]=0;/* revert_to */
        sock_send(c->sock_fd,reply,32,0);X11_RET(0);}
    case X11_GetSelectionOwner:{
        uint8_t reply[32];
        uint32_t selection;
        uint32_t owner=0;
        const char *name;
        if(body_len<4)break;
        selection=X11_U32(body+0);
        name=x11_atom_name_by_id(c,selection);
        owner=x11_selection_owner_for_name(c,name);
        x11_trace_atom(c,"selection",selection,name,owner);
        x11_reply_init(c,reply,sizeof(reply),0);
        X11_W32(reply+8,owner);
        sock_send(c->sock_fd,reply,32,0);
        if(owner&&ulibc_strcmp(name,"_NET_WM_CM_S0")==0&&
           c->xfixes_cm_selection==selection&&!c->xfixes_cm_notified){
            x11_push_xfixes_selection_event(c,
                c->xfixes_cm_window?c->xfixes_cm_window:c->root_window,
                selection,owner);
            c->xfixes_cm_notified=true;
        }
        X11_RET(0);}
    case X11_SetSelectionOwner:
    case X11_ConvertSelection:
    case X11_GrabServer:
    case X11_UngrabServer:
    case X11_OpenFont:
        break;/* no reply */
    case X11_SetInputFocus:break;/* no reply */
    case X11_SendEvent:{
        if(body_len>=36){
            uint32_t dst=X11_U32(body+0);
            uint32_t mask=X11_U32(body+4);
            x11_event_t ev;
            ulibc_memset(&ev,0,sizeof(ev));
            ulibc_memcpy(&ev,body+8,sizeof(ev));
            if(dst==0||dst==1)dst=c->root_window;
            if(dst==c->root_window){
                static uint32_t send_event_drop_trace=0;
                if(send_event_drop_trace<32){
                    ++send_event_drop_trace;
                    __boot_serial_puts("[x11-send] drop-root fd=");
                    __boot_serial_putu32(c?(uint32_t)c->sock_fd:0);
                    __boot_serial_puts(" dst=");
                    __boot_serial_puthex64((uint64_t)dst);
                    __boot_serial_puts(" mask=");
                    __boot_serial_puthex64((uint64_t)mask);
                    __boot_serial_puts(" type=");
                    __boot_serial_putu32((uint32_t)ev.type);
                    __boot_serial_puts("\n");
                }
                /*
                 * These root-directed synthetic events are normally consumed by
                 * the window manager.  Echoing them to the app confuses GDK's
                 * early server-time / WM probing path and can make Xlib treat
                 * the display as broken before the browser window is realized.
                 */
                break;
            }
            ev.type|=0x80u;
            ((uint32_t*)ev.pad)[0]=dst;
            x11_push_event(c,&ev);
        }
        break;}
    case X11_WarpPointer:{
        if(body_len>=20){
            uint32_t dst=X11_U32(body+4);
            int16_t dst_x=(int16_t)X11_U16(body+16);
            int16_t dst_y=(int16_t)X11_U16(body+18);
            x11_window_t *dw=x11_find_window(c,dst);
            if(dw){
                c->pointer_x=dw->x+dst_x;
                c->pointer_y=dw->y+dst_y;
            }else{
                c->pointer_x=dst_x;
                c->pointer_y=dst_y;
            }
        }
        break;}
    case X11_GrabKeyboard:case X11_GrabPointer:{
        uint8_t reply[32];
        x11_reply_init(c,reply,sizeof(reply),0); /* GrabSuccess */
        sock_send(c->sock_fd,reply,32,0);X11_RET(0);}
    case X11_Flush:case X11_Sync:break;/* no-op */
    case X11_ListExtensions:{
        /* Return list of supported X11 extensions */
        static const char *ext_names[]={
            "MIT-SHM","RENDER","RANDR",
            "BIG-REQUESTS","Generic Event Extension",
            "XFIXES",
            "SHAPE","SYNC"
        };
        int ext_count=(int)(sizeof(ext_names)/sizeof(ext_names[0]));
        uint8_t strbuf[512];
        int slen=0;
        int i;
        for(i=0;i<ext_count;i++){
            int nlen=(int)ulibc_strlen(ext_names[i]);
            if(slen+1+nlen>(int)sizeof(strbuf))break;
            strbuf[slen++]=(uint8_t)nlen;
            ulibc_memcpy(strbuf+slen,ext_names[i],(size_t)nlen);
            slen+=nlen;
        }
        {
            int pad=(4-((32+slen)&3))&3;
            int extra_words=(slen+pad)/4;
            uint8_t reply[32];
            uint8_t pz[4]={0,0,0,0};
            x11_reply_init(c,reply,sizeof(reply),(uint32_t)extra_words);
            x11_reply_set_length(c,reply,(uint32_t)extra_words);
            reply[1]=(uint8_t)ext_count;
            sock_send(c->sock_fd,reply,32,0);
            if(slen>0)sock_send(c->sock_fd,strbuf,(size_t)slen,0);
            if(pad>0)sock_send(c->sock_fd,pz,(size_t)pad,0);
        }
        X11_RET(0);}
    case X11_GetKeyboardMapping:{
        /* Core X11 keyboard fallback: one keysym per keycode. */
        uint8_t reply[32];
        int first_kc=extra;
        int count=body_len>=1?(int)body[0]:1;
        int keysyms_per=1;
        int total=count*keysyms_per;
        int i;
        if(count<=0)count=1;
        if(count>248)count=248;
        total=count*keysyms_per;
        __boot_serial_force_puts("[x11-keymap!] first=");
        __boot_serial_force_putu32((uint32_t)first_kc);
        __boot_serial_force_puts(" count=");
        __boot_serial_force_putu32((uint32_t)count);
        __boot_serial_force_puts(" per=");
        __boot_serial_force_putu32((uint32_t)keysyms_per);
        __boot_serial_force_puts("\n");
        x11_reply_init(c,reply,sizeof(reply),(uint8_t)keysyms_per);
        x11_reply_set_length(c,reply,(uint32_t)total);
        sock_send(c->sock_fd,reply,32,0);
        for(i=0;i<total;i++){
            uint8_t ksym[4];
            int kc=first_kc+i;
            uint32_t sym=0;
            switch(kc){
                case 9:sym=0xFF1Bu;break;  /* Escape */
                case 10:sym='1';break;case 11:sym='2';break;case 12:sym='3';break;
                case 13:sym='4';break;case 14:sym='5';break;case 15:sym='6';break;
                case 16:sym='7';break;case 17:sym='8';break;case 18:sym='9';break;
                case 19:sym='0';break;case 20:sym='-';break;case 21:sym='=';break;
                case 22:sym=0xFF08u;break; /* BackSpace */
                case 23:sym=0xFF09u;break; /* Tab */
                case 24:sym='q';break;case 25:sym='w';break;case 26:sym='e';break;
                case 27:sym='r';break;case 28:sym='t';break;case 29:sym='y';break;
                case 30:sym='u';break;case 31:sym='i';break;case 32:sym='o';break;
                case 33:sym='p';break;case 34:sym='[';break;case 35:sym=']';break;
                case 36:sym=0xFF0Du;break; /* Return */
                case 37:sym=0xFFE3u;break; /* Control_L */
                case 38:sym='a';break;case 39:sym='s';break;case 40:sym='d';break;
                case 41:sym='f';break;case 42:sym='g';break;case 43:sym='h';break;
                case 44:sym='j';break;case 45:sym='k';break;case 46:sym='l';break;
                case 47:sym=';';break;case 48:sym='\'';break;case 49:sym='`';break;
                case 50:sym=0xFFE1u;break; /* Shift_L */
                case 51:sym='\\';break;
                case 52:sym='z';break;case 53:sym='x';break;case 54:sym='c';break;
                case 55:sym='v';break;case 56:sym='b';break;case 57:sym='n';break;
                case 58:sym='m';break;case 59:sym=',';break;case 60:sym='.';break;
                case 61:sym='/';break;
                case 64:sym=0xFFE9u;break; /* Alt_L */
                case 65:sym=' ';break;
                case 111:sym=0xFF52u;break; /* Up */
                case 113:sym=0xFF51u;break; /* Left */
                case 114:sym=0xFF53u;break; /* Right */
                case 116:sym=0xFF54u;break; /* Down */
                case 118:sym=0xFF63u;break; /* Insert */
                case 119:sym=0xFFFFu;break; /* Delete */
                case 110:sym=0xFF50u;break; /* Home */
                case 115:sym=0xFF57u;break; /* End */
                case 112:sym=0xFF55u;break; /* Page_Up */
                case 117:sym=0xFF56u;break; /* Page_Down */
                default:sym=0;break;
            }
            x11_wr32(c,ksym,sym);
            sock_send(c->sock_fd,ksym,4,0);
        }
        X11_RET(0);}
    case X11_GetPointerMapping:{
        uint8_t reply[32];
        uint8_t map[8]={1,2,3,4,5,0,0,0};
        x11_reply_init(c,reply,sizeof(reply),5);
        x11_reply_set_length(c,reply,2);
        sock_send(c->sock_fd,reply,32,0);
        sock_send(c->sock_fd,map,sizeof(map),0);
        X11_RET(0);}
    case X11_SetPointerMapping:
    case X11_SetModifierMapping:{
        uint8_t reply[32];
        x11_reply_init(c,reply,sizeof(reply),0); /* MappingSuccess */
        sock_send(c->sock_fd,reply,32,0);
        X11_RET(0);}
    case X11_GetModifierMapping:{
        /* Keep the core modifier table conservative until the XKB/core
         * keymap is complete. Chromium reads this table early and assumes
         * every advertised keycode has already been covered by its local
         * keyboard mapping; handing out Control/Alt keycodes here can trip
         * a hard CHECK before the first browser window is created. */
        uint8_t reply[32];
        uint8_t map[8];
        ulibc_memset(map,0,sizeof(map));
        __boot_serial_force_puts("[x11-modmap!] per=1 zeroed\n");
        x11_reply_init(c,reply,sizeof(reply),2);
        x11_reply_set_length(c,reply,2);
        reply[1]=1;
        sock_send(c->sock_fd,reply,32,0);
        sock_send(c->sock_fd,map,sizeof(map),0);
        X11_RET(0);}
    default:{
        /* Safety: some opcodes expect replies. If we don't reply, the client
         * blocks forever. Log unknown opcodes so we can add handlers. For
         * known reply-requiring opcodes, send a minimal error reply. */
        bool needs_reply=false;
        /* Standard X11 reply-requiring opcodes we haven't handled above */
        if(opcode==47||opcode==49||opcode==97||opcode==108||
           opcode==44||opcode==46||opcode==83||opcode==84)needs_reply=true;
        __boot_serial_puts("[x11-unhandled] op=");
        __boot_serial_putu32((uint32_t)opcode);
        __boot_serial_puts(" extra=");
        __boot_serial_putu32((uint32_t)extra);
        __boot_serial_puts(" body=");
        __boot_serial_putu32((uint32_t)body_len);
        __boot_serial_puts(needs_reply?" REPLY-REQUIRED\n":"\n");
        if(needs_reply){
            /* Send a generic error reply to unblock the client */
            uint8_t err[32];
            ulibc_memset(err,0,sizeof(err));
            err[0]=0; /* error */
            err[1]=17; /* BadImplementation */
            err[2]=(uint8_t)(c->sequence&0xFF);
            err[3]=(uint8_t)((c->sequence>>8)&0xFF);
            sock_send(c->sock_fd,err,32,0);
            X11_RET(0);
        }
        break;}
    }
    X11_RET(0);
    #undef X11_W32
    #undef X11_W16
    #undef X11_U32
    #undef X11_U16
    #undef X11_RET
}

static uint16_t x11_modifier_mask_for_keycode(uint8_t keycode){
    switch(keycode){
    case 50:case 62:return 0x0001u; /* ShiftMask */
    case 37:case 105:return 0x0004u; /* ControlMask */
    case 64:case 108:return 0x0008u; /* Mod1Mask / Alt */
    default:return 0;
    }
}

static void x11_push_key_event_to_window(x11_connection_t *c,uint32_t wid,
                                         uint8_t keycode,bool press){
    uint8_t raw[32];
    x11_event_t ev;
    int root_x,root_y,win_x,win_y;
    x11_window_t *w;
    uint16_t mod_mask;
    uint16_t state;
    if(!c||!c->used)return;
    mod_mask=x11_modifier_mask_for_keycode(keycode);
    state=g_x11_key_state_mask;
    if(press&&mod_mask){
        g_x11_key_state_mask|=mod_mask;
        state=g_x11_key_state_mask;
    }else if(!press&&mod_mask){
        state=(uint16_t)(state|mod_mask);
    }
    w=x11_find_window(c,wid);
    if(!w)wid=c->root_window;
    root_x=c->pointer_x;
    root_y=c->pointer_y;
    win_x=root_x;
    win_y=root_y;
    if(w&&w->id!=c->root_window){
        int ox=0,oy=0;
        if(x11_window_content_origin(c,w,&ox,&oy)){
            win_x=root_x-ox;
            win_y=root_y-oy;
        }
    }
    ulibc_memset(raw,0,sizeof(raw));
    raw[0]=press?X11_KeyPress:X11_KeyRelease;
    raw[1]=keycode;
    x11_wr16(c,raw+2,c->sequence);
    x11_wr32(c,raw+4,c->root_window);
    x11_wr32(c,raw+8,wid);
    x11_wr32(c,raw+12,0);
    x11_wr16(c,raw+16,(uint16_t)(int16_t)root_x);
    x11_wr16(c,raw+18,(uint16_t)(int16_t)root_y);
    x11_wr16(c,raw+20,(uint16_t)(int16_t)win_x);
    x11_wr16(c,raw+22,(uint16_t)(int16_t)win_y);
    x11_wr16(c,raw+24,state);
    raw[30]=1; /* same-screen */
    ulibc_memcpy(&ev,raw,sizeof(ev));
    x11_push_event(c,&ev);
    if(!press&&mod_mask)g_x11_key_state_mask=(uint16_t)(g_x11_key_state_mask&~mod_mask);
}

static void x11_push_mouse_event_to_window(x11_connection_t *c,uint32_t wid,
                                           int root_x,int root_y,
                                           int win_x,int win_y,
                                           int button,bool press){
    uint8_t raw[32];
    x11_event_t ev;
    if(!c||!c->used)return;
    c->pointer_x=root_x;
    c->pointer_y=root_y;
    ulibc_memset(raw,0,sizeof(raw));
    if(button>0){
        raw[0]=press?X11_ButtonPress:X11_ButtonRelease;
        raw[1]=(uint8_t)button;
    }else{
        raw[0]=X11_MotionNotify;
        raw[1]=0;
    }
    x11_wr16(c,raw+2,c->sequence);
    x11_wr32(c,raw+4,c->root_window);
    x11_wr32(c,raw+8,wid?wid:c->root_window);
    x11_wr32(c,raw+12,0);
    x11_wr16(c,raw+16,(uint16_t)(int16_t)root_x);
    x11_wr16(c,raw+18,(uint16_t)(int16_t)root_y);
    x11_wr16(c,raw+20,(uint16_t)(int16_t)win_x);
    x11_wr16(c,raw+22,(uint16_t)(int16_t)win_y);
    x11_wr16(c,raw+24,(button>0&&press)?(uint16_t)(1u<<(button+7)):0);
    raw[30]=1; /* same-screen */
    ulibc_memcpy(&ev,raw,sizeof(ev));
    x11_push_event(c,&ev);
}

void x11_push_key_event(int conn_idx,uint8_t keycode,bool press){
    if(conn_idx<0||conn_idx>=X11_MAX_CONN)return;
    x11_push_key_event_to_window(&g_x11_conns[conn_idx],
                                 g_x11_conns[conn_idx].root_window,
                                 keycode,press);
}

void x11_push_mouse_event(int conn_idx,int x,int y,int button,bool press){
    if(conn_idx<0||conn_idx>=X11_MAX_CONN)return;
    x11_push_mouse_event_to_window(&g_x11_conns[conn_idx],
                                   g_x11_conns[conn_idx].root_window,
                                   x,y,x,y,button,press);
}

static x11_window_t *x11_find_screen_window(int screen_x,int screen_y,
                                            int *conn_idx,int *win_x,int *win_y){
    int ci,wi;
    for(ci=X11_MAX_CONN-1;ci>=0;--ci){
        x11_connection_t *c=&g_x11_conns[ci];
        if(!c->used)continue;
        for(wi=X11_MAX_WINDOWS-1;wi>=0;--wi){
            x11_window_t *w=&c->windows[wi];
            int ox=0,oy=0;
            if(!w->used||!w->mapped||!w->visible||w->id==c->root_window)continue;
            if(w->width<=1||w->height<=1)continue;
            if(!x11_window_content_origin(c,w,&ox,&oy))continue;
            if(screen_x<ox||screen_y<oy)continue;
            if(screen_x>=ox+w->width||screen_y>=oy+w->height)continue;
            if(conn_idx)*conn_idx=ci;
            if(win_x)*win_x=screen_x-ox;
            if(win_y)*win_y=screen_y-oy;
            return w;
        }
    }
    return 0;
}

static bool x11_focus_is_valid(x11_connection_t **out_c,uint32_t *out_wid){
    x11_connection_t *c;
    x11_window_t *w;
    if(g_x11_focus_conn<0||g_x11_focus_conn>=X11_MAX_CONN)return false;
    c=&g_x11_conns[g_x11_focus_conn];
    if(!c->used)return false;
    w=x11_find_window(c,g_x11_focus_window);
    if(!w||!w->used||!w->mapped||!w->visible)return false;
    if(out_c)*out_c=c;
    if(out_wid)*out_wid=w->id;
    return true;
}

int x11_dispatch_pointer_event(int screen_x,int screen_y,int button,bool press){
    int ci=-1,wx=0,wy=0;
    x11_window_t *w=x11_find_screen_window(screen_x,screen_y,&ci,&wx,&wy);
    if(!w||ci<0)return 0;
    if(button>0&&press){
        g_x11_focus_conn=ci;
        g_x11_focus_window=w->id;
    }
    x11_push_mouse_event_to_window(&g_x11_conns[ci],w->id,
                                   screen_x,screen_y,wx,wy,button,press);
    return 1;
}

int x11_dispatch_key_event(uint8_t keycode,bool press){
    extern c7_framebuffer_t g_fb;
    x11_connection_t *c=0;
    uint32_t wid=0;
    if(!x11_focus_is_valid(&c,&wid)){
        int ci=-1,wx=0,wy=0;
        x11_window_t *w=x11_find_screen_window((int)(g_fb.width/2u),
                                               (int)(g_fb.height/2u),
                                               &ci,&wx,&wy);
        (void)wx;(void)wy;
        if(!w||ci<0)return 0;
        c=&g_x11_conns[ci];
        wid=w->id;
        g_x11_focus_conn=ci;
        g_x11_focus_window=wid;
    }
    x11_push_key_event_to_window(c,wid,keycode,press);
    return 1;
}

void x11_push_expose(int conn_idx,uint32_t wid,int x,int y,int w,int h){
    if(conn_idx<0||conn_idx>=X11_MAX_CONN)return;
    x11_connection_t*c=&g_x11_conns[conn_idx];if(!c->used)return;
    x11_push_expose_event(c,wid,x,y,w,h);
}

void x11_push_client_message(int conn_idx,uint32_t wid,uint32_t atom,uint32_t data){
    if(conn_idx<0||conn_idx>=X11_MAX_CONN)return;
    x11_connection_t*c=&g_x11_conns[conn_idx];if(!c->used)return;
    x11_event_t ev;ulibc_memset(&ev,0,sizeof(ev));
    ev.type=X11_ClientMessage;ev.detail=32;/* format */
    uint32_t*ep=(uint32_t*)ev.pad;ep[0]=wid;ep[1]=atom;ep[2]=data;
    x11_push_event(c,&ev);
}

static uint32_t x11_chan_mask(uint8_t bits){
    if(bits==0)return 0;
    if(bits>=32)return 0xFFFFFFFFu;
    return (1u<<bits)-1u;
}

static uint8_t x11_unpack_chan(uint32_t pixel,uint8_t pos,uint8_t bits){
    uint32_t mask=x11_chan_mask(bits);
    uint32_t raw;
    if(!mask)return 0;
    raw=(pixel>>pos)&mask;
    return (uint8_t)((raw*255u)/mask);
}

static uint32_t x11_pack_chan(uint8_t v,uint8_t pos,uint8_t bits){
    uint32_t mask=x11_chan_mask(bits);
    uint32_t scaled;
    if(!mask)return 0;
    scaled=((uint32_t)v*mask+127u)/255u;
    return (scaled&mask)<<pos;
}

static uint32_t x11_pack_fb_pixel(const c7_framebuffer_t *fb,uint8_t r,uint8_t g,uint8_t b){
    if(!fb)return 0;
    return x11_pack_chan(r,fb->red_pos,fb->red_size)|
           x11_pack_chan(g,fb->green_pos,fb->green_size)|
           x11_pack_chan(b,fb->blue_pos,fb->blue_size);
}

static uint32_t x11_read_fb_pixel(const c7_framebuffer_t *fb,int x,int y){
    const uint8_t *p;
    uint32_t v=0;
    uint32_t bytes;
    if(!fb||!fb->ready||x<0||y<0||(uint32_t)x>=fb->width||(uint32_t)y>=fb->height)return 0;
    bytes=(uint32_t)(fb->bpp/8u);
    if(bytes==0||bytes>4)bytes=4;
    p=fb->address+(size_t)y*fb->pitch+(size_t)x*bytes;
    if(bytes>=1)v|=(uint32_t)p[0];
    if(bytes>=2)v|=(uint32_t)p[1]<<8;
    if(bytes>=3)v|=(uint32_t)p[2]<<16;
    if(bytes>=4)v|=(uint32_t)p[3]<<24;
    return v;
}

static void x11_put_fb_pixel_argb(int x,int y,uint32_t argb){
    extern c7_framebuffer_t g_fb;
    uint8_t *p;
    uint32_t bytes;
    uint8_t a=(uint8_t)(argb>>24);
    uint8_t r=(uint8_t)(argb>>16);
    uint8_t g=(uint8_t)(argb>>8);
    uint8_t b=(uint8_t)argb;
    uint32_t out;
    if(!g_fb.ready||x<0||y<0||(uint32_t)x>=g_fb.width||(uint32_t)y>=g_fb.height)return;
    if(a==0)a=255; /* XRGB clients often leave alpha zero. */
    if(a<255){
        uint32_t dst;
        if(g_use_backbuffer)dst=g_backbuffer[(size_t)y*C7_FB_MAX_WIDTH+(size_t)x];
        else dst=x11_read_fb_pixel(&g_fb,x,y);
        uint8_t dr=x11_unpack_chan(dst,g_fb.red_pos,g_fb.red_size);
        uint8_t dg=x11_unpack_chan(dst,g_fb.green_pos,g_fb.green_size);
        uint8_t db=x11_unpack_chan(dst,g_fb.blue_pos,g_fb.blue_size);
        r=(uint8_t)(((uint32_t)r*a+(uint32_t)dr*(255u-a))/255u);
        g=(uint8_t)(((uint32_t)g*a+(uint32_t)dg*(255u-a))/255u);
        b=(uint8_t)(((uint32_t)b*a+(uint32_t)db*(255u-a))/255u);
    }
    out=x11_pack_fb_pixel(&g_fb,r,g,b);
    if(g_use_backbuffer){
        g_backbuffer[(size_t)y*C7_FB_MAX_WIDTH+(size_t)x]=out;
        return;
    }
    bytes=(uint32_t)(g_fb.bpp/8u);
    if(bytes<3)return;
    if(bytes>4)bytes=4;
    p=g_fb.address+(size_t)y*g_fb.pitch+(size_t)x*bytes;
    p[0]=(uint8_t)out;
    p[1]=(uint8_t)(out>>8);
    p[2]=(uint8_t)(out>>16);
    if(bytes>=4)p[3]=(uint8_t)(out>>24);
}

static void x11_fill_fb_rect(int x,int y,int w,int h,uint32_t argb){
    int yy,xx;
    if(w<=0||h<=0)return;
    for(yy=0;yy<h;++yy){
        for(xx=0;xx<w;++xx)x11_put_fb_pixel_argb(x+xx,y+yy,argb);
    }
}

static uint32_t x11_backing_pixel_argb(const uint8_t *p){
    return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);
}

static int x11_window_index(x11_connection_t *c,const x11_window_t *w){
    int i;
    if(!c||!w)return 0;
    for(i=0;i<X11_MAX_WINDOWS;++i)if(&c->windows[i]==w)return i;
    return 0;
}

static x11_window_t *x11_top_window(x11_connection_t *c,x11_window_t *w){
    x11_window_t *cur=w;
    int guard=0;
    if(!c||!w||w->id==c->root_window)return 0;
    while(cur&&cur->parent&&cur->parent!=c->root_window&&guard++<X11_MAX_WINDOWS){
        cur=x11_find_window(c,cur->parent);
    }
    return cur?cur:w;
}

static void x11_top_frame_origin(x11_connection_t *c,x11_window_t *top,int *x,int *y){
    extern c7_framebuffer_t g_fb;
    int idx=x11_window_index(c,top);
    int fx=top?top->x:0;
    int fy=top?top->y:0;
    int fw=top?top->width+4:320;
    int fh=top?top->height+28:240;
    if(fx==0&&fy==0){
        fx=72+idx*24;
        fy=56+idx*24;
    }
    if(g_fb.ready){
        if(fw<(int)g_fb.width&&fx+fw>(int)g_fb.width)fx=(int)g_fb.width-fw;
        if(fh<(int)g_fb.height&&fy+fh>(int)g_fb.height)fy=(int)g_fb.height-fh;
    }
    if(fx<0)fx=0;
    if(fy<0)fy=0;
    if(x)*x=fx;
    if(y)*y=fy;
}

static bool x11_window_content_origin(x11_connection_t *c,x11_window_t *w,int *x,int *y){
    x11_window_t *top;
    x11_window_t *cur;
    int rel_x=0,rel_y=0;
    int fx=0,fy=0;
    int guard=0;
    if(!c||!w||w->id==c->root_window)return false;
    top=x11_top_window(c,w);
    if(!top)return false;
    cur=w;
    while(cur&&cur!=top&&guard++<X11_MAX_WINDOWS){
        rel_x+=cur->x;
        rel_y+=cur->y;
        cur=x11_find_window(c,cur->parent);
    }
    x11_top_frame_origin(c,top,&fx,&fy);
    if(x)*x=fx+2+rel_x;
    if(y)*y=fy+26+rel_y;
    return true;
}

static void x11_blit_window_to_fb(x11_connection_t *c,x11_window_t *w){
    int sx=0,sy=0;
    int row,col;
    if(!c||!w||!w->backing||!w->backing_size||w->width<=0||w->height<=0)return;
    if(!x11_window_content_origin(c,w,&sx,&sy))return;
    for(row=0;row<w->height;++row){
        for(col=0;col<w->width;++col){
            size_t off=((size_t)row*(size_t)w->width+(size_t)col)*4u;
            if(off+4>w->backing_size)continue;
            x11_put_fb_pixel_argb(sx+col,sy+row,x11_backing_pixel_argb(w->backing+off));
        }
    }
}

static void x11_draw_window_frame(x11_connection_t *c,x11_window_t *w){
    int fx=0,fy=0;
    int fw,fh;
    uint32_t bg;
    if(!c||!w||w->id==c->root_window||w->parent!=c->root_window)return;
    x11_top_frame_origin(c,w,&fx,&fy);
    fw=w->width+4;
    fh=w->height+28;
    bg=w->background_pixel?w->background_pixel:0xFF101827u;
    x11_fill_fb_rect(fx,fy,fw,fh,0xFF111827u);
    x11_fill_fb_rect(fx+1,fy+1,fw-2,24,0xFF1D4ED8u);
    x11_fill_fb_rect(fx+2,fy+26,w->width,w->height,bg);
    x11_fill_fb_rect(fx+8,fy+8,8,8,0xFFFF5F57u);
    x11_fill_fb_rect(fx+22,fy+8,8,8,0xFFFFBD2Eu);
    x11_fill_fb_rect(fx+36,fy+8,8,8,0xFF28C840u);
}

static void x11_render_windows(void){
    extern c7_framebuffer_t g_fb;
    int ci,wi;
    if(!g_fb.ready||!g_fb.address)return;
    for(ci=0;ci<X11_MAX_CONN;++ci){
        x11_connection_t *c=&g_x11_conns[ci];
        if(!c->used)continue;
        for(wi=0;wi<X11_MAX_WINDOWS;++wi){
            x11_window_t *w=&c->windows[wi];
            if(!w->used||!w->mapped||!w->visible)continue;
            x11_draw_window_frame(c,w);
        }
        for(wi=0;wi<X11_MAX_WINDOWS;++wi){
            x11_window_t *w=&c->windows[wi];
            if(!w->used||!w->mapped||!w->visible)continue;
            if(w->id==c->root_window)continue;
            x11_blit_window_to_fb(c,w);
        }
    }
}

static bool x11_visible_bounds(int *rx,int *ry,int *rw,int *rh){
    int ci,wi;
    int minx=0,miny=0,maxx=0,maxy=0;
    bool any=false;
    for(ci=0;ci<X11_MAX_CONN;++ci){
        x11_connection_t *c=&g_x11_conns[ci];
        if(!c->used)continue;
        for(wi=0;wi<X11_MAX_WINDOWS;++wi){
            x11_window_t *w=&c->windows[wi];
            int x=0,y=0,bw=0,bh=0;
            if(!w->used||!w->mapped||!w->visible||w->id==c->root_window)continue;
            if(w->width<=0||w->height<=0)continue;
            if(w->parent==c->root_window){
                x11_top_frame_origin(c,w,&x,&y);
                bw=w->width+4;
                bh=w->height+28;
            }else{
                if(!x11_window_content_origin(c,w,&x,&y))continue;
                bw=w->width;
                bh=w->height;
            }
            if(!any){
                minx=x;miny=y;maxx=x+bw;maxy=y+bh;any=true;
            }else{
                if(x<minx)minx=x;
                if(y<miny)miny=y;
                if(x+bw>maxx)maxx=x+bw;
                if(y+bh>maxy)maxy=y+bh;
            }
        }
    }
    if(!any)return false;
    if(rx)*rx=minx;
    if(ry)*ry=miny;
    if(rw)*rw=maxx-minx;
    if(rh)*rh=maxy-miny;
    return true;
}

static bool x11_has_visible_client_windows(void){
    int ci,wi;
    for(ci=0;ci<X11_MAX_CONN;++ci){
        x11_connection_t *c=&g_x11_conns[ci];
        if(!c->used)continue;
        for(wi=0;wi<X11_MAX_WINDOWS;++wi){
            x11_window_t *w=&c->windows[wi];
            if(!w->used||!w->mapped||!w->visible)continue;
            if(w->id==c->root_window)continue;
            if(w->width<=1||w->height<=1)continue;
            return true;
        }
    }
    return false;
}

static bool x11_has_any_connection(void){
    int ci;
    for(ci=0;ci<X11_MAX_CONN;++ci){
        if(g_x11_conns[ci].used)return true;
    }
    return false;
}

static void x11_trace_render_snapshot(void){
    int ci,wi;
    uint32_t visible=0;
    if(g_x11_trace_render_count>=40)return;
    for(ci=0;ci<X11_MAX_CONN;++ci){
        x11_connection_t *c=&g_x11_conns[ci];
        if(!c->used)continue;
        for(wi=0;wi<X11_MAX_WINDOWS;++wi){
            x11_window_t *w=&c->windows[wi];
            if(!w->used||!w->mapped||!w->visible||w->id==c->root_window)continue;
            ++visible;
        }
    }
    if(!visible)return;
    ++g_x11_trace_render_count;
    __boot_serial_puts("[x11-render] #");
    __boot_serial_putu32(g_x11_trace_render_count);
    __boot_serial_puts(" visible=");
    __boot_serial_putu32(visible);
    for(ci=0;ci<X11_MAX_CONN;++ci){
        x11_connection_t *c=&g_x11_conns[ci];
        if(!c->used)continue;
        for(wi=0;wi<X11_MAX_WINDOWS;++wi){
            x11_window_t *w=&c->windows[wi];
            int fx=0,fy=0;
            if(!w->used||!w->mapped||!w->visible||w->id==c->root_window)continue;
            x11_top_frame_origin(c,w,&fx,&fy);
            __boot_serial_puts(" first=0x");
            __boot_serial_puthex64((uint64_t)w->id);
            __boot_serial_puts(" ");
            __boot_serial_putu32((uint32_t)w->width);
            __boot_serial_puts("x");
            __boot_serial_putu32((uint32_t)w->height);
            __boot_serial_puts(" fb=");
            __boot_serial_putu32((uint32_t)fx);
            __boot_serial_puts(",");
            __boot_serial_putu32((uint32_t)fy);
            __boot_serial_puts(" backing=");
            __boot_serial_putu32(w->backing_size);
            __boot_serial_puts("\n");
            return;
        }
    }
    __boot_serial_puts("\n");
}

void x11_render_now(void){
    int bx=0,by=0,bw=0,bh=0;
    bool have_bounds=x11_visible_bounds(&bx,&by,&bw,&bh);
    if(!g_x11_render_dirty&&!have_bounds)return;
    x11_trace_render_snapshot();
    x11_render_windows();
    if(g_use_backbuffer)fb_present();
    if(have_bounds)ridux_present_cursor_after_external_blit(bx,by,bw,bh);
    else ridux_request_cursor_redraw();
    g_x11_render_dirty=false;
}

void x11_tick(void){
    static uint32_t render_spin;
    int i;
    if(!g_x11_render_dirty&&!x11_has_any_connection())return;
    for(i=0;i<X11_MAX_CONN;++i){
        x11_connection_t*c=&g_x11_conns[i];if(!c->used)continue;
        {
            int guard;
            for(guard=0;guard<8;++guard){
                if(x11_process_request(i)<0)break;
            }
        }
        /* Send pending events to client */
        while(c->event_count>0){
            if(g_x11_trace_event_count<80){
                x11_event_t *ev=&c->events[c->event_head];
                ++g_x11_trace_event_count;
                __boot_serial_puts("[x11-event] fd=");
                __boot_serial_putu32((uint32_t)c->sock_fd);
                __boot_serial_puts(" type=");
                __boot_serial_putu32((uint32_t)ev->type);
                __boot_serial_puts(" detail=");
                __boot_serial_putu32((uint32_t)ev->detail);
                __boot_serial_puts(" left=");
                __boot_serial_putu32((uint32_t)c->event_count);
                __boot_serial_puts("\n");
            }
            sock_send(c->sock_fd,&c->events[c->event_head],32,0);
            c->event_head=(c->event_head+1)%X11_MAX_EVENTS;
            c->event_count--;
        }
    }
    if(g_x11_render_dirty){
        x11_render_now();
    }
    (void)render_spin;
}

int x11_get_conn_for_window(uint32_t wid){
    int i;for(i=0;i<X11_MAX_CONN;++i){
        x11_connection_t*c=&g_x11_conns[i];if(!c->used)continue;
        if(x11_find_window(c,wid))return i;}
    return-1;
}

/* WAYLAND COMPOSITOR BRIDGE */
wl7_client_t  g_wl7_clients[WL7_MAX_CLIENTS];
wl7_surface_t g_wl7_surfaces[WL7_MAX_SURFACES];
wl7_buffer_t  g_wl7_buffers[WL7_MAX_BUFFERS];
wl7_global_t  g_wl7_globals[WL7_MAX_GLOBALS];
int g_wl7_global_count=0;

static wl7_surface_t*wl7_find_surface(uint32_t id){int i;for(i=0;i<WL7_MAX_SURFACES;++i)if(g_wl7_surfaces[i].used&&g_wl7_surfaces[i].id==id)return&g_wl7_surfaces[i];return 0;}
static wl7_buffer_t*wl7_find_buffer(uint32_t id){int i;for(i=0;i<WL7_MAX_BUFFERS;++i)if(g_wl7_buffers[i].used&&g_wl7_buffers[i].id==id)return&g_wl7_buffers[i];return 0;}
static int wl7_find_buffer_index(uint32_t id){int i;for(i=0;i<WL7_MAX_BUFFERS;++i)if(g_wl7_buffers[i].used&&g_wl7_buffers[i].id==id)return i;return -1;}

#define WL7_META_MAX_OBJECTS 192
#define WL7_MAX_POOLS 64
#define WL7_MAX_CALLBACKS 128
#define WL7_MAX_VIEWPORTS WL7_MAX_SURFACES
#define WL7_MAX_DMABUF_PARAMS 64
#define WL7_MAX_PRESENT_FEEDBACK 128
#define WL7_MAX_TITLE 96
#define WL7_MAX_APP_ID 96

#define WL7_OBJ_NONE         0
#define WL7_OBJ_DISPLAY      1
#define WL7_OBJ_REGISTRY     2
#define WL7_OBJ_COMPOSITOR   3
#define WL7_OBJ_SHM          4
#define WL7_OBJ_SHELL        5
#define WL7_OBJ_SEAT         6
#define WL7_OBJ_OUTPUT       7
#define WL7_OBJ_SURFACE      8
#define WL7_OBJ_BUFFER       9
#define WL7_OBJ_POOL         10
#define WL7_OBJ_SHELL_SURF   11
#define WL7_OBJ_CALLBACK     12
#define WL7_OBJ_POINTER      13
#define WL7_OBJ_KEYBOARD     14
#define WL7_OBJ_XDG_WM_BASE  15
#define WL7_OBJ_XDG_SURFACE  16
#define WL7_OBJ_XDG_TOPLEVEL 17
#define WL7_OBJ_TOUCH        18
#define WL7_OBJ_VIEWPORTER   19
#define WL7_OBJ_VIEWPORT     20
#define WL7_OBJ_LINUX_DMABUF 21
#define WL7_OBJ_DMABUF_PARAM 22
#define WL7_OBJ_PRESENTATION 23
#define WL7_OBJ_PRESENT_FB   24
#define WL7_OBJ_REGION       25

typedef struct {
    bool     used;
    uint32_t id;
    uint16_t kind;
    int      ref;
} wl7_objmeta_t;

typedef struct {
    bool     used;
    uint32_t id;
    int      client_idx;
    int      fd;
    uint32_t size;
} wl7_pool_t;

typedef struct {
    bool     used;
    uint32_t id;
    int      client_idx;
    uint32_t surface_id;
    bool     pending;
    uint32_t due_ms;
} wl7_callback_t;

typedef struct {
    bool     used;
    uint32_t id;
    int      client_idx;
    uint32_t surface_id;
    uint32_t xdg_surface_id;
    bool     configured;
    bool     activated;
    int32_t  width;
    int32_t  height;
    uint32_t last_configure_serial;
    char     title[WL7_MAX_TITLE];
    char     app_id[WL7_MAX_APP_ID];
} wl7_xdg_toplevel_t;

typedef struct {
    bool     used;
    uint32_t id;
    int      client_idx;
    uint32_t surface_id;
    uint32_t toplevel_id;
    uint32_t last_configure_serial;
    uint32_t last_ack_serial;
    bool     configured;
    bool     initial_commit_seen;
    bool     configure_pending;
} wl7_xdg_surface_t;

typedef struct {
    bool     used;
    uint32_t id;
    int      client_idx;
    uint32_t surface_id;
    bool     src_set;
    int32_t  src_x;
    int32_t  src_y;
    int32_t  src_w;
    int32_t  src_h;
    bool     dst_set;
    int32_t  dst_w;
    int32_t  dst_h;
} wl7_viewport_t;

typedef struct {
    bool     used;
    uint32_t id;
    int      client_idx;
    int      fd;
    uint32_t offset;
    uint32_t stride;
    uint32_t format;
    uint32_t width;
    uint32_t height;
    bool     plane0_set;
} wl7_dmabuf_params_t;

typedef struct {
    bool     used;
    uint32_t id;
    int      client_idx;
    uint32_t surface_id;
    bool     pending;
    uint32_t due_ms;
} wl7_presentation_fb_t;

static wl7_objmeta_t g_wl7_meta[WL7_MAX_CLIENTS][WL7_META_MAX_OBJECTS];
static wl7_pool_t g_wl7_pools[WL7_MAX_POOLS];
static wl7_callback_t g_wl7_callbacks[WL7_MAX_CALLBACKS];
static wl7_xdg_surface_t g_wl7_xdg_surfaces[WL7_MAX_SURFACES];
static wl7_xdg_toplevel_t g_wl7_xdg_toplevels[WL7_MAX_SURFACES];
static wl7_viewport_t g_wl7_viewports[WL7_MAX_VIEWPORTS];
static wl7_dmabuf_params_t g_wl7_dmabuf_params[WL7_MAX_DMABUF_PARAMS];
static wl7_presentation_fb_t g_wl7_present_fbs[WL7_MAX_PRESENT_FEEDBACK];
static int g_wl7_buffer_pool[WL7_MAX_BUFFERS];
static uint32_t g_wl7_buffer_offset[WL7_MAX_BUFFERS];
static bool g_wl7_buffer_dirty[WL7_MAX_BUFFERS];
static bool g_wl7_buffer_owns_fd[WL7_MAX_BUFFERS];
static uint32_t g_wl7_client_registry_obj[WL7_MAX_CLIENTS];
static uint32_t g_wl7_client_seat_obj[WL7_MAX_CLIENTS];
static uint32_t g_wl7_client_pointer_obj[WL7_MAX_CLIENTS];
static uint32_t g_wl7_client_keyboard_obj[WL7_MAX_CLIENTS];
static uint32_t g_wl7_client_touch_obj[WL7_MAX_CLIENTS];
static uint32_t g_wl7_client_output_obj[WL7_MAX_CLIENTS];
static uint32_t g_wl7_client_xdg_wm_base_obj[WL7_MAX_CLIENTS];
static uint32_t g_wl7_client_viewporter_obj[WL7_MAX_CLIENTS];
static uint32_t g_wl7_client_dmabuf_obj[WL7_MAX_CLIENTS];
static uint32_t g_wl7_client_presentation_obj[WL7_MAX_CLIENTS];
static uint32_t g_wl7_client_pointer_focus[WL7_MAX_CLIENTS];
static uint32_t g_wl7_client_keyboard_focus[WL7_MAX_CLIENTS];
static uint32_t g_wl7_client_touch_focus[WL7_MAX_CLIENTS];
static bool g_wl7_client_processing[WL7_MAX_CLIENTS];
static bool g_wl7_client_touch_active[WL7_MAX_CLIENTS];
static int32_t g_wl7_client_touch_x[WL7_MAX_CLIENTS];
static int32_t g_wl7_client_touch_y[WL7_MAX_CLIENTS];
static int g_wl7_active_client=-1;
static uint32_t g_wl7_active_surface=0;
static bool g_wl7_surface_has_damage[WL7_MAX_SURFACES];
static bool g_wl7_surface_full_damage[WL7_MAX_SURFACES];
static bool g_wl7_surface_release_pending[WL7_MAX_SURFACES];
static int32_t g_wl7_surface_damage_x[WL7_MAX_SURFACES];
static int32_t g_wl7_surface_damage_y[WL7_MAX_SURFACES];
static int32_t g_wl7_surface_damage_w[WL7_MAX_SURFACES];
static int32_t g_wl7_surface_damage_h[WL7_MAX_SURFACES];
static uint32_t g_wl7_serial=1u;
static uint32_t g_wl7_frame_clock_ms=0u;
static uint32_t g_wl7_send_trace_count;

static uint32_t wl7_rd_u32(const uint8_t *p){
    return ((uint32_t)p[0])|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);
}
static int32_t wl7_rd_s32(const uint8_t *p){return (int32_t)wl7_rd_u32(p);}
static void wl7_wr_u32(uint8_t *p,uint32_t v){
    p[0]=(uint8_t)v;
    p[1]=(uint8_t)(v>>8);
    p[2]=(uint8_t)(v>>16);
    p[3]=(uint8_t)(v>>24);
}
static size_t wl7_align4(size_t n){return (n+3u)&~3u;}
static bool wl7_read_wl_string(const uint8_t *buf,size_t len,size_t *off,char *out,size_t cap){
    size_t p=wl7_align4(*off);
    uint32_t sl;
    if(!buf||!off||!out||cap<2)return false;
    if(p+4>len)return false;
    sl=wl7_rd_u32(buf+p);
    p+=4;
    if(p+sl>len)return false;
    if(sl==0){out[0]=0;*off=p;return true;}
    if(sl>=cap)sl=(uint32_t)(cap-1);
    ulibc_memcpy(out,buf+p,sl);
    out[sl]=0;
    p=wl7_align4(p+(size_t)wl7_rd_u32(buf+wl7_align4(*off)));
    *off=p;
    return true;
}
static wl7_objmeta_t*wl7_meta_find(int client_idx,uint32_t id){
    int i;
    if(client_idx<0||client_idx>=WL7_MAX_CLIENTS||id==0)return 0;
    for(i=0;i<WL7_META_MAX_OBJECTS;++i){
        if(!g_wl7_meta[client_idx][i].used)continue;
        if(g_wl7_meta[client_idx][i].id==id)return &g_wl7_meta[client_idx][i];
    }
    return 0;
}
static wl7_objmeta_t*wl7_meta_set(int client_idx,uint32_t id,uint16_t kind,int ref){
    wl7_objmeta_t *m=wl7_meta_find(client_idx,id);
    int i;
    if(client_idx<0||client_idx>=WL7_MAX_CLIENTS||id==0)return 0;
    if(m){m->kind=kind;m->ref=ref;return m;}
    for(i=0;i<WL7_META_MAX_OBJECTS;++i){
        if(g_wl7_meta[client_idx][i].used)continue;
        g_wl7_meta[client_idx][i].used=true;
        g_wl7_meta[client_idx][i].id=id;
        g_wl7_meta[client_idx][i].kind=kind;
        g_wl7_meta[client_idx][i].ref=ref;
        return &g_wl7_meta[client_idx][i];
    }
    return 0;
}
static void wl7_meta_drop(int client_idx,uint32_t id){
    wl7_objmeta_t *m=wl7_meta_find(client_idx,id);
    if(!m)return;
    m->used=false;
    m->id=0;
    m->kind=WL7_OBJ_NONE;
    m->ref=0;
}
static wl7_pool_t*wl7_find_pool(uint32_t id){
    int i;
    for(i=0;i<WL7_MAX_POOLS;++i)if(g_wl7_pools[i].used&&g_wl7_pools[i].id==id)return &g_wl7_pools[i];
    return 0;
}
static wl7_pool_t*wl7_alloc_pool(void){
    int i;
    for(i=0;i<WL7_MAX_POOLS;++i){
        if(g_wl7_pools[i].used)continue;
        ulibc_memset(&g_wl7_pools[i],0,sizeof(g_wl7_pools[i]));
        g_wl7_pools[i].used=true;
        g_wl7_pools[i].fd=-1;
        return &g_wl7_pools[i];
    }
    return 0;
}
static wl7_callback_t*wl7_find_callback(uint32_t id){
    int i;
    for(i=0;i<WL7_MAX_CALLBACKS;++i)if(g_wl7_callbacks[i].used&&g_wl7_callbacks[i].id==id)return &g_wl7_callbacks[i];
    return 0;
}
static wl7_callback_t*wl7_alloc_callback(void){
    int i;
    for(i=0;i<WL7_MAX_CALLBACKS;++i){
        if(g_wl7_callbacks[i].used)continue;
        ulibc_memset(&g_wl7_callbacks[i],0,sizeof(g_wl7_callbacks[i]));
        g_wl7_callbacks[i].used=true;
        return &g_wl7_callbacks[i];
    }
    return 0;
}
static wl7_xdg_surface_t*wl7_find_xdg_surface(uint32_t id){
    int i;
    for(i=0;i<WL7_MAX_SURFACES;++i)if(g_wl7_xdg_surfaces[i].used&&g_wl7_xdg_surfaces[i].id==id)return &g_wl7_xdg_surfaces[i];
    return 0;
}
static wl7_xdg_surface_t*wl7_alloc_xdg_surface(void){
    int i;
    for(i=0;i<WL7_MAX_SURFACES;++i){
        if(g_wl7_xdg_surfaces[i].used)continue;
        ulibc_memset(&g_wl7_xdg_surfaces[i],0,sizeof(g_wl7_xdg_surfaces[i]));
        g_wl7_xdg_surfaces[i].used=true;
        return &g_wl7_xdg_surfaces[i];
    }
    return 0;
}
static wl7_xdg_toplevel_t*wl7_find_xdg_toplevel(uint32_t id){
    int i;
    for(i=0;i<WL7_MAX_SURFACES;++i)if(g_wl7_xdg_toplevels[i].used&&g_wl7_xdg_toplevels[i].id==id)return &g_wl7_xdg_toplevels[i];
    return 0;
}
static wl7_xdg_toplevel_t*wl7_alloc_xdg_toplevel(void){
    int i;
    for(i=0;i<WL7_MAX_SURFACES;++i){
        if(g_wl7_xdg_toplevels[i].used)continue;
        ulibc_memset(&g_wl7_xdg_toplevels[i],0,sizeof(g_wl7_xdg_toplevels[i]));
        g_wl7_xdg_toplevels[i].used=true;
        return &g_wl7_xdg_toplevels[i];
    }
    return 0;
}
static wl7_viewport_t*wl7_find_viewport(uint32_t id){
    int i;
    for(i=0;i<WL7_MAX_VIEWPORTS;++i)if(g_wl7_viewports[i].used&&g_wl7_viewports[i].id==id)return &g_wl7_viewports[i];
    return 0;
}
static wl7_viewport_t*wl7_find_viewport_by_surface(uint32_t surface_id){
    int i;
    for(i=0;i<WL7_MAX_VIEWPORTS;++i){
        if(!g_wl7_viewports[i].used)continue;
        if(g_wl7_viewports[i].surface_id==surface_id)return &g_wl7_viewports[i];
    }
    return 0;
}
static wl7_viewport_t*wl7_alloc_viewport(void){
    int i;
    for(i=0;i<WL7_MAX_VIEWPORTS;++i){
        if(g_wl7_viewports[i].used)continue;
        ulibc_memset(&g_wl7_viewports[i],0,sizeof(g_wl7_viewports[i]));
        g_wl7_viewports[i].used=true;
        return &g_wl7_viewports[i];
    }
    return 0;
}
static wl7_dmabuf_params_t*wl7_find_dmabuf_params(uint32_t id){
    int i;
    for(i=0;i<WL7_MAX_DMABUF_PARAMS;++i){
        if(g_wl7_dmabuf_params[i].used&&g_wl7_dmabuf_params[i].id==id)return &g_wl7_dmabuf_params[i];
    }
    return 0;
}
static wl7_dmabuf_params_t*wl7_alloc_dmabuf_params(void){
    int i;
    for(i=0;i<WL7_MAX_DMABUF_PARAMS;++i){
        if(g_wl7_dmabuf_params[i].used)continue;
        ulibc_memset(&g_wl7_dmabuf_params[i],0,sizeof(g_wl7_dmabuf_params[i]));
        g_wl7_dmabuf_params[i].used=true;
        g_wl7_dmabuf_params[i].fd=-1;
        return &g_wl7_dmabuf_params[i];
    }
    return 0;
}
static wl7_presentation_fb_t*wl7_find_present_fb(uint32_t id){
    int i;
    for(i=0;i<WL7_MAX_PRESENT_FEEDBACK;++i){
        if(g_wl7_present_fbs[i].used&&g_wl7_present_fbs[i].id==id)return &g_wl7_present_fbs[i];
    }
    return 0;
}
static wl7_presentation_fb_t*wl7_alloc_present_fb(void){
    int i;
    for(i=0;i<WL7_MAX_PRESENT_FEEDBACK;++i){
        if(g_wl7_present_fbs[i].used)continue;
        ulibc_memset(&g_wl7_present_fbs[i],0,sizeof(g_wl7_present_fbs[i]));
        g_wl7_present_fbs[i].used=true;
        return &g_wl7_present_fbs[i];
    }
    return 0;
}
static int32_t wl7_fixed_to_int(int32_t fx){
    return (fx>=0)?(fx>>8):-(((-fx)>>8));
}
static void wl7_buffer_reset_slot(int bidx,int client_idx,bool drop_meta){
    uint32_t old_id;
    if(bidx<0||bidx>=WL7_MAX_BUFFERS||!g_wl7_buffers[bidx].used)return;
    old_id=g_wl7_buffers[bidx].id;
    if(g_wl7_buffer_owns_fd[bidx]&&g_wl7_buffers[bidx].fd>=0){
        (void)real_sys_close(g_wl7_buffers[bidx].fd);
    }
    if(g_wl7_buffers[bidx].data){
        ulibc_free(g_wl7_buffers[bidx].data);
    }
    ulibc_memset(&g_wl7_buffers[bidx],0,sizeof(g_wl7_buffers[bidx]));
    g_wl7_buffer_pool[bidx]=0;
    g_wl7_buffer_offset[bidx]=0;
    g_wl7_buffer_dirty[bidx]=false;
    g_wl7_buffer_owns_fd[bidx]=false;
    if(drop_meta&&client_idx>=0&&client_idx<WL7_MAX_CLIENTS&&old_id){
        wl7_meta_drop(client_idx,old_id);
    }
}
static uint32_t wl7_next_serial(void){
    uint32_t s=g_wl7_serial++;
    if(s==0){
        s=1;
        g_wl7_serial=2;
    }
    return s;
}
static int32_t wl7_fixed_from_int(int v){
    return (int32_t)(v<<8);
}
static uint32_t wl7_linux_button_code(uint32_t button){
    switch(button){
        case 1: return 0x110u; /* BTN_LEFT */
        case 2: return 0x112u; /* BTN_MIDDLE */
        case 3: return 0x111u; /* BTN_RIGHT */
        case 4: return 0x113u; /* BTN_SIDE */
        case 5: return 0x114u; /* BTN_EXTRA */
        default: return button;
    }
}
static int wl7_surface_index(uint32_t id){
    int i;
    for(i=0;i<WL7_MAX_SURFACES;++i){
        if(g_wl7_surfaces[i].used&&g_wl7_surfaces[i].id==id)return i;
    }
    return -1;
}
static wl7_xdg_surface_t*wl7_find_xdg_surface_by_surface(uint32_t surface_id){
    int i;
    for(i=0;i<WL7_MAX_SURFACES;++i){
        if(!g_wl7_xdg_surfaces[i].used)continue;
        if(g_wl7_xdg_surfaces[i].surface_id==surface_id)return &g_wl7_xdg_surfaces[i];
    }
    return 0;
}
static wl7_xdg_toplevel_t*wl7_find_xdg_toplevel_by_surface(uint32_t surface_id){
    int i;
    for(i=0;i<WL7_MAX_SURFACES;++i){
        if(!g_wl7_xdg_toplevels[i].used)continue;
        if(g_wl7_xdg_toplevels[i].surface_id==surface_id)return &g_wl7_xdg_toplevels[i];
    }
    return 0;
}
static void wl7_clear_surface_damage(int sidx){
    if(sidx<0||sidx>=WL7_MAX_SURFACES)return;
    g_wl7_surface_has_damage[sidx]=false;
    g_wl7_surface_full_damage[sidx]=false;
    g_wl7_surface_damage_x[sidx]=0;
    g_wl7_surface_damage_y[sidx]=0;
    g_wl7_surface_damage_w[sidx]=0;
    g_wl7_surface_damage_h[sidx]=0;
}
static void wl7_mark_surface_damage(uint32_t surface_id,int32_t x,int32_t y,int32_t w,int32_t h,bool full){
    int sidx=wl7_surface_index(surface_id);
    if(sidx<0)return;
    if(full||w<=0||h<=0){
        g_wl7_surface_has_damage[sidx]=true;
        g_wl7_surface_full_damage[sidx]=true;
        g_wl7_surface_damage_x[sidx]=0;
        g_wl7_surface_damage_y[sidx]=0;
        g_wl7_surface_damage_w[sidx]=0;
        g_wl7_surface_damage_h[sidx]=0;
        return;
    }
    if(!g_wl7_surface_has_damage[sidx]||g_wl7_surface_full_damage[sidx]){
        g_wl7_surface_has_damage[sidx]=true;
        g_wl7_surface_full_damage[sidx]=false;
        g_wl7_surface_damage_x[sidx]=x;
        g_wl7_surface_damage_y[sidx]=y;
        g_wl7_surface_damage_w[sidx]=w;
        g_wl7_surface_damage_h[sidx]=h;
        return;
    }
    {
        int32_t x0=g_wl7_surface_damage_x[sidx];
        int32_t y0=g_wl7_surface_damage_y[sidx];
        int32_t x1=x0+g_wl7_surface_damage_w[sidx];
        int32_t y1=y0+g_wl7_surface_damage_h[sidx];
        int32_t nx1=x+w;
        int32_t ny1=y+h;
        if(x<x0)x0=x;
        if(y<y0)y0=y;
        if(nx1>x1)x1=nx1;
        if(ny1>y1)y1=ny1;
        g_wl7_surface_damage_x[sidx]=x0;
        g_wl7_surface_damage_y[sidx]=y0;
        g_wl7_surface_damage_w[sidx]=x1-x0;
        g_wl7_surface_damage_h[sidx]=y1-y0;
    }
}
static int wl7_send_event(int sock_fd,uint32_t obj,uint16_t opcode,const void *payload,size_t payload_len){
    uint8_t msg[1024];
    uint32_t op_sz;
    size_t total=8u+wl7_align4(payload_len);
    if(total>sizeof(msg)||total<8u)return -E2BIG;
    ulibc_memset(msg,0,total);
    wl7_wr_u32(msg,obj);
    op_sz=((uint32_t)total<<16)|opcode;
    wl7_wr_u32(msg+4,op_sz);
    if(payload_len&&payload)ulibc_memcpy(msg+8,payload,payload_len);
    return sock_send(sock_fd,msg,total,0);
}
static void wl7_send_display_delete_id(int sock_fd,uint32_t id){
    uint8_t pld[4];
    wl7_wr_u32(pld,id);
    (void)wl7_send_event(sock_fd,WL7_DISPLAY_ID,1,pld,4); /* wl_display.delete_id */
}
static void wl7_send_callback_done(int sock_fd,uint32_t callback_id,uint32_t time_ms){
    uint8_t pld[4];
    wl7_wr_u32(pld,time_ms);
    (void)wl7_send_event(sock_fd,callback_id,0,pld,4);
    wl7_send_display_delete_id(sock_fd,callback_id);
}
static void wl7_send_buffer_release(int sock_fd,uint32_t buffer_obj){
    (void)wl7_send_event(sock_fd,buffer_obj,0,0,0);
}
static void wl7_send_surface_enter(int sock_fd,uint32_t surface_obj,uint32_t output_obj){
    uint8_t pld[4];
    wl7_wr_u32(pld,output_obj?output_obj:WL7_OUTPUT_ID);
    (void)wl7_send_event(sock_fd,surface_obj,0,pld,4);
}
static void wl7_send_surface_leave(int sock_fd,uint32_t surface_obj,uint32_t output_obj){
    uint8_t pld[4];
    wl7_wr_u32(pld,output_obj?output_obj:WL7_OUTPUT_ID);
    (void)wl7_send_event(sock_fd,surface_obj,1,pld,4);
}
static void wl7_send_pointer_enter(int sock_fd,uint32_t pointer_obj,uint32_t surface_obj,int sx,int sy){
    uint8_t pld[16];
    wl7_wr_u32(pld+0,wl7_next_serial());
    wl7_wr_u32(pld+4,surface_obj);
    wl7_wr_u32(pld+8,(uint32_t)wl7_fixed_from_int(sx));
    wl7_wr_u32(pld+12,(uint32_t)wl7_fixed_from_int(sy));
    (void)wl7_send_event(sock_fd,pointer_obj,0,pld,16);
}
static void wl7_send_pointer_leave(int sock_fd,uint32_t pointer_obj,uint32_t surface_obj){
    uint8_t pld[8];
    wl7_wr_u32(pld+0,wl7_next_serial());
    wl7_wr_u32(pld+4,surface_obj);
    (void)wl7_send_event(sock_fd,pointer_obj,1,pld,8);
}
static void wl7_send_keyboard_keymap(int sock_fd,uint32_t keyboard_obj){
    uint8_t pld[12];
    wl7_wr_u32(pld+0,0); /* WL_KEYBOARD_KEYMAP_FORMAT_NO_KEYMAP */
    wl7_wr_u32(pld+4,0); /* no fd */
    wl7_wr_u32(pld+8,0); /* size */
    (void)wl7_send_event(sock_fd,keyboard_obj,0,pld,12);
}
static void wl7_send_keyboard_repeat_info(int sock_fd,uint32_t keyboard_obj){
    uint8_t pld[8];
    wl7_wr_u32(pld+0,25);  /* rate */
    wl7_wr_u32(pld+4,600); /* delay */
    (void)wl7_send_event(sock_fd,keyboard_obj,5,pld,8);
}
static void wl7_send_keyboard_modifiers(int sock_fd,uint32_t keyboard_obj){
    uint8_t pld[20];
    wl7_wr_u32(pld+0,wl7_next_serial());
    wl7_wr_u32(pld+4,0);
    wl7_wr_u32(pld+8,0);
    wl7_wr_u32(pld+12,0);
    wl7_wr_u32(pld+16,0);
    (void)wl7_send_event(sock_fd,keyboard_obj,4,pld,20);
}
static void wl7_send_keyboard_enter(int sock_fd,uint32_t keyboard_obj,uint32_t surface_obj){
    uint8_t pld[12];
    wl7_wr_u32(pld+0,wl7_next_serial());
    wl7_wr_u32(pld+4,surface_obj);
    wl7_wr_u32(pld+8,0); /* empty array of pressed keys */
    (void)wl7_send_event(sock_fd,keyboard_obj,1,pld,12);
    wl7_send_keyboard_modifiers(sock_fd,keyboard_obj);
}
static void wl7_send_keyboard_leave(int sock_fd,uint32_t keyboard_obj,uint32_t surface_obj){
    uint8_t pld[8];
    wl7_wr_u32(pld+0,wl7_next_serial());
    wl7_wr_u32(pld+4,surface_obj);
    (void)wl7_send_event(sock_fd,keyboard_obj,2,pld,8);
}
static void wl7_send_touch_down(int sock_fd,uint32_t touch_obj,uint32_t surface_obj,int32_t x,int32_t y){
    uint8_t pld[24];
    wl7_wr_u32(pld+0,wl7_next_serial());
    wl7_wr_u32(pld+4,g_wl7_frame_clock_ms);
    wl7_wr_u32(pld+8,surface_obj);
    wl7_wr_u32(pld+12,0); /* touch id */
    wl7_wr_u32(pld+16,(uint32_t)wl7_fixed_from_int(x));
    wl7_wr_u32(pld+20,(uint32_t)wl7_fixed_from_int(y));
    (void)wl7_send_event(sock_fd,touch_obj,0,pld,24);
}
static void wl7_send_touch_up(int sock_fd,uint32_t touch_obj){
    uint8_t pld[12];
    wl7_wr_u32(pld+0,wl7_next_serial());
    wl7_wr_u32(pld+4,g_wl7_frame_clock_ms);
    wl7_wr_u32(pld+8,0); /* touch id */
    (void)wl7_send_event(sock_fd,touch_obj,1,pld,12);
}
static void wl7_send_touch_motion(int sock_fd,uint32_t touch_obj,int32_t x,int32_t y){
    uint8_t pld[16];
    wl7_wr_u32(pld+0,g_wl7_frame_clock_ms);
    wl7_wr_u32(pld+4,0); /* touch id */
    wl7_wr_u32(pld+8,(uint32_t)wl7_fixed_from_int(x));
    wl7_wr_u32(pld+12,(uint32_t)wl7_fixed_from_int(y));
    (void)wl7_send_event(sock_fd,touch_obj,2,pld,16);
}
static void wl7_send_touch_frame(int sock_fd,uint32_t touch_obj){
    (void)wl7_send_event(sock_fd,touch_obj,3,0,0);
}
static void wl7_send_touch_cancel(int sock_fd,uint32_t touch_obj){
    (void)wl7_send_event(sock_fd,touch_obj,4,0,0);
}
static void wl7_send_dmabuf_formats(int sock_fd,uint32_t dmabuf_obj){
    uint8_t pld[12];
    wl7_wr_u32(pld+0,WL7_FMT_DRM_ARGB32);
    (void)wl7_send_event(sock_fd,dmabuf_obj,0,pld,4); /* format */
    wl7_wr_u32(pld+0,WL7_FMT_DRM_XRGB32);
    (void)wl7_send_event(sock_fd,dmabuf_obj,0,pld,4); /* format */
    wl7_wr_u32(pld+0,WL7_FMT_DRM_ABGR32);
    (void)wl7_send_event(sock_fd,dmabuf_obj,0,pld,4); /* format */
    wl7_wr_u32(pld+0,WL7_FMT_DRM_XBGR32);
    (void)wl7_send_event(sock_fd,dmabuf_obj,0,pld,4); /* format */
    wl7_wr_u32(pld+0,WL7_FMT_DRM_XRGB32); /* format */
    wl7_wr_u32(pld+4,0); /* modifier_hi */
    wl7_wr_u32(pld+8,0); /* modifier_lo (linear/invalid) */
    (void)wl7_send_event(sock_fd,dmabuf_obj,1,pld,12); /* modifier */
}
static void wl7_send_presentation_clock_id(int sock_fd,uint32_t presentation_obj){
    uint8_t pld[4];
    wl7_wr_u32(pld,1); /* CLOCK_MONOTONIC */
    (void)wl7_send_event(sock_fd,presentation_obj,0,pld,4);
}
static void wl7_send_presentation_feedback(int sock_fd,uint32_t feedback_obj,uint32_t output_obj){
    uint8_t out_pld[4];
    uint8_t prs_pld[28];
    uint64_t ns=(uint64_t)g_wl7_frame_clock_ms*1000000ull;
    uint64_t sec=ns/1000000000ull;
    uint32_t nsec=(uint32_t)(ns%1000000000ull);
    wl7_wr_u32(out_pld,output_obj?output_obj:WL7_OUTPUT_ID);
    (void)wl7_send_event(sock_fd,feedback_obj,0,out_pld,4); /* sync_output */
    wl7_wr_u32(prs_pld+0,(uint32_t)(sec>>32));
    wl7_wr_u32(prs_pld+4,(uint32_t)(sec&0xFFFFFFFFu));
    wl7_wr_u32(prs_pld+8,nsec);
    wl7_wr_u32(prs_pld+12,16666666u); /* refresh ns @ 60Hz */
    wl7_wr_u32(prs_pld+16,0);         /* seq_hi */
    wl7_wr_u32(prs_pld+20,g_wl7_frame_clock_ms/16u); /* seq_lo */
    wl7_wr_u32(prs_pld+24,1);         /* VSYNC */
    (void)wl7_send_event(sock_fd,feedback_obj,1,prs_pld,28); /* presented */
    wl7_send_display_delete_id(sock_fd,feedback_obj);
}
static void wl7_send_xdg_wm_base_ping(int sock_fd,uint32_t xdg_wm_base_obj,uint32_t serial){
    uint8_t pld[4];
    wl7_wr_u32(pld,serial);
    (void)wl7_send_event(sock_fd,xdg_wm_base_obj,0,pld,4);
}
static void wl7_send_xdg_surface_configure(int sock_fd,uint32_t xdg_surface_obj,uint32_t serial){
    uint8_t pld[4];
    wl7_wr_u32(pld,serial);
    (void)wl7_send_event(sock_fd,xdg_surface_obj,0,pld,4);
}
static void wl7_send_xdg_toplevel_configure(int sock_fd,uint32_t toplevel_obj,int32_t w,int32_t h,bool activated){
    uint8_t pld[16];
    size_t len=12;
    wl7_wr_u32(pld+0,(uint32_t)w);
    wl7_wr_u32(pld+4,(uint32_t)h);
    if(activated){
        wl7_wr_u32(pld+8,4);  /* states array byte length */
        wl7_wr_u32(pld+12,4); /* XDG_TOPLEVEL_STATE_ACTIVATED */
        len=16;
    }else{
        wl7_wr_u32(pld+8,0);  /* empty states array */
    }
    (void)wl7_send_event(sock_fd,toplevel_obj,0,pld,len);
}
static void wl7_send_xdg_toplevel_close(int sock_fd,uint32_t toplevel_obj){
    (void)wl7_send_event(sock_fd,toplevel_obj,1,0,0);
}
static void wl7_send_initial_focus_events(int client_idx,uint32_t surface_id){
    wl7_client_t *cl;
    uint32_t ptr_obj;
    uint32_t kbd_obj;
    uint32_t touch_obj;
    uint32_t out_obj;
    if(client_idx<0||client_idx>=WL7_MAX_CLIENTS)return;
    cl=&g_wl7_clients[client_idx];
    if(!cl->used)return;
    out_obj=g_wl7_client_output_obj[client_idx];
    wl7_send_surface_enter(cl->sock_fd,surface_id,out_obj?out_obj:WL7_OUTPUT_ID);
    ptr_obj=g_wl7_client_pointer_obj[client_idx];
    kbd_obj=g_wl7_client_keyboard_obj[client_idx];
    if(ptr_obj){
        wl7_send_pointer_enter(cl->sock_fd,ptr_obj,surface_id,0,0);
        g_wl7_client_pointer_focus[client_idx]=surface_id;
    }
    if(kbd_obj){
        wl7_send_keyboard_enter(cl->sock_fd,kbd_obj,surface_id);
        g_wl7_client_keyboard_focus[client_idx]=surface_id;
    }
    touch_obj=g_wl7_client_touch_obj[client_idx];
    if(touch_obj){
        g_wl7_client_touch_focus[client_idx]=surface_id;
    }
    g_wl7_active_client=client_idx;
    g_wl7_active_surface=surface_id;
}
static void wl7_send_focus_leave_events(int client_idx,uint32_t surface_id){
    wl7_client_t *cl;
    uint32_t ptr_obj;
    uint32_t kbd_obj;
    uint32_t touch_obj;
    uint32_t out_obj;
    if(client_idx<0||client_idx>=WL7_MAX_CLIENTS)return;
    cl=&g_wl7_clients[client_idx];
    if(!cl->used)return;
    out_obj=g_wl7_client_output_obj[client_idx];
    wl7_send_surface_leave(cl->sock_fd,surface_id,out_obj?out_obj:WL7_OUTPUT_ID);
    ptr_obj=g_wl7_client_pointer_obj[client_idx];
    if(ptr_obj&&g_wl7_client_pointer_focus[client_idx]==surface_id){
        wl7_send_pointer_leave(cl->sock_fd,ptr_obj,surface_id);
        g_wl7_client_pointer_focus[client_idx]=0;
    }
    kbd_obj=g_wl7_client_keyboard_obj[client_idx];
    if(kbd_obj&&g_wl7_client_keyboard_focus[client_idx]==surface_id){
        wl7_send_keyboard_leave(cl->sock_fd,kbd_obj,surface_id);
        g_wl7_client_keyboard_focus[client_idx]=0;
    }
    touch_obj=g_wl7_client_touch_obj[client_idx];
    if(touch_obj&&g_wl7_client_touch_focus[client_idx]==surface_id){
        if(g_wl7_client_touch_active[client_idx]){
            wl7_send_touch_cancel(cl->sock_fd,touch_obj);
        }
        g_wl7_client_touch_focus[client_idx]=0;
        g_wl7_client_touch_active[client_idx]=false;
    }
    if(g_wl7_active_client==client_idx&&g_wl7_active_surface==surface_id){
        g_wl7_active_client=-1;
        g_wl7_active_surface=0;
    }
}
static bool wl7_surface_accepts_input(int client_idx,uint32_t surface_id){
    wl7_surface_t *s=wl7_find_surface(surface_id);
    if(!s||!s->used||!s->mapped)return false;
    return s->client_idx==client_idx;
}
static void wl7_set_active_surface(int client_idx,uint32_t surface_id,int sx,int sy,bool pointer,bool keyboard){
    wl7_client_t *cl;
    uint32_t ptr_obj;
    uint32_t kbd_obj;
    uint32_t old_surface;
    if(client_idx<0||client_idx>=WL7_MAX_CLIENTS)return;
    if(!wl7_surface_accepts_input(client_idx,surface_id))return;
    cl=&g_wl7_clients[client_idx];
    if(!cl->used)return;
    ptr_obj=g_wl7_client_pointer_obj[client_idx];
    kbd_obj=g_wl7_client_keyboard_obj[client_idx];
    if(pointer&&ptr_obj){
        old_surface=g_wl7_client_pointer_focus[client_idx];
        if(old_surface&&old_surface!=surface_id){
            wl7_send_pointer_leave(cl->sock_fd,ptr_obj,old_surface);
        }
        if(old_surface!=surface_id){
            wl7_send_pointer_enter(cl->sock_fd,ptr_obj,surface_id,sx,sy);
        }
        g_wl7_client_pointer_focus[client_idx]=surface_id;
    }
    if(keyboard&&kbd_obj){
        old_surface=g_wl7_client_keyboard_focus[client_idx];
        if(old_surface&&old_surface!=surface_id){
            wl7_send_keyboard_leave(cl->sock_fd,kbd_obj,old_surface);
        }
        if(old_surface!=surface_id){
            wl7_send_keyboard_enter(cl->sock_fd,kbd_obj,surface_id);
        }
        g_wl7_client_keyboard_focus[client_idx]=surface_id;
    }
    if(pointer||keyboard){
        g_wl7_active_client=client_idx;
        g_wl7_active_surface=surface_id;
    }
}
static void wl7_schedule_xdg_configure(int client_idx,uint32_t surface_id){
    wl7_client_t *cl;
    wl7_xdg_surface_t *xs;
    wl7_xdg_toplevel_t *tl;
    uint32_t serial;
    if(client_idx<0||client_idx>=WL7_MAX_CLIENTS)return;
    cl=&g_wl7_clients[client_idx];
    if(!cl->used)return;
    xs=wl7_find_xdg_surface_by_surface(surface_id);
    if(!xs)return;
    if(!xs->initial_commit_seen){
        xs->configure_pending=true;
        return;
    }
    if(xs->last_configure_serial&&xs->last_ack_serial!=xs->last_configure_serial){
        xs->configure_pending=true;
        return;
    }
    serial=wl7_next_serial();
    xs->last_configure_serial=serial;
    xs->configured=false;
    xs->configure_pending=false;
    if(xs->toplevel_id){
        tl=wl7_find_xdg_toplevel(xs->toplevel_id);
        if(tl){
            bool initial_configure=(xs->last_ack_serial==0);
            int32_t cw=initial_configure?0:(tl->width>0?tl->width:1280);
            int32_t ch=initial_configure?0:(tl->height>0?tl->height:720);
            bool active=initial_configure?false:tl->activated;
            tl->last_configure_serial=serial;
            tl->configured=false;
            wl7_send_xdg_toplevel_configure(cl->sock_fd,tl->id,cw,ch,active);
            if(g_wl7_send_trace_count<WL7_TRACE_SEND_LIMIT){
                ++g_wl7_send_trace_count;
                __boot_serial_puts("[wl7-send] xdg_toplevel.configure obj=");
                __boot_serial_puthex64((uint64_t)tl->id);
                __boot_serial_puts(" surf=");
                __boot_serial_puthex64((uint64_t)surface_id);
                __boot_serial_puts(" size=");
                __boot_serial_putu32((uint32_t)cw);
                __boot_serial_puts("x");
                __boot_serial_putu32((uint32_t)ch);
                __boot_serial_puts(" active=");
                __boot_serial_putu32(active?1u:0u);
                __boot_serial_puts(" serial=");
                __boot_serial_putu32(serial);
                __boot_serial_puts("\n");
            }
        }
    }
    wl7_send_xdg_surface_configure(cl->sock_fd,xs->id,serial);
    if(g_wl7_send_trace_count<WL7_TRACE_SEND_LIMIT){
        ++g_wl7_send_trace_count;
        __boot_serial_puts("[wl7-send] xdg_surface.configure obj=");
        __boot_serial_puthex64((uint64_t)xs->id);
        __boot_serial_puts(" surf=");
        __boot_serial_puthex64((uint64_t)surface_id);
        __boot_serial_puts(" serial=");
        __boot_serial_putu32(serial);
        __boot_serial_puts("\n");
    }
}
static uint32_t wl7_pick_mapped_surface(int client_idx){
    int i;
    for(i=0;i<WL7_MAX_SURFACES;++i){
        if(!g_wl7_surfaces[i].used)continue;
        if(g_wl7_surfaces[i].client_idx!=client_idx)continue;
        if(!g_wl7_surfaces[i].mapped)continue;
        return g_wl7_surfaces[i].id;
    }
    return 0;
}
static void wl7_release_surface_buffers(uint32_t surface_id){
    int i;
    for(i=0;i<WL7_MAX_SURFACES;++i){
        if(!g_wl7_surfaces[i].used)continue;
        if(g_wl7_surfaces[i].id!=surface_id)continue;
        if(g_wl7_surfaces[i].buffer_id){
            int bidx=wl7_find_buffer_index(g_wl7_surfaces[i].buffer_id);
            if(bidx>=0)g_wl7_surface_release_pending[i]=true;
        }
        return;
    }
}
static void wl7_drop_surface_related(int client_idx,uint32_t surface_id,bool send_close){
    int i;
    wl7_xdg_surface_t *xs=wl7_find_xdg_surface_by_surface(surface_id);
    wl7_xdg_toplevel_t *tl=wl7_find_xdg_toplevel_by_surface(surface_id);
    if(xs){
        xs->used=false;
        xs->id=0;
        xs->client_idx=0;
        xs->surface_id=0;
        xs->toplevel_id=0;
        xs->configured=false;
        xs->last_configure_serial=0;
        xs->last_ack_serial=0;
        xs->initial_commit_seen=false;
        xs->configure_pending=false;
    }
    if(tl){
        if(send_close&&g_wl7_clients[client_idx].used){
            wl7_send_xdg_toplevel_close(g_wl7_clients[client_idx].sock_fd,tl->id);
        }
        tl->used=false;
        tl->id=0;
        tl->client_idx=0;
        tl->surface_id=0;
        tl->xdg_surface_id=0;
        tl->configured=false;
        tl->activated=false;
        tl->width=0;
        tl->height=0;
        tl->last_configure_serial=0;
        tl->title[0]=0;
        tl->app_id[0]=0;
    }
    for(i=0;i<WL7_MAX_CALLBACKS;++i){
        if(!g_wl7_callbacks[i].used)continue;
        if(g_wl7_callbacks[i].client_idx!=client_idx)continue;
        if(g_wl7_callbacks[i].surface_id!=surface_id)continue;
        wl7_send_display_delete_id(g_wl7_clients[client_idx].sock_fd,g_wl7_callbacks[i].id);
        wl7_meta_drop(client_idx,g_wl7_callbacks[i].id);
        g_wl7_callbacks[i].used=false;
        g_wl7_callbacks[i].id=0;
        g_wl7_callbacks[i].client_idx=0;
        g_wl7_callbacks[i].surface_id=0;
        g_wl7_callbacks[i].pending=false;
        g_wl7_callbacks[i].due_ms=0;
    }
    for(i=0;i<WL7_MAX_VIEWPORTS;++i){
        if(!g_wl7_viewports[i].used)continue;
        if(g_wl7_viewports[i].client_idx!=client_idx)continue;
        if(g_wl7_viewports[i].surface_id!=surface_id)continue;
        wl7_meta_drop(client_idx,g_wl7_viewports[i].id);
        ulibc_memset(&g_wl7_viewports[i],0,sizeof(g_wl7_viewports[i]));
    }
    for(i=0;i<WL7_MAX_PRESENT_FEEDBACK;++i){
        if(!g_wl7_present_fbs[i].used)continue;
        if(g_wl7_present_fbs[i].client_idx!=client_idx)continue;
        if(g_wl7_present_fbs[i].surface_id!=surface_id)continue;
        wl7_send_display_delete_id(g_wl7_clients[client_idx].sock_fd,g_wl7_present_fbs[i].id);
        wl7_meta_drop(client_idx,g_wl7_present_fbs[i].id);
        ulibc_memset(&g_wl7_present_fbs[i],0,sizeof(g_wl7_present_fbs[i]));
    }
}
static void wl7_send_registry_globals(int sock_fd,uint32_t registry_obj){
    int i;
    for(i=0;i<g_wl7_global_count;++i){
        uint8_t pld[256];
        size_t pos=0;
        uint32_t gid=g_wl7_globals[i].id;
        uint32_t ver=g_wl7_globals[i].version;
        size_t ilen=ulibc_strlen(g_wl7_globals[i].interface)+1;
        wl7_wr_u32(pld+pos,gid);pos+=4;
        wl7_wr_u32(pld+pos,(uint32_t)ilen);pos+=4;
        ulibc_memcpy(pld+pos,g_wl7_globals[i].interface,ilen);pos+=ilen;
        pos=wl7_align4(pos);
        wl7_wr_u32(pld+pos,ver);pos+=4;
        (void)wl7_send_event(sock_fd,registry_obj,0,pld,pos); /* wl_registry.global */
    }
}
static void wl7_send_shm_formats(int sock_fd,uint32_t shm_obj){
    uint8_t pld[4];
    wl7_wr_u32(pld,0); /* WL_SHM_FORMAT_ARGB8888 */
    (void)wl7_send_event(sock_fd,shm_obj,0,pld,4);
    wl7_wr_u32(pld,1); /* WL_SHM_FORMAT_XRGB8888 */
    (void)wl7_send_event(sock_fd,shm_obj,0,pld,4);
}
static void wl7_send_seat_caps(int sock_fd,uint32_t seat_obj){
    uint8_t pld[4];
    wl7_wr_u32(pld,3); /* pointer + keyboard; Firefox necesita ambas para sentirse vivo */
    (void)wl7_send_event(sock_fd,seat_obj,0,pld,4);
    {
        uint8_t np[32];
        size_t nlen=6; /* ridux\0 */
        wl7_wr_u32(np,(uint32_t)nlen);
        ulibc_memcpy(np+4,"ridux",6);
        (void)wl7_send_event(sock_fd,seat_obj,1,np,wl7_align4(4+nlen));
    }
}
static void wl7_send_output_info(int sock_fd,uint32_t out_obj){
    uint8_t pld[96];
    size_t pos=0;
    ulibc_memset(pld,0,sizeof(pld));
    wl7_wr_u32(pld+pos,0);pos+=4; /* x */
    wl7_wr_u32(pld+pos,0);pos+=4; /* y */
    wl7_wr_u32(pld+pos,0);pos+=4; /* phys w */
    wl7_wr_u32(pld+pos,0);pos+=4; /* phys h */
    wl7_wr_u32(pld+pos,0);pos+=4; /* subpixel */
    wl7_wr_u32(pld+pos,6);pos+=4;ulibc_memcpy(pld+pos,"Ridux",6);pos+=6;pos=wl7_align4(pos);
    wl7_wr_u32(pld+pos,6);pos+=4;ulibc_memcpy(pld+pos,"Flush",6);pos+=6;pos=wl7_align4(pos);
    wl7_wr_u32(pld+pos,0);pos+=4; /* transform */
    (void)wl7_send_event(sock_fd,out_obj,0,pld,pos); /* geometry */
    pos=0;
    wl7_wr_u32(pld+pos,3);pos+=4; /* current|preferred */
    wl7_wr_u32(pld+pos,1280);pos+=4;
    wl7_wr_u32(pld+pos,720);pos+=4;
    wl7_wr_u32(pld+pos,60000);pos+=4;
    (void)wl7_send_event(sock_fd,out_obj,1,pld,pos); /* mode */
    wl7_wr_u32(pld,1);
    (void)wl7_send_event(sock_fd,out_obj,3,pld,4); /* scale */
    (void)wl7_send_event(sock_fd,out_obj,2,0,0);   /* done */
}
static int wl7_refresh_buffer_data(int bidx){
    wl7_buffer_t *b;
    wl7_pool_t *pool;
    int src_fd=-1;
    size_t need;
    int64_t rd;
    if(bidx<0||bidx>=WL7_MAX_BUFFERS)return -EINVAL;
    b=&g_wl7_buffers[bidx];
    if(!b->used)return -ENOENT;
    pool=wl7_find_pool((uint32_t)g_wl7_buffer_pool[bidx]);
    if(pool&&pool->fd>=0)src_fd=pool->fd;
    else if(b->fd>=0)src_fd=b->fd;
    if(src_fd<0)return -EBADF;
    need=(size_t)b->stride*(size_t)b->height;
    if(need==0||need>16u*1024u*1024u)return -E2BIG;
    if(!b->data||b->data_size<need){
        if(b->data)ulibc_free(b->data);
        b->data=(uint8_t*)ulibc_malloc(need);
        if(!b->data){b->data_size=0;return -ENOMEM;}
        b->data_size=(uint32_t)need;
    }
    (void)real_sys_lseek(src_fd,(int64_t)g_wl7_buffer_offset[bidx],SEEK_SET);
    rd=real_sys_read(src_fd,b->data,need);
    if(rd<0)return (int)rd;
    if((size_t)rd<need)ulibc_memset(b->data+rd,0,need-(size_t)rd);
    g_wl7_buffer_dirty[bidx]=false;
    return 0;
}

static void wl7_register_globals(void){
    if(g_wl7_global_count>0)return;
    g_wl7_globals[0].id=WL7_COMPOSITOR_ID;g_wl7_globals[0].interface=WL7_IFACE_COMPOSITOR;g_wl7_globals[0].version=4;
    g_wl7_globals[1].id=WL7_SHM_ID;g_wl7_globals[1].interface=WL7_IFACE_SHM;g_wl7_globals[1].version=1;
    g_wl7_globals[2].id=WL7_SHELL_ID;g_wl7_globals[2].interface=WL7_IFACE_SHELL;g_wl7_globals[2].version=1;
    g_wl7_globals[3].id=WL7_SEAT_ID;g_wl7_globals[3].interface=WL7_IFACE_SEAT;g_wl7_globals[3].version=6;
    g_wl7_globals[4].id=WL7_OUTPUT_ID;g_wl7_globals[4].interface=WL7_IFACE_OUTPUT;g_wl7_globals[4].version=2;
    g_wl7_globals[5].id=WL7_XDG_WM_BASE_ID;g_wl7_globals[5].interface=WL7_IFACE_XDG_WM_BASE;g_wl7_globals[5].version=2;
    g_wl7_globals[6].id=WL7_VIEWPORTER_ID;g_wl7_globals[6].interface=WL7_IFACE_VIEWPORTER;g_wl7_globals[6].version=1;
    /* Do not advertise linux-dmabuf yet: wl_shm path is stable and avoids
     * black/invisible frames when clients pick GPU buffers we can't import. */
    g_wl7_globals[7].id=WL7_PRESENTATION_ID;g_wl7_globals[7].interface=WL7_IFACE_PRESENTATION;g_wl7_globals[7].version=1;
    g_wl7_global_count=8;
}

static int wl7_client_idx_by_sock(int sock_fd){
    int i;
    for(i=0;i<WL7_MAX_CLIENTS;++i){
        if(g_wl7_clients[i].used&&g_wl7_clients[i].sock_fd==sock_fd)return i;
    }
    return -1;
}

static int wl7_attach_client_socket(int cfd){
    wl7_client_t*cl=0;int i;
    for(i=0;i<WL7_MAX_CLIENTS;++i)if(!g_wl7_clients[i].used){cl=&g_wl7_clients[i];break;}
    if(!cl)return-1;
    ulibc_memset(cl,0,sizeof(*cl));cl->used=true;cl->sock_fd=cfd;cl->next_id=0xFF000000;
    ulibc_memset(g_wl7_meta[(int)(cl-g_wl7_clients)],0,sizeof(g_wl7_meta[(int)(cl-g_wl7_clients)]));
    g_wl7_client_registry_obj[(int)(cl-g_wl7_clients)]=0;
    g_wl7_client_seat_obj[(int)(cl-g_wl7_clients)]=0;
    g_wl7_client_pointer_obj[(int)(cl-g_wl7_clients)]=0;
    g_wl7_client_keyboard_obj[(int)(cl-g_wl7_clients)]=0;
    g_wl7_client_touch_obj[(int)(cl-g_wl7_clients)]=0;
    g_wl7_client_output_obj[(int)(cl-g_wl7_clients)]=0;
    g_wl7_client_xdg_wm_base_obj[(int)(cl-g_wl7_clients)]=0;
    g_wl7_client_viewporter_obj[(int)(cl-g_wl7_clients)]=0;
    g_wl7_client_dmabuf_obj[(int)(cl-g_wl7_clients)]=0;
    g_wl7_client_presentation_obj[(int)(cl-g_wl7_clients)]=0;
    g_wl7_client_pointer_focus[(int)(cl-g_wl7_clients)]=0;
    g_wl7_client_keyboard_focus[(int)(cl-g_wl7_clients)]=0;
    g_wl7_client_touch_focus[(int)(cl-g_wl7_clients)]=0;
    g_wl7_client_processing[(int)(cl-g_wl7_clients)]=false;
    g_wl7_client_touch_active[(int)(cl-g_wl7_clients)]=false;
    g_wl7_client_touch_x[(int)(cl-g_wl7_clients)]=0;
    g_wl7_client_touch_y[(int)(cl-g_wl7_clients)]=0;
    wl7_meta_set((int)(cl-g_wl7_clients),WL7_DISPLAY_ID,WL7_OBJ_DISPLAY,0);
    wl7_register_globals();
    if(g_wl7_trace_count<WL7_TRACE_ATTACH_LIMIT){
        ++g_wl7_trace_count;
        __boot_serial_puts("[wl7] attach fd=");
        __boot_serial_putu32((uint32_t)cfd);
        __boot_serial_puts(" client=");
        __boot_serial_putu32((uint32_t)(cl-g_wl7_clients));
        __boot_serial_puts("\n");
    }
    return(int)(cl-g_wl7_clients);
}

int wl7_accept_client(int listen_sock_fd){
    uint32_t src_ip;uint16_t src_port;
    int cfd=sock_accept(listen_sock_fd,&src_ip,&src_port);
    if(cfd<0)return-1;
    if(wl7_attach_client_socket(cfd)<0){
        sock_close(cfd);
        return -1;
    }
    return wl7_client_idx_by_sock(cfd);
}

int wl7_attach_socket(int sock_fd){
    if(sock_fd<0)return -1;
    if(wl7_client_idx_by_sock(sock_fd)>=0)return 0;
    return wl7_attach_client_socket(sock_fd)>=0?0:-1;
}

int wl7_detach_socket(int sock_fd){
    int idx=wl7_client_idx_by_sock(sock_fd);
    int i;
    if(idx<0)return -1;
    for(i=0;i<WL7_MAX_SURFACES;++i){
        wl7_surface_t *s=&g_wl7_surfaces[i];
        if(!s->used||s->client_idx!=idx)continue;
        wl7_send_focus_leave_events(idx,s->id);
        wl7_release_surface_buffers(s->id);
        wl7_drop_surface_related(idx,s->id,true);
        wl7_clear_surface_damage(i);
        s->used=false;
        s->id=0;
        s->client_idx=0;
        s->buffer_id=0;
        s->attached_buffer=0;
        s->mapped=false;
        s->x=0;
        s->y=0;
        s->frame_callback_id=0;
    }
    for(i=0;i<WL7_MAX_CALLBACKS;++i){
        if(!g_wl7_callbacks[i].used)continue;
        if(g_wl7_callbacks[i].client_idx!=idx)continue;
        g_wl7_callbacks[i].used=false;
        g_wl7_callbacks[i].id=0;
        g_wl7_callbacks[i].client_idx=0;
        g_wl7_callbacks[i].surface_id=0;
        g_wl7_callbacks[i].pending=false;
        g_wl7_callbacks[i].due_ms=0;
    }
    for(i=0;i<WL7_MAX_VIEWPORTS;++i){
        if(!g_wl7_viewports[i].used)continue;
        if(g_wl7_viewports[i].client_idx!=idx)continue;
        wl7_meta_drop(idx,g_wl7_viewports[i].id);
        ulibc_memset(&g_wl7_viewports[i],0,sizeof(g_wl7_viewports[i]));
    }
    for(i=0;i<WL7_MAX_DMABUF_PARAMS;++i){
        if(!g_wl7_dmabuf_params[i].used)continue;
        if(g_wl7_dmabuf_params[i].client_idx!=idx)continue;
        if(g_wl7_dmabuf_params[i].fd>=0){
            (void)real_sys_close(g_wl7_dmabuf_params[i].fd);
        }
        wl7_meta_drop(idx,g_wl7_dmabuf_params[i].id);
        ulibc_memset(&g_wl7_dmabuf_params[i],0,sizeof(g_wl7_dmabuf_params[i]));
        g_wl7_dmabuf_params[i].fd=-1;
    }
    for(i=0;i<WL7_MAX_PRESENT_FEEDBACK;++i){
        if(!g_wl7_present_fbs[i].used)continue;
        if(g_wl7_present_fbs[i].client_idx!=idx)continue;
        wl7_meta_drop(idx,g_wl7_present_fbs[i].id);
        ulibc_memset(&g_wl7_present_fbs[i],0,sizeof(g_wl7_present_fbs[i]));
    }
    for(i=0;i<WL7_MAX_POOLS;++i){
        if(!g_wl7_pools[i].used)continue;
        if(g_wl7_pools[i].client_idx!=idx)continue;
        if(g_wl7_pools[i].fd>=0){
            (void)real_sys_close(g_wl7_pools[i].fd);
        }
        g_wl7_pools[i].used=false;
        g_wl7_pools[i].id=0;
        g_wl7_pools[i].client_idx=0;
        g_wl7_pools[i].fd=-1;
        g_wl7_pools[i].size=0;
    }
    for(i=0;i<WL7_MAX_BUFFERS;++i){
        if(!g_wl7_buffers[i].used)continue;
        if(!wl7_meta_find(idx,g_wl7_buffers[i].id))continue;
        wl7_buffer_reset_slot(i,idx,true);
    }
    ulibc_memset(g_wl7_meta[idx],0,sizeof(g_wl7_meta[idx]));
    g_wl7_client_registry_obj[idx]=0;
    g_wl7_client_seat_obj[idx]=0;
    g_wl7_client_pointer_obj[idx]=0;
    g_wl7_client_keyboard_obj[idx]=0;
    g_wl7_client_touch_obj[idx]=0;
    g_wl7_client_output_obj[idx]=0;
    g_wl7_client_xdg_wm_base_obj[idx]=0;
    g_wl7_client_viewporter_obj[idx]=0;
    g_wl7_client_dmabuf_obj[idx]=0;
    g_wl7_client_presentation_obj[idx]=0;
    g_wl7_client_pointer_focus[idx]=0;
    g_wl7_client_keyboard_focus[idx]=0;
    g_wl7_client_touch_focus[idx]=0;
    g_wl7_client_processing[idx]=false;
    g_wl7_client_touch_active[idx]=false;
    g_wl7_client_touch_x[idx]=0;
    g_wl7_client_touch_y[idx]=0;
    if(g_wl7_active_client==idx){
        g_wl7_active_client=-1;
        g_wl7_active_surface=0;
    }
    ulibc_memset(&g_wl7_clients[idx],0,sizeof(g_wl7_clients[idx]));
    return 0;
}

int wl7_process_message(int client_idx){
    if(client_idx<0||client_idx>=WL7_MAX_CLIENTS)return-1;
    wl7_client_t*cl=&g_wl7_clients[client_idx];
    int ret=0;
    if(!cl->used)return-1;
    if(g_wl7_client_processing[client_idx])return -EAGAIN;
    g_wl7_client_processing[client_idx]=true;
    {
        uint8_t hdr[8];
        int rc=sock_recv(cl->sock_fd,hdr,8,0);
        uint32_t obj_id;
        uint32_t op_sz;
        uint16_t opcode;
        uint16_t msg_size;
        size_t body_len;
        uint8_t body[4096];
        wl7_objmeta_t *meta;
        if(rc<8){ret=-1;goto out;}
        obj_id=wl7_rd_u32(hdr);
        op_sz=wl7_rd_u32(hdr+4);
        opcode=(uint16_t)(op_sz&0xFFFFu);
        msg_size=(uint16_t)(op_sz>>16);
        body_len=(msg_size>8)?(size_t)(msg_size-8):0;
        if(msg_size<8||body_len>sizeof(body)){ret=-EINVAL;goto out;}
        if(body_len>0){
            rc=sock_recv(cl->sock_fd,body,body_len,0);
            if(rc<(int)body_len){ret=-1;goto out;}
        }
        meta=wl7_meta_find(client_idx,obj_id);
        if(!meta){
            if(obj_id==WL7_DISPLAY_ID)meta=wl7_meta_set(client_idx,obj_id,WL7_OBJ_DISPLAY,0);
            else if(obj_id==WL7_REGISTRY_ID)meta=wl7_meta_set(client_idx,obj_id,WL7_OBJ_REGISTRY,0);
        }
        if(!meta)goto out;
        if(g_wl7_req_trace_count<WL7_TRACE_REQ_LIMIT){
            ++g_wl7_req_trace_count;
            __boot_serial_puts("[wl7-req] c=");
            __boot_serial_putu32((uint32_t)client_idx);
            __boot_serial_puts(" obj=");
            __boot_serial_puthex64((uint64_t)obj_id);
            __boot_serial_puts(" kind=");
            __boot_serial_putu32((uint32_t)meta->kind);
            __boot_serial_puts(" op=");
            __boot_serial_putu32((uint32_t)opcode);
            __boot_serial_puts(" size=");
            __boot_serial_putu32((uint32_t)msg_size);
            __boot_serial_puts("\n");
        }
        switch(meta->kind){
            case WL7_OBJ_DISPLAY:
                if(opcode==0&&body_len>=4){ /* sync(new_id callback) */
                    uint32_t cb_id=wl7_rd_u32(body+0);
                    wl7_callback_t *cb=wl7_alloc_callback();
                    if(cb){
                        cb->id=cb_id;
                        cb->client_idx=client_idx;
                        cb->surface_id=0;
                        cb->pending=false;
                        cb->due_ms=0;
                        wl7_meta_set(client_idx,cb_id,WL7_OBJ_CALLBACK,0);
                        wl7_send_callback_done(cl->sock_fd,cb_id,g_wl7_frame_clock_ms);
                        cb->used=false;
                        wl7_meta_drop(client_idx,cb_id);
                    }
                }else if(opcode==1&&body_len>=4){ /* get_registry */
                    uint32_t reg_id=wl7_rd_u32(body+0);
                    g_wl7_client_registry_obj[client_idx]=reg_id;
                    wl7_meta_set(client_idx,reg_id,WL7_OBJ_REGISTRY,0);
                    wl7_send_registry_globals(cl->sock_fd,reg_id);
                }
                break;
            case WL7_OBJ_REGISTRY:
                if(opcode==0&&body_len>=16){ /* bind */
                    uint32_t name=wl7_rd_u32(body+0);
                    size_t off=4;
                    char iface[64];
                    uint32_t version,new_id;
                    if(!wl7_read_wl_string(body,body_len,&off,iface,sizeof(iface)))break;
                    off=wl7_align4(off);
                    if(off+8>body_len)break;
                    version=wl7_rd_u32(body+off);off+=4;
                    new_id=wl7_rd_u32(body+off);
                    (void)version;
                    if(g_wl7_req_trace_count<WL7_TRACE_REQ_LIMIT){
                        ++g_wl7_req_trace_count;
                        __boot_serial_puts("[wl7-bind] name=");
                        __boot_serial_putu32(name);
                        __boot_serial_puts(" iface=");
                        __boot_serial_puts(iface);
                        __boot_serial_puts(" new=");
                        __boot_serial_puthex64((uint64_t)new_id);
                        __boot_serial_puts("\n");
                    }
                    if(name==WL7_COMPOSITOR_ID){
                        wl7_meta_set(client_idx,new_id,WL7_OBJ_COMPOSITOR,0);
                    }else if(name==WL7_SHM_ID){
                        wl7_meta_set(client_idx,new_id,WL7_OBJ_SHM,0);
                        wl7_send_shm_formats(cl->sock_fd,new_id);
                    }else if(name==WL7_SHELL_ID){
                        wl7_meta_set(client_idx,new_id,WL7_OBJ_SHELL,0);
                    }else if(name==WL7_SEAT_ID){
                        g_wl7_client_seat_obj[client_idx]=new_id;
                        wl7_meta_set(client_idx,new_id,WL7_OBJ_SEAT,0);
                        wl7_send_seat_caps(cl->sock_fd,new_id);
                    }else if(name==WL7_OUTPUT_ID){
                        g_wl7_client_output_obj[client_idx]=new_id;
                        wl7_meta_set(client_idx,new_id,WL7_OBJ_OUTPUT,0);
                        wl7_send_output_info(cl->sock_fd,new_id);
                    }else if(name==WL7_XDG_WM_BASE_ID){
                        g_wl7_client_xdg_wm_base_obj[client_idx]=new_id;
                        wl7_meta_set(client_idx,new_id,WL7_OBJ_XDG_WM_BASE,0);
                        wl7_send_xdg_wm_base_ping(cl->sock_fd,new_id,wl7_next_serial());
                    }else if(name==WL7_VIEWPORTER_ID){
                        g_wl7_client_viewporter_obj[client_idx]=new_id;
                        wl7_meta_set(client_idx,new_id,WL7_OBJ_VIEWPORTER,0);
                    }else if(name==WL7_DMABUF_ID){
                        g_wl7_client_dmabuf_obj[client_idx]=new_id;
                        wl7_meta_set(client_idx,new_id,WL7_OBJ_LINUX_DMABUF,0);
                        wl7_send_dmabuf_formats(cl->sock_fd,new_id);
                    }else if(name==WL7_PRESENTATION_ID){
                        g_wl7_client_presentation_obj[client_idx]=new_id;
                        wl7_meta_set(client_idx,new_id,WL7_OBJ_PRESENTATION,0);
                        wl7_send_presentation_clock_id(cl->sock_fd,new_id);
                    }
                }
                break;
            case WL7_OBJ_COMPOSITOR:
                if(opcode==0&&body_len>=4){ /* create_surface */
                    uint32_t surf_id=wl7_rd_u32(body+0);
                    wl7_surface_t*s=0;
                    int i;
                    for(i=0;i<WL7_MAX_SURFACES;++i){
                        if(!g_wl7_surfaces[i].used){s=&g_wl7_surfaces[i];break;}
                    }
                    if(!s)break;
                    ulibc_memset(s,0,sizeof(*s));
                    s->used=true;
                    s->id=surf_id;
                    s->client_idx=client_idx;
                    s->mapped=false;
                    wl7_meta_set(client_idx,surf_id,WL7_OBJ_SURFACE,0);
                    if(cl->surface_count<WL7_MAX_SURFACES)cl->surfaces[cl->surface_count++]=surf_id;
                    wl7_mark_surface_damage(surf_id,0,0,0,0,true);
                    if(g_wl7_trace_count<WL7_TRACE_ATTACH_LIMIT){
                        ++g_wl7_trace_count;
                        __boot_serial_puts("[wl7] surface-create client=");
                        __boot_serial_putu32((uint32_t)client_idx);
                        __boot_serial_puts(" id=");
                        __boot_serial_puthex64((uint64_t)surf_id);
                        __boot_serial_puts("\n");
                    }
                }else if(opcode==1&&body_len>=4){ /* create_region */
                    uint32_t region_id=wl7_rd_u32(body+0);
                    wl7_meta_set(client_idx,region_id,WL7_OBJ_REGION,0);
                }
                break;
            case WL7_OBJ_SHM:
                if(opcode==0&&body_len>=8){ /* create_pool(new_id, size) + fd via SCM_RIGHTS */
                    uint32_t pool_id=wl7_rd_u32(body+0);
                    uint32_t pool_sz=wl7_rd_u32(body+4);
                    int memfd=-1;
                    wl7_pool_t *pool=wl7_alloc_pool();
                    (void)sock_recv_right(cl->sock_fd,&memfd);
                    if(!pool)break;
                    pool->id=pool_id;
                    pool->client_idx=client_idx;
                    pool->fd=memfd;
                    pool->size=pool_sz;
                    wl7_meta_set(client_idx,pool_id,WL7_OBJ_POOL,0);
                }
                break;
            case WL7_OBJ_POOL:
                if(opcode==0&&body_len>=24){ /* create_buffer */
                    wl7_pool_t *pool=wl7_find_pool(obj_id);
                    uint32_t new_id=wl7_rd_u32(body+0);
                    uint32_t offset=wl7_rd_u32(body+4);
                    uint32_t w=wl7_rd_u32(body+8);
                    uint32_t h=wl7_rd_u32(body+12);
                    uint32_t stride=wl7_rd_u32(body+16);
                    uint32_t fmt=wl7_rd_u32(body+20);
                    int i;
                    if(!pool||pool->fd<0||w==0||h==0||stride==0)break;
                    for(i=0;i<WL7_MAX_BUFFERS;++i){
                        if(g_wl7_buffers[i].used)continue;
                        ulibc_memset(&g_wl7_buffers[i],0,sizeof(g_wl7_buffers[i]));
                        g_wl7_buffers[i].used=true;
                        g_wl7_buffers[i].id=new_id;
                        g_wl7_buffers[i].fd=pool->fd;
                        g_wl7_buffers[i].width=w;
                        g_wl7_buffers[i].height=h;
                        g_wl7_buffers[i].stride=stride;
                        g_wl7_buffers[i].format=fmt;
                        g_wl7_buffers[i].data=0;
                        g_wl7_buffers[i].data_size=0;
                        g_wl7_buffer_pool[i]=(int)pool->id;
                        g_wl7_buffer_offset[i]=offset;
                        g_wl7_buffer_dirty[i]=true;
                        g_wl7_buffer_owns_fd[i]=false;
                        wl7_meta_set(client_idx,new_id,WL7_OBJ_BUFFER,i);
                        break;
                    }
                }else if(opcode==1){ /* destroy */
                    wl7_pool_t *pool=wl7_find_pool(obj_id);
                    if(pool){
                        int i,j;
                        for(i=0;i<WL7_MAX_BUFFERS;++i){
                            if(!g_wl7_buffers[i].used)continue;
                            if(g_wl7_buffer_pool[i]!=(int)pool->id)continue;
                            for(j=0;j<WL7_MAX_SURFACES;++j){
                                if(!g_wl7_surfaces[j].used)continue;
                                if(g_wl7_surfaces[j].buffer_id==g_wl7_buffers[i].id)g_wl7_surfaces[j].buffer_id=0;
                                if(g_wl7_surfaces[j].attached_buffer==g_wl7_buffers[i].id)g_wl7_surfaces[j].attached_buffer=0;
                            }
                            wl7_buffer_reset_slot(i,client_idx,true);
                        }
                        if(pool->fd>=0)(void)real_sys_close(pool->fd);
                        pool->used=false;
                        pool->id=0;
                        pool->client_idx=0;
                        pool->fd=-1;
                        pool->size=0;
                    }
                    wl7_meta_drop(client_idx,obj_id);
                }else if(opcode==2&&body_len>=4){ /* resize */
                    wl7_pool_t *pool=wl7_find_pool(obj_id);
                    if(pool)pool->size=wl7_rd_u32(body+0);
                }
                break;
            case WL7_OBJ_SURFACE:{
                wl7_surface_t *s=wl7_find_surface(obj_id);
                int sidx=wl7_surface_index(obj_id);
                if(!s)break;
                if(opcode==0){ /* destroy */
                    int j;
                    wl7_send_focus_leave_events(client_idx,s->id);
                    wl7_release_surface_buffers(s->id);
                    wl7_drop_surface_related(client_idx,s->id,false);
                    if(sidx>=0)wl7_clear_surface_damage(sidx);
                    for(j=0;j<cl->surface_count;++j){
                        if(cl->surfaces[j]!=s->id)continue;
                        for(;j+1<cl->surface_count;++j)cl->surfaces[j]=cl->surfaces[j+1];
                        cl->surfaces[cl->surface_count-1]=0;
                        if(cl->surface_count>0)--cl->surface_count;
                        break;
                    }
                    s->used=false;
                    s->id=0;
                    s->client_idx=0;
                    s->buffer_id=0;
                    s->attached_buffer=0;
                    s->frame_callback_id=0;
                    s->mapped=false;
                    s->x=0;
                    s->y=0;
                    wl7_meta_drop(client_idx,obj_id);
                }else if(opcode==1&&body_len>=12){ /* attach */
                    s->attached_buffer=wl7_rd_u32(body+0);
                    s->x=wl7_rd_s32(body+4);
                    s->y=wl7_rd_s32(body+8);
                    wl7_mark_surface_damage(s->id,0,0,0,0,true);
                    if(g_wl7_trace_count<WL7_TRACE_ATTACH_LIMIT){
                        ++g_wl7_trace_count;
                        __boot_serial_puts("[wl7] attach-surf surf=");
                        __boot_serial_puthex64((uint64_t)s->id);
                        __boot_serial_puts(" buf=");
                        __boot_serial_puthex64((uint64_t)s->attached_buffer);
                        __boot_serial_puts(" xy=");
                        __boot_serial_putu32((uint32_t)s->x);
                        __boot_serial_puts(",");
                        __boot_serial_putu32((uint32_t)s->y);
                        __boot_serial_puts("\n");
                    }
                }else if(opcode==2&&body_len>=16){ /* damage */
                    int32_t dx=wl7_rd_s32(body+0);
                    int32_t dy=wl7_rd_s32(body+4);
                    int32_t dw=wl7_rd_s32(body+8);
                    int32_t dh=wl7_rd_s32(body+12);
                    wl7_mark_surface_damage(s->id,dx,dy,dw,dh,false);
                }else if(opcode==3&&body_len>=4){ /* frame */
                    uint32_t cb_id=wl7_rd_u32(body+0);
                    wl7_callback_t *old_cb=wl7_find_callback(s->frame_callback_id);
                    wl7_callback_t *cb=wl7_alloc_callback();
                    if(old_cb){
                        old_cb->used=false;
                        old_cb->pending=false;
                        wl7_meta_drop(client_idx,s->frame_callback_id);
                        wl7_send_display_delete_id(cl->sock_fd,s->frame_callback_id);
                    }
                    if(cb){
                        cb->id=cb_id;
                        cb->client_idx=client_idx;
                        cb->surface_id=s->id;
                        cb->pending=false;
                        cb->due_ms=0;
                        s->frame_callback_id=cb_id;
                        wl7_meta_set(client_idx,cb_id,WL7_OBJ_CALLBACK,0);
                    }
                }else if(opcode==4||opcode==5){ /* set_opaque_region / set_input_region */
                    /* Region clipping is currently advisory for our software compositor. */
                }else if(opcode==6){ /* commit */
                    bool was_mapped=s->mapped;
                    wl7_buffer_t *b=wl7_find_buffer(s->attached_buffer);
                    bool has_buffer=false;
                    wl7_xdg_surface_t *xs=wl7_find_xdg_surface_by_surface(s->id);
                    if(s->attached_buffer==0){
                        s->buffer_id=0;
                        s->mapped=false;
                    }
                    if(b){
                        int bidx=wl7_find_buffer_index(s->attached_buffer);
                        s->buffer_id=s->attached_buffer;
                        has_buffer=true;
                        if(bidx>=0){
                            g_wl7_buffer_dirty[bidx]=true;
                            if(sidx>=0)g_wl7_surface_release_pending[sidx]=true;
                        }
                    }
                    if(g_wl7_trace_count<WL7_TRACE_ATTACH_LIMIT){
                        ++g_wl7_trace_count;
                        __boot_serial_puts("[wl7] commit surf=");
                        __boot_serial_puthex64((uint64_t)s->id);
                        __boot_serial_puts(" buf=");
                        __boot_serial_puthex64((uint64_t)s->attached_buffer);
                        if(b){
                            __boot_serial_puts(" size=");
                            __boot_serial_putu32(b->width);
                            __boot_serial_puts("x");
                            __boot_serial_putu32(b->height);
                        }
                        __boot_serial_puts("\n");
                    }
                    if(xs){
                        if(!xs->initial_commit_seen){
                            uint32_t out_obj=g_wl7_client_output_obj[client_idx];
                            xs->initial_commit_seen=true;
                            xs->configure_pending=true;
                            wl7_send_surface_enter(cl->sock_fd,s->id,out_obj?out_obj:WL7_OUTPUT_ID);
                        }
                        if(xs->configure_pending||!xs->configured){
                            wl7_schedule_xdg_configure(client_idx,s->id);
                        }
                    }
                    if(!has_buffer){
                        if(sidx>=0)wl7_clear_surface_damage(sidx);
                        break;
                    }
                    s->mapped=true;
                    if(sidx>=0&&!g_wl7_surface_has_damage[sidx]){
                        wl7_mark_surface_damage(s->id,0,0,0,0,true);
                    }
                    if(!was_mapped){
                        wl7_send_initial_focus_events(client_idx,s->id);
                    }
                    if(s->frame_callback_id){
                        wl7_callback_t *cb=wl7_find_callback(s->frame_callback_id);
                        if(cb){
                            cb->pending=true;
                            cb->due_ms=g_wl7_frame_clock_ms+16;
                        }
                    }
                }else if(opcode==9&&body_len>=16){ /* damage_buffer */
                    int32_t dx=wl7_rd_s32(body+0);
                    int32_t dy=wl7_rd_s32(body+4);
                    int32_t dw=wl7_rd_s32(body+8);
                    int32_t dh=wl7_rd_s32(body+12);
                    wl7_mark_surface_damage(s->id,dx,dy,dw,dh,false);
                }
                break;}
            case WL7_OBJ_BUFFER:
                if(opcode==0){ /* destroy */
                    int bidx=wl7_find_buffer_index(obj_id);
                    int i;
                    if(bidx>=0){
                        for(i=0;i<WL7_MAX_SURFACES;++i){
                            if(!g_wl7_surfaces[i].used)continue;
                            if(g_wl7_surfaces[i].attached_buffer==obj_id)g_wl7_surfaces[i].attached_buffer=0;
                            if(g_wl7_surfaces[i].buffer_id==obj_id)g_wl7_surfaces[i].buffer_id=0;
                        }
                        wl7_buffer_reset_slot(bidx,client_idx,false);
                    }
                    wl7_meta_drop(client_idx,obj_id);
                }
                break;
            case WL7_OBJ_SHELL:
                if(opcode==0&&body_len>=8){ /* get_shell_surface */
                    uint32_t shell_surf_id=wl7_rd_u32(body+0);
                    uint32_t surf_id=wl7_rd_u32(body+4);
                    wl7_surface_t *s=wl7_find_surface(surf_id);
                    if(s){
                        s->mapped=true;
                        wl7_meta_set(client_idx,shell_surf_id,WL7_OBJ_SHELL_SURF,(int)surf_id);
                    }
                }
                break;
            case WL7_OBJ_SHELL_SURF:
                if(opcode==3||opcode==1||opcode==2||opcode==8){ /* set_toplevel/move/resize/set_title */
                    int surf_id=meta->ref;
                    wl7_surface_t *s=wl7_find_surface((uint32_t)surf_id);
                    if(s){
                        s->mapped=true;
                        wl7_mark_surface_damage(s->id,0,0,0,0,true);
                    }
                }
                break;
            case WL7_OBJ_SEAT:
                if(opcode==0&&body_len>=4){ /* get_pointer */
                    uint32_t ptr_id=wl7_rd_u32(body+0);
                    g_wl7_client_pointer_obj[client_idx]=ptr_id;
                    wl7_meta_set(client_idx,ptr_id,WL7_OBJ_POINTER,0);
                    if(g_wl7_client_pointer_focus[client_idx]){
                        wl7_send_pointer_enter(cl->sock_fd,ptr_id,g_wl7_client_pointer_focus[client_idx],0,0);
                    }
                }else if(opcode==1&&body_len>=4){ /* get_keyboard */
                    uint32_t kbd_id=wl7_rd_u32(body+0);
                    g_wl7_client_keyboard_obj[client_idx]=kbd_id;
                    wl7_meta_set(client_idx,kbd_id,WL7_OBJ_KEYBOARD,0);
                    wl7_send_keyboard_keymap(cl->sock_fd,kbd_id);
                    wl7_send_keyboard_repeat_info(cl->sock_fd,kbd_id);
                    if(g_wl7_client_keyboard_focus[client_idx]){
                        wl7_send_keyboard_enter(cl->sock_fd,kbd_id,g_wl7_client_keyboard_focus[client_idx]);
                    }
                }else if(opcode==2&&body_len>=4){ /* get_touch */
                    uint32_t touch_id=wl7_rd_u32(body+0);
                    g_wl7_client_touch_obj[client_idx]=touch_id;
                    wl7_meta_set(client_idx,touch_id,WL7_OBJ_TOUCH,0);
                    if(!g_wl7_client_touch_focus[client_idx]){
                        g_wl7_client_touch_focus[client_idx]=wl7_pick_mapped_surface(client_idx);
                    }
                }
                break;
            case WL7_OBJ_TOUCH:
                if(opcode==0){ /* release */
                    if(g_wl7_client_touch_obj[client_idx]==obj_id)g_wl7_client_touch_obj[client_idx]=0;
                    g_wl7_client_touch_focus[client_idx]=0;
                    g_wl7_client_touch_active[client_idx]=false;
                    wl7_meta_drop(client_idx,obj_id);
                }
                break;
            case WL7_OBJ_VIEWPORTER:
                if(opcode==0){ /* destroy */
                    if(g_wl7_client_viewporter_obj[client_idx]==obj_id)g_wl7_client_viewporter_obj[client_idx]=0;
                    wl7_meta_drop(client_idx,obj_id);
                }else if(opcode==1&&body_len>=8){ /* get_viewport */
                    uint32_t vp_id=wl7_rd_u32(body+0);
                    uint32_t surf_id=wl7_rd_u32(body+4);
                    wl7_surface_t *s=wl7_find_surface(surf_id);
                    wl7_viewport_t *vp=wl7_alloc_viewport();
                    if(s&&vp){
                        vp->id=vp_id;
                        vp->client_idx=client_idx;
                        vp->surface_id=surf_id;
                        vp->src_set=false;
                        vp->dst_set=false;
                        wl7_meta_set(client_idx,vp_id,WL7_OBJ_VIEWPORT,0);
                        wl7_mark_surface_damage(surf_id,0,0,0,0,true);
                    }
                }
                break;
            case WL7_OBJ_VIEWPORT:{
                wl7_viewport_t *vp=wl7_find_viewport(obj_id);
                if(!vp)break;
                if(opcode==0){ /* destroy */
                    wl7_mark_surface_damage(vp->surface_id,0,0,0,0,true);
                    ulibc_memset(vp,0,sizeof(*vp));
                    wl7_meta_drop(client_idx,obj_id);
                }else if(opcode==1&&body_len>=16){ /* set_source */
                    int32_t x=wl7_rd_s32(body+0);
                    int32_t y=wl7_rd_s32(body+4);
                    int32_t w=wl7_rd_s32(body+8);
                    int32_t h=wl7_rd_s32(body+12);
                    if(x==-1||y==-1||w==-1||h==-1){
                        vp->src_set=false;
                    }else{
                        vp->src_set=true;
                        vp->src_x=x;
                        vp->src_y=y;
                        vp->src_w=w;
                        vp->src_h=h;
                    }
                    wl7_mark_surface_damage(vp->surface_id,0,0,0,0,true);
                }else if(opcode==2&&body_len>=8){ /* set_destination */
                    int32_t w=wl7_rd_s32(body+0);
                    int32_t h=wl7_rd_s32(body+4);
                    if(w==-1||h==-1){
                        vp->dst_set=false;
                    }else{
                        vp->dst_set=(w>0&&h>0);
                        vp->dst_w=w;
                        vp->dst_h=h;
                    }
                    wl7_mark_surface_damage(vp->surface_id,0,0,0,0,true);
                }
                break;}
            case WL7_OBJ_LINUX_DMABUF:
                if(opcode==0){ /* destroy */
                    if(g_wl7_client_dmabuf_obj[client_idx]==obj_id)g_wl7_client_dmabuf_obj[client_idx]=0;
                    wl7_meta_drop(client_idx,obj_id);
                }else if(opcode==1&&body_len>=4){ /* create_params */
                    uint32_t params_id=wl7_rd_u32(body+0);
                    wl7_dmabuf_params_t *dp=wl7_alloc_dmabuf_params();
                    if(dp){
                        dp->id=params_id;
                        dp->client_idx=client_idx;
                        dp->fd=-1;
                        dp->plane0_set=false;
                        wl7_meta_set(client_idx,params_id,WL7_OBJ_DMABUF_PARAM,0);
                    }
                }else if((opcode==2&&body_len>=4)||(opcode==3&&body_len>=8)){ /* feedback objects */
                    uint32_t fb_id=wl7_rd_u32(body+0);
                    wl7_meta_set(client_idx,fb_id,WL7_OBJ_NONE,0);
                }
                break;
            case WL7_OBJ_DMABUF_PARAM:{
                wl7_dmabuf_params_t *dp=wl7_find_dmabuf_params(obj_id);
                if(opcode==0){ /* destroy */
                    if(dp){
                        if(dp->fd>=0)(void)real_sys_close(dp->fd);
                        ulibc_memset(dp,0,sizeof(*dp));
                        dp->fd=-1;
                    }
                    wl7_meta_drop(client_idx,obj_id);
                }else if(opcode==1&&body_len>=20){ /* add */
                    int dmafd=-1;
                    uint32_t plane_idx=wl7_rd_u32(body+0);
                    uint32_t offset=wl7_rd_u32(body+4);
                    uint32_t stride=wl7_rd_u32(body+8);
                    uint32_t mod_hi=wl7_rd_u32(body+12);
                    uint32_t mod_lo=wl7_rd_u32(body+16);
                    (void)mod_hi;(void)mod_lo;
                    (void)sock_recv_right(cl->sock_fd,&dmafd);
                    if(!dp||plane_idx!=0){
                        if(dmafd>=0)(void)real_sys_close(dmafd);
                        break;
                    }
                    if(dp->fd>=0)(void)real_sys_close(dp->fd);
                    dp->fd=dmafd;
                    dp->offset=offset;
                    dp->stride=stride;
                    dp->plane0_set=true;
                }else if(opcode==2){ /* create: force fallback path */
                    (void)wl7_send_event(cl->sock_fd,obj_id,1,0,0); /* failed */
                }else if(opcode==3&&body_len>=20){ /* create_immed */
                    uint32_t new_id=wl7_rd_u32(body+0);
                    int32_t width=wl7_rd_s32(body+4);
                    int32_t height=wl7_rd_s32(body+8);
                    uint32_t format=wl7_rd_u32(body+12);
                    uint32_t flags=wl7_rd_u32(body+16);
                    int i;
                    (void)flags;
                    if(!dp||!dp->plane0_set||dp->fd<0||width<=0||height<=0){
                        break;
                    }
                    for(i=0;i<WL7_MAX_BUFFERS;++i){
                        if(g_wl7_buffers[i].used)continue;
                        ulibc_memset(&g_wl7_buffers[i],0,sizeof(g_wl7_buffers[i]));
                        g_wl7_buffers[i].used=true;
                        g_wl7_buffers[i].id=new_id;
                        g_wl7_buffers[i].fd=dp->fd;
                        g_wl7_buffers[i].width=(uint32_t)width;
                        g_wl7_buffers[i].height=(uint32_t)height;
                        g_wl7_buffers[i].stride=dp->stride?dp->stride:((uint32_t)width*4u);
                        g_wl7_buffers[i].format=format;
                        g_wl7_buffers[i].data=0;
                        g_wl7_buffers[i].data_size=0;
                        g_wl7_buffer_pool[i]=0;
                        g_wl7_buffer_offset[i]=dp->offset;
                        g_wl7_buffer_dirty[i]=true;
                        g_wl7_buffer_owns_fd[i]=true;
                        wl7_meta_set(client_idx,new_id,WL7_OBJ_BUFFER,i);
                        dp->fd=-1;
                        dp->plane0_set=false;
                        break;
                    }
                }
                break;}
            case WL7_OBJ_PRESENTATION:
                if(opcode==0){ /* destroy */
                    if(g_wl7_client_presentation_obj[client_idx]==obj_id)g_wl7_client_presentation_obj[client_idx]=0;
                    wl7_meta_drop(client_idx,obj_id);
                }else if(opcode==1&&body_len>=8){ /* feedback */
                    uint32_t fb_id=wl7_rd_u32(body+0);
                    uint32_t surf_id=wl7_rd_u32(body+4);
                    wl7_presentation_fb_t *pf=wl7_alloc_present_fb();
                    if(pf){
                        pf->id=fb_id;
                        pf->client_idx=client_idx;
                        pf->surface_id=surf_id;
                        pf->pending=true;
                        pf->due_ms=g_wl7_frame_clock_ms+16u;
                        wl7_meta_set(client_idx,fb_id,WL7_OBJ_PRESENT_FB,0);
                    }
                }
                break;
            case WL7_OBJ_PRESENT_FB:
                if(opcode==0){ /* destroy */
                    wl7_presentation_fb_t *pf=wl7_find_present_fb(obj_id);
                    if(pf)ulibc_memset(pf,0,sizeof(*pf));
                    wl7_meta_drop(client_idx,obj_id);
                }
                break;
            case WL7_OBJ_XDG_WM_BASE:
                if(opcode==0){ /* destroy */
                    if(g_wl7_client_xdg_wm_base_obj[client_idx]==obj_id)g_wl7_client_xdg_wm_base_obj[client_idx]=0;
                    wl7_meta_drop(client_idx,obj_id);
                }else if(opcode==1&&body_len>=4){ /* create_positioner */
                    uint32_t pos_id=wl7_rd_u32(body+0);
                    wl7_meta_set(client_idx,pos_id,WL7_OBJ_NONE,0);
                }else if(opcode==2&&body_len>=8){ /* get_xdg_surface */
                    uint32_t xdg_surf_id=wl7_rd_u32(body+0);
                    uint32_t surf_id=wl7_rd_u32(body+4);
                    wl7_xdg_surface_t *xs=wl7_alloc_xdg_surface();
                    if(xs){
                        xs->id=xdg_surf_id;
                        xs->client_idx=client_idx;
                        xs->surface_id=surf_id;
                        xs->toplevel_id=0;
                        xs->configured=false;
                        xs->last_configure_serial=0;
                        xs->last_ack_serial=0;
                        xs->initial_commit_seen=false;
                        xs->configure_pending=false;
                        wl7_meta_set(client_idx,xdg_surf_id,WL7_OBJ_XDG_SURFACE,0);
                    }
                }else if(opcode==3&&body_len>=4){ /* pong */
                    uint32_t serial=wl7_rd_u32(body+0);
                    (void)serial;
                }
                break;
            case WL7_OBJ_XDG_SURFACE:
                if(opcode==0){ /* destroy */
                    wl7_xdg_surface_t *xs=wl7_find_xdg_surface(obj_id);
                    if(xs){
                        if(xs->toplevel_id){
                            wl7_xdg_toplevel_t *tl=wl7_find_xdg_toplevel(xs->toplevel_id);
                            if(tl){
                                wl7_meta_drop(client_idx,tl->id);
                                ulibc_memset(tl,0,sizeof(*tl));
                            }
                        }
                        ulibc_memset(xs,0,sizeof(*xs));
                    }
                    wl7_meta_drop(client_idx,obj_id);
                }else if(opcode==1&&body_len>=4){ /* get_toplevel */
                    uint32_t tl_id=wl7_rd_u32(body+0);
                    wl7_xdg_surface_t *xs=wl7_find_xdg_surface(obj_id);
                    wl7_xdg_toplevel_t *tl=wl7_alloc_xdg_toplevel();
                    if(xs&&tl){
                        tl->id=tl_id;
                        tl->client_idx=client_idx;
                        tl->surface_id=xs->surface_id;
                        tl->xdg_surface_id=xs->id;
                        tl->configured=false;
                        tl->activated=true;
                        tl->width=1280;
                        tl->height=720;
                        tl->last_configure_serial=0;
                        tl->title[0]=0;
                        tl->app_id[0]=0;
                        xs->toplevel_id=tl_id;
                        wl7_meta_set(client_idx,tl_id,WL7_OBJ_XDG_TOPLEVEL,0);
                        wl7_schedule_xdg_configure(client_idx,xs->surface_id);
                    }
                }else if(opcode==2&&body_len>=4){ /* get_popup */
                    uint32_t popup_id=wl7_rd_u32(body+0);
                    wl7_meta_set(client_idx,popup_id,WL7_OBJ_NONE,0);
                }else if(opcode==3&&body_len>=16){ /* set_window_geometry */
                    wl7_xdg_surface_t *xs=wl7_find_xdg_surface(obj_id);
                    int32_t x=wl7_rd_s32(body+0);
                    int32_t y=wl7_rd_s32(body+4);
                    int32_t w=wl7_rd_s32(body+8);
                    int32_t h=wl7_rd_s32(body+12);
                    if(xs){
                        wl7_surface_t *s=wl7_find_surface(xs->surface_id);
                        wl7_xdg_toplevel_t *tl=wl7_find_xdg_toplevel(xs->toplevel_id);
                        if(s){s->x=x;s->y=y;}
                        if(tl&&w>0&&h>0){tl->width=w;tl->height=h;}
                    }
                }else if(opcode==4&&body_len>=4){ /* ack_configure */
                    uint32_t serial=wl7_rd_u32(body+0);
                    wl7_xdg_surface_t *xs=wl7_find_xdg_surface(obj_id);
                    if(xs){
                        xs->last_ack_serial=serial;
                        xs->configured=true;
                        {
                            bool had_pending=xs->configure_pending;
                            xs->configure_pending=false;
                            if(had_pending&&serial==xs->last_configure_serial){
                                wl7_schedule_xdg_configure(client_idx,xs->surface_id);
                            }
                        }
                        if(g_wl7_send_trace_count<WL7_TRACE_SEND_LIMIT){
                            ++g_wl7_send_trace_count;
                            __boot_serial_puts("[wl7] xdg-ack obj=");
                            __boot_serial_puthex64((uint64_t)obj_id);
                            __boot_serial_puts(" serial=");
                            __boot_serial_putu32(serial);
                            __boot_serial_puts(" expected=");
                            __boot_serial_putu32(xs->last_configure_serial);
                            __boot_serial_puts("\n");
                        }
                        if(xs->toplevel_id){
                            wl7_xdg_toplevel_t *tl=wl7_find_xdg_toplevel(xs->toplevel_id);
                            if(tl&&tl->last_configure_serial==serial)tl->configured=true;
                        }
                    }
                }
                break;
            case WL7_OBJ_XDG_TOPLEVEL:{
                wl7_xdg_toplevel_t *tl=wl7_find_xdg_toplevel(obj_id);
                if(!tl)break;
                if(opcode==0){ /* destroy */
                    wl7_xdg_surface_t *xs=wl7_find_xdg_surface(tl->xdg_surface_id);
                    if(xs)xs->toplevel_id=0;
                    ulibc_memset(tl,0,sizeof(*tl));
                    wl7_meta_drop(client_idx,obj_id);
                }else if(opcode==2){ /* set_title */
                    size_t off=0;
                    char tmp[WL7_MAX_TITLE];
                    if(wl7_read_wl_string(body,body_len,&off,tmp,sizeof(tmp))){
                        ulibc_strcpy(tl->title,tmp);
                    }
                }else if(opcode==3){ /* set_app_id */
                    size_t off=0;
                    char tmp[WL7_MAX_APP_ID];
                    if(wl7_read_wl_string(body,body_len,&off,tmp,sizeof(tmp))){
                        ulibc_strcpy(tl->app_id,tmp);
                    }
                }else if(opcode==5||opcode==6){ /* move/resize */
                    wl7_surface_t *s=wl7_find_surface(tl->surface_id);
                    if(s)s->mapped=true;
                }else if(opcode==7&&body_len>=8){ /* set_max_size */
                    (void)wl7_rd_s32(body+0);
                    (void)wl7_rd_s32(body+4);
                }else if(opcode==8&&body_len>=8){ /* set_min_size */
                    (void)wl7_rd_s32(body+0);
                    (void)wl7_rd_s32(body+4);
                }else if(opcode==9||opcode==11){ /* set_maximized/fullscreen */
                    tl->activated=true;
                    wl7_schedule_xdg_configure(client_idx,tl->surface_id);
                }else if(opcode==10||opcode==12){ /* unset_maximized/fullscreen */
                    tl->activated=false;
                    wl7_schedule_xdg_configure(client_idx,tl->surface_id);
                }
                break;}
            case WL7_OBJ_REGION:
                if(opcode==0){ /* destroy */
                    wl7_meta_drop(client_idx,obj_id);
                }else if(opcode==1||opcode==2){ /* add/subtract */
                    /* Accepted as no-op; our compositor paints full buffers. */
                }
                break;
            case WL7_OBJ_CALLBACK:
                if(opcode==0){ /* destroy */
                    wl7_callback_t *cb=wl7_find_callback(obj_id);
                    int i;
                    if(cb){
                        for(i=0;i<WL7_MAX_SURFACES;++i){
                            if(!g_wl7_surfaces[i].used)continue;
                            if(g_wl7_surfaces[i].frame_callback_id==obj_id)g_wl7_surfaces[i].frame_callback_id=0;
                        }
                        cb->used=false;
                        cb->id=0;
                        cb->client_idx=0;
                        cb->surface_id=0;
                        cb->pending=false;
                        cb->due_ms=0;
                    }
                    wl7_meta_drop(client_idx,obj_id);
                }
                break;
            default:
                break;
        }
    }
out:
    g_wl7_client_processing[client_idx]=false;
    return ret;
}

int wl7_process_socket(int sock_fd){
    int idx=wl7_client_idx_by_sock(sock_fd);
    if(idx<0)return -1;
    return wl7_process_message(idx);
}

static uint32_t wl7_blend_argb_over(uint32_t dst,uint32_t src){
    uint32_t sa=(src>>24)&0xFFu;
    uint32_t sr=(src>>16)&0xFFu;
    uint32_t sg=(src>>8)&0xFFu;
    uint32_t sb=src&0xFFu;
    uint32_t dr=(dst>>16)&0xFFu;
    uint32_t dg=(dst>>8)&0xFFu;
    uint32_t db=dst&0xFFu;
    uint32_t ia;
    uint32_t rr,rg,rb;
    if(sa>=255u)return src;
    if(sa==0u)return dst;
    ia=255u-sa;
    rr=(sr*sa+dr*ia)/255u;
    rg=(sg*sa+dg*ia)/255u;
    rb=(sb*sa+db*ia)/255u;
    return 0xFF000000u|(rr<<16)|(rg<<8)|rb;
}

static uint32_t wl7_pixel_to_argb(uint32_t px,uint32_t fmt){
    uint32_t a=(px>>24)&0xFFu;
    if(fmt==WL7_FMT_XRGB8888||fmt==WL7_FMT_DRM_XRGB32){
        return px|0xFF000000u;
    }
    if(fmt==WL7_FMT_DRM_XBGR32||fmt==WL7_FMT_DRM_ABGR32){
        uint32_t aa=(fmt==WL7_FMT_DRM_XBGR32)?0xFFu:a;
        uint32_t r=px&0xFFu;
        uint32_t g=(px>>8)&0xFFu;
        uint32_t b=(px>>16)&0xFFu;
        return (aa<<24)|(r<<16)|(g<<8)|b;
    }
    if(a==0u){
        /* Many clients submit opaque RGB data with zeroed alpha. */
        return px|0xFF000000u;
    }
    return px;
}

void wl7_render_surfaces(void){
    extern c7_framebuffer_t g_fb;
    int i;
    bool rendered=false;
    for(i=0;i<WL7_MAX_SURFACES;++i){
        wl7_surface_t*s=&g_wl7_surfaces[i];
        int bidx;
        wl7_buffer_t*b;
        wl7_viewport_t *vp;
        bool viewport_active=false;
        int32_t src_x0=0,src_y0=0,src_w,src_h;
        int32_t vp_src_x0=0,vp_src_y0=0,vp_src_w=0,vp_src_h=0;
        int32_t out_w=0,out_h=0;
        int32_t row,col;
        uint32_t *fb;
        uint32_t fbw,fbh,fbp;
        uint32_t stride_px;
        if(!s->used||!s->mapped)continue;
        bidx=wl7_find_buffer_index(s->buffer_id);
        b=(bidx>=0)?&g_wl7_buffers[bidx]:0;
        if(!b){
            wl7_clear_surface_damage(i);
            g_wl7_surface_release_pending[i]=false;
            continue;
        }
        if(g_wl7_buffer_dirty[bidx])(void)wl7_refresh_buffer_data(bidx);
        if(!b->data||b->width==0||b->height==0||b->stride<4){
            wl7_clear_surface_damage(i);
            continue;
        }
        if(g_wl7_trace_count<WL7_TRACE_ATTACH_LIMIT){
            ++g_wl7_trace_count;
            __boot_serial_puts("[wl7] render surf=");
            __boot_serial_puthex64((uint64_t)s->id);
            __boot_serial_puts(" buf=");
            __boot_serial_puthex64((uint64_t)b->id);
            __boot_serial_puts(" size=");
            __boot_serial_putu32(b->width);
            __boot_serial_puts("x");
            __boot_serial_putu32(b->height);
            __boot_serial_puts(" stride=");
            __boot_serial_putu32(b->stride);
            __boot_serial_puts("\n");
        }
        vp=wl7_find_viewport_by_surface(s->id);
        vp_src_x0=0;
        vp_src_y0=0;
        vp_src_w=(int32_t)b->width;
        vp_src_h=(int32_t)b->height;
        out_w=vp_src_w;
        out_h=vp_src_h;
        if(vp){
            if(vp->src_set){
                vp_src_x0=wl7_fixed_to_int(vp->src_x);
                vp_src_y0=wl7_fixed_to_int(vp->src_y);
                vp_src_w=wl7_fixed_to_int(vp->src_w);
                vp_src_h=wl7_fixed_to_int(vp->src_h);
                viewport_active=true;
            }
            if(vp->dst_set&&vp->dst_w>0&&vp->dst_h>0){
                out_w=vp->dst_w;
                out_h=vp->dst_h;
                viewport_active=true;
            }
        }
        src_x0=vp_src_x0;
        src_y0=vp_src_y0;
        src_w=vp_src_w;
        src_h=vp_src_h;
        if(!viewport_active&&g_wl7_surface_has_damage[i]&&!g_wl7_surface_full_damage[i]){
            src_x0=g_wl7_surface_damage_x[i];
            src_y0=g_wl7_surface_damage_y[i];
            src_w=g_wl7_surface_damage_w[i];
            src_h=g_wl7_surface_damage_h[i];
        }
        if(src_x0<0){src_w+=src_x0;src_x0=0;}
        if(src_y0<0){src_h+=src_y0;src_y0=0;}
        if(src_x0>=((int32_t)b->width)||src_y0>=((int32_t)b->height)||src_w<=0||src_h<=0){
            wl7_clear_surface_damage(i);
            if(g_wl7_surface_release_pending[i]){
                if(s->client_idx>=0&&s->client_idx<WL7_MAX_CLIENTS&&g_wl7_clients[s->client_idx].used){
                    wl7_send_buffer_release(g_wl7_clients[s->client_idx].sock_fd,s->buffer_id);
                }
                g_wl7_surface_release_pending[i]=false;
            }
            continue;
        }
        if(src_x0+src_w>(int32_t)b->width)src_w=(int32_t)b->width-src_x0;
        if(src_y0+src_h>(int32_t)b->height)src_h=(int32_t)b->height-src_y0;
        if(viewport_active&&out_w<=0)out_w=src_w;
        if(viewport_active&&out_h<=0)out_h=src_h;
        fb=(uint32_t*)(uintptr_t)g_fb.address;
        fbw=g_fb.width;
        fbh=g_fb.height;
        fbp=g_fb.pitch;
        stride_px=b->stride/4u;
        if(stride_px==0u||fbp<4u){
            wl7_clear_surface_damage(i);
            continue;
        }
        if(viewport_active){
            rendered=true;
            for(row=0;row<out_h;++row){
                int32_t sy=src_y0+(int32_t)(((int64_t)row*(int64_t)src_h)/(int64_t)out_h);
                int32_t dy=s->y+row;
                if(dy<0||dy>=(int32_t)fbh)continue;
                for(col=0;col<out_w;++col){
                    int32_t sx=src_x0+(int32_t)(((int64_t)col*(int64_t)src_w)/(int64_t)out_w);
                    int32_t dx=s->x+col;
                    uint32_t src_off,dst_off,src_px,dst_px;
                    if(dx<0||dx>=(int32_t)fbw)continue;
                    src_off=(uint32_t)sy*stride_px+(uint32_t)sx;
                    dst_off=(uint32_t)dy*(fbp/4u)+(uint32_t)dx;
                    if(src_off>=b->data_size/4u||dst_off>=fbw*fbh)continue;
                    src_px=((uint32_t*)b->data)[src_off];
                    src_px=wl7_pixel_to_argb(src_px,b->format);
                    dst_px=fb[dst_off];
                    fb[dst_off]=wl7_blend_argb_over(dst_px,src_px);
                }
            }
        }else{
            rendered=true;
            for(row=0;row<src_h;++row){
                int32_t sy=src_y0+row;
                int32_t dy=s->y+sy;
                if(dy<0||dy>=(int32_t)fbh)continue;
                for(col=0;col<src_w;++col){
                    int32_t sx=src_x0+col;
                    int32_t dx=s->x+sx;
                    uint32_t src_off,dst_off,src_px,dst_px;
                    if(dx<0||dx>=(int32_t)fbw)continue;
                    src_off=(uint32_t)sy*stride_px+(uint32_t)sx;
                    dst_off=(uint32_t)dy*(fbp/4u)+(uint32_t)dx;
                    if(src_off>=b->data_size/4u||dst_off>=fbw*fbh)continue;
                    src_px=((uint32_t*)b->data)[src_off];
                    src_px=wl7_pixel_to_argb(src_px,b->format);
                    dst_px=fb[dst_off];
                    fb[dst_off]=wl7_blend_argb_over(dst_px,src_px);
                }
            }
        }
        wl7_clear_surface_damage(i);
        if(g_wl7_surface_release_pending[i]){
            if(s->client_idx>=0&&s->client_idx<WL7_MAX_CLIENTS&&g_wl7_clients[s->client_idx].used){
                wl7_send_buffer_release(g_wl7_clients[s->client_idx].sock_fd,s->buffer_id);
            }
            g_wl7_surface_release_pending[i]=false;
        }
    }
    if(rendered)ridux_request_cursor_redraw();
}

void wl7_push_keyboard_event(int client_idx,uint32_t key,uint32_t state){
    uint32_t obj;
    uint32_t focus;
    uint8_t pld[16];
    if(client_idx<0||client_idx>=WL7_MAX_CLIENTS)return;
    if(!g_wl7_clients[client_idx].used)return;
    obj=g_wl7_client_keyboard_obj[client_idx];
    if(!obj)return;
    focus=g_wl7_client_keyboard_focus[client_idx];
    if(!focus){
        focus=wl7_pick_mapped_surface(client_idx);
        if(focus){
            wl7_send_keyboard_enter(g_wl7_clients[client_idx].sock_fd,obj,focus);
            g_wl7_client_keyboard_focus[client_idx]=focus;
        }
    }
    wl7_wr_u32(pld+0,wl7_next_serial());
    wl7_wr_u32(pld+4,g_wl7_frame_clock_ms);
    wl7_wr_u32(pld+8,key);
    wl7_wr_u32(pld+12,state);
    (void)wl7_send_event(g_wl7_clients[client_idx].sock_fd,obj,3,pld,16);
}

void wl7_push_pointer_event(int client_idx,int x,int y,uint32_t button,uint32_t state){
    uint32_t obj;
    uint32_t touch_obj;
    uint32_t focus;
    uint8_t pld[16];
    if(client_idx<0||client_idx>=WL7_MAX_CLIENTS)return;
    if(!g_wl7_clients[client_idx].used)return;
    obj=g_wl7_client_pointer_obj[client_idx];
    touch_obj=g_wl7_client_touch_obj[client_idx];
    if(!obj&&!touch_obj)return;
    if(obj){
        focus=g_wl7_client_pointer_focus[client_idx];
        if(!focus){
            focus=wl7_pick_mapped_surface(client_idx);
            if(focus){
                wl7_send_pointer_enter(g_wl7_clients[client_idx].sock_fd,obj,focus,x,y);
                g_wl7_client_pointer_focus[client_idx]=focus;
            }
        }
        wl7_wr_u32(pld+0,g_wl7_frame_clock_ms);
        wl7_wr_u32(pld+4,(uint32_t)wl7_fixed_from_int(x));
        wl7_wr_u32(pld+8,(uint32_t)wl7_fixed_from_int(y));
        (void)wl7_send_event(g_wl7_clients[client_idx].sock_fd,obj,2,pld,12); /* motion */
        if(button){
            wl7_wr_u32(pld+0,wl7_next_serial());
            wl7_wr_u32(pld+4,g_wl7_frame_clock_ms);
            wl7_wr_u32(pld+8,wl7_linux_button_code(button));
            wl7_wr_u32(pld+12,state);
            (void)wl7_send_event(g_wl7_clients[client_idx].sock_fd,obj,3,pld,16); /* button */
        }
        (void)wl7_send_event(g_wl7_clients[client_idx].sock_fd,obj,5,0,0); /* frame */
    }
    if(touch_obj){
        focus=g_wl7_client_touch_focus[client_idx];
        if(!focus){
            focus=wl7_pick_mapped_surface(client_idx);
            g_wl7_client_touch_focus[client_idx]=focus;
        }
        if(focus){
            if(button&&state&&!g_wl7_client_touch_active[client_idx]){
                wl7_send_touch_down(g_wl7_clients[client_idx].sock_fd,touch_obj,focus,x,y);
                g_wl7_client_touch_active[client_idx]=true;
            }else if(button&&!state&&g_wl7_client_touch_active[client_idx]){
                wl7_send_touch_motion(g_wl7_clients[client_idx].sock_fd,touch_obj,x,y);
                wl7_send_touch_up(g_wl7_clients[client_idx].sock_fd,touch_obj);
                g_wl7_client_touch_active[client_idx]=false;
            }else if(g_wl7_client_touch_active[client_idx]){
                wl7_send_touch_motion(g_wl7_clients[client_idx].sock_fd,touch_obj,x,y);
            }
            g_wl7_client_touch_x[client_idx]=x;
            g_wl7_client_touch_y[client_idx]=y;
            if(g_wl7_client_touch_active[client_idx]||button){
                wl7_send_touch_frame(g_wl7_clients[client_idx].sock_fd,touch_obj);
            }
        }
    }
}

int wl7_dispatch_pointer_event(int screen_x,int screen_y,uint32_t button,uint32_t state){
    int i;
    for(i=WL7_MAX_SURFACES-1;i>=0;--i){
        wl7_surface_t *s=&g_wl7_surfaces[i];
        wl7_buffer_t *b;
        int lx,ly;
        if(!s->used||!s->mapped)continue;
        b=wl7_find_buffer(s->buffer_id);
        if(!b||!b->used||!b->width||!b->height)continue;
        if(screen_x<s->x||screen_y<s->y)continue;
        if(screen_x>=s->x+(int)b->width||screen_y>=s->y+(int)b->height)continue;
        lx=screen_x-s->x;
        ly=screen_y-s->y;
        /* Si el usuario clickea una superficie Wayland, esa superficie pasa
         * a ser la dueña del teclado. Esto era lo que dejaba a Firefox
         * dibujado pero medio "fantasma": recibia pixels, no foco real. */
        if(button&&state){
            wl7_set_active_surface(s->client_idx,s->id,lx,ly,true,true);
        }else{
            wl7_set_active_surface(s->client_idx,s->id,lx,ly,true,false);
        }
        wl7_push_pointer_event(s->client_idx,lx,ly,button,state);
        return 1;
    }
    return 0;
}

int wl7_dispatch_keyboard_event(uint32_t key,uint32_t state){
    int i;
    if(g_wl7_active_client>=0&&g_wl7_active_client<WL7_MAX_CLIENTS&&
       g_wl7_clients[g_wl7_active_client].used&&
       wl7_surface_accepts_input(g_wl7_active_client,g_wl7_active_surface)){
        wl7_set_active_surface(g_wl7_active_client,g_wl7_active_surface,0,0,false,true);
        wl7_push_keyboard_event(g_wl7_active_client,key,state);
        return 1;
    }
    for(i=0;i<WL7_MAX_CLIENTS;++i){
        uint32_t focus;
        if(!g_wl7_clients[i].used)continue;
        focus=g_wl7_client_keyboard_focus[i];
        if(!focus)focus=wl7_pick_mapped_surface(i);
        if(!focus)continue;
        wl7_set_active_surface(i,focus,0,0,false,true);
        wl7_push_keyboard_event(i,key,state);
        return 1;
    }
    return 0;
}

static bool wl7_has_work(void){
    int i;
    for(i=0;i<WL7_MAX_CLIENTS;++i)if(g_wl7_clients[i].used)return true;
    for(i=0;i<WL7_MAX_SURFACES;++i){
        if(g_wl7_surfaces[i].used&&
           (g_wl7_surface_has_damage[i]||g_wl7_surface_release_pending[i]))return true;
    }
    for(i=0;i<WL7_MAX_CALLBACKS;++i)if(g_wl7_callbacks[i].used&&g_wl7_callbacks[i].pending)return true;
    for(i=0;i<WL7_MAX_PRESENT_FEEDBACK;++i)if(g_wl7_present_fbs[i].used&&g_wl7_present_fbs[i].pending)return true;
    return false;
}

void wl7_tick(void){
    int i;
    int pass;
    if(!wl7_has_work())return;
    g_wl7_frame_clock_ms+=16u;
    for(i=0;i<WL7_MAX_CLIENTS;++i){
        if(!g_wl7_clients[i].used)continue;
        for(pass=0;pass<16;++pass){
            if(wl7_process_message(i)<0)break;
        }
    }
    for(i=0;i<WL7_MAX_CALLBACKS;++i){
        wl7_callback_t *cb=&g_wl7_callbacks[i];
        if(!cb->used||!cb->pending)continue;
        if(cb->due_ms>g_wl7_frame_clock_ms)continue;
        if(cb->client_idx>=0&&cb->client_idx<WL7_MAX_CLIENTS&&g_wl7_clients[cb->client_idx].used){
            wl7_send_callback_done(g_wl7_clients[cb->client_idx].sock_fd,cb->id,g_wl7_frame_clock_ms);
            if(cb->surface_id){
                wl7_surface_t *s=wl7_find_surface(cb->surface_id);
                if(s&&s->frame_callback_id==cb->id)s->frame_callback_id=0;
            }
        }
        wl7_meta_drop(cb->client_idx,cb->id);
        cb->used=false;
        cb->id=0;
        cb->client_idx=0;
        cb->surface_id=0;
        cb->pending=false;
        cb->due_ms=0;
    }
    for(i=0;i<WL7_MAX_PRESENT_FEEDBACK;++i){
        wl7_presentation_fb_t *pf=&g_wl7_present_fbs[i];
        if(!pf->used||!pf->pending)continue;
        if(pf->due_ms>g_wl7_frame_clock_ms)continue;
        if(pf->client_idx>=0&&pf->client_idx<WL7_MAX_CLIENTS&&g_wl7_clients[pf->client_idx].used){
            uint32_t out_obj=g_wl7_client_output_obj[pf->client_idx];
            wl7_send_presentation_feedback(g_wl7_clients[pf->client_idx].sock_fd,pf->id,out_obj);
        }
        wl7_meta_drop(pf->client_idx,pf->id);
        ulibc_memset(pf,0,sizeof(*pf));
    }
    for(i=0;i<WL7_MAX_SURFACES;++i){
        int bidx;
        if(!g_wl7_surfaces[i].used||!g_wl7_surfaces[i].mapped)continue;
        if(!g_wl7_surface_has_damage[i]&&!g_wl7_surface_release_pending[i])continue;
        bidx=wl7_find_buffer_index(g_wl7_surfaces[i].buffer_id);
        if(bidx>=0)g_wl7_buffer_dirty[bidx]=true;
    }
    wl7_render_surfaces();
}

void compat7_tick_all(void){
    static uint32_t tick_div;
    if((tick_div++ & 3u)==0u)tcp7_tick();
    x11_tick();
    wl7_tick();
}

/* SHELL COMMANDS + INIT */
static void cmd7_tcp(const char*a,char*o,int mx){
    (void)a;int i,cnt=0;
    ulibc_snprintf(o,(size_t)mx,
        "=== RiduxOS Real TCP (compat7) ===\n"
        "Max connections: %d  MSS: %d  Initial CWND: %d\n"
        "Active TCBs:\n",TCP7_MAX_CONNECTIONS,TCP7_MSS_DEFAULT,TCP7_INITIAL_CWND);
    size_t l=ulibc_strlen(o);
    for(i=0;i<TCP7_MAX_CONNECTIONS&&l<(size_t)mx-64;++i){
        tcp7_tcb_t*t=&g_tcp7_tcbs[i];if(!t->sock_fd||t->sock_fd<0)continue;
        cnt++;
        char tmp[128];ulibc_snprintf(tmp,sizeof(tmp),"  fd=%d state=%d cwnd=%u ssthresh=%u srtt=%u\n",
            t->sock_fd,t->state,t->cwnd,t->ssthresh,t->srtt);
        ulibc_memcpy(o+l,tmp,ulibc_strlen(tmp)+1);l+=ulibc_strlen(tmp);
    }
    if(!cnt){ulibc_memcpy(o+l,"  (none)\n",9);l+=9;}
}

static void cmd7_tls(const char*a,char*o,int mx){
    (void)a;int i,cnt=0;
    ulibc_snprintf(o,(size_t)mx,
        "=== RiduxOS TLS 1.2 (compat7) ===\n"
        "Max sessions: %d  CA certs: %d\n"
        "Cipher: RSA_WITH_AES_128_CBC_SHA256\n"
        "Active sessions:\n",TLS7_MAX_SESSIONS,g_ca7_count);
    size_t l=ulibc_strlen(o);
    for(i=0;i<TLS7_MAX_SESSIONS&&l<(size_t)mx-64;++i){
        tls7_session_t*s=&g_tls7_sessions[i];if(!s->used)continue;
        cnt++;
        char tmp[128];ulibc_snprintf(tmp,sizeof(tmp),"  fd=%d state=%d sni=%s\n",
            s->sock_fd,s->state,s->sni_hostname[0]?s->sni_hostname:"(none)");
        ulibc_memcpy(o+l,tmp,ulibc_strlen(tmp)+1);l+=ulibc_strlen(tmp);
    }
    if(!cnt){ulibc_memcpy(o+l,"  (none)\n",9);l+=9;}
}

static void cmd7_x11(const char*a,char*o,int mx){
    (void)a;int i,cnt=0;
    ulibc_snprintf(o,(size_t)mx,
        "=== RiduxOS X11 Protocol (compat7) ===\n"
        "Max connections: %d  Max windows: %d\n"
        "Active connections:\n",X11_MAX_CONN,X11_MAX_WINDOWS);
    size_t l=ulibc_strlen(o);
    for(i=0;i<X11_MAX_CONN&&l<(size_t)mx-64;++i){
        x11_connection_t*c=&g_x11_conns[i];if(!c->used)continue;
        cnt++;
        char tmp[200];ulibc_snprintf(tmp,sizeof(tmp),"  fd=%d windows=%d pixmaps=%d gcs=%d props=%d shm=%d pics=%d atoms=%d events=%d ptr=(%d,%d)\n",
            c->sock_fd,c->window_count,c->pixmap_count,c->gc_count,c->prop_count,c->shmseg_count,c->picture_count,c->atom_count,c->event_count,c->pointer_x,c->pointer_y);
        ulibc_memcpy(o+l,tmp,ulibc_strlen(tmp)+1);l+=ulibc_strlen(tmp);
    }
    if(!cnt){ulibc_memcpy(o+l,"  (none)\n",9);l+=9;}
}

static void cmd7_wl(const char*a,char*o,int mx){
    (void)a;
    ulibc_snprintf(o,(size_t)mx,
        "=== RiduxOS Wayland Bridge (compat7) ===\n"
        "Clients: %d/%d  Surfaces: %d/%d  Buffers: %d/%d  Globals: %d\n",
        WL7_MAX_CLIENTS,WL7_MAX_CLIENTS,
        WL7_MAX_SURFACES,WL7_MAX_SURFACES,
        WL7_MAX_BUFFERS,WL7_MAX_BUFFERS,
        g_wl7_global_count);
}

void compat7_register_shell_cmds(void){
    extern compat_shell_cmd_t g_compat_cmds[];
    extern int g_compat_cmd_count;
    #define REG7(n,h,fn) if(g_compat_cmd_count<COMPAT_SHELL_CMD_MAX){g_compat_cmds[g_compat_cmd_count].name=n;g_compat_cmds[g_compat_cmd_count].help=h;g_compat_cmds[g_compat_cmd_count].handler=fn;++g_compat_cmd_count;}
    REG7("tcp7","Real TCP connections",cmd7_tcp)
    REG7("tls7","TLS 1.2 sessions",cmd7_tls)
    REG7("x11","X11 protocol connections",cmd7_x11)
    REG7("wl","Wayland compositor bridge",cmd7_wl)
    #undef REG7
}

void compat7_init_all(void){
    int i;
    ulibc_memset(g_tcp7_tcbs,0,sizeof(g_tcp7_tcbs));
    ulibc_memset(g_tls7_sessions,0,sizeof(g_tls7_sessions));
    ulibc_memset(g_x11_conns,0,sizeof(g_x11_conns));
    g_x11_render_dirty=false;
    g_x11_trace_req_count=0;
    g_x11_trace_ext_count=0;
    g_x11_trace_win_count=0;
    g_x11_trace_event_count=0;
    g_x11_trace_atom_count=0;
    g_x11_trace_render_count=0;
    g_x11_server_time=1;
    g_wl7_trace_count=0;
    g_wl7_req_trace_count=0;
    g_wl7_send_trace_count=0;
    g_wl7_active_client=-1;
    g_wl7_active_surface=0;
    ulibc_memset(g_wl7_clients,0,sizeof(g_wl7_clients));
    ulibc_memset(g_wl7_surfaces,0,sizeof(g_wl7_surfaces));
    ulibc_memset(g_wl7_buffers,0,sizeof(g_wl7_buffers));
    ulibc_memset(g_wl7_globals,0,sizeof(g_wl7_globals));
    ulibc_memset(g_wl7_meta,0,sizeof(g_wl7_meta));
    ulibc_memset(g_wl7_pools,0,sizeof(g_wl7_pools));
    ulibc_memset(g_wl7_callbacks,0,sizeof(g_wl7_callbacks));
    ulibc_memset(g_wl7_xdg_surfaces,0,sizeof(g_wl7_xdg_surfaces));
    ulibc_memset(g_wl7_xdg_toplevels,0,sizeof(g_wl7_xdg_toplevels));
    ulibc_memset(g_wl7_viewports,0,sizeof(g_wl7_viewports));
    ulibc_memset(g_wl7_dmabuf_params,0,sizeof(g_wl7_dmabuf_params));
    ulibc_memset(g_wl7_present_fbs,0,sizeof(g_wl7_present_fbs));
    ulibc_memset(g_wl7_buffer_pool,0,sizeof(g_wl7_buffer_pool));
    ulibc_memset(g_wl7_buffer_offset,0,sizeof(g_wl7_buffer_offset));
    ulibc_memset(g_wl7_buffer_dirty,0,sizeof(g_wl7_buffer_dirty));
    ulibc_memset(g_wl7_buffer_owns_fd,0,sizeof(g_wl7_buffer_owns_fd));
    ulibc_memset(g_wl7_client_registry_obj,0,sizeof(g_wl7_client_registry_obj));
    ulibc_memset(g_wl7_client_seat_obj,0,sizeof(g_wl7_client_seat_obj));
    ulibc_memset(g_wl7_client_pointer_obj,0,sizeof(g_wl7_client_pointer_obj));
    ulibc_memset(g_wl7_client_keyboard_obj,0,sizeof(g_wl7_client_keyboard_obj));
    ulibc_memset(g_wl7_client_touch_obj,0,sizeof(g_wl7_client_touch_obj));
    ulibc_memset(g_wl7_client_output_obj,0,sizeof(g_wl7_client_output_obj));
    ulibc_memset(g_wl7_client_xdg_wm_base_obj,0,sizeof(g_wl7_client_xdg_wm_base_obj));
    ulibc_memset(g_wl7_client_viewporter_obj,0,sizeof(g_wl7_client_viewporter_obj));
    ulibc_memset(g_wl7_client_dmabuf_obj,0,sizeof(g_wl7_client_dmabuf_obj));
    ulibc_memset(g_wl7_client_presentation_obj,0,sizeof(g_wl7_client_presentation_obj));
    ulibc_memset(g_wl7_client_pointer_focus,0,sizeof(g_wl7_client_pointer_focus));
    ulibc_memset(g_wl7_client_keyboard_focus,0,sizeof(g_wl7_client_keyboard_focus));
    ulibc_memset(g_wl7_client_touch_focus,0,sizeof(g_wl7_client_touch_focus));
    ulibc_memset(g_wl7_client_processing,0,sizeof(g_wl7_client_processing));
    ulibc_memset(g_wl7_client_touch_active,0,sizeof(g_wl7_client_touch_active));
    ulibc_memset(g_wl7_client_touch_x,0,sizeof(g_wl7_client_touch_x));
    ulibc_memset(g_wl7_client_touch_y,0,sizeof(g_wl7_client_touch_y));
    ulibc_memset(g_wl7_surface_has_damage,0,sizeof(g_wl7_surface_has_damage));
    ulibc_memset(g_wl7_surface_full_damage,0,sizeof(g_wl7_surface_full_damage));
    ulibc_memset(g_wl7_surface_release_pending,0,sizeof(g_wl7_surface_release_pending));
    ulibc_memset(g_wl7_surface_damage_x,0,sizeof(g_wl7_surface_damage_x));
    ulibc_memset(g_wl7_surface_damage_y,0,sizeof(g_wl7_surface_damage_y));
    ulibc_memset(g_wl7_surface_damage_w,0,sizeof(g_wl7_surface_damage_w));
    ulibc_memset(g_wl7_surface_damage_h,0,sizeof(g_wl7_surface_damage_h));
    g_wl7_global_count=0;
    g_wl7_serial=1u;
    g_wl7_frame_clock_ms=0u;
    g_ca7_count=0;
    for(i=0;i<TCP7_MAX_CONNECTIONS;++i)g_tcp7_tcbs[i].sock_fd=-1;
    for(i=0;i<WL7_MAX_POOLS;++i)g_wl7_pools[i].fd=-1;
    for(i=0;i<WL7_MAX_DMABUF_PARAMS;++i)g_wl7_dmabuf_params[i].fd=-1;
    wl7_register_globals();
    compat7_register_shell_cmds();
}
