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

#endif /* GST_HGUARD_GSTPPC_H */
