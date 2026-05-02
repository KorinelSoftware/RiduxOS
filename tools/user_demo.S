.section .text
.global _start

_start:
    mov $0x2A, %eax
    add $0x03, %eax
    xor %ebx, %ebx
    ret
