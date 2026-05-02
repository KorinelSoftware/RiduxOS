/* Minimal _start for freestanding RiduxOS Ring 3 ELF apps that link
 * against libridux. Calls main(), then issues SYS_exit_group with the
 * return value. No argc/argv/envp because the kernel's elf64 mapper
 * doesn't push them yet for this code path. */

.global _start
.section .text

_start:
    xor   %rbp, %rbp           /* clear frame pointer */
    call  main
    mov   %rax, %rdi           /* exit code = main's return */
    mov   $231, %rax           /* SYS_exit_group */
    syscall
1:  hlt
    jmp 1b
