#ifndef GST_HGUARD_GSTPPC_H
#define GST_HGUARD_GSTPPC_H

/* Hmm - does this work, or do the braces cause other stack manipulation?
 * XXX
 */
#define GET_SP(target) { \
	register unsigned long r1 __asm__("r1"); \
	target = r1; \
}

#define SET_SP(source) { \
	register unsigned long r1 __asm__("r1"); \
	r1 = source; \
}

#define JUMP(target) \
    __asm__("b " SYMBOL_NAME_STR(cothread_stub))

#endif /* GST_HGUARD_GSTPPC_H */
