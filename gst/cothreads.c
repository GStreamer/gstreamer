#include <sys/time.h>
#include <linux/linkage.h>
#include <stdio.h>   
#include <stdlib.h>
#include <signal.h>   
#include <setjmp.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/mman.h>

#include "cothreads.h"

pthread_key_t _cothread_key = -1;

cothread_state *cothread_create(cothread_context *ctx) {
  cothread_state *s;

  if (pthread_self() == 0) {
    s = (cothread_state *)malloc(sizeof(int) * COTHREAD_STACKSIZE);
  } else {
    char *sp = CURRENT_STACK_FRAME;
    unsigned long *stack_end = (unsigned long *)((unsigned long)sp &
      ~(STACK_SIZE - 1));
    s = (cothread_state *)(stack_end + ((ctx->nthreads - 1) *
                           COTHREAD_STACKSIZE));
    if (mmap((char *)s,COTHREAD_STACKSIZE*(sizeof(int)),
             PROT_READ|PROT_WRITE|PROT_EXEC,MAP_PRIVATE|MAP_ANONYMOUS,
             -1,0) < 0) {
      perror("mmap'ing cothread stack space");
      return NULL;
    }
  }

  s->ctx = ctx;
  s->threadnum = ctx->nthreads;
  s->flags = 0;
  s->sp = (int *)(s + COTHREAD_STACKSIZE);

  ctx->threads[ctx->nthreads++] = s;

//  printf("created cothread at %p\n",s);

  return s;
}

void cothread_setfunc(cothread_state *thread,cothread_func func,int argc,char **argv) {
  thread->func = func;
  thread->argc = argc;
  thread->argv = argv;
  thread->pc = (int *)func;
}

cothread_context *cothread_init() {
  cothread_context *ctx = (cothread_context *)malloc(sizeof(cothread_context));

  if (_cothread_key == -1) {
    if (pthread_key_create(&_cothread_key,NULL) != 0) {
      perror("pthread_key_create");
      return;
    }
  }
  pthread_setspecific(_cothread_key,ctx);

  memset(ctx->threads,0,sizeof(ctx->threads));

  ctx->threads[0] = (cothread_state *)malloc(sizeof(cothread_state));
  ctx->threads[0]->ctx = ctx;
  ctx->threads[0]->threadnum = 0;
  ctx->threads[0]->func = NULL;
  ctx->threads[0]->argc = 0;
  ctx->threads[0]->argv = NULL;
  ctx->threads[0]->flags = COTHREAD_STARTED;
  ctx->threads[0]->sp = CURRENT_STACK_FRAME;
  ctx->threads[0]->pc = 0;

//  fprintf(stderr,"0th thread is at %p\n",ctx->threads[0]);

  // we consider the initiating process to be cothread 0
  ctx->nthreads = 1;
  ctx->current = 0;

  return ctx;
}

cothread_state *cothread_main(cothread_context *ctx) {
//  fprintf(stderr,"returning %p, the 0th cothread\n",ctx->threads[0]);
  return ctx->threads[0];
}

void cothread_stub() {
  cothread_context *ctx = pthread_getspecific(_cothread_key);
  register cothread_state *thread = ctx->threads[ctx->current];

  thread->flags |= COTHREAD_STARTED;
  thread->func(thread->argc,thread->argv);
  thread->flags &= ~COTHREAD_STARTED;
  thread->pc = 0;
//  printf("uh, yeah, we shouldn't be here, but we should deal anyway\n");
}

void cothread_switch(cothread_state *thread) {
  cothread_context *ctx;
  cothread_state *current;
  int enter = 0;
//  int i;

  if (thread == NULL)
    return;

  ctx = thread->ctx;

  current = ctx->threads[ctx->current];
  if (current == NULL) {
    fprintf(stderr,"there's no current thread, help!\n");
    exit(2);
  }

  if (current == thread) {
    fprintf(stderr,"trying to switch to same thread, legal but not necessary\n");
    return;
  }

  // find the number of the thread to switch to
  ctx->current = thread->threadnum;
//  fprintf(stderr,"about to switch to thread #%d\n",ctx->current);

  /* save the current stack pointer, frame pointer, and pc */
  __asm__("movl %%esp, %0" : "=m"(current->sp) : : "esp", "ebp");
  enter = setjmp(current->jmp);
  if (enter != 0)
    return;
  enter = 1;

  /* restore stack pointer and other stuff of new cothread */
  __asm__("movl %0, %%esp\n" : "=m"(thread->sp));
  if (thread->flags & COTHREAD_STARTED) {
    // switch to it
    longjmp(thread->jmp,1);
  } else {
    // start it
    __asm__("jmp " SYMBOL_NAME_STR(cothread_stub));
  }
}
