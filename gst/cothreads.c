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

#include <glib.h>

#include <sys/time.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <setjmp.h>
#include <unistd.h>
#include <errno.h>
#include <sys/mman.h>

#include "gst_private.h"

#include "cothreads.h"
#include "gstarch.h"
#include "gstinfo.h"
#include "gstutils.h"

#ifdef HAVE_UCONTEXT_H
#include <ucontext.h>
#endif

#ifndef MAP_ANONYMOUS
#ifdef MAP_ANON
/* older glibc's have MAP_ANON instead of MAP_ANONYMOUS */
#define MAP_ANONYMOUS MAP_ANON
#else
/* make due without.  If this fails, we need to open and map /dev/zero */
#define MAP_ANONYMOUS 0
#endif
#endif

#define STACK_SIZE 0x200000

#define COTHREAD_MAGIC_NUMBER 0xabcdef

#define COTHREAD_MAXTHREADS 16
#define COTHREAD_STACKSIZE (STACK_SIZE/COTHREAD_MAXTHREADS)

static void cothread_destroy (cothread_state * cothread);

struct _cothread_context
{
  cothread_state *cothreads[COTHREAD_MAXTHREADS];       /* array of cothread states */
  int ncothreads;
  int current;
  unsigned long stack_top;
  GHashTable *data;
  GThread *thread;
};

/* Disabling this define allows you to shut off a few checks in
 * cothread_switch.  This likely will speed things up fractionally */
#define COTHREAD_PARANOID


/* this _cothread_ctx_key is used as a GThread key to the thread's context
 * a GThread key is a "pointer" to memory space that is/can be different
 * (ie. private) for each thread.  The key itself is shared among threads,
 * so it only needs to be initialized once.
 */
static GStaticPrivate _cothread_ctx_key = G_STATIC_PRIVATE_INIT;

/*
 * This should only after context init, since we do checking.
 */
static cothread_context *
cothread_get_current_context (void)
{
  cothread_context *ctx;

  ctx = g_static_private_get (&_cothread_ctx_key);
  g_assert (ctx);

#ifdef COTHREAD_PARANOID
  g_assert (ctx->thread == g_thread_self ());
#endif

  return ctx;
}

/**
 * cothread_context_init:
 *
 * Create and initialize a new cothread context 
 *
 * Returns: the new cothread context
 */
cothread_context *
cothread_context_init (void)
{
  char __csf;
  void *current_stack_frame = &__csf;   /* Get pointer inside current stack frame */
  cothread_context *ctx;

  /* if there already is a cotread context for this thread,
   * just return it */
  ctx = g_static_private_get (&_cothread_ctx_key);
  if (ctx) {
    GST_CAT_INFO (GST_CAT_COTHREADS,
        "returning private _cothread_ctx_key %p", ctx);
    return ctx;
  }

  /*
   * initalize the whole of the cothreads context 
   */
  ctx = (cothread_context *) g_malloc (sizeof (cothread_context));

  /* we consider the initiating process to be cothread 0 */
  ctx->ncothreads = 1;
  ctx->current = 0;
  ctx->data = g_hash_table_new (g_str_hash, g_str_equal);
  ctx->thread = g_thread_self ();

  GST_CAT_INFO (GST_CAT_COTHREADS, "initializing cothreads");

  /* set this thread's context pointer */
  GST_CAT_INFO (GST_CAT_COTHREADS,
      "setting private _cothread_ctx_key to %p in thread %p", ctx,
      g_thread_self ());
  g_static_private_set (&_cothread_ctx_key, ctx, NULL);

  g_assert (ctx == cothread_get_current_context ());

  /* clear the cothread data */
  memset (ctx->cothreads, 0, sizeof (ctx->cothreads));

  /* FIXME this may not be 64bit clean
   *       could use casts to uintptr_t from inttypes.h
   *       if only all platforms had inttypes.h
   */
  /* stack_top is the address of the first byte past our stack segment. */
  /* FIXME: an assumption is made that the stack segment is STACK_SIZE
   * aligned. */
  ctx->stack_top = ((gulong) current_stack_frame | (STACK_SIZE - 1)) + 1;
  GST_CAT_DEBUG (GST_CAT_COTHREADS, "stack top is 0x%08lx", ctx->stack_top);

  /*
   * initialize the 0th cothread
   */
  ctx->cothreads[0] = (cothread_state *) g_malloc0 (sizeof (cothread_state));
  ctx->cothreads[0]->ctx = ctx;
  ctx->cothreads[0]->cothreadnum = 0;
  ctx->cothreads[0]->func = NULL;
  ctx->cothreads[0]->argc = 0;
  ctx->cothreads[0]->argv = NULL;
  ctx->cothreads[0]->priv = NULL;
  ctx->cothreads[0]->flags = COTHREAD_STARTED;
  ctx->cothreads[0]->sp = (void *) current_stack_frame;

  GST_CAT_INFO (GST_CAT_COTHREADS, "0th cothread is %p at sp:%p",
      ctx->cothreads[0], ctx->cothreads[0]->sp);

  return ctx;
}

/**
 * cothread_context_free:
 * @ctx: the cothread context to free
 *
 * Free the cothread context.
 */
void
cothread_context_free (cothread_context * ctx)
{
  gint i;

  g_return_if_fail (ctx != NULL);
  g_assert (ctx->thread == g_thread_self ());
  g_assert (ctx->current == 0);

  GST_CAT_INFO (GST_CAT_COTHREADS, "free cothread context");

  for (i = 1; i < COTHREAD_MAXTHREADS; i++) {
    if (ctx->cothreads[i]) {
      cothread_destroy (ctx->cothreads[i]);
    }
  }
  if (ctx->cothreads[0]) {
    g_free (ctx->cothreads[0]);
    ctx->cothreads[0] = NULL;
  }
  g_hash_table_destroy (ctx->data);
  /* make sure we free the private key for cothread context */
  GST_CAT_INFO (GST_CAT_COTHREADS,
      "setting private _cothread_ctx_key to NULL in thread %p",
      g_thread_self ());
  g_static_private_set (&_cothread_ctx_key, NULL, NULL);
  g_free (ctx);
}

/**
 * cothread_create:
 * @ctx: the cothread context
 *
 * Create a new cothread state in the given context
 *
 * Returns: the new cothread state or NULL on error
 */
cothread_state *
cothread_create (cothread_context * ctx)
{
  cothread_state *cothread;
  void *mmaped = 0;
  gint slot = 0;
  unsigned long page_size;

  g_return_val_if_fail (ctx != NULL, NULL);

  GST_CAT_DEBUG (GST_CAT_COTHREADS, "manager sef %p, cothread self %p",
      ctx->thread, g_thread_self ());

  if (ctx->ncothreads == COTHREAD_MAXTHREADS) {
    /* this is pretty fatal */
    g_warning ("cothread_create: attempt to create > COTHREAD_MAXTHREADS");
    return NULL;
  }
  /* find a free spot in the stack, note slot 0 has the main thread */
  for (slot = 1; slot < ctx->ncothreads; slot++) {
    if (ctx->cothreads[slot] == NULL)
      break;
    else if (ctx->cothreads[slot]->flags & COTHREAD_DESTROYED &&
        slot != ctx->current) {
      cothread_destroy (ctx->cothreads[slot]);
      break;
    }
  }

  GST_CAT_DEBUG (GST_CAT_COTHREADS, "Found free cothread slot %d", slot);

  /* cothread stack space of the thread is mapped in reverse, with cothread 0
   * stack space at the top */
  cothread =
      (cothread_state *) (ctx->stack_top - (slot + 1) * COTHREAD_STACKSIZE);
  GST_CAT_DEBUG (GST_CAT_COTHREADS, "cothread pointer is %p", cothread);

#if 0
  /* This tests to see whether or not we can grow down the stack */
  {
    unsigned long ptr;

    for (ptr = ctx->stack_top - 4096; ptr > (unsigned long) cothread;
        ptr -= 4096) {
      GST_CAT_DEBUG (GST_CAT_COTHREADS, "touching location 0x%08lx", ptr);
      *(volatile unsigned int *) ptr = *(volatile unsigned int *) ptr;
      GST_CAT_DEBUG (GST_CAT_COTHREADS, "ok (0x%08x)", *(unsigned int *) ptr);
    }
  }
#endif

#ifdef _SC_PAGESIZE
  page_size = sysconf (_SC_PAGESIZE);
#else
  page_size = getpagesize ();
#endif

  /* The mmap is necessary on Linux/i386, and possibly others, since the
   * kernel is picky about when we can expand our stack. */
  GST_CAT_DEBUG (GST_CAT_COTHREADS, "mmaping %p, size 0x%08x", cothread,
      COTHREAD_STACKSIZE);
  /* Remap with a guard page. This decreases our stack size by 8 kB (for
   * 4 kB pages) and also wastes almost 4 kB for the cothreads
   * structure */
  munmap ((void *) cothread, COTHREAD_STACKSIZE);
  mmaped = mmap ((void *) cothread, page_size,
      PROT_READ | PROT_WRITE, MAP_FIXED | MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  mmaped = mmap (((void *) cothread) + page_size * 2,
      COTHREAD_STACKSIZE - page_size * 2,
      PROT_READ | PROT_WRITE | PROT_EXEC,
      MAP_FIXED | MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  GST_CAT_DEBUG (GST_CAT_COTHREADS, "coming out of mmap");
  if (mmaped == MAP_FAILED) {
    perror ("mmap'ing cothread stack space");
    return NULL;
  }
  if (mmaped != (void *) cothread + page_size * 2) {
    g_warning ("could not mmap requested memory for cothread");
    return NULL;
  }

  cothread->magic_number = COTHREAD_MAGIC_NUMBER;
  GST_CAT_DEBUG (GST_CAT_COTHREADS,
      "create  cothread %d with magic number 0x%x", slot,
      cothread->magic_number);
  cothread->ctx = ctx;
  cothread->cothreadnum = slot;
  cothread->flags = 0;
  cothread->priv = NULL;
  cothread->sp = ((guchar *) cothread + COTHREAD_STACKSIZE);
  cothread->stack_size = COTHREAD_STACKSIZE - page_size * 2;
  cothread->stack_base = (void *) cothread + 2 * page_size;

  GST_CAT_INFO (GST_CAT_COTHREADS,
      "created cothread #%d in slot %d: %p at sp:%p",
      ctx->ncothreads, slot, cothread, cothread->sp);

  ctx->cothreads[slot] = cothread;
  ctx->ncothreads++;

  return cothread;
}

/**
 * cothread_free:
 * @cothread: the cothread state
 *
 * Free the given cothread state
 */
void
cothread_free (cothread_state * cothread)
{
  g_return_if_fail (cothread != NULL);

  GST_CAT_INFO (GST_CAT_COTHREADS, "flag cothread %d for destruction",
      cothread->cothreadnum);

  /* we simply flag the cothread for destruction here */
  if (cothread)
    cothread->flags |= COTHREAD_DESTROYED;
  else
    g_warning ("somebody set up us the bomb");
}

static void
cothread_destroy (cothread_state * cothread)
{
  cothread_context *ctx;
  gint cothreadnum;

  g_return_if_fail (cothread != NULL);

  cothreadnum = cothread->cothreadnum;
  ctx = cothread->ctx;
  g_assert (ctx->thread == g_thread_self ());
  g_assert (ctx == cothread_get_current_context ());

  GST_CAT_INFO (GST_CAT_COTHREADS, "destroy cothread %d %p %d",
      cothreadnum, cothread, ctx->current);

  /* cothread 0 needs to be destroyed specially */
  g_assert (cothreadnum != 0);

  /* doing cleanups of the cothread create */
  GST_CAT_DEBUG (GST_CAT_COTHREADS,
      "destroy cothread %d with magic number 0x%x", cothreadnum,
      cothread->magic_number);
  g_assert (cothread->magic_number == COTHREAD_MAGIC_NUMBER);

  g_assert (cothread->priv == NULL);

  memset (cothread, 0, sizeof (*cothread));

  ctx->cothreads[cothreadnum] = NULL;
  ctx->ncothreads--;
}

/**
 * cothread_setfunc:
 * @cothread: the cothread state
 * @func: the function to call
 * @argc: argument count for the cothread function
 * @argv: arguments for the cothread function
 *
 * Set the cothread function
 */
void
cothread_setfunc (cothread_state * cothread, cothread_func func, int argc,
    char **argv)
{
  cothread->func = func;
  cothread->argc = argc;
  cothread->argv = argv;
}

/**
 * cothread_stop:
 * @cothread: the cothread to stop
 *
 * Stop the cothread and reset the stack and program counter.
 */
void
cothread_stop (cothread_state * cothread)
{
  cothread->flags &= ~COTHREAD_STARTED;
}

/**
 * cothread_main:
 * @ctx: cothread context to find main cothread of.
 *
 * Gets the main thread.
 *
 * Returns: the #cothread_state of the main (0th) cothread.
 */
cothread_state *
cothread_main (cothread_context * ctx)
{
  g_assert (ctx->thread == g_thread_self ());

  GST_CAT_DEBUG (GST_CAT_COTHREADS, "returning %p, the 0th cothread",
      ctx->cothreads[0]);
  return ctx->cothreads[0];
}

/**
 * cothread_current_main:
 *
 * Get the main thread in the current GThread.
 *
 * Returns: the #cothread_state of the main (0th) thread in the current GThread
 */
cothread_state *
cothread_current_main (void)
{
  cothread_context *ctx = cothread_get_current_context ();

  return ctx->cothreads[0];
}

/**
 * cothread_current:
 *
 * Get the currenttly executing cothread
 *
 * Returns: the #cothread_state of the current cothread
 */
cothread_state *
cothread_current (void)
{
  cothread_context *ctx = cothread_get_current_context ();

  return ctx->cothreads[ctx->current];
}

static void
cothread_stub (void)
{
  cothread_context *ctx = cothread_get_current_context ();
  cothread_state *cothread = ctx->cothreads[ctx->current];

#ifndef GST_DISABLE_GST_DEBUG
  char __csf;
  void *current_stack_frame = &__csf;
#endif

  GST_CAT_DEBUG (GST_CAT_COTHREADS, "stack addr %p", &ctx);

  cothread->flags |= COTHREAD_STARTED;

  while (TRUE) {
    cothread->func (cothread->argc, cothread->argv);

    GST_CAT_DEBUG (GST_CAT_COTHREADS, "cothread[%d] thread->func exited",
        ctx->current);

    GST_CAT_DEBUG (GST_CAT_COTHREADS, "sp=%p", current_stack_frame);
    GST_CAT_DEBUG (GST_CAT_COTHREADS, "ctx=%p current=%p", ctx,
        cothread_get_current_context ());
    g_assert (ctx == cothread_get_current_context ());

    g_assert (ctx->current != 0);

    /* we do this to avoid ever returning, we just switch to 0th thread */
    cothread_switch (cothread_main (ctx));
  }
}

/**
 * cothread_getcurrent:
 *
 * Get the current cothread id
 *
 * Returns: the current cothread id
 */
int
cothread_getcurrent (void)
    G_GNUC_NO_INSTRUMENT;

     int cothread_getcurrent (void)
{
  cothread_context *ctx = cothread_get_current_context ();

  if (!ctx)
    return -1;

  return ctx->current;
}

/**
 * cothread_set_private:
 * @cothread: the cothread state
 * @data: the data
 *
 * set private data for the cothread.
 */
void
cothread_set_private (cothread_state * cothread, gpointer data)
{
  cothread->priv = data;
}

/**
 * cothread_context_set_data:
 * @cothread: the cothread state
 * @key: a key for the data
 * @data: the data
 *
 * adds data to a cothread
 */
void
cothread_context_set_data (cothread_state * cothread, gchar * key,
    gpointer data)
{
  cothread_context *ctx = cothread_get_current_context ();

  g_hash_table_insert (ctx->data, key, data);
}

/**
 * cothread_get_private:
 * @cothread: the cothread state
 *
 * get the private data from the cothread
 *
 * Returns: the private data of the cothread
 */
gpointer
cothread_get_private (cothread_state * cothread)
{
  return cothread->priv;
}

/**
 * cothread_context_get_data:
 * @cothread: the cothread state
 * @key: a key for the data
 *
 * get data from the cothread
 *
 * Returns: the data associated with the key
 */
gpointer
cothread_context_get_data (cothread_state * cothread, gchar * key)
{
  cothread_context *ctx = cothread_get_current_context ();

  return g_hash_table_lookup (ctx->data, key);
}

/**
 * cothread_switch:
 * @cothread: cothread state to switch to
 *
 * Switches to the given cothread state
 */
void
cothread_switch (cothread_state * cothread)
{
  cothread_context *ctx;
  cothread_state *current;
  int enter;

#ifdef COTHREAD_PARANOID
  if (cothread == NULL)
    goto nothread;
#endif
  ctx = cothread->ctx;

  /* paranoia check to make sure we're in the right thread */
  g_assert (ctx->thread == g_thread_self ());

#ifdef COTHREAD_PARANOID
  if (ctx == NULL)
    goto nocontext;
#endif

  current = ctx->cothreads[ctx->current];
#ifdef COTHREAD_PARANOID
  if (current == NULL)
    goto nocurrent;
#endif
  if (current == cothread)
    goto selfswitch;


  /* find the number of the thread to switch to */
  GST_CAT_INFO (GST_CAT_COTHREAD_SWITCH,
      "switching from cothread #%d to cothread #%d",
      ctx->current, cothread->cothreadnum);
  ctx->current = cothread->cothreadnum;

  /* save the current stack pointer, frame pointer, and pc */
#ifdef GST_ARCH_PRESETJMP
  GST_ARCH_PRESETJMP ();
#endif
  enter = setjmp (current->jmp);
  if (enter != 0) {
    GST_CAT_DEBUG (GST_CAT_COTHREADS,
        "enter cothread #%d %d sp=%p jmpbuf=%p",
        current->cothreadnum, enter, current->sp, current->jmp);
    return;
  }
  GST_CAT_DEBUG (GST_CAT_COTHREADS, "exit cothread #%d %d sp=%p jmpbuf=%p",
      current->cothreadnum, enter, current->sp, current->jmp);
  enter = 1;

  if (current->flags & COTHREAD_DESTROYED) {
    cothread_destroy (current);
  }

  GST_CAT_DEBUG (GST_CAT_COTHREADS, "set stack to %p", cothread->sp);
  /* restore stack pointer and other stuff of new cothread */
  if (cothread->flags & COTHREAD_STARTED) {
    GST_CAT_DEBUG (GST_CAT_COTHREADS, "via longjmp() jmpbuf %p", cothread->jmp);
    /* switch to it */
    longjmp (cothread->jmp, 1);
  } else {
#ifdef HAVE_MAKECONTEXT
    ucontext_t ucp;

    GST_CAT_DEBUG (GST_CAT_COTHREADS, "making context");

    g_assert (cothread != cothread_main (ctx));

    getcontext (&ucp);
    ucp.uc_stack.ss_sp = (void *) cothread->stack_base;
    ucp.uc_stack.ss_size = cothread->stack_size;
    makecontext (&ucp, cothread_stub, 0);
    setcontext (&ucp);
#else
    GST_ARCH_SETUP_STACK ((char *) cothread->sp);
    GST_ARCH_SET_SP (cothread->sp);
    /* start it */
    GST_ARCH_CALL (cothread_stub);
#endif

    GST_CAT_DEBUG (GST_CAT_COTHREADS, "exit thread ");
    ctx->current = 0;
  }

  return;

#ifdef COTHREAD_PARANOID
nothread:
  g_warning ("cothread: can't switch to NULL cothread!");
  return;
nocontext:
  g_warning ("cothread: there's no context, help!");
  exit (2);
nocurrent:
  g_warning ("cothread: there's no current thread, help!");
  exit (2);
#endif /* COTHREAD_PARANOID */
selfswitch:
  return;
}
