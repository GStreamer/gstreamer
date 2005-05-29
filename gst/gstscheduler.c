/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wim.taymans@chello.be>
 *                    2004 Benjamin Otte <otte@gnomee.org>
 *
 * gstscheduler.c: Default scheduling code for most cases
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

#include "gst_private.h"

#include "gstsystemclock.h"
#include "gstscheduler.h"
#include "gstinfo.h"
#include "gstregistrypool.h"

/*
GST_DEBUG_CATEGORY_STATIC (sched_debug, "GST_SCHEDULER", 
    GST_DEBUG_BOLD | GST_DEBUG_FG_BLUE, "scheduler base class");
#define GST_CAT_DEFAULT sched_debug
*/

static void gst_scheduler_class_init (GstSchedulerClass * klass);
static void gst_scheduler_init (GstScheduler * sched);
static void gst_scheduler_dispose (GObject * object);

static void gst_scheduler_real_add_element (GstScheduler * scheduler,
    GstElement * element);
static void gst_scheduler_real_remove_element (GstScheduler * scheduler,
    GstElement * element);

static GstObjectClass *parent_class = NULL;

GType
gst_scheduler_get_type (void)
{
  static GType _gst_scheduler_type = 0;

  if (!_gst_scheduler_type) {
    static const GTypeInfo scheduler_info = {
      sizeof (GstSchedulerClass),
      NULL,
      NULL,
      (GClassInitFunc) gst_scheduler_class_init,
      NULL,
      NULL,
      sizeof (GstScheduler),
      0,
      (GInstanceInitFunc) gst_scheduler_init,
      NULL
    };

    _gst_scheduler_type =
        g_type_register_static (GST_TYPE_OBJECT, "GstScheduler",
        &scheduler_info, G_TYPE_FLAG_ABSTRACT);
  }
  return _gst_scheduler_type;
}

static void
gst_scheduler_class_init (GstSchedulerClass * klass)
{
  GObjectClass *gobject = G_OBJECT_CLASS (klass);

  parent_class = g_type_class_peek_parent (klass);

  gobject->dispose = gst_scheduler_dispose;

  klass->add_element = gst_scheduler_real_add_element;
  klass->remove_element = gst_scheduler_real_remove_element;
}

static void
gst_scheduler_init (GstScheduler * sched)
{
  sched->clock_providers = NULL;
  sched->clock_receivers = NULL;
  sched->clock = NULL;
}

static void
gst_scheduler_dispose (GObject * object)
{
  GstScheduler *sched = GST_SCHEDULER (object);

  /* thse lists should all be NULL */
  GST_DEBUG ("scheduler %p dispose %p %p",
      object, sched->clock_providers, sched->clock_receivers);

  gst_object_replace ((GstObject **) & sched->current_clock, NULL);
  gst_object_replace ((GstObject **) & sched->clock, NULL);

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
gst_scheduler_real_add_element (GstScheduler * scheduler, GstElement * element)
{
  GSList *walk;
  GstSchedulerClass *klass = GST_SCHEDULER_GET_CLASS (scheduler);

  g_assert (klass->add_element);
  for (walk = element->actions; walk; walk = g_slist_next (walk)) {
    klass->add_action (scheduler, walk->data);
  }
}

static void
gst_scheduler_real_remove_element (GstScheduler * scheduler,
    GstElement * element)
{
  GSList *walk;
  GstSchedulerClass *klass = GST_SCHEDULER_GET_CLASS (scheduler);

  g_assert (klass->remove_element);
  for (walk = element->actions; walk; walk = g_slist_next (walk)) {
    klass->remove_action (scheduler, walk->data);
  }
}

/**
 * gst_scheduler_marshal:
 * @sched: #GstScheduler to marshal to
 * @func: function to be called
 * @data: user data provided to the function
 *
 * This function is meant to be used from a different thread. Use this whenever
 * you need to marshal function calls into the thread this scheduler is running 
 * in. Note that there are no guarantees made as to when the provided function
 * will be exected, though schedulers will make a best effort to execute it as
 * soon as possible.
 **/
void
gst_scheduler_marshal (GstScheduler * sched, GstMarshalFunc func, gpointer data)
{
  GstSchedulerClass *klass;

  g_return_if_fail (GST_IS_SCHEDULER (sched));
  g_return_if_fail (func != NULL);
  klass = GST_SCHEDULER_GET_CLASS (sched);
  g_return_if_fail (klass->marshal != NULL);
  klass->marshal (sched, func, data);
}

/**
 * gst_scheduler_pad_link:
 * @sched: the scheduler
 * @srcpad: the srcpad to link
 * @sinkpad: the sinkpad to link to
 *
 * Links the srcpad to the given sinkpad.
 */
void
gst_scheduler_pad_link (GstScheduler * sched, GstPad * srcpad, GstPad * sinkpad)
{
  GstSchedulerClass *sclass;

  g_return_if_fail (GST_IS_SCHEDULER (sched));
  g_return_if_fail (GST_IS_PAD (srcpad));
  g_return_if_fail (GST_IS_PAD (sinkpad));

  sclass = GST_SCHEDULER_GET_CLASS (sched);

  if (sclass->pad_link)
    sclass->pad_link (sched, srcpad, sinkpad);
}

/**
 * gst_scheduler_pad_unlink:
 * @sched: the scheduler
 * @srcpad: the srcpad to unlink
 * @sinkpad: the sinkpad to unlink from
 *
 * Unlinks the srcpad from the given sinkpad.
 */
void
gst_scheduler_pad_unlink (GstScheduler * sched, GstPad * srcpad,
    GstPad * sinkpad)
{
  GstSchedulerClass *sclass;

  g_return_if_fail (GST_IS_SCHEDULER (sched));
  g_return_if_fail (GST_IS_PAD (srcpad));
  g_return_if_fail (GST_IS_PAD (sinkpad));

  sclass = GST_SCHEDULER_GET_CLASS (sched);

  if (sclass->pad_unlink)
    sclass->pad_unlink (sched, srcpad, sinkpad);
}

/**
 * gst_scheduler_pad_select:
 * @sched: the scheduler
 * @padlist: the padlist to select on
 *
 * register the given padlist for a select operation. 
 *
 * Returns: the pad which received a buffer.
 */
GstPad *
gst_scheduler_pad_select (GstScheduler * sched, GList * padlist)
{
  g_return_val_if_fail (GST_IS_SCHEDULER (sched), NULL);
  g_return_val_if_fail (padlist != NULL, NULL);

  return NULL;
}

/**
 * gst_scheduler_add_element:
 * @sched: the scheduler
 * @element: the element to add to the scheduler
 *
 * Add an element to the scheduler.
 */
void
gst_scheduler_add_element (GstScheduler * sched, GstElement * element)
{
  GstSchedulerClass *sclass;

  g_return_if_fail (GST_IS_SCHEDULER (sched));
  g_return_if_fail (GST_IS_ELEMENT (element));

  /* if it's already in this scheduler, don't bother doing anything */
  if (GST_ELEMENT_SCHED (element) == sched) {
    GST_CAT_DEBUG (GST_CAT_SCHEDULING, "element %s already in scheduler %p",
        GST_ELEMENT_NAME (element), sched);
    return;
  }

  /* if it's not inside this scheduler, it has to be NULL */
  g_assert (GST_ELEMENT_SCHED (element) == NULL);

  if (gst_element_provides_clock (element)) {
    sched->clock_providers = g_list_prepend (sched->clock_providers, element);
    GST_CAT_DEBUG (GST_CAT_CLOCK, "added clock provider %s",
        GST_ELEMENT_NAME (element));
  }
  if (gst_element_requires_clock (element)) {
    sched->clock_receivers = g_list_prepend (sched->clock_receivers, element);
    GST_CAT_DEBUG (GST_CAT_CLOCK, "added clock receiver %s",
        GST_ELEMENT_NAME (element));
  }

  gst_element_set_scheduler (element, sched);

  sclass = GST_SCHEDULER_GET_CLASS (sched);

  if (sclass->add_element)
    sclass->add_element (sched, element);
}

/**
 * gst_scheduler_remove_element:
 * @sched: the scheduler
 * @element: the element to remove
 *
 * Remove an element from the scheduler.
 */
void
gst_scheduler_remove_element (GstScheduler * sched, GstElement * element)
{
  GstSchedulerClass *sclass;

  g_return_if_fail (GST_IS_SCHEDULER (sched));
  g_return_if_fail (GST_IS_ELEMENT (element));

  sched->clock_providers = g_list_remove (sched->clock_providers, element);
  sched->clock_receivers = g_list_remove (sched->clock_receivers, element);

  sclass = GST_SCHEDULER_GET_CLASS (sched);

  if (sclass->remove_element)
    sclass->remove_element (sched, element);

  gst_element_set_scheduler (element, NULL);
}

/**
 * gst_scheduler_state_transition:
 * @sched: the scheduler
 * @element: the element with the state transition
 * @transition: the state transition
 *
 * Tell the scheduler that an element changed its state.
 *
 * Returns: a GstElementStateReturn indicating success or failure
 * of the state transition.
 */
GstElementStateReturn
gst_scheduler_state_transition (GstScheduler * sched, GstElement * element,
    gint transition)
{
  GstSchedulerClass *sclass;

  g_return_val_if_fail (GST_IS_SCHEDULER (sched), GST_STATE_FAILURE);
  g_return_val_if_fail (GST_IS_ELEMENT (element), GST_STATE_FAILURE);

  if (GST_OBJECT (element) == gst_object_get_parent (GST_OBJECT (sched))) {
    switch (transition) {
      case GST_STATE_READY_TO_PAUSED:
      {
        GstClock *clock = gst_scheduler_get_clock (sched);

        GST_CAT_DEBUG (GST_CAT_CLOCK,
            "scheduler READY to PAUSED clock is %p (%s)", clock,
            (clock ? GST_OBJECT_NAME (clock) : "nil"));

        gst_scheduler_set_clock (sched, clock);
        break;
      }
    }
  }

  sclass = GST_SCHEDULER_GET_CLASS (sched);

  if (sclass->state_transition)
    return sclass->state_transition (sched, element, transition);

  return GST_STATE_SUCCESS;
}

/**
 * gst_scheduler_error:
 * @sched: the scheduler
 * @element: the element with the error
 *
 * Tell the scheduler an element was in error
 */
void
gst_scheduler_error (GstScheduler * sched, GstElement * element)
{
  GstSchedulerClass *sclass;

  g_return_if_fail (GST_IS_SCHEDULER (sched));
  g_return_if_fail (GST_IS_ELEMENT (element));

  sclass = GST_SCHEDULER_GET_CLASS (sched);

  if (sclass->error)
    sclass->error (sched, element);
}

/**
 * gst_scheduler_get_clock:
 * @sched: the scheduler
 *
 * Gets the current clock used by the scheduler.
 *
 * Returns: a GstClock
 */
GstClock *
gst_scheduler_get_clock (GstScheduler * sched)
{
  GstClock *clock = NULL;

  /* if we have a fixed clock, use that one */
  if (GST_FLAG_IS_SET (sched, GST_SCHEDULER_FLAG_FIXED_CLOCK)) {
    clock = sched->clock;

    GST_CAT_DEBUG (GST_CAT_CLOCK, "scheduler using fixed clock %p (%s)",
        clock, clock ? GST_STR_NULL (GST_OBJECT_NAME (clock)) : "-");
  } else {
    GList *providers = sched->clock_providers;

    /* still no clock, try to find one in the providers */
    while (!clock && providers) {
      clock = gst_element_get_clock (GST_ELEMENT (providers->data));
      if (clock)
        GST_CAT_DEBUG (GST_CAT_CLOCK, "scheduler found provider clock: %p (%s)",
            clock, clock ? GST_STR_NULL (GST_OBJECT_NAME (clock)) : "-");
      providers = g_list_next (providers);
    }
    /* still no clock, use a system clock */
    if (!clock) {
      clock = gst_system_clock_obtain ();
      /* we unref since this function is not supposed to increase refcount
       * of clock object returned; this is ok since the systemclock always
       * has a refcount of at least one in the current code. */
      gst_object_unref (GST_OBJECT (clock));
      GST_CAT_DEBUG (GST_CAT_CLOCK, "scheduler obtained system clock: %p (%s)",
          clock, clock ? GST_STR_NULL (GST_OBJECT_NAME (clock)) : "-");
    }
  }

  return clock;
}

/**
 * gst_scheduler_use_clock:
 * @sched: the scheduler
 * @clock: the clock to use
 *
 * Force the scheduler to use the given clock. The scheduler will
 * always use the given clock even if new clock providers are added
 * to this scheduler.
 */
void
gst_scheduler_use_clock (GstScheduler * sched, GstClock * clock)
{
  g_return_if_fail (sched != NULL);
  g_return_if_fail (GST_IS_SCHEDULER (sched));

  GST_FLAG_SET (sched, GST_SCHEDULER_FLAG_FIXED_CLOCK);

  gst_object_replace ((GstObject **) & sched->clock, (GstObject *) clock);

  GST_CAT_DEBUG (GST_CAT_CLOCK, "scheduler using fixed clock %p (%s)", clock,
      (clock ? GST_OBJECT_NAME (clock) : "nil"));
}

/**
 * gst_scheduler_set_clock:
 * @sched: the scheduler
 * @clock: the clock to set
 *
 * Set the clock for the scheduler. The clock will be distributed
 * to all the elements managed by the scheduler.
 */
void
gst_scheduler_set_clock (GstScheduler * sched, GstClock * clock)
{
  GList *receivers;

  g_return_if_fail (sched != NULL);
  g_return_if_fail (GST_IS_SCHEDULER (sched));

  receivers = sched->clock_receivers;
  gst_object_replace ((GstObject **) & sched->current_clock,
      (GstObject *) clock);

  while (receivers) {
    GstElement *element = GST_ELEMENT (receivers->data);

    GST_CAT_DEBUG (GST_CAT_CLOCK,
        "scheduler setting clock %p (%s) on element %s", clock,
        (clock ? GST_OBJECT_NAME (clock) : "nil"), GST_ELEMENT_NAME (element));

    gst_element_set_clock (element, clock);
    receivers = g_list_next (receivers);
  }
}

/**
 * gst_scheduler_auto_clock:
 * @sched: the scheduler
 *
 * Let the scheduler select a clock automatically.
 */
void
gst_scheduler_auto_clock (GstScheduler * sched)
{
  g_return_if_fail (sched != NULL);
  g_return_if_fail (GST_IS_SCHEDULER (sched));

  GST_FLAG_UNSET (sched, GST_SCHEDULER_FLAG_FIXED_CLOCK);

  gst_object_replace ((GstObject **) & sched->clock, NULL);

  GST_DEBUG_OBJECT (sched, "using automatic clock");
}

void
gst_scheduler_pad_push (GstScheduler * sched, GstRealPad * pad, GstData * data)
{
  GstSchedulerClass *klass;

  g_return_if_fail (GST_IS_SCHEDULER (sched));
  g_return_if_fail (GST_IS_REAL_PAD (pad));
  g_return_if_fail (GST_PAD_IS_SRC (pad));
  g_return_if_fail (data != NULL);

  klass = GST_SCHEDULER_GET_CLASS (sched);
  g_return_if_fail (klass->pad_push);
  klass->pad_push (sched, pad, data);
}

/**
 * gst_scheduler_show:
 * @sched: the scheduler
 *
 * Dump the state of the scheduler
 */
void
gst_scheduler_show (GstScheduler * sched)
{
  GstSchedulerClass *sclass;

  g_return_if_fail (GST_IS_SCHEDULER (sched));

  sclass = GST_SCHEDULER_GET_CLASS (sched);

  if (sclass->show)
    sclass->show (sched);
}
