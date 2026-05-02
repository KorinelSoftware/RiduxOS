.set MB2_MAGIC, 0xE85250D6
.set MB2_ARCH, 0
.set MB2_HEADER_LEN, multiboot_header_end - multiboot_header_start
.set MB2_CHECKSUM, -(MB2_MAGIC + MB2_ARCH + MB2_HEADER_LEN)

.section .multiboot, "a"
.align 8
multiboot_header_start:
    .long MB2_MAGIC
    .long MB2_ARCH
    .long MB2_HEADER_LEN
    .long MB2_CHECKSUM

    /* Framebuffer request tag: keep software composition light in VBox. */
    .short 5
    .short 0
    .long 20
    .long 1024
    .long 768
    .long 32

    .align 8
    .short 0
    .short 0
    .long 8
multiboot_header_end:

.section .bss
.align 4096
pml4_table:
    .skip 4096
pdpt_table:
    .skip 4096
pd_table0:
    .skip 4096
pd_table1:
    .skip 4096
pd_table2:
    .skip 4096
pd_table3:
    .skip 4096

.align 16
stack_bottom:
    .skip 131072
stack_top:

boot_magic:
    .long 0
boot_mbi:
    .long 0

.section .rodata
.align 16
gdt64:
    .quad 0x0000000000000000
    .quad 0x00AF9A000000FFFF    /* 0x08: 64-bit code */
    .quad 0x00CF92000000FFFF    /* 0x10: data */
gdt64_end:
gdt64_ptr:
    .word gdt64_end - gdt64 - 1
    .quad gdt64

.section .text
.code32
.global _start
.type _start, @function
.extern kernel_main
_start:
    cli
    mov $stack_top, %esp

    /* Save multiboot args from GRUB. */
    mov %eax, boot_magic
    mov %ebx, boot_mbi

    /* Zero page tables: 6 pages = 6144 dwords. */
    mov $pml4_table, %edi
    xor %eax, %eax
    mov $6144, %ecx
    rep stosl

    /* PML4[0] -> PDPT */
    mov $pdpt_table, %eax
    or $0x3, %eax
    mov %eax, pml4_table

    /* PDPT[0..3] -> PD tables (map first 4 GiB, 2 MiB pages). */
    mov $pd_table0, %eax
    or $0x3, %eax
    mov %eax, pdpt_table

    mov $pd_table1, %eax
    or $0x3, %eax
    mov %eax, pdpt_table + 8

    mov $pd_table2, %eax
    or $0x3, %eax
    mov %eax, pdpt_table + 16

    mov $pd_table3, %eax
    or $0x3, %eax
    mov %eax, pdpt_table + 24

    /* DMAP: PML4[256] -> same PDPT.
     * This mirrors the first 4 GiB of physical RAM at virtual address
     * 0xFFFF800000000000 (FreeBSD-style direct map). The kernel can
     * always reach any physical frame via DMAP regardless of which
     * user CR3 is currently active. */
    mov $pdpt_table, %eax
    or $0x3, %eax
    mov %eax, pml4_table + 2048     /* 256 * 8 = 2048 */

    /* Identity map first 4 GiB with 2 MiB pages (2048 entries). */
    mov $pd_table0, %edi
    xor %ebx, %ebx
    mov $2048, %ecx
1:
    mov %ebx, %eax
    or $0x83, %eax               /* present|rw|2MiB page */
    mov %eax, (%edi)
    movl $0, 4(%edi)
    add $0x200000, %ebx
    add $8, %edi
    loop 1b

    /* Enable PAE + PSE + OSFXSR + OSXMMEXCPT.
     * Some compiler-generated integer paths may still use XMM moves,
     * and without OSFXSR they fault with #UD. */
    mov %cr4, %eax
    or $0x630, %eax
    mov %eax, %cr4

    /* Load PML4 base. */
    mov $pml4_table, %eax
    mov %eax, %cr3

    /* Enable long mode in EFER. */
    mov $0xC0000080, %ecx
    rdmsr
    or $0x00000100, %eax
    wrmsr

    /* Enable paging + protected mode + x87 monitor coprocessor.
     * Clear EM so FP/SSE instructions are legal. */
    mov %cr0, %eax
    and $0xFFFFFFFB, %eax
    or $0x80000003, %eax
    mov %eax, %cr0

    lgdt gdt64_ptr
    ljmp $0x08, $long_mode_start

.code64
long_mode_start:
    mov $0x10, %ax
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %fs
    mov %ax, %gs
    mov %ax, %ss

    /* AVX/XSETBV init intentionally skipped for early-boot stability on
     * some VirtualBox/NEM setups. */

    mov $stack_top, %rsp
    xor %rbp, %rbp

    mov boot_magic(%rip), %edi
    mov boot_mbi(%rip), %esi
    call kernel_main

2:
    hlt
    jmp 2b
