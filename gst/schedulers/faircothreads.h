/* GStreamer
 * Copyright (C) 2004 Martin Soto <martinsoto@users.sourceforge.net>
 *
 * faircothread.h: High level cothread implementation for the fair scheduler.
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

#ifndef __FAIRCOTHREADS_H__
#define __FAIRCOTHREADS_H__


#ifdef _COTHREADS_PTH
#include "pth-cothreads.h"
#else
#define GTHREAD_COTHREADS_NO_DEFINITIONS
#include "cothreads_compat.h"
#endif


typedef struct _GstFairSchedulerCothread GstFairSchedulerCothread;
typedef struct _GstFairSchedulerCothreadQueue GstFairSchedulerCothreadQueue;

/* Possible states of a cothread. */
enum
{
  GST_FAIRSCHEDULER_CTSTATE_STOPPED,
  GST_FAIRSCHEDULER_CTSTATE_SUSPENDED,
  GST_FAIRSCHEDULER_CTSTATE_RUNNING,
};

/* Maximum number of cothread parameters. */
#define GST_FAIRSCHEDULER_MAX_CTARGS 7

/* Cothread function type. */
typedef void (*GstFairSchedulerCtFunc) (GstFairSchedulerCothread * ct,
    gpointer first_arg, ...);

struct _GstFairSchedulerCothread {
  GstFairSchedulerCothreadQueue *queue;
				/* Cothread queue this cothread
                                   belongs to. */
  GstFairSchedulerCtFunc func;  /* Cothread function. */
  char *argv[1 + GST_FAIRSCHEDULER_MAX_CTARGS]; /*
                                   Arguments for the cothread function.
                                   argv[0] is always the cothread
                                   object itself. */
  int argc;                     /* Number of stored parameters. */

  cothread *execst;             /* Execution state for this cothread. */
  gint state;                   /* Current cothread state. */
  gboolean sleeping;		/* Is this cothread sleeping? */

  GMutex *mutex;		/* If not null, a mutex to lock before
                                   giving control to this cothread. */

#ifndef GST_DISABLE_GST_DEBUG
  GString *readable_name;	/* Readable name for this cothread. */
  gint pid;			/* Process or thread id associated to
                                   this cothread. */
#endif
};

struct _GstFairSchedulerCothreadQueue {
  cothread_context *context;    /* Cothread context. */
  GQueue *ct_queue;             /* Queue of currently running
                                   cothreads. New cothreads are pushed
                                   on the tail. If a cothread is
                                   executing, it is the one in the
                                   head. */

  /* Asynchronous support. */
  GQueue *async_queue;		/* Queue storing asynchronous
                                   operations (operations on the queue
                                   requested potentially from other
                                   threads. */
  GMutex *async_mutex;		/* Mutex to protect acceses to
                                   async_queue. */
  GCond *new_async_op;		/* Condition variable to signal the
                                   presence of a new asynchronous
                                   operation in the queue. */  
};


extern GstFairSchedulerCothreadQueue *
gst_fair_scheduler_cothread_queue_new (void);

extern void
gst_fair_scheduler_cothread_queue_destroy (
    GstFairSchedulerCothreadQueue * queue);

extern void
gst_fair_scheduler_cothread_queue_start (
    GstFairSchedulerCothreadQueue * queue);

extern void
gst_fair_scheduler_cothread_queue_stop (
    GstFairSchedulerCothreadQueue * queue);

extern gboolean
gst_fair_scheduler_cothread_queue_iterate (
    GstFairSchedulerCothreadQueue * queue);

extern void
gst_fair_scheduler_cothread_queue_show (
    GstFairSchedulerCothreadQueue * queue);


extern GstFairSchedulerCothread *
gst_fair_scheduler_cothread_new (GstFairSchedulerCothreadQueue * queue,
    GstFairSchedulerCtFunc function, gpointer first_arg, ...);

extern void
gst_fair_scheduler_cothread_destroy (GstFairSchedulerCothread * ct);

extern void
gst_fair_scheduler_cothread_change_state (GstFairSchedulerCothread * ct,
    gint new_state);

extern void
gst_fair_scheduler_cothread_change_state_async (
    GstFairSchedulerCothread * ct, gint new_state);

extern void
gst_fair_scheduler_cothread_sleep (GstFairSchedulerCothreadQueue * queue);

extern void
gst_fair_scheduler_cothread_sleep_mutex (
    GstFairSchedulerCothreadQueue * queue, GMutex * mutex);

extern void
gst_fair_scheduler_cothread_yield (GstFairSchedulerCothreadQueue * queue);

extern void
gst_fair_scheduler_cothread_yield_mutex (
    GstFairSchedulerCothreadQueue * queue, GMutex * mutex);

extern void
gst_fair_scheduler_cothread_awake (GstFairSchedulerCothread * ct,
    gint priority);

extern void
gst_fair_scheduler_cothread_awake_async (GstFairSchedulerCothread * ct,
    gint priority);

extern GstFairSchedulerCothread *
gst_fair_scheduler_cothread_current (GstFairSchedulerCothreadQueue * queue);


#endif /* __FAIRCOTHREADS_H__ */
