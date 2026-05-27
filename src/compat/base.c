/*
 * Base de compatibilidad de Ridux.
 *
 * Aca quedan drivers simples, tabla de syscalls, sockets virtuales y varias
 * piezas viejas que todavia son utiles para arrancar userspace.
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "base.h"
#include "memory_tasks.h"
#include "linux_syscalls.h"
#include "display_wayland.h"

extern bool kvfs_exists(const char *path);
static bool compat_wayfire_debug_trace_enabled(void);

/* Local IO helpers */
#define SOCK_VIRT_REQ_SNAPSHOT_SIZE 4096u

static inline uint8_t c_inb(uint16_t port) {
    uint8_t r; __asm__ volatile("inb %1, %0" : "=a"(r) : "Nd"(port)); return r;
}
static inline void c_outb(uint16_t port, uint8_t v) {
    __asm__ volatile("outb %0, %1" : : "a"(v), "Nd"(port));
}
static inline uint16_t c_inw(uint16_t port) {
    uint16_t r; __asm__ volatile("inw %1, %0" : "=a"(r) : "Nd"(port)); return r;
}
static inline void c_outw(uint16_t port, uint16_t v) {
    __asm__ volatile("outw %0, %1" : : "a"(v), "Nd"(port));
}
static inline uint32_t c_inl(uint16_t port) {
    uint32_t r; __asm__ volatile("inl %1, %0" : "=a"(r) : "Nd"(port)); return r;
}
static inline void c_outl(uint16_t port, uint32_t v) {
    __asm__ volatile("outl %0, %1" : : "a"(v), "Nd"(port));
}
static inline void c_io_wait(void) { c_outb(0x80, 0); }

static bool compat_task_name_contains(const task_t *t,const char *needle){
    size_t i,j,nl;
    if(!t||!needle)return false;
    for(nl=0;needle[nl];++nl){}
    if(!nl)return true;
    for(i=0;t->name[i];++i){
        for(j=0;j<nl&&t->name[i+j]&&t->name[i+j]==needle[j];++j){}
        if(j==nl)return true;
    }
    return false;
}

/* Local string/mem helpers */
static void *c_memset(void *d, int v, size_t n) {
    uint8_t *p=(uint8_t*)d; size_t i; for(i=0;i<n;++i) p[i]=(uint8_t)v; return d;
}
static void *c_memcpy(void *d, const void *s, size_t n) {
    uint8_t *dd=(uint8_t*)d; const uint8_t *ss=(const uint8_t*)s;
    size_t i; for(i=0;i<n;++i) dd[i]=ss[i]; return d;
}
static int c_memcmp(const void *a,const void *b,size_t n){
    const uint8_t *x=(const uint8_t*)a,*y=(const uint8_t*)b;
    size_t i;
    for(i=0;i<n;++i)if(x[i]!=y[i])return(int)x[i]-(int)y[i];
    return 0;
}
static size_t c_strlen(const char *s) { size_t n=0; while(s[n])++n; return n; }
static void c_strlcpy(char *d, const char *s, size_t c) {
    size_t i=0; if(!c)return; while(i+1<c&&s[i]){d[i]=s[i];++i;} d[i]=0;
}
static int c_strcmp(const char *a, const char *b) {
    while(*a&&*b&&*a==*b){++a;++b;} return (int)((unsigned char)*a-(unsigned char)*b);
}
static int c_strncmp(const char *a,const char *b,size_t n){
    size_t i;
    for(i=0;i<n&&a[i]&&b[i];++i)if(a[i]!=b[i])return(int)((unsigned char)a[i]-(unsigned char)b[i]);
    if(i==n)return 0;
    return(int)((unsigned char)a[i]-(unsigned char)b[i]);
}
static char c_tolower(char ch){return(ch>='A'&&ch<='Z')?(char)(ch+32):ch;}
static int c_strncasecmp(const char *a,const char *b,size_t n){
    size_t i;
    for(i=0;i<n&&a[i]&&b[i];++i){
        char ca=c_tolower(a[i]),cb=c_tolower(b[i]);
        if(ca!=cb)return(int)((unsigned char)ca-(unsigned char)cb);
    }
    if(i==n)return 0;
    return(int)((unsigned char)c_tolower(a[i])-(unsigned char)c_tolower(b[i]));
}
static bool c_starts_with(const char *s,const char *prefix){
    size_t n;
    if(!s||!prefix)return false;
    n=c_strlen(prefix);
    return c_strncmp(s,prefix,n)==0;
}
static bool c_mem_has(const uint8_t *buf,size_t len,const char *pat){
    size_t i,pl;
    if(!buf||!pat)return false;
    pl=c_strlen(pat);
    if(pl==0||len<pl)return false;
    for(i=0;i+pl<=len;++i){
        if(c_memcmp(buf+i,pat,pl)==0)return true;
    }
    return false;
}
static void c_append_str(char *d, size_t *l, size_t c, const char *s) {
    while(*s&&*l+1<c){d[(*l)++]=*s++;} d[*l]=0;
}
static void c_append_ch(char *d, size_t *l, size_t c, char ch) {
    if(*l+1<c){d[(*l)++]=ch; d[*l]=0;}
}
static void c_append_u32(char *d, size_t *l, size_t c, uint32_t v) {
    char t[12]; int i=0;
    if(!v){c_append_ch(d,l,c,'0');return;}
    while(v){t[i++]='0'+(char)(v%10);v/=10;} while(--i>=0)c_append_ch(d,l,c,t[i]);
}
static void c_append_i32(char *d, size_t *l, size_t c, int32_t v) {
    if(v<0){c_append_ch(d,l,c,'-');c_append_u32(d,l,c,(uint32_t)(-v));return;}
    c_append_u32(d,l,c,(uint32_t)v);
}
static void c_append_u64(char *d, size_t *l, size_t c, uint64_t v) {
    char t[24]; int i=0;
    if(!v){c_append_ch(d,l,c,'0');return;}
    while(v){t[i++]='0'+(char)(v%10);v/=10;} while(--i>=0)c_append_ch(d,l,c,t[i]);
}
static void c_append_ip4(char *d, size_t *l, size_t c, uint32_t ip) {
    c_append_u32(d,l,c,(ip>>24)&0xFF); c_append_ch(d,l,c,'.');
    c_append_u32(d,l,c,(ip>>16)&0xFF); c_append_ch(d,l,c,'.');
    c_append_u32(d,l,c,(ip>>8)&0xFF);  c_append_ch(d,l,c,'.');
    c_append_u32(d,l,c,ip&0xFF);
}
static void c_append_mac(char *d, size_t *l, size_t cap, const uint8_t *m) {
    const char *hx="0123456789abcdef"; int i;
    for(i=0;i<6;++i){if(i)c_append_ch(d,l,cap,':');
        c_append_ch(d,l,cap,hx[(m[i]>>4)&0xF]);c_append_ch(d,l,cap,hx[m[i]&0xF]);}
}
static uint32_t c_make_ip4(uint8_t a,uint8_t b,uint8_t c,uint8_t d) {
    return ((uint32_t)a<<24)|((uint32_t)b<<16)|((uint32_t)c<<8)|d;
}

/* Forward declarations for functions used before their definition */
static void   e1000_setup_rings(int iface_idx);
static bool   sock_ip_match(uint32_t bind_ip, uint32_t target_ip);
static size_t sock_push_rx(socket_t *dst, const uint8_t *src, size_t len);

/* PCI config (local) */
static uint32_t pci_rd32(uint8_t bus,uint8_t slot,uint8_t func,uint8_t off) {
    c_outl(0xCF8, 0x80000000u|((uint32_t)bus<<16)|((uint32_t)slot<<11)|
           ((uint32_t)func<<8)|(off&0xFCu));
    return c_inl(0xCFC);
}
static uint16_t pci_rd16(uint8_t bus,uint8_t slot,uint8_t func,uint8_t off) {
    uint32_t v=pci_rd32(bus,slot,func,(uint8_t)(off&0xFCu));
    return (uint16_t)((v>>((off&2u)*8u))&0xFFFFu);
}
static void pci_wr32(uint8_t bus,uint8_t slot,uint8_t func,uint8_t off,uint32_t val) {
    c_outl(0xCF8, 0x80000000u|((uint32_t)bus<<16)|((uint32_t)slot<<11)|
           ((uint32_t)func<<8)|(off&0xFCu));
    c_outl(0xCFC, val);
}

/* ATA PIO driver */

#define ATA_PRIMARY_IO   0x1F0
#define ATA_PRIMARY_CTRL 0x3F6
#define ATA_SECONDARY_IO 0x170
#define ATA_SECONDARY_CTRL 0x376
#define ATA_REG_DATA     0
#define ATA_REG_ERROR    1
#define ATA_REG_FEATURES 1
#define ATA_REG_SECCOUNT 2
#define ATA_REG_LBA_LO   3
#define ATA_REG_LBA_MID  4
#define ATA_REG_LBA_HI   5
#define ATA_REG_DRIVE    6
#define ATA_REG_STATUS   7
#define ATA_REG_COMMAND  7
#define ATA_SR_BSY  0x80
#define ATA_SR_DRDY 0x40
#define ATA_SR_DF   0x20
#define ATA_SR_DRQ  0x08
#define ATA_SR_ERR  0x01
#define ATA_CMD_READ_PIO  0x20
#define ATA_CMD_WRITE_PIO 0x30
#define ATA_CMD_IDENTIFY  0xEC
#define ATA_CMD_IDENTIFY_PACKET 0xA1
#define ATA_CMD_FLUSH     0xE7

ata_disk_t  g_ata_disks[ATA_MAX_DISKS];
int         g_ata_disk_count;
block_dev_t g_block_devs[BLOCK_MAX_DEVS];
int         g_block_dev_count;

static bool ata_wait_bsy(uint16_t io, int tries) {
    int i; for(i=0;i<tries;++i){if(!(c_inb(io+ATA_REG_STATUS)&ATA_SR_BSY))return true;c_io_wait();}return false;
}
static bool ata_wait_drq(uint16_t io, int tries) {
    int i; for(i=0;i<tries;++i){uint8_t s=c_inb(io+ATA_REG_STATUS);if(s&ATA_SR_ERR)return false;if(s&ATA_SR_DF)return false;if(s&ATA_SR_DRQ)return true;c_io_wait();}return false;
}
static void ata_400ns(uint16_t io){c_inb(io+ATA_REG_STATUS);c_inb(io+ATA_REG_STATUS);c_inb(io+ATA_REG_STATUS);c_inb(io+ATA_REG_STATUS);}
static void ata_soft_reset(uint16_t ctrl){c_outb(ctrl,0x04);c_io_wait();c_io_wait();c_outb(ctrl,0x00);c_io_wait();c_io_wait();}
static void ata_string_fix(char *s, int len) {
    int i; for(i=0;i<len;i+=2){char t=s[i];s[i]=s[i+1];s[i+1]=t;}
    for(i=len-1;i>=0&&s[i]==' ';--i)s[i]=0; s[len]=0;
}

static bool ata_identify(uint8_t ch, uint8_t dr, ata_disk_t *out) {
    uint16_t io=(ch==0)?ATA_PRIMARY_IO:ATA_SECONDARY_IO;
    uint16_t ident[256]; int i; uint8_t st;
    c_memset(out,0,sizeof(*out)); out->channel=ch; out->drive=dr;
    c_outb(io+ATA_REG_DRIVE,(uint8_t)(0xA0|(dr<<4))); ata_400ns(io);
    c_outb(io+ATA_REG_SECCOUNT,0); c_outb(io+ATA_REG_LBA_LO,0);
    c_outb(io+ATA_REG_LBA_MID,0); c_outb(io+ATA_REG_LBA_HI,0);
    c_outb(io+ATA_REG_COMMAND,ATA_CMD_IDENTIFY); ata_400ns(io);
    st=c_inb(io+ATA_REG_STATUS); if(!st)return false;
    if(!ata_wait_bsy(io,100000))return false;
    if(c_inb(io+ATA_REG_LBA_MID)||c_inb(io+ATA_REG_LBA_HI)){
        c_outb(io+ATA_REG_COMMAND,ATA_CMD_IDENTIFY_PACKET); ata_400ns(io);
        if(!ata_wait_bsy(io,100000))return false;
        if(c_inb(io+ATA_REG_STATUS)&ATA_SR_ERR)return false;
        out->is_atapi=true;
    }
    if(!ata_wait_drq(io,100000))return false;
    for(i=0;i<256;++i)ident[i]=c_inw(io+ATA_REG_DATA);
    c_memcpy(out->identify,ident,sizeof(ident));
    c_memcpy(out->serial,&ident[10],20); ata_string_fix(out->serial,20);
    c_memcpy(out->firmware,&ident[23],8); ata_string_fix(out->firmware,8);
    c_memcpy(out->model,&ident[27],40); ata_string_fix(out->model,40);
    if(ident[83]&(1<<10)){out->sectors=ident[100]|((uint32_t)ident[101]<<16);out->sectors_hi=ident[102]|((uint32_t)ident[103]<<16);}
    else{out->sectors=ident[60]|((uint32_t)ident[61]<<16);out->sectors_hi=0;}
    out->present=true; return true;
}

void ata_init(void) {
    int ch,dr; g_ata_disk_count=0; c_memset(g_ata_disks,0,sizeof(g_ata_disks));
    for(ch=0;ch<2;++ch){ata_soft_reset((ch==0)?ATA_PRIMARY_CTRL:ATA_SECONDARY_CTRL);
        for(dr=0;dr<2;++dr){if(g_ata_disk_count>=ATA_MAX_DISKS)break;
            if(ata_identify((uint8_t)ch,(uint8_t)dr,&g_ata_disks[g_ata_disk_count]))++g_ata_disk_count;}}
    g_block_dev_count=0;
    for(ch=0;ch<g_ata_disk_count;++ch){if(g_block_dev_count>=BLOCK_MAX_DEVS)break;
        block_dev_t *b=&g_block_devs[g_block_dev_count];
        b->present=true; b->name=g_ata_disks[ch].is_atapi?"sr":"sd"; b->type="ata";
        b->sector_sz=ATA_SECTOR_SIZE; b->total_sectors=g_ata_disks[ch].sectors|((uint64_t)g_ata_disks[ch].sectors_hi<<32);
        b->ata_index=ch; ++g_block_dev_count;}
}

bool ata_read_sectors(int disk, uint32_t lba, uint8_t count, void *buf) {
    uint16_t io,*ptr=(uint16_t*)buf; uint8_t dsel; int s,i;
    if(disk<0||disk>=g_ata_disk_count||!g_ata_disks[disk].present)return false;
    io=(g_ata_disks[disk].channel==0)?ATA_PRIMARY_IO:ATA_SECONDARY_IO;
    dsel=(uint8_t)(0xE0|(g_ata_disks[disk].drive<<4)|((lba>>24)&0x0F));
    if(!ata_wait_bsy(io,100000))return false;
    c_outb(io+ATA_REG_DRIVE,dsel); ata_400ns(io);
    c_outb(io+ATA_REG_FEATURES,0); c_outb(io+ATA_REG_SECCOUNT,count);
    c_outb(io+ATA_REG_LBA_LO,(uint8_t)(lba&0xFF));
    c_outb(io+ATA_REG_LBA_MID,(uint8_t)((lba>>8)&0xFF));
    c_outb(io+ATA_REG_LBA_HI,(uint8_t)((lba>>16)&0xFF));
    c_outb(io+ATA_REG_COMMAND,ATA_CMD_READ_PIO);
    for(s=0;s<count;++s){if(!ata_wait_drq(io,200000))return false;
        for(i=0;i<256;++i)*ptr++=c_inw(io+ATA_REG_DATA);ata_400ns(io);}
    return true;
}

bool ata_write_sectors(int disk, uint32_t lba, uint8_t count, const void *buf) {
    uint16_t io; const uint16_t*ptr=(const uint16_t*)buf; uint8_t dsel; int s,i;
    if(disk<0||disk>=g_ata_disk_count||!g_ata_disks[disk].present)return false;
    io=(g_ata_disks[disk].channel==0)?ATA_PRIMARY_IO:ATA_SECONDARY_IO;
    dsel=(uint8_t)(0xE0|(g_ata_disks[disk].drive<<4)|((lba>>24)&0x0F));
    if(!ata_wait_bsy(io,100000))return false;
    c_outb(io+ATA_REG_DRIVE,dsel); ata_400ns(io);
    c_outb(io+ATA_REG_FEATURES,0); c_outb(io+ATA_REG_SECCOUNT,count);
    c_outb(io+ATA_REG_LBA_LO,(uint8_t)(lba&0xFF));
    c_outb(io+ATA_REG_LBA_MID,(uint8_t)((lba>>8)&0xFF));
    c_outb(io+ATA_REG_LBA_HI,(uint8_t)((lba>>16)&0xFF));
    c_outb(io+ATA_REG_COMMAND,ATA_CMD_WRITE_PIO);
    for(s=0;s<count;++s){if(!ata_wait_drq(io,200000))return false;
        for(i=0;i<256;++i)c_outw(io+ATA_REG_DATA,*ptr++);ata_400ns(io);}
    c_outb(io+ATA_REG_COMMAND,ATA_CMD_FLUSH); ata_wait_bsy(io,200000);
    return true;
}

/* AHCI stub */
ahci_port_t g_ahci_ports[AHCI_MAX_PORTS];
int g_ahci_port_count; bool g_ahci_present;

void ahci_init(void) {
    int bus,slot,func; uint32_t bar5=0,pi,i;
    g_ahci_present=false; g_ahci_port_count=0; c_memset(g_ahci_ports,0,sizeof(g_ahci_ports));
    for(bus=0;bus<256&&!g_ahci_present;++bus)for(slot=0;slot<32&&!g_ahci_present;++slot)for(func=0;func<8&&!g_ahci_present;++func){
        uint16_t vid=pci_rd16((uint8_t)bus,(uint8_t)slot,(uint8_t)func,0);
        if(vid==0xFFFF)continue;
        uint32_t cls=pci_rd32((uint8_t)bus,(uint8_t)slot,(uint8_t)func,8);
        if(((cls>>24)&0xFF)==0x01&&((cls>>16)&0xFF)==0x06){
            bar5=pci_rd32((uint8_t)bus,(uint8_t)slot,(uint8_t)func,0x24)&~0xFu;
            g_ahci_present=true;
            uint32_t cmd=pci_rd32((uint8_t)bus,(uint8_t)slot,(uint8_t)func,4);
            pci_wr32((uint8_t)bus,(uint8_t)slot,(uint8_t)func,4,cmd|6);
        }
    }
    if(!g_ahci_present||!bar5)return;
    volatile uint32_t *abar=(volatile uint32_t*)(uintptr_t)bar5;
    pi=abar[0x0C/4];
    for(i=0;i<32&&g_ahci_port_count<AHCI_MAX_PORTS;++i){if(!(pi&(1u<<i)))continue;
        uint32_t pb=0x100+i*0x80;
        uint32_t ssts=abar[pb/4+0x28/4]; uint32_t sig=abar[pb/4+0x24/4];
        ahci_port_t *ap=&g_ahci_ports[g_ahci_port_count]; ap->port=(uint8_t)i;
        if((ssts&0xF)==3&&((ssts>>8)&0xF)==1){ap->present=true;
            if(sig==0x00000101)ap->type=1;else if(sig==0xEB140101)ap->type=2;else ap->type=1;}
        ++g_ahci_port_count;
    }
}

/* NIC drivers (E1000 + RTL8139 detection) */
net_iface_t g_net_ifaces[NET_MAX_IFACES];
int g_net_iface_count;
static volatile uint32_t *g_e1000_mmio;
static uint16_t g_rtl8139_iobase;
static bool g_e1000_found, g_rtl8139_found;

void nic_init(void) {
    int bus,slot,func;
    g_net_iface_count=0; c_memset(g_net_ifaces,0,sizeof(g_net_ifaces));
    g_e1000_found=false; g_rtl8139_found=false; g_e1000_mmio=0; g_rtl8139_iobase=0;
    /* loopback */
    {net_iface_t *lo=&g_net_ifaces[g_net_iface_count++];
     c_strlcpy(lo->name,"lo",16); c_strlcpy(lo->driver,"loopback",16);
     lo->present=true; lo->up=true; lo->ip4=c_make_ip4(127,0,0,1); lo->netmask=c_make_ip4(255,0,0,0);}
    /* PCI scan for NICs */
    for(bus=0;bus<256;++bus)for(slot=0;slot<32;++slot)for(func=0;func<8;++func){
        uint16_t vid=pci_rd16((uint8_t)bus,(uint8_t)slot,(uint8_t)func,0);
        uint16_t did; if(vid==0xFFFF)continue;
        did=pci_rd16((uint8_t)bus,(uint8_t)slot,(uint8_t)func,2);
        /* E1000 */
        if(vid==0x8086&&(did==0x100E||did==0x100F||did==0x10D3)&&!g_e1000_found){
            uint32_t bar0=pci_rd32((uint8_t)bus,(uint8_t)slot,(uint8_t)func,0x10);
            uint32_t cmd=pci_rd32((uint8_t)bus,(uint8_t)slot,(uint8_t)func,4);
            pci_wr32((uint8_t)bus,(uint8_t)slot,(uint8_t)func,4,cmd|6);
            if(!(bar0&1)){g_e1000_mmio=(volatile uint32_t*)(uintptr_t)(bar0&~0xFu);g_e1000_found=true;}
            if(g_net_iface_count<NET_MAX_IFACES){
                net_iface_t *ni=&g_net_ifaces[g_net_iface_count++];
                c_strlcpy(ni->name,"eth0",16); c_strlcpy(ni->driver,"e1000",16);
                ni->present=true; ni->mmio_base=bar0&~0xFu;
                ni->irq=(uint8_t)(pci_rd32((uint8_t)bus,(uint8_t)slot,(uint8_t)func,0x3C)&0xFF);
                if(g_e1000_mmio){uint32_t ral=g_e1000_mmio[0x5400/4],rah=g_e1000_mmio[0x5404/4];
                    ni->mac[0]=(uint8_t)ral;ni->mac[1]=(uint8_t)(ral>>8);ni->mac[2]=(uint8_t)(ral>>16);
                    ni->mac[3]=(uint8_t)(ral>>24);ni->mac[4]=(uint8_t)rah;ni->mac[5]=(uint8_t)(rah>>8);}
            }
        }
        /* RTL8139 */
        if(vid==0x10EC&&did==0x8139&&!g_rtl8139_found){
            uint32_t bar0=pci_rd32((uint8_t)bus,(uint8_t)slot,(uint8_t)func,0x10);
            uint32_t cmd=pci_rd32((uint8_t)bus,(uint8_t)slot,(uint8_t)func,4);
            pci_wr32((uint8_t)bus,(uint8_t)slot,(uint8_t)func,4,cmd|5);
            if(bar0&1){g_rtl8139_iobase=(uint16_t)(bar0&~3u);g_rtl8139_found=true;
                c_outb(g_rtl8139_iobase+0x52,0x00);c_outb(g_rtl8139_iobase+0x37,0x10);
                int w; for(w=0;w<100000&&(c_inb(g_rtl8139_iobase+0x37)&0x10);++w)c_io_wait();
                c_outb(g_rtl8139_iobase+0x37,0x0C);c_outl(g_rtl8139_iobase+0x44,0x0000000F);}
            if(g_net_iface_count<NET_MAX_IFACES){
                net_iface_t *ni=&g_net_ifaces[g_net_iface_count++];
                c_strlcpy(ni->name,"eth0",16); c_strlcpy(ni->driver,"rtl8139",16);
                ni->present=true; ni->mmio_base=bar0;
                ni->irq=(uint8_t)(pci_rd32((uint8_t)bus,(uint8_t)slot,(uint8_t)func,0x3C)&0xFF);
                if(g_rtl8139_iobase){int m;for(m=0;m<6;++m)ni->mac[m]=c_inb(g_rtl8139_iobase+(uint16_t)m);}
            }
        }
    }
    /* E1000 HW init + DMA ring setup */
    if(g_e1000_found&&g_e1000_mmio){
        uint32_t ctrl=g_e1000_mmio[0]; g_e1000_mmio[0]=ctrl|(1u<<26);
        int w; for(w=0;w<100000;++w){c_io_wait();if(!(g_e1000_mmio[0]&(1u<<26)))break;}
        ctrl=g_e1000_mmio[0]; ctrl|=(1u<<5)|(1u<<6); ctrl&=~((1u<<3)|(1u<<31)|(1u<<7));
        g_e1000_mmio[0]=ctrl;
        g_e1000_mmio[0x0100/4]=(1u<<1)|(1u<<15)|(1u<<25)|(1u<<2);
        g_e1000_mmio[0x0400/4]=(1u<<1)|(1u<<3)|(0x40u<<12);

        /* Find the eth0 iface we just registered */
        {int ni;for(ni=0;ni<g_net_iface_count;++ni){
            if(g_net_ifaces[ni].present&&c_strcmp(g_net_ifaces[ni].driver,"e1000")==0){
                e1000_setup_rings(ni);
                break;
            }
        }}
    }
}
void net_iface_up(int i){if(i>=0&&i<g_net_iface_count)g_net_ifaces[i].up=true;}
void net_iface_down(int i){if(i>=0&&i<g_net_iface_count)g_net_ifaces[i].up=false;}

/* E1000 real DMA ring TX/RX */

#define E1000_CTRL   0x0000u
#define E1000_ICR    0x00C0u
#define E1000_IMS    0x00D0u
#define E1000_RCTL   0x0100u
#define E1000_TCTL   0x0400u
#define E1000_TIPG   0x0410u
#define E1000_ITR    0x0C54u
#define E1000_RDBAL  0x2800u
#define E1000_RDBAH  0x2804u
#define E1000_RDLEN  0x2808u
#define E1000_RDH    0x2810u
#define E1000_RDT    0x2818u
#define E1000_TDBAL  0x3800u
#define E1000_TDBAH  0x3804u
#define E1000_TDLEN  0x3808u
#define E1000_TDH    0x3810u
#define E1000_TDT    0x3818u

/* E1000 Legacy TX descriptor (16 bytes) */
typedef struct {
    uint64_t addr;       /* buffer address */
    uint16_t length;     /* data length */
    uint8_t  cso;        /* checksum offset */
    uint8_t  cmd;        /* command field */
    uint8_t  status;     /* status (DD bit) */
    uint8_t  css;        /* checksum start */
    uint16_t special;
} __attribute__((packed)) e1000_tx_desc_t;

/* E1000 Legacy RX descriptor (16 bytes) */
typedef struct {
    uint64_t addr;       /* buffer address */
    uint16_t length;     /* data length */
    uint16_t checksum;   /* packet checksum */
    uint8_t  status;     /* status (DD+EOP bits) */
    uint8_t  errors;
    uint16_t special;
} __attribute__((packed)) e1000_rx_desc_t;

/* Statically allocated DMA-safe buffers (must be 16-byte aligned) */
static e1000_tx_desc_t g_e1000_tx_ring[NET_TX_RING] __attribute__((aligned(16)));
static e1000_rx_desc_t g_e1000_rx_ring[NET_RX_RING] __attribute__((aligned(16)));
static uint8_t g_e1000_tx_bufs[NET_TX_RING][2048] __attribute__((aligned(16)));
static uint8_t g_e1000_rx_bufs[NET_RX_RING][2048] __attribute__((aligned(16)));

static net_iface_t *e1000_iface(void){
    int i;
    for(i=0;i<g_net_iface_count;++i){
        if(g_net_ifaces[i].present&&c_strcmp(g_net_ifaces[i].driver,"e1000")==0)return &g_net_ifaces[i];
    }
    return 0;
}

static void e1000_setup_rings(int iface_idx){
    net_iface_t *ni=&g_net_ifaces[iface_idx];
    volatile uint32_t *mmio=g_e1000_mmio;
    int i;

    /* Apago RX/TX un momento para tocar los rings sin carreras raras. */
    mmio[E1000_TCTL/4] &= ~(1u<<1);
    mmio[E1000_RCTL/4] &= ~(1u<<1);

    /* TX ring */
    c_memset(g_e1000_tx_ring, 0, sizeof(g_e1000_tx_ring));
    for(i=0;i<NET_TX_RING;++i){
        g_e1000_tx_ring[i].addr = (uint64_t)(uintptr_t)g_e1000_tx_bufs[i];
        g_e1000_tx_ring[i].status = (1u<<0); /* DD=1 (free) */
        ni->tx_bufs[i] = g_e1000_tx_bufs[i];
    }
    ni->tx_descs = g_e1000_tx_ring;
    ni->tx_head = 0;
    ni->tx_tail = 0;

    /* Base, largo, head y tail segun el datasheet del e1000. */
    mmio[E1000_TDBAL/4] = (uint32_t)((uint64_t)(uintptr_t)g_e1000_tx_ring & 0xFFFFFFFF);
    mmio[E1000_TDBAH/4] = (uint32_t)((uint64_t)(uintptr_t)g_e1000_tx_ring >> 32);
    mmio[E1000_TDLEN/4] = NET_TX_RING * sizeof(e1000_tx_desc_t);
    mmio[E1000_TDH/4] = 0;
    mmio[E1000_TDT/4] = 0;

    mmio[E1000_TCTL/4] = (1u<<1)|(1u<<3)|(0x10u<<4)|(0x40u<<12);
    mmio[E1000_TIPG/4] = 10u | (8u<<10) | (6u<<20);

    /* RX ring */
    c_memset(g_e1000_rx_ring, 0, sizeof(g_e1000_rx_ring));
    for(i=0;i<NET_RX_RING;++i){
        g_e1000_rx_ring[i].addr = (uint64_t)(uintptr_t)g_e1000_rx_bufs[i];
        g_e1000_rx_ring[i].status = 0;
        ni->rx_bufs[i] = g_e1000_rx_bufs[i];
    }
    ni->rx_descs = g_e1000_rx_ring;
    ni->rx_head = 0;
    ni->rx_tail = NET_RX_RING - 1;
    ni->rx_pending = false;

    mmio[E1000_RDBAL/4] = (uint32_t)((uint64_t)(uintptr_t)g_e1000_rx_ring & 0xFFFFFFFF);
    mmio[E1000_RDBAH/4] = (uint32_t)((uint64_t)(uintptr_t)g_e1000_rx_ring >> 32);
    mmio[E1000_RDLEN/4] = NET_RX_RING * sizeof(e1000_rx_desc_t);
    mmio[E1000_RDH/4] = 0;
    mmio[E1000_RDT/4] = NET_RX_RING - 1;

    mmio[E1000_RCTL/4] = (1u<<1)|(1u<<15)|(1u<<26)|(1u<<3)|(1u<<4);

    mmio[E1000_ITR/4] = 0;

    mmio[E1000_ICR/4] = 0xFFFFFFFF;
    mmio[E1000_IMS/4] = (1u<<0)|(1u<<1)|(1u<<2)|(1u<<7);

    ni->up = true;
}

/* Send a raw Ethernet frame via e1000. Returns bytes sent or -1 on error. */
int e1000_send(const uint8_t *frame, size_t len){
    volatile uint32_t *mmio = g_e1000_mmio;
    e1000_tx_desc_t *ring = g_e1000_tx_ring;
    net_iface_t *ni = e1000_iface();
    uint32_t tail;

    if(!g_e1000_found || !mmio || !ni || len == 0 || len > sizeof(g_e1000_tx_bufs[0])) return -1;

    tail = ni->tx_tail % NET_TX_RING;

    /* Check if the descriptor is free (DD bit set) */
    if(!(ring[tail].status & 0x01)){
        /* Descriptor busy — TX ring full */
        return -1;
    }

    /* Copy frame data into the TX buffer */
    c_memcpy(g_e1000_tx_bufs[tail], frame, len);

    /* Fill descriptor */
    ring[tail].length = (uint16_t)len;
    ring[tail].cso = 0;
    ring[tail].cmd = (1u<<0) | (1u<<1) | (1u<<3); /* EOP + IFCS + RS */
    ring[tail].status = 0; /* clear DD to mark as in-use */

    /* Advance tail */
    uint32_t new_tail = (tail + 1) % NET_TX_RING;
    ni->tx_tail = new_tail;
    mmio[E1000_TDT/4] = new_tail;

    /* Wait for completion (with timeout) */
    {int w; for(w=0; w<1000000; ++w){
        if(ring[tail].status & 0x01) break;
    }}

    ni->tx_packets++;
    ni->tx_bytes += (uint64_t)len;

    return (int)len;
}

/* Receive a raw Ethernet frame from e1000. Returns frame length or 0 if none. */
int e1000_recv(uint8_t *buf, size_t buf_size){
    volatile uint32_t *mmio = g_e1000_mmio;
    e1000_rx_desc_t *ring = g_e1000_rx_ring;
    net_iface_t *ni = e1000_iface();
    uint32_t next;

    if(!g_e1000_found || !mmio || !ni || !buf || buf_size == 0) return 0;

    next = (ni->rx_tail + 1) % NET_RX_RING;

    /* Check if the next descriptor has DD+EOP set (packet ready) */
    if(!(ring[next].status & 0x01) || !(ring[next].status & 0x02)){
        return 0; /* not ready */
    }

    /* Copy packet data */
    size_t pkt_len = ring[next].length;
    if(pkt_len > buf_size) pkt_len = buf_size;
    if(pkt_len > 0) c_memcpy(buf, g_e1000_rx_bufs[next], pkt_len);

    /* Mark descriptor as available again */
    ring[next].status = 0;
    ring[next].addr = (uint64_t)(uintptr_t)g_e1000_rx_bufs[next];

    ni->rx_tail = next;
    mmio[E1000_RDT/4] = next;

    return (int)pkt_len;
}

/* Poll the e1000 for all pending RX packets and feed them into the stack. */
void e1000_poll_rx(void){
    uint8_t frame[2048];
    int len;
    if(!g_e1000_found) return;
    while((len = e1000_recv(frame, sizeof(frame))) > 0){
        /* Feed into ARP cache */
        arp_process(frame, (size_t)len);
        /* Feed into TCP/IP stack */
        net_process_frame(frame, (size_t)len);
        /* Update stats */
        {int ni; for(ni=0;ni<g_net_iface_count;++ni){
            if(g_net_ifaces[ni].present && c_strcmp(g_net_ifaces[ni].driver,"e1000")==0){
                g_net_ifaces[ni].rx_packets++;
                g_net_ifaces[ni].rx_bytes += (uint64_t)len;
                break;
            }
        }}
    }
}

/* Real network frame helpers — build full Eth+IP+TCP/UDP and e1000_send() */
bool net_is_real_external(uint32_t ip){
    if(!ip)return false;
    if((ip&0xFF000000u)==0x7F000000u)return false; /* 127.x.x.x */
    return true;
}

static bool net_get_src_mac(uint8_t *mac){
    int i;
    for(i=0;i<g_net_iface_count;++i){
        if(g_net_ifaces[i].present&&g_net_ifaces[i].up&&c_strcmp(g_net_ifaces[i].driver,"loopback")!=0){
            c_memcpy(mac,g_net_ifaces[i].mac,6);
            return true;
        }
    }
    return false;
}

static uint32_t net_get_gateway_ip(uint32_t dst_ip){
    if(g_dhcp_lease.obtained&&g_dhcp_lease.ip&&g_dhcp_lease.netmask){
        if((dst_ip&g_dhcp_lease.netmask)==(g_dhcp_lease.ip&g_dhcp_lease.netmask))return dst_ip;
    }
    if(g_dhcp_lease.obtained&&g_dhcp_lease.gateway)return g_dhcp_lease.gateway;
    /* VirtualBox NAT default gateway */
    return c_make_ip4(10,0,2,2);
}

static uint32_t net_get_src_ip(void){
    if(g_dhcp_lease.obtained&&g_dhcp_lease.ip)return g_dhcp_lease.ip;
    return c_make_ip4(10,0,2,15);
}

static int net_send_arp_request(uint32_t target_ip){
    uint8_t frame[64];
    uint8_t src_mac[6];
    uint8_t bcast[6];
    int eth_len, arp_len;
    c_memset(bcast,0xFF,sizeof(bcast));
    if(!net_get_src_mac(src_mac))return -1;
    eth_len=net_build_eth(frame,bcast,src_mac,0x0806);
    arp_len=net_build_arp_request(frame+eth_len,src_mac,net_get_src_ip(),target_ip);
    return e1000_send(frame,(size_t)(eth_len+arp_len));
}

static uint16_t net_transport_checksum(uint32_t src_ip,uint32_t dst_ip,uint8_t proto,
                                       const uint8_t *seg,size_t seg_len){
    uint8_t tmp[1536];
    size_t total;
    if(!seg||seg_len+12u>sizeof(tmp))return 0;
    tmp[0]=(uint8_t)(src_ip>>24);tmp[1]=(uint8_t)(src_ip>>16);tmp[2]=(uint8_t)(src_ip>>8);tmp[3]=(uint8_t)src_ip;
    tmp[4]=(uint8_t)(dst_ip>>24);tmp[5]=(uint8_t)(dst_ip>>16);tmp[6]=(uint8_t)(dst_ip>>8);tmp[7]=(uint8_t)dst_ip;
    tmp[8]=0;tmp[9]=proto;tmp[10]=(uint8_t)(seg_len>>8);tmp[11]=(uint8_t)seg_len;
    c_memcpy(tmp+12,seg,seg_len);
    total=12u+seg_len;
    return net_checksum(tmp,total);
}

int net_send_tcp_frame(int sock_fd, const uint8_t *tcp_seg, size_t seg_len){
    uint8_t frame[1600];
    uint8_t src_mac[6], dst_mac[6];
    socket_t *s;
    uint32_t gw_ip, src_ip, dst_ip;
    int eth_len, ip_len;

    if(sock_fd<0||sock_fd>=SOCK_MAX||!g_sockets[sock_fd].used)return -1;
    s=&g_sockets[sock_fd];
    if(!seg_len||seg_len>1480)return -1;
    if(!net_get_src_mac(src_mac))return -1;

    src_ip=s->local_ip?s->local_ip:net_get_src_ip();
    dst_ip=s->remote_ip;
    gw_ip=net_get_gateway_ip(s->remote_ip);
    if(!arp_resolve(gw_ip,dst_mac)){
        (void)net_send_arp_request(gw_ip);
        return -1;
    }

    eth_len=net_build_eth(frame,dst_mac,src_mac,0x0800);
    ip_len=net_build_ip(frame+eth_len,src_ip,dst_ip,IP_PROTO_TCP,(uint16_t)seg_len);
    c_memcpy(frame+eth_len+ip_len,tcp_seg,seg_len);
    if(seg_len>=20){
        uint8_t *tcp=frame+eth_len+ip_len;
        uint16_t ck;
        tcp[16]=0;tcp[17]=0;
        ck=net_transport_checksum(src_ip,dst_ip,IP_PROTO_TCP,tcp,seg_len);
        tcp[16]=(uint8_t)(ck>>8);tcp[17]=(uint8_t)ck;
    }

    return e1000_send(frame,(size_t)(eth_len+ip_len)+(size_t)seg_len);
}

int net_send_udp_frame(int sock_fd, const uint8_t *payload, size_t len){
    uint8_t frame[1600];
    uint8_t src_mac[6], dst_mac[6];
    socket_t *s;
    uint32_t gw_ip;
    int eth_len, ip_len, udp_len;

    if(sock_fd<0||sock_fd>=SOCK_MAX||!g_sockets[sock_fd].used)return -1;
    s=&g_sockets[sock_fd];
    if(!len||len>1400)return -1;
    if(!net_get_src_mac(src_mac))return -1;

    gw_ip=net_get_gateway_ip(s->remote_ip);
    if(!arp_resolve(gw_ip,dst_mac)){
        (void)net_send_arp_request(gw_ip);
        return -1;
    }

    eth_len=net_build_eth(frame,dst_mac,src_mac,0x0800);
    udp_len=8+(int)len; /* UDP header + payload */
    ip_len=net_build_ip(frame+eth_len,s->local_ip?s->local_ip:net_get_src_ip(),
                        s->remote_ip,IP_PROTO_UDP,(uint16_t)udp_len);
    net_build_udp(frame+eth_len+ip_len,s->local_port,s->remote_port,(uint16_t)len);
    c_memcpy(frame+eth_len+ip_len+8,payload,len);

    return e1000_send(frame,(size_t)(eth_len+ip_len+udp_len));
}

/* TCP/IP stack + sockets + ARP + DHCP + DNS */
socket_t g_sockets[SOCK_MAX];
compat_arp_entry_t g_arp_cache[ARP_CACHE_SIZE];
dhcp_lease_t g_dhcp_lease;
dns_entry_t g_dns_cache[DNS_CACHE_SIZE];
static uint16_t g_ephemeral_port=49152;

void arp_init(void){c_memset(g_arp_cache,0,sizeof(g_arp_cache));}
bool arp_resolve(uint32_t ip,uint8_t *mac){int i;
    for(i=0;i<ARP_CACHE_SIZE;++i)if(g_arp_cache[i].valid&&g_arp_cache[i].ip==ip){c_memcpy(mac,g_arp_cache[i].mac,6);return true;}
    c_memset(mac,0xFF,6);return false;}
void arp_process(const uint8_t *f,size_t len){int i,slot=-1;uint32_t sip;const uint8_t *arp;
    if(!f)return;
    if(len>=42&&f[12]==0x08&&f[13]==0x06)arp=f+14;
    else if(len>=28)arp=f;
    else return;
    if(arp[0]!=0||arp[1]!=1||arp[2]!=0x08||arp[3]!=0x00||arp[4]!=6||arp[5]!=4)return;
    sip=((uint32_t)arp[14]<<24)|((uint32_t)arp[15]<<16)|((uint32_t)arp[16]<<8)|arp[17];
    if(!sip)return;
    for(i=0;i<ARP_CACHE_SIZE;++i){
        if(g_arp_cache[i].valid&&g_arp_cache[i].ip==sip){slot=i;break;}
        if(slot<0&&!g_arp_cache[i].valid)slot=i;
    }
    if(slot<0)slot=0;
    g_arp_cache[slot].ip=sip;
    c_memcpy(g_arp_cache[slot].mac,arp+8,6);
    g_arp_cache[slot].valid=true;
}

void tcp_ip_init(void){c_memset(g_sockets,0,sizeof(g_sockets));c_memset(&g_dhcp_lease,0,sizeof(g_dhcp_lease));
    c_memset(g_dns_cache,0,sizeof(g_dns_cache));arp_init();g_ephemeral_port=49152;}

/* Process a raw Ethernet frame from the NIC.
 * Parses EtherType, demuxes IP protocols, delivers to matching sockets. */
void net_process_frame(const uint8_t *frame, size_t len){
    uint16_t ethertype;
    const uint8_t *ip_hdr;
    int ihl, i;
    uint8_t proto;
    uint32_t src_ip, dst_ip;
    uint16_t src_port, dst_port;
    size_t ip_total_len;

    if(!frame || len < 14) return;

    /* EtherType at offset 12 (big-endian) */
    ethertype = ((uint16_t)frame[12] << 8) | frame[13];

    /* ARP */
    if(ethertype == 0x0806){
        arp_process(frame, len);
        return;
    }

    /* Only handle IPv4 */
    if(ethertype != 0x0800) return;

    ip_hdr = frame + 14;
    if(len < 14 + 20) return; /* too short for IP */

    /* Check IP version */
    if((ip_hdr[0] >> 4) != 4) return;

    ihl = (ip_hdr[0] & 0x0F) * 4;
    if(ihl < 20 || (size_t)(14 + ihl) > len) return;
    ip_total_len=((size_t)ip_hdr[2]<<8)|ip_hdr[3];
    if(ip_total_len<(size_t)ihl)return;
    if(14u+ip_total_len>len)ip_total_len=len-14u;

    proto = ip_hdr[9];
    src_ip = ((uint32_t)ip_hdr[12]<<24)|((uint32_t)ip_hdr[13]<<16)|
             ((uint32_t)ip_hdr[14]<<8)|ip_hdr[15];
    dst_ip = ((uint32_t)ip_hdr[16]<<24)|((uint32_t)ip_hdr[17]<<16)|
             ((uint32_t)ip_hdr[18]<<8)|ip_hdr[19];

    /* TCP or UDP */
    if(proto == 6 || proto == 17){
        const uint8_t *tp = ip_hdr + ihl;
        if((size_t)ihl + 4u > ip_total_len) return;
        src_port = ((uint16_t)tp[0]<<8)|tp[1];
        dst_port = ((uint16_t)tp[2]<<8)|tp[3];

        /* Find matching socket and push data */
        for(i = 0; i < SOCK_MAX; ++i){
            socket_t *s = &g_sockets[i];
            if(!s->used) continue;
            if(s->domain != 2) continue;
            if(proto == 6 && s->type != 1) continue;
            if(proto == 17 && s->type != 2) continue;
            if(s->protocol && s->protocol != proto) continue;
            if(!sock_ip_match(s->local_ip, dst_ip)) continue;
            if(s->local_port && s->local_port != dst_port) continue;
            /* Also match remote for connected TCP sockets */
            if(proto == 6 && s->remote_ip && s->remote_ip != src_ip) continue;
            if(proto == 6 && s->remote_port && s->remote_port != src_port) continue;
            /* Found a match; TCP7 decides if the payload is in-order. */
            {
                size_t hdr_size = (proto == 6) ?
                    (((tp[12] >> 4) & 0xF) * 4) : 8; /* TCP hdr len / UDP 8 */
                const uint8_t *payload = tp + hdr_size;
                if((proto==6&&hdr_size<20)||hdr_size<8||(size_t)ihl+hdr_size>ip_total_len)return;
                size_t payload_len = ip_total_len - (size_t)ihl - hdr_size;
                if(proto == 6){
                    int tcp_accept=tcp7_incoming_segment(i, tp, ip_total_len - (size_t)ihl);
                    if(tcp_accept>0&&payload_len>0&&payload_len<=SOCK_BUF_SIZE){
                        (void)sock_push_rx(s, payload, payload_len);
                    }
                }else if(payload_len > 0 && payload_len <= SOCK_BUF_SIZE){
                    size_t pushed=sock_push_rx(s, payload, payload_len);
                    if(pushed==payload_len){
                        uint8_t nq=(uint8_t)((s->rx_msg_head+1u)%SOCK_DGRAMQ);
                        if(nq!=s->rx_msg_tail){
                            s->rx_msg_len[s->rx_msg_head]=(uint16_t)payload_len;
                            s->rx_msg_ip[s->rx_msg_head]=src_ip;
                            s->rx_msg_port[s->rx_msg_head]=src_port;
                            s->rx_msg_head=nq;
                        }
                    }
                }
            }
            break;
        }
    }
    /* ICMP: just acknowledge pings minimally */
    else if(proto == 1){
        /* Could send echo reply here; for now just drop */
    }
}
static uint16_t alloc_eph(void){uint16_t p=g_ephemeral_port++;if(g_ephemeral_port>65500)g_ephemeral_port=49152;return p;}
static bool sock_is_loopback_ip(uint32_t ip){return ip==0||((ip&0xFF000000u)==0x7F000000u);}
static void sock_wake_socket_sleepers(void){
    int ti,fd;
    for(ti=0;ti<TASK_MAX;++ti){
        task_t *t=&g_tasks[ti];
        bool has_wait_fd=false;
        if(!t->used||t->state!=TASK_SLEEPING)continue;
        for(fd=0;fd<TASK_FD_MAX;++fd){
            uint8_t kind=t->fdt.fds[fd].kind;
            if(kind==FDKIND_SOCKET||kind==FDKIND_EPOLL){
                has_wait_fd=true;
                break;
            }
        }
        if(!has_wait_fd)continue;
        task_make_runnable(ti);
    }
}
static bool sock_ip_match(uint32_t bind_ip,uint32_t target_ip){
    if(bind_ip==0||target_ip==0)return true;
    if(bind_ip==target_ip)return true;
    if(sock_is_loopback_ip(bind_ip)&&sock_is_loopback_ip(target_ip))return true;
    return false;
}
static size_t sock_push_rx(socket_t *dst,const uint8_t *src,size_t len){size_t av,i;
    uint32_t used;
    if(!dst||!src)return 0;
    used=dst->rx_head-dst->rx_tail;
    if(used>SOCK_BUF_SIZE)used=SOCK_BUF_SIZE;
    av=SOCK_BUF_SIZE-used;
    if(len>av)len=av;
    for(i=0;i<len;++i){dst->rx_buf[dst->rx_head&(SOCK_BUF_SIZE-1)]=src[i];dst->rx_head++;}
    if(len)sock_wake_socket_sleepers();
    return len;}
static int sock_push_udp(socket_t *dst,const uint8_t *src,size_t len,uint32_t src_ip,uint16_t src_port){
    size_t av;
    uint32_t used;
    uint8_t nq;
    if(!dst||!src)return -EFAULT;
    if(len>65535u)len=65535u;
    used=dst->rx_head-dst->rx_tail;
    if(used>SOCK_BUF_SIZE)used=SOCK_BUF_SIZE;
    av=SOCK_BUF_SIZE-used;
    if(len>av)return -EAGAIN;
    nq=(uint8_t)((dst->rx_msg_head+1u)%SOCK_DGRAMQ);
    if(nq==dst->rx_msg_tail)return -EAGAIN;
    if(sock_push_rx(dst,src,len)!=len)return -EAGAIN;
    dst->rx_msg_len[dst->rx_msg_head]=(uint16_t)len;
    dst->rx_msg_ip[dst->rx_msg_head]=src_ip;
    dst->rx_msg_port[dst->rx_msg_head]=src_port;
    dst->rx_msg_head=nq;
    return (int)len;
}
static int sock_pop_udp(socket_t *s,void *buf,size_t len,int flags){
    uint16_t plen;
    size_t cp,i;
    uint8_t *d=(uint8_t*)buf;
    if(!s||!buf)return -EFAULT;
    if(s->rx_msg_head==s->rx_msg_tail)return (s->non_blocking||(flags&0x40))?-EAGAIN:0;
    plen=s->rx_msg_len[s->rx_msg_tail];
    cp=len<plen?len:plen;
    for(i=0;i<cp;++i){d[i]=s->rx_buf[s->rx_tail&(SOCK_BUF_SIZE-1)];s->rx_tail++;}
    for(;i<plen;++i)s->rx_tail++;
    s->remote_ip=s->rx_msg_ip[s->rx_msg_tail];
    s->remote_port=s->rx_msg_port[s->rx_msg_tail];
    s->rx_msg_tail=(uint8_t)((s->rx_msg_tail+1u)%SOCK_DGRAMQ);
    return (int)cp;
}
static char dns_tolower(char ch){return(ch>='A'&&ch<='Z')?(char)(ch+32):ch;}
static uint16_t dns_be16_rd(const uint8_t *p){return(uint16_t)(((uint16_t)p[0]<<8)|p[1]);}
static void dns_be16_wr(uint8_t *p,uint16_t v){p[0]=(uint8_t)(v>>8);p[1]=(uint8_t)(v&0xFF);}
static void dns_be32_wr(uint8_t *p,uint32_t v){
    p[0]=(uint8_t)(v>>24);p[1]=(uint8_t)(v>>16);p[2]=(uint8_t)(v>>8);p[3]=(uint8_t)v;
}
static int dns_read_qname(const uint8_t *pkt,size_t len,size_t *off,char *host,size_t cap){
    size_t p=*off,out=0;
    int labels=0;
    if(!pkt||!off||!host||cap<2)return -EINVAL;
    while(p<len){
        uint8_t l=pkt[p++];
        if(!l){
            if(!out){host[0]='.';host[1]=0;}
            else host[out]=0;
            *off=p;
            return 0;
        }
        if(l&0xC0u)return -EINVAL;
        if(p+l>len)return -EINVAL;
        if(labels){
            if(out+1>=cap)return -ENOSPC;
            host[out++]='.';
        }
        while(l--){
            if(out+1>=cap)return -ENOSPC;
            host[out++]=dns_tolower((char)pkt[p++]);
        }
        ++labels;
    }
    return -EINVAL;
}
static size_t dns_build_response(const uint8_t *req,size_t req_len,uint8_t *out,size_t out_cap){
    char host[256];
    uint32_t ip=0;
    size_t qoff=12,qend,wo;
    uint16_t qflags,qtype,qclass,rcode=0;
    bool host_found=false,answer=false;
    if(!req||!out||req_len<12||out_cap<12)return 0;
    if(dns_be16_rd(req+4)==0)return 0;
    if(dns_read_qname(req,req_len,&qoff,host,sizeof(host))<0)return 0;
    if(qoff+4>req_len)return 0;
    qtype=dns_be16_rd(req+qoff);
    qclass=dns_be16_rd(req+qoff+2);
    qend=qoff+4;
    host_found=dns_resolve(host,&ip);
    if(host_found&&(qclass==1)&&(qtype==1||qtype==255))answer=true;
    if(!host_found)rcode=3;
    qflags=dns_be16_rd(req+2);
    dns_be16_wr(out+0,dns_be16_rd(req+0));
    dns_be16_wr(out+2,(uint16_t)(0x8000u|(qflags&0x0100u)|0x0080u|rcode));
    dns_be16_wr(out+4,1);
    dns_be16_wr(out+6,answer?1:0);
    dns_be16_wr(out+8,0);
    dns_be16_wr(out+10,0);
    if(12+(qend-12)>out_cap)return 0;
    c_memcpy(out+12,req+12,qend-12);
    wo=qend;
    if(answer){
        if(wo+16>out_cap)return 0;
        out[wo+0]=0xC0;out[wo+1]=0x0C; /* pointer to QNAME */
        dns_be16_wr(out+wo+2,1);  /* TYPE A */
        dns_be16_wr(out+wo+4,1);  /* CLASS IN */
        dns_be32_wr(out+wo+6,300);/* TTL */
        dns_be16_wr(out+wo+10,4); /* RDLEN */
        dns_be32_wr(out+wo+12,ip);
        wo+=16;
    }
    return wo;
}
static int sock_dns_reply(socket_t *s,const uint8_t *data,size_t len){
    uint8_t resp[512];
    size_t out_len;
    uint32_t sip;
    if(!s||!data)return -EFAULT;
    out_len=dns_build_response(data,len,resp,sizeof(resp));
    if(!out_len)return (int)len;
    sip=s->remote_ip?s->remote_ip:(g_dhcp_lease.dns?g_dhcp_lease.dns:c_make_ip4(10,0,2,3));
    return sock_push_udp(s,resp,out_len,sip,53);
}
static int sock_acceptq_len(const socket_t *s){
    if(!s)return 0;
    return (int)((s->accept_head+SOCK_ACCEPTQ-s->accept_tail)%SOCK_ACCEPTQ);
}
static int sock_acceptq_push(socket_t *listener,int sfd){
    uint8_t n;
    if(!listener)return -EINVAL;
    if(listener->backlog>0&&sock_acceptq_len(listener)>=listener->backlog)return -EAGAIN;
    n=(uint8_t)((listener->accept_head+1u)%SOCK_ACCEPTQ);
    if(n==listener->accept_tail)return -EAGAIN;
    listener->accept_q[listener->accept_head]=sfd;
    listener->accept_head=n;
    sock_wake_socket_sleepers();
    return 0;
}
static int sock_acceptq_pop(socket_t *listener){
    int sfd;
    if(!listener)return -EINVAL;
    if(listener->accept_head==listener->accept_tail)return -EAGAIN;
    sfd=listener->accept_q[listener->accept_tail];
    listener->accept_tail=(uint8_t)((listener->accept_tail+1u)%SOCK_ACCEPTQ);
    return sfd;
}
static int sock_find_listener(int domain,uint32_t addr,uint16_t port){
    int i;
    for(i=0;i<SOCK_MAX;++i){
        if(!g_sockets[i].used)continue;
        if(g_sockets[i].domain!=domain||g_sockets[i].type!=1)continue;
        if(g_sockets[i].tcp_state!=TCP_LISTEN)continue;
        if(g_sockets[i].local_port!=port)continue;
        if(!sock_ip_match(g_sockets[i].local_ip,addr))continue;
        return i;
    }
    return -1;
}
static int sock_find_udp_bound(int domain,int except_fd,uint32_t addr,uint16_t port){
    int i;
    for(i=0;i<SOCK_MAX;++i){
        if(i==except_fd)continue;
        if(!g_sockets[i].used)continue;
        if(g_sockets[i].domain!=domain||g_sockets[i].type!=2)continue;
        if(g_sockets[i].local_port!=port)continue;
        if(!sock_ip_match(g_sockets[i].local_ip,addr))continue;
        return i;
    }
    return -1;
}
static bool sock_bind_conflict(int fd,int domain,int type,uint32_t addr,uint16_t port){
    int i;
    if(!port)return false;
    for(i=0;i<SOCK_MAX;++i){
        if(i==fd||!g_sockets[i].used)continue;
        if(g_sockets[i].domain!=domain||g_sockets[i].type!=type)continue;
        if(g_sockets[i].local_port!=port)continue;
        if(sock_ip_match(g_sockets[i].local_ip,addr))return true;
    }
    return false;
}
/* virt_flags bit layout inside socket_t */
#define SOCK_VFLAG_HTTP_TUNNEL 0x0001u
#define SOCK_VFLAG_DBUS_AUTH   0x0002u
#define SOCK_VFLAG_DBUS_UNIXFD 0x0004u
#define SOCK_VFLAG_X11_ATTACHED 0x0008u
#define SOCK_VFLAG_WL_ATTACHED  0x0010u
#define SOCK_VFLAG_DBUS_WAIT_DATA 0x0020u

#define DBUSV_MSG_METHOD_CALL   1u
#define DBUSV_MSG_METHOD_RETURN 2u
#define DBUSV_MSG_ERROR         3u
#define DBUSV_MSG_SIGNAL        4u

#define DBUSV_NAME_MAX      128
#define DBUSV_BIND_MAX      128
#define DBUSV_MATCH_MAX     128
#define DBUSV_UNIQUE_MAX     32
#define DBUSV_FIELD_PATH      1u
#define DBUSV_FIELD_IFACE     2u
#define DBUSV_FIELD_MEMBER    3u
#define DBUSV_FIELD_ERRNAME   4u
#define DBUSV_FIELD_REPLYSER  5u
#define DBUSV_FIELD_DEST      6u
#define DBUSV_FIELD_SENDER    7u
#define DBUSV_FIELD_SIG       8u

typedef struct {
    bool used;
    int  owner_fd; /* -1 means virtual bus-owned service */
    char name[DBUSV_NAME_MAX];
} dbusv_binding_t;

typedef struct {
    bool used;
    int  owner_fd;
    char rule[192];
} dbusv_match_t;

typedef struct {
    bool     le;
    uint8_t  type;
    uint8_t  flags;
    uint8_t  version;
    uint32_t serial;
    uint32_t reply_serial;
    uint32_t body_len;
    size_t   body_off;
    size_t   msg_len;
    const uint8_t *body;
    size_t   body_size;
    char     path[160];
    char     iface[160];
    char     member[160];
    char     dest[160];
    char     sender[160];
    char     err_name[160];
    char     signature[48];
} dbusv_msg_t;

typedef struct {
    uint8_t *buf;
    size_t   cap;
    size_t   pos;
    bool     le;
    bool     ok;
} dbusv_writer_t;

static dbusv_binding_t g_dbusv_bindings[DBUSV_BIND_MAX];
static dbusv_match_t   g_dbusv_matches[DBUSV_MATCH_MAX];
static char            g_dbusv_unique[SOCK_MAX][DBUSV_UNIQUE_MAX];
static bool            g_dbusv_hello[SOCK_MAX];
static bool            g_dbusv_seeded=false;
static uint32_t        g_dbusv_next_unique=1u;
static uint32_t        g_dbusv_next_serial=1u;
static uint8_t         g_dbusv_fields_scratch[2048];
static uint8_t         g_dbusv_packet_scratch[6144];
static uint8_t         g_dbusv_body_scratch[4096];
static const char     *g_dbusv_names_scratch[DBUSV_BIND_MAX+SOCK_MAX];
static char            g_sock_virt_req_snapshot[SOCK_VIRT_REQ_SNAPSHOT_SIZE];
static void dbusv_emit_name_owner_changed(const char *name,const char *old_owner,const char *new_owner);

/* compat7 protocol bridges (wired at runtime). */
extern int x11_attach_socket(int sock_fd);
extern int x11_detach_socket(int sock_fd);
extern int x11_process_socket(int sock_fd);
extern int wl7_attach_socket(int sock_fd);
extern int wl7_process_socket(int sock_fd);
extern int wl7_detach_socket(int sock_fd);

static bool sock_is_http_port(uint16_t port){
    return port==80||port==8080||port==8000||port==3000||port==5000||port==9222;
}
static bool sock_is_https_port(uint16_t port){
    return port==443||port==8443||port==9443;
}
static uint8_t sock_virtual_classify(uint32_t addr,uint16_t port){
    if(sock_is_loopback_ip(addr)){
        if(port>=6000&&port<6064)return SOCK_VIRT_X11;
        if((port>=39010&&port<=39019)||port==39099)return SOCK_VIRT_WL;
        if(port==39020||port==39021||port==39022)return SOCK_VIRT_DBUS;
        return SOCK_VIRT_NONE;
    }
    if(sock_is_http_port(port))return SOCK_VIRT_HTTP;
    if(sock_is_https_port(port))return SOCK_VIRT_HTTPS;
    return SOCK_VIRT_NONE;
}
static size_t sock_tx_snapshot(const socket_t *s,char *out,size_t cap){
    size_t av,i;
    uint32_t t;
    if(!s||!out||cap<2)return 0;
    av=(size_t)(s->tx_head-s->tx_tail);
    if(av>SOCK_BUF_SIZE)av=SOCK_BUF_SIZE;
    if(av>=cap)av=cap-1;
    t=s->tx_tail;
    for(i=0;i<av;++i){out[i]=(char)s->tx_buf[t&(SOCK_BUF_SIZE-1)];++t;}
    out[av]=0;
    return av;
}
static bool sock_http_get_token(const char *req,const char *key,char *out,size_t cap){
    size_t i,kl;
    if(!req||!key||!out||cap<2)return false;
    kl=c_strlen(key);
    for(i=0;req[i];++i){
        if((i==0||req[i-1]=='\n')&&c_strncasecmp(req+i,key,kl)==0){
            size_t j=0,p=i+kl;
            while(req[p]==' '||req[p]=='\t')++p;
            while(req[p]&&req[p]!='\r'&&req[p]!='\n'){
                if(j+1<cap)out[j++]=req[p];
                ++p;
            }
            out[j]=0;
            return true;
        }
    }
    return false;
}
static bool sock_http_parse_req_line(const char *req,char *method,size_t mcap,char *path,size_t pcap){
    size_t i=0,j=0,k=0;
    if(!req||!method||!path||mcap<2||pcap<2)return false;
    method[0]=0;path[0]=0;
    while(req[i]&&req[i]!=' '&&req[i]!='\r'&&req[i]!='\n'){
        if(j+1<mcap)method[j++]=req[i];
        ++i;
    }
    method[j]=0;
    if(req[i]!=' ')return false;
    ++i;
    while(req[i]&&req[i]!=' '&&req[i]!='\r'&&req[i]!='\n'){
        if(k+1<pcap)path[k++]=req[i];
        ++i;
    }
    path[k]=0;
    return method[0]!=0;
}
static int sock_http_queue_response(socket_t *s,const char *req,size_t req_len){
    char host[128],path[192],method[24],body[1024],hdr[1792];
    const char *ctype="text/html; charset=utf-8";
    size_t b=0,h=0;
    const char *status="200 OK";
    bool no_body=false;
    int rc;
    (void)req_len;
    host[0]=0;path[0]=0;method[0]=0;
    if(!sock_http_get_token(req,"Host:",host,sizeof(host)))c_strlcpy(host,"virtual.ridux",sizeof(host));
    if(!sock_http_parse_req_line(req,method,sizeof(method),path,sizeof(path))){
        c_strlcpy(method,"GET",sizeof(method));
        c_strlcpy(path,"/",sizeof(path));
    }
    if(c_strcmp(method,"HEAD")==0)no_body=true;
    if(c_strcmp(path,"/generate_204")==0){status="204 No Content";no_body=true;}
    else if(c_strcmp(path,"/success.txt")==0){
        ctype="text/plain; charset=utf-8";
        c_append_str(body,&b,sizeof(body),"success\n");
    }
    else if(c_strcmp(path,"/favicon.ico")==0){status="404 Not Found";}
    else if(c_strcmp(path,"/healthz")==0||c_strcmp(path,"/readyz")==0){
        ctype="text/plain; charset=utf-8";
        c_append_str(body,&b,sizeof(body),"ok\n");
    }else if(c_strcmp(path,"/json/version")==0){
        ctype="application/json";
        c_append_str(body,&b,sizeof(body),"{\"Browser\":\"Ridux Virtual Chrome\",\"Protocol-Version\":\"1.3\",\"User-Agent\":\"ridux-virt/0.3\",\"WebKit-Version\":\"537.36\"}");
    }else if(c_strcmp(path,"/json/list")==0){
        ctype="application/json";
        c_append_str(body,&b,sizeof(body),"[{\"id\":\"ridux-tab-1\",\"title\":\"Ridux Virtual Tab\",\"url\":\"https://virtual.ridux/\",\"webSocketDebuggerUrl\":\"ws://127.0.0.1:9222/devtools/page/ridux-tab-1\"}]");
    }

    if(!no_body&&b==0){
        c_append_str(body,&b,sizeof(body),"<!doctype html><html><head><title>Ridux Virtual Web</title></head><body>");
        c_append_str(body,&b,sizeof(body),"<h1>RiduxOS Virtual Internet Bridge</h1><p>host=");
        c_append_str(body,&b,sizeof(body),host);
        c_append_str(body,&b,sizeof(body)," path=");
        c_append_str(body,&b,sizeof(body),path);
        c_append_str(body,&b,sizeof(body),"</p><p>HTTP virtual activo (DNS/HTTP listos, TLS real en progreso).</p>");
        c_append_str(body,&b,sizeof(body),"<p>Links de prueba:</p><ul>");
        c_append_str(body,&b,sizeof(body),"<li><a href='http://example.com/'>example.com</a></li>");
        c_append_str(body,&b,sizeof(body),"<li><a href='http://github.com/'>github.com</a></li>");
        c_append_str(body,&b,sizeof(body),"<li><a href='http://google.com/'>google.com</a></li>");
        c_append_str(body,&b,sizeof(body),"<li><a href='http://virtual.ridux/healthz'>virtual.ridux/healthz</a></li>");
        c_append_str(body,&b,sizeof(body),"</ul>");
        c_append_str(body,&b,sizeof(body),"<p>Tip: usa URL http://... para navegar dentro del bridge virtual.</p>");
        c_append_str(body,&b,sizeof(body),"</body></html>");
    }
    c_append_str(hdr,&h,sizeof(hdr),"HTTP/1.1 ");
    c_append_str(hdr,&h,sizeof(hdr),status);
    c_append_str(hdr,&h,sizeof(hdr),"\r\nServer: ridux-virt/0.3\r\nConnection: close\r\nContent-Type: ");
    c_append_str(hdr,&h,sizeof(hdr),ctype);
    c_append_str(hdr,&h,sizeof(hdr),"\r\nContent-Length: ");
    c_append_u32(hdr,&h,sizeof(hdr),(uint32_t)b);
    c_append_str(hdr,&h,sizeof(hdr),"\r\n\r\n");
    if(!no_body)c_append_str(hdr,&h,sizeof(hdr),body);
    rc=(int)sock_push_rx(s,(const uint8_t*)hdr,h);
    if(rc<0)return rc;
    s->virt_state=1;
    s->tcp_state=TCP_CLOSED;
    return 0;
}
static int sock_http_queue_connect_ok(socket_t *s){
    const char rep[]=
        "HTTP/1.1 200 Connection Established\r\n"
        "Proxy-Agent: ridux-virt/0.3\r\n"
        "\r\n";
    if(sock_push_rx(s,(const uint8_t*)rep,sizeof(rep)-1)==0)return -EAGAIN;
    s->virt_flags|=SOCK_VFLAG_HTTP_TUNNEL;
    s->virt_state=2;
    return 0;
}
static int sock_x11_queue_setup_failure(socket_t *s){
    const char reason[]="Ridux X11 bridge pending";
    uint8_t pkt[128];
    uint8_t rlen=(uint8_t)(sizeof(reason)-1);
    uint16_t add=(uint16_t)((rlen+3u)/4u);
    size_t total=8u+(size_t)add*4u;
    if(total>sizeof(pkt))return -E2BIG;
    c_memset(pkt,0,total);
    pkt[0]=0;
    pkt[1]=rlen;
    pkt[2]=11;pkt[3]=0;
    pkt[4]=0;pkt[5]=0;
    pkt[6]=(uint8_t)(add>>8);pkt[7]=(uint8_t)add;
    c_memcpy(pkt+8,reason,rlen);
    if(sock_push_rx(s,pkt,total)==0){
        s->virt_state=1;
        s->tcp_state=TCP_CLOSED;
    }
    return 0;
}
static int sock_dbus_queue_auth(socket_t *s){
    const char rep[]="REJECTED EXTERNAL DBUS_COOKIE_SHA1\r\n";
    if(sock_push_rx(s,(const uint8_t*)rep,sizeof(rep)-1)==0)return -EAGAIN;
    return 0;
}
static int sock_dbus_queue_data(socket_t *s){
    const char rep[]="DATA\r\n";
    if(sock_push_rx(s,(const uint8_t*)rep,sizeof(rep)-1)==0)return -EAGAIN;
    s->virt_flags|=SOCK_VFLAG_DBUS_WAIT_DATA;
    return 0;
}
static int sock_dbus_queue_ok(socket_t *s){
    const char rep[]="OK 72696475786462757300000000000001\r\n";
    if(sock_push_rx(s,(const uint8_t*)rep,sizeof(rep)-1)==0)return -EAGAIN;
    s->virt_flags=(s->virt_flags&~SOCK_VFLAG_DBUS_WAIT_DATA)|SOCK_VFLAG_DBUS_AUTH;
    return 0;
}
static int sock_dbus_queue_unixfd(socket_t *s){
    const char rep[]="AGREE_UNIX_FD\r\n";
    if(sock_push_rx(s,(const uint8_t*)rep,sizeof(rep)-1)==0)return -EAGAIN;
    s->virt_flags|=SOCK_VFLAG_DBUS_UNIXFD;
    return 0;
}
static size_t dbusv_align(size_t v,size_t a){return (v+(a-1u))&~(a-1u);}
static uint32_t dbusv_rd_u32(const uint8_t *p,bool le){
    if(le)return ((uint32_t)p[0])|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);
    return ((uint32_t)p[3])|((uint32_t)p[2]<<8)|((uint32_t)p[1]<<16)|((uint32_t)p[0]<<24);
}
static void dbusv_wr_u32(uint8_t *p,uint32_t v,bool le){
    if(le){p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);p[2]=(uint8_t)(v>>16);p[3]=(uint8_t)(v>>24);return;}
    p[3]=(uint8_t)v;p[2]=(uint8_t)(v>>8);p[1]=(uint8_t)(v>>16);p[0]=(uint8_t)(v>>24);
}
static void dbusv_w_init(dbusv_writer_t *w,uint8_t *buf,size_t cap,bool le){
    if(!w)return;
    w->buf=buf;
    w->cap=cap;
    w->pos=0;
    w->le=le;
    w->ok=(buf&&cap>0);
}
static void dbusv_w_align(dbusv_writer_t *w,size_t a){
    size_t n;
    if(!w||!w->ok||a==0)return;
    n=dbusv_align(w->pos,a);
    if(n>w->cap){w->ok=false;return;}
    while(w->pos<n)w->buf[w->pos++]=0;
}
static void dbusv_w_u8(dbusv_writer_t *w,uint8_t v){
    if(!w||!w->ok)return;
    if(w->pos+1>w->cap){w->ok=false;return;}
    w->buf[w->pos++]=v;
}
static void dbusv_w_u32(dbusv_writer_t *w,uint32_t v){
    if(!w||!w->ok)return;
    if(w->pos+4>w->cap){w->ok=false;return;}
    dbusv_wr_u32(w->buf+w->pos,v,w->le);
    w->pos+=4;
}
static void dbusv_w_u64(dbusv_writer_t *w,uint64_t v){
    int i;
    if(!w||!w->ok)return;
    if(w->pos+8>w->cap){w->ok=false;return;}
    for(i=0;i<8;++i)w->buf[w->pos+(size_t)i]=(uint8_t)(v>>(i*8));
    w->pos+=8;
}
static void dbusv_w_bytes(dbusv_writer_t *w,const void *src,size_t n){
    if(!w||!w->ok||(!src&&n))return;
    if(w->pos+n>w->cap){w->ok=false;return;}
    if(n)c_memcpy(w->buf+w->pos,src,n);
    w->pos+=n;
}
static void dbusv_w_cstr(dbusv_writer_t *w,const char *s,char typ){
    size_t sl=s?c_strlen(s):0;
    dbusv_w_align(w,4);
    dbusv_w_u32(w,(uint32_t)sl);
    if(sl)dbusv_w_bytes(w,s,sl);
    dbusv_w_u8(w,0);
    (void)typ;
}
static void dbusv_w_sig(dbusv_writer_t *w,const char *sig){
    size_t sl=sig?c_strlen(sig):0;
    if(sl>255u)sl=255u;
    dbusv_w_u8(w,(uint8_t)sl);
    if(sl)dbusv_w_bytes(w,sig,sl);
    dbusv_w_u8(w,0);
}
static void dbusv_field_add_u32(dbusv_writer_t *w,uint8_t code,uint32_t v){
    dbusv_w_align(w,8);
    dbusv_w_u8(w,code);
    dbusv_w_sig(w,"u");
    dbusv_w_align(w,4);
    dbusv_w_u32(w,v);
}
static void dbusv_field_add_str(dbusv_writer_t *w,uint8_t code,const char *s,char stype){
    char sig[2];
    sig[0]=stype;
    sig[1]=0;
    dbusv_w_align(w,8);
    dbusv_w_u8(w,code);
    dbusv_w_sig(w,sig);
    dbusv_w_cstr(w,s,stype);
}
static void dbusv_field_add_sig(dbusv_writer_t *w,uint8_t code,const char *s){
    dbusv_w_align(w,8);
    dbusv_w_u8(w,code);
    dbusv_w_sig(w,"g");
    dbusv_w_sig(w,s?s:"");
}
static int dbusv_sock_index(const socket_t *s){
    ptrdiff_t d;
    if(!s)return -1;
    d=s-g_sockets;
    if(d<0||d>=SOCK_MAX)return -1;
    return (int)d;
}
static void dbusv_trace_method(int fd,const dbusv_msg_t *m,const char *phase){
    static uint32_t trace_count=0;
    if(!compat_wayfire_debug_trace_enabled())return;
    if(trace_count>=160||!m)return;
    ++trace_count;
    __boot_serial_puts("[dbus-");
    __boot_serial_puts(phase?phase:"msg");
    __boot_serial_puts("] #");
    __boot_serial_putu32(trace_count);
    __boot_serial_puts(" fd=");
    __boot_serial_putu32(fd<0?0xFFFFFFFFu:(uint32_t)fd);
    __boot_serial_puts(" type=");
    __boot_serial_putu32((uint32_t)m->type);
    __boot_serial_puts(" serial=");
    __boot_serial_putu32(m->serial);
    __boot_serial_puts(" dest=");
    __boot_serial_puts(m->dest[0]?m->dest:"-");
    __boot_serial_puts(" iface=");
    __boot_serial_puts(m->iface[0]?m->iface:"-");
    __boot_serial_puts(" member=");
    __boot_serial_puts(m->member[0]?m->member:"-");
    __boot_serial_puts(" sig=");
    __boot_serial_puts(m->signature[0]?m->signature:"-");
    __boot_serial_puts(" body=");
    __boot_serial_putu32((uint32_t)m->body_size);
    __boot_serial_puts("\n");
}
static void dbusv_trace_stream(socket_t *s,const char *phase,size_t used,size_t consumed){
    static uint32_t trace_count=0;
    if(!compat_wayfire_debug_trace_enabled())return;
    if(trace_count>=96)return;
    ++trace_count;
    __boot_serial_puts("[dbus-stream] #");
    __boot_serial_putu32(trace_count);
    __boot_serial_puts(" fd=");
    __boot_serial_putu32((uint32_t)dbusv_sock_index(s));
    __boot_serial_puts(" phase=");
    __boot_serial_puts(phase?phase:"?");
    __boot_serial_puts(" used=");
    __boot_serial_putu32((uint32_t)used);
    __boot_serial_puts(" consumed=");
    __boot_serial_putu32((uint32_t)consumed);
    __boot_serial_puts(" flags=");
    __boot_serial_puthex64((uint64_t)(s?s->virt_flags:0));
    __boot_serial_puts(" state=");
    __boot_serial_putu32((uint32_t)(s?s->virt_state:0));
    __boot_serial_puts("\n");
}
static void dbusv_seed_defaults(void){
    if(g_dbusv_seeded)return;
    c_memset(g_dbusv_bindings,0,sizeof(g_dbusv_bindings));
    c_memset(g_dbusv_matches,0,sizeof(g_dbusv_matches));
    c_memset(g_dbusv_unique,0,sizeof(g_dbusv_unique));
    c_memset(g_dbusv_hello,0,sizeof(g_dbusv_hello));
    g_dbusv_next_unique=1u;
    g_dbusv_next_serial=1u;
    g_dbusv_seeded=true;
    g_dbusv_bindings[0].used=true;g_dbusv_bindings[0].owner_fd=-1;c_strlcpy(g_dbusv_bindings[0].name,"org.freedesktop.DBus",sizeof(g_dbusv_bindings[0].name));
    g_dbusv_bindings[1].used=true;g_dbusv_bindings[1].owner_fd=-1;c_strlcpy(g_dbusv_bindings[1].name,"org.freedesktop.login1",sizeof(g_dbusv_bindings[1].name));
    g_dbusv_bindings[2].used=true;g_dbusv_bindings[2].owner_fd=-1;c_strlcpy(g_dbusv_bindings[2].name,"org.freedesktop.systemd1",sizeof(g_dbusv_bindings[2].name));
    g_dbusv_bindings[3].used=true;g_dbusv_bindings[3].owner_fd=-1;c_strlcpy(g_dbusv_bindings[3].name,"org.freedesktop.portal.Desktop",sizeof(g_dbusv_bindings[3].name));
    g_dbusv_bindings[4].used=true;g_dbusv_bindings[4].owner_fd=-1;c_strlcpy(g_dbusv_bindings[4].name,"org.freedesktop.portal.Settings",sizeof(g_dbusv_bindings[4].name));
    g_dbusv_bindings[5].used=true;g_dbusv_bindings[5].owner_fd=-1;c_strlcpy(g_dbusv_bindings[5].name,"org.kde.kded6",sizeof(g_dbusv_bindings[5].name));
    g_dbusv_bindings[6].used=true;g_dbusv_bindings[6].owner_fd=-1;c_strlcpy(g_dbusv_bindings[6].name,"org.kde.KWin",sizeof(g_dbusv_bindings[6].name));
    g_dbusv_bindings[7].used=true;g_dbusv_bindings[7].owner_fd=-1;c_strlcpy(g_dbusv_bindings[7].name,"org.kde.plasmashell",sizeof(g_dbusv_bindings[7].name));
    g_dbusv_bindings[8].used=true;g_dbusv_bindings[8].owner_fd=-1;c_strlcpy(g_dbusv_bindings[8].name,"org.kde.ActivityManager",sizeof(g_dbusv_bindings[8].name));
    g_dbusv_bindings[9].used=true;g_dbusv_bindings[9].owner_fd=-1;c_strlcpy(g_dbusv_bindings[9].name,"org.kde.kglobalaccel",sizeof(g_dbusv_bindings[9].name));
    g_dbusv_bindings[10].used=true;g_dbusv_bindings[10].owner_fd=-1;c_strlcpy(g_dbusv_bindings[10].name,"org.kde.StatusNotifierWatcher",sizeof(g_dbusv_bindings[10].name));
    g_dbusv_bindings[11].used=true;g_dbusv_bindings[11].owner_fd=-1;c_strlcpy(g_dbusv_bindings[11].name,"org.freedesktop.PolicyKit1",sizeof(g_dbusv_bindings[11].name));
    g_dbusv_bindings[12].used=true;g_dbusv_bindings[12].owner_fd=-1;c_strlcpy(g_dbusv_bindings[12].name,"org.freedesktop.Accounts",sizeof(g_dbusv_bindings[12].name));
    g_dbusv_bindings[13].used=true;g_dbusv_bindings[13].owner_fd=-1;c_strlcpy(g_dbusv_bindings[13].name,"org.freedesktop.hostname1",sizeof(g_dbusv_bindings[13].name));
    g_dbusv_bindings[14].used=true;g_dbusv_bindings[14].owner_fd=-1;c_strlcpy(g_dbusv_bindings[14].name,"org.freedesktop.locale1",sizeof(g_dbusv_bindings[14].name));
    g_dbusv_bindings[15].used=true;g_dbusv_bindings[15].owner_fd=-1;c_strlcpy(g_dbusv_bindings[15].name,"org.freedesktop.timedate1",sizeof(g_dbusv_bindings[15].name));
    g_dbusv_bindings[16].used=true;g_dbusv_bindings[16].owner_fd=-1;c_strlcpy(g_dbusv_bindings[16].name,"org.freedesktop.RealtimeKit1",sizeof(g_dbusv_bindings[16].name));
}
static int dbusv_find_binding(const char *name){
    int i;
    if(!name||!name[0])return -1;
    dbusv_seed_defaults();
    for(i=0;i<DBUSV_BIND_MAX;++i){
        if(!g_dbusv_bindings[i].used)continue;
        if(c_strcmp(g_dbusv_bindings[i].name,name)==0)return i;
    }
    return -1;
}
static int dbusv_add_binding(const char *name,int owner_fd){
    int i;
    if(!name||!name[0])return -EINVAL;
    i=dbusv_find_binding(name);
    if(i>=0)return i;
    for(i=0;i<DBUSV_BIND_MAX;++i){
        if(g_dbusv_bindings[i].used)continue;
        g_dbusv_bindings[i].used=true;
        g_dbusv_bindings[i].owner_fd=owner_fd;
        c_strlcpy(g_dbusv_bindings[i].name,name,sizeof(g_dbusv_bindings[i].name));
        return i;
    }
    return -ENOMEM;
}
static int dbusv_owner_fd_for_name(const char *name){
    int i;
    if(!name||!name[0])return -2;
    if(c_starts_with(name,":1.")){
        for(i=0;i<SOCK_MAX;++i){
            if(!g_dbusv_hello[i])continue;
            if(c_strcmp(g_dbusv_unique[i],name)==0)return i;
        }
        return -2;
    }
    i=dbusv_find_binding(name);
    if(i<0)return -2;
    return g_dbusv_bindings[i].owner_fd;
}
static bool dbusv_name_has_owner(const char *name){
    return dbusv_owner_fd_for_name(name)>=-1;
}
static const char *dbusv_unique_for_owner(int owner_fd){
    static char bus_name[]="org.freedesktop.DBus";
    if(owner_fd<0)return bus_name;
    if(owner_fd>=SOCK_MAX)return "";
    if(!g_dbusv_unique[owner_fd][0]){
        size_t l=0;
        g_dbusv_unique[owner_fd][0]=0;
        c_append_str(g_dbusv_unique[owner_fd],&l,DBUSV_UNIQUE_MAX,":1.");
        c_append_u32(g_dbusv_unique[owner_fd],&l,DBUSV_UNIQUE_MAX,g_dbusv_next_unique++);
    }
    return g_dbusv_unique[owner_fd];
}
static const char *dbusv_ensure_unique_fd(int fd){
    if(fd<0||fd>=SOCK_MAX)return "";
    if(!g_dbusv_unique[fd][0]){
        size_t l=0;
        g_dbusv_unique[fd][0]=0;
        c_append_str(g_dbusv_unique[fd],&l,DBUSV_UNIQUE_MAX,":1.");
        c_append_u32(g_dbusv_unique[fd],&l,DBUSV_UNIQUE_MAX,g_dbusv_next_unique++);
    }
    return g_dbusv_unique[fd];
}
static void dbusv_drop_socket(int fd){
    int i;
    char old_unique[DBUSV_UNIQUE_MAX];
    if(fd<0||fd>=SOCK_MAX)return;
    dbusv_seed_defaults();
    c_strlcpy(old_unique,dbusv_unique_for_owner(fd),sizeof(old_unique));
    for(i=0;i<DBUSV_BIND_MAX;++i){
        char name[DBUSV_NAME_MAX];
        if(!g_dbusv_bindings[i].used)continue;
        if(g_dbusv_bindings[i].owner_fd!=fd)continue;
        c_strlcpy(name,g_dbusv_bindings[i].name,sizeof(name));
        g_dbusv_bindings[i].used=false;
        g_dbusv_bindings[i].owner_fd=0;
        g_dbusv_bindings[i].name[0]=0;
        dbusv_emit_name_owner_changed(name,old_unique,"");
    }
    for(i=0;i<DBUSV_MATCH_MAX;++i){
        if(!g_dbusv_matches[i].used)continue;
        if(g_dbusv_matches[i].owner_fd!=fd)continue;
        g_dbusv_matches[i].used=false;
        g_dbusv_matches[i].owner_fd=0;
        g_dbusv_matches[i].rule[0]=0;
    }
    if(g_dbusv_hello[fd]&&old_unique[0]){
        dbusv_emit_name_owner_changed(old_unique,old_unique,"");
    }
    g_dbusv_hello[fd]=false;
    g_dbusv_unique[fd][0]=0;
}
static bool dbusv_read_sig(const uint8_t *buf,size_t len,size_t *off,char *out,size_t cap){
    size_t p=*off;
    size_t sl;
    if(p+1>len||!out||cap<2)return false;
    sl=buf[p++];
    if(p+sl+1>len)return false;
    if(sl>=cap)sl=cap-1;
    if(sl)c_memcpy(out,buf+p,sl);
    out[sl]=0;
    p+=(size_t)buf[*off]+1;
    *off=p;
    return true;
}
static bool dbusv_read_cstr(const uint8_t *buf,size_t len,size_t *off,bool le,char *out,size_t cap){
    size_t p=dbusv_align(*off,4);
    uint32_t sl;
    if(!out||cap<2)return false;
    if(p+4>len)return false;
    sl=dbusv_rd_u32(buf+p,le);
    p+=4;
    if(p+(size_t)sl+1>len)return false;
    if(sl>=cap)sl=(uint32_t)(cap-1);
    if(sl)c_memcpy(out,buf+p,sl);
    out[sl]=0;
    p+=(size_t)dbusv_rd_u32(buf+dbusv_align(*off,4),le)+1;
    *off=p;
    return true;
}
static bool dbusv_read_u32(const uint8_t *buf,size_t len,size_t *off,bool le,uint32_t *v){
    size_t p=dbusv_align(*off,4);
    if(p+4>len||!v)return false;
    *v=dbusv_rd_u32(buf+p,le);
    *off=p+4;
    return true;
}
static int dbusv_parse_message(const uint8_t *buf,size_t len,dbusv_msg_t *m,size_t *consumed){
    uint32_t body_len,hdr_len;
    size_t fields_end,body_off,total;
    size_t off;
    if(!buf||!m||!consumed)return -1;
    if(len<16)return 0;
    if(buf[0]!='l'&&buf[0]!='B')return -1;
    c_memset(m,0,sizeof(*m));
    m->le=(buf[0]=='l');
    m->type=buf[1];
    m->flags=buf[2];
    m->version=buf[3];
    body_len=dbusv_rd_u32(buf+4,m->le);
    m->serial=dbusv_rd_u32(buf+8,m->le);
    hdr_len=dbusv_rd_u32(buf+12,m->le);
    fields_end=16u+(size_t)hdr_len;
    body_off=dbusv_align(fields_end,8);
    total=body_off+(size_t)body_len;
    if(fields_end>len||body_off>len)return 0;
    if(total>len)return 0;
    m->body_len=body_len;
    m->body_off=body_off;
    m->msg_len=total;
    off=16;
    while(off<fields_end){
        uint8_t code;
        char vsig[8];
        off=dbusv_align(off,8);
        if(off>=fields_end)break;
        code=buf[off++];
        if(!dbusv_read_sig(buf,fields_end,&off,vsig,sizeof(vsig)))return -1;
        if(vsig[0]=='s'||vsig[0]=='o'){
            char tmp[160];
            if(!dbusv_read_cstr(buf,fields_end,&off,m->le,tmp,sizeof(tmp)))return -1;
            if(code==DBUSV_FIELD_PATH)c_strlcpy(m->path,tmp,sizeof(m->path));
            else if(code==DBUSV_FIELD_IFACE)c_strlcpy(m->iface,tmp,sizeof(m->iface));
            else if(code==DBUSV_FIELD_MEMBER)c_strlcpy(m->member,tmp,sizeof(m->member));
            else if(code==DBUSV_FIELD_DEST)c_strlcpy(m->dest,tmp,sizeof(m->dest));
            else if(code==DBUSV_FIELD_SENDER)c_strlcpy(m->sender,tmp,sizeof(m->sender));
            else if(code==DBUSV_FIELD_ERRNAME)c_strlcpy(m->err_name,tmp,sizeof(m->err_name));
        }else if(vsig[0]=='g'){
            char tmp[48];
            if(!dbusv_read_sig(buf,fields_end,&off,tmp,sizeof(tmp)))return -1;
            if(code==DBUSV_FIELD_SIG)c_strlcpy(m->signature,tmp,sizeof(m->signature));
        }else if(vsig[0]=='u'){
            uint32_t uv=0;
            if(!dbusv_read_u32(buf,fields_end,&off,m->le,&uv))return -1;
            if(code==DBUSV_FIELD_REPLYSER)m->reply_serial=uv;
        }else{
            return -1;
        }
    }
    m->body=buf+body_off;
    m->body_size=body_len;
    *consumed=total;
    return 1;
}
static bool dbusv_body_get_str(const dbusv_msg_t *m,size_t *off,char *out,size_t cap){
    if(!m||!off)return false;
    return dbusv_read_cstr(m->body,m->body_size,off,m->le,out,cap);
}
static bool dbusv_body_get_u32(const dbusv_msg_t *m,size_t *off,uint32_t *v){
    if(!m||!off||!v)return false;
    return dbusv_read_u32(m->body,m->body_size,off,m->le,v);
}
static int dbusv_queue_message(socket_t *dst,uint8_t type,const char *path,const char *iface,const char *member,
                               const char *dest,const char *sender,const char *err_name,uint32_t reply_serial,
                               const char *sig,const uint8_t *body,size_t body_len){
    uint8_t *fields=g_dbusv_fields_scratch;
    uint8_t *pkt=g_dbusv_packet_scratch;
    dbusv_writer_t fw;
    size_t body_off,total;
    uint32_t serial;
    if(!dst)return -EINVAL;
    dbusv_seed_defaults();
    dbusv_w_init(&fw,fields,sizeof(g_dbusv_fields_scratch),true);
    if(path&&path[0])dbusv_field_add_str(&fw,DBUSV_FIELD_PATH,path,'o');
    if(iface&&iface[0])dbusv_field_add_str(&fw,DBUSV_FIELD_IFACE,iface,'s');
    if(member&&member[0])dbusv_field_add_str(&fw,DBUSV_FIELD_MEMBER,member,'s');
    if(err_name&&err_name[0])dbusv_field_add_str(&fw,DBUSV_FIELD_ERRNAME,err_name,'s');
    if(reply_serial)dbusv_field_add_u32(&fw,DBUSV_FIELD_REPLYSER,reply_serial);
    if(dest&&dest[0])dbusv_field_add_str(&fw,DBUSV_FIELD_DEST,dest,'s');
    if(sender&&sender[0])dbusv_field_add_str(&fw,DBUSV_FIELD_SENDER,sender,'s');
    if(sig&&sig[0])dbusv_field_add_sig(&fw,DBUSV_FIELD_SIG,sig);
    if(!fw.ok)return -E2BIG;
    body_off=dbusv_align(16u+fw.pos,8);
    total=body_off+body_len;
    if(total>sizeof(g_dbusv_packet_scratch))return -E2BIG;
    c_memset(pkt,0,total);
    pkt[0]='l';
    pkt[1]=type;
    pkt[2]=0;
    pkt[3]=1;
    dbusv_wr_u32(pkt+4,(uint32_t)body_len,true);
    serial=g_dbusv_next_serial++;
    if(serial==0){serial=g_dbusv_next_serial++;if(serial==0)serial=1;}
    dbusv_wr_u32(pkt+8,serial,true);
    dbusv_wr_u32(pkt+12,(uint32_t)fw.pos,true);
    if(fw.pos)c_memcpy(pkt+16,fields,fw.pos);
    if(body_len&&body)c_memcpy(pkt+body_off,body,body_len);
    if(sock_push_rx(dst,pkt,total)==0)return -EAGAIN;
    return 0;
}
static int dbusv_reply_void(socket_t *dst,const dbusv_msg_t *req,const char *sender){
    return dbusv_queue_message(dst,DBUSV_MSG_METHOD_RETURN,0,0,0,
        req&&req->sender[0]?req->sender:0,sender,0,req?req->serial:0,0,0,0);
}
static int dbusv_reply_u32(socket_t *dst,const dbusv_msg_t *req,uint32_t v,const char *sender){
    uint8_t *body=g_dbusv_body_scratch;
    dbusv_writer_t bw;
    dbusv_w_init(&bw,body,8,true);
    dbusv_w_align(&bw,4);
    dbusv_w_u32(&bw,v);
    if(!bw.ok)return -E2BIG;
    return dbusv_queue_message(dst,DBUSV_MSG_METHOD_RETURN,0,0,0,
        req&&req->sender[0]?req->sender:0,sender,0,req?req->serial:0,"u",body,bw.pos);
}
static int dbusv_reply_bool(socket_t *dst,const dbusv_msg_t *req,bool v,const char *sender){
    return dbusv_reply_u32(dst,req,v?1u:0u,sender);
}
static int dbusv_reply_str(socket_t *dst,const dbusv_msg_t *req,const char *s,const char *sender){
    uint8_t *body=g_dbusv_body_scratch;
    dbusv_writer_t bw;
    dbusv_w_init(&bw,body,1024,true);
    dbusv_w_cstr(&bw,s?s:"",'s');
    if(!bw.ok)return -E2BIG;
    return dbusv_queue_message(dst,DBUSV_MSG_METHOD_RETURN,0,0,0,
        req&&req->sender[0]?req->sender:0,sender,0,req?req->serial:0,"s",body,bw.pos);
}
static int dbusv_reply_objpath(socket_t *dst,const dbusv_msg_t *req,const char *path,const char *sender){
    uint8_t *body=g_dbusv_body_scratch;
    dbusv_writer_t bw;
    dbusv_w_init(&bw,body,1024,true);
    dbusv_w_cstr(&bw,path?path:"/",'o');
    if(!bw.ok)return -E2BIG;
    return dbusv_queue_message(dst,DBUSV_MSG_METHOD_RETURN,0,0,0,
        req&&req->sender[0]?req->sender:0,sender,0,req?req->serial:0,"o",body,bw.pos);
}
static int dbusv_reply_str4(socket_t *dst,const dbusv_msg_t *req,const char *a,const char *b,const char *c,const char *d,const char *sender){
    uint8_t *body=g_dbusv_body_scratch;
    dbusv_writer_t bw;
    dbusv_w_init(&bw,body,2048,true);
    dbusv_w_cstr(&bw,a?a:"",'s');
    dbusv_w_cstr(&bw,b?b:"",'s');
    dbusv_w_cstr(&bw,c?c:"",'s');
    dbusv_w_cstr(&bw,d?d:"",'s');
    if(!bw.ok)return -E2BIG;
    return dbusv_queue_message(dst,DBUSV_MSG_METHOD_RETURN,0,0,0,
        req&&req->sender[0]?req->sender:0,sender,0,req?req->serial:0,"ssss",body,bw.pos);
}
static int dbusv_reply_variant_u32(socket_t *dst,const dbusv_msg_t *req,uint32_t v,const char *sender){
    uint8_t *body=g_dbusv_body_scratch;
    dbusv_writer_t bw;
    dbusv_w_init(&bw,body,64,true);
    dbusv_w_sig(&bw,"u");
    dbusv_w_align(&bw,4);
    dbusv_w_u32(&bw,v);
    if(!bw.ok)return -E2BIG;
    return dbusv_queue_message(dst,DBUSV_MSG_METHOD_RETURN,0,0,0,
        req&&req->sender[0]?req->sender:0,sender,0,req?req->serial:0,"v",body,bw.pos);
}
static int dbusv_reply_variant_i32(socket_t *dst,const dbusv_msg_t *req,int32_t v,const char *sender){
    uint8_t *body=g_dbusv_body_scratch;
    dbusv_writer_t bw;
    dbusv_w_init(&bw,body,64,true);
    dbusv_w_sig(&bw,"i");
    dbusv_w_align(&bw,4);
    dbusv_w_u32(&bw,(uint32_t)v);
    if(!bw.ok)return -E2BIG;
    return dbusv_queue_message(dst,DBUSV_MSG_METHOD_RETURN,0,0,0,
        req&&req->sender[0]?req->sender:0,sender,0,req?req->serial:0,"v",body,bw.pos);
}
static int dbusv_reply_variant_i64(socket_t *dst,const dbusv_msg_t *req,int64_t v,const char *sender){
    uint8_t *body=g_dbusv_body_scratch;
    dbusv_writer_t bw;
    dbusv_w_init(&bw,body,64,true);
    dbusv_w_sig(&bw,"x");
    dbusv_w_align(&bw,8);
    dbusv_w_u64(&bw,(uint64_t)v);
    if(!bw.ok)return -E2BIG;
    return dbusv_queue_message(dst,DBUSV_MSG_METHOD_RETURN,0,0,0,
        req&&req->sender[0]?req->sender:0,sender,0,req?req->serial:0,"v",body,bw.pos);
}
static int dbusv_reply_variant_str(socket_t *dst,const dbusv_msg_t *req,const char *s,const char *sender){
    uint8_t *body=g_dbusv_body_scratch;
    dbusv_writer_t bw;
    dbusv_w_init(&bw,body,320,true);
    dbusv_w_sig(&bw,"s");
    dbusv_w_cstr(&bw,s?s:"",'s');
    if(!bw.ok)return -E2BIG;
    return dbusv_queue_message(dst,DBUSV_MSG_METHOD_RETURN,0,0,0,
        req&&req->sender[0]?req->sender:0,sender,0,req?req->serial:0,"v",body,bw.pos);
}
static int dbusv_reply_settings_value_u32(socket_t *dst,const dbusv_msg_t *req,uint32_t v,const char *sender){
    uint8_t *body=g_dbusv_body_scratch;
    dbusv_writer_t bw;
    dbusv_w_init(&bw,body,64,true);
    dbusv_w_sig(&bw,"v");
    dbusv_w_sig(&bw,"u");
    dbusv_w_align(&bw,4);
    dbusv_w_u32(&bw,v);
    if(!bw.ok)return -E2BIG;
    return dbusv_queue_message(dst,DBUSV_MSG_METHOD_RETURN,0,0,0,
        req&&req->sender[0]?req->sender:0,sender,0,req?req->serial:0,"v",body,bw.pos);
}
static int dbusv_reply_settings_value_str(socket_t *dst,const dbusv_msg_t *req,const char *s,const char *sender){
    uint8_t *body=g_dbusv_body_scratch;
    dbusv_writer_t bw;
    dbusv_w_init(&bw,body,320,true);
    dbusv_w_sig(&bw,"v");
    dbusv_w_sig(&bw,"s");
    dbusv_w_cstr(&bw,s?s:"",'s');
    if(!bw.ok)return -E2BIG;
    return dbusv_queue_message(dst,DBUSV_MSG_METHOD_RETURN,0,0,0,
        req&&req->sender[0]?req->sender:0,sender,0,req?req->serial:0,"v",body,bw.pos);
}
static int dbusv_reply_settings_readall(socket_t *dst,const dbusv_msg_t *req,const char *sender){
    uint8_t *body=g_dbusv_body_scratch;
    dbusv_writer_t bw;
    size_t outer_len_pos,outer_start,ns_len_pos,ns_start;
    #define DBUSV_SETTINGS_SV_U32(_key,_value) do{ \
        dbusv_w_align(&bw,8); \
        dbusv_w_cstr(&bw,(_key),'s'); \
        dbusv_w_sig(&bw,"v"); \
        dbusv_w_sig(&bw,"u"); \
        dbusv_w_align(&bw,4); \
        dbusv_w_u32(&bw,(uint32_t)(_value)); \
    }while(0)
    #define DBUSV_SETTINGS_SV_STR(_key,_value) do{ \
        dbusv_w_align(&bw,8); \
        dbusv_w_cstr(&bw,(_key),'s'); \
        dbusv_w_sig(&bw,"v"); \
        dbusv_w_sig(&bw,"s"); \
        dbusv_w_cstr(&bw,(_value),'s'); \
    }while(0)
    dbusv_w_init(&bw,body,sizeof(g_dbusv_body_scratch),true);
    dbusv_w_align(&bw,4);
    outer_len_pos=bw.pos;
    dbusv_w_u32(&bw,0);
    dbusv_w_align(&bw,8);
    outer_start=bw.pos;

    dbusv_w_align(&bw,8);
    dbusv_w_cstr(&bw,"org.freedesktop.appearance",'s');
    dbusv_w_align(&bw,4);
    ns_len_pos=bw.pos;
    dbusv_w_u32(&bw,0);
    dbusv_w_align(&bw,8);
    ns_start=bw.pos;
    DBUSV_SETTINGS_SV_U32("color-scheme",0);
    dbusv_wr_u32(body+ns_len_pos,(uint32_t)(bw.pos-ns_start),true);

    dbusv_w_align(&bw,8);
    dbusv_w_cstr(&bw,"org.freedesktop.desktop.interface",'s');
    dbusv_w_align(&bw,4);
    ns_len_pos=bw.pos;
    dbusv_w_u32(&bw,0);
    dbusv_w_align(&bw,8);
    ns_start=bw.pos;
    DBUSV_SETTINGS_SV_STR("gtk-theme","Adwaita");
    DBUSV_SETTINGS_SV_STR("icon-theme","Adwaita");
    DBUSV_SETTINGS_SV_STR("cursor-theme","Adwaita");
    DBUSV_SETTINGS_SV_U32("cursor-size",28);
    dbusv_wr_u32(body+ns_len_pos,(uint32_t)(bw.pos-ns_start),true);

    if(!bw.ok)return -E2BIG;
    dbusv_wr_u32(body+outer_len_pos,(uint32_t)(bw.pos-outer_start),true);
    #undef DBUSV_SETTINGS_SV_U32
    #undef DBUSV_SETTINGS_SV_STR
    return dbusv_queue_message(dst,DBUSV_MSG_METHOD_RETURN,0,0,0,
        req&&req->sender[0]?req->sender:0,sender,0,req?req->serial:0,"a{sa{sv}}",body,bw.pos);
}
static int dbusv_reply_str_array(socket_t *dst,const dbusv_msg_t *req,const char **arr,int cnt,const char *sender){
    uint8_t *body=g_dbusv_body_scratch;
    dbusv_writer_t bw;
    size_t len_pos,start,i;
    dbusv_w_init(&bw,body,sizeof(g_dbusv_body_scratch),true);
    dbusv_w_align(&bw,4);
    len_pos=bw.pos;
    dbusv_w_u32(&bw,0);
    start=bw.pos;
    for(i=0;i<(size_t)cnt;++i){
        dbusv_w_cstr(&bw,arr[i]?arr[i]:"",'s');
    }
    if(!bw.ok)return -E2BIG;
    dbusv_wr_u32(body+len_pos,(uint32_t)(bw.pos-start),true);
    return dbusv_queue_message(dst,DBUSV_MSG_METHOD_RETURN,0,0,0,
        req&&req->sender[0]?req->sender:0,sender,0,req?req->serial:0,"as",body,bw.pos);
}
static int dbusv_reply_empty_dict_sv(socket_t *dst,const dbusv_msg_t *req,const char *sender){
    uint8_t *body=g_dbusv_body_scratch;
    dbusv_writer_t bw;
    dbusv_w_init(&bw,body,32,true);
    dbusv_w_align(&bw,4);
    dbusv_w_u32(&bw,0);
    dbusv_w_align(&bw,8);
    if(!bw.ok)return -E2BIG;
    return dbusv_queue_message(dst,DBUSV_MSG_METHOD_RETURN,0,0,0,
        req&&req->sender[0]?req->sender:0,sender,0,req?req->serial:0,"a{sv}",body,bw.pos);
}
static void dbusv_w_sv_i32(dbusv_writer_t *bw,const char *key,int32_t v){
    dbusv_w_align(bw,8);
    dbusv_w_cstr(bw,key,'s');
    dbusv_w_sig(bw,"i");
    dbusv_w_align(bw,4);
    dbusv_w_u32(bw,(uint32_t)v);
}
static void dbusv_w_sv_u32(dbusv_writer_t *bw,const char *key,uint32_t v){
    dbusv_w_align(bw,8);
    dbusv_w_cstr(bw,key,'s');
    dbusv_w_sig(bw,"u");
    dbusv_w_align(bw,4);
    dbusv_w_u32(bw,v);
}
static void dbusv_w_sv_i64(dbusv_writer_t *bw,const char *key,int64_t v){
    dbusv_w_align(bw,8);
    dbusv_w_cstr(bw,key,'s');
    dbusv_w_sig(bw,"x");
    dbusv_w_align(bw,8);
    dbusv_w_u64(bw,(uint64_t)v);
}
static bool dbusv_is_realtime_iface(const char *iface){
    return iface&&(
        c_strcmp(iface,"org.freedesktop.portal.Realtime")==0||
        c_strcmp(iface,"org.freedesktop.RealtimeKit1")==0);
}
static bool dbusv_is_realtime_i32_prop(const char *prop){
    return prop&&(
        c_strcmp(prop,"MaxRealtimePriority")==0||
        c_strcmp(prop,"MinNiceLevel")==0);
}
static bool dbusv_is_realtime_i64_prop(const char *prop){
    return prop&&c_strcmp(prop,"RTTimeUSecMax")==0;
}
static int dbusv_reply_realtimekit_getall(socket_t *dst,const dbusv_msg_t *req,const char *sender){
    uint8_t *body=g_dbusv_body_scratch;
    dbusv_writer_t bw;
    size_t len_pos,start;
    dbusv_w_init(&bw,body,sizeof(g_dbusv_body_scratch),true);
    dbusv_w_align(&bw,4);
    len_pos=bw.pos;
    dbusv_w_u32(&bw,0);
    dbusv_w_align(&bw,8);
    start=bw.pos;
    dbusv_w_sv_i32(&bw,"MaxRealtimePriority",20);
    dbusv_w_sv_i32(&bw,"MinNiceLevel",-20);
    dbusv_w_sv_i64(&bw,"RTTimeUSecMax",200000);
    if(!bw.ok)return -E2BIG;
    dbusv_wr_u32(body+len_pos,(uint32_t)(bw.pos-start),true);
    return dbusv_queue_message(dst,DBUSV_MSG_METHOD_RETURN,0,0,0,
        req&&req->sender[0]?req->sender:0,sender,0,req?req->serial:0,"a{sv}",body,bw.pos);
}
static int dbusv_reply_portal_realtime_getall(socket_t *dst,const dbusv_msg_t *req,const char *sender){
    uint8_t *body=g_dbusv_body_scratch;
    dbusv_writer_t bw;
    size_t len_pos,start;
    dbusv_w_init(&bw,body,sizeof(g_dbusv_body_scratch),true);
    dbusv_w_align(&bw,4);
    len_pos=bw.pos;
    dbusv_w_u32(&bw,0);
    dbusv_w_align(&bw,8);
    start=bw.pos;
    dbusv_w_sv_i32(&bw,"MaxRealtimePriority",20);
    dbusv_w_sv_i32(&bw,"MinNiceLevel",-20);
    dbusv_w_sv_i64(&bw,"RTTimeUSecMax",200000);
    dbusv_w_sv_u32(&bw,"version",1);
    if(!bw.ok)return -E2BIG;
    dbusv_wr_u32(body+len_pos,(uint32_t)(bw.pos-start),true);
    return dbusv_queue_message(dst,DBUSV_MSG_METHOD_RETURN,0,0,0,
        req&&req->sender[0]?req->sender:0,sender,0,req?req->serial:0,"a{sv}",body,bw.pos);
}
static int dbusv_reply_error(socket_t *dst,const dbusv_msg_t *req,const char *err_name,const char *msg,const char *sender){
    uint8_t *body=g_dbusv_body_scratch;
    dbusv_writer_t bw;
    dbusv_w_init(&bw,body,1024,true);
    dbusv_w_cstr(&bw,msg?msg:"",'s');
    if(!bw.ok)return -E2BIG;
    return dbusv_queue_message(dst,DBUSV_MSG_ERROR,0,0,0,
        req&&req->sender[0]?req->sender:0,sender,err_name?err_name:"org.freedesktop.DBus.Error.Failed",
        req?req->serial:0,"s",body,bw.pos);
}
static bool dbusv_match_exists(int fd,const char *rule){
    int i;
    for(i=0;i<DBUSV_MATCH_MAX;++i){
        if(!g_dbusv_matches[i].used)continue;
        if(g_dbusv_matches[i].owner_fd!=fd)continue;
        if(c_strcmp(g_dbusv_matches[i].rule,rule)==0)return true;
    }
    return false;
}
static int dbusv_add_match(int fd,const char *rule){
    int i;
    if(!rule||!rule[0])return -EINVAL;
    if(dbusv_match_exists(fd,rule))return 0;
    for(i=0;i<DBUSV_MATCH_MAX;++i){
        if(g_dbusv_matches[i].used)continue;
        g_dbusv_matches[i].used=true;
        g_dbusv_matches[i].owner_fd=fd;
        c_strlcpy(g_dbusv_matches[i].rule,rule,sizeof(g_dbusv_matches[i].rule));
        return 0;
    }
    return -ENOMEM;
}
static void dbusv_remove_match(int fd,const char *rule){
    int i;
    for(i=0;i<DBUSV_MATCH_MAX;++i){
        if(!g_dbusv_matches[i].used)continue;
        if(g_dbusv_matches[i].owner_fd!=fd)continue;
        if(rule&&rule[0]&&c_strcmp(g_dbusv_matches[i].rule,rule)!=0)continue;
        g_dbusv_matches[i].used=false;
        g_dbusv_matches[i].owner_fd=0;
        g_dbusv_matches[i].rule[0]=0;
        if(rule&&rule[0])return;
    }
}
static const char *dbusv_skip_ws(const char *p){
    while(p&&(*p==' '||*p=='\t'||*p=='\r'||*p=='\n'))++p;
    return p;
}
static bool dbusv_rule_get_value(const char *rule,const char *key,char *out,size_t cap){
    const char *p=rule;
    size_t klen=key?c_strlen(key):0;
    if(!rule||!key||!key[0]||!out||cap<2)return false;
    while(p&&*p){
        const char *ks,*ke,*vs,*ve;
        char q=0;
        size_t n;
        p=dbusv_skip_ws(p);
        ks=p;
        while(*p&&*p!='='&&*p!=',')++p;
        ke=p;
        while(ke>ks&&(ke[-1]==' '||ke[-1]=='\t'))--ke;
        if(*p!='='){
            while(*p&&*p!=',')++p;
            if(*p==',')++p;
            continue;
        }
        ++p;
        p=dbusv_skip_ws(p);
        if(*p=='\''||*p=='"'){q=*p;++p;}
        vs=p;
        if(q){
            while(*p&&*p!=q)++p;
            ve=p;
            if(*p==q)++p;
        }else{
            while(*p&&*p!=',')++p;
            ve=p;
            while(ve>vs&&(ve[-1]==' '||ve[-1]=='\t'))--ve;
        }
        n=(size_t)(ve-vs);
        if((size_t)(ke-ks)==klen&&c_strncmp(ks,key,klen)==0){
            if(n>=cap)n=cap-1;
            if(n)c_memcpy(out,vs,n);
            out[n]=0;
            return true;
        }
        while(*p&&*p!=',')++p;
        if(*p==',')++p;
    }
    return false;
}
static bool dbusv_fd_has_matches(int fd){
    int i;
    for(i=0;i<DBUSV_MATCH_MAX;++i){
        if(!g_dbusv_matches[i].used)continue;
        if(g_dbusv_matches[i].owner_fd==fd)return true;
    }
    return false;
}
static bool dbusv_name_owned_by_fd(int fd,const char *name){
    int i;
    if(fd<0||fd>=SOCK_MAX||!name||!name[0])return false;
    if(c_starts_with(name,":1.")){
        return c_strcmp(dbusv_ensure_unique_fd(fd),name)==0;
    }
    for(i=0;i<DBUSV_BIND_MAX;++i){
        if(!g_dbusv_bindings[i].used)continue;
        if(g_dbusv_bindings[i].owner_fd!=fd)continue;
        if(c_strcmp(g_dbusv_bindings[i].name,name)==0)return true;
    }
    return false;
}
static bool dbusv_rule_matches_signal(const char *rule,const char *path,const char *iface,const char *member,
                                      const char *sender,const char *dest,const char *arg0){
    char v[192];
    if(dbusv_rule_get_value(rule,"type",v,sizeof(v))&&c_strcmp(v,"signal")!=0)return false;
    if(dbusv_rule_get_value(rule,"path",v,sizeof(v))&&c_strcmp(v,path?path:"")!=0)return false;
    if(dbusv_rule_get_value(rule,"interface",v,sizeof(v))&&c_strcmp(v,iface?iface:"")!=0)return false;
    if(dbusv_rule_get_value(rule,"member",v,sizeof(v))&&c_strcmp(v,member?member:"")!=0)return false;
    if(dbusv_rule_get_value(rule,"sender",v,sizeof(v))&&c_strcmp(v,sender?sender:"")!=0)return false;
    if(dbusv_rule_get_value(rule,"destination",v,sizeof(v))&&c_strcmp(v,dest?dest:"")!=0)return false;
    if(dbusv_rule_get_value(rule,"arg0",v,sizeof(v))&&c_strcmp(v,arg0?arg0:"")!=0)return false;
    return true;
}
static bool dbusv_should_deliver_signal(int fd,const char *path,const char *iface,const char *member,
                                        const char *sender,const char *dest,const char *arg0){
    int i;
    bool has_match;
    if(fd<0||fd>=SOCK_MAX)return false;
    if(dest&&dest[0]&&!dbusv_name_owned_by_fd(fd,dest))return false;
    has_match=dbusv_fd_has_matches(fd);
    if(!has_match)return (dest&&dest[0])?true:false;
    for(i=0;i<DBUSV_MATCH_MAX;++i){
        if(!g_dbusv_matches[i].used)continue;
        if(g_dbusv_matches[i].owner_fd!=fd)continue;
        if(dbusv_rule_matches_signal(g_dbusv_matches[i].rule,path,iface,member,sender,dest,arg0))return true;
    }
    return false;
}
static int dbusv_emit_signal_to(socket_t *dst,const char *path,const char *iface,const char *member,
                                const char *dest,const char *sender,const char *sig,const uint8_t *body,size_t body_len){
    return dbusv_queue_message(dst,DBUSV_MSG_SIGNAL,path,iface,member,dest,sender,0,0,sig,body,body_len);
}
static void dbusv_emit_signal_matching(const char *path,const char *iface,const char *member,const char *sender,
                                       const char *sig,const uint8_t *body,size_t body_len,const char *arg0,const char *dest){
    int i;
    for(i=0;i<SOCK_MAX;++i){
        socket_t *sk=&g_sockets[i];
        if(!sk->used||sk->virt_service!=SOCK_VIRT_DBUS)continue;
        if((sk->virt_flags&SOCK_VFLAG_DBUS_AUTH)==0||sk->virt_state==0)continue;
        if(!dbusv_should_deliver_signal(i,path,iface,member,sender,dest,arg0))continue;
        (void)dbusv_emit_signal_to(sk,path,iface,member,dest,sender,sig,body,body_len);
    }
}
static void dbusv_emit_name_owner_changed(const char *name,const char *old_owner,const char *new_owner){
    uint8_t *body=g_dbusv_body_scratch;
    dbusv_writer_t bw;
    dbusv_w_init(&bw,body,1024,true);
    dbusv_w_cstr(&bw,name?name:"",'s');
    dbusv_w_cstr(&bw,old_owner?old_owner:"",'s');
    dbusv_w_cstr(&bw,new_owner?new_owner:"",'s');
    if(!bw.ok)return;
    dbusv_emit_signal_matching("/org/freedesktop/DBus","org.freedesktop.DBus","NameOwnerChanged",
        "org.freedesktop.DBus","sss",body,bw.pos,name,0);
}
static void dbusv_emit_name_signal(int target_fd,const char *member,const char *name){
    uint8_t *body=g_dbusv_body_scratch;
    dbusv_writer_t bw;
    const char *dest;
    socket_t *dst;
    if(target_fd<0||target_fd>=SOCK_MAX)return;
    dst=&g_sockets[target_fd];
    if(!dst->used||dst->virt_service!=SOCK_VIRT_DBUS)return;
    if((dst->virt_flags&SOCK_VFLAG_DBUS_AUTH)==0||dst->virt_state==0)return;
    dest=dbusv_ensure_unique_fd(target_fd);
    if(!dest||!dest[0])return;
    dbusv_w_init(&bw,body,320,true);
    dbusv_w_cstr(&bw,name?name:"",'s');
    if(!bw.ok)return;
    (void)dbusv_emit_signal_to(dst,"/org/freedesktop/DBus","org.freedesktop.DBus",member,
        dest,"org.freedesktop.DBus","s",body,bw.pos);
}
static int dbusv_collect_names(const char **out,int maxn){
    int i,n=0,j;
    if(!out||maxn<=0)return 0;
    for(i=0;i<DBUSV_BIND_MAX&&n<maxn;++i){
        bool dup=false;
        if(!g_dbusv_bindings[i].used)continue;
        for(j=0;j<n;++j)if(c_strcmp(out[j],g_dbusv_bindings[i].name)==0){dup=true;break;}
        if(dup)continue;
        out[n++]=g_dbusv_bindings[i].name;
    }
    for(i=0;i<SOCK_MAX&&n<maxn;++i){
        const char *u;
        bool dup=false;
        if(!g_dbusv_hello[i])continue;
        u=dbusv_ensure_unique_fd(i);
        for(j=0;j<n;++j)if(c_strcmp(out[j],u)==0){dup=true;break;}
        if(dup)continue;
        out[n++]=u;
    }
    return n;
}
static bool dbusv_lookup_owner_unique(const char *name,char *out,size_t cap){
    int owner;
    if(!name||!name[0]||!out||cap<2)return false;
    owner=dbusv_owner_fd_for_name(name);
    if(owner<-1)return false;
    c_strlcpy(out,dbusv_unique_for_owner(owner),cap);
    return out[0]!=0;
}
static void dbusv_make_portal_handle_path(int fd,uint32_t serial,char *out,size_t cap){
    size_t l=0;
    if(!out||cap<2)return;
    out[0]=0;
    c_append_str(out,&l,cap,"/org/freedesktop/portal/desktop/request/ridux/");
    c_append_u32(out,&l,cap,(uint32_t)(fd<0?0:fd));
    c_append_ch(out,&l,cap,'/');
    c_append_u32(out,&l,cap,serial?serial:1u);
}
static void dbusv_emit_portal_response(int fd,const char *handle_path){
    uint8_t *body=g_dbusv_body_scratch;
    dbusv_writer_t bw;
    socket_t *dst;
    const char *dest;
    if(fd<0||fd>=SOCK_MAX||!handle_path||!handle_path[0])return;
    dst=&g_sockets[fd];
    if(!dst->used||dst->virt_service!=SOCK_VIRT_DBUS)return;
    if((dst->virt_flags&SOCK_VFLAG_DBUS_AUTH)==0||dst->virt_state==0)return;
    dest=dbusv_ensure_unique_fd(fd);
    dbusv_w_init(&bw,body,32,true);
    dbusv_w_align(&bw,4);
    dbusv_w_u32(&bw,0); /* response=success */
    dbusv_w_align(&bw,4);
    dbusv_w_u32(&bw,0); /* a{sv} empty */
    dbusv_w_align(&bw,8);
    if(!bw.ok)return;
    (void)dbusv_queue_message(dst,DBUSV_MSG_SIGNAL,handle_path,"org.freedesktop.portal.Request","Response",
        dest,"org.freedesktop.portal.Desktop",0,0,"ua{sv}",body,bw.pos);
}
static bool dbusv_portal_method_returns_handle(const char *iface,const char *member){
    if(!iface||!member)return false;
    if(c_starts_with(iface,"org.freedesktop.portal.FileChooser"))return true;
    if(c_starts_with(iface,"org.freedesktop.portal.OpenURI"))return true;
    if(c_starts_with(iface,"org.freedesktop.portal.Background"))return true;
    if(c_starts_with(iface,"org.freedesktop.portal.Inhibit"))return true;
    if(c_starts_with(iface,"org.freedesktop.portal.ScreenCast"))return true;
    if(c_starts_with(iface,"org.freedesktop.portal.RemoteDesktop"))return true;
    if(c_starts_with(iface,"org.freedesktop.portal.Print"))return true;
    if(c_starts_with(iface,"org.freedesktop.portal.Screenshot"))return true;
    if(c_starts_with(iface,"org.freedesktop.portal.Wallpaper"))return true;
    if(c_strcmp(member,"OpenURI")==0||c_strcmp(member,"OpenFile")==0||c_strcmp(member,"OpenDirectory")==0)return true;
    if(c_strcmp(member,"SaveFile")==0||c_strcmp(member,"SaveFiles")==0||c_strcmp(member,"PickColor")==0)return true;
    if(c_strcmp(member,"SelectSources")==0||c_strcmp(member,"Start")==0||c_strcmp(member,"CreateSession")==0)return true;
    return false;
}
static int dbusv_handle_service_method_call(socket_t *s,int fd,const dbusv_msg_t *m){
    const char *iface=m->iface;
    const char *member=m->member;
    const char *dest=m->dest;
    const char *sender_name="org.freedesktop.portal.Desktop";
    bool portal;
    if(!s||!m||!member[0])return 0;
    portal=(iface&&c_starts_with(iface,"org.freedesktop.portal."))||
           (dest&&c_starts_with(dest,"org.freedesktop.portal."));
    if((dest&&c_strcmp(dest,"org.freedesktop.RealtimeKit1")==0)||
       (iface&&c_strcmp(iface,"org.freedesktop.RealtimeKit1")==0)){
        const char *rt_sender="org.freedesktop.RealtimeKit1";
        if(iface&&c_strcmp(iface,"org.freedesktop.DBus.Properties")==0){
            size_t bo=0;
            char prop_iface[128],prop_name[96];
            prop_iface[0]=0;
            prop_name[0]=0;
            if(c_strcmp(member,"GetAll")==0){
                (void)dbusv_body_get_str(m,&bo,prop_iface,sizeof(prop_iface));
                return dbusv_reply_realtimekit_getall(s,m,rt_sender);
            }
            if(c_strcmp(member,"Get")==0){
                (void)dbusv_body_get_str(m,&bo,prop_iface,sizeof(prop_iface));
                (void)dbusv_body_get_str(m,&bo,prop_name,sizeof(prop_name));
                if(c_strcmp(prop_name,"MaxRealtimePriority")==0)
                    return dbusv_reply_variant_i32(s,m,20,rt_sender);
                if(c_strcmp(prop_name,"MinNiceLevel")==0)
                    return dbusv_reply_variant_i32(s,m,-20,rt_sender);
                if(c_strcmp(prop_name,"RTTimeUSecMax")==0)
                    return dbusv_reply_variant_i64(s,m,200000,rt_sender);
                return dbusv_reply_variant_i32(s,m,0,rt_sender);
            }
        }
        if(c_strcmp(member,"Introspect")==0)
            return dbusv_reply_str(s,m,"<node></node>",rt_sender);
        if(c_strcmp(member,"Ping")==0||c_starts_with(member,"MakeThread"))
            return dbusv_reply_void(s,m,rt_sender);
        return dbusv_reply_void(s,m,rt_sender);
    }
    if(iface&&c_strcmp(iface,"org.freedesktop.DBus.Properties")==0&&
       (portal||(dest&&dest[0]))){
        size_t bo=0;
        char prop_iface[128],prop_name[96];
        prop_iface[0]=0;
        prop_name[0]=0;
        if(c_strcmp(member,"GetAll")==0){
            (void)dbusv_body_get_str(m,&bo,prop_iface,sizeof(prop_iface));
            if(c_strcmp(prop_iface,"org.freedesktop.portal.Realtime")==0){
                return dbusv_reply_portal_realtime_getall(s,m,
                    (dest&&dest[0])?dest:"org.freedesktop.portal.Desktop");
            }
            if(c_strcmp(prop_iface,"org.freedesktop.RealtimeKit1")==0){
                return dbusv_reply_realtimekit_getall(s,m,
                    (dest&&dest[0])?dest:"org.freedesktop.RealtimeKit1");
            }
            return dbusv_reply_empty_dict_sv(s,m,
                (dest&&dest[0])?dest:"org.freedesktop.portal.Desktop");
        }
        if(c_strcmp(member,"Get")==0){
            (void)dbusv_body_get_str(m,&bo,prop_iface,sizeof(prop_iface));
            (void)dbusv_body_get_str(m,&bo,prop_name,sizeof(prop_name));
            if(dbusv_is_realtime_iface(prop_iface)){
                const char *prop_sender=(dest&&dest[0])?dest:
                    (c_strcmp(prop_iface,"org.freedesktop.RealtimeKit1")==0?
                     "org.freedesktop.RealtimeKit1":"org.freedesktop.portal.Desktop");
                if(dbusv_is_realtime_i32_prop(prop_name)){
                    return dbusv_reply_variant_i32(s,m,
                        c_strcmp(prop_name,"MinNiceLevel")==0?-20:20,
                        prop_sender);
                }
                if(dbusv_is_realtime_i64_prop(prop_name)){
                    return dbusv_reply_variant_i64(s,m,200000,prop_sender);
                }
                if(c_strcmp(prop_name,"version")==0||
                   c_strcmp(prop_name,"Version")==0){
                    return dbusv_reply_variant_u32(s,m,1,prop_sender);
                }
                return dbusv_reply_variant_i32(s,m,0,prop_sender);
            }
            if(c_strcmp(prop_name,"version")==0||
               c_strcmp(prop_name,"Version")==0){
                return dbusv_reply_variant_u32(s,m,1,
                    (dest&&dest[0])?dest:"org.freedesktop.portal.Desktop");
            }
            return dbusv_reply_variant_str(s,m,"",
                (dest&&dest[0])?dest:"org.freedesktop.portal.Desktop");
        }
    }
    if(portal){
        if(iface&&c_strcmp(iface,"org.freedesktop.portal.Settings")==0){
            if(c_strcmp(member,"Read")==0||c_strcmp(member,"ReadOne")==0){
                size_t bo=0;
                char ns[80],key[80];
                ns[0]=0;key[0]=0;
                (void)dbusv_body_get_str(m,&bo,ns,sizeof(ns));
                (void)dbusv_body_get_str(m,&bo,key,sizeof(key));
                if(c_strcmp(ns,"org.freedesktop.appearance")==0&&c_strcmp(key,"color-scheme")==0){
                    return dbusv_reply_settings_value_u32(s,m,0,sender_name);
                }
                if(c_strcmp(ns,"org.freedesktop.desktop.interface")==0&&c_strcmp(key,"gtk-theme")==0){
                    return dbusv_reply_settings_value_str(s,m,"Adwaita",sender_name);
                }
                return dbusv_reply_settings_value_str(s,m,"",sender_name);
            }
            if(c_strcmp(member,"ReadAll")==0){
                return dbusv_reply_settings_readall(s,m,sender_name);
            }
        }
        if(iface&&c_strcmp(iface,"org.freedesktop.portal.Request")==0&&c_strcmp(member,"Close")==0){
            return dbusv_reply_void(s,m,sender_name);
        }
        if(iface&&c_strcmp(iface,"org.freedesktop.portal.Session")==0&&c_strcmp(member,"Close")==0){
            return dbusv_reply_void(s,m,sender_name);
        }
        if(dbusv_portal_method_returns_handle(iface,member)){
            char handle[192];
            int rc;
            dbusv_make_portal_handle_path(fd,m->serial,handle,sizeof(handle));
            rc=dbusv_reply_objpath(s,m,handle,sender_name);
            dbusv_emit_portal_response(fd,handle);
            return rc;
        }
        if(c_strcmp(member,"GetVersion")==0||c_strcmp(member,"version")==0){
            return dbusv_reply_u32(s,m,1,sender_name);
        }
        return dbusv_reply_void(s,m,sender_name);
    }
    if((iface&&c_strcmp(iface,"org.freedesktop.Notifications")==0)||
       (dest&&c_strcmp(dest,"org.freedesktop.Notifications")==0)){
        if(c_strcmp(member,"GetCapabilities")==0){
            const char *caps[1]={"body"};
            return dbusv_reply_str_array(s,m,caps,1,"org.freedesktop.Notifications");
        }
        if(c_strcmp(member,"Notify")==0){
            return dbusv_reply_u32(s,m,m->serial?m->serial:1u,"org.freedesktop.Notifications");
        }
        if(c_strcmp(member,"CloseNotification")==0){
            return dbusv_reply_void(s,m,"org.freedesktop.Notifications");
        }
        if(c_strcmp(member,"GetServerInformation")==0){
            return dbusv_reply_str4(s,m,"Ridux Notifications","RiduxOS","0.1","1.2","org.freedesktop.Notifications");
        }
        return dbusv_reply_void(s,m,"org.freedesktop.Notifications");
    }
    if((dest&&(
            c_strcmp(dest,"org.freedesktop.login1")==0||
            c_strcmp(dest,"org.freedesktop.systemd1")==0||
            c_strcmp(dest,"org.freedesktop.PolicyKit1")==0||
            c_strcmp(dest,"org.freedesktop.Accounts")==0||
            c_strcmp(dest,"org.freedesktop.hostname1")==0||
            c_strcmp(dest,"org.freedesktop.locale1")==0||
            c_strcmp(dest,"org.freedesktop.timedate1")==0))||
       (iface&&(
            c_starts_with(iface,"org.freedesktop.login1")||
            c_starts_with(iface,"org.freedesktop.systemd1")||
            c_starts_with(iface,"org.freedesktop.PolicyKit1")||
            c_starts_with(iface,"org.freedesktop.Accounts")||
            c_starts_with(iface,"org.freedesktop.hostname1")||
            c_starts_with(iface,"org.freedesktop.locale1")||
            c_starts_with(iface,"org.freedesktop.timedate1")))){
        const char *sys_sender=(dest&&dest[0])?dest:"org.freedesktop.systemd1";
        if(iface&&c_strcmp(iface,"org.freedesktop.DBus.Properties")==0){
            if(c_strcmp(member,"GetAll")==0)return dbusv_reply_empty_dict_sv(s,m,sys_sender);
            if(c_strcmp(member,"Get")==0)return dbusv_reply_variant_str(s,m,"",sys_sender);
        }
        if(c_strcmp(member,"Introspect")==0)return dbusv_reply_str(s,m,"<node></node>",sys_sender);
        if(c_strcmp(member,"Ping")==0)return dbusv_reply_void(s,m,sys_sender);
        if(c_strcmp(member,"GetId")==0||c_strcmp(member,"GetMachineId")==0)
            return dbusv_reply_str(s,m,"ridux-system",sys_sender);
        if(c_strcmp(member,"CanPowerOff")==0||c_strcmp(member,"CanReboot")==0||
           c_strcmp(member,"CanSuspend")==0||c_strcmp(member,"CanHibernate")==0)
            return dbusv_reply_str(s,m,"na",sys_sender);
        return dbusv_reply_void(s,m,sys_sender);
    }
    if((iface&&c_starts_with(iface,"org.kde."))||
       (dest&&c_starts_with(dest,"org.kde."))){
        const char *kde_sender=(dest&&dest[0])?dest:"org.kde.RiduxCompat";
        if(c_strcmp(member,"GetVersion")==0||c_strcmp(member,"version")==0){
            return dbusv_reply_u32(s,m,1,kde_sender);
        }
        if(c_strcmp(member,"GetId")==0||c_strcmp(member,"GetMachineId")==0){
            return dbusv_reply_str(s,m,"ridux-kde-session",kde_sender);
        }
        if(c_strcmp(member,"Introspect")==0){
            return dbusv_reply_str(s,m,"<node></node>",kde_sender);
        }
        if(c_strcmp(member,"isPlatformX11")==0||c_strcmp(member,"isPlatformWayland")==0){
            return dbusv_reply_bool(s,m,c_strcmp(member,"isPlatformX11")==0,kde_sender);
        }
        return dbusv_reply_void(s,m,kde_sender);
    }
    return 0;
}
static int dbusv_handle_method_call(socket_t *s,int fd,const dbusv_msg_t *m){
    const char *iface=m->iface[0]?m->iface:"org.freedesktop.DBus";
    const char *member=m->member;
    const char *sender="org.freedesktop.DBus";
    if(!member[0])return dbusv_reply_error(s,m,"org.freedesktop.DBus.Error.InvalidArgs","missing member",sender);
    if(c_strcmp(iface,"org.freedesktop.DBus.Introspectable")==0&&c_strcmp(member,"Introspect")==0){
        static const char xml[]=
            "<node>"
            "<interface name='org.freedesktop.DBus'>"
            "<method name='Hello'/>"
            "<method name='RequestName'><arg type='s' direction='in'/><arg type='u' direction='in'/><arg type='u' direction='out'/></method>"
            "<method name='ReleaseName'><arg type='s' direction='in'/><arg type='u' direction='out'/></method>"
            "<method name='ListNames'><arg type='as' direction='out'/></method>"
            "<method name='NameHasOwner'><arg type='s' direction='in'/><arg type='b' direction='out'/></method>"
            "<method name='GetNameOwner'><arg type='s' direction='in'/><arg type='s' direction='out'/></method>"
            "<signal name='NameOwnerChanged'><arg type='s'/><arg type='s'/><arg type='s'/></signal>"
            "</interface>"
            "</node>";
        return dbusv_reply_str(s,m,xml,sender);
    }
    if(c_strcmp(iface,"org.freedesktop.DBus.Peer")==0){
        if(c_strcmp(member,"Ping")==0)return dbusv_reply_void(s,m,sender);
        if(c_strcmp(member,"GetMachineId")==0)return dbusv_reply_str(s,m,"ridux-machine-id",sender);
    }
    if(c_strcmp(iface,"org.freedesktop.DBus")!=0){
        int rc=dbusv_handle_service_method_call(s,fd,m);
        if(rc!=0)return rc;
        return dbusv_reply_error(s,m,"org.freedesktop.DBus.Error.UnknownMethod","unknown interface",sender);
    }
    if(c_strcmp(member,"Hello")==0){
        const char *uniq=dbusv_ensure_unique_fd(fd);
        if(g_dbusv_hello[fd])return dbusv_reply_error(s,m,"org.freedesktop.DBus.Error.Failed","Hello already handled",sender);
        g_dbusv_hello[fd]=true;
        (void)dbusv_reply_str(s,m,uniq,sender);
        dbusv_emit_name_owner_changed(uniq,"",uniq);
        dbusv_emit_name_signal(fd,"NameAcquired",uniq);
        return 0;
    }
    if(c_strcmp(member,"RequestName")==0){
        size_t bo=0;
        char name[DBUSV_NAME_MAX];
        uint32_t flags=0;
        uint32_t result=3;
        int owner;
        const char *uniq=dbusv_ensure_unique_fd(fd);
        if(!dbusv_body_get_str(m,&bo,name,sizeof(name)))return dbusv_reply_error(s,m,"org.freedesktop.DBus.Error.InvalidArgs","RequestName expects name",sender);
        (void)dbusv_body_get_u32(m,&bo,&flags);
        (void)flags;
        owner=dbusv_owner_fd_for_name(name);
        if(owner==fd)result=4;
        else if(owner>=-1)result=3;
        else{
            if(dbusv_add_binding(name,fd)>=0){
                result=1;
                dbusv_emit_name_owner_changed(name,"",uniq);
                dbusv_emit_name_signal(fd,"NameAcquired",name);
            }
        }
        return dbusv_reply_u32(s,m,result,sender);
    }
    if(c_strcmp(member,"ReleaseName")==0){
        size_t bo=0;
        char name[DBUSV_NAME_MAX];
        uint32_t result=2;
        int idx;
        if(!dbusv_body_get_str(m,&bo,name,sizeof(name)))return dbusv_reply_error(s,m,"org.freedesktop.DBus.Error.InvalidArgs","ReleaseName expects name",sender);
        idx=dbusv_find_binding(name);
        if(idx>=0&&g_dbusv_bindings[idx].owner_fd==fd){
            const char *old_owner=dbusv_unique_for_owner(fd);
            g_dbusv_bindings[idx].used=false;
            g_dbusv_bindings[idx].name[0]=0;
            g_dbusv_bindings[idx].owner_fd=0;
            result=1;
            dbusv_emit_name_owner_changed(name,old_owner,"");
            dbusv_emit_name_signal(fd,"NameLost",name);
        }else if(idx>=0){
            result=3;
        }
        return dbusv_reply_u32(s,m,result,sender);
    }
    if(c_strcmp(member,"ListNames")==0){
        int n=dbusv_collect_names(g_dbusv_names_scratch,
            (int)(sizeof(g_dbusv_names_scratch)/sizeof(g_dbusv_names_scratch[0])));
        return dbusv_reply_str_array(s,m,g_dbusv_names_scratch,n,sender);
    }
    if(c_strcmp(member,"NameHasOwner")==0){
        size_t bo=0;
        char name[DBUSV_NAME_MAX];
        if(!dbusv_body_get_str(m,&bo,name,sizeof(name)))return dbusv_reply_error(s,m,"org.freedesktop.DBus.Error.InvalidArgs","NameHasOwner expects name",sender);
        return dbusv_reply_bool(s,m,dbusv_name_has_owner(name),sender);
    }
    if(c_strcmp(member,"GetNameOwner")==0){
        size_t bo=0;
        char name[DBUSV_NAME_MAX];
        char owner[DBUSV_UNIQUE_MAX];
        if(!dbusv_body_get_str(m,&bo,name,sizeof(name)))return dbusv_reply_error(s,m,"org.freedesktop.DBus.Error.InvalidArgs","GetNameOwner expects name",sender);
        if(!dbusv_lookup_owner_unique(name,owner,sizeof(owner))){
            return dbusv_reply_error(s,m,"org.freedesktop.DBus.Error.NameHasNoOwner","name has no owner",sender);
        }
        return dbusv_reply_str(s,m,owner,sender);
    }
    if(c_strcmp(member,"AddMatch")==0){
        size_t bo=0;
        char rule[192];
        if(!dbusv_body_get_str(m,&bo,rule,sizeof(rule)))return dbusv_reply_error(s,m,"org.freedesktop.DBus.Error.InvalidArgs","AddMatch expects rule",sender);
        (void)dbusv_add_match(fd,rule);
        return dbusv_reply_void(s,m,sender);
    }
    if(c_strcmp(member,"RemoveMatch")==0){
        size_t bo=0;
        char rule[192];
        if(!dbusv_body_get_str(m,&bo,rule,sizeof(rule)))return dbusv_reply_error(s,m,"org.freedesktop.DBus.Error.InvalidArgs","RemoveMatch expects rule",sender);
        dbusv_remove_match(fd,rule);
        return dbusv_reply_void(s,m,sender);
    }
    if(c_strcmp(member,"GetId")==0){
        return dbusv_reply_str(s,m,"ridux-bus-0",sender);
    }
    if(c_strcmp(member,"StartServiceByName")==0){
        size_t bo=0;
        char name[DBUSV_NAME_MAX];
        uint32_t flags=0,result=2;
        if(!dbusv_body_get_str(m,&bo,name,sizeof(name)))return dbusv_reply_error(s,m,"org.freedesktop.DBus.Error.InvalidArgs","StartServiceByName expects name",sender);
        (void)dbusv_body_get_u32(m,&bo,&flags);
        (void)flags;
        if(dbusv_owner_fd_for_name(name)<-1){
            if(c_starts_with(name,"org.freedesktop.portal.")||
               c_starts_with(name,"org.freedesktop.secrets")||
               c_starts_with(name,"org.kde.")){
                if(dbusv_add_binding(name,-1)>=0)result=1;
            }else{
                result=1;
            }
        }
        return dbusv_reply_u32(s,m,result,sender);
    }
    if(c_strcmp(member,"GetConnectionUnixProcessID")==0){
        size_t bo=0;
        char name[DBUSV_NAME_MAX];
        int owner;
        uint32_t pid=1;
        if(!dbusv_body_get_str(m,&bo,name,sizeof(name)))return dbusv_reply_error(s,m,"org.freedesktop.DBus.Error.InvalidArgs","GetConnectionUnixProcessID expects name",sender);
        owner=dbusv_owner_fd_for_name(name);
        if(owner<-1)return dbusv_reply_error(s,m,"org.freedesktop.DBus.Error.NameHasNoOwner","name has no owner",sender);
        if(owner>=0)pid=(uint32_t)(1000+owner);
        return dbusv_reply_u32(s,m,pid,sender);
    }
    if(c_strcmp(member,"GetConnectionCredentials")==0){
        return dbusv_reply_empty_dict_sv(s,m,sender);
    }
    return dbusv_reply_error(s,m,"org.freedesktop.DBus.Error.UnknownMethod","unknown method",sender);
}
static size_t dbusv_process_binary_stream(socket_t *s,const uint8_t *buf,size_t len){
    size_t off=0;
    int fd=dbusv_sock_index(s);
    if(fd<0||!buf||!len)return 0;
    while(off<len){
        dbusv_msg_t m;
        size_t used=0;
        int pr;
        if(len-off<16)break;
        pr=dbusv_parse_message(buf+off,len-off,&m,&used);
        if(pr==0)break;
        if(pr<0){
            dbusv_trace_stream(s,"parse-error",len,off);
            off=len;
            break;
        }
        dbusv_trace_method(fd,&m,"call");
        if(m.type==DBUSV_MSG_METHOD_CALL){
            (void)dbusv_handle_method_call(s,fd,&m);
        }
        if(!used)break;
        off+=used;
    }
    return off;
}
static bool dbusv_line_starts(const char *buf,size_t pos,size_t end,const char *lit){
    size_t n;
    if(!buf||!lit||end<pos)return false;
    n=c_strlen(lit);
    if(end-pos<n)return false;
    return c_memcmp(buf+pos,lit,n)==0;
}
static bool dbusv_token_is(const char *buf,size_t start,size_t end,const char *lit){
    size_t n;
    if(!buf||!lit||end<start)return false;
    n=c_strlen(lit);
    return (end-start)==n&&c_memcmp(buf+start,lit,n)==0;
}
static int sock_dbus_on_send(socket_t *s,const char *req,size_t used){
    size_t consumed=0;
    size_t pos=0;
    if(!s||!req||!used)return 0;
    dbusv_trace_stream(s,"enter",used,0);
    dbusv_seed_defaults();
    while(pos<used&&req[pos]==0)++pos;
    consumed=pos;
    while(pos<used&&s->virt_state==0){
        size_t line_end=pos;
        while(line_end<used&&req[line_end]!='\n')++line_end;
        if(line_end>=used)break;
        while(pos<line_end&&(req[pos]=='\r'||req[pos]=='\n'||req[pos]==0))++pos;
        if(dbusv_line_starts(req,pos,line_end,"AUTH")){
            size_t mech=pos+4u;
            size_t mech_end;
            size_t data;
            while(mech<line_end&&(req[mech]==' '||req[mech]=='\t'||req[mech]=='\r'))++mech;
            mech_end=mech;
            while(mech_end<line_end&&req[mech_end]!=' '&&req[mech_end]!='\t'&&req[mech_end]!='\r')++mech_end;
            data=mech_end;
            while(data<line_end&&(req[data]==' '||req[data]=='\t'||req[data]=='\r'))++data;
            if((s->virt_flags&SOCK_VFLAG_DBUS_AUTH)==0){
                if(mech>=line_end)(void)sock_dbus_queue_auth(s);
                else if(dbusv_token_is(req,mech,mech_end,"EXTERNAL")&&data>=line_end)(void)sock_dbus_queue_data(s);
                else if(dbusv_token_is(req,mech,mech_end,"EXTERNAL"))(void)sock_dbus_queue_ok(s);
                else (void)sock_dbus_queue_ok(s);
            }
            consumed=line_end+1u;
            pos=consumed;
            dbusv_trace_stream(s,"auth",used,consumed);
            continue;
        }
        if(dbusv_line_starts(req,pos,line_end,"DATA")){
            if((s->virt_flags&SOCK_VFLAG_DBUS_WAIT_DATA)!=0&&(s->virt_flags&SOCK_VFLAG_DBUS_AUTH)==0){
                (void)sock_dbus_queue_ok(s);
            }else if((s->virt_flags&SOCK_VFLAG_DBUS_AUTH)==0){
                (void)sock_dbus_queue_auth(s);
            }
            consumed=line_end+1u;
            pos=consumed;
            dbusv_trace_stream(s,"data",used,consumed);
            continue;
        }
        if(dbusv_line_starts(req,pos,line_end,"NEGOTIATE_UNIX_FD")){
            if((s->virt_flags&SOCK_VFLAG_DBUS_AUTH)!=0)(void)sock_dbus_queue_unixfd(s);
            else (void)sock_dbus_queue_auth(s);
            consumed=line_end+1u;
            pos=consumed;
            dbusv_trace_stream(s,"unixfd",used,consumed);
            continue;
        }
        if(dbusv_line_starts(req,pos,line_end,"BEGIN")){
            if((s->virt_flags&SOCK_VFLAG_DBUS_AUTH)==0){
                (void)sock_dbus_queue_auth(s);
            }else{
                s->virt_state=1;
            }
            consumed=line_end+1u;
            pos=consumed;
            dbusv_trace_stream(s,"begin",used,consumed);
            break;
        }
        if((s->virt_flags&SOCK_VFLAG_DBUS_AUTH)==0)(void)sock_dbus_queue_auth(s);
        consumed=line_end+1u;
        pos=consumed;
        dbusv_trace_stream(s,"auth-other",used,consumed);
    }
    if(s->virt_state==1&&consumed<used){
        size_t bin_used=dbusv_process_binary_stream(s,(const uint8_t*)req+consumed,used-consumed);
        consumed+=bin_used;
        if(bin_used)dbusv_trace_stream(s,"binary",used,consumed);
    }
    if(consumed>used)consumed=used;
    if(consumed==0)return 0;
    s->tx_tail+=(uint32_t)consumed;
    return 0;
}
static int sock_tls_queue_alert(socket_t *s){
    const uint8_t alert[]={0x15,0x03,0x03,0x00,0x02,0x02,0x28};
    (void)sock_push_rx(s,alert,sizeof(alert));
    s->virt_state=1;
    s->tcp_state=TCP_CLOSED;
    return 0;
}
static int sock_virtual_peer_process(socket_t *client){
    int srvfd;
    int rc;
    int guard;
    if(!client)return -EINVAL;
    srvfd=client->peer;
    if(srvfd<0||srvfd>=SOCK_MAX||!g_sockets[srvfd].used)return -EINVAL;
    switch(client->virt_service){
        case SOCK_VIRT_X11:
            if((client->virt_flags&SOCK_VFLAG_X11_ATTACHED)==0){
                rc=x11_attach_socket(srvfd);
                if(rc<0){
                    (void)sock_x11_queue_setup_failure(client);
                    client->error=ECONNREFUSED;
                    client->tcp_state=TCP_CLOSED;
                    return rc;
                }
                client->virt_flags|=SOCK_VFLAG_X11_ATTACHED;
            }
            for(guard=0;guard<256;++guard){
                rc=x11_process_socket(srvfd);
                if(rc<0)break;
            }
            return 0;
        case SOCK_VIRT_WL:
            if((client->virt_flags&SOCK_VFLAG_WL_ATTACHED)==0){
                rc=wl7_attach_socket(srvfd);
                if(rc<0){
                    client->error=ECONNREFUSED;
                    client->tcp_state=TCP_CLOSED;
                    return rc;
                }
                client->virt_flags|=SOCK_VFLAG_WL_ATTACHED;
            }
            for(guard=0;guard<128;++guard){
                rc=wl7_process_socket(srvfd);
                if(rc<0)break;
            }
            return 0;
        default:
            return 0;
    }
}
static int sock_virtual_stream_on_send(socket_t *s,const uint8_t *buf,size_t len){
    char *req=g_sock_virt_req_snapshot;
    size_t used;
    if(!s||!buf||!len||s->virt_service==SOCK_VIRT_NONE)return 0;
    if((s->virt_service==SOCK_VIRT_HTTP||s->virt_service==SOCK_VIRT_HTTPS)&&s->virt_state==1)return 0;
    switch(s->virt_service){
        case SOCK_VIRT_HTTP:
            if((s->virt_flags&SOCK_VFLAG_HTTP_TUNNEL)&&buf[0]==0x16u){
                return sock_tls_queue_alert(s);
            }
            used=sock_tx_snapshot(s,req,SOCK_VIRT_REQ_SNAPSHOT_SIZE);
            if(used&&c_mem_has((const uint8_t*)req,used,"\r\n\r\n")){
                if(c_starts_with(req,"CONNECT "))return sock_http_queue_connect_ok(s);
                return sock_http_queue_response(s,req,used);
            }
            break;
        case SOCK_VIRT_HTTPS:
            if(buf[0]==0x16u)return sock_tls_queue_alert(s);
            used=sock_tx_snapshot(s,req,SOCK_VIRT_REQ_SNAPSHOT_SIZE);
            if(used&&c_mem_has((const uint8_t*)req,used,"\r\n\r\n"))return sock_http_queue_response(s,req,used);
            break;
        case SOCK_VIRT_X11:
            return sock_x11_queue_setup_failure(s);
        case SOCK_VIRT_WL:
            s->virt_state=1;
            return 0;
        case SOCK_VIRT_DBUS:
            used=sock_tx_snapshot(s,req,SOCK_VIRT_REQ_SNAPSHOT_SIZE);
            return sock_dbus_on_send(s,req,used);
        default:
            break;
    }
    return 0;
}

int sock_create(int domain,int type,int protocol){int i;
    if(domain!=1&&domain!=2&&domain!=16)return -EINVAL;
    if(type==5)type=1;
    if(type==3&&domain==16)type=2;
    if(type!=1&&type!=2)return -EINVAL;
    for(i=0;i<SOCK_MAX;++i)if(!g_sockets[i].used){c_memset(&g_sockets[i],0,sizeof(g_sockets[i]));
        tcp7_drop_socket(i);
        g_sockets[i].used=true;g_sockets[i].domain=domain;g_sockets[i].type=type;
        if(domain==2&&protocol==0)protocol=(type==1)?IP_PROTO_TCP:IP_PROTO_UDP;
        g_sockets[i].protocol=protocol;g_sockets[i].peer=-1;
        g_sockets[i].tcp_state=TCP_CLOSED;g_sockets[i].tcp_window=65535;return i;}
    return -ENOMEM;}
int sock_bind(int fd,uint32_t a,uint16_t p){if(fd<0||fd>=SOCK_MAX||!g_sockets[fd].used)return -EBADF;
    if(sock_bind_conflict(fd,g_sockets[fd].domain,g_sockets[fd].type,a,p))return -EADDRINUSE;
    g_sockets[fd].local_ip=a;g_sockets[fd].local_port=p;return 0;}
int sock_listen(int fd,int bl){if(fd<0||fd>=SOCK_MAX||!g_sockets[fd].used)return -EBADF;
    if(g_sockets[fd].type!=1)return -EINVAL;
    if(bl<1)bl=1;if(bl>SOCK_ACCEPTQ-1)bl=SOCK_ACCEPTQ-1;
    g_sockets[fd].tcp_state=TCP_LISTEN;g_sockets[fd].backlog=bl;
    g_sockets[fd].accept_head=0;g_sockets[fd].accept_tail=0;
    return 0;}
int sock_accept(int fd,uint32_t *a,uint16_t *p){int nsfd;
    if(fd<0||fd>=SOCK_MAX||!g_sockets[fd].used)return -EBADF;
    if(g_sockets[fd].tcp_state!=TCP_LISTEN)return -EINVAL;
    nsfd=sock_acceptq_pop(&g_sockets[fd]);
    if(nsfd<0)return nsfd;
    if(a)*a=g_sockets[nsfd].remote_ip;
    if(p)*p=g_sockets[nsfd].remote_port;
    return nsfd;}
int sock_connect(int fd,uint32_t addr,uint16_t port){int lst,nsfd,rc;socket_t *c,*l,*n;
    if(fd<0||fd>=SOCK_MAX||!g_sockets[fd].used)return -EBADF;
    c=&g_sockets[fd];
    if(c->type==1&&c->tcp_state==TCP_ESTABLISHED)return -EISCONN;
    c->remote_ip=addr;c->remote_port=port;
    c->virt_service=sock_virtual_classify(addr,port);
    c->virt_state=0;
    c->virt_flags=0;
    if(!c->local_port)c->local_port=alloc_eph();
    if(!c->local_ip){
        if(sock_is_loopback_ip(addr))c->local_ip=0x7F000001u;
        else if(g_dhcp_lease.obtained&&g_dhcp_lease.ip)c->local_ip=g_dhcp_lease.ip;
        else c->local_ip=c_make_ip4(10,0,2,15);
    }
    if(c->type==2)return 0;
    if(!sock_is_loopback_ip(addr)){
        /* Real external TCP via TCP7 state machine + E1000 NIC */
        if(c->type==1){
            rc=tcp7_connect(fd,addr,port);
            if(rc<0){
                c->tcp_state=TCP_CLOSED;
                c->error=ECONNREFUSED;
                return rc;
            }
            /* tcp7_connect sets c->tcp_state=TCP_SYN_SENT.
             * Poll E1000 RX to process SYN-ACK and complete handshake. */
            {
                int attempts;
                for(attempts=0;attempts<200;++attempts){
                    e1000_poll_rx();
                    if(c->tcp_state==TCP_ESTABLISHED)break;
                    if((attempts%24)==23)tcp7_tick();
                    {volatile int spin;for(spin=0;spin<50000;++spin)__asm__ volatile("pause");}
                }
            }
            if(c->tcp_state!=TCP_ESTABLISHED){
                /* Connection timed out or refused — keep SYN_SENT for
                 * non-blocking sockets, fail for blocking ones. */
                if(c->non_blocking)return -EINPROGRESS;
                c->error=ETIMEDOUT;
                return -ETIMEDOUT;
            }
            c->error=0;
            return 0;
        }
        /* External UDP — already handled by type==2 return above */
        c->tcp_state=TCP_CLOSED;
        c->error=ECONNREFUSED;
        return -ECONNREFUSED;
    }
    lst=sock_find_listener(c->domain,addr,port);
    if(compat_wayfire_debug_trace_enabled()&&
       (port==39010||port==39020||port==39021||port==39099||(port>=6000&&port<6064))){
        static uint32_t ipc_connect_trace;
        if(ipc_connect_trace<96){
            ++ipc_connect_trace;
            __boot_serial_force_puts("[sock-ipc-connect!] #");
            __boot_serial_force_putu32(ipc_connect_trace);
            __boot_serial_force_puts(" fd=");
            __boot_serial_force_putu32((uint32_t)fd);
            __boot_serial_force_puts(" dom=");
            __boot_serial_force_putu32((uint32_t)c->domain);
            __boot_serial_force_puts(" port=");
            __boot_serial_force_putu32((uint32_t)port);
            __boot_serial_force_puts(" svc=");
            __boot_serial_force_putu32((uint32_t)c->virt_service);
            __boot_serial_force_puts(" lst=");
            __boot_serial_force_putu32(lst<0?0xFFFFFFFFu:(uint32_t)lst);
            if(lst>=0&&lst<SOCK_MAX){
                __boot_serial_force_puts(" lst_state=");
                __boot_serial_force_putu32((uint32_t)g_sockets[lst].tcp_state);
                __boot_serial_force_puts(" lst_q=");
                __boot_serial_force_putu32((uint32_t)sock_acceptq_len(&g_sockets[lst]));
            }
            __boot_serial_force_puts("\n");
        }
    }
    if(lst<0){
        if(c->virt_service==SOCK_VIRT_DBUS){
            dbusv_seed_defaults();
            c->tcp_state=TCP_ESTABLISHED;
            c->tcp_seq=1;c->tcp_ack=1;
            c->tcp_window=65535;
            c->error=0;
            return 0;
        }
        if(c->virt_service==SOCK_VIRT_X11||c->virt_service==SOCK_VIRT_WL){
            nsfd=sock_create(c->domain,1,c->protocol);
            if(nsfd<0){
                c->tcp_state=TCP_CLOSED;
                c->error=ECONNREFUSED;
                return -ECONNREFUSED;
            }
            n=&g_sockets[nsfd];
            n->local_ip=addr;
            n->local_port=port;
            n->remote_ip=c->local_ip?c->local_ip:0x7F000001u;
            n->remote_port=c->local_port;
            n->tcp_state=TCP_ESTABLISHED;
            n->tcp_seq=1;n->tcp_ack=1;
            n->tcp_window=65535;
            n->virt_service=SOCK_VIRT_NONE;
            n->virt_state=0;
            n->virt_flags=0;
            n->peer=fd;
            c->peer=nsfd;
            c->tcp_state=TCP_ESTABLISHED;
            c->tcp_seq=1;c->tcp_ack=1;
            c->tcp_window=65535;
            c->error=0;
            return 0;
        }
        if(c->domain==1){
            c->tcp_state=TCP_CLOSED;
            c->error=ECONNREFUSED;
            return -ECONNREFUSED;
        }
        /* Legacy loopback behavior: allow connect even if no server is bound yet. */
        c->tcp_state=TCP_ESTABLISHED;
        c->tcp_seq=1;c->tcp_ack=1;
        c->tcp_window=65535;
        c->error=0;
        return 0;
    }
    l=&g_sockets[lst];
    if(c->virt_service!=SOCK_VIRT_NONE){
        static uint32_t virt_listener_trace;
        if(compat_wayfire_debug_trace_enabled()&&virt_listener_trace<48){
            ++virt_listener_trace;
            __boot_serial_puts("[sock-virt-real-listener] fd=");
            __boot_serial_putu32((uint32_t)fd);
            __boot_serial_puts(" svc=");
            __boot_serial_putu32((uint32_t)c->virt_service);
            __boot_serial_puts(" port=");
            __boot_serial_putu32((uint32_t)port);
            __boot_serial_puts(" listener=");
            __boot_serial_putu32((uint32_t)lst);
            __boot_serial_puts("\n");
        }
        c->virt_service=SOCK_VIRT_NONE;
        c->virt_state=0;
        c->virt_flags=0;
    }
    nsfd=sock_create(c->domain,1,c->protocol);
    if(nsfd<0)return nsfd;
    n=&g_sockets[nsfd];
    n->local_ip=l->local_ip?l->local_ip:addr;
    n->local_port=l->local_port;
    n->remote_ip=c->local_ip?c->local_ip:0x7F000001u;
    n->remote_port=c->local_port;
    n->tcp_state=TCP_ESTABLISHED;
    n->tcp_seq=1;n->tcp_ack=1;
    n->tcp_window=65535;
    n->virt_service=SOCK_VIRT_NONE;
    n->virt_state=0;
    n->virt_flags=0;
    n->peer=fd;
    c->peer=nsfd;
    c->tcp_state=TCP_ESTABLISHED;
    c->tcp_seq=1;c->tcp_ack=1;
    c->tcp_window=65535;
    rc=sock_acceptq_push(l,nsfd);
    if(rc<0){
        c->peer=-1;
        c->tcp_state=TCP_CLOSED;
        sock_close(nsfd);
        return rc;
    }
    return 0;}
int sock_pair_create(int domain,int type,int protocol,int out_pair[2]){int a,b;uint16_t pa,pb;
    if(!out_pair)return -EFAULT;
    a=sock_create(domain,type,protocol);if(a<0)return a;
    b=sock_create(domain,type,protocol);if(b<0){sock_close(a);return b;}
    pa=alloc_eph();pb=alloc_eph();
    g_sockets[a].peer=b;g_sockets[b].peer=a;
    g_sockets[a].local_ip=0x7F000001u;g_sockets[a].remote_ip=0x7F000001u;
    g_sockets[b].local_ip=0x7F000001u;g_sockets[b].remote_ip=0x7F000001u;
    g_sockets[a].local_port=pa;g_sockets[a].remote_port=pb;
    g_sockets[b].local_port=pb;g_sockets[b].remote_port=pa;
    if(type==1){g_sockets[a].tcp_state=TCP_ESTABLISHED;g_sockets[b].tcp_state=TCP_ESTABLISHED;}
    out_pair[0]=a;out_pair[1]=b;
    return 0;}
static bool sock_pos_after(uint32_t a,uint32_t b){return (int32_t)(a-b)>0;}
int sock_send_right(int fd,int pass_fd){socket_t *s,*p;uint8_t n;
    if(fd<0||fd>=SOCK_MAX||!g_sockets[fd].used)return -EBADF;
    s=&g_sockets[fd];
    if(s->peer<0||s->peer>=SOCK_MAX||!g_sockets[s->peer].used)return -EINVAL;
    p=&g_sockets[s->peer];
    n=(uint8_t)((p->anc_head+1u)%SOCK_ANCQ);
    if(n==p->anc_tail)return -EAGAIN;
    p->anc_fds[p->anc_head]=pass_fd;
    p->anc_pos[p->anc_head]=p->rx_head;
    p->anc_head=n;
    sock_wake_socket_sleepers();
    return 0;}
int sock_recv_right(int fd,int *pass_fd){socket_t *s;
    if(fd<0||fd>=SOCK_MAX||!g_sockets[fd].used)return -EBADF;
    if(!pass_fd)return -EFAULT;
    s=&g_sockets[fd];
    if(s->anc_head==s->anc_tail)return -EAGAIN;
    if(sock_pos_after(s->anc_pos[s->anc_tail],s->rx_tail))return -EAGAIN;
    *pass_fd=s->anc_fds[s->anc_tail];
    s->anc_tail=(uint8_t)((s->anc_tail+1u)%SOCK_ANCQ);
    return 0;}
size_t sock_recv_limit_before_right(int fd,size_t cap){socket_t *s;uint32_t used,dist;
    if(fd<0||fd>=SOCK_MAX||!g_sockets[fd].used)return cap;
    s=&g_sockets[fd];
    if(s->type!=1)return cap;
    if(s->anc_head==s->anc_tail)return cap;
    dist=s->anc_pos[s->anc_tail]-s->rx_tail;
    if((int32_t)dist<=0)return cap;
    used=s->rx_head-s->rx_tail;
    if(used>SOCK_BUF_SIZE)used=SOCK_BUF_SIZE;
    if(dist>used)return used<cap?used:cap;
    return dist<cap?dist:cap;}
int sock_send(int fd,const void *buf,size_t len,int flags){socket_t *s;size_t av,i;const uint8_t *p;(void)flags;
    if(fd<0||fd>=SOCK_MAX||!g_sockets[fd].used)return -EBADF;
    s=&g_sockets[fd];
    if(s->type==1&&s->tcp_state!=TCP_ESTABLISHED)return -ENOTCONN;
    if(s->type==1&&s->shutdown_tx)return -EPIPE;
    p=(const uint8_t*)buf;
    if(s->type==2){
        uint32_t sip=s->local_ip;
        if(!s->remote_port)return -ENOTCONN;
        if(!s->local_port)s->local_port=alloc_eph();
        if(!sip){
            if(sock_is_loopback_ip(s->remote_ip))sip=0x7F000001u;
            else if(g_dhcp_lease.obtained&&g_dhcp_lease.ip)sip=g_dhcp_lease.ip;
            else sip=c_make_ip4(10,0,2,15);
            s->local_ip=sip;
        }
        if(s->peer>=0&&s->peer<SOCK_MAX&&g_sockets[s->peer].used){
            int rc=sock_push_udp(&g_sockets[s->peer],p,len,sip,s->local_port);
            if(rc<0)return rc;
            len=(size_t)rc;
        }else if(s->remote_port==53){
            bool got_real_dns=false;
            if(net_is_real_external(s->remote_ip)){
                uint32_t rx0=s->rx_head-s->rx_tail;
                int send_rc=-1;
                int tries;
                for(tries=0;tries<24;++tries){
                    send_rc=net_send_udp_frame(fd,p,len);
                    e1000_poll_rx();
                    if(send_rc>0)break;
                    {volatile int spin;for(spin=0;spin<20000;++spin)__asm__ volatile("pause");}
                }
                if(send_rc>0){
                    for(tries=0;tries<240;++tries){
                        e1000_poll_rx();
                        if((uint32_t)(s->rx_head-s->rx_tail)!=rx0){got_real_dns=true;break;}
                        {volatile int spin;for(spin=0;spin<20000;++spin)__asm__ volatile("pause");}
                    }
                }
            }
            if(!got_real_dns){
                int rc=sock_dns_reply(s,p,len);
                if(rc<0)return rc;
            }
        }else if(sock_is_loopback_ip(s->remote_ip)){
            int dst=sock_find_udp_bound(s->domain,fd,s->remote_ip,s->remote_port);
            if(dst>=0){
                int rc=sock_push_udp(&g_sockets[dst],p,len,sip,s->local_port);
                if(rc<0)return rc;
                len=(size_t)rc;
            }else{
                /* UDP to unbound loopback port: drop and report sent. */
            }
        }else if(net_is_real_external(s->remote_ip)){
            /* Real external UDP via E1000 NIC */
            net_send_udp_frame(fd,p,len);
        }
    }else if(s->type==1&&net_is_real_external(s->remote_ip)){
        int rc=tcp7_send(fd,p,len);
        if(rc<0)return rc;
        len=(size_t)rc;
    }else if(s->peer>=0&&s->peer<SOCK_MAX&&g_sockets[s->peer].used){
        if(g_sockets[s->peer].shutdown_rx)return -EPIPE;
        len=sock_push_rx(&g_sockets[s->peer],p,len);
        if((s->virt_service==SOCK_VIRT_X11||s->virt_service==SOCK_VIRT_WL)){
            int vrc=sock_virtual_peer_process(s);
            if(vrc<0)return vrc;
        }
    }else{
        uint32_t used=s->tx_head-s->tx_tail;
        if(used>SOCK_BUF_SIZE)used=SOCK_BUF_SIZE;
        av=SOCK_BUF_SIZE-used;
        if(len>av)len=av;
        for(i=0;i<len;++i){s->tx_buf[s->tx_head&(SOCK_BUF_SIZE-1)]=p[i];s->tx_head++;}
        if(s->virt_service!=SOCK_VIRT_NONE){
            int vrc=sock_virtual_stream_on_send(s,p,len);
            if(vrc<0)return vrc;
        }
    }
    s->tcp_seq+=(uint32_t)len;
    if(g_net_iface_count>0){g_net_ifaces[0].tx_bytes+=len;g_net_ifaces[0].tx_packets++;}
    return(int)len;
}
int sock_recv(int fd,void *buf,size_t len,int flags){socket_t *s;size_t av,i;uint8_t *p;uint32_t used;
    if(fd<0||fd>=SOCK_MAX||!g_sockets[fd].used)return -EBADF;
    s=&g_sockets[fd];
    if(net_is_real_external(s->remote_ip))e1000_poll_rx();
    if(s->type==2){
        int rc=sock_pop_udp(s,buf,len,flags);
        if(rc>0){
            s->tcp_ack+=(uint32_t)rc;
            if(g_net_iface_count>0){g_net_ifaces[0].rx_bytes+=(uint64_t)rc;g_net_ifaces[0].rx_packets++;}
        }
        return rc;
    }
    used=s->rx_head-s->rx_tail;
    if(used>SOCK_BUF_SIZE)used=SOCK_BUF_SIZE;
    av=used;
    if(!av){
        if(s->type==1&&(s->tcp_state==TCP_CLOSED||s->shutdown_rx))return 0;
        return(s->non_blocking||(flags&0x40))?-EAGAIN:0;
    }
    if(len>av)len=av;
    p=(uint8_t*)buf;
    for(i=0;i<len;++i){p[i]=s->rx_buf[s->rx_tail&(SOCK_BUF_SIZE-1)];s->rx_tail++;}
    s->tcp_ack+=(uint32_t)len;
    if(g_net_iface_count>0){g_net_ifaces[0].rx_bytes+=len;g_net_ifaces[0].rx_packets++;}
    return(int)len;
}
int sock_shutdown(int fd,int how){socket_t *s;int peer;
    if(fd<0||fd>=SOCK_MAX||!g_sockets[fd].used)return -EBADF;
    if(how<0||how>2)return -EINVAL;
    s=&g_sockets[fd];
    if(s->type!=1)return -ENOTCONN;
    if(how==0||how==2){
        s->shutdown_rx=1;
        s->rx_tail=s->rx_head;
    }
    if(how==1||how==2){
        s->shutdown_tx=1;
        peer=s->peer;
        if(peer>=0&&peer<SOCK_MAX&&g_sockets[peer].used&&g_sockets[peer].peer==fd)
            g_sockets[peer].shutdown_rx=1;
    }
    sock_wake_socket_sleepers();
    return 0;
}
int sock_close(int fd){socket_t *s;int peer;
    if(fd<0||fd>=SOCK_MAX||!g_sockets[fd].used)return -EBADF;
    s=&g_sockets[fd];
    if(s->virt_service==SOCK_VIRT_DBUS){
        dbusv_drop_socket(fd);
    }else if(s->virt_service==SOCK_VIRT_X11){
        if(s->peer>=0&&s->peer<SOCK_MAX&&g_sockets[s->peer].used){
            (void)x11_detach_socket(s->peer);
        }
    }else if(s->virt_service==SOCK_VIRT_WL){
        if(s->peer>=0&&s->peer<SOCK_MAX&&g_sockets[s->peer].used){
            (void)wl7_detach_socket(s->peer);
        }else{
            (void)wl7_detach_socket(fd);
        }
    }
    while(s->accept_head!=s->accept_tail){
        int qfd=s->accept_q[s->accept_tail];
        s->accept_tail=(uint8_t)((s->accept_tail+1u)%SOCK_ACCEPTQ);
        if(qfd>=0&&qfd<SOCK_MAX&&g_sockets[qfd].used)sock_close(qfd);
    }
    peer=s->peer;
    if(peer>=0&&peer<SOCK_MAX&&g_sockets[peer].used&&g_sockets[peer].peer==fd){
        bool internal_bridge=(s->virt_service==SOCK_VIRT_X11||s->virt_service==SOCK_VIRT_WL);
        if(internal_bridge){
            c_memset(&g_sockets[peer],0,sizeof(g_sockets[peer]));
        }else{
            g_sockets[peer].shutdown_rx=1;
            g_sockets[peer].peer=-1;
            if(g_sockets[peer].type==1)g_sockets[peer].tcp_state=TCP_CLOSED;
        }
        sock_wake_socket_sleepers();
    }
    if(s->type==1){tcp7_drop_socket(fd);s->tcp_state=TCP_CLOSED;}
    c_memset(s,0,sizeof(*s));
    return 0;}
int sock_setsockopt(int fd,int l,int o,const void *v,size_t n){(void)l;(void)o;(void)v;(void)n;
    if(fd<0||fd>=SOCK_MAX||!g_sockets[fd].used)return -EBADF;return 0;}

void dhcp_discover(int iface){(void)iface;if(!g_dhcp_lease.obtained){
    g_dhcp_lease.obtained=true;g_dhcp_lease.ip=c_make_ip4(10,0,2,15);
    g_dhcp_lease.netmask=c_make_ip4(255,255,255,0);g_dhcp_lease.gateway=c_make_ip4(10,0,2,2);
    g_dhcp_lease.dns=c_make_ip4(10,0,2,3);g_dhcp_lease.lease_time=86400;g_dhcp_lease.server_ip=c_make_ip4(10,0,2,2);
    if(g_net_iface_count>1){g_net_ifaces[1].ip4=g_dhcp_lease.ip;g_net_ifaces[1].netmask=g_dhcp_lease.netmask;
        g_net_ifaces[1].gateway=g_dhcp_lease.gateway;g_net_ifaces[1].dns=g_dhcp_lease.dns;g_net_ifaces[1].up=true;}}}
void dhcp_process(const uint8_t *d,size_t l){(void)d;(void)l;}

static bool dns_parse_ipv4(const char *s,uint32_t *ip){
    uint32_t p[4]={0,0,0,0};
    int i=0;
    if(!s||!ip)return false;
    while(*s&&i<4){
        uint32_t v=0;
        int digs=0;
        while(*s>='0'&&*s<='9'){
            v=v*10u+(uint32_t)(*s-'0');
            if(v>255u)return false;
            ++s;++digs;
        }
        if(!digs)return false;
        p[i++]=v;
        if(*s=='.')++s;
        else if(*s==0)break;
        else return false;
    }
    if(i!=4||*s!=0)return false;
    *ip=c_make_ip4((uint8_t)p[0],(uint8_t)p[1],(uint8_t)p[2],(uint8_t)p[3]);
    return true;
}
static uint32_t dns_host_hash(const char *s){
    uint32_t h=2166136261u;
    if(!s)return h;
    while(*s){
        h^=(uint8_t)dns_tolower(*s++);
        h*=16777619u;
    }
    return h;
}
static bool dns_should_synthesize(const char *h){
    size_t i=0,dots=0;
    if(!h||!h[0]||c_strcmp(h,".")==0)return false;
    for(i=0;h[i];++i){
        char ch=dns_tolower(h[i]);
        if(ch=='.'){++dots;continue;}
        if((ch>='a'&&ch<='z')||(ch>='0'&&ch<='9')||ch=='-')continue;
        return false;
    }
    if(!dots)return false;
    if(c_starts_with(h,"localhost"))return false;
    return true;
}
static uint32_t dns_synth_ipv4(const char *h){
    uint32_t v=dns_host_hash(h);
    uint8_t c=(uint8_t)((v>>8)&0xFFu);
    uint8_t d=(uint8_t)(v&0xFFu);
    if(c==0||c==255)c^=0x5Au;
    if(d==0||d==255)d^=0xA5u;
    return c_make_ip4(198,18,c,d); /* RFC 2544 benchmarking range */
}
static void dns_cache_put(const char *h,uint32_t ip,uint32_t ttl){
    int i;
    for(i=0;i<DNS_CACHE_SIZE;++i)if(!g_dns_cache[i].valid){
        c_strlcpy(g_dns_cache[i].name,h,64);
        g_dns_cache[i].ip=ip;
        g_dns_cache[i].ttl=ttl;
        g_dns_cache[i].valid=true;
        return;
    }
    g_dns_cache[0].valid=true;
    c_strlcpy(g_dns_cache[0].name,h,64);
    g_dns_cache[0].ip=ip;
    g_dns_cache[0].ttl=ttl;
}
bool dns_resolve(const char *h,uint32_t *ip){int i;
    if(!h||!ip)return false;
    if(dns_parse_ipv4(h,ip))return true;
    for(i=0;i<DNS_CACHE_SIZE;++i)if(g_dns_cache[i].valid&&c_strcmp(g_dns_cache[i].name,h)==0){*ip=g_dns_cache[i].ip;return true;}
    if(c_strcmp(h,"localhost")==0){*ip=c_make_ip4(127,0,0,1);return true;}
    if(c_strcmp(h,"riduxos.local")==0){*ip=c_make_ip4(10,0,2,15);return true;}
    if(c_strcmp(h,"google.com")==0||c_strcmp(h,"dns.google")==0){*ip=c_make_ip4(142,251,129,110);dns_cache_put(h,*ip,300);return true;}
    if(c_strcmp(h,"www.google.com")==0){*ip=c_make_ip4(142,251,151,119);dns_cache_put(h,*ip,300);return true;}
    if(c_strcmp(h,"youtube.com")==0||c_strcmp(h,"www.youtube.com")==0){*ip=c_make_ip4(172,217,28,238);dns_cache_put(h,*ip,300);return true;}
    if(c_strcmp(h,"ytimg.com")==0||c_strcmp(h,"www.ytimg.com")==0){*ip=c_make_ip4(142,250,184,78);dns_cache_put(h,*ip,300);return true;}
    if(c_strcmp(h,"github.com")==0||c_strcmp(h,"www.github.com")==0){*ip=c_make_ip4(4,228,31,150);dns_cache_put(h,*ip,300);return true;}
    if(c_strcmp(h,"example.com")==0||c_strcmp(h,"www.example.com")==0){*ip=c_make_ip4(172,66,147,243);dns_cache_put(h,*ip,300);return true;}
    if(dns_should_synthesize(h)){
        *ip=dns_synth_ipv4(h);
        dns_cache_put(h,*ip,120);
        return true;
    }
    return false;}

/* FAT32 read-only */
fat32_fs_t g_fat32;
static uint8_t g_fat32_sbuf[512];

typedef struct __attribute__((packed)) {
    uint8_t jump[3];char oem[8];uint16_t bps;uint8_t spc;uint16_t rsvd;uint8_t nfats;
    uint16_t rootent;uint16_t totsec16;uint8_t media;uint16_t fatsz16;uint16_t spt;uint16_t heads;
    uint32_t hidden;uint32_t totsec32;uint32_t fatsz32;uint16_t extfl;uint16_t fsver;uint32_t rootclus;
    uint16_t fsinfo;uint16_t bkboot;uint8_t rsv[12];uint8_t drv;uint8_t rsv1;uint8_t bootsig;
    uint32_t volid;char vollabel[11];char fstype[8];
} fat32_bpb_t;
typedef struct __attribute__((packed)) {
    char name[8];char ext[3];uint8_t attr;uint8_t ntrsv;uint8_t crttenth;uint16_t crttime;
    uint16_t crtdate;uint16_t lastacc;uint16_t fstclushi;uint16_t wrttime;uint16_t wrtdate;
    uint16_t fstcluslo;uint32_t filesz;
} fat32_de_t;

static bool f32_read_sec(uint32_t lba){if(g_fat32.block_dev<0||g_fat32.block_dev>=g_block_dev_count)return false;
    int ai=g_block_devs[g_fat32.block_dev].ata_index;if(ai<0)return false;return ata_read_sectors(ai,lba,1,g_fat32_sbuf);}
static uint32_t f32_next_clus(uint32_t c){uint32_t fo=c*4,fs=g_fat32.fat_start_lba+(fo/g_fat32.bytes_per_sector),eo=fo%g_fat32.bytes_per_sector;
    if(!f32_read_sec(fs))return 0x0FFFFFF8;return(*(uint32_t*)&g_fat32_sbuf[eo])&0x0FFFFFFFu;}
static uint32_t f32_clus2lba(uint32_t c){return g_fat32.cluster_start_lba+(c-2)*g_fat32.sectors_per_cluster;}

bool fat32_mount(int bd){fat32_bpb_t *b;c_memset(&g_fat32,0,sizeof(g_fat32));g_fat32.block_dev=bd;
    if(bd<0||bd>=g_block_dev_count||g_block_devs[bd].ata_index<0)return false;
    if(!ata_read_sectors(g_block_devs[bd].ata_index,0,1,g_fat32_sbuf))return false;
    b=(fat32_bpb_t*)g_fat32_sbuf;if(b->bps!=512||!b->nfats||b->nfats>2||!b->fatsz32)return false;
    g_fat32.bytes_per_sector=b->bps;g_fat32.sectors_per_cluster=b->spc;g_fat32.reserved_sectors=b->rsvd;
    g_fat32.num_fats=b->nfats;g_fat32.sectors_per_fat=b->fatsz32;g_fat32.root_cluster=b->rootclus;
    g_fat32.fat_start_lba=b->rsvd;g_fat32.cluster_start_lba=b->rsvd+(uint32_t)b->nfats*b->fatsz32;
    c_memcpy(g_fat32.label,b->vollabel,11);g_fat32.label[11]=0;g_fat32.mounted=true;return true;}

static void f32_parse83(const fat32_de_t *e,char *o,size_t cap){size_t l=0;int i;
    for(i=0;i<8&&e->name[i]!=' ';++i)c_append_ch(o,&l,cap,(e->name[i]>='A'&&e->name[i]<='Z')?(char)(e->name[i]+32):e->name[i]);
    if(e->ext[0]!=' '){c_append_ch(o,&l,cap,'.');
        for(i=0;i<3&&e->ext[i]!=' ';++i)c_append_ch(o,&l,cap,(e->ext[i]>='A'&&e->ext[i]<='Z')?(char)(e->ext[i]+32):e->ext[i]);}}

int fat32_list_dir(const char *path,fat32_dirent_t *ents,int max){uint32_t clus;int cnt=0,sec;(void)path;
    if(!g_fat32.mounted)return -1;clus=g_fat32.root_cluster;
    while(clus<0x0FFFFFF8&&cnt<max){uint32_t lba=f32_clus2lba(clus);
        for(sec=0;sec<g_fat32.sectors_per_cluster&&cnt<max;++sec){int i;if(!f32_read_sec(lba+(uint32_t)sec))break;
            for(i=0;i<16&&cnt<max;++i){fat32_de_t *de=(fat32_de_t*)&g_fat32_sbuf[i*32];
                if((uint8_t)de->name[0]==0)goto done;if((uint8_t)de->name[0]==0xE5)continue;
                if(de->attr==0x0F||de->attr&0x08)continue;
                ents[cnt].name[0]=0;f32_parse83(de,ents[cnt].name,sizeof(ents[cnt].name));
                ents[cnt].cluster=((uint32_t)de->fstclushi<<16)|de->fstcluslo;
                ents[cnt].size=de->filesz;ents[cnt].attr=de->attr;ents[cnt].is_dir=(de->attr&0x10)!=0;++cnt;}}
        clus=f32_next_clus(clus);}done:return cnt;}

bool fat32_read_file(const char *path,uint8_t *buf,uint32_t bsz,uint32_t *osz){
    fat32_dirent_t ents[FAT32_MAX_DIR_ENTRIES];int cnt,i;uint32_t clus,off=0;
    if(!g_fat32.mounted)return false;cnt=fat32_list_dir("/",ents,FAT32_MAX_DIR_ENTRIES);if(cnt<=0)return false;
    for(i=0;i<cnt;++i){const char *fn=path;while(*fn=='/')fn++;if(c_strcmp(ents[i].name,fn)==0&&!ents[i].is_dir)break;}
    if(i>=cnt)return false;clus=ents[i].cluster;*osz=ents[i].size;if(*osz>bsz)*osz=bsz;
    while(clus<0x0FFFFFF8&&off<*osz){uint32_t lba=f32_clus2lba(clus);int s;
        for(s=0;s<g_fat32.sectors_per_cluster&&off<*osz;++s){uint32_t cp=*osz-off;if(cp>512)cp=512;
            if(!f32_read_sec(lba+(uint32_t)s))return false;c_memcpy(buf+off,g_fat32_sbuf,cp);off+=cp;}
        clus=f32_next_clus(clus);}return true;}

/* ext2 read-only (superblock parsing) */
ext2_fs_t g_ext2;
static uint8_t g_ext2_sbuf[1024];

bool ext2_mount(int bd){
    typedef struct __attribute__((packed)){
        uint32_t ic,bc,rbc,fbc,fic,fdb,lbs,lfs,bpg,fpg,ipg,mt,wt;
        uint16_t mc,mmc,magic,state,errors,minor;
        uint32_t lc,ci,cos,rev;uint16_t dru,drg,fino;uint16_t isz;
    } ext2sb_t;
    ext2sb_t *sb; c_memset(&g_ext2,0,sizeof(g_ext2));g_ext2.block_dev=bd;
    if(bd<0||bd>=g_block_dev_count||g_block_devs[bd].ata_index<0)return false;
    if(!ata_read_sectors(g_block_devs[bd].ata_index,2,2,g_ext2_sbuf))return false;
    sb=(ext2sb_t*)g_ext2_sbuf;if(sb->magic!=0xEF53)return false;
    g_ext2.block_size=1024u<<sb->lbs;g_ext2.blocks_count=sb->bc;g_ext2.inodes_count=sb->ic;
    g_ext2.inodes_per_group=sb->ipg;g_ext2.blocks_per_group=sb->bpg;g_ext2.first_data_block=sb->fdb;
    g_ext2.inode_size=sb->isz?sb->isz:128;g_ext2.mounted=true;return true;}
bool ext2_read_file(const char *p,uint8_t *b,uint32_t bs,uint32_t *os){(void)p;(void)b;(void)bs;*os=0;return false;}

/* Mount table */
mount_entry_t g_mounts[MOUNT_MAX]; int g_mount_count;
bool vfs_ext_mount(const char *src,const char *tgt,const char *fs){
    if(g_mount_count>=MOUNT_MAX)return false;mount_entry_t *m=&g_mounts[g_mount_count];
    m->used=true;c_strlcpy(m->source,src,32);c_strlcpy(m->target,tgt,32);c_strlcpy(m->fstype,fs,16);
    m->block_dev=-1;++g_mount_count;return true;}
bool vfs_ext_umount(const char *tgt){int i;for(i=0;i<g_mount_count;++i)
    if(g_mounts[i].used&&c_strcmp(g_mounts[i].target,tgt)==0){g_mounts[i].used=false;return true;}return false;}

/* Linux syscall compat table (130+ entries) */
proc_compat_t g_proc_compat[PROC_COMPAT_MAX];
syscall_fn_t  g_syscall_table[SYSCALL_MAX];
static uint64_t g_syscall_hit_count[SYSCALL_MAX];
static uint64_t g_syscall_enosys_count[SYSCALL_MAX];
static uint64_t g_syscall_last_a0[SYSCALL_MAX];
static uint64_t g_syscall_oob_count;
static uint64_t g_syscall_oob_last_nr;
static uint32_t g_boot_syscall_trace_count;
static uint32_t g_boot_enosys_trace_count;
static uint32_t g_boot_futex_trace_count;
int sys_epoll_ctl(int epfd,int op,int fd,epoll_event_t *ev);
int sys_epoll_wait(int epfd,epoll_event_t *ev,int max,int timeout);

void compat_syscall_trace_reset(void){
    c_memset(g_syscall_hit_count,0,sizeof(g_syscall_hit_count));
    c_memset(g_syscall_enosys_count,0,sizeof(g_syscall_enosys_count));
    c_memset(g_syscall_last_a0,0,sizeof(g_syscall_last_a0));
    g_syscall_oob_count=0;
    g_syscall_oob_last_nr=0;
    g_boot_syscall_trace_count=0;
    g_boot_enosys_trace_count=0;
    g_boot_futex_trace_count=0;
}

void compat_syscall_trace_note(uint64_t nr,uint64_t a0,int64_t ret){
    if(nr>=SYSCALL_MAX){
        ++g_syscall_oob_count;
        g_syscall_oob_last_nr=nr;
        return;
    }
    ++g_syscall_hit_count[nr];
    g_syscall_last_a0[nr]=a0;
    if(ret==-ENOSYS)++g_syscall_enosys_count[nr];
}

#define STUB6(name) static int64_t name(uint64_t a,uint64_t b,uint64_t c,uint64_t d,uint64_t e,uint64_t f){(void)a;(void)b;(void)c;(void)d;(void)e;(void)f;return -ENOSYS;}
#define STUB6_OK(name) static int64_t name(uint64_t a,uint64_t b,uint64_t c,uint64_t d,uint64_t e,uint64_t f){(void)a;(void)b;(void)c;(void)d;(void)e;(void)f;return 0;}
#define STUB6_VAL(name,v) static int64_t name(uint64_t a,uint64_t b,uint64_t c,uint64_t d,uint64_t e,uint64_t f){(void)a;(void)b;(void)c;(void)d;(void)e;(void)f;return v;}

static int64_t sys_read(uint64_t fd,uint64_t b,uint64_t c,uint64_t d,uint64_t e,uint64_t f){(void)d;(void)e;(void)f;if(fd==0)return 0;return -EBADF;}
static int64_t sys_write(uint64_t fd,uint64_t b,uint64_t c,uint64_t d,uint64_t e,uint64_t f){(void)d;(void)e;(void)f;(void)b;if(fd==1||fd==2)return(int64_t)c;return -EBADF;}
static int64_t sys_open(uint64_t a,uint64_t b,uint64_t c,uint64_t d,uint64_t e,uint64_t f){(void)a;(void)b;(void)c;(void)d;(void)e;(void)f;return -ENOENT;}
STUB6_OK(sys_close) STUB6(sys_stat) STUB6(sys_fstat) STUB6_OK(sys_lseek)
STUB6(sys_poll_s) STUB6(sys_ioctl) STUB6(sys_access) STUB6(sys_pipe_s)
STUB6(sys_dup) STUB6(sys_dup2)
STUB6_VAL(sys_getpid,1) STUB6_VAL(sys_getppid,0) STUB6_VAL(sys_getuid,0) STUB6_VAL(sys_getgid,0)
STUB6_VAL(sys_geteuid,0) STUB6_VAL(sys_getegid,0) STUB6_VAL(sys_gettid,1)
static int64_t sys_fork_s(uint64_t a,uint64_t b,uint64_t c,uint64_t d,uint64_t e,uint64_t f){(void)a;(void)b;(void)c;(void)d;(void)e;(void)f;return -EAGAIN;}
static int64_t sys_execve_s(uint64_t a,uint64_t b,uint64_t c,uint64_t d,uint64_t e,uint64_t f){(void)a;(void)b;(void)c;(void)d;(void)e;(void)f;return -ENOEXEC;}
STUB6_OK(sys_exit_s) STUB6(sys_wait4_s) STUB6(sys_kill_s)
static int64_t sys_uname_s(uint64_t buf,uint64_t b,uint64_t c,uint64_t d,uint64_t e,uint64_t f){
    char *p=(char*)(uintptr_t)buf;(void)b;(void)c;(void)d;(void)e;(void)f;if(!p)return -EFAULT;
    c_strlcpy(p,"RiduxOS",65);c_strlcpy(p+65,"ridux",65);c_strlcpy(p+130,"1.0.0",65);
    c_strlcpy(p+195,"RiduxOS 1.0.0 SMP x86_64",65);c_strlcpy(p+260,"x86_64",65);return 0;}
STUB6_OK(sys_fcntl_s) STUB6_OK(sys_chdir_s) STUB6_OK(sys_mkdir_s) STUB6_OK(sys_rmdir_s)
STUB6_OK(sys_unlink_s) STUB6(sys_link_s) STUB6_OK(sys_rename_s) STUB6(sys_readlink_s)
STUB6_OK(sys_chmod_s) STUB6_OK(sys_chown_s)
static int64_t sys_getcwd_s(uint64_t buf,uint64_t sz,uint64_t c,uint64_t d,uint64_t e,uint64_t f){
    (void)c;(void)d;(void)e;(void)f;char *p=(char*)(uintptr_t)buf;if(!p||sz<2)return -EINVAL;c_strlcpy(p,"/",(size_t)sz);return(int64_t)(uintptr_t)p;}
STUB6_OK(sys_time_s) STUB6_OK(sys_clock_gettime_s) STUB6_OK(sys_nanosleep_s)
static int64_t sys_socket_s(uint64_t d,uint64_t t,uint64_t p,uint64_t a3,uint64_t a4,uint64_t a5){(void)a3;(void)a4;(void)a5;return sock_create((int)d,(int)t,(int)p);}
static int64_t sys_connect_s(uint64_t fd,uint64_t a,uint64_t al,uint64_t a3,uint64_t a4,uint64_t a5){(void)a;(void)al;(void)a3;(void)a4;(void)a5;return sock_connect((int)fd,0,0);}
static int64_t sys_bind_s(uint64_t fd,uint64_t a,uint64_t al,uint64_t a3,uint64_t a4,uint64_t a5){(void)a;(void)al;(void)a3;(void)a4;(void)a5;return sock_bind((int)fd,0,0);}
static int64_t sys_listen_s(uint64_t fd,uint64_t bl,uint64_t c,uint64_t d,uint64_t e,uint64_t f){(void)c;(void)d;(void)e;(void)f;return sock_listen((int)fd,(int)bl);}
static int64_t sys_accept_s(uint64_t fd,uint64_t a,uint64_t al,uint64_t a3,uint64_t a4,uint64_t a5){(void)a;(void)al;(void)a3;(void)a4;(void)a5;return sock_accept((int)fd,0,0);}
static int64_t sys_getsockname_s(uint64_t fd,uint64_t a,uint64_t al,uint64_t a3,uint64_t a4,uint64_t a5){(void)fd;(void)a;(void)al;(void)a3;(void)a4;(void)a5;return 0;}
static int64_t sys_sendto_s(uint64_t fd,uint64_t b,uint64_t l,uint64_t fl,uint64_t da,uint64_t dl){(void)da;(void)dl;return sock_send((int)fd,(const void*)(uintptr_t)b,(size_t)l,(int)fl);}
static int64_t sys_recvfrom_s(uint64_t fd,uint64_t b,uint64_t l,uint64_t fl,uint64_t sa,uint64_t sl){(void)sa;(void)sl;return sock_recv((int)fd,(void*)(uintptr_t)b,(size_t)l,(int)fl);}
STUB6_OK(sys_shutdown_s) STUB6_OK(sys_setsockopt_s) STUB6_OK(sys_getsockopt_s)
STUB6(sys_clone_s) STUB6_OK(sys_sigaction_s) STUB6_OK(sys_sigprocmask_s)
STUB6_VAL(sys_getdents64_s,0) STUB6_OK(sys_set_tid_s) STUB6_OK(sys_set_robust_s) STUB6_OK(sys_get_robust_s)
STUB6_OK(sys_prlimit64_s) STUB6_OK(sys_arch_prctl_s) STUB6_OK(sys_prctl_s)
STUB6_OK(sys_sysinfo_s) STUB6_OK(sys_getrusage_s)
static int64_t sys_epoll_wait_s(uint64_t epfd,uint64_t ev,uint64_t max,uint64_t timeout,uint64_t e,uint64_t f){
    (void)e;(void)f;
    return (int64_t)sys_epoll_wait((int)epfd,(epoll_event_t*)(uintptr_t)ev,(int)max,(int)timeout);
}
static int64_t sys_epoll_ctl_s(uint64_t epfd,uint64_t op,uint64_t fd,uint64_t ev,uint64_t e,uint64_t f){
    (void)e;(void)f;
    return (int64_t)sys_epoll_ctl((int)epfd,(int)op,(int)fd,(epoll_event_t*)(uintptr_t)ev);
}
static int64_t sys_getrandom_s(uint64_t buf,uint64_t cnt,uint64_t fl,uint64_t d,uint64_t e,uint64_t f){
    uint8_t *p=(uint8_t*)(uintptr_t)buf;uint64_t i;static uint32_t seed=12345;(void)fl;(void)d;(void)e;(void)f;
    if(!p)return -EFAULT;for(i=0;i<cnt;++i){seed=seed*1103515245u+12345u;p[i]=(uint8_t)(seed>>16);}return(int64_t)cnt;}
static int64_t sys_eventfd_s(uint64_t initv,uint64_t flags,uint64_t c,uint64_t d,uint64_t e,uint64_t f){
    static int64_t next_fd=2000;
    (void)initv;(void)flags;(void)c;(void)d;(void)e;(void)f;
    return next_fd++;
}
static int64_t sys_timerfd_s(uint64_t c,uint64_t f,uint64_t d,uint64_t e,uint64_t g,uint64_t h){
    static int64_t next_tfd=3000;
    (void)c;(void)f;(void)d;(void)e;(void)g;(void)h;
    return next_tfd++;
}
static int64_t sys_timerfd_settime_s(uint64_t fd,uint64_t fl,uint64_t newv,uint64_t oldv,uint64_t e,uint64_t f){
    (void)fd;(void)fl;(void)newv;(void)oldv;(void)e;(void)f;
    return 0;
}
static int64_t sys_sendmmsg_s(uint64_t fd,uint64_t msgvec,uint64_t vlen,uint64_t flags,uint64_t e,uint64_t f){
    (void)fd;(void)msgvec;(void)flags;(void)e;(void)f;
    return (int64_t)vlen;
}
static int64_t sys_recvmmsg_s(uint64_t fd,uint64_t msgvec,uint64_t vlen,uint64_t flags,uint64_t timeout,uint64_t e){
    (void)fd;(void)msgvec;(void)vlen;(void)flags;(void)timeout;(void)e;
    return 0;
}
static int64_t sys_process_vm_readv_s(uint64_t pid,uint64_t liov,uint64_t lcnt,uint64_t riov,uint64_t rcnt,uint64_t fl){
    (void)pid;(void)liov;(void)lcnt;(void)riov;(void)rcnt;(void)fl;
    return -EPERM;
}
static int64_t sys_process_vm_writev_s(uint64_t pid,uint64_t liov,uint64_t lcnt,uint64_t riov,uint64_t rcnt,uint64_t fl){
    (void)pid;(void)liov;(void)lcnt;(void)riov;(void)rcnt;(void)fl;
    return -EPERM;
}
static int64_t sys_userfaultfd_s(uint64_t flags,uint64_t b,uint64_t c,uint64_t d,uint64_t e,uint64_t f){
    (void)flags;(void)b;(void)c;(void)d;(void)e;(void)f;
    return -EPERM;
}
STUB6(sys_signalfd_s) STUB6(sys_memfd_s)
static int64_t sys_writev_s(uint64_t fd,uint64_t iov,uint64_t cnt,uint64_t d,uint64_t e,uint64_t f){(void)iov;(void)d;(void)e;(void)f;if(fd==1||fd==2)return(int64_t)cnt;return -EBADF;}
static int64_t sys_readv_s(uint64_t fd,uint64_t iov,uint64_t cnt,uint64_t d,uint64_t e,uint64_t f){(void)iov;(void)cnt;(void)d;(void)e;(void)f;if(fd==0)return 0;return -EBADF;}
STUB6_OK(sys_pread64_s) STUB6_OK(sys_pwrite64_s) STUB6(sys_sendmsg_s) STUB6(sys_recvmsg_s)
STUB6(sys_truncate_s) STUB6(sys_ftruncate_s) STUB6_OK(sys_fchmod_s) STUB6_OK(sys_fchown_s)
STUB6_OK(sys_umask_s) STUB6(sys_getrlimit_s) STUB6(sys_setrlimit_s) STUB6(sys_sched_yield_s)
STUB6(sys_sched_getaffinity_s) STUB6(sys_sched_setaffinity_s) STUB6_OK(sys_madvise_s)
STUB6(sys_openat_s) STUB6_OK(sys_mkdirat_s) STUB6_OK(sys_fstatat_s) STUB6_OK(sys_unlinkat_s)
STUB6_OK(sys_renameat_s) STUB6(sys_readlinkat_s) STUB6_OK(sys_fchmodat_s) STUB6_OK(sys_fchownat_s)
STUB6(sys_faccessat_s) STUB6(sys_newfstatat_s) STUB6(sys_accept4_s)
STUB6(sys_epoll_create1_s) STUB6(sys_dup3_s) STUB6(sys_pipe2_s)
STUB6(sys_inotify_init_s) STUB6(sys_inotify_add_s) STUB6(sys_inotify_rm_s)

void syscall_init(void) {
    int i; for(i=0;i<SYSCALL_MAX;++i)g_syscall_table[i]=0;
    c_memset(g_proc_compat,0,sizeof(g_proc_compat));
    compat_syscall_trace_reset();
    /* Linux x86_64 syscall numbers */
    g_syscall_table[0]=sys_read;      g_syscall_table[1]=sys_write;
    g_syscall_table[2]=sys_open;      g_syscall_table[3]=sys_close;
    g_syscall_table[4]=sys_stat;      g_syscall_table[5]=sys_fstat;
    g_syscall_table[6]=sys_lseek;     g_syscall_table[7]=sys_poll_s;
    g_syscall_table[8]=sys_lseek;     /* mmap placeholder */
    g_syscall_table[9]=sys_lseek;     /* mmap */
    g_syscall_table[10]=sys_lseek;    /* mprotect */
    g_syscall_table[11]=sys_lseek;    /* munmap */
    g_syscall_table[12]=sys_lseek;    /* brk */
    g_syscall_table[13]=sys_sigaction_s; g_syscall_table[14]=sys_sigprocmask_s;
    g_syscall_table[16]=sys_ioctl;    g_syscall_table[17]=sys_access;
    g_syscall_table[19]=sys_readv_s;  g_syscall_table[20]=sys_writev_s;
    g_syscall_table[21]=sys_access;   g_syscall_table[22]=sys_pipe_s;
    g_syscall_table[28]=sys_time_s;
    g_syscall_table[32]=sys_dup;      g_syscall_table[33]=sys_dup2;
    g_syscall_table[35]=sys_nanosleep_s;
    g_syscall_table[39]=sys_getpid;   g_syscall_table[41]=sys_socket_s;
    g_syscall_table[42]=sys_connect_s; g_syscall_table[43]=sys_accept_s;
    g_syscall_table[44]=sys_sendto_s;  g_syscall_table[45]=sys_recvfrom_s;
    g_syscall_table[46]=sys_sendmsg_s; g_syscall_table[47]=sys_recvmsg_s;
    g_syscall_table[48]=sys_shutdown_s; g_syscall_table[49]=sys_bind_s;
    g_syscall_table[50]=sys_listen_s;  g_syscall_table[51]=sys_getsockopt_s;
    g_syscall_table[52]=sys_setsockopt_s; g_syscall_table[53]=sys_setsockopt_s;
    g_syscall_table[54]=sys_setsockopt_s;
    g_syscall_table[55]=sys_getsockname_s;
    g_syscall_table[56]=sys_clone_s;  g_syscall_table[57]=sys_fork_s;
    g_syscall_table[59]=sys_execve_s; g_syscall_table[60]=sys_exit_s;
    g_syscall_table[61]=sys_wait4_s;  g_syscall_table[62]=sys_kill_s;
    g_syscall_table[63]=sys_uname_s;
    g_syscall_table[72]=sys_fcntl_s;  g_syscall_table[79]=sys_getcwd_s;
    g_syscall_table[80]=sys_chdir_s;  g_syscall_table[82]=sys_rename_s;
    g_syscall_table[83]=sys_mkdir_s;  g_syscall_table[84]=sys_rmdir_s;
    g_syscall_table[87]=sys_unlink_s; g_syscall_table[89]=sys_readlink_s;
    g_syscall_table[90]=sys_chmod_s;  g_syscall_table[92]=sys_chown_s;
    g_syscall_table[95]=sys_umask_s;
    g_syscall_table[96]=sys_time_s;   /* gettimeofday */
    g_syscall_table[97]=sys_getrlimit_s; g_syscall_table[98]=sys_getrusage_s;
    g_syscall_table[99]=sys_sysinfo_s;
    g_syscall_table[102]=sys_getuid;  g_syscall_table[104]=sys_getgid;
    g_syscall_table[107]=sys_geteuid; g_syscall_table[108]=sys_getegid;
    g_syscall_table[110]=sys_getppid;
    g_syscall_table[157]=sys_prctl_s; g_syscall_table[158]=sys_arch_prctl_s;
    g_syscall_table[186]=sys_gettid;
    g_syscall_table[202]=sys_sched_yield_s;
    g_syscall_table[213]=sys_epoll_create1_s;
    g_syscall_table[217]=sys_getdents64_s;
    g_syscall_table[218]=sys_set_tid_s;
    g_syscall_table[228]=sys_clock_gettime_s;
    g_syscall_table[230]=sys_nanosleep_s;
    g_syscall_table[231]=sys_exit_s;  /* exit_group */
    g_syscall_table[232]=sys_epoll_wait_s;
    g_syscall_table[233]=sys_epoll_ctl_s;
    g_syscall_table[257]=sys_openat_s; g_syscall_table[258]=sys_mkdirat_s;
    g_syscall_table[260]=sys_fchownat_s; g_syscall_table[261]=sys_fstatat_s;
    g_syscall_table[262]=sys_unlinkat_s; g_syscall_table[263]=sys_renameat_s;
    g_syscall_table[267]=sys_readlinkat_s; g_syscall_table[268]=sys_fchmodat_s;
    g_syscall_table[269]=sys_faccessat_s;
    g_syscall_table[270]=sys_lseek;   /* pselect6 */
    g_syscall_table[271]=sys_lseek;   /* ppoll */
    g_syscall_table[273]=sys_set_robust_s;
    g_syscall_table[274]=sys_get_robust_s;
    g_syscall_table[281]=sys_epoll_create1_s;
    g_syscall_table[286]=sys_timerfd_settime_s;
    g_syscall_table[288]=sys_accept4_s;
    g_syscall_table[290]=sys_eventfd_s;
    g_syscall_table[291]=sys_epoll_create1_s;
    g_syscall_table[292]=sys_dup3_s;  g_syscall_table[293]=sys_pipe2_s;
    g_syscall_table[294]=sys_inotify_init_s;
    g_syscall_table[299]=sys_sendmmsg_s;
    g_syscall_table[302]=sys_prlimit64_s;
    g_syscall_table[307]=sys_recvmmsg_s;
    g_syscall_table[310]=sys_process_vm_readv_s;
    g_syscall_table[311]=sys_process_vm_writev_s;
    g_syscall_table[318]=sys_getrandom_s;
    g_syscall_table[319]=sys_memfd_s;
    g_syscall_table[323]=sys_userfaultfd_s;
}

/* Cheap bring-up trace of the first syscalls issued from user mode.
 * Keep this small by default: real browsers make thousands of syscalls and
 * serial logging alone can turn startup into minutes. Raise these locally
 * only when chasing an early loader failure. */
#ifndef BOOT_SYSCALL_TRACE_MAX
#define BOOT_SYSCALL_TRACE_MAX 96
#endif
#ifndef BOOT_FUTEX_TRACE_MAX
#define BOOT_FUTEX_TRACE_MAX 32
#endif
#ifndef BOOT_ENOSYS_TRACE_MAX
#define BOOT_ENOSYS_TRACE_MAX 96
#endif

static void compat_syscall_trace_enosys_print(uint64_t nr,uint64_t a0,uint64_t a1){
    if(!compat_wayfire_debug_trace_enabled())return;
    if(g_boot_enosys_trace_count>=BOOT_ENOSYS_TRACE_MAX)return;
    ++g_boot_enosys_trace_count;
    __boot_serial_puts("[sys ENOSYS #");
    __boot_serial_putu32(g_boot_enosys_trace_count);
    __boot_serial_puts("] nr=");
    __boot_serial_putu32((uint32_t)nr);
    __boot_serial_puts(" a0=");
    __boot_serial_puthex64(a0);
    __boot_serial_puts(" a1=");
    __boot_serial_puthex64(a1);
    __boot_serial_puts("\n");
}

static bool compat_sys_task_is_browser(const task_t *t){
    if(!t)return false;
    if(c_starts_with(t->exec_path,"/opt/firefox/")||
       c_starts_with(t->exec_path,"/opt/chromium/")||
       c_mem_has((const uint8_t*)t->exec_path,c_strlen(t->exec_path),"/firefox")||
       c_mem_has((const uint8_t*)t->exec_path,c_strlen(t->exec_path),"/chrome")||
       c_mem_has((const uint8_t*)t->exec_path,c_strlen(t->exec_path),"/chromium"))return true;
    if(c_mem_has((const uint8_t*)t->name,c_strlen(t->name),"firefox")||
       c_mem_has((const uint8_t*)t->name,c_strlen(t->name),"chrome")||
       c_mem_has((const uint8_t*)t->name,c_strlen(t->name),"Chrome")||
       c_mem_has((const uint8_t*)t->name,c_strlen(t->name),"ThreadPool"))return true;
    return false;
}

static bool compat_sys_task_is_wayfire(const task_t *t){
    if(!t)return false;
    if(c_starts_with(t->exec_path,"/opt/wayfire/")||
       c_mem_has((const uint8_t*)t->exec_path,c_strlen(t->exec_path),"/wayfire")||
       c_mem_has((const uint8_t*)t->exec_path,c_strlen(t->exec_path),"/wf-")||
       c_mem_has((const uint8_t*)t->exec_path,c_strlen(t->exec_path),"/wf-shell")||
       c_mem_has((const uint8_t*)t->exec_path,c_strlen(t->exec_path),"/wcm"))return true;
    if(c_mem_has((const uint8_t*)t->name,c_strlen(t->name),"wayfire")||
       c_mem_has((const uint8_t*)t->name,c_strlen(t->name),"wf-")||
       c_mem_has((const uint8_t*)t->name,c_strlen(t->name),"wf-shell")||
       c_mem_has((const uint8_t*)t->name,c_strlen(t->name),"wcm"))return true;
    return false;
}

static volatile uint32_t g_wayfire_after_keymap_trace;
static uint32_t g_wayfire_after_keymap_trace_count;
#define COMPAT_SYSCALL_PENDING_RC (-0x7fffffffffffffffLL - 1LL)

void compat_syscall_trace_wayfire_after_keymap(void){
    if(g_wayfire_after_keymap_trace)return;
    g_wayfire_after_keymap_trace=1;
    __boot_serial_force_puts("[wf-post-keymap-sys-start!]\n");
}

static bool compat_syscall_trace_wayfire_after_keymap_on(const task_t *cur){
    return g_wayfire_after_keymap_trace &&
           g_wayfire_after_keymap_trace_count<260u &&
           compat_sys_task_is_wayfire(cur);
}

static void compat_syscall_trace_wayfire_after_keymap_print(const char *tag,
        const task_t *cur,uint64_t nr,uint64_t a0,uint64_t a1,int64_t rc){
    if(!compat_syscall_trace_wayfire_after_keymap_on(cur))return;
    ++g_wayfire_after_keymap_trace_count;
    __boot_serial_force_puts(tag);
    __boot_serial_force_puts(" #");
    __boot_serial_force_putu32(g_wayfire_after_keymap_trace_count);
    __boot_serial_force_puts(" pid=");
    __boot_serial_force_putu32((uint32_t)(cur?cur->pid:0));
    if(cur&&cur->name[0]){
        __boot_serial_force_puts(" name=");
        __boot_serial_force_puts(cur->name);
    }
    __boot_serial_force_puts(" nr=");
    __boot_serial_force_putu32((uint32_t)nr);
    __boot_serial_force_puts(" rc=");
    if(rc==COMPAT_SYSCALL_PENDING_RC){
        __boot_serial_force_puts("pending");
    }else if(rc<0){
        __boot_serial_force_puts("-");
        __boot_serial_force_putu32((uint32_t)(-rc));
    }else{
        __boot_serial_force_puthex64((uint64_t)rc);
    }
    __boot_serial_force_puts(" a0=");
    __boot_serial_force_puthex64(a0);
    __boot_serial_force_puts(" a1=");
    __boot_serial_force_puthex64(a1);
    __boot_serial_force_puts("\n");
}

static bool compat_syscall_is_browser_interesting(uint64_t nr,int64_t rc){
    if(rc==-ENOSYS||rc==-EINVAL||rc==-EPERM)return true;
    switch(nr){
        case 7:   /* poll */
        case 41:  /* socket */
        case 42:  /* connect */
        case 46:  /* sendmsg */
        case 47:  /* recvmsg */
        case 53:  /* socketpair */
        case 56:  /* clone */
        case 59:  /* execve */
        case 60:  /* exit */
        case 61:  /* wait4 */
        case 158: /* arch_prctl */
        case 202: /* futex */
        case 217: /* getdents64 */
        case 231: /* exit_group */
        case 232: /* epoll_wait */
        case 233: /* epoll_ctl */
        case 262: /* newfstatat */
        case 271: /* ppoll */
        case 291: /* epoll_create1 */
        case 293: /* pipe2 */
        case 302: /* prlimit64 */
        case 318: /* getrandom */
            return true;
        default:
            return false;
    }
}

static void compat_syscall_trace_browser_force(uint64_t nr,uint64_t a0,uint64_t a1,int64_t rc){
    static uint32_t browser_sys_trace_count;
    task_t *cur;
    if(browser_sys_trace_count>=48u)return;
    cur=task_current();
    if(!compat_sys_task_is_browser(cur))return;
    if(!compat_syscall_is_browser_interesting(nr,rc))return;
    ++browser_sys_trace_count;
    __boot_serial_force_puts("[bsys!] pid=");
    __boot_serial_force_putu32((uint32_t)cur->pid);
    if(cur->name[0]){
        __boot_serial_force_puts(" name=");
        __boot_serial_force_puts(cur->name);
    }
    __boot_serial_force_puts(" nr=");
    __boot_serial_force_putu32((uint32_t)nr);
    __boot_serial_force_puts(" rc=");
    if(rc<0){
        __boot_serial_force_puts("-");
        __boot_serial_force_putu32((uint32_t)(-rc));
    }else{
        __boot_serial_force_puthex64((uint64_t)rc);
    }
    __boot_serial_force_puts(" a0=");
    __boot_serial_force_puthex64(a0);
    __boot_serial_force_puts(" a1=");
    __boot_serial_force_puthex64(a1);
    __boot_serial_force_puts("\n");
}

static bool compat_syscall_is_wayfire_interesting(uint64_t nr,int64_t rc){
    if(rc<0)return true;
    switch(nr){
        case 0:   /* read */
        case 1:   /* write */
        case 2:   /* open */
        case 3:   /* close */
        case 7:   /* poll */
        case 9:   /* mmap */
        case 10:  /* mprotect */
        case 11:  /* munmap */
        case 16:  /* ioctl */
        case 25:  /* mremap */
        case 28:  /* madvise */
        case 41:  /* socket */
        case 49:  /* bind */
        case 50:  /* listen */
        case 53:  /* socketpair */
        case 56:  /* clone */
        case 59:  /* execve */
        case 60:  /* exit */
        case 61:  /* wait4 */
        case 72:  /* fcntl */
        case 158: /* arch_prctl */
        case 202: /* futex */
        case 217: /* getdents64 */
        case 218: /* set_tid_address */
        case 228: /* clock_gettime */
        case 231: /* exit_group */
        case 232: /* epoll_wait */
        case 233: /* epoll_ctl */
        case 257: /* openat */
        case 262: /* newfstatat */
        case 273: /* set_robust_list */
        case 274: /* get_robust_list */
        case 281: /* epoll_pwait */
        case 283: /* timerfd_create */
        case 286: /* timerfd_settime */
        case 289: /* signalfd4 */
        case 290: /* eventfd2 */
        case 291: /* epoll_create1 */
        case 292: /* dup3 */
        case 293: /* pipe2 */
        case 302: /* prlimit64 */
        case 318: /* getrandom */
        case 319: /* memfd_create */
        case 332: /* statx */
        case 334: /* rseq */
        case 441: /* epoll_pwait2 */
            return true;
        default:
            return false;
    }
}

static bool compat_wayfire_debug_trace_enabled(void);

static void compat_syscall_trace_wayfire_force(uint64_t nr,uint64_t a0,uint64_t a1,int64_t rc){
    static uint32_t wayfire_sys_trace_count;
    task_t *cur=task_current();
    bool early=wayfire_sys_trace_count<220u;
    if(!compat_wayfire_debug_trace_enabled())return;
    if(wayfire_sys_trace_count>=900u)return;
    if(!compat_sys_task_is_wayfire(cur))return;
    if(!early&&!compat_syscall_is_wayfire_interesting(nr,rc))return;
    ++wayfire_sys_trace_count;
    __boot_serial_force_puts("[wsys!] pid=");
    __boot_serial_force_putu32((uint32_t)cur->pid);
    if(cur->name[0]){
        __boot_serial_force_puts(" name=");
        __boot_serial_force_puts(cur->name);
    }
    __boot_serial_force_puts(" nr=");
    __boot_serial_force_putu32((uint32_t)nr);
    __boot_serial_force_puts(" rc=");
    if(rc<0){
        __boot_serial_force_puts("-");
        __boot_serial_force_putu32((uint32_t)(-rc));
    }else{
        __boot_serial_force_puthex64((uint64_t)rc);
    }
    __boot_serial_force_puts(" a0=");
    __boot_serial_force_puthex64(a0);
    __boot_serial_force_puts(" a1=");
    __boot_serial_force_puthex64(a1);
    __boot_serial_force_puts("\n");
}

static bool compat_syscall_trace_is_ridux_ui(uint64_t nr){
    return nr>=500u&&nr<=505u;
}

static bool compat_ridux_native_debug_trace_enabled(void){
    static int cached=-1;
    if(cached<0)cached=kvfs_exists("/etc/ridux-native-debug.enable")?1:0;
    return cached!=0;
}

static void compat_syscall_trace_ridux_force(const char *tag,uint64_t nr,uint64_t a0,uint64_t a1,int64_t rc){
    static uint32_t ridux_sys_trace_count;
    task_t *cur=task_current();
    if(!compat_syscall_trace_is_ridux_ui(nr))return;
    if(!compat_ridux_native_debug_trace_enabled())return;
    if(ridux_sys_trace_count>=96u)return;
    ++ridux_sys_trace_count;
    __boot_serial_force_puts(tag);
    __boot_serial_force_puts(" #");
    __boot_serial_force_putu32(ridux_sys_trace_count);
    __boot_serial_force_puts(" pid=");
    __boot_serial_force_putu32((uint32_t)(cur?cur->pid:0));
    if(cur&&cur->name[0]){
        __boot_serial_force_puts(" name=");
        __boot_serial_force_puts(cur->name);
    }
    __boot_serial_force_puts(" nr=");
    __boot_serial_force_putu32((uint32_t)nr);
    __boot_serial_force_puts(" rc=");
    if(rc==COMPAT_SYSCALL_PENDING_RC){
        __boot_serial_force_puts("pending");
    }else if(rc<0){
        __boot_serial_force_puts("-");
        __boot_serial_force_putu32((uint32_t)(-rc));
    }else{
        __boot_serial_force_puthex64((uint64_t)rc);
    }
    __boot_serial_force_puts(" a0=");
    __boot_serial_force_puthex64(a0);
    __boot_serial_force_puts(" a1=");
    __boot_serial_force_puthex64(a1);
    __boot_serial_force_puts("\n");
}

static bool compat_wayfire_debug_trace_enabled(void){
    static int cached=-1;
    if(cached<0)cached=kvfs_exists("/etc/ridux-wayfire-debug.enable")?1:0;
    return cached!=0;
}

int64_t syscall_dispatch(uint64_t nr,uint64_t a0,uint64_t a1,uint64_t a2,uint64_t a3,uint64_t a4,uint64_t a5) {
    int64_t rc;
    task_t *post_keymap_trace_task;
    task_t *cur_for_hypr_trace;
    bool trace_hypr_sys=false;
    bool is_futex = (nr == 202);
    bool do_trace = compat_wayfire_debug_trace_enabled() &&
                    (g_boot_syscall_trace_count < BOOT_SYSCALL_TRACE_MAX) &&
                    (!is_futex || g_boot_futex_trace_count < BOOT_FUTEX_TRACE_MAX) &&
                    !compat_syscall_trace_is_ridux_ui(nr);
    bool trace_ipc_loop = (nr==7||nr==43||nr==48||nr==213||nr==232||nr==233||
                           nr==270||nr==271||nr==281||nr==288||nr==291||nr==441);
    task_capture_syscall_user_context();
    post_keymap_trace_task=task_current();
    cur_for_hypr_trace=post_keymap_trace_task;
    if(compat_wayfire_debug_trace_enabled()&&
       cur_for_hypr_trace&&compat_task_name_contains(cur_for_hypr_trace,"Hyprland")){
        static uint32_t hypr_exec_ioctl_count;
        static uint32_t hypr_post_exec_sys_count;
        if(nr==16&&((uint32_t)a1&0xFFu)==66u)
            ++hypr_exec_ioctl_count;
        if(hypr_exec_ioctl_count>=1u&&hypr_post_exec_sys_count<128u){
            ++hypr_post_exec_sys_count;
            trace_hypr_sys=true;
            __boot_serial_force_puts("[hypr-sys>] #");
            __boot_serial_force_putu32(hypr_post_exec_sys_count);
            __boot_serial_force_puts(" pid=");
            __boot_serial_force_putu32((uint32_t)cur_for_hypr_trace->pid);
            __boot_serial_force_puts(" nr=");
            __boot_serial_force_putu32((uint32_t)nr);
            __boot_serial_force_puts(" a0=");
            __boot_serial_force_puthex64(a0);
            __boot_serial_force_puts(" a1=");
            __boot_serial_force_puthex64(a1);
            __boot_serial_force_puts(" a2=");
            __boot_serial_force_puthex64(a2);
            __boot_serial_force_puts("\n");
        }
    }
    if (is_futex && g_boot_futex_trace_count < 0xFFFFFFFFu) ++g_boot_futex_trace_count;
    if (do_trace) {
        ++g_boot_syscall_trace_count;
        __boot_serial_puts("[sys #");
        __boot_serial_putu32(g_boot_syscall_trace_count);
        __boot_serial_puts("] nr=");
        __boot_serial_putu32((uint32_t)nr);
        __boot_serial_puts(" a0=");
        __boot_serial_puthex64(a0);
        __boot_serial_puts(" a1=");
        __boot_serial_puthex64(a1);
        __boot_serial_puts(" ...\n");
    }
    if(nr>=SYSCALL_MAX||!g_syscall_table[nr]){
        compat_syscall_trace_wayfire_after_keymap_print("[wf-postsys>]",post_keymap_trace_task,nr,a0,a1,COMPAT_SYSCALL_PENDING_RC);
        compat_syscall_trace_note(nr,a0,-ENOSYS);
        if (do_trace) {
            __boot_serial_puts("[sys]   -> ENOSYS (nr=");
            __boot_serial_putu32((uint32_t)nr);
            __boot_serial_puts(")\n");
        } else {
            compat_syscall_trace_enosys_print(nr,a0,a1);
        }
        task_browser_coop_yield_after_syscall(nr);
        task_restore_current_user_msrs();
        compat_syscall_trace_browser_force(nr,a0,a1,-ENOSYS);
        compat_syscall_trace_wayfire_force(nr,a0,a1,-ENOSYS);
        compat_syscall_trace_ridux_force("[r3sys<]",nr,a0,a1,-ENOSYS);
        compat_syscall_trace_wayfire_after_keymap_print("[wf-postsys<]",post_keymap_trace_task,nr,a0,a1,-ENOSYS);
        if(trace_ipc_loop&&compat_wayfire_debug_trace_enabled()){
            static uint32_t ipc_sys_trace;
            task_t *cur=task_current();
            if(ipc_sys_trace<160){
                ++ipc_sys_trace;
                __boot_serial_force_puts("[ipc-sys!] #");
                __boot_serial_force_putu32(ipc_sys_trace);
                __boot_serial_force_puts(" pid=");
                __boot_serial_force_putu32((uint32_t)(cur?cur->pid:0));
                if(cur&&cur->name[0]){__boot_serial_force_puts(" name=");__boot_serial_force_puts(cur->name);}
                __boot_serial_force_puts(" nr=");
                __boot_serial_force_putu32((uint32_t)nr);
                __boot_serial_force_puts(" rc=-38 a0=");
                __boot_serial_force_puthex64(a0);
                __boot_serial_force_puts(" a1=");
                __boot_serial_force_puthex64(a1);
                __boot_serial_force_puts("\n");
            }
        }
        return -ENOSYS;
    }
    compat_syscall_trace_wayfire_after_keymap_print("[wf-postsys>]",post_keymap_trace_task,nr,a0,a1,COMPAT_SYSCALL_PENDING_RC);
    compat_syscall_trace_ridux_force("[r3sys>]",nr,a0,a1,COMPAT_SYSCALL_PENDING_RC);
    rc=g_syscall_table[nr](a0,a1,a2,a3,a4,a5);
    if(trace_hypr_sys){
        __boot_serial_force_puts("[hypr-sys<] nr=");
        __boot_serial_force_putu32((uint32_t)nr);
        __boot_serial_force_puts(" rc=");
        if(rc<0){__boot_serial_force_puts("-");__boot_serial_force_putu32((uint32_t)(-rc));}
        else __boot_serial_force_puthex64((uint64_t)rc);
        __boot_serial_force_puts("\n");
    }
    compat_syscall_trace_wayfire_after_keymap_print("[wf-postsys<]",post_keymap_trace_task,nr,a0,a1,rc);
    compat_syscall_trace_ridux_force("[r3sys<]",nr,a0,a1,rc);
    compat_syscall_trace_note(nr,a0,rc);
    if (rc == -ENOSYS && !do_trace) {
        compat_syscall_trace_enosys_print(nr,a0,a1);
    }
    if (do_trace) {
        __boot_serial_puts("[sys]   -> ");
        if (rc < 0) {
            __boot_serial_puts("-");
            __boot_serial_putu32((uint32_t)(-rc));
        } else {
            __boot_serial_puthex64((uint64_t)rc);
        }
        __boot_serial_puts("\n");
    }
    compat_syscall_trace_browser_force(nr,a0,a1,rc);
    compat_syscall_trace_wayfire_force(nr,a0,a1,rc);
    if(trace_ipc_loop&&compat_wayfire_debug_trace_enabled()){
        static uint32_t ipc_sys_trace;
        task_t *cur=task_current();
        if(ipc_sys_trace<240){
            ++ipc_sys_trace;
            __boot_serial_force_puts("[ipc-sys!] #");
            __boot_serial_force_putu32(ipc_sys_trace);
            __boot_serial_force_puts(" pid=");
            __boot_serial_force_putu32((uint32_t)(cur?cur->pid:0));
            if(cur&&cur->name[0]){__boot_serial_force_puts(" name=");__boot_serial_force_puts(cur->name);}
            __boot_serial_force_puts(" nr=");
            __boot_serial_force_putu32((uint32_t)nr);
            __boot_serial_force_puts(" rc=");
            if(rc<0){__boot_serial_force_puts("-");__boot_serial_force_putu32((uint32_t)(-rc));}
            else __boot_serial_force_puthex64((uint64_t)rc);
            __boot_serial_force_puts(" a0=");
            __boot_serial_force_puthex64(a0);
            __boot_serial_force_puts(" a1=");
            __boot_serial_force_puthex64(a1);
            __boot_serial_force_puts("\n");
        }
    }
    task_browser_coop_yield_after_syscall(nr);
    if(trace_hypr_sys){
        __boot_serial_force_puts("[hypr-sys-retmsr>] nr=");
        __boot_serial_force_putu32((uint32_t)nr);
        __boot_serial_force_puts("\n");
    }
    task_restore_current_user_msrs();
    if(trace_hypr_sys){
        uint64_t *sf=task_syscall_user_frame(cur_for_hypr_trace);
        __boot_serial_force_puts("[hypr-sys-return] nr=");
        __boot_serial_force_putu32((uint32_t)nr);
        if(sf){
            __boot_serial_force_puts(" rip=");
            __boot_serial_force_puthex64(sf[17]);
            __boot_serial_force_puts(" rflags=");
            __boot_serial_force_puthex64(sf[19]);
            __boot_serial_force_puts(" rsp=");
            __boot_serial_force_puthex64(sf[20]);
        }
        __boot_serial_force_puts("\n");
    }
    return rc;
}

/* VMM / paging stubs + mmap/brk */
vmm_context_t g_kernel_vmm;
static mmap_region_t g_mmap_regions[MMAP_MAX_REGIONS];

void vmm_init(uint64_t mem_end){c_memset(&g_kernel_vmm,0,sizeof(g_kernel_vmm));
    g_kernel_vmm.total_pages=(uint32_t)(mem_end/PAGE_SIZE);g_kernel_vmm.free_pages=g_kernel_vmm.total_pages;
    c_memset(g_mmap_regions,0,sizeof(g_mmap_regions));}
uint64_t vmm_alloc_page(void){if(g_kernel_vmm.free_pages==0)return 0;--g_kernel_vmm.free_pages;return 0x200000+(uint64_t)(g_kernel_vmm.total_pages-g_kernel_vmm.free_pages)*PAGE_SIZE;}
void vmm_free_page(uint64_t p){(void)p;if(g_kernel_vmm.free_pages<g_kernel_vmm.total_pages)++g_kernel_vmm.free_pages;}
bool vmm_map_page(vmm_context_t *c,uint64_t v,uint64_t p,uint64_t fl){(void)c;(void)v;(void)p;(void)fl;return true;}
bool vmm_unmap_page(vmm_context_t *c,uint64_t v){(void)c;(void)v;return true;}
uint64_t vmm_virt_to_phys(vmm_context_t *c,uint64_t v){(void)c;return v;}

int64_t sys_mmap(uint64_t addr,uint64_t len,int prot,int flags,int fd,uint64_t off){
    int i;(void)addr;(void)prot;(void)fd;(void)off;
    for(i=0;i<MMAP_MAX_REGIONS;++i)if(!g_mmap_regions[i].used){
        g_mmap_regions[i].used=true;g_mmap_regions[i].virt=0x40000000ULL+(uint64_t)i*0x100000ULL;
        g_mmap_regions[i].size=len;g_mmap_regions[i].prot=prot;g_mmap_regions[i].flags=flags;
        g_mmap_regions[i].fd=fd;g_mmap_regions[i].offset=off;
        return(int64_t)g_mmap_regions[i].virt;}
    return -ENOMEM;}
int64_t sys_munmap(uint64_t addr,uint64_t len){int i;(void)len;
    for(i=0;i<MMAP_MAX_REGIONS;++i)if(g_mmap_regions[i].used&&g_mmap_regions[i].virt==addr){g_mmap_regions[i].used=false;return 0;}
    return -EINVAL;}
int64_t sys_mprotect(uint64_t addr,uint64_t len,int prot){int i;(void)len;
    for(i=0;i<MMAP_MAX_REGIONS;++i)if(g_mmap_regions[i].used&&g_mmap_regions[i].virt==addr){g_mmap_regions[i].prot=prot;return 0;}
    return 0;}
int64_t sys_brk(uint64_t addr){
    static uint64_t brk_cur=0x800000ULL;
    if(!addr)return(int64_t)brk_cur;
    if(addr>=0x400000ULL&&addr<0x40000000ULL)brk_cur=addr;
    return(int64_t)brk_cur;}

/* ELF64 loader */
elf64_image_t g_elf64_images[ELF64_MAX_IMAGES];

bool elf64_validate(const uint8_t *d,uint32_t sz){
    if(sz<sizeof(elf64_ehdr_t))return false;
    if(d[0]!=0x7F||d[1]!='E'||d[2]!='L'||d[3]!='F')return false;
    if(d[4]!=2)return false; /* 64-bit */
    if(d[5]!=1)return false; /* little endian */
    return true;}

int elf64_load(const uint8_t *data,uint32_t sz,const char *name){
    const elf64_ehdr_t *eh=(const elf64_ehdr_t*)data;
    int slot=-1,i;
    if(!elf64_validate(data,sz))return -1;
    if(eh->e_machine!=EM_X86_64)return -1;
    if(eh->e_type!=ET_EXEC&&eh->e_type!=ET_DYN)return -1;
    for(i=0;i<ELF64_MAX_IMAGES;++i)if(!g_elf64_images[i].used){slot=i;break;}
    if(slot<0)return -1;
    c_memset(&g_elf64_images[slot],0,sizeof(g_elf64_images[slot]));
    g_elf64_images[slot].used=true;
    c_strlcpy(g_elf64_images[slot].path,name,128);
    g_elf64_images[slot].entry=eh->e_entry;
    g_elf64_images[slot].phdr_count=eh->e_phnum;
    g_elf64_images[slot].is_dynamic=(eh->e_type==ET_DYN);
    /* Parse program headers */
    for(i=0;i<(int)eh->e_phnum;++i){
        uint64_t off=eh->e_phoff+(uint64_t)i*eh->e_phentsize;
        if(off+sizeof(elf64_phdr_t)>sz)break;
        const elf64_phdr_t *ph=(const elf64_phdr_t*)(data+off);
        if(ph->p_type==PT_LOAD){
            if(!g_elf64_images[slot].base_vaddr||ph->p_vaddr<g_elf64_images[slot].base_vaddr)
                g_elf64_images[slot].base_vaddr=ph->p_vaddr;
            uint64_t end=ph->p_vaddr+ph->p_memsz;
            if(end>g_elf64_images[slot].load_end)g_elf64_images[slot].load_end=end;
        }
        if(ph->p_type==PT_INTERP&&ph->p_offset+ph->p_filesz<=sz){
            size_t il=ph->p_filesz;if(il>127)il=127;
            c_memcpy(g_elf64_images[slot].interp,data+ph->p_offset,il);
            g_elf64_images[slot].interp[il]=0;
        }
    }
    g_elf64_images[slot].brk=g_elf64_images[slot].load_end;
    return slot;}

/* Threading / synchronization */
thread_t g_threads[THREAD_MAX];
static int g_next_tid=1;

void thread_init(void){c_memset(g_threads,0,sizeof(g_threads));g_next_tid=1;
    /* Thread 0 = kernel main */
    g_threads[0].used=true;g_threads[0].tid=0;g_threads[0].pid=0;g_threads[0].state=THREAD_RUNNING;}
int thread_create(int pid,uint64_t entry,uint64_t arg){int i;(void)arg;
    for(i=1;i<THREAD_MAX;++i)if(!g_threads[i].used){
        g_threads[i].used=true;g_threads[i].tid=g_next_tid++;g_threads[i].pid=pid;
        g_threads[i].state=THREAD_READY;g_threads[i].rip=entry;g_threads[i].priority=10;return g_threads[i].tid;}
    return -EAGAIN;}
void thread_exit(int tid,int code){int i;(void)code;for(i=0;i<THREAD_MAX;++i)if(g_threads[i].used&&g_threads[i].tid==tid){g_threads[i].state=THREAD_ZOMBIE;break;}}
void thread_yield(void){}
int thread_join(int tid){int i;for(i=0;i<THREAD_MAX;++i)if(g_threads[i].used&&g_threads[i].tid==tid){
    if(g_threads[i].state==THREAD_ZOMBIE){g_threads[i].used=false;return 0;}return -EAGAIN;}return -ESRCH;}

void mutex_init(mutex_t *m,int type){m->lock=0;m->owner_tid=-1;m->type=type;m->count=0;}
void mutex_lock(mutex_t *m){while(__sync_lock_test_and_set(&m->lock,1))thread_yield();m->owner_tid=0;m->count++;}
void mutex_unlock(mutex_t *m){m->count--;if(m->count<=0){m->owner_tid=-1;__sync_lock_release(&m->lock);}}
bool mutex_trylock(mutex_t *m){if(__sync_lock_test_and_set(&m->lock,1))return false;m->owner_tid=0;m->count++;return true;}

void condvar_init(condvar_t *cv){cv->waiters=0;cv->mutex=0;}
void condvar_wait(condvar_t *cv,mutex_t *m){cv->mutex=m;cv->waiters++;mutex_unlock(m);thread_yield();mutex_lock(m);cv->waiters--;}
void condvar_signal(condvar_t *cv){if(cv->waiters>0)cv->waiters--;}
void condvar_broadcast(condvar_t *cv){cv->waiters=0;}

void sem_init(semaphore_t *s,int v){s->value=v;}
void sem_wait(semaphore_t *s){while(s->value<=0)thread_yield();__sync_fetch_and_sub(&s->value,1);}
void sem_post(semaphore_t *s){__sync_fetch_and_add(&s->value,1);}

int64_t sys_futex(uint32_t *uaddr,int op,uint32_t val,uint64_t timeout){(void)timeout;
    if(op==FUTEX_WAIT){if(*uaddr==val)thread_yield();return 0;}
    if(op==FUTEX_WAKE){return(int64_t)val;}return -ENOSYS;}

/* /proc /sys /dev virtual generators */
vdev_entry_t g_vdevs[VDEV_MAX]; int g_vdev_count;

void vdev_init(void){
    g_vdev_count=0;c_memset(g_vdevs,0,sizeof(g_vdevs));
    #define VDEV(n,t,maj,min) {vdev_entry_t *v=&g_vdevs[g_vdev_count++];v->present=true;c_strlcpy(v->name,n,32);c_strlcpy(v->type,t,16);v->major=maj;v->minor=min;}
    VDEV("null","char",1,3) VDEV("zero","char",1,5) VDEV("random","char",1,8) VDEV("urandom","char",1,9)
    VDEV("tty","char",5,0) VDEV("console","char",5,1) VDEV("tty0","char",4,0) VDEV("tty1","char",4,1)
    VDEV("fb0","char",29,0) VDEV("input/event0","char",13,64) VDEV("input/event1","char",13,65)
    VDEV("input/mice","char",13,63) VDEV("sda","block",8,0) VDEV("sda1","block",8,1)
    VDEV("sr0","block",11,0) VDEV("loop0","block",7,0)
    VDEV("snd/timer","char",116,33) VDEV("snd/controlC0","char",116,0) VDEV("snd/pcmC0D0p","char",116,16)
    VDEV("dri/card0","char",226,0) VDEV("dri/renderD128","char",226,128)
    VDEV("ptmx","char",5,2) VDEV("pts/0","char",136,0)
    VDEV("shm","misc",0,0) VDEV("net/tun","char",10,200)
    #undef VDEV
}

int vdev_generate_proc_stat(char *b,int mx){size_t l=0;b[0]=0;
    c_append_str(b,&l,(size_t)mx,"cpu  1024 32 512 4096 128 64 16 0 0 0\n");
    c_append_str(b,&l,(size_t)mx,"cpu0 1024 32 512 4096 128 64 16 0 0 0\n");
    c_append_str(b,&l,(size_t)mx,"intr 8192 128 64 0 0 0 0 0 0 0 32 0 256 0 0 0 512\n");
    c_append_str(b,&l,(size_t)mx,"ctxt 65536\nprocesses 128\nprocs_running 1\nprocs_blocked 0\n");return(int)l;}
int vdev_generate_proc_meminfo(char *b,int mx){size_t l=0;b[0]=0;
    c_append_str(b,&l,(size_t)mx,"MemTotal:      131072 kB\nMemFree:        65536 kB\n");
    c_append_str(b,&l,(size_t)mx,"MemAvailable:   98304 kB\nBuffers:         8192 kB\n");
    c_append_str(b,&l,(size_t)mx,"Cached:         16384 kB\nSwapTotal:          0 kB\nSwapFree:           0 kB\n");return(int)l;}
int vdev_generate_proc_cpuinfo(char *b,int mx){size_t l=0;b[0]=0;
    c_append_str(b,&l,(size_t)mx,"processor\t: 0\nvendor_id\t: RiduxCPU\n");
    c_append_str(b,&l,(size_t)mx,"cpu family\t: 6\nmodel\t\t: 1\nmodel name\t: RiduxOS Virtual CPU x86_64\n");
    c_append_str(b,&l,(size_t)mx,"stepping\t: 0\ncpu MHz\t\t: 3000.000\ncache size\t: 4096 KB\n");
    c_append_str(b,&l,(size_t)mx,"physical id\t: 0\ncpu cores\t: 1\n");
    c_append_str(b,&l,(size_t)mx,"flags\t\t: fpu vme de pse tsc msr pae mce cx8 apic sep pge cmov pat clflush mmx fxsr sse sse2 syscall nx lm\n");
    c_append_str(b,&l,(size_t)mx,"bogomips\t: 6000.00\nclflush size\t: 64\n\n");return(int)l;}
int vdev_generate_proc_version(char *b,int mx){size_t l=0;b[0]=0;
    c_append_str(b,&l,(size_t)mx,"RiduxOS version 1.0.0-ridux (root@ridux) (gcc 13.2.0) #1 SMP x86_64\n");return(int)l;}
int vdev_generate_proc_uptime(char *b,int mx){size_t l=0;b[0]=0;
    c_append_str(b,&l,(size_t)mx,"12345.67 12000.00\n");return(int)l;}
int vdev_generate_proc_mounts(char *b,int mx){size_t l=0;int i;b[0]=0;
    c_append_str(b,&l,(size_t)mx,"rootfs / rootfs rw 0 0\n");
    c_append_str(b,&l,(size_t)mx,"proc /proc proc rw,nosuid,nodev,noexec 0 0\n");
    c_append_str(b,&l,(size_t)mx,"sysfs /sys sysfs rw,nosuid,nodev,noexec 0 0\n");
    c_append_str(b,&l,(size_t)mx,"devtmpfs /dev devtmpfs rw,nosuid 0 0\n");
    c_append_str(b,&l,(size_t)mx,"tmpfs /tmp tmpfs rw,nosuid,nodev 0 0\n");
    for(i=0;i<g_mount_count;++i){if(!g_mounts[i].used)continue;
        c_append_str(b,&l,(size_t)mx,g_mounts[i].source);c_append_ch(b,&l,(size_t)mx,' ');
        c_append_str(b,&l,(size_t)mx,g_mounts[i].target);c_append_ch(b,&l,(size_t)mx,' ');
        c_append_str(b,&l,(size_t)mx,g_mounts[i].fstype);c_append_str(b,&l,(size_t)mx," rw 0 0\n");}
    return(int)l;}
int vdev_generate_proc_net_dev(char *b,int mx){size_t l=0;int i;b[0]=0;
    c_append_str(b,&l,(size_t)mx,"Inter-|   Receive                                                |  Transmit\n");
    c_append_str(b,&l,(size_t)mx," face |bytes    packets errs drop fifo frame compressed multicast|bytes    packets errs drop fifo colls carrier compressed\n");
    for(i=0;i<g_net_iface_count;++i){
        c_append_str(b,&l,(size_t)mx,"  ");c_append_str(b,&l,(size_t)mx,g_net_ifaces[i].name);
        c_append_str(b,&l,(size_t)mx,": ");c_append_u64(b,&l,(size_t)mx,g_net_ifaces[i].rx_bytes);
        c_append_ch(b,&l,(size_t)mx,' ');c_append_u64(b,&l,(size_t)mx,g_net_ifaces[i].rx_packets);
        c_append_str(b,&l,(size_t)mx," 0 0 0 0 0 0 ");
        c_append_u64(b,&l,(size_t)mx,g_net_ifaces[i].tx_bytes);c_append_ch(b,&l,(size_t)mx,' ');
        c_append_u64(b,&l,(size_t)mx,g_net_ifaces[i].tx_packets);c_append_str(b,&l,(size_t)mx," 0 0 0 0 0 0\n");}
    return(int)l;}
int vdev_generate_sys_kernel_hostname(char *b,int mx){size_t l=0;b[0]=0;c_append_str(b,&l,(size_t)mx,"ridux\n");return(int)l;}
int vdev_generate_sys_kernel_osrelease(char *b,int mx){size_t l=0;b[0]=0;c_append_str(b,&l,(size_t)mx,"1.0.0-ridux\n");return(int)l;}

/* Framebuffer, input and sound device state used by the Linux ABI. */
drm_mode_t g_drm_mode;
evdev_device_t g_evdev_kbd;
evdev_device_t g_evdev_mouse;
static uint64_t g_evdev_time_usec;
static uint64_t g_evdev_boot_tsc;

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
} compat_fb_t;
extern compat_fb_t g_fb;

void drm_init(void){
    c_memset(&g_drm_mode,0,sizeof(g_drm_mode));
    if(g_fb.ready&&g_fb.width&&g_fb.height){
        g_drm_mode.width=g_fb.width;
        g_drm_mode.height=g_fb.height;
        g_drm_mode.pitch=g_fb.pitch;
        g_drm_mode.bpp=g_fb.bpp?g_fb.bpp:32;
        g_drm_mode.fb_phys=(uint64_t)(uintptr_t)g_fb.address;
        g_drm_mode.fb_size=g_fb.pitch*g_fb.height;
    }else{
        g_drm_mode.width=1024;
        g_drm_mode.height=768;
        g_drm_mode.pitch=1024u*4u;
        g_drm_mode.bpp=32;
        g_drm_mode.fb_phys=0;
        g_drm_mode.fb_size=g_drm_mode.pitch*g_drm_mode.height;
    }
}
static inline uint64_t evdev_rdtsc(void){
    uint32_t lo,hi;
    __asm__ volatile("rdtsc":"=a"(lo),"=d"(hi));
    return((uint64_t)hi<<32)|lo;
}
static uint64_t evdev_now_usec(void){
    timespec_t ts;
    if(real_sys_clock_gettime(1,&ts)==0&&ts.tv_sec>=0&&ts.tv_nsec>=0)
        return(uint64_t)ts.tv_sec*1000000ULL+(uint64_t)ts.tv_nsec/1000ULL;
    {
        uint64_t now=evdev_rdtsc();
        uint64_t delta;
        if(!g_evdev_boot_tsc)g_evdev_boot_tsc=now;
        delta=now-g_evdev_boot_tsc;
        return(delta*1000000ULL)/3000000000ULL;
    }
}
void evdev_init(void){
    c_memset(&g_evdev_kbd,0,sizeof(g_evdev_kbd));
    c_memset(&g_evdev_mouse,0,sizeof(g_evdev_mouse));
    g_evdev_time_usec=0;
    g_evdev_boot_tsc=evdev_rdtsc();
}
static bool evdev_ref_matches(int got,int want){
    if(want<0)return true;
    if(got==want)return true;
    return want==1&&got==2;
}

static bool evdev_task_fd_waits_input_ref(const task_t *t,int fd,int depth,int ref){
    real_fd_t fe;
    int i;
    if(!t||fd<0||fd>=TASK_FD_MAX)return false;
    fe=t->fdt.fds[fd];
    if(fe.kind==FDKIND_DEVINPUT)return evdev_ref_matches(fe.ref,ref);
    if(fe.kind!=FDKIND_EPOLL||depth>=4)return false;
    if(fe.ref<0||fe.ref>=EPOLL_INSTANCES||!g_epoll_instances[fe.ref].used)return false;
    for(i=0;i<g_epoll_instances[fe.ref].count;++i){
        epoll_item_t *it=&g_epoll_instances[fe.ref].items[i];
        if(!it->used)continue;
        if(evdev_task_fd_waits_input_ref(t,it->fd,depth+1,ref))return true;
    }
    return false;
}

static bool evdev_task_waits_input_ref(const task_t *t,int ref){
    int fd;
    if(!t||!t->used)return false;
    if(t->state==TASK_FREE||t->state==TASK_ZOMBIE)return false;
    for(fd=0;fd<TASK_FD_MAX;++fd)
        if(evdev_task_fd_waits_input_ref(t,fd,0,ref))return true;
    return false;
}

static int evdev_device_ref(const evdev_device_t *d){
    if(d==&g_evdev_mouse)return 1;
    return 0;
}

static void evdev_wake_input_sleepers(int ref){
    address_space_t *owner_as[TASK_MAX];
    int owner_tgid[TASK_MAX];
    int as_count=0,tgid_count=0;
    int ti,i;
    uint32_t woke=0;
    bool prefer_wayfire=false;
    for(ti=0;ti<TASK_MAX;++ti){
        task_t *t=&g_tasks[ti];
        if(!evdev_task_waits_input_ref(t,ref))continue;
        if(compat_sys_task_is_wayfire(t)){prefer_wayfire=true;break;}
    }
    for(ti=0;ti<TASK_MAX;++ti){
        task_t *t=&g_tasks[ti];
        int tgid;
        bool seen;
        if(!evdev_task_waits_input_ref(t,ref))continue;
        if(prefer_wayfire&&!compat_sys_task_is_wayfire(t))continue;
        if(t->addr_space){
            seen=false;
            for(i=0;i<as_count;++i)if(owner_as[i]==t->addr_space){seen=true;break;}
            if(!seen&&as_count<TASK_MAX)owner_as[as_count++]=t->addr_space;
        }
        tgid=t->tgid?t->tgid:t->pid;
        if(tgid>0){
            seen=false;
            for(i=0;i<tgid_count;++i)if(owner_tgid[i]==tgid){seen=true;break;}
            if(!seen&&tgid_count<TASK_MAX)owner_tgid[tgid_count++]=tgid;
        }
    }
    for(ti=0;ti<TASK_MAX;++ti){
        task_t *t=&g_tasks[ti];
        bool related=false;
        int tgid;
        if(!t->used||t->state!=TASK_SLEEPING)continue;
        if(evdev_task_waits_input_ref(t,ref)&&
           (!prefer_wayfire||compat_sys_task_is_wayfire(t)))related=true;
        if(!related&&prefer_wayfire&&compat_sys_task_is_wayfire(t))related=true;
        if(!related&&t->addr_space){
            for(i=0;i<as_count;++i){
                if(owner_as[i]==t->addr_space){related=true;break;}
            }
        }
        tgid=t->tgid?t->tgid:t->pid;
        if(!related&&tgid>0){
            for(i=0;i<tgid_count;++i){
                if(owner_tgid[i]==tgid){related=true;break;}
            }
        }
        if(!related)continue;
        if(task_make_runnable(ti)){
            ++woke;
#if 0
            static uint32_t evdev_wake_trace;
            if(evdev_wake_trace<128u){
                ++evdev_wake_trace;
                __boot_serial_force_puts("[evdev-wake!] #");
                __boot_serial_force_putu32(evdev_wake_trace);
                __boot_serial_force_puts(" pid=");
                __boot_serial_force_putu32((uint32_t)t->pid);
                if(t->name[0]){
                    __boot_serial_force_puts(" name=");
                    __boot_serial_force_puts(t->name);
                }
                __boot_serial_force_puts(" tgid=");
                __boot_serial_force_putu32((uint32_t)tgid);
                __boot_serial_force_puts(" owners_as=");
                __boot_serial_force_putu32((uint32_t)as_count);
                __boot_serial_force_puts(" owners_tgid=");
                __boot_serial_force_putu32((uint32_t)tgid_count);
                __boot_serial_force_puts("\n");
            }
#endif
        }
    }
#if 0
    {
        static uint32_t evdev_wake_summary_trace;
        if(evdev_wake_summary_trace<64u){
            ++evdev_wake_summary_trace;
            __boot_serial_force_puts("[evdev-wake-sum!] #");
            __boot_serial_force_putu32(evdev_wake_summary_trace);
            __boot_serial_force_puts(" owners_as=");
            __boot_serial_force_putu32((uint32_t)as_count);
            __boot_serial_force_puts(" owners_tgid=");
            __boot_serial_force_putu32((uint32_t)tgid_count);
            __boot_serial_force_puts(" woke=");
            __boot_serial_force_putu32(woke);
            __boot_serial_force_puts("\n");
        }
    }
#endif
    (void)woke;
}
#if 0
static void evdev_trace_i32(int32_t v){
    if(v<0){
        __boot_serial_force_puts("-");
        __boot_serial_force_putu32((uint32_t)(-v));
    }else{
        __boot_serial_force_putu32((uint32_t)v);
    }
}
#endif
static void evdev_push_event_raw(evdev_device_t *d,uint16_t type,uint16_t code,int32_t value,bool wake){
    int next;
    uint64_t usec;
    if(!d)return;
    next=(d->head+1)%INPUT_EVENT_RING;
    if(next==d->tail)return;
    usec=evdev_now_usec();
    if(usec<=g_evdev_time_usec)usec=g_evdev_time_usec+1ULL;
    g_evdev_time_usec=usec;
    d->events[d->head].time_sec=usec/1000000ULL;
    d->events[d->head].time_usec=usec%1000000ULL;
    d->events[d->head].type=type;
    d->events[d->head].code=code;
    d->events[d->head].value=value;
    d->head=next;
#if 0
    if(type==EV_KEY||type==EV_REL){
        static uint32_t evdev_push_trace;
        if(evdev_push_trace<96u){
            ++evdev_push_trace;
            __boot_serial_force_puts("[evdev-push!] #");
            __boot_serial_force_putu32(evdev_push_trace);
            __boot_serial_force_puts(d==&g_evdev_mouse?" dev=mouse":" dev=kbd");
            __boot_serial_force_puts(" type=");
            __boot_serial_force_putu32((uint32_t)type);
            __boot_serial_force_puts(" code=");
            __boot_serial_force_putu32((uint32_t)code);
            __boot_serial_force_puts(" val=");
            evdev_trace_i32(value);
            __boot_serial_force_puts(" head=");
            __boot_serial_force_putu32((uint32_t)d->head);
            __boot_serial_force_puts(" tail=");
            __boot_serial_force_putu32((uint32_t)d->tail);
            __boot_serial_force_puts("\n");
        }
    }
#endif
    if(wake)evdev_wake_input_sleepers(evdev_device_ref(d));
}
void evdev_push_key(uint16_t code,int32_t value){
    evdev_push_event_raw(&g_evdev_kbd,EV_KEY,code,value,false);
    evdev_push_event_raw(&g_evdev_kbd,EV_SYN,SYN_REPORT,0,true);
}
void evdev_push_mouse_key(uint16_t code,int32_t value){
    evdev_push_event_raw(&g_evdev_mouse,EV_KEY,code,value,false);
    evdev_push_event_raw(&g_evdev_mouse,EV_SYN,SYN_REPORT,0,true);
}
void evdev_push_rel(uint16_t code,int32_t value){
    evdev_push_event_raw(&g_evdev_mouse,EV_REL,code,value,false);
    evdev_push_event_raw(&g_evdev_mouse,EV_SYN,SYN_REPORT,0,true);
}
void evdev_push_rel_xy(int32_t dx,int32_t dy){
    if(dx)evdev_push_event_raw(&g_evdev_mouse,EV_REL,0,(int32_t)dx,false);
    if(dy)evdev_push_event_raw(&g_evdev_mouse,EV_REL,1,(int32_t)dy,false);
    evdev_push_event_raw(&g_evdev_mouse,EV_SYN,SYN_REPORT,0,true);
}

/* epoll/poll/select stubs */
epoll_instance_t g_epoll_instances[EPOLL_INSTANCES];

int sys_epoll_create(int sz){int i;(void)sz;for(i=0;i<EPOLL_INSTANCES;++i)if(!g_epoll_instances[i].used){
    c_memset(&g_epoll_instances[i],0,sizeof(g_epoll_instances[i]));g_epoll_instances[i].used=true;return i+1000;}return -ENOMEM;}
int sys_epoll_ctl(int epfd,int op,int fd,epoll_event_t *ev){int idx=epfd-1000;
    if(idx<0||idx>=EPOLL_INSTANCES||!g_epoll_instances[idx].used)return -EBADF;
    if(op==EPOLL_CTL_ADD&&g_epoll_instances[idx].count<EPOLL_MAX_EVENTS){
        epoll_item_t *it=&g_epoll_instances[idx].items[g_epoll_instances[idx].count++];
        it->used=true;it->fd=fd;it->events=ev?ev->events:0;it->data=ev?ev->data:0;return 0;}
    if(op==EPOLL_CTL_DEL){int i;for(i=0;i<g_epoll_instances[idx].count;++i)
        if(g_epoll_instances[idx].items[i].fd==fd){g_epoll_instances[idx].items[i].used=false;return 0;}}
    return -EINVAL;}
int sys_epoll_wait(int epfd,epoll_event_t *ev,int max,int timeout){(void)epfd;(void)ev;(void)max;(void)timeout;return 0;}
int sys_poll(void *fds,uint64_t nfds,int timeout){(void)fds;(void)nfds;(void)timeout;return 0;}
int sys_select(int nfds,void *r,void *w,void *e,void *t){(void)nfds;(void)r;(void)w;(void)e;(void)t;return 0;}

/* Pipes */
pipe_t g_pipes[PIPE_MAX];
int pipe_create(int fds[2]){int i;for(i=0;i<PIPE_MAX;++i)if(!g_pipes[i].used){
    c_memset(&g_pipes[i],0,sizeof(g_pipes[i]));g_pipes[i].used=true;g_pipes[i].readers=1;g_pipes[i].writers=1;
    fds[0]=i+500;fds[1]=i+600;return 0;}return -ENOMEM;}
int pipe_read(int id,void *buf,size_t cnt){pipe_t *p;size_t av,i;uint8_t *d;
    if(id<0||id>=PIPE_MAX||!g_pipes[id].used)return -EBADF;p=&g_pipes[id];
    av=(size_t)((p->head-p->tail+PIPE_BUF_SIZE)%PIPE_BUF_SIZE);if(!av)return 0;
    if(cnt>av)cnt=av;d=(uint8_t*)buf;for(i=0;i<cnt;++i){d[i]=p->buf[p->tail%PIPE_BUF_SIZE];p->tail++;}return(int)cnt;}
int pipe_write(int id,const void *buf,size_t cnt){pipe_t *p;size_t av,i;const uint8_t *s;
    if(id<0||id>=PIPE_MAX||!g_pipes[id].used)return -EBADF;p=&g_pipes[id];
    av=(size_t)(PIPE_BUF_SIZE-((p->head-p->tail+PIPE_BUF_SIZE)%PIPE_BUF_SIZE)-1);
    if(cnt>av)cnt=av;s=(const uint8_t*)buf;for(i=0;i<cnt;++i){p->buf[p->head%PIPE_BUF_SIZE]=s[i];p->head++;}return(int)cnt;}
void pipe_close_read(int id){if(id>=0&&id<PIPE_MAX&&g_pipes[id].used){g_pipes[id].readers--;if(g_pipes[id].readers<=0&&g_pipes[id].writers<=0)g_pipes[id].used=false;}}
void pipe_close_write(int id){if(id>=0&&id<PIPE_MAX&&g_pipes[id].used){g_pipes[id].writers--;if(g_pipes[id].readers<=0&&g_pipes[id].writers<=0)g_pipes[id].used=false;}}

/* Shell commands + compat_init_all */
compat_shell_cmd_t g_compat_cmds[COMPAT_SHELL_CMD_MAX];
int g_compat_cmd_count;

static void cmd_lsblk(const char *a,char *o,int mx){size_t l=0;int i;(void)a;o[0]=0;
    c_append_str(o,&l,(size_t)mx,"NAME  TYPE  SIZE         DRIVER  MODEL\n");
    for(i=0;i<g_block_dev_count;++i){if(!g_block_devs[i].present)continue;
        c_append_str(o,&l,(size_t)mx,g_block_devs[i].name);c_append_str(o,&l,(size_t)mx,"   ");
        c_append_str(o,&l,(size_t)mx,g_block_devs[i].type);c_append_str(o,&l,(size_t)mx,"  ");
        c_append_u64(o,&l,(size_t)mx,g_block_devs[i].total_sectors*g_block_devs[i].sector_sz/1024);
        c_append_str(o,&l,(size_t)mx," KB\n");}
    for(i=0;i<g_ata_disk_count;++i){if(!g_ata_disks[i].present)continue;
        c_append_str(o,&l,(size_t)mx,"  disk");c_append_u32(o,&l,(size_t)mx,(uint32_t)i);
        c_append_str(o,&l,(size_t)mx,"  ");c_append_str(o,&l,(size_t)mx,g_ata_disks[i].model);
        c_append_str(o,&l,(size_t)mx,"  serial=");c_append_str(o,&l,(size_t)mx,g_ata_disks[i].serial);
        c_append_ch(o,&l,(size_t)mx,'\n');}
    if(!g_block_dev_count&&!g_ata_disk_count)c_append_str(o,&l,(size_t)mx,"(no block devices)\n");}

static void cmd_ifconfig(const char *a,char *o,int mx){size_t l=0;int i;(void)a;o[0]=0;
    for(i=0;i<g_net_iface_count;++i){net_iface_t *n=&g_net_ifaces[i];
        c_append_str(o,&l,(size_t)mx,n->name);c_append_str(o,&l,(size_t)mx,": ");
        c_append_str(o,&l,(size_t)mx,n->up?"UP":"DOWN");c_append_str(o,&l,(size_t)mx," driver=");
        c_append_str(o,&l,(size_t)mx,n->driver);c_append_ch(o,&l,(size_t)mx,'\n');
        c_append_str(o,&l,(size_t)mx,"  inet ");c_append_ip4(o,&l,(size_t)mx,n->ip4);
        c_append_str(o,&l,(size_t)mx,"  netmask ");c_append_ip4(o,&l,(size_t)mx,n->netmask);c_append_ch(o,&l,(size_t)mx,'\n');
        c_append_str(o,&l,(size_t)mx,"  HWaddr ");c_append_mac(o,&l,(size_t)mx,n->mac);c_append_ch(o,&l,(size_t)mx,'\n');
        c_append_str(o,&l,(size_t)mx,"  RX bytes=");c_append_u64(o,&l,(size_t)mx,n->rx_bytes);
        c_append_str(o,&l,(size_t)mx," packets=");c_append_u64(o,&l,(size_t)mx,n->rx_packets);c_append_ch(o,&l,(size_t)mx,'\n');
        c_append_str(o,&l,(size_t)mx,"  TX bytes=");c_append_u64(o,&l,(size_t)mx,n->tx_bytes);
        c_append_str(o,&l,(size_t)mx," packets=");c_append_u64(o,&l,(size_t)mx,n->tx_packets);c_append_ch(o,&l,(size_t)mx,'\n');}}

static void cmd_route(const char *a,char *o,int mx){size_t l=0;int i;(void)a;o[0]=0;
    c_append_str(o,&l,(size_t)mx,"Destination     Gateway         Genmask         Iface\n");
    for(i=0;i<g_net_iface_count;++i){if(!g_net_ifaces[i].up)continue;
        c_append_str(o,&l,(size_t)mx,"0.0.0.0         ");c_append_ip4(o,&l,(size_t)mx,g_net_ifaces[i].gateway);
        c_append_str(o,&l,(size_t)mx,"    ");c_append_ip4(o,&l,(size_t)mx,g_net_ifaces[i].netmask);
        c_append_str(o,&l,(size_t)mx,"    ");c_append_str(o,&l,(size_t)mx,g_net_ifaces[i].name);c_append_ch(o,&l,(size_t)mx,'\n');}}

static void cmd_mount(const char *a,char *o,int mx){size_t l=0;(void)a;o[0]=0;
    char buf[2048]; vdev_generate_proc_mounts(buf,2048);c_append_str(o,&l,(size_t)mx,buf);}

static void cmd_lsdev(const char *a,char *o,int mx){size_t l=0;int i;(void)a;o[0]=0;
    c_append_str(o,&l,(size_t)mx,"NAME                TYPE   MAJOR MINOR\n");
    for(i=0;i<g_vdev_count;++i){c_append_str(o,&l,(size_t)mx,"/dev/");c_append_str(o,&l,(size_t)mx,g_vdevs[i].name);
        size_t nl=c_strlen(g_vdevs[i].name);while(nl++<20)c_append_ch(o,&l,(size_t)mx,' ');
        c_append_str(o,&l,(size_t)mx,g_vdevs[i].type);c_append_str(o,&l,(size_t)mx,"  ");
        c_append_u32(o,&l,(size_t)mx,(uint32_t)g_vdevs[i].major);c_append_str(o,&l,(size_t)mx,"    ");
        c_append_u32(o,&l,(size_t)mx,(uint32_t)g_vdevs[i].minor);c_append_ch(o,&l,(size_t)mx,'\n');}}

static void cmd_cat_proc(const char *a,char *o,int mx){size_t l=0;o[0]=0;
    if(!a||!*a){c_append_str(o,&l,(size_t)mx,"usage: catproc <stat|meminfo|cpuinfo|version|uptime|mounts|netdev>\n");return;}
    if(c_strcmp(a,"stat")==0)vdev_generate_proc_stat(o,mx);
    else if(c_strcmp(a,"meminfo")==0)vdev_generate_proc_meminfo(o,mx);
    else if(c_strcmp(a,"cpuinfo")==0)vdev_generate_proc_cpuinfo(o,mx);
    else if(c_strcmp(a,"version")==0)vdev_generate_proc_version(o,mx);
    else if(c_strcmp(a,"uptime")==0)vdev_generate_proc_uptime(o,mx);
    else if(c_strcmp(a,"mounts")==0)vdev_generate_proc_mounts(o,mx);
    else if(c_strcmp(a,"netdev")==0)vdev_generate_proc_net_dev(o,mx);
    else c_append_str(o,&l,(size_t)mx,"catproc: unknown file\n");}

static void cmd_nslookup(const char *a,char *o,int mx){size_t l=0;uint32_t ip;o[0]=0;
    if(!a||!*a){c_append_str(o,&l,(size_t)mx,"usage: nslookup <hostname>\n");return;}
    if(dns_resolve(a,&ip)){c_append_str(o,&l,(size_t)mx,a);c_append_str(o,&l,(size_t)mx," => ");c_append_ip4(o,&l,(size_t)mx,ip);c_append_ch(o,&l,(size_t)mx,'\n');}
    else{c_append_str(o,&l,(size_t)mx,"nslookup: could not resolve ");c_append_str(o,&l,(size_t)mx,a);c_append_ch(o,&l,(size_t)mx,'\n');}}

static void cmd_tcpget(const char *a,char *o,int mx){
    char host[128];
    char path[128];
    char req[384];
    char tmp[512];
    size_t l=0,hi=0,pi=0,rl=0;
    const char *p=a;
    uint32_t ip=0;
    uint32_t gw=0;
    uint8_t gw_mac[6];
    int fd,rc,tries,got=0;
    o[0]=0;host[0]=0;path[0]='/';path[1]=0;
    if(!p||!*p)p="example.com/";
    if(c_strncmp(p,"http://",7)==0)p+=7;
    while(*p&&*p!='/'&&*p!=' '&&hi+1<sizeof(host))host[hi++]=*p++;
    host[hi]=0;
    if(*p=='/'){
        while(*p&&*p!=' '&&pi+1<sizeof(path))path[pi++]=*p++;
        path[pi]=0;
    }
    if(!host[0]){c_append_str(o,&l,(size_t)mx,"tcpget: usage tcpget <host>/<path>\n");return;}
    if(!dns_resolve(host,&ip)){c_append_str(o,&l,(size_t)mx,"tcpget: dns failed\n");return;}
    gw=net_get_gateway_ip(ip);
    c_append_str(o,&l,(size_t)mx,"tcpget: ");c_append_str(o,&l,(size_t)mx,host);
    c_append_str(o,&l,(size_t)mx," -> ");c_append_ip4(o,&l,(size_t)mx,ip);c_append_ch(o,&l,(size_t)mx,'\n');
    fd=sock_create(2,1,0);
    if(fd<0){c_append_str(o,&l,(size_t)mx,"socket failed\n");return;}
    g_sockets[fd].non_blocking=true;
    rc=sock_connect(fd,ip,80);
    c_append_str(o,&l,(size_t)mx,"connect rc=");c_append_i32(o,&l,(size_t)mx,rc);
    c_append_str(o,&l,(size_t)mx," state=");c_append_u32(o,&l,(size_t)mx,(uint32_t)g_sockets[fd].tcp_state);c_append_ch(o,&l,(size_t)mx,'\n');
    for(tries=0;tries<3000&&g_sockets[fd].tcp_state==TCP_SYN_SENT;++tries){
        e1000_poll_rx();
        if((tries&15)==15)tcp7_tick();
        {volatile int spin;for(spin=0;spin<20000;++spin)__asm__ volatile("pause");}
    }
    c_append_str(o,&l,(size_t)mx,"after pump state=");c_append_u32(o,&l,(size_t)mx,(uint32_t)g_sockets[fd].tcp_state);
    c_append_str(o,&l,(size_t)mx," err=");c_append_i32(o,&l,(size_t)mx,g_sockets[fd].error);c_append_ch(o,&l,(size_t)mx,'\n');
    c_append_str(o,&l,(size_t)mx,"gateway ");c_append_ip4(o,&l,(size_t)mx,gw);
    if(arp_resolve(gw,gw_mac)){
        c_append_str(o,&l,(size_t)mx," mac=");c_append_mac(o,&l,(size_t)mx,gw_mac);c_append_ch(o,&l,(size_t)mx,'\n');
    }else{
        c_append_str(o,&l,(size_t)mx," arp=miss\n");
    }
    if(g_sockets[fd].tcp_state!=TCP_ESTABLISHED){sock_close(fd);return;}
    c_append_str(req,&rl,sizeof(req),"GET ");
    c_append_str(req,&rl,sizeof(req),path);
    c_append_str(req,&rl,sizeof(req)," HTTP/1.0\r\nHost: ");
    c_append_str(req,&rl,sizeof(req),host);
    c_append_str(req,&rl,sizeof(req),"\r\nUser-Agent: RiduxOS tcpget\r\nConnection: close\r\n\r\n");
    rc=sock_send(fd,req,rl,0);
    c_append_str(o,&l,(size_t)mx,"send rc=");c_append_i32(o,&l,(size_t)mx,rc);c_append_ch(o,&l,(size_t)mx,'\n');
    for(tries=0;tries<3000&&l+1<(size_t)mx;++tries){
        rc=sock_recv(fd,tmp,sizeof(tmp)-1,0);
        if(rc>0){
            int j;
            tmp[rc]=0;
            for(j=0;j<rc&&l+1<(size_t)mx;++j){
                char ch=tmp[j];
                if(ch=='\r')continue;
                o[l++]=(ch>=' '||ch=='\n'||ch=='\t')?ch:'.';
                o[l]=0;
                if(++got>1200)break;
            }
            if(got>1200)break;
        }else{
            e1000_poll_rx();
            if((tries&15)==15)tcp7_tick();
            {volatile int spin;for(spin=0;spin<20000;++spin)__asm__ volatile("pause");}
        }
    }
    sock_close(fd);
}

static void cmd_dhcp(const char *a,char *o,int mx){size_t l=0;(void)a;o[0]=0;
    dhcp_discover(0);c_append_str(o,&l,(size_t)mx,"DHCP: ip=");c_append_ip4(o,&l,(size_t)mx,g_dhcp_lease.ip);
    c_append_str(o,&l,(size_t)mx," gw=");c_append_ip4(o,&l,(size_t)mx,g_dhcp_lease.gateway);
    c_append_str(o,&l,(size_t)mx," dns=");c_append_ip4(o,&l,(size_t)mx,g_dhcp_lease.dns);c_append_ch(o,&l,(size_t)mx,'\n');}

static void cmd_ahci(const char *a,char *o,int mx){size_t l=0;int i;(void)a;o[0]=0;
    if(!g_ahci_present){c_append_str(o,&l,(size_t)mx,"AHCI: not detected\n");return;}
    c_append_str(o,&l,(size_t)mx,"AHCI ports: ");c_append_u32(o,&l,(size_t)mx,(uint32_t)g_ahci_port_count);c_append_ch(o,&l,(size_t)mx,'\n');
    for(i=0;i<g_ahci_port_count;++i){c_append_str(o,&l,(size_t)mx,"  port ");c_append_u32(o,&l,(size_t)mx,(uint32_t)g_ahci_ports[i].port);
        c_append_str(o,&l,(size_t)mx,g_ahci_ports[i].present?" present":" empty");
        c_append_str(o,&l,(size_t)mx," type=");c_append_u32(o,&l,(size_t)mx,(uint32_t)g_ahci_ports[i].type);c_append_ch(o,&l,(size_t)mx,'\n');}}

static void cmd_syscalls(const char *a,char *o,int mx){size_t l=0;int i,cnt=0;(void)a;o[0]=0;
    for(i=0;i<SYSCALL_MAX;++i)if(g_syscall_table[i])cnt++;
    c_append_str(o,&l,(size_t)mx,"Linux compat syscalls registered: ");c_append_u32(o,&l,(size_t)mx,(uint32_t)cnt);
    c_append_str(o,&l,(size_t)mx,"/");c_append_u32(o,&l,(size_t)mx,SYSCALL_MAX);c_append_ch(o,&l,(size_t)mx,'\n');}

static void cmd_sysmiss(const char *a,char *o,int mx){
    bool picked[SYSCALL_MAX];
    const char *arg=a?a:"";
    size_t l=0;
    int i,row=0,top_n=10;
    uint64_t total_enosys=0;
    o[0]=0;
    while(*arg==' '||*arg=='\t')++arg;
    if(*arg){
        if(c_strcmp(arg,"reset")==0){
            compat_syscall_trace_reset();
            c_append_str(o,&l,(size_t)mx,"sysmiss: trace reset ok\n");
            return;
        }
        if(*arg>='0'&&*arg<='9'){
            int n=0;
            while(*arg>='0'&&*arg<='9'){
                n=n*10+(*arg-'0');
                ++arg;
            }
            if(n>0&&n<=64)top_n=n;
        }
    }
    c_memset(picked,0,sizeof(picked));
    for(i=0;i<SYSCALL_MAX;++i)total_enosys+=g_syscall_enosys_count[i];
    c_append_str(o,&l,(size_t)mx,"sysmiss: top ENOSYS syscalls\n");
    c_append_str(o,&l,(size_t)mx,"total ENOSYS: ");
    c_append_u64(o,&l,(size_t)mx,total_enosys);
    c_append_ch(o,&l,(size_t)mx,'\n');
    if(g_syscall_oob_count){
        c_append_str(o,&l,(size_t)mx,"oob ENOSYS: ");
        c_append_u64(o,&l,(size_t)mx,g_syscall_oob_count);
        c_append_str(o,&l,(size_t)mx," last_nr=");
        c_append_u64(o,&l,(size_t)mx,g_syscall_oob_last_nr);
        c_append_ch(o,&l,(size_t)mx,'\n');
    }
    c_append_str(o,&l,(size_t)mx,"nr  enosys  hits  last_a0\n");
    for(row=0;row<top_n;++row){
        int best=-1;
        uint64_t best_cnt=0;
        for(i=0;i<SYSCALL_MAX;++i){
            if(picked[i])continue;
            if(g_syscall_enosys_count[i]>best_cnt){
                best=i;
                best_cnt=g_syscall_enosys_count[i];
            }
        }
        if(best<0||best_cnt==0)break;
        picked[best]=true;
        c_append_u32(o,&l,(size_t)mx,(uint32_t)best);
        c_append_str(o,&l,(size_t)mx,"  ");
        c_append_u64(o,&l,(size_t)mx,g_syscall_enosys_count[best]);
        c_append_str(o,&l,(size_t)mx,"  ");
        c_append_u64(o,&l,(size_t)mx,g_syscall_hit_count[best]);
        c_append_str(o,&l,(size_t)mx,"  ");
        c_append_u64(o,&l,(size_t)mx,g_syscall_last_a0[best]);
        c_append_ch(o,&l,(size_t)mx,'\n');
    }
    if(row==0)c_append_str(o,&l,(size_t)mx,"(sin ENOSYS todavia; ejecuta chrome/firefox y luego sysmiss)\n");
}

static void cmd_threads(const char *a,char *o,int mx){size_t l=0;int i;(void)a;o[0]=0;
    c_append_str(o,&l,(size_t)mx,"TID  PID  STATE\n");
    for(i=0;i<THREAD_MAX;++i){if(!g_threads[i].used)continue;
        c_append_u32(o,&l,(size_t)mx,(uint32_t)g_threads[i].tid);c_append_str(o,&l,(size_t)mx,"   ");
        c_append_u32(o,&l,(size_t)mx,(uint32_t)g_threads[i].pid);c_append_str(o,&l,(size_t)mx,"   ");
        switch(g_threads[i].state){case THREAD_RUNNING:c_append_str(o,&l,(size_t)mx,"running");break;
            case THREAD_READY:c_append_str(o,&l,(size_t)mx,"ready");break;
            case THREAD_BLOCKED:c_append_str(o,&l,(size_t)mx,"blocked");break;
            case THREAD_ZOMBIE:c_append_str(o,&l,(size_t)mx,"zombie");break;
            default:c_append_str(o,&l,(size_t)mx,"unused");}c_append_ch(o,&l,(size_t)mx,'\n');}}

static void cmd_sockets(const char *a,char *o,int mx){size_t l=0;int i;(void)a;o[0]=0;
    c_append_str(o,&l,(size_t)mx,"FD  TYPE  STATE        LOCAL_PORT  REMOTE_PORT\n");
    for(i=0;i<SOCK_MAX;++i){if(!g_sockets[i].used)continue;
        c_append_u32(o,&l,(size_t)mx,(uint32_t)i);c_append_str(o,&l,(size_t)mx,"   ");
        c_append_str(o,&l,(size_t)mx,g_sockets[i].type==1?"TCP":"UDP");c_append_str(o,&l,(size_t)mx,"   ");
        switch(g_sockets[i].tcp_state){
            case TCP_CLOSED:c_append_str(o,&l,(size_t)mx,"CLOSED");break;
            case TCP_LISTEN:c_append_str(o,&l,(size_t)mx,"LISTEN");break;
            case TCP_SYN_SENT:c_append_str(o,&l,(size_t)mx,"SYN_SENT");break;
            case TCP_ESTABLISHED:c_append_str(o,&l,(size_t)mx,"ESTABLISHED");break;
            default:c_append_str(o,&l,(size_t)mx,"OTHER");}
        c_append_str(o,&l,(size_t)mx,"      ");c_append_u32(o,&l,(size_t)mx,(uint32_t)g_sockets[i].local_port);
        c_append_str(o,&l,(size_t)mx,"         ");c_append_u32(o,&l,(size_t)mx,(uint32_t)g_sockets[i].remote_port);
        c_append_ch(o,&l,(size_t)mx,'\n');}}

static void cmd_elf64_list(const char *a,char *o,int mx){size_t l=0;int i;(void)a;o[0]=0;
    for(i=0;i<ELF64_MAX_IMAGES;++i){if(!g_elf64_images[i].used)continue;
        c_append_str(o,&l,(size_t)mx,"[");c_append_u32(o,&l,(size_t)mx,(uint32_t)i);c_append_str(o,&l,(size_t)mx,"] ");
        c_append_str(o,&l,(size_t)mx,g_elf64_images[i].path);
        c_append_str(o,&l,(size_t)mx,g_elf64_images[i].is_dynamic?" (dynamic)":" (static)");
        c_append_str(o,&l,(size_t)mx,g_elf64_images[i].interp[0]?" interp=":"");
        if(g_elf64_images[i].interp[0])c_append_str(o,&l,(size_t)mx,g_elf64_images[i].interp);
        c_append_ch(o,&l,(size_t)mx,'\n');}
    if(l==0)c_append_str(o,&l,(size_t)mx,"(no ELF64 images loaded)\n");}

static void register_cmd(const char *n,const char *h,compat_shell_fn_t fn){
    if(g_compat_cmd_count>=COMPAT_SHELL_CMD_MAX)return;
    g_compat_cmds[g_compat_cmd_count].name=n;g_compat_cmds[g_compat_cmd_count].help=h;
    g_compat_cmds[g_compat_cmd_count].handler=fn;++g_compat_cmd_count;}

bool compat_shell_dispatch(const char *name,const char *args,char *out,int out_max){int i;
    for(i=0;i<g_compat_cmd_count;++i)if(c_strcmp(g_compat_cmds[i].name,name)==0){
        g_compat_cmds[i].handler(args,out,out_max);return true;}return false;}

int compat_summary(char *buf,int mx){size_t l=0;buf[0]=0;
    c_append_str(buf,&l,(size_t)mx,"=== RiduxOS Compat Layer ===\n");
    c_append_str(buf,&l,(size_t)mx,"ATA disks: ");c_append_u32(buf,&l,(size_t)mx,(uint32_t)g_ata_disk_count);c_append_ch(buf,&l,(size_t)mx,'\n');
    c_append_str(buf,&l,(size_t)mx,"Block devs: ");c_append_u32(buf,&l,(size_t)mx,(uint32_t)g_block_dev_count);c_append_ch(buf,&l,(size_t)mx,'\n');
    c_append_str(buf,&l,(size_t)mx,"AHCI: ");c_append_str(buf,&l,(size_t)mx,g_ahci_present?"yes":"no");c_append_ch(buf,&l,(size_t)mx,'\n');
    c_append_str(buf,&l,(size_t)mx,"NICs: ");c_append_u32(buf,&l,(size_t)mx,(uint32_t)g_net_iface_count);c_append_ch(buf,&l,(size_t)mx,'\n');
    c_append_str(buf,&l,(size_t)mx,"FAT32: ");c_append_str(buf,&l,(size_t)mx,g_fat32.mounted?"mounted":"not mounted");c_append_ch(buf,&l,(size_t)mx,'\n');
    c_append_str(buf,&l,(size_t)mx,"ext2: ");c_append_str(buf,&l,(size_t)mx,g_ext2.mounted?"mounted":"not mounted");c_append_ch(buf,&l,(size_t)mx,'\n');
    {int cnt=0,i;for(i=0;i<SYSCALL_MAX;++i)if(g_syscall_table[i])cnt++;
     c_append_str(buf,&l,(size_t)mx,"Syscalls: ");c_append_u32(buf,&l,(size_t)mx,(uint32_t)cnt);c_append_ch(buf,&l,(size_t)mx,'\n');}
    c_append_str(buf,&l,(size_t)mx,"Threads: ");{int cnt=0,i;for(i=0;i<THREAD_MAX;++i)if(g_threads[i].used)cnt++;
        c_append_u32(buf,&l,(size_t)mx,(uint32_t)cnt);}c_append_ch(buf,&l,(size_t)mx,'\n');
    c_append_str(buf,&l,(size_t)mx,"Devices: ");c_append_u32(buf,&l,(size_t)mx,(uint32_t)g_vdev_count);c_append_ch(buf,&l,(size_t)mx,'\n');
    return(int)l;}

void compat_init_all(void) {
    /* Storage */
    ata_init();
    ahci_init();
    /* Network */
    nic_init();
    tcp_ip_init();
    dhcp_discover(0);
    /* Filesystems */
    g_mount_count=0;c_memset(g_mounts,0,sizeof(g_mounts));
    vfs_ext_mount("rootfs","/","riduxfs");
    vfs_ext_mount("proc","/proc","proc");
    vfs_ext_mount("sysfs","/sys","sysfs");
    vfs_ext_mount("devtmpfs","/dev","devtmpfs");
    vfs_ext_mount("tmpfs","/tmp","tmpfs");
    /* Try FAT32 on first block device */
    if(g_block_dev_count>0)fat32_mount(0);
    /* VMM */
    vmm_init(128*1024*1024);
    /* Syscalls */
    syscall_init();
    /* Threading */
    thread_init();
    /* Devices */
    vdev_init();
    drm_init();
    evdev_init();
    /* ELF64 */
    c_memset(g_elf64_images,0,sizeof(g_elf64_images));
    /* Epoll + pipes */
    c_memset(g_epoll_instances,0,sizeof(g_epoll_instances));
    c_memset(g_pipes,0,sizeof(g_pipes));
    /* Shell commands */
    g_compat_cmd_count=0;
    register_cmd("lsblk","List block devices",cmd_lsblk);
    register_cmd("ifconfig","Show network interfaces",cmd_ifconfig);
    register_cmd("route","Show routing table",cmd_route);
    register_cmd("mount","Show mounted filesystems",cmd_mount);
    register_cmd("lsdev","List /dev devices",cmd_lsdev);
    register_cmd("catproc","Read /proc files",cmd_cat_proc);
    register_cmd("nslookup","DNS lookup",cmd_nslookup);
    register_cmd("tcpget","HTTP GET over real TCP",cmd_tcpget);
    register_cmd("dhclient","Run DHCP",cmd_dhcp);
    register_cmd("ahci","Show AHCI status",cmd_ahci);
    register_cmd("syscalls","Show registered syscalls",cmd_syscalls);
    register_cmd("sysmiss","Top ENOSYS syscalls (sysmiss [N|reset])",cmd_sysmiss);
    register_cmd("threads","List threads",cmd_threads);
    register_cmd("ss","Show sockets",cmd_sockets);
    register_cmd("elf64","List ELF64 images",cmd_elf64_list);
}
