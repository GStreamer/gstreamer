#ifndef __COTHREADS_H__
#define __COTHREADS_H__

#include <setjmp.h>
#include <pthread.h>

#define COTHREAD_STACKSIZE 8192
#define COTHREAD_MAXTHREADS 16
#define STACK_SIZE 0x200000

#ifndef CURRENT_STACK_FRAME
#define CURRENT_STACK_FRAME  ({ char __csf; &__csf; })
#endif /* CURRENT_STACK_FRAME */

typedef struct _cothread_state cothread_state;
typedef struct _cothread_context cothread_context;

typedef int (*cothread_func)(int argc,char **argv);

#define COTHREAD_STARTED	0x01

struct _cothread_state {
  cothread_context *ctx;
  int threadnum;

  cothread_func func;
  int argc;
  char **argv;

  int flags;
  int *sp;
  int *top_sp;
  int *pc;
  jmp_buf jmp;
};

struct _cothread_context {
  cothread_state *threads[COTHREAD_MAXTHREADS];
  int nthreads;
  int current;
};

cothread_context *cothread_init();
cothread_state *cothread_create(cothread_context *ctx);
void cothread_setfunc(cothread_state *thread,cothread_func func,int argc,char **argv);
void cothread_switch(cothread_state *thread);
cothread_state *cothread_main(cothread_context *ctx);

#endif /* __COTHREAD_H__ */
