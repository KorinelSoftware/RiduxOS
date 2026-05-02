/* ELF64 Linux ABI smoke test for Ridux.
 * Run from the shell with:
 *   browser run /bin/abi-smoke.elf
 */

.equ SYS_write, 1
.equ SYS_mmap, 9
.equ SYS_mprotect, 10
.equ SYS_munmap, 11
.equ SYS_brk, 12
.equ SYS_getpid, 39
.equ SYS_exit_group, 231
.equ SYS_gettid, 186
.equ SYS_statfs, 137
.equ SYS_clock_gettime, 228

.equ PROT_READ, 1
.equ PROT_WRITE, 2
.equ MAP_PRIVATE, 2
.equ MAP_ANONYMOUS, 32
.equ CLOCK_MONOTONIC, 1

.global _start
.section .text
_start:
    lea msg_start(%rip), %rsi
    mov $msg_start_len, %rdx
    call write_stdout

    mov $SYS_brk, %rax
    xor %rdi, %rdi
    syscall
    test %rax, %rax
    jz fail

    mov $SYS_mmap, %rax
    xor %rdi, %rdi
    mov $4096, %rsi
    mov $(PROT_READ | PROT_WRITE), %rdx
    mov $(MAP_PRIVATE | MAP_ANONYMOUS), %r10
    mov $-1, %r8
    xor %r9, %r9
    syscall
    test %rax, %rax
    js fail
    mov %rax, %r12

    movl $0x55444952, (%r12)

    mov $SYS_mprotect, %rax
    mov %r12, %rdi
    mov $4096, %rsi
    mov $PROT_READ, %rdx
    syscall
    test %rax, %rax
    js fail

    mov $SYS_munmap, %rax
    mov %r12, %rdi
    mov $4096, %rsi
    syscall
    test %rax, %rax
    js fail

    mov $SYS_getpid, %rax
    syscall
    test %rax, %rax
    jle fail

    mov $SYS_gettid, %rax
    syscall
    test %rax, %rax
    jle fail

    mov $SYS_clock_gettime, %rax
    mov $CLOCK_MONOTONIC, %rdi
    lea timespec_buf(%rip), %rsi
    syscall
    test %rax, %rax
    js fail

    mov $SYS_statfs, %rax
    lea root_path(%rip), %rdi
    lea statfs_buf(%rip), %rsi
    syscall
    test %rax, %rax
    js fail

    lea msg_pass(%rip), %rsi
    mov $msg_pass_len, %rdx
    call write_stdout
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

.section .rodata
msg_start:
    .ascii "abi-smoke: start\n"
.equ msg_start_len, . - msg_start
msg_pass:
    .ascii "abi-smoke: PASS\n"
.equ msg_pass_len, . - msg_pass
msg_fail:
    .ascii "abi-smoke: FAIL\n"
.equ msg_fail_len, . - msg_fail
root_path:
    .asciz "/"

.section .bss
.align 8
timespec_buf:
    .skip 16
statfs_buf:
    .skip 128
