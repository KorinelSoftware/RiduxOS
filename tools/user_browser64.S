/* Minimal ELF64 user payload used to exercise browser run path. */
.global _start
.section .text
_start:
1:
    jmp 1b
