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

    /* Framebuffer request tag */
    .short 5
    .short 0
    .long 20
    .long 1920
    .long 1080
    .long 32

    .align 8
    /* End tag */
    .short 0
    .short 0
    .long 8
multiboot_header_end:

.section .bss
.align 16
stack_bottom:
    .skip 65536
stack_top:

.section .text
.global _start
.type _start, @function
_start:
    cli
    mov $stack_top, %esp

    /* kernel_main(magic, mbi_addr) */
    push %ebx
    push %eax
    call kernel_main

.halt:
    hlt
    jmp .halt
