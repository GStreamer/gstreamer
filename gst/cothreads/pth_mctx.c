/*
**  GNU Pth - The GNU Portable Threads
**  Copyright (c) 1999-2001 Ralf S. Engelschall <rse@engelschall.com>
**
**  This file is part of GNU Pth, a non-preemptive thread scheduling
**  library which can be found at http://www.gnu.org/software/pth/.
**
**  This library is free software; you can redistribute it and/or
**  modify it under the terms of the GNU Lesser General Public
**  License as published by the Free Software Foundation; either
**  version 2.1 of the License, or (at your option) any later version.
**
**  This library is distributed in the hope that it will be useful,
**  but WITHOUT ANY WARRANTY; without even the implied warranty of
**  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
**  Lesser General Public License for more details.
**
**  You should have received a copy of the GNU Lesser General Public
**  License along with this library; if not, write to the Free Software
**  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
**  USA, or contact Ralf S. Engelschall <rse@engelschall.com>.
**
**  pth_mctx.c: Pth machine context handling
*/
                             /* ``If you can't do it in
                                  ANSI C, it isn't worth doing.'' 
                                                -- Unknown        */
#include "pth_p.h"

#if cpp

/*
 * machine context state structure
 *
 * In `jb' the CPU registers, the program counter, the stack
 * pointer and (usually) the signals mask is stored. When the
 * signal mask cannot be implicitly stored in `jb', it's
 * alternatively stored explicitly in `sigs'. The `error' stores
 * the value of `errno'.
 */

#if PTH_MCTX_MTH(mcsc)
#include <ucontext.h>
#endif

typedef struct pth_mctx_st pth_mctx_t;
struct pth_mctx_st {
#if PTH_MCTX_MTH(mcsc)
    ucontext_t uc;
#elif PTH_MCTX_MTH(sjlj)
    pth_sigjmpbuf jb;
#else
#error "unknown mctx method"
#endif
    sigset_t sigs;
#if PTH_MCTX_DSP(sjlje)
    sigset_t block;
#endif
    int error;
};

/*
** ____ MACHINE STATE SWITCHING ______________________________________
*/

/*
 * save the current machine context
 */
#if PTH_MCTX_MTH(mcsc)
#define pth_mctx_save(mctx) \
        ( (mctx)->error = errno, \
          getcontext(&(mctx)->uc) )
#elif PTH_MCTX_MTH(sjlj) && PTH_MCTX_DSP(sjlje)
#define pth_mctx_save(mctx) \
        ( (mctx)->error = errno, \
          sigprocmask(SIG_SETMASK, &((mctx)->block), NULL), \
          pth_sigsetjmp((mctx)->jb) )
#elif PTH_MCTX_MTH(sjlj)
#define pth_mctx_save(mctx) \
        ( (mctx)->error = errno, \
          pth_sigsetjmp((mctx)->jb) )
#else
#error "unknown mctx method"
#endif

/*
 * restore the current machine context
 * (at the location of the old context)
 */
#if PTH_MCTX_MTH(mcsc)
#define pth_mctx_restore(mctx) \
        ( errno = (mctx)->error, \
          (void)setcontext(&(mctx)->uc) )
#elif PTH_MCTX_MTH(sjlj)
#define pth_mctx_restore(mctx) \
        ( errno = (mctx)->error, \
          (void)pth_siglongjmp((mctx)->jb, 1) )
#else
#error "unknown mctx method"
#endif

/*
 * restore the current machine context
 * (at the location of the new context)
 */
#if PTH_MCTX_MTH(sjlj) && PTH_MCTX_DSP(sjlje)
#define pth_mctx_restored(mctx) \
        sigprocmask(SIG_SETMASK, &((mctx)->sigs), NULL)
#else
#define pth_mctx_restored(mctx) \
        /*nop*/
#endif

/*
 * switch from one machine context to another
 */
#define SWITCH_DEBUG_LINE \
        "==== THREAD CONTEXT SWITCH ==========================================="
#ifdef PTH_DEBUG
#define  _pth_mctx_switch_debug pth_debug(NULL, 0, 1, SWITCH_DEBUG_LINE);
#else
#define  _pth_mctx_switch_debug /*NOP*/
#endif
#if PTH_MCTX_MTH(mcsc)
#define pth_mctx_switch(old,new) \
    _pth_mctx_switch_debug \
    swapcontext(&((old)->uc), &((new)->uc));
#elif PTH_MCTX_MTH(sjlj)
#define pth_mctx_switch(old,new) \
    _pth_mctx_switch_debug \
    if (pth_mctx_save(old) == 0) \
        pth_mctx_restore(new); \
    pth_mctx_restored(old);
#else
#error "unknown mctx method"
#endif

#endif /* cpp */

/*
** ____ MACHINE STATE INITIALIZATION ________________________________
*/

#if PTH_MCTX_MTH(mcsc)

/*
 * VARIANT 1: THE STANDARDIZED SVR4/SUSv2 APPROACH
 *
 * This is the preferred variant, because it uses the standardized
 * SVR4/SUSv2 makecontext(2) and friends which is a facility intended
 * for user-space context switching. The thread creation therefore is
 * straight-foreward.
 */

intern int pth_mctx_set(
    pth_mctx_t *mctx, void (*func)(void), char *sk_addr_lo, char *sk_addr_hi)
{
    /* fetch current context */
    if (getcontext(&(mctx->uc)) != 0)
        return FALSE;

    /* remove parent link */
    mctx->uc.uc_link           = NULL;

    /* configure new stack */
    mctx->uc.uc_stack.ss_sp    = pth_skaddr(makecontext, sk_addr_lo, sk_addr_hi-sk_addr_lo);
    mctx->uc.uc_stack.ss_size  = pth_sksize(makecontext, sk_addr_lo, sk_addr_hi-sk_addr_lo);
    mctx->uc.uc_stack.ss_flags = 0;

    /* configure startup function (with no arguments) */
    makecontext(&(mctx->uc), func, 0+1);

    return TRUE;
}

#elif PTH_MCTX_MTH(sjlj)     &&\
      !PTH_MCTX_DSP(sjljlx)  &&\
      !PTH_MCTX_DSP(sjljisc) &&\
      !PTH_MCTX_DSP(sjljw32)

/*
 * VARIANT 2: THE SIGNAL STACK TRICK
 *
 * This uses sigstack/sigaltstack() and friends and is really the
 * most tricky part of Pth. When you understand the following
 * stuff you're a good Unix hacker and then you've already
 * understood the gory ingredients of Pth.  So, either welcome to
 * the club of hackers, or do yourself a favor and skip this ;)
 *
 * The ingenious fact is that this variant runs really on _all_ POSIX
 * compliant systems without special platform kludges.  But be _VERY_
 * carefully when you change something in the following code. The slightest
 * change or reordering can lead to horribly broken code.  Really every
 * function call in the following case is intended to be how it is, doubt
 * me...
 *
 * For more details we strongly recommend you to read the companion
 * paper ``Portable Multithreading -- The Signal Stack Trick for
 * User-Space Thread Creation'' from Ralf S. Engelschall. A copy of the
 * draft of this paper you can find in the file rse-pmt.ps inside the
 * GNU Pth distribution.
 */

#if !defined(SA_ONSTACK) && defined(SV_ONSTACK)
#define SA_ONSTACK SV_ONSTACK
#endif
#if !defined(SS_DISABLE) && defined(SA_DISABLE)
#define SS_DISABLE SA_DISABLE
#endif
#if PTH_MCTX_STK(sas) && !defined(HAVE_SS_SP) && defined(HAVE_SS_BASE)
#define ss_sp ss_base
#endif

static volatile jmp_buf      mctx_trampoline;

static volatile pth_mctx_t   mctx_caller;
static volatile sig_atomic_t mctx_called;

static pth_mctx_t * volatile mctx_creating;
static      void (* volatile mctx_creating_func)(void);
static volatile sigset_t     mctx_creating_sigs;

static void pth_mctx_set_trampoline(int);
static void pth_mctx_set_bootstrap(void);

/* initialize a machine state */
intern int pth_mctx_set(
    pth_mctx_t *mctx, void (*func)(void), char *sk_addr_lo, char *sk_addr_hi)
{
    struct sigaction sa;
    struct sigaction osa;
#if PTH_MCTX_STK(sas) && defined(HAVE_STACK_T)
    stack_t ss;
    stack_t oss;
#elif PTH_MCTX_STK(sas)
    struct sigaltstack ss;
    struct sigaltstack oss;
#elif PTH_MCTX_STK(ss)
    struct sigstack ss;
    struct sigstack oss;
#else
#error "unknown mctx stack setup"
#endif
    sigset_t osigs;
    sigset_t sigs;

    pth_debug1("pth_mctx_set: enter");

    /*
     * Preserve the SIGUSR1 signal state, block SIGUSR1,
     * and establish our signal handler. The signal will
     * later transfer control onto the signal stack.
     */
    sigemptyset(&sigs);
    sigaddset(&sigs, SIGUSR1);
    sigprocmask(SIG_BLOCK, &sigs, &osigs);
    sa.sa_handler = pth_mctx_set_trampoline;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_ONSTACK;
    if (sigaction(SIGUSR1, &sa, &osa) != 0)
        return FALSE;

    /*
     * Set the new stack.
     *
     * For sigaltstack we're lucky [from sigaltstack(2) on
     * FreeBSD 3.1]: ``Signal stacks are automatically adjusted
     * for the direction of stack growth and alignment
     * requirements''
     *
     * For sigstack we have to decide ourself [from sigstack(2)
     * on Solaris 2.6]: ``The direction of stack growth is not
     * indicated in the historical definition of struct sigstack.
     * The only way to portably establish a stack pointer is for
     * the application to determine stack growth direction.''
     */
#if PTH_MCTX_STK(sas)
    ss.ss_sp    = pth_skaddr(sigaltstack, sk_addr_lo, sk_addr_hi-sk_addr_lo);
    ss.ss_size  = pth_sksize(sigaltstack, sk_addr_lo, sk_addr_hi-sk_addr_lo);
    ss.ss_flags = 0;
    if (sigaltstack(&ss, &oss) < 0)
        return FALSE;
#elif PTH_MCTX_STK(ss)
    ss.ss_sp = pth_skaddr(sigstack, sk_addr_lo, sk_addr_hi-sk_addr_lo);
    ss.ss_onstack = 0;
    if (sigstack(&ss, &oss) < 0)
        return FALSE;
#else
#error "unknown mctx stack setup"
#endif

    /*
     * Now transfer control onto the signal stack and set it up.
     * It will return immediately via "return" after the setjmp()
     * was performed. Be careful here with race conditions.  The
     * signal can be delivered the first time sigsuspend() is
     * called.
     */
    mctx_called = FALSE;
    kill(getpid(), SIGUSR1);
    sigfillset(&sigs);
    sigdelset(&sigs, SIGUSR1);
    while (!mctx_called)
        sigsuspend(&sigs);

    /*
     * Inform the system that we are back off the signal stack by
     * removing the alternative signal stack. Be careful here: It
     * first has to be disabled, before it can be removed.
     */
#if PTH_MCTX_STK(sas)
    sigaltstack(NULL, &ss);
    ss.ss_flags = SS_DISABLE;
    if (sigaltstack(&ss, NULL) < 0)
        return FALSE;
    sigaltstack(NULL, &ss);
    if (!(ss.ss_flags & SS_DISABLE))
        return_errno(FALSE, EIO);
    if (!(oss.ss_flags & SS_DISABLE))
        sigaltstack(&oss, NULL);
#elif PTH_MCTX_STK(ss)
    if (sigstack(&oss, NULL))
        return FALSE;
#endif

    /*
     * Restore the old SIGUSR1 signal handler and mask
     */
    sigaction(SIGUSR1, &osa, NULL);
    sigprocmask(SIG_SETMASK, &osigs, NULL);

    /*
     * Initialize additional ingredients of the machine
     * context structure.
     */
#if PTH_MCTX_DSP(sjlje)
    sigemptyset(&mctx->block);
#endif
    sigemptyset(&mctx->sigs);
    mctx->error = 0;

    /*
     * Tell the trampoline and bootstrap function where to dump
     * the new machine context, and what to do afterwards...
     */
    mctx_creating      = mctx;
    mctx_creating_func = func;
    memcpy((void *)&mctx_creating_sigs, &osigs, sizeof(sigset_t));

    /*
     * Now enter the trampoline again, but this time not as a signal
     * handler. Instead we jump into it directly. The functionally
     * redundant ping-pong pointer arithmentic is neccessary to avoid
     * type-conversion warnings related to the `volatile' qualifier and
     * the fact that `jmp_buf' usually is an array type.
     */
    if (pth_mctx_save((pth_mctx_t *)&mctx_caller) == 0)
        longjmp(*((jmp_buf *)&mctx_trampoline), 1);

    /*
     * Ok, we returned again, so now we're finished
     */
    pth_debug1("pth_mctx_set: leave");
    return TRUE;
}

/* trampoline signal handler */
static void pth_mctx_set_trampoline(int sig)
{
    /*
     * Save current machine state and _immediately_ go back with
     * a standard "return" (to stop the signal handler situation)
     * to let him remove the stack again. Notice that we really
     * have do a normal "return" here, or the OS would consider
     * the thread to be running on a signal stack which isn't
     * good (for instance it wouldn't allow us to spawn a thread
     * from within a thread, etc.)
     *
     * The functionally redundant ping-pong pointer arithmentic is again
     * neccessary to avoid type-conversion warnings related to the
     * `volatile' qualifier and the fact that `jmp_buf' usually is an
     * array type.
     *
     * Additionally notice that we INTENTIONALLY DO NOT USE pth_mctx_save()
     * here. Instead we use a plain setjmp(3) call because we have to make
     * sure the alternate signal stack environment is _NOT_ saved into the
     * machine context (which can be the case for sigsetjmp(3) on some
     * platforms).
     */
    if (setjmp(*((jmp_buf *)&mctx_trampoline)) == 0) {
        pth_debug1("pth_mctx_set_trampoline: return to caller");
        mctx_called = TRUE;
        return;
    }
    pth_debug1("pth_mctx_set_trampoline: reentered from caller");

    /*
     * Ok, the caller has longjmp'ed back to us, so now prepare
     * us for the real machine state switching. We have to jump
     * into another function here to get a new stack context for
     * the auto variables (which have to be auto-variables
     * because the start of the thread happens later). Else with
     * PIC (i.e. Position Independent Code which is used when PTH
     * is built as a shared library) most platforms would
     * horrible core dump as experience showed.
     */
    pth_mctx_set_bootstrap();
}

/* boot function */
static void pth_mctx_set_bootstrap(void)
{
    pth_mctx_t * volatile mctx_starting;
    void (* volatile mctx_starting_func)(void);

    /*
     * Switch to the final signal mask (inherited from parent)
     */
    sigprocmask(SIG_SETMASK, (sigset_t *)&mctx_creating_sigs, NULL);

    /*
     * Move startup details from static storage to local auto
     * variables which is necessary because it has to survive in
     * a local context until the thread is scheduled for real.
     */
    mctx_starting      = mctx_creating;
    mctx_starting_func = mctx_creating_func;

    /*
     * Save current machine state (on new stack) and
     * go back to caller until we're scheduled for real...
     */
    pth_debug1("pth_mctx_set_trampoline_jumpin: switch back to caller");
    pth_mctx_switch((pth_mctx_t *)mctx_starting, (pth_mctx_t *)&mctx_caller);

    /*
     * The new thread is now running: GREAT!
     * Now we just invoke its init function....
     */
    pth_debug1("pth_mctx_set_trampoline_jumpin: reentered from scheduler");
    mctx_starting_func();
    abort();
}

#elif PTH_MCTX_MTH(sjlj) && PTH_MCTX_DSP(sjljlx)

/*
 * VARIANT 3: LINUX SPECIFIC JMP_BUF FIDDLING
 *
 * Oh hell, I really love it when Linux guys talk about their "POSIX
 * compliant system". It's far away from POSIX compliant, IMHO. Autoconf
 * finds sigstack/sigaltstack() on Linux, yes. But it doesn't work. Why?
 * Because on Linux below version 2.2 and glibc versions below 2.1 these
 * two functions are nothing more than silly stub functions which always
 * return just -1. Very useful, yeah...
 */

#include <features.h>

intern int pth_mctx_set(
    pth_mctx_t *mctx, void (*func)(void), char *sk_addr_lo, char *sk_addr_hi)
{
    pth_mctx_save(mctx);
#if defined(__GLIBC__) && defined(__GLIBC_MINOR__) \
    && __GLIBC__ >= 2 && __GLIBC_MINOR__ >= 0 && defined(JB_PC) && defined(JB_SP)
    mctx->jb[0].__jmpbuf[JB_PC] = (int)func;
    mctx->jb[0].__jmpbuf[JB_SP] = (int)sk_addr_hi;
#elif defined(__GLIBC__) && defined(__GLIBC_MINOR__) \
    && __GLIBC__ >= 2 && __GLIBC_MINOR__ >= 0 && defined(__mc68000__)
    mctx->jb[0].__jmpbuf[0].__aregs[0] = (long int)func;
    mctx->jb[0].__jmpbuf[0].__sp = (int *)sk_addr_hi;
#elif defined(__GNU_LIBRARY__) && defined(__i386__)
    mctx->jb[0].__jmpbuf[0].__pc = (char *)func;
    mctx->jb[0].__jmpbuf[0].__sp = sk_addr_hi;
#else
#error "Unsupported Linux (g)libc version and/or platform"
#endif
    sigemptyset(&mctx->sigs);
    mctx->error = 0;
    return TRUE;
}

/*
 * VARIANT 4: INTERACTIVE SPECIFIC JMP_BUF FIDDLING
 *
 * No wonder, Interactive Unix (ISC) 4.x contains Microsoft code, so
 * it's clear that this beast lacks both sigstack and sigaltstack (about
 * makecontext we not even have to talk). So our only chance is to
 * fiddle with it's jmp_buf ingredients, of course. We support only i386
 * boxes.
 */

#elif PTH_MCTX_MTH(sjlj) && PTH_MCTX_DSP(sjljisc)
intern int
pth_mctx_set(pth_mctx_t *mctx, void (*func)(void),
                     char *sk_addr_lo, char *sk_addr_hi)
{
    pth_mctx_save(mctx);
#if i386
    mctx->jb[4] = (int)sk_addr_hi - sizeof(mctx->jb);
    mctx->jb[5] = (int)func;
#else
#error "Unsupported ISC architecture"
#endif
    sigemptyset(&mctx->sigs);
    mctx->error = 0;
    return TRUE;
}

/*
 * VARIANT 5: WIN32 SPECIFIC JMP_BUF FIDDLING
 *
 * Oh hell, Win32 has setjmp(3), but no sigstack(2) or sigaltstack(2).
 * So we have to fiddle around with the jmp_buf here too...
 */

#elif PTH_MCTX_MTH(sjlj) && PTH_MCTX_DSP(sjljw32)
intern int
pth_mctx_set(pth_mctx_t *mctx, void (*func)(void),
                     char *sk_addr_lo, char *sk_addr_hi)
{
    pth_mctx_save(mctx);
#if i386
    mctx->jb[7] = (int)sk_addr_hi;
    mctx->jb[8] = (int)func;
#else
#error "Unsupported Win32 architecture"
#endif
    sigemptyset(&mctx->sigs);
    mctx->error = 0;
    return TRUE;
}

/*
 * VARIANT X: JMP_BUF FIDDLING FOR ONE MORE ESOTERIC OS
 * Add the jmp_buf fiddling for your esoteric OS here...
 *
#elif PTH_MCTX_MTH(sjlj) && PTH_MCTX_DSP(sjljeso)
intern int
pth_mctx_set(pth_mctx_t *mctx, void (*func)(void),
             char *sk_addr_lo, char *sk_addr_hi)
{
    pth_mctx_save(mctx);
    sigemptyset(&mctx->sigs);
    mctx->error = 0;
    ...start hacking here...
    mctx->.... = func;
    mctx->.... = sk_addr_hi;
    mctx->.... = sk_addr_lo;
    return TRUE;
}
*/

#else
#error "unknown mctx method"
#endif

