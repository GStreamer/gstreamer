/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wtay@chello.be>
 *
 * gstentryscheduler.c: A scheduler based on entries
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

#include <gst/gst.h>
#include "cothreads_compat.h"
#include "../gst-i18n-lib.h"

GST_DEBUG_CATEGORY_STATIC (debug_scheduler);
#define GST_CAT_DEFAULT debug_scheduler


#define GET_TYPE(x) gst_entry_ ## x ## _scheduler_get_type
#define GST_TYPE_ENTRY_SCHEDULER \
  (GET_TYPE (COTHREADS_TYPE) ())
#define GST_ENTRY_SCHEDULER(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_ENTRY_SCHEDULER,GstEntryScheduler))
#define GST_ENTRY_SCHEDULER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_ENTRY_SCHEDULER,GstEntrySchedulerClass))
#define GST_IS_ENTRY_SCHEDULER(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_ENTRY_SCHEDULER))
#define GST_IS_ENTRY_SCHEDULER_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_ENTRY_SCHEDULER))

typedef struct
{
  int (*main) (int argc, gchar ** argv);
  cothread *thread;             /* cothread of element */
  gboolean running;             /* if the cothread is currently running */
  gboolean schedulable;         /* if this element can be scheduled */
  GstPad *schedule_pad;         /* pad to schedule next */
}
GstElementPrivate;

#define ELEMENT_PRIVATE(element) ((GstElementPrivate *) GST_ELEMENT (element)->sched_private)
#define SCHED(element) (GST_ENTRY_SCHEDULER ((element)->sched))

typedef struct
{
  cothread *src_thread;         /* cothread of srcpad */
  cothread *sink_thread;        /* cothread of sinkpad */
  gboolean sink_active;         /* if the sink may receive data */
  gboolean src_active;          /* if the src may provide data */
  GstData *bufpen;              /* current data */
}
GstPadPrivate;

#define PAD_PRIVATE(pad) ((GstPadPrivate *) (GST_REAL_PAD (pad))->sched_private)
#define PAD_SET_ACTIVE(pad,active) G_STMT_START{ \
  g_assert (pad->sched_private); \
  if (GST_PAD_DIRECTION (pad) == GST_PAD_SRC) { \
    ((GstPadPrivate *) pad->sched_private)->src_active = active; \
  } else { \
    ((GstPadPrivate *) pad->sched_private)->sink_active = active; \
  } \
}G_STMT_END

typedef struct _GstEntryScheduler GstEntryScheduler;
typedef struct _GstEntrySchedulerClass GstEntrySchedulerClass;

struct _GstEntryScheduler
{
  GstScheduler scheduler;

  cothread_context *context;

  GList *schedule_now;          /* entry points that must be scheduled this 
                                   iteration */
  GList *schedule_possible;     /* possible entry points */
  GList *waiting;               /* elements waiting for the clock */
  gboolean error;               /* if an element threw an error */

  GList *decoupled_pads;        /* all pads we manage that belong to decoupled elements */
};

struct _GstEntrySchedulerClass
{
  GstSchedulerClass scheduler_class;
};

static void gst_entry_scheduler_class_init (gpointer g_class, gpointer data);
static void gst_entry_scheduler_init (GstEntryScheduler * object);


GType GET_TYPE (COTHREADS_TYPE) (void)
{
  static GType object_type = 0;

  if (object_type == 0) {
    static const GTypeInfo object_info = {
      sizeof (GstEntrySchedulerClass),
      NULL,
      NULL,
      gst_entry_scheduler_class_init,
      NULL,
      NULL,
      sizeof (GstEntryScheduler),
      0,
      (GInstanceInitFunc) gst_entry_scheduler_init
    };

    object_type =
        g_type_register_static (GST_TYPE_SCHEDULER,
        "GstEntry" COTHREADS_NAME_CAPITAL "Scheduler", &object_info, 0);
  }
  return object_type;
}

static int gst_entry_scheduler_loop_wrapper (int argc, char **argv);
static int gst_entry_scheduler_get_wrapper (int argc, char **argv);
static int gst_entry_scheduler_decoupled_get_wrapper (int argc, char **argv);
static int gst_entry_scheduler_chain_wrapper (int argc, char **argv);
static int gst_entry_scheduler_decoupled_chain_wrapper (int argc, char **argv);

static void gst_entry_scheduler_setup (GstScheduler * sched);
static void gst_entry_scheduler_reset (GstScheduler * sched);
static void gst_entry_scheduler_add_element (GstScheduler * sched,
    GstElement * element);
static void gst_entry_scheduler_remove_element (GstScheduler * sched,
    GstElement * element);
static GstElementStateReturn gst_entry_scheduler_state_transition (GstScheduler
    * sched, GstElement * element, gint transition);
static void gst_entry_scheduler_lock_element (GstScheduler * sched,
    GstElement * element);
static void gst_entry_scheduler_unlock_element (GstScheduler * sched,
    GstElement * element);
static gboolean gst_entry_scheduler_yield (GstScheduler * sched,
    GstElement * element);
static gboolean gst_entry_scheduler_interrupt (GstScheduler * sched,
    GstElement * element);
static void gst_entry_scheduler_error (GstScheduler * sched,
    GstElement * element);
static void gst_entry_scheduler_pad_link (GstScheduler * sched, GstPad * srcpad,
    GstPad * sinkpad);
static void gst_entry_scheduler_pad_unlink (GstScheduler * sched,
    GstPad * srcpad, GstPad * sinkpad);
static void gst_entry_scheduler_pad_select (GstScheduler * sched, GList * pads);
static GstSchedulerState gst_entry_scheduler_iterate (GstScheduler * sched);
static void gst_entry_scheduler_show (GstScheduler * scheduler);

static gboolean element_may_start (GstElement * element);
static gboolean sinkpad_is_active (GstPad * pad);
static gboolean srcpad_is_active (GstPad * pad);

static void
gst_entry_scheduler_class_init (gpointer klass, gpointer class_data)
{
  GstSchedulerClass *scheduler = GST_SCHEDULER_CLASS (klass);

  scheduler->setup = gst_entry_scheduler_setup;
  scheduler->reset = gst_entry_scheduler_reset;
  scheduler->add_element = gst_entry_scheduler_add_element;
  scheduler->remove_element = gst_entry_scheduler_remove_element;
  scheduler->state_transition = gst_entry_scheduler_state_transition;
  scheduler->lock_element = gst_entry_scheduler_lock_element;
  scheduler->unlock_element = gst_entry_scheduler_unlock_element;
  scheduler->yield = gst_entry_scheduler_yield;
  scheduler->interrupt = gst_entry_scheduler_interrupt;
  scheduler->error = gst_entry_scheduler_error;
  scheduler->pad_link = gst_entry_scheduler_pad_link;
  scheduler->pad_unlink = gst_entry_scheduler_pad_unlink;
  scheduler->pad_select = gst_entry_scheduler_pad_select;
  scheduler->clock_wait = NULL;
  scheduler->iterate = gst_entry_scheduler_iterate;
  scheduler->show = gst_entry_scheduler_show;

  do_cothreads_init (NULL);
}

static void
gst_entry_scheduler_init (GstEntryScheduler * scheduler)
{
}

static gboolean
can_schedule (GstEntryScheduler * scheduler, GstObject * thing)
{
  if (GST_IS_PAD (thing)) {
    return srcpad_is_active (GST_PAD (thing));
  } else if (GST_IS_ELEMENT (thing)) {
    return ELEMENT_PRIVATE (thing)->schedulable
        && element_may_start (GST_ELEMENT (thing))
        && GST_STATE (thing) == GST_STATE_PLAYING;
  } else {
    g_assert_not_reached ();
    return FALSE;
  }
}

#define safe_cothread_switch(sched,cothread) G_STMT_START{ \
  if (do_cothread_get_current (sched->context) != cothread) \
    do_cothread_switch (cothread); \
}G_STMT_END

/* the meat - no guarantee as to which cothread it runs from */
static void
schedule (GstEntryScheduler * sched, GstObject * thing)
{
  g_assert (can_schedule (sched, thing));
  if (GST_IS_PAD (thing)) {
    GstPadPrivate *priv = PAD_PRIVATE (thing);

    if (priv->bufpen) {
      GstElement *element =
          GST_ELEMENT (gst_object_get_parent (GST_OBJECT (GST_PAD_PEER
                  (thing))));
      GST_DEBUG_OBJECT (sched, "scheduling pad %s:%s",
          GST_DEBUG_PAD_NAME (GST_PAD_PEER (thing)));
      if (ELEMENT_PRIVATE (element))
        ELEMENT_PRIVATE (element)->schedule_pad = GST_PAD_PEER (thing);
      if (!priv->sink_thread) {
        do_cothread_create (priv->sink_thread, sched->context,
            gst_entry_scheduler_decoupled_chain_wrapper, 0,
            (gchar **) GST_PAD_PEER (thing));
      }
      safe_cothread_switch (sched, priv->sink_thread);
    } else {
      GstElement *element =
          GST_ELEMENT (gst_object_get_parent (GST_OBJECT (thing)));
      GST_DEBUG_OBJECT (sched, "scheduling pad %s:%s",
          GST_DEBUG_PAD_NAME (thing));
      if (ELEMENT_PRIVATE (element))
        ELEMENT_PRIVATE (element)->schedule_pad = GST_PAD (thing);
      if (!priv->src_thread) {
        do_cothread_create (priv->src_thread, sched->context,
            gst_entry_scheduler_decoupled_get_wrapper, 0, (gchar **) thing);
      }
      safe_cothread_switch (sched, priv->src_thread);
    }
  } else if (GST_IS_ELEMENT (thing)) {
    GstElementPrivate *priv = ELEMENT_PRIVATE (thing);

    priv->schedule_pad = NULL;
    GST_DEBUG_OBJECT (sched, "scheduling element %s", GST_OBJECT_NAME (thing));
    safe_cothread_switch (sched, priv->thread);
  } else {
    g_assert_not_reached ();
    GST_DEBUG_OBJECT (sched, "scheduling main after error");
    safe_cothread_switch (sched, do_cothread_get_main (sched->context));
  }
}

static void
schedule_next_element (GstEntryScheduler * scheduler)
{
  if (scheduler->error) {
    GST_DEBUG_OBJECT (scheduler, "scheduling main after error");
    safe_cothread_switch (scheduler, do_cothread_get_main (scheduler->context));
  } else if (scheduler->waiting) {
    /* FIXME: write me */
    g_assert_not_reached ();
  } else if (scheduler->schedule_now) {
    GList *test;

    for (test = scheduler->schedule_now; test; test = g_list_next (test)) {
      GstObject *thing = test->data;

      if (can_schedule (scheduler, thing)) {
        scheduler->schedule_now =
            g_list_remove (scheduler->schedule_now, thing);
        schedule (scheduler, thing);
        return;
      }
    }
    for (test = scheduler->schedule_possible; test; test = g_list_next (test)) {
      GstObject *thing = test->data;

      if (can_schedule (scheduler, thing)) {
        scheduler->schedule_possible =
            g_list_remove (scheduler->schedule_possible, thing);
        scheduler->schedule_possible =
            g_list_append (scheduler->schedule_possible, thing);
        schedule (scheduler, thing);
        return;
      }
    }
  } else {
    GST_DEBUG_OBJECT (scheduler, "scheduling main");
    safe_cothread_switch (scheduler, do_cothread_get_main (scheduler->context));
  }
}

/* these are the wrappers around the element types - none of them will ever return */
static int
gst_entry_scheduler_loop_wrapper (int argc, char **argv)
{
  GstElement *element = GST_ELEMENT (argv);

  do {
    GST_LOG_OBJECT (SCHED (element), "calling loopfunc for element %s",
        GST_ELEMENT_NAME (element));
    ELEMENT_PRIVATE (element)->running = TRUE;
    ELEMENT_PRIVATE (element)->schedulable = FALSE;
    if (element->loopfunc) {
      element->loopfunc (element);
    } else {
      GST_ELEMENT_ERROR (element, CORE, SCHEDULER, (_("badly behaving plugin")),
          ("loop-based element %s removed loopfunc during processing",
              GST_OBJECT_NAME (element)));
    }
    ELEMENT_PRIVATE (element)->running = FALSE;
    ELEMENT_PRIVATE (element)->schedulable = TRUE;
    GST_LOG_OBJECT (SCHED (element), "done calling loopfunc for element %s",
        GST_OBJECT_NAME (element));
    schedule_next_element (SCHED (element));
  } while (TRUE);

  return 0;
}

static void
run_chainhandler (GstEntryScheduler * sched, GstRealPad * pad)
{
  GstElement *element = GST_ELEMENT (gst_object_get_parent (GST_OBJECT (pad)));

  g_assert (GST_IS_REAL_PAD (pad));
  g_assert (GST_PAD_DIRECTION (pad) == GST_PAD_SINK);
  g_assert (PAD_PRIVATE (pad)->bufpen != NULL);
  GST_LOG_OBJECT (sched, "calling chainfunc for pad %s:%s",
      GST_DEBUG_PAD_NAME (pad));
  if (pad->chainfunc) {
    GstData *data = PAD_PRIVATE (pad)->bufpen;

    PAD_PRIVATE (pad)->bufpen = NULL;
    if (GST_IS_EVENT (data)
        && !GST_FLAG_IS_SET (element, GST_ELEMENT_EVENT_AWARE)) {
      gst_pad_event_default (GST_PAD (pad), GST_EVENT (data));
    } else {
      pad->chainfunc (GST_PAD (pad), data);
    }
    /* don't do anything after here with the pad, it might already be dead! 
       the element is still alive though */
  } else {
    GST_ELEMENT_ERROR (element, CORE, SCHEDULER, (_("badly behaving plugin")),
        ("chain-based element %s removed chainfunc of pad during processing",
            GST_OBJECT_NAME (element)));
  }
  GST_LOG_OBJECT (sched, "done calling chainfunc for element %s",
      GST_OBJECT_NAME (element));
}

static int
gst_entry_scheduler_decoupled_chain_wrapper (int argc, char **argv)
{
  GstRealPad *pad = GST_REAL_PAD (argv);
  GstEntryScheduler *sched =
      GST_ENTRY_SCHEDULER (gst_pad_get_scheduler (GST_PAD (pad)));

  do {
    run_chainhandler (sched, pad);
    schedule_next_element (sched);
  } while (TRUE);
}

static int
gst_entry_scheduler_chain_wrapper (int argc, char **argv)
{
  GstElement *element = GST_ELEMENT (argv);

  do {
    GstRealPad *pad = GST_REAL_PAD (ELEMENT_PRIVATE (element)->schedule_pad);

    ELEMENT_PRIVATE (element)->schedule_pad = NULL;
    ELEMENT_PRIVATE (element)->running = TRUE;
    run_chainhandler (SCHED (element), pad);
    ELEMENT_PRIVATE (element)->running = FALSE;
    schedule_next_element (SCHED (element));
  } while (TRUE);

  return 0;
}

static void
run_gethandler (GstEntryScheduler * sched, GstRealPad * pad)
{
  GstElement *element = GST_ELEMENT (gst_object_get_parent (GST_OBJECT (pad)));

  g_assert (GST_IS_REAL_PAD (pad));
  g_assert (GST_PAD_DIRECTION (pad) == GST_PAD_SRC);
  g_assert (PAD_PRIVATE (pad)->bufpen == NULL);
  GST_LOG_OBJECT (sched, "calling getfunc for pad %s:%s",
      GST_DEBUG_PAD_NAME (pad));
  if (pad->getfunc) {
    GstData *data = pad->getfunc (GST_PAD (pad));

    /* make sure the pad still exists and is linked */
    if (!g_list_find (element->pads, pad)) {
      GST_ELEMENT_ERROR (element, CORE, SCHEDULER, (_("badly behaving plugin")),
          ("get-based element %s removed pad during getfunc",
              GST_OBJECT_NAME (element)));
      gst_data_unref (data);
    } else if (!GST_PAD_PEER (pad)) {
      GST_ELEMENT_ERROR (element, CORE, SCHEDULER, (_("badly behaving plugin")),
          ("get-based element %s unlinked pad during getfunc",
              GST_OBJECT_NAME (element)));
      gst_data_unref (data);
    } else {
      PAD_PRIVATE (pad)->bufpen = data;
      sched->schedule_now = g_list_prepend (sched->schedule_now, pad);
    }
  } else {
    GST_ELEMENT_ERROR (element, CORE, SCHEDULER, (_("badly behaving plugin")),
        ("get-based element %s removed getfunc during processing",
            GST_OBJECT_NAME (element)));
  }
  GST_LOG_OBJECT (sched, "done calling chainfunc for element %s",
      GST_ELEMENT_NAME (element));
}

static int
gst_entry_scheduler_decoupled_get_wrapper (int argc, char **argv)
{
  GstRealPad *pad = GST_REAL_PAD (argv);
  GstEntryScheduler *sched =
      GST_ENTRY_SCHEDULER (gst_pad_get_scheduler (GST_PAD (pad)));

  do {
    run_gethandler (sched, pad);
    schedule_next_element (sched);
  } while (TRUE);
}

static int
gst_entry_scheduler_get_wrapper (int argc, char **argv)
{
  GstElement *element = GST_ELEMENT (argv);

  do {
    GstRealPad *pad = GST_REAL_PAD (ELEMENT_PRIVATE (element)->schedule_pad);

    ELEMENT_PRIVATE (element)->schedule_pad = NULL;
    ELEMENT_PRIVATE (element)->running = TRUE;
    run_gethandler (SCHED (element), pad);
    ELEMENT_PRIVATE (element)->running = FALSE;
    schedule_next_element (SCHED (element));
  } while (TRUE);

  return 0;
}

static gboolean
sinkpad_is_active (GstPad * pad)
{
  GstPadPrivate *priv = PAD_PRIVATE (pad);

  g_assert (GST_PAD_DIRECTION (pad) == GST_PAD_SINK);
  /* don't ever schedule something that's paused */
  if (GST_STATE (gst_object_get_parent (GST_OBJECT (pad))) != GST_STATE_PLAYING)
    return FALSE;
  if (!priv->sink_active)
    return FALSE;
  if (!element_may_start (GST_ELEMENT (gst_object_get_parent (GST_OBJECT
                  (pad)))))
    return FALSE;
  return TRUE;
}

static gboolean
srcpad_is_active (GstPad * pad)
{
  GstPadPrivate *priv = PAD_PRIVATE (pad);

  g_assert (GST_PAD_DIRECTION (pad) == GST_PAD_SRC);
  if (!sinkpad_is_active (GST_PAD_PEER (pad)))
    return FALSE;
  /* don't care about sink when there's already a buffer */
  if (priv->bufpen != NULL)
    return TRUE;
  if (GST_STATE (gst_object_get_parent (GST_OBJECT (pad))) != GST_STATE_PLAYING)
    return FALSE;
  if (!priv->src_active)
    return FALSE;
  return TRUE;
}

/* this is ugly somehow, someone find a better solution */
static gboolean
element_may_start (GstElement * element)
{
  gboolean ret = TRUE;
  GList *pads = element->pads;

  if (GST_FLAG_IS_SET (element, GST_ELEMENT_DECOUPLED))
    return TRUE;
  if (ELEMENT_PRIVATE (element)->main == gst_entry_scheduler_get_wrapper)
    return TRUE;

  while (pads) {
    GstPad *pad = pads->data;

    pads = g_list_next (pads);
    if (GST_PAD_PEER (pad) &&
        /* FIXME: workaround for EOS */
        GST_STATE (gst_object_get_parent (GST_OBJECT (GST_PAD_PEER (pad)))) ==
        GST_STATE_PLAYING && GST_PAD_DIRECTION (pad) == GST_PAD_SRC
        && !sinkpad_is_active (GST_PAD_PEER (pad))) {
      ret = FALSE;
      break;
    }
  }
  return ret;
}

/* handlers to attach to pads */
static void
gst_entry_scheduler_chain_handler (GstPad * pad, GstData * data)
{
  GstPadPrivate *priv = PAD_PRIVATE (pad);
  GstEntryScheduler *sched = GST_ENTRY_SCHEDULER (gst_pad_get_scheduler (pad));

  GST_LOG_OBJECT (sched, "putting data %p in pen of pad %s:%s",
      data, GST_DEBUG_PAD_NAME (pad));

  g_assert (priv->bufpen == NULL);
  priv->bufpen = data;

  sched->schedule_now = g_list_append (sched->schedule_now, GST_PAD_PEER (pad));
  ELEMENT_PRIVATE (gst_object_get_parent (GST_OBJECT (GST_PAD_PEER (pad))))->
      schedulable = TRUE;
  schedule_next_element (sched);
  ELEMENT_PRIVATE (gst_object_get_parent (GST_OBJECT (GST_PAD_PEER (pad))))->
      schedulable = FALSE;

  GST_LOG_OBJECT (sched, "done");
}

static GstData *
gst_entry_scheduler_get_handler (GstPad * pad)
{
  GstData *data;
  GstPadPrivate *priv = PAD_PRIVATE (pad);
  GstElement *element =
      GST_ELEMENT (gst_object_get_parent (GST_OBJECT (GST_PAD_PEER (pad))));
  GstEntryScheduler *sched = GST_ENTRY_SCHEDULER (gst_pad_get_scheduler (pad));

  pad = GST_PAD_PEER (pad);
  GST_LOG_OBJECT (sched, "pad %s:%s pulls", GST_DEBUG_PAD_NAME (pad));

  PAD_SET_ACTIVE (GST_REAL_PAD (pad), TRUE);
  schedule_next_element (sched);
  if (!g_list_find (element->pads, pad)) {
    GST_ERROR_OBJECT (sched, "element %s removed pad it pulled from",
        GST_OBJECT_NAME (element));
    data = GST_DATA (gst_event_new (GST_EVENT_INTERRUPT));
  } else {
    priv = PAD_PRIVATE (GST_REAL_PAD (pad));
    PAD_SET_ACTIVE (GST_REAL_PAD (pad), FALSE);
    g_assert (priv->bufpen != NULL);
    data = priv->bufpen;
    priv->bufpen = NULL;
  }

  GST_LOG_OBJECT (sched, "done with %s:%s", GST_DEBUG_PAD_NAME (pad));
  return data;
}

static gboolean
gst_entry_scheduler_event_handler (GstPad * srcpad, GstEvent * event)
{
  /* FIXME: need to do more here? */
  return GST_RPAD_EVENTFUNC (srcpad) (srcpad, event);
}

/*
 * Entry points for this scheduler.
 */
static void
gst_entry_scheduler_pad_select (GstScheduler * sched, GList * pads)
{
  g_warning ("NOT IMPLEMENTED");
}

static void
gst_entry_scheduler_setup (GstScheduler * sched)
{
  /* first create thread context */
  if (GST_ENTRY_SCHEDULER (sched)->context == NULL) {
    GST_DEBUG_OBJECT (sched, "initializing cothread context");
    GST_ENTRY_SCHEDULER (sched)->context = do_cothread_context_init ();
  }
}

static void
gst_entry_scheduler_reset (GstScheduler * sched)
{
#if 0
  /* FIXME: do we need to destroy cothreads ourselves? */
  GList *elements = GST_ENTRY_SCHEDULER (sched)->elements;

  while (elements) {
    GstElement *element = GST_ELEMENT (elements->data);

    if (GST_ELEMENT_THREADSTATE (element)) {
      do_cothread_destroy (GST_ELEMENT_THREADSTATE (element));
      GST_ELEMENT_THREADSTATE (element) = NULL;
    }
    elements = g_list_next (elements);
  }
#endif

  do_cothread_context_destroy (GST_ENTRY_SCHEDULER (sched)->context);
  GST_ENTRY_SCHEDULER (sched)->context = NULL;
}

static void
gst_entry_scheduler_add_element (GstScheduler * scheduler, GstElement * element)
{
  GstEntryScheduler *sched = GST_ENTRY_SCHEDULER (scheduler);
  GstElementPrivate *priv;

  if (GST_FLAG_IS_SET (element, GST_ELEMENT_DECOUPLED)) {
    GST_INFO_OBJECT (sched, "decoupled element %s added, ignoring",
        GST_OBJECT_NAME (element));
    return;
  }
  /* FIXME ? */
  if (GST_IS_BIN (element)) {
    GST_INFO_OBJECT (sched, "bin %s added, ignoring",
        GST_OBJECT_NAME (element));
    return;
  }

  g_assert (element->sched_private == NULL);
  element->sched_private = priv = g_new0 (GstElementPrivate, 1);
  priv->running = FALSE;
  priv->schedulable = FALSE;
  priv->schedule_pad = NULL;
  if (element->loopfunc) {
    priv->main = gst_entry_scheduler_loop_wrapper;
    priv->schedulable = TRUE;
  } else {
    GList *pads = element->pads;

    while (element->pads) {
      GstPad *pad = pads->data;

      pads = g_list_next (pads);
      if (!GST_IS_REAL_PAD (pad))
        continue;
      /* FIXME: error checking? */
      if (GST_RPAD_CHAINFUNC (pad)) {
        priv->main = gst_entry_scheduler_chain_wrapper;
        break;
      } else if (GST_RPAD_GETFUNC (pad)) {
        priv->main = gst_entry_scheduler_get_wrapper;
        break;
      }
    }
    /* happens when no pad is there to help decide if we're chain- or loopbased */
    g_return_if_fail (priv->main != NULL);
  }
  sched->schedule_possible = g_list_prepend (sched->schedule_possible, element);
  if (GST_STATE (element) >= GST_STATE_READY)
    gst_entry_scheduler_state_transition (scheduler, element,
        GST_STATE_NULL_TO_READY);
  if (GST_STATE (element) >= GST_STATE_PAUSED)
    gst_entry_scheduler_state_transition (scheduler, element,
        GST_STATE_READY_TO_PAUSED);
  if (GST_STATE (element) >= GST_STATE_PLAYING)
    gst_entry_scheduler_state_transition (scheduler, element,
        GST_STATE_PAUSED_TO_PLAYING);
}

static void
gst_entry_scheduler_remove_element (GstScheduler * scheduler,
    GstElement * element)
{
  GstEntryScheduler *sched = GST_ENTRY_SCHEDULER (scheduler);

  if (GST_FLAG_IS_SET (element, GST_ELEMENT_DECOUPLED)) {
    GST_INFO_OBJECT (sched, "decoupled element %s added, ignoring",
        GST_OBJECT_NAME (element));
    return;
  }
  /* FIXME ? */
  if (GST_IS_BIN (element)) {
    GST_INFO_OBJECT (sched, "bin %s added, ignoring",
        GST_OBJECT_NAME (element));
    return;
  }

  if (GST_STATE (element) >= GST_STATE_PLAYING)
    gst_entry_scheduler_state_transition (scheduler, element,
        GST_STATE_PLAYING_TO_PAUSED);
  if (GST_STATE (element) >= GST_STATE_PAUSED)
    gst_entry_scheduler_state_transition (scheduler, element,
        GST_STATE_PAUSED_TO_READY);
  if (GST_STATE (element) >= GST_STATE_READY)
    gst_entry_scheduler_state_transition (scheduler, element,
        GST_STATE_READY_TO_NULL);

  sched->waiting = g_list_remove (sched->waiting, element);
  sched->schedule_now = g_list_remove (sched->schedule_now, element);
  sched->schedule_possible = g_list_remove (sched->schedule_possible, element);
  g_free (element->sched_private);
  element->sched_private = NULL;
}

static inline void
apply_thread (GstElement * element)
{
  GList *pads;

  for (pads = element->pads; pads; pads = g_list_next (pads)) {
    GstPad *pad = pads->data;

    if (!GST_IS_REAL_PAD (pad))
      continue;
    if (GST_PAD_DIRECTION (pad) == GST_PAD_SRC && PAD_PRIVATE (pad)) {
      PAD_PRIVATE (pad)->src_thread = ELEMENT_PRIVATE (element)->thread;
    } else if (GST_PAD_DIRECTION (pad) == GST_PAD_SINK && PAD_PRIVATE (pad)) {
      PAD_PRIVATE (pad)->sink_thread = ELEMENT_PRIVATE (element)->thread;
    } else {
      g_assert (!GST_PAD_PEER (pad));
    }
  }
}

static void
clear_decoupled_pad (GstEntryScheduler * sched, GstPad * pad)
{
  if (GST_PAD_DIRECTION (pad) == GST_PAD_SRC) {
    if (PAD_PRIVATE (pad)->src_thread)
      do_cothread_destroy (PAD_PRIVATE (pad)->src_thread);
    PAD_PRIVATE (pad)->src_thread = NULL;
  } else {
    if (PAD_PRIVATE (pad)->sink_thread)
      do_cothread_destroy (PAD_PRIVATE (pad)->sink_thread);
    PAD_PRIVATE (pad)->sink_thread = NULL;
  }
}

static GstElementStateReturn
gst_entry_scheduler_state_transition (GstScheduler * scheduler,
    GstElement * element, gint transition)
{
  GstEntryScheduler *sched = GST_ENTRY_SCHEDULER (scheduler);

  /* check if our parent changed state */
  switch (transition) {
    case GST_STATE_NULL_TO_READY:
      break;
    case GST_STATE_READY_TO_PAUSED:
      if (element->sched_private != NULL) {
        g_return_val_if_fail (sched->context, GST_STATE_FAILURE);
        do_cothread_create (ELEMENT_PRIVATE (element)->thread, sched->context,
            ELEMENT_PRIVATE (element)->main, 0, (gchar **) element);
        apply_thread (element);
      }
      break;
    case GST_STATE_PAUSED_TO_PLAYING:
      break;
    case GST_STATE_PLAYING_TO_PAUSED:
      break;
    case GST_STATE_PAUSED_TO_READY:
      if (element == scheduler->parent) {
        GList *list;

        for (list = sched->decoupled_pads; list; list = g_list_next (list))
          clear_decoupled_pad (sched, GST_PAD (list->data));
      }
      if (element->sched_private != NULL) {
        do_cothread_destroy (ELEMENT_PRIVATE (element)->thread);
        ELEMENT_PRIVATE (element)->thread = NULL;
        apply_thread (element);
      }
      break;
    case GST_STATE_READY_TO_NULL:
      break;
    default:
      g_warning ("invalid state change %d for element %s", transition,
          GST_OBJECT_NAME (element));
      return GST_STATE_FAILURE;
  }

  return GST_STATE_SUCCESS;
}

static void
gst_entry_scheduler_lock_element (GstScheduler * sched, GstElement * element)
{
  g_warning ("What's this?");
}

static void
gst_entry_scheduler_unlock_element (GstScheduler * sched, GstElement * element)
{
  g_warning ("What's this?");
}

static gboolean
gst_entry_scheduler_yield (GstScheduler * sched, GstElement * element)
{
  ELEMENT_PRIVATE (element)->schedulable = TRUE;
  schedule_next_element (GST_ENTRY_SCHEDULER (sched));
  ELEMENT_PRIVATE (element)->schedulable = FALSE;
  return FALSE;
}

static gboolean
gst_entry_scheduler_interrupt (GstScheduler * sched, GstElement * element)
{
  ELEMENT_PRIVATE (element)->schedulable = TRUE;
  schedule_next_element (GST_ENTRY_SCHEDULER (sched));
  ELEMENT_PRIVATE (element)->schedulable = FALSE;
  return FALSE;
}

static void
gst_entry_scheduler_error (GstScheduler * scheduler, GstElement * element)
{
  GST_ENTRY_SCHEDULER (scheduler)->error = TRUE;
}

static void
gst_entry_scheduler_pad_link (GstScheduler * scheduler, GstPad * srcpad,
    GstPad * sinkpad)
{
  GstEntryScheduler *sched = GST_ENTRY_SCHEDULER (scheduler);
  GstPadPrivate *priv;
  GstElement *element;

  priv = g_new0 (GstPadPrivate, 1);
  /* wrap srcpad */
  element = GST_ELEMENT (gst_object_get_parent (GST_OBJECT (srcpad)));
  if (GST_FLAG_IS_SET (element, GST_ELEMENT_DECOUPLED)) {
    sched->decoupled_pads = g_list_prepend (sched->decoupled_pads, srcpad);
    priv->src_active = TRUE;
  } else {
    priv->src_thread = ELEMENT_PRIVATE (element)->thread;
    priv->src_active =
        ELEMENT_PRIVATE (element)->main == gst_entry_scheduler_get_wrapper;
  }
  GST_RPAD_GETHANDLER (srcpad) = gst_entry_scheduler_get_handler;
  GST_RPAD_EVENTHANDLER (srcpad) = gst_entry_scheduler_event_handler;
  GST_REAL_PAD (srcpad)->sched_private = priv;
  /* wrap sinkpad */
  element = GST_ELEMENT (gst_object_get_parent (GST_OBJECT (sinkpad)));
  if (GST_FLAG_IS_SET (element, GST_ELEMENT_DECOUPLED)) {
    sched->decoupled_pads = g_list_prepend (sched->decoupled_pads, sinkpad);
    priv->sink_active = TRUE;
  } else {
    priv->sink_thread = ELEMENT_PRIVATE (element)->thread;
    priv->sink_active =
        ELEMENT_PRIVATE (element)->main == gst_entry_scheduler_chain_wrapper
        && !ELEMENT_PRIVATE (element)->running;
  }
  GST_RPAD_CHAINHANDLER (sinkpad) = gst_entry_scheduler_chain_handler;
  GST_RPAD_EVENTHANDLER (sinkpad) = gst_entry_scheduler_event_handler;
  GST_REAL_PAD (sinkpad)->sched_private = priv;

  sched->schedule_possible = g_list_prepend (sched->schedule_possible, srcpad);
}

static void
gst_entry_scheduler_pad_unlink (GstScheduler * scheduler, GstPad * srcpad,
    GstPad * sinkpad)
{
  GstEntryScheduler *sched = GST_ENTRY_SCHEDULER (scheduler);
  GstPadPrivate *priv;
  GstElement *element;

  priv = PAD_PRIVATE (srcpad);
  /* wrap srcpad */
  element = GST_ELEMENT (gst_object_get_parent (GST_OBJECT (srcpad)));
  if (GST_FLAG_IS_SET (element, GST_ELEMENT_DECOUPLED)) {
    clear_decoupled_pad (sched, srcpad);
    sched->decoupled_pads = g_list_remove (sched->decoupled_pads, srcpad);
  }
  GST_RPAD_GETHANDLER (srcpad) = NULL;
  GST_RPAD_EVENTHANDLER (srcpad) = NULL;
  GST_REAL_PAD (srcpad)->sched_private = NULL;
  /* wrap sinkpad */
  element = GST_ELEMENT (gst_object_get_parent (GST_OBJECT (sinkpad)));
  if (GST_FLAG_IS_SET (element, GST_ELEMENT_DECOUPLED)) {
    clear_decoupled_pad (sched, sinkpad);
    sched->decoupled_pads = g_list_remove (sched->decoupled_pads, sinkpad);
  }
  GST_RPAD_CHAINHANDLER (sinkpad) = NULL;
  GST_RPAD_EVENTHANDLER (sinkpad) = NULL;
  GST_REAL_PAD (sinkpad)->sched_private = NULL;

  if (priv->bufpen) {
    GST_ERROR_OBJECT (sched,
        "found data in bufpen while unlinking %s:%s and %s:%s, discarding",
        GST_DEBUG_PAD_NAME (srcpad), GST_DEBUG_PAD_NAME (sinkpad));
    gst_data_unref (priv->bufpen);
  }
  sched->schedule_now = g_list_remove (sched->schedule_now, srcpad);
  sched->schedule_possible = g_list_remove (sched->schedule_possible, srcpad);
  g_free (priv);
}

static GstSchedulerState
gst_entry_scheduler_iterate (GstScheduler * scheduler)
{
  GstEntryScheduler *sched = GST_ENTRY_SCHEDULER (scheduler);
  GList *entries = sched->schedule_possible;
  GstSchedulerState ret = GST_SCHEDULER_STATE_STOPPED;

  GST_LOG_OBJECT (sched, "starting iteration in bin %s",
      GST_ELEMENT_NAME (scheduler->parent));
  sched->error = FALSE;

  if (sched->schedule_now) {
    ret = GST_SCHEDULER_STATE_RUNNING;
  } else {
    while (entries) {
      if (can_schedule (sched, GST_OBJECT (entries->data))) {
        gpointer entry = entries->data;

        ret = GST_SCHEDULER_STATE_RUNNING;
        sched->schedule_now = g_list_prepend (sched->schedule_now, entry);
        sched->schedule_possible =
            g_list_remove (sched->schedule_possible, entry);
        sched->schedule_possible =
            g_list_append (sched->schedule_possible, entry);
        break;
      }
      entries = g_list_next (entries);
    }
  }
  if (ret == GST_SCHEDULER_STATE_RUNNING)
    schedule_next_element (sched);
  if (sched->error || sched->schedule_now) {
    GST_ERROR_OBJECT (sched, "returning error because of %s",
        sched->error ? "element error" : "unschedulable elements");
#if 0
    gst_entry_scheduler_show (scheduler);
#endif
    return GST_SCHEDULER_STATE_ERROR;
  } else if (GST_STATE (GST_SCHEDULER (sched)->parent) == GST_STATE_PLAYING &&
      ret == GST_SCHEDULER_STATE_STOPPED && scheduler->schedulers == NULL) {
    GST_ERROR_OBJECT (sched,
        "returning error because we contain running elements and we didn't do a thing");
#if 0
    gst_entry_scheduler_show (scheduler);
#endif
    return GST_SCHEDULER_STATE_ERROR;
  } else {
    return ret;
  }
}

static void
print_thing (GstEntryScheduler * sched, gpointer thing)
{
  if (GST_IS_PAD (thing)) {
    g_print ("    %s %s:%s%s => %s:%s%s%s\n", can_schedule (sched,
            thing) ? "OK" : "  ", GST_DEBUG_PAD_NAME (thing),
        PAD_PRIVATE (thing)->src_active ? " (active)" : "",
        GST_DEBUG_PAD_NAME (GST_PAD_PEER (thing)),
        PAD_PRIVATE (thing)->sink_active ? "(active) " : "",
        PAD_PRIVATE (thing)->bufpen ? " FILLED" : "");
  } else if (GST_IS_ELEMENT (thing)) {
    g_print ("    %s %s (%srunning, %sschedulable)\n", can_schedule (sched,
            thing) ? "OK" : "  ", GST_ELEMENT_NAME (thing),
        ELEMENT_PRIVATE (thing)->running ? "" : "not ",
        ELEMENT_PRIVATE (thing)->schedulable ? "" : "not ");
  }
}

static void
gst_entry_scheduler_show (GstScheduler * scheduler)
{
  GstEntryScheduler *sched = GST_ENTRY_SCHEDULER (scheduler);
  GList *list;

  g_print ("entry points waiting:\n");
  for (list = sched->waiting; list; list = g_list_next (list)) {
    print_thing (sched, list->data);
  }
  g_print ("entry points to schedule now:\n");
  for (list = sched->schedule_now; list; list = g_list_next (list)) {
    print_thing (sched, list->data);
  }
  g_print ("entry points that might be scheduled:\n");
  for (list = sched->schedule_possible; list; list = g_list_next (list)) {
    print_thing (sched, list->data);
  }
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  GstSchedulerFactory *factory;

  GST_DEBUG_CATEGORY_INIT (debug_scheduler, "entry" COTHREADS_NAME, 0,
      "entry " COTHREADS_NAME "scheduler");

  factory = gst_scheduler_factory_new ("entry" COTHREADS_NAME,
      "A entry scheduler using " COTHREADS_NAME " cothreads",
      GST_TYPE_ENTRY_SCHEDULER);
  if (factory == NULL)
    return FALSE;

  gst_plugin_add_feature (plugin, GST_PLUGIN_FEATURE (factory));
  return TRUE;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR, GST_VERSION_MINOR, "gstentry" COTHREADS_NAME "scheduler", "an entry scheduler using " COTHREADS_NAME " cothreads",        /* FIXME */
    plugin_init, VERSION, GST_LICENSE, GST_PACKAGE, GST_ORIGIN)
