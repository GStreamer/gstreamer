/* GStreamer
 * Copyright (C) 2004 Martin Soto <martinsoto@users.sourceforge.net>
 *
 * gstfairscheduler.c: Fair cothread based scheduler
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
#include <gst/gstqueue.h>

#include "faircothreads.h"


GST_DEBUG_CATEGORY_STATIC (debug_fair);
#define GST_CAT_DEFAULT debug_fair
GST_DEBUG_CATEGORY (debug_fair_ct);
GST_DEBUG_CATEGORY_STATIC (debug_fair_queues);


typedef struct _GstFairScheduler GstFairScheduler;
typedef struct _GstFairSchedulerClass GstFairSchedulerClass;
typedef struct _GstFairSchedulerPrivElem GstFairSchedulerPrivElem;
typedef struct _GstFairSchedulerPrivLink GstFairSchedulerPrivLink;
typedef struct _GstFairSchedulerWaitEntry GstFairSchedulerWaitEntry;


#define GST_TYPE_FAIR_SCHEDULER \
  (gst_fair_scheduler_get_type())
#define GST_FAIR_SCHEDULER(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_FAIR_SCHEDULER,GstFairScheduler))
#define GST_FAIR_SCHEDULER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_FAIR_SCHEDULER,GstFairSchedulerClass))
#define GST_IS_FAIR_SCHEDULER(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_FAIR_SCHEDULER))
#define GST_IS_FAIR_SCHEDULER_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_FAIR_SCHEDULER))


/* Private scheduler data associated to an element. */
struct _GstFairSchedulerPrivElem
{
  GstFairSchedulerCothread *elem_ct;
  /* Element's cothread. */
  GArray *chain_get_pads;       /* Pads in this element with either a
                                   get or a chain function. */
};

#define ELEM_PRIVATE(element) \
  ((GstFairSchedulerPrivElem *) GST_ELEMENT(element)->sched_private)


/* Private scheduler data associated to a pad link. This structure is
   always stored in the source pad of the link. */
struct _GstFairSchedulerPrivLink
{
  GstFairScheduler *owner;      /* The "owner" of this link. */

  GstData *bufpen;              /* A placeholder for one buffer. */
  GstFairSchedulerCothread *waiting_writer;
  /* Cothread waiting to write. */
  GstFairSchedulerCothread *waiting_reader;
  /* Cothread waiting to read. */

  GstFairSchedulerCothread *decoupled_ct;
  /* Cothread to handle the decoupled
     pad in this link (if any). */
  gulong decoupled_signal_id;   /* Id for the signal handler
                                   responsible for managing the
                                   cothread. */

  /* Queue optimizations. */
  gulong queue_blocked_signal_id;
  /* Id for the signal handler connected
     to the under/overrun signal of a
     queue. */
  GstFairSchedulerCothread *waiting_for_queue;
  /* Cothread waiting for a queue to
     unblock. */
};

#define LINK_PRIVATE(pad) \
  ((GstFairSchedulerPrivLink *) \
   (GST_PAD_IS_SRC (pad) ? \
    GST_REAL_PAD(pad)->sched_private : \
    GST_RPAD_PEER (GST_REAL_PAD(pad))->sched_private))


/* An entry in the clock wait list. */
struct _GstFairSchedulerWaitEntry
{
  GstFairSchedulerCothread *ct; /* The waiting cothread. */
  GstClockTime time;            /* The clock time it should wake up
                                   on. */
};


struct _GstFairScheduler
{
  GstScheduler parent;

  GstFairSchedulerCothreadQueue *cothreads;
  /* The queue handling the cothreads
     for the scheduler. */

  /* Scheduling control. */
  gboolean in_element;          /* True if we are running element
                                   code. */

  /* Clock wait support. */
  GSList *waiting;              /* List of waiting cothreads. Elements
                                   are GstFairSchedulerWaitEntry
                                   structures. */

  /* Timing statistics. */
  GTimer *iter_timer;           /* Iteration timer. */
  guint iter_count;             /* Iteration count. */

#ifndef GST_DISABLE_GST_DEBUG
  GList *elements;              /* List of all registered elements
                                   (needed only for debugging. */
  GList *sources;               /* List of all source pads involved in
                                   registered links (needed only for
                                   debugging. */
#endif
};


struct _GstFairSchedulerClass
{
  GstSchedulerClass parent_class;
};


static GType _gst_fair_scheduler_type = 0;


enum
{
  ARG_0,
};


/* Standard GObject Operations */

static GType gst_fair_scheduler_get_type (void);

static void gst_fair_scheduler_class_init (GstFairSchedulerClass * klass);

static void gst_fair_scheduler_init (GObject * object);

static void gst_fair_scheduler_dispose (GObject * object);

static void
gst_fair_scheduler_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static void
gst_fair_scheduler_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);


/* Cothread Function Wrappers */

static void
gst_fair_scheduler_loop_wrapper (GstFairSchedulerCothread * ct,
    GstElement * element);


/* Chain, Get and Event Handlers */

static void gst_fair_scheduler_chain_handler (GstPad * pad, GstData * data);

static GstData *gst_fair_scheduler_get_handler (GstPad * pad);


/* GstScheduler Operations */

static void gst_fair_scheduler_setup (GstScheduler * sched);

static void gst_fair_scheduler_reset (GstScheduler * sched);

static void
gst_fair_scheduler_add_element (GstScheduler * sched, GstElement * element);

static void
gst_fair_scheduler_remove_element (GstScheduler * sched, GstElement * element);

static GstElementStateReturn
gst_fair_scheduler_state_transition (GstScheduler * sched,
    GstElement * element, gint transition);

static void
decoupled_state_transition (GstElement * element, gint old_state,
    gint new_state, gpointer user_data);

static void
gst_fair_scheduler_scheduling_change (GstScheduler * sched,
    GstElement * element);

static gboolean
gst_fair_scheduler_yield (GstScheduler * sched, GstElement * element);

static gboolean
gst_fair_scheduler_interrupt (GstScheduler * sched, GstElement * element);

static void
gst_fair_scheduler_error (GstScheduler * sched, GstElement * element);

static void
gst_fair_scheduler_pad_link (GstScheduler * sched, GstPad * srcpad,
    GstPad * sinkpad);

static void
gst_fair_scheduler_pad_unlink (GstScheduler * sched, GstPad * srcpad,
    GstPad * sinkpad);

static GstData *gst_fair_scheduler_pad_select (GstScheduler * sched,
    GstPad ** pulled_from, GstPad ** pads);

static GstClockReturn
gst_fair_scheduler_clock_wait (GstScheduler * sched, GstElement * element,
    GstClockID id, GstClockTimeDiff * jitter);

static GstSchedulerState gst_fair_scheduler_iterate (GstScheduler * sched);

static void gst_fair_scheduler_show (GstScheduler * sched);


static GstSchedulerClass *parent_class = NULL;


/* 
 * Standard GObject Operations
 */

static GType
gst_fair_scheduler_get_type (void)
{
  if (!_gst_fair_scheduler_type) {
    static const GTypeInfo scheduler_info = {
      sizeof (GstFairSchedulerClass),
      NULL,
      NULL,
      (GClassInitFunc) gst_fair_scheduler_class_init,
      NULL,
      NULL,
      sizeof (GstFairScheduler),
      0,
      (GInstanceInitFunc) gst_fair_scheduler_init,
      NULL
    };

    _gst_fair_scheduler_type = g_type_register_static (GST_TYPE_SCHEDULER,
        "GstFair" COTHREADS_NAME_CAPITAL "Scheduler", &scheduler_info, 0);
  }
  return _gst_fair_scheduler_type;
}


static void
gst_fair_scheduler_class_init (GstFairSchedulerClass * klass)
{
  GObjectClass *gobject_class;
  GstObjectClass *gstobject_class;
  GstSchedulerClass *gstscheduler_class;

  gobject_class = (GObjectClass *) klass;
  gstobject_class = (GstObjectClass *) klass;
  gstscheduler_class = (GstSchedulerClass *) klass;

  parent_class = g_type_class_ref (GST_TYPE_SCHEDULER);

  gobject_class->set_property = gst_fair_scheduler_set_property;
  gobject_class->get_property = gst_fair_scheduler_get_property;
  gobject_class->dispose = gst_fair_scheduler_dispose;

  gstscheduler_class->setup = gst_fair_scheduler_setup;
  gstscheduler_class->reset = gst_fair_scheduler_reset;
  gstscheduler_class->add_element = gst_fair_scheduler_add_element;
  gstscheduler_class->remove_element = gst_fair_scheduler_remove_element;
  gstscheduler_class->state_transition = gst_fair_scheduler_state_transition;
  gstscheduler_class->scheduling_change = gst_fair_scheduler_scheduling_change;
  gstscheduler_class->yield = gst_fair_scheduler_yield;
  gstscheduler_class->interrupt = gst_fair_scheduler_interrupt;
  gstscheduler_class->error = gst_fair_scheduler_error;
  gstscheduler_class->pad_link = gst_fair_scheduler_pad_link;
  gstscheduler_class->pad_unlink = gst_fair_scheduler_pad_unlink;
  gstscheduler_class->pad_select = gst_fair_scheduler_pad_select;
  gstscheduler_class->clock_wait = gst_fair_scheduler_clock_wait;
  gstscheduler_class->iterate = gst_fair_scheduler_iterate;
  gstscheduler_class->show = gst_fair_scheduler_show;
}


static void
gst_fair_scheduler_init (GObject * object)
{
  GstFairScheduler *fsched = GST_FAIR_SCHEDULER (object);

  fsched->cothreads = gst_fair_scheduler_cothread_queue_new ();

  /* Proudly suporting the select operation since 2004! */
  GST_FLAG_SET (fsched, GST_SCHEDULER_FLAG_NEW_API);

  fsched->in_element = FALSE;

  fsched->waiting = NULL;

  fsched->iter_timer = g_timer_new ();

#ifndef GST_DISABLE_GST_DEBUG
  fsched->elements = NULL;
  fsched->sources = NULL;
#endif
}


static void
gst_fair_scheduler_dispose (GObject * object)
{
  GstFairScheduler *fsched = GST_FAIR_SCHEDULER (object);

  GST_WARNING_OBJECT (fsched, "disposing");

  g_slist_free (fsched->waiting);

  g_timer_destroy (fsched->iter_timer);

  gst_fair_scheduler_cothread_queue_destroy (fsched->cothreads);

#ifndef GST_DISABLE_GST_DEBUG
  g_list_free (fsched->elements);
  g_list_free (fsched->sources);
#endif

  G_OBJECT_CLASS (parent_class)->dispose (object);
}


static void
gst_fair_scheduler_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  /*GstFairScheduler *fsched = GST_FAIR_SCHEDULER (object); */

  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}


static void
gst_fair_scheduler_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  /*GstFairScheduler *fsched = GST_FAIR_SCHEDULER (object); */

  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}


/*
 * Helpers
 */

static GstFairSchedulerPrivLink *
get_link_priv (GstPad * pad)
{
  GstFairSchedulerPrivLink *priv;

  GstRealPad *real = GST_PAD_REALIZE (pad);

  if (GST_RPAD_DIRECTION (real) == GST_PAD_SINK) {
    real = GST_RPAD_PEER (real);
  }

  priv = LINK_PRIVATE (real);
  g_return_val_if_fail (priv != NULL, NULL);

  return priv;
}


static void
set_cothread_state (GstFairSchedulerCothread * ct, GstElementState state)
{
  guint ct_state;

  switch (state) {
    case GST_STATE_PLAYING:
      ct_state = GST_FAIRSCHEDULER_CTSTATE_RUNNING;
      break;
    case GST_STATE_PAUSED:
      ct_state = GST_FAIRSCHEDULER_CTSTATE_SUSPENDED;
      break;
    default:
      ct_state = GST_FAIRSCHEDULER_CTSTATE_STOPPED;
      break;
  }

  gst_fair_scheduler_cothread_change_state_async (ct, ct_state);
}


static GstPad *
find_ready_pad (GstPad ** pads)
{
  GstPad *pad;
  GstFairSchedulerPrivLink *priv;
  int i;

  for (i = 0; pads[i] != NULL; i++) {
    pad = pads[i];
    priv = LINK_PRIVATE (pad);

    if (GST_PAD_IS_SRC (pad) && priv->bufpen == NULL) {
      return pad;
    } else if (GST_PAD_IS_SINK (pad) && priv->bufpen != NULL) {
      return pad;
    }
  }

  return NULL;
}


static GstPad *
gst_fair_scheduler_internal_select (GstFairScheduler * fsched, GstPad ** pads)
{
  GstPad *pad;
  GstFairSchedulerPrivLink *priv;
  int i;

  pad = find_ready_pad (pads);
  if (pad == NULL) {
    /* Register the current cothread as waiting writer/reader for
       every pad on the list. */
    for (i = 0; pads[i] != NULL; i++) {
      pad = pads[i];
      priv = LINK_PRIVATE (pad);

      if (GST_PAD_IS_SRC (pad)) {
        g_return_val_if_fail (priv->waiting_writer == NULL, NULL);
        priv->waiting_writer =
            gst_fair_scheduler_cothread_current (fsched->cothreads);
      } else {
        g_return_val_if_fail (priv->waiting_reader == NULL, NULL);
        priv->waiting_reader =
            gst_fair_scheduler_cothread_current (fsched->cothreads);
      }
    }

    /* Sleep until at least one of the pads becomes ready. */
    gst_fair_scheduler_cothread_sleep (fsched->cothreads);

    /* Deregister from all pads. */
    for (i = 0; pads[i] != NULL; i++) {
      pad = pads[i];
      priv = LINK_PRIVATE (pad);

      if (GST_PAD_IS_SRC (pad)) {
        priv->waiting_writer = NULL;
      } else {
        priv->waiting_reader = NULL;
      }
    }

    /* This time it should work. */
    pad = find_ready_pad (pads);
  }

  /* At this point, we must have a pad to return. */
  g_return_val_if_fail (pad != NULL, NULL);

  return pad;
}


/*
 * Cothread Function Wrappers
 */

static void
gst_fair_scheduler_loop_wrapper (GstFairSchedulerCothread * ct,
    GstElement * element)
{
  GST_DEBUG ("Queue %p: entering loop wrapper for '%s'", ct->queue,
      GST_OBJECT_NAME (element));

  g_return_if_fail (element->loopfunc != NULL);

  gst_object_ref (GST_OBJECT (element));

  while (gst_element_get_state (element) == GST_STATE_PLAYING) {
    element->loopfunc (element);
  }

  gst_object_unref (GST_OBJECT (element));

  GST_DEBUG ("Queue %p: leaving loop wrapper for '%s'", ct->queue,
      GST_OBJECT_NAME (element));
}


static void
gst_fair_scheduler_chain_get_wrapper (GstFairSchedulerCothread * ct,
    GstElement * element)
{
  GstData *data;
  GstPad *pad;
  GstFairScheduler *fsched =
      GST_FAIR_SCHEDULER (gst_element_get_scheduler (element));

  GST_DEBUG ("Queue %p: entering chain/get wrapper for '%s'", ct->queue,
      GST_OBJECT_NAME (element));

  gst_object_ref (GST_OBJECT (element));

  while (gst_element_get_state (element) == GST_STATE_PLAYING) {
    /* Run a select on the pad list. */
    pad = gst_fair_scheduler_internal_select (fsched,
        (GstPad **) ELEM_PRIVATE (element)->chain_get_pads->data);

    if (GST_PAD_IS_SRC (pad)) {
      g_return_if_fail (GST_RPAD_GETFUNC (pad) != NULL);
      data = gst_pad_call_get_function (pad);
      gst_pad_push (pad, data);
    } else {
      g_return_if_fail (GST_RPAD_CHAINFUNC (pad) != NULL);
      data = gst_pad_pull (pad);
      gst_pad_call_chain_function (pad, data);
    }
  }

  gst_object_unref (GST_OBJECT (element));

  GST_DEBUG ("Queue %p: leaving chain/get wrapper for '%s'", ct->queue,
      GST_OBJECT_NAME (element));
}


static void
gst_fair_scheduler_queue_read_blocked_handler (GstQueue * queue, GstPad * pad)
{
  GstFairSchedulerPrivLink *priv;

  priv = LINK_PRIVATE (pad);

  GST_CAT_LOG_OBJECT (debug_fair_queues, priv->owner,
      "entering \"blocked\" handler for pad '%s:%s'", GST_DEBUG_PAD_NAME (pad));

  gst_fair_scheduler_cothread_sleep (priv->owner->cothreads);

  GST_CAT_LOG_OBJECT (debug_fair_queues, priv->owner,
      "leaving \"blocked\" handler for queue '%s:%s'",
      GST_DEBUG_PAD_NAME (pad));
}


static void
gst_fair_scheduler_decoupled_chain_wrapper (GstFairSchedulerCothread * ct,
    GstPad * pad)
{
  GstElement *parent = GST_PAD_PARENT (pad);
  GstFairSchedulerPrivLink *priv;
  GstData *data;

  g_return_if_fail (GST_RPAD_CHAINFUNC (pad) != NULL);

  priv = LINK_PRIVATE (pad);

  GST_DEBUG ("Queue %p: entering chain wrapper loop for '%s:%s'", ct->queue,
      GST_DEBUG_PAD_NAME (pad));

  gst_object_ref (GST_OBJECT (parent));

  while (gst_element_get_state (parent) == GST_STATE_PLAYING) {
    data = gst_pad_pull (pad);

    gst_pad_call_chain_function (pad, data);

    if (priv->waiting_for_queue != NULL) {
      gst_fair_scheduler_cothread_awake_async (priv->waiting_for_queue, 0);
    }
  }

  gst_object_unref (GST_OBJECT (parent));

  GST_DEBUG ("Queue %p: leaving chain wrapper loop for '%s:%s'",
      ct->queue, GST_DEBUG_PAD_NAME (pad));
}


static void
gst_fair_scheduler_decoupled_get_wrapper (GstFairSchedulerCothread * ct,
    GstPad * pad)
{
  GstElement *parent = GST_PAD_PARENT (pad);
  GstFairSchedulerPrivLink *priv, *sink_priv = NULL;
  GstData *data;

  g_return_if_fail (GST_RPAD_GETFUNC (pad) != NULL);

  priv = LINK_PRIVATE (pad);

  if (GST_IS_QUEUE (parent)) {
    /* Decoupled elements are almost always queues. We optimize for
       this case. The signal handler stops the cothread when the queue
       has no material available.  */

    priv->queue_blocked_signal_id = g_signal_connect (parent,
        "underrun",
        (GCallback) gst_fair_scheduler_queue_read_blocked_handler, pad);

    /* Register this cothread at the opposite side of the queue. */
    sink_priv = LINK_PRIVATE (gst_element_get_pad (parent, "sink"));
    sink_priv->waiting_for_queue = ct;
  }

  GST_DEBUG ("Queue %p: entering get wrapper loop for '%s:%s'", ct->queue,
      GST_DEBUG_PAD_NAME (pad));

  gst_object_ref (GST_OBJECT (parent));

  while (gst_element_get_state (parent) == GST_STATE_PLAYING) {
    data = gst_pad_call_get_function (pad);
    gst_pad_push (pad, data);
  }

  gst_object_unref (GST_OBJECT (parent));

  GST_DEBUG ("Queue %p: leaving get wrapper loop for '%s:%s'", ct->queue,
      GST_DEBUG_PAD_NAME (pad));

  if (GST_IS_QUEUE (parent)) {
    sink_priv->waiting_for_queue = NULL;

    /* Disconnect from the signal. */
    g_signal_handler_disconnect (parent, priv->queue_blocked_signal_id);
    priv->queue_blocked_signal_id = 0;
  }
}


/*
 * Chain and Get Handlers
 */

static void
gst_fair_scheduler_chain_handler (GstPad * pad, GstData * data)
{
  GstFairSchedulerPrivLink *priv = get_link_priv (pad);
  GstFairScheduler *fsched = priv->owner;

  while (priv->bufpen != NULL) {
    /* The buffer is full. Sleep until it's available again. */
    if (priv->waiting_writer != NULL) {
      GST_ERROR_OBJECT (fsched,
          "concurrent writers not supported, pad '%s:%s', waiting %p, "
          "current %p, ", GST_DEBUG_PAD_NAME (pad),
          priv->waiting_writer,
          gst_fair_scheduler_cothread_current (fsched->cothreads));
      return;
    }
    priv->waiting_writer =
        gst_fair_scheduler_cothread_current (fsched->cothreads);
    gst_fair_scheduler_cothread_sleep (fsched->cothreads);

    /* After sleeping we must be at the head. */
    g_return_if_fail (priv->waiting_writer ==
        gst_fair_scheduler_cothread_current (fsched->cothreads));
    priv->waiting_writer = NULL;
  }

  g_return_if_fail (priv->bufpen == NULL);

  /* Fill the bufpen. */
  priv->bufpen = data;

  /* If there's a waiting reader, wake it up. */
  if (priv->waiting_reader != NULL) {
    gst_fair_scheduler_cothread_awake (priv->waiting_reader, 0);
  }

  GST_LOG_OBJECT (fsched, "pushed data <%p> on pad '%s:%s'",
      data, GST_DEBUG_PAD_NAME (GST_RPAD_PEER (pad)));
}


static GstData *
gst_fair_scheduler_get_handler (GstPad * pad)
{
  GstFairSchedulerPrivLink *priv = get_link_priv (pad);
  GstFairScheduler *fsched = priv->owner;
  GstData *ret;

  while (priv->bufpen == NULL) {
    /* The buffer is empty. Sleep until there's something to read. */
    if (priv->waiting_reader != NULL) {
      GST_ERROR_OBJECT (fsched, "concurrent readers not supported");
      return NULL;
    }
    priv->waiting_reader =
        gst_fair_scheduler_cothread_current (fsched->cothreads);
    gst_fair_scheduler_cothread_sleep (fsched->cothreads);

    /* We should still be there after sleeping. */
    g_return_val_if_fail (priv->waiting_reader ==
        gst_fair_scheduler_cothread_current (fsched->cothreads), NULL);
    priv->waiting_reader = NULL;
  }

  g_return_val_if_fail (priv->bufpen != NULL, NULL);

  /* Empty the bufpen. */
  ret = priv->bufpen;
  priv->bufpen = NULL;

  /* If there's a waiting writer, wake it up. */
  if (priv->waiting_writer != NULL) {
    gst_fair_scheduler_cothread_awake (priv->waiting_writer, 0);
  }

  GST_LOG_OBJECT (fsched, "pulled data <%p> from pad '%s:%s'",
      ret, GST_DEBUG_PAD_NAME (GST_RPAD_PEER (pad)));

  return ret;
}


/*
 * GstScheduler Entry Points
 */

static void
gst_fair_scheduler_setup (GstScheduler * sched)
{
  GstFairScheduler *fsched = GST_FAIR_SCHEDULER (sched);

  GST_DEBUG_OBJECT (fsched, "setting up scheduler");

  /* Initialize the cothread system. */
  gst_fair_scheduler_cothread_queue_start (fsched->cothreads);

  fsched->iter_count = 0;
  g_timer_start (fsched->iter_timer);
}


static void
gst_fair_scheduler_reset (GstScheduler * sched)
{
  GstFairScheduler *fsched = GST_FAIR_SCHEDULER (sched);

  GST_DEBUG_OBJECT (fsched, "resetting scheduler");

  g_timer_stop (fsched->iter_timer);
  {
#ifndef GST_DISABLE_GST_DEBUG
    gulong msecs;
    double elapsed = g_timer_elapsed (fsched->iter_timer, &msecs);
#endif

    GST_INFO_OBJECT (fsched,
        "%u iterations in %0.3fs, %.0f iterations/sec.",
        fsched->iter_count, elapsed, fsched->iter_count / elapsed);
  }

  /* Shut down the cothreads system. */
  gst_fair_scheduler_cothread_queue_stop (fsched->cothreads);
}


static void
gst_fair_scheduler_add_element (GstScheduler * sched, GstElement * element)
{
  GstFairScheduler *fsched = GST_FAIR_SCHEDULER (sched);
  GstFairSchedulerPrivElem *priv;

  if (GST_FLAG_IS_SET (element, GST_ELEMENT_DECOUPLED)) {
    /* Decoupled elements don't have their own cothread. Their pads do
       have one, though, but it is assigned in the link operation. */
    return;
  }

  GST_DEBUG_OBJECT (fsched, "adding element '%s'", GST_OBJECT_NAME (element));

  g_return_if_fail (ELEM_PRIVATE (element) == NULL);

  priv = g_malloc (sizeof (GstFairSchedulerPrivElem));

  /* Create the element's cothread. */
  if (element->loopfunc != NULL) {
    priv->elem_ct =
        gst_fair_scheduler_cothread_new (fsched->cothreads,
        (GstFairSchedulerCtFunc) gst_fair_scheduler_loop_wrapper,
        element, NULL);
#ifndef GST_DISABLE_GST_DEBUG
    g_string_printf (priv->elem_ct->readable_name, "%s:loop",
        GST_OBJECT_NAME (element));
#endif
    GST_CAT_INFO_OBJECT (debug_fair_ct, fsched,
        "cothread %p is loop for element '%s'",
        priv->elem_ct, GST_OBJECT_NAME (element));
  } else {
    priv->elem_ct =
        gst_fair_scheduler_cothread_new (fsched->cothreads,
        (GstFairSchedulerCtFunc) gst_fair_scheduler_chain_get_wrapper,
        element, NULL);
#ifndef GST_DISABLE_GST_DEBUG
    g_string_printf (priv->elem_ct->readable_name, "%s:chain/get",
        GST_OBJECT_NAME (element));
#endif
    GST_CAT_INFO_OBJECT (debug_fair_ct, fsched,
        "cothread %p is chain/get for element '%s'",
        priv->elem_ct, GST_OBJECT_NAME (element));
  }

  set_cothread_state (priv->elem_ct, gst_element_get_state (element));

  priv->chain_get_pads = g_array_new (TRUE, FALSE, sizeof (GstPad *));

  element->sched_private = priv;

#ifndef GST_DISABLE_GST_DEBUG
  fsched->elements = g_list_prepend (fsched->elements, element);
#endif
}


static void
gst_fair_scheduler_remove_element (GstScheduler * sched, GstElement * element)
{
#ifndef GST_DISABLE_GST_DEBUG
  GstFairScheduler *fsched = GST_FAIR_SCHEDULER (sched);
#endif
  GstFairSchedulerPrivElem *priv = ELEM_PRIVATE (element);

  if (GST_FLAG_IS_SET (element, GST_ELEMENT_DECOUPLED)) {
    return;
  }

  GST_DEBUG_OBJECT (fsched, "removing element '%s'", GST_OBJECT_NAME (element));

  g_return_if_fail (priv != NULL);

  /* Clean up the cothread. */
  g_return_if_fail (priv->elem_ct != NULL);
  gst_fair_scheduler_cothread_destroy (priv->elem_ct);

#ifndef GST_DISABLE_GST_DEBUG
  fsched->elements = g_list_remove (fsched->elements, element);
#endif

  g_free (priv);
  element->sched_private = NULL;
}


static void
gst_fair_scheduler_pad_link (GstScheduler * sched, GstPad * srcpad,
    GstPad * sinkpad)
{
  GstFairScheduler *fsched = GST_FAIR_SCHEDULER (sched);
  GstFairSchedulerPrivLink *priv;
  GstElement *src_parent, *sink_parent;

  g_return_if_fail (LINK_PRIVATE (srcpad) == NULL);

  GST_DEBUG_OBJECT (fsched, "linking pads '%s:%s' and '%s:%s'",
      GST_DEBUG_PAD_NAME (srcpad), GST_DEBUG_PAD_NAME (sinkpad));

  /* Initialize the private information block. */
  priv = g_malloc (sizeof (GstFairSchedulerPrivLink));

  priv->owner = fsched;
  priv->bufpen = NULL;
  priv->waiting_writer = NULL;
  priv->waiting_reader = NULL;
  priv->decoupled_ct = NULL;
  priv->decoupled_signal_id = 0;
  priv->queue_blocked_signal_id = 0;
  priv->waiting_for_queue = NULL;

  GST_REAL_PAD (srcpad)->sched_private = priv;

  src_parent = GST_PAD_PARENT (srcpad);
  sink_parent = GST_PAD_PARENT (sinkpad);

  if (GST_RPAD_GETFUNC (srcpad) != NULL) {
    if (GST_FLAG_IS_SET (src_parent, GST_ELEMENT_DECOUPLED)) {
      /* Pad is decoupled. Create a separate cothread to run its get
         function. */
      priv->decoupled_ct =
          gst_fair_scheduler_cothread_new (fsched->cothreads,
          (GstFairSchedulerCtFunc) gst_fair_scheduler_decoupled_get_wrapper,
          srcpad, NULL);
#ifndef GST_DISABLE_GST_DEBUG
      g_string_printf (priv->decoupled_ct->readable_name, "%s:%s:get",
          GST_DEBUG_PAD_NAME (srcpad));
#endif
      GST_CAT_INFO_OBJECT (debug_fair_ct, fsched,
          "cothread %p is get for pad '%s:%s'",
          priv->decoupled_ct, GST_DEBUG_PAD_NAME (srcpad));

      /* Connect to the state change signal of the decoupled element
         in order to manage the state of this cothread. */
      priv->decoupled_signal_id = g_signal_connect (src_parent,
          "state-change", (GCallback) decoupled_state_transition,
          priv->decoupled_ct);

      set_cothread_state (priv->decoupled_ct,
          gst_element_get_state (src_parent));
    } else {
      g_array_append_val (ELEM_PRIVATE (src_parent)->chain_get_pads, srcpad);
    }
  }

  if (GST_RPAD_CHAINFUNC (sinkpad) != NULL) {
    if (GST_FLAG_IS_SET (sink_parent, GST_ELEMENT_DECOUPLED)) {
      /* Pad is decoupled. Create a separate cothread to run its chain
         function. */
      priv->decoupled_ct =
          gst_fair_scheduler_cothread_new (fsched->cothreads,
          (GstFairSchedulerCtFunc) gst_fair_scheduler_decoupled_chain_wrapper,
          sinkpad, NULL);
#ifndef GST_DISABLE_GST_DEBUG
      g_string_printf (priv->decoupled_ct->readable_name, "%s:%s:chain",
          GST_DEBUG_PAD_NAME (srcpad));
#endif
      GST_CAT_INFO_OBJECT (debug_fair_ct, fsched,
          "cothread %p is chain for pad '%s:%s'",
          priv->decoupled_ct, GST_DEBUG_PAD_NAME (sinkpad));

      /* Connect to the state change signal of the decoupled element
         in order to manage the state of this cothread. */
      priv->decoupled_signal_id = g_signal_connect (sink_parent,
          "state-change", (GCallback) decoupled_state_transition,
          priv->decoupled_ct);

      set_cothread_state (priv->decoupled_ct,
          gst_element_get_state (sink_parent));
    } else {
      g_array_append_val (ELEM_PRIVATE (sink_parent)->chain_get_pads, sinkpad);
    }
  }

  /* Set the data handlers. */
  GST_RPAD_GETHANDLER (srcpad) = gst_fair_scheduler_get_handler;
  GST_RPAD_EVENTHANDLER (srcpad) = GST_RPAD_EVENTFUNC (srcpad);

  GST_RPAD_CHAINHANDLER (sinkpad) = gst_fair_scheduler_chain_handler;
  GST_RPAD_EVENTHANDLER (sinkpad) = GST_RPAD_EVENTFUNC (sinkpad);

#ifndef GST_DISABLE_GST_DEBUG
  fsched->sources = g_list_prepend (fsched->sources, srcpad);
#endif
}


static void
array_remove_pad (GArray * array, GstPad * pad)
{
  int i;

  for (i = 0; i < array->len; i++) {
    if (g_array_index (array, GstPad *, i) == pad) {
      g_array_remove_index_fast (array, i);
      break;
    }
  }
}


static void
gst_fair_scheduler_pad_unlink (GstScheduler * sched, GstPad * srcpad,
    GstPad * sinkpad)
{
#ifndef GST_DISABLE_GST_DEBUG
  GstFairScheduler *fsched = GST_FAIR_SCHEDULER (sched);
#endif
  GstFairSchedulerPrivLink *priv;
  GstElement *src_parent, *sink_parent;

  priv = LINK_PRIVATE (srcpad);
  g_return_if_fail (priv != NULL);

  GST_DEBUG_OBJECT (fsched, "unlinking pads '%s:%s' and '%s:%s'",
      GST_DEBUG_PAD_NAME (srcpad), GST_DEBUG_PAD_NAME (sinkpad));

  src_parent = GST_PAD_PARENT (srcpad);
  sink_parent = GST_PAD_PARENT (sinkpad);

  if (GST_RPAD_GETFUNC (srcpad) != NULL) {
    if (GST_FLAG_IS_SET (src_parent, GST_ELEMENT_DECOUPLED)) {
      gst_fair_scheduler_cothread_destroy (priv->decoupled_ct);
    } else {
      array_remove_pad (ELEM_PRIVATE (src_parent)->chain_get_pads, srcpad);
    }
  }

  if (GST_RPAD_CHAINFUNC (sinkpad) != NULL) {
    if (GST_FLAG_IS_SET (sink_parent, GST_ELEMENT_DECOUPLED)) {
      gst_fair_scheduler_cothread_destroy (priv->decoupled_ct);
    } else {
      array_remove_pad (ELEM_PRIVATE (sink_parent)->chain_get_pads, sinkpad);
    }
  }

  if (priv->decoupled_signal_id != 0) {
    g_signal_handler_disconnect (sink_parent, priv->decoupled_signal_id);
  }
  if (priv->queue_blocked_signal_id != 0) {
    g_signal_handler_disconnect (sink_parent, priv->queue_blocked_signal_id);
  }

  if (priv->bufpen != NULL) {
    gst_data_unref (priv->bufpen);
  }
  g_free (priv);

  GST_REAL_PAD (srcpad)->sched_private = NULL;

#ifndef GST_DISABLE_GST_DEBUG
  fsched->sources = g_list_remove (fsched->sources, srcpad);
#endif
}


static GstElementStateReturn
gst_fair_scheduler_state_transition (GstScheduler * sched,
    GstElement * element, gint transition)
{
#ifndef GST_DISABLE_GST_DEBUG
  GstFairScheduler *fsched = GST_FAIR_SCHEDULER (sched);
#endif
  gint old_state, new_state;

  GST_DEBUG_OBJECT (sched, "Element %s changing from %s to %s",
      GST_ELEMENT_NAME (element),
      gst_element_state_get_name (transition >> 8),
      gst_element_state_get_name (transition & 0xff));

  if (GST_FLAG_IS_SET (element, GST_ELEMENT_DECOUPLED)) {
    return GST_STATE_SUCCESS;
  }

  /* The parent element must be handled specially. */
  if (GST_IS_BIN (element)) {
    if (GST_SCHEDULER_PARENT (sched) == element) {
      switch (transition) {
        case GST_STATE_PLAYING_TO_PAUSED:
          GST_INFO_OBJECT (fsched, "setting scheduler state to stopped");
          GST_SCHEDULER_STATE (sched) = GST_SCHEDULER_STATE_STOPPED;
          break;
        case GST_STATE_PAUSED_TO_PLAYING:
          GST_INFO_OBJECT (fsched, "setting scheduler state to running");
          GST_SCHEDULER_STATE (sched) = GST_SCHEDULER_STATE_RUNNING;
          break;
      }
    }
    return GST_STATE_SUCCESS;
  }

  /* FIXME: Are there eny GStreamer macros for doing this? */
  old_state = transition >> 8;
  new_state = transition & 0xff;
  if (old_state < new_state) {
    set_cothread_state (ELEM_PRIVATE (element)->elem_ct, transition & 0xff);
  }

  return GST_STATE_SUCCESS;
}


static void
decoupled_state_transition (GstElement * element, gint old_state,
    gint new_state, gpointer user_data)
{
  GstFairSchedulerCothread *ct = (GstFairSchedulerCothread *) user_data;

  /* This function is only responsible for activating the
     cothread. The wrapper function itself does the deactivation. This
     is necessary to avoid weird interactions between multiple
     threads. */
  if (old_state < new_state) {
    set_cothread_state (ct, new_state);
  }
}


static void
gst_fair_scheduler_scheduling_change (GstScheduler * sched,
    GstElement * element)
{
#ifndef GST_DISABLE_GST_DEBUG
  GstFairScheduler *fsched = GST_FAIR_SCHEDULER (sched);
#endif

  GST_WARNING_OBJECT (fsched, "operation not implemented");
}


static gboolean
gst_fair_scheduler_yield (GstScheduler * sched, GstElement * element)
{
  GstFairScheduler *fsched = GST_FAIR_SCHEDULER (sched);

  g_return_val_if_fail (fsched->in_element, FALSE);

  /* FIXME: What's the difference between yield and interrupt? */
  gst_fair_scheduler_cothread_yield (fsched->cothreads);

  return FALSE;
}


static gboolean
gst_fair_scheduler_interrupt (GstScheduler * sched, GstElement * element)
{
  GstFairScheduler *fsched = GST_FAIR_SCHEDULER (sched);

  g_return_val_if_fail (fsched->in_element, FALSE);

  gst_fair_scheduler_cothread_yield (fsched->cothreads);

  return FALSE;
}


static void
gst_fair_scheduler_error (GstScheduler * sched, GstElement * element)
{
  GstFairScheduler *fsched = GST_FAIR_SCHEDULER (sched);

  GST_SCHEDULER_STATE (sched) = GST_SCHEDULER_STATE_STOPPED;
  if (fsched->in_element) {
    gst_fair_scheduler_cothread_yield (fsched->cothreads);
  }
}


static gint
wait_entry_compare (const GstFairSchedulerWaitEntry * first,
    const GstFairSchedulerWaitEntry * second)
{
  if (first->time < second->time) {
    return -1;
  } else if (first->time == second->time) {
    return 0;
  } else {
    return 1;
  }
}


static GstData *
gst_fair_scheduler_pad_select (GstScheduler * sched,
    GstPad ** pulled_from, GstPad ** pads)
{
  GstFairScheduler *fsched = GST_FAIR_SCHEDULER (sched);

  *pulled_from = gst_fair_scheduler_internal_select (fsched, pads);
  g_return_val_if_fail (GST_PAD_IS_SINK (*pulled_from), NULL);

  return gst_pad_pull (*pulled_from);
}


static GstClockReturn
gst_fair_scheduler_clock_wait (GstScheduler * sched, GstElement * element,
    GstClockID id, GstClockTimeDiff * jitter)
{
  GstFairScheduler *fsched = GST_FAIR_SCHEDULER (sched);
  GstClockEntry *clock_entry = (GstClockEntry *) id;
  GstClockTime requested, now;
  GstFairSchedulerWaitEntry *entry;

  g_return_val_if_fail (sched->current_clock != NULL, GST_CLOCK_ERROR);
  g_return_val_if_fail (sched->current_clock ==
      GST_CLOCK_ENTRY_CLOCK (clock_entry), GST_CLOCK_ERROR);

  now = gst_clock_get_time (sched->current_clock);
  requested = GST_CLOCK_ENTRY_TIME (clock_entry);

  if (requested <= now) {
    /* It is already too late. */
    if (jitter) {
      *jitter = now - requested;
    }
    return GST_CLOCK_EARLY;
  }

  /* Insert a wait entry. */
  entry = g_malloc (sizeof (GstFairSchedulerWaitEntry));
  entry->ct = gst_fair_scheduler_cothread_current (fsched->cothreads);
  entry->time = requested;
  fsched->waiting = g_slist_insert_sorted (fsched->waiting, entry,
      (GCompareFunc) wait_entry_compare);

  /* Go to sleep until it is time... */
  gst_fair_scheduler_cothread_sleep (fsched->cothreads);

  if (jitter) {
    now = gst_clock_get_time (sched->current_clock);
    *jitter = now - requested;
  }

  /* FIXME: Is this the right value to return? */
  return GST_CLOCK_EARLY;
}


static GstSchedulerState
gst_fair_scheduler_iterate (GstScheduler * sched)
{
  GstFairScheduler *fsched = GST_FAIR_SCHEDULER (sched);
  GstFairSchedulerWaitEntry *entry;
  GSList *activate = NULL, *node;
  GstClockTime now;
  gboolean res;

  /* Count a new iteration for the stats. */
  ++fsched->iter_count;

  /* Check for waiting cothreads. */
  if (fsched->waiting != NULL && sched->current_clock != NULL) {
    now = gst_clock_get_time (sched->current_clock);

    /* We need to activate all cothreads whose waiting time was
       already reached by the clock. The following code makes sure
       that the cothread with the earlier waiting time will be
       scheduled first. */

    /* Move all ready cothreads to the activate list. */
    while (fsched->waiting != NULL) {
      entry = (GstFairSchedulerWaitEntry *) fsched->waiting->data;

      if (entry->time > now) {
        break;
      }

      /* Extract a node from the begining of the waiting
         list. */
      node = fsched->waiting;
      fsched->waiting = fsched->waiting->next;

      /* Add it to the beginning of the activate list. */
      node->next = activate;
      activate = node;
    }

    /* Activate the threads in the activate list. */
    while (activate != NULL) {
      entry = (GstFairSchedulerWaitEntry *) activate->data;
      gst_fair_scheduler_cothread_awake (entry->ct, 1);
      activate = g_slist_delete_link (activate, activate);
      g_free (entry);
    }
  }

  /* Handle control to the next cothread. */
  fsched->in_element = TRUE;
  res = gst_fair_scheduler_cothread_queue_iterate (fsched->cothreads);
  fsched->in_element = FALSE;

  return res ? GST_SCHEDULER_STATE_RUNNING : GST_SCHEDULER_STATE_STOPPED;
}


static void
gst_fair_scheduler_show (GstScheduler * sched)
{
#ifndef GST_DISABLE_GST_DEBUG
  GstFairScheduler *fsched = GST_FAIR_SCHEDULER (sched);
  GstElement *element;
  GstPad *pad;
  GstFairSchedulerPrivLink *link_priv;
  GstFairSchedulerWaitEntry *entry;
  GList *iter1;
  GSList *iter2;
  GList *iterpads;

  g_print ("Fair scheduler at %p:\n", fsched);

  g_print ("\n  Registered elements:\n");

  for (iter1 = fsched->elements; iter1 != NULL; iter1 = iter1->next) {
    element = GST_ELEMENT (iter1->data);

    g_print ("\n    %p: %s (%s)\n", element, GST_ELEMENT_NAME (element),
        g_type_name (G_OBJECT_TYPE (element)));

    if (GST_IS_BIN (element)) {
      continue;
    }

    for (iterpads = GST_ELEMENT_PADS (element); iterpads != NULL;
        iterpads = iterpads->next) {
      pad = GST_PAD (iterpads->data);

      if (GST_IS_GHOST_PAD (pad)) {
        continue;
      }

      if (GST_PAD_IS_SINK (pad)) {
        g_print ("      Sink ");
      } else {
        g_print ("      Source ");
      }

      g_print ("'%s'", GST_PAD_NAME (pad));

      link_priv = LINK_PRIVATE (pad);

      if (link_priv == NULL) {
        g_print (", unlinked");
      } else {
        if (link_priv->bufpen != NULL) {
          g_print (", buffer in bufpen");
        }
        if (link_priv->waiting_writer != NULL) {
          g_print (", waiting writer '%s'",
              link_priv->waiting_writer->readable_name->str);
        }
        if (link_priv->waiting_reader != NULL) {
          g_print (", waiting reader '%s'",
              link_priv->waiting_reader->readable_name->str);
        }
        if (link_priv->waiting_for_queue != NULL) {
          g_print (", waiting for queue '%s'",
              link_priv->waiting_for_queue->readable_name->str);
        }
      }

      g_print ("\n");
    }
  }

  gst_fair_scheduler_cothread_queue_show (fsched->cothreads);

  g_print ("\n  Waiting cothreads (current time %" GST_TIME_FORMAT "):\n",
      GST_TIME_ARGS (gst_clock_get_time (sched->current_clock)));

  for (iter2 = fsched->waiting; iter2 != NULL; iter2 = iter2->next) {
    entry = (GstFairSchedulerWaitEntry *) iter2->data;
    g_print ("    %p: %s (%d), time = %" GST_TIME_FORMAT "\n", entry->ct,
        entry->ct->readable_name->str, entry->ct->pid,
        GST_TIME_ARGS (entry->time));
  }
#else
  g_print ("Sorry, the 'show' method only works when "
      "debugging is activated.");
#endif
}


/*
 * Plugin Initialization
 */
static gboolean
plugin_init (GstPlugin * plugin)
{
  GstSchedulerFactory *factory;

  GST_DEBUG_CATEGORY_INIT (debug_fair, "fair", 0, "fair scheduler");
  GST_DEBUG_CATEGORY_INIT (debug_fair_ct, "fairct", 0,
      "fair scheduler cothreads");
  GST_DEBUG_CATEGORY_INIT (debug_fair_queues, "fairqueues", 0,
      "fair scheduler queue related optimizations");

  factory = gst_scheduler_factory_new ("fair" COTHREADS_NAME,
      "A fair scheduler based on " COTHREADS_NAME " cothreads",
      gst_fair_scheduler_get_type ());

  if (factory != NULL) {
    gst_plugin_add_feature (plugin, GST_PLUGIN_FEATURE (factory));
  } else {
    g_warning ("could not register scheduler: fair");
  }
  return TRUE;
}


GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "gstfair" COTHREADS_NAME "scheduler",
    "A 'fair' type scheduler based on " COTHREADS_NAME " cothreads",
    plugin_init, VERSION, GST_LICENSE, GST_PACKAGE, GST_ORIGIN);
