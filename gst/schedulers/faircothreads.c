/* GStreamer
 * Copyright (C) 2004 Martin Soto <martinsoto@users.sourceforge.net>
 *
 * faircothread.c: High level cothread implementation for the fair scheduler.
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

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <glib.h>

#include <gst/gst.h>

#ifdef _COTHREADS_PTH
#include "pth-cothreads.h"
#else
#include "cothreads_compat.h"
#endif

#include "faircothreads.h"

#if !defined(GST_DISABLE_GST_DEBUG) && defined(FAIRSCHEDULER_USE_GETTID)
#include <sys/types.h>
#include <linux/unistd.h>

_syscall0 (pid_t, gettid)
#endif
    GST_DEBUG_CATEGORY_EXTERN (debug_fair_ct);
#define GST_CAT_DEFAULT debug_fair_ct


/*
 * Support for Asynchronous Operations
 */

     enum
     {
       ASYNC_OP_CHANGE_STATE = 1,
       ASYNC_OP_AWAKE
     };

     typedef struct _AsyncOp AsyncOp;
     typedef struct _AsyncOpChangeState AsyncOpChangeState;
     typedef struct _AsyncOpAwake AsyncOpAwake;

     struct _AsyncOp
     {
       int type;
     };

     struct _AsyncOpChangeState
     {
       AsyncOp parent;
       GstFairSchedulerCothread *ct;    /* Cothread whose state will be
                                           changed. */
       gint new_state;          /* New state for the cothread. */
     };

     struct _AsyncOpAwake
     {
       AsyncOp parent;
       GstFairSchedulerCothread *ct;    /* Cothread to awake. */
       gint priority;           /* Priority for the cothread. */
     };


     static gchar *gst_fairscheduler_ct_state_names[] = {
       "stopped",
       "suspended",
       "running"
     };


/*
 * Helpers
 */

static int
cothread_base_func (int argc, char **argv)
{
  GstFairSchedulerCothread *ct;

  g_return_val_if_fail (argc >= 1, -1);

  ct = (GstFairSchedulerCothread *) argv[0];

  GST_INFO ("queue %p: Cothread %p starting", ct->queue, ct);
#ifndef GST_DISABLE_GST_DEBUG
#ifdef FAIRSCHEDULER_USE_GETTID
  ct->pid = gettid ();
#else
  ct->pid = 0;
#endif
#endif

  /* Call the thread function. This looks sort of funny, but there's
     no other way I know of doing it. */
  switch (argc - 1) {
    case 0:
      ct->func (ct, NULL);
      break;
    case 1:
      ct->func (ct, argv[1], NULL);
      break;
    case 2:
      ct->func (ct, argv[1], argv[2], NULL);
      break;
    case 3:
      ct->func (ct, argv[1], argv[2], argv[3], NULL);
      break;
    case 4:
      ct->func (ct, argv[1], argv[2], argv[3], argv[4], NULL);
      break;
    case 5:
      ct->func (ct, argv[1], argv[2], argv[3], argv[4], argv[5], NULL);
      break;
    case 6:
      ct->func (ct, argv[1], argv[2], argv[3], argv[4], argv[5], argv[6], NULL);
      break;
    case 7:
      ct->func (ct, argv[1], argv[2], argv[3], argv[4], argv[5],
          argv[6], argv[7], NULL);
      break;
    default:
      g_return_val_if_reached (-1);
      break;
  }

  /* After the cothread function is finished, we go to the stopped
     state. */
  gst_fair_scheduler_cothread_change_state (ct,
      GST_FAIRSCHEDULER_CTSTATE_STOPPED);

  return 0;
}


static void
cothread_activate (GstFairSchedulerCothread * ct, gint priority)
{
  GST_DEBUG ("queue %p: activating cothread %p", ct->queue, ct);

  if (priority > 0) {
    g_queue_push_head (ct->queue->ct_queue, ct);
  } else {
    g_queue_push_tail (ct->queue->ct_queue, ct);
  }
}


static void
cothread_deactivate (GstFairSchedulerCothread * ct)
{
  GList *node;

  GST_DEBUG ("queue %p: deactivating cothread %p", ct->queue, ct);

  /* Find the node. */
  node = g_list_find (ct->queue->ct_queue->head, ct);
  if (node == NULL) {
    return;
  }

  if (node->next == NULL) {
    g_queue_pop_tail (ct->queue->ct_queue);
  } else {
    ct->queue->ct_queue->head =
        g_list_remove_link (ct->queue->ct_queue->head, node);
  }
}


static void
queue_async_op (GstFairSchedulerCothreadQueue * queue, AsyncOp * op)
{
  g_mutex_lock (queue->async_mutex);
  g_queue_push_tail (queue->async_queue, op);
  g_cond_signal (queue->new_async_op);
  g_mutex_unlock (queue->async_mutex);
}


/*
 * Cothreads API
 */

extern GstFairSchedulerCothreadQueue *
gst_fair_scheduler_cothread_queue_new (void)
{
  GstFairSchedulerCothreadQueue *new;

  new = g_malloc (sizeof (GstFairSchedulerCothreadQueue));

  new->context = NULL;
  new->ct_queue = g_queue_new ();

  new->async_queue = g_queue_new ();
  new->async_mutex = g_mutex_new ();
  new->new_async_op = g_cond_new ();

  return new;
}


extern void
gst_fair_scheduler_cothread_queue_destroy (GstFairSchedulerCothreadQueue *
    queue)
{
  GList *iter;

  /* Destroy all remaining cothreads. */
  for (iter = queue->ct_queue->head; iter != NULL; iter = iter->next) {
    gst_fair_scheduler_cothread_destroy (
        (GstFairSchedulerCothread *) iter->data);
  }
  g_queue_free (queue->ct_queue);

  for (iter = queue->async_queue->head; iter != NULL; iter = iter->next) {
    g_free (iter->data);
  }
  g_queue_free (queue->async_queue);

  g_mutex_free (queue->async_mutex);
  g_cond_free (queue->new_async_op);

  g_free (queue);
}


extern void
gst_fair_scheduler_cothread_queue_start (GstFairSchedulerCothreadQueue * queue)
{
  if (queue->context == NULL) {
    do_cothreads_init (NULL);
    queue->context = do_cothread_context_init ();
  }
}


extern void
gst_fair_scheduler_cothread_queue_stop (GstFairSchedulerCothreadQueue * queue)
{
  if (queue->context != NULL) {
    do_cothread_context_destroy (queue->context);
  }
}


gboolean
gst_fair_scheduler_cothread_queue_iterate (GstFairSchedulerCothreadQueue *
    queue)
{
  GstFairSchedulerCothread *ct;

  g_return_val_if_fail (queue->context != NULL, FALSE);

  GST_LOG ("queue %p: iterating", queue);

  /* Perform any pending asynchronous operations. Checking the queue
     is safe and more efficient without locking the mutex. */
  if (!g_queue_is_empty (queue->async_queue)) {
    AsyncOp *basic_op;

    GST_LOG ("queue %p: processing asynchronous operations", queue);

    g_mutex_lock (queue->async_mutex);

    while (!g_queue_is_empty (queue->async_queue)) {
      basic_op = (AsyncOp *) g_queue_pop_head (queue->async_queue);

      switch (basic_op->type) {
        case ASYNC_OP_CHANGE_STATE:
        {
          AsyncOpChangeState *op = (AsyncOpChangeState *) basic_op;

          gst_fair_scheduler_cothread_change_state (op->ct, op->new_state);
        }
          break;
        case ASYNC_OP_AWAKE:
        {
          AsyncOpAwake *op = (AsyncOpAwake *) basic_op;

          gst_fair_scheduler_cothread_awake (op->ct, op->priority);
        }
          break;
        default:
          g_return_val_if_reached (FALSE);
          break;
      }

      g_free (basic_op);
    }

    g_mutex_unlock (queue->async_mutex);
  }

  /* First cothread in the queue (if any) should get control. */
  ct = g_queue_peek_head (queue->ct_queue);

  if (ct == NULL) {
    GTimeVal timeout;

    g_get_current_time (&timeout);
    g_time_val_add (&timeout, 5000);

    /* No cothread available, wait until some other thread queues an
       operation. */
    g_mutex_lock (queue->async_mutex);
    g_cond_timed_wait (queue->new_async_op, queue->async_mutex, &timeout);
    g_mutex_unlock (queue->async_mutex);

    return FALSE;
  }

  g_return_val_if_fail (ct->state == GST_FAIRSCHEDULER_CTSTATE_RUNNING, FALSE);

  /* Check for a cothread mutex. */
  if (ct->mutex != NULL) {
    g_mutex_lock (ct->mutex);
    ct->mutex = NULL;
  }

  GST_LOG ("queue %p: giving control to %p", queue, ct);

  /* Handle control to the cothread. */
  do_cothread_switch (ct->execst);

  return TRUE;
}


void
gst_fair_scheduler_cothread_queue_show (GstFairSchedulerCothreadQueue * queue)
{
  GList *iter;
  GstFairSchedulerCothread *ct;

  g_print ("\n  Running cothreads (last is active):\n");

  for (iter = queue->ct_queue->tail; iter != NULL; iter = iter->prev) {
    ct = (GstFairSchedulerCothread *) iter->data;
#ifndef GST_DISABLE_GST_DEBUG
    g_print ("    %p: %s (%d)\n", ct, ct->readable_name->str, ct->pid);
#endif
  }
}


GstFairSchedulerCothread *
gst_fair_scheduler_cothread_new (GstFairSchedulerCothreadQueue * queue,
    GstFairSchedulerCtFunc function, gpointer first_arg, ...)
{
  GstFairSchedulerCothread *new;
  va_list ap;
  gpointer arg;

  new = g_malloc (sizeof (GstFairSchedulerCothread));

  new->queue = queue;
  new->func = function;

  /* The first parameter is always the cothread structure itself. */
  new->argv[0] = (char *) new;
  new->argc = 1;

  /* Store the parameters. */
  va_start (ap, first_arg);
  arg = first_arg;
  while (new->argc < GST_FAIRSCHEDULER_MAX_CTARGS && arg != NULL) {
    new->argv[new->argc] = (char *) arg;
    new->argc++;
    arg = va_arg (ap, gpointer);
  }

  /* Make sure we don't have more parameters than we can handle. */
  g_return_val_if_fail (arg == NULL, NULL);

  /* Creation of the actual execution state is defered to transition
     to running/suspended. */
  new->execst = NULL;

  /* All cothreads are created in the stopped state. */
  new->state = GST_FAIRSCHEDULER_CTSTATE_STOPPED;

  new->mutex = NULL;

#ifndef GST_DISABLE_GST_DEBUG
  new->readable_name = g_string_new ("");
  new->pid = 0;
#endif

  GST_DEBUG ("queue %p: cothread %p created", queue, new);

  return new;
}


void
gst_fair_scheduler_cothread_destroy (GstFairSchedulerCothread * ct)
{
  GST_DEBUG ("queue %p: destroying cothread %p", ct->queue, ct);

  if (ct->state != GST_FAIRSCHEDULER_CTSTATE_STOPPED) {
    cothread_deactivate (ct);
  }

  if (ct->execst != NULL) {
    do_cothread_destroy (ct->execst);
  }
#ifndef GST_DISABLE_GST_DEBUG
  g_string_free (ct->readable_name, TRUE);
#endif

  g_free (ct);
}


void
gst_fair_scheduler_cothread_change_state (GstFairSchedulerCothread * ct,
    gint new_state)
{
  if (new_state == ct->state) {
    return;
  }

  GST_DEBUG ("queue %p: changing state of %p from %s to %s", ct->queue, ct,
      gst_fairscheduler_ct_state_names[ct->state],
      gst_fairscheduler_ct_state_names[new_state]);

  switch (ct->state) {
    case GST_FAIRSCHEDULER_CTSTATE_STOPPED:
      /* (Re)Initialize the cothread. */
      if (ct->execst == NULL) {
        /* Initialize cothread's execution state. */
        do_cothread_create (ct->execst, ct->queue->context,
            cothread_base_func, ct->argc, ct->argv);
        GST_LOG_OBJECT (ct->queue,
            "cothread %p has exec state %p", ct, ct->execst);
      } else {
        /* Reset cothread's execution state. */
        do_cothread_setfunc (ct->execst, ct->queue->context,
            cothread_base_func, ct->argc, ct->argv);
      }

      ct->sleeping = FALSE;

      if (new_state == GST_FAIRSCHEDULER_CTSTATE_RUNNING) {
        cothread_activate (ct, 0);
      }

      break;

    case GST_FAIRSCHEDULER_CTSTATE_RUNNING:
      if (!ct->sleeping) {
        cothread_deactivate (ct);
      }
      break;

    case GST_FAIRSCHEDULER_CTSTATE_SUSPENDED:
      if (new_state == GST_FAIRSCHEDULER_CTSTATE_RUNNING && !ct->sleeping) {
        cothread_activate (ct, 0);
      }
      break;
  }

  ct->state = new_state;
}


void
gst_fair_scheduler_cothread_change_state_async (GstFairSchedulerCothread * ct,
    gint new_state)
{
  AsyncOpChangeState *op;

  /* Queue an asynchronous operation. */
  op = g_new (AsyncOpChangeState, 1);
  op->parent.type = ASYNC_OP_CHANGE_STATE;
  op->ct = ct;
  op->new_state = new_state;

  queue_async_op (ct->queue, (AsyncOp *) op);
}


void
gst_fair_scheduler_cothread_sleep (GstFairSchedulerCothreadQueue * queue)
{
  gst_fair_scheduler_cothread_sleep_mutex (queue, NULL);
}


/*
 *  Go to sleep but unblock the mutex while sleeping.
 */
void
gst_fair_scheduler_cothread_sleep_mutex (GstFairSchedulerCothreadQueue * queue,
    GMutex * mutex)
{
  GstFairSchedulerCothread *ct;

  g_return_if_fail (queue->context != NULL);

  /* The sleep operation can be invoked when the cothread is already
     deactivated. */
  ct = gst_fair_scheduler_cothread_current (queue);
  if (ct != NULL && ct->execst == do_cothread_get_current (queue->context)) {
    ct = g_queue_pop_head (queue->ct_queue);
    ct->sleeping = TRUE;
  }

  ct->mutex = mutex;
  if (mutex != NULL) {
    g_mutex_unlock (mutex);
  }

  GST_LOG ("queue %p: cothread going to sleep", queue);

  /* Switch back to the main cothread. */
  do_cothread_switch (do_cothread_get_main (queue->context));
}


void
gst_fair_scheduler_cothread_yield (GstFairSchedulerCothreadQueue * queue)
{
  gst_fair_scheduler_cothread_yield_mutex (queue, NULL);
}


void
gst_fair_scheduler_cothread_yield_mutex (GstFairSchedulerCothreadQueue * queue,
    GMutex * mutex)
{
  GstFairSchedulerCothread *ct;

  g_return_if_fail (queue->context != NULL);

  /* The yield operation can be invoked when the cothread is already
     deactivated. */
  ct = gst_fair_scheduler_cothread_current (queue);
  if (ct != NULL && ct->execst == do_cothread_get_current (queue->context)) {
    ct = g_queue_pop_head (queue->ct_queue);
    g_queue_push_tail (queue->ct_queue, ct);
  }

  ct->mutex = mutex;
  if (mutex != NULL) {
    g_mutex_unlock (mutex);
  }

  GST_LOG ("queue %p: cothread yielding control", queue);

  /* Switch back to the main cothread. */
  do_cothread_switch (do_cothread_get_main (queue->context));
}


void
gst_fair_scheduler_cothread_awake (GstFairSchedulerCothread * ct, gint priority)
{
  g_return_if_fail (ct->state != GST_FAIRSCHEDULER_CTSTATE_STOPPED);

  if (!ct->sleeping) {
    /* Cothread is already awake. */
    return;
  }

  ct->sleeping = FALSE;

  if (ct->state == GST_FAIRSCHEDULER_CTSTATE_RUNNING) {
    cothread_activate (ct, priority);
  }
}


void
gst_fair_scheduler_cothread_awake_async (GstFairSchedulerCothread * ct,
    gint priority)
{
  AsyncOpAwake *op;

  /* Queue an asynchronous operation. */
  op = g_new (AsyncOpAwake, 1);
  op->parent.type = ASYNC_OP_AWAKE;
  op->ct = ct;
  op->priority = priority;

  queue_async_op (ct->queue, (AsyncOp *) op);
}


GstFairSchedulerCothread *
gst_fair_scheduler_cothread_current (GstFairSchedulerCothreadQueue * queue)
{
  return g_queue_peek_head (queue->ct_queue);
}
