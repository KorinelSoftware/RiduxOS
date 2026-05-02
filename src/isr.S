/*
 * RiduxOS low-level ISR/IRQ entry stubs.
 * Each entry saves minimal state and calls the C dispatcher
 *   isr_dispatch(uint32_t vec, uint32_t err_code)
 * or
 *   irq_dispatch(uint32_t irq)
 */

.code32
.section .text

.extern isr_dispatch
.extern irq_dispatch

/* ---- CPU exception stubs (0..31) ------------------------------------ */

/* Macros to reduce boilerplate. */
.macro ISR_NOERR vec
    .global isr_\vec
isr_\vec:
    cli
    pushl $0              /* fake error code */
    pushl $\vec
    jmp isr_common_stub
.endm

.macro ISR_ERR vec
    .global isr_\vec
isr_\vec:
    cli
    pushl $\vec
    jmp isr_common_stub
.endm

ISR_NOERR 0
ISR_NOERR 1
ISR_NOERR 2
ISR_NOERR 3
ISR_NOERR 4
ISR_NOERR 5
ISR_NOERR 6
ISR_NOERR 7
ISR_ERR   8
ISR_NOERR 9
ISR_ERR   10
ISR_ERR   11
ISR_ERR   12
ISR_ERR   13
ISR_ERR   14
ISR_NOERR 15
ISR_NOERR 16
ISR_ERR   17
ISR_NOERR 18
ISR_NOERR 19
ISR_NOERR 20
ISR_NOERR 21
ISR_NOERR 22
ISR_NOERR 23
ISR_NOERR 24
ISR_NOERR 25
ISR_NOERR 26
ISR_NOERR 27
ISR_NOERR 28
ISR_NOERR 29
ISR_ERR   30
ISR_NOERR 31

/* Stack layout at isr_common_stub:
 * +0: vector (just pushed)
 * +4: err_code
 * +8: eip, cs, eflags[, esp, ss]  (from CPU)
 */
isr_common_stub:
    pusha                 /* edi esi ebp esp ebx edx ecx eax */
    pushl %ds
    pushl %es
    pushl %fs
    pushl %gs

    /* Reload kernel data segments (0x10 = GDT index 2 * 8). */
    movw $0x10, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %fs
    movw %ax, %gs

    /* Args: isr_dispatch(vec, err) - pull from pushed values. */
    movl 48(%esp), %eax   /* vector   (4 segs*4 + 8 pusha*4 = 48) */
    movl 52(%esp), %ebx   /* err code */
    pushl %ebx
    pushl %eax
    call isr_dispatch
    addl $8, %esp

    popl %gs
    popl %fs
    popl %es
    popl %ds
    popa
    addl $8, %esp         /* drop vector + err */
    sti
    iret

/* ---- IRQ stubs (remapped to vectors 32..47) ------------------------- */

.macro IRQ n, vec
    .global irq_\n
irq_\n:
    cli
    pushl $0
    pushl $\vec
    jmp irq_common_stub
.endm

IRQ 0,  32
IRQ 1,  33
IRQ 2,  34
IRQ 3,  35
IRQ 4,  36
IRQ 5,  37
IRQ 6,  38
IRQ 7,  39
IRQ 8,  40
IRQ 9,  41
IRQ 10, 42
IRQ 11, 43
IRQ 12, 44
IRQ 13, 45
IRQ 14, 46
IRQ 15, 47

irq_common_stub:
    pusha
    pushl %ds
    pushl %es
    pushl %fs
    pushl %gs

    movw $0x10, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %fs
    movw %ax, %gs

    /* vector is at +48 relative to esp after 4 seg regs + 8 pusha regs.  */
    movl 48(%esp), %eax
    subl $32, %eax        /* convert 32..47 vector -> 0..15 IRQ number   */
    pushl %eax
    call irq_dispatch
    addl $4, %esp

    popl %gs
    popl %fs
    popl %es
    popl %ds
    popa
    addl $8, %esp
    sti
    iret
