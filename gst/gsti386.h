#ifndef GST_HGUARD_GSTI386_H
#define GST_HGUARD_GSTI386_H

/* Hmm - does this work, or do the braces cause other stack manipulation?
 * XXX
 */
#define GET_SP(target) \
  __asm__("movl %%esp, %0" : "=m"(target) : : "esp", "ebp");

#define SET_SP(source) \
  __asm__("movl %0, %%esp\n" : "=m"(thread->sp));

#define JUMP(target) \
    __asm__("jmp " SYMBOL_NAME_STR(cothread_stub))

#endif /* GST_HGUARD_GSTI386_H */
