/* GStreamer
 * Copyright (C) 2003 Benjamin Otte <in7y118@public.uni-hamburg.de>
 *
 * gthread-cothreads.c: cothreads implemented via GThread for compatibility
 *                      They're probably slooooooow
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
#include <gst/gstthread.h>

/* the name of this cothreads */
#define COTHREADS_TYPE		gthread
#define COTHREADS_NAME		"gthread"
#define COTHREADS_NAME_CAPITAL	"GThread"

/*
 * Theory of operation:
 * Instead of using cothreads, GThreads and 1 mutex are used.
 * Every thread may only run if it holds the mutex. Otherwise it holds its own
 * cond which has to be signaled to wakeit up.
 */

/* define "cothread", "cothread_context" and "cothread_func" */
typedef int (*cothread_func) (int, char **);

typedef struct _cothread cothread;
typedef struct _cothread_context cothread_context;

struct _cothread_context {
  GSList *              cothreads; /* contains all threads but main */
  cothread *            main;
  cothread *            current;
  GMutex *              mutex;
  GstThread *		gst_thread; /* the GstThread we're running from */
};

struct _cothread {
  GThread *             thread;
  GCond *               cond;
  cothread_func         run;
  int                   argc;
  char **               argv;
  cothread *            creator;
  gboolean              die;
  cothread_context *    context;
};

/* define functions
 * Functions starting with "do_" are used by the scheduler.
 */
static void             do_cothreads_init               (void *unused);
static cothread_context *do_cothread_context_init       (void);
static void             do_cothread_context_destroy     (cothread_context *context);
static cothread *       cothread_create                 (cothread_context *context,
                                                         cothread_func func,
                                                         int argc, 
                                                         char **argv);
#define do_cothread_create(new_cothread, context, func, argc, argv)         \
  G_STMT_START{                                                             \
    new_cothread = cothread_create ((context), (func), argc, (char**) (argv)); \
  }G_STMT_END
static void             do_cothread_switch              (cothread *to);
static void             do_cothread_destroy             (cothread *thread);
#define do_cothread_get_current(context) ((context)->current)
#define do_cothread_get_main(context) ((context)->main)

static void
do_cothreads_init (void *unused)
{
  if (!g_thread_supported ()) g_thread_init (NULL);   
}
static cothread_context *
do_cothread_context_init (void)
{
  cothread_context *ret = g_new0 (cothread_context, 1);

  ret->main = g_new0 (cothread, 1);
  ret->main->thread = g_thread_self ();
  ret->main->cond = g_cond_new ();
  ret->main->die = FALSE;
  ret->main->context = ret;
  ret->mutex = g_mutex_new ();
  ret->cothreads = NULL;
  ret->current = ret->main;
  ret->gst_thread = gst_thread_get_current();
  g_mutex_lock (ret->mutex);
  
  return ret;
}
static void
do_cothread_context_destroy (cothread_context *context)
{
  g_assert (g_thread_self() == context->main->thread);
  
  while (context->cothreads) {
    do_cothread_destroy ((cothread *) context->cothreads->data);
  }
  g_mutex_unlock (context->mutex);
  g_mutex_free (context->mutex);
  
  g_free (context);
}
static void
die (cothread *to_die) {
  g_cond_free (to_die->cond);
  to_die->context->cothreads = g_slist_remove (to_die->context->cothreads, to_die);
  g_free (to_die);
  g_thread_exit (to_die);
  /* don't unlock the mutex here, the thread waiting for us to die is gonna take it */
}
static gpointer
run_new_thread (gpointer data)
{
  cothread *self = (cothread *) data;
  
  g_mutex_lock (self->context->mutex);
  g_private_set (gst_thread_current, self->context->gst_thread);
  g_cond_signal (self->creator->cond);
  g_cond_wait (self->cond, self->context->mutex);
  if (self->die)
    die (self);
  while (TRUE) {
    self->run (self->argc, self->argv);
    /* compatibility */
    do_cothread_switch (do_cothread_get_main (self->context));
  }
  g_assert_not_reached ();
  return NULL;
}
static cothread *
cothread_create (cothread_context *context, cothread_func func, int argc, char **argv)
{
  cothread *ret;
  
  if ((ret = g_new (cothread, 1)) == NULL) {
    goto out1;
  }
  ret->cond = g_cond_new ();
  ret->run = func;
  ret->argc = argc;
  ret->argv = argv;
  ret->creator = do_cothread_get_current (context);
  ret->die = FALSE;
  ret->context = context; 
  context->cothreads = g_slist_prepend (context->cothreads, ret);  
  ret->thread = g_thread_create (run_new_thread, ret, TRUE, NULL);
  if (ret->thread == NULL) goto out2;
  g_cond_wait (do_cothread_get_current (context)->cond, context->mutex);
  return ret;

out2:
  context->cothreads = g_slist_remove (context->cothreads, ret);
  g_free (ret);
out1:
  return NULL;
}

static void do_cothread_switch (cothread *to)
{
  cothread *self = do_cothread_get_current(to->context);
  
  if (self == to) {
    g_warning ("trying to switch to the same cothread, not allowed");
  } else {
    self->context->current = to;
    g_cond_signal (to->cond);
    g_cond_wait (self->cond, self->context->mutex);
    if (self->die)
      die (self);
  }
}

#define do_cothread_setfunc(thread,context,_func,_argc,_argv) G_STMT_START {\
  ((cothread *)(thread))->run = (_func); \
  ((cothread *)(thread))->argc = _argc; \
  ((cothread *)(thread))->argv = _argv; \
}G_STMT_END

static void
do_cothread_destroy (cothread *thread)
{
  GThread *join;
  cothread_context *context;
  g_return_if_fail (thread != thread->context->main);
  g_return_if_fail (thread != thread->context->current);
  
  thread->die = TRUE;
  join = thread->thread;
  context = thread->context;
  g_cond_signal (thread->cond);
  g_mutex_unlock (thread->context->mutex);
  g_thread_join (join);
  /* the mutex was locked by the thread that we joined, no need to lock again */
}
  
#define do_cothread_get_current(context) ((context)->current)
#define do_cothread_get_main(context) ((context)->main)
