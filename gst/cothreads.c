/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wtay@chello.be>
 *
 * cothreads.c: Cothreading routines
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#include <pthread.h>
#include <stdio.h>   
#include <stdlib.h>
#include <signal.h>   
#include <setjmp.h>
#include <unistd.h>
#include <sys/mman.h>

/* we make too much noise for normal debugging... */
#define GST_DEBUG_FORCE_DISABLE
#include "gst_private.h"

#include "cothreads.h"
#include "gstarch.h"


pthread_key_t _cothread_key = -1;

/* Disablig this define allows you to shut off a few checks in
 * cothread_switch.  This likely will speed things up fractionally */
#define COTHREAD_PARANOID

/**
 * cothread_init:
 *
 * Create and initialize a new cothread context 
 *
 * Returns: the new cothread context
 */
cothread_context*
cothread_init (void) 
{
  cothread_context *ctx = (cothread_context *)malloc(sizeof(cothread_context));

  GST_INFO (GST_CAT_COTHREADS,"initializing cothreads");

  if (_cothread_key == -1) {
    if (pthread_key_create (&_cothread_key,NULL) != 0) {
      perror ("pthread_key_create");
      return NULL;
    }
  }
  pthread_setspecific (_cothread_key,ctx);

  memset (ctx->threads,0,sizeof(ctx->threads));

  ctx->threads[0] = (cothread_state *)malloc(sizeof(cothread_state));
  ctx->threads[0]->ctx = ctx;
  ctx->threads[0]->threadnum = 0;
  ctx->threads[0]->func = NULL;
  ctx->threads[0]->argc = 0;
  ctx->threads[0]->argv = NULL;
  ctx->threads[0]->flags = COTHREAD_STARTED;
  ctx->threads[0]->sp = (int *)CURRENT_STACK_FRAME;
  ctx->threads[0]->pc = 0;

  // initialize the lock
#ifdef COTHREAD_ATOMIC
  atomic_set (&ctx->threads[0]->lock, 0);
#else
  ctx->threads[0]->lock = g_mutex_new();
#endif

  GST_INFO (GST_CAT_COTHREADS,"0th thread is %p at sp:%p",ctx->threads[0], ctx->threads[0]->sp);

  // we consider the initiating process to be cothread 0
  ctx->nthreads = 1;
  ctx->current = 0;
  ctx->data = g_hash_table_new(g_str_hash, g_str_equal);

  return ctx;
}

/**
 * cothread_create:
 * @ctx: the cothread context
 *
 * Create a new cothread state in the given context
 *
 * Returns: the new cothread state
 */
cothread_state*
cothread_create (cothread_context *ctx) 
{
  cothread_state *s;

  GST_DEBUG (0,"pthread_self() %ld\n",pthread_self());
  //if (0) {
  if (pthread_self() == 0) {
    s = (cothread_state *)malloc(sizeof(int) * COTHREAD_STACKSIZE);
    GST_DEBUG (0,"new stack (case 1) at %p\n",s);
  } else {
    char *sp = CURRENT_STACK_FRAME;
    unsigned long *stack_end = (unsigned long *)((unsigned long)sp &
      ~(STACK_SIZE - 1));
    s = (cothread_state *)(stack_end + ((ctx->nthreads - 1) *
                           COTHREAD_STACKSIZE));
    GST_DEBUG (0,"new stack (case 2) at %p\n",s);
    if (mmap((char *)s,COTHREAD_STACKSIZE*(sizeof(int)),
             PROT_READ|PROT_WRITE|PROT_EXEC,MAP_FIXED|MAP_PRIVATE|MAP_ANON,
             -1,0) < 0) {
      perror("mmap'ing cothread stack space");
      return NULL;
    }
  }

  s->ctx = ctx;
  s->threadnum = ctx->nthreads;
  s->flags = 0;
  s->sp = ((int *)s + COTHREAD_STACKSIZE);
  // is this needed anymore?
  s->top_sp = s->sp;

  // initialize the lock
#ifdef COTHREAD_ATOMIC
  atomic_set (s->lock, 0);
#else
  s->lock = g_mutex_new();
#endif

  GST_INFO (GST_CAT_COTHREADS,"created cothread #%d: %p at sp:%p", ctx->nthreads, s, s->sp);

  ctx->threads[ctx->nthreads++] = s;

  return s;
}

/**
 * cothread_setfunc:
 * @thread: the cothread state
 * @func: the function to call
 * @argc: argument count for the cothread function
 * @argv: arguments for the cothread function
 *
 * Set the cothread function
 */
void 
cothread_setfunc (cothread_state *thread,
		  cothread_func func,
		  int argc,
		  char **argv) 
{
  thread->func = func;
  thread->argc = argc;
  thread->argv = argv;
  thread->pc = (int *)func;
}

/**
 * cothread_main:
 * @ctx: cothread context to find main thread of
 *
 * Returns: the #cothread_state of the main (0th) thread
 */
cothread_state*
cothread_main(cothread_context *ctx) 
{
  GST_DEBUG (0,"returning %p, the 0th cothread\n",ctx->threads[0]);
  return ctx->threads[0];
}

static void 
cothread_stub (void) 
{
  cothread_context *ctx = pthread_getspecific(_cothread_key);
  register cothread_state *thread = ctx->threads[ctx->current];

  GST_DEBUG_ENTER("");

  thread->flags |= COTHREAD_STARTED;
//#ifdef COTHREAD_ATOMIC
//  // do something here to lock
//#else
//  g_mutex_lock(thread->lock);
//#endif
  while (1) {
    thread->func(thread->argc,thread->argv);
    // we do this to avoid ever returning, we just switch to 0th thread
    cothread_switch(cothread_main(ctx));
  }
  thread->flags &= ~COTHREAD_STARTED;
  thread->pc = 0;
  thread->sp = thread->top_sp;
  fprintf(stderr,"uh, yeah, we shouldn't be here, but we should deal anyway\n");
  GST_DEBUG_LEAVE("");
}

/**
 * cothread_getcurrent:
 *
 * Returns: the current cothread id
 */
int cothread_getcurrent(void) {
  cothread_context *ctx = pthread_getspecific(_cothread_key);
  if (!ctx) return -1;
  return ctx->current;
}

/**
 * cothread_set_data:
 * @thread: the cothread state
 * @key: a key for the data
 * @data: the data
 *
 * adds data to a cothread
 */
void
cothread_set_data (cothread_state *thread, 
		   gchar *key,
		   gpointer data)
{
  cothread_context *ctx = pthread_getspecific(_cothread_key);

  g_hash_table_insert(ctx->data, key, data);
}

/**
 * cothread_get_data:
 * @thread: the cothread state
 * @key: a key for the data
 *
 * get data from the cothread
 *
 * Returns: the data assiciated with the key
 */
gpointer
cothread_get_data (cothread_state *thread, 
		   gchar *key)
{
  cothread_context *ctx = pthread_getspecific(_cothread_key);

  return g_hash_table_lookup(ctx->data, key);
}

/**
 * cothread_switch:
 * @thread: cothread state to switch to
 *
 * Switches to the given cothread state
 */
void 
cothread_switch (cothread_state *thread) 
{
  cothread_context *ctx;
  cothread_state *current;
  int enter;

#ifdef COTHREAD_PARANOID
  if (thread == NULL) goto nothread;
#endif
  ctx = thread->ctx;
#ifdef COTHREAD_PARANOID
  if (ctx == NULL) goto nocontext;
#endif

  current = ctx->threads[ctx->current];
#ifdef COTHREAD_PARANOID
  if (current == NULL) goto nocurrent;
#endif
  if (current == thread) goto selfswitch;

  // unlock the current thread, we're out of that context now
#ifdef COTHREAD_ATOMIC
  // do something to unlock the cothread
#else
  g_mutex_unlock(current->lock);
#endif

  // lock the next cothread before we even switch to it
#ifdef COTHREAD_ATOMIC
  // do something to lock the cothread
#else
  g_mutex_lock(thread->lock);
#endif

  // find the number of the thread to switch to
  GST_INFO (GST_CAT_COTHREAD_SWITCH,"switching from cothread #%d to cothread #%d",
            ctx->current,thread->threadnum);
  ctx->current = thread->threadnum;

  /* save the current stack pointer, frame pointer, and pc */
#ifdef GST_ARCH_PRESETJMP
  GST_ARCH_PRESETJMP();
#endif
  enter = setjmp(current->jmp);
  if (enter != 0) {
    GST_DEBUG (0,"enter thread #%d %d %p<->%p (%d)\n",current->threadnum, enter, 
		    current->sp, current->top_sp, current->top_sp-current->sp);
    return;
  }
  GST_DEBUG (0,"exit thread #%d %d %p<->%p (%d)\n",current->threadnum, enter, 
		    current->sp, current->top_sp, current->top_sp-current->sp);
  enter = 1;

  GST_DEBUG (0,"set stack to %p\n", thread->sp);
  /* restore stack pointer and other stuff of new cothread */
  if (thread->flags & COTHREAD_STARTED) {
    GST_DEBUG (0,"in thread \n");
    // switch to it
    longjmp(thread->jmp,1);
  } else {
    GST_ARCH_SETUP_STACK(thread->sp);
    GST_ARCH_SET_SP(thread->sp);
    // start it
    GST_ARCH_CALL(cothread_stub);
    GST_DEBUG (0,"exit thread \n");
    ctx->current = 0;
  }

  return;

#ifdef COTHREAD_PARANOID
nothread:
  g_print("cothread: there's no thread, strange...\n");
  return;
nocontext:
  g_print("cothread: there's no context, help!\n");
  exit(2);
nocurrent:
  g_print("cothread: there's no current thread, help!\n");
  exit(2);
#endif /* COTHREAD_PARANOID */
selfswitch:
  g_print("cothread: trying to switch to same thread, legal but not necessary\n");
  return;
}


void
cothread_lock (cothread_state *thread)
{
#ifdef COTHREAD_ATOMIC
  // do something to lock the cothread
#else
  g_mutex_lock(thread->lock);
#endif
}

gboolean
cothread_trylock (cothread_state *thread)
{
#ifdef COTHREAD_ATOMIC
  // do something to try to lock the cothread
#else
  return g_mutex_trylock(thread->lock);
#endif
}

void
cothread_unlock (cothread_state *thread)
{
#ifdef COTHREAD_ATOMIC
  // do something to unlock the cothread
#else
  g_mutex_unlock(thread->lock);
#endif
}

