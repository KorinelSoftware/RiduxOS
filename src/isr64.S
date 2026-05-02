/*
 * RiduxOS 64-bit ISR/IRQ entry stubs + SYSCALL/SYSRET.
 * Calls:
 *   isr_dispatch(uint32_t vec, uint32_t err_code)
 *   irq_dispatch(uint32_t irq)
 *   syscall_dispatch(uint64_t nr, uint64_t a1, uint64_t a2, uint64_t a3,
 *                    uint64_t a4, uint64_t a5, uint64_t a6)
 */

.code64
.section .text

.extern isr_dispatch
.extern irq_dispatch
.extern syscall_dispatch
.extern task_note_irq_user_context
.extern task_restore_current_user_msrs
.extern g_kernel_preempt_disable
.extern g_tss_ptr  /* for TSS rsp0 retrieval */

/* Pointer to the PUSH_GPRS frame on the kernel stack during a syscall.
 * C code (e.g. clone) reads this to get actual user register state. */
.global g_syscall_user_frame
.section .bss
.align 8
g_syscall_user_frame:
    .skip 8
.section .text

/* Original push/pop order preserved (rax pushed first, r15 pushed last).
 * After PUSH_GPRS, rsp-relative layout is:
 *     +0   r15  +8   r14  +16  r13  +24  r12
 *     +32  r11  +40  r10  +48  r9   +56  r8
 *     +64  rbp  +72  rdi  +80  rsi  +88  rdx
 *     +96  rcx  +104 rbx  +112 rax
 * (Kept as-is to avoid touching the IRQ/ISR paths that already worked.)
 */
.macro PUSH_GPRS
    pushq %rax
    pushq %rbx
    pushq %rcx
    pushq %rdx
    pushq %rsi
    pushq %rdi
    pushq %rbp
    pushq %r8
    pushq %r9
    pushq %r10
    pushq %r11
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15
.endm

.macro POP_GPRS
    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %r11
    popq %r10
    popq %r9
    popq %r8
    popq %rbp
    popq %rdi
    popq %rsi
    popq %rdx
    popq %rcx
    popq %rbx
    popq %rax
.endm

/* ---- CPU exceptions (0..31) ---- */
.macro ISR_NOERR vec
    .global isr_\vec
isr_\vec:
    pushq $0
    pushq $\vec
    jmp isr_common_stub64
.endm

.macro ISR_ERR vec
    .global isr_\vec
isr_\vec:
    pushq $\vec
    jmp isr_common_stub64
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

isr_common_stub64:
    PUSH_GPRS
    movq 144(%rsp), %rax   /* saved CS */
    testq $3, %rax         /* interrupt/exception from ring 3? */
    jz 1f
    swapgs                 /* make kernel GS/per-CPU active */
1:
    movq 120(%rsp), %rdi   /* vector */
    movq 128(%rsp), %rsi   /* err code */
    movq 136(%rsp), %rdx   /* fault RIP */
    movq %rsp, %rcx        /* saved frame base */
    call isr_dispatch
    movq 144(%rsp), %rax
    testq $3, %rax
    jz 2f
    call task_restore_current_user_msrs
    swapgs                 /* restore user GS before iretq */
2:
    POP_GPRS
    addq $16, %rsp         /* drop vector + err */
    iretq

/* ---- IRQ stubs (vectors 32..47) ---- */
.macro IRQ n, vec
    .global irq_\n
irq_\n:
    pushq $0
    pushq $\vec
    jmp irq_common_stub64
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

irq_common_stub64:
    PUSH_GPRS
    movq 144(%rsp), %rax   /* saved CS */
    testq $3, %rax         /* IRQ from ring 3? */
    jz 1f
    swapgs                 /* make kernel GS/per-CPU active */
    movq 136(%rsp), %rdi   /* saved user RIP */
    movq 160(%rsp), %rsi   /* saved user RSP */
    movq %rsp, %rdx        /* saved frame base */
    call task_note_irq_user_context
1:
    movq 120(%rsp), %rdi
    subq $32, %rdi         /* vector -> irq number */
    call irq_dispatch
    movq 144(%rsp), %rax
    testq $3, %rax
    jz 2f
    call task_restore_current_user_msrs
    swapgs                 /* restore user GS before iretq */
2:
    POP_GPRS
    addq $16, %rsp         /* drop vector + fake err */
    iretq

/* ================================================================
 *  SYSCALL entry (64-bit fast system call)
 *
 *  On SYSCALL entry (hardware does automatically):
 *    RCX = saved user RIP    R11 = saved user RFLAGS
 *    RAX = syscall number
 *    RDI,RSI,RDX,R10,R8,R9 = args 1-6
 *    CS ← IA32_MSR_STAR[32:47]   SS ← IA32_MSR_STAR[48:63]+8
 *
 *  We build an iretq-compatible frame and return via iretq.
 *  This is simpler and handles signals / ptrace correctly.
 * ================================================================ */

.global syscall_entry
syscall_entry:
    /* SYSCALL hardware stores:
     *   RCX <- user RIP    R11 <- user RFLAGS
     * Nothing else is saved. We must preserve user RSP ourselves.
     * Order of operations:
     *   1. swapgs                 -> kernel GS base active
     *   2. save user RSP to gs:8  -> so we can load kernel RSP
     *   3. load kernel RSP from gs:0
     */
    swapgs
    movq %rsp, %gs:8            /* save user RSP before we lose it */
    movq %gs:0, %rsp            /* kernel stack from per-CPU (TSS rsp0) */

    /* Build an iretq-compatible frame (SS / RSP / RFLAGS / CS / RIP).
     * Selectors match the GDT layout in compat2.c after gdt64_setup:
     *   user code @ offset 0x28 -> CS = 0x28 | RPL=3 = 0x2B
     *   user data @ offset 0x30 -> SS = 0x30 | RPL=3 = 0x33
     */
    pushq $0x33                 /* user SS  (ring3 data, GDT 0x30 | 3) */
    pushq %gs:8                 /* user RSP (just saved above)         */
    pushq %r11                  /* user RFLAGS                         */
    pushq $0x2B                 /* user CS  (ring3 code, GDT 0x28 | 3) */
    pushq %rcx                  /* user RIP                            */

    /* Dummy error code + vector marker so layout mirrors ISR/IRQ. */
    pushq $0                   /* dummy err code */
    pushq $0x80                /* vector 0x80 = syscall marker */

    PUSH_GPRS

    movq $0x10, %rax
    movq %rax, %ds
    movq %rax, %es

    /*  Rearrange registers for the C calling convention.
     *
     *  PUSH_GPRS layout (original order, rsp-relative):
     *    +0   r15  +8   r14  +16  r13  +24  r12
     *    +32  r11  +40  r10  +48  r9   +56  r8
     *    +64  rbp  +72  rdi  +80  rsi  +88  rdx
     *    +96  rcx  +104 rbx  +112 rax
     *    +120 vec  +128 err
     *    +136 rip  +144 cs   +152 rflags +160 rsp +168 ss
     *
     *  User register values at SYSCALL entry (Linux x86-64 ABI):
     *    rax = nr   rdi = a1   rsi = a2   rdx = a3
     *    r10 = a4   r8  = a5   r9  = a6
     *
     *  C function:    int64_t syscall_dispatch(nr, a1, a2, a3, a4, a5, a6)
     *  C convention:  rdi, rsi, rdx, rcx, r8, r9, [rsp+0]  (7th arg on stack)
     *
     *  Push a6 (user r9, currently at +48) first so it lands as 7th C
     *  argument. After that push, every other offset shifts by +8.
     */
    pushq 48(%rsp)              /* push r9 (a6) as 7th C arg on stack */

    /* Save a pointer to the PUSH_GPRS frame so C code (e.g. clone)
     * can read the actual user register state from the kernel stack.
     * After the 7th-arg push, the PUSH_GPRS frame starts at RSP+8. */
    leaq 8(%rsp), %rax
    movq %rax, g_syscall_user_frame(%rip)

    movq  120(%rsp), %rdi       /* nr  (rax was at +112 -> +120 after push) */
    movq   80(%rsp), %rsi       /* a1  (rdi was at +72  -> +80)              */
    movq   88(%rsp), %rdx       /* a2  (rsi was at +80  -> +88)              */
    movq   96(%rsp), %rcx       /* a3  (rdx was at +88  -> +96)              */
    movq   48(%rsp), %r8        /* a4  (r10 was at +40  -> +48)              */
    movq   64(%rsp), %r9        /* a5  (r8  was at +56  -> +64)              */

    /* IA32_FMASK clears IF on SYSCALL entry so the kernel starts with
     * interrupts disabled. That is safe for tiny syscalls, but real browser
     * startup spends a long time in mmap/mprotect/openat/loader paths; keeping
     * IF clear there starves the PIT, scheduler, input, and Ridux renderer.
     * Re-enable interrupts after the full user frame/GPR state is saved and
     * after g_syscall_user_frame is published. The return path restores the
     * user's original RFLAGS via iretq below. */
    incl g_kernel_preempt_disable(%rip)
    sti
    call syscall_dispatch
    cli
    decl g_kernel_preempt_disable(%rip)

    addq $8, %rsp               /* drop the pushed 7th arg */

    /* Overwrite saved rax (at +112) with syscall return value. */
    movq %rax, 112(%rsp)

    /* Some deeper syscall paths have been observed to scribble over
     * the iret frame while leaving the saved GPR block intact.
     * Rebuild the user return frame from the surviving copies before
     * we pop registers and iret back to ring 3:
     *   - saved rcx slot still holds the SYSCALL return RIP
     *   - saved r11 slot still holds the original user RFLAGS
     *
     * NOTE: We deliberately do NOT read user RSP from %gs:8 anymore.
     * %gs:8 lives in the per-CPU area (per_cpu_t.user_rsp) and is
     * therefore SHARED across tasks on a uniprocessor. If a syscall
     * (e.g. wait4, futex_wait) calls task_schedule and another task
     * runs and ITSELF enters the kernel via SYSCALL, that task's
     * syscall_entry overwrites %gs:8 with its own user RSP. When the
     * original task is resumed and reaches this code, %gs:8 holds the
     * other task's RSP, and we'd iretq back to ring 3 with a foreign
     * RSP. That manifests as Firefox parent crashing at random low
     * addresses (e.g. RIP=0x3) shortly after wait4 returns from
     * waiting on a child clone.
     *
     * The iretq frame's RSP slot at 160(%rsp) was correctly populated
     * at syscall entry by `pushq %gs:8` BEFORE any task switch, on
     * THIS task's own kernel stack. That stack is per-task, so the
     * value there survives any number of task switches and accurately
     * represents the current task's user RSP at the time it entered
     * this syscall. Trust the kernel stack slot. */
    movq  96(%rsp), %rax        /* saved rcx -> user RIP */
    movq %rax, 136(%rsp)
    movq  32(%rsp), %rax        /* saved r11 -> user RFLAGS */
    movq %rax, 152(%rsp)
    movq $0x2B, 144(%rsp)       /* user CS */
    movq $0x33, 168(%rsp)       /* user SS */
    call task_restore_current_user_msrs

    POP_GPRS
    addq $16, %rsp              /* drop vector + err code */

    /* Returning to user: swapgs must be restored before iretq so
     * user code sees user GS base again. iretq pops RIP/CS/RFLAGS/RSP/SS. */
    swapgs
    iretq                       /* return to user space */


/* ================================================================
 *  task_launch_to_user_asm
 *
 *  One-shot: transitions the current CPU from ring 0 into ring 3
 *  by building an iretq frame on the current kernel stack and
 *  executing iretq. Does NOT return.
 *
 *  Caller must have already:
 *    - Loaded CR3 with the target task's address space (paging_switch).
 *    - Written IA32_FS_BASE          (0xC0000100) with user FS base.
 *    - Written IA32_KERNEL_GS_BASE   (0xC0000102) with the target
 *      user GS base (swapgs below will swap it into GS base).
 *    - Written TSS.rsp0 with the target task's kernel stack top
 *      (so subsequent SYSCALL entries land on that stack).
 *    - Updated g_current_task to point at the target task.
 *
 *  Arguments (SysV ABI):
 *    rdi = user RIP (entry point, e.g. ld-linux's _start)
 *    rsi = user RSP (top of elf64_setup_stack frame)
 *    rdx = user RFLAGS (IF will be forced on below)
 *
 *  Selector values match the GDT layout produced by gdt64_setup()
 *  in compat2.c:
 *     User code  @ GDT offset 0x28  -> CS = 0x28 | RPL=3 = 0x2B
 *     User data  @ GDT offset 0x30  -> SS = 0x30 | RPL=3 = 0x33
 *
 *  Stack frame pushed (top-down for iretq):
 *    +32 SS   = 0x33  (user data, RPL=3)
 *    +24 RSP  = rsi
 *    +16 RFLAGS = rdx | IF
 *    +8  CS   = 0x2B  (user code, RPL=3)
 *    +0  RIP  = rdi
 * ================================================================ */
/* ----------------------------------------------------------------
 * context_switch_kstack(uint64_t *old_rsp_save, uint64_t new_rsp)
 *
 * Saves callee-saved regs onto current stack, stores RSP to
 * *old_rsp_save (rdi), loads new_rsp (rsi) and restores.
 * Returns into whatever code the new task was in when it was
 * previously switched away.  For freshly-created threads whose
 * kernel stack was set up by clone3_setup_kstack(), the "return"
 * address is clone3_entry_trampoline below.
 * ---------------------------------------------------------------- */
.global context_switch_kstack
context_switch_kstack:
    pushfq
    pushq %rbx
    pushq %rbp
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15
    movq %rsp, (%rdi)       /* save outgoing RSP */
    movq %rsi, %rsp          /* load incoming RSP */
    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %rbp
    popq %rbx
    /* Keep interrupts disabled until task_schedule() has re-asserted the
     * resumed task's CR3/TSS/TLS state.  Restoring IF here can let a timer IRQ
     * nest inside the tiny window before the scheduler epilogue runs, which is
     * poisonous for Linux TLS-heavy workloads such as Firefox/Chromium. */
    addq $8, %rsp              /* discard saved rflags */
    ret

/* ----------------------------------------------------------------
 * clone3_entry_trampoline
 *
 * Freshly-created threads land here after the first context_switch.
 * At this point g_current_task is already set, CR3/FSbase/TSS are
 * wired.  We just need to enter ring 3 with the saved user ctx.
 * The task_t pointer was pushed as the "return-address frame" arg
 * by clone3_setup_kstack().
 * ---------------------------------------------------------------- */
.global clone3_entry_trampoline
clone3_entry_trampoline:
    popq %rdi               /* task_t *t (pushed by setup) */
    call clone3_enter_user   /* C helper: sets up and calls task_launch_to_user_asm */
    /* Should not return, but just in case: */
    hlt

/* ----------------------------------------------------------------
 * clone3_iretq_ctx(cpu_context_t *ctx)
 *
 * Restores ALL general-purpose registers from the cpu_context_t
 * pointed to by RDI, builds an iretq frame, does swapgs, and
 * iretq's into ring 3. NEVER RETURNS.
 *
 * cpu_context_t layout (uint64_t fields):
 *   +0   rax  +8   rbx  +16  rcx  +24  rdx
 *   +32  rsi  +40  rdi  +48  rbp
 *   +56  r8   +64  r9   +72  r10  +80  r11
 *   +88  r12  +96  r13  +104 r14  +112 r15
 *   +120 rip  +128 rsp  +136 rflags
 *   +144 cs   +152 ss   +160 fs_base  +168 gs_base
 * ---------------------------------------------------------------- */
.global clone3_iretq_ctx
clone3_iretq_ctx:
    cli
    movw $0x33, %ax
    movw %ax, %ds
    movw %ax, %es

    /* Build iretq frame */
    pushq $0x33                 /* SS */
    pushq 128(%rdi)             /* RSP from ctx */
    movq  136(%rdi), %rax
    orq   $0x200, %rax          /* force IF */
    pushq %rax                  /* RFLAGS */
    pushq $0x2B                 /* CS */
    pushq 120(%rdi)             /* RIP from ctx */

    swapgs

    /* Restore GPRs. RDI is used as base pointer until last. */
    movq 112(%rdi), %r15
    movq 104(%rdi), %r14
    movq  96(%rdi), %r13
    movq  88(%rdi), %r12
    movq  80(%rdi), %r11
    movq  72(%rdi), %r10
    movq  64(%rdi), %r9
    movq  56(%rdi), %r8
    movq  48(%rdi), %rbp
    movq  32(%rdi), %rsi
    movq  24(%rdi), %rdx
    movq  16(%rdi), %rcx
    movq   8(%rdi), %rbx
    movq   0(%rdi), %rax
    /* Load rdi LAST since it's our base pointer */
    movq  40(%rdi), %rdi

    iretq

.global task_launch_to_user_asm
task_launch_to_user_asm:
    cli

    /* Force IF on so user space can receive interrupts. */
    orq $0x200, %rdx

    /* Load user data selectors. 0x33 = selector @ 0x30 + RPL 3. */
    movw $0x33, %ax
    movw %ax, %ds
    movw %ax, %es
    /* Do NOT touch %fs / %gs selectors: their base MSRs carry the
     * real addresses, and swapgs below handles the GS base swap. */

    /* Build iretq frame. */
    pushq $0x33                 /* user SS   (0x30 | RPL=3)  */
    pushq %rsi                  /* user RSP                  */
    pushq %rdx                  /* user RFLAGS (IF forced)   */
    pushq $0x2B                 /* user CS   (0x28 | RPL=3)  */
    pushq %rdi                  /* user RIP                  */

    /* Swap to user GS. Before: GS base = per-CPU (kernel),
     * KERNEL_GS_BASE = user GS (set by caller via wrmsr).
     * After swapgs: GS base = user GS, KERNEL_GS_BASE = per-CPU. */
    swapgs

    /* Wipe scratch regs so we don't leak kernel values. ld-linux
     * ignores them anyway (it re-initialises from its own state). */
    xorq %rax, %rax
    xorq %rbx, %rbx
    xorq %rcx, %rcx
    xorq %rdx, %rdx
    xorq %rsi, %rsi
    xorq %rdi, %rdi
    xorq %rbp, %rbp
    xorq %r8,  %r8
    xorq %r9,  %r9
    xorq %r10, %r10
    xorq %r11, %r11
    xorq %r12, %r12
    xorq %r13, %r13
    xorq %r14, %r14
    xorq %r15, %r15

    iretq
