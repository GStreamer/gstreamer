/* GStreamer
 * Copyright (C) 2004 Benjamin Otte <otte@gnome.org>
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

#define SCHED_ASSERT(sched, assertion) G_STMT_START{ \
  if (!(assertion)) \
    gst_scheduler_show (GST_SCHEDULER (sched)); \
  g_assert (assertion); \
}G_STMT_END

typedef enum
{
  WAIT_FOR_NOTHING,
  WAIT_FOR_MUM,
  WAIT_FOR_PADS,                /* pad must be scheduled */
  /* add more */
  WAIT_FOR_ANYTHING
}
WaitInfo;

typedef enum
{
  ENTRY_UNDEFINED,
  ENTRY_COTHREAD,
  ENTRY_LINK
}
EntryType;

typedef struct
{
  EntryType type;
}
Entry;

#define ENTRY_IS_COTHREAD(x)	(((Entry *)(x))->type == ENTRY_COTHREAD)
#define ENTRY_IS_LINK(x)	(((Entry *)(x))->type == ENTRY_LINK)

typedef struct _GstEntryScheduler GstEntryScheduler;
typedef struct _GstEntrySchedulerClass GstEntrySchedulerClass;

typedef struct
{
  Entry entry;
  /* pointer to scheduler */
  GstEntryScheduler *sched;
  /* pointer to element */
  GstElement *element;
  /* the main function of the cothread */
  int (*main) (int argc, gchar ** argv);
  /* wether the given pad is schedulable */
    gboolean (*can_schedule) (GstRealPad * pad);
  /* what the element is currently waiting for */
  WaitInfo wait;
  /* cothread of element */
  cothread *thread;
  /* pad to schedule next */
  GstRealPad *schedule_pad;
}
CothreadPrivate;

#define ELEMENT_PRIVATE(element) ((CothreadPrivate *) GST_ELEMENT (element)->sched_private)
#define SCHED(element) (GST_ENTRY_SCHEDULER ((element)->sched))

typedef struct
{
  Entry entry;
  /* pads */
  GstRealPad *srcpad;
  GstRealPad *sinkpad;
  /* private struct of srcpad's element, needed for decoupled elements */
  CothreadPrivate *src;
  /* private struct of sinkpad's element */
  CothreadPrivate *sink;
  /* current data */
  GstData *bufpen;
  /* if this link needs a discont - FIXME: remove when core ready for this */
  gboolean need_discont;
}
LinkPrivate;

#define PAD_PRIVATE(pad) ((LinkPrivate *) (GST_REAL_PAD (pad))->sched_private)

struct _GstEntryScheduler
{
  GstScheduler scheduler;

  cothread_context *context;

  GList *schedule_now;          /* entry points that must be scheduled this 
                                   iteration */
  GList *schedule_possible;     /* possible entry points */
  GList *waiting;               /* elements waiting for the clock */
  gboolean error;               /* if an element threw an error */
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
static int gst_entry_scheduler_chain_wrapper (int argc, char **argv);

static void gst_entry_scheduler_setup (GstScheduler * sched);
static void gst_entry_scheduler_reset (GstScheduler * sched);
static void gst_entry_scheduler_add_element (GstScheduler * sched,
    GstElement * element);
static void gst_entry_scheduler_remove_element (GstScheduler * sched,
    GstElement * element);
static GstElementStateReturn gst_entry_scheduler_state_transition (GstScheduler
    * sched, GstElement * element, gint transition);
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
static GstData *gst_entry_scheduler_pad_select (GstScheduler * sched,
    GstPad ** pulled_from, GstPad ** pads);
static GstSchedulerState gst_entry_scheduler_iterate (GstScheduler * sched);
static void gst_entry_scheduler_show (GstScheduler * scheduler);

static gboolean can_schedule_pad (GstRealPad * pad);
static void schedule_next_element (GstEntryScheduler * sched);

static void
gst_entry_scheduler_class_init (gpointer klass, gpointer class_data)
{
  GstSchedulerClass *scheduler = GST_SCHEDULER_CLASS (klass);

  scheduler->setup = gst_entry_scheduler_setup;
  scheduler->reset = gst_entry_scheduler_reset;
  scheduler->add_element = gst_entry_scheduler_add_element;
  scheduler->remove_element = gst_entry_scheduler_remove_element;
  scheduler->state_transition = gst_entry_scheduler_state_transition;
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
  GST_FLAG_SET (scheduler, GST_SCHEDULER_FLAG_NEW_API);
}

/*
 * We've got to setup 5 different element types here:
 * - loopbased
 * - chainbased
 * - chainbased PADS of decoupled elements
 * - getbased
 * - getbased PADS of decoupled elements
 */

/*
 * LOOPBASED
 */

typedef struct
{
  CothreadPrivate element;
  GstPad **sinkpads;
}
LoopPrivate;

#define LOOP_PRIVATE(x) ((LoopPrivate *) ELEMENT_PRIVATE (x))

static gboolean
_can_schedule_loop (GstRealPad * pad)
{
  LoopPrivate *priv;
  gint i = 0;

  g_assert (PAD_PRIVATE (pad));

  if (GST_PAD_IS_SRC (pad))
    return FALSE;

  priv = LOOP_PRIVATE (gst_pad_get_parent (GST_PAD (pad)));
  g_assert (priv);
  if (!priv->sinkpads)
    return FALSE;

  while (priv->sinkpads[i]) {
    if (pad == GST_REAL_PAD (priv->sinkpads[i++]))
      return TRUE;
  }
  return FALSE;
}

static int
gst_entry_scheduler_loop_wrapper (int argc, char **argv)
{
  CothreadPrivate *priv = (CothreadPrivate *) argv;
  GstElement *element = priv->element;

  do {
    g_assert (priv->wait == WAIT_FOR_NOTHING);
    GST_LOG_OBJECT (SCHED (element), "calling loopfunc for element %s",
        GST_ELEMENT_NAME (element));
    if (element->loopfunc) {
      element->loopfunc (element);
    } else {
      GST_ELEMENT_ERROR (element, CORE, SCHEDULER, (_("badly behaving plugin")),
          ("loop-based element %s removed loopfunc during processing",
              GST_OBJECT_NAME (element)));
    }
    GST_LOG_OBJECT (SCHED (element), "done calling loopfunc for element %s",
        GST_OBJECT_NAME (element));
    priv->wait = WAIT_FOR_NOTHING;
    schedule_next_element (SCHED (element));
  } while (TRUE);

  return 0;
}

static CothreadPrivate *
setup_loop (GstEntryScheduler * sched, GstElement * element)
{
  /* the types not matching is intentional, that's why it's g_new0 */
  CothreadPrivate *priv = (CothreadPrivate *) g_new0 (LoopPrivate, 1);

  priv->element = element;
  priv->main = gst_entry_scheduler_loop_wrapper;
  priv->wait = WAIT_FOR_NOTHING;
  priv->can_schedule = _can_schedule_loop;

  return priv;
}

/*
 * CHAINBASED
 */

/* this function returns the buffer currently in the bufpen
 * it's this complicated because we check that we don't need to invent 
 * a DISCONT event first. 
 * FIXME: The whole if part should go when the core supports this.
 */
static GstData *
get_buffer (GstEntryScheduler * sched, GstRealPad * pad)
{
  LinkPrivate *priv = PAD_PRIVATE (pad);
  GstData *data;

  g_assert (GST_PAD_IS_SINK (pad));
  if (priv->need_discont && GST_IS_BUFFER (priv->bufpen)) {
    if (GST_BUFFER_TIMESTAMP_IS_VALID (priv->bufpen)) {
      data =
          GST_DATA (gst_event_new_discontinuous (TRUE, GST_FORMAT_TIME,
              GST_BUFFER_TIMESTAMP (priv->bufpen),
              GST_BUFFER_OFFSET_IS_VALID (priv->
                  bufpen) ? GST_FORMAT_DEFAULT : 0,
              GST_BUFFER_OFFSET (priv->bufpen), 0));
      GST_WARNING_OBJECT (sched,
          "needed to invent a DISCONT (time %" G_GUINT64_FORMAT
          ") for %s:%s => %s:%s, fix it please",
          GST_BUFFER_TIMESTAMP (priv->bufpen),
          GST_DEBUG_PAD_NAME (GST_PAD_PEER (pad)), GST_DEBUG_PAD_NAME (pad));
    } else {
      data = GST_DATA (gst_event_new_discontinuous (TRUE,
              GST_BUFFER_OFFSET_IS_VALID (priv->
                  bufpen) ? GST_FORMAT_DEFAULT : 0,
              GST_BUFFER_OFFSET (priv->bufpen), 0));
      GST_WARNING_OBJECT (sched,
          "needed to invent a DISCONT (no time) for %s:%s => %s:%s, fix it please",
          GST_DEBUG_PAD_NAME (GST_PAD_PEER (pad)), GST_DEBUG_PAD_NAME (pad));
    }
    sched->schedule_now = g_list_prepend (sched->schedule_now, priv);
  } else {
    data = priv->bufpen;
    priv->bufpen = NULL;
  }
  g_assert (data);
  if (GST_IS_EVENT (data) && GST_EVENT_TYPE (data) == GST_EVENT_DISCONTINUOUS)
    priv->need_discont = FALSE;
  return data;
}

static int
gst_entry_scheduler_chain_wrapper (int argc, char **argv)
{
  CothreadPrivate *priv = (CothreadPrivate *) argv;
  GstElement *element = priv->element;

  do {
    GstRealPad *pad = priv->schedule_pad;

    g_assert (priv->wait == WAIT_FOR_PADS);
    g_assert (pad);
    g_assert (GST_PAD_IS_SINK (pad));
    g_assert (PAD_PRIVATE (pad)->bufpen != NULL);
    GST_LOG_OBJECT (priv->sched, "calling chainfunc for pad %s:%s",
        GST_DEBUG_PAD_NAME (pad));
    if (pad->chainfunc) {
      GstData *data = get_buffer (priv->sched, pad);

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
      gst_data_unref (PAD_PRIVATE (pad)->bufpen);
      PAD_PRIVATE (pad)->bufpen = NULL;
    }
    GST_LOG_OBJECT (priv->sched, "done calling chainfunc for element %s",
        GST_OBJECT_NAME (element));
    priv->wait = WAIT_FOR_PADS;
    schedule_next_element (priv->sched);
  } while (TRUE);

  return 0;
}

static gboolean
_can_schedule_chain (GstRealPad * pad)
{
  g_assert (PAD_PRIVATE (pad));

  if (GST_PAD_IS_SRC (pad))
    return FALSE;

  g_assert (PAD_PRIVATE (pad));
  return PAD_PRIVATE (pad)->sink->wait == WAIT_FOR_PADS;
}

static CothreadPrivate *
setup_chain (GstEntryScheduler * sched, GstElement * element)
{
  CothreadPrivate *priv = g_new0 (CothreadPrivate, 1);

  priv->main = gst_entry_scheduler_chain_wrapper;
  priv->wait = WAIT_FOR_PADS;
  priv->can_schedule = _can_schedule_chain;

  return priv;
}

/*
 * GETBASED
 */

static int
gst_entry_scheduler_get_wrapper (int argc, char **argv)
{
  CothreadPrivate *priv = (CothreadPrivate *) argv;
  GstElement *element = priv->element;

  do {
    GstRealPad *pad = priv->schedule_pad;

    g_assert (pad);
    g_assert (GST_PAD_IS_SRC (pad));
    g_assert (PAD_PRIVATE (pad)->bufpen == NULL);
    GST_LOG_OBJECT (priv->sched, "calling getfunc for pad %s:%s",
        GST_DEBUG_PAD_NAME (pad));
    if (pad->getfunc) {
      GstData *data = pad->getfunc (GST_PAD (pad));

      /* make sure the pad still exists and is linked */
      if (!g_list_find (element->pads, pad)) {
        GST_ELEMENT_ERROR (element, CORE, SCHEDULER,
            (_("badly behaving plugin")),
            ("get-based element %s removed pad during getfunc",
                GST_OBJECT_NAME (element)));
        gst_data_unref (data);
      } else if (!GST_PAD_PEER (pad)) {
        GST_ELEMENT_ERROR (element, CORE, SCHEDULER,
            (_("badly behaving plugin")),
            ("get-based element %s unlinked pad during getfunc",
                GST_OBJECT_NAME (element)));
        gst_data_unref (data);
      } else {
        PAD_PRIVATE (pad)->bufpen = data;
        priv->sched->schedule_now =
            g_list_prepend (priv->sched->schedule_now, PAD_PRIVATE (pad));
      }
    } else {
      GST_ELEMENT_ERROR (element, CORE, SCHEDULER, (_("badly behaving plugin")),
          ("get-based element %s removed getfunc during processing",
              GST_OBJECT_NAME (element)));
    }
    GST_LOG_OBJECT (priv->sched, "done calling chainfunc for element %s",
        GST_ELEMENT_NAME (element));

    priv->wait = WAIT_FOR_PADS;
    schedule_next_element (priv->sched);
  } while (TRUE);

  return 0;
}

static gboolean
_can_schedule_get (GstRealPad * pad)
{
  g_assert (PAD_PRIVATE (pad));
  g_assert (GST_PAD_IS_SRC (pad));

  g_assert (PAD_PRIVATE (pad));
  return PAD_PRIVATE (pad)->bufpen == NULL &&
      PAD_PRIVATE (pad)->src->wait == WAIT_FOR_PADS &&
      can_schedule_pad (PAD_PRIVATE (pad)->sinkpad);
}

static CothreadPrivate *
setup_get (GstEntryScheduler * sched, GstElement * element)
{
  CothreadPrivate *priv = g_new0 (CothreadPrivate, 1);

  priv->main = gst_entry_scheduler_get_wrapper;
  priv->wait = WAIT_FOR_PADS;
  priv->can_schedule = _can_schedule_get;

  return priv;
}

/*
 * scheduling functions
 */

static gboolean
can_schedule_pad (GstRealPad * pad)
{
  LinkPrivate *link = PAD_PRIVATE (pad);

  g_assert (link);
  if (GST_STATE (gst_pad_get_parent (GST_PAD (pad))) != GST_STATE_PLAYING)
    return FALSE;
  if (GST_PAD_IS_SINK (pad)) {
    return link->sink->can_schedule (pad);
  } else {
    return link->src->can_schedule (pad);
  }
}

static gboolean
can_schedule (Entry * entry)
{
  if (ENTRY_IS_LINK (entry)) {
    LinkPrivate *link = (LinkPrivate *) entry;
    CothreadPrivate *priv;
    GstRealPad *pad;

    if (link->bufpen) {
      priv = link->sink;
      pad = link->sinkpad;
    } else {
      priv = link->src;
      pad = link->srcpad;
    }
    if (priv->wait != WAIT_FOR_PADS)
      return FALSE;
    return can_schedule_pad (pad);
  } else if (ENTRY_IS_COTHREAD (entry)) {
    CothreadPrivate *priv = (CothreadPrivate *) entry;
    GList *list;

    if (priv->wait != WAIT_FOR_NOTHING)
      return FALSE;
    if (GST_STATE (priv->element) != GST_STATE_PLAYING)
      return FALSE;
    if (GST_FLAG_IS_SET (priv->element, GST_ELEMENT_DECOUPLED)) {
      g_assert (PAD_PRIVATE (priv->schedule_pad));
      return PAD_PRIVATE (priv->schedule_pad)->bufpen == NULL;
    }
    for (list = priv->element->pads; list; list = g_list_next (list)) {
      GstPad *pad = GST_PAD (list->data);

      if (GST_PAD_IS_SRC (pad) && PAD_PRIVATE (pad) &&
          PAD_PRIVATE (pad)->bufpen != NULL)
        return FALSE;
    }
    return TRUE;
  } else {
    g_assert_not_reached ();
    return FALSE;
  }
}

#define safe_cothread_switch(sched,cothread) G_STMT_START{ \
  if (do_cothread_get_current (sched->context) != cothread) \
    do_cothread_switch (cothread); \
}G_STMT_END

/* the meat - no guarantee as to which cothread this function is called */
static void
schedule (GstEntryScheduler * sched, Entry * entry)
{
  CothreadPrivate *schedule_me;

  g_assert (can_schedule (entry));
  sched->schedule_now = g_list_remove (sched->schedule_now, entry);
  sched->schedule_possible = g_list_remove (sched->schedule_possible, entry);
  sched->schedule_possible = g_list_append (sched->schedule_possible, entry);
  if (ENTRY_IS_LINK (entry)) {
    LinkPrivate *link = (LinkPrivate *) entry;

    if (link->bufpen) {
      schedule_me = link->sink;
      schedule_me->schedule_pad = link->sinkpad;
    } else {
      schedule_me = link->src;
      schedule_me->schedule_pad = link->srcpad;
    }
    GST_DEBUG_OBJECT (sched, "scheduling pad %s:%s",
        GST_DEBUG_PAD_NAME (schedule_me->schedule_pad));
  } else if (ENTRY_IS_COTHREAD (entry)) {
    schedule_me = (CothreadPrivate *) entry;
    GST_DEBUG_OBJECT (sched, "scheduling element %s",
        GST_OBJECT_NAME (schedule_me->element));
  } else {
    g_assert_not_reached ();
    GST_DEBUG_OBJECT (sched, "scheduling main after error");
    sched->error = TRUE;
    safe_cothread_switch (sched, do_cothread_get_main (sched->context));
    return;
  }

  if (!schedule_me->thread) {
    GST_LOG_OBJECT (sched, "creating cothread for %p (element %s)", schedule_me,
        GST_OBJECT_NAME (schedule_me->element));
    do_cothread_create (schedule_me->thread, sched->context, schedule_me->main,
        0, (gchar **) schedule_me);
  }

  safe_cothread_switch (sched, schedule_me->thread);
}

/* this function will die a horrible death if you have cyclic pipelines */
static Entry *
schedule_forward (Entry * entry)
{
  if (can_schedule (entry))
    return entry;
  if (ENTRY_IS_LINK (entry)) {
    return schedule_forward ((Entry *) ((LinkPrivate *) entry)->sink);
  } else if (ENTRY_IS_COTHREAD (entry)) {
    GList *list;
    Entry *entry;
    GstElement *element = ((CothreadPrivate *) entry)->element;

    if (GST_FLAG_IS_SET (element, GST_ELEMENT_DECOUPLED))
      return FALSE;
    for (list = element->pads; list; list = g_list_next (list)) {
      if (GST_PAD_IS_SINK (list->data))
        continue;
      entry = schedule_forward ((Entry *) PAD_PRIVATE (list->data));
      if (entry)
        return entry;
    }
  } else {
    g_assert_not_reached ();
  }
  return NULL;
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
      Entry *entry = schedule_forward ((Entry *) test->data);

      if (entry) {
        schedule (scheduler, entry);
        return;
      }
    }
    if (!scheduler->waiting) {
      GST_ERROR_OBJECT (scheduler,
          "have stuff that must be scheduled, but nothing that can be scheduled");
      scheduler->error = TRUE;
    }
  }
  GST_DEBUG_OBJECT (scheduler, "scheduling main");
  safe_cothread_switch (scheduler, do_cothread_get_main (scheduler->context));
}

/* 
 * handlers to attach to pads 
 */

static void
gst_entry_scheduler_chain_handler (GstPad * pad, GstData * data)
{
  LinkPrivate *priv = PAD_PRIVATE (pad);
  CothreadPrivate *thread = priv->src;
  GstEntryScheduler *sched = thread->sched;

  GST_LOG_OBJECT (sched, "putting data %p in pen of pad %s:%s",
      data, GST_DEBUG_PAD_NAME (pad));

  if (priv->bufpen != NULL) {
    GST_ERROR_OBJECT (sched, "scheduling error: trying to push data in bufpen"
        "of pad %s:%s, but bufpen was full", GST_DEBUG_PAD_NAME (pad));
    sched->error = TRUE;
    gst_data_unref (data);
  } else {
    priv->bufpen = data;
    sched->schedule_now = g_list_append (sched->schedule_now, priv);
  }

  thread->wait = WAIT_FOR_NOTHING;
  schedule_next_element (sched);

  GST_LOG_OBJECT (sched, "done");
}

static GstData *
gst_entry_scheduler_get_handler (GstPad * pad)
{
  GstData *data;
  GstEntryScheduler *sched = GST_ENTRY_SCHEDULER (gst_pad_get_scheduler (pad));
  GstPad *pads[2] = { NULL, NULL };
  GstPad *ret;

  pad = GST_PAD_PEER (pad);
  pads[0] = pad;
  GST_LOG_OBJECT (sched, "pad %s:%s pulls", GST_DEBUG_PAD_NAME (pad));

  data = gst_entry_scheduler_pad_select (GST_SCHEDULER (sched), &ret, pads);
  g_assert (pad == ret);

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

static GstData *
gst_entry_scheduler_pad_select (GstScheduler * scheduler, GstPad ** pulled_from,
    GstPad ** pads)
{
  GstData *data;
  GstRealPad *pad;
  GstElement *element = NULL;
  GstEntryScheduler *sched = GST_ENTRY_SCHEDULER (scheduler);
  gint i = 0;

  /* sanity check */
  while (pads[i]) {
    pad = GST_REAL_PAD (pads[i++]);
    if (PAD_PRIVATE (pad)->bufpen) {
      sched->schedule_now =
          g_list_remove (sched->schedule_now, PAD_PRIVATE (pad));
      goto found;
    }
  }
  element = gst_pad_get_parent (GST_PAD (pad));
  g_assert (element);
  g_assert (ELEMENT_PRIVATE (element)->main ==
      gst_entry_scheduler_loop_wrapper);
  LOOP_PRIVATE (element)->sinkpads = pads;
  ELEMENT_PRIVATE (element)->wait = WAIT_FOR_PADS;
  schedule_next_element (SCHED (element));
  LOOP_PRIVATE (element)->sinkpads = NULL;
  pad = ELEMENT_PRIVATE (element)->schedule_pad;
  g_assert (PAD_PRIVATE (pad)->bufpen);
found:
  data = get_buffer (sched, pad);
  g_return_val_if_fail (pulled_from, data);
  *pulled_from = GST_PAD (pad);
  return data;
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

static CothreadPrivate *
_setup_cothread (GstEntryScheduler * sched, GstElement * element,
    CothreadPrivate * (*setup_func) (GstEntryScheduler *, GstElement *))
{
  CothreadPrivate *priv = setup_func (sched, element);

  priv->entry.type = ENTRY_COTHREAD;
  priv->sched = sched;
  priv->element = element;
  sched->schedule_possible = g_list_prepend (sched->schedule_possible, priv);

  if (GST_STATE (element) >= GST_STATE_READY)
    gst_entry_scheduler_state_transition (GST_SCHEDULER (sched), element,
        GST_STATE_NULL_TO_READY);
  if (GST_STATE (element) >= GST_STATE_PAUSED)
    gst_entry_scheduler_state_transition (GST_SCHEDULER (sched), element,
        GST_STATE_READY_TO_PAUSED);
  if (GST_STATE (element) >= GST_STATE_PLAYING)
    gst_entry_scheduler_state_transition (GST_SCHEDULER (sched), element,
        GST_STATE_PAUSED_TO_PLAYING);

  return priv;
}

static void
gst_entry_scheduler_add_element (GstScheduler * scheduler, GstElement * element)
{
  GstEntryScheduler *sched = GST_ENTRY_SCHEDULER (scheduler);

  if (GST_FLAG_IS_SET (element, GST_ELEMENT_DECOUPLED)) {
    GST_INFO_OBJECT (sched, "decoupled element %s added, ignoring",
        GST_OBJECT_NAME (element));
    return;
  }

  g_assert (element->sched_private == NULL);
  if (element->loopfunc) {
    element->sched_private = _setup_cothread (sched, element, setup_loop);
  }
}

static void
_remove_cothread (CothreadPrivate * priv)
{
  GstEntryScheduler *sched = priv->sched;

  sched->waiting = g_list_remove (sched->waiting, priv);
  sched->schedule_now = g_list_remove (sched->schedule_now, priv);
  sched->schedule_possible = g_list_remove (sched->schedule_possible, priv);

  if (priv->thread)
    do_cothread_destroy (priv->thread);
  g_free (priv);
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

  if (element->sched_private) {
    _remove_cothread (element->sched_private);
    element->sched_private = NULL;
  }
}

static GstElementStateReturn
gst_entry_scheduler_state_transition (GstScheduler * scheduler,
    GstElement * element, gint transition)
{
  GList *list;
  GstEntryScheduler *sched = GST_ENTRY_SCHEDULER (scheduler);

  if (GST_FLAG_IS_SET (element, GST_ELEMENT_DECOUPLED))
    return GST_STATE_SUCCESS;

  /* check if our parent changed state */
  switch (transition) {
    case GST_STATE_NULL_TO_READY:
      break;
    case GST_STATE_READY_TO_PAUSED:
      for (list = element->pads; list; list = g_list_next (list)) {
        GstPad *pad = list->data;

        if (!GST_IS_REAL_PAD (pad))
          continue;
        if (PAD_PRIVATE (pad)) {
          PAD_PRIVATE (pad)->need_discont = TRUE;
        }
      }
      break;
    case GST_STATE_PAUSED_TO_PLAYING:
      break;
    case GST_STATE_PLAYING_TO_PAUSED:
      break;
    case GST_STATE_PAUSED_TO_READY:
      if (element == scheduler->parent) {
        GList *list;

        for (list = sched->schedule_possible; list; list = g_list_next (list)) {
          if (ENTRY_IS_COTHREAD (list->data)) {
            CothreadPrivate *priv = (CothreadPrivate *) list->data;

            if (priv->thread) {
              do_cothread_destroy (priv->thread);
              priv->thread = NULL;
            }
          }
        }
      }
      if (element->sched_private != NULL
          && ELEMENT_PRIVATE (element)->thread != NULL) {
        do_cothread_destroy (ELEMENT_PRIVATE (element)->thread);
        ELEMENT_PRIVATE (element)->thread = NULL;
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

static gboolean
gst_entry_scheduler_yield (GstScheduler * sched, GstElement * element)
{
  g_assert (ELEMENT_PRIVATE (element));
  ELEMENT_PRIVATE (element)->wait = WAIT_FOR_NOTHING;
  schedule_next_element (GST_ENTRY_SCHEDULER (sched));
  return FALSE;
}

static gboolean
gst_entry_scheduler_interrupt (GstScheduler * sched, GstElement * element)
{
  /* FIXME? */
  return gst_entry_scheduler_yield (sched, element);
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
  LinkPrivate *priv;
  GstElement *element;

  priv = g_new0 (LinkPrivate, 1);
  priv->entry.type = ENTRY_LINK;
  priv->need_discont = TRUE;
  /* wrap srcpad */
  element = gst_pad_get_parent (srcpad);
  priv->srcpad = GST_REAL_PAD (srcpad);
  if (GST_FLAG_IS_SET (element, GST_ELEMENT_DECOUPLED)) {
    priv->src = _setup_cothread (sched, element, setup_get);
  } else {
    priv->src = ELEMENT_PRIVATE (element);
    if (!priv->src) {
      GList *list;

      for (list = element->pads; list; list = g_list_next (list)) {
        if (GST_PAD_IS_SINK (list->data)) {
          priv->src = _setup_cothread (sched, element, setup_chain);
          break;
        }
      }
      if (!priv->src)
        priv->src = _setup_cothread (sched, element, setup_get);
      element->sched_private = priv->src;
    }
  }
  GST_RPAD_GETHANDLER (srcpad) = gst_entry_scheduler_get_handler;
  GST_RPAD_EVENTHANDLER (srcpad) = gst_entry_scheduler_event_handler;
  GST_REAL_PAD (srcpad)->sched_private = priv;
  /* wrap sinkpad */
  element = gst_pad_get_parent (sinkpad);
  priv->sinkpad = GST_REAL_PAD (sinkpad);
  if (GST_FLAG_IS_SET (element, GST_ELEMENT_DECOUPLED)) {
    priv->sink = _setup_cothread (sched, element, setup_chain);
  } else {
    priv->sink = ELEMENT_PRIVATE (element);
    if (priv->sink) {
      /* LOOP or CHAIN */
      g_assert (priv->sink->main != gst_entry_scheduler_get_wrapper);
    } else {
      priv->sink = _setup_cothread (sched, element, setup_chain);
      element->sched_private = priv->sink;
    }
  }
  GST_RPAD_CHAINHANDLER (sinkpad) = gst_entry_scheduler_chain_handler;
  GST_RPAD_EVENTHANDLER (sinkpad) = gst_entry_scheduler_event_handler;
  GST_REAL_PAD (sinkpad)->sched_private = priv;

  sched->schedule_possible = g_list_prepend (sched->schedule_possible, priv);
}

static void
gst_entry_scheduler_pad_unlink (GstScheduler * scheduler, GstPad * srcpad,
    GstPad * sinkpad)
{
  GstEntryScheduler *sched = GST_ENTRY_SCHEDULER (scheduler);
  LinkPrivate *priv;
  GstElement *element;

  priv = PAD_PRIVATE (srcpad);
  /* wrap srcpad */
  element = gst_pad_get_parent (srcpad);
  if (GST_FLAG_IS_SET (element, GST_ELEMENT_DECOUPLED))
    _remove_cothread (priv->src);
  GST_RPAD_GETHANDLER (srcpad) = NULL;
  GST_RPAD_EVENTHANDLER (srcpad) = NULL;
  GST_REAL_PAD (srcpad)->sched_private = NULL;
  /* wrap sinkpad */
  element = gst_pad_get_parent (sinkpad);
  if (GST_FLAG_IS_SET (element, GST_ELEMENT_DECOUPLED))
    _remove_cothread (priv->sink);
  GST_RPAD_CHAINHANDLER (sinkpad) = NULL;
  GST_RPAD_EVENTHANDLER (sinkpad) = NULL;
  GST_REAL_PAD (sinkpad)->sched_private = NULL;

  if (priv->bufpen) {
    GST_WARNING_OBJECT (sched,
        "found data in bufpen while unlinking %s:%s and %s:%s, discarding",
        GST_DEBUG_PAD_NAME (srcpad), GST_DEBUG_PAD_NAME (sinkpad));
    gst_data_unref (priv->bufpen);
  }
  sched->schedule_now = g_list_remove (sched->schedule_now, priv);
  sched->schedule_possible = g_list_remove (sched->schedule_possible, priv);
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
      if (can_schedule ((Entry *) entries->data)) {
        Entry *entry = entries->data;

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
  } else if (ret == GST_SCHEDULER_STATE_STOPPED) {
    GST_INFO_OBJECT (sched, "done iterating returning STOPPED");
    return GST_SCHEDULER_STATE_STOPPED;
  } else {
    return ret;
  }
}

static const gchar *
print_state (CothreadPrivate * priv)
{
  switch (priv->wait) {
    case WAIT_FOR_NOTHING:
      return "runnable";
    case WAIT_FOR_PADS:
      return "waiting for pads";
    case WAIT_FOR_ANYTHING:
    case WAIT_FOR_MUM:
    default:
      g_assert_not_reached ();
  }
  return "";
}

static void
print_entry (GstEntryScheduler * sched, Entry * entry)
{
  if (ENTRY_IS_LINK (entry)) {
    LinkPrivate *link = (LinkPrivate *) entry;

    g_print ("    %s", can_schedule (entry) ? "OK" : "  ");
    g_print (" %s:%s%s =>", GST_DEBUG_PAD_NAME (link->srcpad),
        can_schedule_pad (link->srcpad) ? " (active)" : "");
    g_print (" %s:%s%s", GST_DEBUG_PAD_NAME (link->sinkpad),
        can_schedule_pad (link->sinkpad) ? " (active)" : "");
    g_print ("%s\n", link->bufpen ? " FILLED" : "");
/*    g_print ("    %s %s:%s%s => %s:%s%s%s\n", can_schedule (entry) ? "OK" : "  ", GST_DEBUG_PAD_NAME (link->srcpad),
        link->src->can_schedule (link->srcpad) ? " (active)" : "",
        GST_DEBUG_PAD_NAME (link->sink),
        link->sink->can_schedule (link->sinkpad) ? "(active) " : "",
        link->bufpen ? " FILLED" : "");
*/ } else if (ENTRY_IS_COTHREAD (entry)) {
    CothreadPrivate *priv = (CothreadPrivate *) entry;

    g_print ("    %s %s (%s)\n", can_schedule (entry) ? "OK" : "  ",
        GST_ELEMENT_NAME (priv->element), print_state (priv));
  } else {
    g_assert_not_reached ();
  }
}

static void
gst_entry_scheduler_show (GstScheduler * scheduler)
{
  GstEntryScheduler *sched = GST_ENTRY_SCHEDULER (scheduler);
  GList *list;

  g_print ("entry points waiting:\n");
  for (list = sched->waiting; list; list = g_list_next (list)) {
    print_entry (sched, (Entry *) list->data);
  }
  g_print ("entry points to schedule now:\n");
  for (list = sched->schedule_now; list; list = g_list_next (list)) {
    print_entry (sched, (Entry *) list->data);
  }
  g_print ("entry points that might be scheduled:\n");
  for (list = sched->schedule_possible; list; list = g_list_next (list)) {
    print_entry (sched, (Entry *) list->data);
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
