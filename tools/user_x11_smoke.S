/* ELF64 X11 smoke client for Ridux Linux ABI.
 * It opens /tmp/.X11-unix/X0, performs a real X11 setup handshake,
 * creates a window, maps it, and sends a ZPixmap image.
 *
 * Run from the shell with:
 *   browser run /bin/x11-smoke.elf
 */

.equ SYS_read,       0
.equ SYS_write,      1
.equ SYS_close,      3
.equ SYS_nanosleep, 35
.equ SYS_socket,    41
.equ SYS_connect,   42
.equ SYS_exit_group,231

.equ AF_UNIX,       1
.equ SOCK_STREAM,   1

.equ X11_W,         320
.equ X11_H,         180
.equ X11_PIXELS,    (X11_W * X11_H)
.equ X11_IMG_BYTES, (X11_PIXELS * 4)
.equ X11_PUT_WORDS, ((28 + X11_IMG_BYTES) / 4)

.equ XID_WIN,       0x00400000
.equ XID_GC,        0x00400001
.equ XID_ROOT,      0x00000024
.equ XID_VISUAL,    0x00000021

.global _start
.section .text
_start:
    lea msg_start(%rip), %rsi
    mov $msg_start_len, %rdx
    call write_stdout

    mov $SYS_socket, %rax
    mov $AF_UNIX, %rdi
    mov $SOCK_STREAM, %rsi
    xor %rdx, %rdx
    syscall
    test %rax, %rax
    js fail
    mov %rax, %r12

    mov $SYS_connect, %rax
    mov %r12, %rdi
    lea sockaddr_x11(%rip), %rsi
    mov $sockaddr_x11_len, %rdx
    syscall
    test %rax, %rax
    js fail

    lea x11_setup(%rip), %rsi
    mov $x11_setup_len, %rdx
    call write_all_x11

    mov $SYS_read, %rax
    mov %r12, %rdi
    lea setup_reply(%rip), %rsi
    mov $256, %rdx
    syscall
    cmp $8, %rax
    jl fail
    cmpb $1, setup_reply(%rip)
    jne fail

    call fill_image

    lea req_create_window(%rip), %rsi
    mov $req_create_window_len, %rdx
    call write_all_x11

    lea req_create_gc(%rip), %rsi
    mov $req_create_gc_len, %rdx
    call write_all_x11

    lea req_map_window(%rip), %rsi
    mov $req_map_window_len, %rdx
    call write_all_x11

    lea req_put_image(%rip), %rsi
    mov $req_put_image_len, %rdx
    call write_all_x11

    lea image_buf(%rip), %rsi
    mov $X11_IMG_BYTES, %rdx
    call write_all_x11

    lea msg_pass(%rip), %rsi
    mov $msg_pass_len, %rdx
    call write_stdout

    mov $SYS_nanosleep, %rax
    lea sleep_ts(%rip), %rdi
    xor %rsi, %rsi
    syscall

    mov $SYS_close, %rax
    mov %r12, %rdi
    syscall
    xor %rdi, %rdi
    jmp exit

fail:
    lea msg_fail(%rip), %rsi
    mov $msg_fail_len, %rdx
    call write_stdout
    mov $1, %rdi

exit:
    mov $SYS_exit_group, %rax
    syscall
1:
    jmp 1b

write_stdout:
    mov $SYS_write, %rax
    mov $1, %rdi
    syscall
    ret

write_all_x11:
    push %rbx
    push %r13
    mov %rsi, %rbx
    mov %rdx, %r13
2:
    test %r13, %r13
    jz 3f
    mov $SYS_write, %rax
    mov %r12, %rdi
    mov %rbx, %rsi
    mov %r13, %rdx
    syscall
    test %rax, %rax
    jle 4f
    add %rax, %rbx
    sub %rax, %r13
    jmp 2b
3:
    pop %r13
    pop %rbx
    ret
4:
    pop %r13
    pop %rbx
    jmp fail

fill_image:
    lea image_buf(%rip), %rdi
    xor %r8d, %r8d
5:
    xor %r9d, %r9d
6:
    mov %r9d, %eax
    and $0xFF, %eax
    shl $16, %eax
    mov %r8d, %ecx
    imul $3, %ecx, %ecx
    and $0xFF, %ecx
    shl $8, %ecx
    or %ecx, %eax
    mov %r9d, %edx
    xor %r8d, %edx
    and $0xFF, %edx
    or %edx, %eax
    or $0xFF000000, %eax
    movl %eax, (%rdi)
    add $4, %rdi
    inc %r9d
    cmp $X11_W, %r9d
    jl 6b
    inc %r8d
    cmp $X11_H, %r8d
    jl 5b
    ret

.section .rodata
msg_start:
    .ascii "x11-smoke: start\n"
.equ msg_start_len, . - msg_start
msg_pass:
    .ascii "x11-smoke: PASS\n"
.equ msg_pass_len, . - msg_pass
msg_fail:
    .ascii "x11-smoke: FAIL\n"
.equ msg_fail_len, . - msg_fail

x11_setup:
    .byte 0x6c, 0
    .word 11, 0
    .word 0, 0
    .word 0
.equ x11_setup_len, . - x11_setup

req_create_window:
    .byte 1, 24
    .word 8
    .long XID_WIN
    .long XID_ROOT
    .word 40, 40
    .word X11_W, X11_H
    .word 0
    .word 1
    .long XID_VISUAL
    .long 0
.equ req_create_window_len, . - req_create_window

req_create_gc:
    .byte 55, 0
    .word 4
    .long XID_GC
    .long XID_WIN
    .long 0
.equ req_create_gc_len, . - req_create_gc

req_map_window:
    .byte 8, 0
    .word 2
    .long XID_WIN
.equ req_map_window_len, . - req_map_window

req_put_image:
    .byte 72, 2
    .word X11_PUT_WORDS
    .long XID_WIN
    .long XID_GC
    .word X11_W, X11_H
    .word 0, 0
    .byte 2, 24, 0, 0
    .long 0
    .long 0
.equ req_put_image_len, . - req_put_image

sockaddr_x11:
    .word AF_UNIX
    .asciz "/tmp/.X11-unix/X0"
.equ sockaddr_x11_len, . - sockaddr_x11

sleep_ts:
    .quad 20
    .quad 0

.section .bss
.align 8
setup_reply:
    .skip 256
.align 16
image_buf:
    .skip X11_IMG_BYTES
