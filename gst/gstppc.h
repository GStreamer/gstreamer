#ifndef GST_HGUARD_GSTPPC_H
#define GST_HGUARD_GSTPPC_H

/* FIXME: Hmm - does this work?
 */
#define GET_SP(target) \
    __asm__("stw 1,%0" : "=m"(target) : : "r1");

#define SET_SP(source) \
    __asm__("lwz 1,%0" : "=m"(source))

#define JUMP(target) \
    __asm__("b " SYMBOL_NAME_STR(cothread_stub))

struct minimal_ppc_stackframe {
    unsigned long back_chain;
    unsigned long LR_save;
    unsigned long unused1;
    unsigned long unused2;
};

#define SETUP_STACK(sp) \
    sp = ((unsigned long *)(sp)) - 4; \
    ((struct minimal_ppc_stackframe *)sp)->back_chain = 0;

#endif /* GST_HGUARD_GSTPPC_H */
