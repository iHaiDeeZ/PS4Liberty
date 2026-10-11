/* ucontext implementation for PS4 (x86_64)
 *
 * The PS4 (OpenOrbis SDK) ships musl headers with ucontext_t declarations
 * but does NOT include the implementation in libc.a.  This file provides
 * getcontext, setcontext, swapcontext, and makecontext as plain C with
 * inline assembly. Register slot offsets are computed with offsetof() from
 * whichever ucontext_t layout the headers declare (see the OFF_* table).
 */

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* Pull in the musl ucontext_t definition. */
#define _GNU_SOURCE
#include <signal.h>
#include <ucontext.h>

/* Offsets into ucontext_t for the registers we save, taken from whichever
 * mcontext layout the headers declare:
 *   - musl's Linux-style gregs[] array (stock OpenOrbis bits/signal.h, which
 *     defines the REG_* index macros), or
 *   - the FreeBSD/amd64 struct __mcontext with mc_* fields, which is what the
 *     PS4 kernel actually writes into signal handler contexts (provided by a
 *     libc overlay such as DolphinPS4's).
 * Only these get/set/swap/makecontext functions interpret the saved registers
 * for fibers, so either layout works as long as every translation unit sees
 * the same ucontext_t. */
#ifdef REG_RIP
#define GREG_OFF(idx) (offsetof(ucontext_t, uc_mcontext.gregs) + (idx) * 8)
#define OFF_R8   GREG_OFF(REG_R8)
#define OFF_R9   GREG_OFF(REG_R9)
#define OFF_R10  GREG_OFF(REG_R10)
#define OFF_R11  GREG_OFF(REG_R11)
#define OFF_R12  GREG_OFF(REG_R12)
#define OFF_R13  GREG_OFF(REG_R13)
#define OFF_R14  GREG_OFF(REG_R14)
#define OFF_R15  GREG_OFF(REG_R15)
#define OFF_RDI  GREG_OFF(REG_RDI)
#define OFF_RSI  GREG_OFF(REG_RSI)
#define OFF_RBP  GREG_OFF(REG_RBP)
#define OFF_RBX  GREG_OFF(REG_RBX)
#define OFF_RDX  GREG_OFF(REG_RDX)
#define OFF_RAX  GREG_OFF(REG_RAX)
#define OFF_RCX  GREG_OFF(REG_RCX)
#define OFF_RSP  GREG_OFF(REG_RSP)
#define OFF_RIP  GREG_OFF(REG_RIP)
#define OFF_EFL  GREG_OFF(REG_EFL)
#else
#define MC_OFF(field) offsetof(ucontext_t, uc_mcontext.field)
#define OFF_R8   MC_OFF(mc_r8)
#define OFF_R9   MC_OFF(mc_r9)
#define OFF_R10  MC_OFF(mc_r10)
#define OFF_R11  MC_OFF(mc_r11)
#define OFF_R12  MC_OFF(mc_r12)
#define OFF_R13  MC_OFF(mc_r13)
#define OFF_R14  MC_OFF(mc_r14)
#define OFF_R15  MC_OFF(mc_r15)
#define OFF_RDI  MC_OFF(mc_rdi)
#define OFF_RSI  MC_OFF(mc_rsi)
#define OFF_RBP  MC_OFF(mc_rbp)
#define OFF_RBX  MC_OFF(mc_rbx)
#define OFF_RDX  MC_OFF(mc_rdx)
#define OFF_RAX  MC_OFF(mc_rax)
#define OFF_RCX  MC_OFF(mc_rcx)
#define OFF_RSP  MC_OFF(mc_rsp)
#define OFF_RIP  MC_OFF(mc_rip)
#define OFF_EFL  MC_OFF(mc_rflags)
#endif

/* Store a register value into a ucontext by byte offset. */
#define UC_REG(ucp, off) (*(long *)((char *)(ucp) + (off)))

/* ========================================================================= */
/* getcontext                                                                 */
/* ========================================================================= */
__attribute__((naked))
int getcontext(ucontext_t *ucp) {
    __asm__ volatile (
        /* rdi = ucp (System V AMD64 ABI first arg) */
        "movq %%r8,  " "%c[r8]" "(%%rdi)\n"
        "movq %%r9,  " "%c[r9]" "(%%rdi)\n"
        "movq %%r10, " "%c[r10]" "(%%rdi)\n"
        "movq %%r11, " "%c[r11]" "(%%rdi)\n"
        "movq %%r12, " "%c[r12]" "(%%rdi)\n"
        "movq %%r13, " "%c[r13]" "(%%rdi)\n"
        "movq %%r14, " "%c[r14]" "(%%rdi)\n"
        "movq %%r15, " "%c[r15]" "(%%rdi)\n"
        "movq %%rdi, " "%c[rdi]" "(%%rdi)\n"
        "movq %%rsi, " "%c[rsi]" "(%%rdi)\n"
        "movq %%rbp, " "%c[rbp]" "(%%rdi)\n"
        "movq %%rbx, " "%c[rbx]" "(%%rdi)\n"
        "movq %%rdx, " "%c[rdx]" "(%%rdi)\n"
        "movq %%rcx, " "%c[rcx]" "(%%rdi)\n"
        /* RSP: caller's RSP is current RSP+8 (skip return address) */
        "leaq 8(%%rsp), %%rcx\n"
        "movq %%rcx, " "%c[rsp]" "(%%rdi)\n"
        /* RIP: return address on the stack */
        "movq (%%rsp), %%rcx\n"
        "movq %%rcx, " "%c[rip]" "(%%rdi)\n"
        /* Clear EFL */
        "movq $0, " "%c[efl]" "(%%rdi)\n"
        /* RAX: return 0 */
        "xorl %%eax, %%eax\n"
        "movq %%rax, " "%c[rax]" "(%%rdi)\n"
        "retq\n"
        :
        : [r8]  "i"(OFF_R8),  [r9]  "i"(OFF_R9),
          [r10] "i"(OFF_R10), [r11] "i"(OFF_R11),
          [r12] "i"(OFF_R12), [r13] "i"(OFF_R13),
          [r14] "i"(OFF_R14), [r15] "i"(OFF_R15),
          [rdi] "i"(OFF_RDI), [rsi] "i"(OFF_RSI),
          [rbp] "i"(OFF_RBP), [rbx] "i"(OFF_RBX),
          [rdx] "i"(OFF_RDX), [rax] "i"(OFF_RAX),
          [rcx] "i"(OFF_RCX), [rsp] "i"(OFF_RSP),
          [rip] "i"(OFF_RIP), [efl] "i"(OFF_EFL)
    );
}

/* ========================================================================= */
/* setcontext                                                                 */
/* ========================================================================= */
__attribute__((naked, noreturn))
int setcontext(const ucontext_t *ucp) {
    __asm__ volatile (
        /* rdi = ucp */
        "movq " "%c[r8]"  "(%%rdi), %%r8\n"
        "movq " "%c[r9]"  "(%%rdi), %%r9\n"
        "movq " "%c[r10]" "(%%rdi), %%r10\n"
        "movq " "%c[r11]" "(%%rdi), %%r11\n"
        "movq " "%c[r12]" "(%%rdi), %%r12\n"
        "movq " "%c[r13]" "(%%rdi), %%r13\n"
        "movq " "%c[r14]" "(%%rdi), %%r14\n"
        "movq " "%c[r15]" "(%%rdi), %%r15\n"
        "movq " "%c[rsi]" "(%%rdi), %%rsi\n"
        "movq " "%c[rbp]" "(%%rdi), %%rbp\n"
        "movq " "%c[rbx]" "(%%rdi), %%rbx\n"
        "movq " "%c[rdx]" "(%%rdi), %%rdx\n"
        "movq " "%c[rax]" "(%%rdi), %%rax\n"
        "movq " "%c[rcx]" "(%%rdi), %%rcx\n"
        "movq " "%c[rsp]" "(%%rdi), %%rsp\n"
        /* Push RIP, then restore RDI, then ret to RIP */
        "pushq " "%c[rip]" "(%%rdi)\n"
        "movq " "%c[rdi]" "(%%rdi), %%rdi\n"
        "retq\n"
        :
        : [r8]  "i"(OFF_R8),  [r9]  "i"(OFF_R9),
          [r10] "i"(OFF_R10), [r11] "i"(OFF_R11),
          [r12] "i"(OFF_R12), [r13] "i"(OFF_R13),
          [r14] "i"(OFF_R14), [r15] "i"(OFF_R15),
          [rdi] "i"(OFF_RDI), [rsi] "i"(OFF_RSI),
          [rbp] "i"(OFF_RBP), [rbx] "i"(OFF_RBX),
          [rdx] "i"(OFF_RDX), [rax] "i"(OFF_RAX),
          [rcx] "i"(OFF_RCX), [rsp] "i"(OFF_RSP),
          [rip] "i"(OFF_RIP)
    );
}

/* ========================================================================= */
/* swapcontext                                                                */
/* ========================================================================= */
__attribute__((naked))
int swapcontext(ucontext_t *old_ucp, const ucontext_t *new_ucp) {
    __asm__ volatile (
        /* rdi = old_ucp, rsi = new_ucp */
        /* Save current context into old_ucp */
        "movq %%r8,  " "%c[r8]"  "(%%rdi)\n"
        "movq %%r9,  " "%c[r9]"  "(%%rdi)\n"
        "movq %%r10, " "%c[r10]" "(%%rdi)\n"
        "movq %%r11, " "%c[r11]" "(%%rdi)\n"
        "movq %%r12, " "%c[r12]" "(%%rdi)\n"
        "movq %%r13, " "%c[r13]" "(%%rdi)\n"
        "movq %%r14, " "%c[r14]" "(%%rdi)\n"
        "movq %%r15, " "%c[r15]" "(%%rdi)\n"
        "movq %%rdi, " "%c[rdi]" "(%%rdi)\n"
        "movq %%rsi, " "%c[rsi]" "(%%rdi)\n"
        "movq %%rbp, " "%c[rbp]" "(%%rdi)\n"
        "movq %%rbx, " "%c[rbx]" "(%%rdi)\n"
        "movq %%rdx, " "%c[rdx]" "(%%rdi)\n"
        "movq %%rcx, " "%c[rcx]" "(%%rdi)\n"
        "leaq 8(%%rsp), %%rcx\n"
        "movq %%rcx, " "%c[rsp]" "(%%rdi)\n"
        "movq (%%rsp), %%rcx\n"
        "movq %%rcx, " "%c[rip]" "(%%rdi)\n"
        "movq $0, " "%c[efl]" "(%%rdi)\n"
        "xorl %%eax, %%eax\n"
        "movq %%rax, " "%c[rax]" "(%%rdi)\n"
        /* Restore new context from rsi (new_ucp) */
        "movq %%rsi, %%rdi\n"      /* rdi = new_ucp for setcontext logic */
        "movq " "%c[r8]"  "(%%rdi), %%r8\n"
        "movq " "%c[r9]"  "(%%rdi), %%r9\n"
        "movq " "%c[r10]" "(%%rdi), %%r10\n"
        "movq " "%c[r11]" "(%%rdi), %%r11\n"
        "movq " "%c[r12]" "(%%rdi), %%r12\n"
        "movq " "%c[r13]" "(%%rdi), %%r13\n"
        "movq " "%c[r14]" "(%%rdi), %%r14\n"
        "movq " "%c[r15]" "(%%rdi), %%r15\n"
        "movq " "%c[rsi]" "(%%rdi), %%rsi\n"
        "movq " "%c[rbp]" "(%%rdi), %%rbp\n"
        "movq " "%c[rbx]" "(%%rdi), %%rbx\n"
        "movq " "%c[rdx]" "(%%rdi), %%rdx\n"
        "movq " "%c[rax]" "(%%rdi), %%rax\n"
        "movq " "%c[rcx]" "(%%rdi), %%rcx\n"
        "movq " "%c[rsp]" "(%%rdi), %%rsp\n"
        "pushq " "%c[rip]" "(%%rdi)\n"
        "movq " "%c[rdi]" "(%%rdi), %%rdi\n"
        "retq\n"
        :
        : [r8]  "i"(OFF_R8),  [r9]  "i"(OFF_R9),
          [r10] "i"(OFF_R10), [r11] "i"(OFF_R11),
          [r12] "i"(OFF_R12), [r13] "i"(OFF_R13),
          [r14] "i"(OFF_R14), [r15] "i"(OFF_R15),
          [rdi] "i"(OFF_RDI), [rsi] "i"(OFF_RSI),
          [rbp] "i"(OFF_RBP), [rbx] "i"(OFF_RBX),
          [rdx] "i"(OFF_RDX), [rax] "i"(OFF_RAX),
          [rcx] "i"(OFF_RCX), [rsp] "i"(OFF_RSP),
          [rip] "i"(OFF_RIP), [efl] "i"(OFF_EFL)
    );
}

/* ========================================================================= */
/* FPU/SSE save/restore helpers                                               */
/* ========================================================================= */
/*
 * The AMD64 SysV ABI marks the x87 control word and MXCSR as callee-saved,
 * so a correct context switch must preserve them.  The base getcontext /
 * swapcontext above only save GPRs — callers that also want to preserve FPU
 * state should call rex_fpu_save/rex_fpu_restore around swapcontext().
 *
 * We use fxsave/fxrstor rather than individual STMXCSR + FNSTCW pairs: the
 * 512-byte fxsave buffer also captures x87 regs and XMM0-15, which covers
 * any path where the guest recompiler temporarily uses SSE registers across
 * a fiber yield.  The buffer must be 16-byte aligned.
 */
__attribute__((naked))
void rex_fpu_save(void *buf16) {
    /* rdi = buf16 (must be 16-byte aligned) */
    __asm__ volatile (
        "fxsave (%%rdi)\n"
        "retq\n"
        ::
    );
}

__attribute__((naked))
void rex_fpu_restore(const void *buf16) {
    /* rdi = buf16 (must be 16-byte aligned) */
    __asm__ volatile (
        "fxrstor (%%rdi)\n"
        "retq\n"
        ::
    );
}

/* ========================================================================= */
/* makecontext                                                                */
/* ========================================================================= */

/* When the user function returns, we land on this trampoline which calls
 * setcontext(uc_link) if it exists, or _exit(0) otherwise. */
static void __makecontext_return(void) {
    /* At this point RBX still holds the ucontext_t* we stashed in
     * makecontext(). */
    ucontext_t *uc;
    __asm__ volatile ("movq %%rbx, %0" : "=r"(uc));

    if (uc && uc->uc_link) {
        setcontext(uc->uc_link);
    }
    /* No uc_link — exit. */
    extern void _exit(int);
    _exit(0);
}

void makecontext(ucontext_t *ucp, void (*func)(), int argc, ...) {
    va_list ap;

    /* Stack grows downward. Compute the top of the stack. */
    unsigned long sp = (unsigned long)ucp->uc_stack.ss_sp + ucp->uc_stack.ss_size;

    /* Ensure 16-byte alignment. After we push the fake return address,
     * RSP must be 16-byte aligned at the call target. So we need
     * (sp - 8) to be 16-aligned, i.e. sp & 0xF == 8. */
    sp &= ~(unsigned long)0xF;

    /* Reserve space for a return address slot on the stack. */
    sp -= 8;
    *(unsigned long *)sp = (unsigned long)__makecontext_return;

    /* If argc > 6, we need stack slots for the extra arguments
     * (System V ABI passes first 6 integer args in registers). */
    if (argc > 6) {
        int extra = argc - 6;
        /* Ensure 16-byte alignment with the extra slots */
        if (extra & 1) extra++;
        sp -= extra * 8;
    }

    /* Set RIP = func, RSP = sp, RBP = 0 (end of call chain) */
    UC_REG(ucp, OFF_RIP) = (long)(uintptr_t)func;
    UC_REG(ucp, OFF_RSP) = (long)sp;
    UC_REG(ucp, OFF_RBP) = 0;   /* end of call chain */

    /* Stash a pointer to the ucontext in RBX (callee-saved) so the
     * return trampoline can find uc_link. */
    UC_REG(ucp, OFF_RBX) = (long)(uintptr_t)ucp;

    /* Distribute variadic integer arguments to registers and stack. */
    va_start(ap, argc);
    /* SysV integer argument registers, in order. */
    static const size_t reg_order[] = { OFF_RDI, OFF_RSI, OFF_RDX, OFF_RCX, OFF_R8, OFF_R9 };

    for (int i = 0; i < argc && i < 6; i++) {
        UC_REG(ucp, reg_order[i]) = va_arg(ap, long);
    }

    /* Extra args go on the stack (above the return address) */
    if (argc > 6) {
        long *stack_args = (long *)(sp + 8); /* just above return addr slot */
        /* Actually, after the return address, extra args start. But the ABI
         * says the 7th+ args are at [rsp+0], [rsp+8], etc. at the point
         * of the call. Since we set rsp to sp which has the return addr,
         * the args should be at sp+8, sp+16, etc. But wait — by SysV ABI,
         * at the point of calling func, stack args are above the return
         * address. Since we placed the return address at sp, we want:
         *   [sp]   = return address (__makecontext_return)
         *   [sp-8] won't be used (RSP points to [sp])
         * The function call convention has [rsp] = return address, and
         * args 7+ at [rsp+8], [rsp+16], etc. But we're setting RIP
         * directly, not doing a CALL, so on entry to func:
         *   [rsp]   = __makecontext_return (ret addr)
         *   [rsp+8] = arg7
         *   etc. */
        /* Actually for setcontext->jmp, upon entry RSP points to the return
         * address we placed. When the function executes 'ret', it'll pop
         * that. So stack args should appear at sp+8 (which, after the 'call',
         * would be [rsp+8] from the callee's perspective — that doesn't work).
         *
         * Correct approach: the stack args in SysV ABI are placed BEFORE the
         * return address in memory (higher addresses). Since sp is the lowest
         * and we're building the stack top-down:
         *   Memory layout (high to low):
         *     stack_top
         *       ...
         *       arg8       <-- sp + 16
         *       arg7       <-- sp + 8
         *       ret_addr   <-- sp   (= __makecontext_return) = RSP
         *
         * This is wrong. In SysV, the caller pushed args then calls. At entry:
         *   [rsp]     = return address
         *   [rsp+8]   = arg7   (NO! args are ABOVE the call frame)
         *
         * Actually: before the CALL instruction, the stack looks like:
         *   [rsp]     = arg7
         *   [rsp+8]   = arg8
         * CALL pushes return address, so callee sees:
         *   [rsp]     = return addr
         *   [rsp+8]   = arg7
         *   [rsp+16]  = arg8
         *
         * Since we're mimicking this with setcontext (which loads RSP then
         * ret), we need:
         *   [sp]      = ret addr (__makecontext_return)
         *   [sp+8]    = arg7
         *   [sp+16]   = arg8     etc.
         */
        for (int i = 6; i < argc; i++) {
            stack_args[i - 6] = va_arg(ap, long);
        }
    }

    va_end(ap);
}
