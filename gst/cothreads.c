#include <pthread.h>
#include <sys/time.h>
#include <linux/linkage.h>
#include <stdio.h>   
#include <stdlib.h>
#include <signal.h>   
#include <setjmp.h>
#include <unistd.h>
#include <sys/mman.h>

//#define DEBUG_ENABLED
#include "gst/gst.h"
#include "cothreads.h"
#include "gst/gstarch.h"

pthread_key_t _cothread_key = -1;

cothread_state *cothread_create(cothread_context *ctx) {
  cothread_state *s;

  DEBUG("cothread: pthread_self() %ld\n",pthread_self());
  //if (pthread_self() == 0) {
  if (0) {
    s = (cothread_state *)malloc(sizeof(int) * COTHREAD_STACKSIZE);
    DEBUG("cothread: new stack at %p\n",s);
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
  s->sp = ((int *)s + COTHREAD_STACKSIZE);
  s->top_sp = s->sp;

  ctx->threads[ctx->nthreads++] = s;

  DEBUG("cothread: created cothread at %p %p\n",s, s->sp);

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
      return NULL;
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
  ctx->threads[0]->sp = (int *)CURRENT_STACK_FRAME;
  ctx->threads[0]->pc = 0;

  DEBUG("cothread: 0th thread is at %p %p\n",ctx->threads[0], ctx->threads[0]->sp);

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

  DEBUG("cothread: cothread_stub() entered\n");
  thread->flags |= COTHREAD_STARTED;
  if (thread->func)
    thread->func(thread->argc,thread->argv);
  thread->flags &= ~COTHREAD_STARTED;
  thread->pc = 0;
  thread->sp = thread->top_sp;
  DEBUG("cothread: cothread_stub() exit\n");
  //printf("uh, yeah, we shouldn't be here, but we should deal anyway\n");
}

void cothread_switch(cothread_state *thread) {
  cothread_context *ctx;
  cothread_state *current;
  int enter;
//  int i;

  if (thread == NULL) {
    g_print("cothread: there's no thread, strange...\n");
    return;
  }

  ctx = thread->ctx;

  current = ctx->threads[ctx->current];
  if (current == NULL) {
    g_print("cothread: there's no current thread, help!\n");
    exit(2);
  }

  if (current == thread) {
    g_print("cothread: trying to switch to same thread, legal but not necessary\n");
    return;
  }

  // find the number of the thread to switch to
  ctx->current = thread->threadnum;
  DEBUG("cothread: about to switch to thread #%d\n",ctx->current);

  /* save the current stack pointer, frame pointer, and pc */
  GET_SP(current->sp);
  enter = sigsetjmp(current->jmp, 1);
  if (enter != 0) {
    DEBUG("cothread: enter thread #%d %d %p<->%p (%d)\n",current->threadnum, enter, 
		    current->sp, current->top_sp, current->top_sp-current->sp);
    return;
  }
  DEBUG("cothread: exit thread #%d %d %p<->%p (%d)\n",current->threadnum, enter, 
		    current->sp, current->top_sp, current->top_sp-current->sp);
  enter = 1;

  DEBUG("cothread: set stack to %p\n", thread->sp);
  /* restore stack pointer and other stuff of new cothread */
  if (thread->flags & COTHREAD_STARTED) {
    DEBUG("cothread: in thread \n");
    SET_SP(thread->sp);
    // switch to it
    siglongjmp(thread->jmp,1);
  } else {
    SETUP_STACK(thread->sp);
    SET_SP(thread->sp);
    // start it
    //JUMP(cothread_stub);
    cothread_stub();
    DEBUG("cothread: exit thread \n");
    ctx->current = 0;
  }
}
